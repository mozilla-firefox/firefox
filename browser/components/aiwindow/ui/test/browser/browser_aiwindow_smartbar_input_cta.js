/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests for the input-cta component in the Smartbar.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchTestUtils: "resource://testing-common/SearchTestUtils.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

SearchTestUtils.init(this);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.suggest.enabled", false],
      ["browser.smartwindow.endpoint", "http://localhost:0/v1"],
    ],
  });

  const fakeIntentEngine = {
    run({ args: [[query]] }) {
      const searchKeywords = ["search", "hello"];
      const formattedPrompt = query.toLowerCase();
      const isSearch = searchKeywords.some(keyword =>
        formattedPrompt.includes(keyword)
      );

      if (isSearch) {
        return [
          { label: "search", score: 0.95 },
          { label: "chat", score: 0.05 },
        ];
      }
      return [
        { label: "chat", score: 0.95 },
        { label: "search", score: 0.05 },
      ];
    },
  };

  gIntentEngineStub.resolves(fakeIntentEngine);
});

add_task(async function test_smartbar_cta_default_search_engine_label() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  const inputCta = BrowserTestUtils.querySelectorDeep(
    browser.contentDocument,
    "input-cta"
  );
  await TestUtils.waitForCondition(
    () => inputCta.searchEngineInfo.name,
    "Wait for the default search engine info to populate"
  );

  const searchEngineName = inputCta.searchEngineInfo.name;
  inputCta.action = "search";
  await inputCta.updateComplete;
  const searchLabel = await browser.contentDocument.l10n.formatValue(
    "aiwindow-input-cta-menu-label-search",
    { searchEngineName }
  );

  Assert.ok(searchEngineName, "Search engine name should be set");
  Assert.ok(inputCta.searchEngineInfo.icon, "Search engine icon should be set");
  Assert.equal(
    searchLabel,
    `Search with ${searchEngineName}`,
    `Search label should include engine name: [${searchLabel}]`
  );

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_smartbar_cta_search_engines_list() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  const inputCta = BrowserTestUtils.querySelectorDeep(
    browser.contentDocument,
    "input-cta"
  );
  await TestUtils.waitForCondition(
    () => !!inputCta.searchEngines.length,
    "Wait for the search engines list to populate"
  );

  Assert.greater(
    inputCta.searchEngines.length,
    0,
    "searchEngines should have at least one engine"
  );

  for (const engine of inputCta.searchEngines) {
    Assert.ok(engine.name, "Each search engine should have a name");
  }

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_search_with_locks_and_remembers_engine() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  // Trigger 'chat' intent
  const query = "tell me a joke";

  await stubOpenSERP(browser);
  await typeInSmartbar(browser, query);
  await waitForSmartbarAction(browser, "chat");
  await selectSmartbarSearchEngine(browser);

  // Choosing a specific engine locks the action to 'search' without submitting.
  await waitForSmartbarAction(browser, "search");
  const beforeSubmit = await getStubLoadURLResult(browser);
  Assert.ok(
    !beforeSubmit.called,
    "Selecting a search engine should not submit on its own"
  );

  await submitSmartbar(browser);
  const searchResult = await getStubOpenSERPResult(browser);
  Assert.ok(
    searchResult.called,
    "Submitting after picking an engine should run a search even when the guess was 'chat'"
  );
  Assert.equal(
    searchResult.terms,
    query,
    "Search terms should match the query"
  );

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_smartbar_cta_default_action_available() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  const inputCta = BrowserTestUtils.querySelectorDeep(
    browser.contentDocument,
    "input-cta"
  );

  // The action button defaults to "Ask" before any typing, so the action and
  // its dropdown are always available.
  await BrowserTestUtils.waitForMutationCondition(
    inputCta,
    { attributes: true },
    () => inputCta.action == "chat"
  );
  await inputCta.updateComplete;

  const mozButton = inputCta.shadowRoot.querySelector("moz-button");
  Assert.equal(
    mozButton.type,
    "split",
    "Default action should render the split button with a dropdown"
  );
  Assert.ok(
    !mozButton.disabled,
    "Default action button should be enabled before typing"
  );

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_smartbar_manual_pick_locks_through_typing() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  await typeInSmartbar(browser, "tell me a joke");
  await waitForSmartbarAction(browser, "chat");

  await selectExplicitSmartbarAction(browser, "search");
  await waitForSmartbarAction(browser, "search");

  // Continued typing must not override the locked choice with the live guess.
  await typeInSmartbar(browser, " please");
  const smartbar = BrowserTestUtils.querySelectorDeep(
    browser.contentDocument,
    "#ai-window-smartbar"
  );
  Assert.equal(
    smartbar.smartbarAction,
    "search",
    "A manual pick should stay locked while typing"
  );

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_smartbar_resets_after_submit() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  await stubOpenSERP(browser);
  await typeInSmartbar(browser, "tell me about cats");
  await selectExplicitSmartbarAction(browser, "search");
  await waitForSmartbarAction(browser, "search");
  await submitSmartbar(browser);

  // Submitting clears the manual lock and restores the default action, so the
  // bar guesses again from scratch.
  await waitForSmartbarAction(browser, "chat");

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_smartbar_go_guardrail() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  await stubLoadURL(browser, { captureURL: true });
  await typeInSmartbar(browser, "this is not a url");
  await selectExplicitSmartbarAction(browser, "navigate");
  await waitForSmartbarAction(browser, "navigate");

  // "Go" is blocked while the text isn't URL-shaped: the primary button is
  // inert and submitting does nothing.
  const smartbar = BrowserTestUtils.querySelectorDeep(
    browser.contentDocument,
    "#ai-window-smartbar"
  );
  const inputCta = smartbar.querySelector("input-cta");
  await BrowserTestUtils.waitForMutationCondition(
    inputCta,
    { attributes: true },
    () => inputCta.hasAttribute("submit-disabled")
  );

  await submitSmartbar(browser);
  const blocked = await getStubLoadURLResult(browser);
  Assert.ok(!blocked.called, "Blocked Go submission should do nothing");

  // Replacing the text with a URL clears the guardrail while still locked to Go.
  smartbar.inputField.focus();
  smartbar.setSelectionRange(0, smartbar.value.length);
  EventUtils.sendString("example.com", browser.contentWindow);
  await smartbar.lastQueryContextPromise;
  await BrowserTestUtils.waitForMutationCondition(
    inputCta,
    { attributes: true },
    () =>
      smartbar.smartbarAction == "navigate" &&
      !inputCta.hasAttribute("submit-disabled")
  );

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_smartbar_cta_intent() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  const smartbar = BrowserTestUtils.querySelectorDeep(
    browser.contentDocument,
    "#ai-window-smartbar"
  );
  const inputCta = smartbar.querySelector("input-cta");
  const TEST_QUERIES = [
    { query: "Search for weather", expectedAction: "search" },
    { query: "Hello, how are you?", expectedAction: "chat" },
    { query: "mozilla.com", expectedAction: "navigate" },
  ];
  for (const { query, expectedAction } of TEST_QUERIES) {
    smartbar.focus();

    info("Waiting for action to update to " + expectedAction);
    let toExpected = BrowserTestUtils.waitForMutationCondition(
      inputCta,
      { attributes: true, subtree: true },
      () => inputCta.action == expectedAction
    );
    EventUtils.sendString(query, browser.contentWindow);
    await toExpected;

    info("Backspace the whole string; the action falls back to the default.");
    smartbar.setSelectionRange(0, query.length);
    let toDefault = BrowserTestUtils.waitForMutationCondition(
      inputCta,
      { attributes: true, subtree: true },
      () => inputCta.action == "chat"
    );
    EventUtils.sendKey("BACK_SPACE", browser.contentWindow);
    await toDefault;
  }

  await BrowserTestUtils.closeWindow(win);
});

add_task(
  async function test_smartbar_search_with_falls_back_to_default_engine() {
    const engineName = "AIWindowFallbackTest";
    const searchExtension = await SearchTestUtils.installSearchExtension(
      {
        name: engineName,
        search_url: "https://example.org/aiwindow-test-serp/",
        search_url_get_params: "?q={searchTerms}",
      },
      { skipUnload: true }
    );
    const engineId = SearchService.getEngineByName(engineName);

    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    let extensionUnloaded = false;

    try {
      await stubOpenSERP(browser);
      await typeInSmartbar(browser, "kittens playing");

      // Wait for the freshly installed engine to appear in the CTA submenu.
      const inputCta = BrowserTestUtils.querySelectorDeep(
        browser.contentDocument,
        "input-cta"
      );
      await TestUtils.waitForCondition(() =>
        inputCta.searchEngines.some(e => e.name === engineName)
      );

      // Pick the installed engine from "Search with…", which locks "search" and
      // remembers this engine by name.
      const mozButton = BrowserTestUtils.querySelectorDeep(
        inputCta,
        "moz-button"
      );
      const chevronButton = await BrowserTestUtils.waitForMutationCondition(
        mozButton.shadowRoot,
        { childList: true, subtree: true },
        () => mozButton.shadowRoot.querySelector("#chevron-button")
      );
      const [mainPanel, searchSubpanel] =
        inputCta.shadowRoot.querySelectorAll("panel-list");
      const mainShown = BrowserTestUtils.waitForEvent(mainPanel, "shown");
      chevronButton.click();
      await mainShown;
      const subpanelShown = BrowserTestUtils.waitForEvent(
        searchSubpanel,
        "shown"
      );
      mainPanel.querySelector('panel-item[icon="search-with"]').click();
      await subpanelShown;
      const engineItem = await TestUtils.waitForCondition(() =>
        [...searchSubpanel.querySelectorAll('panel-item[icon="engine"]')].find(
          item => item.textContent.includes(engineName)
        )
      );
      engineItem.click();

      await waitForSmartbarAction(browser, "search");

      // The CTA icon reflects the picked engine, confirming the selection.
      await TestUtils.waitForCondition(
        () => inputCta.searchEngineInfo?.name === engineName
      );

      // Remove the remembered engine before submitting: #submitSearch must fall
      // back to the default engine rather than failing.
      await searchExtension.unload();
      extensionUnloaded = true;

      await submitSmartbar(browser, { useButton: true });

      const searchResult = await getStubOpenSERPResult(browser);
      Assert.ok(
        searchResult.called,
        "Submitting after the engine vanished should still search"
      );
      Assert.notEqual(
        searchResult,
        engineId,
        `Should not use the removed engine`
      );
      Assert.equal(
        searchResult.terms,
        "kittens playing",
        `Search terms are correct`
      );
    } finally {
      if (!extensionUnloaded) {
        await searchExtension.unload();
      }
      await BrowserTestUtils.closeWindow(win);
    }
  }
);

add_task(async function test_smartbar_selected_row_overrides_locked_action() {
  const testUrl = "https://example.com/aiwindow-locked-row/";
  await PlacesTestUtils.addVisits([
    { uri: testUrl, title: "AI Window Locked Row Suggestion" },
  ]);
  registerCleanupFunction(() => PlacesUtils.history.clear());

  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  // The query stays in "chat" mode (no "search"/"hello" keyword), with the
  // seeded history visit appearing as a URL row below the heuristic.
  await promiseSmartbarSuggestionsOpen(browser, () =>
    typeInSmartbar(browser, "aiwindow-locked-row")
  );
  await waitForSmartbarAction(browser, "chat");

  // Lock an action that differs from the row: if the lock won, Enter would run
  // a search instead of navigating to the row's URL.
  await selectExplicitSmartbarAction(browser, "search");
  await waitForSmartbarAction(browser, "search");

  await stubLoadURL(browser, { captureURL: true });

  const smartbar = BrowserTestUtils.querySelectorDeep(
    browser.contentDocument,
    "#ai-window-smartbar"
  );

  const rowIndex = await TestUtils.waitForCondition(() => {
    const rows = [...smartbar.querySelectorAll(".urlbarView-row")];
    const index = rows.findIndex(row => {
      const res = smartbar.view.getResultFromElement(row);
      return (
        res?.type === UrlbarShared.RESULT_TYPE.URL &&
        res.payload.url === testUrl
      );
    });
    return index === -1 ? false : index;
  }, "Wait for the seeded URL suggestion row");

  // Keyboard-selecting the row sets the value from the result, so it is safe
  // to pick on Enter.
  smartbar.view.selectedRowIndex = rowIndex;
  await TestUtils.waitForCondition(
    () => smartbar.value === testUrl && !smartbar.valueIsTyped,
    "Selecting the row should reflect its value into the input"
  );

  smartbar.inputField.focus();
  EventUtils.synthesizeKey("KEY_Enter", {}, browser.contentWindow);

  const { called, url: loadedUrl } = await getStubLoadURLResult(browser);
  Assert.ok(
    called,
    "A keyboard-selected suggestion row should still navigate over a locked action"
  );
  Assert.equal(
    loadedUrl,
    testUrl,
    "Enter should open the selected row, not run the locked search"
  );

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_compact_cta_icon_forced_colors() {
  // A dark palette, so a black icon fill differs from the button text color.
  await SpecialPowers.pushPrefEnv({ set: [["ui.systemUsesDarkTheme", 1]] });
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();
  await typeInSmartbar(sidebarBrowser, "tell me a joke");
  await waitForSmartbarAction(sidebarBrowser, "chat");

  const inputCta = BrowserTestUtils.querySelectorDeep(
    sidebarBrowser.contentDocument,
    "input-cta"
  );
  const mozButton = inputCta.shadowRoot.querySelector("moz-button");
  await mozButton.updateComplete;
  const mainButton = mozButton.shadowRoot.querySelector("#main-button");
  const buttonStyle = sidebarBrowser.contentWindow.getComputedStyle(
    mainButton.querySelector(".button-background")
  );
  Assert.stringContains(
    buttonStyle.backgroundImage,
    "ask-icon.svg",
    "Sanity check: the sidebar shows the compact Ask icon"
  );

  sidebarBrowser.browsingContext.forcedColorsOverride = "active";
  try {
    Assert.ok(
      sidebarBrowser.contentWindow.matchMedia("(forced-colors)").matches,
      "Sanity check: forced colors are active"
    );
    Assert.equal(
      buttonStyle.fill,
      buttonStyle.color,
      "The Ask icon uses the button text color"
    );

    InspectorUtils.addPseudoClassLock(mainButton, ":hover");
    Assert.equal(
      buttonStyle.fill,
      buttonStyle.color,
      "The Ask icon uses the button text color on hover"
    );
  } finally {
    InspectorUtils.removePseudoClassLock(mainButton, ":hover");
    sidebarBrowser.browsingContext.forcedColorsOverride = "none";
    await BrowserTestUtils.closeWindow(win);
    await SpecialPowers.popPrefEnv();
  }
});
