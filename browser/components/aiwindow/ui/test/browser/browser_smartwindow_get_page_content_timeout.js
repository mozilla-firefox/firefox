/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * @type {import("../../../../../../toolkit/components/ml/tests/MLTestUtils.sys.mjs")}
 */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

/**
 * @type {import("../AIWindowTestUtils.sys.mjs")}
 */
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const { GET_PAGE_CONTENT } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);

const { MESSAGE_ROLE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatEnums.sys.mjs"
);

/**
 * @import { ChatConversation } from "../../modules/ChatConversation.sys.mjs"
 */

/**
 * A get_page_content read of a page that never loads must not hang the turn.
 * The load timeout is shortened so the read gives up within the test.
 */
add_task(async function test_get_page_content_page_never_loads() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.pageExtractor.headlessTimeoutMs", 500]],
  });
  const mockEngineManager = new MockEngineManager();
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();
  const { url, cleanup: stopServer } = MLTestUtils.serveStalledPage();

  try {
    await mockEngineManager.respondTo({
      purpose: "convo-starters-sidebar",
      response: "A suggested conversation starter.",
    });

    await typeInSmartbar(sidebarBrowser, "What does that page say?");
    await submitSmartbar(sidebarBrowser);

    /** @type {ChatConversation} */
    const conversation = await TestUtils.waitForCondition(
      () => AIWindow.getActiveConversation(win),
      "Conversation should be created when the first message is sent."
    );

    await mockEngineManager.respondTo({
      purpose: "chat",
      response: {
        text: "",
        tokens: null,
        isPrompt: false,
        toolCalls: [
          {
            id: "call_get_page_content_1",
            function: {
              name: GET_PAGE_CONTENT,
              arguments: JSON.stringify({ url_list: [url] }),
            },
          },
        ],
      },
    });

    const toolMessage = await TestUtils.waitForCondition(
      () =>
        conversation.messages.find(
          message =>
            message.role === MESSAGE_ROLE.TOOL &&
            message.content?.name === GET_PAGE_CONTENT
        ),
      "A tool result for get_page_content should be added to the conversation."
    );

    Assert.stringContains(
      toolMessage.content.body.join("\n"),
      "did not finish loading in time",
      "The model is told the read timed out rather than being left waiting."
    );

    // Nothing renders below unless the turn ran to completion.
    const answer = "That page did not respond, so I could not read it.";
    await mockEngineManager.respondTo({ purpose: "chat", response: answer });
    await mockEngineManager.respondTo({
      purpose: "title-generation",
      response: "Page read",
    });

    const rendered = await TestUtils.waitForCondition(async () => {
      const messages = await getSidebarChatMessages(sidebarBrowser);
      return messages.find(
        message => message.role === "assistant" && message.hasRendered
      );
    }, "The assistant should render a reply once the turn completes.");

    Assert.equal(
      rendered.message,
      answer,
      "The turn completes and the assistant answers from the failed read."
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await BrowserTestUtils.closeWindow(win);
    await stopServer();
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * A get_page_content read of a page that redirects off-site, the way bot
 * detection sends a request to a challenge page, tells the model the page
 * blocks automated access rather than reporting a generic timeout.
 */
add_task(async function test_get_page_content_page_redirected_to_challenge() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.pageExtractor.headlessTimeoutMs", 500]],
  });
  const mockEngineManager = new MockEngineManager();
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();
  const { url, cleanup: stopServer } = MLTestUtils.serveRedirect({
    to: "https://example.com/",
  });

  try {
    await mockEngineManager.respondTo({
      purpose: "convo-starters-sidebar",
      response: "A suggested conversation starter.",
    });

    await typeInSmartbar(sidebarBrowser, "What does that page say?");
    await submitSmartbar(sidebarBrowser);

    /** @type {ChatConversation} */
    const conversation = await TestUtils.waitForCondition(
      () => AIWindow.getActiveConversation(win),
      "Conversation should be created when the first message is sent."
    );

    await mockEngineManager.respondTo({
      purpose: "chat",
      response: {
        text: "",
        tokens: null,
        isPrompt: false,
        toolCalls: [
          {
            id: "call_get_page_content_1",
            function: {
              name: GET_PAGE_CONTENT,
              arguments: JSON.stringify({ url_list: [url] }),
            },
          },
        ],
      },
    });

    const toolMessage = await TestUtils.waitForCondition(
      () =>
        conversation.messages.find(
          message =>
            message.role === MESSAGE_ROLE.TOOL &&
            message.content?.name === GET_PAGE_CONTENT
        ),
      "A tool result for get_page_content should be added to the conversation."
    );

    Assert.equal(
      toolMessage.content.body.join("\n"),
      `The page at ${url} appears to block automated access (e.g. a bot-detection challenge), so its content is unavailable. Do not retry it.`,
      "The model is told the page blocks automated access, not that it timed out."
    );

    // Nothing renders below unless the turn ran to completion.
    const answer = "That page blocks automated access, so I could not read it.";
    await mockEngineManager.respondTo({ purpose: "chat", response: answer });
    await mockEngineManager.respondTo({
      purpose: "title-generation",
      response: "Page read",
    });

    const rendered = await TestUtils.waitForCondition(async () => {
      const messages = await getSidebarChatMessages(sidebarBrowser);
      return messages.find(
        message => message.role === "assistant" && message.hasRendered
      );
    }, "The assistant should render a reply once the turn completes.");

    Assert.equal(
      rendered.message,
      answer,
      "The turn completes and the assistant answers from the blocked read."
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await BrowserTestUtils.closeWindow(win);
    await stopServer();
    await SpecialPowers.popPrefEnv();
  }
});
