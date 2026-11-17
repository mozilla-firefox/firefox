/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Checks the SETTINGS_MAX_HEADER_LIST_SIZE value the server receives.

"use strict";

/* import-globals-from head_cache.js */
/* import-globals-from head_cookies.js */
/* import-globals-from head_channels.js */

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { NodeHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

const MAX_HEADER_LIST_SIZE_PREF = "network.http.http2.max_header_list_size";
const SEND_MAX_HEADER_LIST_SIZE_PREF =
  "network.http.http2.send_max_header_list_size";

add_setup(async function () {
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(MAX_HEADER_LIST_SIZE_PREF);
    Services.prefs.clearUserPref(SEND_MAX_HEADER_LIST_SIZE_PREF);
  });
});

// Each call uses a new server, so the request opens a new HTTP/2 session
// that picks up the current prefs. Returns the setting the server received.
async function receivedMaxHeaderListSize() {
  let server = new NodeHTTP2Server();
  await server.start();
  try {
    await server.registerPathHandler("/", (req, res) => {
      let settings = req.stream.session.remoteSettings;
      res.writeHead(200, { "content-type": "application/json" });
      res.end(
        JSON.stringify({ maxHeaderListSize: settings.maxHeaderListSize })
      );
    });

    let chan = NetUtil.newChannel({
      uri: `https://localhost:${server.port()}/`,
      loadUsingSystemPrincipal: true,
    }).QueryInterface(Ci.nsIHttpChannel);
    let { req, buffer } = await new Promise(resolve => {
      chan.asyncOpen(
        new ChannelListener(
          (request, data) => resolve({ req: request, buffer: data }),
          null,
          CL_ALLOW_UNKNOWN_CL
        )
      );
    });
    Assert.equal(req.status, Cr.NS_OK);
    Assert.equal(req.protocolVersion, "h2");
    return JSON.parse(buffer).maxHeaderListSize;
  } finally {
    await server.stop();
  }
}

add_task(async function test_default() {
  Services.prefs.clearUserPref(MAX_HEADER_LIST_SIZE_PREF);
  Services.prefs.clearUserPref(SEND_MAX_HEADER_LIST_SIZE_PREF);
  let received = await receivedMaxHeaderListSize();
  if (AppConstants.NIGHTLY_BUILD) {
    Assert.equal(received, 256 * 1024);
  } else {
    Assert.notEqual(received, 256 * 1024);
  }
});

add_task(async function test_sends_custom_limit() {
  Services.prefs.setIntPref(MAX_HEADER_LIST_SIZE_PREF, 100000);
  Services.prefs.setBoolPref(SEND_MAX_HEADER_LIST_SIZE_PREF, true);
  Assert.equal(await receivedMaxHeaderListSize(), 100000);
});

add_task(async function test_not_sent_when_send_pref_disabled() {
  Services.prefs.setIntPref(MAX_HEADER_LIST_SIZE_PREF, 100000);
  Services.prefs.setBoolPref(SEND_MAX_HEADER_LIST_SIZE_PREF, false);
  let received = await receivedMaxHeaderListSize();
  Assert.notEqual(received, 100000);
});

add_task(async function test_not_sent_when_limit_disabled() {
  Services.prefs.setIntPref(MAX_HEADER_LIST_SIZE_PREF, 0);
  Services.prefs.setBoolPref(SEND_MAX_HEADER_LIST_SIZE_PREF, true);
  let received = await receivedMaxHeaderListSize();
  Assert.notEqual(received, 0);
});
