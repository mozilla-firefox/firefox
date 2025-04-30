/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
/* import-globals-from /toolkit/profile/test/xpcshell/head.js */

"use strict";

const { SelectableProfile } = ChromeUtils.importESModule(
  "resource:///modules/profiles/SelectableProfile.sys.mjs"
);
const { Sqlite } = ChromeUtils.importESModule(
  "resource://gre/modules/Sqlite.sys.mjs"
);

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "SelectableProfileService", () => {
  const { SelectableProfileService } = ChromeUtils.importESModule(
    "resource:///modules/profiles/SelectableProfileService.sys.mjs"
  );

  return SelectableProfileService;
});

ChromeUtils.defineLazyGetter(lazy, "ProfilesDatastoreService", () => {
  const { ProfilesDatastoreService } = ChromeUtils.importESModule(
    "resource:///modules/profiles/ProfilesDatastoreService.sys.mjs"
  );

  ProfilesDatastoreService.overrideDirectoryService({
    ProfD: getProfileService().currentProfile.rootDir,
  });

  return ProfilesDatastoreService;
});

let gProfileServiceInitialised = false;

/**
 * Starts up the toolkit profile services and initialises it with a new default profile.
 */
function startProfileService() {
  if (gProfileServiceInitialised) {
    return;
  }

  gProfileServiceInitialised = true;
  selectStartupProfile();
}

function getSelectableProfileService() {
  return lazy.SelectableProfileService;
}

function getProfilesDatastoreService() {
  return lazy.ProfilesDatastoreService;
}

/**
 * Starts the selectable profile service and creates the group store for the
 * current profile.
 */
async function initSelectableProfileService() {
  startProfileService();

  const SelectableProfileService = getSelectableProfileService();
  const ProfilesDatastoreService = getProfilesDatastoreService();

  await ProfilesDatastoreService.init();

  await SelectableProfileService.init();

  await SelectableProfileService.maybeSetupDataStore();
}

function getRelativeProfilePath(path) {
  let relativePath = path.getRelativePath(
    Services.dirsvc.get("UAppData", Ci.nsIFile)
  );

  if (AppConstants.platform === "win") {
    relativePath = relativePath.replace("/", "\\");
  }

  return relativePath;
}

// Waits for the profile service to update about a change
async function updateNotified() {
  let { resolve, promise } = Promise.withResolvers();
  let observer = (subject, topic, data) => {
    Services.obs.removeObserver(observer, "sps-profiles-updated");
    resolve(data);
  };

  Services.obs.addObserver(observer, "sps-profiles-updated");

  await promise;
}

async function openDatabase() {
  let dbFile = Services.dirsvc.get("UAppData", Ci.nsIFile);
  dbFile.append("Profile Groups");
  dbFile.append(`${getProfileService().currentProfile.storeID}.sqlite`);
  return Sqlite.openConnection({
    path: dbFile.path,
    openNotExclusive: true,
  });
}

async function createTestProfile(profileData = {}) {
  const SelectableProfileService = getSelectableProfileService();

  let name = profileData.name ?? "Test";
  let path = profileData.path;

  if (!path) {
    path = await SelectableProfileService.createProfileDirs(name);
    await SelectableProfileService.createProfileInitialFiles(path);
    path = SelectableProfileService.getRelativeProfilePath(path);
  }

  return SelectableProfileService.insertProfile({
    avatar: profileData.avatar ?? "book",
    name,
    path,
    themeBg: profileData.themeBg ?? "var(--background-color-box)",
    themeFg: profileData.themeFg ?? "var(--text-color)",
    themeId: profileData.themeId ?? "default",
  });
}
