"use strict";

const { setEnterpriseGuards } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionPermissions.sys.mjs"
);

XPCOMUtils.defineLazyServiceGetter(
  this,
  "gProxyService",
  "@mozilla.org/network/protocol-proxy-service;1",
  Ci.nsIProtocolProxyService
);

const TRANSPARENT_PROXY_RESOLVES_HOST =
  Ci.nsIProxyInfo.TRANSPARENT_PROXY_RESOLVES_HOST;

function getProxyInfo(url = "http://www.mozilla.org/") {
  return new Promise(resolve => {
    let channel = NetUtil.newChannel({
      uri: url,
      loadUsingSystemPrincipal: true,
    });

    gProxyService.asyncResolve(channel, 0, {
      onProxyAvailable(req, uri, pi) {
        resolve(pi);
      },
    });
  });
}

const testData = [
  {
    // An ExtensionError is thrown for this, but we are unable to catch it as we
    // do with the PAC script api.  In this case, we expect null for proxyInfo.
    proxyInfo: "not_defined",
    expected: {
      proxyInfo: null,
    },
  },
  {
    proxyInfo: 1,
    expected: {
      error: {
        message:
          "ProxyInfoData: proxyData must be an object or array of objects",
      },
    },
  },
  {
    proxyInfo: [
      {
        type: "socks",
        host: "foo.bar",
        port: 1080,
        username: "johnsmith",
        password: "pass123",
        proxyDNS: true,
        failoverTimeout: 3,
      },
      { type: "http", host: "192.168.1.1", port: 3128 },
      { type: "https", host: "192.168.1.2", port: 1121, failoverTimeout: 1 },
      {
        type: "socks",
        host: "192.168.1.3",
        port: 1999,
        proxyDNS: true,
        username: "mungosantamaria",
        password: "foobar",
      },
      { type: "direct" },
    ],
    expected: {
      proxyInfo: {
        type: "socks",
        host: "foo.bar",
        port: 1080,
        proxyDNS: true,
        username: "johnsmith",
        password: "pass123",
        failoverTimeout: 3,
        failoverProxy: {
          host: "192.168.1.1",
          port: 3128,
          type: "http",
          failoverProxy: {
            host: "192.168.1.2",
            port: 1121,
            type: "https",
            failoverTimeout: 1,
            failoverProxy: {
              host: "192.168.1.3",
              port: 1999,
              type: "socks",
              proxyDNS: TRANSPARENT_PROXY_RESOLVES_HOST,
              username: "mungosantamaria",
              password: "foobar",
              failoverProxy: {
                type: "direct",
              },
            },
          },
        },
      },
    },
  },
];

add_task(async function test_proxy_listener() {
  let extensionData = {
    manifest: {
      permissions: ["proxy", "<all_urls>"],
    },
    background() {
      // Some tests generate multiple errors, we'll just rely on the first.
      let seenError = false;
      let proxyInfo;
      browser.proxy.onError.addListener(error => {
        if (!seenError) {
          browser.test.sendMessage("proxy-error-received", error);
          seenError = true;
        }
      });

      browser.proxy.onRequest.addListener(
        details => {
          browser.test.log(`onRequest ${JSON.stringify(details)}`);
          if (proxyInfo == "not_defined") {
            return not_defined; // eslint-disable-line no-undef
          }
          return proxyInfo;
        },
        { urls: ["<all_urls>"] }
      );

      browser.test.onMessage.addListener((message, data) => {
        if (message === "set-proxy") {
          seenError = false;
          proxyInfo = data.proxyInfo;
        }
      });

      browser.test.sendMessage("ready");
    },
  };

  let extension = ExtensionTestUtils.loadExtension(extensionData);
  await extension.startup();
  await extension.awaitMessage("ready");

  for (let test of testData) {
    extension.sendMessage("set-proxy", test);
    let testError = test.expected.error;
    let errorWait = testError && extension.awaitMessage("proxy-error-received");

    let proxyInfo = await getProxyInfo();
    let expectedProxyInfo = test.expected.proxyInfo;

    if (testError) {
      info("waiting for error data");
      let error = await errorWait;
      equal(error.message, testError.message, "Correct error message received");
      equal(proxyInfo, null, "no proxyInfo received");
    } else if (expectedProxyInfo === null) {
      equal(proxyInfo, null, "no proxyInfo received");
    } else {
      for (
        let proxyUsed = proxyInfo;
        proxyUsed;
        proxyUsed = proxyUsed.failoverProxy
      ) {
        let {
          type,
          host,
          port,
          username,
          password,
          proxyDNS,
          failoverTimeout,
        } = expectedProxyInfo;
        equal(proxyUsed.host, host, `Expected proxy host to be ${host}`);
        equal(proxyUsed.port, port || -1, `Expected proxy port to be ${port}`);
        equal(proxyUsed.type, type, `Expected proxy type to be ${type}`);
        // May be null or undefined depending on use of newProxyInfoWithAuth or newProxyInfo
        equal(
          proxyUsed.username || "",
          username || "",
          `Expected proxy username to be ${username}`
        );
        equal(
          proxyUsed.password || "",
          password || "",
          `Expected proxy password to be ${password}`
        );
        equal(
          proxyUsed.flags,
          proxyDNS == undefined ? 0 : proxyDNS,
          `Expected proxyDNS to be ${proxyDNS}`
        );
        // Default timeout is 10
        equal(
          proxyUsed.failoverTimeout,
          failoverTimeout || 10,
          `Expected failoverTimeout to be ${failoverTimeout}`
        );
        expectedProxyInfo = expectedProxyInfo.failoverProxy;
      }
      ok(!expectedProxyInfo, "no left over failoverProxy");
    }
  }

  await extension.unload();
});

async function getExtension(expectedProxyInfo) {
  function background(proxyInfo) {
    browser.test.log(
      `testing proxy.onRequest with proxyInfo = ${JSON.stringify(proxyInfo)}`
    );
    browser.proxy.onRequest.addListener(
      () => {
        return proxyInfo;
      },
      { urls: ["<all_urls>"] }
    );
  }
  let extensionData = {
    manifest: {
      permissions: ["proxy", "<all_urls>"],
    },
    background: `(${background})(${JSON.stringify(expectedProxyInfo)})`,
  };
  let extension = ExtensionTestUtils.loadExtension(extensionData);
  await extension.startup();
  return extension;
}

add_task(async function test_passthrough() {
  let ext1 = await getExtension(null);
  let ext2 = await getExtension({ host: "1.2.3.4", port: 8888, type: "https" });

  // Also use a restricted url to test the ability to proxy those.
  let proxyInfo = await getProxyInfo("https://addons.mozilla.org/");

  equal(proxyInfo.host, "1.2.3.4", `second extension won`);
  equal(proxyInfo.port, "8888", `second extension won`);
  equal(proxyInfo.type, "https", `second extension won`);

  await ext2.unload();

  proxyInfo = await getProxyInfo();
  equal(proxyInfo, null, `expected no proxy`);
  await ext1.unload();
});

add_task(async function test_passthrough_guarded_url() {
  setEnterpriseGuards({
    "*": {
      runtime_blocked_hosts: ["*://guarded.example.com/*"],
      runtime_allowed_hosts: [],
    },
  });

  let extension = await getExtension({
    host: "1.2.3.4",
    port: 8888,
    type: "https",
  });

  let proxyInfo = await getProxyInfo("https://guarded.example.com/");
  equal(proxyInfo.host, "1.2.3.4", `guard ignored in proxy.onRequest`);

  await extension.unload();
  setEnterpriseGuards({});
});

add_task(async function test_ftp_disabled() {
  let extension = await getExtension({
    host: "1.2.3.4",
    port: 8888,
    type: "http",
  });

  let proxyInfo = await getProxyInfo("ftp://somewhere.mozilla.org/");

  equal(
    proxyInfo,
    null,
    `proxy of ftp request is not available when ftp is disabled`
  );

  await extension.unload();
});

add_task(async function test_ws() {
  let proxyRequestCount = 0;
  let proxy = createHttpServer();
  proxy.registerPathHandler("CONNECT", (request, response) => {
    response.setStatusLine(request.httpVersion, 404, "Proxy not found");
    ++proxyRequestCount;
  });

  let extension = await getExtension({
    host: proxy.identity.primaryHost,
    port: proxy.identity.primaryPort,
    type: "http",
  });

  // We need a page to use the WebSocket constructor, so let's use an extension.
  let dummy = ExtensionTestUtils.loadExtension({
    background() {
      // The connection will not be upgraded to WebSocket, so it will close.
      let ws = new WebSocket("wss://example.net/");
      ws.onclose = () => browser.test.sendMessage("websocket_closed");
    },
  });
  await dummy.startup();
  await dummy.awaitMessage("websocket_closed");
  await dummy.unload();

  equal(proxyRequestCount, 1, "Expected one proxy request");
  await extension.unload();
});
