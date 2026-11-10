"use strict";

// test_newtab calls SpecialPowers.spawn, which injects ContentTaskUtils in the
// scope of the callback. Eslint doesn't know about that.
/* global ContentTaskUtils */

const { WeatherFeed } = ChromeUtils.importESModule(
  "resource://newtab/lib/WeatherFeed.sys.mjs"
);

const { DiscoveryStreamFeed } = ChromeUtils.importESModule(
  "resource://newtab/lib/DiscoveryStreamFeed.sys.mjs"
);
const { PREFS_CONFIG } = ChromeUtils.importESModule(
  "resource://newtab/lib/ActivityStream.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  GeolocationTestUtils:
    "resource://testing-common/GeolocationTestUtils.sys.mjs",
  MerinoTestUtils: "resource://testing-common/MerinoTestUtils.sys.mjs",
});

const { WEATHER_SUGGESTION } = MerinoTestUtils;

add_setup(async function () {
  let sandbox = sinon.createSandbox();

  sandbox
    .stub(DiscoveryStreamFeed.prototype, "generateFeedUrl")
    .returns(
      "https://example.com/browser/browser/extensions/newtab/test/browser/topstories.json"
    );

  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.discoverystream.config",
        PREFS_CONFIG.get("discoverystream.config").getValue({
          geo: "US",
          locale: "en-US",
        }),
      ],
      [
        "browser.newtabpage.activity-stream.discoverystream.endpoints",
        "https://example.com",
      ],
    ],
  });

  registerCleanupFunction(async () => {
    sandbox.restore();
    await SpecialPowers.popPrefEnv();
  });

  GeolocationTestUtils.init(this);
  GeolocationTestUtils.stubGeolocation(GeolocationTestUtils.SAN_FRANCISCO);
});

test_newtab({
  async before({ pushPrefs }) {
    await pushPrefs(
      ["browser.newtabpage.activity-stream.feeds.topsites", false],
      ["browser.newtabpage.activity-stream.feeds.section.topstories", false]
    );
  },
  test: async function test_render_customizeMenu() {
    function getSection(sectionIdentifier) {
      return content.document.querySelector(
        `section[data-section-id="${sectionIdentifier}"]`
      );
    }
    function promiseSectionShown(sectionIdentifier) {
      return ContentTaskUtils.waitForMutationCondition(
        content.document.querySelector("main"),
        { childList: true, subtree: true },
        () => getSection(sectionIdentifier)
      );
    }
    const TOPSITES_PREF = "browser.newtabpage.activity-stream.feeds.topsites";
    const TOPSTORIES_PREF =
      "browser.newtabpage.activity-stream.feeds.section.topstories";

    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelector(
          ".personalize-button, .open-customization-button"
        ),
      "Wait for prefs button to load on the newtab page"
    );

    let customizeButton = content.document.querySelector(
      ".personalize-button, .open-customization-button"
    );
    customizeButton.click();

    await ContentTaskUtils.waitForCondition(
      () =>
        content.document
          .querySelector(".customize-menu")
          .classList.contains("customize-animate-enter-done"),
      "Customize Menu should have finished opening"
    );

    // Test that clicking the shortcuts toggle will make the section
    // appear on the newtab page.
    //
    // We waive XRay wrappers because we want to call the click()
    // method defined on the toggle from this context.
    let shortcutsSwitch = Cu.waiveXrays(
      content.document.querySelector("#shortcuts-section moz-toggle")
    );
    Assert.ok(
      !Services.prefs.getBoolPref(TOPSITES_PREF),
      "Topsites are turned off"
    );
    Assert.ok(!getSection("topsites"), "Shortcuts section is not rendered");

    let sectionShownPromise = promiseSectionShown("topsites");
    shortcutsSwitch.click();
    await sectionShownPromise;

    Assert.ok(getSection("topsites"), "Shortcuts section is rendered");

    // Test that clicking the pocket toggle will make the pocket section
    // appear on the newtab page
    //
    // We waive XRay wrappers because we want to call the click()
    // method defined on the toggle from this context.
    let pocketSwitch = Cu.waiveXrays(
      content.document.querySelector("#pocket-section moz-toggle")
    );
    Assert.ok(
      !Services.prefs.getBoolPref(TOPSTORIES_PREF),
      "Pocket pref is turned off"
    );
    Assert.ok(!getSection("topstories"), "Pocket section is not rendered");

    sectionShownPromise = promiseSectionShown("topstories");
    pocketSwitch.click();
    await sectionShownPromise;

    Assert.ok(getSection("topstories"), "Pocket section is rendered");
  },
  async after() {
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.feeds.topsites"
    );
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.feeds.section.topstories"
    );
  },
});

test_newtab({
  async before({ pushPrefs }) {
    // @nova-cleanup(remove-pref): Remove novaEnabled detection
    const novaEnabled = Services.prefs.getBoolPref(
      "browser.newtabpage.activity-stream.nova.enabled",
      false
    );

    // WeatherFeed calls fetchWeatherReport() on the merino client instance.
    // The old "fetch" method no longer exists on the client.
    sinon.stub(WeatherFeed.prototype, "MerinoClient").returns({
      fetchWeatherReport: () => Promise.resolve(WEATHER_SUGGESTION),
      fetchHourlyForecasts: () => Promise.resolve([]),
    });

    const prefs = [
      ["browser.newtabpage.activity-stream.system.showWeather", true],
      ["browser.newtabpage.activity-stream.widgets.system.enabled", true],
      ["browser.newtabpage.activity-stream.widgets.enabled", true],
      [
        "browser.newtabpage.activity-stream.widgets.system.weather.enabled",
        true,
      ],
      ["browser.newtabpage.activity-stream.widgets.weather.enabled", false],
      ["browser.newtabpage.activity-stream.widgets.weather.size", "small"],
    ];

    // @nova-cleanup(remove-conditional): Remove this block; showWeather is a classic-only pref
    if (!novaEnabled) {
      prefs.push(["browser.newtabpage.activity-stream.showWeather", false]);
    }

    await pushPrefs(...prefs);
  },
  test: async function test_render_customizeMenuWeather() {
    // @nova-cleanup(remove-pref): Remove novaEnabled detection
    const novaEnabled = Services.prefs.getBoolPref(
      "browser.newtabpage.activity-stream.nova.enabled",
      false
    );

    // @nova-cleanup(remove-conditional): Remove novaEnabled check; always use widgets.weather.enabled
    const WEATHER_PREF = novaEnabled
      ? "browser.newtabpage.activity-stream.widgets.weather.enabled"
      : "browser.newtabpage.activity-stream.showWeather";

    // @nova-cleanup(remove-conditional): Remove novaEnabled check; always use .weather-widget
    function getWeatherWidget() {
      return novaEnabled
        ? content.document.querySelector(".weather-widget")
        : content.document.querySelector(".weather");
    }

    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelector(
          ".personalize-button, .open-customization-button"
        ),
      "Wait for prefs button to load on the newtab page"
    );

    let customizeButton = content.document.querySelector(
      ".personalize-button, .open-customization-button"
    );
    customizeButton.click();

    await ContentTaskUtils.waitForCondition(
      () =>
        content.document
          .querySelector(".customize-menu")
          .classList.contains("customize-animate-enter-done"),
      "Customize Menu should have finished opening"
    );

    // @nova-cleanup(remove-conditional): Remove novaEnabled guard; always open the widgets sub-panel
    if (novaEnabled) {
      await ContentTaskUtils.waitForCondition(
        () =>
          content.document.querySelector(
            "#widgets-management-panel moz-box-button"
          ),
        "Widgets management button should be present"
      );
      Cu.waiveXrays(
        content.document.querySelector(
          "#widgets-management-panel moz-box-button"
        )
      ).click();
    }

    // Wait for the weather toggle to be present in the DOM (it is unmounted
    // until the sub-panel opens).
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("#weather-toggle"),
      "Weather section toggle should be present"
    );

    // We waive XRay wrappers because we want to call the click()
    // method defined on the toggle from this context.
    let weatherSwitch = Cu.waiveXrays(
      content.document.querySelector("#weather-toggle")
    );
    Assert.ok(
      !Services.prefs.getBoolPref(WEATHER_PREF),
      "Weather pref is turned off"
    );
    Assert.ok(!getWeatherWidget(), "Weather widget is not rendered");

    let sectionShownPromise = ContentTaskUtils.waitForCondition(
      () => getWeatherWidget(),
      "Weather widget should be rendered"
    );
    weatherSwitch.click();
    await sectionShownPromise;

    Assert.ok(getWeatherWidget(), "Weather widget is rendered");
  },
  async after() {
    sinon.restore();
    // @nova-cleanup(remove-conditional): Remove; showWeather is a classic-only pref
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.showWeather"
    );
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.system.showWeather"
    );
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.widgets.weather.enabled"
    );
  },
});

test_newtab({
  test: async function test_open_close_customizeMenu() {
    const EventUtils = ContentTaskUtils.getEventUtils(content);
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelector(
          ".personalize-button, .open-customization-button"
        ),
      "Wait for prefs button to load on the newtab page"
    );

    let customizeButton = content.document.querySelector(
      ".personalize-button, .open-customization-button"
    );
    customizeButton.click();

    await ContentTaskUtils.waitForCondition(
      () =>
        content.document
          .querySelector(".customize-menu")
          .classList.contains("customize-animate-enter-done"),
      "Customize Menu should have finished opening"
    );

    await ContentTaskUtils.waitForCondition(
      () => content.document.activeElement.id === "close-button",
      "Close button should be focused when menu becomes visible"
    );

    await ContentTaskUtils.waitForCondition(
      () =>
        content.getComputedStyle(
          content.document.querySelector(
            ".personalize-button, .open-customization-button"
          )
        ).visibility === "hidden",
      "Personalize button should become hidden"
    );

    // Test close button.
    let closeButton = content.document.querySelector("#close-button");
    closeButton.click();
    await ContentTaskUtils.waitForCondition(
      () => !content.document.querySelector(".customize-menu").open,
      "Customize Menu should have closed"
    );

    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.activeElement.classList.contains(
          "personalize-button"
        ) ||
        content.document.activeElement.classList.contains(
          "open-customization-button"
        ),
      "Personalize button should be focused when menu closes"
    );

    await ContentTaskUtils.waitForCondition(
      () =>
        content.getComputedStyle(
          content.document.querySelector(
            ".personalize-button, .open-customization-button"
          )
        ).visibility === "visible",
      "Personalize button should become visible"
    );

    // Reopen the customize menu
    customizeButton.click();
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document
          .querySelector(".customize-menu")
          .classList.contains("customize-animate-enter-done"),
      "Customize Menu should have finished opening again"
    );

    // Test closing with esc key.
    EventUtils.synthesizeKey("VK_ESCAPE", {}, content);
    await ContentTaskUtils.waitForCondition(
      () => !content.document.querySelector(".customize-menu").open,
      "Customize Menu should have closed"
    );

    // Reopen the customize menu
    customizeButton.click();
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document
          .querySelector(".customize-menu")
          .classList.contains("customize-animate-enter-done"),
      "Customize Menu should have finished opening again"
    );

    // Test closing with external click. With the dialog element, clicking outside
    // the panel content fires on the dialog element itself, so we click it directly.
    content.document.querySelector(".customize-menu").click();
    await ContentTaskUtils.waitForCondition(
      () => !content.document.querySelector(".customize-menu").open,
      "Customize Menu should have closed"
    );
  },
});

test_newtab({
  async before({ pushPrefs }) {
    // The transition this test is about is only declared under
    // (prefers-reduced-motion: no-preference).
    await pushPrefs(["ui.prefersReducedMotion", 0]);
  },
  test: async function test_focus_customizeMenuOnEnter() {
    let dialog = content.document.querySelector(".customize-menu");
    let closeButton = content.document.querySelector("#close-button");

    // Drive the panel the way CustomizeMenu does, but with no style flush
    // during the enter phase: react-transition-group resolves style once with
    // only customize-animate-enter applied, then adds the class that makes the
    // panel visible, and onEntered focuses the close button. Transitioning
    // visibility on the way in makes that first resolution the transition's
    // first sample, which still computes hidden, and focus() on a hidden
    // subtree is a silent no-op.
    for (let visibleClass of [
      "customize-animate-enter-active",
      "customize-animate-enter-done",
    ]) {
      dialog.showModal();
      dialog.classList.add("customize-animate-enter");
      dialog.getBoundingClientRect();
      dialog.classList.add(visibleClass);
      closeButton.focus();

      Assert.equal(
        content.getComputedStyle(dialog).visibility,
        "visible",
        `Panel is visible as soon as ${visibleClass} is applied`
      );
      Assert.equal(
        content.document.activeElement.id,
        "close-button",
        `Close button is focusable with ${visibleClass} applied`
      );

      dialog.classList.remove("customize-animate-enter", visibleClass);
      dialog.close();
    }
  },
});
