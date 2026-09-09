/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests to ensure the production search configuration is correctly set up for
 * the search engines. These are, by default, run against the remote settings
 * dumps in this repository. See `maybeSetupConfig` in the head file for testing
 * against servers.
 */

// Wikipedia locale-to-domain mapping configuration
const WIKIPEDIA_LOCALES_INFO = [
  [["af"]],
  [["an"]],
  [["ar"]],
  [["ast"]],
  [["az"]],
  [["be"]],
  [["bg"]],
  [["bn"]],
  [["br"]],
  [["bs"]],
  [["ca", "ca-valencia"], "ca", "ca"],
  [["cs"], "cs", "cz"],
  [["cy"]],
  [["da"]],
  [["de"]],
  [["dsb"]],
  [["el"]],
  [["eo"]],
  [["cak", "es-AR", "es-CL", "es-ES", "es-MX", "trs"], "es", "es"],
  [["et"]],
  [["eu"]],
  [["fa"]],
  [["fi"]],
  [["fr", "ff", "son"], "fr", "fr"],
  [["fy-NL"], "fy", "fy-NL"],
  [["ga-IE"], "ga", "ga-IE"],
  [["gd"]],
  [["gl"]],
  [["gn"]],
  [["gu-IN"], "gu", "gu"],
  [["hi-IN"], "hi", "hi"],
  [["he"]],
  [["hr"]],
  [["hsb"]],
  [["hu"]],
  [["hy-AM"], "hy", "hy"],
  [["ia"]],
  [["id"]],
  [["is"]],
  [["ja", "ja-JP-macos"], "ja", "ja"],
  [["ka"]],
  [["kab"]],
  [["kk"]],
  [["km"]],
  [["kn"]],
  [["ko"], "ko", "kr"],
  [["it", "fur", "sc"], "it", "it"],
  [["lij"]],
  [["lo"]],
  [["lt"]],
  [["ltg"]],
  [["lv"]],
  [["mk"]],
  [["mr"]],
  [["ms"]],
  [["my"]],
  [["nb-NO"], "no", "NO"],
  [["ne-NP"], "ne", "ne"],
  [["nl"]],
  [["nn-NO"], "nn", "NN"],
  [["oc"]],
  [["pa-IN"], "pa", "pa"],
  [["pl", "szl"], "pl", "pl"],
  [["pt-BR", "pt-PT"], "pt", "pt"],
  [["rm"]],
  [["ro"]],
  [["ru"]],
  [["si"]],
  [["sk"]],
  [["sl"]],
  [["sq"]],
  [["sr"]],
  [["sv-SE"], "sv", "sv-SE"],
  [["ta"]],
  [["te"]],
  [["th"]],
  [["tl"]],
  [["tr"]],
  [["uk"]],
  [["ur"]],
  [["uz"]],
  [["vi"]],
  [["wo"]],
  [["zh-CN"], "zh", "zh-CN"],
  [["zh-TW"], "zh", "zh-TW"],
];

const EBAY_DOMAIN_LOCALES = {
  "ebay-ca": ["en-CA"],
  "ebay-ch": ["rm"],
  "ebay-de": ["de", "dsb", "hsb"],
  "ebay-es": ["an", "ast", "ca", "ca-valencia", "es-ES", "eu", "gl"],
  "ebay-ie": ["ga-IE", "ie"],
  "ebay-it": ["fur", "it", "lij", "sc"],
  "ebay-nl": ["fy-NL", "nl"],
  "ebay-uk": ["cy", "en-GB", "gd"],
};

const wikipediaConfig = {
  identifierStartsWith: "wikipedia",
  default: {
    // Not default anywhere.
  },
  available: {
    excluded: [
      // Should be available everywhere.
    ],
  },
  details: [
    // Details will be populated in populateWikipediaConfig.
  ],
};

/**
 * Generates the expected details for the given locales and inserts
 * them into the wikipediaConfig.
 *
 * @param {string[]} locales
 *   The locales for this details entry - which locales this variant of
 *   Wikipedia is expected to be deployed to.
 * @param {string} [subDomainName]
 *   The expected sub domain name for this variant of Wikipedia. If not
 *   specified, defaults to the first item in the locales array.
 * @param {string} [telemetrySuffix]
 *   The expected suffix used when this variant is reported via telemetry. If
 *   not specified, defaults to the first item in the array. If this is the
 *   empty string, then it "wikipedia" (i.e. no suffix) will be the expected
 *   value.
 */
function generateExpectedDetails(locales, subDomainName, telemetrySuffix) {
  if (!subDomainName) {
    subDomainName = locales[0];
  }
  if (telemetrySuffix == undefined) {
    telemetrySuffix = locales[0];
  }
  wikipediaConfig.details.push({
    domain: `${subDomainName}.wikipedia.org`,
    telemetryId: telemetrySuffix ? `wikipedia-${telemetrySuffix}` : "wikipedia",
    required_aliases: ["@wikipedia"],
    included: [{ locales }],
  });
}

/**
 * Populates the Wikipedia configuration with locale-to-domain mappings.
 * For the "en" version of Wikipedia, we ship it to all locales where other
 * Wikipedias are not shipped.
 */
async function populateWikipediaConfig() {
  const allLocales = await test.getLocales();

  // Build list of locales for en.wikipedia.org
  let enLocales = [];
  for (let locale of allLocales) {
    if (!WIKIPEDIA_LOCALES_INFO.find(d => d[0].includes(locale))) {
      enLocales.push(locale);
    }
  }

  console.log("en.wikipedia.org expected locales are:", enLocales);
  generateExpectedDetails(enLocales, "en", "");

  for (let details of WIKIPEDIA_LOCALES_INFO) {
    generateExpectedDetails(...details);
  }
}

const test = new SearchConfigTest([
  {
    identifier: "amazondotcom-us",
    default: {
      // Not default anywhere.
    },
    available: {
      included: [
        {
          // The main regions we ship Amazon to. Below this are special cases.
          regions: ["us"],
        },
      ],
    },
    details: [
      {
        domain: "amazon.com",
        telemetryId: "amazondotcom-us-adm",
        aliases: ["@amazon"],
        included: [
          {
            regions: ["us"],
          },
        ],
        noSuggestionsURL: true,
        searchUrlCode: "tag=admarketus-20",
      },
    ],
  },
  {
    identifier: "baidu",
    aliases: ["@百度", "@baidu"],
    default: {
      included: [
        {
          regions: ["cn"],
          locales: ["zh-CN"],
        },
      ],
    },
    available: {
      included: [
        {
          locales: ["zh-CN"],
        },
      ],
    },
    details: [
      {
        included: [{}],
        domain: "baidu.com",
        telemetryId: "baidu",
      },
    ],
  },
  {
    identifier: "bing",
    aliases: ["@bing"],
    default: {
      // Not included anywhere.
    },
    available: {
      excluded: [
        // Should be available everywhere.
      ],
    },
    details: [
      {
        included: [{}],
        domain: "bing.com",
        telemetryId:
          SearchUtils.MODIFIED_APP_CHANNEL == "esr" ? "bing-esr" : "bing",
        searchUrlCode:
          SearchUtils.MODIFIED_APP_CHANNEL == "esr" ? "pc=MOZR" : "pc=MOZI",
      },
    ],
  },
  {
    identifier: "funsearch",
    aliases: ["@funsearch"],
    default: {
      excluded: [
        {
          regions: ["cn"],
          locales: ["zh-CN"],
        },
      ],
    },
    available: {
      excluded: [],
    },
    details: [
      {
        included: [{}],
        domain: "funsearchapp.netlify.app",
        telemetryId: "funsearch",
      },
    ],
  },
  {
    identifier: "ddg",
    aliases: ["@duckduckgo", "@ddg"],
    default: {
      // Not included anywhere.
    },
    available: {
      excluded: [
        // Should be available everywhere.
      ],
    },
    details: [
      {
        included: [{}],
        domain: "duckduckgo.com",
        telemetryId:
          SearchUtils.MODIFIED_APP_CHANNEL == "esr" ? "ddg-esr" : "ddg",
        searchUrlCode:
          SearchUtils.MODIFIED_APP_CHANNEL == "esr" ? "t=ftsa" : "t=ffab",
      },
    ],
  },
  {
    identifierStartsWith: "ebay",
    aliases: ["@ebay"],
    default: {
      // Not included anywhere.
    },
    available: {
      included: [
        {
          // We don't currently enforce by region, but do locale instead.
          // regions: [
          //   "us", "gb", "ca", "ie", "fr", "it", "de", "at", "es", "nl", "ch", "au"
          // ],
          locales: [
            "an",
            "ast",
            "br",
            "ca",
            "ca-valencia",
            "cy",
            "de",
            "dsb",
            "en-CA",
            "en-GB",
            "es-ES",
            "eu",
            "fur",
            "fr",
            "fy-NL",
            "ga-IE",
            "gd",
            "gl",
            "hsb",
            "it",
            "lij",
            "nl",
            "rm",
            "sc",
            "wo",
          ],
        },
        {
          regions: ["pl"],
        },
        {
          regions: ["au", "be", "ca", "ch", "gb", "ie", "nl", "us"],
          locales: ["en-US"],
        },
        {
          regions: ["gb"],
          locales: ["sco"],
        },
      ],
    },
    suggestionUrlBase: "https://autosug.ebay.com/autosug",
    details: [
      {
        // Note: These should be based on region, but we don't currently enforce that.
        // Note: the order here is important. A region/locale match higher up in the
        // list will override a region/locale match lower down.
        domain: "www.befr.ebay.be",
        telemetryId: "ebay-be",
        included: [
          {
            regions: ["be"],
            locales: ["br", "unknown", "en-US", "fr", "fy-NL", "nl", "wo"],
          },
        ],
        excluded: [{ regions: ["pl"] }],
        searchUrlCode: "mkrid=1553-53471-19255-0",
        suggestUrlCode: "sId=23",
      },
      {
        domain: "www.ebay.at",
        telemetryId: "ebay-at",
        included: [
          {
            regions: ["at"],
            locales: ["de", "dsb", "hsb"],
          },
        ],
        excluded: [{ regions: ["pl"] }],
        searchUrlCode: "mkrid=5221-53469-19255-0",
        suggestUrlCode: "sId=16",
      },
      {
        domain: "www.ebay.ca",
        telemetryId: "ebay-ca",
        included: [
          {
            locales: EBAY_DOMAIN_LOCALES["ebay-ca"],
          },
          {
            regions: ["ca"],
          },
        ],
        excluded: [
          {
            locales: [
              ...EBAY_DOMAIN_LOCALES["ebay-ch"],
              ...EBAY_DOMAIN_LOCALES["ebay-de"],
              ...EBAY_DOMAIN_LOCALES["ebay-es"],
              ...EBAY_DOMAIN_LOCALES["ebay-ie"],
              ...EBAY_DOMAIN_LOCALES["ebay-it"],
              ...EBAY_DOMAIN_LOCALES["ebay-nl"],
              ...EBAY_DOMAIN_LOCALES["ebay-uk"],
            ],
          },
          {
            regions: ["pl"],
          },
        ],
        searchUrlCode: "mkrid=706-53473-19255-0",
        suggestUrlCode: "sId=2",
      },
      {
        domain: "www.ebay.ch",
        telemetryId: "ebay-ch",
        included: [
          {
            locales: EBAY_DOMAIN_LOCALES["ebay-ch"],
          },
          {
            regions: ["ch"],
          },
        ],
        excluded: [
          {
            locales: [
              ...EBAY_DOMAIN_LOCALES["ebay-ca"],
              ...EBAY_DOMAIN_LOCALES["ebay-es"],
              ...EBAY_DOMAIN_LOCALES["ebay-ie"],
              ...EBAY_DOMAIN_LOCALES["ebay-it"],
              ...EBAY_DOMAIN_LOCALES["ebay-nl"],
              ...EBAY_DOMAIN_LOCALES["ebay-uk"],
            ],
          },
          {
            regions: ["pl"],
          },
        ],
        searchUrlCode: "mkrid=5222-53480-19255-0",
        suggestUrlCode: "sId=193",
      },
      {
        domain: "www.ebay.com",
        telemetryId: "ebay",
        included: [
          {
            locales: ["unknown", "en-US"],
          },
        ],
        excluded: [
          { regions: ["au", "be", "ca", "ch", "gb", "ie", "nl", "pl"] },
        ],
        searchUrlCode: "mkrid=711-53200-19255-0",
        suggestUrlCode: "sId=0",
      },
      {
        domain: "www.ebay.com.au",
        telemetryId: "ebay-au",
        included: [
          {
            regions: ["au"],
            locales: ["cy", "unknown", "en-GB", "en-US", "gd"],
          },
        ],
        excluded: [{ regions: ["pl"] }],
        searchUrlCode: "mkrid=705-53470-19255-0",
        suggestUrlCode: "sId=15",
      },
      {
        domain: "www.ebay.ie",
        telemetryId: "ebay-ie",
        included: [
          {
            locales: EBAY_DOMAIN_LOCALES["ebay-ie"],
          },
          {
            regions: ["ie"],
            locales: ["cy", "unknown", "en-GB", "en-US", "gd"],
          },
        ],
        excluded: [{ regions: ["pl"] }],
        searchUrlCode: "mkrid=5282-53468-19255-0",
        suggestUrlCode: "sId=205",
      },
      {
        domain: "www.ebay.co.uk",
        telemetryId: "ebay-uk",
        included: [
          {
            locales: EBAY_DOMAIN_LOCALES["ebay-uk"],
          },
          {
            locales: ["unknown", "en-US", "sco"],
            regions: ["gb"],
          },
        ],
        excluded: [{ regions: ["au", "ie", "pl"] }],
        searchUrlCode: "mkrid=710-53481-19255-0",
        suggestUrlCode: "sId=3",
      },
      {
        domain: "www.ebay.de",
        telemetryId: "ebay-de",
        included: [
          {
            locales: EBAY_DOMAIN_LOCALES["ebay-de"],
          },
        ],
        excluded: [{ regions: ["at", "ch", "pl"] }],
        searchUrlCode: "mkrid=707-53477-19255-0",
        suggestUrlCode: "sId=77",
      },
      {
        domain: "www.ebay.es",
        telemetryId: "ebay-es",
        included: [
          {
            locales: EBAY_DOMAIN_LOCALES["ebay-es"],
          },
        ],
        excluded: [{ regions: ["pl"] }],
        searchUrlCode: "mkrid=1185-53479-19255-0",
        suggestUrlCode: "sId=186",
      },
      {
        domain: "www.ebay.fr",
        telemetryId: "ebay-fr",
        included: [
          {
            locales: ["br", "fr", "wo"],
          },
        ],
        excluded: [{ regions: ["be", "ca", "ch", "pl"] }],
        searchUrlCode: "mkrid=709-53476-19255-0",
        suggestUrlCode: "sId=71",
      },
      {
        domain: "www.ebay.it",
        telemetryId: "ebay-it",
        included: [
          {
            locales: EBAY_DOMAIN_LOCALES["ebay-it"],
          },
        ],
        excluded: [{ regions: ["pl"] }],
        searchUrlCode: "mkrid=724-53478-19255-0",
        suggestUrlCode: "sId=101",
      },
      {
        domain: "www.ebay.nl",
        telemetryId: "ebay-nl",
        included: [
          {
            locales: EBAY_DOMAIN_LOCALES["ebay-nl"],
          },
          {
            locales: ["unknown", "en-US"],
            regions: ["nl"],
          },
        ],
        excluded: [{ regions: ["be", "pl"] }],
        searchUrlCode: "mkrid=1346-53482-19255-0",
        suggestUrlCode: "sId=146",
      },
      {
        domain: "www.ebay.pl",
        telemetryId: "ebay-pl",
        included: [
          {
            regions: ["pl"],
          },
        ],
        searchUrlCode: "mkrid=4908-226936-19255-0",
        suggestUrlCode: "sId=212",
      },
    ],
  },
  {
    identifier: "ecosia",
    aliases: ["@ecosia"],
    default: {
      // Not default anywhere.
    },
    available: {
      included: [
        {
          locales: ["de"],
        },
        {
          regions: ["at", "be", "ch", "de", "es", "it", "nl", "se"],
        },
      ],
    },
    details: [
      {
        included: [{}],
        domain: "www.ecosia.org",
        telemetryId: "ecosia",
        searchUrlCode: "tt=mzl",
      },
    ],
  },
  {
    identifier: "google",
    aliases: ["@google"],
    default: {
      // Funsearch is the application default; Google remains available.
    },
    available: {
      excluded: [
        // Should be available everywhere.
      ],
    },
    details: [
      {
        included: [{ regions: ["us"] }],
        domain: "google.com",
        telemetryId:
          SearchUtils.MODIFIED_APP_CHANNEL == "esr"
            ? "google-b-1-e"
            : "google-b-1-d",
        searchUrlCode:
          SearchUtils.MODIFIED_APP_CHANNEL == "esr"
            ? "client=firefox-b-1-e"
            : "client=firefox-b-1-d",
        partnerCode:
          SearchUtils.MODIFIED_APP_CHANNEL == "esr"
            ? "firefox-b-1-e"
            : "firefox-b-1-d",
      },
      {
        excluded: [{ regions: ["us", "by", "ru"] }],
        included: [{}],
        domain: "google.com",
        telemetryId:
          SearchUtils.MODIFIED_APP_CHANNEL == "esr"
            ? "google-b-e"
            : "google-b-d",
        searchUrlCode:
          SearchUtils.MODIFIED_APP_CHANNEL == "esr"
            ? "client=firefox-b-e"
            : "client=firefox-b-d",
        partnerCode:
          SearchUtils.MODIFIED_APP_CHANNEL == "esr"
            ? "firefox-b-e"
            : "firefox-b-d",
      },
      {
        included: [{ regions: ["by", "ru"] }],
        domain: "google.com",
        telemetryId: "google-com-nocodes",
        partnerCode: "",
        searchUrlParamNotInQuery: "client",
      },
    ],
  },
  {
    identifier: "perplexity",
    aliases: ["@perplexity"],
    default: {
      // Not default anywhere.
    },
    available: {
      excluded: [
        // Should be available everywhere.
      ],
    },
    details: [
      {
        included: [{}],
        domain: "www.perplexity.ai",
        searchUrlCode: "pc=firefox",
      },
    ],
  },
  {
    identifier: "qwant",
    aliases: ["@qwant"],
    default: {
      // Not default anywhere.
    },
    available: {
      included: [
        {
          locales: ["fr"],
        },
        {
          regions: ["be", "ch", "es", "fr", "it", "nl"],
        },
      ],
    },
    details: [
      {
        included: [{}],
        domain: "www.qwant.com",
        telemetryId: "qwant",
        searchUrlCode: "client=brz-moz",
        suggestUrlCode: "client=opensearch",
      },
    ],
  },
  wikipediaConfig,
  {
    identifier: "yahoo-jp",
    aliases: [],
    default: {
      // Not default anywhere.
    },
    available: {
      included: [
        {
          locales: ["ja", "ja-JP-macos"],
        },
      ],
    },
    details: [
      {
        included: [{}],
        domain: "search.yahoo.co.jp",
        telemetryId: "yahoo-jp",
        searchUrlCode: "fr=mozff",
      },
    ],
  },
]);

add_setup(async function () {
  await populateWikipediaConfig();
  await test.setup();
});

add_task(async function test_searchConfigs() {
  await test.run();
});
