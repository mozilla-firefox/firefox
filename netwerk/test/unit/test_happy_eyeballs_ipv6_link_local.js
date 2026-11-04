/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Bug 2083080: a host that resolves only to an IPv6 link-local address must be
// reachable with Happy Eyeballs. A link-local address is only usable together
// with its scope id (the interface), which the resolver supplies.
//
// The test resolves a name to the link-local address of the loopback interface
// (fe80::1%lo0 on macOS) through the native DNS override and serves it from a
// server listening on all IPv6 interfaces.

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

const HOST = "link-local.example.com";
const LINK_LOCAL_ADDR = "fe80::1%lo0";
const RESPONSE = "link-local response";

const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);

let httpServer;

add_setup(async function setup() {
  httpServer = new HttpServer();
  httpServer.registerPathHandler("/", (metadata, response) => {
    response.setHeader("Content-Type", "text/plain", false);
    response.write(RESPONSE);
  });
  httpServer._start(-1, "[::]");
  httpServer.identity.add("http", HOST, httpServer.identity.primaryPort);

  override.addIPOverride(HOST, LINK_LOCAL_ADDR);

  Services.prefs.setBoolPref("network.dns.disableIPv6", false);
  Services.prefs.setBoolPref("network.http.http3.enable", false);
  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);

  registerCleanupFunction(async () => {
    override.clearOverrides();
    Services.prefs.clearUserPref("network.dns.disableIPv6");
    Services.prefs.clearUserPref("network.http.http3.enable");
    Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
    await httpServer.stop();
  });
});

function load() {
  let chan = NetUtil.newChannel({
    uri: `http://${HOST}:${httpServer.identity.primaryPort}/`,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  return new Promise(resolve => {
    let buffer = "";
    chan.asyncOpen({
      QueryInterface: ChromeUtils.generateQI([
        "nsIStreamListener",
        "nsIRequestObserver",
      ]),
      onStartRequest() {},
      onDataAvailable(request, stream, offset, count) {
        buffer += read_stream(stream, count);
      },
      onStopRequest(request, status) {
        resolve({ status, buffer });
      },
    });
  });
}

async function runTest(aHappyEyeballs) {
  Services.prefs.setBoolPref(
    "network.http.happy_eyeballs_enabled",
    aHappyEyeballs
  );
  Services.dns.clearCache(true);
  Services.obs.notifyObservers(null, "net:cancel-all-connections");

  let { status, buffer } = await load();
  info(`status=0x${status.toString(16)}`);
  Assert.equal(status, Cr.NS_OK, "load of the link-local host succeeds");
  Assert.equal(buffer, RESPONSE, "got the response from the server");
}

add_task(async function test_link_local_without_happy_eyeballs() {
  await runTest(false);
});

add_task(async function test_link_local_with_happy_eyeballs() {
  await runTest(true);
});
