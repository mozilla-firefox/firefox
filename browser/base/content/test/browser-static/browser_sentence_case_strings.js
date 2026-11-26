/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test file checks that our en-US builds use sentence case strings
 * where appropriate. It's not exhaustive - some panels will show different
 * items in different states, and this test doesn't iterate all of them.
 */

/* global PanelUI */

const { CustomizableUITestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/CustomizableUITestUtils.sys.mjs"
);

const { AppMenuNotifications } = ChromeUtils.importESModule(
  "resource://gre/modules/AppMenuNotifications.sys.mjs"
);

// These are brand names, proper names, or other things that we expect to
// not abide exactly to sentence case. NAMES is for single words, and PHRASES
// is for words in a specific order.
const NAMES = new Set(["Mozilla", "Nightly", "Firefox", "AI"]);
const PHRASES = new Set([
  "Extensions and Themes",
  "Extensions for Developers",
  "Find in Page…",
  "Help and Report",
  "Share Ideas and Feedback…",
  "Troubleshoot Mode…",
  "Create a New Profile",
]);

let gCUITestUtils = new CustomizableUITestUtils(window);
let gLocalization = new Localization(["browser/newtab/asrouter.ftl"], true);

/**
 * This recursive function will take the current main or subview, find all of
 * the buttons that navigate to subviews inside it, and click each one
 * individually. Upon entering the new view, we recurse. When the subviews
 * within a view have been exhausted, we go back up a level.
 *
 * @generator
 * @param {<xul:panelview>} parentView The view to start scanning for
 *        subviews.
 * @yields {<xul:panelview>} Each found <xul:panelview>, in depth-first search
 *         order.
 */
async function* iterateSubviews(parentView) {
  let navButtons = Array.from(
    // Ensure that only enabled buttons are tested
    parentView.querySelectorAll(
      ".subviewbutton-nav:not([disabled]):not([hidden])"
    )
  );
  if (!navButtons) {
    return;
  }

  for (let button of navButtons) {
    info("Click " + button.id);
    let panel = parentView.closest("panel");
    let panelmultiview = parentView.closest("panelmultiview");
    let promiseViewShown = BrowserTestUtils.waitForEvent(panel, "ViewShown");
    button.click();
    let viewShownEvent = await promiseViewShown;

    yield viewShownEvent.originalTarget;

    info("Shown " + viewShownEvent.originalTarget.id);
    yield* iterateSubviews(viewShownEvent.originalTarget);
    promiseViewShown = BrowserTestUtils.waitForEvent(parentView, "ViewShown");
    panelmultiview.goBack();
    await promiseViewShown;
  }
}

/**
 * Given a <xul:panelview>, look for <xul:toolbarbutton> descendants, extract
 * any relevant strings from them, and check to see if they are in sentence
 * case. By default, labels, textContent, and toolTipText (including dynamic
 * toolTipText) are checked.
 *
 * @param {<xul:panelview>} view The <xul:panelview> to check.
 */
function checkToolbarButtons(view) {
  let toolbarbuttons = view.querySelectorAll("toolbarbutton");
  info("Checking toolbarbuttons in subview with id " + view.id);

  for (let toolbarbutton of toolbarbuttons) {
    // Concatenated title + description creates a false positive.
    // checkUpdateBanner handles the update banner separately.
    if (toolbarbutton.id === "appMenu-update-banner") {
      continue;
    }
    let strings = [
      toolbarbutton.label,
      toolbarbutton.textContent,
      toolbarbutton.toolTipText,
      DynamicShortcutTooltip.getText(toolbarbutton.id),
    ];
    info("Checking toolbarbutton " + toolbarbutton.id);
    for (let string of strings) {
      checkSentenceCase(string, toolbarbutton.id);
    }
  }
}

function checkSubheaders(view) {
  let subheaders = view.querySelectorAll("h2");
  info("Checking subheaders in subview with id " + view.id);

  for (let subheader of subheaders) {
    checkSentenceCase(subheader.textContent, subheader.id);
  }
}

async function checkUpdateBanner(view) {
  let banner = view.querySelector("#appMenu-update-banner");

  const notifications = [
    "update-downloading",
    "update-available",
    "update-manual",
    "update-unsupported",
    "update-restart",
  ];

  for (const notification of notifications) {
    // Forcibly remove the label in order to wait for the new label.
    banner.removeAttribute("label");

    let labelPromise = BrowserTestUtils.waitForMutationCondition(
      banner,
      { attributes: true, attributeFilter: ["label"] },
      () => !!banner.getAttribute("label")
    );

    AppMenuNotifications.showNotification(notification);

    await labelPromise;

    checkSentenceCase(banner.label, banner.id);

    AppMenuNotifications.removeNotification(/.*/);
  }
}

/**
 * Asserts whether or not a string matches sentence case.
 *
 * @param {string} string The string to check for sentence case.
 * @param {string} elementID The ID of the element being tested. This is
 *        mainly used for the assertion message to make it easier to debug
 *        failures, but items without IDs will not be checked (as these are
 *        likely using dynamic strings, like bookmarked page titles).
 */
function checkSentenceCase(string, elementID) {
  if (!string || !elementID) {
    return;
  }

  info("Testing string: " + string);

  let words = string.trim().split(/\s+/);

  // A string in title case (every word capitalized) is also acceptable.
  if (words.every(word => hasExpectedCapitalization(word, true))) {
    Assert.ok(
      true,
      `${string} for ${elementID} should have sentence or title casing.`
    );
    return;
  }

  // We expect that the first word is always capitalized. If it isn't,
  // there's no need to keep checking the rest of the string, since we're
  // going to fail the assertion.
  let result = hasExpectedCapitalization(words[0], true);
  if (result) {
    for (let wordIndex = 1; wordIndex < words.length; ++wordIndex) {
      let word = words[wordIndex];

      if (word) {
        if (isPartOfPhrase(words, wordIndex)) {
          // Skip capitalization check for words that are part of a phrase
          // The phrase defines the correct capitalization - needed to make it
          // work with phrases such as Find in Page with lowercase fillers such as in
          result = true;
        } else {
          let isName = NAMES.has(word);
          result = hasExpectedCapitalization(word, isName);
        }
        if (!result) {
          break;
        }
      }
    }
  }

  Assert.ok(result, `${string} for ${elementID} should have sentence casing.`);
}

/**
 * Returns true if a word is part of a phrase defined in the PHRASES set.
 * The function will see if the word is contained within any of the defined
 * PHRASES, and will then scan back and forward within the words array to
 * to see if the word is indeed part of the phrase in context.
 *
 * @param {Array} words The full array of words being checked by the caller.
 * @param {number} wordIndex The index of the word being checked within the
 *        words array.
 * @return {boolean}
 */
function isPartOfPhrase(words, wordIndex) {
  let word = words[wordIndex];

  info(`Checking if ${word} is part of a phrase`);

  for (let phrase of PHRASES) {
    let phraseFragments = phrase.split(" ");
    let fragmentIndex = phraseFragments.indexOf(word);

    // If we didn't find the word within this phrase, the candidate phrase
    // has more words than what we're analyzing, or the word doesn't have
    // enough words before it to match the candidate phrase, then move on.
    if (
      fragmentIndex == -1 ||
      words.length - phraseFragments.length < 0 ||
      fragmentIndex > wordIndex
    ) {
      continue;
    }

    let wordsSlice = words.slice(
      wordIndex - fragmentIndex,
      wordIndex + phraseFragments.length
    );
    let matches = wordsSlice.every((w, index) => {
      return phraseFragments[index] === w;
    });

    if (matches) {
      info(`${word} is part of phrase ${phrase}`);
      return true;
    }
  }

  return false;
}

/**
 * Tests that the strings under the AppMenu are in sentence case.
 */
add_task(async function test_sentence_case_appmenu() {
  // Some of these panels are lazy, so it's necessary to open them in
  // order for them to be inserted into the DOM.
  await gCUITestUtils.openMainMenu();
  registerCleanupFunction(async () => {
    await gCUITestUtils.hideMainMenu();
  });

  checkToolbarButtons(PanelUI.mainView);
  checkSubheaders(PanelUI.mainView);

  for await (const view of iterateSubviews(PanelUI.mainView)) {
    checkToolbarButtons(view);
    checkSubheaders(view);
  }

  await checkUpdateBanner(PanelUI.mainView);
});

/**
 * Tests that the strings under the All Tabs panel are in sentence case.
 */
add_task(async function test_sentence_case_all_tabs_panel() {
  gTabsPanel.init();

  const allTabsView = document.getElementById("allTabsMenu-allTabsView");
  let allTabsPopupShownPromise = BrowserTestUtils.waitForEvent(
    allTabsView,
    "ViewShown"
  );
  gTabsPanel.showAllTabsPanel();
  await allTabsPopupShownPromise;

  registerCleanupFunction(async () => {
    let allTabsPopupHiddenPromise = BrowserTestUtils.waitForEvent(
      allTabsView.panelMultiView,
      "PanelMultiViewHidden"
    );
    gTabsPanel.hideAllTabsPanel();
    await allTabsPopupHiddenPromise;
  });

  checkToolbarButtons(gTabsPanel.allTabsView);
  checkSubheaders(gTabsPanel.allTabsView);

  for await (const view of iterateSubviews(gTabsPanel.allTabsView)) {
    checkToolbarButtons(view);
    checkSubheaders(view);
  }
});
