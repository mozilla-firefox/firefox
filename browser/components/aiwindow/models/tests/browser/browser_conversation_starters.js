/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { generateConversationStartersSidebar } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/ConversationSuggestions.sys.mjs"
);
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

add_task(async function test_convo_starter_generation_with_mock_engine() {
  const mockEngineManager = new MockEngineManager();

  try {
    const tabs = [
      {
        url: "https://example.com",
        title: "Example Domain",
      },
    ];
    const generated = generateConversationStartersSidebar(tabs, 2, true);
    await mockEngineManager.respondTo({
      purpose: "convo-starters-sidebar",
      response: "suggestion 1\nsuggestion 2",
    });
    Assert.deepEqual(
      await generated,
      [
        { text: "suggestion 1", type: "chat" },
        { text: "suggestion 2", type: "chat" },
      ],
      "Should return two suggestions"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});
