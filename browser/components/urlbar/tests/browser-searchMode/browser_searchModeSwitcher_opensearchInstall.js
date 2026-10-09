/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const ENGINE_TEST_URL =
  "http://mochi.test:8888/browser/browser/components/search/test/browser/opensearch.html";

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.scotchBonnet.enableOverride", true],
      // Tests get no default Top Sites, and testInstallEngine opens the view on
      // an empty string, which would otherwise leave it closed with no results.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });
});

add_task(async () => {
  info("Test installing via keyboard.");
  await testInstallEngine(() => {
    EventUtils.synthesizeKey("KEY_ArrowUp");
    EventUtils.synthesizeKey("KEY_ArrowUp");
    EventUtils.synthesizeKey("KEY_Enter");
  }, "");

  await testInstallEngine(() => {
    EventUtils.synthesizeKey("KEY_ArrowUp");
    EventUtils.synthesizeKey("KEY_ArrowUp");
    EventUtils.synthesizeKey("KEY_Enter");
  }, "sample string");

  info("Test installing via mouse.");
  await testInstallEngine(popup => {
    let item = popup.querySelector("panel-item[data-engine-name=engine1]");
    EventUtils.synthesizeMouseAtCenter(item, {});
  }, "");
});

async function testInstallEngine(installFun, testString) {
  info("Test installing opensearch engine");
  await BrowserTestUtils.loadURIString({
    browser: gBrowser.selectedBrowser,
    uriString: ENGINE_TEST_URL,
  });

  let promiseEngineAdded = SearchTestUtils.promiseEngine("Foo");

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: testString,
  });

  let popup = await UrlbarTestUtils.openSearchModeSwitcher(window);
  info("Waiting for installFun.");
  await installFun(popup);
  info("Waiting for engine to be added.");
  let engine = await promiseEngineAdded;
  Assert.ok(true, "The engine was installed.");

  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "Foo",
    entry: "searchbutton",
  });

  Assert.equal(
    gURLBar.value,
    testString,
    "Preserve the url contents when entering search mode"
  );

  await UrlbarTestUtils.exitSearchMode(window, {
    backspace: true,
    waitForSearch: false,
  });

  await UrlbarTestUtils.promisePopupClose(window);

  let promiseEngineRemoved = SearchTestUtils.promiseSearchNotification(
    SearchUtils.MODIFIED_TYPE.REMOVED,
    SearchUtils.TOPIC_ENGINE_MODIFIED
  );
  let settingsWritten = SearchTestUtils.promiseSearchNotification(
    "write-settings-to-disk-complete"
  );
  await Promise.all([
    SearchService.removeEngine(engine),
    promiseEngineRemoved,
    settingsWritten,
  ]);
}
