/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Pure rendering behavior, tested on a static page rather than the full Smart
// Window since no starter generation or chat is involved.
const PROMPTS_PAGE =
  getRootDirectory(gTestPath) + "test_smartwindow_prompts_page.html";

describe("smartwindow-prompts", () => {
  let tab;

  beforeEach(async () => {
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, PROMPTS_PAGE);
  });

  afterEach(() => {
    BrowserTestUtils.removeTab(tab);
  });

  // Clicks both pills in one render purely to check each event's shape.
  it("adds memory and content to a resume pill's selected event", async () => {
    const memory = { id: "memory-1", memory_summary: "Research project" };
    const content = {
      headline: "Pick up your research",
      status: "Continue reading",
      previewTabs: [
        { url: "https://example.com/1", title: "First tab" },
        { url: "https://example.com/2", title: "Second tab" },
      ],
    };
    const el = await createPromptsElement(tab, [
      { text: "Pick up your research", type: "resume", memory, content },
      { text: "Write a first draft", type: "chat" },
    ]);
    const details = [];
    el.addEventListener("SmartWindowPrompt:prompt-selected", event => {
      details.push(event.detail);
    });

    const buttons = el.shadowRoot.querySelectorAll(".sw-prompt-button");
    buttons[0].click();
    buttons[1].click();

    Assert.deepEqual(
      details,
      [
        { text: "Pick up your research", type: "resume", memory, content },
        { text: "Write a first draft", type: "chat" },
      ],
      "Resume prompt events include their memory and content, chat ones stay plain"
    );
  });

  describe("favicon cluster", () => {
    it("is not rendered without preview icons", async () => {
      const el = await createPromptsElement(tab, [
        { text: "Write a first draft", type: "chat" },
      ]);

      Assert.ok(
        !el.shadowRoot.querySelector(".sw-prompt-favicons"),
        "No favicon cluster should render without previewIcons"
      );
    });

    it("renders every icon up to the max", async () => {
      const el = await createPromptsElement(tab, [
        {
          text: "Pick up your Tokyo trip",
          type: "resume",
          previewIcons: previewIcons(2),
        },
      ]);

      Assert.equal(
        el.shadowRoot.querySelectorAll(".sw-prompt-favicon").length,
        2,
        "Should render one icon per previewIcons entry when under the max"
      );
      Assert.ok(
        !el.shadowRoot.querySelector(".sw-prompt-favicon-overflow"),
        "No overflow badge should render when under the max"
      );
    });

    it("shows an overflow badge beyond the max", async () => {
      const el = await createPromptsElement(tab, [
        {
          text: "Continue shopping",
          type: "resume",
          previewIcons: previewIcons(4),
        },
      ]);

      Assert.equal(
        el.shadowRoot.querySelectorAll(".sw-prompt-favicon").length,
        2,
        "Should cap visible icons when previewIcons exceeds the max"
      );
      Assert.equal(
        el.shadowRoot.querySelector(".sw-prompt-favicon-overflow")?.textContent,
        "+2",
        "Overflow badge should show the count of hidden icons"
      );
    });
  });

  describe("scroll arrows", () => {
    it("are not rendered when the pills fit", async () => {
      const el = await createPromptsElement(tab, [
        { text: "Write a first draft", type: "chat" },
        { text: "Brainstorm ideas", type: "chat" },
      ]);

      Assert.ok(
        !el.shadowRoot.querySelector(".sw-prompts-scroll-button"),
        "No scroll arrows should render when content fits"
      );
    });

    it("start with only the end arrow when the pills overflow", async () => {
      const el = await createPromptsElement(tab, buildManyPrompts());

      await forceOverflowByShrinking(el);

      Assert.ok(
        el.shadowRoot.querySelector(".sw-prompts-scroll-end"),
        "End arrow should render"
      );
      Assert.ok(
        !el.shadowRoot.querySelector(".sw-prompts-scroll-start"),
        "Start arrow should not render at the very start"
      );
    });

    it("go away once the element is widened to fit", async () => {
      const el = await createPromptsElement(tab, buildManyPrompts());
      await forceOverflowByShrinking(el);

      el.style.width = "10000px";

      await TestUtils.waitForCondition(
        () => !el.canScrollEnd,
        "Wait for the resize observer to detect the content now fits"
      );
      Assert.ok(
        !el.shadowRoot.querySelector(".sw-prompts-scroll-button"),
        "No scroll arrows should render once widened to fit"
      );
    });

    it("follow overflow on a container recreated after the prompts emptied", async () => {
      const el = await createPromptsElement(tab, buildManyPrompts());
      const contentWin = tab.linkedBrowser.contentWindow;
      // render() omits the container while prompts is empty, detaching it.
      // Let the resulting disconnect-triggered resize notification settle
      // before continuing, so it can't mask a stale observer later.
      el.prompts = [];
      await el.updateComplete;
      await new Promise(resolve =>
        contentWin.requestAnimationFrame(() =>
          contentWin.requestAnimationFrame(resolve)
        )
      );
      // A new container is created here; the observer must re-attach to it.
      el.prompts = buildManyPrompts();
      await el.updateComplete;
      Assert.ok(
        !el.canScrollEnd,
        "Sanity check: the new container isn't overflowing yet"
      );

      await forceOverflowByShrinking(el);

      Assert.ok(
        el.shadowRoot.querySelector(".sw-prompts-scroll-end"),
        "Resize observer should still detect overflow on the recreated container"
      );
    });

    it("scroll forward from the end arrow in RTL", async () => {
      const el = await createPromptsElement(tab, buildManyPrompts());
      el.setAttribute("dir", "rtl");
      await forceOverflowByShrinking(el);
      Assert.ok(
        el.shadowRoot.querySelector(".sw-prompts-scroll-end"),
        "End arrow should render in RTL too"
      );

      el.shadowRoot.querySelector(".sw-prompts-scroll-end").click();

      await TestUtils.waitForCondition(
        () => el.canScrollStart,
        "Wait for the scroll to advance past the first pill in RTL"
      );
    });

    // Reduced motion turns off scroll-behavior: smooth, so each click settles
    // immediately instead of racing an in-flight animation.
    describe("with reduced motion", () => {
      beforeEach(async () => {
        await SpecialPowers.pushPrefEnv({
          set: [["ui.prefersReducedMotion", 1]],
        });
      });

      afterEach(async () => {
        await SpecialPowers.popPrefEnv();
      });

      it("reveal the start arrow once the end arrow scrolls forward", async () => {
        const el = await createPromptsElement(tab, buildManyPrompts());
        await forceOverflowByShrinking(el);

        el.shadowRoot.querySelector(".sw-prompts-scroll-end").click();

        await TestUtils.waitForCondition(
          () => el.canScrollStart,
          "Wait for the scroll to advance past the first pill"
        );
        Assert.ok(
          el.shadowRoot.querySelector(".sw-prompts-scroll-start"),
          "Start arrow should render once scrolled forward"
        );
      });

      it("stop at the last pill without wrapping", async () => {
        const prompts = buildManyPrompts();
        const el = await createPromptsElement(tab, prompts);
        await forceOverflowByShrinking(el);

        // More clicks than there are pills - should hold at the last pill
        // rather than wrapping back around to the first.
        for (let i = 0; i < prompts.length + 2; i++) {
          const endButton = el.shadowRoot.querySelector(
            ".sw-prompts-scroll-end"
          );
          if (!endButton) {
            break;
          }
          endButton.click();
          await TestUtils.waitForTick();
        }

        await TestUtils.waitForCondition(
          () => !el.canScrollEnd,
          "Wait for the last pill to settle into view"
        );
        Assert.ok(
          el.canScrollStart,
          "Should still be able to scroll back from the end"
        );
      });
    });
  });
});

/**
 * Creates a smartwindow-prompts element directly on the static test page.
 *
 * @param {MozTabbrowserTab} tab
 * @param {Array<object>} prompts
 * @returns {Promise<HTMLElement>}
 */
async function createPromptsElement(tab, prompts) {
  const contentWin = tab.linkedBrowser.contentWindow;
  await contentWin.customElements.whenDefined("smartwindow-prompts");
  const el = contentWin.document.createElement("smartwindow-prompts");
  el.mode = "fullpage";
  el.prompts = prompts;
  contentWin.document.body.appendChild(el);
  await el.updateComplete;
  return el;
}

function previewIcons(count) {
  return Array.from({ length: count }, (_, i) => ({
    iconSrc: `page-icon:https://example.com/${i + 1}`,
  }));
}

function buildManyPrompts(count = 10) {
  return Array.from({ length: count }, (_, i) => ({
    text: `Overflow test prompt number ${i}`,
    type: "chat",
  }));
}

/**
 * Shrinks the element to a fixed width narrow enough to force overflow,
 * and waits for the resulting (ResizeObserver-driven, async) scroll-state
 * update, rather than asserting immediately after the style change.
 *
 * @param {HTMLElement} el
 */
async function forceOverflowByShrinking(el) {
  el.style.cssText = "display: block; width: 250px;";
  await TestUtils.waitForCondition(
    () => el.canScrollEnd,
    "Wait for the resize observer to detect overflow"
  );
}
