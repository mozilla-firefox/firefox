/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test that JS sources are collected and included in profile additional information.
 */
add_task(async function test_js_sources_in_profile_additional_info() {
  await ProfilerTestUtils.assertProfilerInactive();

  const url = BASE_URL + "simple.html";
  await BrowserTestUtils.withNewTab(url, async contentBrowser => {
    await ProfilerTestUtils.startProfiler({ features: ["js", "jssources"] });
    // Execute some JavaScript to create sources
    await SpecialPowers.spawn(contentBrowser, [], () => {
      content.window.eval(`
        function testSourceFunction() {
          console.log("This is a test function");
          return 42;
        }
        testSourceFunction();
      `);
    });

    // Get the profile with additional information
    // Note: We don't have to await here for the pause because the await for
    // getProfileDataAsGzippedArrayBuffer will actively wait for the pause to
    // be executed in the child process first.
    Services.profiler.Pause();
    const profileData =
      await Services.profiler.getProfileDataAsGzippedArrayBuffer();
    await Services.profiler.StopProfiler();

    // Verify we got profile data
    Assert.ok(!!profileData, "Should receive profile data");
    Assert.ok(!!profileData.profile, "Should have profile data");
    Assert.ok(
      !!profileData.additionalInformation,
      "Should have additional information"
    );

    // Check that the additional information has JS sources
    const sources = profileData.additionalInformation.jsSources;
    Assert.ok(!!sources, "Additional info should contain jsSources");
    Assert.equal(typeof sources, "object", "jsSources should be an object");

    // Check that we have some JS sources
    Assert.greater(
      Object.keys(sources).length,
      0,
      "Should have at least one JS source"
    );

    info(`Total JS sources collected: ${sources.length}`);

    // Check the sources to verify they contain actual source text
    for (const sourceId in sources) {
      const sourceInfo = sources[sourceId];
      Assert.ok(
        typeof sourceId === "string" && !!sourceId.length,
        "sourceId should be a non-empty string"
      );
      Assert.ok(
        typeof sourceInfo?.sourceText === "string" &&
          !!sourceInfo.sourceText.length,
        `Source ${sourceId} should have non-empty sourceText`
      );
    }
  });
});

/**
 * Test that different types of JS sources are handled correctly.
 */
add_task(async function test_js_sources_different_types() {
  await ProfilerTestUtils.assertProfilerInactive();

  const url = BASE_URL + "simple.html";
  await BrowserTestUtils.withNewTab(url, async contentBrowser => {
    await ProfilerTestUtils.startProfiler({ features: ["js", "jssources"] });
    // Execute different types of JavaScript to test source collection
    await SpecialPowers.spawn(contentBrowser, [], () => {
      // Inline script
      content.window.eval(`
        function inlineFunction() {
          return "inline";
        }
        inlineFunction();
      `);

      // Anonymous function
      content.window.eval(`
        (function() {
          console.log("anonymous function");
        })();
      `);
    });

    // Get profile data
    // Note: We don't have to await here for the pause because the await for
    // getProfileDataAsGzippedArrayBuffer will actively wait for the pause to
    // be executed in the child process first.
    Services.profiler.Pause();
    const profileData =
      await Services.profiler.getProfileDataAsGzippedArrayBuffer();
    await Services.profiler.StopProfiler();

    const sources = profileData.additionalInformation.jsSources;
    Assert.ok(!!sources, "Additional info should contain jsSources");
    Assert.equal(typeof sources, "object", "jsSources should be an object");

    // Check that we captured sources for different types of JavaScript
    let inlineSourceCount = 0;
    let inlineSourceLength = 0;

    for (const sourceId in sources) {
      const sourceText = sources[sourceId]?.sourceText;
      if (typeof sourceText === "string" && sourceText.length) {
        // Check if we found our test functions
        if (
          sourceText.includes("inlineFunction") ||
          sourceText.includes("anonymous function")
        ) {
          inlineSourceCount += 1;
          inlineSourceLength += sourceText.length;
        }
      }
    }

    Assert.greater(
      inlineSourceLength,
      0,
      "Should have collected source text with content"
    );

    info(`Total source text length: ${inlineSourceLength} characters`);

    Assert.greaterOrEqual(
      inlineSourceCount,
      2,
      "Should have collected all the test functions"
    );
  });
});

/**
 * Test that external JS files are properly collected in JS sources.
 */
add_task(async function test_js_sources_external_scripts() {
  await ProfilerTestUtils.assertProfilerInactive();

  const url = BASE_URL + "page_with_external_js.html";
  await BrowserTestUtils.withNewTab(url, async contentBrowser => {
    await ProfilerTestUtils.startProfiler({ features: ["js", "jssources"] });
    // Wait for page to load and execute scripts
    await SpecialPowers.spawn(contentBrowser, [], () => {
      return new Promise(resolve => {
        if (content.document.readyState === "complete") {
          resolve();
        } else {
          content.window.addEventListener("load", resolve);
        }
      });
    });

    // Get profile data
    // Note: We don't have to await here for the pause because the await for
    // getProfileDataAsGzippedArrayBuffer will actively wait for the pause to
    // be executed in the child process first.
    Services.profiler.Pause();
    const profileData =
      await Services.profiler.getProfileDataAsGzippedArrayBuffer();
    await Services.profiler.StopProfiler();

    const sources = profileData.additionalInformation.jsSources;
    Assert.ok(!!sources, "Additional info should contain jsSources");
    Assert.equal(typeof sources, "object", "jsSources should be an object");

    // Look for external script sources
    let foundExternalScript = false;
    let foundInlineScript = false;
    let externalScriptSource = null;

    for (const sourceId in sources) {
      const sourceText = sources[sourceId]?.sourceText;
      if (typeof sourceText === "string" && sourceText.length) {
        // Check for external script content
        if (
          sourceText.includes("externalFunction") &&
          sourceText.includes("calculateSum")
        ) {
          foundExternalScript = true;
          externalScriptSource = sourceText;
          info(`Found external script source (${sourceText.length} chars)`);
        }

        // Check for inline script content
        if (
          sourceText.includes("inlineFunction") &&
          sourceText.includes("window.onload")
        ) {
          foundInlineScript = true;
          info(`Found inline script source (${sourceText.length} chars)`);
        }
      }
    }

    Assert.ok(
      foundExternalScript,
      "Should find external JavaScript file content in sources"
    );

    Assert.ok(
      foundInlineScript,
      "Should find inline JavaScript content in sources"
    );

    // Verify external script has expected functions
    Assert.ok(
      externalScriptSource.includes("This is an external function"),
      "External script should contain expected comment"
    );

    Assert.ok(
      externalScriptSource.includes("calculateSum"),
      "External script should contain calculateSum function"
    );

    info("Successfully verified external JS file collection");
  });
});

/**
 * Test that non-ASCII characters in UTF-8 external scripts are decoded
 * correctly rather than interpreted as Latin-1.
 */
add_task(async function test_js_sources_utf8_external_script() {
  await ProfilerTestUtils.assertProfilerInactive();

  await ProfilerTestUtils.startProfiler({ features: ["js", "jssources"] });
  const url = BASE_URL + "page_with_utf8_external_js.html";
  await BrowserTestUtils.withNewTab(url, async () => {
    Services.profiler.Pause();
    const profileData =
      await Services.profiler.getProfileDataAsGzippedArrayBuffer();
    await Services.profiler.StopProfiler();

    const sources = profileData.additionalInformation.jsSources;
    const sourceText = Object.values(sources).find(source =>
      source?.url?.endsWith("/utf8_external_script.js")
    )?.sourceText;

    Assert.ok(!!sourceText, "Should find the UTF-8 external script source");
    Assert.ok(
      sourceText.includes(
        "// Non-ASCII characters: \u00e9\u00e9\u00e9\u00e9\n"
      ),
      "Comment should contain correctly decoded non-ASCII characters"
    );
    Assert.ok(
      sourceText.includes('"\u00fcn\u00efc\u00f6d\u00e9\u6f22"'),
      "String literal should contain correctly decoded characters beyond Latin-1"
    );
  });
});

/**
 * Test that unavailable JS sources are omitted from jsSources rather than
 * included with a placeholder value. When the "jssources" feature is not
 * enabled, all sources have unavailable data and should not appear in jsSources.
 */
add_task(async function test_js_sources_unavailable_are_omitted() {
  await ProfilerTestUtils.assertProfilerInactive();

  const url = BASE_URL + "simple.html";
  await BrowserTestUtils.withNewTab(url, async contentBrowser => {
    // Start profiler with js feature but WITHOUT jssources. All source data
    // will be unavailable.
    await ProfilerTestUtils.startProfiler({ features: ["js"] });

    await SpecialPowers.spawn(contentBrowser, [], () => {
      content.window.eval(`
        function testSourceFunction() {
          console.log("This is a test function");
          return 42;
        }
        testSourceFunction();
      `);
    });

    Services.profiler.Pause();
    const profileData =
      await Services.profiler.getProfileDataAsGzippedArrayBuffer();
    await Services.profiler.StopProfiler();

    Assert.ok(
      !!profileData.additionalInformation,
      "Should have additional information"
    );

    const sources = profileData.additionalInformation.jsSources;
    Assert.ok(!!sources, "Additional info should contain jsSources");
    Assert.equal(typeof sources, "object", "jsSources should be an object");

    // Without the jssources feature, all source data is unavailable and must be
    // omitted entirely.
    Assert.equal(
      Object.keys(sources).length,
      0,
      "jsSources should be empty when the jssources feature is not enabled"
    );
  });
});
