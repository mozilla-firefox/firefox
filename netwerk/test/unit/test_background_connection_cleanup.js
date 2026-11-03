/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { NodeHTTPServer, NodeHTTP2Server, with_node_servers } =
  ChromeUtils.importESModule("resource://testing-common/NodeServer.sys.mjs");

const PREF = "network.http.close_connections_on_background";

add_setup(async function setup() {
  do_get_profile();
});

async function portCount(origin) {
  let chan = NetUtil.newChannel({
    uri: `${origin}/`,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  let [req, buffer] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
  Assert.equal(req.responseStatus, 200);
  return parseInt(buffer);
}

async function registerPortCounter(server) {
  await server.registerPathHandler("/", (req, resp) => {
    global.ports.add(req.socket.remotePort);
    resp.writeHead(200, { "Content-Type": "text/plain" });
    resp.end(`${global.ports.size}`);
  });
}

add_task(async function test_pref_default() {
  Assert.equal(Services.prefs.getDefaultBranch("").getBoolPref(PREF), false);
});

add_task(async function test_background_connection_cleanup() {
  await with_node_servers([NodeHTTPServer, NodeHTTP2Server], async server => {
    await registerPortCounter(server);
    for (let [closeOnBackground, expected] of [
      [true, 2],
      [false, server.version() == "h2" ? 1 : 2],
    ]) {
      Services.prefs.setBoolPref(PREF, closeOnBackground);
      await server.execute("global.ports = new Set()");
      await portCount(server.origin());
      Services.obs.notifyObservers(null, "application-background");
      Assert.equal(
        await portCount(server.origin()),
        expected,
        `${server.version()} ${PREF}=${closeOnBackground}`
      );
    }
  });
});

add_task(async function test_background_coalesced_connection() {
  Services.prefs.setCharPref(
    "network.dns.localDomains",
    "foo.example.com, alt1.example.com"
  );
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.dns.localDomains");
  });
  await with_node_servers([NodeHTTP2Server], async server => {
    await registerPortCounter(server);
    const foo = `https://foo.example.com:${server.port()}`;
    const alt1 = `https://alt1.example.com:${server.port()}`;
    for (let [closeOnBackground, expected] of [
      [false, 1],
      [true, 2],
    ]) {
      Services.prefs.setBoolPref(PREF, closeOnBackground);
      await server.execute("global.ports = new Set()");
      await portCount(foo);
      Assert.equal(await portCount(alt1), 1, "alt1 coalesced onto foo");
      Services.obs.notifyObservers(null, "application-background");
      Assert.equal(
        await portCount(alt1),
        expected,
        `coalesced ${PREF}=${closeOnBackground}`
      );
    }
  });
});
