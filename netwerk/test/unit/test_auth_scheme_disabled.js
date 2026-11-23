/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Regression test for bug 2019322: network.http.auth.disabled-schemes lets
// an admin remove specific auth schemes (parity with Chrome/Edge's
// AuthSchemes enterprise policy). A challenge for a disabled scheme must be
// ignored entirely -- no auth prompt is invoked for it -- and the channel
// must fail with NS_ERROR_HTTP_AUTH_SCHEME_DISABLED while still delivering
// the server's original (e.g. 401) response.

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
const { NodeHTTPProxyServer, NodeHTTPSServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

/* import-globals-from head_channels.js */

let httpserver = null;

function basicAuthHandler(metadata, response) {
  response.setStatusLine(metadata.httpVersion, 401, "Unauthorized");
  response.setHeader("WWW-Authenticate", 'Basic realm="secret"', false);
  const body = "auth required";
  response.bodyOutputStream.write(body, body.length);
}

function makeChan(url) {
  return NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
}

add_task(async function test_disabled_scheme_is_ignored() {
  Services.prefs.setCharPref("network.http.auth.disabled-schemes", "basic");
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.http.auth.disabled-schemes");
  });

  let chan = makeChan(
    `http://localhost:${httpserver.identity.primaryPort}/auth`
  );
  // No nsIAuthPrompt2 is registered on this channel's notification
  // callbacks. If the disabled Basic challenge were not filtered out,
  // Firefox would try to invoke a prompt to get credentials for it and
  // that QueryInterface would throw, failing the test loudly instead of
  // silently passing.
  let [req] = await channelOpenPromise(chan, CL_EXPECT_LATE_FAILURE);

  equal(
    req.status,
    Cr.NS_ERROR_HTTP_AUTH_SCHEME_DISABLED,
    "channel fails with the auth-scheme-disabled status"
  );
  equal(
    chan.responseStatus,
    401,
    "the server's original 401 response is still delivered"
  );
});

add_task(async function test_scheme_list_is_case_insensitive() {
  Services.prefs.setCharPref("network.http.auth.disabled-schemes", "BASIC");
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.http.auth.disabled-schemes");
  });

  let chan = makeChan(
    `http://localhost:${httpserver.identity.primaryPort}/auth`
  );
  let [req] = await channelOpenPromise(chan, CL_EXPECT_LATE_FAILURE);

  equal(req.status, Cr.NS_ERROR_HTTP_AUTH_SCHEME_DISABLED);
});

// A failed CONNECT whose only challenge is disabled must still go through
// ProcessFailedProxyConnect(), but report NS_ERROR_HTTP_AUTH_SCHEME_DISABLED
// rather than the generic proxy-connect error.
add_task(async function test_disabled_scheme_ignored_on_proxy_connect() {
  Cc["@mozilla.org/network/http-auth-manager;1"]
    .getService(Ci.nsIHttpAuthManager)
    .clearAll();

  Services.prefs.setCharPref("network.http.auth.disabled-schemes", "basic");
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.http.auth.disabled-schemes");
  });

  function connectHandler(req, clientSocket) {
    clientSocket.write(
      "HTTP/1.1 407 Unauthorized\r\n" +
        "Proxy-agent: Node.js-Proxy\r\n" +
        'Proxy-Authenticate: Basic realm="proxy"\r\n' +
        "Content-Length: 0\r\n" +
        "\r\n"
    );
  }

  let server = new NodeHTTPSServer();
  await server.start();

  let proxy = new NodeHTTPProxyServer();
  await proxy.start();
  await proxy.registerConnectHandler(connectHandler);

  try {
    // The NodeHTTPSServer uses a certificate valid for `alt1.example.com`.
    let chan = makeChan(`https://alt1.example.com:${server.port()}/test`);
    // No nsIAuthPrompt2 is registered on this channel's notification
    // callbacks, so if the disabled Basic proxy challenge were not
    // filtered out this would try to invoke a prompt.
    let [req] = await channelOpenPromise(chan, CL_EXPECT_LATE_FAILURE);

    equal(
      req.status,
      Cr.NS_ERROR_HTTP_AUTH_SCHEME_DISABLED,
      "CONNECT failure surfaces the auth-scheme-disabled status"
    );
  } finally {
    await proxy.stop();
    await server.stop();
  }
});

function run_test() {
  httpserver = new HttpServer();
  httpserver.registerPathHandler("/auth", basicAuthHandler);
  httpserver.start(-1);

  registerCleanupFunction(() => {
    return new Promise(resolve => httpserver.stop(resolve));
  });

  run_next_test();
}
