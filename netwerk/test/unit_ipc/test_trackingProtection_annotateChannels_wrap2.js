const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);

function run_test() {
  do_get_profile();
  Services.prefs.setBoolPref(
    "privacy.trackingprotection.annotate_channels",
    true
  );
  Services.prefs.setBoolPref(
    "privacy.trackingprotection.lower_network_priority",
    true
  );
  do_test_pending();
  UrlClassifierTestUtils.addTestTrackers().then(() => {
    run_test_in_child(
      "../unit/test_trackingProtection_annotateChannels.js",
      () => {
        UrlClassifierTestUtils.cleanupTestTrackers().finally(do_test_finished);
      }
    );
    do_test_finished();
  });
}
