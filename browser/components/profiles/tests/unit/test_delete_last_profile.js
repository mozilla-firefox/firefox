/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const execProcess = sinon.fake();
const sendCommandLine = sinon.fake.throws(Cr.NS_ERROR_NOT_AVAILABLE);

add_setup(async () => {
  await initSelectableProfileService();

  sinon.replace(
    getSelectableProfileService(),
    "sendCommandLine",
    (path, args, raise) => sendCommandLine(path, [...args], raise)
  );
  sinon.replace(getSelectableProfileService(), "execProcess", execProcess);
});

add_task(async function test_delete_last_profile() {
  const SelectableProfileService = getSelectableProfileService();

  let profiles = await SelectableProfileService.getAllProfiles();
  Assert.equal(profiles.length, 1, "Only 1 profile exists before deleting");

  let profile = profiles[0];
  Assert.equal(
    SelectableProfileService.groupToolkitProfile.rootDir.path,
    profile.path,
    "The group toolkit profile path should be the path of the original profile"
  );

  await SelectableProfileService.setShowProfileSelectorWindow(true);
  Assert.ok(
    SelectableProfileService.groupToolkitProfile.showProfileSelector,
    "Show profile selector is enabled"
  );

  let updated = updateNotified();
  await SelectableProfileService.deleteCurrentProfile();
  await updated;

  profiles = await SelectableProfileService.getAllProfiles();
  Assert.equal(profiles.length, 1, "Only 1 profile exists after deleting");

  profile = profiles[0];

  let expected = ["-url", "about:newprofile"];

  await TestUtils.waitForCondition(
    () => sendCommandLine.callCount > 1,
    "Waiting for notify task to complete"
  );

  Assert.equal(
    sendCommandLine.callCount,
    2,
    "Should have attempted to remote twice"
  );

  let [openCall, updateCall] = sendCommandLine.getCalls();
  // These can come in any order so switch them if needed.
  if (openCall.args[1].length == 1) {
    [updateCall, openCall] = [openCall, updateCall];
  }

  Assert.deepEqual(
    openCall.args,
    [profile.path, expected, true],
    "Expected sendCommandLine arguments to open new profile"
  );

  Assert.deepEqual(
    updateCall.args,
    [profile.path, ["--profiles-updated"], false],
    "Expected sendCommandLine arguments to update other profiles"
  );

  expected.unshift("--profile", profile.path);

  if (Services.appinfo.OS === "Darwin") {
    expected.unshift("-foreground");
  }

  // Our mock remote service claims the instance is not running so we will fall back to launching
  // a new process.

  Assert.equal(execProcess.callCount, 1, "Should have called execProcess once");
  Assert.deepEqual(
    execProcess.firstCall.args,
    [expected],
    "Expected execProcess arguments"
  );

  Assert.equal(
    SelectableProfileService.groupToolkitProfile.rootDir.path,
    profile.path,
    "The group toolkit profile path should be the path of the newly created profile"
  );

  Assert.ok(
    !SelectableProfileService.groupToolkitProfile.showProfileSelector,
    "Show profile selector is disabled"
  );
});
