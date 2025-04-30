/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
const lazy = {};
const IN_WORKER = typeof importScripts !== "undefined";

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: IN_WORKER ? "Error" : "browser.ml.logLevel",
    prefix: "ML:OPFS",
  });
});

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

  return fileHandle;
}

/**
 * Delete a file or directory from the Origin Private File System (OPFS).
 *
 * @param {string} path - The path to delete, using "/" as the directory separator.
 * @param {object} options - Configuration object
 * @param {boolean} options.recursive - if `true` (default is false) a directory path
 *                                      is recursively deleted.
 * @returns {Promise<void>} A promise that resolves when the path has been successfully deleted.
 */
async function removeFromOPFS(path, { recursive = false } = {}) {
  // Extract the root directory and basename from the path.
  const lastSlashIndex = path.lastIndexOf("/");
  const fileName = path.substring(lastSlashIndex + 1);
  const dirPath = path.substring(0, lastSlashIndex);

  const directoryHandle = await getDirectoryHandleFromOPFS(dirPath);
  if (!directoryHandle) {
    throw new Error("Directory does not exist: " + dirPath);
  }
  await directoryHandle.removeEntry(fileName, { recursive });
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
    // Try from OPFS first
    let blob = await this.getBlobFromOPFS();

    // If not in OPFS, try the provided URLs
    if (!blob) {
      if (!this.urls) {
        throw new Error("File not present in OPFS and no urls provided");
      }

      for (const url of this.urls) {
        blob = await this.getBlobFromURL(url);
        if (blob) {
          break;
        }
      }
    }

    if (!blob) {
      throw new Error("Could not fetch the resource from the provided urls");
    }

    // Cache the newly fetched file in OPFS
    try {
      const newFileHandle = await getFileHandleFromOPFS(this.localPath, {
        create: true,
      });
      const writable = await newFileHandle.createWritable();
      await writable.write(blob);
      await writable.close();
    } catch (writeErr) {
      lazy.console.warning(`Failed to write file to OPFS cache: ${writeErr}`);
      // Even if caching fails, we still return the fetched blob's URL
    }

    // Return a Blob URL for the fetched (and potentially cached) file
    return URL.createObjectURL(blob);
  }
}

// OPFS operations
export var OPFS = OPFS || {};
OPFS.getFileHandle = getFileHandleFromOPFS;
OPFS.getDirectoryHandle = getDirectoryHandleFromOPFS;
OPFS.remove = removeFromOPFS;
OPFS.File = OPFSFile;
