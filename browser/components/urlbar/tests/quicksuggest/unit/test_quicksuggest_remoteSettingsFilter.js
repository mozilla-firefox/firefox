/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// When a remote settings record has a `filter_expression`, the client should
// ingest it only if the filter and client match.
//
// Filter-expression matching works like this:
//
// 1. `SuggestBackendRust` creates its `SuggestStore` with a remote settings
//    context dictionary (also called an app context) that includes the client's
//    country, form factor (always "desktop"), locale, OS, and other info.
// 2. When `SuggestStore` ingests a record, its internal RS client plugs the
//    context values from step 1 into the record's `filter_expression`. If the
//    resulting expression is true, the store ingests the record's attachment.
//
// A filter expression is a JEXL string predicate that can include any number of
// variables. This example matches desktop clients in the U.S.:
//
// ```
// env.country == 'US' && env.formFactor == 'desktop'
// ```
//
// In this example, if the client's context value for `country` is "US" and its
// value for `formFactor` is "desktop", the expression will match. The values of
// other keys in the client's context dictionary do not matter.
//
// Currently we expect the following variables in a `filter_expression`:
//
// * `country` - ISO 3166-2 country code, i.e., two uppercase chars
// * `formFactor` - One of "desktop", "phone", or "tablet"

"use strict";

add_setup(async function () {
  // Build an array of RS records with different combinations of record types
  // and filter expressions.
  let remoteSettingsRecords = [];
  let types = [
    {
      collection: QuickSuggestTestUtils.RS_COLLECTION.AMP,
      type: QuickSuggestTestUtils.RS_TYPE.AMP,
    },
    {
      collection: QuickSuggestTestUtils.RS_COLLECTION.OTHER,
      type: QuickSuggestTestUtils.RS_TYPE.WIKIPEDIA,
    },
  ];
  for (let { collection, type } of types) {
    for (let country of ["US", "GB", ""]) {
      for (let formFactor of ["desktop", "phone", ""]) {
        let opts = { collection, type };
        if (!country && !formFactor) {
          // Add a record without a filter and a record with an empty filter.
          remoteSettingsRecords.push(
            makeRecord(opts),
            makeRecord({ ...opts, filterExpression: {} })
          );
        } else {
          opts.filterExpression = {};
          if (country) {
            opts.filterExpression.country = country;
          }
          if (formFactor) {
            opts.filterExpression.formFactor = formFactor;
          }
          remoteSettingsRecords.push(makeRecord(opts));
        }
      }
    }
  }

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords,
    prefs: [
      ["suggest.quicksuggest.sponsored", true],
      ["suggest.quicksuggest.nonsponsored", true],
      ["quicksuggest.ampTopPickCharThreshold", 0],
    ],
  });
});

add_task(async function () {
  let tests = [
    // en-US in US
    {
      homeRegion: "US",
      locales: ["en-US"],
      tests: [
        // Amp queries
        {
          expected: QuickSuggestTestUtils.ampResult,
          queries: [
            "amp us desktop",
            "amp us",
            "amp desktop",
            "amp",
            "amp no-filter",
          ],
        },
        {
          expected: null,
          queries: [
            "amp us phone",
            "amp gb desktop",
            "amp gb phone",
            "amp gb",
            "amp phone",
          ],
        },

        // Wikipedia queries
        {
          expected: QuickSuggestTestUtils.wikipediaResult,
          queries: [
            "wikipedia us desktop",
            "wikipedia us",
            "wikipedia desktop",
            "wikipedia",
            "wikipedia no-filter",
          ],
        },
        {
          expected: null,
          queries: [
            "wikipedia us phone",
            "wikipedia gb desktop",
            "wikipedia gb phone",
            "wikipedia gb",
            "wikipedia phone",
          ],
        },
      ],
    },

    // en-GB in GB, en-US in GB
    //
    // The locale doesn't matter, only the home region matters, so US
    // suggestions should not be matched even when the locale is en-US.
    {
      homeRegion: "GB",
      locales: ["en-GB", "en-US"],
      tests: [
        // Amp queries
        {
          expected: QuickSuggestTestUtils.ampResult,
          queries: [
            "amp gb desktop",
            "amp gb",
            "amp desktop",
            "amp",
            "amp no-filter",
          ],
        },
        {
          expected: null,
          queries: [
            "amp gb phone",
            "amp us desktop",
            "amp us phone",
            "amp us",
            "amp phone",
          ],
        },

        // Wikipedia queries
        {
          expected: QuickSuggestTestUtils.wikipediaResult,
          queries: [
            "wikipedia gb desktop",
            "wikipedia gb",
            "wikipedia desktop",
            "wikipedia",
            "wikipedia no-filter",
          ],
        },
        {
          expected: null,
          queries: [
            "wikipedia gb phone",
            "wikipedia us desktop",
            "wikipedia us phone",
            "wikipedia us",
            "wikipedia phone",
          ],
        },
      ],
    },

    // de in DE, en-US in DE
    //
    // The locale doesn't matter, only the home region matters, so US
    // suggestions should not be matched even when the locale is en-US.
    {
      homeRegion: "DE",
      locales: ["de", "en-US"],
      tests: [
        // Amp queries
        {
          expected: QuickSuggestTestUtils.ampResult,
          queries: ["amp desktop", "amp", "amp no-filter"],
        },
        {
          expected: null,
          queries: [
            "amp us desktop",
            "amp us",
            "amp us phone",
            "amp gb desktop",
            "amp gb phone",
            "amp gb",
            "amp phone",
          ],
        },

        // Wikipedia queries
        {
          expected: QuickSuggestTestUtils.wikipediaResult,
          queries: ["wikipedia desktop", "wikipedia", "wikipedia no-filter"],
        },
        {
          expected: null,
          queries: [
            "wikipedia us desktop",
            "wikipedia us",
            "wikipedia us phone",
            "wikipedia gb desktop",
            "wikipedia gb phone",
            "wikipedia gb",
            "wikipedia phone",
          ],
        },
      ],
    },
  ];

  for (let test of tests) {
    await doTests(test);
  }
});

async function doTests({ locales, homeRegion, tests }) {
  for (let locale of locales) {
    // Disable and reenable Suggest so its store is recreated with the
    // appropriate remote settings app context for the locale and region.
    info("Disabling Suggest: " + JSON.stringify({ locales, homeRegion }));
    UrlbarPrefs.set("quicksuggest.enabled", false);

    await QuickSuggestTestUtils.withLocales({
      homeRegion,
      locales: [locale],
      callback: async () => {
        info("Reenabling Suggest: " + JSON.stringify({ locale, homeRegion }));
        UrlbarPrefs.set("quicksuggest.enabled", true);
        await QuickSuggestTestUtils.forceSync();

        for (let { expected, queries } of tests) {
          for (let query of queries) {
            info(
              "Doing query: " + JSON.stringify({ locale, homeRegion, query })
            );

            let expectedResults = [];
            if (expected) {
              expectedResults.push(
                expected({
                  keyword: query,
                  url: `https://example.com/${query}`,
                  title: `Suggestion: ${query}`,
                })
              );
            }

            await check_results({
              context: createContext(query, {
                providers: [UrlbarProviderQuickSuggest.name],
                isPrivate: false,
              }),
              matches: expectedResults,
            });
          }
        }
      },
    });
  }
}

function makeRecord({ collection, type, filterExpression }) {
  let recordFunc;
  switch (type) {
    case QuickSuggestTestUtils.RS_TYPE.AMP:
      recordFunc = QuickSuggestTestUtils.ampRemoteSettings;
      break;
    case QuickSuggestTestUtils.RS_TYPE.WIKIPEDIA:
      recordFunc = QuickSuggestTestUtils.wikipediaRemoteSettings;
      break;
    default:
      throw new Error("Unhandled RS type: " + type);
  }

  // Generate the record's keyword. Keywords are lowercase.
  let parts = [type];
  if (filterExpression) {
    // The keyword's format will be:
    // type filterExpressionValue0 filterExpressionValue1 filterExpressionValue2...
    //
    // Example:
    //
    // filterExpression: { country: "US", formFactor: "desktop" }
    // key: "amp us desktop"
    parts.push(...Object.values(filterExpression));
  } else {
    // The keyword's format will be:
    // type no-filter
    //
    // Example key: "amp no-filter"
    parts.push("no-filter");
  }
  let keyword = parts.join(" ").toLowerCase();

  let record = {
    collection,
    type,
    attachment: [
      recordFunc({
        keywords: [keyword],
        url: `https://example.com/${keyword}`,
        title: `Suggestion: ${keyword}`,
      }),
    ],
  };

  if (filterExpression) {
    record.filter_expression = Object.entries(filterExpression)
      .map(([k, v]) => `env.${k} == '${v}'`)
      .join(" && ");
  }

  return record;
}
