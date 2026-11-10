/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// "last-pb-context-exited" must only remove private Alt-Svc mappings.

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

let h2Port;

const ORIGIN_HOST = "foo.example.com";
const ALT_HOST = "alt1.example.com";

add_setup(async function setup() {
  do_get_profile();
  h2Port = Services.env.get("MOZHTTP2_PORT");
  Assert.notEqual(h2Port, null);
  Assert.notEqual(h2Port, "");
  Services.prefs.setBoolPref("network.http.altsvc.enabled", true);
  Services.prefs.setCharPref(
    "network.dns.localDomains",
    `${ORIGIN_HOST},${ALT_HOST}`
  );
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.http.altsvc.enabled");
    Services.prefs.clearUserPref("network.dns.localDomains");
  });
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");
});

async function loadAltSvcOrigin(originAttributes) {
  let chan = NetUtil.newChannel({
    uri: `https://${ORIGIN_HOST}:${h2Port}/altsvc-test`,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.setRequestHeader("x-altsvc", `${ALT_HOST}:${h2Port}`, false);

  chan.loadInfo.originAttributes = originAttributes;
  if (originAttributes.privateBrowsingId) {
    chan.notificationCallbacks = Cu.createPrivateLoadContext();
    chan.loadGroup = Cc["@mozilla.org/network/load-group;1"].createInstance(
      Ci.nsILoadGroup
    );
  }

  let req = await new Promise(resolve => {
    chan.asyncOpen(new ChannelListener(resolve, null, CL_ALLOW_UNKNOWN_CL));
  });
  Assert.equal(req.responseStatus, 200);
}

function hasAltSvcMapping(originAttributes) {
  let suffix = ChromeUtils.originAttributesToSuffix(originAttributes);
  let privateFlag = originAttributes.privateBrowsingId ? "P" : ".";
  return Services.io
    .getProtocolHandler("http")
    .QueryInterface(Ci.nsIHttpProtocolHandler)
    .altSvcCacheKeys.some(key =>
      key.startsWith(`https:${ORIGIN_HOST}:${h2Port}:${privateFlag}:${suffix}:`)
    );
}

add_task(async function test_last_pb_context_exited_clears_only_private() {
  const regular_oa = {};
  const private_oa = { privateBrowsingId: 1 };

  await loadAltSvcOrigin(regular_oa);
  await loadAltSvcOrigin(private_oa);

  await TestUtils.waitForCondition(
    () => hasAltSvcMapping(regular_oa) && hasAltSvcMapping(private_oa),
    "Regular and private Alt-Svc mappings were stored"
  );

  Services.obs.notifyObservers(null, "last-pb-context-exited");

  Assert.ok(hasAltSvcMapping(regular_oa), "Regular Alt-Svc mapping is kept");
  Assert.ok(
    !hasAltSvcMapping(private_oa),
    "Private Alt-Svc mapping is removed"
  );
});
