/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests feedback and dismissal acknowledgments in the view.
 */

"use strict";

// See the comment in the setup task for the expected index of the main test
// result.
const RESULT_INDEX = 2;

// The command that dismisses a single result.
const DISMISS_ONE_COMMAND = "dismiss-one";

// The command that dismisses all results of a particular type.
const DISMISS_ALL_COMMAND = "dismiss-all";

// The command that removes a result without acknowledging the dismissal.
const REMOVE_COMMAND = "remove";

// The name of this command must be one that's recognized as not ending the
// urlbar session. See `isSessionOngoing` comments for details.
const FEEDBACK_COMMAND = "show_less_frequently";

let gTestProvider;

add_setup(async function () {
  // This test expects the following results in the following order:
  //
  // 1. The heuristic
  // 2. A history result
  // 3. A result from our test provider. This will be the main result we'll use
  //    during this test.
  // 4. Another history result
  //
  // This ensures a couple things:
  //
  // * The main test result has rows above and below it. Feedback and dismissal
  //   acknowledgments in the main result row should not affect adjacent rows,
  //   except that when the dismissal acknowledgment itself is dismissed, it
  //   should be replaced by the row below it.
  // * The main result does not have a row label (a.k.a. group label). There's a
  //   separate task that specifically checks the row label, and that way this
  //   test covers both cases, where the row does and does not have a row label.
  gTestProvider = new TestProvider({
    // This ensures the result is sandwiched between the two history
    // results in the Firefox Suggest group.
    results: [makeResult({ suggestedIndex: 1 })],
  });

  gTestProvider.commandCount = {};
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  providersManager.registerProvider(gTestProvider);

  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
  await UrlbarTestUtils.formHistory.clear();

  // Add visits for the two history results.
  await PlacesTestUtils.addVisits([
    "https://example.com/aaa",
    "https://example.com/bbb",
  ]);

  registerCleanupFunction(() => {
    providersManager.unregisterProvider(gTestProvider);
  });
});

// Tests dismissal acknowledgment when the dismissed row is not selected.
add_task(async function acknowledgeDismissal_rowNotSelected() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
  });
  await doDismissTest({
    command: DISMISS_ONE_COMMAND,
    shouldBeSelected: false,
  });
});

// Tests dismissal acknowledgment when the dismissed row is selected.
add_task(async function acknowledgeDismissal_rowSelected() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
  });

  // Select the row.
  gURLBar.view.selectedRowIndex = RESULT_INDEX;

  await doDismissTest({
    command: DISMISS_ONE_COMMAND,
    shouldBeSelected: true,
  });
});

// Tests a feedback acknowledgment command immediately followed by a dismissal
// acknowledgment command. This makes sure that both feedback acknowledgment
// works and a subsequent dismissal command works while the urlbar session
// remains ongoing.
add_task(async function acknowledgeFeedbackAndDismissal() {
  // Trigger the suggestion.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
  });

  let details = await UrlbarTestUtils.getDetailsOfResultAt(
    window,
    RESULT_INDEX
  );

  // Click the feedback command.
  await UrlbarTestUtils.openResultMenuAndClickItem(window, FEEDBACK_COMMAND, {
    resultIndex: RESULT_INDEX,
  });

  // The feedback acknowledgment is applied to the row content-side; on the
  // message path it arrives asynchronously over the actor. Immediate in-process.
  await BrowserTestUtils.waitForMutationCondition(
    details.element.row,
    { attributes: true, attributeFilter: ["feedback-acknowledgment"] },
    () => details.element.row.hasAttribute("feedback-acknowledgment")
  );

  Assert.equal(
    gTestProvider.commandCount[FEEDBACK_COMMAND],
    1,
    "One feedback command should have happened"
  );
  gTestProvider.commandCount[FEEDBACK_COMMAND] = 0;

  Assert.ok(
    gURLBar.view.isOpen,
    "The view should remain open clicking the command"
  );
  Assert.ok(
    details.element.row.hasAttribute("feedback-acknowledgment"),
    "Row should have feedback acknowledgment after clicking command"
  );

  info("Doing dismissal");
  await doDismissTest({
    command: DISMISS_ONE_COMMAND,
    shouldBeSelected: true,
  });
});

// Tests dismissal of all results of a particular type.
add_task(async function acknowledgeDismissal_all() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
  });
  await doDismissTest({
    command: DISMISS_ALL_COMMAND,
    shouldBeSelected: false,
  });
});

// When a row with a row label (a.k.a. group label) is dismissed, the dismissal
// acknowledgment tip should retain the label. When the tip is then dismissed,
// the row that replaces it should also retain the label.
add_task(async function acknowledgeDismissal_rowLabel() {
  // Show the result as the first row in the Firefox Suggest section so that it
  // has the "Firefox Suggest" group label.
  gTestProvider.results[0] = makeResult({ suggestedIndex: 0 });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
  });
  await doDismissTest({
    resultIndex: 1,
    command: DISMISS_ALL_COMMAND,
    shouldBeSelected: false,
    expectedLabelOnOriginalRow: "Firefox Suggest",
  });
});

// When a row with `hideRowLabel` set is dismissed, the dismissal acknowledgment
// tip should not have a row label.
add_task(async function acknowledgeDismissal_hideRowLabel() {
  // Show the result as the first row in the Firefox Suggest section so that it
  // has the "Firefox Suggest" group label.
  gTestProvider.results[0] = makeResult({ suggestedIndex: 0 });

  // Make sure the label is visible.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
  });
  await checkRowLabel(1, "Firefox Suggest");
  await UrlbarTestUtils.promisePopupClose(window);

  // Now hide the row label and dismiss the result.
  gTestProvider.results[0] = makeResult({
    suggestedIndex: 0,
    hideRowLabel: true,
  });
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
  });

  await checkRowLabel(1, null);
  await checkRowLabel(2, "Firefox Suggest");

  await doDismissTest({
    resultIndex: 1,
    command: DISMISS_ALL_COMMAND,
    shouldBeSelected: false,
    expectedLabelOnOriginalRow: null,
    expectedLabelOnReplacementRow: "Firefox Suggest",
  });
});

// Dismissing the only result leaves the acknowledgment tip as the only row, and
// dismissing the tip in turn closes the view.
add_task(async function acknowledgeDismissal_lastResult() {
  await promiseLoneResultPopup();

  let promiseRemoved = UrlbarTestUtils.promiseControllerNotification(
    window,
    "onQueryResultRemoved"
  );
  await UrlbarTestUtils.openResultMenuAndClickItem(
    window,
    DISMISS_ONE_COMMAND,
    {
      resultIndex: 0,
      openByMouse: true,
    }
  );
  await promiseRemoved;

  Assert.ok(gURLBar.view.isOpen, "The view stays open for the tip");
  Assert.equal(UrlbarTestUtils.getResultCount(window), 1, "One row remains");
  let details = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(
    details.result.payload.type,
    "dismissalAcknowledgment",
    "The remaining row is the acknowledgment tip"
  );

  let gotItButton = UrlbarTestUtils.getButtonForResultIndex(window, "0", 0);
  await UrlbarTestUtils.promisePopupClose(window, () =>
    EventUtils.synthesizeMouseAtCenter(gotItButton, {}, window)
  );
  Assert.ok(!gURLBar.hasAttribute("popover-open"), "The popover is gone");
  Assert.ok(gURLBar.focused, "The input keeps focus");
});

// Removing the only result without an acknowledgment closes the view.
add_task(async function remove_lastResult() {
  await promiseLoneResultPopup();

  await UrlbarTestUtils.promisePopupClose(window, () =>
    UrlbarTestUtils.openResultMenuAndClickItem(window, REMOVE_COMMAND, {
      resultIndex: 0,
      openByMouse: true,
    })
  );
  Assert.ok(!gURLBar.hasAttribute("popover-open"), "The popover is gone");
  Assert.ok(gURLBar.focused, "The input keeps focus");
});

// Opens the view with the test result as its only row, by making the test
// provider suppress every other provider for that query.
async function promiseLoneResultPopup() {
  gTestProvider.results[0] = makeResult({
    isSuggestedIndexRelativeToGroup: false,
  });
  gTestProvider.priority = 1;
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
  });
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    1,
    "The test result is the only row"
  );
}

/**
 * Does a dismissal test:
 *
 * 1. Clicks a dismiss command in the test result
 * 2. Verifies a dismissal acknowledgment tip replaces the result
 * 3. Clicks the "Got it" button in the tip
 * 4. Verifies the tip is dismissed
 *
 * @param {object} options
 *   Options object
 * @param {string} options.command
 *   One of: DISMISS_ONE_COMMAND, DISMISS_ALL_COMMAND
 * @param {boolean} options.shouldBeSelected
 *   True if the test result is expected to be selected initially. If true, this
 *   function verifies the "Got it" button in the dismissal acknowledgment tip
 *   also becomes selected.
 * @param {number} options.resultIndex
 *   The index of the test result, if known beforehand. Leave -1 to find it
 *   automatically.
 * @param {string} options.expectedLabelOnOriginalRow
 *   The row label (a.k.a. group label) both the original row and the
 *   acknowledgment tip are expected to have. This should be the expected
 *   translated en-US string, not an l10n object. If null, the row is expected
 *   not to have a row label at all.
 * @param {string} options.expectedLabelOnReplacementRow
 *   The expected label of the row that replaces the acknowledgment tip when the
 *   tip's "Got it" button is clicked.
 */
async function doDismissTest({
  command,
  shouldBeSelected,
  resultIndex = 2,
  expectedLabelOnOriginalRow = null,
  expectedLabelOnReplacementRow = expectedLabelOnOriginalRow,
}) {
  let details = await UrlbarTestUtils.getDetailsOfResultAt(window, resultIndex);
  Assert.equal(
    details.result.providerName,
    gTestProvider.name,
    "The test result should be at the expected index"
  );
  if (details.result.providerName != gTestProvider.name) {
    return;
  }

  let selectedElement = gURLBar.view.selectedElement;
  Assert.ok(selectedElement, "There should be an initially selected element");

  if (shouldBeSelected) {
    Assert.equal(
      gURLBar.view.selectedRowIndex,
      resultIndex,
      "The test result should be selected"
    );
  } else {
    Assert.notEqual(
      gURLBar.view.selectedRowIndex,
      resultIndex,
      "The test result should not be selected"
    );
  }

  info("Checking the row label on the original row");
  await checkRowLabel(resultIndex, expectedLabelOnOriginalRow);

  let resultCount = UrlbarTestUtils.getResultCount(window);

  // On the message path the dismissal round-trips over the actor (the
  // engagement runs parent-side, calls removeResult, and notifies back), so the
  // row updates asynchronously. Await the removal notification; it fires
  // synchronously on the in-process path.
  let promiseRemoved = UrlbarTestUtils.promiseControllerNotification(
    window,
    "onQueryResultRemoved"
  );

  // Click the command.
  await UrlbarTestUtils.openResultMenuAndClickItem(window, command, {
    resultIndex,
    openByMouse: true,
  });

  await promiseRemoved;

  Assert.equal(
    gTestProvider.commandCount[command],
    1,
    "One dismissal should have happened"
  );
  gTestProvider.commandCount[command] = 0;

  // The row should be a tip now.
  Assert.ok(gURLBar.view.isOpen, "The view should remain open after dismissal");
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    resultCount,
    "The result count should not haved changed after dismissal"
  );

  details = await UrlbarTestUtils.getDetailsOfResultAt(window, resultIndex);
  Assert.equal(
    details.type,
    UrlbarShared.RESULT_TYPE.TIP,
    "Row should be a tip after dismissal"
  );
  Assert.equal(
    details.result.payload.type,
    "dismissalAcknowledgment",
    "Tip type should be dismissalAcknowledgment"
  );
  Assert.equal(
    details.displayed.title,
    command == DISMISS_ONE_COMMAND
      ? "Thanks for your feedback. You won’t see this suggestion again."
      : "Thanks for your feedback. You won’t see these suggestions anymore.",
    "Tip text should be correct for the dismiss type"
  );
  Assert.ok(
    !details.element.row.hasAttribute("selected"),
    "Row should not have 'selected' attribute"
  );
  Assert.ok(
    !details.element.row._content.hasAttribute("selected"),
    "Row-inner should not have 'selected' attribute"
  );
  Assert.ok(
    !details.element.row.hasAttribute("feedback-acknowledgment"),
    "Row should not have feedback acknowledgment after dismissal"
  );

  info("Checking the row label on the dismissal acknowledgment tip");
  await checkRowLabel(resultIndex, expectedLabelOnOriginalRow);

  // Get the dismissal acknowledgment's "Got it" button.
  let gotItButton = UrlbarTestUtils.getButtonForResultIndex(
    window,
    "0",
    resultIndex
  );
  Assert.ok(gotItButton, "Row should have a 'Got it' button");

  if (shouldBeSelected) {
    Assert.equal(
      gURLBar.view.selectedElement,
      gotItButton,
      "The 'Got it' button should be selected"
    );
  } else {
    Assert.notEqual(
      gURLBar.view.selectedElement,
      gotItButton,
      "The 'Got it' button should not be selected"
    );
    Assert.equal(
      gURLBar.view.selectedElement,
      selectedElement,
      "The initially selected element should remain selected"
    );
  }

  // Click it.
  EventUtils.synthesizeMouseAtCenter(gotItButton, {}, window);

  // The view should remain open and the tip row should be gone.
  Assert.ok(
    gURLBar.view.isOpen,
    "The view should remain open clicking the 'Got it' button"
  );
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    resultCount - 1,
    "The result count should be one less after clicking 'Got it' button"
  );
  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.ok(
      details.type != UrlbarShared.RESULT_TYPE.TIP &&
        details.result.providerName != gTestProvider.name,
      "Tip result and test result should not be present"
    );
  }

  info(
    "Checking the row label on the row that replaced the dismissal acknowledgment tip"
  );
  await checkRowLabel(resultIndex, expectedLabelOnReplacementRow);

  await UrlbarTestUtils.promisePopupClose(window);
}

/**
 * A provider that acknowledges feedback and dismissals.
 */
class TestProvider extends UrlbarTestUtils.TestProvider {
  getResultCommands(_result) {
    // The l10n values aren't important.
    return [
      {
        name: FEEDBACK_COMMAND,
        l10n: {
          id: "urlbar-result-menu-report-inaccurate-location2",
        },
      },
      {
        name: DISMISS_ONE_COMMAND,
        l10n: {
          id: "firefox-suggest-command-not-interested2",
        },
      },
      {
        name: DISMISS_ALL_COMMAND,
        l10n: {
          id: "firefox-suggest-command-not-interested2",
        },
      },
      {
        name: REMOVE_COMMAND,
        l10n: {
          id: "urlbar-result-menu-remove-from-history2",
        },
      },
    ];
  }

  onEngagement(queryContext, controller, details) {
    if (details.result?.providerName == this.name) {
      let { selType } = details;

      info(`onEngagement called, selType=` + selType);

      if (!this.commandCount.hasOwnProperty(selType)) {
        this.commandCount[selType] = 0;
      }
      this.commandCount[selType]++;

      switch (selType) {
        case FEEDBACK_COMMAND:
          controller.view.acknowledgeFeedback(details.result.id);
          break;
        case DISMISS_ONE_COMMAND:
          controller.removeResult(details.result, {
            acknowledgeDismissalL10n: {
              id: "firefox-suggest-dismissal-acknowledgment-one",
            },
          });
          break;
        case DISMISS_ALL_COMMAND:
          controller.removeResult(details.result, {
            acknowledgeDismissalL10n: {
              id: "urlbar-result-dismissal-acknowledgment-all",
            },
          });
          break;
        case REMOVE_COMMAND:
          controller.removeResult(details.result);
          break;
      }
    }
  }
}

async function checkRowLabel(resultIndex, expectedLabel) {
  let details = await UrlbarTestUtils.getDetailsOfResultAt(window, resultIndex);
  let { row } = details.element;
  let before = getComputedStyle(row, "::before");

  if (expectedLabel) {
    Assert.equal(
      before.content,
      `"${expectedLabel}"`,
      "::before content should use the row label"
    );
    Assert.equal(
      row.getAttribute("label"),
      expectedLabel,
      "Row should have the expected label attribute"
    );
  } else {
    Assert.equal(before.content, "none", "::before content should be 'none'");
    Assert.ok(
      !row.hasAttribute("label"),
      "Row should not have a label attribute"
    );
  }
}

function makeResult(resultParams) {
  return new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.URL,
    source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
    payload: {
      url: "https://example.com/",
      isBlockable: true,
    },
    isSuggestedIndexRelativeToGroup: true,
    ...resultParams,
  });
}
