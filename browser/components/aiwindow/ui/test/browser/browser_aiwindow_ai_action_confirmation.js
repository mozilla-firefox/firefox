/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_PAGE =
  "chrome://mochitests/content/browser/browser/components/aiwindow/ui/test/browser/test_ai_action_confirmation_page.html";

function makeTabs(tabCount) {
  return Array.from({ length: tabCount }, (_, index) => ({
    url: `https://example.com/${index + 1}`,
    title: `Example tab ${index + 1}`,
    iconSrc: "",
  }));
}

async function withTestPage(fn) {
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.customElements.whenDefined("ai-action-confirmation");
    await content.customElements.whenDefined("ai-action-confirmation-tab-row");
  });
  try {
    await fn(tab.linkedBrowser);
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
}

async function setProps(browser, props) {
  await SpecialPowers.spawn(browser, [props], async properties => {
    const el = content.document.getElementById("test-action-confirmation");
    Object.assign(el, properties);
    await el.updateComplete;
  });
}

add_task(async function test_label_localized_text_renders() {
  await withTestPage(async browser => {
    await setProps(browser, {
      labelL10nId: "smart-window-closed-tabs-label",
      labelL10nArgs: { count: 2 },
      items: makeTabs(2),
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-confirmation");
      const label = el.shadowRoot.querySelector(".action-confirmation-label");
      await content.document.l10n.translateFragment(label);
      Assert.equal(
        label.textContent.trim(),
        "Closed 2 tabs",
        "renders the localized summary"
      );
    });
  });
});

add_task(async function test_l10n_label_attributes() {
  await withTestPage(async browser => {
    await setProps(browser, {
      labelL10nId: "smart-window-closed-tabs-label",
      labelL10nArgs: { count: 3 },
      items: makeTabs(2),
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const label = content.document
        .getElementById("test-action-confirmation")
        .shadowRoot.querySelector(".action-confirmation-label");
      Assert.equal(
        label.getAttribute("data-l10n-id"),
        "smart-window-closed-tabs-label",
        "has the summary l10n id"
      );
      Assert.equal(
        label.getAttribute("data-l10n-args"),
        '{"count":3}',
        "has the summary l10n args"
      );
    });
  });
});

add_task(async function test_toggle_expand_collapse() {
  await withTestPage(async browser => {
    await setProps(browser, {
      labelL10nId: "smart-window-closed-tabs-label",
      labelL10nArgs: { count: 2 },
      items: makeTabs(2),
      isExpanded: false,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-confirmation");
      const shadow = el.shadowRoot;

      Assert.ok(
        !shadow.querySelector(".action-confirmation-items"),
        "list hidden when collapsed"
      );

      const summary = shadow.querySelector(".action-confirmation-summary");
      summary.click();
      await el.updateComplete;

      Assert.ok(
        shadow.querySelector(".action-confirmation-items"),
        "list shown after expanding"
      );
      Assert.equal(el.isExpanded, true, "isExpanded true after expand");

      summary.click();
      await el.updateComplete;

      Assert.ok(
        !shadow.querySelector(".action-confirmation-items"),
        "list hidden after collapsing"
      );
      Assert.equal(el.isExpanded, false, "isExpanded false after collapse");
    });
  });
});

add_task(async function test_toggle_dispatches_event() {
  await withTestPage(async browser => {
    await setProps(browser, {
      labelL10nId: "smart-window-closed-tabs-label",
      labelL10nArgs: { count: 2 },
      items: makeTabs(2),
      isExpanded: false,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-confirmation");
      const shadow = el.shadowRoot;

      const events = [];
      el.addEventListener("action-confirmation-toggle", e =>
        events.push(e.detail.isExpanded)
      );

      const summary = shadow.querySelector(".action-confirmation-summary");
      summary.click();
      await el.updateComplete;
      summary.click();
      await el.updateComplete;

      Assert.deepEqual(
        events,
        [true, false],
        "toggle fires with the new isExpanded value"
      );
    });
  });
});

add_task(async function test_tabs_render_when_expanded() {
  await withTestPage(async browser => {
    const tabs = makeTabs(2);
    await setProps(browser, {
      labelL10nId: "smart-window-closed-tabs-label",
      labelL10nArgs: { count: tabs.length },
      items: tabs,
      isExpanded: true,
    });

    await SpecialPowers.spawn(browser, [tabs], async expectedTabs => {
      const shadow = content.document.getElementById(
        "test-action-confirmation"
      ).shadowRoot;
      const rows = shadow.querySelectorAll("ai-action-confirmation-tab-row");
      Assert.equal(
        rows.length,
        expectedTabs.length,
        "one row per affected tab"
      );
      Assert.equal(
        rows[0].shadowRoot.querySelector(".tab-row-label").textContent,
        expectedTabs[0].title,
        "first row shows the tab title"
      );
    });
  });
});

add_task(async function test_no_expand_without_tabs() {
  await withTestPage(async browser => {
    await setProps(browser, {
      labelL10nId: "smart-window-closed-tabs-label",
      labelL10nArgs: { count: 0 },
      items: [],
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-confirmation");
      const shadow = el.shadowRoot;

      const summary = shadow.querySelector(".action-confirmation-summary");
      Assert.ok(summary.disabled, "header disabled with no tabs");
      summary.click();
      await el.updateComplete;

      Assert.equal(el.isExpanded, false, "does not expand with no tabs");
    });
  });
});

add_task(async function test_tab_click_opens_link() {
  await withTestPage(async browser => {
    const tabs = makeTabs(2);
    await setProps(browser, {
      labelL10nId: "smart-window-closed-tabs-label",
      labelL10nArgs: { count: tabs.length },
      items: tabs,
      isExpanded: true,
    });

    await SpecialPowers.spawn(browser, [tabs], async expectedTabs => {
      const el = content.document.getElementById("test-action-confirmation");
      const row = el.shadowRoot
        .querySelector("ai-action-confirmation-tab-row")
        .shadowRoot.querySelector(".tab-row");

      const events = [];
      el.addEventListener("AIChatContent:OpenLink", e => events.push(e.detail));

      row.click();
      row.dispatchEvent(
        new content.PointerEvent("click", {
          bubbles: true,
          cancelable: true,
          ctrlKey: true,
        })
      );
      await el.updateComplete;

      Assert.equal(events.length, 2, "each click requests a link open");
      Assert.equal(events[0].url, expectedTabs[0].url, "requests the row url");
      Assert.ok(
        events[0].preferSwitchToTab,
        "a plain click switches to an already open tab"
      );
      Assert.ok(
        !events[1].preferSwitchToTab,
        "a modified click opens a new tab instead"
      );
    });
  });
});

add_task(async function test_undo_visibility_and_event() {
  await withTestPage(async browser => {
    await setProps(browser, {
      labelL10nId: "smart-window-closed-tabs-label",
      labelL10nArgs: { count: 2 },
      items: makeTabs(2),
      canUndo: false,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const shadow = content.document.getElementById(
        "test-action-confirmation"
      ).shadowRoot;
      Assert.ok(
        !shadow.querySelector(".action-confirmation-undo"),
        "undo hidden when canUndo is false"
      );
    });

    await setProps(browser, { canUndo: true });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-confirmation");
      const shadow = el.shadowRoot;
      const undo = shadow.querySelector(".action-confirmation-undo");
      Assert.ok(undo, "undo shown when canUndo is true");

      let undoFired = false;
      el.addEventListener("action-confirmation-undo", () => {
        undoFired = true;
      });
      undo.click();
      await el.updateComplete;

      Assert.ok(undoFired, "undo fires the undo event");
    });
  });
});

add_task(async function test_marks_overflowing_list() {
  await withTestPage(async browser => {
    const manyTabs = makeTabs(20);
    await setProps(browser, {
      labelL10nId: "smart-window-closed-tabs-label",
      labelL10nArgs: { count: manyTabs.length },
      items: manyTabs,
      isExpanded: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-confirmation");
      const list = el.shadowRoot.querySelector(".action-confirmation-items");
      const scroller = list.parentElement;

      const waitForOverflow = expected =>
        ContentTaskUtils.waitForMutationCondition(
          scroller,
          { attributes: true, attributeFilter: ["data-overflowing"] },
          () => scroller.hasAttribute("data-overflowing") === expected
        );

      list.style.maxHeight = "none";
      await waitForOverflow(false);
      Assert.ok(
        !scroller.hasAttribute("data-overflowing"),
        "not overflowing when it fits"
      );

      list.style.maxHeight = "10px";
      await waitForOverflow(true);
      Assert.ok(
        scroller.hasAttribute("data-overflowing"),
        "overflowing when taller than its box"
      );
    });
  });
});
