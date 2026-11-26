/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  NodeHTTPServer,
  NodeHTTPSServer,
  NodeHTTP2Server,
  NodeHTTPProxyServer,
  NodeHTTPSProxyServer,
  NodeHTTP2ProxyServer,
} = ChromeUtils.importESModule("resource://testing-common/NodeServer.sys.mjs");

/* import-globals-from head_cache.js */
/* import-globals-from head_cookies.js */
/* import-globals-from head_channels.js */

// We don't normally allow localhost channels to be proxied, but this
// is easier than updating all the certs and/or domains.
Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
registerCleanupFunction(() => {
  Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
});

function makeChan(uri) {
  let chan = NetUtil.newChannel({
    uri,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return chan;
}

async function test_cert_failure(server_creator, https_proxy) {
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");
  addCertFromFile(certdb, "proxy-ca.pem", "CTu,u,u");

  let server = new server_creator();
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });

  await server.registerPathHandler("/test", (req, resp) => {
    resp.writeHead(200);
    resp.end("done");
  });
  let url;
  if (server_creator == NodeHTTPServer) {
    url = `http://localhost:${server.port()}/test`;
  } else {
    url = `https://localhost:${server.port()}/test`;
  }
  let chan = makeChan(url);
  let req = await new Promise(resolve => {
    chan.asyncOpen(new ChannelListener(resolve, null, CL_ALLOW_UNKNOWN_CL));
  });
  equal(req.status, Cr.NS_OK);
  equal(req.QueryInterface(Ci.nsIHttpChannel).responseStatus, 200);
  let secinfo = req.securityInfo;
  if (server_creator == NodeHTTPServer) {
    if (!https_proxy) {
      Assert.equal(secinfo, null);
    } else {
      // In the case we are connecting to an insecure HTTP server
      // through a secure proxy, nsHttpChannel will have the security
      // info from the proxy.
      // We will discuss this behavir in bug 1785777.
      secinfo.QueryInterface(Ci.nsITransportSecurityInfo);
      Assert.equal(secinfo.serverCert.commonName, " Proxy Test Cert");
    }
  } else {
    secinfo.QueryInterface(Ci.nsITransportSecurityInfo);
    Assert.equal(secinfo.serverCert.commonName, "HTTP2 Test Cert");
  }
}

add_task(async function test_http() {
  await test_cert_failure(NodeHTTPServer, false);
});

add_task(async function test_https() {
  await test_cert_failure(NodeHTTPSServer, false);
});

add_task(async function test_http2() {
  await test_cert_failure(NodeHTTP2Server, false);
});

add_task(async function test_http_proxy_http_server() {
  let proxy = new NodeHTTPProxyServer();
  await proxy.start();
  registerCleanupFunction(() => {
    proxy.stop();
  });
  await test_cert_failure(NodeHTTPServer, false);
});

add_task(async function test_http_proxy_https_server() {
  let proxy = new NodeHTTPProxyServer();
  await proxy.start();
  registerCleanupFunction(() => {
    proxy.stop();
  });
  await test_cert_failure(NodeHTTPSServer, false);
});

add_task(async function test_http_proxy_http2_server() {
  let proxy = new NodeHTTPProxyServer();
  await proxy.start();
  registerCleanupFunction(() => {
    proxy.stop();
  });
  await test_cert_failure(NodeHTTP2Server, false);
});

add_task(async function test_https_proxy_http_server() {
  let proxy = new NodeHTTPSProxyServer();
  await proxy.start();
  registerCleanupFunction(() => {
    proxy.stop();
  });
  await test_cert_failure(NodeHTTPServer, true);
});

add_task(async function test_https_proxy_https_server() {
  let proxy = new NodeHTTPSProxyServer();
  await proxy.start();
  registerCleanupFunction(() => {
    proxy.stop();
  });
  await test_cert_failure(NodeHTTPSServer, true);
});

add_task(async function test_https_proxy_http2_server() {
  let proxy = new NodeHTTPSProxyServer();
  await proxy.start();
  registerCleanupFunction(() => {
    proxy.stop();
  });
  await test_cert_failure(NodeHTTP2Server, true);
});

add_task(async function test_http2_proxy_http_server() {
  let proxy = new NodeHTTP2ProxyServer();
  await proxy.start();
  registerCleanupFunction(() => {
    proxy.stop();
  });

  await test_cert_failure(NodeHTTPServer, true);
});

add_task(async function test_http2_proxy_https_server() {
  let proxy = new NodeHTTP2ProxyServer();
  await proxy.start();
  registerCleanupFunction(() => {
    proxy.stop();
  });

  await test_cert_failure(NodeHTTPSServer, true);
});

add_task(async function test_http2_proxy_http2_server() {
  let proxy = new NodeHTTP2ProxyServer();
  await proxy.start();
  registerCleanupFunction(() => {
    proxy.stop();
  });

  await test_cert_failure(NodeHTTP2Server, true);
});
