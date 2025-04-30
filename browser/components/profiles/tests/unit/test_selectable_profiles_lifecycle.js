/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);

// Windows doesn't support the normal permissions and always creates and returns as 0666.
const EXPECTED_PERMISSIONS = AppConstants.platform == "win" ? 0o666 : 0o700;

const badgingService = {
  isRegistered: false,
  badge: null,

  // nsIMacDockSupport
  setBadgeImage(image, paintContext) {
    this.badge = { image, paintContext };
  },

  QueryInterface: ChromeUtils.generateQI(["nsIMacDockSupport"]),

  assertBadged(fg, bg) {
    if (!this.isRegistered) {
      return;
    }

    Assert.ok(this.badge?.image, "Should have set a badge image");
    Assert.ok(this.badge?.paintContext, "Should have set a paint context");
    Assert.equal(
      this.badge?.paintContext?.strokeColor,
      fg,
      "Stroke color should be correct"
    );
    Assert.equal(
      this.badge?.paintContext?.fillColor,
      bg,
      "Stroke color should be correct"
    );
  },

  assertNotBadged() {
    if (!this.isRegistered) {
      return;
    }

    Assert.ok(!this.badge?.image, "Should not have set a badge image");
    Assert.ok(!this.badge?.paintContext, "Should not have set a paint context");
  },
};

add_setup(() => {
  if ("nsIMacDockSupport" in Ci) {
    badgingService.isRegistered = true;
    MockRegistrar.register(
      "@mozilla.org/widget/macdocksupport;1",
      badgingService
    );
  }
});

add_task(async function test_SelectableProfileLifecycle() {
  startProfileService();

  const SelectableProfileService = getSelectableProfileService();
  const ProfilesDatastoreService = getProfilesDatastoreService();

  await ProfilesDatastoreService.init();

  Services.prefs.setBoolPref("browser.profiles.enabled", false);
  Services.prefs.setBoolPref("browser.profiles.created", false);
  await SelectableProfileService.init();
  Assert.ok(
    !SelectableProfileService.isEnabled,
    "Service should not be enabled"
  );

  Services.prefs.setBoolPref("browser.profiles.enabled", true);
  await SelectableProfileService.init();
  Assert.ok(
    SelectableProfileService.isEnabled,
    "Service should now be enabled"
  );

  let profiles = await SelectableProfileService.getAllProfiles();

  Assert.ok(!profiles.length, "No selectable profiles exist yet");

  await SelectableProfileService.maybeSetupDataStore();
  let currentProfile = SelectableProfileService.currentProfile;

  badgingService.assertNotBadged();

  const leafName = (await currentProfile.rootDir).leafName;

  const profilePath = PathUtils.join(
    Services.dirsvc.get("DefProfRt", Ci.nsIFile).path,
    leafName
  );

  let profileDirExists = await IOUtils.exists(profilePath);
  const profileLocalPath = PathUtils.join(
    Services.dirsvc.get("DefProfLRt", Ci.nsIFile).path,
    leafName
  );
  let profileLocalDirExists = await IOUtils.exists(profileLocalPath);

  Assert.ok(
    profileDirExists,
    `Profile dir was successfully created at ${profilePath}`
  );
  Assert.ok(
    profileLocalDirExists,
    `Profile local dir was successfully created at ${profileLocalPath}`
  );

  profiles = await SelectableProfileService.getAllProfiles();

  Assert.equal(profiles.length, 1, "One selectable profile exists");

  let selectableProfile = profiles[0];

  Assert.equal(
    selectableProfile.id,
    SelectableProfileService.currentProfile.id,
    "Should be the selected profile."
  );

  let profile = await SelectableProfileService.getProfile(selectableProfile.id);

  for (let attr of ["id", "name", "path"]) {
    Assert.equal(
      profile[attr],
      currentProfile[attr],
      `We got the correct profile ${attr}`
    );

    Assert.equal(
      selectableProfile[attr],
      currentProfile[attr],
      `We got the correct profile ${attr}`
    );
  }

  selectableProfile.name = "updatedTestProfile";
  selectableProfile.theme = {
    themeId: "lightTheme",
    themeFg: "#e2e1e3",
    themeBg: "010203",
  };

  await updateNotified();

  profile = await SelectableProfileService.getProfile(selectableProfile.id);

  Assert.equal(
    profile.name,
    "updatedTestProfile",
    "We got the correct profile name: updatedTestProfile"
  );

  badgingService.assertNotBadged();

  let newProfile = await createTestProfile({ name: "New profile" });

  await updateNotified();

  let rootDir = await newProfile.rootDir;
  let localDir = PathUtils.join(
    Services.dirsvc.get("DefProfLRt", Ci.nsIFile).path,
    rootDir.leafName
  );

  profileDirExists = await IOUtils.exists(rootDir.path);
  profileLocalDirExists = await IOUtils.exists(localDir);
  Assert.ok(profileDirExists, "Profile dir was successfully created");
  Assert.ok(
    profileLocalDirExists,
    "Profile local dir was successfully created"
  );

  Assert.equal(
    (await IOUtils.stat(profilePath)).permissions,
    EXPECTED_PERMISSIONS,
    "Profile dir should have the correct permissions"
  );
  Assert.equal(
    (await IOUtils.stat(profileLocalPath)).permissions,
    EXPECTED_PERMISSIONS,
    "Profile local dir should have the correct permissions"
  );

  let times = PathUtils.join(rootDir.path, "times.json");
  Assert.ok(await IOUtils.exists(times), "times.json should exist");
  let json = await IOUtils.readJSON(times);
  Assert.ok(
    json.created <= Date.now() && json.created >= Date.now() - 30000,
    "Should have been created roughly now."
  );

  let prefs = PathUtils.join(rootDir.path, "prefs.js");
  Assert.ok(await IOUtils.exists(prefs), "prefs.js should exist");
  let contents = (await IOUtils.readUTF8(prefs)).split("\n");

  let sawStoreID = false;
  let sawEnabled = false;
  for (let line of contents) {
    // Strip the windows \r
    line = line.trim();

    if (line == `user_pref("browser.profiles.enabled", true);`) {
      sawEnabled = true;
    }

    if (
      line ==
      `user_pref("toolkit.profiles.storeID", "${
        getProfileService().currentProfile.storeID
      }");`
    ) {
      sawStoreID = true;
    }
  }

  Assert.ok(sawStoreID, "Should have seen the store ID defined in prefs.js");
  Assert.ok(sawEnabled, "Should have seen the service enabled in prefs.js");

  profiles = await SelectableProfileService.getAllProfiles();
  Assert.equal(profiles.length, 2, "Should now be two profiles.");

  badgingService.assertBadged("#e2e1e3", "010203");

  await SelectableProfileService.deleteProfile(newProfile);
  await updateNotified();

  profiles = await SelectableProfileService.getAllProfiles();
  Assert.equal(profiles.length, 1, "Should now be one profiles.");

  badgingService.assertNotBadged();

  profileDirExists = await IOUtils.exists(rootDir.path);
  profileLocalDirExists = await IOUtils.exists(localDir);
  Assert.ok(!profileDirExists, "Profile dir was successfully removed");
  Assert.ok(
    !profileLocalDirExists,
    "Profile local dir was successfully removed"
  );
});
