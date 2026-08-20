/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint max-len: ["error", 80] */
"use strict";

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

AddonTestUtils.initMochitest(this);

const AMO_TEST_HOST = "addons.allizom.org";
// eslint-disable-next-line sdl/no-insecure-url
const AMO_TEST_URL = `http://${AMO_TEST_HOST}/`;

const amoServer = AddonTestUtils.createHttpServer({
  hosts: [AMO_TEST_HOST],
});
amoServer.registerPrefixHandler("/", (request, response) => {
  response.write("");
});

function makeResult({ guid, type }) {
  return {
    addon: {
      authors: [{ name: "Some author" }],
      current_version: {
        files: [{ platform: "all", url: "data:," }],
      },
      url: "data:,",
      guid,
      type,
    },
  };
}

function mockResults() {
  let types = ["extension", "theme", "extension", "extension", "theme"];
  return {
    results: types.map((type, i) =>
      makeResult({
        guid: `${type}${i}@mochi.test`,
        type,
      })
    ),
  };
}

add_setup(async function () {
  let results = btoa(JSON.stringify(mockResults()));
  await SpecialPowers.pushPrefEnv({
    set: [
      // Disable personalized recommendations, they will break the data URI.
      ["browser.discovery.enabled", false],
      ["extensions.getAddons.discovery.api_url", `data:;base64,${results}`],
      [
        "extensions.recommendations.themeRecommendationUrl",
        "https://example.com/theme",
      ],
    ],
  });
});

const PROMO_L10N_IDS = {
  extension: [
    "find-more-extensions-promo",
    "find-more-extensions-promo-open-amo-button",
  ],
  theme: ["find-more-themes-promo", "find-more-themes-promo-open-amo-button"],
};

function checkExtraContents(doc, type, opts = {}) {
  let { showThemeRecommendationFooter = type === "theme" } = opts;
  let footer = doc.querySelector("footer");
  let amoButton = footer.querySelector('button[action="open-amo"]');
  let footerPromo = footer.querySelector("addons-promo");
  let promoAmoBtn = footer.querySelector('moz-button[action="open-amo"]');
  let privacyPolicyLink = footer.querySelector(".privacy-policy-link");
  let themeRecommendationFooter = footer.querySelector(".theme-recommendation");
  let themeRecommendationLink =
    themeRecommendationFooter && themeRecommendationFooter.querySelector("a");
  let taarNotice = doc.querySelector("taar-notice");

  is_element_visible(footer, "The footer is visible");

  if (type == "extension") {
    ok(taarNotice, "There is a TAAR notice");
    is_element_visible(privacyPolicyLink, "The privacy policy is visible");
  } else if (type == "theme") {
    ok(!taarNotice, "There is no TAAR notice");
    ok(!privacyPolicyLink, "There is no privacy policy");
  } else {
    throw new Error(`Unexpected type ${type}`);
  }

  if (Services.prefs.getBoolPref("browser.nova.enabled")) {
    // An addons-promo element is expected to be shown in both
    // the extensions and themes list view.
    is_element_visible(footerPromo, "The promo card is shown");
    is_element_visible(
      promoAmoBtn,
      "The promo card slotted AMO button is shown"
    );
    // The legacy AMO button that has been replaced with the addons-promo
    // should be hidden when Nova is enabled.
    is_element_hidden(amoButton, "The AMO button is hidden");

    let [promoId, promoButtonId] = PROMO_L10N_IDS[type];
    is(
      footerPromo.getAttribute("data-l10n-id"),
      promoId,
      "The promo card has the expected l10n id"
    );
    is(
      promoAmoBtn.getAttribute("data-l10n-id"),
      promoButtonId,
      "The promo card AMO button has the expected l10n id"
    );
  } else {
    // With Nova disabled the addons-promo are expected to be hidden
    // and the previously shown Nova button to be visible.
    is_element_visible(amoButton, "The AMO button is shown");
    is_element_hidden(footerPromo, "The promo card is hidden");
  }

  if (
    Services.prefs.getBoolPref("browser.nova.enabled") ||
    !showThemeRecommendationFooter
  ) {
    ok(
      !themeRecommendationFooter ||
        BrowserTestUtils.isHidden(themeRecommendationFooter),
      "There's no theme recommendation"
    );
  } else {
    // Additional pre-Nova assertion for the legacy themes list view footer.
    is_element_visible(
      themeRecommendationFooter,
      "There's a theme recommendation footer"
    );
    is_element_visible(themeRecommendationLink, "There's a link to the theme");
    is(themeRecommendationLink.target, "_blank", "The link opens in a new tab");
    is(
      themeRecommendationLink.href,
      "https://example.com/theme",
      "The link goes to the pref's URL"
    );
    is(
      doc.l10n.getAttributes(themeRecommendationFooter).id,
      "recommended-theme-1",
      "The recommendation has the right l10n-id"
    );
  }
}

async function installAddon({ card, recommendedList, manifestExtra = {} }) {
  // Install an add-on to hide the card.
  let hidden = BrowserTestUtils.waitForEvent(
    recommendedList,
    "card-hidden",
    false,
    e => e.detail.card == card
  );
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: { gecko: { id: card.addonId } },
      ...manifestExtra,
    },
    useAddonManager: "temporary",
  });
  await extension.startup();
  await hidden;
  return extension;
}

async function testListRecommendations({ type }) {
  let win = await loadInitialView(type);
  let doc = win.document;

  // Wait for the list to render, rendering is tested with the discovery pane.
  let recommendedList = doc.querySelector("recommended-addon-list");
  await recommendedList.cardsReady;

  checkExtraContents(doc, type);

  // Check that the cards are all for the right type.
  let cards = doc.querySelectorAll("recommended-addon-card");
  ok(!!cards.length, "There were some cards found");
  for (let card of cards) {
    is(card.discoAddon.type, type, `The card is for a ${type}`);
    is_element_visible(card, "The card is visible");
  }

  // Install an add-on for the first card, verify it is hidden.
  let { addonId } = cards[0];
  ok(addonId, "The card has an addonId");

  // Installing the add-on will fail since the URL doesn't point to a valid
  // XPI.
  let installButton = cards[0].querySelector('[action="install-addon"]');
  let { panel } = PopupNotifications;
  let popupId = "addon-install-failed-notification";
  let failPromise = TestUtils.topicObserved("addon-install-failed");
  installButton.click();
  await failPromise;
  // Wait for the installing popup to be hidden and leave just the error popup.
  await TestUtils.waitForCondition(() => {
    return panel.children.length == 1 && panel.firstElementChild.id == popupId;
  });
  await BrowserTestUtils.waitForPopupEvent(panel, "shown");

  // Dismiss the popup.
  panel.firstElementChild.button.click();
  await BrowserTestUtils.waitForPopupEvent(panel, "hidden");

  let extension = await installAddon({ card: cards[0], recommendedList });
  is_element_hidden(cards[0], "The card is now hidden");

  // Switch away and back, there should still be a hidden card.
  await closeView(win);
  win = await loadInitialView(type);
  doc = win.document;
  recommendedList = doc.querySelector("recommended-addon-list");
  await recommendedList.cardsReady;

  cards = Array.from(doc.querySelectorAll("recommended-addon-card"));

  let hiddenCard = cards.pop();
  is(hiddenCard.addonId, addonId, "The expected card was found");
  is_element_hidden(hiddenCard, "The card is still hidden");

  ok(!!cards.length, "There are still some visible cards");
  for (let card of cards) {
    is(card.discoAddon.type, type, `The card is for a ${type}`);
    is_element_visible(card, "The card is visible");
  }

  // Uninstall the add-on, verify the card is shown again.
  let shown = BrowserTestUtils.waitForEvent(recommendedList, "card-shown");
  await extension.unload();
  await shown;

  is_element_visible(hiddenCard, "The card is now shown");

  await closeView(win);
}

add_task(async function testExtensionList() {
  await testListRecommendations({ type: "extension" });
});

add_task(async function testThemeList() {
  await testListRecommendations({
    type: "theme",
    manifestExtra: { theme: {} },
  });
});

add_task(async function testInstallAllExtensions() {
  let type = "extension";
  let win = await loadInitialView(type);
  let doc = win.document;

  // Wait for the list to render, rendering is tested with the discovery pane.
  let recommendedList = doc.querySelector("recommended-addon-list");
  await recommendedList.cardsReady;

  // Find more button is shown.
  checkExtraContents(doc, type);

  let cards = Array.from(doc.querySelectorAll("recommended-addon-card"));
  is(cards.length, 3, "We found some cards");

  let extensions = await Promise.all(
    cards.map(card => installAddon({ card, recommendedList }))
  );

  // The find more on AMO button is shown.
  checkExtraContents(doc, type);

  // Uninstall one of the extensions, the button should still be shown.
  let extension = extensions.pop();
  let shown = BrowserTestUtils.waitForEvent(recommendedList, "card-shown");
  await extension.unload();
  await shown;

  // The find more on AMO button is shown.
  checkExtraContents(doc, type);

  await Promise.all(extensions.map(extension => extension.unload()));
  await closeView(win);
});

add_task(async function testError() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.getAddons.discovery.api_url", "data:,"]],
  });

  let win = await loadInitialView("extension");
  let doc = win.document;

  // Wait for the list to render, rendering is tested with the discovery pane.
  let recommendedList = doc.querySelector("recommended-addon-list");
  await recommendedList.cardsReady;

  checkExtraContents(doc, "extension");

  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

add_task(async function testThemesNoRecommendationUrl() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.recommendations.themeRecommendationUrl", ""]],
  });

  let win = await loadInitialView("theme");
  let doc = win.document;

  // Wait for the list to render, rendering is tested with the discovery pane.
  let recommendedList = doc.querySelector("recommended-addon-list");
  await recommendedList.cardsReady;

  checkExtraContents(doc, "theme", { showThemeRecommendationFooter: false });

  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

add_task(async function testRecommendationsDisabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.htmlaboutaddons.recommendations.enabled", false]],
  });

  let types = ["extension", "theme"];

  for (let type of types) {
    let win = await loadInitialView(type);
    let doc = win.document;

    let recommendedList = doc.querySelector("recommended-addon-list");
    ok(!recommendedList, `There are no recommendations on the ${type} page`);

    await closeView(win);
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function testRecommendationsFooterAmoButtonUtmContent() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.getAddons.link.url", AMO_TEST_URL]],
  });

  async function clickAndCheckUtm(win, selector, expectedUtmContent) {
    let button = win.document.querySelector(selector);
    ok(button, `Found button: ${selector}`);
    let tabbrowser = win.windowRoot.window.gBrowser;
    let tabPromise = BrowserTestUtils.waitForNewTab(tabbrowser, url =>
      url.startsWith(AMO_TEST_URL)
    );
    button.click();
    let tab = await tabPromise;
    let tabUrl = new URL(tab.linkedBrowser.currentURI.spec);
    Assert.deepEqual(
      tabUrl.searchParams.getAll("utm_content"),
      [expectedUtmContent],
      `utm_content should be "${expectedUtmContent}" and only have one entry`
    );
    BrowserTestUtils.removeTab(tab);
    return tabUrl;
  }

  // Extension list: Nova promo button vs legacy AMO button.
  {
    let win = await loadInitialView("extension");
    if (Services.prefs.getBoolPref("browser.nova.enabled")) {
      await clickAndCheckUtm(
        win,
        'footer moz-button[action="open-amo"]',
        "find-more-promo-bottom"
      );
    } else {
      await clickAndCheckUtm(
        win,
        'footer button[action="open-amo"]',
        "find-more-link-bottom"
      );
    }
    await closeView(win);
  }

  // Theme list: footer AMO button opens themes path with correct utm_content,
  // Nova promo button vs legacy AMO button.
  {
    let win = await loadInitialView("theme");
    let tabUrl;
    if (Services.prefs.getBoolPref("browser.nova.enabled")) {
      tabUrl = await clickAndCheckUtm(
        win,
        'footer moz-button[action="open-amo"]',
        "find-more-promo-bottom"
      );
    } else {
      tabUrl = await clickAndCheckUtm(
        win,
        'footer button[action="open-amo"]',
        "find-more-link-bottom"
      );
    }
    ok(
      tabUrl.pathname.includes("/themes"),
      "AMO URL includes the /themes path"
    );
    await closeView(win);
  }

  await SpecialPowers.popPrefEnv();
});
