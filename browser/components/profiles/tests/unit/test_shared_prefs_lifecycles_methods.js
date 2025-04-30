/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async () => {
  startProfileService();
  const SelectableProfileService = getSelectableProfileService();
  const ProfilesDatastoreService = getProfilesDatastoreService();

  Services.prefs.setIntPref("testPrefInt0", 5);
  Services.prefs.setBoolPref("testBoolPref", true);
  Services.prefs.setCharPref("testCharPref", "hello");

  SelectableProfileService.constructor.permanentSharedPrefs.splice(
    0,
    SelectableProfileService.constructor.permanentSharedPrefs.length,
    "testPrefInt0",
    "testBoolPref",
    "testCharPref"
  );

  await ProfilesDatastoreService.init();
  await SelectableProfileService.init();

  await SelectableProfileService.maybeSetupDataStore();
});

add_task(async function test_SharedPrefsLifecycle() {
  const SelectableProfileService = getSelectableProfileService();
  let prefs = await SelectableProfileService.getAllDBPrefs();

  Assert.equal(
    prefs.length,
    3,
    "Should have stored the default prefs into the database."
  );

  Assert.equal(
    prefs.find(p => p.name == "testPrefInt0")?.value,
    5,
    "testPrefInt0 should be correct"
  );
  Assert.equal(
    prefs.find(p => p.name == "testBoolPref")?.value,
    true,
    "testBoolPref should be correct"
  );
  Assert.equal(
    prefs.find(p => p.name == "testCharPref")?.value,
    "hello",
    "testCharPref should be correct"
  );

  Services.prefs.setIntPref("testPrefInt0", 2);
  await updateNotified();

  Services.prefs.setBoolPref("testBoolPref", false);
  await updateNotified();

  Services.prefs.setCharPref("testCharPref", "goodbye");
  await updateNotified();

  prefs = await SelectableProfileService.getAllDBPrefs();

  Assert.equal(
    prefs.length,
    3,
    "Shoulds have stored the default prefs into the database."
  );

  Assert.equal(
    prefs.find(p => p.name == "testPrefInt0")?.value,
    2,
    "testPrefInt0 should be correct"
  );
  Assert.equal(
    prefs.find(p => p.name == "testBoolPref")?.value,
    false,
    "testBoolPref should be correct"
  );
  Assert.equal(
    prefs.find(p => p.name == "testCharPref")?.value,
    "goodbye",
    "testCharPref should be correct"
  );

  Services.prefs.setIntPref("testPrefInt0", 0);
  Services.prefs.setIntPref("testPrefInt1", 1);
  await SelectableProfileService.trackPref("testPrefInt1");
  Services.prefs.setIntPref("testPrefInt2", 2);
  await SelectableProfileService.trackPref("testPrefInt2");

  // Notifications are deferred so we should only see one.
  await updateNotified();

  await Services.prefs.setCharPref("testPrefString0", "Hello world!");
  await SelectableProfileService.trackPref("testPrefString0");
  await Services.prefs.setCharPref("testPrefString1", "Hello world 2!");
  await SelectableProfileService.trackPref("testPrefString1");

  await Services.prefs.setBoolPref("testPrefBoolTrue", true);
  await SelectableProfileService.trackPref("testPrefBoolTrue");
  await Services.prefs.setBoolPref("testPrefBoolFalse", false);
  await SelectableProfileService.trackPref("testPrefBoolFalse");

  await updateNotified();

  prefs = await SelectableProfileService.getAllDBPrefs();

  Assert.equal(prefs.length, 9, "The right number of  shared prefs exist");

  Assert.equal(
    await SelectableProfileService.getDBPref("testPrefInt0"),
    0,
    "testPrefInt0 value is 0"
  );
  Assert.equal(
    await SelectableProfileService.getDBPref("testPrefInt1"),
    1,
    "testPrefInt1 value is 1"
  );
  Assert.equal(
    await SelectableProfileService.getDBPref("testPrefInt2"),
    2,
    "testPrefInt2 value is 2"
  );
  Assert.equal(
    await SelectableProfileService.getDBPref("testPrefString0"),
    "Hello world!",
    'testPrefString0 value is "Hello world!"'
  );
  Assert.equal(
    await SelectableProfileService.getDBPref("testPrefString1"),
    "Hello world 2!",
    'testPrefString1 value is "Hello world 2!"'
  );
  Assert.equal(
    await SelectableProfileService.getDBPref("testPrefBoolTrue"),
    true,
    "testPrefBoolTrue value is true"
  );
  Assert.equal(
    await SelectableProfileService.getDBPref("testPrefBoolFalse"),
    false,
    "testPrefBoolFalse value is false"
  );

  await SelectableProfileService.uninit();

  // Make some changes to the database while the service is shutdown.
  let db = await openDatabase();
  await db.execute(
    "UPDATE SharedPrefs SET value=NULL, isBoolean=FALSE WHERE name=:name;",
    { name: "testPrefInt0" }
  );
  await db.execute(
    "UPDATE SharedPrefs SET value=6, isBoolean=FALSE WHERE name=:name;",
    { name: "testPrefInt1" }
  );
  await db.execute(
    "UPDATE SharedPrefs SET value=FALSE, isBoolean=TRUE WHERE name=:name;",
    { name: "testPrefBoolTrue" }
  );
  await db.close();

  await SelectableProfileService.init();

  Assert.equal(
    Services.prefs.getPrefType("testPrefInt0"),
    Ci.nsIPrefBranch.PREF_INVALID,
    "Should have cleared the testPrefInt0 pref"
  );
  Assert.equal(
    Services.prefs.getIntPref("testPrefInt1"),
    6,
    "Should have updated testPrefInt1"
  );
  Assert.equal(
    Services.prefs.getBoolPref("testPrefBoolTrue"),
    false,
    "Should have updated testPrefBoolTrue"
  );
});
