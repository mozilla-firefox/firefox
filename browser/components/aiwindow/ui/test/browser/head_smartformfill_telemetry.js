/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_smartformfill_form_review.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_smartformfill_form_review.js",
  this
);

const { SmartFormFillModel } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs"
);

// Two forms: a contact form with email + tel, and a details form with a
// textarea.
const TEST_PAGE = `${getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
)}test_smartformfill_telemetry.html`;

// Fields of the contact form, which is the form every round runs on.
const CONTACT_FIELDS = 2;

// What the fakes report as the model and prompt a request was built with.
const TEST_MODEL_INFO = { model: "test-model", promptVersion: "42" };

// What the fakes answer a field with, and so what an edit is measured against.
const GENERATED_VALUE = "generated value";

/**
 * Classifies every field of the request, so the recorded field_kind has a
 * value.
 *
 * The model calls are awaited or chained with then(), so every fake has to be
 * async the way the real ones are. Every fake also has to report its model
 * info, which is what makes the real model layer record the request event.
 *
 * @param {object} request
 * @param {object} [param1={}]
 * @param {Function} [param1.onDispatch]
 * @returns {Promise<object>}
 */
async function classifyEveryField(request, { onDispatch } = {}) {
  onDispatch?.(TEST_MODEL_INFO);

  return {
    fields: request.fields.map(({ id }) => ({
      id,
      type: "email",
      confidence: "high",
    })),
  };
}

/**
 * Classifies each field as whatever its name is mapped to, so a round can mix
 * the fields the model could name with the ones it could not.
 *
 * @param {Map<string, string | undefined>} typeByFieldName
 * @returns {Function} A fake for classifyFields
 */
function classifyAs(typeByFieldName) {
  return async (request, { onDispatch } = {}) => {
    onDispatch?.(TEST_MODEL_INFO);

    return {
      fields: request.fields.map(({ id, name }) => ({
        id,
        type: typeByFieldName.get(name),
        confidence: "high",
      })),
    };
  };
}

/**
 * Finds no tab relevant, which is what the assertions that are not about tab
 * selection expect.
 *
 * @param {object} request
 * @param {object} [param1={}]
 * @param {Function} [param1.onDispatch]
 * @returns {Promise<object>}
 */
async function selectNoTabs(request, { onDispatch } = {}) {
  onDispatch?.(TEST_MODEL_INFO);

  return { selectedTabs: [] };
}

/**
 * Finds the first tab of the request relevant, so a round starts from a
 * selection the user can then be made to change.
 *
 * @param {object} request
 * @param {object} [param1={}]
 * @param {Function} [param1.onDispatch]
 * @returns {Promise<object>}
 */
async function selectTheFirstTab(request, { onDispatch } = {}) {
  onDispatch?.(TEST_MODEL_INFO);

  return {
    selectedTabs: request.tabs
      .slice(0, 1)
      .map(({ id }) => ({ id, relevance: "high" })),
  };
}

/**
 * Answers every field of the request with a generated value.
 *
 * @param {object} request
 * @param {object} [param1={}]
 * @param {Function} [param1.onDispatch]
 * @returns {Promise<object>}
 */
async function generateEveryValue(request, { onDispatch } = {}) {
  onDispatch?.(TEST_MODEL_INFO);

  return {
    memories_used: [],
    tabs_used: [],
    fields: request.fields.map(({ id }) => ({
      id,
      action: "generate",
      value: GENERATED_VALUE,
      confidence: "high",
    })),
    batches: { total: 1, failed: 0 },
  };
}

/**
 * @param {string} name Glean metric name on smartWindow.
 * @returns {Array<object>} The extras of every recorded event.
 */
function recordedExtras(name) {
  return (Glean.smartWindow[name].testGetValue() ?? []).map(
    event => event.extra
  );
}

/**
 * @param {string} name Glean metric name on smartWindow.
 * @param {number} count Number of events to wait for.
 * @returns {Promise<Array<object>>} The extras, once count events landed.
 */
async function waitForEvents(name, count) {
  await TestUtils.waitForCondition(
    () => Glean.smartWindow[name].testGetValue()?.length >= count,
    `${name} should have recorded ${count} events`
  );

  return recordedExtras(name);
}

/**
 * Aborts whatever review dialog the tab currently holds.
 *
 * @param {Window} win
 * @param {MozBrowser} browser
 * @returns {Promise<void>}
 */
async function closeFormReview(win, browser) {
  const tabDialogBox = win.gBrowser.getTabDialogBox(browser);
  tabDialogBox.abortAllDialogs();
  await TestUtils.waitForCondition(
    () => !tabDialogBox.getTabDialogManager()._dialogs.length,
    "Waiting for the form review dialog to close"
  );
}

/**
 * Opens a Smart Window on the test form with the model calls stubbed. Nothing
 * is requested until a form is asked about, so a test that asserts on requests
 * starts a round itself.
 *
 * The model is stubbed so the assertions describe the telemetry rather than
 * whatever the model layer currently answers.
 *
 * @param {object} overrides Fakes for the stubbed model calls, plus
 *   contextTabs: urls to open before the form, for the model to choose from.
 * @param {Function} callback Receives { win, browser, actor }.
 * @returns {Promise<void>}
 */
async function withFormPage(overrides, callback) {
  Services.fog.testResetFOG();

  const sandbox = sinon.createSandbox();
  sandbox
    .stub(SmartFormFillModel, "findRelevantTabs")
    .callsFake(overrides.findRelevantTabs ?? selectNoTabs);
  sandbox
    .stub(SmartFormFillModel, "classifyFields")
    .callsFake(overrides.classifyFields ?? classifyEveryField);
  sandbox
    .stub(SmartFormFillModel, "generateFormValues")
    .callsFake(overrides.generateFormValues ?? generateEveryValue);
  const mockEngineManager = new MockEngineManager();

  const win = await openAIWindow();

  // Opened before the form, so the form is the focused tab once they are all
  // there.
  for (const url of overrides.contextTabs ?? []) {
    await openTabAndWaitForTabList(win, url);
  }

  const tab = await openTabAndWaitForTabList(win, TEST_PAGE);
  const browser = tab.linkedBrowser;
  const actor =
    browser.browsingContext.currentWindowGlobal.getActor("SmartFormFill");

  // An autocomplete popup left open by an earlier file holds the focus, and the
  // events that end a fill depend on the content document having it.
  const popup = browser.autoCompletePopup;
  if (popup?.popupOpen) {
    const hidden = BrowserTestUtils.waitForPopupEvent(popup, "hidden");
    popup.hidePopup();
    await hidden;
  }

  try {
    await callback({ win, browser, actor });
  } finally {
    await closeFormReview(win, browser);

    BrowserTestUtils.removeTab(tab);
    await BrowserTestUtils.closeWindow(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    sandbox.restore();
  }
}

/**
 * Focuses a field in the content document.
 *
 * Focused from content rather than with a synthesized click:
 * BrowserTestUtils.synthesizeMouse reaches content through its own sendQuery,
 * which calls this.waitForCondition, a method BrowserTestUtils does not have,
 * so a click that races the content process throws instead of waiting. The
 * autocomplete tests focus fields this way too.
 *
 * @param {MozBrowser} browser
 * @param {string} selector Field to focus
 * @returns {Promise<void>}
 */
function focusField(browser, selector) {
  return SpecialPowers.spawn(browser, [selector], fieldSelector =>
    content.document.querySelector(fieldSelector).focus()
  );
}

/**
 * Blurs a field, which is what ends the fill of that one field.
 *
 * The report a blur triggers is sent before the reply to this call, both being
 * messages of the same window global, so an outcome it recorded has landed by
 * the time this resolves.
 *
 * @param {MozBrowser} browser
 * @param {string} selector Field to blur
 * @returns {Promise<void>}
 */
function blurField(browser, selector) {
  return SpecialPowers.spawn(browser, [selector], fieldSelector =>
    content.document.querySelector(fieldSelector).blur()
  );
}

/**
 * Replaces what a field holds, selecting the filled value first so an empty
 * string leaves the field empty rather than appending nothing.
 *
 * @param {MozBrowser} browser
 * @param {string} selector Field to replace the value of
 * @param {string} value Text to leave the field with
 * @returns {Promise<void>}
 */
function replaceFieldValue(browser, selector, value) {
  return SpecialPowers.spawn(
    browser,
    [selector, value],
    async (fieldSelector, text) => {
      const field = content.document.querySelector(fieldSelector);
      field.focus();
      EventUtils.synthesizeKey("a", { accelKey: true }, content);
      EventUtils.synthesizeKey("KEY_Backspace", {}, content);

      if (text) {
        await EventUtils.sendString(text, content);
      }
    }
  );
}

/**
 * Submits a form without letting it navigate, which ends the fill of every
 * field of that form at once.
 *
 * The listener that reports the outcomes is in the system group, so cancelling
 * the submission from the page stops the navigation without stopping the
 * report. Validation is turned off because a generated value is not a valid
 * email.
 *
 * @param {MozBrowser} browser
 * @param {string} selector Form to submit
 * @returns {Promise<void>}
 */
function submitForm(browser, selector) {
  return SpecialPowers.spawn(browser, [selector], formSelector => {
    const form = content.document.querySelector(formSelector);
    form.noValidate = true;
    form.addEventListener("submit", event => event.preventDefault(), {
      once: true,
    });
    form.requestSubmit();
  });
}

/**
 * Runs a whole round on the form a field belongs to: relevant tabs,
 * classification and generation. Nothing is requested until a form is asked
 * about, so every assertion about requests starts from here.
 *
 * @param {MozBrowser} browser
 * @param {object} actor The SmartFormFill parent actor
 * @param {string} [selector] Field to run the round for
 * @returns {Promise<void>}
 */
async function runRoundOnForm(browser, actor, selector = "#email") {
  await SimpleTest.promiseFocus(browser);
  await focusField(browser, selector);

  await actor.triggerAutofill();
}

/**
 * Waits for the review dialog and hands back the dialog along with the browser
 * holding the review component, which is what the values are asserted on and
 * edited through.
 *
 * @param {Window} win
 * @param {MozBrowser} browser
 * @returns {Promise<{dialog: SubDialog, reviewBrowser: MozBrowser}>}
 */
async function getFormReview(win, browser) {
  const dialogManager = win.gBrowser
    .getTabDialogBox(browser)
    .getTabDialogManager();
  await TestUtils.waitForCondition(
    () => dialogManager._dialogs.length,
    "Waiting for the form review dialog"
  );

  const dialog = dialogManager._dialogs.at(-1);
  await dialog._dialogReady;

  const reviewBrowser = dialog._frame.contentWindow.document.querySelector(
    "#form-review-browser"
  );
  await waitForFormReviewState(reviewBrowser, FORM_REVIEW_STATES.REVIEW);

  return { dialog, reviewBrowser };
}

/**
 * Approves the values the review dialog is holding, which is what makes the
 * page write them.
 *
 * @param {Window} win
 * @param {MozBrowser} browser
 * @returns {Promise<void>}
 */
async function fillFormReview(win, browser) {
  const { reviewBrowser } = await getFormReview(win, browser);
  await fillReviewedForm(reviewBrowser);
}

async function fillReviewedForm(reviewBrowser) {
  // Fill form only enables once the generated values have been scrolled
  // through.
  await scrollFormReviewFieldsToBottom(reviewBrowser);
  await activateFormReviewButton(
    reviewBrowser,
    "ai-smart-form-fill-fill-form-button"
  );
}

/**
 * Rejects the values the review dialog is holding, which is what makes the
 * round end without the page writing anything.
 *
 * @param {Window} win
 * @param {MozBrowser} browser
 * @returns {Promise<void>}
 */
async function cancelFormReview(win, browser) {
  const { dialog, reviewBrowser } = await getFormReview(win, browser);
  const closed = waitForFormReviewClose(win, dialog);
  await activateFormReviewButton(
    reviewBrowser,
    "ai-smart-form-fill-cancel-review-button"
  );
  await closed;
}

/**
 * Adds a field to the contact form, which the document's observer reports as a
 * form update.
 *
 * @param {MozBrowser} browser
 * @returns {Promise<void>}
 */
function addFieldToForm(browser) {
  return SpecialPowers.spawn(browser, [], () => {
    const input = content.document.createElement("input");
    input.type = "text";
    input.name = "city";
    content.document.getElementById("contact").append(input);
  });
}

/**
 * Runs a round on the contact form and approves the reviewed values, which is
 * the state every outcome assertion starts from: the page has written the
 * values and is waiting for something to end their fill.
 *
 * @param {Window} win
 * @param {MozBrowser} browser
 * @param {object} actor The SmartFormFill parent actor
 * @param {number} [fieldCount] Fields the round is expected to report, which a
 *   form a test added a field to has more of
 * @returns {Promise<void>}
 */
async function fillContactForm(
  win,
  browser,
  actor,
  fieldCount = CONTACT_FIELDS
) {
  await runRoundOnForm(browser, actor);
  await fillFormReview(win, browser);

  // The decision events land when the page reports what it filled, which is
  // also what starts the tracking the outcomes come from.
  await waitForEvents("formFillField", fieldCount);
  await closeFormReview(win, browser);
  await SimpleTest.promiseFocus(browser);
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SMART_FORM_FILL_PREF, true],
      // The test forms are smaller than the minimum the feature ships with.
      [MIN_FORM_FIELDS_PREF, 1],
    ],
  });
});
