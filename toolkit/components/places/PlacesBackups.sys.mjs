/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  BookmarkJSONUtils: "resource://gre/modules/BookmarkJSONUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(
  lazy,
  "filenamesRegex",
  () =>
    /^bookmarks-([0-9-]+)(?:_([0-9]+)){0,1}(?:_([a-z0-9=_+-]{24,})){0,1}\.(json(lz4)?)$/i
);

/**
 * Limits the number of backup files by deleting old backups.
 *
 * @param {number} aMaxBackups
 * @param {string[]} backupFiles
 *
 * @returns {Promise<void>}
 *   Resolves when the oldest files have been deleted.
 */
async function limitBackups(aMaxBackups, backupFiles) {
  if (
    typeof aMaxBackups == "number" &&
    aMaxBackups > -1 &&
    backupFiles.length >= aMaxBackups
  ) {
    let numberOfBackupsToDelete = backupFiles.length - aMaxBackups;
    while (numberOfBackupsToDelete--) {
      let oldestBackup = backupFiles.pop();
      await IOUtils.remove(oldestBackup);
    }
  }
}

/**
 * Appends meta-data information to a given filename.
 *
 * @param {string} aFilename
 *   The file name.
 * @param {{count: string|number, hash: string}} aMetaData
 *   Metadata related to the file.
 *
 * @returns {string}
 *   The filename with appended metadata information.
 */
function appendMetaDataToFilename(aFilename, aMetaData) {
  let matches = aFilename.match(lazy.filenamesRegex);
  return (
    "bookmarks-" +
    matches[1] +
    "_" +
    aMetaData.count +
    "_" +
    aMetaData.hash +
    "." +
    matches[4]
  );
}

/**
 * Gets the hash from a backup filename.
 *
 * @param {string} aFilename
 *   The backup file name.
 *
 * @returns {null|string}
 *   The extracted hash or null.
 */
function getHashFromFilename(aFilename) {
  let matches = aFilename.match(lazy.filenamesRegex);
  if (matches && matches[3]) {
    return matches[3];
  }
  return null;
}

/**
 * Given two filenames, checks if they contain the same date.
 *
 * @param {string} aSourceName
 * @param {string} aTargetName
 *
 * @returns {boolean}
 */
function isFilenameWithSameDate(aSourceName, aTargetName) {
  let sourceMatches = aSourceName.match(lazy.filenamesRegex);
  let targetMatches = aTargetName.match(lazy.filenamesRegex);

  return sourceMatches && targetMatches && sourceMatches[1] == targetMatches[1];
}

/**
 * Given a filename, searches for another backup with the same date.
 *
 * @param {string} aFilename
 *   The name of the backup file.
 *
 * @returns {Promise<string|null>}
 *   Resolves to a path string or null.
 */
function getBackupFileForSameDate(aFilename) {
  return (async function () {
    let backupFiles = await PlacesBackups.getBackupFiles();
    for (let backupFile of backupFiles) {
      if (isFilenameWithSameDate(PathUtils.filename(backupFile), aFilename)) {
        return backupFile;
      }
    }
    return null;
  })();
}

export var PlacesBackups = {
  /**
   * Matches the backup filename:
   *  0: file name
   *  1: date in form Y-m-d
   *  2: bookmarks count
   *  3: contents hash
   *  4: file extension
   */
  get filenamesRegex() {
    return lazy.filenamesRegex;
  },

  /**
   * Gets backup folder asynchronously.
   *
   * @returns {Promise<string>}
   *   A promise that resolves to the folder string path.
   */
  getBackupFolder: function PB_getBackupFolder() {
    return (async () => {
      if (this._backupFolder) {
        return this._backupFolder;
      }
      let backupsDirPath = PathUtils.join(
        PathUtils.profileDir,
        this.profileRelativeFolderPath
      );
      await IOUtils.makeDirectory(backupsDirPath);
      return (this._backupFolder = backupsDirPath);
    })();
  },

  /**
   * @returns {string}
   */
  get profileRelativeFolderPath() {
    return "bookmarkbackups";
  },

  /**
   * Cache current backups in a sorted (by date DESC) array.
   *
   * @returns {Promise<string[]>}
   *   A sorted array of string paths.
   */
  getBackupFiles: function PB_getBackupFiles() {
    return (async () => {
      if (this._backupFiles) {
        return this._backupFiles;
      }

      this._backupFiles = [];

      let backupFolderPath = await this.getBackupFolder();
      let children = await IOUtils.getChildren(backupFolderPath);
      let list = [];
      for (const entry of children) {
        // Since IOUtils I/O is serialized, we can safely remove .tmp files
        // without risking to remove ongoing backups.
        let filename = PathUtils.filename(entry);
        if (filename.endsWith(".tmp")) {
          list.push(IOUtils.remove(entry));
          continue;
        }

        if (lazy.filenamesRegex.test(filename)) {
          // Remove bogus backups in future dates.
          if (this.getDateForFile(entry) > new Date()) {
            list.push(IOUtils.remove(entry));
            continue;
          }
          this._backupFiles.push(entry);
        }
      }
      await Promise.all(list);

      this._backupFiles.sort((a, b) => {
        let aDate = this.getDateForFile(a);
        let bDate = this.getDateForFile(b);
        return bDate.getTime() - aDate.getTime();
      });

      return this._backupFiles;
    })();
  },

  /**
   * Invalidates the internal cache for testing purposes.
   */
  invalidateCache() {
    this._backupFiles = null;
  },

  /**
   * Generates a ISO date string (YYYY-MM-DD) from a Date object.
   *
   * @param {Date} dateObj
   *   The date object to parse.
   *
   * @returns {string}
   *   An ISO date string.
   */
  toISODateString: function toISODateString(dateObj) {
    if (!dateObj || dateObj.constructor.name != "Date" || !dateObj.getTime()) {
      throw new Error("invalid date object");
    }
    let padDate = val => ("0" + val).substr(-2, 2);
    return [
      dateObj.getFullYear(),
      padDate(dateObj.getMonth() + 1),
      padDate(dateObj.getDate()),
    ].join("-");
  },

  /**
   * Creates a filename for bookmarks backup files.
   *
   * @param {Date} [aDateObj]
   *   Date object used to build the filename. Will use current date if empty.
   * @param {boolean} [aCompress]
   *   Determines if the file extension is json or jsonlz4. Default is json.
   *
   * @returns {string}
   *   A bookmarks backup filename.
   */
  getFilenameForDate: function PB_getFilenameForDate(aDateObj, aCompress) {
    let dateObj = aDateObj || new Date();
    // Use YYYY-MM-DD (ISO 8601) as it doesn't contain illegal characters
    // and makes the alphabetical order of multiple backup files more useful.
    return (
      "bookmarks-" +
      PlacesBackups.toISODateString(dateObj) +
      ".json" +
      (aCompress ? "lz4" : "")
    );
  },

  /**
   * Creates a Date object from a backup file.  The date is the backup
   * creation date.
   *
   * @param {string} aBackupFile
   *   The path of the backup.
   *
   * @returns {Date}
   *   A Date object for the backup's creation time.
   */
  getDateForFile: function PB_getDateForFile(aBackupFile) {
    let filename = PathUtils.filename(aBackupFile);
    let matches = filename.match(lazy.filenamesRegex);
    if (!matches) {
      throw new Error(`Invalid backup file name: ${filename}`);
    }
    return new Date(matches[1].replace(/-/g, "/"));
  },

  /**
   * Get the most recent backup file.
   *
   * @returns {Promise<string|null>}
   *   The path to the file or null if no valid backup was found.
   */
  getMostRecentBackup: function PB_getMostRecentBackup() {
    return (async () => {
      let entries = await this.getBackupFiles();
      for (let entry of entries) {
        let rx = /\.json(lz4)?$/;
        if (PathUtils.filename(entry).match(rx)) {
          return entry;
        }
      }
      return null;
    })();
  },

  /**
   * Returns whether a recent enough backup exists, using these heuristic: if
   * a backup exists, it should be newer than the last browser session date,
   * otherwise it should not be older than maxDays.
   * If the backup is older than the last session, the calculated time is
   * reported to telemetry.
   *
   * @param {object} [options={}]
   * @param {number} [options.maxDays]
   *   The maximum number of days a backup can be old.
   *
   * @returns {Promise<boolean>}
   *   Resolves to true if a recent backup is available.
   */
  async hasRecentBackup({ maxDays = 3 } = {}) {
    let lastBackupFile = await PlacesBackups.getMostRecentBackup();
    if (!lastBackupFile) {
      return false;
    }
    let lastBackupTime = PlacesBackups.getDateForFile(lastBackupFile).getTime();
    let profileLastUse = Services.appinfo.replacedLockTime || Date.now();
    if (lastBackupTime > profileLastUse) {
      return true;
    }
    let backupAge = Math.round((profileLastUse - lastBackupTime) / 86400000);
    // Telemetry the age of the last available backup.
    Glean.places.backupsDaysfromlast.accumulateSingleSample(backupAge);
    return backupAge <= maxDays;
  },

  /**
   * Serializes bookmarks using JSON, and writes to the supplied file.
   *
   * @param {string} aFilePath
   *   Path for the "bookmarks.json" file to be created.
   *
   * @returns {Promise<number>}
   *   The number of serialized uri nodes.
   */
  async saveBookmarksToJSONFile(aFilePath) {
    let { count: nodeCount, hash: hash } =
      await lazy.BookmarkJSONUtils.exportToFile(aFilePath);

    let backupFolderPath = await this.getBackupFolder();
    if (PathUtils.profileDir == backupFolderPath) {
      // We are creating a backup in the default backups folder,
      // so just update the internal cache.
      if (!this._backupFiles) {
        await this.getBackupFiles();
      }
      this._backupFiles.unshift(aFilePath);
    } else {
      let aMaxBackup = Services.prefs.getIntPref(
        "browser.bookmarks.max_backups"
      );
      if (aMaxBackup === 0) {
        if (!this._backupFiles) {
          await this.getBackupFiles();
        }
        limitBackups(aMaxBackup, this._backupFiles);
        return nodeCount;
      }
      // If we are saving to a folder different than our backups folder, then
      // we also want to create a new compressed version in it.
      // This way we ensure the latest valid backup is the same saved by the
      // user.  See bug 424389.
      let mostRecentBackupFile = await this.getMostRecentBackup();
      if (
        !mostRecentBackupFile ||
        hash != getHashFromFilename(PathUtils.filename(mostRecentBackupFile))
      ) {
        let name = this.getFilenameForDate(undefined, true);
        let newFilename = appendMetaDataToFilename(name, {
          count: nodeCount,
          hash,
        });
        let newFilePath = PathUtils.join(backupFolderPath, newFilename);
        let backupFile = await getBackupFileForSameDate(name);
        if (backupFile) {
          // There is already a backup for today, replace it.
          await IOUtils.remove(backupFile);
          if (!this._backupFiles) {
            await this.getBackupFiles();
          } else {
            this._backupFiles.shift();
          }
          this._backupFiles.unshift(newFilePath);
        } else {
          // There is no backup for today, add the new one.
          if (!this._backupFiles) {
            await this.getBackupFiles();
          }
          this._backupFiles.unshift(newFilePath);
        }
        let jsonString = await IOUtils.read(aFilePath);
        await IOUtils.write(newFilePath, jsonString, {
          compress: true,
        });
        await limitBackups(aMaxBackup, this._backupFiles);
      }
    }
    return nodeCount;
  },

  /**
   * Creates a dated backup in <profile>/bookmarkbackups.
   * Stores the bookmarks using a lz4 compressed JSON file.
   *
   * @param {number} [aMaxBackups]
   *   The maximum number of backups to keep.  If set to 0 all existing backups
   *   are removed and aForceBackup is ignored, so a new one won't be created.
   * @param {boolean} [aForceBackup]
   *   Forces creating a backup even if one was already created that day
   *   (overwrites).
   *
   * @returns {Promise<void>}
   */
  create: function PB_create(aMaxBackups, aForceBackup) {
    return (async () => {
      if (aMaxBackups === 0) {
        // Backups are disabled, delete any existing one and bail out.
        if (!this._backupFiles) {
          await this.getBackupFiles();
        }
        await limitBackups(0, this._backupFiles);
        return;
      }

      // Ensure to initialize _backupFiles
      if (!this._backupFiles) {
        await this.getBackupFiles();
      }
      let newBackupFilename = this.getFilenameForDate(undefined, true);
      // If we already have a backup for today we should do nothing, unless we
      // were required to enforce a new backup.
      let backupFile = await getBackupFileForSameDate(newBackupFilename);
      if (backupFile && !aForceBackup) {
        return;
      }

      if (backupFile) {
        // In case there is a backup for today we should recreate it.
        this._backupFiles.shift();
        await IOUtils.remove(backupFile);
      }

      // Now check the hash of the most recent backup, and try to create a new
      // backup, if that fails due to hash conflict, just rename the old backup.
      let mostRecentBackupFile = await this.getMostRecentBackup();
      let mostRecentHash =
        mostRecentBackupFile &&
        getHashFromFilename(PathUtils.filename(mostRecentBackupFile));

      // Save bookmarks to a backup file.
      let backupFolder = await this.getBackupFolder();
      let newBackupFile = PathUtils.join(backupFolder, newBackupFilename);
      let newFilenameWithMetaData;
      try {
        let { count: nodeCount, hash: hash } =
          await lazy.BookmarkJSONUtils.exportToFile(newBackupFile, {
            compress: true,
            failIfHashIs: mostRecentHash,
          });
        newFilenameWithMetaData = appendMetaDataToFilename(newBackupFilename, {
          count: nodeCount,
          hash,
        });
      } catch (ex) {
        if (!ex.becauseSameHash) {
          throw ex;
        }
        // The last backup already contained up-to-date information, just
        // rename it as if it was today's backup.
        this._backupFiles.shift();
        newBackupFile = mostRecentBackupFile;
        // Ensure we retain the proper extension when renaming
        // the most recent backup file.
        if (/\.json$/.test(PathUtils.filename(mostRecentBackupFile))) {
          newBackupFilename = this.getFilenameForDate();
        }
        newFilenameWithMetaData = appendMetaDataToFilename(newBackupFilename, {
          count: this.getBookmarkCountForFile(mostRecentBackupFile),
          hash: mostRecentHash,
        });
      }

      // Append metadata to the backup filename.
      let newBackupFileWithMetadata = PathUtils.join(
        backupFolder,
        newFilenameWithMetaData
      );
      await IOUtils.move(newBackupFile, newBackupFileWithMetadata);
      this._backupFiles.unshift(newBackupFileWithMetadata);

      // Limit the number of backups.
      await limitBackups(aMaxBackups, this._backupFiles);
    })();
  },

  /**
   * Gets the bookmark count for backup file.
   *
   * @param {string} aFilePath
   *   The file path to the backup file.
   *
   * @returns {string|null}
   *   The bookmark count or null.
   */
  getBookmarkCountForFile: function PB_getBookmarkCountForFile(aFilePath) {
    let count = null;
    let filename = PathUtils.filename(aFilePath);
    let matches = filename.match(lazy.filenamesRegex);
    if (matches && matches[2]) {
      count = matches[2];
    }
    return count;
  },

  /**
   * Gets a bookmarks tree representation usable to create backups in different
   * file formats.  The root or the tree is PlacesUtils.bookmarks.rootGuid.
   *
   * @returns {Promise<[object, number]>}
   *   An array containing two values:
   *   The first value is an object representing a tree with the places root as
   *   its root. The second value the number of items the root has.
   *   Each bookmark is represented by an object having these properties:
   *   - id: the item id (make this not enumerable after bug 824502)
   *   - title: the title
   *   - guid: unique id
   *   - parent: item id of the parent folder, not enumerable
   *   - index: the position in the parent
   *   - dateAdded: microseconds from the epoch
   *   - lastModified: microseconds from the epoch
   *   - type: type of the originating node as defined in PlacesUtils
   *   The following properties exist only for a subset of bookmarks:
   *   annos: array of annotations
   *   - uri: url
   *   - iconUri: favicon's url
   *   - keyword: associated keyword
   *   - charset: last known charset
   *   - tags: csv string of tags
   *   - root: string describing whether this represents a root
   *   - children: array of child items in a folder
   */
  async getBookmarksTree() {
    let timerId = Glean.places.backupsBookmarkstree.start();
    let root = await lazy.PlacesUtils.promiseBookmarksTree(
      lazy.PlacesUtils.bookmarks.rootGuid,
      {
        includeItemIds: true,
        includeIcons: true,
      }
    );

    Glean.places.backupsBookmarkstree.stopAndAccumulate(timerId);
    return [root, root.itemsCount];
  },
};
