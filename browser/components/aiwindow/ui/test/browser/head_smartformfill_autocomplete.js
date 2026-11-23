/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* import-globals-from head.js */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);
const { UrlTokenizer } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/UrlTokenizer.sys.mjs"
);

const FORM_URL =
  "https://example.com/browser/browser/components/aiwindow/ui/test/browser/test_smartformfill_autocomplete.html";
const SOURCE_URL = "https://example.org/";
const SMART_FORM_FILL_PREF = "browser.smartwindow.smartformfill.enabled";
const MIN_FORM_FIELDS_PREF = "browser.smartwindow.smartformfill.minFormFields";

const SMART_FORM_FILL_MODEL_PURPOSE = "smart-form-fill";
const FIELD_CLASSIFICATION_SCHEMA = "SmartFormFillFieldClassification";
const RELEVANT_TABS_SCHEMA = "SmartFormFillRelevantTabs";
const FORM_VALUES_SCHEMA = "SmartFormFillFormValues";

const SMART_FORM_FILL_PROMPT_MODULES = {
  "field-detection-system-instructions": "Test system prompt.",
  "field-detection-user-data": '{"fields": {fields}}',
  "tab-selection-system-instructions": "Test system prompt.",
  "tab-selection-user-data": '{"tabs": {tabs}}',
  "value-generation-system-instructions": "Test system prompt.",
  "value-generation-user-data": '{"fields": {fields}}',
};

const SMART_FORM_FILL_REMOTE_SETTINGS_RECORDS = [
  {
    kind: "params",
    feature: "smart-form-fill",
    model: "test-model",
    service_type: "ai",
    purpose: SMART_FORM_FILL_MODEL_PURPOSE,
    parameters: {},
    modules: Object.keys(SMART_FORM_FILL_PROMPT_MODULES).map(name => ({
      name,
      version: "v2.0",
    })),
    version: "v2.0",
    is_default: true,
  },
  ...Object.entries(SMART_FORM_FILL_PROMPT_MODULES).map(
    ([module, prompts]) => ({
      kind: "module",
      feature: "smart-form-fill",
      module,
      model: "test-model",
      prompts,
      version: "v2.0",
    })
  ),
];

/**
 * Sets up the shared Smart Form Fill autocomplete test environment.
 *
 * @returns {Promise<{
 *   win: Window,
 *   mockEngineManager: MockEngineManager,
 *   originalRegion: string
 * }>} Test environment state needed for cleanup.
 */
async function setupSmartFormFillAutocompleteTest() {
  const originalRegion = Region.home;
  Region._setHomeRegion("US", false);

  await SpecialPowers.pushPrefEnv({
    set: [
      [SMART_FORM_FILL_PREF, true],
      // The test forms are smaller than the minimum the feature ships with.
      [MIN_FORM_FIELDS_PREF, 1],
      ["signon.rememberSignons", true],
      ["signon.showAutoCompleteFooter", true],
      // Autofilling a saved login writes to the field asynchronously, which
      // cancels an autocomplete search that is already in flight. These tests
      // only need the login to exist so its footer row shows up.
      ["signon.autofillForms", false],
    ],
  });

  _setRemoteClientForTesting({
    get: async () => [
      ...MOCK_RS_RECORDS,
      ...SMART_FORM_FILL_REMOTE_SETTINGS_RECORDS,
    ],
  });

  return {
    win: await openAIWindow(),
    mockEngineManager: new MockEngineManager(),
    originalRegion,
  };
}

/**
 * Restores state created by setupSmartFormFillAutocompleteTest().
 *
 * @param {object} context Test environment state.
 * @param {Window} context.win AI Window opened for the test.
 * @param {MockEngineManager} context.mockEngineManager Mock model manager.
 * @param {string} context.originalRegion Region to restore.
 */
async function cleanupSmartFormFillAutocompleteTest(context) {
  _setRemoteClientForTesting({
    get: async () => MOCK_RS_RECORDS,
  });

  await context.mockEngineManager?.rejectAllRequests();
  context.mockEngineManager?.cleanupMocks();

  try {
    if (context.win && !context.win.closed) {
      await closeAutocomplete(context.win.gBrowser.selectedBrowser);
    }
  } finally {
    try {
      await SpecialPowers.popPrefEnv();
    } finally {
      Region._setHomeRegion(context.originalRegion, false);

      if (context.win && !context.win.closed) {
        await BrowserTestUtils.closeWindow(context.win);
      }
    }
  }
}

/**
 * Gets the structured-response schema requested by a model call.
 *
 * @param {object} request Captured model request.
 *
 * @returns {string} Requested schema name.
 */
function getModelRequestSchema(request) {
  return request.inferenceParams.response_format.json_schema.name;
}

/**
 * Parses the test data from a model request's user prompt.
 *
 * @param {object} request Captured model request.
 *
 * @returns {object} Parsed request data.
 */
function getModelRequestData(request) {
  const userMessage = request.args.findLast(message => message.role === "user");

  if (!userMessage || typeof userMessage.content !== "string") {
    throw new Error("Smart Form Fill request has no user prompt");
  }

  return JSON.parse(userMessage.content);
}

/**
 * Responds to one field-classification or relevant-tabs request.
 *
 * @param {string} schemaName Requested schema name.
 * @param {object} requestData Parsed request data.
 * @param {(response: string) => void} respond Resolves the model request.
 * @param {string | null | undefined} selectedSourceUrl Source tab to select.
 */
function respondToMetadataRequest(
  schemaName,
  requestData,
  respond,
  selectedSourceUrl
) {
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
      const selectedSourceToken = selectedSourceUrl
        ? new UrlTokenizer().encodeToken(selectedSourceUrl, false)
        : null;
      const source = requestData.tabs.find(
        tab => tab.url === selectedSourceToken
      );
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

  throw new Error(`Unexpected Smart Form Fill schema: ${schemaName}`);
}

/**
 * Captures and answers metadata requests made through the Smart Form Fill
 * model integration.
 *
 * @param {MockEngineManager} mockEngineManager Mock model manager.
 * @param {object} [options] Request capture options.
 * @param {Array<string>} [options.expectedSchemas] Schemas to capture.
 * @param {string | null} [options.selectedSourceUrl] Source tab to select.
 *
 * @returns {Promise<Map<string, object>>} Request data by schema.
 */
async function captureMetadataRequests(
  mockEngineManager,
  {
    expectedSchemas = [FIELD_CLASSIFICATION_SCHEMA, RELEVANT_TABS_SCHEMA],
    selectedSourceUrl = null,
  } = {}
) {
  const requestDataBySchema = new Map();

  while (expectedSchemas.some(schema => !requestDataBySchema.has(schema))) {
    const { request, respond } = await mockEngineManager.captureRequest({
      purpose: SMART_FORM_FILL_MODEL_PURPOSE,
    });
    const schemaName = getModelRequestSchema(request);

    Assert.ok(
      expectedSchemas.includes(schemaName),
      `The ${schemaName} request should be expected`
    );
    Assert.ok(
      !requestDataBySchema.has(schemaName),
      `The ${schemaName} request should only run once`
    );

    const requestData = getModelRequestData(request);
    requestDataBySchema.set(schemaName, requestData);
    respondToMetadataRequest(
      schemaName,
      requestData,
      respond,
      selectedSourceUrl
    );
  }

  return requestDataBySchema;
}

/**
 * Settles all pending Smart Form Fill metadata requests.
 *
 * @param {MockEngineManager} mockEngineManager Mock model manager.
 * @param {Set<string>} handledSchemas Schemas handled by this test flow.
 * @param {string | null | undefined} selectedSourceUrl Source tab to select.
 * @param {Array<string>} failedSchemas Schemas whose requests should fail.
 * @param {Array<string>} [schemasToSettle] Metadata schemas to settle.
 * @param {Map<string, object> | null} [requestDataBySchema] Captured request
 *   data by schema.
 */
function settlePendingMetadataRequests(
  mockEngineManager,
  handledSchemas,
  selectedSourceUrl,
  failedSchemas,
  schemasToSettle = [FIELD_CLASSIFICATION_SCHEMA, RELEVANT_TABS_SCHEMA],
  requestDataBySchema = null
) {
  const engine = mockEngineManager.engines.get(SMART_FORM_FILL_MODEL_PURPOSE);
  if (!engine) {
    return;
  }

  for (const [requestId, { request, reject }] of [...engine.runRequests]) {
    const schemaName = getModelRequestSchema(request);
    if (!schemasToSettle.includes(schemaName)) {
      continue;
    }

    const requestData = getModelRequestData(request);
    requestDataBySchema?.set(schemaName, requestData);

    if (failedSchemas.includes(schemaName)) {
      engine.runRequests.delete(requestId);
      reject(new Error(`Test failure for ${schemaName}`));
    } else {
      respondToMetadataRequest(
        schemaName,
        requestData,
        response => engine.respond(requestId, response),
        selectedSourceUrl
      );
    }
    handledSchemas.add(schemaName);
  }
}

/**
 * Resolves expected metadata requests and waits for the refreshed row.
 *
 * @param {XULPopupElement} popup Autocomplete popup.
 * @param {MockEngineManager} mockEngineManager Mock model manager.
 * @param {Array<string>} expectedSchemas Schemas expected by the test.
 * @param {string | null | undefined} selectedSourceUrl Source tab to select.
 * @param {Array<string>} failedSchemas Schemas whose requests should fail.
 *
 * @returns {Promise<{
 *   row: HTMLElement,
 *   handledSchemas: Set<string>,
 *   requestDataBySchema: Map<string, object>
 * }>} Updated row and metadata schemas handled while waiting.
 */
async function waitForMetadataAndUpdatedRow(
  popup,
  mockEngineManager,
  expectedSchemas,
  selectedSourceUrl,
  failedSchemas
) {
  const handledSchemas = new Set();
  const requestDataBySchema = new Map();
  let row;

  await TestUtils.waitForCondition(() => {
    settlePendingMetadataRequests(
      mockEngineManager,
      handledSchemas,
      selectedSourceUrl,
      failedSchemas,
      undefined,
      requestDataBySchema
    );

    row = popup
      .querySelector('[originaltype="smartFormFill"]')
      ?.querySelector("autocomplete-row-item");

    return (
      row &&
      !row.loading &&
      expectedSchemas.every(schema => handledSchemas.has(schema))
    );
  }, "Waiting for Smart Form Fill metadata and autocomplete refresh");

  await row.updateComplete;
  return { row, handledSchemas, requestDataBySchema };
}

function getSmartFormFillActor(browser) {
  return browser.browsingContext.currentWindowGlobal.getActor("SmartFormFill");
}

function getFieldsByName(formData) {
  return new Map(formData.fields.map(field => [field.name, field]));
}

function hasSmartFormFillProvider(browser, selector) {
  return SpecialPowers.spawn(browser, [selector], fieldSelector => {
    const input = content.document.querySelector(fieldSelector);
    const autocompleteActor =
      input.documentGlobal.windowGlobalChild.getActor("AutoComplete");

    return [...autocompleteActor.providersByInput(input)].some(
      provider => provider.actorName === "SmartFormFill"
    );
  });
}

async function waitForFocusedForm(browser, selector, check, message) {
  await waitForSmartFormFillProvider(browser, selector, { focus: true });

  const actor = getSmartFormFillActor(browser);
  let formData;
  await TestUtils.waitForCondition(async () => {
    formData = await actor.sendQuery("SmartFormFill:GetFocusedForm");
    return formData && check(formData);
  }, message);

  return formData;
}

/**
 * Waits for Smart Form Fill to register for a field and optionally focuses it.
 *
 * @param {MozBrowser} browser Browser containing the field.
 * @param {string} selector Selector for the field.
 * @param {object} [options] Field interaction options.
 * @param {boolean} [options.focus=false] Whether to focus the field.
 *
 * @returns {Promise<string | null>} ID of the focused element, when requested.
 */
async function waitForSmartFormFillProvider(
  browser,
  selector,
  { focus: shouldFocus = false } = {}
) {
  await SpecialPowers.spawn(browser, [selector], async fieldSelector => {
    const input = content.document.querySelector(fieldSelector);
    const autocompleteActor =
      input.documentGlobal.windowGlobalChild.getActor("AutoComplete");

    await ContentTaskUtils.waitForCondition(
      () =>
        [...autocompleteActor.providersByInput(input)].some(
          provider => provider.actorName === "SmartFormFill"
        ),
      "Waiting for Smart Form Fill to register as an autocomplete provider"
    );
  });

  if (!shouldFocus) {
    return SpecialPowers.spawn(
      browser,
      [],
      () => content.document.activeElement?.id ?? null
    );
  }

  // The AI Window can still be settling and take focus back after the browser
  // first got it. document.activeElement stays on the field even then, so the
  // field looks focused while its document is not, and the arrow key reaches
  // the input without ever opening the autocomplete popup. Re-assert focus
  // until the form document actually holds it.
  let focusedField = null;
  await TestUtils.waitForCondition(async () => {
    await SimpleTest.promiseFocus(browser);
    focusedField = await SpecialPowers.spawn(
      browser,
      [selector],
      fieldSelector => {
        const input = content.document.querySelector(fieldSelector);
        input.focus();
        return content.document.hasFocus() &&
          content.document.activeElement === input
          ? input.id
          : null;
      }
    );
    return focusedField !== null;
  }, "Waiting for the form field to actually hold focus");

  return focusedField;
}

/**
 * Gets the serialized Smart Form Fill metadata visible to content.
 *
 * @param {MozBrowser} browser Browser containing the form.
 * @param {string} selector Selector for the autocomplete field.
 * @returns {Promise<object>} Serialized Smart Form Fill metadata.
 */
async function getContentSmartFormFillMetadata(browser, selector) {
  return SpecialPowers.spawn(browser, [selector], fieldSelector => {
    const input = content.document.querySelector(fieldSelector);
    const autocompleteActor =
      input.documentGlobal.windowGlobalChild.getActor("AutoComplete");
    const result = autocompleteActor
      .getResultsFromController(autocompleteActor._input)
      .find(entry => entry.style === "smartFormFill");

    return JSON.parse(result.comment);
  });
}

/**
 * Opens Smart Form Fill autocomplete without settling its metadata requests.
 *
 * @param {Window} win AI Window containing the form.
 * @param {string} selector Selector for the field to focus.
 *
 * @returns {Promise<{
 *   browser: MozBrowser,
 *   popup: XULPopupElement,
 *   item: Element
 * }>} Open autocomplete elements.
 */
async function openLoadingAutocomplete(win, selector) {
  await getAichatBrowser(win.document.getElementById("ai-window-browser"));

  const browser = win.gBrowser.selectedBrowser;
  const popup = browser.autoCompletePopup;

  await SimpleTest.promiseFocus(browser);

  const focusedField = await waitForSmartFormFillProvider(browser, selector, {
    focus: true,
  });

  Assert.equal(
    focusedField,
    selector.replace(/^#/, ""),
    "The autocomplete field should be focused"
  );

  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
  await TestUtils.waitForCondition(
    () => popup.state == "open",
    "Waiting for the autocomplete popup to open"
  );

  let item = popup.querySelector('[originaltype="smartFormFill"]');
  if (!item) {
    await BrowserTestUtils.waitForMutationCondition(
      popup.richlistbox,
      { childList: true, subtree: true },
      () => (item = popup.querySelector('[originaltype="smartFormFill"]'))
    );
  }

  return { browser, popup, item };
}

/**
 * Opens Smart Form Fill autocomplete and resolves its metadata requests.
 *
 * @param {Window} win AI Window containing the form.
 * @param {string} selector Selector for the field to focus.
 * @param {MockEngineManager} mockEngineManager Mock model manager.
 * @param {object} [options] Metadata response options.
 * @param {Array<string>} [options.expectedSchemas] Schemas expected by the
 *   test.
 * @param {Array<string>} [options.failedSchemas] Schemas whose requests
 *   should fail.
 * @param {string | null} [options.selectedSourceUrl] Source tab to select.
 *
 * @returns {Promise<{
 *   browser: MozBrowser,
 *   popup: XULPopupElement,
 *   item: Element,
 *   row: HTMLElement,
 *   handledSchemas: Set<string>
 * }>} Open autocomplete elements.
 */
async function openAutocomplete(
  win,
  selector,
  mockEngineManager,
  {
    expectedSchemas = [FIELD_CLASSIFICATION_SCHEMA, RELEVANT_TABS_SCHEMA],
    failedSchemas = [],
    selectedSourceUrl = null,
  } = {}
) {
  const { browser, popup, item } = await openLoadingAutocomplete(win, selector);
  const metadata = await waitForMetadataAndUpdatedRow(
    popup,
    mockEngineManager,
    expectedSchemas,
    selectedSourceUrl,
    failedSchemas
  );

  return {
    browser,
    popup,
    item,
    ...metadata,
  };
}

/**
 * Closes the autocomplete popup when it is open.
 *
 * @param {MozBrowser} browser Browser owning the popup.
 */
async function closeAutocomplete(browser) {
  const popup = browser.autoCompletePopup;
  if (!popup?.popupOpen) {
    return;
  }

  const popupHidden = BrowserTestUtils.waitForPopupEvent(popup, "hidden");
  await BrowserTestUtils.synthesizeKey("KEY_Escape", {}, browser);
  await popupHidden;
}
