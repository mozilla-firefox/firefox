/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/**
 * @import { ProgressAndStatusCallbackParams } from "./Utils.sys.mjs"
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  console() {
    return console.createInstance({
      maxLogLevelPref: "browser.ml.logLevel",
      logModule: "GeckoMLOPFS",
    });
  },
  Progress: "chrome://global/content/ml/Utils.sys.mjs",
  computeHash: "chrome://global/content/ml/Utils.sys.mjs",
});

/**
 * Remove every entry (file or directory) in the given directory apart from the one
 * being kept. No failures if the directory doesn't exist. The removals are executed
 * in parallel using Promise.all.
 *
 * @param {string | null} directoryPath
 * @param {string} keepFileName
 *   The name of the single entry to leave in place.
 *
 * @returns {Promise<void>}
 */
async function removeOtherOPFSEntries(directoryPath, keepFileName) {
  let dirHandle;
  try {
    dirHandle = await OPFS.getDirectoryHandle(directoryPath);
  } catch (error) {
    if (error.name === "NotFoundError") {
      // There is nothing to delete, exit early.
      return;
    }
    throw error;
  }

  // Queue up all the deletes.
  const deletes = /** @type {Array<Promise<void>>} */ ([]);
  for await (const [name, handle] of dirHandle.entries()) {
    if (name === keepFileName) {
      continue;
    }
    deletes.push(
      dirHandle.removeEntry(name, {
        recursive: handle.kind === "directory",
      })
    );
  }

  await Promise.all(deletes);
}

/**
 * Validates that the hash and size match the blob if they are provided. If not, the
 * file needs a fresh download.
 *
 * @param {Blob} blob
 * @param {string} [expectedHash]
 * @param {number} [expectedFileSize]
 * @returns {Promise<boolean>}
 */

async function doesModelRequireRedownload(
  blob,
  expectedHash,
  expectedFileSize
) {
  if (expectedFileSize != null && blob.size != expectedFileSize) {
    // The file size did not match.
    return true;
  }

  if (expectedHash != null) {
    return (await lazy.computeHash(blob, "sha256", "hex")) != expectedHash;
  }

  return false;
}

/**
 * @param {string | URL | Response} source
 * @returns {string}
 */
function sourceToString(source) {
  if (typeof source === "string") {
    return source;
  }
  if (URL.isInstance(source)) {
    return source.href;
  }
  return source.url;
}

/**
 * OPFS operations tied to the browser.
 */
export class OPFS {
  /**
   * Retrieves a handle to a file at the specified file path.
   *
   * @param {string} filePath
   * @param {FileSystemGetDirectoryOptions} [options]
   * @returns {Promise<FileSystemFileHandle>}
   */
  static async getFileHandle(filePath, options) {
    // Extract the directory path and filename from the filePath.
    const lastSlashIndex = filePath.lastIndexOf("/");
    const fileName = filePath.substring(lastSlashIndex + 1);
    const dirPath = filePath.substring(0, lastSlashIndex);

    const directoryHandle = await OPFS.getDirectoryHandle(dirPath, options);
    const fileHandle = await directoryHandle.getFileHandle(fileName, options);

    ChromeUtils.addProfilerMarker(
      "MLEngine:OPFS",
      undefined,
      `File handle: ${fileName}`
    );

    return fileHandle;
  }

  /**
   * Retrieves a handle to a directory at the specified path.
   *
   * @param {string|null} path
   * @param {FileSystemGetDirectoryOptions} [options]
   * @returns {Promise<FileSystemDirectoryHandle>}
   */
  static async getDirectoryHandle(path = null, options) {
    let directoryHandle = await globalThis.storage.getDirectory();

    if (!path) {
      return directoryHandle;
    }

    const components = path.split("/").filter(Boolean);

    for (const dirName of components) {
      directoryHandle = await directoryHandle.getDirectoryHandle(
        dirName,
        options
      );
    }

    return directoryHandle;
  }

  /**
   * Delete a file or directory.
   *
   * @param {string} path
   * @param {FileSystemRemoveOptions & { ignoreErrors?: boolean }} [options]
   * @returns {Promise<void>}
   */
  static async remove(path, options) {
    // Extract the root directory and basename from the path.
    const lastSlashIndex = path.lastIndexOf("/");
    const fileName = path.substring(lastSlashIndex + 1);
    const dirPath = path.substring(0, lastSlashIndex);

    const directoryHandle = await OPFS.getDirectoryHandle(dirPath);
    if (!directoryHandle && !options?.ignoreErrors) {
      throw new Error("Directory does not exist: " + dirPath);
    }
    if (directoryHandle) {
      try {
        await directoryHandle.removeEntry(fileName, options);
      } catch (e) {
        if (!options?.ignoreErrors) {
          throw e;
        }
      }
    }
  }

  /**
   * Remove a file's now-possibly-empty ancestor directories, since otherwise
   * they accumulate for every model and revision ever downloaded.
   *
   * @param {string} path - The removed file's original path.
   * @param {string} stopAt - Ancestor to leave alone, along with everything
   *   above it.
   * @returns {Promise<void>}
   */
  static async pruneEmptyDirectories(path, stopAt) {
    const floor = stopAt.split("/").filter(Boolean).length;
    let components = path.split("/").filter(Boolean).slice(0, -1);

    while (components.length > floor) {
      const dirPath = components.join("/");

      try {
        const dirHandle = await OPFS.getDirectoryHandle(dirPath);
        const { done } = await dirHandle.keys().next();
        if (!done) {
          return;
        }
        await OPFS.remove(dirPath, { ignoreErrors: true });
      } catch (error) {
        if (error.name !== "NotFoundError") {
          throw error;
        }
      }

      components = components.slice(0, -1);
    }
  }

  /**
   * @typedef {object} DownloadOptions
   *
   * @property {string | URL | Response} source - The source of the content. Either
   *  If a string or URL is given, it will be fetched. If a Response is provided, it will
   *  be used directly.
   * @property {string} savePath
   *
   * @property {?function(ProgressAndStatusCallbackParams):void} [progressCallback]
   * @property {boolean} [useCache]
   * @property {number} [expectedFileSize]
   *   Validate the cached file based on the expected file size.
   * @property {string} [expectedHash]
   *   Validate the cached file based on the expected hash.
   * @property {boolean} [deletePreviousVersions]
   *   If true, deletes other entries in the parent directory after successful download.
   * @property {boolean} [ignoreCachingErrors]
   * @property {AbortSignal} [abortSignal]
   */

  /**
   * Downloads content from a URL and saves it to the Origin Private File System (OPFS).
   * If `useCache` is true and a valid file already exists at the given path,
   * the existing file is returned and no download is performed.
   *
   * @param {DownloadOptions} options
   * @returns {Promise<File | Blob>}
   */
  static async download({
    source,
    savePath,
    progressCallback,
    useCache,
    expectedHash,
    expectedFileSize,
    deletePreviousVersions,
    ignoreCachingErrors = false,
    abortSignal,
  }) {
    /** @type {File | Blob | null} */
    let fileObject = null;
    let cacheWasUsed = false;

    if (useCache) {
      // Attempt to get the file from the cache.
      try {
        const cachedHandle = await OPFS.getFileHandle(savePath, {
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

    if (!fileObject) {
      // File does not exists, download it.
      let response = Response.isInstance(source)
        ? source
        : await lazy.Progress.fetchUrl(source.toString());

      // Check if the current file exists, indicating a replacement download.
      const isReplacementDownoad = await OPFS.getFileHandle(savePath, {
        create: false,
      })
        .then(() => true)
        .catch(() => false);

      try {
        const fileHandle = await OPFS.getFileHandle(savePath, { create: true });

        await lazy.Progress.readResponseToWriter({
          response,
          writableStream: await fileHandle.createWritable({
            keepExistingData: false,
          }),
          progressCallback: progressCallback ?? null,
          abortSignal: abortSignal ?? null,
        });

        fileObject = await fileHandle.getFile();
      } catch (err) {
        if (!isReplacementDownoad) {
          // getFileHandle created the entry before any bytes were written.
          await OPFS.remove(savePath, { ignoreErrors: true });
        }

        if (ignoreCachingErrors && DOMException.isInstance(err)) {
          lazy.console.warn(
            `Caching Error when saving url  ${sourceToString(source)}. Returning the file without caching.`
          );
          return response.blob();
        }

        throw err;
      }
    }

    if (
      await doesModelRequireRedownload(
        fileObject,
        expectedHash,
        expectedFileSize
      )
    ) {
      // Failures could be due to corrupted file, remote file changes, incorrect ground
      // truth hash/size.
      const message = `Hash/size check failed for url ${sourceToString(source)} saved at ${savePath}.`;

      if (cacheWasUsed) {
        lazy.console.warn(`${message} Purging the cache and re-downloading.`);
        return OPFS.download({
          source,
          savePath,
          progressCallback,
          useCache: false,
          expectedHash,
          expectedFileSize,
          abortSignal,
        });
      }
      await OPFS.remove(savePath, { ignoreErrors: true });
      throw new Error(message);
    }

    if (deletePreviousVersions) {
      // Extract revision directory and file name from savePath
      const lastSlashIndex = savePath.lastIndexOf("/");
      const revisionsDir = savePath.substring(0, lastSlashIndex) || "";
      const currentFileName = savePath.substring(lastSlashIndex + 1);

      await removeOtherOPFSEntries(revisionsDir, currentFileName);
    }

    return fileObject;
  }
}
