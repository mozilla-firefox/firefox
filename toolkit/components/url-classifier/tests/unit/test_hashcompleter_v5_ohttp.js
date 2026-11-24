/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);

const KEY_COLLECTION = "safebrowsing-v5-rt-key";
const PREF_RELAY_URL = "browser.safebrowsing.realTime.ohttpRelayURL";
const PREF_GETHASH_TIMEOUT = "urlclassifier.gethash.timeout_ms";
const PREF_DISABLE_TEST_BACKOFF =
  "browser.safebrowsing.provider.test.disableBackoff";
const V5_TABLE = "test-google5-malware-proto";
const V4_TABLE = "test-phish-proto";
const RELAY_PATH = "/relay";
const GETHASH_PATH = "/v5/hashes:search";
const API_KEY = "TESTKEY";

const gCompleter = Cc["@mozilla.org/url-classifier/hashcompleter;1"].getService(
  Ci.nsIUrlClassifierHashCompleter
);
const gTestUtils = Cc["@mozilla.org/url-classifier/test-utils;1"].getService(
  Ci.nsIUrlClassifierTestUtils
);
const gBinaryHttp = Cc["@mozilla.org/network/binary-http;1"].getService(
  Ci.nsIBinaryHttp
);
const gOHTTPServer = Cc["@mozilla.org/network/oblivious-http;1"]
  .getService(Ci.nsIObliviousHttp)
  .server();

let gHttpServer;
let gLocalGethashUrl;
let gRelayRequests = [];
let gDirectRequests = [];
let gRelayStatus = 200;

function fullHashFor(prefix) {
  return prefix + "f".repeat(32 - prefix.length);
}

function makeV5Response([prefix]) {
  return gTestUtils.makeFindFullHashResponseV5(fullHashFor(prefix));
}

function prefixesFromQuery(pathQuery) {
  let query = pathQuery.substring(pathQuery.indexOf("?"));
  return new URLSearchParams(query).getAll("hashPrefixes").map(p => atob(p));
}

function readBody(request) {
  let stream = Cc["@mozilla.org/scriptableinputstream;1"].createInstance(
    Ci.nsIScriptableInputStream
  );
  stream.init(request.bodyInputStream);
  return stream.readBytes(stream.available());
}

function stringToBytes(str) {
  return Array.from(str, c => c.charCodeAt(0));
}

function bytesToString(bytes) {
  return String.fromCharCode(...bytes);
}

class BinaryHttpResponse {
  constructor(status, content) {
    this.status = status;
    this.headerNames = ["content-type"];
    this.headerValues = ["application/x-protobuf"];
    this.content = stringToBytes(content);
  }

  QueryInterface = ChromeUtils.generateQI(["nsIBinaryHttpResponse"]);
}

function relayHandler(request, response) {
  let decapsulated = gOHTTPServer.decapsulate(stringToBytes(readBody(request)));
  let inner = gBinaryHttp.decodeRequest(decapsulated.request);
  gRelayRequests.push({
    method: request.method,
    contentType: request.getHeader("Content-Type"),
    inner: {
      method: inner.method,
      scheme: inner.scheme,
      authority: inner.authority,
      path: inner.path,
      prefixes: prefixesFromQuery(inner.path),
    },
  });

  if (gRelayStatus != 200) {
    response.setStatusLine(request.httpVersion, gRelayStatus, "Relay Error");
    return;
  }

  let innerResponse = new BinaryHttpResponse(
    200,
    makeV5Response(prefixesFromQuery(inner.path))
  );
  let encapsulated = decapsulated.encapsulate(
    gBinaryHttp.encodeResponse(innerResponse)
  );
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "message/ohttp-res", false);
  response.write(bytesToString(encapsulated));
}

function directHandler(request, response) {
  let prefixes = prefixesFromQuery(request.path + "?" + request.queryString);
  gDirectRequests.push({ prefixes });
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "application/x-protobuf", false);
  response.write(makeV5Response(prefixes));
}

function quitObserverCount() {
  return Array.from(Services.obs.enumerateObservers("quit-application")).length;
}

function complete(prefix, gethashUrl, table, useOHTTP) {
  return new Promise(resolve => {
    let fullHashes = [];
    gCompleter.complete(prefix, gethashUrl, table, useOHTTP, {
      completionV2() {
        ok(false, "V2 completion is not expected");
      },
      completionV4(partialHash, tableName, negativeCacheDuration, matches) {
        for (let match of matches.enumerate(Ci.nsIFullHashMatch)) {
          fullHashes.push(match.fullHash);
        }
      },
      completionFinished(status) {
        resolve({ status, fullHashes });
      },
    });
  });
}

async function setKeyRecords(records) {
  let client = RemoteSettings(KEY_COLLECTION);
  await client.db.importChanges({}, Date.now(), records, { clear: true });
  await client.emit("sync", {
    data: { current: records, created: [], updated: [], deleted: [] },
  });
}

function keyRecord() {
  let config = new Uint8Array(gOHTTPServer.encodedConfig);
  return { id: "latest", key_id: config[0], key_config: config.toBase64() };
}

add_setup(async function () {
  gHttpServer = new HttpServer();
  gHttpServer.registerPathHandler(RELAY_PATH, relayHandler);
  gHttpServer.registerPathHandler(GETHASH_PATH, directHandler);
  gHttpServer.start(-1);
  let origin = `http://localhost:${gHttpServer.identity.primaryPort}`;
  gLocalGethashUrl = `${origin}${GETHASH_PATH}?key=${API_KEY}`;
  Services.prefs.setStringPref(PREF_RELAY_URL, origin + RELAY_PATH);

  await RemoteSettings(KEY_COLLECTION).db.importChanges({}, 42, [keyRecord()]);

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref(PREF_RELAY_URL);
    await new Promise(resolve => gHttpServer.stop(resolve));
  });
});

add_task(async function test_ohttp_request() {
  gRelayRequests = [];
  gDirectRequests = [];
  let gethashUrl = `https://safebrowsing.example.test${GETHASH_PATH}?key=${API_KEY}`;

  let result = await complete("abcd", gethashUrl, V5_TABLE, true);

  equal(result.status, Cr.NS_OK, "OHTTP completion succeeds");
  deepEqual(
    result.fullHashes,
    [fullHashFor("abcd")],
    "Full hash from the encapsulated response is delivered"
  );
  equal(gRelayRequests.length, 1, "One request reaches the relay");
  equal(gDirectRequests.length, 0, "No direct request is made");

  let { method, contentType, inner } = gRelayRequests[0];
  equal(method, "POST", "Relay request is a POST");
  equal(contentType, "message/ohttp-req", "Relay request is an OHTTP request");
  equal(inner.method, "GET", "Inner request is a GET");
  equal(inner.scheme, "https", "Inner request targets https");
  equal(inner.authority, "safebrowsing.example.test", "Inner authority");
  ok(
    inner.path.startsWith(`${GETHASH_PATH}?key=${API_KEY}&`),
    "Inner request carries the gethash path and API key"
  );
  deepEqual(inner.prefixes, ["abcd"], "Inner request carries the prefix");
});

add_task(async function test_no_relay_url() {
  gRelayRequests = [];
  gDirectRequests = [];
  let relayURL = Services.prefs.getStringPref(PREF_RELAY_URL);
  Services.prefs.setStringPref(PREF_RELAY_URL, "");
  let observers = quitObserverCount();

  let result = await complete("bcde", gLocalGethashUrl, V5_TABLE, true);

  equal(result.status, Cr.NS_ERROR_NOT_AVAILABLE, "Completion fails");
  deepEqual(result.fullHashes, [], "No full hash is delivered");
  equal(gRelayRequests.length, 0, "No request reaches the relay");
  equal(gDirectRequests.length, 0, "No direct request is made");
  equal(quitObserverCount(), observers, "No quit-application observer leaks");

  Services.prefs.setStringPref(PREF_RELAY_URL, relayURL);
});

add_task(async function test_no_key_config() {
  gRelayRequests = [];
  gDirectRequests = [];
  await setKeyRecords([]);
  let observers = quitObserverCount();

  let [result, direct] = await Promise.all([
    complete("cdef", gLocalGethashUrl, V5_TABLE, true),
    complete("cdeg", gLocalGethashUrl, V5_TABLE, false),
  ]);

  equal(result.status, Cr.NS_ERROR_NOT_AVAILABLE, "Completion fails");
  deepEqual(result.fullHashes, [], "No full hash is delivered");
  equal(gRelayRequests.length, 0, "No request reaches the relay");
  equal(direct.status, Cr.NS_OK, "A queued direct completion still runs");
  deepEqual(
    gDirectRequests.map(r => r.prefixes),
    [["cdeg"]],
    "Only the direct prefix is sent directly"
  );
  equal(quitObserverCount(), observers, "No quit-application observer leaks");

  await setKeyRecords([keyRecord()]);
});

add_task(async function test_ohttp_and_direct_are_separate_requests() {
  gRelayRequests = [];
  gDirectRequests = [];

  let [ohttp, direct] = await Promise.all([
    complete("defg", gLocalGethashUrl, V5_TABLE, true),
    complete("efgh", gLocalGethashUrl, V5_TABLE, false),
  ]);

  equal(ohttp.status, Cr.NS_OK, "OHTTP completion succeeds");
  deepEqual(ohttp.fullHashes, [fullHashFor("defg")], "OHTTP full hash");
  equal(direct.status, Cr.NS_OK, "Direct completion succeeds");
  deepEqual(direct.fullHashes, [fullHashFor("efgh")], "Direct full hash");

  equal(gRelayRequests.length, 1, "One request reaches the relay");
  deepEqual(
    gRelayRequests[0].inner.prefixes,
    ["defg"],
    "Relay request only carries the OHTTP prefix"
  );
  equal(gDirectRequests.length, 1, "One direct request is made");
  deepEqual(
    gDirectRequests[0].prefixes,
    ["efgh"],
    "Direct request only carries the direct prefix"
  );
});

add_task(async function test_ohttp_on_v4_table() {
  gRelayRequests = [];
  gDirectRequests = [];

  let [result, direct] = await Promise.all([
    complete("fghi", gLocalGethashUrl, V4_TABLE, true),
    complete("fghj", gLocalGethashUrl, V5_TABLE, false),
  ]);

  equal(result.status, Cr.NS_ERROR_NOT_AVAILABLE, "Completion fails");
  equal(gRelayRequests.length, 0, "No request reaches the relay");
  equal(direct.status, Cr.NS_OK, "A queued direct completion still runs");
  deepEqual(
    gDirectRequests.map(r => r.prefixes),
    [["fghj"]],
    "Only the direct prefix is sent directly"
  );
});

add_task(async function test_relay_empty_response() {
  gDirectRequests = [];
  let relayRequests = 0;
  gHttpServer.registerPathHandler("/relay-empty", (request, response) => {
    relayRequests++;
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader("Content-Type", "message/ohttp-res", false);
  });
  let relayURL = Services.prefs.getStringPref(PREF_RELAY_URL);
  Services.prefs.setStringPref(
    PREF_RELAY_URL,
    `http://localhost:${gHttpServer.identity.primaryPort}/relay-empty`
  );

  let result = await complete("hijk", gLocalGethashUrl, V5_TABLE, true);

  equal(result.status, Cr.NS_ERROR_ABORT, "Completion fails");
  deepEqual(result.fullHashes, [], "No full hash is delivered");
  equal(relayRequests, 1, "The request reaches the relay");
  equal(gDirectRequests.length, 0, "No direct request is made");

  Services.prefs.setStringPref(PREF_RELAY_URL, relayURL);
  gHttpServer.registerPathHandler("/relay-empty", null);
});

add_task(async function test_key_config_timeout() {
  gRelayRequests = [];
  gDirectRequests = [];
  await setKeyRecords([]);
  let client = RemoteSettings(KEY_COLLECTION);
  let records = Promise.withResolvers();
  client.get = () => records.promise;
  let timeout = Services.prefs.getIntPref(PREF_GETHASH_TIMEOUT);
  Services.prefs.setIntPref(PREF_GETHASH_TIMEOUT, 100);

  let result = await complete("ijkl", gLocalGethashUrl, V5_TABLE, true);

  equal(result.status, Cr.NS_ERROR_NOT_AVAILABLE, "Completion fails");
  deepEqual(result.fullHashes, [], "No full hash is delivered");
  equal(gRelayRequests.length, 0, "No request reaches the relay");
  equal(gDirectRequests.length, 0, "No direct request is made");

  Services.prefs.setIntPref(PREF_GETHASH_TIMEOUT, timeout);
  records.resolve([]);
  delete client.get;
  await setKeyRecords([keyRecord()]);
});

add_task(async function test_malformed_key_config() {
  gRelayRequests = [];
  gDirectRequests = [];
  let config = new Uint8Array([7, 0x00, 0x20, 1, 2, 3]);
  await setKeyRecords([
    { id: "latest", key_id: config[0], key_config: config.toBase64() },
  ]);

  let result = await complete("klmn", gLocalGethashUrl, V5_TABLE, true);

  ok(!Components.isSuccessCode(result.status), "Completion fails");
  deepEqual(result.fullHashes, [], "No full hash is delivered");
  equal(gRelayRequests.length, 0, "No request reaches the relay");
  equal(gDirectRequests.length, 0, "No direct request is made");

  await setKeyRecords([keyRecord()]);
});

add_task(async function test_relay_error() {
  gRelayRequests = [];
  gDirectRequests = [];
  gRelayStatus = 500;

  let result = await complete("ghij", gLocalGethashUrl, V5_TABLE, true);

  ok(!Components.isSuccessCode(result.status), "Completion fails");
  deepEqual(result.fullHashes, [], "No full hash is delivered");
  equal(gRelayRequests.length, 1, "The request reaches the relay");
  equal(gDirectRequests.length, 0, "No direct request is made");

  gRelayStatus = 200;
});

add_task(async function test_ohttp_and_direct_back_off_separately() {
  gRelayRequests = [];
  gDirectRequests = [];
  Services.prefs.setBoolPref(PREF_DISABLE_TEST_BACKOFF, false);
  let gethashUrl = `${gLocalGethashUrl}&backoff=1`;

  gRelayStatus = 500;
  let failed = await complete("lmno", gethashUrl, V5_TABLE, true);
  gRelayStatus = 200;
  ok(!Components.isSuccessCode(failed.status), "The relay error fails");

  let ohttp = await complete("mnop", gethashUrl, V5_TABLE, true);
  equal(ohttp.status, Cr.NS_ERROR_ABORT, "The OHTTP request is backed off");
  equal(gRelayRequests.length, 1, "Only the first request reaches the relay");

  let direct = await complete("nopq", gethashUrl, V5_TABLE, false);
  equal(direct.status, Cr.NS_OK, "The direct request is not backed off");
  deepEqual(
    direct.fullHashes,
    [fullHashFor("nopq")],
    "The direct request delivers its full hash"
  );
  equal(gDirectRequests.length, 1, "The direct request is sent");

  Services.prefs.clearUserPref(PREF_DISABLE_TEST_BACKOFF);
});

add_task(async function test_key_change_resets_ohttp_backoff() {
  gRelayRequests = [];
  gDirectRequests = [];
  Services.prefs.setBoolPref(PREF_DISABLE_TEST_BACKOFF, false);
  let gethashUrl = `${gLocalGethashUrl}&keychange=1`;
  let directErrors = 0;
  gHttpServer.registerPathHandler("/gethash-error", (request, response) => {
    directErrors++;
    response.setStatusLine(request.httpVersion, 500, "Server Error");
  });
  let directUrl = `http://localhost:${gHttpServer.identity.primaryPort}/gethash-error?key=${API_KEY}`;
  await complete("rstu", directUrl, V5_TABLE, false);
  await complete("stuv", directUrl, V5_TABLE, false);
  equal(directErrors, 1, "The second direct request is backed off");

  gRelayStatus = 500;
  await complete("opqr", gethashUrl, V5_TABLE, true);
  gRelayStatus = 200;
  let backedOff = await complete("pqrs", gethashUrl, V5_TABLE, true);
  equal(backedOff.status, Cr.NS_ERROR_ABORT, "The OHTTP request is backed off");
  equal(gRelayRequests.length, 1, "Only the first request reaches the relay");

  await setKeyRecords([]);
  await setKeyRecords([keyRecord()]);

  let result = await complete("qrst", gethashUrl, V5_TABLE, true);
  equal(result.status, Cr.NS_OK, "The OHTTP request succeeds after a new key");
  deepEqual(
    result.fullHashes,
    [fullHashFor("qrst")],
    "The OHTTP request delivers its full hash"
  );
  equal(gRelayRequests.length, 2, "The request reaches the relay");

  await complete("tuvw", directUrl, V5_TABLE, false);
  equal(directErrors, 1, "The direct backoff is not reset");

  Services.prefs.clearUserPref(PREF_DISABLE_TEST_BACKOFF);
  gHttpServer.registerPathHandler("/gethash-error", null);
});

add_task(async function test_shutdown_during_key_wait() {
  gRelayRequests = [];
  gDirectRequests = [];
  await setKeyRecords([]);
  let client = RemoteSettings(KEY_COLLECTION);
  let getCalled = Promise.withResolvers();
  let records = Promise.withResolvers();
  client.get = () => {
    getCalled.resolve();
    return records.promise;
  };

  let completion = complete("jklm", gLocalGethashUrl, V5_TABLE, true);
  await getCalled.promise;
  gCompleter
    .QueryInterface(Ci.nsIObserver)
    .observe(null, "quit-application", null);
  records.resolve([keyRecord()]);
  let result = await completion;

  equal(result.status, Cr.NS_ERROR_NOT_AVAILABLE, "Completion fails");
  deepEqual(result.fullHashes, [], "No full hash is delivered");
  equal(gRelayRequests.length, 0, "No request reaches the relay");
  equal(gDirectRequests.length, 0, "No direct request is made");

  delete client.get;
});
