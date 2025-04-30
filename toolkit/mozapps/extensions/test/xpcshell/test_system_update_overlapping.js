// Tests that system add-on upgrades work.

createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "2");

// Enable SCOPE_APPLICATION for builtin testing.  Default in tests is only SCOPE_PROFILE.
let scopes = AddonManager.SCOPE_PROFILE | AddonManager.SCOPE_APPLICATION;
Services.prefs.setIntPref("extensions.enabledScopes", scopes);

let distroDir = FileUtils.getDir("ProfD", ["sysfeatures", "empty"]);
distroDir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);
registerDirectory("XREAppFeat", distroDir);
add_task(() => initSystemAddonDirs());

AddonTestUtils.usePrivilegedSignatures = () => "system";

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
 * fails:      An optional property, if true the update check is expected to
 *             fail.
 * finalState: An optional property, the expected final state of system add-ons,
 *             if missing the test condition's initialState is used.
 */
const TESTS = {
  // Tests that a set of system add-ons, some new, some existing gets installed
  overlapping: {
    // updateList is populated in setup() below
    updateList: [],
    finalState: {
      blank: [
        { isUpgrade: true, version: "2.0" },
        { isUpgrade: true, version: "2.0" },
        { isUpgrade: true, version: "3.0" },
        { isUpgrade: true, version: "1.0" },
        { isUpgrade: false, version: null },
      ],
      withAppSet: [
        { isUpgrade: true, version: "2.0" },
        { isUpgrade: true, version: "2.0" },
        { isUpgrade: true, version: "3.0" },
        { isUpgrade: true, version: "1.0" },
        { isUpgrade: false, version: null },
      ],
      withProfileSet: [
        { isUpgrade: true, version: "2.0" },
        { isUpgrade: true, version: "2.0" },
        { isUpgrade: true, version: "3.0" },
        { isUpgrade: true, version: "1.0" },
        { isUpgrade: false, version: null },
      ],
      withBothSets: [
        { isUpgrade: true, version: "2.0" },
        { isUpgrade: true, version: "2.0" },
        { isUpgrade: true, version: "3.0" },
        { isUpgrade: true, version: "1.0" },
        { isUpgrade: false, version: null },
      ],
    },
  },
};

add_task(async function setup() {
  // Initialise the profile
  await overrideBuiltIns({ system: [] });
  await promiseStartupManager();
  await promiseShutdownManager();

  let list = TESTS.overlapping.updateList;
  let xpi = await getSystemAddonXPI(1, "2.0");
  list.push({
    id: "system1@tests.mozilla.org",
    version: "2.0",
    path: "system1_2.xpi",
    xpi,
  });

  xpi = await getSystemAddonXPI(2, "2.0");
  list.push({
    id: "system2@tests.mozilla.org",
    version: "2.0",
    path: "system2_2.xpi",
    xpi,
  });

  xpi = await getSystemAddonXPI(3, "3.0");
  list.push({
    id: "system3@tests.mozilla.org",
    version: "3.0",
    path: "system3_3.xpi",
    xpi,
  });

  xpi = await getSystemAddonXPI(4, "1.0");
  list.push({
    id: "system4@tests.mozilla.org",
    version: "1.0",
    path: "system4_1.xpi",
    xpi,
  });
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
