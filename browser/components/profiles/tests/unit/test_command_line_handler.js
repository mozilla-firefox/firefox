/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const execProcess = sinon.stub();
const sendCommandLine = sinon.stub().throws(Cr.NS_ERROR_NOT_AVAILABLE);

add_setup(async () => {
  await initSelectableProfileService();

  sinon.replace(
    getSelectableProfileService(),
    "sendCommandLine",
    (path, args, raise) => sendCommandLine(path, [...args], raise)
  );
  sinon.replace(getSelectableProfileService(), "execProcess", execProcess);
});

function nextCall(stub) {
  let { promise, resolve } = Promise.withResolvers();

  stub.callsFake(() => resolve());
  return promise;
}

add_task(async function test_profiles_update() {
  let clh = Cc[
    "@mozilla.org/browser/selectable-profiles-service-clh;1"
  ].createInstance(Ci.nsICommandLineHandler);

  let observer = sinon.stub();
  Services.obs.addObserver(observer, "sps-profiles-updated");
  let observerArgsPromise = nextCall(observer);

  let cmdLine = Cu.createCommandLine(
    ["--profiles-updated"],
    null,
    Ci.nsICommandLine.STATE_REMOTE_AUTO
  );

  clh.handle(cmdLine);

  await observerArgsPromise;

  Assert.equal(observer.callCount, 1, "Should have called the observer");
  Assert.deepEqual(observer.firstCall.args, [
    null,
    "sps-profiles-updated",
    "remote",
  ]);

  Assert.ok(
    cmdLine.preventDefault,
    "Should have prevented the default handler"
  );

  Services.obs.removeObserver(observer, "sps-profiles-updated");
});

add_task(async function test_redirect_args() {
  if (Services.appinfo.OS !== "Darwin") {
    // This behaviour is specific to macOS
    return;
  }

  let clh = Cc[
    "@mozilla.org/browser/selectable-profiles-service-clh;1"
  ].createInstance(Ci.nsICommandLineHandler);

  let { rootDir } = getProfileService().currentProfile;

  let profilePath = rootDir.path;

  let cmdLine = Cu.createCommandLine(
    ["-url", "https://www.google.com/"],
    null,
    Ci.nsICommandLine.STATE_REMOTE_EXPLICIT
  );

  clh.handle(cmdLine);

  Assert.ok(
    cmdLine.preventDefault,
    "Should have prevented the default handler"
  );

  await nextCall(execProcess);

  Assert.equal(
    sendCommandLine.callCount,
    1,
    "Should have attempted to remote once"
  );
  Assert.deepEqual(
    sendCommandLine.firstCall.args,
    [profilePath, ["-url", "https://www.google.com/"], true],
    "should have used the right arguments"
  );

  sendCommandLine.resetHistory();

  Assert.equal(execProcess.callCount, 1, "Should have attempted to exec once");
  Assert.deepEqual(
    execProcess.firstCall.args,
    [["-foreground", "-url", "https://www.google.com/"]],
    "should have used the right arguments"
  );

  execProcess.resetHistory();

  let profileData = readProfilesIni();

  for (let profile of profileData.profiles) {
    if (profile.storeID == getSelectableProfileService().storeID) {
      profile.path = profile.path.replace(rootDir.leafName, "other-profile");

      break;
    }
  }

  writeProfilesIni(profileData);

  rootDir.leafName = "other-profile";

  profilePath = rootDir.path;

  cmdLine = Cu.createCommandLine(
    ["-url", "https://www.example.com/", "-url", "https://www.mozilla.org/"],
    null,
    Ci.nsICommandLine.STATE_REMOTE_EXPLICIT
  );

  clh.handle(cmdLine);

  Assert.ok(
    cmdLine.preventDefault,
    "Should have prevented the default handler"
  );

  await nextCall(execProcess);

  Assert.equal(
    sendCommandLine.callCount,
    1,
    "Should have attempted to remote once"
  );
  Assert.deepEqual(
    sendCommandLine.firstCall.args,
    [
      profilePath,
      ["-url", "https://www.example.com/", "-url", "https://www.mozilla.org/"],
      true,
    ],
    "should have used the right arguments"
  );

  sendCommandLine.resetHistory();

  Assert.equal(execProcess.callCount, 1, "Should have attempted to exec once");
  Assert.deepEqual(
    execProcess.firstCall.args,
    [
      [
        "-foreground",
        "-url",
        "https://www.example.com/",
        "-url",
        "https://www.mozilla.org/",
      ],
    ],
    "should have used the right arguments"
  );

  execProcess.resetHistory();
});
