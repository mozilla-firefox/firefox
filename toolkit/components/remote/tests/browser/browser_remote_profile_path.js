/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { Subprocess } = ChromeUtils.importESModule(
  "resource://gre/modules/Subprocess.sys.mjs"
);

async function createSymlinkToProfile() {
  let link = PathUtils.join(
    PathUtils.tempDir,
    `remote-profile-link-${Date.now()}`
  );
  let process = await Subprocess.call({
    command: "/bin/ln",
    arguments: ["-s", PathUtils.profileDir, link],
  });
  let { exitCode } = await process.wait();
  Assert.equal(exitCode, 0, "Should have created the symlink");
  registerCleanupFunction(() => IOUtils.remove(link));
  return link;
}

// Other spellings of the profile path must be recognized as the current
// profile. Linux clients cannot remote their own profile, so sending to it fails
// with NS_ERROR_INVALID_ARG, elsewhere it opens the URL in the current window.
async function assertIsOwnProfile(aPath) {
  let remoteService = Cc["@mozilla.org/remote;1"].getService(
    Ci.nsIRemoteService
  );
  if (AppConstants.platform == "linux") {
    Assert.throws(
      () => remoteService.sendCommandLine(aPath, [], false),
      e => e.result == Cr.NS_ERROR_INVALID_ARG,
      `${aPath} should be identified as the current profile`
    );
    return;
  }

  let newTabOpened = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "https://example.org/",
    true
  );
  remoteService.sendCommandLine(aPath, ["https://example.org/"], false);
  Assert.ok(true, `${aPath} should be identified as the current profile`);
  BrowserTestUtils.removeTab(await newTabOpened);
}

add_task(async function test_profile_path() {
  await assertIsOwnProfile(PathUtils.profileDir);
});

add_task(async function test_unnormalized_profile_path() {
  let profileDir = PathUtils.profileDir;
  await assertIsOwnProfile(
    `${PathUtils.parent(profileDir)}/./${PathUtils.filename(profileDir)}/`
  );
});

add_task(async function test_symlinked_profile_path() {
  await assertIsOwnProfile(await createSymlinkToProfile());
});

// Launching from a profiles.ini entry that points to the current profile through
// a symlink must find the running instance, which was started with `-profile`.
// Headless instances don't use remoting.
add_task(
  { skip_if: () => Services.env.exists("MOZ_HEADLESS") },
  async function test_launch_from_symlinked_profiles_ini() {
    let link = await createSymlinkToProfile();
    let appData = await IOUtils.createUniqueDirectory(
      PathUtils.tempDir,
      "remote-profile-appdata"
    );
    registerCleanupFunction(() => IOUtils.remove(appData, { recursive: true }));
    // DevEdition only starts the profile with this name.
    let profileName = AppConstants.MOZ_DEV_EDITION
      ? "dev-edition-default"
      : "remote-test";
    await IOUtils.writeUTF8(
      PathUtils.join(appData, "profiles.ini"),
      `[General]
StartWithLastProfile=1
Version=2

[Profile0]
Name=${profileName}
IsRelative=0
Path=${link}
Default=1
`
    );

    let newTabOpened = BrowserTestUtils.waitForNewTab(
      gBrowser,
      "https://example.org/",
      true
    );
    let process = await Subprocess.call({
      command: Services.dirsvc.get("XREExeF", Ci.nsIFile).path,
      arguments: ["https://example.org/"],
      environment: {
        MOZ_APP_DATA: appData,
        MOZ_LOCAL_APP_DATA: PathUtils.join(appData, "local"),
        MOZ_LEGACY_PROFILES: "1",
      },
      environmentAppend: true,
    });
    // If the running instance isn't found, the new one reports the profile as
    // locked and waits for the user, so kill it instead of timing out.
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    let timer = setTimeout(() => process.kill(), 30000);
    let { exitCode } = await process.wait();
    clearTimeout(timer);
    Assert.equal(exitCode, 0, "Should have sent the command line remotely");
    if (exitCode == 0) {
      BrowserTestUtils.removeTab(await newTabOpened);
    }
  }
);
