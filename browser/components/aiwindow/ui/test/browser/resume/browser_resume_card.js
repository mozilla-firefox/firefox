/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const CARD_MODULE_URL =
  "chrome://browser/content/aiwindow/components/smartwindow-resume-card.mjs";

const SAMPLE_CONTENT = {
  headline: "Team offsite planning",
  status: "Reviewed venue quotes and the calendar poll for next week.",
  previewTabs: [{ url: "https://a.example/" }, { url: "https://b.example/" }],
};

let win;

add_setup(async function () {
  win = await openSharedAIWindow();
});

describe("Resume card", () => {
  let doc;

  beforeEach(async () => {
    doc = (await openOnlyAIWindowTab(win)).ownerDocument;
  });

  describe("rendering", () => {
    it("shows the headline, status and one favicon per preview tab", async () => {
      const card = await createResumeCard(doc, { content: SAMPLE_CONTENT });

      const shadow = card.shadowRoot;
      Assert.equal(
        shadow.querySelector(".resume-card-title").textContent,
        SAMPLE_CONTENT.headline,
        "Title should match content.headline"
      );
      Assert.equal(
        shadow.querySelector(".resume-card-description").textContent,
        SAMPLE_CONTENT.status,
        "Description should match content.status"
      );
      Assert.equal(
        shadow.querySelectorAll(".resume-card-favicon").length,
        SAMPLE_CONTENT.previewTabs.length,
        "Should render one favicon per preview tab when under the visible max"
      );
    });

    it("caps the favicons at three however many tabs there are", async () => {
      const card = await createResumeCard(doc, {
        content: {
          ...SAMPLE_CONTENT,
          previewTabs: Array.from({ length: 5 }, (_, i) => ({
            url: `https://site${i}.example/`,
          })),
        },
      });

      Assert.equal(
        card.shadowRoot.querySelectorAll(".resume-card-favicon").length,
        3,
        "Should cap at MAX_VISIBLE_FAVICONS favicons regardless of tab count"
      );
    });
  });

  describe("buttons", () => {
    let card;

    beforeEach(async () => {
      card = await createResumeCard(doc, {
        content: SAMPLE_CONTENT,
        journeyId: "memory-42",
      });
    });

    it("dispatches resume with the card's journeyId from the Resume button", async () => {
      const resumed = nextEventDetail(card, "smartwindow-resume-card:resume");

      card.shadowRoot.querySelector(".resume-card-resume-button").click();

      Assert.deepEqual(
        await resumed,
        { journeyId: "memory-42", source: "resume_button" },
        "Resume button should dispatch the card's journeyId"
      );
    });

    it("dispatches dismiss with the card's journeyId from the close button", async () => {
      const dismissed = nextEventDetail(
        card,
        "smartwindow-resume-card:dismiss"
      );

      card.shadowRoot.querySelector(".resume-card-dismiss").click();

      Assert.deepEqual(
        await dismissed,
        { journeyId: "memory-42" },
        "Dismiss button should dispatch the card's journeyId"
      );
    });

    // Each menu item must provide its own ID; native click detail has none.
    it("dispatches the picked menu item's id and closes the menu", async () => {
      // Open normally so panel-list registers its close-on-select listener.
      const panelList = await openMoreMenu(card);
      const menuSelected = nextEventDetail(
        card,
        "smartwindow-resume-card:menu-item-selected"
      );
      const hidden = BrowserTestUtils.waitForEvent(panelList, "hidden");

      card.shadowRoot.querySelectorAll("panel-item")[0].click();

      Assert.deepEqual(
        await menuSelected,
        { journeyId: "memory-42", itemId: "open_tabs" },
        "Clicking the first menu item should dispatch its itemId with the card's journeyId"
      );
      await hidden;
    });
  });

  describe("body click", () => {
    let card;
    let title;
    let resumes;

    beforeEach(async () => {
      card = await createResumeCard(doc, {
        content: SAMPLE_CONTENT,
        journeyId: "memory-42",
      });
      title = card.shadowRoot.querySelector(".resume-card-title");
      resumes = [];
      card.addEventListener("smartwindow-resume-card:resume", e =>
        resumes.push(e.detail.source)
      );
    });

    afterEach(() => {
      doc.getSelection().removeAllRanges();
    });

    it("resumes the card with source card", () => {
      title.click();

      Assert.deepEqual(
        resumes,
        ["card"],
        "Clicking anywhere on the card body should resume it as a card click"
      );
    });

    // Each stops propagation so only its own event fires.
    it("is not triggered by the close button, More button or menu items", () => {
      card.shadowRoot.querySelector(".resume-card-dismiss").click();
      card.shadowRoot.querySelector(".resume-card-more-button").click();
      card.shadowRoot.querySelectorAll("panel-item")[0].click();

      Assert.deepEqual(
        resumes,
        [],
        "Dismiss, More, and menu item clicks should not also dispatch resume"
      );
    });

    it("does not resume while releasing a text selection", () => {
      selectText(title);

      pointerClick(title);

      Assert.deepEqual(
        resumes,
        [],
        "A real click shouldn't resume while releasing a text selection"
      );
    });

    it("resumes once the text selection is collapsed", () => {
      selectText(title);
      doc.getSelection().collapseToStart();

      pointerClick(title);

      Assert.equal(
        resumes.length,
        1,
        "A real click should resume once the selection is collapsed"
      );
    });

    // Keyboard activation has detail 0.
    it("resumes on keyboard activation despite a text selection", () => {
      selectText(title);

      title.click();

      Assert.equal(
        resumes.length,
        1,
        "Keyboard-style activation should resume regardless of selection"
      );
    });

    // panel-list closes on mousedown before the card receives click.
    it("does not resume when the click closes the More menu", async () => {
      const panelList = await openMoreMenu(card);
      const hidden = BrowserTestUtils.waitForEvent(panelList, "hidden");

      clickOutsideMenu(title);

      await hidden;
      Assert.deepEqual(
        resumes,
        [],
        "Closing the more menu by clicking elsewhere on the card should not also resume it"
      );
    });

    it("resumes on a later click once the More menu is closed", async () => {
      const panelList = await openMoreMenu(card);
      const hidden = BrowserTestUtils.waitForEvent(panelList, "hidden");
      clickOutsideMenu(title);
      await hidden;

      title.click();

      Assert.equal(
        resumes.length,
        1,
        "A later click with the menu already closed should resume normally"
      );
    });
  });
});

async function ensureCardDefined(doc) {
  if (doc.defaultView.customElements.get("smartwindow-resume-card")) {
    return;
  }
  const script = doc.createElement("script");
  script.type = "module";
  script.src = CARD_MODULE_URL;
  doc.head.appendChild(script);
  await doc.defaultView.customElements.whenDefined("smartwindow-resume-card");
  script.remove();
}

/**
 * Renders a standalone resume card in the page and scrolls it into view.
 *
 * @param {Document} doc
 * @param {object} options
 * @param {object} options.content - The card's headline, status and tabs
 * @param {string} [options.journeyId]
 * @returns {Promise<Element>} The rendered card.
 */
async function createResumeCard(doc, { content, journeyId = "memory-1" }) {
  await ensureCardDefined(doc);
  const el = doc.createElement("smartwindow-resume-card");
  el.content = content;
  el.journeyId = journeyId;
  doc.body.appendChild(el);
  await el.updateComplete;
  // The card lands below the AI window. Bring it into view as a user would
  // see it, so that opening its menu doesn't scroll the page, which closes
  // panel-list.
  const { scrollY } = doc.defaultView;
  el.scrollIntoView();
  if (doc.defaultView.scrollY != scrollY) {
    await BrowserTestUtils.waitForEvent(doc, "scroll");
  }
  return el;
}

function nextEventDetail(card, type) {
  return new Promise(resolve =>
    card.addEventListener(type, e => resolve(e.detail), { once: true })
  );
}

/**
 * @param {Element} card
 * @returns {Promise<Element>} The card's panel-list, once shown.
 */
async function openMoreMenu(card) {
  const panelList = card.shadowRoot.querySelector("panel-list");
  const shown = BrowserTestUtils.waitForEvent(panelList, "shown");
  card.shadowRoot.querySelector(".resume-card-more-button").click();
  await shown;
  return panelList;
}

function selectText(element) {
  const selection = element.ownerDocument.getSelection();
  const range = element.ownerDocument.createRange();
  range.selectNodeContents(element);
  selection.removeAllRanges();
  selection.addRange(range);
}

// "click" must use PointerEvent here, or trusted dispatch crashes debug builds.
function pointerClick(element) {
  element.dispatchEvent(
    new PointerEvent("click", { bubbles: true, composed: true, detail: 1 })
  );
}

/**
 * Dispatches the full pointer sequence of a click on an element outside the
 * open menu.
 *
 * @param {Element} element
 */
function clickOutsideMenu(element) {
  element.dispatchEvent(
    new PointerEvent("pointerdown", { bubbles: true, composed: true })
  );
  element.dispatchEvent(
    new MouseEvent("mousedown", { bubbles: true, composed: true })
  );
  element.dispatchEvent(
    new PointerEvent("pointerup", { bubbles: true, composed: true })
  );
  element.dispatchEvent(
    new MouseEvent("mouseup", { bubbles: true, composed: true })
  );
  pointerClick(element);
}
