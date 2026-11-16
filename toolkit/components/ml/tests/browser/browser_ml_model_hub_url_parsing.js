/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/** @type {import("../MLTestUtils.sys.mjs")} */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

/**
 * Test parsing of a well-formed full URL, including protocol and path.
 */
add_task(async function test_parseUrl_wellFormedFullUrl() {
  const hub = MLTestUtils.createMockModelHub();
  // Root URLs have a real subpath in practice (unlike a bare origin, whose
  // pathname normalizes to "/"), which parseUrl's absolute-URL branch
  // depends on to find where the model/revision/file segments start.
  const rootUrl = `${hub.rootUrl}/models`;
  const modelHub = new ModelHub({
    rootUrl,
    urlTemplate: "{model}/{revision}",
  });
  const url = `${rootUrl}/org1/model1/v1/file/path`;
  const result = modelHub.parseUrl(url);

  Assert.equal(
    result.model,
    "org1/model1",
    "Model should be parsed correctly."
  );
  Assert.equal(result.revision, "v1", "Revision should be parsed correctly.");
  Assert.equal(
    result.file,
    "file/path",
    "File path should be parsed correctly."
  );

  await hub.cleanup();
});

/**
 * Test parsing of well-formed URLs, starting with a slash.
 */
add_task(async function test_parseUrl_wellFormedRelativeUrl() {
  const examples = [
    {
      url: "/Xenova/bert-base-NER/resolve/main/onnx/model.onnx",
      model: "Xenova/bert-base-NER",
      revision: "main",
      file: "onnx/model.onnx",
      urlTemplate: "{model}/resolve/{revision}",
    },
    {
      url: "/org1/model1/v1/file/path",
      model: "org1/model1",
      revision: "v1",
      file: "file/path",
      urlTemplate: "{model}/{revision}",
    },
  ];

  const hub = MLTestUtils.createMockModelHub();

  for (const example of examples) {
    const modelHub = new ModelHub({
      rootUrl: hub.rootUrl,
      urlTemplate: example.urlTemplate,
    });
    const result = modelHub.parseUrl(example.url);

    Assert.equal(
      result.model,
      example.model,
      "Model should be parsed correctly."
    );
    Assert.equal(
      result.revision,
      example.revision,
      "Revision should be parsed correctly."
    );
    Assert.equal(
      result.file,
      example.file,
      "File path should be parsed correctly."
    );
  }

  await hub.cleanup();
});

/**
 * Ensures an error is thrown when the URL does not start with the expected root URL or a slash.
 */
add_task(async function test_parseUrl_invalidDomain() {
  const hub = MLTestUtils.createMockModelHub();
  const modelHub = new ModelHub({
    rootUrl: hub.rootUrl,
    urlTemplate: hub.urlTemplate,
  });
  const url = "https://example.com/org1/model1/resolve/v1/file/path";
  Assert.throws(
    () => modelHub.parseUrl(url),
    new RegExp(`Error: Invalid domain for model URL: ${url}`),
    `Should throw with ${url}`
  );

  await hub.cleanup();
});

/**
 * Tests the method's error handling when the URL format does not include the required segments.
 */
add_task(async function test_parseUrl_tooFewParts() {
  const hub = MLTestUtils.createMockModelHub();
  const modelHub = new ModelHub({
    rootUrl: hub.rootUrl,
    urlTemplate: hub.urlTemplate,
  });
  const url = "/org1/model1/resolve";
  Assert.throws(
    () => modelHub.parseUrl(url),
    new RegExp(`Error: Invalid model URL format: ${url}`),
    `Should throw with ${url}`
  );

  await hub.cleanup();
});
