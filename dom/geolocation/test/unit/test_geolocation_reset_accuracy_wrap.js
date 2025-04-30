const providerCID = Components.ID("{14aa4b81-e266-45cb-88f8-89595dece114}");
const providerContract = "@mozilla.org/geolocation/provider;1";

const categoryName = "geolocation-provider";

var provider = {
  QueryInterface: ChromeUtils.generateQI([
    "nsIFactory",
    "nsIGeolocationProvider",
  ]),
  createInstance: function eventsink_ci(iid) {
    return this.QueryInterface(iid);
  },
  startup() {},
  watch() {},
  shutdown() {},
  setHighAccuracy(enable) {
    this._isHigh = enable;
    if (enable) {
      this._seenHigh = true;
      do_send_remote_message("high_acc_enabled");
    }
  },
  _isHigh: false,
  _seenHigh: false,
};

function run_test() {
  // XPCShell does not get a profile by default. The geolocation service
  // depends on the settings service which uses IndexedDB and IndexedDB
  // needs a place where it can store databases.
  do_get_profile();

  Components.manager.nsIComponentRegistrar.registerFactory(
    providerCID,
    "Unit test geo provider",
    providerContract,
    provider
  );

  Services.catMan.addCategoryEntry(
    categoryName,
    "unit test",
    providerContract,
    false,
    true
  );

  Services.prefs.setBoolPref("geo.provider.network.scan", false);

  run_test_in_child("test_geolocation_reset_accuracy.js", check_results);
}

function check_results() {
  // check the provider was set to high accuracy during the test
  Assert.ok(provider._seenHigh);
  // check the provider is not currently set to high accuracy
  Assert.ok(!provider._isHigh);

  do_test_finished();
}
