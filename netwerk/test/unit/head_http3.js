/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from head_channels.js */
/* import-globals-from head_cookies.js */

const {
  HTTP3Server: HeadHTTP3Server,
  NodeHTTP2Server: HeadNodeHTTP2Server,
  NodeHTTP2ProxyServer: HeadNodeHTTP2ProxyServer,
  startOnSharedPort: headStartOnSharedPort,
} = ChromeUtils.importESModule("resource://testing-common/NodeServer.sys.mjs");

const SERVER_PATH = Services.env.get("MOZ_HTTP3_SERVER_PATH");
const CERT_DB_PATH = Services.env.get("MOZ_HTTP3_CERT_DB_PATH");

async function with_http3_server(selectPort) {
  const server = new HeadHTTP3Server();
  const h3Port = await server.start(SERVER_PATH, CERT_DB_PATH);
  registerCleanupFunction(() => server.stop()); // returns a Promise; cleanup will await it
  return selectPort(server, h3Port);
}

async function create_h3_server() {
  return with_http3_server((_, h3Port) => h3Port);
}

// Starts an HTTP/2 server, stopped at cleanup. |handlers| maps each path to
// its handler, and defaults to answering "/" with a 200.
async function create_h2_server(
  handlers = {
    "/": (req, resp) => {
      resp.writeHead(200, {
        "Content-Type": "text/plain",
        "Content-Length": 2,
      });
      resp.end("ok");
    },
  }
) {
  // The server installs its CA cert, which needs a profile.
  do_get_profile();
  const server = new HeadNodeHTTP2Server();
  await server.start();
  registerCleanupFunction(() => server.stop());
  for (const [path, handler] of Object.entries(handlers)) {
    await server.registerPathHandler(path, handler);
  }
  return server;
}

// Also starts the HTTP/2 proxy that MASQUE falls back to, on the same port.
async function create_masque_proxy_server() {
  const server = new HeadHTTP3Server();
  const fallbackProxy = new HeadNodeHTTP2ProxyServer();
  const stop = async () => {
    await fallbackProxy.stop();
    await server.stop();
  };
  await headStartOnSharedPort(
    () => server.start(SERVER_PATH, CERT_DB_PATH),
    () => fallbackProxy.startWithoutProxyFilter(server.masque_proxy_port()),
    stop
  );
  registerCleanupFunction(stop);
  return { masqueProxyPort: server.masque_proxy_port() };
}

async function http3_setup_tests(http3version, reload) {
  do_get_profile();

  let h3Port;
  let isAndroid = mozinfo.os == "android";
  // On Android, we don't have a way to start the server on the host on demand.
  if (reload && !isAndroid) {
    h3Port = await create_h3_server();
  } else {
    h3Port = Services.env.get("MOZHTTP3_PORT");
  }

  Assert.notEqual(h3Port, null);
  Assert.notEqual(h3Port, "");

  let h3Route = "foo.example.com:" + h3Port;

  Services.prefs.setBoolPref("network.http.http3.enable", true);
  if (isAndroid) {
    const overrideService = Cc[
      "@mozilla.org/network/native-dns-override;1"
    ].getService(Ci.nsINativeDNSResolverOverride);
    // To connect to the http3Server running on the host, we need to set this
    // special IP address.
    overrideService.addIPOverride("foo.example.com", "10.0.2.2");
  } else {
    Services.prefs.setCharPref("network.dns.localDomains", "foo.example.com");
  }
  Services.prefs.setBoolPref("network.dns.disableIPv6", true);
  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    `foo.example.com;${http3version}=:${h3Port}`
  );

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  // `../unit/` so that unit_ipc tests can use as well
  addCertFromFile(certdb, "../unit/http2-ca.pem", "CTu,u,u");

  await setup_altsvc("https://foo.example.com/", h3Route, http3version);
}

function makeChan(uri) {
  let chan = NetUtil.newChannel({
    uri,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return chan;
}

let CheckHttp3Listener = function () {};

CheckHttp3Listener.prototype = {
  expectedRoute: "",
  http3version: "",

  onStartRequest: function testOnStartRequest() {},

  onDataAvailable: function testOnDataAvailable(request, stream, off, cnt) {
    read_stream(stream, cnt);
  },

  onStopRequest: function testOnStopRequest(request) {
    let routed = "NA";
    try {
      routed = request.getRequestHeader("Alt-Used");
    } catch (e) {}
    dump("routed is " + routed + "\n");

    if (routed == this.expectedRoute) {
      let httpVersion = "";
      try {
        httpVersion = request.protocolVersion;
      } catch (e) {}
      Assert.equal(httpVersion, this.http3version);
      this.finish(true);
    } else {
      dump("try again to get alt svc mapping\n");
      this.finish(false);
    }
  },
};

async function setup_altsvc(uri, expectedRoute, http3version) {
  let result = false;
  do {
    let chan = makeChan(uri);
    let listener = new CheckHttp3Listener();
    listener.expectedRoute = expectedRoute;
    listener.http3version = http3version;
    result = await altsvcSetupPromise(chan, listener);
    dump("results=" + result);
  } while (result === false);
}

function altsvcSetupPromise(chan, listener) {
  return new Promise(resolve => {
    function finish(result) {
      resolve(result);
    }
    listener.finish = finish;
    chan.asyncOpen(listener);
  });
}

function http3_clear_prefs() {
  Services.prefs.clearUserPref("network.http.http3.enable");
  Services.prefs.clearUserPref("network.dns.localDomains");
  Services.prefs.clearUserPref("network.dns.disableIPv6");
  Services.prefs.clearUserPref(
    "network.http.http3.alt-svc-mapping-for-testing"
  );
  Services.prefs.clearUserPref("network.http.http3.support_version1");
  dump("cleanup done\n");
}
