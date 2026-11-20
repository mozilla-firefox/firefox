/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This test checks following expectations when using HTTP/2 proxy:
 *
 * - when we request https access, we don't create different sessions for
 *   different origins, only new tunnels inside a single session
 * - when the isolation key (`proxy_isolation`) is changed, new single session
 *   is created for new requests to same origins as before
 * - error code returned from the tunnel (a proxy error - not end-server
 *   error!) doesn't kill the existing session
 * - check we are seeing expected nsresult error codes on channels
 *   (nsIChannel.status) corresponding to different proxy status code
 *   responses (502, 504, 407, ...)
 * - check we don't try to ask for credentials or otherwise authenticate to
 *   the proxy when 407 is returned and there is no Proxy-Authenticate
 *   response header sent
 * - a stream reset for a connect stream to the proxy does not cause session to
 *   be closed and the request through the proxy will failed.
 * - a "soft" stream error on a connection to the origin server will close the
 *   stream, but it will not close niether the HTTP/2 session to the proxy nor
 *   to the origin server.
 * - a "hard" stream error on a connection to the origin server will close the
 *   HTTP/2 session to the origin server, but it will not close the HTTP/2
 *   session to the proxy.
 */

/* global serverPort */

"use strict";

// We don't normally allow localhost channels to be proxied, but this
// is easier than updating all the certs and/or domains.
Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
registerCleanupFunction(() => {
  Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
});

const pps = Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();
const { ADB, NodeLegacyHTTP2Server, NodeServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let proxy_port;
let filter;
let proxy;

// See moz-http2
const proxy_auth = "authorization-token";
let proxy_isolation;

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
        proxy_auth,
        proxy_isolation,
        this._flags,
        1000,
        null
      )
    );
  }
}

class UnxpectedAuthPrompt2 {
  constructor(signal) {
    this.signal = signal;
    this.QueryInterface = ChromeUtils.generateQI(["nsIAuthPrompt2"]);
  }
  asyncPromptAuth() {
    this.signal.triggered = true;
    throw Components.Exception("", Cr.ERROR_UNEXPECTED);
  }
}

class SimpleAuthPrompt2 {
  constructor(signal) {
    this.signal = signal;
    this.QueryInterface = ChromeUtils.generateQI(["nsIAuthPrompt2"]);
  }
  asyncPromptAuth(channel, callback, context, encryptionLevel, authInfo) {
    this.signal.triggered = true;
    executeSoon(function () {
      authInfo.username = "user";
      authInfo.password = "pass";
      callback.onAuthAvailable(context, authInfo);
    });
  }
}

class AuthRequestor {
  constructor(prompt) {
    this.prompt = prompt;
    this.QueryInterface = ChromeUtils.generateQI(["nsIInterfaceRequestor"]);
  }
  getInterface(iid) {
    if (iid.equals(Ci.nsIAuthPrompt2)) {
      return this.prompt();
    }
    throw Components.Exception("", Cr.NS_ERROR_NO_INTERFACE);
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
  return NetUtil.newChannel({
    uri: url,
    loadingPrincipal: createPrincipal(url),
    securityFlags: Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT,
    // Using TYPE_DOCUMENT for the authentication dialog test, it'd be blocked for other types
    contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
  });
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

let initial_session_count = 0;

class http2ProxyCode {
  static startNewProxy() {
    const fs = require("fs");
    const options = {
      key: fs.readFileSync(__dirname + "/http2-cert.key"),
      cert: fs.readFileSync(__dirname + "/http2-cert.pem"),
    };
    const http2 = require("http2");
    global.proxy = http2.createSecureServer(options);
    this.setupProxy();
    return ADB.listenAndForwardPort(proxy, 0).then(port => {
      return { port, success: true };
    });
  }

  static closeProxy() {
    proxy.closeSockets();
    return new Promise(resolve => {
      proxy.close(resolve);
    });
  }

  static proxySessionCount() {
    if (!proxy) {
      return 0;
    }
    return proxy.proxy_session_count;
  }

  static proxySessionToOriginServersCount() {
    if (!proxy) {
      return 0;
    }
    return proxy.sessionToOriginServersCount;
  }

  static setupProxy() {
    if (!proxy) {
      throw new Error("proxy is null");
    }
    proxy.proxy_session_count = 0;
    proxy.sessionToOriginServersCount = 0;
    proxy.on("session", () => {
      ++proxy.proxy_session_count;
    });

    // We need to track active connections so we can forcefully close keep-alive
    // connections when shutting down the proxy.
    proxy.socketIndex = 0;
    proxy.socketMap = {};
    proxy.on("connection", function (socket) {
      let index = proxy.socketIndex++;
      proxy.socketMap[index] = socket;
      socket.on("close", function () {
        delete proxy.socketMap[index];
      });
    });
    proxy.closeSockets = function () {
      for (let i in proxy.socketMap) {
        proxy.socketMap[i].destroy();
      }
    };

    proxy.on("stream", (stream, headers) => {
      if (headers[":method"] !== "CONNECT") {
        // Only accept CONNECT requests
        stream.respond({ ":status": 405 });
        stream.end();
        return;
      }

      const target = headers[":authority"];

      const authorization_token = headers["proxy-authorization"];
      if (target == "407.example.com:443") {
        stream.respond({ ":status": 407 });
        // Deliberately send no Proxy-Authenticate header
        stream.end();
        return;
      }
      if (target == "407.basic.example.com:443") {
        // we want to return a different response than 407 to not re-request
        // credentials (and thus loop) but also not 200 to not let the channel
        // attempt to waste time connecting a non-existing https server - hence
        // 418 I'm a teapot :)
        if ("Basic dXNlcjpwYXNz" == authorization_token) {
          stream.respond({ ":status": 418 });
          stream.end();
          return;
        }
        stream.respond({
          ":status": 407,
          "proxy-authenticate": "Basic realm='foo'",
        });
        stream.end();
        return;
      }
      if (target == "404.example.com:443") {
        // 404 Not Found, a response code that a proxy should return when the host can't be found
        stream.respond({ ":status": 404 });
        stream.end();
        return;
      }
      if (target == "429.example.com:443") {
        // 429 Too Many Requests, a response code that a proxy should return when receiving too many requests
        stream.respond({ ":status": 429 });
        stream.end();
        return;
      }
      if (target == "502.example.com:443") {
        // 502 Bad Gateway, a response code mostly resembling immediate connection error
        stream.respond({ ":status": 502 });
        stream.end();
        return;
      }
      if (target == "504.example.com:443") {
        // 504 Gateway Timeout, did not receive a timely response from an upstream server
        stream.respond({ ":status": 504 });
        stream.end();
        return;
      }
      if (target == "reset.example.com:443") {
        // always reset the stream.
        stream.close(0x0);
        return;
      }

      ++proxy.sessionToOriginServersCount;
      const net = require("net");
      const socket = net.connect(serverPort, "127.0.0.1", () => {
        try {
          stream.respond({ ":status": 200 });
          socket.pipe(stream);
          stream.pipe(socket);
        } catch (exception) {
          console.log(exception);
          stream.close();
        }
      });
      socket.on("error", error => {
        throw new Error(
          `Unexpected error when conneting the HTTP/2 server from the HTTP/2 proxy during CONNECT handling: '${error}'`
        );
      });
    });
  }
}

async function proxy_session_counter() {
  let data = await NodeServer.execute(
    processId,
    `http2ProxyCode.proxySessionCount()`
  );
  return parseInt(data) - initial_session_count;
}
async function proxy_session_to_origin_server_counter() {
  let data = await NodeServer.execute(
    processId,
    `http2ProxyCode.proxySessionToOriginServersCount()`
  );
  return parseInt(data) - initial_session_count;
}
let processId;
let origin;
add_setup(async function setup() {
  // Set to allow the cert presented by our H2 server
  do_get_profile();

  // The moz-http2 cert is for foo.example.com and is signed by http2-ca.pem
  // so add that cert to the trust list as a signing cert.
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");

  origin = new NodeLegacyHTTP2Server();
  await origin.start();
  await origin.setRequestHandler((req, res) => {
    const path = new URL(req.url, "https://localhost").pathname;
    if (path == "/750ms" || path == "/750msNoData") {
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      setTimeout(() => {
        res.writeHead(200);
        if (path == "/750ms") {
          res.end("It's all good 750ms.");
        }
      }, 750);
      return;
    }
    // node-http2 encodes these headers illegally. The soft one fails only the
    // stream, the hard one fails the whole session.
    if (path == "/illegalhpacksoft") {
      res.setHeader("x-softillegalhpack", "true");
    } else if (path == "/illegalhpackhard") {
      res.setHeader("x-hardillegalhpack", "true");
    }
    res.setHeader("Content-Type", "text/html");
    res.writeHead(200);
    res.end(`<body>You Win! (by requesting${path})</body>`);
  });
  let server_port = origin.port();
  processId = await NodeServer.fork();
  await NodeServer.execute(processId, `serverPort = ${server_port}`);
  await NodeServer.execute(processId, ADB);
  await NodeServer.execute(processId, http2ProxyCode);
  let newProxy = await NodeServer.execute(
    processId,
    `http2ProxyCode.startNewProxy()`
  );
  proxy_port = newProxy.port;
  Assert.notEqual(proxy_port, null);

  Services.prefs.setStringPref(
    "services.settings.server",
    `data:,#remote-settings-dummy/v1`
  );

  Services.prefs.setBoolPref("network.http.http2.enabled", true);

  // make all native resolve calls "secretly" resolve localhost instead
  Services.prefs.setBoolPref("network.dns.native-is-localhost", true);

  filter = new ProxyFilter("https", "localhost", proxy_port, 0);
  pps.registerFilter(filter, 10);

  initial_session_count = await proxy_session_counter();
  info(`Initial proxy session count = ${initial_session_count}`);
});

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("services.settings.server");
  Services.prefs.clearUserPref("network.http.http2.enabled");
  Services.prefs.clearUserPref("network.dns.native-is-localhost");

  pps.unregisterFilter(filter);

  await NodeServer.execute(processId, `http2ProxyCode.closeProxy()`);
  await NodeServer.kill(processId);
  // Stopped after the proxy, which throws when its tunnels to the origin get
  // reset, as they do on Windows when the origin's process is killed.
  await origin.stop();
});

/**
 * Test series beginning.
 */

// Check we reach the h2 end server and keep only one session with the proxy for two different origin.
// Here we use the first isolation token.
add_task(async function proxy_success_one_session() {
  proxy_isolation = "TOKEN1";

  const foo = await get_response(
    make_channel(`https://foo.example.com/random-request-1`)
  );
  const alt1 = await get_response(
    make_channel(`https://alt1.example.com/random-request-2`)
  );

  Assert.equal(foo.status, Cr.NS_OK);
  Assert.equal(foo.proxy_connect_response_code, 200);
  Assert.equal(foo.http_code, 200);
  Assert.ok(foo.data.match("random-request-1"));
  Assert.ok(foo.data.match("You Win!"));
  Assert.equal(alt1.status, Cr.NS_OK);
  Assert.equal(alt1.proxy_connect_response_code, 200);
  Assert.equal(alt1.http_code, 200);
  Assert.ok(alt1.data.match("random-request-2"));
  Assert.ok(alt1.data.match("You Win!"));
  Assert.equal(
    await proxy_session_counter(),
    1,
    "Created just one session with the proxy"
  );
});

// The proxy responses with 407 instead of 200 Connected, make sure we get a proper error
// code from the channel and not try to ask for any credentials.
add_task(async function proxy_auth_failure() {
  const chan = make_channel(`https://407.example.com/`);
  const auth_prompt = { triggered: false };
  chan.notificationCallbacks = new AuthRequestor(
    () => new UnxpectedAuthPrompt2(auth_prompt)
  );
  const { status, http_code, proxy_connect_response_code } = await get_response(
    chan,
    CL_EXPECT_FAILURE
  );

  Assert.equal(status, Cr.NS_ERROR_PROXY_AUTHENTICATION_FAILED);
  Assert.equal(proxy_connect_response_code, 407);
  Assert.equal(http_code, undefined);
  Assert.equal(auth_prompt.triggered, false, "Auth prompt didn't trigger");
  Assert.equal(
    await proxy_session_counter(),
    1,
    "No new session created by 407"
  );
});

// The proxy responses with 407 with Proxy-Authenticate header presence. Make
// sure that we prompt the auth prompt to ask for credentials.
add_task(async function proxy_auth_basic() {
  const chan = make_channel(`https://407.basic.example.com/`);
  const auth_prompt = { triggered: false };
  chan.notificationCallbacks = new AuthRequestor(
    () => new SimpleAuthPrompt2(auth_prompt)
  );
  const { status, http_code, proxy_connect_response_code } = await get_response(
    chan,
    CL_EXPECT_FAILURE
  );

  // 418 indicates we pass the basic authentication.
  Assert.equal(status, Cr.NS_ERROR_PROXY_CONNECTION_REFUSED);
  Assert.equal(proxy_connect_response_code, 418);
  Assert.equal(http_code, undefined);
  Assert.equal(auth_prompt.triggered, true, "Auth prompt should trigger");
  Assert.equal(
    await proxy_session_counter(),
    1,
    "No new session created by 407"
  );
});

// 502 Bad gateway code returned by the proxy, still one session only, proper different code
// from the channel.
add_task(async function proxy_bad_gateway_failure() {
  const { status, http_code, proxy_connect_response_code } = await get_response(
    make_channel(`https://502.example.com/`),
    CL_EXPECT_FAILURE
  );

  Assert.equal(status, Cr.NS_ERROR_PROXY_BAD_GATEWAY);
  Assert.equal(proxy_connect_response_code, 502);
  Assert.equal(http_code, undefined);
  Assert.equal(
    await proxy_session_counter(),
    1,
    "No new session created by 502 after 407"
  );
});

// Second 502 Bad gateway code returned by the proxy, still one session only with the proxy.
add_task(async function proxy_bad_gateway_failure_two() {
  const { status, http_code, proxy_connect_response_code } = await get_response(
    make_channel(`https://502.example.com/`),
    CL_EXPECT_FAILURE
  );

  Assert.equal(status, Cr.NS_ERROR_PROXY_BAD_GATEWAY);
  Assert.equal(proxy_connect_response_code, 502);
  Assert.equal(http_code, undefined);
  Assert.equal(
    await proxy_session_counter(),
    1,
    "No new session created by second 502"
  );
});

// 504 Gateway timeout code returned by the proxy, still one session only, proper different code
// from the channel.
add_task(async function proxy_gateway_timeout_failure() {
  const { status, http_code, proxy_connect_response_code } = await get_response(
    make_channel(`https://504.example.com/`),
    CL_EXPECT_FAILURE
  );

  Assert.equal(status, Cr.NS_ERROR_PROXY_GATEWAY_TIMEOUT);
  Assert.equal(proxy_connect_response_code, 504);
  Assert.equal(http_code, undefined);
  Assert.equal(
    await proxy_session_counter(),
    1,
    "No new session created by 504 after 502"
  );
});

// 404 Not Found means the proxy could not resolve the host.  As for other error responses
// we still expect this not to close the existing session.
add_task(async function proxy_host_not_found_failure() {
  const { status, http_code, proxy_connect_response_code } = await get_response(
    make_channel(`https://404.example.com/`),
    CL_EXPECT_FAILURE
  );

  Assert.equal(status, Cr.NS_ERROR_UNKNOWN_HOST);
  Assert.equal(proxy_connect_response_code, 404);
  Assert.equal(http_code, undefined);
  Assert.equal(
    await proxy_session_counter(),
    1,
    "No new session created by 404 after 504"
  );
});

add_task(async function proxy_too_many_requests_failure() {
  const { status, http_code, proxy_connect_response_code } = await get_response(
    make_channel(`https://429.example.com/`),
    CL_EXPECT_FAILURE
  );

  Assert.equal(status, Cr.NS_ERROR_PROXY_TOO_MANY_REQUESTS);
  Assert.equal(proxy_connect_response_code, 429);
  Assert.equal(http_code, undefined);
  Assert.equal(
    await proxy_session_counter(),
    1,
    "No new session created by 429 after 504"
  );
});

add_task(async function proxy_stream_reset_failure() {
  const { status, http_code, proxy_connect_response_code } = await get_response(
    make_channel(`https://reset.example.com/`),
    CL_EXPECT_FAILURE
  );

  Assert.equal(status, Cr.NS_ERROR_NET_INTERRUPT);
  Assert.equal(proxy_connect_response_code, 0);
  Assert.equal(http_code, undefined);
  Assert.equal(
    await proxy_session_counter(),
    1,
    "No new session created by 429 after 504"
  );
});

// The soft errors are not closing the session.
add_task(async function origin_server_stream_soft_failure() {
  var current_num_sessions_to_origin_server =
    await proxy_session_to_origin_server_counter();

  const { status, http_code, proxy_connect_response_code } = await get_response(
    make_channel(`https://foo.example.com/illegalhpacksoft`),
    CL_EXPECT_FAILURE
  );

  Assert.equal(status, Cr.NS_ERROR_ILLEGAL_VALUE);
  Assert.equal(proxy_connect_response_code, 200);
  Assert.equal(http_code, undefined);
  Assert.equal(
    await proxy_session_counter(),
    1,
    "No session to the proxy closed by soft stream errors"
  );
  Assert.equal(
    await proxy_session_to_origin_server_counter(),
    current_num_sessions_to_origin_server,
    "No session to the origin server closed by soft stream errors"
  );
});

// The soft errors are not closing the session.
add_task(
  async function origin_server_stream_soft_failure_multiple_streams_not_affected() {
    var current_num_sessions_to_origin_server =
      await proxy_session_to_origin_server_counter();

    let should_succeed = get_response(
      make_channel(`https://foo.example.com/750ms`)
    );

    const failed = await get_response(
      make_channel(`https://foo.example.com/illegalhpacksoft`),
      CL_EXPECT_FAILURE,
      20
    );

    const succeeded = await should_succeed;

    Assert.equal(failed.status, Cr.NS_ERROR_ILLEGAL_VALUE);
    Assert.equal(failed.proxy_connect_response_code, 200);
    Assert.equal(failed.http_code, undefined);
    Assert.equal(succeeded.status, Cr.NS_OK);
    Assert.equal(succeeded.proxy_connect_response_code, 200);
    Assert.equal(succeeded.http_code, 200);
    Assert.equal(
      await proxy_session_counter(),
      1,
      "No session to the proxy closed by soft stream errors"
    );
    Assert.equal(
      await proxy_session_to_origin_server_counter(),
      current_num_sessions_to_origin_server,
      "No session to the origin server closed by soft stream errors"
    );
  }
);

// Make sure that the above error codes don't kill the session to the proxy.
add_task(async function proxy_success_still_one_session() {
  const foo = await get_response(
    make_channel(`https://foo.example.com/random-request-1`)
  );
  const alt1 = await get_response(
    make_channel(`https://alt1.example.com/random-request-2`)
  );

  Assert.equal(foo.status, Cr.NS_OK);
  Assert.equal(foo.http_code, 200);
  Assert.equal(foo.proxy_connect_response_code, 200);
  Assert.ok(foo.data.match("random-request-1"));
  Assert.equal(alt1.status, Cr.NS_OK);
  Assert.equal(alt1.proxy_connect_response_code, 200);
  Assert.equal(alt1.http_code, 200);
  Assert.ok(alt1.data.match("random-request-2"));
  Assert.equal(
    await proxy_session_counter(),
    1,
    "No new session to the proxy created after stream error codes"
  );
});

// Have a new isolation key, this means we are expected to create a new, and again one only,
// session with the proxy to reach the end server.
add_task(async function proxy_success_isolated_session() {
  Assert.notEqual(proxy_isolation, "TOKEN2");
  proxy_isolation = "TOKEN2";

  const foo = await get_response(
    make_channel(`https://foo.example.com/random-request-1`)
  );
  const alt1 = await get_response(
    make_channel(`https://alt1.example.com/random-request-2`)
  );
  const lh = await get_response(
    make_channel(`https://localhost/random-request-3`)
  );

  Assert.equal(foo.status, Cr.NS_OK);
  Assert.equal(foo.proxy_connect_response_code, 200);
  Assert.equal(foo.http_code, 200);
  Assert.ok(foo.data.match("random-request-1"));
  Assert.ok(foo.data.match("You Win!"));
  Assert.equal(alt1.status, Cr.NS_OK);
  Assert.equal(alt1.proxy_connect_response_code, 200);
  Assert.equal(alt1.http_code, 200);
  Assert.ok(alt1.data.match("random-request-2"));
  Assert.ok(alt1.data.match("You Win!"));
  Assert.equal(lh.status, Cr.NS_OK);
  Assert.equal(lh.proxy_connect_response_code, 200);
  Assert.equal(lh.http_code, 200);
  Assert.ok(lh.data.match("random-request-3"));
  Assert.ok(lh.data.match("You Win!"));
  Assert.equal(
    await proxy_session_counter(),
    2,
    "Just one new session seen after changing the isolation key"
  );
});

// Check that error codes are still handled the same way with new isolation, just in case.
add_task(async function proxy_bad_gateway_failure_isolated() {
  const failure1 = await get_response(
    make_channel(`https://502.example.com/`),
    CL_EXPECT_FAILURE
  );
  const failure2 = await get_response(
    make_channel(`https://502.example.com/`),
    CL_EXPECT_FAILURE
  );

  Assert.equal(failure1.status, Cr.NS_ERROR_PROXY_BAD_GATEWAY);
  Assert.equal(failure1.proxy_connect_response_code, 502);
  Assert.equal(failure1.http_code, undefined);
  Assert.equal(failure2.status, Cr.NS_ERROR_PROXY_BAD_GATEWAY);
  Assert.equal(failure2.proxy_connect_response_code, 502);
  Assert.equal(failure2.http_code, undefined);
  Assert.equal(
    await proxy_session_counter(),
    2,
    "No new session created by 502"
  );
});

add_task(async function proxy_success_check_number_of_session() {
  const foo = await get_response(
    make_channel(`https://foo.example.com/random-request-1`)
  );
  const alt1 = await get_response(
    make_channel(`https://alt1.example.com/random-request-2`)
  );
  const lh = await get_response(
    make_channel(`https://localhost/random-request-3`)
  );

  Assert.equal(foo.status, Cr.NS_OK);
  Assert.equal(foo.proxy_connect_response_code, 200);
  Assert.equal(foo.http_code, 200);
  Assert.ok(foo.data.match("random-request-1"));
  Assert.ok(foo.data.match("You Win!"));
  Assert.equal(alt1.status, Cr.NS_OK);
  Assert.equal(alt1.proxy_connect_response_code, 200);
  Assert.equal(alt1.http_code, 200);
  Assert.ok(alt1.data.match("random-request-2"));
  Assert.ok(alt1.data.match("You Win!"));
  Assert.equal(lh.status, Cr.NS_OK);
  Assert.equal(lh.proxy_connect_response_code, 200);
  Assert.equal(lh.http_code, 200);
  Assert.ok(lh.data.match("random-request-3"));
  Assert.ok(lh.data.match("You Win!"));
  Assert.equal(
    await proxy_session_counter(),
    2,
    "The number of sessions has not changed"
  );
});

// The hard errors are closing the session.
add_task(async function origin_server_stream_hard_failure() {
  var current_num_sessions_to_origin_server =
    await proxy_session_to_origin_server_counter();
  const { status, http_code, proxy_connect_response_code } = await get_response(
    make_channel(`https://foo.example.com/illegalhpackhard`),
    CL_EXPECT_FAILURE
  );

  Assert.equal(status, 0x804b0053);
  Assert.equal(proxy_connect_response_code, 200);
  Assert.equal(http_code, undefined);
  Assert.equal(
    await proxy_session_counter(),
    2,
    "No new session to the proxy."
  );
  Assert.equal(
    await proxy_session_to_origin_server_counter(),
    current_num_sessions_to_origin_server,
    "No new session to the origin server yet."
  );

  // Check the a new session ill be opened.
  const foo = await get_response(
    make_channel(`https://foo.example.com/random-request-1`)
  );

  Assert.equal(foo.status, Cr.NS_OK);
  Assert.equal(foo.proxy_connect_response_code, 200);
  Assert.equal(foo.http_code, 200);
  Assert.ok(foo.data.match("random-request-1"));
  Assert.ok(foo.data.match("You Win!"));

  Assert.equal(
    await proxy_session_counter(),
    2,
    "No new session to the proxy is created after a hard stream failure on the session to the origin server."
  );
  Assert.equal(
    await proxy_session_to_origin_server_counter(),
    current_num_sessions_to_origin_server + 1,
    "A new session to the origin server after a hard stream error"
  );
});

// The hard errors are closing the session.
add_task(
  async function origin_server_stream_hard_failure_multiple_streams_affected() {
    var current_num_sessions_to_origin_server =
      await proxy_session_to_origin_server_counter();
    let should_fail = get_response(
      make_channel(`https://foo.example.com/750msNoData`),
      CL_EXPECT_FAILURE
    );
    const failed1 = await get_response(
      make_channel(`https://foo.example.com/illegalhpackhard`),
      CL_EXPECT_FAILURE,
      10
    );

    const failed2 = await should_fail;

    Assert.equal(failed1.status, 0x804b0053);
    Assert.equal(failed1.proxy_connect_response_code, 200);
    Assert.equal(failed1.http_code, undefined);
    Assert.equal(failed2.status, 0x804b0053);
    Assert.equal(failed2.proxy_connect_response_code, 200);
    Assert.equal(failed2.http_code, undefined);
    Assert.equal(
      await proxy_session_counter(),
      2,
      "No new session to the proxy"
    );
    Assert.equal(
      await proxy_session_to_origin_server_counter(),
      current_num_sessions_to_origin_server,
      "No session to the origin server yet."
    );
    // Check the a new session ill be opened.
    const foo = await get_response(
      make_channel(`https://foo.example.com/random-request-1`)
    );

    Assert.equal(foo.status, Cr.NS_OK);
    Assert.equal(foo.proxy_connect_response_code, 200);
    Assert.equal(foo.http_code, 200);
    Assert.ok(foo.data.match("random-request-1"));
    Assert.ok(foo.data.match("You Win!"));

    Assert.equal(
      await proxy_session_counter(),
      2,
      "No new session to the proxy is created after a hard stream failure on the session to the origin server."
    );

    Assert.equal(
      await proxy_session_to_origin_server_counter(),
      current_num_sessions_to_origin_server + 1,
      "A new session to the origin server after a hard stream error"
    );
  }
);
