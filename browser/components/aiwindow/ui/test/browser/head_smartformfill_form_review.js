/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head.js */
/* import-globals-from head_smartformfill_autocomplete.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_smartformfill_autocomplete.js",
  this
);

const { FORM_REVIEW_ERRORS, FORM_REVIEW_STATES } = ChromeUtils.importESModule(
  "chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs"
);
const { NonPrivateTabs } = ChromeUtils.importESModule(
  "resource:///modules/OpenTabs.sys.mjs"
);

/**
 * @typedef {object} FormReviewTestContext
 * @property {string} originalRegion - Region to restore during cleanup.
 * @property {MockEngineManager} mockEngineManager - Manager for mocked model
 *   requests.
 * @property {Window|null} win - AI Window used by the test.
 * @property {boolean} prefEnvPushed - Whether the test pref was pushed.
 */

/**
 * @typedef {object} FormReviewModelRequest
 * @property {{response_format: {json_schema: {name: string}}}}
 *   inferenceParams - Structured-response configuration for the request.
 * @property {Array<{role: string, content: *}>} args - Messages sent to the
 *   model.
 */

/**
 * @typedef {object} FormReviewFieldRequestData
 * @property {Array<{id: string, inputType: string}>} fields - Detected form
 *   fields included in a classification or value-generation request.
 */

/**
 * @typedef {object} FormReviewTabsRequestData
 * @property {Array<{id: string, url: string}>} tabs - Open tabs included in a
 *   relevant-tabs request.
 */

/**
 * @typedef {object} FormReviewSnapshot
 * @property {string} state - Current form review state.
 * @property {string|null} errorType - Current error result, if any.
 * @property {number|null} filledFieldCount - Number of fields filled, if known.
 * @property {Array<object>} fields - Fields currently held by the component.
 * @property {string[]} l10nIds - Localization IDs rendered by the component.
 * @property {boolean|null} fillButtonDisabled - Fill button state, if rendered.
 */

/**
 * @typedef {object} FormReviewInputSnapshot
 * @property {string} label - Label displayed for the input.
 * @property {string} value - Current editable input value.
 */

/**
 * @typedef {object} FormReviewGeneratedFieldInput
 * @property {string} action - Model-selected action for the field.
 * @property {string} value - Value generated for the field.
 * @property {string} confidence - Model confidence in the generated value.
 */

/**
 * Responds to a field-classification or relevant-tabs model request.
 *
 * @param {string} schemaName - Schema identifying the metadata request.
 * @param {FormReviewFieldRequestData|FormReviewTabsRequestData} requestData -
 *   Parsed request data.
 * @param {(response: string) => void} respond - Resolves the model request.
 * @returns {void}
 */
function respondToFormReviewMetadataRequest(schemaName, requestData, respond) {
  switch (schemaName) {
    case FIELD_CLASSIFICATION_SCHEMA:
      respond(
        JSON.stringify({
          fields: requestData.fields.map(field => ({
            id: field.id,
            type: field.inputType,
            confidence: "high",
          })),
        })
      );
      return;

    case RELEVANT_TABS_SCHEMA: {
      const sourceToken = new UrlTokenizer().encodeToken(SOURCE_URL);
      const source = requestData.tabs.find(tab => tab.url === sourceToken);
      respond(
        JSON.stringify({
          selectedTabs: source
            ? [
                {
                  id: source.id,
                  relevance: "high",
                  reason: "Test source",
                },
              ]
            : [],
        })
      );
      return;
    }
  }

  throw new Error(`Unexpected Smart Form Fill metadata schema: ${schemaName}`);
}

/**
 * Resolves pending per-form metadata requests.
 *
 * @param {MockEngineManager} mockEngineManager - Manager containing pending
 *   model requests.
 * @returns {void}
 */
function respondToPendingFormReviewMetadataRequests(mockEngineManager) {
  const engine = mockEngineManager.engines.get(SMART_FORM_FILL_MODEL_PURPOSE);
  if (!engine) {
    return;
  }

  for (const [requestId, { request }] of [...engine.runRequests]) {
    const schemaName = getModelRequestSchema(request);
    if (
      schemaName !== FIELD_CLASSIFICATION_SCHEMA &&
      schemaName !== RELEVANT_TABS_SCHEMA
    ) {
      continue;
    }

    respondToFormReviewMetadataRequest(
      schemaName,
      getModelRequestData(request),
      response => engine.respond(requestId, response)
    );
  }
}

/**
 * Captures the next request for a schema, resolving intervening metadata
 * requests required by the production flow.
 *
 * @param {MockEngineManager} mockEngineManager - Manager used to capture the
 *   request.
 * @param {string} expectedSchema - Schema of the request to return.
 * @returns {Promise<{request: FormReviewModelRequest, respond: (response: string) => void}>}
 *   Captured request and its response callback.
 */
async function captureFormReviewRequest(mockEngineManager, expectedSchema) {
  while (true) {
    const captured = await mockEngineManager.captureRequest({
      purpose: SMART_FORM_FILL_MODEL_PURPOSE,
    });
    const schemaName = getModelRequestSchema(captured.request);

    if (schemaName === expectedSchema) {
      return captured;
    }

    if (
      schemaName !== FIELD_CLASSIFICATION_SCHEMA &&
      schemaName !== RELEVANT_TABS_SCHEMA
    ) {
      throw new Error(
        `Expected Smart Form Fill schema ${expectedSchema}, got ${schemaName}`
      );
    }

    respondToFormReviewMetadataRequest(
      schemaName,
      getModelRequestData(captured.request),
      captured.respond
    );
  }
}

/**
 * Focuses a form field and returns its Smart Form Fill autocomplete result.
 *
 * @param {Window} win - AI Window containing the form tab.
 * @param {string} selector - Selector for the form field to focus.
 * @param {MockEngineManager} mockEngineManager - Manager containing metadata
 *   requests started by focusing the field.
 * @returns {Promise<{browser: MozBrowser, item: Element}>} Form browser and
 *   Smart Form Fill autocomplete item.
 */
async function getFormReviewAutocomplete(win, selector, mockEngineManager) {
  const browser = win.gBrowser.selectedBrowser;
  const popup = browser.autoCompletePopup;

  await SimpleTest.promiseFocus(browser);
  await SpecialPowers.spawn(browser, [selector], fieldSelector => {
    content.document.querySelector(fieldSelector).focus();
  });

  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
  await TestUtils.waitForCondition(
    () => popup.popupOpen,
    "Waiting for the autocomplete popup to open"
  );

  let item = popup.querySelector('[originaltype="smartFormFill"]');
  if (!item) {
    item = await BrowserTestUtils.waitForMutationCondition(
      popup.richlistbox,
      {
        childList: true,
        subtree: true,
      },
      () => popup.querySelector('[originaltype="smartFormFill"]')
    );
  }

  let row;
  await TestUtils.waitForCondition(() => {
    respondToPendingFormReviewMetadataRequests(mockEngineManager);
    item = popup.querySelector('[originaltype="smartFormFill"]');
    row = item?.querySelector("autocomplete-row-item");
    return row && !row.loading;
  }, "Waiting for Smart Form Fill metadata and autocomplete refresh");

  await row.updateComplete;

  return { browser, item };
}

function waitForFormReviewTabChange(sourceEvent) {
  return BrowserTestUtils.waitForEvent(
    NonPrivateTabs,
    "TabChange",
    false,
    event => event.detail.sourceEvents.includes(sourceEvent)
  );
}

/**
 * Opens a foreground tab and waits for the open tab list to have caught up
 * with it.
 *
 * The list reports an open in batches, the last of which trails the tab's
 * title. A batch that lands later takes the source editor away the way a tab
 * the user opened would, so every tab a test opens is waited out up to that
 * last batch.
 *
 * @param {Window} win - AI Window to open the tab in.
 * @param {string} url - Page to open.
 * @returns {Promise<MozTabbrowserTab>} The opened tab.
 */
async function openTabAndWaitForTabList(win, url) {
  const tabListUpdated = waitForFormReviewTabChange("TabAttrModified");
  const tab = await BrowserTestUtils.openNewForegroundTab(win.gBrowser, url);
  await tabListUpdated;

  return tab;
}

/**
 * Creates an AI Window, source tab, and form tab ready for a review test.
 *
 * @returns {Promise<FormReviewTestContext>} Initialized test context.
 */
async function setupFormReviewTest() {
  const context = {
    originalRegion: Region.home,
    mockEngineManager: new MockEngineManager(),
    win: null,
    prefEnvPushed: false,
  };

  try {
    Region._setHomeRegion("US", false);

    await SpecialPowers.pushPrefEnv({
      set: [
        [SMART_FORM_FILL_PREF, true],
        // The test forms are smaller than the minimum the feature ships with.
        [MIN_FORM_FIELDS_PREF, 1],
      ],
    });
    context.prefEnvPushed = true;

    _setRemoteClientForTesting({
      get: async () => [
        ...MOCK_RS_RECORDS,
        ...SMART_FORM_FILL_REMOTE_SETTINGS_RECORDS,
      ],
    });

    context.win = await openAIWindow();

    await openTabAndWaitForTabList(context.win, SOURCE_URL);
    await openTabAndWaitForTabList(context.win, FORM_URL);

    const sidebarBrowser = await BrowserTestUtils.waitForMutationCondition(
      context.win.document.documentElement,
      {
        childList: true,
        subtree: true,
      },
      () => context.win.document.getElementById("ai-window-browser")
    );

    await Promise.all([
      getAichatBrowser(sidebarBrowser),
      context.mockEngineManager.respondTo({
        purpose: "convo-starters-sidebar",
        response: "Test conversation starter",
      }),
    ]);

    return context;
  } catch (error) {
    await cleanupFormReviewTest(context);
    throw error;
  }
}

/**
 * Restores mocks, closes the AI Window, and restores the original region.
 *
 * @param {FormReviewTestContext|null|undefined} context - Context to clean up.
 * @returns {Promise<void>}
 */
async function cleanupFormReviewTest(context) {
  if (!context) {
    return;
  }

  _setRemoteClientForTesting({
    get: async () => MOCK_RS_RECORDS,
  });
  await context.mockEngineManager.rejectAllRequests();
  context.mockEngineManager.cleanupMocks();

  try {
    if (context.win && !context.win.closed) {
      await BrowserTestUtils.closeWindow(context.win);
    }
  } finally {
    if (context.prefEnvPushed) {
      await SpecialPowers.popPrefEnv();
      context.prefEnvPushed = false;
    }

    Region._setHomeRegion(context.originalRegion, false);
  }
}

/**
 * Waits until the isolated review component renders the requested state.
 *
 * @param {MozBrowser} reviewBrowser - Browser hosting the review component.
 * @param {string} expectedState - Review state to wait for.
 * @returns {Promise<void>}
 */
async function waitForFormReviewState(reviewBrowser, expectedState) {
  await SpecialPowers.spawn(reviewBrowser, [expectedState], async state => {
    const review = Cu.waiveXrays(
      content.document.querySelector("ai-sff-form-review")
    );

    if (review.state !== state) {
      await new Promise(resolve => {
        const observer = new content.MutationObserver(() => {
          if (review.state === state) {
            observer.disconnect();
            resolve();
          }
        });
        observer.observe(review.renderRoot, {
          attributes: true,
          childList: true,
          subtree: true,
        });
      });
    }

    await review.updateComplete;
    await ContentTaskUtils.waitForCondition(
      () =>
        review.renderRoot.activeElement ===
        review.renderRoot.querySelector(".form-review-dialog"),
      `Waiting for the ${state} state to receive focus`
    );
  });
}

/**
 * Waits for an element in the review component to receive focus.
 *
 * @param {MozBrowser} reviewBrowser - Browser hosting the review component.
 * @param {string} selector - Selector for the expected focused element.
 * @returns {Promise<void>}
 */
async function waitForFormReviewFocus(reviewBrowser, selector) {
  await SpecialPowers.spawn(reviewBrowser, [selector], async expected => {
    const review = Cu.waiveXrays(
      content.document.querySelector("ai-sff-form-review")
    );

    await ContentTaskUtils.waitForCondition(
      () => review.renderRoot.activeElement?.matches(expected),
      `Waiting for "${expected}" to receive focus`
    );
  });
}

/**
 * Presses Tab and waits for the expected review element to receive focus.
 *
 * @param {MozBrowser} reviewBrowser - Browser hosting the review component.
 * @param {string} selector - Selector for the expected focused element.
 * @returns {Promise<void>}
 */
async function tabToFormReviewElement(reviewBrowser, selector) {
  await BrowserTestUtils.synthesizeKey("KEY_Tab", {}, reviewBrowser);
  await waitForFormReviewFocus(reviewBrowser, selector);
}

/**
 * Reads the observable state rendered by the review component.
 *
 * @param {MozBrowser} reviewBrowser - Browser hosting the review component.
 * @returns {Promise<FormReviewSnapshot>} Component state snapshot.
 */
async function getFormReviewSnapshot(reviewBrowser) {
  return SpecialPowers.spawn(reviewBrowser, [], async () => {
    const review = Cu.waiveXrays(
      content.document.querySelector("ai-sff-form-review")
    );
    await review.updateComplete;

    return {
      state: review.state,
      errorType: review.errorType,
      filledFieldCount: review.filledFieldCount,
      fields: review.fields.map(field => ({ ...field })),
      l10nIds: [...review.renderRoot.querySelectorAll("[data-l10n-id]")].map(
        element => element.getAttribute("data-l10n-id")
      ),
      fillButtonDisabled:
        review.renderRoot.querySelector(
          'moz-button[data-l10n-id="ai-smart-form-fill-fill-form-button"]'
        )?.disabled ?? null,
    };
  });
}

/**
 * Activates a rendered review button through synthesized user input.
 *
 * @param {MozBrowser} reviewBrowser - Browser hosting the review component.
 * @param {string} l10nId - Localization ID identifying the button.
 * @returns {Promise<void>}
 */
async function activateFormReviewButton(reviewBrowser, l10nId) {
  const { x, y } = await SpecialPowers.spawn(
    reviewBrowser,
    [l10nId],
    async id => {
      const review = Cu.waiveXrays(
        content.document.querySelector("ai-sff-form-review")
      );
      await review.updateComplete;

      const button = review.renderRoot.querySelector(
        `moz-button[data-l10n-id="${id}"]`
      );
      if (!button) {
        throw new Error(`Could not find form review button "${id}"`);
      }

      await ContentTaskUtils.waitForCondition(
        () => !button.disabled,
        `Waiting for form review button "${id}" to be enabled`
      );

      const rect = button.buttonEl.getBoundingClientRect();
      return {
        x: rect.left + rect.width / 2,
        y: rect.top + rect.height / 2,
      };
    }
  );

  await BrowserTestUtils.synthesizeMouseAtPoint(x, y, {}, reviewBrowser);
}

/**
 * Constrains the review field list so a generated field requires scrolling.
 *
 * @param {MozBrowser} reviewBrowser - Browser hosting the review component.
 * @returns {Promise<void>}
 */
async function makeFormReviewFieldsScrollable(reviewBrowser) {
  await SpecialPowers.spawn(reviewBrowser, [], async () => {
    const review = Cu.waiveXrays(
      content.document.querySelector("ai-sff-form-review")
    );
    await review.updateComplete;

    const stylesheet = new content.CSSStyleSheet();
    stylesheet.replaceSync(".form-review-fields { max-height: 1px; }");
    Cu.waiveXrays(review.renderRoot).adoptedStyleSheets =
      content.Array.of(stylesheet);
  });
}

/**
 * Scrolls the review field list to its maximum scroll position.
 *
 * @param {MozBrowser} reviewBrowser - Browser hosting the review component.
 * @returns {Promise<void>}
 */
async function scrollFormReviewFieldsToBottom(reviewBrowser) {
  await SpecialPowers.spawn(reviewBrowser, [], async () => {
    const review = Cu.waiveXrays(
      content.document.querySelector("ai-sff-form-review")
    );
    await review.updateComplete;

    const fields = review.renderRoot.querySelector(".form-review-fields");
    if (!fields) {
      throw new Error("Could not find form review fields");
    }

    // The list sets scroll-behavior: smooth, so the position animates and only
    // lands a few frames later.
    fields.scrollTop = fields.scrollHeight;
    await ContentTaskUtils.waitForCondition(
      () => fields.scrollHeight - fields.scrollTop - fields.clientHeight <= 1,
      "Waiting for the review fields to reach their maximum scroll position"
    );

    fields.dispatchEvent(new content.Event("scroll"));
    await review.updateComplete;
  });
}

/**
 * Reads the labels and values of the editable review inputs.
 *
 * @param {MozBrowser} reviewBrowser - Browser hosting the review component.
 * @returns {Promise<FormReviewInputSnapshot[]>} Rendered input snapshots.
 */
async function getFormReviewInputs(reviewBrowser) {
  return SpecialPowers.spawn(reviewBrowser, [], async () => {
    const review = Cu.waiveXrays(
      content.document.querySelector("ai-sff-form-review")
    );
    await review.updateComplete;

    return [...review.renderRoot.querySelectorAll("moz-input-text")].map(
      input => ({
        label: input.label,
        value: input.value,
      })
    );
  });
}

/**
 * Replaces an editable review input's value through synthesized user input.
 *
 * @param {MozBrowser} reviewBrowser - Browser hosting the review component.
 * @param {number} index - Index of the input to edit.
 * @param {string} value - Replacement value to enter.
 * @returns {Promise<void>}
 */
async function editFormReviewInput(reviewBrowser, index, value) {
  await SpecialPowers.spawn(
    reviewBrowser,
    [index, value],
    async (inputIndex, inputValue) => {
      const review = Cu.waiveXrays(
        content.document.querySelector("ai-sff-form-review")
      );
      await review.updateComplete;

      const inputs = review.renderRoot.querySelectorAll("moz-input-text");
      const input = inputs[inputIndex];
      if (!input) {
        throw new Error(`Could not find form review input ${inputIndex}`);
      }

      EventUtils.synthesizeMouseAtCenter(input.inputEl, {}, content);
      EventUtils.synthesizeKey("a", { accelKey: true }, content);
      EventUtils.synthesizeKey("KEY_Backspace", {}, content);

      if (inputValue) {
        await EventUtils.sendString(inputValue, content);
      }

      await review.updateComplete;
    }
  );
}

/**
 * Reads a field value from the form content process.
 *
 * @param {MozBrowser} browser - Browser containing the form.
 * @param {string} selector - Selector for the field to read.
 * @returns {Promise<string>} Current field value.
 */
function getFormFieldValue(browser, selector) {
  return SpecialPowers.spawn(
    browser,
    [selector],
    fieldSelector => content.document.querySelector(fieldSelector).value
  );
}

/**
 * Enters a value into a form field through synthesized user input.
 *
 * @param {MozBrowser} browser - Browser containing the form.
 * @param {string} selector - Selector for the field to edit.
 * @param {string} value - Value to enter.
 * @returns {Promise<void>}
 */
async function typeInFormField(browser, selector, value) {
  await SpecialPowers.spawn(
    browser,
    [selector, value],
    async (fieldSelector, fieldValue) => {
      const field = content.document.querySelector(fieldSelector);
      field.focus();
      await EventUtils.sendString(fieldValue, content);
    }
  );
}

/**
 * Opens the review dialog through the Smart Form Fill autocomplete action.
 *
 * @param {FormReviewTestContext} context - Initialized test context.
 * @returns {Promise<{dialog: SubDialog, reviewBrowser: MozBrowser}>} Open
 *   dialog and its isolated review browser.
 */
async function openFormReview(context) {
  const { browser, item } = await getFormReviewAutocomplete(
    context.win,
    "#email",
    context.mockEngineManager
  );
  const dialogManager = context.win.gBrowser
    .getTabDialogBox(browser)
    .getTabDialogManager();
  const previousDialogCount = dialogManager._dialogs.length;
  const row = item.querySelector("autocomplete-row-item");

  EventUtils.synthesizeMouseAtCenter(row, { type: "mousemove" }, context.win);
  await TestUtils.waitForCondition(
    () => item.hasAttribute("selected"),
    "Waiting for the Smart Form Fill row to be pointer-selected"
  );
  EventUtils.synthesizeMouseAtCenter(row, {}, context.win);

  await TestUtils.waitForCondition(
    () => dialogManager._dialogs.length > previousDialogCount,
    "Waiting for the form review dialog"
  );
  const dialog = dialogManager._dialogs.at(-1);
  await dialog._dialogReady;

  const dialogWindow = dialog._frame.contentWindow;
  const reviewBrowser = dialogWindow.document.querySelector(
    "#form-review-browser"
  );

  Assert.ok(reviewBrowser, "The isolated form review browser should exist");
  Assert.equal(
    reviewBrowser.currentURI.spec,
    "about:smartformfillreview",
    "The isolated browser should contain the form review document"
  );

  await waitForFormReviewState(reviewBrowser, FORM_REVIEW_STATES.PROGRESS);

  return { dialog, reviewBrowser };
}

/**
 * Waits for a specific review dialog to close.
 *
 * @param {Window} win - AI Window containing the dialog.
 * @param {SubDialog} dialog - Dialog expected to close.
 * @returns {Promise<Event>} Dialog close event.
 */
function waitForFormReviewClose(win, dialog) {
  return BrowserTestUtils.waitForEvent(
    win.document,
    "dialogclose",
    false,
    event => event.detail.dialog === dialog
  );
}

/**
 * Responds to value generation using fields supplied by the test.
 *
 * @param {MockEngineManager} mockEngineManager - Manager used to capture the
 *   value-generation request.
 * @param {FormReviewGeneratedFieldInput[]} fields - Generated field data to
 *   return from the model.
 * @returns {Promise<Array<FormReviewGeneratedFieldInput & {id: string}>>}
 *   Generated fields joined with their requested field IDs.
 */
async function respondWithGeneratedFields(mockEngineManager, fields) {
  const { request, respond } = await captureFormReviewRequest(
    mockEngineManager,
    FORM_VALUES_SCHEMA
  );
  const requestData = getModelRequestData(request);
  const responseFields = fields.map((field, index) => ({
    id: requestData.fields[index].id,
    ...field,
  }));

  respond(
    JSON.stringify({
      memories_used: [],
      fields: responseFields,
    })
  );

  return responseFields;
}
