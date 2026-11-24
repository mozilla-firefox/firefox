/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

// Tests the default value of the RFC 9218 incremental flag of the
// Priority header.

"use strict";

const { NodeHTTPSServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let server = null;

add_setup(async function setup() {
  Services.prefs.setBoolPref("network.http.priority_header.enabled", true);

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");

  server = new NodeHTTPSServer();
  await server.start();
  // Echoes the Priority header back, using the content type the client asked
  // for so that loads of types with a strict MIME check (modules, JSON
  // modules, ...) are not rejected before we get to see the request.
  await server.registerPathHandler("/test", (req, resp) => {
    let contentType = new URL(req.url, "https://localhost").searchParams.get(
      "ct"
    );
    resp.writeHead(200, { "Content-Type": contentType });
    resp.end(req.headers.priority ?? "");
  });
});

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("network.http.priority_header.enabled");
  Services.prefs.clearUserPref("network.http.priority.incremental_by_default");
  await server.stop();
});

function channelOpenPromise(chan) {
  return new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener(
        (req, buffer) => resolve([req, buffer]),
        null,
        CL_ALLOW_UNKNOWN_CL
      )
    );
  });
}

// Opens a channel of the given content policy type and returns the Priority
// header the server received.
async function priorityHeaderFor(
  { type, contentType = "text/plain" },
  incrementalOverride
) {
  let chan = NetUtil.newChannel({
    uri: `https://localhost:${server.port()}/test?ct=${encodeURIComponent(
      contentType
    )}`,
    loadUsingSystemPrincipal: true,
    contentPolicyType: type,
  }).QueryInterface(Ci.nsIHttpChannel);

  if (incrementalOverride !== undefined) {
    chan.QueryInterface(Ci.nsIClassOfService).incremental = incrementalOverride;
  }

  let [req, buffer] = await channelOpenPromise(chan);
  Assert.equal(req.status, Cr.NS_OK, `type ${type} loaded`);
  // No class flags means u=4. We expect the header to be always present.
  Assert.stringContains(buffer, "u=4", `type ${type} sent u=4`);
  return buffer;
}

function isIncremental(header) {
  return header.split(",").some(item => item.trim() == "i");
}

const JS = "application/javascript";
const FETCH = { type: Ci.nsIContentPolicy.TYPE_FETCH };
const SCRIPT = { type: Ci.nsIContentPolicy.TYPE_SCRIPT, contentType: JS };

// Resources that the page can start using before they are complete must be
// incremental.
add_task(async function test_incremental_types() {
  for (let resource of [
    FETCH,
    { type: Ci.nsIContentPolicy.TYPE_OTHER },
    { type: Ci.nsIContentPolicy.TYPE_IMAGE, contentType: "image/png" },
    { type: Ci.nsIContentPolicy.TYPE_INTERNAL_IMAGE, contentType: "image/png" },
    { type: Ci.nsIContentPolicy.TYPE_MEDIA, contentType: "video/mp4" },
    { type: Ci.nsIContentPolicy.TYPE_XMLHTTPREQUEST },
  ]) {
    let header = await priorityHeaderFor(resource);
    Assert.ok(
      isIncremental(header),
      `type ${resource.type} is incremental: ${header}`
    );
  }
});

// Resources that are only usable once fully received keep the RFC default of
// non-incremental.
add_task(async function test_non_incremental_types() {
  for (let resource of [
    SCRIPT,
    { type: Ci.nsIContentPolicy.TYPE_INTERNAL_SCRIPT, contentType: JS },
    { type: Ci.nsIContentPolicy.TYPE_INTERNAL_MODULE, contentType: JS },
    { type: Ci.nsIContentPolicy.TYPE_STYLESHEET, contentType: "text/css" },
    {
      type: Ci.nsIContentPolicy.TYPE_INTERNAL_STYLESHEET,
      contentType: "text/css",
    },
    { type: Ci.nsIContentPolicy.TYPE_FONT, contentType: "font/woff2" },
    { type: Ci.nsIContentPolicy.TYPE_UA_FONT, contentType: "font/woff2" },
    { type: Ci.nsIContentPolicy.TYPE_XSLT, contentType: "text/xml" },
    { type: Ci.nsIContentPolicy.TYPE_DTD, contentType: "application/xml-dtd" },
    { type: Ci.nsIContentPolicy.TYPE_JSON, contentType: "application/json" },
    { type: Ci.nsIContentPolicy.TYPE_TEXT },
    {
      type: Ci.nsIContentPolicy.TYPE_WEB_MANIFEST,
      contentType: "application/manifest+json",
    },
    {
      type: Ci.nsIContentPolicy.TYPE_WEB_IDENTITY,
      contentType: "application/json",
    },
  ]) {
    let header = await priorityHeaderFor(resource);
    Assert.ok(
      !isIncremental(header),
      `type ${resource.type} is not incremental: ${header}`
    );
  }
});

add_task(async function test_explicit_override() {
  Assert.ok(
    !isIncremental(await priorityHeaderFor(FETCH, false)),
    "an explicit false overrides the incremental default"
  );
  Assert.ok(
    isIncremental(await priorityHeaderFor(SCRIPT, true)),
    "an explicit true overrides the non-incremental default"
  );
});

add_task(async function test_pref_disabled() {
  Services.prefs.setBoolPref(
    "network.http.priority.incremental_by_default",
    false
  );
  Assert.ok(
    !isIncremental(await priorityHeaderFor(FETCH)),
    "no incremental by default when the pref is off"
  );
  Assert.ok(
    isIncremental(await priorityHeaderFor(FETCH, true)),
    "an explicit true still works when the pref is off"
  );
  Services.prefs.clearUserPref("network.http.priority.incremental_by_default");
});
