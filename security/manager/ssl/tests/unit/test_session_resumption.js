// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/publicdomain/zero/1.0/
"use strict";

// Tests that PSM makes the correct determination of the security status of
// loads involving session resumption (i.e. when a TLS handshake bypasses the
// AuthCertificate callback).

do_get_profile();
const certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
  Ci.nsIX509CertDB
);

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("security.OCSP.enabled");
  if (gConnectionRejector) {
    stopConnectionRejector();
  }
});

Services.prefs.setIntPref("security.OCSP.enabled", 1);

addCertFromFile(certdb, "bad_certs/evroot.pem", "CTu,,");
addCertFromFile(certdb, "bad_certs/ev-test-intermediate.pem", ",,");

const SERVER_PORT = 8888;

// For some tests we expect the OCSP responder to fail. http://localhost:8888/
// How long it takes a connection to a port with no listener to be refused is
// platform-dependent, and on Windows it is slow enough that the accumulated
// OCSP fetches push the test over the xpcshell timeout.
// Keep a socket on the port instead, so that every OCSP fetch
// that isn't served by the responder fails promptly on all platforms.
let gConnectionRejector = null;

function startConnectionRejector() {
  gConnectionRejector = Cc[
    "@mozilla.org/network/server-socket;1"
  ].createInstance(Ci.nsIServerSocket);
  gConnectionRejector.init(SERVER_PORT, true, -1);
  gConnectionRejector.asyncListen({
    onSocketAccepted(_socket, transport) {
      transport.close(Cr.NS_OK);
    },
    onStopListening() {},
  });
}

function stopConnectionRejector() {
  gConnectionRejector.close();
  gConnectionRejector = null;
}

// For expired.example.com, the platform will make a connection that will fail.
// Using information gathered at that point, an override will be added and
// another connection will be made. This connection will succeed. At that point,
// as long as the session cache isn't cleared, subsequent new connections should
// use session resumption, thereby bypassing the AuthCertificate hook. We need
// to ensure that the correct security state is propagated to the new connection
// information object.
function add_resume_non_ev_with_override_test() {
  // This adds the override and makes one successful connection.
  add_cert_override_test("expired.example.com", SEC_ERROR_EXPIRED_CERTIFICATE);

  // This connects again, using session resumption. Note that we don't clear
  // the TLS session cache between these operations (that would defeat the
  // purpose).
  add_connection_test(
    "expired.example.com",
    PRErrorCodeSuccess,
    null,
    transportSecurityInfo => {
      ok(transportSecurityInfo.resumed, "connection should be resumed");
      ok(
        transportSecurityInfo.securityState &
          Ci.nsIWebProgressListener.STATE_CERT_USER_OVERRIDDEN,
        "expired.example.com should have STATE_CERT_USER_OVERRIDDEN flag"
      );
      equal(
        transportSecurityInfo.succeededCertChain.length,
        0,
        "expired.example.com should not have succeededCertChain set"
      );
      equal(
        transportSecurityInfo.handshakeCertificates.length,
        2,
        "expired.example.com should have handshakeCertificates set"
      );
      equal(
        transportSecurityInfo.overridableErrorCategory,
        Ci.nsITransportSecurityInfo.ERROR_TIME,
        "expired.example.com should have time overridable error category"
      );
      ok(
        !transportSecurityInfo.isExtendedValidation,
        "expired.example.com should not have isExtendedValidation set"
      );

      let certOverrideService = Cc[
        "@mozilla.org/security/certoverride;1"
      ].getService(Ci.nsICertOverrideService);
      certOverrideService.clearValidityOverride(
        "expired.example.com",
        8443,
        {}
      );
    }
  );
}

// Helper function that adds a test that connects to ev-test.example.com and
// verifies that it validates as EV (or not, if we're running a non-debug
// build). This assumes that an appropriate OCSP responder is running or that
// good responses are cached.
function add_one_ev_test(resumed) {
  add_connection_test(
    "ev-test.example.com",
    PRErrorCodeSuccess,
    null,
    transportSecurityInfo => {
      equal(
        transportSecurityInfo.resumed,
        resumed,
        "connection should be resumed or not resumed as expected"
      );
      ok(
        !(
          transportSecurityInfo.securityState &
          Ci.nsIWebProgressListener.STATE_CERT_USER_OVERRIDDEN
        ),
        "ev-test.example.com should not have STATE_CERT_USER_OVERRIDDEN flag"
      );
      equal(
        transportSecurityInfo.succeededCertChain.length,
        3,
        "ev-test.example.com should have succeededCertChain set"
      );
      equal(
        transportSecurityInfo.handshakeCertificates.length,
        2,
        "ev-test.example.com should have handshakeCertificates set"
      );
      equal(
        transportSecurityInfo.overridableErrorCategory,
        Ci.nsITransportSecurityInfo.ERROR_UNSET,
        "ev-test.example.com should not have an overridable error category"
      );
      ok(
        !gEVExpected || transportSecurityInfo.isExtendedValidation,
        "ev-test.example.com should have isExtendedValidation set " +
          "(or this is a non-debug build)"
      );
    }
  );
}

// This test is similar, except with extended validation. We should connect
// successfully, and the certificate should be EV in debug builds. Without
// clearing the session cache, we should connect successfully again, this time
// with session resumption. The certificate should again be EV in debug builds.
function add_resume_ev_test() {
  let expectedRequestPaths = ["ev-test"];
  let responseTypes = ["good"];
  // Since we cache OCSP responses, we only ever actually serve one set.
  let ocspResponder;
  // If we don't wrap this in an `add_test`, the OCSP responder will be running
  // while we are actually running unrelated testcases, which can disrupt them.
  add_test(() => {
    stopConnectionRejector();
    ocspResponder = startOCSPResponder(
      SERVER_PORT,
      "localhost",
      "bad_certs",
      expectedRequestPaths,
      expectedRequestPaths.slice(),
      null,
      responseTypes
    );
    run_next_test();
  });
  // We should be able to connect and verify the certificate as EV (in debug
  // builds).
  add_one_ev_test(false);
  // We should be able to connect again (using session resumption). In debug
  // builds, the certificate should be noted as EV. Again, it's important that
  // nothing clears the TLS cache in between these two operations.
  add_one_ev_test(true);

  add_test(() => {
    ocspResponder.stop(() => {
      startConnectionRejector();
      run_next_test();
    });
  });
}

const GOOD_DOMAIN = "good.include-subdomains.pinning.example.com";

// Helper function that adds a test that connects to a domain that should
// succeed (but isn't EV) and verifies that its succeededCertChain gets set
// appropriately.
function add_one_non_ev_test() {
  add_connection_test(
    GOOD_DOMAIN,
    PRErrorCodeSuccess,
    null,
    transportSecurityInfo => {
      ok(
        !(
          transportSecurityInfo.securityState &
          Ci.nsIWebProgressListener.STATE_CERT_USER_OVERRIDDEN
        ),
        `${GOOD_DOMAIN} should not have STATE_CERT_USER_OVERRIDDEN flag`
      );
      ok(
        transportSecurityInfo.succeededCertChain,
        `${GOOD_DOMAIN} should have succeededCertChain set`
      );
      equal(
        transportSecurityInfo.overridableErrorCategory,
        0,
        `${GOOD_DOMAIN} should not have an overridable error category set`
      );
      ok(
        !transportSecurityInfo.isExtendedValidation,
        `${GOOD_DOMAIN} should not have isExtendedValidation set`
      );
    }
  );
}

// This test is similar, except with non-extended validation. We should connect
// successfully, and the certificate should not be EV. Without clearing the
// session cache, we should connect successfully again, this time with session
// resumption. In this case, though, we want to ensure the succeededCertChain is
// set.
function add_resume_non_ev_test() {
  add_one_non_ev_test();
  add_one_non_ev_test();
}

const statsPtr = getSSLStatistics();
const toInt32 = ctypes.Int64.lo;

// Connect to the same domain with two origin attributes and check if any ssl
// session is resumed.
function add_origin_attributes_test(
  originAttributes1,
  originAttributes2,
  resumeExpected
) {
  add_connection_test(
    GOOD_DOMAIN,
    PRErrorCodeSuccess,
    clearSessionCache,
    null,
    null,
    originAttributes1
  );

  let hitsBeforeConnect;
  let missesBeforeConnect;
  let expectedHits = resumeExpected ? 1 : 0;
  let expectedMisses = 1 - expectedHits;

  add_connection_test(
    GOOD_DOMAIN,
    PRErrorCodeSuccess,
    function () {
      // Add the hits and misses before connection.
      let stats = statsPtr.contents;
      hitsBeforeConnect = toInt32(stats.sch_sid_cache_hits);
      missesBeforeConnect = toInt32(stats.sch_sid_cache_misses);
    },
    function () {
      let stats = statsPtr.contents;
      equal(
        toInt32(stats.sch_sid_cache_hits),
        hitsBeforeConnect + expectedHits,
        "Unexpected cache hits"
      );
      equal(
        toInt32(stats.sch_sid_cache_misses),
        missesBeforeConnect + expectedMisses,
        "Unexpected cache misses"
      );
    },
    null,
    originAttributes2
  );
}

function add_resumption_tests() {
  add_resume_ev_test();
  add_resume_non_ev_test();
  add_resume_non_ev_with_override_test();
  add_origin_attributes_test({}, {}, true);
  add_origin_attributes_test({ userContextId: 1 }, { userContextId: 2 }, false);
  add_origin_attributes_test({ userContextId: 3 }, { userContextId: 3 }, true);
  add_origin_attributes_test(
    { firstPartyDomain: "foo.com" },
    { firstPartyDomain: "bar.com" },
    false
  );
  add_origin_attributes_test(
    { firstPartyDomain: "baz.com" },
    { firstPartyDomain: "baz.com" },
    true
  );
}

function run_test() {
  add_tls_server_setup("BadCertAndPinningServer", "bad_certs");
  add_test(() => {
    startConnectionRejector();
    run_next_test();
  });
  add_resumption_tests();
  run_next_test();
}
