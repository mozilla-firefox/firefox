function run_test() {
  Services.prefs.setBoolPref("geo.provider.network.scan", false);

  Services.prefs.setCharPref("geo.provider.network.url", "UrlNotUsedHere");
  run_test_in_child("./test_geolocation_position_unavailable.js");
}
