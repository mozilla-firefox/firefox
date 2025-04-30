/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_recover_storeID() {
  startProfileService();
  Services.prefs.setCharPref("toolkit.profiles.storeID", "foobar");
  Services.prefs.setBoolPref("browser.profiles.created", true);

  // The database needs to exist already
  let groupsPath = PathUtils.join(
    Services.dirsvc.get("UAppData", Ci.nsIFile).path,
    "Profile Groups"
  );

  await IOUtils.makeDirectory(groupsPath);
  let dbFile = PathUtils.join(groupsPath, "foobar.sqlite");
  let db = await Sqlite.openConnection({
    path: dbFile,
    openNotExclusive: true,
  });

  let path = getRelativeProfilePath(getProfileService().currentProfile.rootDir);

  // Slightly annoying we have to replicate this...
  await db.execute(`CREATE TABLE IF NOT EXISTS "Profiles" (
      id  INTEGER NOT NULL,
      path	TEXT NOT NULL UNIQUE,
      name	TEXT NOT NULL,
      avatar	TEXT NOT NULL,
      themeId	TEXT NOT NULL,
      themeFg	TEXT NOT NULL,
      themeBg	TEXT NOT NULL,
      PRIMARY KEY(id)
    );`);

  await db.execute(
    `INSERT INTO Profiles VALUES (NULL, :path, :name, :avatar, :themeId, :themeFg, :themeBg);`,
    {
      path,
      name: "Fake Profile",
      avatar: "book",
      themeId: "default",
      themeFg: "",
      themeBg: "",
    }
  );

  await db.close();

  const SelectableProfileService = getSelectableProfileService();
  const ProfilesDatastoreService = getProfilesDatastoreService();

  await ProfilesDatastoreService.init();
  await SelectableProfileService.init();
  Assert.ok(SelectableProfileService.initialized, "Did initialize the service");

  let profile = SelectableProfileService.currentProfile;
  Assert.ok(profile, "Should have a current profile");
  Assert.equal(profile.name, "Fake Profile");
  Assert.equal(
    getProfileService().currentProfile.storeID,
    "foobar",
    "Should have updated the store ID on the profile"
  );
});
