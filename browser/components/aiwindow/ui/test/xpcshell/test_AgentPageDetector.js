/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AgentPageDetector, AgentPageCategory } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AgentPageDetector.sys.mjs"
);
const { getPageCategory, getPageInfo, getProductInfo, isProductURL } =
  AgentPageDetector;
const { PRODUCT_DETAIL, SHOPPING_HOME, NEWS_HOME } = AgentPageCategory;

// Supported product pages (US .com only): [url, expectedSite, expectedId].
const PRODUCT_URLS = [
  // Amazon - /dp/, /gp/product/
  [
    "https://www.amazon.com/Apple-AirPods-Max/dp/B08PZHYWJS",
    "amazon",
    "B08PZHYWJS",
  ],
  ["https://amazon.com/gp/product/B004R68INY", "amazon", "B004R68INY"],
  // Walmart - /ip/<slug>/<id>.
  [
    "https://www.walmart.com/ip/Apple-AirPods-Max/611698847",
    "walmart",
    "611698847",
  ],
  // Best Buy - /product/<slug>/<id>.
  [
    "https://www.bestbuy.com/product/apple-airpods-max-2-usb-c-purple/JJGCQYH68H",
    "bestbuy",
    "JJGCQYH68H",
  ],
  // Sephora - P-id in the path.
  [
    "https://www.sephora.com/product/some-serum-P123456.html",
    "sephora",
    "P123456",
  ],
  [
    "https://www.amazon.co.uk/Apple-AirPods-Max/dp/B08PZHYWJS",
    "amazon",
    "B08PZHYWJS",
  ],
  // Argos - /product/<catalogue number>.
  ["https://www.argos.co.uk/product/7202643", "argos", "7202643"],
  // Currys - /products/<slug>-<id>.html
  [
    "https://www.currys.co.uk/products/samsung-galaxy-s26-fe-256-gb-blueberry-10309754.html",
    "currys",
    "10309754",
  ],
  // Boots - /<slug>-<id>
  [
    "https://www.boots.com/no7-future-renew-damage-reversal-serum-50ml-10322082",
    "boots",
    "10322082",
  ],
];

// URLs that must not be treated as a supported product page.
const NON_PRODUCT_URLS = [
  // Supported hosts, but not product pages.
  "https://www.amazon.com/",
  "https://www.amazon.com/s?k=headphones",
  "https://www.walmart.com/cp/electronics/3944",
  "https://www.bestbuy.com/",
  // Best Buy category page (.c), not a product.
  "https://www.bestbuy.com/site/all-laptops/abcat0502000.c",
  // Best Buy /product/ path whose id segment isn't a clean SKU (has a dot).
  "https://www.bestbuy.com/product/slug/abc.def",
  // Supported retailers, unsupported markets.
  "https://www.amazon.ca/dp/B086Z6XSW4",
  "https://www.walmart.ca/en/ip/apple-airpods-max/611698847",
  "https://www.bestbuy.ca/en-ca/product/apple-airpods-max/12345678",
  "https://www.sephora.ca/ca/en/product/some-serum-P123456",
  // UK retailers, but not product pages.
  "https://www.argos.co.uk/search/kettle/",
  "https://www.argos.co.uk/product/",
  "https://www.currys.co.uk/computing/laptops/laptops",
  "https://www.boots.com/beauty/skincare",
  "https://www.boots.com/no7",
  "https://www.boots.com/no7-inspiration-and-advice/discover-no7-serum-10322082",
  // Unsupported retailers.
  "https://www.target.com/p/-/A-12345678",
  "https://example.com/dp/B08PZHYWJS",
  // Non-web schemes and non-URLs.
  "about:blank",
  "data:text/html,hi",
  "not a url",
];

// Home pages, with the host they are expected to be matched on.
const SHOPPING_HOME_URLS = [
  // US.
  ["https://www.amazon.com/", "amazon.com"],
  ["https://www.ebay.com", "ebay.com"],
  ["https://www.walmart.com/?athbdg=L1700", "walmart.com"],
  ["https://www.homedepot.com/", "homedepot.com"],
  ["https://www.etsy.com/", "etsy.com"],
  ["https://www.target.com/", "target.com"],
  ["https://www.cvs.com/", "cvs.com"],
  ["https://www.lowes.com/", "lowes.com"],
  ["https://www.samsung.com/us/", "samsung.com"],
  ["https://www.walgreens.com/", "walgreens.com"],
  ["https://www.costco.com/", "costco.com"],
  ["https://www.bestbuy.com/", "bestbuy.com"],
  ["https://www.wayfair.com/", "wayfair.com"],
  ["https://us.shein.com/?cdn_rsite=cf&ref=www", "us.shein.com"],
  ["https://www.macys.com/", "macys.com"],
  ["https://www.gap.com/", "gap.com"],
  ["https://www.aliexpress.com/", "aliexpress.com"],
  ["https://www.samsclub.com/", "samsclub.com"],
  // Canada.
  ["https://www.amazon.ca/", "amazon.ca"],
  ["https://www.walmart.ca/en", "walmart.ca"],
  ["https://www.walmart.ca/fr/", "walmart.ca"],
  ["https://ca.shein.com/", "ca.shein.com"],
  ["https://www.temu.com/", "temu.com"],
  ["https://www.costco.ca/", "costco.ca"],
  ["https://www.canadiantire.ca/en/", "canadiantire.ca"],
  ["https://www.canadiantire.ca/en.html", "canadiantire.ca"],
  ["https://www.bestbuy.ca/en-ca", "bestbuy.ca"],
  ["https://www.homedepot.ca/", "homedepot.ca"],
  ["https://www.homedepot.ca/en/home.html", "homedepot.ca"],
  ["https://www.ebay.ca/", "ebay.ca"],
  ["https://www.ikea.com/ca/en/", "ikea.com"],
  // UK.
  ["https://www.amazon.co.uk/", "amazon.co.uk"],
  ["https://www.ebay.co.uk/", "ebay.co.uk"],
  ["https://www.argos.co.uk/", "argos.co.uk"],
  ["https://www.tesco.com/groceries", "tesco.com"],
  ["https://www.tesco.com/groceries/en-GB/", "tesco.com"],
  ["https://www.currys.co.uk/", "currys.co.uk"],
  ["https://www.johnlewis.com/", "johnlewis.com"],
  ["https://www.diy.com/", "diy.com"],
  ["https://www.ikea.com/gb/en/", "ikea.com"],
];

const NEWS_HOME_URLS = [
  // US.
  ["https://www.nytimes.com/", "nytimes.com"],
  ["https://www.cnn.com/", "cnn.com"],
  ["https://www.foxnews.com/", "foxnews.com"],
  ["https://nypost.com/", "nypost.com"],
  ["https://www.dailymail.com/", "dailymail.com"],
  ["https://www.dailymail.com/ushome/index.html", "dailymail.com"],
  ["https://www.usatoday.com/", "usatoday.com"],
  ["https://www.washingtonpost.com/", "washingtonpost.com"],
  ["https://people.com/", "people.com"],
  ["https://www.bbc.com/news", "bbc.com"],
  ["https://www.newsweek.com/", "newsweek.com"],
  // UK.
  ["https://www.bbc.co.uk/", "bbc.co.uk"],
  ["https://www.bbc.co.uk/news", "bbc.co.uk"],
  ["https://www.theguardian.com/uk", "theguardian.com"],
  ["https://www.dailymail.co.uk/home/index.html", "dailymail.co.uk"],
  ["https://www.thesun.co.uk/", "thesun.co.uk"],
  ["https://www.independent.co.uk/", "independent.co.uk"],
  ["https://news.sky.com/", "news.sky.com"],
  ["https://www.telegraph.co.uk/", "telegraph.co.uk"],
  ["https://www.mirror.co.uk/", "mirror.co.uk"],
  ["https://www.express.co.uk/", "express.co.uk"],
  ["https://metro.co.uk/", "metro.co.uk"],
  // Canada.
  ["https://www.yahoo.com/", "yahoo.com"],
  ["https://www.yahoo.com/news/", "yahoo.com"],
  ["https://ca.yahoo.com/", "ca.yahoo.com"],
  ["https://ca.news.yahoo.com/", "ca.news.yahoo.com"],
  ["https://www.cbc.ca/news", "cbc.ca"],
  ["https://globalnews.ca/", "globalnews.ca"],
  ["https://www.ctvnews.ca/", "ctvnews.ca"],
  ["https://www.theglobeandmail.com/", "theglobeandmail.com"],
  ["https://www.thestar.com/", "thestar.com"],
  ["https://nationalpost.com/", "nationalpost.com"],
];

// Listed hosts, but deeper than the home page, plus unlisted hosts.
const NON_HOME_URLS = [
  "https://www.amazon.com/s?k=headphones",
  "https://www.target.com/c/grocery/-/N-5xt1a",
  "https://www.etsy.com/listing/123456789/some-item",
  "https://www.ikea.com/",
  "https://www.ikea.com/us/en/",
  "https://www.ikea.com/ca/en/cat/beds-bm003/",
  "https://www.samsung.com/",
  "https://www.samsung.com/uk/",
  "https://www.tesco.com/",
  "https://us.shein.com/women-clothing-c-2030.html",
  "https://www.canadiantire.ca/en/categories/automotive.html",
  "https://www.homedepot.ca/en/home/categories/appliances.html",
  "https://www.dailymail.com/sport/index.html",
  "https://www.yahoo.com/entertainment/",
  "https://www.cnn.com/2026/01/01/politics/some-article",
  "https://www.bbc.com/sport",
  "https://www.cbc.ca/news/canada/some-article-1.1234567",
  "https://www.example.com/",
];

add_task(function test_detects_supported_product_pages() {
  for (const [url, site, id] of PRODUCT_URLS) {
    const info = getProductInfo(url);
    Assert.ok(info, `Detects a product page: ${url}`);
    Assert.equal(info.site, site, `Correct retailer for ${url}`);
    Assert.equal(info.id, id, `Correct product id for ${url}`);
    Assert.ok(isProductURL(url), `isProductURL is true for ${url}`);
    Assert.deepEqual(
      getPageInfo(url),
      { category: PRODUCT_DETAIL, site, id },
      `Categorized as a product page: ${url}`
    );
  }
});

add_task(function test_ignores_non_product_pages() {
  for (const url of NON_PRODUCT_URLS) {
    Assert.equal(getProductInfo(url), null, `Not a product page: ${url}`);
    Assert.ok(!isProductURL(url), `isProductURL is false for ${url}`);
    Assert.notEqual(
      getPageCategory(url),
      PRODUCT_DETAIL,
      `Not categorized as a product page: ${url}`
    );
  }
});

add_task(function test_detects_home_pages() {
  const cases = [
    [SHOPPING_HOME, SHOPPING_HOME_URLS],
    [NEWS_HOME, NEWS_HOME_URLS],
  ];
  for (const [category, urls] of cases) {
    for (const [url, site] of urls) {
      Assert.deepEqual(
        getPageInfo(url),
        { category, site },
        `Detects a ${category} page: ${url}`
      );
    }
  }
});

add_task(function test_ignores_non_home_pages() {
  for (const url of NON_HOME_URLS) {
    Assert.equal(getPageCategory(url), null, `Not a home page: ${url}`);
  }
});

add_task(function test_accepts_url_and_nsiuri_inputs() {
  const spec = "https://www.amazon.com/dp/B08PZHYWJS";
  Assert.equal(
    getProductInfo(new URL(spec))?.id,
    "B08PZHYWJS",
    "Accepts a URL instance"
  );
  Assert.equal(
    getProductInfo(Services.io.newURI(spec))?.id,
    "B08PZHYWJS",
    "Accepts an nsIURI instance"
  );
  Assert.equal(getProductInfo(null), null, "Handles null input");
  Assert.equal(
    getPageCategory(Services.io.newURI("https://www.cnn.com/")),
    NEWS_HOME,
    "getPageCategory accepts an nsIURI instance"
  );
  Assert.equal(getPageCategory(null), null, "getPageCategory handles null");
});
