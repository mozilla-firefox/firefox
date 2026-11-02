/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const { SearchService } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/search/SearchService.sys.mjs"
);
const { SearchTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SearchTestUtils.sys.mjs"
);

SearchTestUtils.init(this);

add_task(async function () {
  // Our search would be handled by the urlbar normally and not by the docshell,
  // thus we must force going through dns first, so that the urlbar thinks
  // the value may be a url, and asks the docshell to visit it.
  // On NS_ERROR_UNKNOWN_HOST the docshell will fix it up.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.fixup.dns_first_for_single_words", true]],
  });
  const kSearchEngineName = "test_urifixup_search_engine";
  await SearchTestUtils.installSearchExtension(
    {
      name: kSearchEngineName,
      search_url: "http://localhost/",
      search_url_get_params: "search={searchTerms}",
    },
    { setAsDefault: true }
  );

  let defaultEngine = await SearchService.getDefault();
  Assert.equal(
    defaultEngine.name,
    kSearchEngineName,
    "Check fake search engine is selected"
  );

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  gBrowser.selectedTab = tab;

  gURLBar.value = "firefox";
  gURLBar.handleCommand();

  let [subject, data] = await TestUtils.topicObserved("keyword-search");

  let engineId = subject.QueryInterface(Ci.nsIURIFixupInfo).keywordProviderId;

  Assert.equal(engineId, defaultEngine.id, "Should be the search engine id");
  Assert.equal(data, "firefox", "Notification data is search term.");

  gBrowser.removeTab(tab);
});
