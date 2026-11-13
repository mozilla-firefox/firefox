/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

const xpi = RELATIVE_DIR + "addons/browser_installssl.xpi";
const redirect = RELATIVE_DIR + "redirect.sjs?";
const SUCCESS = 0;
const NETWORK_FAILURE = AddonManager.ERROR_NETWORK_FAILURE;

const HTTP = "http://example.com/";
const HTTPS = "https://example.com/";
const NOCERT = "https://nocert.example.com/";
const SELFSIGNED = "https://self-signed.example.com/";
const UNTRUSTED = "https://untrusted.example.com/";
const EXPIRED = "https://expired.example.com/";

const PREF_INSTALL_REQUIREBUILTINCERTS =
  "extensions.install.requireBuiltInCerts";

var gTests = [];
var gStart = 0;
var gLast = 0;
var gPendingInstall = null;

function test() {
  gStart = Date.now();
  requestLongerTimeout(4);
  waitForExplicitFinish();

  registerCleanupFunction(function () {
    var cos = Cc["@mozilla.org/security/certoverride;1"].getService(
      Ci.nsICertOverrideService
    );
    cos.clearValidityOverride("nocert.example.com", -1, {});
    cos.clearValidityOverride("self-signed.example.com", -1, {});
    cos.clearValidityOverride("untrusted.example.com", -1, {});
    cos.clearValidityOverride("expired.example.com", -1, {});

    if (gPendingInstall) {
      gTests = [];
      ok(
        false,
        "Timed out in the middle of downloading " +
          gPendingInstall.sourceURI.spec
      );
      try {
        gPendingInstall.cancel();
      } catch (e) {}
    }
  });

  run_next_test();
}

function end_test() {
  info("All tests completed in " + (Date.now() - gStart) + "ms");
  finish();
}

function add_install_test(mainURL, redirectURL, expectedStatus) {
  gTests.push([mainURL, redirectURL, expectedStatus]);
}

function run_install_tests(callback) {
  async function run_next_install_test() {
    if (!gTests.length) {
      callback();
      return;
    }
    gLast = Date.now();

    let [mainURL, redirectURL, expectedStatus] = gTests.shift();
    if (redirectURL) {
      var url = mainURL + redirect + redirectURL + xpi;
      var message =
        "Should have seen the right result for an install redirected from " +
        mainURL +
        " to " +
        redirectURL;
    } else {
      url = mainURL + xpi;
      message =
        "Should have seen the right result for an install from " + mainURL;
    }

    let install = await AddonManager.getInstallForURL(url, {
      id: "sslinstall-1@tests.mozilla.org",
      type: "extension",
      version: "1.0",
    });
    gPendingInstall = install;
    install.addListener({
      onDownloadEnded(install) {
        is(SUCCESS, expectedStatus, message);
        info("Install test ran in " + (Date.now() - gLast) + "ms");
        // Don't proceed with the install
        install.cancel();
        gPendingInstall = null;
        run_next_install_test();
        return false;
      },

      onDownloadFailed(install) {
        is(install.error, expectedStatus, message);
        info("Install test ran in " + (Date.now() - gLast) + "ms");
        gPendingInstall = null;
        run_next_install_test();
      },
    });
    install.install();
  }

  run_next_install_test();
}

// Runs tests with built-in certificates required, no certificate exceptions
// and no hashes
add_test(async function test_builtin_required() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_INSTALL_REQUIREBUILTINCERTS, true]],
  });
  // Tests that a simple install works as expected.
  add_install_test(HTTP, null, SUCCESS);
  add_install_test(HTTPS, null, NETWORK_FAILURE);
  add_install_test(NOCERT, null, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, null, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, null, NETWORK_FAILURE);
  add_install_test(EXPIRED, null, NETWORK_FAILURE);

  // Tests that redirecting from http to other servers works as expected
  add_install_test(HTTP, HTTP, SUCCESS);
  add_install_test(HTTP, HTTPS, SUCCESS);
  add_install_test(HTTP, NOCERT, NETWORK_FAILURE);
  add_install_test(HTTP, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(HTTP, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(HTTP, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from valid https to other servers works as expected
  add_install_test(HTTPS, HTTP, NETWORK_FAILURE);
  add_install_test(HTTPS, HTTPS, NETWORK_FAILURE);
  add_install_test(HTTPS, NOCERT, NETWORK_FAILURE);
  add_install_test(HTTPS, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(HTTPS, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(HTTPS, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from nocert https to other servers works as expected
  add_install_test(NOCERT, HTTP, NETWORK_FAILURE);
  add_install_test(NOCERT, HTTPS, NETWORK_FAILURE);
  add_install_test(NOCERT, NOCERT, NETWORK_FAILURE);
  add_install_test(NOCERT, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(NOCERT, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(NOCERT, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from self-signed https to other servers works as expected
  add_install_test(SELFSIGNED, HTTP, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, HTTPS, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, NOCERT, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from untrusted https to other servers works as expected
  add_install_test(UNTRUSTED, HTTP, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, HTTPS, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, NOCERT, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from expired https to other servers works as expected
  add_install_test(EXPIRED, HTTP, NETWORK_FAILURE);
  add_install_test(EXPIRED, HTTPS, NETWORK_FAILURE);
  add_install_test(EXPIRED, NOCERT, NETWORK_FAILURE);
  add_install_test(EXPIRED, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(EXPIRED, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(EXPIRED, EXPIRED, NETWORK_FAILURE);

  run_install_tests(run_next_test);
});

// Runs tests without requiring built-in certificates, no certificate
// exceptions and no hashes
add_test(async function test_builtin_not_required() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_INSTALL_REQUIREBUILTINCERTS, false]],
  });

  // Tests that a simple install works as expected.
  add_install_test(HTTP, null, SUCCESS);
  add_install_test(HTTPS, null, SUCCESS);
  add_install_test(NOCERT, null, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, null, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, null, NETWORK_FAILURE);
  add_install_test(EXPIRED, null, NETWORK_FAILURE);

  // Tests that redirecting from http to other servers works as expected
  add_install_test(HTTP, HTTP, SUCCESS);
  add_install_test(HTTP, HTTPS, SUCCESS);
  add_install_test(HTTP, NOCERT, NETWORK_FAILURE);
  add_install_test(HTTP, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(HTTP, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(HTTP, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from valid https to other servers works as expected
  add_install_test(HTTPS, HTTP, NETWORK_FAILURE);
  add_install_test(HTTPS, HTTPS, SUCCESS);
  add_install_test(HTTPS, NOCERT, NETWORK_FAILURE);
  add_install_test(HTTPS, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(HTTPS, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(HTTPS, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from nocert https to other servers works as expected
  add_install_test(NOCERT, HTTP, NETWORK_FAILURE);
  add_install_test(NOCERT, HTTPS, NETWORK_FAILURE);
  add_install_test(NOCERT, NOCERT, NETWORK_FAILURE);
  add_install_test(NOCERT, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(NOCERT, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(NOCERT, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from self-signed https to other servers works as expected
  add_install_test(SELFSIGNED, HTTP, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, HTTPS, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, NOCERT, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from untrusted https to other servers works as expected
  add_install_test(UNTRUSTED, HTTP, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, HTTPS, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, NOCERT, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from expired https to other servers works as expected
  add_install_test(EXPIRED, HTTP, NETWORK_FAILURE);
  add_install_test(EXPIRED, HTTPS, NETWORK_FAILURE);
  add_install_test(EXPIRED, NOCERT, NETWORK_FAILURE);
  add_install_test(EXPIRED, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(EXPIRED, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(EXPIRED, EXPIRED, NETWORK_FAILURE);

  run_install_tests(run_next_test);
});

// Set up overrides for the next test.
add_test(() => {
  addCertOverrides().then(run_next_test);
});

// Runs tests with built-in certificates required, all certificate exceptions
// and no hashes
add_test(async function test_builtin_required_overrides() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_INSTALL_REQUIREBUILTINCERTS, true]],
  });

  // Tests that a simple install works as expected.
  add_install_test(HTTP, null, SUCCESS);
  add_install_test(HTTPS, null, NETWORK_FAILURE);
  add_install_test(NOCERT, null, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, null, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, null, NETWORK_FAILURE);
  add_install_test(EXPIRED, null, NETWORK_FAILURE);

  // Tests that redirecting from http to other servers works as expected
  add_install_test(HTTP, HTTP, SUCCESS);
  add_install_test(HTTP, HTTPS, SUCCESS);
  add_install_test(HTTP, NOCERT, SUCCESS);
  add_install_test(HTTP, SELFSIGNED, SUCCESS);
  add_install_test(HTTP, UNTRUSTED, SUCCESS);
  add_install_test(HTTP, EXPIRED, SUCCESS);

  // Tests that redirecting from valid https to other servers works as expected
  add_install_test(HTTPS, HTTP, NETWORK_FAILURE);
  add_install_test(HTTPS, HTTPS, NETWORK_FAILURE);
  add_install_test(HTTPS, NOCERT, NETWORK_FAILURE);
  add_install_test(HTTPS, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(HTTPS, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(HTTPS, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from nocert https to other servers works as expected
  add_install_test(NOCERT, HTTP, NETWORK_FAILURE);
  add_install_test(NOCERT, HTTPS, NETWORK_FAILURE);
  add_install_test(NOCERT, NOCERT, NETWORK_FAILURE);
  add_install_test(NOCERT, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(NOCERT, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(NOCERT, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from self-signed https to other servers works as expected
  add_install_test(SELFSIGNED, HTTP, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, HTTPS, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, NOCERT, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from untrusted https to other servers works as expected
  add_install_test(UNTRUSTED, HTTP, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, HTTPS, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, NOCERT, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, EXPIRED, NETWORK_FAILURE);

  // Tests that redirecting from expired https to other servers works as expected
  add_install_test(EXPIRED, HTTP, NETWORK_FAILURE);
  add_install_test(EXPIRED, HTTPS, NETWORK_FAILURE);
  add_install_test(EXPIRED, NOCERT, NETWORK_FAILURE);
  add_install_test(EXPIRED, SELFSIGNED, NETWORK_FAILURE);
  add_install_test(EXPIRED, UNTRUSTED, NETWORK_FAILURE);
  add_install_test(EXPIRED, EXPIRED, NETWORK_FAILURE);

  run_install_tests(run_next_test);
});

// Runs tests without requiring built-in certificates, all certificate
// exceptions and no hashes
add_test(async function test_builtin_not_required_overrides() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_INSTALL_REQUIREBUILTINCERTS, false]],
  });

  // Tests that a simple install works as expected.
  add_install_test(HTTP, null, SUCCESS);
  add_install_test(HTTPS, null, SUCCESS);
  add_install_test(NOCERT, null, SUCCESS);
  add_install_test(SELFSIGNED, null, SUCCESS);
  add_install_test(UNTRUSTED, null, SUCCESS);
  add_install_test(EXPIRED, null, SUCCESS);

  // Tests that redirecting from http to other servers works as expected
  add_install_test(HTTP, HTTP, SUCCESS);
  add_install_test(HTTP, HTTPS, SUCCESS);
  add_install_test(HTTP, NOCERT, SUCCESS);
  add_install_test(HTTP, SELFSIGNED, SUCCESS);
  add_install_test(HTTP, UNTRUSTED, SUCCESS);
  add_install_test(HTTP, EXPIRED, SUCCESS);

  // Tests that redirecting from valid https to other servers works as expected
  add_install_test(HTTPS, HTTP, NETWORK_FAILURE);
  add_install_test(HTTPS, HTTPS, SUCCESS);
  add_install_test(HTTPS, NOCERT, SUCCESS);
  add_install_test(HTTPS, SELFSIGNED, SUCCESS);
  add_install_test(HTTPS, UNTRUSTED, SUCCESS);
  add_install_test(HTTPS, EXPIRED, SUCCESS);

  // Tests that redirecting from nocert https to other servers works as expected
  add_install_test(NOCERT, HTTP, NETWORK_FAILURE);
  add_install_test(NOCERT, HTTPS, SUCCESS);
  add_install_test(NOCERT, NOCERT, SUCCESS);
  add_install_test(NOCERT, SELFSIGNED, SUCCESS);
  add_install_test(NOCERT, UNTRUSTED, SUCCESS);
  add_install_test(NOCERT, EXPIRED, SUCCESS);

  // Tests that redirecting from self-signed https to other servers works as expected
  add_install_test(SELFSIGNED, HTTP, NETWORK_FAILURE);
  add_install_test(SELFSIGNED, HTTPS, SUCCESS);
  add_install_test(SELFSIGNED, NOCERT, SUCCESS);
  add_install_test(SELFSIGNED, SELFSIGNED, SUCCESS);
  add_install_test(SELFSIGNED, UNTRUSTED, SUCCESS);
  add_install_test(SELFSIGNED, EXPIRED, SUCCESS);

  // Tests that redirecting from untrusted https to other servers works as expected
  add_install_test(UNTRUSTED, HTTP, NETWORK_FAILURE);
  add_install_test(UNTRUSTED, HTTPS, SUCCESS);
  add_install_test(UNTRUSTED, NOCERT, SUCCESS);
  add_install_test(UNTRUSTED, SELFSIGNED, SUCCESS);
  add_install_test(UNTRUSTED, UNTRUSTED, SUCCESS);
  add_install_test(UNTRUSTED, EXPIRED, SUCCESS);

  // Tests that redirecting from expired https to other servers works as expected
  add_install_test(EXPIRED, HTTP, NETWORK_FAILURE);
  add_install_test(EXPIRED, HTTPS, SUCCESS);
  add_install_test(EXPIRED, NOCERT, SUCCESS);
  add_install_test(EXPIRED, SELFSIGNED, SUCCESS);
  add_install_test(EXPIRED, UNTRUSTED, SUCCESS);
  add_install_test(EXPIRED, EXPIRED, SUCCESS);

  run_install_tests(run_next_test);
});
