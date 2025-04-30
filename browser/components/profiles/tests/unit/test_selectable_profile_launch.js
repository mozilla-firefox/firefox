/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
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

add_task(async function test_launcher() {
  let profile = await createTestProfile();

  const SelectableProfileService = getSelectableProfileService();
  SelectableProfileService.launchInstance(profile);

  let expected = ["--profiles-activate"];

  Assert.equal(
    sendCommandLine.callCount,
    1,
    "Should have attempted to remote to one instance"
  );
  Assert.deepEqual(
    sendCommandLine.firstCall.args,
    [profile.path, expected, true],
    "Expected sendCommandLine arguments"
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

  sendCommandLine.resetHistory();
  execProcess.resetHistory();

  SelectableProfileService.launchInstance(profile, "about:profilemanager");

  expected = ["-url", "about:profilemanager"];

  Assert.equal(
    sendCommandLine.callCount,
    1,
    "Should have attempted to remote to one instance"
  );
  Assert.deepEqual(
    sendCommandLine.firstCall.args,
    [profile.path, expected, true],
    "Expected sendCommandLine arguments"
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
});
