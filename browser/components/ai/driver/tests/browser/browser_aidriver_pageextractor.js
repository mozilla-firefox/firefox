/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/** @type {import("../../../../../../toolkit/components/ml/tests/MLTestUtils.sys.mjs")} */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

const { handleMessage } = ChromeUtils.importESModule(
  "moz-src:///browser/components/ai/driver/BrowserAIDriver.sys.mjs"
);

add_task(async function test_get_page_text() {
  const { html } = MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { cleanup } = await html`<div>This is the driven page.</div>`;

  const result = await handleMessage("get_page_text", {});
  is(result.text, "This is the driven page.", "The page text is extracted.");

  await cleanup();
});

add_task(async function test_get_page_metadata() {
  const { html } = MLTestUtils.serveHTMLInTab({ browser: gBrowser });
  const { cleanup } = await html`
    <script type="application/ld+json">
      { "@context": "https://schema.org", "@type": "Article" }
    </script>
    <div>Some article text.</div>
  `;

  const metadata = await handleMessage("get_page_metadata");
  Assert.deepEqual(
    metadata.structuredDataTypes,
    ["Article"],
    "The JSON-LD @type is surfaced in the metadata."
  );
  Assert.greater(metadata.wordCount, 0, "A word count is reported.");

  await cleanup();
});

add_task(async function test_get_headless_page_text() {
  const { html } = MLTestUtils.serveHTML();
  const { url, cleanup } = html`<div>This is a headless document.</div>`;

  const result = await handleMessage("get_headless_page_text", url, {});
  is(
    result.text,
    "This is a headless document.",
    "The headless page text is extracted."
  );

  await cleanup();
});

add_task(async function test_unknown_command() {
  await Assert.rejects(
    handleMessage("bogus"),
    /Unknown message: bogus/,
    "An unknown command rejects."
  );
});

add_task(async function test_describe() {
  const described = await handleMessage("describe");

  Assert.greater(described.apiVersion, 0, "The API version is reported.");
  Assert.ok(
    described.commands.includes("get_page_text"),
    "A toolkit command is listed."
  );
  Assert.ok(
    described.commands.includes("init_conversation"),
    "A browser-only command is listed."
  );
  Assert.ok(
    described.commands.includes("describe"),
    "describe lists itself, so clients can probe for it."
  );
});

add_task(async function test_init_conversation() {
  const first = await handleMessage("init_conversation");
  ok(first.ok, "The command reports success.");
  ok(first.conversationId, "A conversation id is returned.");

  const second = await handleMessage("init_conversation");
  Assert.notEqual(
    second.conversationId,
    first.conversationId,
    "A second call starts a distinct conversation."
  );
});
