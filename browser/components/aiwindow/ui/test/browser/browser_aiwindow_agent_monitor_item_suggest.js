/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_agent_monitor_item.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_agent_monitor_item.js",
  this
);

const { SUGGESTION_TIMEOUT_MS } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/agents/MonitorSetup.sys.mjs"
);

// The spinner the button shows while it waits
const LOADING_ICON = "chrome://global/skin/icons/loading.svg";
const GENERATING_L10N_ID = "ai-tasks-alert-suggest-generating-button";
const FAILED_L10N_ID = "ai-tasks-alert-error-suggestion-failed";

const SUGGESTION_PAGE = "https://example.com/trail-runner";
const OTHER_PAGE = "https://example.com/trail-runner-3";

const FIRST = { title: "First name", condition: "First thing" };
const SECOND = { title: "Second name", condition: "Second thing" };
const LATE = { title: "Late name", condition: "Late thing" };
const NOTHING = { title: "", condition: "" };

/**
 * @param {?nsIAccessible} accessible
 * @returns {string} The text a screen reader finds under the accessible
 */
function accessibleText(accessible) {
  if (!accessible) {
    return "";
  }
  if (accessible.role === Ci.nsIAccessibleRole.ROLE_TEXT_LEAF) {
    return accessible.name;
  }
  let text = "";
  for (let index = 0; index < accessible.childCount; index++) {
    text += accessibleText(accessible.getChildAt(index));
  }
  return text;
}

/**
 * Runs a task on a create card whose host is the test: it records the
 * suggestions the card asks for and answers them by hand. The presses that go
 * through a real host are in browser_aiwindow_monitor_suggest_*.js.
 *
 * @param {Function} task
 * @param {object} [agent] - What the form is seeded with
 */
async function withCreateCard(task, agent = { watchUrls: [SUGGESTION_PAGE] }) {
  await withTestPage(async browser => {
    const win = browser.contentWindow;
    const el = win.document.getElementById("test-agent-monitor-item");
    const requests = [];
    el.addEventListener("agent-monitor-item:request-suggestion", event =>
      requests.push({
        requestId: event.detail.requestId,
        watchUrls: [...event.detail.watchUrls],
        previousDrafts: event.detail.previousDrafts.map(
          ({ title, condition }) => ({ title, condition })
        ),
      })
    );
    el.agent = agent;
    el.mode = "create";
    await el.updateComplete;

    const field = selector => el.shadowRoot.querySelector(selector);
    await task({
      win,
      el,
      requests,
      field,
      button: () => field(".suggest-button"),
      labelId: () => field(".suggest-button").dataset.l10nId,
      isBusy: () => field(".suggest-button").iconSrc === LOADING_ICON,
      /**
       * @param {string} text
       * @returns {Promise<void>} Once the status line's live region reads the
       *   text in the accessibility tree
       */
      announces(text) {
        const accService = Cc["@mozilla.org/accessibilityService;1"].getService(
          Ci.nsIAccessibilityService
        );
        return TestUtils.waitForCondition(
          () =>
            accessibleText(
              accService.getAccessibleFor(field(".suggest-status"))
            ).trim() === text,
          `The live region reads "${text}"`
        );
      },
      error: () => field(".suggest-status .error-message"),
      name: () => field(".monitor-name-input").value,
      condition: () => field(".monitor-condition-input").value,
      type(selector, text) {
        const input = field(selector);
        input.value = text;
        input.dispatchEvent(new win.Event("input", { bubbles: true }));
      },
      async click(selector) {
        field(selector).click();
        await el.updateComplete;
        await field(".suggest-button")?.updateComplete;
      },
      press() {
        return this.click(".suggest-button");
      },
      /**
       * @param {object} suggestion
       * @param {object} [request] - The request to answer, the newest if left out
       */
      async answer(suggestion, request = requests.at(-1)) {
        el.dispatchEvent(
          new win.CustomEvent("agent-monitor-item:suggestion", {
            detail: { requestId: request.requestId, ...suggestion },
          })
        );
        await el.updateComplete;
        await field(".suggest-button")?.updateComplete;
      },
      async pressAndAnswer(suggestion) {
        await this.press();
        await this.answer(suggestion);
      },
      async addPage(url) {
        el.pendingUrl = url;
        await el.updateComplete;
        await this.click(".add-page-btn");
      },
      async removePages() {
        for (const chip of el.shadowRoot.querySelectorAll("smartwindow-chip")) {
          chip.dispatchEvent(new win.CustomEvent("smartwindow-chip:remove"));
        }
        await el.updateComplete;
      },
    });
  });
}

add_task(async function test_press_asks_once_and_the_answer_fills_the_form() {
  await withCreateCard(async card => {
    const { el, win, requests, field } = card;
    const firstId = card.labelId();
    const fieldErrors = () =>
      el.shadowRoot.querySelectorAll(
        ".field .error-message, .monitor-name-field .error-message"
      ).length;

    // Submitting the blank form marks the name and the condition as missing
    await card.click('moz-button[type="primary"]');
    Assert.equal(fieldErrors(), 2, "The blank name and condition are flagged");

    // The form's only debounce is the one its draft waits out after typing
    const drafted = BrowserTestUtils.waitForEvent(
      el,
      "agent-monitor-item:draft-change"
    );
    card.type(".monitor-name-input", "My running shoes");
    await drafted;
    await card.addPage(OTHER_PAGE);
    Assert.equal(
      requests.length,
      0,
      "Opening and editing the form asks for nothing"
    );

    await card.press();
    Assert.deepEqual(
      requests,
      [
        {
          requestId: requests[0]?.requestId,
          watchUrls: [SUGGESTION_PAGE, OTHER_PAGE],
          previousDrafts: [],
        },
      ],
      "A press asks once, for the pages in the form, with nothing the user typed and no earlier drafts"
    );
    const button = card.button();
    Assert.ok(card.isBusy(), "The button shows a spinner while it waits");
    Assert.ok(
      button.disabled && button.shadowRoot.querySelector("button").disabled,
      "and is disabled, down to the button inside it"
    );
    const busyId = card.labelId();
    Assert.notEqual(busyId, firstId, "While it waits the button reads busy");
    const [generating, failed] = await win.document.l10n.formatValues([
      { id: GENERATING_L10N_ID },
      { id: FAILED_L10N_ID },
    ]);
    await card.announces(generating);
    Assert.equal(
      field(".suggest-status").getBoundingClientRect().height,
      0,
      "What the live region says while it waits takes no room on screen"
    );

    await SpecialPowers.pushPrefEnv({ set: [["ui.prefersReducedMotion", 0]] });
    Assert.equal(
      win.getComputedStyle(button).animationName,
      "pulse",
      "The waiting button pulses"
    );
    await SpecialPowers.pushPrefEnv({ set: [["ui.prefersReducedMotion", 1]] });
    Assert.equal(
      win.getComputedStyle(button).animationName,
      "none",
      "but not for a user who asked for less motion"
    );
    await SpecialPowers.popPrefEnv();
    await SpecialPowers.popPrefEnv();

    await card.press();
    Assert.equal(requests.length, 1, "A second press while busy is ignored");

    await card.answer(FIRST);
    Assert.ok(
      !card.isBusy() && !button.disabled,
      "The answer ends the busy state"
    );
    Assert.deepEqual(
      { title: card.name(), condition: card.condition() },
      FIRST,
      "The suggestion replaces what the user typed"
    );
    Assert.equal(fieldErrors(), 0, "Filling the fields clears their errors");
    Assert.ok(!card.error(), "No error is shown for an answer that arrived");
    await card.announces("");
    Assert.ok(
      ![firstId, busyId].includes(card.labelId()),
      "After a draft the button offers another one"
    );

    await card.pressAndAnswer({ title: "", condition: SECOND.condition });
    await card.pressAndAnswer({ title: SECOND.title, condition: "" });
    Assert.deepEqual(
      { title: card.name(), condition: card.condition() },
      SECOND,
      "An answer missing the title or the condition leaves that field as it is"
    );

    await card.pressAndAnswer(NOTHING);
    await card.announces(failed);
  });
});

add_task(async function test_stale_and_late_answers_are_dropped() {
  await withCreateCard(async card => {
    const { el, requests } = card;
    const firstId = card.labelId();
    const assertDropped = async message => {
      await card.answer(LATE);
      Assert.equal(card.name(), FIRST.title, message);
    };

    await card.pressAndAnswer(FIRST);
    await card.press();
    Assert.notEqual(
      requests[1].requestId,
      requests[0].requestId,
      "Each press has its own id"
    );
    await card.answer(LATE, requests[0]);
    Assert.equal(card.name(), FIRST.title, "An older answer is dropped");
    Assert.ok(card.isBusy(), "and the button keeps waiting for the current");

    await card.click("#cancel-create-button");
    Assert.ok(!card.isBusy(), "Cancelling ends the busy state");
    Assert.equal(card.labelId(), firstId, "and resets the button's label");
    await assertDropped("An answer that arrives after cancelling is dropped");

    await card.press();
    el.mode = "display";
    await el.updateComplete;
    el.mode = "create";
    await el.updateComplete;
    Assert.ok(!card.isBusy(), "Leaving the create form ends the busy state");
    await assertDropped("and drops the answer");

    await card.press();
    await card.removePages();
    Assert.ok(!card.button(), "The button leaves with the last page");
    await assertDropped("which drops the answer to a request in flight");
    await card.addPage(SUGGESTION_PAGE);
    Assert.ok(
      !card.isBusy() && !card.button().disabled && !card.error(),
      "The button comes back ready to be pressed, without an error"
    );

    await card.press();
    el.canSuggest = false;
    await el.updateComplete;
    el.canSuggest = true;
    await el.updateComplete;
    Assert.ok(!card.isBusy(), "A host turning the button off ends the wait");
    await assertDropped("and drops the answer");
  });
});

add_task(async function test_no_answer_in_time_shows_the_error() {
  await withCreateCard(async card => {
    const { el, win } = card;

    // Hold on to the timer the press sets, so the wait can be ended from here
    const realSetTimeout = win.setTimeout;
    const giveUps = [];
    win.setTimeout = (callback, delay, ...args) => {
      if (delay > SUGGESTION_TIMEOUT_MS) {
        giveUps.push(callback);
        return realSetTimeout.call(win, () => {}, 0);
      }
      return realSetTimeout.call(win, callback, delay, ...args);
    };
    try {
      await card.press();
    } finally {
      win.setTimeout = realSetTimeout;
    }
    Assert.equal(
      giveUps.length,
      1,
      "The card waits a little longer than the model call is given"
    );

    giveUps[0]();
    await el.updateComplete;
    Assert.ok(card.error(), "With no answer in time, the error is shown");
    Assert.ok(
      !card.isBusy() && !card.button().disabled,
      "and the button can be pressed again"
    );
    await card.answer(LATE);
    Assert.equal(card.name(), "", "An answer after the limit is dropped");
  });
});

add_task(async function test_error_goes_once_the_user_moves_on() {
  await withCreateCard(async card => {
    const { el, field } = card;
    const fail = async () => {
      await card.pressAndAnswer(NOTHING);
      Assert.ok(card.error(), "A failed press shows the error");
    };

    await fail();
    field(".monitor-name-input").focus();
    await el.updateComplete;
    Assert.ok(!card.error(), "Focusing a field of the form clears the error");

    await fail();
    card.type(".monitor-condition-input", "The price drops");
    await el.updateComplete;
    Assert.ok(!card.error(), "Editing a field clears it");

    // The blank name is what gets flagged, and it already has focus, so
    // nothing but the submit itself is left to clear the error
    await fail();
    await card.click('moz-button[type="primary"]');
    Assert.ok(
      field(".monitor-name-field .error-message"),
      "Submitting flags the blank name"
    );
    Assert.ok(!card.error(), "and clears the error of the failed suggestion");
  });
});

add_task(async function test_button_needs_something_to_go_on() {
  await withCreateCard(async card => {
    const { el, requests, field } = card;
    Assert.ok(!card.button(), "A blank form with no chat has no button");
    Assert.ok(
      field(".create-header .monitor-card-state-title"),
      "The form still has its title"
    );

    await card.addPage(SUGGESTION_PAGE);
    await card.pressAndAnswer(NOTHING);
    Assert.ok(card.error(), "With a page the button is there, and can fail");
    await card.removePages();
    Assert.ok(
      !card.button() && !card.error(),
      "The button and its error go away with the last page"
    );
    await card.addPage(SUGGESTION_PAGE);
    Assert.ok(
      card.button() && !card.error(),
      "The error does not come back with the button"
    );
    await card.removePages();

    el.hasChatContext = true;
    await el.updateComplete;
    await card.pressAndAnswer(FIRST);
    Assert.deepEqual(
      requests.at(-1).watchUrls,
      [],
      "Chat to draw on is enough: the card asks with no pages"
    );
    Assert.equal(card.name(), FIRST.title, "and fills the form");

    el.canSuggest = false;
    await card.addPage(SUGGESTION_PAGE);
    Assert.ok(
      !field(".suggest-button, .suggest-status"),
      "A host that turned suggestions off gets no button, with a page and with chat"
    );
    Assert.ok(
      field(".create-header .monitor-card-state-title"),
      "The form still has its title"
    );

    el.selfContained = false;
    await el.updateComplete;
    Assert.ok(
      !field(".create-top, .create-header"),
      "A host with its own title gets no empty header row"
    );
  }, {});
});

add_task(async function test_edit_form_has_no_suggest_button() {
  await withTestPage(async browser => {
    const el = browser.contentDocument.getElementById(
      "test-agent-monitor-item"
    );
    let requests = 0;
    el.addEventListener(
      "agent-monitor-item:request-suggestion",
      () => requests++
    );
    el.agent = {
      id: "agent-1",
      monitorName: "Trail Runner 2 price",
      watchUrls: [SUGGESTION_PAGE],
      condition: "the price drops",
      status: { label: "Active", kind: "watching" },
    };
    el.mode = "display";
    el.expanded = true;
    el.editing = true;
    await el.updateComplete;
    Assert.ok(
      el.shadowRoot.querySelector(".monitor-condition-input"),
      "The edit form is open"
    );
    Assert.ok(
      !el.shadowRoot.querySelector(".suggest-button"),
      "Only the create form has the button"
    );
    Assert.equal(requests, 0, "and nothing is asked for");
  });
});

add_task(async function test_retry_sends_the_earlier_drafts() {
  await withCreateCard(async card => {
    const { el, requests } = card;
    let draft;
    el.addEventListener(
      "agent-monitor-item:draft-change",
      event => (draft = event.detail?.draft)
    );
    const sent = () => requests.at(-1).previousDrafts;
    const reseed = async () => {
      el.agent = { watchUrls: [SUGGESTION_PAGE] };
      await el.updateComplete;
    };
    const firstId = card.labelId();

    await card.pressAndAnswer(FIRST);
    Assert.deepEqual(sent(), [], "The first press sends no earlier drafts");
    const retryId = card.labelId();

    const partial = { title: "", condition: SECOND.condition };
    card.type(".monitor-name-input", "My own name");
    await card.pressAndAnswer(partial);
    Assert.deepEqual(
      sent(),
      [FIRST],
      "A retry sends the draft that was written, not what the user typed over it"
    );
    await card.pressAndAnswer(NOTHING);

    // Each press sends the newest drafts that were filled in, oldest first
    const written = [FIRST, partial];
    for (const n of [3, 4, 5, 6]) {
      await card.press();
      Assert.deepEqual(
        sent(),
        written.slice(-sent().length),
        `Press ${n} sends the drafts so far, and a failed press added none`
      );
      const next = { title: `Name ${n}`, condition: `Thing ${n}` };
      await card.answer(next);
      written.push(next);
    }
    Assert.less(sent().length, written.length - 1, "The oldest are let go");
    Assert.deepEqual(
      draft.previousDrafts.map(({ title, condition }) => ({
        title,
        condition,
      })),
      written.slice(-draft.previousDrafts.length),
      "The drafts travel with the form's draft, the newest one included"
    );

    await card.click("#cancel-create-button");
    await card.pressAndAnswer(FIRST);
    Assert.deepEqual(sent(), [], "Cancelling the form forgets its drafts");

    el.draft = null;
    await reseed();
    Assert.equal(card.labelId(), firstId, "Seeding the form again resets it");
    await card.pressAndAnswer(SECOND);
    Assert.deepEqual(sent(), [], "and forgets its drafts");

    const submitted = BrowserTestUtils.waitForEvent(
      el,
      "agent-monitor-item:submit"
    );
    await card.click('moz-button[type="primary"]');
    await submitted;
    await el.updateComplete;
    Assert.equal(card.labelId(), firstId, "Submitting the form resets it");
    await card.pressAndAnswer(FIRST);
    Assert.deepEqual(sent(), [], "and forgets its drafts");

    // A host that rebuilds the card hands the drafts back with the draft
    el.draft = { suggested: true, previousDrafts: [FIRST, SECOND] };
    await reseed();
    Assert.equal(
      card.labelId(),
      retryId,
      "A restored draft that was suggested comes back offering a retry"
    );
    await card.press();
    Assert.deepEqual(sent(), [FIRST, SECOND], "with its earlier drafts");
  });
});

add_task(async function test_suggest_button_keeps_keyboard_focus() {
  await withCreateCard(async card => {
    const { el, win } = card;
    const doc = win.document;
    const button = card.button();
    const innerButton = button.shadowRoot.querySelector("button");
    // The element that has focus, however many shadow roots down it is
    const focused = () => {
      let active = doc.activeElement;
      while (active?.shadowRoot?.activeElement) {
        active = active.shadowRoot.activeElement;
      }
      return active;
    };
    // Focus leaves a disabled element when the page is next rendered
    const focusDropped = () =>
      TestUtils.waitForCondition(
        () => focused() === doc.body,
        "Focus drops to the page while the button is disabled"
      );
    const answerAndSettle = async suggestion => {
      await card.answer(suggestion);
      await new Promise(resolve => win.requestAnimationFrame(resolve));
    };

    button.focus();
    Assert.equal(focused(), innerButton, "The button has keyboard focus");
    await card.press();
    await focusDropped();
    let refocused = BrowserTestUtils.waitForEvent(innerButton, "focus");
    await card.answer(FIRST);
    await refocused;
    Assert.ok(
      !innerButton.disabled,
      "The answer puts focus back on a button that can be pressed"
    );

    await card.press();
    await focusDropped();
    refocused = BrowserTestUtils.waitForEvent(innerButton, "focus");
    await card.answer(NOTHING);
    await refocused;
    Assert.equal(focused(), innerButton, "A failure puts focus back too");
    Assert.ok(
      card.error(),
      "which leaves the error up: the button is no field"
    );

    await card.press();
    await focusDropped();
    el.shadowRoot.querySelector(".monitor-name-input").focus();
    const nameFocus = focused();
    Assert.notEqual(nameFocus, doc.body, "The user moved on to the name");
    await answerAndSettle(SECOND);
    Assert.equal(
      focused(),
      nameFocus,
      "Focus the user moved while waiting is left where it is"
    );

    nameFocus.blur();
    await card.press();
    await answerAndSettle(FIRST);
    Assert.equal(
      focused(),
      doc.body,
      "A press that did not come from a focused button gives it no focus"
    );
  });
});

add_task(async function test_create_header_holds_at_narrow_widths() {
  await withCreateCard(async card => {
    const { el, win, field } = card;
    const header = field(".create-header");
    const title = field(".monitor-card-state-title");
    const status = () => field(".suggest-status");
    const rect = node => node.getBoundingClientRect();
    const overflow = () =>
      field(".monitor-card").scrollWidth - field(".monitor-card").clientWidth;
    const settle = async () => {
      await el.updateComplete;
      await card.button()?.updateComplete;
      await win.document.l10n.translateFragment(el.shadowRoot);
      await new Promise(resolve => win.requestAnimationFrame(resolve));
    };

    Assert.ok(
      !header.contains(status()) && header.nextElementSibling === status(),
      "The error line is a row of its own under the header, not part of it"
    );
    await settle();
    const oneLine = rect(title).height;
    const titleWidth = rect(title.parentNode).width;
    Assert.greater(oneLine, 0, "The title is laid out");

    // The card's width in the chat sidebar at 412, 340, 280 and 216 px
    const widths = [360, 288, 228, 164];
    // How far the rest of the form overflows at each width without the button.
    // At the narrowest the form's own buttons already do.
    const withoutButton = new Map();
    el.canSuggest = false;
    for (const width of widths) {
      el.style.width = `${width}px`;
      await el.updateComplete;
      withoutButton.set(width, overflow());
    }
    el.canSuggest = true;
    await settle();

    // Every label the button gets, with and without the error line
    const states = [
      ["before a press", () => {}, false],
      ["while waiting", () => card.press(), false],
      ["after a failed press", () => card.answer(NOTHING), true],
      ["after a draft", () => card.pressAndAnswer(FIRST), false],
      ["after a failed retry", () => card.pressAndAnswer(NOTHING), true],
    ];
    for (const [state, reach, hasError] of states) {
      await reach();
      for (const width of widths) {
        el.style.width = `${width}px`;
        await settle();
        const where = `${width}px ${state}`;
        const button = card.button();
        Assert.ok(
          button.textContent.trim(),
          `The button has a label at ${where}`
        );
        Assert.lessOrEqual(
          header.scrollWidth,
          header.clientWidth,
          `Nothing overflows the header at ${where}`
        );
        Assert.lessOrEqual(
          overflow(),
          withoutButton.get(width),
          `The button adds no overflow to the card at ${where}`
        );
        Assert.ok(
          Math.round(rect(button).right) <= Math.round(rect(header).right) &&
            Math.round(rect(button).left) >= Math.round(rect(header).left),
          `The button sits inside the card at ${where}`
        );
        if (titleWidth <= header.clientWidth) {
          Assert.equal(
            rect(title).height,
            oneLine,
            `The title stays on one line at ${where}`
          );
        }
        Assert.equal(
          !!card.error(),
          hasError,
          `The error shows only when there is one at ${where}`
        );
        if (hasError) {
          Assert.greaterOrEqual(
            Math.round(rect(status()).top),
            Math.round(rect(header).bottom),
            `The error sits under the header at ${where}`
          );
          Assert.equal(
            Math.round(rect(status()).width),
            Math.round(rect(header).width),
            `on a row as wide as the header at ${where}`
          );
        }
      }
    }
  });
});
