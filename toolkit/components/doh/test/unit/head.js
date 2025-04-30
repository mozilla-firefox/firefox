/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

const { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

let h2Port, trrServer1, trrServer2, trrList;
let DNSLookup, LookupAggregator, TRRRacer;

function readFile(file) {
  let fstream = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsIFileInputStream
  );
  fstream.init(file, -1, 0, 0);
  let data = NetUtil.readInputStreamToString(fstream, fstream.available());
  fstream.close();
  return data;
}

function addCertFromFile(certdb, filename, trustString) {
  let certFile = do_get_file(filename, false);
  let pem = readFile(certFile)
    .replace(/-----BEGIN CERTIFICATE-----/, "")
    .replace(/-----END CERTIFICATE-----/, "")
    .replace(/[\r\n]/g, "");
  certdb.addCertFromBase64(pem, trustString);
}

async function ensureNoTelemetry() {
  let events =
    await Glean.securityDohTrrPerformance.resolvedRecord.testGetValue();
  Assert.ok(
    !events ||
      !events.filter(e => e.category == "security.doh.trr_performance").length
  );
}

function setup() {
  h2Port = Services.env.get("MOZHTTP2_PORT");
  Assert.notEqual(h2Port, null);
  Assert.notEqual(h2Port, "");

  // Set to allow the cert presented by our H2 server
  do_get_profile();

  Services.prefs.setBoolPref("network.http.http2.enabled", true);

  // use the h2 server as DOH provider
  trrServer1 = `https://foo.example.com:${h2Port}/doh?responseIP=1.1.1.1`;
  trrServer2 = `https://foo.example.com:${h2Port}/doh?responseIP=2.2.2.2`;
  trrList = [trrServer1, trrServer2];
  // make all native resolve calls "secretly" resolve localhost instead
  Services.prefs.setBoolPref("network.dns.native-is-localhost", true);

  // The moz-http2 cert is for foo.example.com and is signed by http2-ca.pem
  // so add that cert to the trust list as a signing cert.  // the foo.example.com domain name.
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");

  Services.prefs.setIntPref("doh-rollout.trrRace.randomSubdomainCount", 2);

  Services.prefs.setCharPref(
    "doh-rollout.trrRace.popularDomains",
    "foo.example.com., bar.example.com."
  );

  Services.prefs.setCharPref(
    "doh-rollout.trrRace.canonicalDomain",
    "firefox-dns-perf-test.net."
  );

  let TRRPerformance = ChromeUtils.importESModule(
    "resource://gre/modules/TRRPerformance.sys.mjs"
  );

  DNSLookup = TRRPerformance.DNSLookup;
  LookupAggregator = TRRPerformance.LookupAggregator;
  TRRRacer = TRRPerformance.TRRRacer;

  let oldCanRecord = Services.telemetry.canRecordExtended;
  Services.telemetry.canRecordExtended = true;

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.http.http2.enabled");
    Services.prefs.clearUserPref("network.dns.native-is-localhost");

    Services.telemetry.canRecordExtended = oldCanRecord;
  });
}
