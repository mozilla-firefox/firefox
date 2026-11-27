"use strict";

const { HTTP3Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "1",
  "43"
);

let { promiseShutdownManager, promiseStartupManager } = AddonTestUtils;

function readFile(file) {
  let fstream = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsIFileInputStream
  );
  fstream.init(file, -1, 0, 0);
  let data = NetUtil.readInputStreamToString(fstream, fstream.available());
  fstream.close();
  return data;
}

function addCertFromFile(certdb, filename, trustString) {
  let certFile = do_get_file(filename, false);
  let pem = readFile(certFile)
    .replace(/-----BEGIN CERTIFICATE-----/, "")
    .replace(/-----END CERTIFICATE-----/, "")
    .replace(/[\r\n]/g, "");
  certdb.addCertFromBase64(pem, trustString);
}

let proxyHost;
let proxyPort;
let h3Port;

/**
 * Sets up HTTP3 proxy configuration for extension tests
 */
add_setup(async function setup() {
  Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
  Services.prefs.setBoolPref("network.dns.disableIPv6", true);
  Services.prefs.setIntPref("network.webtransport.datagram_size", 1500);
  Services.prefs.setCharPref("network.dns.localDomains", "foo.example.com");

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(
    certdb,
    "../../../../../netwerk/test/unit/proxy-ca.pem",
    "CTu,u,u"
  );
  addCertFromFile(
    certdb,
    "../../../../../netwerk/test/unit/http2-ca.pem",
    "CTu,u,u"
  );

  const proxyServer = new HTTP3Server();
  registerCleanupFunction(() => proxyServer.stop());
  h3Port = await proxyServer.start(
    Services.env.get("MOZ_HTTP3_SERVER_PATH"),
    Services.env.get("MOZ_HTTP3_CERT_DB_PATH")
  );

  proxyHost = "foo.example.com";
  proxyPort = proxyServer.masque_proxy_port();

  // A dummy request to make sure AltSvcCache::mStorage is ready.
  // It doesn't really matter if it succeeds or fails.
  try {
    await fetch("https://localhost");
  } catch (e) {}

  // In case the prefs have a different value by default.
  Services.prefs.setBoolPref(
    "extensions.webextensions.early_background_wakeup_on_request",
    false
  );

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
  });
});

add_task(async function test_connect_udp() {
  await promiseStartupManager();

  function background(proxyInfo) {
    browser.proxy.onRequest.addListener(
      details => {
        browser.test.log(`onRequest: ${JSON.stringify(details)}`);
        browser.test.sendMessage("onRequest", details.url);
        return proxyInfo;
      },
      { urls: ["<all_urls>"] }
    );
  }

  let proxyInfo = {
    host: proxyHost,
    port: proxyPort,
    type: "masque",
    masqueTemplate: "/.well-known/masque/udp/{target_host}/{target_port}/",
    connectionIsolationKey: "masque-udp",
  };

  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      permissions: ["proxy", "<all_urls>"],
    },
    background: `(${background})(${JSON.stringify(proxyInfo)})`,
  });

  await extension.startup();

  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    `alt1.example.com;h3=:${h3Port}`
  );

  // Test the HTTP3 connection through the extension proxy
  let response = await ExtensionTestUtils.fetch(
    `https://alt1.example.com:${h3Port}/no_body`,
    `https://alt1.example.com:${h3Port}/`
  );
  Assert.equal(response, "Hello World", "HTTP3 proxy request succeeded");

  equal(
    await extension.awaitMessage("onRequest"),
    `https://alt1.example.com:${h3Port}/no_body`,
    "Observed proxy.onRequest for speculative document request"
  );
  equal(
    await extension.awaitMessage("onRequest"),
    `https://alt1.example.com:${h3Port}/no_body`,
    "Observed proxy.onRequest for document request"
  );
  equal(
    await extension.awaitMessage("onRequest"),
    `https://alt1.example.com:${h3Port}/`,
    "Observed proxy.onRequest for fetch() call"
  );

  await extension.unload();
  await promiseShutdownManager();
});
