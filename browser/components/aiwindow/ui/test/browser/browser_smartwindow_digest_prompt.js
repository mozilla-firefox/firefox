/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { DigestManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestManager.sys.mjs"
);
const { DigestStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestStore.sys.mjs"
);
const {
  DIGEST_HIGHLIGHT_CLEARED,
  DigestActivity,
  _clearDigestHighlightsForTesting,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/DigestActivity.sys.mjs"
);
const { DIGEST_STATUS, DIGEST_SCHEDULE_TYPE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestConstants.sys.mjs"
);

const PROMPTS_PAGE =
  "chrome://mochitests/content/browser/browser/components/aiwindow/ui/test/browser/test_smartwindow_prompts_page.html";

const DIGEST_STORE_PATH = PathUtils.join(
  PathUtils.profileDir,
  "memories",
  "digests.json.lz4"
);

async function resetDigestStore() {
  await DigestStore.testOnlyFlushAndResetCache();
  await IOUtils.remove(DIGEST_STORE_PATH, { ignoreAbsent: true });
}

/**
 * @param {Array<object>} prompts
 * @returns {Promise<{tab: MozTabbrowserTab, el: HTMLElement}>}
 */
async function openPromptsPage(prompts) {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PROMPTS_PAGE
  );
  const doc = tab.linkedBrowser.contentDocument;
  await tab.linkedBrowser.contentWindow.customElements.whenDefined(
    "smartwindow-prompts"
  );
  const el = doc.createElement("smartwindow-prompts");
  el.mode = "fullpage";
  el.prompts = prompts;
  doc.body.appendChild(el);
  await el.updateComplete;
  const digestEl = el.shadowRoot.querySelector("smartwindow-digest-prompt");
  await digestEl?.updateComplete;
  return { tab, el };
}

function isHighlighted(digestEl) {
  return digestEl
    .querySelector(".sw-prompt-button")
    .classList.contains("sw-digest-highlighted");
}

function digestPrompt(overrides = {}) {
  return {
    title: "Morning News",
    type: "digest",
    id: "catch-up",
    highlightExpiresAt: 0,
    ...overrides,
  };
}

/**
 * Opens the test page with a single digest pill in a prompts row.
 *
 * @param {object} [overrides] - Overrides for the digest prompt
 * @returns {Promise<{tab: MozTabbrowserTab, el: HTMLElement, digestEl: HTMLElement}>}
 */
async function renderDigestPrompt(overrides) {
  const { tab, el } = await openPromptsPage([digestPrompt(overrides)]);
  const digestEl = el.shadowRoot.querySelector("smartwindow-digest-prompt");
  return { tab, el, digestEl };
}

/**
 * Hovers a digest pill to reveal its dismiss button, then clicks it.
 *
 * @param {MozTabbrowserTab} tab
 * @param {HTMLElement} digestEl
 */
async function clickDismissButton(tab, digestEl) {
  const win = tab.linkedBrowser.contentWindow;
  const dismiss = digestEl.querySelector(".sw-prompt-dismiss");
  EventUtils.synthesizeMouseAtCenter(digestEl, { type: "mousemove" }, win);
  await TestUtils.waitForCondition(
    () =>
      win.getComputedStyle(dismiss).opacity === "1" &&
      dismiss.getBoundingClientRect().width,
    "The dismiss button should show on hover"
  );
  EventUtils.synthesizeMouseAtCenter(dismiss, {}, win);
}

describe("smartwindow-digest-prompt", () => {
  let tab, el, digestEl;

  afterEach(() => {
    if (tab) {
      BrowserTestUtils.removeTab(tab);
    }
    tab = el = digestEl = null;
    _clearDigestHighlightsForTesting();
  });

  it("should render as a prompt button in the row", async () => {
    ({ tab, el, digestEl } = await renderDigestPrompt());

    Assert.ok(
      el.shadowRoot.querySelector(".sw-prompt-button"),
      "The digest pill should be found as a prompt button in the row"
    );
  });

  it("should show the digest's title as the label", async () => {
    ({ tab, el, digestEl } = await renderDigestPrompt());

    Assert.equal(
      digestEl.querySelector(".sw-prompt-button").textContent.trim(),
      "Morning News",
      "The pill label should be the digest's title"
    );
  });

  it("should truncate a long title with an ellipsis", async () => {
    ({ tab, el, digestEl } = await renderDigestPrompt({
      title: "A very long digest title ".repeat(10),
    }));
    const win = tab.linkedBrowser.contentWindow;
    const button = digestEl.querySelector(".sw-prompt-button");
    const label = button.shadowRoot.querySelector("label");

    Assert.equal(
      win.getComputedStyle(button).whiteSpace,
      "nowrap",
      "The title should stay on one line"
    );
    Assert.equal(
      win.getComputedStyle(label).textOverflow,
      "ellipsis",
      "An overflowing title should end in an ellipsis"
    );
  });

  it("should show the dismiss button only on hover", async () => {
    ({ tab, el, digestEl } = await renderDigestPrompt());
    const win = tab.linkedBrowser.contentWindow;
    const dismiss = digestEl.querySelector(".sw-prompt-dismiss");

    // Start with the pointer away from the pill.
    EventUtils.synthesizeMouseAtPoint(1, 1, { type: "mousemove" }, win);
    await TestUtils.waitForCondition(
      () => win.getComputedStyle(dismiss).opacity === "0",
      "Wait for the dismiss button to be hidden"
    );
    Assert.equal(
      win.getComputedStyle(dismiss).pointerEvents,
      "none",
      "The dismiss button should not be clickable before hover"
    );

    EventUtils.synthesizeMouseAtCenter(digestEl, { type: "mousemove" }, win);
    await TestUtils.waitForCondition(
      () => win.getComputedStyle(dismiss).opacity === "1",
      "Wait for the dismiss button to show on hover"
    );
    Assert.equal(
      win.getComputedStyle(dismiss).pointerEvents,
      "auto",
      "The dismiss button should be clickable on hover"
    );
  });

  describe("highlight", () => {
    it("should be highlighted before its expiry", async () => {
      ({ tab, el, digestEl } = await renderDigestPrompt({
        highlightExpiresAt: Date.now() + 60_000,
      }));

      Assert.ok(isHighlighted(digestEl), "The pill should be highlighted");
    });

    it("should not be highlighted without an expiry", async () => {
      ({ tab, el, digestEl } = await renderDigestPrompt());

      Assert.ok(!isHighlighted(digestEl), "The pill should not be highlighted");
    });

    it("should end the highlight once its expiry passes", async () => {
      // Start with an expiry no machine can reach before the check, then
      // bring it close, as the page does when the highlight's end changes.
      ({ tab, el, digestEl } = await renderDigestPrompt({
        highlightExpiresAt: Date.now() + 60_000,
      }));
      Assert.ok(isHighlighted(digestEl), "The pill should start highlighted");

      el.prompts = [digestPrompt({ highlightExpiresAt: Date.now() + 100 })];
      await el.updateComplete;
      digestEl = el.shadowRoot.querySelector("smartwindow-digest-prompt");
      await TestUtils.waitForCondition(
        () => !isHighlighted(digestEl),
        "The highlight should end once its expiry passes"
      );
    });
  });

  describe("when its tab closes", () => {
    const observerCount = () =>
      [...Services.obs.enumerateObservers(DIGEST_HIGHLIGHT_CLEARED)].length;

    it("should stop observing highlight changes", async () => {
      const before = observerCount();
      ({ tab, el, digestEl } = await renderDigestPrompt());
      Assert.equal(observerCount(), before + 1, "The pill starts observing");

      const unloaded = BrowserTestUtils.waitForEvent(
        tab.linkedBrowser.contentWindow,
        "unload"
      );
      BrowserTestUtils.removeTab(tab);
      tab = null;
      await unloaded;
      Assert.equal(
        observerCount(),
        before,
        "The pill stops observing once its tab is gone"
      );
    });
  });

  describe("when clicked", () => {
    beforeEach(async () => {
      ({ tab, el, digestEl } = await renderDigestPrompt({
        highlightExpiresAt: Date.now() + 60_000,
      }));
    });

    it("should send the digest's id", async () => {
      const selected = BrowserTestUtils.waitForEvent(
        el,
        "SmartWindowPrompt:prompt-selected"
      );
      EventUtils.synthesizeMouseAtCenter(
        digestEl.querySelector(".sw-prompt-button"),
        {},
        tab.linkedBrowser.contentWindow
      );
      const { detail } = await selected;

      Assert.deepEqual(
        { ...detail },
        {
          text: "Morning News",
          type: "digest",
          id: "catch-up",
        },
        "The click should send the digest's id"
      );
    });

    it("should end the highlight", async () => {
      EventUtils.synthesizeMouseAtCenter(
        digestEl.querySelector(".sw-prompt-button"),
        {},
        tab.linkedBrowser.contentWindow
      );
      await digestEl.updateComplete;

      Assert.ok(!isHighlighted(digestEl), "Clicking should end the highlight");
    });

    it("should end the highlight on the same digest's pill in other tabs", async () => {
      const other = await renderDigestPrompt({
        highlightExpiresAt: Date.now() + 60_000,
      });
      await BrowserTestUtils.switchTab(gBrowser, tab);
      try {
        Assert.ok(
          isHighlighted(other.digestEl),
          "The other tab's pill should start highlighted"
        );

        EventUtils.synthesizeMouseAtCenter(
          digestEl.querySelector(".sw-prompt-button"),
          {},
          tab.linkedBrowser.contentWindow
        );
        await TestUtils.waitForCondition(
          () => !isHighlighted(other.digestEl),
          "Clicking should end the highlight in the other tab"
        );
      } finally {
        BrowserTestUtils.removeTab(other.tab);
      }
    });
  });

  describe("DigestActivity", () => {
    it("should highlight a digest created this session for 20 minutes", () => {
      const created_at = Date.now();

      Assert.equal(
        DigestActivity.getHighlightExpiresAt({ id: "new", created_at }),
        created_at + 20 * 60 * 1000,
        "A new digest's highlight should end 20 minutes after creation"
      );
    });

    it("should not highlight a digest created before this session", () => {
      const sessionStart = Services.startup.getStartupInfo().process.getTime();

      Assert.equal(
        DigestActivity.getHighlightExpiresAt({
          id: "old",
          created_at: sessionStart - 1,
        }),
        0,
        "A digest from a previous session should not be highlighted"
      );
    });

    it("should not highlight a digest once cleared", () => {
      DigestActivity.clearHighlight("cleared");

      Assert.equal(
        DigestActivity.getHighlightExpiresAt({
          id: "cleared",
          created_at: Date.now(),
        }),
        0,
        "A cleared digest should not be highlighted"
      );
    });
  });

  describe("when dismissed", () => {
    let digest;

    beforeEach(async () => {
      await resetDigestStore();
      digest = await DigestManager.createDigest({
        memory_id: "mem.test",
        title: "Morning News",
        description: "News from this morning.",
        schedule: { type: DIGEST_SCHEDULE_TYPE.ON_DEMAND },
      });
      ({ tab, el, digestEl } = await renderDigestPrompt({ id: digest.id }));
    });

    afterEach(async () => {
      await resetDigestStore();
    });

    it("should fire prompt-dismissed without selecting the prompt", async () => {
      let selectedFired = false;
      el.addEventListener("SmartWindowPrompt:prompt-selected", () => {
        selectedFired = true;
      });
      const dismissed = BrowserTestUtils.waitForEvent(
        el,
        "SmartWindowPrompt:prompt-dismissed"
      );

      await clickDismissButton(tab, digestEl);
      const { detail } = await dismissed;

      Assert.deepEqual(
        { ...detail },
        { type: "digest", id: digest.id },
        "The dismiss event should carry the digest type and id"
      );
      Assert.ok(!selectedFired, "Dismissing should not also select the prompt");
    });

    it("should disable the digest", async () => {
      await clickDismissButton(tab, digestEl);

      await TestUtils.waitForCondition(
        async () =>
          (await DigestManager.getDigestById(digest.id)).status ===
          DIGEST_STATUS.DISABLED,
        "Dismissing a digest pill should disable the digest"
      );
    });
  });

  describe("when dismissed in a Smart Window", () => {
    let windows = [];

    /**
     * Opens a Smart Window with a digest pill first in its starter row.
     *
     * @param {string} id - The digest's ID
     * @returns {Promise<{win: Window, promptsEl: HTMLElement}>}
     */
    async function openWindowWithDigestPill(id) {
      const win = await openAIWindow();
      windows.push(win);
      const aiWindow =
        win.gBrowser.selectedBrowser.contentDocument.querySelector("ai-window");
      const promptsEl = await TestUtils.waitForCondition(
        () =>
          aiWindow.startersResolved &&
          aiWindow.shadowRoot.querySelector("smartwindow-prompts"),
        "Wait for the starter row"
      );

      // Update this when prompt is integrated into ai-window
      const starters = [digestPrompt({ id }), ...promptsEl.prompts];
      aiWindow.conversation.transientStarters = starters;
      promptsEl.prompts = starters;
      await promptsEl.updateComplete;
      const pill = promptsEl.shadowRoot.querySelector(
        "smartwindow-digest-prompt"
      );
      await pill.updateComplete;
      Assert.ok(pill, "The digest pill should be in the row");
      return { win, promptsEl };
    }

    /**
     * @param {Window} win
     * @param {HTMLElement} promptsEl
     */
    async function dismissDigestPill(win, promptsEl) {
      const winContent = win.gBrowser.selectedBrowser.contentWindow;
      const pill = promptsEl.shadowRoot.querySelector(
        "smartwindow-digest-prompt"
      );
      const dismiss = pill.querySelector(".sw-prompt-dismiss");
      EventUtils.synthesizeMouseAtCenter(
        pill,
        { type: "mousemove" },
        winContent
      );
      await TestUtils.waitForCondition(
        () => winContent.getComputedStyle(dismiss).opacity === "1",
        "The dismiss button should show on hover"
      );
      EventUtils.synthesizeMouseAtCenter(dismiss, {}, winContent);
    }

    afterEach(async () => {
      for (const win of windows) {
        await BrowserTestUtils.closeWindow(win);
      }
      windows = [];
    });

    it("should remove the pill from the row", async () => {
      const { win, promptsEl } =
        await openWindowWithDigestPill("dismiss-in-window");

      await dismissDigestPill(win, promptsEl);

      await TestUtils.waitForCondition(
        () => !promptsEl.shadowRoot.querySelector("smartwindow-digest-prompt"),
        "Dismissing should remove the digest pill from the row"
      );
      Assert.ok(
        promptsEl.shadowRoot.querySelectorAll(".sw-prompt-button").length,
        "The other starters should stay in the row"
      );
    });

    it("should remove the pill from other windows", async () => {
      const first = await openWindowWithDigestPill("dismiss-everywhere");
      const second = await openWindowWithDigestPill("dismiss-everywhere");
      await SimpleTest.promiseFocus(first.win);

      await dismissDigestPill(first.win, first.promptsEl);

      await TestUtils.waitForCondition(
        () =>
          !second.promptsEl.shadowRoot.querySelector(
            "smartwindow-digest-prompt"
          ),
        "Dismissing should remove the digest pill from the other window"
      );
    });
  });
});
