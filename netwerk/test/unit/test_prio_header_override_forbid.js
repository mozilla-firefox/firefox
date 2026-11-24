/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

add_setup(() => {
  Services.prefs.setBoolPref("network.http.priority_header.enabled", true);
});

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("network.http.priority_header.enabled");
});

const { NodeHTTPSServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

function channelOpenPromise(chan, flags) {
  return new Promise(resolve => {
    function finish(req, buffer) {
      resolve([req, buffer]);
    }
    chan.asyncOpen(new ChannelListener(finish, null, flags));
  });
}

async function test_flag_override_priority(
  prioHeaderValue,
  listenerPriorityValue,
  flag
) {
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");

  let server = new NodeHTTPSServer();
  await server.start();

  await server.registerPathHandler("/test", (req, resp) => {
    resp.writeHead(200);
    resp.end(req.headers.priority);
  });

  let request = NetUtil.newChannel({
    uri: `https://localhost:${server.port()}/test`,
    loadUsingSystemPrincipal: true,
  });
  let chan = request.QueryInterface(Ci.nsIHttpChannel);

  if (prioHeaderValue !== null) {
    request.setRequestHeader("Priority", prioHeaderValue, false);
  }

  // Setting flags should not override if priority is already set
  let cos = chan.QueryInterface(Ci.nsIClassOfService);
  cos.addClassFlags(flag);

  let [req, buff] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
  Assert.equal(req.status, Cr.NS_OK);
  Assert.equal(buff, listenerPriorityValue); // Check buffer
  Assert.equal(req.getRequestHeader("Priority"), listenerPriorityValue);
  await server.stop();
}

add_task(async function test_prio_header_no_override() {
  await test_flag_override_priority(
    null,
    "u=2, i",
    Ci.nsIClassOfService.Leader
  );
});

add_task(async function test_prio_header_no_override2() {
  await test_flag_override_priority(
    null,
    "u=4, i",
    Ci.nsIClassOfService.Follower
  );
});

add_task(async function test_prio_header_override() {
  await test_flag_override_priority("foo", "foo", Ci.nsIClassOfService.Leader);
});

add_task(async function test_prio_header_override2() {
  await test_flag_override_priority(
    "foo",
    "foo",
    Ci.nsIClassOfService.Follower
  );
});
