/* import-globals-from head_addons.js */

const PREF_SYSTEM_ADDON_SET = "extensions.systemAddonSet";
const PREF_SYSTEM_ADDON_UPDATE_URL = "extensions.systemAddon.update.url";
const PREF_SYSTEM_ADDON_UPDATE_ENABLED =
  "extensions.systemAddon.update.enabled";

// See bug 1507255
Services.prefs.setBoolPref("media.gmp-manager.updateEnabled", true);

function root(server) {
  let { primaryScheme, primaryHost, primaryPort } = server.identity;
  return `${primaryScheme}://${primaryHost}:${primaryPort}/data`;
}

ChromeUtils.defineLazyGetter(this, "testserver", () => {
  let server = new HttpServer();
  server.start();
  Services.prefs.setCharPref(
    PREF_SYSTEM_ADDON_UPDATE_URL,
    `${root(server)}/update.xml`
  );
  return server;
});

async function serveSystemUpdate(xml, perform_update) {
  testserver.registerPathHandler("/data/update.xml", (request, response) => {
    response.write(xml);
  });

  try {
    await perform_update();
  } finally {
    testserver.registerPathHandler("/data/update.xml", null);
  }
}

// Runs an update check making it use the passed in xml string. Uses the direct
// call to the update function so we get rejections on failure.
async function installSystemAddons(xml, waitIDs = []) {
  info("Triggering system add-on update check.");

  await serveSystemUpdate(
    xml,
    async function () {
      let { XPIExports } = ChromeUtils.importESModule(
        "resource://gre/modules/addons/XPIExports.sys.mjs"
      );
      await Promise.all([
        XPIExports.XPIProvider.updateSystemAddons(),
        ...waitIDs.map(id => promiseWebExtensionStartup(id)),
      ]);
    },
    testserver
  );
}

// Runs a full add-on update check which will in some cases do a system add-on
// update check. Always succeeds.
async function updateAllSystemAddons(xml) {
  info("Triggering full add-on update check.");

  await serveSystemUpdate(
    xml,
    function () {
      return new Promise(resolve => {
        Services.obs.addObserver(function observer() {
          Services.obs.removeObserver(
            observer,
            "addons-background-update-complete"
          );

          resolve();
        }, "addons-background-update-complete");

        // Trigger the background update timer handler
        gInternalManager.notify(null);
      });
    },
    testserver
  );
}

// Builds an update.xml file for an update check based on the data passed.
function buildSystemAddonUpdates(addons) {
  let xml = `<?xml version="1.0" encoding="UTF-8"?>\n\n<updates>\n`;
  if (addons) {
    xml += `  <addons>\n`;
    for (let addon of addons) {
      if (addon.xpi) {
        testserver.registerFile(`/data/${addon.path}`, addon.xpi);
      }

      xml += `    <addon id="${addon.id}" URL="${root(testserver)}/${
        addon.path
      }" version="${addon.version}"`;
      if (addon.hashFunction) {
        xml += ` hashFunction="${addon.hashFunction}"`;
      }
      if (addon.hashValue) {
        xml += ` hashValue="${addon.hashValue}"`;
      }
      xml += `/>\n`;
    }
    xml += `  </addons>\n`;
  }
  xml += `</updates>\n`;

  return xml;
}

let _systemXPIs = new Map();
function getSystemAddonXPI(num, version) {
  let key = `${num}:${version}`;
  if (!_systemXPIs.has(key)) {
    _systemXPIs.set(
      key,
      AddonTestUtils.createTempWebExtensionFile({
        manifest: {
          name: `System Add-on ${num}`,
          version,
          browser_specific_settings: {
            gecko: {
              id: `system${num}@tests.mozilla.org`,
            },
          },
        },
      })
    );
  }
  return _systemXPIs.get(key);
}

async function getSystemBuiltin(num, addon_version, res_url) {
  const id = `system${num}@tests.mozilla.org`;
  const version = addon_version ?? "1.0";
  const addon_res_url_path = res_url ?? `builtin-system${num}`;
  await setupBuiltinExtension(
    {
      manifest: {
        name: `Built-in add-on #${num}`,
        version,
        browser_specific_settings: { gecko: { id } },
      },
    },
    addon_res_url_path
  );
  return {
    addon_id: id,
    addon_version: version,
    res_url: `resource://${addon_res_url_path}/`,
  };
}

async function initSystemAddonDirs() {
  let hiddenSystemAddonDir = FileUtils.getDir("ProfD", [
    "sysfeatures",
    "hidden",
  ]);
  hiddenSystemAddonDir.create(
    Ci.nsIFile.DIRECTORY_TYPE,
    FileUtils.PERMS_DIRECTORY
  );
  let system1_1 = await getSystemAddonXPI(1, "1.0");
  system1_1.copyTo(hiddenSystemAddonDir, "system1@tests.mozilla.org.xpi");

  let system2_1 = await getSystemAddonXPI(2, "1.0");
  system2_1.copyTo(hiddenSystemAddonDir, "system2@tests.mozilla.org.xpi");

  let prefilledSystemAddonDir = FileUtils.getDir("ProfD", [
    "sysfeatures",
    "prefilled",
  ]);
  prefilledSystemAddonDir.create(
    Ci.nsIFile.DIRECTORY_TYPE,
    FileUtils.PERMS_DIRECTORY
  );
  let system2_2 = await getSystemAddonXPI(2, "2.0");
  system2_2.copyTo(prefilledSystemAddonDir, "system2@tests.mozilla.org.xpi");
  let system3_2 = await getSystemAddonXPI(3, "2.0");
  system3_2.copyTo(prefilledSystemAddonDir, "system3@tests.mozilla.org.xpi");
}

/**
 * Returns current system add-on update directory (stored in pref).
 */
function getCurrentSystemAddonUpdatesDir() {
  const updatesDir = FileUtils.getDir("ProfD", ["features"]);
  let dir = updatesDir.clone();
  let set = JSON.parse(Services.prefs.getCharPref(PREF_SYSTEM_ADDON_SET));
  dir.append(set.directory);
  return dir;
}

/**
 * Removes all files from system add-on update directory.
 */
function clearSystemAddonUpdatesDir() {
  const updatesDir = FileUtils.getDir("ProfD", ["features"]);
  // Delete any existing directories
  if (updatesDir.exists()) {
    updatesDir.remove(true);
  }

  Services.prefs.clearUserPref(PREF_SYSTEM_ADDON_SET);
}

registerCleanupFunction(() => {
  clearSystemAddonUpdatesDir();
});

/**
 * Installs a known set of add-ons into the system add-on update directory.
 */
async function buildPrefilledUpdatesDir() {
  clearSystemAddonUpdatesDir();

  // Build the test set
  let dir = FileUtils.getDir("ProfD", ["features", "prefilled"]);
  dir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);

  let xpi = await getSystemAddonXPI(2, "2.0");
  xpi.copyTo(dir, "system2@tests.mozilla.org.xpi");

  xpi = await getSystemAddonXPI(3, "2.0");
  xpi.copyTo(dir, "system3@tests.mozilla.org.xpi");

  // Mark these in the past so the startup file scan notices when files have changed properly
  {
    let toModify = await IOUtils.getFile(
      PathUtils.profileDir,
      "features",
      "prefilled",
      "system2@tests.mozilla.org.xpi"
    );
    toModify.lastModifiedTime -= 10000;

    toModify = await IOUtils.getFile(
      PathUtils.profileDir,
      "features",
      "prefilled",
      "system3@tests.mozilla.org.xpi"
    );
    toModify.lastModifiedTime -= 10000;
  }

  Services.prefs.setCharPref(
    PREF_SYSTEM_ADDON_SET,
    JSON.stringify({
      schema: 1,
      directory: dir.leafName,
      addons: {
        "system2@tests.mozilla.org": {
          version: "2.0",
        },
        "system3@tests.mozilla.org": {
          version: "2.0",
        },
      },
    })
  );
}

/**
 * Check currently installed ssystem add-ons against a set of conditions.
 *
 * @param {Array<Object>} conditions - an array of objects of the form { isUpgrade: false, version: null}
 * @param {nsIFile} distroDir - the system add-on distribution directory (the "features" dir in the app directory)
 */
async function checkInstalledSystemAddons(conditions, distroDir) {
  for (let i = 0; i < conditions.length; i++) {
    let condition = conditions[i];
    let id = "system" + (i + 1) + "@tests.mozilla.org";
    let addon = await promiseAddonByID(id);

    if (!("isUpgrade" in condition) || !("version" in condition)) {
      throw Error("condition must contain isUpgrade and version");
    }
    let isUpgrade = conditions[i].isUpgrade;
    let version = conditions[i].version;

    let expectedDir = isUpgrade ? getCurrentSystemAddonUpdatesDir() : distroDir;

    if (version) {
      info(`Checking state of add-on ${id}, expecting version ${version}`);

      // Add-on should be installed
      Assert.notEqual(addon, null);
      Assert.equal(addon.version, version);
      Assert.ok(addon.isActive);
      Assert.ok(!addon.foreignInstall);
      Assert.ok(addon.hidden);
      Assert.ok(addon.isSystem);

      // Verify the add-ons file is in the right place
      let file = expectedDir.clone();
      file.append(id + ".xpi");
      Assert.ok(file.exists());
      Assert.ok(file.isFile());

      let uri = addon.getResourceURI();
      if (uri instanceof Ci.nsIJARURI) {
        uri = uri.JARFile;
      }

      if (isUpgrade) {
        Assert.equal(addon.signedState, AddonManager.SIGNEDSTATE_SYSTEM);
      } else {
        Assert.equal(uri.spec, `resource://test-builtin-ext${i + 1}/`);
      }
    } else {
      info(`Checking state of add-on ${id}, expecting it to be missing`);

      if (isUpgrade) {
        // Add-on should not be installed
        Assert.equal(addon, null);
      }
    }
  }
}

/**
 * Returns all system add-on updates directories.
 */
async function getSystemAddonDirectories() {
  const updatesDir = FileUtils.getDir("ProfD", ["features"]);
  let subdirs = [];

  if (await IOUtils.exists(updatesDir.path)) {
    for (const child of await IOUtils.getChildren(updatesDir.path)) {
      const stat = await IOUtils.stat(child);
      if (stat.type === "directory") {
        subdirs.push(child);
      }
    }
  }

  return subdirs;
}

/**
 * Sets up initial system add-on update conditions.
 *
 * @param {Object<function, Array<Object>} setup - an object containing a setup function and an array of objects
 *  of the form {isUpgrade: false, version: null}
 *
 * @param {nsIFile} distroDir - the system add-on distribution directory (the "features" dir in the app directory)
 */
async function setupSystemAddonConditions(setup, distroDir) {
  info("Clearing existing database.");
  Services.prefs.clearUserPref(PREF_SYSTEM_ADDON_SET);
  distroDir.leafName = "empty";

  await overrideBuiltIns({ system: [], builtins: [] });
  await promiseStartupManager();
  await promiseShutdownManager();

  info("Setting up conditions.");
  await setup.setup();

  let preinstalledList = [];
  if (distroDir) {
    if (distroDir.path.endsWith("hidden")) {
      preinstalledList = [
        "system1@tests.mozilla.org",
        "system2@tests.mozilla.org",
      ];
    } else if (distroDir.path.endsWith("prefilled")) {
      preinstalledList = [
        "system2@tests.mozilla.org",
        "system3@tests.mozilla.org",
      ];
    }
  }

  const startupPromises = [];
  const overriddenBuiltInsData = {
    system: [],
    builtins: [],
  };

  for (const [i, state] of setup.initialState.entries()) {
    const id = `system${i + 1}@tests.mozilla.org`;

    if (!state.version || !preinstalledList.includes(id)) {
      continue;
    }

    const res_path = `test-builtin-ext${i + 1}`;
    let version = distroDir.path.endsWith("hidden") ? "1.0" : "2.0";
    await setupBuiltinExtension(
      {
        manifest: {
          name: `Built-In System Add-on ${i + 1}`,
          version,
          browser_specific_settings: {
            gecko: {
              id: `system${i + 1}@tests.mozilla.org`,
            },
          },
        },
      },
      res_path
    );
    overriddenBuiltInsData.builtins.push({
      addon_id: id,
      addon_version: version,
      res_url: `resource://${res_path}/`,
    });
    startupPromises.push(promiseWebExtensionStartup(id));
  }

  info(
    `setupSystemAddonConditions overriddenBuiltIns: ${JSON.stringify(overriddenBuiltInsData, null, 2)}\n`
  );
  await overrideBuiltIns(overriddenBuiltInsData);

  await Promise.all([promiseStartupManager(), ...startupPromises]);

  // Make sure the initial state is correct
  info("Checking initial state.");
  await checkInstalledSystemAddons(setup.initialState, distroDir);
}

/**
 * Verify state of system add-ons after installation.
 *
 * @param {Array<Object>} initialState - an array of objects of the form {isUpgrade: false, version: null}
 * @param {Array<Object>} finalState - an array of objects of the form {isUpgrade: false, version: null}
 * @param {Boolean} alreadyUpgraded - whether a restartless upgrade has already been performed.
 * @param {nsIFile} distroDir - the system add-on distribution directory (the "features" dir in the app directory)
 */
async function verifySystemAddonState(
  initialState,
  finalState = undefined,
  alreadyUpgraded = false,
  distroDir
) {
  let expectedDirs = 0;

  // If the initial state was using the profile set then that directory will
  // still exist.

  if (initialState.some(a => a.isUpgrade)) {
    expectedDirs++;
  }

  if (finalState == undefined) {
    finalState = initialState;
  } else if (finalState.some(a => a.isUpgrade)) {
    // If the new state is using the profile then that directory will exist.
    expectedDirs++;
  }

  // Since upgrades are restartless now, the previous update dir hasn't been removed.
  if (alreadyUpgraded) {
    expectedDirs++;
  }

  info("Checking final state.");

  let dirs = await getSystemAddonDirectories();
  Assert.equal(dirs.length, expectedDirs);

  await checkInstalledSystemAddons(...finalState, distroDir);

  // Check that the new state is active after a restart
  await promiseShutdownManager();

  // TODO: refator this logic out
  let preinstalledList = [];

  if (distroDir) {
    if (distroDir.path.endsWith("hidden")) {
      preinstalledList = [
        "system1@tests.mozilla.org",
        "system2@tests.mozilla.org",
      ];
    } else if (distroDir.path.endsWith("prefilled")) {
      preinstalledList = [
        "system2@tests.mozilla.org",
        "system3@tests.mozilla.org",
      ];
    }
  }

  const overriddenBuiltInsData = {
    system: [],
    builtins: [],
  };

  for (const [i] of finalState.entries()) {
    const id = `system${i + 1}@tests.mozilla.org`;

    if (!preinstalledList.includes(id)) {
      continue;
    }

    const res_path = `test-builtin-ext${i + 1}`;
    let version = distroDir.path.endsWith("hidden") ? "1.0" : "2.0";
    overriddenBuiltInsData.builtins.push({
      addon_id: id,
      addon_version: version,
      res_url: `resource://${res_path}/`,
    });
  }

  await overrideBuiltIns(overriddenBuiltInsData);

  await promiseStartupManager();
  await checkInstalledSystemAddons(finalState, distroDir);
}

/**
 * Run system add-on tests and compare the results against a set of expected conditions.
 *
 * @param {String} setupName - name of the current setup conditions.
 * @param {Object<function, Array<Object>} setup -  Defines the set of initial conditions to run each test against. Each should
 *                                                  define the following properties:
 *    setup:        A task to setup the profile into the initial state.
 *    initialState: The initial expected system add-on state after setup has run.
 * @param {Array<Object>} test -  The test to run. Each test must define an updateList or test. The following
 *                                properties are used:
 *    updateList: The set of add-ons the server should respond with.
 *    test:       A function to run to perform the update check (replaces
 *                updateList)
 *    fails:      An optional regex property, if present the update check is expected to
 *                fail.
 *    finalState: An optional property, the expected final state of system add-ons,
 *                if missing the test condition's initialState is used.
 * @param {nsIFile} distroDir - the system add-on distribution directory (the "features" dir in the app directory)
 */

async function execSystemAddonTest(setupName, setup, test, distroDir) {
  // Initial system addon conditions need system signature
  AddonTestUtils.usePrivilegedSignatures = "system";
  await setupSystemAddonConditions(setup, distroDir);

  // The test may define what signature to use when running the test
  if (test.usePrivilegedSignatures != undefined) {
    AddonTestUtils.usePrivilegedSignatures = test.usePrivilegedSignatures;
  }

  function runTest() {
    if ("test" in test) {
      return test.test();
    }
    let xml = buildSystemAddonUpdates(test.updateList);
    let ids = (test.updateList || []).map(item => item.id);
    return installSystemAddons(xml, ids);
  }

  if (test.fails) {
    await Assert.rejects(runTest(), test.fails);
  } else {
    await runTest();
  }

  // some tests have a different expected combination of default
  // and updated add-ons.
  if (test.finalState && setupName in test.finalState) {
    await verifySystemAddonState(
      setup.initialState,
      test.finalState[setupName],
      false,
      distroDir
    );
  } else {
    await verifySystemAddonState(
      setup.initialState,
      undefined,
      false,
      distroDir
    );
  }

  await promiseShutdownManager();
}
