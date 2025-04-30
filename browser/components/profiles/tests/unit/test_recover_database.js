/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// If we think profiles have been created but there is no current profile in the database but there
// are other profiles we should create the current profile entry.
add_task(async function test_recover_database() {
  startProfileService();

  const SelectableProfileService = getSelectableProfileService();
  const ProfilesDatastoreService = getProfilesDatastoreService();

  await ProfilesDatastoreService.init();

  let db = await ProfilesDatastoreService.getConnection();

  let rootDir = getProfileService().currentProfile.rootDir.clone();
  rootDir.leafName = "other";
  let otherPath = getRelativeProfilePath(rootDir);

  // Inject some other profile into the database
  await db.execute(
    `INSERT INTO Profiles VALUES (NULL, :path, :name, :avatar, :themeId, :themeFg, :themeBg);`,
    {
      path: otherPath,
      name: "Fake Profile",
      avatar: "book",
      themeId: "default",
      themeFg: "",
      themeBg: "",
    }
  );

  let toolkitProfile = getProfileService().currentProfile;
  toolkitProfile.storeID = await ProfilesDatastoreService.storeID;

  Services.prefs.setBoolPref("browser.profiles.enabled", true);
  Services.prefs.setBoolPref("browser.profiles.created", true);
  await SelectableProfileService.init();

  Assert.ok(SelectableProfileService.isEnabled, "Service should be enabled");

  Assert.ok(
    Services.prefs.getBoolPref("browser.profiles.created", false),
    "Should have kept the profile created state."
  );

  Assert.equal(
    toolkitProfile.storeID,
    await ProfilesDatastoreService.storeID,
    "Should not have cleared the store ID"
  );
  Assert.ok(
    SelectableProfileService.currentProfile,
    "Should have created the current profile"
  );

  let profiles = await SelectableProfileService.getAllProfiles();
  Assert.equal(profiles.length, 2, "Should be two profiles in the database");

  let newProfile = await SelectableProfileService.createNewProfile(false);
  Assert.ok(newProfile, "Should have created a new profile");

  profiles = await SelectableProfileService.getAllProfiles();
  Assert.equal(profiles.length, 3, "Should be three profiles in the database");
});
