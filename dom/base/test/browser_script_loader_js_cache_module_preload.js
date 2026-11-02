// A <link rel="modulepreload"> whose module is served from the in-memory
// navigation cache (SharedScriptCache) is fetched without opening a channel,
// just like a module already present in the module map. Such a preload must
// still fire a "load" event rather than a spurious "error".
const BASE_URL = "http://mochi.test:8888/browser/dom/base/test/";
const MODULE_URL = BASE_URL + "file_js_cache_small.js";

add_task(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.expose_test_interfaces", true],
      ["dom.script_loader.bytecode_cache.enabled", true],
      ["dom.script_loader.bytecode_cache.strategy", 0],
      ["dom.script_loader.experimental.navigation_cache", true],
      ["dom.script_loader.disk_cache_delay_ms", 0],
    ],
  });

  await BrowserTestUtils.withNewTab(BASE_URL + "empty.html", async browser => {
    const tab = gBrowser.getTabForBrowser(browser);

    ChromeUtils.clearResourceCache();
    Services.cache2.clear();

    // Load the module normally so its stencil is stored in the process-wide
    // SharedScriptCache. Wait for "memorycache:saved" so the cache is populated
    // before we reload.
    await SpecialPowers.spawn(browser, [MODULE_URL], async url => {
      const saved = new Promise(resolve => {
        const obs = (subject, topic, data) => {
          const params = {};
          for (const line of data.split("\n")) {
            const i = line.indexOf(":");
            params[line.slice(0, i)] = line.slice(i + 1);
          }
          if (params.event === "memorycache:saved" && params.url === url) {
            Services.obs.removeObserver(obs, "ScriptLoaderTest");
            resolve();
          }
        };
        Services.obs.addObserver(obs, "ScriptLoaderTest");
      });

      const script = content.document.createElement("script");
      script.type = "module";
      script.crossOrigin = "";
      script.src = url;
      content.document.body.appendChild(script);

      await saved;
    });

    // Reload to get a fresh document (empty module map) while the
    // SharedScriptCache persists across the same-origin navigation.
    await BrowserTestUtils.reloadTab(tab);

    // In the fresh document, a <link rel="modulepreload"> for the same URL is
    // served from the memory cache (no channel opened). It must fire "load",
    // and we assert the memory-cache path was actually taken.
    const result = await SpecialPowers.spawn(
      browser,
      [MODULE_URL],
      async url => {
        let usedMemoryCache = false;
        const obs = (subject, topic, data) => {
          const params = {};
          for (const line of data.split("\n")) {
            const i = line.indexOf(":");
            params[line.slice(0, i)] = line.slice(i + 1);
          }
          if (params.event === "load:memorycache" && params.url === url) {
            usedMemoryCache = true;
          }
        };
        Services.obs.addObserver(obs, "ScriptLoaderTest");

        const link = content.document.createElement("link");
        link.rel = "modulepreload";
        link.as = "script";
        link.crossOrigin = "";
        link.href = url;

        const event = await new Promise(resolve => {
          link.addEventListener("load", () => resolve("load"), { once: true });
          link.addEventListener("error", () => resolve("error"), {
            once: true,
          });
          content.document.head.appendChild(link);
        });

        Services.obs.removeObserver(obs, "ScriptLoaderTest");
        return { event, usedMemoryCache };
      }
    );

    is(
      result.event,
      "load",
      "modulepreload served from memory cache fires load"
    );
    ok(
      result.usedMemoryCache,
      "the modulepreload was served from the in-memory navigation cache"
    );
  });

  await SpecialPowers.popPrefEnv();
});
