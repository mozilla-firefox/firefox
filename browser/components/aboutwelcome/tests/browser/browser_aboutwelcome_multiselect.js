"use strict";

const BASE_CONTENT = {
  id: "MULTI_SELECT_TEST",
  targeting: "true",
  content: {
    position: "split",
    progress_bar: true,
    logo: {},
    tiles: {
      type: "multiselect",
      data: [
        {
          id: "checkbox-1",
          label: {
            raw: "Pin to taskbar",
          },
          action: {
            type: "PIN_FIREFOX_TO_TASKBAR",
          },
        },
        {
          id: "checkbox-2",
          label: {
            raw: "Label for second option",
          },
          description: {
            raw: "Description for second option",
          },
          action: {
            type: "SET_PREF",
            data: {
              pref: {
                name: "test-pref",
                value: true,
              },
            },
          },
        },
      ],
    },
  },
};

const COMBO_CONTENT = {
  id: "MULTI_SELECT_COMBO_TEST",
  targeting: "true",
  content: {
    position: "center-large",
    logo: {},
    title: { raw: "Firefox has your back, starting now" },
    tiles: {
      type: "multiselect",
      data: [
        {
          id: "checkbox-default",
          defaultValue: true,
          label: { raw: "Open all links with Firefox" },
          action: { type: "SET_DEFAULT_BROWSER" },
          uncheckedNotice: {
            title: { raw: "Don't miss out on built-in protection" },
            subtitle: {
              raw: "Firefox automatically stops companies tracking you.",
            },
          },
        },
        {
          id: "checkbox-pin",
          defaultValue: true,
          label: { raw: "Add Firefox to your taskbar" },
          action: { type: "PIN_FIREFOX_TO_TASKBAR" },
          uncheckedNotice: {
            title: { raw: "Keep Firefox a click away" },
            subtitle: {
              raw: "Quickly jump back in to your favorite sites.",
            },
          },
        },
      ],
    },
    primary_button: {
      label: { raw: "Let's go!" },
      action: {
        type: "MULTI_ACTION",
        collectSelect: true,
        navigate: true,
        data: { actions: [] },
      },
    },
  },
};

// The consolidated screen's third tile: the data-collection controls behind a
// link-style disclosure, as one card around the group.
const DISCLOSURE_CONTENT = {
  id: "MULTI_SELECT_DISCLOSURE_TEST",
  targeting: "true",
  content: {
    position: "center-large",
    logo: {},
    title: { raw: "Firefox has your back, starting now" },
    tiles: [
      {
        type: "multiselect",
        multiSelectItemDesign: "select-card",
        data: [
          {
            id: "checkbox-default",
            defaultValue: true,
            label: { raw: "Open all links with Firefox" },
          },
        ],
      },
      {
        type: "text",
        font_styles: "legal",
        text: [
          "By continuing, you agree to the ",
          { raw: "Firefox Terms of Use", link_key: "terms_of_use" },
          ".",
        ],
      },
      {
        type: "multiselect",
        header: {
          title: { raw: "Manage data collection settings" },
          linkStyle: true,
        },
        multiSelectItemDesign: "grouped-card",
        data: [
          {
            id: "interaction-data",
            type: "checkbox",
            defaultValue: true,
            label: { raw: "Send technical and interaction data to Mozilla" },
            description: {
              raw: "Data about your device and how you use Firefox.",
            },
          },
          {
            id: "crash-data",
            type: "checkbox",
            defaultValue: false,
            label: { raw: "Automatically send crash reports" },
            description: {
              raw: "Reports may include personal or sensitive data.",
            },
          },
        ],
      },
    ],
    terms_of_use: {
      action: { type: "OPEN_URL", data: { args: "https://example.com" } },
    },
    primary_button: {
      label: { raw: "Let's go!" },
      action: {
        type: "MULTI_ACTION",
        collectSelect: true,
        navigate: true,
        data: { actions: [] },
      },
    },
  },
};

const PICKER_CONTENT = {
  id: "MULTI_SELECT_TEST",
  targeting: "true",
  content: {
    fullscreen: true,
    position: "split",
    progress_bar: true,
    logo: {},
    tiles: [
      {
        type: "multiselect",
        multiSelectItemDesign: "picker",
        subtitle: { raw: "What are you using Firefox for?" },
        data: [
          {
            id: "checkbox-school",
            defaultValue: false,
            pickerEmoji: "🎓",
            pickerEmojiBackgroundColor: "#c3e0ff",
            label: {
              raw: "School",
            },
            checkedAction: {
              type: "SET_PREF",
              data: {
                pref: {
                  name: "onboarding-personalization.school",
                  value: true,
                },
              },
            },
            uncheckedAction: {
              type: "SET_PREF",
              data: {
                pref: {
                  name: "onboarding-personalization.school",
                  value: false,
                },
              },
            },
          },
        ],
      },
    ],
  },
};

/**
 * Core multiselect functionality is covered in
 * browser_aboutwelcome_multistage_mr.js
 */

/**
 * Test the consolidated Terms of Use / pin / default screen shape: multiselect
 * items that reveal a notice while they are unchecked.
 */
add_task(async function test_multiselect_unchecked_notices() {
  const TEST_JSON = JSON.stringify([COMBO_CONTENT]);
  let browser = await openAboutWelcome(TEST_JSON);

  await test_screen_content(
    browser,
    "renders both notice live regions, empty while the items are checked",
    // Expected selectors:
    [
      `.multi-select-item-group input#checkbox-default:checked`,
      `.multi-select-item-group input#checkbox-pin:checked`,
      `.multi-select-notice-region[role="status"]`,
    ],
    // Unexpected selectors
    [`.multi-select-notice`]
  );

  await onButtonClick(browser, "input#checkbox-default");

  await test_screen_content(
    browser,
    "reveals the notice for the unchecked item only",
    // Expected selectors:
    [
      `.multi-select-notice`,
      `.multi-select-notice-title`,
      `.multi-select-notice-subtitle`,
    ],
    // Unexpected selectors
    [`input#checkbox-default:checked`]
  );

  // Exactly one notice is shown, since only one item is unchecked.
  await SpecialPowers.spawn(browser, [], async () => {
    is(
      content.document.querySelectorAll(".multi-select-notice").length,
      1,
      "Only the unchecked item shows a notice"
    );
    is(
      content.document.querySelectorAll(".multi-select-notice-region").length,
      2,
      "Both live regions stay in the DOM"
    );
  });

  // The notice is a compact strip: a plain inline icon, not a full-height rail.
  await test_element_styles(browser, ".multi-select-notice-icon", {
    width: "16px",
    height: "16px",
    "background-color": "rgba(0, 0, 0, 0)",
  });

  await onButtonClick(browser, "input#checkbox-default");

  await test_screen_content(
    browser,
    "hides the notice again once the item is re-checked",
    // Expected selectors:
    [`.multi-select-notice-region[role="status"]`],
    // Unexpected selectors
    [`.multi-select-notice`]
  );
});

/**
 * Test rendering a screen with the MultiSelect checklist including an item with
 * a description.
 */
add_task(async function test_multiselect_with_item_description() {
  const TEST_JSON = JSON.stringify([BASE_CONTENT]);
  let browser = await openAboutWelcome(TEST_JSON);

  await test_screen_content(
    browser,
    "renders screen with a checklist item with no description and an item with a description",
    // Expected selectors:
    [
      // Both items have labels
      `.multi-select-container .multi-select-item:first-of-type label`,
      `.multi-select-container .multi-select-item:last-of-type label`,
      // Second item has input label and description linked to input
      `.multi-select-container .multi-select-item:last-of-type input[aria-describedby="checkbox-2-description"]`,
      `.multi-select-container .multi-select-item:last-of-type input[aria-labelledby="checkbox-2-label"]`,
      `.multi-select-container .multi-select-item:last-of-type p#checkbox-2-description`,
      `.multi-select-container .multi-select-item:last-of-type label#checkbox-2-label`,
    ],
    // Unexpected selectors
    [
      // First item has no description paragraph or aria-describedby attribute
      `.multi-select-container .multi-select-item:first-of-type p`,
      `.multi-select-container .multi-select-item:first-of-type input[aria-describedby*="-description"]`,
      `.multi-select-container .multi-select-item:first-of-type input[aria-labelledby*="-label"]`,
    ]
  );
});

/**
 * Test the "select-card" item design: full-width rows with the checkbox on the
 * inline-end edge, achieved without changing DOM order.
 */
add_task(async function test_card_multiselect_design() {
  const CARD_CONTENT = structuredClone(COMBO_CONTENT);
  CARD_CONTENT.id = "MULTI_SELECT_CARD_TEST";
  CARD_CONTENT.content.tiles.multiSelectItemDesign = "select-card";
  const TEST_JSON = JSON.stringify([CARD_CONTENT]);
  let browser = await openAboutWelcome(TEST_JSON);

  await test_screen_content(
    browser,
    "renders the multiselect with the select-card design",
    // Expected selectors:
    [
      `.multi-select-container.select-card`,
      `.multi-select-container.select-card .checkbox-container.multi-select-item`,
    ]
  );

  // Assert the resulting layout rather than the mechanism that produces it, so
  // the test survives a change of technique in the stylesheet.
  //
  // The input still precedes its label in the DOM, so the label association
  // and focus order are unchanged; only the visual order is flipped.
  await SpecialPowers.spawn(browser, [], async () => {
    const item = content.document.querySelector(
      ".multi-select-container.select-card .checkbox-container.multi-select-item"
    );
    const input = item.querySelector("input");
    const label = item.querySelector("label");
    const FOLLOWING = content.Node.DOCUMENT_POSITION_FOLLOWING;
    is(
      input.compareDocumentPosition(label) & FOLLOWING,
      FOLLOWING,
      "Input still comes before its label in the DOM"
    );
    is(
      label.getAttribute("for"),
      input.id,
      "Label is still associated with the input"
    );
    // Checkbox paints to the inline-end of the label.
    Assert.greater(
      input.getBoundingClientRect().left,
      label.getBoundingClientRect().left,
      "Checkbox renders after the label visually"
    );
    // Rows span the full width of the container.
    const container = content.document.querySelector(
      ".multi-select-container.select-card"
    );
    is(
      Math.round(item.getBoundingClientRect().width),
      Math.round(container.getBoundingClientRect().width),
      "Card row spans the full container width"
    );
  });
});

/**
 * Test the data-collection disclosure: a link-style collapsible header that
 * reveals the two data-collection checkboxes as one grouped card.
 */
add_task(async function test_link_style_disclosure() {
  const TEST_JSON = JSON.stringify([DISCLOSURE_CONTENT]);
  let browser = await openAboutWelcome(TEST_JSON);

  await test_screen_content(
    browser,
    "renders the disclosure collapsed, with the legal paragraph above it",
    // Expected selectors:
    [
      `.content-tile .legal-paragraph`,
      `.content-tile .legal-paragraph a[value="terms_of_use"]`,
      `button.tile-header.link-style[aria-expanded="false"]`,
      `button.tile-header.link-style .arrow-icon`,
    ],
    // Unexpected selectors
    [
      `.multi-select-container.grouped-card`,
      // A link-style header drops the button chrome, so it must not also
      // render the external-link affordance a `link` tile uses.
      `button.tile-header.link-style .external-link-icon`,
    ]
  );

  await onButtonClick(browser, "button.tile-header.link-style");

  await test_screen_content(
    browser,
    "reveals both data-collection checkboxes in one grouped card",
    // Expected selectors:
    [
      `button.tile-header.link-style[aria-expanded="true"]`,
      `.multi-select-container.grouped-card`,
      `.multi-select-container.grouped-card input#interaction-data:checked`,
      `.multi-select-container.grouped-card p#interaction-data-description`,
      `.multi-select-container.grouped-card input[aria-describedby="interaction-data-description"]`,
      `.multi-select-container.grouped-card p#crash-data-description`,
    ],
    // Unexpected selectors
    [
      // Crash reporting is opt-in, so it must not arrive pre-checked.
      `.multi-select-container.grouped-card input#crash-data:checked`,
    ]
  );

  // Assert the resulting layout rather than the mechanism that produces it, so
  // the test survives a change of technique in the stylesheet.
  await SpecialPowers.spawn(browser, [], async () => {
    const container = content.document.querySelector(
      ".multi-select-container.grouped-card"
    );
    const items = container.querySelectorAll(
      ".checkbox-container.multi-select-item"
    );
    is(items.length, 2, "Both items are inside the one card");

    const [item] = items;
    const input = item.querySelector("input");
    const label = item.querySelector("label");
    const description = item.querySelector("p");

    // Description sits beneath its label, with the checkbox to the inline-end
    // of both and vertically centred across them.
    Assert.greater(
      description.getBoundingClientRect().top,
      label.getBoundingClientRect().top,
      "Description renders below the label"
    );
    Assert.greater(
      input.getBoundingClientRect().left,
      description.getBoundingClientRect().right,
      "Checkbox renders after the description text"
    );
    const inputMid =
      input.getBoundingClientRect().top +
      input.getBoundingClientRect().height / 2;
    const rowRect = item.getBoundingClientRect();
    Assert.less(
      Math.abs(inputMid - (rowRect.top + rowRect.height / 2)),
      2,
      "Checkbox is centred across the label and description"
    );

    // The single card wraps the group, so the items themselves are unfilled.
    const cardBackground = content.getComputedStyle(container).backgroundColor;
    isnot(cardBackground, "rgba(0, 0, 0, 0)", "The group is a filled card");
    is(
      content.getComputedStyle(item).backgroundColor,
      "rgba(0, 0, 0, 0)",
      "Individual rows are not separately filled"
    );
  });

  // Everything stacked in the content column shares one pair of inline edges:
  // the cards, the legal paragraph and the action buttons below them.
  await SpecialPowers.spawn(browser, [], async () => {
    const edges = selector => {
      const r = content.document
        .querySelector(selector)
        .getBoundingClientRect();
      return [Math.round(r.left), Math.round(r.right)];
    };
    const tiles = edges("#content-tiles-container");
    for (const selector of [
      ".multi-select-container.select-card .checkbox-container.multi-select-item",
      ".multi-select-container.grouped-card",
      ".legal-paragraph",
      ".action-buttons",
    ]) {
      Assert.deepEqual(
        edges(selector),
        tiles,
        `${selector} lines up with the tiles container`
      );
    }
  });

  // Legal copy is small but full strength: it carries the terms being agreed
  // to, so it must not be rendered as deemphasized supporting text.
  await SpecialPowers.spawn(browser, [], async () => {
    const legal = content.document.querySelector(".legal-paragraph");
    const style = content.getComputedStyle(legal);
    is(style.fontSize, "13px", "Legal copy uses the small font size");
    is(
      style.color,
      content.getComputedStyle(
        content.document.querySelector(".onboardingContainer")
      ).color,
      "Legal copy is not deemphasized"
    );

    is(
      content.getComputedStyle(
        content.document.querySelector("button.tile-header.link-style")
      ).textDecorationLine,
      "underline",
      "The disclosure is underlined, so it reads as a link"
    );
  });

  await onButtonClick(browser, "button.tile-header.link-style");

  await test_screen_content(
    browser,
    "collapses the disclosure again",
    // Expected selectors:
    [`button.tile-header.link-style[aria-expanded="false"]`],
    // Unexpected selectors
    [`.multi-select-container.grouped-card`]
  );
});

/**
 * Test multiselect styles with picker configuration
 */
add_task(async function test_picker_multiselect_styles() {
  const TEST_JSON = JSON.stringify([PICKER_CONTENT]);
  let browser = await openAboutWelcome(TEST_JSON);

  await test_screen_content(
    browser,
    "renders screen with a picker checklist item",
    // Expected selectors:
    [
      // multiselect container has picker class
      `.multi-select-container.picker`,
      // Checkbox container should have role, tabindex, aria-checked properties
      `.checkbox-container[role="checkbox"]`,
      `.checkbox-container[tabIndex="0"]`,
      `.checkbox-container[aria-checked="false"]`,
    ],
    // Unexpected selectors
    [
      // Hidden input should be unchecked
      `input[type="checkbox"]:checked`,
    ]
  );

  // Hidden input should indeed be hidden
  await test_element_styles(browser, ".checkbox-container input", {
    width: "0px",
    height: "0px",
    opacity: "0",
  });

  // Picker icon background color should match passed value
  await test_element_styles(browser, ".picker-icon", {
    backgroundColor: "rgb(195, 224, 255)",
  });
});
