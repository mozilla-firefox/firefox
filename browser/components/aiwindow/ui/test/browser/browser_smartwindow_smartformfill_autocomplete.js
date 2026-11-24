/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_smartformfill_autocomplete.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_smartformfill_autocomplete.js",
  this
);

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  LoginTestUtils: "resource://testing-common/LoginTestUtils.sys.mjs",
  NonPrivateTabs: "resource:///modules/OpenTabs.sys.mjs",
  sanitizeUntrustedContent:
    "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs",
});

const { formAutofillStorage } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofillStorage.sys.mjs"
);

// The autocomplete form has no address section, so the preview test uses the
// form whose fields carry an autocomplete attribute Form Autofill can fill.
const ADDRESS_FORM_URL =
  "https://example.com/browser/browser/components/aiwindow/ui/test/browser/test_smartformfill.html";

const TEST_ADDRESS = {
  "given-name": "John",
  email: "john.smith@example.com",
  country: "US",
};

ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () => new Localization(["browser/aiWindow.ftl"], true)
);

/**
 * Waits for the aggregated tab-change event containing a source event.
 *
 * @param {"TabOpen" | "TabClose" | "TabAttrModified"} sourceEvent Tab event
 *   to wait for.
 * @returns {Promise<Event>} The aggregated tab-change event.
 */
function waitForTabChange(sourceEvent) {
  return BrowserTestUtils.waitForEvent(
    lazy.NonPrivateTabs,
    "TabChange",
    false,
    event => event.detail.sourceEvents.includes(sourceEvent)
  );
}

/**
 * Gets the displayed sources text from an autocomplete row. The value renders
 * as a sibling of the fixed label rather than in an element of its own.
 *
 * @param {HTMLElement} row Smart Form Fill autocomplete row.
 *
 * @returns {string} Displayed sources text.
 */
function getSourcesValue(row) {
  const sources = row.renderRoot.querySelector(".smart-form-fill-sources");
  const label = sources.querySelector(".sources-label").textContent;
  return sources.textContent.replace(label, "").trim();
}

/**
 * Moves the pointer over an autocomplete row and waits for the autofill
 * preview to settle. A row only reacts to a move once the popup's throttling
 * window has passed, so reset it before each attempt.
 *
 * @param {Window} win AI Window owning the popup.
 * @param {XULPopupElement} popup Autocomplete popup.
 * @param {Element} item Row to hover.
 */
async function hoverRow(win, popup, item) {
  const previewSettled = TestUtils.topicObserved(
    "formautofill-preview-complete"
  );

  await TestUtils.waitForCondition(() => {
    popup.mLastMoveTime = 0;
    EventUtils.synthesizeMouseAtCenter(item, { type: "mousemove" }, win);
    return item.hasAttribute("selected");
  }, "Waiting for the hovered row to be pointer-selected");

  await previewSettled;
}

/**
 * Asserts whether the address fields carry an autofill preview.
 *
 * @param {MozBrowser} browser Browser showing the form.
 * @param {boolean} previewed Whether the fields should be previewed.
 */
function assertPreviewedFields(browser, previewed) {
  return SpecialPowers.spawn(browser, [previewed], shouldPreview => {
    for (const selector of ["#first-name", "#email"]) {
      const field = content.document.querySelector(selector);

      Assert.equal(
        field.autofillState,
        shouldPreview ? "preview" : "",
        `${selector} preview state`
      );
      Assert.equal(
        !!field.previewValue,
        shouldPreview,
        `${selector} preview value`
      );
    }
  });
}

describe("Smart Form Fill autocomplete row item menu", () => {
  let context;
  let win;
  let mockEngineManager;

  beforeEach(async () => {
    context = await setupSmartFormFillAutocompleteTest();
    ({ win, mockEngineManager } = context);

    const tabUpdated = waitForTabChange("TabAttrModified");
    await promiseNavigateAndLoad(win.gBrowser.selectedBrowser, FORM_URL);
    await tabUpdated;
  });

  afterEach(async () => {
    await cleanupSmartFormFillAutocompleteTest(context);
    context = null;
    win = null;
    mockEngineManager = null;
  });

  it("renders the fixed label and no-tabs guidance", async () => {
    const { row } = await openAutocomplete(win, "#email", mockEngineManager);

    Assert.equal(
      row.renderRoot.querySelector(".sources-label").textContent,
      await lazy.l10n.formatValue(
        "ai-smart-form-fill-autocomplete-sources-label"
      ),
      "The fixed sources label should be rendered"
    );
    Assert.equal(
      getSourcesValue(row),
      await lazy.l10n.formatValue("ai-smart-form-fill-autocomplete-open-tabs"),
      "The row should prompt the user to open tabs"
    );
  });

  it("shows empty-source guidance when relevant tabs fail", async () => {
    const { row } = await openAutocomplete(win, "#email", mockEngineManager, {
      failedSchemas: [RELEVANT_TABS_SCHEMA],
    });

    Assert.equal(
      getSourcesValue(row),
      await lazy.l10n.formatValue("ai-smart-form-fill-autocomplete-open-tabs"),
      "A relevant-tab failure should fall back to an empty source list"
    );
  });

  it("renders the source pill row", async () => {
    const login = lazy.LoginTestUtils.testData.formLogin({
      origin: "https://example.com",
      formActionOrigin: "https://example.com",
      username: "saved@example.com",
      password: "password",
      usernameField: "email",
      passwordField: "password",
    });
    await Services.logins.addLoginAsync(login);

    let browser;
    try {
      const sourceTabUpdated = waitForTabChange("TabAttrModified");
      const sourceTab = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        SOURCE_URL
      );
      await sourceTabUpdated;

      const formTabUpdated = waitForTabChange("TabAttrModified");
      await BrowserTestUtils.openNewForegroundTab(win.gBrowser, FORM_URL);
      await formTabUpdated;

      const result = await openAutocomplete(win, "#email", mockEngineManager, {
        selectedSourceUrl: SOURCE_URL,
      });
      ({ browser } = result);
      const { popup, item, row } = result;

      const contentMetadata = await getContentSmartFormFillMetadata(
        browser,
        "#email"
      );
      Assert.ok(
        !Object.hasOwn(contentMetadata, "sources"),
        "Source metadata should not be exposed to the content process"
      );

      const pill = row.renderRoot.querySelector(".sources-pill");

      Assert.ok(pill, "The relevant source should render as a pill");
      Assert.equal(
        pill.querySelector(".source-label").textContent.trim(),
        lazy.sanitizeUntrustedContent(sourceTab.label),
        "The sources pill should show the tab title"
      );
      Assert.equal(
        pill.querySelector(".source-favicon").getAttribute("src"),
        `page-icon:${SOURCE_URL}`,
        "The sources pill should use the tab favicon"
      );
      Assert.ok(
        row.renderRoot.querySelector("moz-button.secondary-action"),
        "The edit-sources action should be rendered"
      );

      const loginFooter = popup.querySelector('[originaltype="loginsFooter"]');
      Assert.ok(loginFooter, "LoginManager rows should remain in the popup");

      const displayedItems = [
        ...popup.querySelectorAll(".autocomplete-row-item"),
      ].filter(candidate => !candidate.collapsed);

      Assert.less(
        displayedItems.indexOf(item),
        displayedItems.indexOf(loginFooter),
        "Smart Form Fill should appear before the LoginManager footer"
      );
    } finally {
      if (browser) {
        await closeAutocomplete(browser);
      }
      await Services.logins.removeLoginAsync(login);
    }
  });

  it("updates its guidance when tabs are opened and closed", async () => {
    const formTab = win.gBrowser.selectedTab;
    let result = await openAutocomplete(win, "#email", mockEngineManager);

    Assert.equal(
      getSourcesValue(result.row),
      await lazy.l10n.formatValue("ai-smart-form-fill-autocomplete-open-tabs"),
      "The row should initially report that no source tabs exist"
    );
    Assert.ok(
      !result.row.renderRoot.querySelector("moz-button.secondary-action"),
      "The edit-sources action should be hidden without source tabs"
    );
    await closeAutocomplete(result.browser);

    const tabUpdated = waitForTabChange("TabAttrModified");
    const sourceTab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      SOURCE_URL
    );
    await tabUpdated;
    await BrowserTestUtils.switchTab(win.gBrowser, formTab);

    result = await openAutocomplete(win, "#email", mockEngineManager, {
      expectedSchemas: [RELEVANT_TABS_SCHEMA],
    });
    Assert.equal(
      getSourcesValue(result.row),
      await lazy.l10n.formatValue(
        "ai-smart-form-fill-autocomplete-choose-tabs"
      ),
      "The row should report that source tabs are available"
    );
    Assert.ok(
      result.row.renderRoot.querySelector("moz-button.secondary-action"),
      "The edit-sources action should be shown when source tabs are available"
    );
    await closeAutocomplete(result.browser);

    const tabClosed = waitForTabChange("TabClose");
    await BrowserTestUtils.removeTab(sourceTab);
    await tabClosed;

    result = await openAutocomplete(win, "#email", mockEngineManager, {
      expectedSchemas: [RELEVANT_TABS_SCHEMA],
    });
    Assert.equal(
      getSourcesValue(result.row),
      await lazy.l10n.formatValue("ai-smart-form-fill-autocomplete-open-tabs"),
      "The row should update after the last source tab closes"
    );
    Assert.ok(
      !result.row.renderRoot.querySelector("moz-button.secondary-action"),
      "The edit-sources action should be hidden after the last source tab closes"
    );
  });

  describe("with a saved address", () => {
    beforeEach(async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["extensions.formautofill.addresses.supported", "on"]],
      });
      await formAutofillStorage.initialize();
      await formAutofillStorage.addresses.add(TEST_ADDRESS);

      const tabUpdated = waitForTabChange("TabAttrModified");
      await promiseNavigateAndLoad(
        win.gBrowser.selectedBrowser,
        ADDRESS_FORM_URL
      );
      await tabUpdated;
    });

    afterEach(async () => {
      await formAutofillStorage.addresses.removeAll();
      await SpecialPowers.popPrefEnv();
    });

    it("drops the address preview left by the previously hovered row", async () => {
      const { browser, popup, item } = await openAutocomplete(
        win,
        "#first-name",
        mockEngineManager
      );

      const addressItem = popup.querySelector('[originaltype="autofill"]');
      Assert.ok(addressItem, "The address row should be shown");

      await hoverRow(win, popup, addressItem);
      await assertPreviewedFields(browser, true);

      await hoverRow(win, popup, item);
      await assertPreviewedFields(browser, false);

      await hoverRow(win, popup, addressItem);
      await assertPreviewedFields(browser, true);
    });
  });
});
