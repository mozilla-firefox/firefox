function successCallback() {
  Assert.ok(false);
  do_test_finished();
}

function errorCallback(err) {
  // GeolocationPositionError has no interface object, so we can't get constants off that.
  Assert.equal(err.POSITION_UNAVAILABLE, err.code);
  Assert.equal(2, err.code);
  do_test_finished();
}

function run_test() {
  do_test_pending();

  if (Services.appinfo.processType == Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT) {
    // XPCShell does not get a profile by default. The geolocation service
    // depends on the settings service which uses IndexedDB and IndexedDB
    // needs a place where it can store databases.
    do_get_profile();

    Services.prefs.setBoolPref("geo.provider.network.scan", false);
    Services.prefs.setCharPref("geo.provider.network.url", "UrlNotUsedHere:");
  }

  var geolocation = Cc["@mozilla.org/geolocation;1"].getService(Ci.nsISupports);
  geolocation.getCurrentPosition(successCallback, errorCallback);
}
