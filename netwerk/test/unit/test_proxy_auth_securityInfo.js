/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Test that making an HTTPS request through an HTTP proxy that answers CONNECT
// with a 407 and a connection-based (sticky) auth scheme still ends up with a
// securityInfo corresponding to the original HTTPS request.

"use strict";

const { NodeHTTPProxyServer, NodeHTTPSServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

/* import-globals-from head_channels.js */

// Answers the first CONNECT with a 407 offering mock_auth, then tunnels to the
// destination server once credentials arrive on the same connection.
function connectHandler(req, clientSocket) {
  const net = require("net");
  const url = require("url");

  let buffered = Buffer.alloc(0);

  const { port } = url.parse(`//${req.url}`, false, true);

  function handleConnect(credentials, pending) {
    if (credentials != "moz_test_credentials") {
      clientSocket.write(
        "HTTP/1.1 407 Unauthorized\r\n" +
          "Proxy-agent: Node.js-Proxy\r\n" +
          "Connection: keep-alive\r\n" +
          "Proxy-Authenticate: mock_auth\r\n" +
          "Content-Length: 0\r\n" +
          "\r\n"
      );
    } else {
      clientSocket.removeListener("data", onData);
      let serverSocket = net.connect(
        { port, host: "127.0.0.1", family: 4 },
        () => {
          clientSocket.write(
            "HTTP/1.1 200 Connection Established\r\n" +
              "Proxy-agent: Node.js-Proxy\r\n" +
              "\r\n"
          );
          if (pending && pending.length) {
            serverSocket.write(pending);
          }
          serverSocket.pipe(clientSocket);
          clientSocket.pipe(serverSocket);
        }
      );
    }
  }

  function onData(data) {
    buffered = Buffer.concat([buffered, data]);
    let headersEnd = buffered.indexOf("\r\n\r\n");
    if (headersEnd == -1) {
      return;
    }
    let headers = buffered.subarray(0, headersEnd).toString("latin1");
    let rest = buffered.subarray(headersEnd + 4);
    buffered = Buffer.alloc(0);

    let credentials = "";
    for (let line of headers.split("\r\n")) {
      let pair = line.split(":").map(element => element.trim());
      if (pair[0] == "Proxy-Authorization") {
        credentials = pair[1];
      }
    }
    handleConnect(credentials, rest);
  }

  clientSocket.on("data", onData);
  handleConnect(req.headers["proxy-authorization"] || "", null);
}

async function makeProxiedConnectionAndCheckSecurityInfo() {
  Cc["@mozilla.org/network/http-auth-manager;1"]
    .getService(Ci.nsIHttpAuthManager)
    .clearAll();

  let server = new NodeHTTPSServer();
  await server.start();
  await server.registerPathHandler("/test", (req, resp) => {
    let body = "hello from the server";
    resp.setHeader("Content-Type", "text/plain");
    resp.setHeader("Content-Length", body.length);
    resp.writeHead(200);
    resp.end(body);
  });

  let proxy = new NodeHTTPProxyServer();
  await proxy.start();
  await proxy.registerConnectHandler(connectHandler);

  try {
    // The NodeHTTPSServer uses a certificate valid for `alt1.example.com`.
    let chan = makeChan(`https://alt1.example.com:${server.port()}/test`);
    let [req, buffer] = await channelOpenPromise(chan);

    Assert.equal(req.status, Cr.NS_OK, "channel succeeded");
    Assert.equal(req.QueryInterface(Ci.nsIHttpChannel).responseStatus, 200);
    Assert.equal(buffer, "hello from the server", "got the server's response");
    Assert.equal(
      req.QueryInterface(Ci.nsIProxiedChannel).httpProxyConnectResponseCode,
      200,
      "the tunnel was established"
    );

    let secInfo = req.QueryInterface(Ci.nsIChannel).securityInfo;
    Assert.notEqual(secInfo, null, "channel should have securityInfo");
    secInfo.QueryInterface(Ci.nsITransportSecurityInfo);
    Assert.notEqual(
      secInfo.serverCert,
      null,
      "securityInfo should have a serverCert"
    );
  } finally {
    await proxy.stop();
    await server.stop();
  }
}

// Test with auth retries configured to use a new channel.
add_task(
  { pref_set: [["network.auth.use_redirect_for_retries", true]] },
  async function retry_via_redirect() {
    await makeProxiedConnectionAndCheckSecurityInfo();
  }
);

// Test with auth retries configured to re-use the original channel.
add_task(
  { pref_set: [["network.auth.use_redirect_for_retries", false]] },
  async function retry_via_restart() {
    await makeProxiedConnectionAndCheckSecurityInfo();
  }
);
