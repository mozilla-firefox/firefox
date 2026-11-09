/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "NimbusFeatures", () => {
  const { NimbusFeatures } = ChromeUtils.importESModule(
    "resource://nimbus/ExperimentAPI.sys.mjs"
  );
  return NimbusFeatures;
});

/**
 * Builds a URL with UTM tracking parameters.
 *
 * @param {object} options
 * @param {string} options.url - Base product URL
 * @param {string} options.region - Region code ("global", "us")
 * @param {boolean} [options.hasEmail] - Whether URL is from email link
 * @returns {string}
 */
function getURL({ url, region, hasEmail = false }) {
  let option =
    lazy.NimbusFeatures.moreFromMozilla.getVariable("template") || "default";

  const URL_PARAMS = {
    utm_source: "about-prefs",
    utm_campaign: "morefrommozilla",
    utm_medium: "firefox-desktop",
  };
  const utm_content = {
    default: "default",
    simple: "fxvt-113-a",
  };

  let pageUrl = new URL(url);
  for (let [key, val] of Object.entries(URL_PARAMS)) {
    pageUrl.searchParams.append(key, val);
  }

  pageUrl.searchParams.set(
    "utm_content",
    `${utm_content[option]}-${region}${hasEmail ? "-email" : ""}`
  );

  if (option !== "default") {
    pageUrl.searchParams.set(
      "entrypoint_experiment",
      "morefrommozilla-experiment-1846"
    );
    pageUrl.searchParams.set("entrypoint_variation", `treatment-${option}`);
  }
  return pageUrl.toString();
}

/**
 * Builds the list of products to display, respecting region and promo
 * eligibility.
 *
 * @returns {object[]}
 */
function getProducts() {
  const isRegionUS = lazy.Region.home?.toLowerCase() === "us";
  let products = [
    {
      id: "mozilla-monitor",
      l10nId: "more-from-moz-mozilla-monitor-card",
      region: isRegionUS ? "us" : "global",
      link: {
        l10nId: "more-from-moz-mozilla-monitor-box-link",
        iconSrc: "chrome://browser/content/logos/monitor.svg",
        actionURL: "https://monitor.mozilla.org/",
      },
    },
  ];

  if (lazy.BrowserUtils.shouldShowVPNPromo()) {
    products.push({
      id: "mozilla-vpn",
      l10nId: "more-from-moz-mozilla-vpn-card",
      region: "global",
      link: {
        l10nId: "more-from-moz-mozilla-vpn-box-link",
        iconSrc: "chrome://browser/skin/preferences/vpn-logo.svg",
        actionURL: "https://www.mozilla.org/products/vpn/",
      },
    });
  }

  if (lazy.BrowserUtils.shouldShowPromo(lazy.BrowserUtils.PromoType.RELAY)) {
    products.push({
      id: "firefox-relay",
      l10nId: "more-from-moz-firefox-relay-card",
      region: "global",
      link: {
        l10nId: "more-from-moz-firefox-relay-box-link",
        iconSrc: "chrome://browser/content/logos/relay.svg",
        actionURL: "https://relay.firefox.com/",
      },
    });
  }

  products.push(
    {
      id: "mdn",
      l10nId: "more-from-moz-mdn-card",
      region: "global",
      link: {
        l10nId: "more-from-moz-mdn-box-link",
        iconSrc: "chrome://global/skin/icons/mdn.svg",
        actionURL: "https://developer.mozilla.org/docs/Learn_web_development",
      },
    },
    {
      id: "thunderbird",
      l10nId: "more-from-moz-thunderbird-card",
      region: "global",
      link: {
        l10nId: "more-from-moz-thunderbird-box-link",
        iconSrc: "chrome://browser/skin/preferences/thunderbird-color-16.svg",
        actionURL: "https://www.thunderbird.net/",
      },
    },
    {
      id: "firefox-enterprise",
      l10nId: "more-from-moz-firefox-enterprise-card",
      region: "global",
      link: {
        l10nId: "more-from-moz-firefox-enterprise-box-link",
        iconSrc: "chrome://browser/content/logos/firefox-enterprise.svg",
        actionURL: "https://www.firefox.com/enterprise/",
      },
    },
    {
      id: "mozilla-new-products",
      l10nId: "more-from-moz-new-products-card2",
      region: "global",
      link: {
        l10nId: "more-from-moz-new-products-box-link",
        iconSrc: "chrome://browser/skin/preferences/mozilla-16.svg",
        actionURL: "https://future.mozilla.org/",
      },
    }
  );

  return products;
}

Preferences.addSetting({
  id: "moreFromMozillaProductGrid",
});

Preferences.addSetting({
  id: "moreFromMozillaPromo",
});

Preferences.addSetting({
  id: "promoGroup",
});

Preferences.addSetting({
  id: "promoGroupLink",
  getControlConfig: config => {
    let href = getURL({
      url: "https://www.mozilla.org/firefox/browsers/mobile/",
      region: "global",
    });
    return {
      ...config,
      controlAttrs: {
        href,
        target: "_blank",
      },
    };
  },
});

Preferences.addSetting({
  id: "firefoxMobilePromo",
  getControlConfig: config => {
    let option =
      lazy.NimbusFeatures.moreFromMozilla.getVariable("template") || "default";
    let templateName = option === "default" ? "simple" : option;
    return {
      ...config,
      controlAttrs: {
        imagesrc: `chrome://browser/content/preferences/more-from-mozilla-qr-code-${templateName}.svg`,
        imagealignment: "start",
      },
    };
  },
});

Preferences.addSetting({
  id: "firefoxMobilePromoLink",
  visible: () => lazy.BrowserUtils.sendToDeviceEmailsSupported(),
  getControlConfig: config => {
    let href = getURL({
      url: "https://www.mozilla.org/firefox/mobile/get-app/?v=mfm",
      region: "global",
      hasEmail: true,
    });
    return {
      ...config,
      controlAttrs: {
        href,
        target: "_blank",
      },
    };
  },
});

SettingGroupManager.registerGroups({
  moreFromMozillaPromo: {
    items: [
      {
        id: "promoGroup",
        control: "moz-fieldset",
        l10nId: "more-from-moz-firefox-mobile",
        iconSrc: "chrome://browser/content/logos/firefox.svg",
        headingLevel: 2,
        items: [
          {
            id: "promoGroupLink",
            l10nId: "more-from-moz-learn-more-link",
            control: "a",
            slot: "support-link",
          },
          {
            id: "firefoxMobilePromo",
            l10nId: "more-from-moz-firefox-mobile-qr-promo",
            control: "moz-promo",
            items: [
              {
                id: "firefoxMobilePromoLink",
                control: "a",
                slot: "support-link",
                l10nId: "more-from-moz-firefox-mobile-email-link",
              },
            ],
          },
        ],
      },
    ],
  },
  moreFromMozillaProducts: {
    card: "never",
    items: [
      {
        id: "moreFromMozillaProductGrid",
        control: "div",
        controlAttrs: {
          class: "products-grid",
        },
        options: getProducts().map(product => ({
          control: "moz-card",
          controlAttrs: {
            id: product.id,
          },
          options: [
            {
              control: "moz-fieldset",
              l10nId: product.l10nId,
              headingLevel: 2,
            },
            {
              l10nId: product.link.l10nId,
              control: "moz-box-link",
              controlAttrs: {
                iconsrc: product.link.iconSrc || "",
                layout: "large-icon",
                href: getURL({
                  url: product.link.actionURL,
                  region: product.region,
                }),
              },
            },
          ],
        })),
      },
    ],
  },
});
