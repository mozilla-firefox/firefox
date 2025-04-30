"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const {
  _ExperimentFeature: ExperimentFeature,
  ExperimentAPI,
  NimbusFeatures,
} = ChromeUtils.importESModule("resource://nimbus/ExperimentAPI.sys.mjs");
const { ExperimentFakes, ExperimentTestUtils, NimbusTestUtils } =
  ChromeUtils.importESModule(
    "resource://testing-common/NimbusTestUtils.sys.mjs"
  );

ChromeUtils.defineESModuleGetters(this, {
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  RegionTestUtils: "resource://testing-common/RegionTestUtils.sys.mjs",
});

NimbusTestUtils.init(this);

add_setup(function () {
  do_get_profile();
});

function assertEmptyStore(store) {
  NimbusTestUtils.assert.storeIsEmpty(store);
}

/**
 * Assert the manager has no active pref observers.
 */
function assertNoObservers(manager) {
  Assert.equal(
    manager._prefs.size,
    0,
    "There should be no active pref observers"
  );
  Assert.equal(
    manager._prefsBySlug.size,
    0,
    "There should be no active pref observers"
  );
  Assert.equal(
    manager._prefFlips._prefs.size,
    0,
    "There should be no prefFlips feature observers"
  );
}

/**
 * Remove all pref observers on the given ExperimentManager.
 */
function removePrefObservers(manager) {
  for (const [name, entry] of manager._prefs.entries()) {
    Services.prefs.removeObserver(name, entry.observer);
  }

  manager._prefs.clear();
  manager._prefsBySlug.clear();
}
