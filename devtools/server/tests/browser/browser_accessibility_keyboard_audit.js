/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Checks functionality around keyboard audit for the AccessibleActor.
 */

const {
  accessibility: {
    AUDIT_TYPE: { KEYBOARD },
    SCORES: { FAIL, WARNING },
    ISSUE_TYPE: {
      [KEYBOARD]: {
        FOCUSABLE_NO_SEMANTICS,
        FOCUSABLE_POSITIVE_TABINDEX,
        INTERACTIVE_NOT_FOCUSABLE,
        MOUSE_INTERACTIVE_ONLY,
        NO_FOCUS_VISIBLE,
      },
    },
  },
} = require("resource://devtools/shared/constants.js");

add_task(async function () {
  const { target, walker, parentAccessibility, a11yWalker } =
    await initAccessibilityFrontsForUrl(
      `${MAIN_DOMAIN}doc_accessibility_keyboard_audit.html`
    );

  const tests = [
    [
      "Focusable element (styled button) with no semantics.",
      "#button-1",
      { score: WARNING, issue: FOCUSABLE_NO_SEMANTICS },
    ],
    ["Element (styled button) with no semantics.", "#button-2", null],
    [
      "Container element for out of order focusable element.",
      "#input-container",
      null,
    ],
    [
      "Interactive element with focus out of order (-1).",
      "#input-1",
      {
        score: FAIL,
        issue: INTERACTIVE_NOT_FOCUSABLE,
      },
    ],
    [
      "Interactive element with focus out of order (-1) when disabled.",
      "#input-2",
      null,
    ],
    ["Interactive element when disabled.", "#input-3", null],
    ["Focusable interactive element.", "#input-4", null],
    [
      "Interactive accessible (link with no attributes) with no accessible actions.",
      "#link-1",
      null,
    ],
    ["Interactive accessible (link with valid href).", "#link-2", null],
    ["Interactive accessible (link with # as href).", "#link-3", null],
    [
      "Interactive accessible (link with empty string as href).",
      "#link-4",
      null,
    ],
    ["Interactive accessible with no tabindex.", "#button-3", null],
    [
      "Interactive accessible with -1 tabindex.",
      "#button-4",
      {
        score: FAIL,
        issue: INTERACTIVE_NOT_FOCUSABLE,
      },
    ],
    ["Interactive accessible with 0 tabindex.", "#button-5", null],
    [
      "Interactive accessible with 1 tabindex.",
      "#button-6",
      { score: WARNING, issue: FOCUSABLE_POSITIVE_TABINDEX },
    ],
    [
      "Focusable ARIA button with no focus styling.",
      "#focusable-1",
      { score: WARNING, issue: NO_FOCUS_VISIBLE },
    ],
    ["Focusable ARIA button with focus styling.", "#focusable-2", null],
    ["Focusable ARIA button with browser styling.", "#focusable-3", null],
    [
      "Not focusable, non-semantic element that has a click handler.",
      "#mouse-only-1",
      { score: FAIL, issue: MOUSE_INTERACTIVE_ONLY },
    ],
    [
      "Focusable, non-semantic element that has a click handler.",
      "#focusable-4",
      { score: WARNING, issue: FOCUSABLE_NO_SEMANTICS },
    ],
    [
      "Not focusable, ARIA button that has a click handler.",
      "#button-7",
      { score: FAIL, issue: INTERACTIVE_NOT_FOCUSABLE },
    ],
    ["Focusable, ARIA button with a click handler.", "#button-8", null],
    ["Regular image, no keyboard checks should flag an issue.", "#img-1", null],
    [
      "Image with a longdesc (accessible will have showlongdesc action).",
      "#img-2",
      null,
    ],
    [
      "Clickable image with a longdesc (accessible will have click and showlongdesc actions).",
      "#img-3",
      { score: FAIL, issue: MOUSE_INTERACTIVE_ONLY },
    ],
    [
      "Clickable image (accessible will have click action).",
      "#img-4",
      { score: FAIL, issue: MOUSE_INTERACTIVE_ONLY },
    ],
    ["Focusable button with aria-haspopup.", "#buttonmenu-1", null],
    [
      "Not focusable aria button with aria-haspopup.",
      "#buttonmenu-2",
      {
        score: FAIL,
        issue: INTERACTIVE_NOT_FOCUSABLE,
      },
    ],
    ["Focusable checkbox.", "#checkbox-1", null],
    ["Focusable select element size > 1", "#listbox-1", null],
    ["Focusable select element with one option", "#combobox-1", null],
    ["Focusable select element with no options", "#combobox-2", null],
    ["Focusable select element with two options", "#combobox-3", null],
    [
      "Non-focusable aria combobox with one aria option.",
      "#editcombobox-1",
      null,
    ],
    ["Non-focusable aria combobox with no options.", "#editcombobox-2", null],
    ["Focusable aria combobox with no options.", "#editcombobox-3", null],
    [
      "Non-focusable aria switch",
      "#switch-1",
      {
        score: FAIL,
        issue: INTERACTIVE_NOT_FOCUSABLE,
      },
    ],
    ["Focusable aria switch", "#switch-2", null],
    [
      "Combobox list that is visible (has focusable state)",
      "#owned_listbox",
      null,
    ],
    [
      "Mouse interactive, label that contains form element (linked)",
      "#label-1",
      null,
    ],
    ["Mouse interactive label for external element (linked)", "#label-2", null],
    ["Not interactive unlinked label", "#label-3", null],
    [
      "Not interactive unlinked label with folloing form element",
      "#label-4",
      null,
    ],
    ["Image inside an anchor (href)", "#img-5", null],
    ["Image inside an anchor (onmousedown)", "#img-6", null],
    ["Image inside an anchor (onclick)", "#img-7", null],
    ["Image inside an anchor (onmouseup)", "#img-8", null],
    [
      "Section with a collapse action from aria-expanded attribute",
      "#section-1",
      null,
    ],
    ["Tabindex -1 should not report an element as focusable", "#main", null],
    [
      "Not keyboard focusable element with no focus styling.",
      "#not-keyboard-focusable-1",
      null,
    ],
    ["Interactive grid that is not focusable.", "#grid-1", null],
    [
      "Focusable interactive grid.",
      "#grid-2",
      { score: "WARNING", issue: "FOCUSABLE_NO_SEMANTICS" },
    ],
    [
      "Non interactive ARIA table does not need to be focusable.",
      "#table-1",
      null,
    ],
    [
      "Focusable ARIA table does not have interactive semantics",
      "#table-2",
      { score: "WARNING", issue: "FOCUSABLE_NO_SEMANTICS" },
    ],
    ["Non interactive table does not need to be focusable.", "#table-3", null],
    [
      "Focusable table does not have interactive semantics",
      "#table-4",
      { score: "WARNING", issue: "FOCUSABLE_NO_SEMANTICS" },
    ],
    [
      "Article that is not focusable is not considered interactive",
      "#article-1",
      null,
    ],
    ["Focusable article is considered interactive", "#article-2", null],
    [
      "Column header that is not focusable is not considered interactive (ARIA grid)",
      "#columnheader-1",
      null,
    ],
    [
      "Column header that is not focusable is not considered interactive (ARIA table)",
      "#columnheader-2",
      null,
    ],
    [
      "Column header that is not focusable is not considered interactive (table)",
      "#columnheader-3",
      null,
    ],
    [
      "Column header that is focusable is considered interactive (table)",
      "#columnheader-4",
      null,
    ],
    [
      "Column header that is not focusable is not considered interactive (table as ARIA grid)",
      "#columnheader-5",
      null,
    ],
    [
      "Column header that is focusable is considered interactive (table as ARIA grid)",
      "#columnheader-6",
      null,
    ],
    [
      "Row header that is not focusable is not considered interactive",
      "#rowheader-1",
      null,
    ],
    [
      "Row header that is not focusable is not considered interactive",
      "#rowheader-2",
      null,
    ],
    [
      "Row header that is not focusable is not considered interactive",
      "#rowheader-3",
      null,
    ],
    [
      "Row header that is focusable is considered interactive",
      "#rowheader-4",
      null,
    ],
    [
      "Row header that is not focusable is not considered interactive (table as ARIA grid)",
      "#rowheader-5",
      null,
    ],
    [
      "Row header that is focusable is considered interactive (table as ARIA grid)",
      "#rowheader-6",
      null,
    ],
    [
      "Gridcell that is not focusable is not considered interactive (ARIA grid)",
      "#gridcell-1",
      null,
    ],
    [
      "Gridcell that is focusable is considered interactive (ARIA grid)",
      "#gridcell-2",
      null,
    ],
    [
      "Gridcell that is not focusable is not considered interactive (table as ARIA grid)",
      "#gridcell-3",
      null,
    ],
    [
      "Gridcell that is focusable is considered interactive (table as ARIA grid)",
      "#gridcell-4",
      null,
    ],
    [
      "Tab list that is not focusable is not considered interactive",
      "#tablist-1",
      null,
    ],
    ["Focusable tab list is considered interactive", "#tablist-2", null],
    [
      "Scrollbar that is not focusable is not considered interactive",
      "#scrollbar-1",
      null,
    ],
    ["Focusable scrollbar is considered interactive", "#scrollbar-2", null],
    [
      "Separator that is not focusable is not considered interactive",
      "#separator-1",
      null,
    ],
    ["Focusable separator is considered interactive", "#separator-2", null],
    [
      "Toolbar that is not focusable is not considered interactive",
      "#toolbar-1",
      null,
    ],
    ["Focusable toolbar is considered interactive", "#toolbar-2", null],
    [
      "Menu popup that is not focusable is not considered interactive",
      "#menu-1",
      null,
    ],
    ["Focusable menu popup is considered interactive", "#menu-2", null],
    [
      "Menubar that is not focusable is not considered interactive",
      "#menubar-1",
      null,
    ],
    ["Focusable menubar is considered interactive", "#menubar-2", null],
    ["input type=search is considered interactive", "#input-search", null],
  ];

  for (const [description, selector, expected] of tests) {
    info(description);
    const node = await walker.querySelector(walker.rootNode, selector);
    const front = await a11yWalker.getAccessibleFor(node);
    const audit = await front.audit({ types: [KEYBOARD] });
    Assert.deepEqual(
      audit[KEYBOARD],
      expected,
      `Audit result for ${selector} is correct.`
    );
  }

  info("Text leaf inside a link (jump action is propagated to the text link)");
  let node = await walker.querySelector(walker.rootNode, "#link-5");
  let parent = await a11yWalker.getAccessibleFor(node);
  let front = (await parent.children())[0];
  let audit = await front.audit({ types: [KEYBOARD] });
  Assert.deepEqual(
    audit[KEYBOARD],
    null,
    "Text leafs are excluded from semantics rule."
  );

  info("Combobox list that is invisible");
  node = await walker.querySelector(walker.rootNode, "#combobox-1");
  parent = await a11yWalker.getAccessibleFor(node);
  front = (await parent.children())[0];
  audit = await front.audit({ types: [KEYBOARD] });
  Assert.deepEqual(
    audit[KEYBOARD],
    null,
    "Combobox lists (invisible) are excluded from semantics rule."
  );

  await waitForA11yShutdown(parentAccessibility);
  await target.destroy();
  gBrowser.removeCurrentTab();
});
