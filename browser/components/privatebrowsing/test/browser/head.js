/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

ChromeUtils.defineESModuleGetters(this, {
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  NimbusTestUtils: "resource://testing-common/NimbusTestUtils.sys.mjs",
  PanelTestProvider: "resource:///modules/asrouter/PanelTestProvider.sys.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

function whenNewWindowLoaded(aOptions, aCallback) {
  let win = OpenBrowserWindow(aOptions);
  let focused = SimpleTest.promiseFocus(win);
  let startupFinished = TestUtils.topicObserved(
    "browser-delayed-startup-finished",
    subject => subject == win
  ).then(() => win);
  Promise.all([focused, startupFinished]).then(results =>
    executeSoon(() => aCallback(results[1]))
  );

  return win;
}

function openWindow(aParent, aOptions) {
  return BrowserWindowTracker.promiseOpenWindow({
    openerWindow: aParent,
    ...aOptions,
  });
}

/**
 * Opens a new private window and loads "about:privatebrowsing" there.
 */
async function openAboutPrivateBrowsing() {
  let win = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
    waitForTabURL: "about:privatebrowsing",
  });
  let tab = win.gBrowser.selectedBrowser;
  return { win, tab };
}

/**
 * Wrapper for openAboutPrivateBrowsing that returns after render is complete
 */
async function openTabAndWaitForRender() {
  let { win, tab } = await openAboutPrivateBrowsing();
  await SpecialPowers.spawn(tab, [], async function () {
    // Wait for render to complete
    await ContentTaskUtils.waitForCondition(() =>
      content.document.documentElement.hasAttribute(
        "PrivateBrowsingRenderComplete"
      )
    );
  });
  return { win, tab };
}

/**
 * Elements of the <moz-promo> layout on about:privatebrowsing. Pass into a
 * content task via SpecialPowers.spawn. See aboutPrivateBrowsing.js/.html.
 */
const PROMO_SELECTORS = {
  container: ".nova-promo-wrapper",
};

/**
 * Fluent ids of the about:privatebrowsing info section.
 */
const INFO_L10N = {
  body: "about-private-browsing-nova-info-body",
  link: "about-private-browsing-nova-info-link",
};

/**
 * Clicks moz-promo's own close button, which lives in its shadow root and so
 * can't be reached with a single selector from a content task.
 *
 * @param {MozBrowser} browser The about:privatebrowsing browser.
 */
async function clickPromoDismissButton(browser) {
  await SpecialPowers.spawn(browser, [], async function () {
    let button;
    await ContentTaskUtils.waitForCondition(
      () =>
        (button = content.document
          .getElementById("nova-promo")
          ?.shadowRoot.querySelector("moz-button.close")),
      "Waiting for the promo close button"
    );
    button.scrollIntoView({ block: "center" });
    await EventUtils.synthesizeClick(button);
  });
}

// Mirrors the bundles loaded by aboutPrivateBrowsing.html: promo strings
// reference brand terms such as -brand-short-name.
ChromeUtils.defineLazyGetter(
  this,
  "aboutPrivateBrowsingL10n",
  () =>
    new Localization(
      ["branding/brand.ftl", "browser/aboutPrivateBrowsing.ftl"],
      true
    )
);

/**
 * Resolves an about:privatebrowsing Fluent id to its localized string in the
 * parent process. Used to assert on the rendered promo header, which is set
 * as plain text on <moz-promo> rather than via a data-l10n-id attribute.
 *
 * @param {string} id The Fluent id.
 * @returns {string} The localized string.
 */
function formatPrivateBrowsingString(id) {
  return aboutPrivateBrowsingL10n.formatValueSync(id);
}

/**
 * Asserts that the default promo shown is the one identified by the given
 * header Fluent id. moz-promo renders the resolved string as its heading text.
 *
 * @param {MozBrowser} tab The about:privatebrowsing browser.
 * @param {string} headerL10nId The expected promo header Fluent id.
 * @param {string} message The assertion message.
 */
async function assertPromoHeader(tab, headerL10nId, message) {
  let expectedText = formatPrivateBrowsingString(headerL10nId);
  await SpecialPowers.spawn(
    tab,
    [{ expectedText, message }],
    async function (args) {
      // The header string is set as the moz-promo heading, which Lit renders
      // into the shadow DOM as `.heading`.
      const promoEl = content.document.getElementById("nova-promo");
      await ContentTaskUtils.waitForMutationCondition(
        promoEl.shadowRoot,
        { childList: true, subtree: true, characterData: true },
        () => promoEl.shadowRoot.querySelector(".heading")?.textContent
      );
      is(
        promoEl.shadowRoot.querySelector(".heading").textContent,
        args.expectedText,
        args.message
      );
    }
  );
}

/**
 * Asserts that the promo identified by the given header Fluent id is NOT the one
 * currently shown. Holds whether the promo slot is empty or shows a different
 * promo, so callers don't need to know which promo (if any) takes its place.
 *
 * @param {MozBrowser} tab The about:privatebrowsing browser.
 * @param {string} headerL10nId The promo header Fluent id that must not show.
 * @param {string} message The assertion message.
 */
async function assertPromoHeaderNot(tab, headerL10nId, message) {
  let notExpectedText = formatPrivateBrowsingString(headerL10nId);
  await SpecialPowers.spawn(
    tab,
    [{ notExpectedText, message }],
    async function (args) {
      const heading = content.document
        .getElementById("nova-promo")
        ?.shadowRoot?.querySelector(".heading")?.textContent;
      isnot(heading, args.notExpectedText, args.message);
    }
  );
}

/**
 * Reads the promo call-to-action text, regardless of CTA type. The promo
 * renders either a link (#nova-promo-link, text as textContent) or a
 * moz-button (#nova-promo-button, text as the `label` attribute) depending on
 * the message's promoLinkType.
 *
 * @param {MozBrowser} tab The about:privatebrowsing browser.
 * @returns {Promise<string>} The CTA text.
 */
function getPromoCtaText(tab) {
  return SpecialPowers.spawn(tab, [], async function () {
    const linkEl = content.document.getElementById("nova-promo-link");
    if (linkEl) {
      return linkEl.textContent;
    }
    const buttonEl = content.document.getElementById("nova-promo-button");
    await ContentTaskUtils.waitForCondition(
      () => buttonEl.getAttribute("label"),
      "Nova promo button label rendered"
    );
    return buttonEl.getAttribute("label");
  });
}

/**
 * Reads the promo's large image src, which moz-promo renders inside its shadow
 * DOM (.image-container img). Returns undefined when no promo (and hence no
 * image) is shown.
 *
 * @param {MozBrowser} tab The about:privatebrowsing browser.
 * @returns {Promise<string|undefined>} The resolved image src, if any.
 */
function getPromoImageSrc(tab) {
  return SpecialPowers.spawn(tab, [], async function () {
    const promoEl = content.document.getElementById("nova-promo");
    if (!promoEl) {
      return undefined;
    }
    let img;
    await ContentTaskUtils.waitForCondition(
      () => (img = promoEl.shadowRoot?.querySelector(".image-container img")),
      "Nova promo image rendered"
    );
    return img.src;
  });
}

function newDirectory() {
  let dir = FileUtils.getDir("TmpD", ["testdir"]);
  dir.createUnique(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);
  return dir;
}

function newFileInDirectory(aDir) {
  let file = aDir.clone();
  file.append("testfile");
  file.createUnique(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_FILE);
  return file;
}

function clearHistory() {
  // simulate clearing the private data
  Services.obs.notifyObservers(null, "browser:purge-session-history");
}

function _initTest() {
  // Don't use about:home as the homepage for new windows
  Services.prefs.setIntPref("browser.startup.page", 0);
  registerCleanupFunction(() =>
    Services.prefs.clearUserPref("browser.startup.page")
  );
}

function waitForTelemetryEvent(category, value) {
  info("waiting for telemetry event");
  return TestUtils.waitForCondition(() => {
    let events = Services.telemetry.snapshotEvents(
      Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
      false
    ).content;

    if (!events) {
      return null;
    }
    events = events.filter(e => e[1] == category);
    info(JSON.stringify(events));

    // Check for experimentId passed as value
    // if exists return events only for specific experimentId
    if (value) {
      events = events.filter(e => e[4].includes(value));
    }
    if (events.length) {
      return events[0];
    }
    return null;
  }, "wait and retrieve telemetry event");
}

async function setupMSExperimentWithMessage(message) {
  Services.telemetry.clearEvents();
  Services.telemetry.snapshotEvents(
    Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
    true
  );
  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig(
    {
      featureId: "pbNewtab",
      value: message,
    },
    { slug: message.id }
  );
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.asrouter.providers.messaging-experiments",
        '{"id":"messaging-experiments","enabled":true,"type":"remote-experiments","updateCycleInMs":0}',
      ],
    ],
  });
  // Reload the provider
  await ASRouter._updateMessageProviders();
  // Wait to load the messages from the messaging-experiments provider
  await ASRouter.loadMessagesFromAllProviders();

  // XXX this only runs at the end of the file, so some of this stuff (eg unblockAll) should be run
  // at the bottom of various test functions too.  Quite possibly other stuff beside unblockAll too.
  registerCleanupFunction(async () => {
    // Clear telemetry side effects
    Services.telemetry.clearEvents();
    // Make sure the side-effects from dismisses are cleared.
    ASRouter.unblockAll();
    // put the disabled providers back
    SpecialPowers.popPrefEnv();
    // Reload the provider again at cleanup to remove the experiment message
    await ASRouter._updateMessageProviders();
    // Wait to load the messages from the messaging-experiments provider
    await ASRouter.loadMessagesFromAllProviders();
  });

  Assert.ok(
    ASRouter.state.messages.find(m => m.id.includes(message.id)),
    "Experiment message found in ASRouter state"
  );

  return doExperimentCleanup;
}

_initTest();
