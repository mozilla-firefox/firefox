/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { Sqlite } = ChromeUtils.importESModule(
  "resource://gre/modules/Sqlite.sys.mjs"
);

const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);

/**
 * A mock toolkit profile.
 */
class MockProfile {
  #service = null;
  #storeID = null;

  constructor(service) {
    this.#service = service;
    this.name = "Testing";
    this.rootDir = Services.dirsvc.get("ProfD", Ci.nsIFile);
    this.localDir = Services.dirsvc.get("ProfD", Ci.nsIFile);
    this.#storeID = null;
    this.showProfileSelector = false;
  }

  get storeID() {
    return this.#storeID;
  }

  set storeID(val) {
    this.#storeID = val;

    if (val) {
      this.#service.groupProfile = this;
    } else {
      this.#service.groupProfile = null;
    }
  }
}

/**
 * A mock profile service to use with the selectable profile service.
 */
class MockProfileService {
  constructor() {
    this.currentProfile = new MockProfile(this);
    this.groupProfile = null;
  }

  async asyncFlush() {}

  async asyncFlushGroupProfile() {}
}

const gProfileService = new MockProfileService();

let testRoot = Services.dirsvc.get("ProfD", Ci.nsIFile);
testRoot.append(`SP${Date.now()}`);
try {
  testRoot.remove(true);
} catch (e) {
  if (e.result != Cr.NS_ERROR_FILE_NOT_FOUND) {
    console.error(e);
  }
}
try {
  testRoot.create(Ci.nsIFile.DIRECTORY_TYPE, 0o777);
} catch (e) {
  console.error(e);
}

// UAppData must be above any profile folder.
let uAppData = Services.dirsvc.get("ProfD", Ci.nsIFile).parent;
let defProfRt = testRoot.clone();
defProfRt.append("DefProfRt");
let defProflLRt = testRoot.clone();
defProflLRt.append("DefProfLRt");

ProfilesDatastoreService.overrideDirectoryService({
  UAppData: uAppData,
  DefProfRt: defProfRt,
  DefProfLRt: defProflLRt,
  ProfileGroups: testRoot.path,
});

async function openDatabase() {
  let dbFile = testRoot.clone();
  dbFile.append(`${gProfileService.currentProfile.storeID}.sqlite`);
  return Sqlite.openConnection({
    path: dbFile.path,
    openNotExclusive: true,
  });
}

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.profiles.created", false]],
  });
  await ProfilesDatastoreService.resetProfileService(gProfileService);
  await SelectableProfileService.uninit();
  await SelectableProfileService.init();

  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
    ProfilesDatastoreService.overrideDirectoryService(null);
    await ProfilesDatastoreService.resetProfileService(null);
    await ProfilesDatastoreService.uninit();
    await SelectableProfileService.uninit();
  });
});

async function initGroupDatabase() {
  await SelectableProfileService.maybeSetupDataStore();
}

// Verifies Glean probes are recorded as expected. Expects method and object
// to be passed in as snake_case, and converts to lowerCamelCase internally
// as expected by Glean code.
//
// Example 1. Basic usage.
//
// To verify
//   `Glean.profilesNew.avatar.record({value: "book"})`,
// we would call
//   `assertGlean("profiles", "new", "avatar", "book")`.
//
// Example 2. Dealing with snake_case `object`.
//
// To verify
//   `Glean.profilesNew.learnMore.record()`,
// pass in the snake-case version,
//   `assertGlean("profiles", "new", "learn_more")`
// and snake-case will be auto-converted to camelCase where needed.
//
// Example 3. Dealing with snake_case `method`.
//
// To verify
//   `Glean.profilesSelectorWindow.launch.record()`,
// pass in the snake-case version,
//   `assertGlean("profiles", "selector_window", "launch")`
// and snake-case will be auto-converted to camelCase where needed.
const assertGlean = async (category, method, object, extra) => {
  // Needed to convert 'learn_more' to 'learnMore'.
  const snakeToCamel = str =>
    str.replace(/_([a-z])/g, (_, nextChar) => nextChar.toUpperCase());

  // Converts 'profiles' and 'new' to 'profilesNew'.
  let camelMethod = snakeToCamel(method);
  let gleanFn = category + camelMethod[0].toUpperCase() + camelMethod.substr(1);

  await Services.fog.testFlushAllChildren();
  let testEvents = Glean[gleanFn][snakeToCamel(object)].testGetValue();
  Assert.equal(
    testEvents.length,
    1,
    `Should have recorded the ${category} ${method} ${object} event exactly once`
  );
  Assert.equal(
    testEvents[0].category,
    `${category}.${method}`,
    "Should have expected Glean event category"
  );
  Assert.equal(
    testEvents[0].name,
    object,
    "Should have expected Glean event name"
  );
  if (extra) {
    Assert.equal(
      testEvents[0].extra.value,
      extra,
      "Should have expected Glean extra field"
    );
  }
  TelemetryTestUtils.assertEvents([[category, method, object]], {
    category,
    method,
    object,
  });
};
