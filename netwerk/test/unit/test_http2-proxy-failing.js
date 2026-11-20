/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Test stream failure on the session to the proxy:
 *  - Test the case the error closes the affected stream only
 *  - Test the case the error closes the whole session and cancels existing
 *    streams.
 */

"use strict";

const { NodeLegacyHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

const pps = Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();

let filter;

class ProxyFilter {
  constructor(type, host, port, flags) {
    this._type = type;
    this._host = host;
    this._port = port;
    this._flags = flags;
    this.QueryInterface = ChromeUtils.generateQI(["nsIProtocolProxyFilter"]);
  }
  applyFilter(uri, pi, cb) {
    cb.onProxyFilterResult(
      pps.newProxyInfo(
        this._type,
        this._host,
        this._port,
        null,
        null,
        this._flags,
        1000,
        null
      )
    );
  }
}

function createPrincipal(url) {
  var ssm = Services.scriptSecurityManager;
  try {
    return ssm.createContentPrincipal(Services.io.newURI(url), {});
  } catch (e) {
    return null;
  }
}

function make_channel(url) {
  return Services.io.newChannelFromURIWithProxyFlags(
    Services.io.newURI(url),
    null,
    16,
    null,
    createPrincipal(url),
    createPrincipal(url),
    Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT,
    Ci.nsIContentPolicy.TYPE_OTHER
  );
}

function get_response(channel, flags = CL_ALLOW_UNKNOWN_CL, delay = 0) {
  return new Promise(resolve => {
    var listener = new ChannelListener(
      (request, data) => {
        request.QueryInterface(Ci.nsIHttpChannel);
        const status = request.status;
        const http_code = status ? undefined : request.responseStatus;
        request.QueryInterface(Ci.nsIProxiedChannel);
        const proxy_connect_response_code =
          request.httpProxyConnectResponseCode;
        resolve({ status, http_code, data, proxy_connect_response_code });
      },
      null,
      flags
    );
    if (delay > 0) {
      do_timeout(delay, function () {
        channel.asyncOpen(listener);
      });
    } else {
      channel.asyncOpen(listener);
    }
  });
}

add_task(async function setup() {
  // Set to allow the cert presented by our H2 server
  do_get_profile();

  let proxy = new NodeLegacyHTTP2Server();
  await proxy.start();
  registerCleanupFunction(async () => {
    await proxy.stop();
  });
  await proxy.setConnectHandler((req, res) => {
    switch (req.headers.host) {
      case "illegalhpacksoft.example.com:80":
        // node-http2 encodes this header illegally, failing only the stream.
        res.setHeader("x-softillegalhpack", "true");
        res.writeHead(200);
        res.end("ok");
        return;
      case "illegalhpackhard.example.com:80":
        // node-http2 encodes this header illegally, failing the session.
        res.setHeader("x-hardillegalhpack", "true");
        res.writeHead(200);
        res.end("ok");
        return;
      case "750.example.com:80":
        // Accepts the tunnel and answers the request through it after 750ms.
        // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
        setTimeout(() => res.end("HTTP/1.1 200\n\r\n\r"), 750);
        return;
      case "h11required.com:80":
        res.stream.reset("HTTP_1_1_REQUIRED");
        return;
    }
    res.writeHead(404);
    res.end();
  });
  let proxy_port = proxy.port();

  Services.prefs.setBoolPref("network.http.http2.enabled", true);
  // make all native resolve calls "secretly" resolve localhost instead
  Services.prefs.setBoolPref("network.dns.native-is-localhost", true);

  filter = new ProxyFilter("https", "localhost", proxy_port, 16);
  pps.registerFilter(filter, 10);
});

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("network.http.http2.enabled");
  Services.prefs.clearUserPref("network.dns.native-is-localhost");

  pps.unregisterFilter(filter);
});

add_task(
  async function proxy_server_stream_soft_failure_multiple_streams_not_affected() {
    let should_succeed = get_response(make_channel(`http://750.example.com`));
    const failed = await get_response(
      make_channel(`http://illegalhpacksoft.example.com`),
      CL_EXPECT_FAILURE,
      20
    );

    const succeeded = await should_succeed;

    Assert.equal(failed.status, Cr.NS_ERROR_ILLEGAL_VALUE);
    Assert.equal(failed.proxy_connect_response_code, 0);
    Assert.equal(failed.http_code, undefined);
    Assert.equal(succeeded.status, Cr.NS_OK);
    Assert.equal(succeeded.proxy_connect_response_code, 200);
    Assert.equal(succeeded.http_code, 200);
  }
);

add_task(
  async function proxy_server_stream_hard_failure_multiple_streams_affected() {
    let should_failed = get_response(
      make_channel(`http://750.example.com`),
      CL_EXPECT_FAILURE
    );
    const failed1 = await get_response(
      make_channel(`http://illegalhpackhard.example.com`),
      CL_EXPECT_FAILURE
    );

    const failed2 = await should_failed;

    Assert.equal(failed1.status, 0x804b0053);
    Assert.equal(failed1.proxy_connect_response_code, 0);
    Assert.equal(failed1.http_code, undefined);
    Assert.equal(failed2.status, 0x804b0053);
    Assert.equal(failed2.proxy_connect_response_code, 0);
    Assert.equal(failed2.http_code, undefined);
  }
);

add_task(async function test_http2_h11required_stream() {
  let should_failed = await get_response(
    make_channel(`http://h11required.com`),
    CL_EXPECT_FAILURE
  );

  // See HTTP/1.1 connect handler in moz-http2.js. The handler returns
  // "404 Not Found", so the expected error code is NS_ERROR_UNKNOWN_HOST.
  Assert.equal(should_failed.status, Cr.NS_ERROR_UNKNOWN_HOST);
  Assert.equal(should_failed.proxy_connect_response_code, 404);
  Assert.equal(should_failed.http_code, undefined);
});
