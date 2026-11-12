/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test that JS sources with identical content get deduplicated.
 * This test verifies that the profiler uses hash-based UUIDs for sources,
 * enabling deduplication when the same source is loaded multiple times.
 */
add_task(async function test_js_sources_deduplication() {
  await ProfilerTestUtils.assertProfilerInactive();

  const url = BASE_URL + "simple.html";
  await BrowserTestUtils.withNewTab(url, async contentBrowser => {
    const contentPid = await SpecialPowers.spawn(contentBrowser, [], () => {
      return Services.appinfo.processID;
    });

    await ProfilerTestUtils.startProfiler({ features: ["js", "jssources"] });

    // Load the same script file multiple times.
    // This simulates what happens when the same source is loaded in multiple contexts.
    await SpecialPowers.spawn(contentBrowser, [], async () => {
      // Dynamically inject the same script multiple times.
      const loadScript = () => {
        return new Promise((resolve, reject) => {
          const script = content.document.createElement("script");
          script.src = "test_dedup_script.js";
          script.onload = resolve;
          script.onerror = reject;
          content.document.head.appendChild(script);
        });
      };

      for (let i = 0; i < 10; i++) {
        await loadScript();
      }
    });

    const { contentProcess } = await stopProfilerNowAndGetThreads(contentPid);

    Assert.ok(contentProcess.sources, "Profile should have sources table");
    Assert.ok(contentProcess.sources.data, "Sources should have data");

    const sourceFilenameCol = contentProcess.sources.schema.filename;

    info(`Total sources: ${contentProcess.sources.data.length}`);

    // Look for the test script.
    let testScriptCount = 0;
    for (const sourceEntry of contentProcess.sources.data) {
      const filename = sourceEntry[sourceFilenameCol];
      if (filename && filename.includes("test_dedup_script.js")) {
        testScriptCount++;
      }
    }

    // Should have exactly one entry for test_dedup_script.js despite loading it multiple times.
    Assert.equal(
      testScriptCount,
      1,
      "Should have exactly one deduplicated entry for test_dedup_script.js"
    );
  });
});
