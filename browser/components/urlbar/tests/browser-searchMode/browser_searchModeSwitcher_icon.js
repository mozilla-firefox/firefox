/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const CONFIG = [
  { identifier: "engine1" },
  { identifier: "engine2" },
  { identifier: "engineNoIcon" },
];

const ICONS = {
  engine1: "data:image/x-icon;base64,R0lGODlhAQABAAAAACwAAAAAAQABAAA=",
  engine2:
    "data:image/gif;base64,R0lGODlhAQABAIAAAAUEBAAAACwAAAAAAQABAAACAkQBADs=",
};

add_setup(async function setup() {
  let iconStub = sinon
    .stub(AppProvidedConfigEngine.prototype, "getIconURL")
    .callsFake(async function () {
      return ICONS[this.id] ?? null;
    });
  await SearchTestUtils.updateRemoteSettingsConfig(CONFIG);

  UrlbarTestUtils.registerZeroPrefixProvider();
  registerCleanupFunction(() => iconStub.restore());
});

add_task(async function test_icon_new_window() {
  // The switcher's initial icon needs the engine store populated before it
  // first renders, which the message path can't do: the store's snapshot
  // arrives from the parent, so the fallback icon shows until then
  // (bug 2059513). An input picks its transport at construction, so setting the
  // pref here applies to the window opened below.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.ipc.chromeMessagePassing", false]],
  });

  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  await UrlbarTestUtils.assertSearchModeSwitcherIcon(
    newWin,
    await SearchService.defaultEngine.getIconURL(),
    "The search mode switcher should already have the engine favicon."
  );

  await BrowserTestUtils.closeWindow(newWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_search_icon_change() {
  await SpecialPowers.pushPrefEnv({
    set: [["keyword.enabled", false]],
  });

  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  const globeIconUrl = UrlbarShared.ICON.GLOBE;

  Assert.equal(
    UrlbarTestUtils.getSearchModeSwitcherIcon(newWin),
    globeIconUrl,
    "The search mode switcher should have the globe icon url since keyword.enabled is false"
  );

  let popup = UrlbarTestUtils.searchModeSwitcherPopup(newWin);
  let engine1 = SearchService.getEngineById("engine1");
  info("Open the urlbar and searchmode switcher popup");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: newWin,
    value: "",
  });
  await UrlbarTestUtils.openSearchModeSwitcher(newWin);
  info("Press on the engine1 menu button and enter search mode");
  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(newWin);
  popup.querySelector(`panel-item[data-engine-id=${engine1.id}]`).click();
  await popupHidden;

  await UrlbarTestUtils.assertSearchModeSwitcherIcon(
    newWin,
    await engine1.getIconURL(),
    "The search mode switcher should have the engine1 icon since we are in \
     search mode"
  );
  await UrlbarTestUtils.assertSearchMode(newWin, {
    engineName: engine1.name,
    entry: "searchbutton",
    source: 3,
  });

  info("Press the close button and exit search mode");
  newWin.gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(newWin, null);

  let searchModeSwitcherIconUrl = await TestUtils.waitForCondition(
    () => UrlbarTestUtils.getSearchModeSwitcherIcon(newWin),
    "Waiting for the search mode switcher icon to update after exiting search mode."
  );

  Assert.equal(
    searchModeSwitcherIconUrl,
    globeIconUrl,
    "The search mode switcher should have the globe icon url since keyword.enabled is false"
  );

  await BrowserTestUtils.closeWindow(newWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_globe_does_not_wait_for_engine_store() {
  let defaultEngineIcon = UrlbarTestUtils.getSearchModeSwitcherIcon(window);
  let { promise, resolve } = Promise.withResolvers();
  let stub = sinon
    .stub(gURLBar.controller.engineStore, "init")
    .returns(promise);

  await SpecialPowers.pushPrefEnv({ set: [["keyword.enabled", false]] });
  await TestUtils.waitForCondition(
    () =>
      UrlbarTestUtils.searchModeSwitcherIconIs(window, UrlbarShared.ICON.GLOBE),
    "Globe is shown while the engine store is still initializing."
  );

  resolve();
  stub.restore();
  await SpecialPowers.popPrefEnv();
  await TestUtils.waitForCondition(
    () => UrlbarTestUtils.searchModeSwitcherIconIs(window, defaultEngineIcon),
    "Default engine icon is shown again."
  );
});

add_task(async function test_search_mode_switcher_engine_no_icon() {
  await UrlbarTestUtils.activateSearchModeSwitcherItem(
    window,
    `panel-item[data-engine-id="engineNoIcon"]`
  );

  Assert.equal(
    UrlbarTestUtils.getSearchModeSwitcherIcon(window),
    UrlbarShared.ICON.SEARCH_GLASS,
    "The search mode switcher should display the default search glass icon when the engine has no icon."
  );

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);
});

add_task(async function test_search_mode_switcher_private_engine_icon() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.separatePrivateDefault.featureGate", true],
      // This test also asserts the switcher's icon in a window it opens itself;
      // see test_icon_new_window.
      ["browser.urlbar.ipc.chromeMessagePassing", false],
    ],
  });

  const defaultPrivateEngine = SearchService.getEngineById("engine2");
  const defaultPrivateEngineIcon = await defaultPrivateEngine.getIconURL();
  const defaultEngine = await SearchService.getDefault();
  const defaultEngineIcon = await defaultEngine.getIconURL();

  SearchService.setDefaultPrivate(
    defaultPrivateEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );

  Assert.notEqual(
    defaultEngine.id,
    defaultPrivateEngine.id,
    "Default engine is not private engine."
  );
  Assert.equal(
    (await SearchService.getDefault()).id,
    defaultEngine.id,
    "Default engine is still correct."
  );
  Assert.equal(
    (await SearchService.getDefaultPrivate()).id,
    defaultPrivateEngine.id,
    "Default private engine is correct."
  );

  await UrlbarTestUtils.assertSearchModeSwitcherIcon(
    window,
    defaultEngineIcon,
    "Is the icon of the default engine."
  );

  info("Open a private window");
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  info("Input any text to update the icon");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: privateWin,
    value: "abc",
  });

  await UrlbarTestUtils.assertSearchModeSwitcherIcon(
    privateWin,
    defaultPrivateEngineIcon,
    "Is the icon of the default private engine."
  );

  info("Changing the default private engine.");
  SearchService.setDefaultPrivate(
    defaultEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );

  info("Waiting for the icon to be updated.");
  await TestUtils.waitForCondition(() =>
    UrlbarTestUtils.searchModeSwitcherIconIs(privateWin, defaultEngineIcon)
  );
  Assert.ok(true, "The icon was updated.");

  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();
});
