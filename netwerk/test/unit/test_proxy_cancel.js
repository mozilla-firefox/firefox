/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* globals setTimeout */

// We don't normally allow localhost channels to be proxied, but this
// is easier than updating all the certs and/or domains.
Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
registerCleanupFunction(() => {
  Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
});

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const {
  NodeHTTPServer,
  NodeHTTPSServer,
  NodeHTTP2Server,
  NodeHTTPProxyServer,
  NodeHTTPSProxyServer,
  NodeHTTP2ProxyServer,
  with_node_servers,
} = ChromeUtils.importESModule("resource://testing-common/NodeServer.sys.mjs");

function makeChan(uri) {
  let chan = NetUtil.newChannel({
    uri,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return chan;
}

add_setup(async function setup() {
  // See Bug 1878505
  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);
  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
  });
});

add_task(async function test_cancel_after_asyncOpen() {
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");
  addCertFromFile(certdb, "proxy-ca.pem", "CTu,u,u");

  let proxies = [
    NodeHTTPProxyServer,
    NodeHTTPSProxyServer,
    NodeHTTP2ProxyServer,
  ];
  for (let p of proxies) {
    let proxy = new p();
    await proxy.start();
    registerCleanupFunction(async () => {
      await proxy.stop();
    });
    await with_node_servers(
      [NodeHTTPServer, NodeHTTPSServer, NodeHTTP2Server],
      async server => {
        info(`Testing ${p.name} with ${server.constructor.name}`);
        await server.execute(
          `global.server_name = "${server.constructor.name}";`
        );

        await server.registerPathHandler("/test", (req, resp) => {
          resp.writeHead(200);
          resp.end(global.server_name);
        });

        let chan = makeChan(`${server.origin()}/test`);
        let openPromise = new Promise(resolve => {
          chan.asyncOpen(
            new ChannelListener(
              (req, buff) => resolve({ req, buff }),
              null,
              CL_EXPECT_FAILURE
            )
          );
        });
        chan.cancel(Cr.NS_ERROR_ABORT);
        let { req } = await openPromise;
        Assert.equal(req.status, Cr.NS_ERROR_ABORT);
      }
    );
    await proxy.stop();
  }
});

// const NS_NET_STATUS_CONNECTING_TO = 0x4b0007;
// const NS_NET_STATUS_CONNECTED_TO = 0x4b0004;
// const NS_NET_STATUS_SENDING_TO = 0x4b0005;
const NS_NET_STATUS_WAITING_FOR = 0x4b000a; // 2152398858
const NS_NET_STATUS_RECEIVING_FROM = 0x4b0006;
// const NS_NET_STATUS_TLS_HANDSHAKE_STARTING = 0x4b000c; // 2152398860
// const NS_NET_STATUS_TLS_HANDSHAKE_ENDED = 0x4b000d; // 2152398861

add_task(async function test_cancel_after_connect_http2proxy() {
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");
  addCertFromFile(certdb, "proxy-ca.pem", "CTu,u,u");

  await with_node_servers(
    [NodeHTTPServer, NodeHTTPSServer, NodeHTTP2Server],
    async server => {
      // Set up a proxy for each server to make sure proxy state is clean
      // for each test.
      let proxy = new NodeHTTP2ProxyServer();
      await proxy.start();
      registerCleanupFunction(async () => {
        await proxy.stop();
      });
      await proxy.execute(`
        global.session_counter = 0;
        global.proxy.on("session", () => {
          global.session_counter++;
        });
      `);

      info(`Testing ${proxy.constructor.name} with ${server.constructor.name}`);
      await server.execute(
        `global.server_name = "${server.constructor.name}";`
      );

      await server.registerPathHandler("/test", (req, resp) => {
        global.reqCount = (global.reqCount || 0) + 1;
        resp.writeHead(200);
        resp.end(global.server_name);
      });

      let chan = makeChan(`${server.origin()}/test`);
      chan.notificationCallbacks = {
        QueryInterface: ChromeUtils.generateQI([
          "nsIInterfaceRequestor",
          "nsIProgressEventSink",
        ]),

        getInterface(iid) {
          return this.QueryInterface(iid);
        },

        onProgress() {},
        onStatus(request, status) {
          info(`status = ${status}`);
          // XXX(valentin): Is this the best status to be cancelling?
          if (status == NS_NET_STATUS_WAITING_FOR) {
            info("cancelling connected channel");
            chan.cancel(Cr.NS_ERROR_ABORT);
          }
        },
      };
      let openPromise = new Promise(resolve => {
        chan.asyncOpen(
          new ChannelListener(
            (req, buff) => resolve({ req, buff }),
            null,
            CL_EXPECT_FAILURE
          )
        );
      });
      let { req } = await openPromise;
      Assert.equal(req.status, Cr.NS_ERROR_ABORT);

      // Since we're cancelling just after connect, we'd expect that no
      // requests are actually registered. But because we're cancelling on the
      // main thread, and the request is being performed on the socket thread,
      // it might actually reach the server, especially in chaos test mode.
      // Assert.equal(
      //   await server.execute(`global.reqCount || 0`),
      //   0,
      //   `No requests should have been made at this point`
      // );
      // For the same reason, the proxy might not have set up the HTTP/2
      // session yet when the channel gets cancelled.
      Assert.lessOrEqual(await proxy.execute(`global.session_counter`), 1);

      chan = makeChan(`${server.origin()}/test`);
      await new Promise(resolve => {
        chan.asyncOpen(
          new ChannelListener(
            // eslint-disable-next-line no-shadow
            (req, buff) => resolve({ req, buff }),
            null,
            CL_ALLOW_UNKNOWN_CL
          )
        );
      });

      // Check that there's still only one session.
      Assert.equal(await proxy.execute(`global.session_counter`), 1);
      await proxy.stop();
    }
  );
});

add_task(async function test_cancel_after_sending_request() {
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");
  addCertFromFile(certdb, "proxy-ca.pem", "CTu,u,u");

  await with_node_servers(
    [NodeHTTPServer, NodeHTTPSServer, NodeHTTP2Server],
    async server => {
      let proxies = [
        NodeHTTPProxyServer,
        NodeHTTPSProxyServer,
        NodeHTTP2ProxyServer,
      ];
      for (let p of proxies) {
        let proxy = new p();
        await proxy.start();
        registerCleanupFunction(async () => {
          await proxy.stop();
        });

        await proxy.execute(`
        global.session_counter = 0;
        global.proxy.on("session", () => {
          global.session_counter++;
        });
      `);
        info(`Testing ${p.name} with ${server.constructor.name}`);
        await server.execute(
          `global.server_name = "${server.constructor.name}";`
        );

        await server.registerPathHandler("/test", (req, resp) => {
          // Here we simmulate a slow response to give the test time to
          // cancel the channel before receiving the response.
          global.request_count = (global.request_count || 0) + 1;
          // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
          setTimeout(() => {
            resp.writeHead(200);
            resp.end(global.server_name);
          }, 2000);
        });
        await server.registerPathHandler("/instant", (req, resp) => {
          resp.writeHead(200);
          resp.end(global.server_name);
        });

        // It seems proxy.on("session") only gets emitted after a full request.
        // So we first load a simple request, then the long lasting request
        // that we then cancel before it has the chance to complete.
        let chan = makeChan(`${server.origin()}/instant`);
        await new Promise(resolve => {
          chan.asyncOpen(
            new ChannelListener(resolve, null, CL_ALLOW_UNKNOWN_CL)
          );
        });

        chan = makeChan(`${server.origin()}/test`);
        let openPromise = new Promise(resolve => {
          chan.asyncOpen(
            new ChannelListener(
              (req, buff) => resolve({ req, buff }),
              null,
              CL_EXPECT_FAILURE
            )
          );
        });
        // XXX(valentin) This might be a little racy
        await TestUtils.waitForCondition(async () => {
          return (await server.execute("global.request_count")) > 0;
        });

        chan.cancel(Cr.NS_ERROR_ABORT);

        let { req } = await openPromise;
        Assert.equal(req.status, Cr.NS_ERROR_ABORT);

        async function checkSessionCounter() {
          if (p.name == "NodeHTTP2ProxyServer") {
            Assert.equal(await proxy.execute(`global.session_counter`), 1);
          }
        }

        await checkSessionCounter();

        chan = makeChan(`${server.origin()}/instant`);
        await new Promise(resolve => {
          chan.asyncOpen(
            new ChannelListener(
              // eslint-disable-next-line no-shadow
              (req, buff) => resolve({ req, buff }),
              null,
              CL_ALLOW_UNKNOWN_CL
            )
          );
        });
        await checkSessionCounter();

        await proxy.stop();
      }
    }
  );
});

add_task(async function test_cancel_during_response() {
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");
  addCertFromFile(certdb, "proxy-ca.pem", "CTu,u,u");

  await with_node_servers(
    [NodeHTTPServer, NodeHTTPSServer, NodeHTTP2Server],
    async server => {
      let proxies = [
        NodeHTTPProxyServer,
        NodeHTTPSProxyServer,
        NodeHTTP2ProxyServer,
      ];
      for (let p of proxies) {
        let proxy = new p();
        await proxy.start();
        registerCleanupFunction(async () => {
          await proxy.stop();
        });

        await proxy.execute(`
        global.session_counter = 0;
        global.proxy.on("session", () => {
          global.session_counter++;
        });
      `);

        info(`Testing ${p.name} with ${server.constructor.name}`);
        await server.execute(
          `global.server_name = "${server.constructor.name}";`
        );

        await server.registerPathHandler("/test", (req, resp) => {
          resp.writeHead(200);
          resp.write("a".repeat(1000));
          // Here we send the response back in two chunks.
          // The channel should be cancelled after the first one.
          // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
          setTimeout(() => {
            resp.write("a".repeat(1000));
            resp.end(global.server_name);
          }, 2000);
        });
        await server.registerPathHandler("/instant", (req, resp) => {
          resp.writeHead(200);
          resp.end(global.server_name);
        });

        let chan = makeChan(`${server.origin()}/test`);

        chan.notificationCallbacks = {
          QueryInterface: ChromeUtils.generateQI([
            "nsIInterfaceRequestor",
            "nsIProgressEventSink",
          ]),

          getInterface(iid) {
            return this.QueryInterface(iid);
          },

          onProgress(request, progress, progressMax) {
            info(`progress: ${progress}/${progressMax}`);
            // Check that we never get more than 1000 bytes.
            Assert.equal(progress, 1000);
          },
          onStatus(request, status) {
            if (status == NS_NET_STATUS_RECEIVING_FROM) {
              info("cancelling when receiving request");
              chan.cancel(Cr.NS_ERROR_ABORT);
            }
          },
        };

        let openPromise = new Promise(resolve => {
          chan.asyncOpen(
            new ChannelListener(
              (req, buff) => resolve({ req, buff }),
              null,
              CL_EXPECT_FAILURE
            )
          );
        });

        let { req } = await openPromise;
        Assert.equal(req.status, Cr.NS_ERROR_ABORT);

        async function checkSessionCounter() {
          if (p.name == "NodeHTTP2ProxyServer") {
            Assert.equal(await proxy.execute(`global.session_counter`), 1);
          }
        }

        await checkSessionCounter();

        chan = makeChan(`${server.origin()}/instant`);
        await new Promise(resolve => {
          chan.asyncOpen(
            new ChannelListener(
              // eslint-disable-next-line no-shadow
              (req, buff) => resolve({ req, buff }),
              null,
              CL_ALLOW_UNKNOWN_CL
            )
          );
        });

        await checkSessionCounter();

        await proxy.stop();
      }
    }
  );
});
