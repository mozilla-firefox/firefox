/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// @ts-nocheck - TODO - Remove this to type check this file.

/**
 * @typedef {import("./Utils.sys.mjs").ProgressAndStatusCallbackParams} ProgressAndStatusCallbackParams
 */
const lazy = {};
const IN_WORKER = typeof importScripts !== "undefined";
const ES_MODULES_OPTIONS = IN_WORKER ? { global: "current" } : {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: IN_WORKER ? "Error" : "browser.ml.logLevel",
    logModule: "GeckoMLOPFS",
  });
});

ChromeUtils.defineESModuleGetters(
  lazy,
  {
    Progress: "chrome://global/content/ml/Utils.sys.mjs",
    computeHash: "chrome://global/content/ml/Utils.sys.mjs",
  },
  ES_MODULES_OPTIONS
);

/**
 * Retrieves a handle to a directory at the specified path in the Origin Private File System (OPFS).
 *
 * @param {string|null} path - The path to the directory, using "/" as the directory separator.
 *                        Example: "subdir1/subdir2/subdir3"
 *                        If null, returns the root.
 * @param {object} options - Configuration object
 * @param {boolean} options.create - if `true` (default is false), create any missing subdirectories.
 * @returns {Promise<FileSystemDirectoryHandle>} - A promise that resolves to the directory handle
 *                                                 for the specified path.
 */
async function getDirectoryHandleFromOPFS(
  path = null,
  { create = false } = {}
) {
  let currentNavigator = globalThis.navigator;
  if (!currentNavigator) {
    currentNavigator = Services.wm.getMostRecentBrowserWindow().navigator;
  }
  let directoryHandle = await currentNavigator.storage.getDirectory();

  if (!path) {
    return directoryHandle;
  }

  // Split the `path` into directory components.
  const components = path.split("/").filter(Boolean);

  // Traverse or creates subdirectories based on the path components.
  for (const dirName of components) {
    directoryHandle = await directoryHandle.getDirectoryHandle(dirName, {
      create,
    });
  }

  return directoryHandle;
}

/**
 * Retrieves a handle to a file at the specified file path in the Origin Private File System (OPFS).
 *
 * @param {string} filePath - The path to the file, using "/" as the directory separator.
 *                            Example: "subdir1/subdir2/filename.txt"
 * @param {object} options - Configuration object
 * @param {boolean} options.create - if `true` (default is false), create any missing directories
 *                                   and the file itself.
 * @returns {Promise<FileSystemFileHandle>} - A promise that resolves to the file handle
 *                                            for the specified file.
 */
async function getFileHandleFromOPFS(filePath, { create = false } = {}) {
  // Extract the directory path and filename from the filePath.
  const lastSlashIndex = filePath.lastIndexOf("/");
  const fileName = filePath.substring(lastSlashIndex + 1);
  const dirPath = filePath.substring(0, lastSlashIndex);

  // Get or create the directory handle for the file's parent directory.
  const directoryHandle = await getDirectoryHandleFromOPFS(dirPath, { create });

  // Retrieve or create the file handle within the directory.
  const fileHandle = await directoryHandle.getFileHandle(fileName, { create });

  ChromeUtils.addProfilerMarker(
    "MLEngine:OPFS",
    null,
    `File handle: ${fileName}`
  );

  return fileHandle;
}

/**
 * Remove all entries (files or directories) in the given directory handle
 * for which the provided predicate returns true. No failures if the file doesn't exist.
 *
 * The removals are executed in parallel using Promise.all.
 *
 * @param {string | null} path  - The path to the directory to scan for entries.
 * @param {(name: string, handle: FileSystemHandle) => boolean} predicate - A function that returns true if the entry should be removed.
 * @returns {Promise<void>} A promise that resolves when all removals are complete.
 */
async function removeMatchingOPFSEntries(path = null, predicate) {
  const deletePromises = [];

  let dirHandle;
  try {
    dirHandle = await getDirectoryHandleFromOPFS(path, { create: false });
  } catch (err) {
    if (err.name === "NotFoundError") {
      return;
    }
    throw err;
  }

  for await (const [name, handle] of dirHandle.entries()) {
    if (predicate(name, handle)) {
      try {
        deletePromises.push(
          dirHandle.removeEntry(name, {
            recursive: handle.kind === "directory",
          })
        );
      } catch (err) {
        if (err.name !== "NotFoundError") {
          throw err;
        }
      }
    }
  }

  await Promise.all(deletePromises);
}

/**
 * Converts a file in OPFS and given headers to a Response object.
 *
 * @param {string} filePath - path to the file in Origin Private FileSystem (OPFS).
 * @param {object|null} headers
 * @returns {Response} The generated Response instance
 */
export async function createResponseFromOPFSFile(filePath, headers) {
  let responseHeaders = {};

  if (headers) {
    // Headers are converted to strings, as the cache may hold int keys like fileSize
    for (let key in headers) {
      if (headers[key] != null) {
        responseHeaders[key] = headers[key].toString();
      }
    }
  }

  const file = await (await getFileHandleFromOPFS(filePath)).getFile();

  return new Response(file.stream(), {
    status: 200,
    headers: responseHeaders,
  });
}

/**
 * Downloads content from a URL and saves it to the Origin Private File System (OPFS).
 *
 * @param {object} params - Parameters.
 * @param {string | URL | Response} params.source - The source of the content. If a string or URL is given, it will be fetched. If a Response is provided, it will be used directly.
 * @param {string} params.savePath - OPFS path to save the file (e.g., "folder/file.txt").
 * @param {?function(ProgressAndStatusCallbackParams):void} [params.progressCallback] - Optional progress callback.
 * @param {boolean} [params.ignoreCachingErrors=false] - If true, all errors due to retrieving/saving from OPFS are ignored.
 * @param {?AbortSignal} params.abortSignal - AbortSignal to cancel the download.
 * @returns {Promise<File>} The saved or existing file.
 */
async function downloadToOPFSImpl({
  source,
  savePath,
  progressCallback,
  abortSignal,
  ignoreCachingErrors = false,
} = {}) {
  // Download and write file
  let response = source;

  if (!Response.isInstance(source)) {
    response = await lazy.Progress.fetchUrl(source.toString()); // Assumes fetchUrl throws if !ok
  }

  let fileObject;

  try {
    // Delay file creation until response is ok
    const fileHandle = await getFileHandleFromOPFS(savePath, { create: true });

    const writableStream = await fileHandle.createWritable({
      keepExistingData: false,
      mode: "siloed",
    });

    await lazy.Progress.readResponseToWriter({
      response,
      writableStream,
      progressCallback,
      abortSignal,
    });

    fileObject = await fileHandle.getFile();
  } catch (err) {
    if (ignoreCachingErrors && DOMException.isInstance(err)) {
      lazy.console.warn(
        `Caching Error when saving url  ${source.url ?? source}. Returning the file without caching.`
      );
      return response.blob();
    }

    throw err;
  }

  return fileObject;
}

/**
 * Verifies that a Blob matches an expected SHA-256 hash and/or size, if provided.
 *
 * @param {Blob} blob - The Blob to validate.
 * @param {string|null} expectedHash - Optional expected SHA-256 hash in hexadecimal format.
 * @param {number|null} expectedSize - Optional expected size of the Blob in bytes.
 * @returns {Promise<boolean>} True if all provided expectations match; false if any do not.
 */

async function maybeVerifyBlob(blob, expectedHash, expectedSize) {
  if (expectedSize != null && blob.size != expectedSize) {
    return false;
  }

  if (expectedHash != null) {
    return (await lazy.computeHash(blob, "sha256", "hex")) == expectedHash;
  }

  return true;
}

/**
 * Downloads content from a URL and saves it to the Origin Private File System (OPFS).
 *
 * If `skipIfExists` is true and a valid file already exists at the given path,
 * the existing file is returned and no download is performed.
 *
 * @param {object} params - Parameters.
 * @param {string | URL | Response} [params.source] - The source of the content. If a string or URL is given, it will be fetched. If a Response is provided, it will be used directly.
 * @param {string} params.savePath - OPFS path to save the file (e.g., "folder/file.txt").
 * @param {?function(ProgressAndStatusCallbackParams):void} [params.progressCallback] - Optional progress callback.
 * @param {boolean} [params.skipIfExists=false] - Whether to skip download if the file exists and passes hash check.
 * @param {number} params.fileSize - Expected file size.
 * @param {string} params.sha256Hash - Expected SHA-256 hash (hex).
 * @param {boolean} [params.deletePreviousVersions=false] - If true, deletes other entries in the parent directory after successful download.
 * @param {boolean} [params.ignoreCachingErrors=false] - If true, all errors due to retrieving/saving from OPFS are ignored.
 * @param {AbortSignal} [params.abortSignal] - AbortSignal to cancel the download.
 * @returns {Promise<File>} The saved or existing file.
 */
async function downloadToOPFS({
  source,
  savePath,
  progressCallback,
  skipIfExists = false,
  sha256Hash,
  fileSize,
  deletePreviousVersions = false,
  ignoreCachingErrors = false,
  abortSignal,
} = {}) {
  let fileObject;
  let cacheWasUsed = false;

  if (skipIfExists) {
    try {
      const cachedHandle = await getFileHandleFromOPFS(savePath, {
        create: false,
      });
      fileObject = await cachedHandle.getFile();
      cacheWasUsed = true;
    } catch (err) {
      if (err.name !== "NotFoundError" && !ignoreCachingErrors) {
        throw err;
      }
    }
  }

  // File does not exists — downloading
  if (!fileObject) {
    fileObject = await downloadToOPFSImpl({
      source,
      savePath,
      progressCallback,
      ignoreCachingErrors,
      abortSignal,
    });
  }

  // Validate hash and size
  if (!(await maybeVerifyBlob(fileObject, sha256Hash, fileSize))) {
    // Failures could be due to corrupted file, remote file changes, incorrect ground truth hash/size.
    const message = `Hash check failed for url ${source.url ?? source} saved at ${savePath}.`;

    if (cacheWasUsed) {
      lazy.console.warn(`${message} Purging the cache and re-downloading.`);
      return downloadToOPFS({
        source,
        savePath,
        progressCallback,
        skipIfExists: false, // Retrigger forced download.
        sha256Hash,
        fileSize,
        abortSignal,
      });
    }
    throw new Error(message);
  }

  // Optionally delete other versions
  if (deletePreviousVersions) {
    // Extract revision directory and file name from savePath
    const lastSlashIndex = savePath.lastIndexOf("/");
    const revisionsDir = savePath.substring(0, lastSlashIndex) || "";
    const currentFileName = savePath.substring(lastSlashIndex + 1);

    await removeMatchingOPFSEntries(revisionsDir, name => {
      return name !== currentFileName;
    });
  }

  return fileObject;
}

/**
 * Delete a file or directory from the Origin Private File System (OPFS).
 *
 * @param {string} path - The path to delete, using "/" as the directory separator.
 * @param {object} options - Configuration object
 * @param {boolean} options.recursive - if `true` (default is false) a directory path
 * @param {boolean} options.ignoreErrors - if `true` (default is true) errors are ignored
 *                                      is recursively deleted.
 * @returns {Promise<void>} A promise that resolves when the path has been successfully deleted.
 */
async function removeFromOPFS(
  path,
  { recursive = false, ignoreErrors = true } = {}
) {
  // Extract the root directory and basename from the path.
  const lastSlashIndex = path.lastIndexOf("/");
  const fileName = path.substring(lastSlashIndex + 1);
  const dirPath = path.substring(0, lastSlashIndex);

  const directoryHandle = await getDirectoryHandleFromOPFS(dirPath);
  if (!directoryHandle && !ignoreErrors) {
    throw new Error("Directory does not exist: " + dirPath);
  }
  if (directoryHandle) {
    try {
      await directoryHandle.removeEntry(fileName, { recursive });
    } catch (e) {
      if (!ignoreErrors) {
        throw e;
      }
    }
  }
}

/**
 * Represents a file that can be fetched and cached in OPFS (Origin Private File System).
 */
class OPFSFile {
  /**
   * Creates an instance of OPFSFile.
   *
   * @param {object} options - The options for creating an OPFSFile instance.
   * @param {string[]} [options.urls=null] - An array of URLs from which the file may be fetched.
   * @param {string} options.localPath - A path (in OPFS) where the file should be stored or retrieved from.
   */
  constructor({ urls = null, localPath }) {
    /**
     * @type {string[]|null}
     * An array of possible remote URLs that can provide this file.
     */
    this.urls = urls;

    /**
     * @type {string}
     * A string path within OPFS where this file is or will be stored.
     */
    this.localPath = localPath;
  }

  /**
   * Attempts to read the file from OPFS.
   *
   * @returns {Promise<Blob|null>} A promise that resolves to the file as a Blob if found in OPFS, otherwise null.
   */
  async getBlobFromOPFS() {
    let fileHandle;
    try {
      fileHandle = await getFileHandleFromOPFS(this.localPath, {
        create: false,
      });
      if (fileHandle) {
        const file = await fileHandle.getFile();
        return new Response(file.stream()).blob();
      }
    } catch (e) {
      // If getFileHandle() throws, it likely doesn't exist in OPFS
    }
    return null;
  }

  /**
   * Fetches the file as a Blob from a given URL.
   *
   * @param {string} url - The URL to fetch the file from.
   * @returns {Promise<Blob|null>} A promise that resolves to the file as a Blob if the fetch was successful, otherwise null.
   */
  async getBlobFromURL(url) {
    lazy.console.debug(`Fetching ${url}...`);
    const response = await fetch(url);
    if (!response.ok) {
      return null;
    }
    return response.blob();
  }

  /**
   * Deletes the file from OPFS, if it exists.
   *
   * @returns {Promise<void>} Resolves once the file is removed (or if it does not exist).
   */
  async delete() {
    const fileHandle = await getFileHandleFromOPFS(this.localPath);
    if (fileHandle) {
      await removeFromOPFS(this.localPath);
    }
  }

  /**
   * Retrieves the file (either from OPFS or via the provided URLs), caches it in OPFS, and returns its object URL.
   *
   * @throws {Error} If the file cannot be fetched from OPFS or any of the provided URLs.
   * @returns {Promise<string>} A promise that resolves to the file's object URL.
   */
  async getAsObjectURL() {
    let fileObject;
    let response;

    if (!this.urls) {
      throw new Error("File not present in OPFS and no urls provided");
    }

    for (const url of this.urls) {
      try {
        fileObject = await downloadToOPFS({
          source: url,
          savePath: this.localPath, // Cache the newly fetched file in OPFS
          deletePreviousVersions: false,
          skipIfExists: true, // Try from OPFS first
          ignoreCachingErrors: true,
        });

        break;
      } catch (error) {
        // Ignored
      }
    }

    if (!fileObject && response?.ok) {
      // Even if caching fails, we still return the fetched blob's URL
      fileObject = await response.blob();
    }

    if (!fileObject) {
      throw new Error("Could not fetch the resource from the provided urls");
    }

    // Return a Blob URL for the fetched (and potentially cached) file
    return URL.createObjectURL(fileObject);
  }
}

// OPFS operations
export var OPFS = OPFS || {};
OPFS.getFileHandle = getFileHandleFromOPFS;
OPFS.getDirectoryHandle = getDirectoryHandleFromOPFS;
OPFS.remove = removeFromOPFS;
OPFS.download = downloadToOPFS;
OPFS.toResponse = createResponseFromOPFSFile;
OPFS.File = OPFSFile;
