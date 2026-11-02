"use strict";

const server = createHttpServer({ hosts: ["example.com"] });

server.registerPathHandler("/", (request, response) => {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/html", false);
  const body = `
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Test declarativeNetRequest behavior with in-memory cache</title>
  </head>
  <body>
    <h1>Test declarativeNetRequest behavior with in-memory cache</h1>
    <script src="/original.js"></script>
  </body>
</html>
`;
  response.bodyOutputStream.write(body, body.length);
});

server.registerPathHandler("/original.js", (request, response) => {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/javascript", false);
  response.setHeader("Cache-Control", "max-age=10000", false);
  const body = `
document.body.append("original.js loaded");
`;
  response.bodyOutputStream.write(body, body.length);
});

server.registerPathHandler("/redirected.js", (request, response) => {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/javascript", false);
  response.setHeader("Cache-Control", "max-age=10000", false);
  const body = `
document.body.append("redirected.js loaded");
`;
  response.bodyOutputStream.write(body, body.length);
});

Services.prefs.setBoolPref("dom.expose_test_interfaces", true);
registerCleanupFunction(function () {
  Services.prefs.clearUserPref("dom.expose_test_interfaces");
});

const TEST_URL = "http://example.com/";
const ORIGINAL_JS_URL = "http://example.com/original.js";
const REDIRECT_JS_URL = "http://example.com/redirected.js";

add_task(
  {
    pref_set: [["dom.script_loader.experimental.navigation_cache", false]],
  },
  async function testStaticRuleset_WithoutNavigationCache() {
    await testStaticRuleset(false);

    Services.prefs.clearUserPref(
      "dom.script_loader.experimental.navigation_cache"
    );
  }
);

add_task(
  {
    pref_set: [["dom.script_loader.experimental.navigation_cache", true]],
  },
  async function testStaticRuleset_WithNavigationCache() {
    await testStaticRuleset(true);

    Services.prefs.clearUserPref(
      "dom.script_loader.experimental.navigation_cache"
    );
  }
);

add_task(
  {
    pref_set: [["dom.script_loader.experimental.navigation_cache", false]],
  },
  async function testStaticRule_WithoutNavigationCache() {
    await testStaticRule(false);

    Services.prefs.clearUserPref(
      "dom.script_loader.experimental.navigation_cache"
    );
  }
);

add_task(
  {
    pref_set: [["dom.script_loader.experimental.navigation_cache", true]],
  },
  async function testStaticRule_WithNavigationCache() {
    await testStaticRule(true);

    Services.prefs.clearUserPref(
      "dom.script_loader.experimental.navigation_cache"
    );
  }
);

add_task(
  {
    pref_set: [["dom.script_loader.experimental.navigation_cache", false]],
  },
  async function testDynamicRule_WithoutNavigationCache() {
    await testDynamicRule(false);

    Services.prefs.clearUserPref(
      "dom.script_loader.experimental.navigation_cache"
    );
  }
);

add_task(
  {
    pref_set: [["dom.script_loader.experimental.navigation_cache", true]],
  },
  async function testDynamicRule_WithNavigationCache() {
    await testDynamicRule(true);

    Services.prefs.clearUserPref(
      "dom.script_loader.experimental.navigation_cache"
    );
  }
);

add_task(
  {
    pref_set: [["dom.script_loader.experimental.navigation_cache", false]],
  },
  async function testSessionRule_WithoutNavigationCache() {
    await testSessionRule(false);

    Services.prefs.clearUserPref(
      "dom.script_loader.experimental.navigation_cache"
    );
  }
);

add_task(
  {
    pref_set: [["dom.script_loader.experimental.navigation_cache", true]],
  },
  async function testSessionRule_WithNavigationCache() {
    await testSessionRule(true);

    Services.prefs.clearUserPref(
      "dom.script_loader.experimental.navigation_cache"
    );
  }
);

add_task(
  {
    pref_set: [["dom.script_loader.experimental.navigation_cache", true]],
  },
  async function testNoRule_WithNavigationCache() {
    await testNoRule(true);

    Services.prefs.clearUserPref(
      "dom.script_loader.experimental.navigation_cache"
    );
  }
);

async function testStaticRuleset(expectInvalidation) {
  const extensionData = {
    manifest: {
      manifest_version: 3,
      permissions: ["declarativeNetRequest"],
      host_permissions: ["http://example.com/*"],
      declarative_net_request: {
        rule_resources: [
          {
            id: "ruleset_1",
            enabled: true,
            path: "ruleset_1.json",
          },
        ],
      },
    },
    background() {
      browser.test.onMessage.addListener(async msg => {
        if (msg === "test:wait-ready") {
          browser.test.sendMessage("test:wait-ready:done");
          return;
        }

        if (msg === "test:enable") {
          await browser.declarativeNetRequest.updateEnabledRulesets({
            enableRulesetIds: ["ruleset_1"],
          });
          browser.test.sendMessage("test:enable:done");
          return;
        }

        if (msg === "test:disable") {
          await browser.declarativeNetRequest.updateEnabledRulesets({
            disableRulesetIds: ["ruleset_1"],
          });
          browser.test.sendMessage("test:disable:done");
          return;
        }

        if (msg === "test:enable-disable") {
          for (let i = 0; i < 10; i++) {
            await browser.declarativeNetRequest.updateEnabledRulesets({
              enableRulesetIds: ["ruleset_1"],
            });
            await browser.declarativeNetRequest.updateEnabledRulesets({
              disableRulesetIds: ["ruleset_1"],
            });
          }
          browser.test.sendMessage("test:enable-disable:done");
          return;
        }

        browser.test.fail(`Unexpected test message: ${msg}`);
      });
    },
    files: {
      "ruleset_1.json": JSON.stringify([
        {
          id: 1,
          condition: {
            urlFilter: ORIGINAL_JS_URL,
          },
          action: {
            type: "redirect",
            redirect: { url: REDIRECT_JS_URL },
          },
        },
      ]),
    },
  };

  await doTest({
    extensionData,
    enabledAtStart: true,
    canEnable: true,
    expectInvalidation,
  });
}

async function testStaticRule(expectInvalidation) {
  const extensionData = {
    manifest: {
      manifest_version: 3,
      permissions: ["declarativeNetRequest"],
      host_permissions: ["http://example.com/*"],
      declarative_net_request: {
        rule_resources: [
          {
            id: "ruleset_1",
            enabled: true,
            path: "ruleset_1.json",
          },
        ],
      },
    },
    background() {
      browser.test.onMessage.addListener(async msg => {
        if (msg === "test:wait-ready") {
          browser.test.sendMessage("test:wait-ready:done");
          return;
        }

        if (msg === "test:enable") {
          await browser.declarativeNetRequest.updateStaticRules({
            rulesetId: "ruleset_1",
            enableRuleIds: [1],
          });
          browser.test.sendMessage("test:enable:done");
          return;
        }

        if (msg === "test:disable") {
          await browser.declarativeNetRequest.updateStaticRules({
            rulesetId: "ruleset_1",
            disableRuleIds: [1],
          });
          browser.test.sendMessage("test:disable:done");
          return;
        }

        if (msg === "test:enable-disable") {
          for (let i = 0; i < 10; i++) {
            await browser.declarativeNetRequest.updateStaticRules({
              rulesetId: "ruleset_1",
              enableRuleIds: [1],
            });
            await browser.declarativeNetRequest.updateStaticRules({
              rulesetId: "ruleset_1",
              disableRuleIds: [1],
            });
          }
          browser.test.sendMessage("test:enable-disable:done");
          return;
        }

        browser.test.fail(`Unexpected test message: ${msg}`);
      });
    },
    files: {
      "ruleset_1.json": JSON.stringify([
        {
          id: 1,
          condition: {
            urlFilter: ORIGINAL_JS_URL,
          },
          action: {
            type: "redirect",
            redirect: { url: REDIRECT_JS_URL },
          },
        },
      ]),
    },
  };

  await doTest({
    extensionData,
    enabledAtStart: true,
    canEnable: true,
    expectInvalidation,
  });
}

async function testDynamicRule(expectInvalidation) {
  const extensionData = {
    manifest: {
      manifest_version: 3,
      permissions: ["declarativeNetRequest"],
      host_permissions: ["http://example.com/*"],
    },
    background() {
      const ORIGINAL_JS_URL = "http://example.com/original.js";
      const REDIRECT_JS_URL = "http://example.com/redirected.js";
      browser.test.onMessage.addListener(async msg => {
        if (msg === "test:wait-ready") {
          browser.test.sendMessage("test:wait-ready:done");
          return;
        }

        if (msg === "test:enable") {
          await browser.declarativeNetRequest.updateDynamicRules({
            addRules: [
              {
                id: 1,
                condition: {
                  urlFilter: ORIGINAL_JS_URL,
                },
                action: {
                  type: "redirect",
                  redirect: { url: REDIRECT_JS_URL },
                },
              },
            ],
          });
          browser.test.sendMessage("test:enable:done");
          return;
        }

        if (msg === "test:disable") {
          await browser.declarativeNetRequest.updateDynamicRules({
            removeRuleIds: [1],
          });
          browser.test.sendMessage("test:disable:done");
          return;
        }

        if (msg === "test:enable-disable") {
          for (let i = 2; i < 10; i++) {
            await browser.declarativeNetRequest.updateDynamicRules({
              addRules: [
                {
                  id: i,
                  condition: {
                    urlFilter: ORIGINAL_JS_URL,
                  },
                  action: {
                    type: "redirect",
                    redirect: { url: REDIRECT_JS_URL },
                  },
                },
              ],
            });
            await browser.declarativeNetRequest.updateDynamicRules({
              removeRuleIds: [i],
            });
          }
          browser.test.sendMessage("test:enable-disable:done");
          return;
        }

        browser.test.fail(`Unexpected test message: ${msg}`);
      });
    },
  };

  await doTest({
    extensionData,
    enabledAtStart: false,
    canEnable: true,
    expectInvalidation,
  });
}

async function testSessionRule(expectInvalidation) {
  const extensionData = {
    manifest: {
      manifest_version: 3,
      permissions: ["declarativeNetRequest"],
      host_permissions: ["http://example.com/*"],
    },
    background() {
      const ORIGINAL_JS_URL = "http://example.com/original.js";
      const REDIRECT_JS_URL = "http://example.com/redirected.js";
      browser.test.onMessage.addListener(async msg => {
        if (msg === "test:wait-ready") {
          browser.test.sendMessage("test:wait-ready:done");
          return;
        }

        if (msg === "test:enable") {
          await browser.declarativeNetRequest.updateSessionRules({
            addRules: [
              {
                id: 1,
                condition: {
                  urlFilter: ORIGINAL_JS_URL,
                },
                action: {
                  type: "redirect",
                  redirect: { url: REDIRECT_JS_URL },
                },
              },
            ],
          });
          browser.test.sendMessage("test:enable:done");
          return;
        }

        if (msg === "test:disable") {
          await browser.declarativeNetRequest.updateSessionRules({
            removeRuleIds: [1],
          });
          browser.test.sendMessage("test:disable:done");
          return;
        }

        if (msg === "test:enable-disable") {
          for (let i = 2; i < 10; i++) {
            await browser.declarativeNetRequest.updateSessionRules({
              addRules: [
                {
                  id: i,
                  condition: {
                    urlFilter: ORIGINAL_JS_URL,
                  },
                  action: {
                    type: "redirect",
                    redirect: { url: REDIRECT_JS_URL },
                  },
                },
              ],
            });
            await browser.declarativeNetRequest.updateSessionRules({
              removeRuleIds: [i],
            });
          }
          browser.test.sendMessage("test:enable-disable:done");
          return;
        }

        browser.test.fail(`Unexpected test message: ${msg}`);
      });
    },
  };

  await doTest({
    extensionData,
    enabledAtStart: false,
    canEnable: true,
    expectInvalidation,
  });
}

async function testNoRule(expectInvalidation) {
  const extensionData = {
    manifest: {
      manifest_version: 3,
      permissions: ["declarativeNetRequest"],
      host_permissions: ["http://example.com/*"],
    },
    background() {
      browser.test.onMessage.addListener(async msg => {
        if (msg === "test:wait-ready") {
          // Just use the API without adding any rule.
          browser.declarativeNetRequest
            .isRegexSupported({
              regex: "a.*b",
            })
            .then(() => {
              browser.test.sendMessage("test:wait-ready:done");
            });
        }
      });
    },
  };

  await doTest({
    extensionData,
    enabledAtStart: false,
    canEnable: false,
    expectInvalidation,
  });
}

async function doTest({
  extensionData,
  enabledAtStart,
  expectInvalidation,
  canEnable,
}) {
  const contentPage = await ExtensionTestUtils.loadContentPage(TEST_URL);

  function waitForTestMessage(message) {
    return new Promise(resolve => {
      contentPage.browser.messageManager.addMessageListener(message, resolve);
    });
  }

  const readyPromise = waitForTestMessage("Test:Ready");
  await contentPage.loadFrameScript(function () {
    let count = 0;

    const observer = (subject, topic, data) => {
      const param = {};
      for (const line of data.split("\n")) {
        const m = line.match(/^([^:]+):(.*)/);
        param[m[1]] = m[2];
      }

      if (param.event === "memorycache:invalidate") {
        count++;
        this.sendAsyncMessage("Test:Invalidate", { count });
      }
    };
    Services.obs.addObserver(observer, "ScriptLoaderTest");

    this.addMessageListener("Test:GetInvalidateCount", () => {
      this.sendAsyncMessage("Test:InvalidateCount", { count });
    });

    this.sendAsyncMessage("Test:Ready");
  });
  await readyPromise;

  async function getResult() {
    return contentPage.spawn([], async () => {
      const text = this.content.document.body.textContent;
      let result;
      if (text.includes("original.js loaded")) {
        result = "original";
      } else if (text.includes("redirected.js loaded")) {
        result = "redirected";
      } else {
        result = "?";
      }
      return result;
    });
  }

  let result = await getResult();
  Assert.equal(
    result,
    "original",
    "Original js should be used before enabling"
  );

  const extension = ExtensionTestUtils.loadExtension(extensionData);

  // For the case we expect invalidation, we wait each invalidation before
  // proceeding to the next one.
  //
  // Each operation before/between maybeExpectInvalidationWith should
  // perform only one action that triggers one invalidation, execpt for the case
  // that explicitly tests the coalescing behavior which performs multiple
  // operations that should be coalesced.
  let nextInvalidationCount = 1;
  async function maybeExpectInvalidationWith(f) {
    if (!expectInvalidation) {
      await f();
      return;
    }
    const invalidationPromise = waitForTestMessage("Test:Invalidate");
    await f();
    const invalidationResult = await invalidationPromise;
    Assert.equal(invalidationResult.data.count, nextInvalidationCount);
    nextInvalidationCount++;
  }

  if (enabledAtStart) {
    await maybeExpectInvalidationWith(async () => {
      await extension.startup();
    });

    extension.sendMessage("test:wait-ready");
    await extension.awaitMessage("test:wait-ready:done");

    await contentPage.reload();
    result = await getResult();
    Assert.equal(
      result,
      "redirected",
      "Redirected js should be used after starting up with enabled rule"
    );
  } else {
    await extension.startup();

    extension.sendMessage("test:wait-ready");
    await extension.awaitMessage("test:wait-ready:done");

    await contentPage.reload();
    result = await getResult();
    Assert.equal(
      result,
      "original",
      "Original js should be used after starting up without enabled rule"
    );
  }

  if (canEnable) {
    if (!enabledAtStart) {
      await maybeExpectInvalidationWith(async () => {
        extension.sendMessage("test:enable");
        await extension.awaitMessage("test:enable:done");
      });

      await contentPage.reload();
      result = await getResult();
      Assert.equal(
        result,
        "redirected",
        "Redirected js should be used after enabling"
      );
    }

    await contentPage.reload({ bypassCache: true });
    result = await getResult();
    Assert.equal(
      result,
      "redirected",
      "Redirected js should be used with force reload"
    );

    // See the comment below for the details.
    Services.prefs.setIntPref("extensions.dnr.invalidateCacheTimeout", 5000);
    await maybeExpectInvalidationWith(async () => {
      extension.sendMessage("test:disable");
      await extension.awaitMessage("test:disable:done");
    });

    await contentPage.reload();
    result = await getResult();
    Assert.equal(
      result,
      "original",
      "Original js should be used after disabling the rule"
    );

    // Enabling/disabling the rule multiple times should invalidate the cache,
    // but only once or twice.
    //
    // Given the long timeout set before the last modification above, the last
    // invalidation from "disable" suppresses all invalidations happens within
    // 5000ms time frame.
    //
    // Both the first invalidation from the "enable" and the second invalidaion
    // from the "disable" below should be coalesced into one invalidation after
    // the 5000ms timeout.
    //
    // We use 5000ms timeout to ensure all invalidations are coalesced even if
    // the execution is slow, but the timeout shouldn't be too large, in
    // order to receive the coalesced invalidation within the test execution.
    //
    // This function (doTest) is called 9 times, and each takes 5s here.
    // The entire test will take 45s and some more for other parts.
    // The default timeout for the xpcshell test is 30s, and thus
    // we request 2x timeout in the xpcshell.toml file.
    await maybeExpectInvalidationWith(async () => {
      extension.sendMessage("test:enable-disable");
      await extension.awaitMessage("test:enable-disable:done");
    });
    Services.prefs.clearUserPref("extensions.dnr.invalidateCacheTimeout");

    await contentPage.reload();
    result = await getResult();
    Assert.equal(
      result,
      "original",
      "Original js should be used after enabling/disabling the rule"
    );

    await maybeExpectInvalidationWith(async () => {
      extension.sendMessage("test:enable");
      await extension.awaitMessage("test:enable:done");
    });

    await contentPage.reload();
    result = await getResult();
    Assert.equal(
      result,
      "redirected",
      "Redirected js should be used after re-enabling the rule"
    );

    // Unloading the extension with an enabled rule should invalidate the
    // cache.
    await maybeExpectInvalidationWith(async () => {
      await extension.unload();
    });
  } else {
    // Unloading the extension with no enabled rule should not invalidate the
    // cache.
    await extension.unload();
  }

  if (!canEnable || !expectInvalidation) {
    const countPromise = waitForTestMessage("Test:InvalidateCount");
    contentPage.browser.messageManager.sendAsyncMessage(
      "Test:GetInvalidateCount"
    );
    const result = await countPromise;
    Assert.equal(result.data.count, 0, "Invalidation shouldn't have happened");
  }

  await contentPage.reload();
  result = await getResult();
  Assert.equal(
    result,
    "original",
    "Original js should be used after unloading"
  );

  await contentPage.close();
}
