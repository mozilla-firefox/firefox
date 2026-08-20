/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// A stale entry A may be selected by No-Vary-Search to revalidate a request for
// a different URL B. When the server answers B with a full (non-304) response,
// that response describes B, not A, and must never be written back under A's
// cache key.

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

let gHttpServer;
let gPort;
let gRequests = [];
const PATH = "/nvs-reval";
const PAIR_PATH = "/nvs-pair";
const REDIR_PATH = "/nvs-redir";
const TARGET_PATH = "/nvs-target";
const NOTMOD_PATH = "/nvs-304";
const PARTIAL_PATH = "/nvs-partial";
const NOSTORE_PATH = "/nvs-nostore";
const REFRESH_PATH = "/nvs-refresh";
const SWAP_PATH = "/nvs-swap";

// Bumped between rounds so that a revalidation is answered with a full 200
// carrying new content rather than a 304.
let gRound = 1;

// 2kb of body, so that a partial entry is unambiguously incomplete.
let gLongBody = "response";
for (let i = 0; i < 8; ++i) {
  gLongBody += gLongBody;
}
const PARTIAL_PREFIX_LEN = 100;

function recordRequest(metadata) {
  gRequests.push({
    path: metadata.path,
    query: metadata.queryString,
    conditional: metadata.hasHeader("If-None-Match"),
    range: metadata.hasHeader("Range") || metadata.hasHeader("If-Range"),
  });
}

function handler(metadata, response) {
  const query = metadata.queryString;
  recordRequest(metadata);

  response.setStatusLine(metadata.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/plain", false);

  let body;
  if (query === "u=a") {
    // Always stale, always revalidated, and declares "u" irrelevant.
    response.setHeader("Cache-Control", "max-age=0, must-revalidate", false);
    response.setHeader("ETag", '"v1"', false);
    response.setHeader("No-Vary-Search", 'params=("u")', false);
    body = "a";
  } else {
    // Freshly cacheable, and withdraws the No-Vary-Search policy.
    response.setHeader("Cache-Control", "max-age=10000", false);
    body = "b";
  }

  response.bodyOutputStream.write(body, body.length);
}

// Two sibling URLs that both must revalidate. Only "u=b" carries
// No-Vary-Search, so B's entry is a secondary-index candidate that would also
// cover A's URL.
function pairHandler(metadata, response) {
  recordRequest(metadata);

  const which = metadata.queryString === "u=a" ? "a" : "b";

  response.setStatusLine(metadata.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/plain", false);
  response.setHeader("Cache-Control", "max-age=0, must-revalidate", false);
  response.setHeader("ETag", `"${which}${gRound}"`, false);
  if (which === "b") {
    response.setHeader("No-Vary-Search", 'params=("u")', false);
  }

  const body = `${which}-${gRound}`;
  response.bodyOutputStream.write(body, body.length);
}

// Same shape as the first test, but B's full response is a cacheable redirect
// rather than a plain 200. That travels a different path in nsHttpChannel
function redirHandler(metadata, response) {
  recordRequest(metadata);

  if (metadata.queryString === "u=a") {
    response.setStatusLine(metadata.httpVersion, 200, "OK");
    response.setHeader("Content-Type", "text/plain", false);
    response.setHeader("Cache-Control", "max-age=0, must-revalidate", false);
    response.setHeader("ETag", '"r1"', false);
    response.setHeader("No-Vary-Search", 'params=("u")', false);
    response.bodyOutputStream.write("a", 1);
    return;
  }

  response.setStatusLine(metadata.httpVersion, 302, "Found");
  response.setHeader("Location", `http://localhost:${gPort}${TARGET_PATH}`);
  response.setHeader("Cache-Control", "max-age=10000", false);
}

function targetHandler(metadata, response) {
  recordRequest(metadata);
  response.setStatusLine(metadata.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/plain", false);
  response.setHeader("Cache-Control", "no-cache", false);
  response.bodyOutputStream.write("target", 6);
}

// A is the only stored representation; a request for B revalidates against it
// and the server confirms it is still good. This is No-Vary-Search working as
// intended and must keep working.
function notModifiedHandler(metadata, response) {
  recordRequest(metadata);

  if (metadata.hasHeader("If-None-Match")) {
    response.setStatusLine(metadata.httpVersion, 304, "Not Modified");
    return;
  }

  response.setStatusLine(metadata.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/plain", false);
  response.setHeader("Cache-Control", "max-age=0, must-revalidate", false);
  response.setHeader("ETag", '"n1"', false);
  response.setHeader("No-Vary-Search", 'params=("u")', false);
  response.bodyOutputStream.write("a", 1);
}

// Like notModifiedHandler, but the 304 also extends freshness. Merging that
// into the candidate is a legitimate write to an entry stored under another
// URL, and must still reach it.
function refreshHandler(metadata, response) {
  recordRequest(metadata);

  if (metadata.hasHeader("If-None-Match")) {
    response.setStatusLine(metadata.httpVersion, 304, "Not Modified");
    response.setHeader("Cache-Control", "max-age=10000", false);
    return;
  }

  response.setStatusLine(metadata.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/plain", false);
  response.setHeader("Cache-Control", "max-age=0, must-revalidate", false);
  response.setHeader("ETag", '"f1"', false);
  response.setHeader("No-Vary-Search", 'params=("u")', false);
  response.bodyOutputStream.write("a", 1);
}

// A is stored fresh but truncated: it declares a Content-Length it never
// delivers, so the cache holds an incomplete entry that carries a
// No-Vary-Search policy covering B.
function partialHandler(metadata, response) {
  recordRequest(metadata);

  response.setHeader("Content-Type", "text/plain", false);
  response.setHeader("Accept-Ranges", "bytes", false);
  response.setHeader("Cache-Control", "max-age=360000", false);
  response.setHeader("ETag", '"p1"', false);

  if (metadata.queryString === "u=a") {
    response.setHeader("No-Vary-Search", 'params=("u")', false);
    response.setHeader("Content-Length", `${gLongBody.length}`, false);
    response.processAsync();
    const slice = gLongBody.slice(0, PARTIAL_PREFIX_LEN);
    response.bodyOutputStream.write(slice, slice.length);
    response.finish();
    return;
  }

  response.setHeader("Content-Length", `${gLongBody.length}`, false);
  response.bodyOutputStream.write(gLongBody, gLongBody.length);
}

// B's full response forbids storage, so the replacement entry must be
// memory-only rather than landing on disk.
function noStoreHandler(metadata, response) {
  recordRequest(metadata);

  response.setStatusLine(metadata.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/plain", false);

  if (metadata.queryString === "u=a") {
    response.setHeader("Cache-Control", "max-age=0, must-revalidate", false);
    response.setHeader("ETag", '"s1"', false);
    response.setHeader("No-Vary-Search", 'params=("u")', false);
    response.bodyOutputStream.write("a", 1);
    return;
  }

  response.setHeader("Cache-Control", "no-store", false);
  response.bodyOutputStream.write("b", 1);
}

// The 304 changes the No-Vary-Search rule: the candidate was selected because
// "u" was insignificant, but the refreshed policy makes "u" significant and
// "v" insignificant instead, so the two URLs are no longer equivalent. Each
// response body is its own query string, so it is visible which URL's content
// a request was served.
function swapHandler(metadata, response) {
  recordRequest(metadata);

  if (metadata.hasHeader("If-None-Match")) {
    response.setStatusLine(metadata.httpVersion, 304, "Not Modified");
    response.setHeader("No-Vary-Search", 'params=("v")', false);
    return;
  }

  response.setStatusLine(metadata.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/plain", false);
  response.setHeader("Cache-Control", "max-age=0, must-revalidate", false);
  response.setHeader("ETag", '"w1"', false);
  response.setHeader("No-Vary-Search", 'params=("u")', false);
  const body = metadata.queryString;
  response.bodyOutputStream.write(body, body.length);
}

function uri(query) {
  return `http://localhost:${gPort}${PATH}?${query}`;
}

function pairURI(query) {
  return `http://localhost:${gPort}${PAIR_PATH}?${query}`;
}

function pathURI(path, query) {
  return `http://localhost:${gPort}${path}?${query}`;
}

function fetchURI(spec) {
  return new Promise(resolve => {
    let chan = NetUtil.newChannel({
      uri: spec,
      loadUsingSystemPrincipal: true,
    });
    chan.asyncOpen(
      new ChannelListener(
        (request, buffer) => resolve(buffer),
        null,
        CL_ALLOW_UNKNOWN_CL
      )
    );
  });
}

// Like fetchURI, but for a response whose body is shorter than its declared
// Content-Length. That fails with NS_ERROR_NET_PARTIAL_TRANSFER after
// onStartRequest, and leaves an incomplete entry behind, which is the point.
function fetchTruncatedURI(spec) {
  return new Promise(resolve => {
    let chan = NetUtil.newChannel({
      uri: spec,
      loadUsingSystemPrincipal: true,
    });
    chan.asyncOpen(
      new ChannelListener(
        (request, buffer) => resolve(buffer),
        null,
        CL_EXPECT_LATE_FAILURE | CL_IGNORE_CL
      )
    );
  });
}

// Opens the entry stored under exactly |spec|. Rejects with the failure code
// when there is no such entry.
function openEntry(spec, where = "disk") {
  return new Promise((resolve, reject) => {
    asyncOpenCacheEntry(
      spec,
      where,
      Ci.nsICacheStorage.OPEN_READONLY | Ci.nsICacheStorage.OPEN_SECRETLY,
      null,
      (status, entry) => {
        if (!Components.isSuccessCode(status)) {
          reject(status);
          return;
        }
        resolve(entry);
      }
    );
  });
}

// The cache input stream is non-blocking, so the data has to be pumped rather
// than read synchronously.
function entryBody(entry) {
  return new Promise(resolve => {
    pumpReadStream(entry.openInputStream(0), resolve);
  });
}

function entryMetaData(entry, key) {
  try {
    return entry.getMetaDataElement(key);
  } catch (e) {
    return null;
  }
}

// The key of the entry a lookup for |spec| resolves to, or null when there is
// none. Note this is not an exact-key probe: a read-only open runs the same
// No-Vary-Search secondary lookup a channel would, so the answer may be a
// sibling's key. That is precisely what we want to be able to tell apart.
async function resolvedKey(spec, where = "disk") {
  try {
    let entry = await openEntry(spec, where);
    return entry.key;
  } catch (status) {
    return null;
  }
}

add_setup(function () {
  do_get_profile();
  Services.prefs.setBoolPref("network.cache.no_vary_search", true);
  // The partial-entry scenario deliberately serves fewer bytes than it
  // announces; without this the channel rejects the response outright.
  Services.prefs.setBoolPref("network.http.enforce-framing.soft", false);

  gHttpServer = new HttpServer();
  gHttpServer.registerPathHandler(PATH, handler);
  gHttpServer.registerPathHandler(PAIR_PATH, pairHandler);
  gHttpServer.registerPathHandler(REDIR_PATH, redirHandler);
  gHttpServer.registerPathHandler(TARGET_PATH, targetHandler);
  gHttpServer.registerPathHandler(NOTMOD_PATH, notModifiedHandler);
  gHttpServer.registerPathHandler(PARTIAL_PATH, partialHandler);
  gHttpServer.registerPathHandler(NOSTORE_PATH, noStoreHandler);
  gHttpServer.registerPathHandler(REFRESH_PATH, refreshHandler);
  gHttpServer.registerPathHandler(SWAP_PATH, swapHandler);
  gHttpServer.start(-1);
  gPort = gHttpServer.identity.primaryPort;

  registerCleanupFunction(() => {
    gHttpServer.stop(() => {});
    Services.prefs.clearUserPref("network.cache.no_vary_search");
    Services.prefs.clearUserPref("network.http.enforce-framing.soft");
  });
});

add_task(
  async function test_full_response_is_not_written_under_nvs_candidate() {
    Services.cache2.clear();
    gRequests = [];

    Assert.equal(await fetchURI(uri("u=a")), "a", "A is primed");

    // B misses on its exact key, so NVS selects the stale A as the candidate to
    // revalidate against. The server answers 200 with a different body.
    Assert.equal(await fetchURI(uri("u=b")), "b", "B is fetched");
    Assert.equal(gRequests.length, 2, "B went to the network");
    Assert.ok(gRequests[1].conditional, "B revalidated against A's validator");

    // A must still be A. With the bug, B's response was stored under A's key and
    // this returns "b" without hitting the network.
    Assert.equal(await fetchURI(uri("u=a")), "a", "A is unchanged");
    Assert.equal(gRequests.length, 3, "A was revalidated on the network");
    Assert.equal(gRequests[2].query, "u=a", "the third request was for A");

    // B's response was stored under B and is still fresh.
    Assert.equal(await fetchURI(uri("u=b")), "b", "B is cached");
    Assert.equal(gRequests.length, 3, "B was served from its own cache entry");
  }
);

// Two sibling entries coexist, one of them carrying a No-Vary-Search policy
// that also covers the other. Revalidating both must leave each response in the
// entry it belongs to: the NVS candidate must neither be written over nor
// divert a write away from the entry it was meant for.
add_task(async function test_sibling_entries_stay_separate() {
  Services.cache2.clear();
  gRequests = [];
  gRound = 1;

  const a = pairURI("u=a");
  const b = pairURI("u=b");

  // Prime A first, then B. B is the one declaring No-Vary-Search.
  Assert.equal(await fetchURI(a), "a-1", "A is primed");
  Assert.equal(await fetchURI(b), "b-1", "B is primed");
  Assert.equal(gRequests.length, 2, "both went to the network");

  // Both are must-revalidate, and the server now answers with new content.
  gRound = 2;

  Assert.equal(await fetchURI(a), "a-2", "A revalidated to new content");
  Assert.equal(await fetchURI(b), "b-2", "B revalidated to new content");
  Assert.equal(gRequests.length, 4, "both revalidations went to the network");
  Assert.ok(gRequests[2].conditional, "A's refetch was conditional");
  Assert.ok(gRequests[3].conditional, "B's refetch was conditional");

  // Inspect the cache directly instead of inferring from the response bodies.
  let entryA = await openEntry(a);
  Assert.equal(entryA.key, a, "A's URL resolves to an entry keyed on A");
  Assert.equal(await entryBody(entryA), "a-2", "A's entry holds A's response");
  Assert.equal(
    entryMetaData(entryA, "no-vary-search"),
    null,
    "A's entry carries no No-Vary-Search policy"
  );

  let entryB = await openEntry(b);
  Assert.equal(entryB.key, b, "B's URL resolves to an entry keyed on B");
  Assert.equal(await entryBody(entryB), "b-2", "B's entry holds B's response");
  Assert.equal(
    entryMetaData(entryB, "no-vary-search"),
    'params=("u")',
    "B's entry kept its No-Vary-Search policy"
  );
});

// The full response to B is a cacheable redirect. Stored under A, a later load
// of A would follow it instead of serving A's own content.
add_task(async function test_cacheable_redirect_not_written_under_alias() {
  Services.cache2.clear();
  gRequests = [];

  const a = pathURI(REDIR_PATH, "u=a");
  const b = pathURI(REDIR_PATH, "u=b");

  Assert.equal(await fetchURI(a), "a", "A is primed");

  // B misses on its exact key, revalidates against A, and gets a redirect.
  Assert.equal(await fetchURI(b), "target", "B followed the redirect");
  Assert.ok(
    gRequests.some(r => r.path === TARGET_PATH),
    "the redirect target was fetched"
  );

  // A must not have become a redirect to the target.
  Assert.equal(await fetchURI(a), "a", "A still serves its own content");

  let entryA = await openEntry(a);
  Assert.equal(entryA.key, a, "A's URL still resolves to an entry keyed on A");
  Assert.equal(await entryBody(entryA), "a", "A's entry holds A's response");
});

// The legitimate No-Vary-Search path: the candidate entry is confirmed still
// fresh, so it is reused for B and stays the single stored representation.
add_task(async function test_not_modified_still_uses_the_candidate() {
  Services.cache2.clear();
  gRequests = [];

  const a = pathURI(NOTMOD_PATH, "u=a");
  const b = pathURI(NOTMOD_PATH, "u=b");

  Assert.equal(await fetchURI(a), "a", "A is primed");

  // B is served out of A's entry after a 304.
  Assert.equal(await fetchURI(b), "a", "B reused A's stored response");
  Assert.equal(gRequests.length, 2, "B revalidated on the network");
  Assert.ok(gRequests[1].conditional, "B's request was conditional");

  // A 304 must not spawn a second entry: the whole point of No-Vary-Search is
  // that the one entry serves both URLs, so B's URL still has to resolve to it.
  Assert.equal(
    await resolvedKey(b),
    a,
    "B's URL still resolves to A's entry, no second entry was created"
  );

  let entryA = await openEntry(a);
  Assert.equal(await entryBody(entryA), "a", "A's entry is unchanged");
});

// Every sibling covered by the policy revalidates against the same entry, and
// keeps doing so afterwards: a 304 must not disturb the No-Vary-Search metadata
// that put the entry in the secondary index in the first place.
add_task(async function test_siblings_keep_reusing_the_revalidated_entry() {
  Services.cache2.clear();
  gRequests = [];

  const a = pathURI(NOTMOD_PATH, "u=a");
  const b = pathURI(NOTMOD_PATH, "u=b");
  const c = pathURI(NOTMOD_PATH, "u=c");

  Assert.equal(await fetchURI(a), "a", "A is primed");
  Assert.equal(await fetchURI(b), "a", "B reused A's entry");
  Assert.equal(await fetchURI(c), "a", "C reused A's entry too");

  Assert.equal(gRequests.length, 3, "each sibling revalidated once");
  Assert.ok(gRequests[2].conditional, "C's request was conditional");

  // All three URLs still resolve to the one entry.
  Assert.equal(await resolvedKey(a), a, "A resolves to itself");
  Assert.equal(await resolvedKey(b), a, "B still resolves to A's entry");
  Assert.equal(await resolvedKey(c), a, "C still resolves to A's entry");

  let entryA = await openEntry(a);
  Assert.equal(
    entryMetaData(entryA, "no-vary-search"),
    'params=("u")',
    "the revalidated entry kept its No-Vary-Search policy"
  );
});

// A 304 may carry header updates. Merging them is a write to an entry stored
// under a different URL, but it is the correct one: it belongs to the response
// being revalidated, so it must land on the candidate rather than be diverted.
add_task(async function test_not_modified_refreshes_the_candidate() {
  Services.cache2.clear();
  gRequests = [];

  const a = pathURI(REFRESH_PATH, "u=a");
  const b = pathURI(REFRESH_PATH, "u=b");

  Assert.equal(await fetchURI(a), "a", "A is primed as must-revalidate");

  // B revalidates and the 304 extends freshness on the entry it reused.
  Assert.equal(await fetchURI(b), "a", "B reused A's stored response");
  Assert.equal(gRequests.length, 2, "B revalidated on the network");

  let entryA = await openEntry(a);
  Assert.equal(entryA.key, a, "the refreshed entry is still keyed on A");
  Assert.ok(
    entryMetaData(entryA, "response-head").includes("max-age=10000"),
    "the 304's headers were merged into the candidate"
  );

  // Because the merge landed on the entry both URLs share, neither needs the
  // network any more.
  Assert.equal(await fetchURI(a), "a", "A is now served fresh from cache");
  Assert.equal(await fetchURI(b), "a", "B is now served fresh from cache");
  Assert.equal(gRequests.length, 2, "no further network requests");
});

// An incomplete candidate cannot be completed with our bytes: appending them
// would put our content under the other URL, and a fresh entry would be
// missing the prefix. The entry must simply not be used.
add_task(async function test_partial_alias_entry_is_not_completed() {
  Services.cache2.clear();
  gRequests = [];

  const a = pathURI(PARTIAL_PATH, "u=a");
  const b = pathURI(PARTIAL_PATH, "u=b");

  const primed = await fetchTruncatedURI(a);
  Assert.equal(primed.length, PARTIAL_PREFIX_LEN, "A was stored truncated");

  gRequests = [];

  // B must be fetched whole from the network rather than range-completed out
  // of A's partial entry.
  Assert.equal(await fetchURI(b), gLongBody, "B got the full body");
  Assert.equal(gRequests.length, 1, "B took exactly one request");
  Assert.ok(!gRequests[0].range, "B did not issue a byte-range request");

  // A is still the truncated entry it was.
  let entryA = await openEntry(a);
  Assert.equal(entryA.key, a, "A's URL still resolves to an entry keyed on A");
  Assert.equal(
    (await entryBody(entryA)).length,
    PARTIAL_PREFIX_LEN,
    "A's entry still holds only its own partial content"
  );
});

// When the diverted response may not be stored, the replacement entry has to
// be memory-only; it must not be written to disk under either URL.
add_task(async function test_no_store_response_is_not_persisted() {
  Services.cache2.clear();
  gRequests = [];

  const a = pathURI(NOSTORE_PATH, "u=a");
  const b = pathURI(NOSTORE_PATH, "u=b");

  Assert.equal(await fetchURI(a), "a", "A is primed");
  Assert.equal(await fetchURI(b), "b", "B is fetched");

  // The response was diverted to an entry of B's own, and because it may not be
  // stored that entry has to be memory-only. Memory and disk entries share one
  // table, so this is a property of the entry rather than of the lookup.
  let entryB = await openEntry(b);
  Assert.equal(entryB.key, b, "B's response went to an entry keyed on B");
  Assert.ok(!entryB.persistent, "B's no-store entry is memory-only");

  // And A did not become B, nor lose its own persistence.
  Assert.equal(await fetchURI(a), "a", "A still serves its own content");
  let entryA = await openEntry(a);
  Assert.equal(entryA.key, a, "A's URL resolves to an entry keyed on A");
  Assert.equal(await entryBody(entryA), "a", "A's entry holds A's response");
  Assert.ok(entryA.persistent, "A is still a persistent entry");
});

// A 304 can also carry a No-Vary-Search value that changes the rule, so the
// candidate and the requested URL stop being equivalent. The change only
// arrives after the candidate has already been picked, so the request in flight
// is still answered from it -- which is what a 304 against that candidate's
// validator means. What must not happen is the index continuing to hand out the
// entry under the old rule afterwards.
add_task(async function test_304_changing_the_rule_updates_the_index() {
  Services.cache2.clear();
  gRequests = [];

  const a = pathURI(SWAP_PATH, "u=a");
  const b = pathURI(SWAP_PATH, "u=b");
  const c = pathURI(SWAP_PATH, "u=c");

  Assert.equal(await fetchURI(a), "u=a", 'A is primed under params=("u")');

  // Selected under the old rule and confirmed by a 304, so B is served from A.
  Assert.equal(await fetchURI(b), "u=a", "B was answered from the candidate");
  Assert.equal(gRequests.length, 2, "B revalidated on the network");

  // The refreshed rule is stored on the entry it belongs to, and nothing was
  // written under B.
  let entryA = await openEntry(a);
  Assert.equal(entryA.key, a, "the entry is still keyed on A");
  Assert.equal(await entryBody(entryA), "u=a", "A's content is untouched");
  Assert.equal(
    entryMetaData(entryA, "no-vary-search"),
    'params=("v")',
    "A's stored policy was updated by the 304"
  );

  // Under the new rule "u" is significant, so siblings no longer match A.
  Assert.equal(
    await resolvedKey(b),
    null,
    "B no longer resolves to A once the rule changed"
  );

  Assert.equal(await fetchURI(c), "u=c", "C is fetched on its own");
  Assert.equal(gRequests.length, 3, "C went to the network");
  Assert.equal(await resolvedKey(c), c, "C got its own entry");
});
