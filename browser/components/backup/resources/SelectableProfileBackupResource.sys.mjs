/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { BackupResource } from "moz-src:///browser/components/backup/resources/BackupResource.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "l10n", () => {
  return new Localization(["browser/backupSettings.ftl"]);
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "SelectableProfileBackupResource",
    maxLogLevel: Services.prefs.getBoolPref("browser.backup.log", false)
      ? "Debug"
      : "Warn",
  });
});

const SELECTABLE_PROFILE_STAGING_FOLDER_NAME = "selectable_profile_metadata";
const SELECTABLE_PROFILE_METADATA_FILE_NAME = "profile_metadata.json";

/**
 * Class representing data needed for backing up/restoring a selectable profile. This resource
 * is only to be used if we are
 * a. backing up a selectable profile
 * b. recovering a selectable profile type backup. This is guaranteed if (a) was true when backing up.
 */
export class SelectableProfileBackupResource extends BackupResource {
  static get key() {
    return "selectable_profile";
  }

  static get requiresEncryption() {
    return false;
  }

  static get canBackupResource() {
    // If the current profile is not a Selectable Profile - we don't want to backup these items
    return lazy.SelectableProfileService.currentProfile;
  }

  async backup(
    stagingPath,
    profilePath = PathUtils.profileDir,
    _isEncrypting = false
  ) {
    let selectableProfile = await lazy.SelectableProfileService.currentProfile;

    if (!selectableProfile) {
      // No selectable profile for this path - nothing to backup
      lazy.logConsole.warn(
        `There is no valid selectable profile in this path ${profilePath}`
      );
      return null;
    }

    // Create a selectable profile directory in the staging path
    let stagingSelectableProfilesDirectoryPath = PathUtils.join(
      stagingPath,
      SELECTABLE_PROFILE_STAGING_FOLDER_NAME
    );
    await IOUtils.makeDirectory(stagingSelectableProfilesDirectoryPath);

    lazy.logConsole.debug(
      `Created staging directory at ${stagingSelectableProfilesDirectoryPath}`
    );

    // add the avatar file if there is a custom avatar being used
    if (selectableProfile.hasCustomAvatar) {
      let avatarDirectoryPath = PathUtils.parent(
        lazy.SelectableProfileService.currentProfile.getAvatarPath()
      );
      // Copy over the custom avatar file into this folder, the size doesn't matter here
      await BackupResource.copyFiles(
        avatarDirectoryPath,
        stagingSelectableProfilesDirectoryPath,
        [selectableProfile.avatar]
      );

      lazy.logConsole.debug(`Copied over the custom avatar file into staging`);
    }

    // Create a json with the name, theme, avatar to store
    let metadata = {
      name: selectableProfile.name,
      theme: selectableProfile.theme,
      avatar: selectableProfile.avatar,
      hasCustomAvatar: selectableProfile.hasCustomAvatar,
      deviceName: Services.sysinfo.get("device") || Services.dns.myHostName,
    };

    let metadataFile = PathUtils.join(
      stagingSelectableProfilesDirectoryPath,
      SELECTABLE_PROFILE_METADATA_FILE_NAME
    );
    await IOUtils.writeJSON(metadataFile, metadata);

    lazy.logConsole.debug(
      `Finished backing up selectable profile data with metadata ${JSON.stringify(metadata)}`
    );

    return null;
  }

  async recover(_manifestEntry, recoveryPath, destProfilePath) {
    // Guard against calling this recover method if the current profile
    // is not a selectable profile type.
    if (!lazy.SelectableProfileService.hasCreatedSelectableProfiles()) {
      lazy.logConsole.error(
        `We can only call recover on SelectableProfileBackupResource if the current profile is a selectable profile`
      );
      return null;
    }

    // Read the data!
    let selectableProfileRecoveryDirectoryPath = PathUtils.join(
      recoveryPath,
      SELECTABLE_PROFILE_STAGING_FOLDER_NAME
    );
    let selectableProfileMetadataFilePath = PathUtils.join(
      selectableProfileRecoveryDirectoryPath,
      SELECTABLE_PROFILE_METADATA_FILE_NAME
    );

    let metadata;
    try {
      metadata = await IOUtils.readJSON(selectableProfileMetadataFilePath);
    } catch (e) {
      lazy.logConsole.error(`Had trouble reading JSON ${e}`);
      return null;
    }

    let newProfile = await lazy.SelectableProfileService.getProfileByPath(
      await IOUtils.getFile(destProfilePath)
    );

    if (!newProfile) {
      lazy.logConsole.error(
        `No selectable profile found at ${destProfilePath}`
      );
      return null;
    }

    lazy.logConsole.debug(
      `Set the new profile's data with backed up metadata ${JSON.stringify(metadata)}`
    );

    // Before we set the name, let's make sure it's unique
    let profilesArray = await lazy.SelectableProfileService.getAllProfiles();
    let isDupe = profilesArray.some(
      p => p.id !== newProfile.id && p.name === metadata.name
    );

    if (isDupe) {
      metadata.name = await lazy.l10n.formatValue(
        "backup-restored-profile-name",
        {
          deviceName: metadata.deviceName || "backup",
          date: Date.now(),
        }
      );
    }

    await newProfile.setNameAsync(metadata.name);
    await newProfile.setThemeAsync(metadata.theme);

    if (metadata.hasCustomAvatar) {
      // Ensure that there is an avatar file in the recovery directory
      let avatarRecoveryFilePath = PathUtils.join(
        selectableProfileRecoveryDirectoryPath,
        metadata.avatar
      );

      if (!(await IOUtils.exists(avatarRecoveryFilePath))) {
        lazy.logConsole.error(
          `We expect a custom avatar, but there is no file in the recovery dest: ${avatarRecoveryFilePath}`
        );
        return null;
      }

      let avatarFile = await File.createFromFileName(avatarRecoveryFilePath);
      await newProfile.setAvatar(avatarFile);
    } else {
      await newProfile.setAvatar(metadata.avatar);
    }

    lazy.logConsole.debug("Finished recovery of selectable profile data");
    return null;
  }

  async postRecovery(postRecoveryEntry) {
    let themeId = postRecoveryEntry?.themeId;
    if (!themeId) {
      return;
    }

    try {
      await lazy.SelectableProfileService.enableTheme(themeId);
    } catch (e) {
      lazy.logConsole.warn(
        "Failed to enable theme, falling back to default",
        e
      );
      await lazy.SelectableProfileService.enableTheme(
        "default-theme@mozilla.org"
      );
    }
  }

  async measure(_profilePath) {
    // No measurements needed here
  }
}
