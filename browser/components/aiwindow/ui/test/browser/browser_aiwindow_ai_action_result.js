/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_PAGE =
  "chrome://mochitests/content/browser/browser/components/aiwindow/ui/test/browser/test_ai_action_result_page.html";

async function openTestPage() {
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.customElements.whenDefined("ai-action-result");
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
    const el = content.document.getElementById("test-action-result");
    Object.assign(el, properties);
    await el.updateComplete;
  });
}

add_task(async function test_label_and_summary_render() {
  await withTestPage(async browser => {
    await setProps(browser, {
      label: "Closed tabs",
      summary: "I closed any open tabs about NYC hotels.",
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-result");
      const shadow = el.shadowRoot;

      Assert.equal(
        shadow.querySelector(".action-result-label").textContent.trim(),
        "Closed tabs",
        "Label text should match the label property"
      );
      Assert.equal(
        shadow.querySelector(".action-result-summary").textContent.trim(),
        "I closed any open tabs about NYC hotels.",
        "Summary text should match the summary property"
      );
    });
  });
});

add_task(async function test_toggle_expand_collapse() {
  await withTestPage(async browser => {
    await setProps(browser, {
      label: "Closed tabs",
      summary: "I closed any open tabs.",
      isExpanded: false,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-result");
      const shadow = el.shadowRoot;

      Assert.ok(
        !shadow.querySelector(".action-result-expanded"),
        "Expanded section should not be present when collapsed"
      );

      const header = shadow.querySelector(".action-result-header");
      header.getBoundingClientRect();
      header.click();
      await el.updateComplete;

      Assert.ok(
        shadow.querySelector(".action-result-expanded"),
        "Expanded section should appear after clicking header"
      );
      Assert.equal(
        el.isExpanded,
        true,
        "isExpanded should be true after toggle"
      );

      header.getBoundingClientRect();
      header.click();
      await el.updateComplete;

      Assert.ok(
        !shadow.querySelector(".action-result-expanded"),
        "Expanded section should be removed after second click"
      );
      Assert.equal(
        el.isExpanded,
        false,
        "isExpanded should be false after second toggle"
      );
    });
  });
});

add_task(async function test_items_render_when_expanded() {
  await withTestPage(async browser => {
    const items = [
      { url: "https://nychotels.com", label: "NYC Hotels", iconSrc: "" },
      { url: "https://booking.com", label: "Booking NYC", iconSrc: "" },
    ];

    await setProps(browser, {
      label: "Closed tabs",
      summary: "I closed any open tabs about NYC hotels.",
      rows: [{ label: "Closed tabs", items }],
      isExpanded: true,
    });

    await SpecialPowers.spawn(browser, [items], async expectedItems => {
      const shadow =
        content.document.getElementById("test-action-result").shadowRoot;

      const container = shadow.querySelector("website-chip-container");
      Assert.ok(
        container,
        "website-chip-container should be present when expanded"
      );
      Assert.deepEqual(
        container.websites,
        expectedItems,
        "website-chip-container should receive the items array"
      );
    });
  });
});

add_task(async function test_row_label_renders() {
  await withTestPage(async browser => {
    await setProps(browser, {
      label: "Closed 3 tabs",
      rows: [{ label: "Closed tabs", items: [] }],
      isExpanded: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const shadow =
        content.document.getElementById("test-action-result").shadowRoot;

      Assert.equal(
        shadow
          .querySelector(".action-result-expanded-row-label")
          .textContent.trim(),
        "Closed tabs",
        "Expanded row label should display the row's label"
      );
    });
  });
});

add_task(async function test_l10n_attributes_render() {
  await withTestPage(async browser => {
    await setProps(browser, {
      labelL10nId: "smart-window-closed-tabs-label",
      labelL10nArgs: { count: 3 },
      summaryL10nId: "smart-window-closed-tabs-summary",
      summaryL10nArgs: { count: 3 },
      rows: [
        {
          labelL10nId: "smart-window-restored-row-label",
          labelL10nArgs: { count: 2 },
          items: [],
        },
      ],
      isExpanded: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-result");
      const shadow = el.shadowRoot;

      // Check main label L10n attributes
      const label = shadow.querySelector(".action-result-label");
      Assert.equal(
        label.getAttribute("data-l10n-id"),
        "smart-window-closed-tabs-label",
        "Label should have correct data-l10n-id"
      );
      Assert.equal(
        label.getAttribute("data-l10n-args"),
        '{"count":3}',
        "Label should have correct data-l10n-args"
      );

      // Check summary L10n attributes
      const summary = shadow.querySelector(".action-result-summary");
      Assert.equal(
        summary.getAttribute("data-l10n-id"),
        "smart-window-closed-tabs-summary",
        "Summary should have correct data-l10n-id"
      );
      Assert.equal(
        summary.getAttribute("data-l10n-args"),
        '{"count":3}',
        "Summary should have correct data-l10n-args"
      );

      // Check row label L10n attributes
      const rowLabel = shadow.querySelector(
        ".action-result-expanded-row-label"
      );
      Assert.equal(
        rowLabel.getAttribute("data-l10n-id"),
        "smart-window-restored-row-label",
        "Row label should have correct data-l10n-id"
      );
      Assert.equal(
        rowLabel.getAttribute("data-l10n-args"),
        '{"count":2}',
        "Row label should have correct data-l10n-args"
      );
    });
  });
});

add_task(async function test_mixed_l10n_and_plain_strings() {
  await withTestPage(async browser => {
    // Mix L10n and plain strings to ensure both work
    await setProps(browser, {
      labelL10nId: "smart-window-closed-tabs-label",
      labelL10nArgs: { count: 1 },
      summary: "This is a plain text summary", // Plain string
      isExpanded: true,
      rows: [
        { label: "Plain text row", items: [] }, // Plain string
        {
          labelL10nId: "smart-window-restored-row-label",
          labelL10nArgs: { count: 1 },
          items: [],
        },
      ],
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const shadow =
        content.document.getElementById("test-action-result").shadowRoot;

      // L10n label should have attribute
      Assert.ok(
        shadow
          .querySelector(".action-result-label")
          .hasAttribute("data-l10n-id"),
        "Label with L10n ID should have data-l10n-id attribute"
      );

      // Plain summary should not have L10n attribute
      Assert.ok(
        !shadow
          .querySelector(".action-result-summary")
          .hasAttribute("data-l10n-id"),
        "Plain text summary should not have data-l10n-id attribute"
      );

      const rowLabels = shadow.querySelectorAll(
        ".action-result-expanded-row-label"
      );
      // First row should be plain text
      Assert.ok(
        !rowLabels[0].hasAttribute("data-l10n-id"),
        "Plain text row should not have data-l10n-id"
      );
      // Second row should have L10n
      Assert.ok(
        rowLabels[1].hasAttribute("data-l10n-id"),
        "L10n row should have data-l10n-id"
      );
    });
  });
});

add_task(async function test_label_links_render_and_do_not_toggle() {
  const HREF = "https://support.mozilla.org/kb/smart-window-exa";
  const link = { l10nName: "exa-link", href: HREF };

  await withTestPage(async browser => {
    // Render the Exa link in both places at once: the header label and an
    // expanded row.
    await setProps(browser, {
      labelL10nId: "action-log-searching-web-with-exa",
      labelLink: link,
      isExpanded: true,
      rows: [
        { labelL10nId: "action-log-searched-web-with-exa", link, items: [] },
      ],
    });

    await SpecialPowers.spawn(browser, [HREF], async href => {
      const el = content.document.getElementById("test-action-result");
      const shadow = el.shadowRoot;
      const anchors = shadow.querySelectorAll("a.action-result-label-link");

      Assert.equal(anchors.length, 2, "Header and row each render a link");
      for (const anchor of anchors) {
        Assert.equal(anchor.getAttribute("data-l10n-name"), "exa-link");
        Assert.equal(anchor.getAttribute("href"), href);
        Assert.equal(
          anchor.getAttribute("target"),
          "_blank",
          "opens a new tab"
        );
      }

      // The header anchor is inside the toggle button; clicking it must not
      // toggle the card. Cancel the native target=_blank navigation first so
      // the test doesn't open a real tab.
      content.document.addEventListener("click", e => e.preventDefault(), {
        capture: true,
        once: true,
      });
      shadow.querySelector(".action-result-label a").click();
      Assert.ok(el.isExpanded, "Clicking the link does not toggle the card");
    });
  });
});

add_task(async function test_linked_pending_label_completes_and_expands() {
  const link = {
    l10nName: "exa-link",
    href: "https://support.mozilla.org/kb/smart-window-exa",
  };

  await withTestPage(async browser => {
    await setProps(browser, {
      labelL10nId: "action-log-searching-web-with-exa",
      labelLink: link,
      isLoading: true,
      rows: [
        { labelL10nId: "action-log-searched-web-with-exa", link, items: [] },
      ],
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-result");
      const shadow = el.shadowRoot;
      await ContentTaskUtils.waitForCondition(
        () =>
          shadow.querySelector(".action-result-label").textContent.trim() ==
          "Searching the web with Exa",
        "Pending label is translated with its link"
      );

      el.isLoading = false;
      el.labelL10nId = "action-log-completed-steps";
      el.labelL10nArgs = { count: 1 };
      el.labelLink = null;
      await ContentTaskUtils.waitForCondition(
        () =>
          shadow.querySelector(".action-result-label").textContent.trim() ==
          "Completed 1 step",
        "Completed label replaces the linked pending label"
      );

      shadow.querySelector(".action-result-header").click();
      await el.updateComplete;
      await ContentTaskUtils.waitForCondition(
        () =>
          shadow
            .querySelector(".action-result-expanded-row-label")
            ?.textContent.trim() == "Searched the web with Exa",
        "Expanded row renders after the linked label completes"
      );
    });
  });
});

add_task(async function test_exa_answered_row_renders_with_link() {
  const HREF = "https://support.mozilla.org/kb/smart-window-exa";
  const link = { l10nName: "exa-link", href: HREF };

  await withTestPage(async browser => {
    await setProps(browser, {
      labelL10nId: "action-log-completed-steps",
      labelL10nArgs: { count: 1 },
      isExpanded: true,
      rows: [
        {
          labelL10nId: "action-log-searched-and-answered-with-exa",
          link,
          items: [],
        },
      ],
    });

    await SpecialPowers.spawn(browser, [HREF], async href => {
      const shadow =
        content.document.getElementById("test-action-result").shadowRoot;
      const rowLabel = shadow.querySelector(
        ".action-result-expanded-row-label"
      );
      await ContentTaskUtils.waitForCondition(
        () => rowLabel.textContent.trim() == "Searched and answered with Exa",
        "The row says Exa wrote the reply"
      );
      const anchor = rowLabel.querySelector("a.action-result-label-link");
      Assert.equal(anchor?.textContent, "Exa", "Exa is the linked word");
      Assert.equal(
        anchor?.getAttribute("href"),
        href,
        "Exa links to the support article"
      );
    });
  });
});

add_task(async function test_toggle_dispatches_event() {
  await withTestPage(async browser => {
    await setProps(browser, { label: "Closed tabs", isExpanded: false });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-result");
      const shadow = el.shadowRoot;

      const events = [];
      el.addEventListener("action-result-toggle", e =>
        events.push(e.detail?.isExpanded)
      );

      const header = shadow.querySelector(".action-result-header");
      header.getBoundingClientRect();
      header.click();
      await el.updateComplete;
      header.getBoundingClientRect();
      header.click();
      await el.updateComplete;

      Assert.deepEqual(
        events,
        [true, false],
        "action-result-toggle should fire on each click with the new isExpanded value"
      );
    });
  });
});

add_task(async function test_loading_state_shimmer_and_swap() {
  // Force reduced motion so the completion transition is deterministic: the
  // shimmer loop is disabled and the scripted sweep short-circuits (no running
  // animation to finish), so the swap to the completed label happens promptly
  // instead of waiting on animation/timer callbacks that can stall in CI.
  await SpecialPowers.pushPrefEnv({ set: [["ui.prefersReducedMotion", 1]] });

  await withTestPage(async browser => {
    // Enter the loading state with a pending label.
    await setProps(browser, { label: "Searching tabs", isLoading: true });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-result");
      const shadow = el.shadowRoot;

      Assert.ok(
        el.hasAttribute("is-loading"),
        "is-loading is reflected while loading"
      );
      Assert.ok(
        el.hasAttribute("shimmering"),
        "shimmering treatment is applied while loading"
      );
      Assert.equal(
        shadow.querySelector(".action-result-label").textContent.trim(),
        "Searching tabs",
        "The pending label is shown while loading"
      );
      // The expand chevron stays visible while loading so the card can be
      // expanded mid-flight to reveal the in-progress steps.
      Assert.notEqual(
        content.getComputedStyle(
          shadow.querySelector(".action-result-header"),
          "::after"
        ).display,
        "none",
        "The expand chevron is shown while loading"
      );
    });

    // Completing swaps the label to the completed text.
    await setProps(browser, { label: "Completed 2 steps", isLoading: false });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-result");
      const shadow = el.shadowRoot;

      await ContentTaskUtils.waitForCondition(
        () => !el.hasAttribute("shimmering"),
        "shimmering clears once the action completes"
      );

      Assert.ok(
        !el.hasAttribute("is-loading"),
        "is-loading is removed once completed"
      );
      Assert.equal(
        shadow.querySelector(".action-result-label").textContent.trim(),
        "Completed 2 steps",
        "The label swaps to the completed text"
      );
      // The chevron returns once the card is interactive again.
      Assert.notEqual(
        content.getComputedStyle(
          shadow.querySelector(".action-result-header"),
          "::after"
        ).display,
        "none",
        "The expand chevron is shown again once completed"
      );
    });
  });
});

add_task(async function test_expand_while_loading() {
  await withTestPage(async browser => {
    await setProps(browser, {
      label: "Searching the web with Exa",
      rows: [{ label: "Searching the web with Exa", items: [] }],
      isLoading: true,
    });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.getElementById("test-action-result");
      const shadow = el.shadowRoot;

      Assert.ok(
        el.hasAttribute("shimmering"),
        "Card is shimmering while loading"
      );
      Assert.ok(
        !shadow.querySelector(".action-result-expanded"),
        "Not expanded initially"
      );

      const header = shadow.querySelector(".action-result-header");
      header.getBoundingClientRect();
      header.click();
      await el.updateComplete;

      Assert.ok(
        shadow.querySelector(".action-result-expanded"),
        "The card can be expanded while still loading"
      );
    });
  });
});
