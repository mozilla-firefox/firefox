// Tests that system add-on upgrades fail to upgrade in expected cases.

createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "2");

// Enable SCOPE_APPLICATION for builtin testing.  Default in tests is only SCOPE_PROFILE.
let scopes = AddonManager.SCOPE_PROFILE | AddonManager.SCOPE_APPLICATION;
Services.prefs.setIntPref("extensions.enabledScopes", scopes);

let distroDir = FileUtils.getDir("ProfD", ["sysfeatures", "empty"]);
distroDir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);
registerDirectory("XREAppFeat", distroDir);

/**
 * Defines the set of initial conditions to run each test against. Each should
 * define the following properties:
 *
 * setup:        A task to setup the profile into the initial state.
 * initialState: The initial expected system add-on state after setup has run.
 */
const TEST_CONDITIONS = {
  // Runs tests with no updated or default system add-ons initially installed
  blank: {
    setup() {
      clearSystemAddonUpdatesDir();
      distroDir.leafName = "empty";
    },
    initialState: [
      { isUpgrade: false, version: null },
      { isUpgrade: false, version: null },
      { isUpgrade: false, version: null },
      { isUpgrade: false, version: null },
      { isUpgrade: false, version: null },
    ],
  },
  // Runs tests with default system add-ons installed
  withAppSet: {
    setup() {
      clearSystemAddonUpdatesDir();
      distroDir.leafName = "prefilled";
    },
    initialState: [
      { isUpgrade: false, version: null },
      { isUpgrade: false, version: "2.0" },
      { isUpgrade: false, version: "2.0" },
      { isUpgrade: false, version: null },
      { isUpgrade: false, version: null },
    ],
  },

  // Runs tests with updated system add-ons installed
  withProfileSet: {
    async setup() {
      await buildPrefilledUpdatesDir();
      distroDir.leafName = "empty";
    },
    initialState: [
      { isUpgrade: false, version: null },
      { isUpgrade: true, version: "2.0" },
      { isUpgrade: true, version: "2.0" },
      { isUpgrade: false, version: null },
      { isUpgrade: false, version: null },
    ],
  },

  // Runs tests with both default and updated system add-ons installed
  withBothSets: {
    async setup() {
      await buildPrefilledUpdatesDir();
      distroDir.leafName = "hidden";
    },
    initialState: [
      { isUpgrade: false, version: "1.0" },
      { isUpgrade: true, version: "2.0" },
      { isUpgrade: true, version: "2.0" },
      { isUpgrade: false, version: null },
      { isUpgrade: false, version: null },
    ],
  },
};

/**
 * The tests to run. Each test must define an updateList or test. The following
 * properties are used:
 *
 * updateList: The set of add-ons the server should respond with.
 * test:       A function to run to perform the update check (replaces
 *             updateList)
 * fails:      regex to test error in Assert.rejects.
 * finalState: An optional property, the expected final state of system add-ons,
 *             if missing the test condition's initialState is used.
 */
const TESTS = {
  // Specifying an incorrect version should stop us updating anything
  badVersion: {
    fails:
      /Error: Rejecting updated system add-on set that either could not be downloaded or contained unusable add-ons./,
    updateList: [
      {
        id: "system2@tests.mozilla.org",
        version: "4.0",
        path: "system2_3.xpi",
      },
      {
        id: "system3@tests.mozilla.org",
        version: "3.0",
        path: "system3_3.xpi",
      },
    ],
  },

  // Specifying an invalid size should stop us updating anything
  badSize: {
    fails:
      /Error: Rejecting updated system add-on set that either could not be downloaded or contained unusable add-ons./,
    updateList: [
      {
        id: "system2@tests.mozilla.org",
        version: "3.0",
        path: "system2_3.xpi",
        size: 2,
      },
      {
        id: "system3@tests.mozilla.org",
        version: "3.0",
        path: "system3_3.xpi",
      },
    ],
  },

  // Specifying an incorrect hash should stop us updating anything
  badHash: {
    fails:
      /Error: Rejecting updated system add-on set that either could not be downloaded or contained unusable add-ons./,
    updateList: [
      {
        id: "system2@tests.mozilla.org",
        version: "3.0",
        path: "system2_3.xpi",
      },
      {
        id: "system3@tests.mozilla.org",
        version: "3.0",
        path: "system3_3.xpi",
        hashFunction: "sha256",
        hashValue: "205a4c49bd513ebd30594e380c19e86bba1f83e2",
      },
    ],
  },

  // A bad certificate should stop updates
  badCert: {
    fails:
      /Error: Rejecting updated system add-on set that either could not be downloaded or contained unusable add-ons./,
    // true is not system addon signed
    usePrivilegedSignatures: true,
    updateList: [
      {
        id: "system1@tests.mozilla.org",
        version: "1.0",
        path: "system1_1_badcert.xpi",
      },
      {
        id: "system3@tests.mozilla.org",
        version: "1.0",
        path: "system3_1.xpi",
      },
    ],
  },
};

add_setup(async function setup() {
  // Initialise the profile
  initSystemAddonDirs();
  await overrideBuiltIns({ system: [], builtins: [] });
  await promiseStartupManager();
  await promiseShutdownManager();
});

add_task(async function () {
  for (let setupName of Object.keys(TEST_CONDITIONS)) {
    for (let testName of Object.keys(TESTS)) {
      info("Running test " + setupName + " " + testName);

      let setup = TEST_CONDITIONS[setupName];
      let test = TESTS[testName];

      await execSystemAddonTest(setupName, setup, test, distroDir);
    }
  }
});
