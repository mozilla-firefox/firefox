/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that the get_page_content tool call can extract content from pages.
 */
add_task(async function test_get_page_content_basic() {
  const html = `
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset="utf-8">
      <title>Test Page</title>
    </head>
    <body>
      <article>
        <h1>Sample Article Title</h1>
        <p>This is the first paragraph with some sample content.</p>
        <p>This is the second paragraph with additional information.</p>
      </article>
    </body>
    </html>
  `;

  const { ChatConversation } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
  );

  const { url_list, GetPageContent, cleanup } =
    await setupGetPageContentTests(html);

  // Manually set the ai-window attribute for testing
  // (in production this is set via window features when opening the window)
  window.document.documentElement.setAttribute("ai-window", "true");

  // Verify we're in an AI Window
  const { AIWindow } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs"
  );
  info("Is AI Window: " + AIWindow.isAIWindowActive(window));
  info(
    "Window has ai-window attribute: " +
      window.document.documentElement.hasAttribute("ai-window")
  );

  const conversation = new ChatConversation({
    title: "",
    description: "",
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });

  // Call the tool with the URL
  const result_array = await GetPageContent.getPageContent(
    { url_list },
    conversation
  );
  const result = result_array[0].content;

  info("Extraction result: " + JSON.stringify(result_array[0]));

  ok(result_array[0].ok, "The read succeeded");
  ok(result.includes("Sample Article Title"), "Text should contain the title");
  ok(
    result.includes("first paragraph"),
    "Text should contain text from the first paragraph"
  );
  ok(
    result.includes("second paragraph"),
    "Text should contain text from the second paragraph"
  );
  ok(
    result.startsWith("Content from"),
    "Text should start with content prefix"
  );

  await cleanup();
});

/**
 * Test that get_page_content keeps the URL of an anchor that wraps block
 * content, such as a card on a listing page.
 */
add_task(async function test_get_page_content_block_anchor_links() {
  const html = `
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset="utf-8">
      <title>Hotels</title>
    </head>
    <body>
      <h1>Hotels in Lisbon</h1>
      <a href="https://example.com/hotel-a">
        <div>
          <h2>Hotel A</h2>
          <p>Scored 8.9</p>
        </div>
      </a>
      <a href="https://example.com/hotel-b">
        <div>
          <h2>Hotel B</h2>
          <p>Scored 9.1</p>
        </div>
      </a>
    </body>
    </html>
  `;

  const { ChatConversation } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
  );

  const { url_list, GetPageContent, cleanup } =
    await setupGetPageContentTests(html);

  const conversation = new ChatConversation({
    title: "",
    description: "",
    pageUrl: new URL("https://example.com"),
    pageMeta: {},
  });

  const [result] = await GetPageContent.getPageContent(
    { url_list },
    conversation
  );

  ok(result.ok, "The read succeeded");
  ok(
    result.content.includes("[Hotel A](https://example.com/hotel-a)"),
    `The first card keeps its URL: ${result.content}`
  );
  ok(
    result.content.includes("[Hotel B](https://example.com/hotel-b)"),
    `The second card keeps its URL: ${result.content}`
  );

  await cleanup();
});
