/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_recover_storeID() {
  startProfileService();
  Services.prefs.setCharPref("toolkit.profiles.storeID", "foobar");

  const SelectableProfileService = getSelectableProfileService();
  const ProfilesDatastoreService = getProfilesDatastoreService();

  await ProfilesDatastoreService.init();
  await SelectableProfileService.init();

  Assert.ok(
    !ProfilesDatastoreService.initialized,
    "Didn't initialize the datastore service"
  );
  Assert.ok(
    !SelectableProfileService.initialized,
    "Didn't initialize the profiles service"
  );

  let profile = SelectableProfileService.currentProfile;
  Assert.ok(!profile, "Should not have a current profile");
  Assert.equal(
    getProfileService().currentProfile.storeID,
    null,
    "Should not have updated the store ID on the profile"
  );
});
