/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * The assistant chat bubble uses --ai-background-color as its background so a
 * blur effect masks content overflow at the edges of ai-chat-content. This
 * test verifies the token propagates from the theme manifest into the chat
 * content document and cascades down to <ai-chat-content>.
 * It also checks if it has the same value as the ai window bg color: --lwt-accent-color
 */
add_task(async function test_ai_background_token_exists_on_chat_content() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  const aiWindowEl = browser.contentDocument?.querySelector("ai-window");
  const aichatBrowser = await TestUtils.waitForCondition(
    () => aiWindowEl.shadowRoot?.querySelector("#aichat-browser"),
    "Wait for aichat-browser"
  );
  if (aichatBrowser.currentURI?.spec !== "about:aichatcontent") {
    await BrowserTestUtils.browserLoaded(aichatBrowser);
  }

  // --lwt-accent-color gets the ai window background color from the manifest
  const lwtAccentColor = win
    .getComputedStyle(win.document.documentElement)
    .getPropertyValue("--lwt-accent-color");

  await SpecialPowers.spawn(
    aichatBrowser,
    [lwtAccentColor],
    async expectedAccent => {
      await ContentTaskUtils.waitForCondition(
        () => content.document.querySelector("ai-chat-content"),
        "Wait for ai-chat-content element"
      );
      const chatContent = content.document.querySelector("ai-chat-content");
      Assert.ok(chatContent, "ai-chat-content should exist");

      const root = content.document.documentElement;
      await ContentTaskUtils.waitForCondition(
        () =>
          content.window
            .getComputedStyle(root)
            .getPropertyValue("--ai-background-color") !== "",
        "Wait for --ai-background-color to be set on the root element"
      );

      const bgColor = content.window
        .getComputedStyle(root)
        .getPropertyValue("--ai-background-color");

      Assert.ok(
        bgColor,
        `--ai-background-color should be set on the root element (got "${bgColor}")`
      );

      // --ai-background-color comes from theme.properties as a hex code string
      // while --lwt-accent-color is normalized to rgb(...) by the theme consumer
      // we normalize both through computed style before comparing.
      const normalizeColor = cssColor => {
        const probe = content.document.createElement("div");
        probe.style.color = cssColor;
        content.document.body.appendChild(probe);
        const computed = content.window.getComputedStyle(probe).color;
        probe.remove();
        return computed;
      };

      Assert.equal(
        normalizeColor(bgColor),
        normalizeColor(expectedAccent),
        `--ai-background-color should be the same as --lwt-accent-color`
      );

      const chatContentBgColor = content.window
        .getComputedStyle(chatContent)
        .getPropertyValue("--ai-background-color");

      Assert.equal(
        chatContentBgColor,
        bgColor,
        "--ai-background-color should cascade to ai-chat-content"
      );
    }
  );

  await BrowserTestUtils.closeWindow(win);
});
