/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_monitor_suggest.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_monitor_suggest.js",
  this
);

// The mock embedder finds a memory by the words it shares with the query: the
// first with USER_LINE alone, the second with ASSISTANT_LINE alone.
const RESTOCK_MEMORY = "Waits for a restock";
const SOLD_MEMORY = "Sold a bike right away";

/**
 * Presses the memories switch in the smartbar and waits for chat to take it.
 *
 * @param {MozBrowser} browser - The browser holding the ai-window
 */
async function pressMemoriesSwitch(browser) {
  const aiWindow = browser.contentDocument.querySelector("ai-window");
  const memoriesButton = aiWindow.shadowRoot.querySelector(
    "memories-icon-button"
  );
  await memoriesButton.updateComplete;
  const before = aiWindow.memoriesEnabled;
  memoriesButton.shadowRoot.querySelector("moz-button").click();
  // Chat takes the press after an async read, with no event to wait for
  await TestUtils.waitForCondition(
    () => aiWindow.memoriesEnabled !== before,
    "Wait for chat to take the press"
  );
}

add_task(async function test_chat_press_fills_the_card_from_the_chat() {
  const memoriesRetrieval = new MockMemoriesRetrieval();
  await memoriesRetrieval.seedMemories(
    [SHOES_MEMORY, RESTOCK_MEMORY, SOLD_MEMORY].map(
      (memory_summary, index) => ({ id: `memory-${index}`, memory_summary })
    )
  );

  try {
    await withSidebarChat(
      async ({ sidebarBrowser, page, mockEngineManager }) => {
        await chatTurn(sidebarBrowser, mockEngineManager);
        await sendInSmartbar(sidebarBrowser, WATCH_COMMAND);

        // Each content task presses, then waits for what the answer brings
        const failed = createCard(sidebarBrowser, {
          action: "press",
          failed: true,
        });
        let call = await captureSuggestionRequest(mockEngineManager);
        call.reject(new Error("429 status code (no body)"));
        let card = await failed;
        Assert.equal(
          card.error,
          card.labels.failed,
          "A failed call shows the error line"
        );
        Assert.ok(
          !card.busy && card.label === card.labels.first,
          "and the button goes back to its first label"
        );
        Assert.equal(card.name, "", "The card is left alone");

        let filled = createCard(sidebarBrowser, {
          action: "press",
          filledWith: SUGGESTION.title,
        });
        call = await captureSuggestionRequest(mockEngineManager);
        Assert.equal(
          promptBlock(call.user, "recent_chat"),
          CHAT_LINES,
          "The request carries what the user and the assistant said, and neither the command nor the card's own lead-in"
        );
        Assert.ok(
          call.user.includes(
            `- ${sanitizeUntrustedContent(PRODUCT_TITLE)} (${page.url})`
          ),
          "It names the page the chat sits next to"
        );
        Assert.ok(
          promptBlock(call.user, "page_text").includes(PRODUCT_PRICE),
          "and carries its text"
        );
        Assert.deepEqual(
          promptBlock(call.user, "memories").split("\n").sort(),
          [`- ${RESTOCK_MEMORY}`, `- ${SHOES_MEMORY}`],
          "With the memories switch left on, memories are found by the page title and by what the user said, not by what the assistant said"
        );
        Assert.strictEqual(
          promptBlock(call.user, "earlier_drafts"),
          null,
          "A failed press leaves no earlier draft"
        );
        call.respond(JSON.stringify(SUGGESTION));
        card = await filled;
        Assert.deepEqual(
          { title: card.name, condition: card.condition },
          SUGGESTION,
          "The answer fills the card in chat"
        );
        Assert.equal(
          card.label,
          card.labels.retry,
          "The button offers a retry"
        );
        Assert.ok(!card.error, "and the error is gone");

        await pressMemoriesSwitch(sidebarBrowser);
        const other = {
          title: "Trail Runner 2 price",
          condition: `The price drops below ${PRODUCT_PRICE}`,
        };
        filled = createCard(sidebarBrowser, {
          action: "press",
          filledWith: other.title,
        });
        call = await captureSuggestionRequest(mockEngineManager);
        Assert.equal(
          promptBlock(call.user, "earlier_drafts"),
          JSON.stringify(SUGGESTION),
          "The retry carries the draft the card was given before"
        );
        Assert.ok(
          !call.user.includes(SHOES_MEMORY) &&
            promptBlock(call.user, "memories") === null,
          "Switched off with the card already there: no memory is sent"
        );
        call.respond(JSON.stringify(other));
        card = await filled;
        Assert.deepEqual(
          { title: card.name, condition: card.condition },
          other,
          "A different answer replaces both fields"
        );
      }
    );
  } finally {
    await memoriesRetrieval.cleanupMocks();
  }
});

// A /watch command records no memories state on its message, so the card
// follows the switch as it stands when the button is pressed.
add_task(async function test_chat_memories_switched_off_before_the_command() {
  const memoriesRetrieval = new MockMemoriesRetrieval();
  await memoriesRetrieval.seedMemories([
    { id: "memory-0", memory_summary: SHOES_MEMORY },
  ]);

  try {
    await withSidebarChat(async ({ sidebarBrowser, mockEngineManager }) => {
      await pressMemoriesSwitch(sidebarBrowser);
      await sendInSmartbar(sidebarBrowser, WATCH_COMMAND);

      const filled = createCard(sidebarBrowser, {
        action: "press",
        filledWith: SUGGESTION.title,
      });
      const call = await captureSuggestionRequest(mockEngineManager);
      Assert.ok(
        !call.user.includes(SHOES_MEMORY) &&
          promptBlock(call.user, "memories") === null,
        "Switched off before /watch, the first message: no memory is sent"
      );
      Assert.ok(
        promptBlock(call.user, "page_text").includes(PRODUCT_PRICE),
        "The page is still sent"
      );
      call.respond(JSON.stringify(SUGGESTION));
      await filled;
    });
  } finally {
    await memoriesRetrieval.cleanupMocks();
  }
});
