// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Tests that CRLite filter downloading works correctly.
//
// The test filters were generated test_crlite_filters/make_filters.sh which
// uses the rust-create-cascade program from https://github.com/mozilla/crlite.

"use strict";
do_get_profile(); // must be called before getting nsIX509CertDB

const { RemoteSecuritySettings } = ChromeUtils.importESModule(
  "moz-src:///security/manager/ssl/RemoteSecuritySettings.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const { CRLiteFiltersClient } = RemoteSecuritySettings.init();

const CRLITE_FILTERS_ENABLED_PREF =
  "security.remote_settings.crlite_filters.enabled";
const CRLITE_FILTER_CHANNEL_PREF = "security.pki.crlite_channel";
const CRLITE_MODE_PREF = "security.pki.crlite_mode";
const CRLITE_TIMESTAMPS_FOR_COVERAGE_PREF =
  "security.pki.crlite_timestamps_for_coverage";
const CERTIFICATE_TRANSPARENCY_MODE_PREF =
  "security.pki.certificate_transparency.mode";
const INTERMEDIATES_ENABLED_PREF =
  "security.remote_settings.intermediates.enabled";
const INTERMEDIATES_DL_PER_POLL_PREF =
  "security.remote_settings.intermediates.downloads_per_poll";
const OCSP_ENABLED_PREF = "security.OCSP.enabled";
const OCSP_REQUIRED_PREF = "security.OCSP.require";
const BUILTIN_ROOT_HASH_PREF = "security.test.built_in_root_hash";
const CHECK_AT_TIME = new Date("2020-01-01T00:00:00Z").getTime() / 1000;

var gOCSPRequestCount = 0;

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

// Get the name of the source file in the test directory for the given filter type.
function getSourceFilenameForFilter(filter) {
  return filter.type == "filter"
    ? "20200101-0-filter"
    : "20200101-1-filter.delta";
}

// Get the attachment filename for the given filter. Each delta record gets a
// unique filename (keyed by id) so that isFilterInstalled can distinguish between
// them, mirroring how production records have unique filenames per update step.
function getAttachmentFilenameForFilter(filter) {
  if (filter.type == "filter") {
    return "20200101-0-filter";
  }
  return `20200101-${filter.id}-filter.delta`;
}

/**
 * Simulate a Remote Settings synchronization by filling up the local data with
 * fake records.
 *
 * @param {*} filters List of filters for which we will create records.
 * @param {boolean} clear Whether or not to clear the local DB first. Defaults
 *                        to true.
 */
async function syncAndDownload(filters, clear = true, channel = undefined) {
  const localDB = await CRLiteFiltersClient.client.db;
  if (clear) {
    await localDB.clear();
  }

  channel =
    typeof channel === "undefined"
      ? Services.prefs.getStringPref(CRLITE_FILTER_CHANNEL_PREF)
      : channel;

  for (let filter of filters) {
    const sourceFilename = getSourceFilenameForFilter(filter);
    const attachmentFilename = getAttachmentFilenameForFilter(filter);
    const file = do_get_file(`test_crlite_filters/${sourceFilename}`);
    const fileBytes = readFile(file);

    const incremental = filter.type == "diff";

    const record = {
      details: {
        name: `${filter.timestamp}-${filter.type}`,
      },
      attachment: {
        hash: getHash(fileBytes),
        size: fileBytes.length,
        filename: attachmentFilename,
        location: `security-state-workspace/cert-revocations/test_crlite_filters/${sourceFilename}`,
        mimetype: "application/octet-stream",
      },
      incremental,
      effectiveTimestamp: new Date(filter.timestamp).getTime(),
      parent: incremental ? filter.parent : undefined,
      id: filter.id,
      channel: `${channel}`,
      filter_expression: `'${channel}' == '${CRLITE_FILTER_CHANNEL_PREF}'|preferenceValue('none')`,
    };

    await localDB.create(record);
  }
  // This promise will wait for the end of installation.
  let promise = TestUtils.topicObserved(
    "remote-security-settings:crlite-filters-updated"
  );
  // Simulate polling for changes, trigger the download of attachments.
  Services.obs.notifyObservers(null, "remote-settings:changes-poll-end");
  let results = await promise;
  return results[1]; // topicObserved gives back a 2-array
}

function expectInstalled(result, expected) {
  let [status, installed] = result.split(";");
  equal(status, "finished", "CRLite filter installation should have run");
  deepEqual(
    installed.split(","),
    expected.length ? expected : [""],
    "Should have installed the expected CRLite filters"
  );
}

function expectDownloaded(result, expected) {
  let [status, , downloaded] = result.split(";");
  equal(status, "finished", "CRLite filter installation should have run");
  deepEqual(
    downloaded.split(","),
    expected.length ? expected : [""],
    "Should have fetched the expected CRLite filters from the network"
  );
}

function setup_certdb() {
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );

  let ca = addCertFromFile(certdb, "test_crlite_filters/ca.pem", "C,C,");
  Services.prefs.setCharPref(BUILTIN_ROOT_HASH_PREF, ca.sha256Fingerprint);

  addCertFromFile(certdb, "test_crlite_filters/int.pem", ",,");
  return certdb;
}

function set_crlite_mode(mode) {
  Services.prefs.setBoolPref(
    CRLITE_FILTERS_ENABLED_PREF,
    mode != CRLiteModeDisabledPrefValue
  );
  Services.prefs.setIntPref(CRLITE_MODE_PREF, mode);
}

function set_crlite_channel(channel) {
  Services.prefs.setStringPref(CRLITE_FILTER_CHANNEL_PREF, channel);
}

async function cleanup() {
  Services.prefs.clearUserPref(BUILTIN_ROOT_HASH_PREF);
  Services.prefs.clearUserPref(CRLITE_FILTERS_ENABLED_PREF);
  Services.prefs.clearUserPref(CRLITE_FILTER_CHANNEL_PREF);
  Services.prefs.clearUserPref(CRLITE_MODE_PREF);
  Services.prefs.clearUserPref(CRLITE_TIMESTAMPS_FOR_COVERAGE_PREF);
  Services.prefs.clearUserPref(OCSP_ENABLED_PREF);
  Services.prefs.clearUserPref(OCSP_REQUIRED_PREF);
  // Remove CRLite files from security_state so each test starts fresh.
  let securityStateDir = PathUtils.join(PathUtils.profileDir, "security_state");
  for (let path of await IOUtils.getChildren(securityStateDir)) {
    let ext = PathUtils.filename(path).split(".").at(-1);
    if (["coverage", "delta", "enrollment", "filter", "stash"].includes(ext)) {
      await IOUtils.remove(path);
    }
  }
  await syncAndDownload([], true);
}

add_task(async function test_crlite_filters_disabled() {
  set_crlite_mode(CRLiteModeDisabledPrefValue);

  let result = await syncAndDownload([
    {
      timestamp: "2019-01-01T00:00:00Z",
      type: "filter",
      id: "0000",
    },
  ]);
  equal(result, "disabled", "CRLite filter download should not have run");

  await cleanup();
});

add_task(async function test_crlite_no_filters() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);

  let result = await syncAndDownload([]);
  equal(
    result,
    "unavailable",
    "CRLite filter download should have run, but nothing was available"
  );

  await cleanup();
});

add_task(async function test_crlite_no_filters_in_channel() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);

  let result = await syncAndDownload(
    [{ timestamp: "2019-01-01T00:00:00Z", type: "filter", id: "0000" }],
    true,
    "other"
  );
  equal(
    result,
    "unavailable",
    "CRLite filter download should have run, but nothing was available"
  );

  await cleanup();
});

add_task(async function test_crlite_only_incremental_filters() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);

  let result = await syncAndDownload([
    {
      timestamp: "2019-01-01T06:00:00Z",
      type: "diff",
      id: "0001",
      parent: "0000",
    },
    {
      timestamp: "2019-01-01T18:00:00Z",
      type: "diff",
      id: "0002",
      parent: "0001",
    },
    {
      timestamp: "2019-01-01T12:00:00Z",
      type: "diff",
      id: "0003",
      parent: "0002",
    },
  ]);
  equal(
    result,
    "unavailable",
    "CRLite filter download should have run, but no full filters were available"
  );

  await cleanup();
});

add_task(async function test_crlite_filters_basic() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);

  let result = await syncAndDownload([
    { timestamp: "2019-01-01T00:00:00Z", type: "filter", id: "0000" },
  ]);
  expectInstalled(result, ["2019-01-01T00:00:00Z-filter"]);
  expectDownloaded(result, ["2019-01-01T00:00:00Z-filter"]);

  await cleanup();
});

add_task(async function test_crlite_filters_not_cached() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);
  let filters = [
    { timestamp: "2019-01-01T00:00:00Z", type: "filter", id: "0000" },
  ];
  let result = await syncAndDownload(filters);
  expectInstalled(result, ["2019-01-01T00:00:00Z-filter"]);
  expectDownloaded(result, ["2019-01-01T00:00:00Z-filter"]);

  let records = await CRLiteFiltersClient.client.db.list();

  // `syncAndDownload` should not cache the attachment, so this download should
  // get the attachment from the source.
  let attachment = await CRLiteFiltersClient.client.attachments.download(
    records[0]
  );
  equal(attachment._source, "remote_match");
  await CRLiteFiltersClient.client.attachments.deleteDownloaded(records[0]);

  await cleanup();
});

add_task(async function test_crlite_filters_full_and_incremental() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);

  let result = await syncAndDownload([
    // These are deliberately listed out of order.
    {
      timestamp: "2019-01-01T06:00:00Z",
      type: "diff",
      id: "0001",
      parent: "0000",
    },
    { timestamp: "2019-01-01T00:00:00Z", type: "filter", id: "0000" },
    {
      timestamp: "2019-01-01T18:00:00Z",
      type: "diff",
      id: "0003",
      parent: "0002",
    },
    {
      timestamp: "2019-01-01T12:00:00Z",
      type: "diff",
      id: "0002",
      parent: "0001",
    },
  ]);
  expectInstalled(result, [
    "2019-01-01T00:00:00Z-filter",
    "2019-01-01T06:00:00Z-diff",
    "2019-01-01T12:00:00Z-diff",
    "2019-01-01T18:00:00Z-diff",
  ]);
  expectDownloaded(result, [
    "2019-01-01T00:00:00Z-filter",
    "2019-01-01T06:00:00Z-diff",
    "2019-01-01T12:00:00Z-diff",
    "2019-01-01T18:00:00Z-diff",
  ]);

  await cleanup();
});

add_task(async function test_crlite_filters_and_check_revocation() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);

  let certdb = setup_certdb();

  let result = await syncAndDownload([
    {
      timestamp: "2020-01-01T00:00:00Z",
      type: "filter",
      id: "0000",
    },
  ]);
  expectInstalled(result, ["2020-01-01T00:00:00Z-filter"]);
  expectDownloaded(result, ["2020-01-01T00:00:00Z-filter"]);

  let validCert = constructCertFromFile(
    "test_crlite_filters/valid.example.com.pem"
  );
  let notCoveredCert = constructCertFromFile(
    "test_crlite_filters/not-covered.example.com.pem"
  );
  let revokedCert = constructCertFromFile(
    "test_crlite_filters/revoked.example.com.pem"
  );

  Services.prefs.setIntPref(OCSP_ENABLED_PREF, 1);
  Services.prefs.setBoolPref(OCSP_REQUIRED_PREF, false);
  //                        CRLite   |   OCSP    | Result
  // validCert                   ok  |   none    |         ok
  // notCoveredCert            none  |   none    |         ok
  // revokedCert            revoked  |   none    |    revoked
  gOCSPRequestCount = 0;
  await checkCertErrorGenericAtTime(
    certdb,
    validCert,
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "valid.example.com",
    0
  );

  Services.fog.testResetFOG();
  Assert.equal(
    null,
    await Glean.certVerifier.crliteNotCoveredCertAge.testGetValue(),
    "crliteNotCoveredCertAge should not be accumulated yet"
  );
  await checkCertErrorGenericAtTime(
    certdb,
    notCoveredCert,
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "not-covered.example.com",
    0
  );
  Assert.notEqual(
    null,
    await Glean.certVerifier.crliteNotCoveredCertAge.testGetValue(),
    "crliteNotCoveredCertAge should have been accumulated"
  );

  await checkCertErrorGenericAtTime(
    certdb,
    revokedCert,
    SEC_ERROR_REVOKED_CERTIFICATE,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "revoked.example.com",
    0
  );
  if (AppConstants.DEBUG) {
    Assert.equal(
      gOCSPRequestCount,
      0,
      "no OCSP requests should have been made"
    );
  } else {
    // OCSP requests are made for certificates that do not chain up to the
    // mozilla root store, and `security.test.built_in_root_hash` only works in
    // debug builds.
    Assert.equal(
      gOCSPRequestCount,
      1,
      "exactly one OCSP request should have been made"
    );
  }

  Services.prefs.setIntPref(OCSP_ENABLED_PREF, 1);
  Services.prefs.setBoolPref(OCSP_REQUIRED_PREF, true);
  //                        CRLite   |   OCSP    | Result
  // validCert                   ok  |   none    |         ok
  // notCoveredCert            none  | hard fail |      error
  // revokedCert            revoked  |   none    |    revoked
  gOCSPRequestCount = 0;
  await checkCertErrorGenericAtTime(
    certdb,
    validCert,
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "valid.example.com",
    0
  );

  await checkCertErrorGenericAtTime(
    certdb,
    notCoveredCert,
    SEC_ERROR_OCSP_MALFORMED_RESPONSE,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "not-covered.example.com",
    0
  );

  await checkCertErrorGenericAtTime(
    certdb,
    revokedCert,
    SEC_ERROR_REVOKED_CERTIFICATE,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "revoked.example.com",
    0
  );
  Assert.equal(
    gOCSPRequestCount,
    1,
    "exactly one OCSP request should have been made"
  );

  await cleanup();
});

add_task(async function test_crlite_filters_with_delta() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);

  let certdb = setup_certdb();

  let result = await syncAndDownload([
    {
      timestamp: "2020-01-01T00:00:00Z",
      type: "filter",
      id: "0000",
    },
  ]);
  expectInstalled(result, ["2020-01-01T00:00:00Z-filter"]);
  expectDownloaded(result, ["2020-01-01T00:00:00Z-filter"]);

  let revokedInDeltaCert = constructCertFromFile(
    "test_crlite_filters/revoked-in-delta.example.com.pem"
  );

  Services.prefs.setIntPref(OCSP_ENABLED_PREF, 1);
  Services.prefs.setBoolPref(OCSP_REQUIRED_PREF, false);
  await checkCertErrorGenericAtTime(
    certdb,
    revokedInDeltaCert,
    PRErrorCodeSuccess, // we only have the full filter
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "revoked-in-delta.example.com",
    0
  );

  result = await syncAndDownload(
    [
      {
        timestamp: "2020-01-01T12:00:00Z",
        type: "diff",
        id: "0001",
        parent: "0000",
      },
    ],
    false
  );
  expectInstalled(result, ["2020-01-01T12:00:00Z-diff"]);
  expectDownloaded(result, ["2020-01-01T12:00:00Z-diff"]);

  await checkCertErrorGenericAtTime(
    certdb,
    revokedInDeltaCert,
    SEC_ERROR_REVOKED_CERTIFICATE,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "revoked-in-delta.example.com",
    0
  );

  await cleanup();
});

add_task(async function test_crlite_timestamps_for_coverage() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);

  let certdb = setup_certdb();

  let result = await syncAndDownload([
    {
      timestamp: "2020-01-01T00:00:00Z",
      type: "filter",
      id: "0000",
    },
  ]);
  expectInstalled(result, ["2020-01-01T00:00:00Z-filter"]);
  expectDownloaded(result, ["2020-01-01T00:00:00Z-filter"]);

  let validCert = constructCertFromFile(
    "test_crlite_filters/valid.example.com.pem"
  );

  Services.prefs.setIntPref(OCSP_ENABLED_PREF, 1);
  Services.prefs.setBoolPref(OCSP_REQUIRED_PREF, true);
  await checkCertErrorGenericAtTime(
    certdb,
    validCert,
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "valid.example.com",
    0
  );

  // We can make validCert fail the coverage check by changing the
  // security.pki.crlite_timestamps_for_coverage check. We'll detect this
  // by having OCSP hard-fail.
  Services.prefs.setIntPref(CRLITE_TIMESTAMPS_FOR_COVERAGE_PREF, 100);
  await checkCertErrorGenericAtTime(
    certdb,
    validCert,
    SEC_ERROR_OCSP_MALFORMED_RESPONSE,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "valid.example.com",
    0
  );

  await cleanup();
});

add_task(async function test_crlite_filters_avoid_reprocessing_filters() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);

  let result = await syncAndDownload([
    {
      timestamp: "2019-01-01T00:00:00Z",
      type: "filter",
      id: "0000",
    },
    {
      timestamp: "2019-01-01T06:00:00Z",
      type: "diff",
      id: "0001",
      parent: "0000",
    },
    {
      timestamp: "2019-01-01T12:00:00Z",
      type: "diff",
      id: "0002",
      parent: "0001",
    },
    {
      timestamp: "2019-01-01T18:00:00Z",
      type: "diff",
      id: "0003",
      parent: "0002",
    },
  ]);
  expectInstalled(result, [
    "2019-01-01T00:00:00Z-filter",
    "2019-01-01T06:00:00Z-diff",
    "2019-01-01T12:00:00Z-diff",
    "2019-01-01T18:00:00Z-diff",
  ]);
  expectDownloaded(result, [
    "2019-01-01T00:00:00Z-filter",
    "2019-01-01T06:00:00Z-diff",
    "2019-01-01T12:00:00Z-diff",
    "2019-01-01T18:00:00Z-diff",
  ]);
  // This simulates another poll without clearing the database first. The
  // filter and stashes should not be re-downloaded.
  result = await syncAndDownload([], false);
  expectInstalled(result, []);
  expectDownloaded(result, []);

  // If a new stash is added, only it should be downloaded.
  result = await syncAndDownload(
    [
      {
        timestamp: "2019-01-02T00:00:00Z",
        type: "diff",
        id: "0004",
        parent: "0003",
      },
    ],
    false
  );
  expectInstalled(result, ["2019-01-02T00:00:00Z-diff"]);
  expectDownloaded(result, ["2019-01-02T00:00:00Z-diff"]);

  await cleanup();
});

add_task(
  async function test_crlite_filters_reprocess_filters_on_channel_change() {
    set_crlite_mode(CRLiteModeEnforcePrefValue);
    set_crlite_channel("specified");

    // Download filters from the "specified" channel.
    let result = await syncAndDownload(
      [
        {
          timestamp: "2019-01-01T00:00:00Z",
          type: "filter",
          id: "0000",
        },
        {
          timestamp: "2019-01-01T06:00:00Z",
          type: "diff",
          id: "0001",
          parent: "0000",
        },
      ],
      true,
      "specified"
    );
    expectInstalled(result, [
      "2019-01-01T00:00:00Z-filter",
      "2019-01-01T06:00:00Z-diff",
    ]);
    expectDownloaded(result, [
      "2019-01-01T00:00:00Z-filter",
      "2019-01-01T06:00:00Z-diff",
    ]);

    // Now add records for the "priority" channel without clearing the database.
    // The user is subscribed to "specified" so nothing should be downloaded.
    result = await syncAndDownload(
      [
        {
          timestamp: "2020-01-01T00:00:00Z",
          type: "filter",
          id: "0002",
        },
        {
          timestamp: "2020-01-01T06:00:00Z",
          type: "diff",
          id: "0003",
          parent: "0002",
        },
      ],
      false,
      "priority"
    );
    expectInstalled(result, []);
    expectDownloaded(result, []);

    // Subscribe the user to "priority" channel. The channel change observer
    // calls onObservePollEnd directly, which should download the priority
    // filters. The full filter is not re-downloaded or installed because all
    // test filters share the same source bytes and therefore the same hash.
    let priorityResultPromise = TestUtils.topicObserved(
      "remote-security-settings:crlite-filters-updated"
    );
    set_crlite_channel("priority");
    [, result] = await priorityResultPromise;
    expectInstalled(result, ["2020-01-01T06:00:00Z-diff"]);
    expectDownloaded(result, ["2020-01-01T06:00:00Z-diff"]);

    // Switch back to the "specified" channel. The specified delta is on disk
    // and has the same hash as the priority delta already loaded in cert_storage
    // (all test filter files share the same bytes), so nothing needs to be
    // re-installed.
    let specifiedResultPromise = TestUtils.topicObserved(
      "remote-security-settings:crlite-filters-updated"
    );
    set_crlite_channel("specified");
    [, result] = await specifiedResultPromise;
    expectInstalled(result, []);
    expectDownloaded(result, []);

    await cleanup();
  }
);

add_task(async function test_crlite_full_filter_on_disk_not_loaded() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);

  // Copy the full filter directly to security_state, bypassing cert_storage.
  // This simulates the file surviving a restart before cert_storage has loaded it.
  let filterFile = do_get_file("test_crlite_filters/20200101-0-filter");
  let securityStateDir = PathUtils.join(PathUtils.profileDir, "security_state");
  await IOUtils.makeDirectory(securityStateDir, { ignoreExisting: true });
  await IOUtils.copy(
    filterFile.path,
    PathUtils.join(securityStateDir, "crlite.filter")
  );

  // Sync with a record matching the file already on disk.
  let result = await syncAndDownload([
    { timestamp: "2019-01-01T00:00:00Z", type: "filter", id: "0000" },
  ]);
  expectInstalled(result, ["2019-01-01T00:00:00Z-filter"]);
  expectDownloaded(result, []);

  await cleanup();
});

add_task(async function test_crlite_delta_on_disk_not_loaded() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);

  // First install the full filter normally so it is loaded in cert_storage.
  let result = await syncAndDownload([
    { timestamp: "2019-01-01T00:00:00Z", type: "filter", id: "0000" },
  ]);
  expectInstalled(result, ["2019-01-01T00:00:00Z-filter"]);
  expectDownloaded(result, ["2019-01-01T00:00:00Z-filter"]);

  // Copy the delta directly to security_state, bypassing cert_storage.
  // This simulates the delta file surviving a restart before cert_storage
  // has loaded it.
  let deltaFile = do_get_file("test_crlite_filters/20200101-1-filter.delta");
  let securityStateDir = PathUtils.join(PathUtils.profileDir, "security_state");
  // getAttachmentFilenameForFilter({ type: "diff", id: "0001" }) = "20200101-0001-filter.delta"
  await IOUtils.copy(
    deltaFile.path,
    PathUtils.join(securityStateDir, "20200101-0001-filter.delta")
  );

  result = await syncAndDownload(
    [
      {
        timestamp: "2019-01-01T06:00:00Z",
        type: "diff",
        id: "0001",
        parent: "0000",
      },
    ],
    false
  );
  expectInstalled(result, ["2019-01-01T06:00:00Z-diff"]);
  expectDownloaded(result, []);

  await cleanup();
});

add_task(async function test_get_crlite_filter_hashes() {
  set_crlite_mode(CRLiteModeEnforcePrefValue);

  const certList = Cc["@mozilla.org/security/certstorage;1"].getService(
    Ci.nsICertStorage
  );

  async function getCRLiteFilterHashes() {
    return new Promise(resolve => {
      certList.getCRLiteFilterHashes((rv, result) => {
        Assert.equal(rv, Cr.NS_OK, "getCRLiteFilterHashes should succeed");
        resolve(result ? result.toString() : "");
      });
    });
  }

  const fullFilterHash = getHash(
    readFile(do_get_file("test_crlite_filters/20200101-0-filter"))
  );
  const deltaHash = getHash(
    readFile(do_get_file("test_crlite_filters/20200101-1-filter.delta"))
  );

  // After installing only the full filter, getCRLiteFilterHashes should return
  // just its hash.
  await syncAndDownload([
    { timestamp: "2019-01-01T00:00:00Z", type: "filter", id: "0000" },
  ]);
  Assert.equal(
    await getCRLiteFilterHashes(),
    fullFilterHash,
    "should return only the full filter hash"
  );

  // After adding a delta, getCRLiteFilterHashes should return both hashes in
  // order: full filter first, then delta.
  await syncAndDownload(
    [
      {
        timestamp: "2019-01-01T06:00:00Z",
        type: "diff",
        id: "0001",
        parent: "0000",
      },
    ],
    false
  );
  Assert.equal(
    await getCRLiteFilterHashes(),
    `${fullFilterHash},${deltaHash}`,
    "should return full filter hash followed by delta hash"
  );

  await cleanup();
});

let server;

function run_test() {
  server = new HttpServer();
  server.start(-1);
  registerCleanupFunction(() => server.stop(() => {}));

  server.registerDirectory(
    "/cdn/security-state-workspace/cert-revocations/",
    do_get_file(".")
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

  let ocspResponder = new HttpServer();
  ocspResponder.registerPrefixHandler("/", function (_request, _response) {
    gOCSPRequestCount++;
  });
  ocspResponder.start(8888);
  registerCleanupFunction(() => ocspResponder.stop(() => {}));

  Services.prefs.setCharPref(
    "services.settings.server",
    `http://localhost:${server.identity.primaryPort}/v1`
  );

  // Set intermediate preloading to download 0 intermediates at a time.
  Services.prefs.setBoolPref(INTERMEDIATES_ENABLED_PREF, true);
  Services.prefs.setIntPref(INTERMEDIATES_DL_PER_POLL_PREF, 0);

  Services.prefs.setCharPref("browser.policies.loglevel", "debug");

  // All of the tests here should work even when CT is disabled.
  Services.prefs.setIntPref(
    CERTIFICATE_TRANSPARENCY_MODE_PREF,
    CT_MODE_DISABLE
  );

  run_next_test();
}
