add_task(async function test() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.expose_test_interfaces", true],
      ["dom.script_loader.bytecode_cache.enabled", true],
      ["dom.script_loader.bytecode_cache.strategy", 0],
      ["dom.script_loader.experimental.navigation_cache", true],
      ["dom.script_loader.disk_cache_delay_ms", 0],
    ],
  });

  await BrowserTestUtils.withNewTab(
    JS_CACHE_BASE_URL_HTTPS + "file_js_cache_module_csp.sjs?main.html",
    async browser => {
      await SpecialPowers.spawn(browser, [], async () => {
        const netErrorCard = await ContentTaskUtils.waitForCondition(
          () => content.document.body?.getAttribute("result") === "OK"
        );
      });

      ok(true, "Passed");
    }
  );

  await SpecialPowers.popPrefEnv();
});
