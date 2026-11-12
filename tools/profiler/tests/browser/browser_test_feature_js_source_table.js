/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test that the source table in the profile includes all necessary information.
 */
add_task(async function test_source_table_schema() {
  await ProfilerTestUtils.assertProfilerInactive();

  const url = BASE_URL_HTTPS + "simple.html";
  await BrowserTestUtils.withNewTab(url, async contentBrowser => {
    await ProfilerTestUtils.startProfiler({ features: ["js", "jssources"] });

    const contentPid = await SpecialPowers.spawn(
      contentBrowser,
      [],
      () => Services.appinfo.processID
    );

    // Execute some inline JavaScript to create sources.
    await SpecialPowers.spawn(contentBrowser, [], () => {
      content.window.eval(`
        function testFunction() {
          return 42;
        }
        testFunction();
      `);
    });

    const { contentProcess } = await stopProfilerNowAndGetThreads(contentPid);
    Assert.ok(
      !!contentProcess,
      "Full profile should include the child process"
    );

    // Check that the profile has a source table.
    Assert.ok(!!contentProcess.sources, "Profile should have a source table");

    // Check the schema.
    const { schema } = contentProcess.sources;
    Assert.ok(!!schema, "Source table should have a schema");
    Assert.strictEqual(typeof schema, "object", "Schema should be an object");

    // Verify the schema includes all necessary keys.
    Assert.ok(
      Object.keys(schema).includes("id"),
      "Schema should include 'id' key"
    );
    Assert.ok(
      Object.keys(schema).includes("filename"),
      "Schema should include 'filename' key"
    );
    Assert.ok(
      Object.keys(schema).includes("startLine"),
      "Schema should include 'startLine' key"
    );
    Assert.ok(
      Object.keys(schema).includes("startColumn"),
      "Schema should include 'startColumn' key"
    );

    info(`Source table schema keys: [${Object.keys(schema).join(", ")}]`);

    // Check that we have some source data.
    const { data } = contentProcess.sources;
    Assert.ok(!!data, "Source table should have data");
    Assert.ok(Array.isArray(data), "Data should be an array");
    Assert.greater(data.length, 0, "Should have at least one source");

    // Find the expected field indices from the schema.
    const idIndex = schema.id;
    const filenameIndex = schema.filename;
    const startLineIndex = schema.startLine;
    const startColumnIndex = schema.startColumn;

    // Verify each source entry has the correct structure.
    for (const source of data) {
      Assert.ok(Array.isArray(source), "Each source should be an array");
      Assert.greaterOrEqual(
        Object.keys(schema).length,
        source.length,
        "Source entry should be either the same length or lower due to omitted entries"
      );

      // Check id (should be a string).
      const id = source[idIndex];
      Assert.equal(typeof id, "string", "Id should be a string");

      // Check filename (can be null or a string).
      const filename = source[filenameIndex];
      Assert.ok(
        filename === null || typeof filename === "string",
        "Filename should be null or a string"
      );

      // Check startLine (should be a number).
      const startLine = source[startLineIndex];
      Assert.equal(typeof startLine, "number", "startLine should be a number");
      Assert.greaterOrEqual(startLine, 1, "startLine should be >= 1 (1-based)");

      // Check startColumn (should be a number).
      const startColumn = source[startColumnIndex];
      Assert.equal(
        typeof startColumn,
        "number",
        "startColumn should be a number"
      );
      Assert.greaterOrEqual(
        startColumn,
        1,
        "startColumn should be >= 1 (1-based)"
      );

      info(
        `Source: id=${id}, filename=${filename}, ` +
          `startLine=${startLine}, startColumn=${startColumn}`
      );
    }
  });
});

/**
 * Test that inline scripts have correct startLine and startColumn values.
 */
add_task(async function test_inline_script_start_positions() {
  await ProfilerTestUtils.assertProfilerInactive();

  await ProfilerTestUtils.startProfiler({
    features: ["stackwalk", "js", "jssources"],
  });
  const url = BASE_URL_HTTPS + "inline_script_test.html";
  await BrowserTestUtils.withNewTab(url, async contentBrowser => {
    const contentPid = await SpecialPowers.spawn(
      contentBrowser,
      [],
      () => Services.appinfo.processID
    );

    // Wait a bit for scripts to execute.
    await SpecialPowers.spawn(contentBrowser, [], () => {
      return new Promise(resolve => {
        content.window.setTimeout(resolve, 100);
      });
    });

    const { contentProcess } = await stopProfilerNowAndGetThreads(contentPid);
    Assert.ok(
      !!contentProcess,
      "Full profile should include the child process"
    );

    // Check source table.
    Assert.ok(!!contentProcess.sources, "Profile should have a source table");

    const { schema, data } = contentProcess.sources;
    const startLineIndex = schema.startLine;
    const startColumnIndex = schema.startColumn;
    const filenameIndex = schema.filename;

    // Look for inline script sources (they'll have inline_script_test.html as filename).
    const inlineSources = data.filter(source => {
      const filename = source[filenameIndex];
      return filename && filename.includes("inline_script_test.html");
    });

    Assert.greater(inlineSources.length, 0, "Should find inline sources");
    info(`Found ${inlineSources.length} inline script source(s)`);

    for (const source of inlineSources) {
      const startLine = source[startLineIndex];
      const startColumn = source[startColumnIndex];
      const filename = source[filenameIndex];

      info(
        `Inline source: filename=${filename}, ` +
          `startLine=${startLine}, startColumn=${startColumn}`
      );

      // For inline scripts, we expect specific line numbers.
      // Note: These are 1-based indices, so we expect them to be greater than 1.
      Assert.greater(startLine, 1, `startLine should be valid: ${startLine}`);
      Assert.greater(
        startColumn,
        1,
        `startColumn should be valid: ${startColumn}`
      );
    }
  });
});
