// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

"use strict";
do_get_profile(); // must be called before getting nsIX509CertDB

const { RemoteSecuritySettings } = ChromeUtils.importESModule(
  "moz-src:///security/manager/ssl/RemoteSecuritySettings.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);
const { IntermediatePreloadsClient } = RemoteSecuritySettings.init();

let server;

const INTERMEDIATES_DL_PER_POLL_PREF =
  "security.remote_settings.intermediates.downloads_per_poll";
const INTERMEDIATES_ENABLED_PREF =
  "security.remote_settings.intermediates.enabled";

function getHashCommon(aStr, useBase64) {
  let hasher = Cc["@mozilla.org/security/hash;1"].createInstance(
    Ci.nsICryptoHash
  );
  hasher.init(Ci.nsICryptoHash.SHA256);
  let stringStream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
    Ci.nsIStringInputStream
  );
  stringStream.setByteStringData(aStr);
  hasher.updateFromStream(stringStream, -1);

  return hasher.finish(useBase64);
}

// Get a hexified SHA-256 hash of the given string.
function getHash(aStr) {
  return hexify(getHashCommon(aStr, false));
}

function getSubjectBytes(certDERString) {
  let bytes = stringToArray(certDERString);
  let cert = new X509.Certificate();
  cert.parse(bytes);
  return arrayToString(cert.tbsCertificate.subject._der._bytes);
}

function getSPKIBytes(certDERString) {
  let bytes = stringToArray(certDERString);
  let cert = new X509.Certificate();
  cert.parse(bytes);
  return arrayToString(cert.tbsCertificate.subjectPublicKeyInfo._der._bytes);
}

/**
 * Simulate a Remote Settings synchronization by filling up the
 * local data with fake records.
 *
 * @param {*} filenames List of pem files for which we will create
 *                      records.
 * @param {*} options Options for records to generate.
 */
async function syncAndDownload(filenames, options = {}) {
  const {
    hashFunc = getHash,
    lengthFunc = arr => arr.length,
    clear = true,
  } = options;

  const localDB = await IntermediatePreloadsClient.client.db;
  if (clear) {
    await localDB.clear();
  }

  let count = 1;
  for (const filename of filenames) {
    const file = do_get_file(`test_intermediate_preloads/${filename}`);
    const certBytes = readFile(file);
    const certDERBytes = atob(pemToBase64(certBytes));

    const record = {
      details: {
        who: "",
        why: "",
        name: "",
        created: "",
      },
      derHash: getHashCommon(certDERBytes, true),
      subject: "",
      subjectDN: btoa(getSubjectBytes(certDERBytes)),
      attachment: {
        hash: hashFunc(certBytes),
        size: lengthFunc(certBytes),
        filename: `intermediate certificate #${count}.pem`,
        location: `security-state-workspace/intermediates/${filename}`,
        mimetype: "application/x-pem-file",
      },
      whitelist: false,
      pubKeyHash: getHashCommon(getSPKIBytes(certDERBytes), true),
      crlite_enrolled: true,
    };

    await localDB.create(record);
    count++;
  }
  // This promise will wait for the end of downloading.
  const updatedPromise = TestUtils.topicObserved(
    "remote-security-settings:intermediates-updated"
  );
  // Simulate polling for changes, trigger the download of attachments.
  Services.obs.notifyObservers(null, "remote-settings:changes-poll-end");
  const results = await updatedPromise;
  return results[1]; // topicObserved gives back a 2-array
}

/**
 * Return the list of records whose attachment was downloaded.
 */
async function locallyDownloaded() {
  return IntermediatePreloadsClient.client.get({
    filters: { cert_import_complete: true },
    syncIfEmpty: false,
  });
}

add_task(async function test_preload_empty() {
  Services.prefs.setBoolPref(INTERMEDIATES_ENABLED_PREF, true);

  let certDB = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );

  // load the first root and end entity, ignore the initial intermediate
  addCertFromFile(certDB, "test_intermediate_preloads/ca.pem", "CTu,,");

  let ee_cert = constructCertFromFile(
    "test_intermediate_preloads/default-ee.pem"
  );
  notEqual(ee_cert, null, "EE cert should have successfully loaded");

  equal(
    await syncAndDownload([]),
    "success",
    "Preloading update should have run"
  );

  equal(
    (await locallyDownloaded()).length,
    0,
    "There should have been no downloads"
  );

  // check that ee cert 1 is unknown
  await checkCertErrorGeneric(
    certDB,
    ee_cert,
    SEC_ERROR_UNKNOWN_ISSUER,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
});

add_task(async function test_preload_disabled() {
  Services.prefs.setBoolPref(INTERMEDIATES_ENABLED_PREF, false);

  equal(
    await syncAndDownload(["int.pem"]),
    "disabled",
    "Preloading update should not have run"
  );

  equal(
    (await locallyDownloaded()).length,
    0,
    "There should have been no downloads"
  );
});

add_task(async function test_preload_invalid_hash() {
  Services.prefs.setBoolPref(INTERMEDIATES_ENABLED_PREF, true);
  const invalidHash =
    "6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d";

  const result = await syncAndDownload(["int.pem"], {
    hashFunc: () => invalidHash,
  });
  equal(result, "success", "Preloading update should have run");

  equal(
    (await locallyDownloaded()).length,
    0,
    "There should be no local entry"
  );

  let certDB = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );

  // load the first root and end entity, ignore the initial intermediate
  addCertFromFile(certDB, "test_intermediate_preloads/ca.pem", "CTu,,");

  let ee_cert = constructCertFromFile(
    "test_intermediate_preloads/default-ee.pem"
  );
  notEqual(ee_cert, null, "EE cert should have successfully loaded");

  // We should still have a missing intermediate.
  await checkCertErrorGeneric(
    certDB,
    ee_cert,
    SEC_ERROR_UNKNOWN_ISSUER,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
});

add_task(async function test_preload_invalid_length() {
  Services.prefs.setBoolPref(INTERMEDIATES_ENABLED_PREF, true);

  const result = await syncAndDownload(["int.pem"], {
    lengthFunc: () => 42,
  });
  equal(result, "success", "Preloading update should have run");

  equal(
    (await locallyDownloaded()).length,
    0,
    "There should be no local entry"
  );

  let certDB = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );

  // load the first root and end entity, ignore the initial intermediate
  addCertFromFile(certDB, "test_intermediate_preloads/ca.pem", "CTu,,");

  let ee_cert = constructCertFromFile(
    "test_intermediate_preloads/default-ee.pem"
  );
  notEqual(ee_cert, null, "EE cert should have successfully loaded");

  // We should still have a missing intermediate.
  await checkCertErrorGeneric(
    certDB,
    ee_cert,
    SEC_ERROR_UNKNOWN_ISSUER,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
});

add_task(async function test_preload_basic() {
  Services.prefs.setBoolPref(INTERMEDIATES_ENABLED_PREF, true);
  Services.prefs.setIntPref(INTERMEDIATES_DL_PER_POLL_PREF, 100);

  let certDB = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );

  // load the first root and end entity, ignore the initial intermediate
  addCertFromFile(certDB, "test_intermediate_preloads/ca.pem", "CTu,,");

  let ee_cert = constructCertFromFile(
    "test_intermediate_preloads/default-ee.pem"
  );
  notEqual(ee_cert, null, "EE cert should have successfully loaded");

  // load the second end entity, ignore both intermediate and root
  let ee_cert_2 = constructCertFromFile("test_intermediate_preloads/ee2.pem");
  notEqual(ee_cert_2, null, "EE cert 2 should have successfully loaded");

  // check that the missing intermediate causes an unknown issuer error, as
  // expected, in both cases
  await checkCertErrorGeneric(
    certDB,
    ee_cert,
    SEC_ERROR_UNKNOWN_ISSUER,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
  await checkCertErrorGeneric(
    certDB,
    ee_cert_2,
    SEC_ERROR_UNKNOWN_ISSUER,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );

  let intermediateBytes = readFile(
    do_get_file("test_intermediate_preloads/int.pem")
  );
  let intermediateDERBytes = atob(pemToBase64(intermediateBytes));
  let intermediateCert = new X509.Certificate();
  intermediateCert.parse(stringToArray(intermediateDERBytes));

  const result = await syncAndDownload(["int.pem", "int2.pem"]);
  equal(result, "success", "Preloading update should have run");

  equal(
    (await locallyDownloaded()).length,
    2,
    "There should have been 2 downloads"
  );

  // check that ee cert 1 verifies now the update has happened and there is
  // an intermediate

  // First verify by connecting to a server that uses that end-entity
  // certificate but doesn't send the intermediate.
  await asyncStartTLSTestServer(
    "BadCertAndPinningServer",
    "test_intermediate_preloads",
    false
  );
  // This ensures the test server doesn't include the intermediate in the
  // handshake.
  let args = ["-D", "-n", "int"];
  run_certutil_on_directory(
    getCertDirCopy("test_intermediate_preloads").path,
    args
  );

  await checkCertErrorGeneric(
    certDB,
    ee_cert,
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );

  let localDB = await IntermediatePreloadsClient.client.db;
  let data = await localDB.list();
  ok(!!data.length, "should have some entries");
  // simulate a sync (syncAndDownload doesn't actually... sync.)
  await IntermediatePreloadsClient.client.emit("sync", {
    data: {
      current: data,
      created: data,
      deleted: [],
      updated: [],
    },
  });

  // check that ee cert 2 does not verify - since we don't know the issuer of
  // this certificate
  await checkCertErrorGeneric(
    certDB,
    ee_cert_2,
    SEC_ERROR_UNKNOWN_ISSUER,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
});

add_task(async function test_preload_200() {
  Services.prefs.setBoolPref(INTERMEDIATES_ENABLED_PREF, true);
  Services.prefs.setIntPref(INTERMEDIATES_DL_PER_POLL_PREF, 100);

  const files = [];
  for (let i = 0; i < 200; i++) {
    files.push(["int.pem", "int2.pem"][i % 2]);
  }

  let result = await syncAndDownload(files);
  equal(result, "success", "Preloading update should have run");

  equal(
    (await locallyDownloaded()).length,
    100,
    "There should have been only 100 downloaded"
  );

  // Re-run
  result = await syncAndDownload([], { clear: false });
  equal(result, "success", "Preloading update should have run");

  equal(
    (await locallyDownloaded()).length,
    200,
    "There should have been 200 downloaded"
  );
});

add_task(async function test_delete() {
  Services.prefs.setBoolPref(INTERMEDIATES_ENABLED_PREF, true);
  Services.prefs.setIntPref(INTERMEDIATES_DL_PER_POLL_PREF, 100);

  let syncResult = await syncAndDownload(["int.pem", "int2.pem"]);
  equal(syncResult, "success", "Preloading update should have run");

  equal(
    (await locallyDownloaded()).length,
    2,
    "There should have been 2 downloads"
  );

  let localDB = await IntermediatePreloadsClient.client.db;
  let data = await localDB.list();
  ok(!!data.length, "should have some entries");
  let subject = data[0].subjectDN;
  let certStorage = Cc["@mozilla.org/security/certstorage;1"].getService(
    Ci.nsICertStorage
  );
  let resultsBefore = certStorage.findCertsBySubject(
    stringToArray(atob(subject))
  );
  equal(
    resultsBefore.length,
    1,
    "should find the intermediate in cert storage before"
  );
  // simulate a sync where we deleted the entry
  await IntermediatePreloadsClient.client.emit("sync", {
    data: {
      current: [],
      created: [],
      deleted: [data[0]],
      updated: [],
    },
  });
  let resultsAfter = certStorage.findCertsBySubject(
    stringToArray(atob(subject))
  );
  equal(
    resultsAfter.length,
    0,
    "shouldn't find intermediate in cert storage now"
  );
});

add_task(async function test_bug1966632() {
  let certDB = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );

  constructCertFromFile("test_intermediate_preloads/bug1966632-int1.pem", ",,");
  await checkRootOfBuiltChain(
    certDB,
    constructCertFromFile("test_intermediate_preloads/bug1966632-ee.pem", ",,"),
    "G/ANXI8TwJTdF+AFBM8IiIUPEv0Gf6H5LA/b9guG4yE=",
    new Date("2025-05-21T00:00:00Z").getTime() / 1000,
    undefined,
    Ci.nsIX509CertDB.FLAG_LOCAL_ONLY
  );
});

function run_test() {
  server = new HttpServer();
  server.start(-1);
  registerCleanupFunction(() => server.stop(() => {}));

  server.registerDirectory(
    "/cdn/security-state-workspace/intermediates/",
    do_get_file("test_intermediate_preloads")
  );

  server.registerPathHandler("/v1/", (request, response) => {
    response.write(
      JSON.stringify({
        capabilities: {
          attachments: {
            base_url: `http://localhost:${server.identity.primaryPort}/cdn/`,
          },
        },
      })
    );
    response.setHeader("Content-Type", "application/json; charset=UTF-8");
    response.setStatusLine(null, 200, "OK");
  });

  Services.prefs.setCharPref(
    "services.settings.server",
    `http://localhost:${server.identity.primaryPort}/v1`
  );

  Services.prefs.setCharPref("browser.policies.loglevel", "debug");

  run_next_test();
}
