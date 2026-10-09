"use strict";

const { SearchService } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/search/SearchService.sys.mjs"
);
const { SearchTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SearchTestUtils.sys.mjs"
);

SearchTestUtils.init(this);

add_setup(async function () {
  // Set up the configuration so that Baidu is available as a search engine, so
  // the search shortcut works.
  await SearchTestUtils.updateRemoteSettingsConfig([
    {
      identifier: "google",
      base: {
        name: "Google",
        aliases: ["google"],
        urls: {
          search: {
            base: "https://www.google.com/search",
            searchTermParamName: "q",
          },
        },
      },
    },
    {
      identifier: "baidu",
      base: {
        name: "百度",
        aliases: ["百度", "baidu"],
        urls: {
          search: {
            base: "https://www.baidu.com/baidu",
            searchTermParamName: "wd",
          },
        },
      },
    },
  ]);
});

// Check TopSites edit modal and overlay show up.
test_newtab({
  before: setTestTopSites,
  // it should be able to click the topsites add button to reveal the add top site modal and overlay.
  test: async function topsites_edit() {
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".top-sites .context-menu-button"),
      "Should find a visible topsite context menu button [topsites_edit]"
    );

    // Open the section context menu.
    content.document.querySelector(".top-sites .context-menu-button").click();

    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".top-sites .context-menu"),
      "Should find a visible topsite context menu [topsites_edit]"
    );

    const topsitesAddBtn = content.document.querySelector(
      ".top-sites li:nth-child(2) button"
    );
    topsitesAddBtn.click();

    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".topsite-form"),
      "Should find a visible topsite form"
    );

    let found = content.document.querySelector(".topsite-form");
    ok(found && !found.hidden, "Should find a visible topsite form");

    found = content.document.querySelector(".modalOverlayOuter");
    ok(found && !found.hidden, "Should find a visible overlay");
  },
});

// Test pin/unpin context menu options.
test_newtab({
  before: setDefaultTopSites,
  // it should pin the website when we click the first option of the topsite context menu.
  test: async function topsites_pin_unpin() {
    const siteSelector = ".top-site-outer:not(.search-shortcut, .placeholder)";
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(siteSelector),
      "Topsite tippytop icon not found"
    );
    // There are only topsites on the page, the selector with find the first topsite menu button.
    let topsiteEl = content.document.querySelector(siteSelector);
    let topsiteContextBtn = topsiteEl.querySelector(".context-menu-button");
    topsiteContextBtn.click();

    await ContentTaskUtils.waitForCondition(
      () => topsiteEl.querySelector(".top-sites-list .context-menu"),
      "No context menu found"
    );

    let contextMenu = topsiteEl.querySelector(".top-sites-list .context-menu");
    ok(contextMenu, "Should find a topsite context menu");

    const pinUnpinTopsiteBtn = contextMenu.querySelector(
      ".top-sites .context-menu-item button"
    );
    // Pin the topsite.
    pinUnpinTopsiteBtn.click();

    // Need to wait for pin action.
    await ContentTaskUtils.waitForCondition(
      () => topsiteEl.querySelector(".icon-pin-small"),
      "No pinned icon found"
    );

    let pinnedIcon = topsiteEl.querySelectorAll(".icon-pin-small").length;
    is(pinnedIcon, 1, "should find 1 pinned topsite");

    // Unpin the topsite.
    topsiteContextBtn = topsiteEl.querySelector(".context-menu-button");
    ok(topsiteContextBtn, "Should find a context menu button");
    topsiteContextBtn.click();

    await ContentTaskUtils.waitForCondition(
      () => topsiteEl.querySelector(".context-menu-item button"),
      "Should find context menu item button for unpin"
    );
    topsiteEl.querySelector(".context-menu-item button").click();

    // Need to wait for unpin action.
    await ContentTaskUtils.waitForCondition(
      () => !topsiteEl.querySelector(".icon-pin-small"),
      "Topsite should be unpinned"
    );
  },
});

// Check Topsites add
test_newtab({
  before: setTestTopSites,
  // it should be able to click the topsites edit button to reveal the edit topsites modal and overlay.
  test: async function topsites_add() {
    let nativeInputValueSetter = Object.getOwnPropertyDescriptor(
      content.window.HTMLInputElement.prototype,
      "value"
    ).set;
    let event = new content.Event("input", { bubbles: true });

    // Wait for context menu button to load
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".top-sites .context-menu-button"),
      "Should find a visible topsite context menu button [topsites_add]"
    );

    content.document.querySelector(".top-sites .context-menu-button").click();

    // Wait for context menu to load
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".top-sites .context-menu"),
      "Should find a visible topsite context menu [topsites_add]"
    );

    // Find topsites edit button
    const topsitesAddBtn = content.document.querySelector(
      ".top-sites li:nth-child(2) button"
    );

    topsitesAddBtn.click();

    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".modalOverlayOuter"),
      "No overlay found"
    );

    let found = content.document.querySelector(".modalOverlayOuter");
    ok(found && !found.hidden, "Should find a visible overlay");

    // Write field title
    let fieldTitle = content.document.querySelector(".field input");
    ok(fieldTitle && !fieldTitle.hidden, "Should find field title input");

    nativeInputValueSetter.call(fieldTitle, "Bugzilla");
    fieldTitle.dispatchEvent(event);
    is(fieldTitle.value, "Bugzilla", "The field title should match");

    // Write field url
    let fieldURL = content.document.querySelector(".field.url input");
    ok(fieldURL && !fieldURL.hidden, "Should find field url input");

    nativeInputValueSetter.call(fieldURL, "https://bugzilla.mozilla.org");
    fieldURL.dispatchEvent(event);
    is(
      fieldURL.value,
      "https://bugzilla.mozilla.org",
      "The field url should match"
    );

    // Click the "Add" button
    await ContentTaskUtils.waitForCondition(
      () => content.document.getElementById("topsites-form-save-button"),
      "No add button found"
    );
    let addBtn = content.document.getElementById("topsites-form-save-button");
    addBtn.click();

    // Wait for Topsite to be populated
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelector("[href='https://bugzilla.mozilla.org']"),
      "No Topsite found"
    );

    // Remove topsite after test is complete
    let topsiteContextBtn = content.document.querySelector(
      ".top-sites-list li:nth-child(1) .context-menu-button"
    );
    topsiteContextBtn.click();
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".top-sites-list .context-menu"),
      "No context menu found"
    );

    const dismissBtn = content.document.querySelector(
      ".top-sites li:nth-child(7) button"
    );
    dismissBtn.click();

    // Wait for Topsite to be removed
    await ContentTaskUtils.waitForCondition(
      () =>
        !content.document.querySelector(
          "[href='https://bugzilla.mozilla.org']"
        ),
      "Topsite not removed"
    );
    // This was set when the topsite was removed, we need to reset it.
    SpecialPowers.clearUserPref("browser.newtabpage.blocked");
  },
});

test_newtab({
  before: setDefaultTopSites,
  test: async function test_search_topsite_keyword() {
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".search-shortcut .title.pinned"),
      "Wait for pinned search topsites"
    );

    const searchTopSites = content.document.querySelectorAll(".title.pinned");
    Assert.greaterOrEqual(
      searchTopSites.length,
      1,
      "There should be at least 1 search topsites"
    );

    searchTopSites[0].click();

    return searchTopSites[0].innerText.trim();
  },
  async after(searchTopSiteTag) {
    ok(
      gURLBar.focused,
      "We clicked a search topsite the focus should be in location bar"
    );

    let engine = await SearchService.getEngineByAlias(searchTopSiteTag);

    // We don't use UrlbarTestUtils.assertSearchMode here since the newtab
    // testing scope doesn't integrate well with UrlbarTestUtils.
    Assert.deepEqual(
      gURLBar.searchMode,
      {
        engineName: engine.name,
        entry: "topsites_newtab",
        isPreview: false,
        isGeneralPurposeEngine: true,
        source: UrlbarUtils.RESULT_SOURCE.SEARCH,
      },
      "The Urlbar is in search mode."
    );
    ok(
      gURLBar.hasAttribute("searchmode"),
      "The Urlbar has the searchmode attribute."
    );
  },
});

// test_newtab is not used here as this test requires two steps into the
// content process with chrome process activity in-between.
add_task(async function test_search_topsite_remove_engine() {
  // Open about:newtab without using the default load listener
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:newtab",
    false
  );

  // Specially wait for potentially preloaded browsers
  let browser = tab.linkedBrowser;
  await waitForPreloaded(browser);

  // Add shared helpers to the content process
  SpecialPowers.spawn(browser, [], addContentHelpers);

  // Wait for React to render something
  await BrowserTestUtils.waitForCondition(
    () =>
      SpecialPowers.spawn(
        browser,
        [],
        () => content.document.getElementById("root")?.children.length
      ),
    "Should render activity stream content"
  );

  await setDefaultTopSites();

  let [topSiteAlias, numTopSites] = await SpecialPowers.spawn(
    browser,
    [],
    async () => {
      await ContentTaskUtils.waitForCondition(
        () => content.document.querySelector(".search-shortcut .title.pinned"),
        "Wait for pinned search topsites"
      );

      const searchTopSites = content.document.querySelectorAll(".title.pinned");
      Assert.greaterOrEqual(
        searchTopSites.length,
        1,
        "There should be at least one topsite"
      );
      return [searchTopSites[0].innerText.trim(), searchTopSites.length];
    }
  );

  await SearchService.removeEngine(
    await SearchService.getEngineByAlias(topSiteAlias)
  );

  registerCleanupFunction(() => {
    SearchService.restoreDefaultEngines();
  });

  await SpecialPowers.spawn(
    browser,
    [numTopSites],
    async originalNumTopSites => {
      await ContentTaskUtils.waitForCondition(
        () => !content.document.querySelector(".search-shortcut .title.pinned"),
        "Wait for pinned search topsites"
      );

      const searchTopSites = content.document.querySelectorAll(".title.pinned");
      is(
        searchTopSites.length,
        originalNumTopSites - 1,
        "There should be one less search topsites"
      );
    }
  );

  BrowserTestUtils.removeTab(tab);
});
