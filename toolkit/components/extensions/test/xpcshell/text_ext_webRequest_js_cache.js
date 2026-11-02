"use strict";

const server = createHttpServer({ hosts: ["example.com"] });

server.registerDirectory("/data/", do_get_file("data"));

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
      browser.webRequest.onBeforeRequest.addListener(
        () => {
          browser.test.sendMessage("onBeforeRequest");
        },
        {
          urls: ["http://example.com/data/file_script_good.js"],
        },
        ["blocking"]
      );

      browser.webRequest.onHeadersReceived.addListener(
        () => {
          browser.test.sendMessage("onHeadersReceived");
        },
        {
          urls: ["http://example.com/data/file_script_good.js"],
        },
        ["blocking"]
      );

      // NOTE: Do not explicitly call browser.webRequest.handlerBehaviorChanged,
      //       in order to test the cached behavior.
    },

    manifest: {
      permissions: ["webRequest", "webRequestBlocking", "http://example.com/*"],
    },
  });

  let contentPage = await ExtensionTestUtils.loadContentPage(
    "http://example.com/data/file_script.html"
  );

  await extension.startup();

  // Use the same ContentPage, so that the in-memory cache is shared across
  // those two loads.
  await contentPage.loadURL("http://example.com/data/file_script.html");

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
