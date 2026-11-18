/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { BackupResource } from "moz-src:///browser/components/backup/resources/BackupResource.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ASRouterStorage: "resource:///modules/asrouter/ASRouterStorage.sys.mjs",
  ProfileAge: "resource://gre/modules/ProfileAge.sys.mjs",
});

const SNIPPETS_TABLE_NAME = "snippets";
const FILES_FOR_BACKUP = [
  "enumerate_devices.txt",
  "protections.sqlite",
  "SiteSecurityServiceState.bin",
];

/**
 * Class representing miscellaneous files for telemetry, site storage,
 * media device origin mapping, chrome privileged IndexedDB databases,
 * and Mozilla Accounts within a user profile.
 */
export class MiscDataBackupResource extends BackupResource {
  static get key() {
    return "miscellaneous";
  }

  static get requiresEncryption() {
    return false;
  }

  async backup(
    stagingPath,
    profilePath = PathUtils.profileDir,
    _isEncrypting = false
  ) {
    const files = ["enumerate_devices.txt", "SiteSecurityServiceState.bin"];
    await BackupResource.copyFiles(profilePath, stagingPath, files);

    const sqliteDatabases = ["protections.sqlite"];
    await BackupResource.copySqliteDatabases(
      profilePath,
      stagingPath,
      sqliteDatabases
    );

    // Bug 1890585 - we don't currently have the ability to copy the
    // chrome-privileged IndexedDB databases under storage/permanent/chrome.
    // Instead, we'll manually export any IndexedDB data we need to backup
    // to a separate JSON file.

    // The first IndexedDB database we want to back up is the ASRouter
    // one - specifically, the "snippets" table, as this contains information
    // on ASRouter impressions, blocked messages, message group impressions,
    // etc.
    let storage = new lazy.ASRouterStorage({
      storeNames: [SNIPPETS_TABLE_NAME],
    });
    let snippetsTable = await storage.getDbTable(SNIPPETS_TABLE_NAME);
    let snippetsObj = {};
    for (let key of await snippetsTable.getAllKeys()) {
      snippetsObj[key] = await snippetsTable.get(key);
    }
    let snippetsBackupFile = PathUtils.join(
      stagingPath,
      "activity-stream-snippets.json"
    );
    await IOUtils.writeJSON(snippetsBackupFile, snippetsObj);

    return null;
  }

  async recover(_manifestEntry, recoveryPath, destProfilePath) {
    await BackupResource.copyFiles(
      recoveryPath,
      destProfilePath,
      FILES_FOR_BACKUP
    );

    // We also want to write the recoveredFromBackup timestamp now.
    let profileAge = await lazy.ProfileAge(destProfilePath);
    await profileAge.recordRecoveredFromBackup();

    // The activity-stream-snippets data will need to be written during the
    // postRecovery phase, so we'll stash the path to the JSON file in the
    // post recovery entry.
    let snippetsBackupFile = PathUtils.join(
      recoveryPath,
      "activity-stream-snippets.json"
    );
    return { snippetsBackupFile };
  }

  async postRecovery(postRecoveryEntry) {
    let { snippetsBackupFile } = postRecoveryEntry;

    // If for some reason, the activity-stream-snippets data file has been
    // removed already, there's nothing to do.
    if (!IOUtils.exists(snippetsBackupFile)) {
      return;
    }

    let snippetsData = await IOUtils.readJSON(snippetsBackupFile);
    let storage = new lazy.ASRouterStorage({
      storeNames: [SNIPPETS_TABLE_NAME],
    });
    let snippetsTable = await storage.getDbTable(SNIPPETS_TABLE_NAME);
    for (let key in snippetsData) {
      let value = snippetsData[key];
      await snippetsTable.set(key, value);
    }
  }

  async measure(profilePath = PathUtils.profileDir) {
    let fullSize = 0;

    for (let filePath of FILES_FOR_BACKUP) {
      let resourcePath = PathUtils.join(profilePath, filePath);
      let resourceSize = await BackupResource.getFileSize(resourcePath);
      if (Number.isInteger(resourceSize)) {
        fullSize += resourceSize;
      }
    }

    let chromeIndexedDBDirPath = PathUtils.join(
      profilePath,
      "storage",
      "permanent",
      "chrome"
    );
    let chromeIndexedDBDirSize = await BackupResource.getDirectorySize(
      chromeIndexedDBDirPath
    );
    if (Number.isInteger(chromeIndexedDBDirSize)) {
      fullSize += chromeIndexedDBDirSize;
    }

    Glean.browserBackup.miscDataSize.set(fullSize);
  }
}
