/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test that sourceIndexes are collected properly in profiles.
 */
add_task(async function test_profile_js_sources() {
  await ProfilerTestUtils.assertProfilerInactive();

  const url = BASE_URL + "simple.html";
  await BrowserTestUtils.withNewTab(url, async contentBrowser => {
    // Start profiling to capture JS sources
    await ProfilerTestUtils.startProfiler({ features: ["js", "jssources"] });

    const contentPid = await SpecialPowers.spawn(
      contentBrowser,
      [],
      () => Services.appinfo.processID
    );

    // Execute some JavaScript that will create frames with sourceIndexes
    await SpecialPowers.spawn(contentBrowser, [], () => {
      content.window.eval(`
        function testFunction() {
          // Simple function to generate JIT frames with 1ms busy loop
          const start = performance.now();
          let result = 0;
          while (performance.now() - start < 1) {
            for (let i = 0; i < 1000; i++) {
              result += Math.random();
            }
          }
          return result;
        }

        // Call the function multiple times to trigger JIT compilation
        for (let i = 0; i < 100; i++) {
          testFunction();
        }
      `);
    });

    const { contentThread } =
      await waitSamplingAndStopProfilerAndGetThreads(contentPid);

    // Check that we have frames with sourceIndexes in location strings
    const { frameTable } = contentThread;
    const FRAME_LOCATION_SLOT = frameTable.schema.location;

    // Find frames that have sourceIndexes in their location strings
    const framesWithSourceIndexes = [];
    for (const frame of frameTable.data) {
      const location = contentThread.stringTable[frame[FRAME_LOCATION_SLOT]];
      if (location && location.match(/\[\d+\]$/)) {
        framesWithSourceIndexes.push({ frame, location });
      }
    }

    Assert.greater(
      framesWithSourceIndexes.length,
      0,
      "Found frames with sourceIndexes in location strings"
    );

    // Verify that sourceIndexes are valid numbers
    for (const { location } of framesWithSourceIndexes) {
      const sourceIndexMatch = location.match(/\[(\d+)\]$/);
      const sourceIndex = parseInt(sourceIndexMatch[1]);
      Assert.greaterOrEqual(
        sourceIndex,
        0,
        `SourceIndex ${sourceIndex} is a valid number`
      );
    }

    info(`Found ${framesWithSourceIndexes.length} frames with sourceIndexes`);

    // Check if testFunction frames have sourceIndexes
    let testFunctionFrames = 0;

    for (const frame of frameTable.data) {
      const location = contentThread.stringTable[frame[FRAME_LOCATION_SLOT]];
      if (location && location.includes("testFunction")) {
        testFunctionFrames++;
        if (!location.match(/\[\d+\]$/)) {
          Assert.ok(false, `Failed to find sourceIndex for ${location}`);
        }
      }
    }

    Assert.greater(
      testFunctionFrames,
      0,
      "At least some testFunction frames should have sourceIndexes"
    );
  });
});

/**
 * Test that JS tracer frames include sourceIndexes when tracing is enabled.
 */
add_task(async function test_profile_js_sources_with_tracing() {
  await ProfilerTestUtils.assertProfilerInactive();

  // sync about:blank (bug 543435) somehow causes a
  // CycleCollectedJSContext::EndExecutionTracingAsync runnable to leak. See bug 2000283
  await TestUtils.waitForTick();
  await TestUtils.waitForTick();

  const url = BASE_URL + "tracing.html";
  await BrowserTestUtils.withNewTab("about:blank", async contentBrowser => {
    // Start profiling with tracing to capture JS tracer frames
    await ProfilerTestUtils.startProfiler({
      features: ["tracing", "jssources"],
    });

    await BrowserTestUtils.startLoadingURIString(contentBrowser, url);
    await BrowserTestUtils.browserLoaded(contentBrowser, false, url);
    const contentPid = await SpecialPowers.spawn(
      contentBrowser,
      [],
      () => Services.appinfo.processID
    );

    const { contentThread } = await stopProfilerNowAndGetThreads(contentPid);

    // Check that tracer frames have sourceIndexes in location strings
    const { frameTable } = contentThread;
    const FRAME_LOCATION_SLOT = frameTable.schema.location;

    // Look for our test functions from tracing.html with sourceIndexes
    const tracingFramesWithSourceIndexes = [];
    for (const frame of frameTable.data) {
      const location = contentThread.stringTable[frame[FRAME_LOCATION_SLOT]];
      if (
        location &&
        location.includes("tracing.html") &&
        location.match(/\[\d+\]$/)
      ) {
        tracingFramesWithSourceIndexes.push({ frame, location });
      }
    }

    dump(tracingFramesWithSourceIndexes.map(a => a.location));

    Assert.greater(
      tracingFramesWithSourceIndexes.length,
      0,
      "Found tracing frames with sourceIndexes"
    );

    info(
      `Found ${tracingFramesWithSourceIndexes.length} tracing frames with sourceIndexes`
    );
  });
});

/**
 * Test that sourceIndexes work with eval.
 */
add_task(async function test_profile_js_sources_location_format() {
  await ProfilerTestUtils.assertProfilerInactive();

  const url = BASE_URL + "simple.html";
  await BrowserTestUtils.withNewTab(url, async contentBrowser => {
    await ProfilerTestUtils.startProfiler({ features: ["js", "jssources"] });
    const contentPid = await SpecialPowers.spawn(
      contentBrowser,
      [],
      () => Services.appinfo.processID
    );

    // Execute JavaScript with eval
    await SpecialPowers.spawn(contentBrowser, [], () => {
      content.window.eval(`
        function sourceIndexTest() {
          return 42;
        }
        sourceIndexTest();
      `);
    });

    const { contentThread } =
      await waitSamplingAndStopProfilerAndGetThreads(contentPid);

    const { frameTable } = contentThread;
    const FRAME_LOCATION_SLOT = frameTable.schema.location;

    // Find frames with sourceIndex in their location strings
    const framesWithSourceIndexes = [];
    for (const frame of frameTable.data) {
      const location = contentThread.stringTable[frame[FRAME_LOCATION_SLOT]];
      if (location && location.match(/\[\d+\]$/)) {
        framesWithSourceIndexes.push({ frame, location });
        info(`Found sourceIndex in location: ${location}`);
      }
    }

    Assert.greater(
      framesWithSourceIndexes.length,
      0,
      "Found at least one frame location that includes sourceIndex in brackets"
    );
  });
});
