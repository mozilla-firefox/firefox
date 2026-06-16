/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests that from an empty database a default profile is created.
 */

add_task(async () => {
  let start = Date.now();
  let service = getProfileService();
  let { profile, didCreate } = selectStartupProfile();

  let timesFile = profile.rootDir.clone();
  timesFile.append("times.json");
  let times = await IOUtils.readJSON(timesFile.path);

  // Allow 5ms tolerance.
  Assert.greaterOrEqual(
    times.created,
    start - 5,
    "Profile should have been created after the test startup began"
  );
  Assert.lessOrEqual(
    times.created,
    Date.now(),
    "Profile should have been created before the test startup finished"
  );

  checkStartupReason("firstrun-created-default");
  await checkProfileSource(profile, "firstrun-created-default");

  let profileData = readProfilesIni();
  checkProfileService(profileData);

  Assert.ok(didCreate, "Should have created a new profile.");
  Assert.equal(
    profile,
    service.defaultProfile,
    "Should now be the default profile."
  );
  Assert.equal(
    profile.name,
    DEDICATED_NAME,
    "Should have created a new profile with the right name."
  );

  profile = [...getProfileService().profiles].find(p => p.name == "default");
  await checkProfileSource(profile, "legacy");

  Assert.ok(
    profileData.options.startWithLastProfile,
    "Should be set to start with the last profile."
  );
  Assert.equal(
    profileData.profiles.length,
    2,
    "Should have the right number of profiles."
  );

  profile = profileData.profiles[0];
  Assert.equal(profile.name, "default", "Should have the right name.");
  Assert.ok(profile.default, "Should be marked as the old-style default.");

  profile = profileData.profiles[1];
  Assert.equal(profile.name, DEDICATED_NAME, "Should have the right name.");
  Assert.ok(!profile.default, "Should not be marked as the old-style default.");

  let hash = xreDirProvider.getInstallHash();
  Assert.ok(
    profileData.installs[hash].locked,
    "Should have locked the profile"
  );
});
