/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_PAGE =
  "chrome://mochitests/content/browser/browser/components/aiwindow/ui/test/browser/test_agent_monitor_item_page.html";

const AGENT = {
  id: "agent-1",
  monitorName: "Sony WH-1000XM5 price",
  url: "soundnest.com/audio/sony-wh-1000xm5",
  condition: "the price drops below $270",
  conditionPresets: ["Any drop", "Below $270", "Below $250"],
  status: { label: "Active", kind: "watching" },
  cadence: "Auto · on-device",
};

async function openTestPage() {
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.customElements.whenDefined("agent-monitor-item");
  });
  return { tab, browser: tab.linkedBrowser };
}

async function withTestPage(fn) {
  const { tab, browser } = await openTestPage();
  try {
    await fn(browser);
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
}

async function setProps(browser, props) {
  await SpecialPowers.spawn(browser, [props], async properties => {
    const el = content.document.getElementById("test-agent-monitor-item");
    Object.assign(el, properties);
    await el.updateComplete;
  });
}

add_task(async function test_display_collapsed_and_expanded() {
  await withTestPage(async browser => {
    await setProps(browser, { agent: AGENT, mode: "display", expanded: false });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      Assert.ok(
        shadow.querySelector(".monitor-card.chatcard"),
        "Display mode renders a collapsible monitor card"
      );
      Assert.equal(
        shadow.querySelector(".monitor-card-name").textContent.trim(),
        "Sony WH-1000XM5 price",
        "Head shows the monitor name from the agent property"
      );
      Assert.ok(
        !shadow.querySelector(".watch-expand"),
        "Expanded body is absent when collapsed"
      );

      el.expanded = true;
      await el.updateComplete;

      Assert.ok(
        shadow.querySelector(".watch-expand"),
        "Expanded body appears when expanded is true"
      );
    });
  });
});

add_task(async function test_toggle_dispatches_and_expands() {
  await withTestPage(async browser => {
    await setProps(browser, { agent: AGENT, mode: "display", expanded: false });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      const events = [];
      el.addEventListener("agent-monitor-item:toggle", e =>
        events.push(e.detail?.expanded)
      );

      shadow.querySelector(".chev").click();
      await el.updateComplete;

      Assert.equal(
        el.expanded,
        true,
        "Activating the chevron expands the card"
      );
      Assert.deepEqual(
        events,
        [true],
        "toggle event fires with the new expanded value"
      );
    });
  });
});

add_task(async function test_edit_toggle_expands_and_shows_field() {
  await withTestPage(async browser => {
    await setProps(browser, { agent: AGENT, mode: "display", expanded: false });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      const events = [];
      el.addEventListener("agent-monitor-item:edit-toggle", e =>
        events.push(e.detail?.editing)
      );

      // First expand the card to reveal the edit button
      shadow.querySelector(".chev").click();
      await el.updateComplete;

      // Now click the edit button
      shadow.querySelector("#edit-button").click();
      await el.updateComplete;

      Assert.equal(el.editing, true, "Edit button turns on editing");
      Assert.equal(
        el.expanded,
        true,
        "Opening edit while collapsed also expands the card"
      );
      Assert.ok(
        shadow.querySelector("moz-textarea.monitor-condition-input"),
        "Editable condition field is shown while editing"
      );
      Assert.deepEqual(events, [true], "edit-toggle event fires with editing");
    });
  });
});

add_task(async function test_preset_updates_condition() {
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: AGENT,
      mode: "display",
      expanded: true,
      editing: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      const chips = shadow.querySelectorAll(".chip");
      chips[2].click(); // "Below $250"
      await el.updateComplete;

      Assert.equal(
        shadow.querySelector("moz-textarea.monitor-condition-input").value,
        "Below $250",
        "Selecting a preset chip updates the condition field"
      );
    });
  });
});

add_task(async function test_submit_and_delete_dispatch_detail() {
  await withTestPage(async browser => {
    await setProps(browser, {
      // A schedule the card seeds from, so the submitted one doesn't depend on
      // the form's clock-based default
      agent: {
        ...AGENT,
        schedule: { frequency: "daily", time: "09:00", weekday: 1 },
      },
      mode: "display",
      expanded: true,
      editing: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      let submitDetail = null;
      let deleteDetail = null;
      el.addEventListener(
        "agent-monitor-item:submit",
        e => (submitDetail = e.detail)
      );
      el.addEventListener(
        "agent-monitor-item:delete",
        e => (deleteDetail = e.detail)
      );

      // Save dispatches submit
      const saveButton = shadow.querySelector(
        'moz-button[data-l10n-id="ai-tasks-alert-save-button"]'
      );
      saveButton.click();
      await el.updateComplete;
      Assert.deepEqual(
        submitDetail,
        {
          mode: "display",
          id: "agent-1",
          monitorName: "Sony WH-1000XM5 price",
          condition: "the price drops below $270",
          watchUrls: ["soundnest.com/audio/sony-wh-1000xm5"],
          schedule: { frequency: "daily", time: "09:00", weekday: 1 },
          autoExpandAndCheck: false,
        },
        "submit carries mode, id, monitor name, condition, watch URLs and schedule"
      );

      const deleteButton = shadow.querySelector(
        'moz-button[data-l10n-id="ai-tasks-alert-delete-button"]'
      );
      deleteButton.click();
      await el.updateComplete;
      Assert.deepEqual(
        deleteDetail,
        { id: "agent-1" },
        "delete carries the agent id"
      );
    });
  });
});

/**
 * The watching/paused pill is its own element, so every surface that lists
 * monitors shows the same one. The card only says which status to state.
 */
add_task(async function test_status_chip_states_the_status() {
  await withTestPage(async browser => {
    await setProps(browser, { agent: AGENT, mode: "display" });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const chip = el.shadowRoot.querySelector("monitor-status-chip");
      Assert.ok(chip, "The display card shows a status chip");
      Assert.equal(chip.kind, "watching", "An active monitor is watching");

      await ContentTaskUtils.waitForCondition(
        () => chip.shadowRoot.querySelector("span")?.textContent,
        "the chip is localized"
      );
      Assert.equal(
        chip.shadowRoot.querySelector("span").textContent,
        "Active",
        "The chip states the status in words"
      );

      el.agent = { ...el.agent, status: { kind: "paused" } };
      await el.updateComplete;
      Assert.equal(chip.kind, "paused", "A paused monitor says so");

      el.agent = { ...el.agent, status: null };
      await el.updateComplete;
      Assert.ok(
        !chip.shadowRoot.querySelector("span"),
        "A monitor with no status has no pill"
      );
      await ContentTaskUtils.waitForCondition(
        () => content.getComputedStyle(chip).display === "none",
        "an empty pill takes up no room"
      );
    });
  });
});

add_task(async function test_pause_button_toggles_label_and_detail() {
  await withTestPage(async browser => {
    await setProps(browser, { agent: AGENT, mode: "display", expanded: true });

    const pausedAgent = {
      ...AGENT,
      status: { label: "Paused", kind: "paused" },
    };

    await SpecialPowers.spawn(browser, [pausedAgent], async pausedAgentArg => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      const pauseDetails = [];
      el.addEventListener("agent-monitor-item:pause", e =>
        pauseDetails.push(e.detail)
      );

      const clickWhenLaidOut = async button => {
        await ContentTaskUtils.waitForCondition(() => {
          const rect = button.getBoundingClientRect();
          return rect.width > 0 && rect.height > 0;
        }, "button is laid out before clicking");
        button.click();
      };

      const pauseButton = shadow.querySelector(
        'moz-button[data-l10n-id="ai-tasks-alert-pause-button"]'
      );
      Assert.ok(pauseButton, "Active monitor shows a Pause button");
      await clickWhenLaidOut(pauseButton);
      await el.updateComplete;
      Assert.deepEqual(
        pauseDetails.at(-1),
        { id: "agent-1", paused: true },
        "Pause requests paused: true"
      );

      el.agent = pausedAgentArg;
      await el.updateComplete;

      const resumeButton = shadow.querySelector(
        'moz-button[data-l10n-id="ai-tasks-alert-resume-button"]'
      );
      Assert.ok(resumeButton, "Paused monitor shows a Resume button");
      await clickWhenLaidOut(resumeButton);
      await el.updateComplete;
      Assert.deepEqual(
        pauseDetails.at(-1),
        { id: "agent-1", paused: false },
        "Resume requests paused: false"
      );
    });
  });
});

add_task(async function test_check_now_dispatches_detail() {
  await withTestPage(async browser => {
    await setProps(browser, { agent: AGENT, mode: "display", expanded: true });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      let checkDetail = null;
      el.addEventListener(
        "agent-monitor-item:check-now",
        e => (checkDetail = e.detail)
      );

      const checkNowButton = shadow.querySelector(
        'moz-button[data-l10n-id="ai-tasks-alert-check-now-button"]'
      );

      await ContentTaskUtils.waitForCondition(() => {
        const rect = checkNowButton.getBoundingClientRect();
        return rect.width > 0 && rect.height > 0;
      }, "Check now button is laid out before clicking");
      checkNowButton.click();
      await el.updateComplete;

      Assert.deepEqual(
        checkDetail,
        { id: "agent-1" },
        "check-now carries the agent id"
      );
    });
  });
});

add_task(async function test_watch_url_chip_dispatches_open() {
  const WATCH_URL = "https://soundnest.com/audio/sony-wh-1000xm5";

  await withTestPage(async browser => {
    await setProps(browser, {
      agent: { ...AGENT, watchUrls: [WATCH_URL] },
      mode: "display",
      expanded: true,
    });

    await SpecialPowers.spawn(browser, [WATCH_URL], async watchUrl => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      let openDetail = null;
      el.addEventListener(
        "AIChatContent:OpenLink",
        e => (openDetail = e.detail)
      );

      const chip = shadow.querySelector(".url-chips smartwindow-chip");
      Assert.ok(chip, "Watched pages render as website chips");
      Assert.equal(
        chip.label,
        "soundnest.com",
        "The chip is labelled with the hostname"
      );
      Assert.equal(
        chip.href,
        watchUrl,
        "The chip is a real link to the full watched URL"
      );

      chip.shadowRoot.querySelector(".chip").click();
      await el.updateComplete;

      Assert.ok(openDetail, "Clicking a chip emits AIChatContent:OpenLink");
      Assert.equal(
        openDetail.url,
        watchUrl,
        "OpenLink carries the full watched URL"
      );
      Assert.ok(
        openDetail.preferSwitchToTab,
        "A plain click prefers an already open tab"
      );
    });
  });
});

add_task(async function test_watch_url_chip_shows_resolved_title() {
  const WITH_TITLE = "https://soundnest.com/audio/sony-wh-1000xm5";
  const NO_TITLE = "https://example.com/no-title-here";

  await withTestPage(async browser => {
    await setProps(browser, {
      agent: {
        ...AGENT,
        watchUrls: [WITH_TITLE, NO_TITLE],
        watchUrlTitles: { [WITH_TITLE]: "Sony WH-1000XM5 Headphones" },
      },
      mode: "display",
      expanded: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const chips = el.shadowRoot.querySelectorAll(
        ".url-chips smartwindow-chip"
      );

      Assert.equal(chips.length, 2, "Both watched pages render as chips");
      Assert.equal(
        chips[0].label,
        "Sony WH-1000XM5 Headphones",
        "A chip with a resolved title is labelled with the title"
      );
      Assert.equal(
        chips[1].label,
        "example.com",
        "A chip with no resolved title falls back to the hostname"
      );
    });
  });
});

add_task(async function test_edit_mode_shows_pages_and_scheduler() {
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: AGENT,
      mode: "display",
      expanded: true,
      editing: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      Assert.equal(
        shadow.querySelectorAll(".page-pills-row smartwindow-chip").length,
        1,
        "Edit mode seeds a pill for the monitor's existing URL"
      );
      Assert.ok(
        shadow.querySelector(".page-input-row moz-input-url.page-url-input"),
        "A pending URL input is shown for adding pages"
      );
      Assert.ok(
        shadow.querySelector(".page-input-row moz-button.add-page-btn"),
        "The pending URL input has an add-page button"
      );
      const selects = shadow.querySelectorAll(".schedule-container moz-select");
      Assert.equal(
        selects.length,
        1,
        "Daily edit shows only the frequency select"
      );

      el.checkFrequency = "weekly";
      await el.updateComplete;
      Assert.equal(
        shadow.querySelectorAll(".schedule-container moz-select").length,
        2,
        "Weekly edit reveals the Day select alongside frequency"
      );
    });
  });
});

add_task(async function test_expanded_display_shows_saved_schedule() {
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: {
        ...AGENT,
        cadence: undefined,
        schedule: { frequency: "weekly", time: "09:00", weekday: "1" },
      },
      mode: "display",
      expanded: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      const checkRow = shadow
        .querySelector('[data-l10n-id^="ai-tasks-alert-schedule-"]')
        ?.closest(".monitor-row");
      Assert.ok(checkRow, "Expanded display shows a Check row");
      Assert.equal(
        checkRow.querySelector("[data-l10n-id]").getAttribute("data-l10n-id"),
        "ai-tasks-alert-schedule-weekly-monday",
        "Check row uses the correct localization ID for weekly Monday schedule"
      );
    });
  });
});

add_task(async function test_create_mode_renders_form() {
  await withTestPage(async browser => {
    await setProps(browser, { agent: AGENT, mode: "create" });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      Assert.ok(
        shadow.querySelector(".monitor-card"),
        "Create mode renders a monitor card"
      );
      Assert.ok(
        !shadow.querySelector(".chatcard"),
        "Create mode is not the collapsible chat card"
      );
      Assert.ok(
        shadow.querySelector("moz-textarea.monitor-condition-input"),
        "Create mode shows the condition field"
      );

      let submitDetail = null;
      el.addEventListener(
        "agent-monitor-item:submit",
        e => (submitDetail = e.detail)
      );
      // Start monitoring
      const createButton = shadow.querySelector(
        'moz-button[data-l10n-id="ai-tasks-alert-create-button"]'
      );
      createButton.click();
      await el.updateComplete;

      Assert.equal(
        submitDetail?.mode,
        "create",
        "submit from create mode reports mode 'create'"
      );
    });
  });
});

/**
 * A self-contained card frames and titles itself. A host that already does both
 * - the Tasks panel, which has its own header - sets selfContained to false and
 * gets just the fields.
 */
add_task(async function test_self_contained_frame_and_title() {
  await withTestPage(async browser => {
    await setProps(browser, { agent: AGENT, mode: "create" });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      Assert.ok(
        el.selfContained,
        "Cards are self-contained unless a host says otherwise"
      );
      Assert.ok(
        el.hasAttribute("self-contained"),
        "selfContained is reflected so the stylesheet can drop the frame"
      );
      Assert.ok(
        shadow.querySelector(".title-container"),
        "A self-contained create card states its own title"
      );

      el.selfContained = false;
      await el.updateComplete;

      Assert.ok(
        !el.hasAttribute("self-contained"),
        "Turning it off removes the attribute"
      );
      Assert.ok(
        !shadow.querySelector(".title-container"),
        "A hosted create card leaves the title to its host"
      );
      Assert.ok(
        shadow.querySelector("moz-textarea.monitor-condition-input"),
        "A hosted create card still shows the fields"
      );

      const card = shadow.querySelector(".monitor-card");
      const style = content.getComputedStyle(card);
      Assert.equal(
        style.borderTopWidth,
        "0px",
        "A hosted card does not draw its own border"
      );
      Assert.notEqual(
        style.paddingTop,
        "0px",
        "A hosted card keeps its padding so fields clear the host's edge"
      );
    });
  });
});

/**
 * moz-select swaps its native <select> for a popover-based list as soon as an
 * option has an icon, and a popover renders in the document's top layer - the
 * wrong place when the host is a XUL panel, which is its own widget. A hosted
 * card therefore drops the option icons to keep the native dropdown.
 */
add_task(async function test_hosted_card_keeps_native_select() {
  await withTestPage(async browser => {
    await setProps(browser, { agent: AGENT, mode: "create" });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const selects = () => [...el.shadowRoot.querySelectorAll("moz-select")];
      const optionsHaveIcons = () =>
        selects().every(select =>
          [...select.querySelectorAll("moz-option")].every(option =>
            option.hasAttribute("iconsrc")
          )
        );

      await Promise.all(selects().map(select => select.updateComplete));
      Assert.ok(selects().length, "The create form has selects to check");
      Assert.ok(
        optionsHaveIcons(),
        "A self-contained card keeps its option icons"
      );
      Assert.ok(
        selects().every(select => select.usePanelList),
        "Icons mean moz-select renders its popover list"
      );

      el.selfContained = false;
      await el.updateComplete;
      await Promise.all(selects().map(select => select.updateComplete));

      Assert.ok(
        selects().every(select =>
          [...select.querySelectorAll("moz-option")].every(
            option => !option.hasAttribute("iconsrc")
          )
        ),
        "A hosted card drops the option icons"
      );
      Assert.ok(
        selects().every(select => !select.usePanelList),
        "Without icons moz-select falls back to the native dropdown"
      );
      Assert.ok(
        selects().every(select => select.shadowRoot.querySelector("select")),
        "A hosted card renders real native selects"
      );
    });
  });
});

add_task(async function test_create_mode_empty_state_inputs() {
  await withTestPage(async browser => {
    // when no monitorName or value is available the card should offer editable inputs instead
    await setProps(browser, {
      agent: { conditionPresets: ["Any drop"] },
      mode: "create",
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      const nameInput = shadow.querySelector(
        "moz-input-text.monitor-name-input"
      );
      Assert.ok(nameInput, "Name input is shown when there is no monitor name");

      const setValue = (input, value) => {
        input.value = value;
        input.dispatchEvent(new content.Event("input", { bubbles: true }));
        input.dispatchEvent(new content.Event("change", { bubbles: true }));
      };

      let submitDetail = null;
      el.addEventListener(
        "agent-monitor-item:submit",
        e => (submitDetail = e.detail)
      );

      const startButton = shadow.querySelector(
        'moz-button[data-l10n-id="ai-tasks-alert-create-button"]'
      );

      startButton.click();
      await el.updateComplete;
      Assert.equal(
        submitDetail,
        null,
        "Submit is blocked while the form is empty"
      );

      const errorIds = [...shadow.querySelectorAll(".error-message")]
        .map(node => node.getAttribute("data-l10n-id"))
        .sort();
      Assert.deepEqual(
        errorIds,
        [
          "ai-tasks-alert-error-condition-required",
          "ai-tasks-alert-error-name-required",
          "ai-tasks-alert-error-no-pages",
        ],
        "Empty submit surfaces name, condition and pages errors"
      );
      Assert.ok(nameInput.hasAttribute("invalid"), "Name input is invalid");
      Assert.ok(
        shadow
          .querySelector("moz-textarea.monitor-condition-input")
          .hasAttribute("invalid"),
        "Condition input is invalid"
      );
      Assert.ok(
        shadow
          .querySelector("moz-input-url.page-url-input")
          .hasAttribute("invalid"),
        "URL input is invalid"
      );
      Assert.equal(
        shadow.activeElement,
        nameInput,
        "Focus moves to the first invalid field"
      );

      setValue(nameInput, "Sony Headphone");
      setValue(
        shadow.querySelector("moz-textarea.monitor-condition-input"),
        "the price drops"
      );

      const urlInput = shadow.querySelector("moz-input-url.page-url-input");
      urlInput.value = "https://example.com/product";
      urlInput.dispatchEvent(new content.Event("input", { bubbles: true }));
      urlInput.dispatchEvent(
        new content.KeyboardEvent("keydown", { key: "Enter", bubbles: true })
      );
      await el.updateComplete;
      Assert.equal(
        shadow.querySelectorAll(".page-pills-row smartwindow-chip").length,
        1,
        "Adding a URL shows a pill"
      );

      startButton.click();
      await el.updateComplete;

      Assert.equal(
        submitDetail?.monitorName,
        "Sony Headphone",
        "submit carries the typed monitor name"
      );
      Assert.equal(
        submitDetail?.condition,
        "the price drops",
        "submit carries the typed condition"
      );
      Assert.deepEqual(
        submitDetail?.watchUrls,
        ["https://example.com/product"],
        "submit carries the added page URL"
      );
    });
  });
});

add_task(async function test_create_mode_defaults_time_to_next_slot() {
  await withTestPage(async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      const SLOTS_PER_DAY = 48;
      const slotValue = date => {
        const slot =
          Math.ceil((date.getHours() * 60 + date.getMinutes()) / 30) %
          SLOTS_PER_DAY;
        const hour = Math.floor(slot / 2);
        return `${String(hour).padStart(2, "0")}:${slot % 2 ? "30" : "00"}`;
      };

      // The card computes its default in its constructor, so bracket that call
      // with timestamps: the only acceptable values are the slots those two
      // instants map to, which collapse to one unless a boundary was crossed.
      const before = new Date();
      const el = content.document.createElement("agent-monitor-item");
      const after = new Date();

      const accepted = [...new Set([slotValue(before), slotValue(after)])];

      el.mode = "create";
      content.document.body.append(el);
      await el.updateComplete;

      const timeSelect = el.shadowRoot.querySelectorAll(
        "moz-select.form-select"
      )[1];
      Assert.ok(
        accepted.includes(timeSelect.value),
        `Time defaults to the upcoming half-hour slot, got ${timeSelect.value}, expected one of ${accepted}`
      );
      Assert.ok(
        [...el.shadowRoot.querySelectorAll("moz-option")].some(
          opt => opt.value === timeSelect.value
        ),
        "The default is a value the time dropdown actually offers"
      );

      el.remove();
    });
  });
});

add_task(async function test_typing_without_blur_persists() {
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: { conditionPresets: [] },
      mode: "create",
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      let draft;
      el.addEventListener(
        "agent-monitor-item:draft-change",
        e => (draft = e.detail?.draft)
      );

      // Typing and switching away without ever leaving the field: no change
      // event fires, so the coalesced input is all the host will ever get
      for (const [field, value] of [
        ["moz-input-text.monitor-name-input", "Sony Head"],
        ["moz-textarea.monitor-condition-input", "the price dro"],
      ]) {
        const input = shadow.querySelector(field);
        input.value = value;
        input.dispatchEvent(new content.Event("input", { bubbles: true }));
      }

      Assert.equal(
        draft,
        undefined,
        "Typing does not emit a draft on every keystroke"
      );

      await ContentTaskUtils.waitForCondition(
        () => draft,
        "Waiting for the coalesced draft"
      );

      Assert.equal(
        draft.monitorName,
        "Sony Head",
        "A name typed but never blurred still reaches the host"
      );
      Assert.equal(
        draft.condition,
        "the price dro",
        "So does a partially typed condition"
      );
    });
  });
});

add_task(async function test_form_edits_emit_draft() {
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: { conditionPresets: ["Any drop"] },
      mode: "create",
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      let draft;
      el.addEventListener(
        "agent-monitor-item:draft-change",
        e => (draft = e.detail?.draft)
      );

      const nameInput = shadow.querySelector(
        "moz-input-text.monitor-name-input"
      );
      nameInput.value = "Sony Headphone";
      nameInput.dispatchEvent(new content.Event("change", { bubbles: true }));
      await el.updateComplete;

      Assert.equal(
        draft?.monitorName,
        "Sony Headphone",
        "Editing the name emits a draft carrying it"
      );

      shadow.querySelector(".chip-row moz-button.chip").click();
      await el.updateComplete;

      Assert.equal(
        draft?.condition,
        "Any drop",
        "Picking a preset emits a draft carrying the condition"
      );
      Assert.equal(
        draft?.monitorName,
        "Sony Headphone",
        "Each draft is a full snapshot, not just the changed field"
      );

      const urlInput = shadow.querySelector("moz-input-url.page-url-input");
      urlInput.value = "https://example.com/a";
      urlInput.dispatchEvent(new content.Event("input", { bubbles: true }));
      shadow.querySelector("moz-button.add-page-btn").click();
      await el.updateComplete;

      Assert.deepEqual(
        draft?.watchUrls,
        ["https://example.com/a"],
        "Adding a URL emits a draft carrying it"
      );

      // Cancelling takes the whole card with it, so the host needs no further
      // draft update
      draft = undefined;
      shadow.querySelector("#cancel-create-button").click();
      await el.updateComplete;

      Assert.strictEqual(
        draft,
        undefined,
        "Cancelling the create card emits no trailing draft"
      );
    });
  });
});

add_task(async function test_entering_edit_mode_persists() {
  await withTestPage(async browser => {
    await setProps(browser, { agent: AGENT, mode: "display", expanded: true });

    await SpecialPowers.spawn(browser, [AGENT.condition], async condition => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      let draft;
      el.addEventListener(
        "agent-monitor-item:draft-change",
        e => (draft = e.detail?.draft)
      );

      // Opening the form and switching away without touching a field
      shadow.querySelector("#edit-button").click();
      await el.updateComplete;

      Assert.ok(draft, "Opening the edit form emits a draft on its own");
      Assert.strictEqual(
        draft.editing,
        true,
        "The draft records that an edit session is open"
      );
      Assert.equal(
        draft.condition,
        condition,
        "An untouched form keeps the monitor's current values"
      );
    });
  });
});

add_task(async function test_draft_restores_edit_mode() {
  await withTestPage(async browser => {
    // A card rebuilt mid-edit: the agent says collapsed and not editing, so
    // only the draft knows the user was in the middle of something
    await setProps(browser, {
      agent: AGENT,
      mode: "display",
      expanded: false,
      editing: false,
      draft: {
        editing: true,
        condition: "the price drops below $100",
      },
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      Assert.ok(el.editing, "The unsubmitted edit reopens the edit form");
      Assert.ok(
        el.expanded,
        "The card expands so the edit form is actually visible"
      );
      Assert.ok(
        shadow.querySelector("#cancel-edit-button"),
        "Edit mode affordances are rendered"
      );
      Assert.equal(
        shadow.querySelector("moz-textarea.monitor-condition-input").value,
        "the price drops below $100",
        "The edit in progress is restored into the field"
      );
    });
  });
});

add_task(async function test_leaving_edit_mode_discards_draft() {
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: AGENT,
      mode: "display",
      expanded: true,
      editing: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      let draft;
      el.addEventListener(
        "agent-monitor-item:draft-change",
        e => (draft = e.detail?.draft)
      );

      shadow.querySelector(".chip").click();
      await el.updateComplete;
      Assert.ok(draft, "Editing an existing monitor emits a draft");

      // The card stays, so the discarded edit has to be cleared explicitly
      shadow.querySelector("#cancel-edit-button").click();
      await el.updateComplete;

      Assert.strictEqual(
        draft,
        null,
        "Cancelling out of edit mode discards the draft"
      );
    });
  });
});

add_task(async function test_draft_restores_over_agent_values() {
  await withTestPage(async browser => {
    // A rebuilt card is seeded from the agent, then the host's draft is layered
    // back over it - the values the user never submitted.
    await setProps(browser, {
      agent: {
        condition: "the price drops below $270",
        watchUrls: ["https://example.com/seeded"],
        schedule: { frequency: "daily", time: "09:00" },
      },
      draft: {
        monitorName: "Half typed name",
        condition: "the price drops below $100",
        watchUrls: ["https://example.com/a", "https://example.com/b"],
        pendingUrl: "https://example.com/not-added-yet",
        schedule: { frequency: "weekly", time: "17:30", weekday: 3 },
      },
      mode: "create",
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      Assert.equal(
        shadow.querySelector("moz-input-text.monitor-name-input").value,
        "Half typed name",
        "Name is restored from the draft"
      );
      Assert.equal(
        shadow.querySelector("moz-textarea.monitor-condition-input").value,
        "the price drops below $100",
        "Condition is restored from the draft, not the agent"
      );
      Assert.equal(
        shadow.querySelectorAll(".page-pills-row smartwindow-chip").length,
        2,
        "Added URLs are restored from the draft, replacing the seeded one"
      );
      Assert.equal(
        shadow.querySelector("moz-input-url.page-url-input").value,
        "https://example.com/not-added-yet",
        "A typed but not yet added URL is restored too"
      );
      const selects = shadow.querySelectorAll("moz-select.form-select");
      Assert.equal(
        selects[0].value,
        "weekly",
        "Check frequency is restored from the draft"
      );
      Assert.equal(
        shadow.querySelectorAll(".form-section-half").length,
        3,
        "Weekly restores the weekday field alongside check and time"
      );

      // A live update from the host reseeds from the agent - the in-progress
      // form has to survive it
      el.agent = { ...el.agent, history: [{ checkedAt: Date.now() }] };
      await el.updateComplete;

      Assert.equal(
        shadow.querySelector("moz-textarea.monitor-condition-input").value,
        "the price drops below $100",
        "A fresh agent object does not wipe the in-progress form"
      );
    });
  });
});

add_task(async function test_form_requests_and_shows_page_titles() {
  const SEEDED = "https://example.com/seeded";
  const ADDED = "https://example.com/added";

  await withTestPage(async browser => {
    await SpecialPowers.spawn(
      browser,
      [SEEDED, ADDED],
      async (seeded, added) => {
        const el = content.document.getElementById("test-agent-monitor-item");
        const requests = [];
        el.addEventListener("agent-monitor-item:resolve-titles", e =>
          requests.push([...e.detail.urls])
        );
        const chipLabels = () =>
          [
            ...el.shadowRoot.querySelectorAll(
              ".page-pills-row smartwindow-chip"
            ),
          ].map(chip => chip.label);

        el.agent = Cu.cloneInto(
          { condition: "", watchUrls: [seeded] },
          content
        );
        el.mode = "create";
        await el.updateComplete;

        Assert.deepEqual(
          requests,
          [[seeded]],
          "The form asks for the title of the page it was seeded with"
        );
        Assert.deepEqual(
          chipLabels(),
          ["example.com"],
          "A chip shows the hostname until its title arrives"
        );

        el.dispatchEvent(
          new content.CustomEvent("agent-monitor-item:titles-resolved", {
            detail: Cu.cloneInto(
              { titles: { [seeded]: "Seeded Page" } },
              content
            ),
          })
        );
        await el.updateComplete;
        Assert.deepEqual(
          chipLabels(),
          ["Seeded Page"],
          "The chip shows the title once the host answers"
        );

        el.pendingUrl = added;
        await el.updateComplete;
        el.shadowRoot.querySelector(".add-page-btn").click();
        await el.updateComplete;

        Assert.deepEqual(
          requests,
          [[seeded], [added]],
          "Adding a page asks only for the new page's title"
        );

        el.alertDescription = "edited";
        await el.updateComplete;
        Assert.equal(
          requests.length,
          2,
          "An untitled page is not asked about again on later updates"
        );

        el.agent = Cu.cloneInto(
          { condition: "", watchUrls: [seeded, added] },
          content
        );
        await el.updateComplete;
        Assert.deepEqual(
          requests,
          [[seeded], [added], [added]],
          "Fresh agent data asks again for pages still without a title"
        );
      }
    );
  });
});

add_task(async function test_add_and_remove_page_pills() {
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: { conditionPresets: [] },
      mode: "create",
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      Assert.equal(
        shadow.querySelectorAll(".page-pills-row smartwindow-chip").length,
        0,
        "Starts with no page pills when nothing is seeded"
      );

      const urlInput = shadow.querySelector("moz-input-url.page-url-input");
      urlInput.value = "https://example.com/a";
      urlInput.dispatchEvent(new content.Event("input", { bubbles: true }));
      shadow.querySelector("moz-button.add-page-btn").click();
      await el.updateComplete;
      Assert.equal(
        shadow.querySelectorAll(".page-pills-row smartwindow-chip").length,
        1,
        "Add button adds the typed URL as a pill"
      );

      const chip = shadow.querySelector(".page-pills-row smartwindow-chip");
      await chip.updateComplete;
      // The chip's remove button only becomes visible on hover, which a test
      // can't trigger reliably, so click it directly.
      chip.shadowRoot.querySelector(".chip-remove").click();
      await el.updateComplete;
      Assert.equal(
        shadow.querySelectorAll(".page-pills-row smartwindow-chip").length,
        0,
        "Pill remove button removes the URL"
      );
    });
  });
});

add_task(async function test_invalid_url_shows_error() {
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: { conditionPresets: [] },
      mode: "create",
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      const urlInput = shadow.querySelector("moz-input-url.page-url-input");
      urlInput.value = "not a url";
      urlInput.dispatchEvent(new content.Event("input", { bubbles: true }));
      shadow.querySelector("moz-button.add-page-btn").click();

      await el.updateComplete;
      Assert.equal(
        shadow.querySelector(".error-message")?.getAttribute("data-l10n-id"),
        "ai-tasks-alert-error-invalid-url",
        "A value that is not a URL shows the invalid-URL error"
      );
      Assert.equal(
        shadow.querySelectorAll(".page-pills-row smartwindow-chip").length,
        0,
        "An invalid URL is not added as a pill"
      );
    });
  });
});

add_task(async function test_bare_host_defaults_to_https() {
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: { conditionPresets: [] },
      mode: "create",
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      const urlInput = shadow.querySelector("moz-input-url.page-url-input");
      const addButton = shadow.querySelector("moz-button.add-page-btn");

      // A scheme-less host is watched over https rather than rejected.
      urlInput.value = "example.com";
      urlInput.dispatchEvent(new content.Event("input", { bubbles: true }));
      addButton.click();
      await el.updateComplete;

      Assert.ok(
        !shadow.querySelector(".error-message"),
        "A scheme-less host is accepted without an error"
      );
      Assert.deepEqual(
        el.pageUrls,
        ["https://example.com/"],
        "A host with no scheme is watched over https"
      );

      // The same page typed differently canonicalizes to the same URL
      urlInput.value = "HTTPS://EXAMPLE.COM";
      urlInput.dispatchEvent(new content.Event("input", { bubbles: true }));
      addButton.click();
      await el.updateComplete;

      Assert.equal(
        shadow.querySelector(".error-message")?.getAttribute("data-l10n-id"),
        "ai-tasks-alert-error-duplicate-url",
        "The same host in a different case is rejected as a duplicate"
      );
      Assert.deepEqual(
        el.pageUrls,
        ["https://example.com/"],
        "The duplicate is not added again"
      );
    });
  });
});

add_task(async function test_submit_handles_uncommitted_url() {
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: { conditionPresets: [] },
      mode: "create",
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      const setValue = (input, value) => {
        input.value = value;
        input.dispatchEvent(new content.Event("input", { bubbles: true }));
        input.dispatchEvent(new content.Event("change", { bubbles: true }));
      };

      let submitDetail = null;
      el.addEventListener(
        "agent-monitor-item:submit",
        e => (submitDetail = e.detail)
      );

      setValue(
        shadow.querySelector("moz-input-text.monitor-name-input"),
        "Sony Headphone"
      );
      setValue(
        shadow.querySelector("moz-textarea.monitor-condition-input"),
        "the price drops"
      );

      const urlInput = shadow.querySelector("moz-input-url.page-url-input");
      const addButton = shadow.querySelector("moz-button.add-page-btn");
      const startButton = shadow.querySelector(
        'moz-button[data-l10n-id="ai-tasks-alert-create-button"]'
      );

      // A valid page is added, then invalid text is typed but never committed
      // with +, so there's no visible error yet.
      setValue(urlInput, "https://example.com/product");
      addButton.click();
      await el.updateComplete;
      setValue(urlInput, "not a url");

      // Submit must not silently drop the uncommitted input
      startButton.click();
      await el.updateComplete;

      Assert.equal(
        submitDetail,
        null,
        "An uncommitted invalid url blocks submit even with a valid page added"
      );
      Assert.equal(
        shadow.querySelector(".error-message")?.getAttribute("data-l10n-id"),
        "ai-tasks-alert-error-invalid-url",
        "Submit surfaces the uncommitted url's error"
      );
      Assert.ok(
        urlInput.hasAttribute("invalid"),
        "The url input is marked invalid"
      );
      Assert.equal(
        shadow.activeElement,
        urlInput,
        "Focus moves to the url input"
      );

      // Correcting the input to a valid url lets submit commit it
      setValue(urlInput, "https://example.com/second");
      startButton.click();
      await el.updateComplete;

      Assert.deepEqual(
        submitDetail?.watchUrls,
        ["https://example.com/product", "https://example.com/second"],
        "A valid uncommitted url is committed and carried on submit"
      );
    });
  });
});

add_task(async function test_max_watch_urls_from_host() {
  await withTestPage(async browser => {
    // The host owns the cap: about:smartwindowtasks passes the actor's value
    await setProps(browser, {
      agent: { conditionPresets: [] },
      mode: "create",
      maxWatchUrls: 1,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      const setValue = (input, value) => {
        input.value = value;
        input.dispatchEvent(new content.Event("input", { bubbles: true }));
        input.dispatchEvent(new content.Event("change", { bubbles: true }));
      };
      const urlInput = shadow.querySelector("moz-input-url.page-url-input");

      setValue(urlInput, "https://example.com/a");
      shadow.querySelector("moz-button.add-page-btn").click();
      await el.updateComplete;
      Assert.equal(
        shadow.querySelectorAll(".page-pills-row smartwindow-chip").length,
        1,
        "The first URL is added"
      );
      Assert.ok(
        !shadow.querySelector("moz-button.add-page-btn"),
        "The add page button is hidden once the cap is reached"
      );
      Assert.equal(
        shadow.activeElement,
        urlInput,
        "Focus moves to the url input when the add button goes away"
      );

      setValue(urlInput, "https://example.com/b");
      urlInput.dispatchEvent(
        new content.KeyboardEvent("keydown", { key: "Enter", bubbles: true })
      );
      await el.updateComplete;
      Assert.equal(
        shadow.querySelector(".error-message")?.getAttribute("data-l10n-id"),
        "ai-tasks-alert-error-max-urls",
        "Adding past the cap shows the max-pages error"
      );
      Assert.equal(
        shadow.querySelectorAll(".page-pills-row smartwindow-chip").length,
        1,
        "A URL past the host's cap is not added"
      );
      // Assert on substitution rather than copy: the cap has to reach Fluent
      // under the name the string expects, or the message never resolves.
      // data-l10n-id fills text asynchronously, so wait for the cap to appear.
      await ContentTaskUtils.waitForCondition(
        () => shadow.querySelector(".error-message")?.textContent.includes("1"),
        "Waiting for the max URLs error to interpolate the cap"
      );
      const errorText = shadow
        .querySelector(".error-message")
        .textContent.trim();
      Assert.ok(
        errorText.includes("1") && !errorText.includes("{"),
        `The error message interpolates the cap: ${errorText}`
      );

      let submitDetail = null;
      el.addEventListener(
        "agent-monitor-item:submit",
        e => (submitDetail = e.detail)
      );
      setValue(
        shadow.querySelector("moz-input-text.monitor-name-input"),
        "Sony Headphone"
      );
      setValue(
        shadow.querySelector("moz-textarea.monitor-condition-input"),
        "the price drops"
      );
      shadow
        .querySelector(
          'moz-button[data-l10n-id="ai-tasks-alert-create-button"]'
        )
        .click();
      await el.updateComplete;

      Assert.deepEqual(
        submitDetail?.watchUrls,
        ["https://example.com/a"],
        "A url left in the input is ignored on submit once the cap is reached"
      );
    });
  });
});

add_task(async function test_add_page_button_returns_below_cap() {
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: {
        conditionPresets: [],
        watchUrls: ["https://example.com/a", "https://example.com/b"],
      },
      mode: "create",
      maxWatchUrls: 2,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;

      Assert.ok(
        !shadow.querySelector("moz-button.add-page-btn"),
        "The add page button is hidden when seeded at the cap"
      );

      shadow
        .querySelector(".page-pills-row smartwindow-chip")
        .dispatchEvent(
          new content.CustomEvent("smartwindow-chip:remove", { bubbles: true })
        );
      await el.updateComplete;

      Assert.ok(
        shadow.querySelector("moz-button.add-page-btn"),
        "The add page button returns after removing a page"
      );
    });
  });
});

add_task(async function test_history_result_states() {
  const checkedAt = "2026-06-23T13:00:00.000Z";
  const history = [
    {
      id: "h-error",
      checkedAt,
      status: "error",
      resultExplanation: "TypeError: raw internal detail",
      conditionMet: false,
      errorCode: "rate_limit",
    },
    {
      id: "h-met",
      checkedAt,
      status: "success",
      resultExplanation: "The price dropped to $265.",
      conditionMet: true,
    },
    {
      id: "h-not-met",
      checkedAt,
      status: "success",
      resultExplanation: "The price is still $299.",
      conditionMet: false,
    },
  ];

  await withTestPage(async browser => {
    await setProps(browser, {
      agent: { ...AGENT, history },
      mode: "display",
      expanded: true,
      showLastResult: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const shadow = el.shadowRoot;
      const badges = shadow.querySelectorAll(".history-item .condition-badge");

      Assert.deepEqual(
        Array.from(badges, badge => badge.getAttribute("data-l10n-id")),
        [
          "ai-tasks-alert-condition-could-not-check",
          "ai-tasks-alert-condition-met",
          "ai-tasks-alert-condition-not-met",
        ],
        "A failed run reads Couldn't check, alongside the two success states"
      );

      const rows = shadow.querySelectorAll(".history-item");
      Assert.equal(
        rows[0].lastElementChild.getAttribute("data-l10n-id"),
        "ai-tasks-alert-history-error-rate-limit",
        "The note explains the failure the error code names"
      );
      Assert.ok(
        !rows[0].textContent.includes("TypeError"),
        "The raw error message is not shown to the user"
      );
      Assert.equal(
        rows[1].lastElementChild.textContent.trim(),
        "The price dropped to $265.",
        "A successful run still shows the model's explanation"
      );

      Assert.equal(
        shadow
          .querySelector(".monitor-card-head-right .last-result")
          .getAttribute("data-l10n-id"),
        "ai-tasks-alert-last-result-could-not-check",
        "The header chip reports the most recent run's failure too"
      );
    });
  });
});

add_task(async function test_history_error_without_a_code() {
  // Entries stored before error codes existed still have to render.
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: {
        ...AGENT,
        history: [
          {
            id: "h-legacy",
            checkedAt: "2026-06-23T13:00:00.000Z",
            status: "error",
            resultExplanation: "Something went wrong",
            conditionMet: false,
          },
        ],
      },
      mode: "display",
      expanded: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const row = el.shadowRoot.querySelector(".history-item");

      Assert.equal(
        row.querySelector(".condition-badge").getAttribute("data-l10n-id"),
        "ai-tasks-alert-condition-could-not-check",
        "An entry with no error code still reads Couldn't check"
      );
      Assert.equal(
        row.lastElementChild.getAttribute("data-l10n-id"),
        "ai-tasks-alert-history-error-unknown",
        "It falls back to the generic explanation"
      );
    });
  });
});

/**
 * A host that has no room left under its active-monitor limit says so with
 * canResume, and the paused card's Resume button goes disabled rather than
 * letting the user press a button that cannot do anything.
 */
add_task(async function test_resume_disabled_without_room() {
  const PAUSED = { ...AGENT, status: { label: "Paused", kind: "paused" } };
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: PAUSED,
      mode: "display",
      expanded: true,
      canResume: false,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const button = () => el.shadowRoot.getElementById("pause-button");

      Assert.equal(
        button().getAttribute("data-l10n-id"),
        "ai-tasks-alert-resume-button",
        "A paused monitor offers Resume"
      );
      Assert.ok(
        button().disabled,
        "Resume is disabled while the limit leaves no room for it"
      );

      // Pausing one of the others frees a slot up.
      el.canResume = true;
      await el.updateComplete;
      Assert.ok(
        !button().disabled,
        "Resume comes back once there is room under the limit"
      );

      // Pausing is never blocked, whatever the limit says.
      el.canResume = false;
      el.agent = { ...el.agent, status: { label: "Active", kind: "watching" } };
      await el.updateComplete;
      Assert.equal(
        button().getAttribute("data-l10n-id"),
        "ai-tasks-alert-pause-button",
        "An active monitor offers Pause"
      );
      Assert.ok(!button().disabled, "Pause stays available at the limit");
    });
  });
});

/**
 * While the service is refusing checks there is nothing a manual check can do,
 * so the button goes inert and says which refusal it is waiting out. The wait
 * is a deadline rather than a flag: nothing tells the card when it elapses, so
 * the card has to come back on its own.
 */
add_task(async function test_check_now_button_while_checks_unavailable() {
  const cases = [
    ["budget", "budget_exceeded", "ai-tasks-alert-history-error-budget"],
    ["throttle", "rate_limit", "ai-tasks-alert-history-error-rate-limit"],
    [
      "throttle",
      "fastly_rate_limit",
      "ai-tasks-alert-history-error-connection-blocked",
    ],
    [
      "throttle",
      "global_budget_exceeded",
      "ai-tasks-alert-history-error-service-budget",
    ],
  ];
  for (const [reason, errorCode, l10nId] of cases) {
    await withTestPage(async browser => {
      await setProps(browser, {
        agent: {
          ...AGENT,
          checkLimit: { until: Date.now() + 60000, reason, errorCode },
        },
        mode: "display",
        expanded: true,
      });

      await SpecialPowers.spawn(browser, [l10nId], async expectedId => {
        const el = content.document.getElementById("test-agent-monitor-item");
        const button = el.shadowRoot.querySelector("#check-now-button");
        Assert.ok(button.disabled, "The button is disabled");
        Assert.ok(
          button.shadowRoot.querySelector("#main-button").disabled,
          "And so is the inner button, which is what assistive tech sees"
        );
        Assert.equal(
          el.shadowRoot.querySelector("#check-limit-reason")?.dataset.l10nId,
          expectedId,
          "The reason for the refusal is shown as text"
        );

        let asked = false;
        el.addEventListener("agent-monitor-item:check-now", () => {
          asked = true;
        });
        button.click();
        await el.updateComplete;
        Assert.ok(!asked, "Clicking it does not ask for a check");
      });
    });
  }
});

/**
 * A wait that has already elapsed, or none at all, leaves the button alone.
 */
add_task(async function test_check_now_button_when_checks_available() {
  const cases = [
    null,
    { until: Date.now() - 1000, reason: "throttle", errorCode: "rate_limit" },
  ];
  for (const checkLimit of cases) {
    await withTestPage(async browser => {
      await setProps(browser, {
        agent: { ...AGENT, checkLimit },
        mode: "display",
        expanded: true,
      });

      await SpecialPowers.spawn(browser, [], async () => {
        const el = content.document.getElementById("test-agent-monitor-item");
        const button = el.shadowRoot.querySelector("#check-now-button");
        Assert.ok(!button.disabled, "The button is available");
        Assert.equal(
          el.shadowRoot.querySelector("#check-limit-reason"),
          null,
          "No refusal is explained"
        );

        const asked = new Promise(resolve =>
          el.addEventListener("agent-monitor-item:check-now", e =>
            resolve(e.detail.id)
          )
        );
        button.click();
        Assert.equal(await asked, "agent-1", "Clicking it asks for a check");
      });
    });
  }
});

/**
 * The button comes back by itself when the wait elapses, without the host
 * pushing the task again.
 */
add_task(async function test_check_now_button_re_enables_itself() {
  await withTestPage(async browser => {
    await setProps(browser, {
      agent: {
        ...AGENT,
        checkLimit: {
          until: Date.now() + 300,
          reason: "throttle",
          errorCode: "rate_limit",
        },
      },
      mode: "display",
      expanded: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-agent-monitor-item");
      const button = () => el.shadowRoot.querySelector("#check-now-button");
      Assert.ok(button().disabled, "The button starts out unavailable");

      await ContentTaskUtils.waitForCondition(
        () => !button().disabled,
        "The button re-enables once the wait elapses"
      );
      Assert.equal(
        el.shadowRoot.querySelector("#check-limit-reason"),
        null,
        "And stops explaining a refusal that has passed"
      );
    });
  });
});
