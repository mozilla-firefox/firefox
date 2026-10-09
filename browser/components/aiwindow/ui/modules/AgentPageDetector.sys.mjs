/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Client-side, network-free classification of URLs into the page categories
 * Smart Window feature callout is gated on:
 *
 * - "product-detail": a retailer product page (Amazon, Walmart, Best Buy and
 *   Sephora, US (.com) only).
 * - "shopping-home": the home page of a US or Canadian retailer.
 * - "news-home": the home page of a US or Canadian news site.
 *
 */

/** Page categories returned by getPageInfo. */
export const AgentPageCategory = {
  PRODUCT_DETAIL: "product-detail",
  SHOPPING_HOME: "shopping-home",
  NEWS_HOME: "news-home",
};

/**
 * @typedef {object} ProductSiteConfig
 * @property {RegExp} productIdRegex
 *   Regex run against the URL pathname; must expose a `productId` named group.
 * @property {string[]} validTLDs
 *   Public suffixes supported for this retailer.
 */

/** @type {Record<string, ProductSiteConfig>} */
const ProductConfig = {
  amazon: {
    // A 10-char ASIN bounded by a path delimiter; covers /dp/, /gp/product/, etc.
    productIdRegex: /(?:[/]|$|%2F)(?<productId>[A-Z0-9]{10})(?:[/]|$|%2F)/,
    validTLDs: ["com"],
  },
  walmart: {
    // /ip/<slug>/<id>
    productIdRegex: /\/ip\/(?:[^/]{1,320}\/)?(?<productId>[0-9]{3,13})/,
    validTLDs: ["com"],
  },
  bestbuy: {
    // /product/<slug>/<id> (e.g. .../JJGCQYH68H)
    productIdRegex: /\/product\/[^/]+\/(?<productId>[A-Za-z0-9]+)(?:[/]|$)/,
    validTLDs: ["com"],
  },
  sephora: {
    // A Sephora product id ("P" followed by digits) in the path,
    // e.g. /product/<slug>-P123456.html.
    productIdRegex: /^\/product\/[^/]*-(?<productId>P\d{4,12})(?:\.html)?\/?$/,
    validTLDs: ["com"],
  },
};

const DEFAULT_HOME_PATHS = ["/"];

/**
 * Hosts whose home page is a shopping home page.
 * Listed once even when a site is in both the US and the Canadian list.
 */
const ShoppingHomeHosts = [
  // US.
  "amazon.com",
  "ebay.com",
  "walmart.com",
  "homedepot.com",
  "etsy.com",
  "target.com",
  "cvs.com",
  "lowes.com",
  "samsung.com",
  "walgreens.com",
  "costco.com",
  "bestbuy.com",
  "wayfair.com",
  "us.shein.com",
  "ca.shein.com",
  "macys.com",
  "gap.com",
  "aliexpress.com",
  "samsclub.com",
  // Canada.
  "amazon.ca",
  "walmart.ca",
  "temu.com",
  "costco.ca",
  "canadiantire.ca",
  "bestbuy.ca",
  "homedepot.ca",
  "ebay.ca",
  "ikea.com",
];

/** Hosts whose home page is a news home page. */
const NewsHomeHosts = [
  // US.
  "nytimes.com",
  "cnn.com",
  "foxnews.com",
  "nypost.com",
  "dailymail.com",
  "usatoday.com",
  "washingtonpost.com",
  "people.com",
  "bbc.com",
  "newsweek.com",
  // Canada.
  "yahoo.com",
  "ca.yahoo.com",
  "ca.news.yahoo.com",
  "news.yahoo.com",
  "cbc.ca",
  "globalnews.ca",
  "ctvnews.ca",
  "theglobeandmail.com",
  "thestar.com",
  "nationalpost.com",
];

/**
 * Paths that count as the home page for sites that don't serve it from "/",
 * or that serve it from a locale-prefixed path as well.
 *
 * @type {Map<string, string[]>}
 */
const HomePaths = new Map([
  // Only the Canadian storefront is in scope and "/" is a country picker.
  ["ikea.com", ["/ca", "/ca/en", "/ca/fr"]],
  ["samsung.com", ["/us", "/ca", "/ca/en", "/ca/fr"]],
  ["walmart.ca", ["/", "/en", "/fr"]],
  ["canadiantire.ca", ["/", "/en", "/fr", "/en.html", "/fr.html"]],
  ["homedepot.ca", ["/", "/en", "/fr", "/en/home.html", "/fr/home.html"]],
  ["bestbuy.ca", ["/", "/en-ca", "/fr-ca"]],
  ["bbc.com", ["/", "/news"]],
  ["cbc.ca", ["/", "/news"]],
  ["dailymail.com", ["/", "/ushome/index.html", "/home/index.html"]],
  ["yahoo.com", ["/", "/news"]],
  ["ca.yahoo.com", ["/", "/news"]],
]);

/** @type {Map<string, string>} Host to home page category. */
const HomeCategoryByHost = new Map([
  ...ShoppingHomeHosts.map(host => [host, AgentPageCategory.SHOPPING_HOME]),
  ...NewsHomeHosts.map(host => [host, AgentPageCategory.NEWS_HOME]),
]);

/**
 * Coerce accepted inputs to a URL.
 *
 * @param {string | URL | nsIURI} input
 * @returns {URL | null}
 */
function toURL(input) {
  if (!input) {
    return null;
  }
  try {
    if (input instanceof Ci.nsIURI) {
      return URL.fromURI(input);
    }
    if (URL.isInstance(input)) {
      return input;
    }
    if (typeof input === "string") {
      return new URL(input);
    }
  } catch {
    return null;
  }
  return null;
}

/**
 * The host of an http(s) URL with any leading "www." removed, or null for
 * other schemes and unparseable inputs.
 *
 * @param {URL | null} url
 * @returns {string | null}
 */
function getHost(url) {
  if (!url || (url.protocol !== "https:" && url.protocol !== "http:")) {
    return null;
  }
  return url.hostname.toLowerCase().replace(/^www\./, "") || null;
}

/**
 * Lowercase the path and drop any trailing slash, so that "/EN/" and "/en"
 * compare equal.
 *
 * @param {string} pathname
 * @returns {string}
 */
function normalizePath(pathname) {
  return pathname.toLowerCase().replace(/\/+$/, "") || "/";
}

/**
 * @typedef {object} ProductInfo
 * @property {string} site - Retailer key, e.g. "amazon".
 * @property {string} id - Extracted product id.
 */

/**
 * Classify a URL as a supported retailer product page.
 *
 * @param {string | URL | nsIURI} input - The URL to classify.
 * @returns {ProductInfo | null} The retailer and product id, or null when the
 *   URL is not a supported product page.
 */
function getProductInfo(input) {
  const url = toURL(input);
  const host = getHost(url);
  if (!host) {
    return null;
  }

  let tld;
  try {
    tld = Services.eTLD.getPublicSuffixFromHost(host);
  } catch {
    return null;
  }
  if (!tld.length) {
    return null;
  }

  // Strip the public suffix and its separating dot to get the retailer key.
  const site = host.slice(0, -(tld.length + 1));
  const config = Object.hasOwn(ProductConfig, site)
    ? ProductConfig[site]
    : null;
  if (!config || !config.validTLDs.includes(tld)) {
    return null;
  }

  const id = url.pathname.match(config.productIdRegex)?.groups?.productId;
  return id ? { site, id } : null;
}

/**
 * Whether a URL is a supported retailer product page.
 *
 * @param {string | URL | nsIURI} input
 * @returns {boolean}
 */
function isProductURL(input) {
  return getProductInfo(input) !== null;
}

/**
 * @typedef {object} PageInfo
 * @property {string} category - One of the `AgentPageCategory` values.
 * @property {string} site - Retailer key for a product page (like "amazon")
 *  and a home page ("amazon.ca").
 * @property {string} [id] - Product id.
 */

/**
 * Classify a URL as one of the page categories the Smart Window callout is
 * gated on. A product page wins over a home page when both could match.
 *
 * @param {string | URL | nsIURI} input - The URL to classify.
 * @returns {PageInfo | null} The page category and the site it was matched on,
 *   or null when the URL is in none of the categories.
 */
function getPageInfo(input) {
  const url = toURL(input);
  const host = getHost(url);
  if (!host) {
    return null;
  }

  const product = getProductInfo(url);
  if (product) {
    return { category: AgentPageCategory.PRODUCT_DETAIL, ...product };
  }

  const category = HomeCategoryByHost.get(host);
  if (!category) {
    return null;
  }
  const homePaths = HomePaths.get(host) ?? DEFAULT_HOME_PATHS;
  return homePaths.includes(normalizePath(url.pathname))
    ? { category, site: host }
    : null;
}

/**
 * The page category of a URL, for message targeting.
 *
 * @param {string | URL | nsIURI} input
 * @returns {string | null} An `AgentPageCategory` value, or null.
 */
function getPageCategory(input) {
  return getPageInfo(input)?.category ?? null;
}

export const AgentPageDetector = {
  getPageCategory,
  getPageInfo,
  getProductInfo,
  isProductURL,
};
