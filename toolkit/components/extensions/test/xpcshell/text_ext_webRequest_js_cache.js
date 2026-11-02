"use strict";

let TEST_URL;
let TEST_URL_PATTERN;
let GOOD_JS_URL;

add_setup(async function () {
  const { NodeHTTPSServer } = ChromeUtils.importESModule(
    "resource://testing-common/NodeServer.sys.mjs"
  );

  const server = new NodeHTTPSServer();
  await server.start();

  registerCleanupFunction(async function () {
    await server.stop();
  });

  await server.registerPathHandler("/", (req, res) => {
    res.setHeader("Content-Type", "text/html");
    res.setHeader("Cache-Control", "max-age=10000");
    res.writeHead(200);
    const body = `
<!DOCTYPE HTML>

<html>
<head>
<meta charset="utf-8">
<script type="application/javascript" src="file_script_good.js"></script>
<script type="application/javascript" src="file_script_bad.js"></script>
</head>
<body>

<div id="test">Sample text</div>

</body>
</html>
`;
    res.end(body);
  });

  await server.registerPathHandler("/file_script_good.js", (req, res) => {
    res.setHeader("Content-Type", "text/javascript");
    res.setHeader("Cache-Control", "max-age=10000");
    res.writeHead(200);
    const body = `
"use strict";

window.success = window.success ? window.success + 1 : 1;
window.addEventListener(
  "load",
  () => {
    let el = document.createElement("div");
    el.setAttribute("id", "good");
    document.body.appendChild(el);
  },
  { once: true }
);
`;
    res.end(body);
  });

  await server.registerPathHandler("/file_script_bad.js", (req, res) => {
    res.setHeader("Content-Type", "text/javascript");
    res.setHeader("Cache-Control", "max-age=10000");
    res.writeHead(200);
    const body = `
"use strict";

window.failure = true;
window.addEventListener(
  "load",
  () => {
    let el = document.createElement("div");
    el.setAttribute("id", "bad");
    document.body.appendChild(el);
  },
  { once: true }
);
`;
    res.end(body);
  });

  TEST_URL = server.origin() + "/";

  // Replace port to work around bug 1468162.
  TEST_URL_PATTERN = TEST_URL.replace(/:[0-9]+\//, "/") + "*";
  GOOD_JS_URL = TEST_URL.replace(/:[0-9]+\//, "/") + "file_script_good.js";
});

// The JavaScript disk cache stores the serialized stencil into the necko's
// alternative data stream.  The request that uses the disk cache goes through
// necko, and the webRequest listeners are called.
//
// The JavaScript in-memory cache is stored in each content process, and
// the request that uses the in-memory cache doesn't go through necko, and
// the webRequest listeners are not called.

add_task(async function test_noDiskCache_noMemoryCache() {
  await test_filterResponseData({
    diskCache: false,
    memoryCache: false,
    listenersShouldBeCalled: true,
  });
});

add_task(async function test_useDiskCache_noMemoryCache() {
  await test_filterResponseData({
    diskCache: true,
    memoryCache: false,
    listenersShouldBeCalled: true,
  });
});

add_task(async function test_noDiskCache_useMemoryCache() {
  await test_filterResponseData({
    diskCache: false,
    memoryCache: true,
    listenersShouldBeCalled: false,
  });
});

add_task(async function test_useDiskCache_useMemoryCache() {
  await test_filterResponseData({
    diskCache: true,
    memoryCache: true,
    listenersShouldBeCalled: false,
  });
});

async function test_filterResponseData({
  diskCache,
  memoryCache,
  listenersShouldBeCalled,
}) {
  Services.cache2.clear();
  ChromeUtils.clearResourceCache();

  if (diskCache) {
    Services.prefs.setBoolPref(
      "dom.script_loader.bytecode_cache.enabled",
      true
    );
    Services.prefs.setIntPref("dom.script_loader.bytecode_cache.strategy", -1);
  } else {
    Services.prefs.setBoolPref(
      "dom.script_loader.bytecode_cache.enabled",
      false
    );
  }

  if (memoryCache) {
    Services.prefs.setBoolPref(
      "dom.script_loader.experimental.navigation_cache",
      true
    );
    Services.prefs.setBoolPref(
      "dom.script_loader.experimental.navigation_cache.check_memory_pressure",
      false
    );
  } else {
    Services.prefs.setBoolPref(
      "dom.script_loader.experimental.navigation_cache",
      false
    );
  }

  let extension = ExtensionTestUtils.loadExtension({
    background() {
      const GOOD_JS_URL = browser.runtime.getManifest().description;

      browser.webRequest.onBeforeRequest.addListener(
        () => {
          browser.test.sendMessage("onBeforeRequest");
        },
        {
          urls: [GOOD_JS_URL],
        },
        ["blocking"]
      );

      browser.webRequest.onHeadersReceived.addListener(
        () => {
          browser.test.sendMessage("onHeadersReceived");
        },
        {
          urls: [GOOD_JS_URL],
        },
        ["blocking"]
      );

      // NOTE: Do not explicitly call browser.webRequest.handlerBehaviorChanged,
      //       in order to test the cached behavior.
    },

    manifest: {
      permissions: ["webRequest", "webRequestBlocking", TEST_URL_PATTERN],
      description: GOOD_JS_URL,
    },
  });

  let contentPage = await ExtensionTestUtils.loadContentPage(TEST_URL);

  await extension.startup();

  // Use the same ContentPage, so that the in-memory cache is shared across
  // those two loads.
  await contentPage.loadURL(TEST_URL);

  if (listenersShouldBeCalled) {
    await Promise.all([
      extension.awaitMessage("onBeforeRequest"),
      extension.awaitMessage("onHeadersReceived"),
    ]);
  } else {
    // NOTE: The test harness should catch if there's any unhandled message.
  }

  await contentPage.close();
  await extension.unload();

  Services.prefs.clearUserPref(
    "dom.script_loader.experimental.navigation_cache"
  );
  Services.prefs.clearUserPref("dom.script_loader.bytecode_cache.enabled");
  Services.prefs.clearUserPref("dom.script_loader.bytecode_cache.strategy");
}
