/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// test for exposure events

"use strict";

// Avoid timeouts in verify mode, especially on "OS X 10.15 WebRender opt" in
// verify mode on Treeherder. This test has taken up to 275231ms on try runs in
// verify mode on that machine.
requestLongerTimeout(15);

add_setup(async function () {
  await initExposureTest();
});

add_task(async function engagement() {
  await doExposureTest({
    prefs: [
      ["exposureResults", suggestResultType("adm_sponsored")],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "amp",
        expectedVisible: ["adm_sponsored"],
      },
    ],
    trigger: doClick,
    expectedEvents: [{ results: "adm_sponsored", terminal: "true" }],
  });
});

add_task(async function abandonment() {
  await doExposureTest({
    prefs: [
      ["exposureResults", suggestResultType("adm_sponsored")],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "amp",
        expectedVisible: ["adm_sponsored"],
      },
    ],
    trigger: doBlur,
    expectedEvents: [{ results: "adm_sponsored", terminal: "true" }],
  });
});

add_task(async function oneExposureResult_shown_matched() {
  await doExposureTest({
    prefs: [
      ["exposureResults", suggestResultType("adm_sponsored")],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "amp",
        expectedVisible: ["adm_sponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "true" }],
  });
});

add_task(async function oneExposureResult_shown_notMatched() {
  await doExposureTest({
    prefs: [
      ["exposureResults", suggestResultType("adm_sponsored")],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "wikipedia",
        expectedNotVisible: ["adm_sponsored"],
      },
    ],
    expectedEvents: [],
  });
});

add_task(async function oneExposureResult_hidden_matched() {
  await doExposureTest({
    prefs: [
      ["exposureResults", suggestResultType("adm_sponsored")],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "amp",
        expectedNotVisible: ["adm_sponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "true" }],
  });
});

add_task(async function oneExposureResult_hidden_notMatched() {
  await doExposureTest({
    prefs: [
      ["exposureResults", suggestResultType("adm_sponsored")],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "wikipedia",
        expectedNotVisible: ["adm_sponsored"],
      },
    ],
    expectedEvents: [],
  });
});

add_task(async function manyExposureResults_shown_oneMatched_1() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "amp",
        expectedVisible: ["adm_sponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "true" }],
  });
});

add_task(async function manyExposureResults_shown_oneMatched_2() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "wikipedia",
        expectedVisible: ["adm_nonsponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_nonsponsored", terminal: "true" }],
  });
});

add_task(async function manyExposureResults_shown_manyMatched() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "amp and wikipedia",
        // Only the sponsored result should be visible and recorded since this
        // task shows exposures, at most one Suggest result should be shown, and
        // sponsored has the higher score.
        expectedVisible: ["adm_sponsored"],
        expectedNotVisible: ["adm_nonsponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "true" }],
  });
});

add_task(async function manyExposureResults_hidden_oneMatched_1() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "amp",
        expectedNotVisible: ["adm_sponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "true" }],
  });
});

add_task(async function manyExposureResults_hidden_oneMatched_2() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "wikipedia",
        expectedNotVisible: ["adm_nonsponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_nonsponsored", terminal: "true" }],
  });
});

add_task(async function manyExposureResults_hidden_manyMatched() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "amp and wikipedia",
        expectedNotVisible: ["adm_sponsored", "adm_nonsponsored"],
      },
    ],
    // Both results should be recorded since this task hides exposures and
    // there's no limit on the number of hidden-exposure Suggest results.
    expectedEvents: [
      { results: "adm_nonsponsored,adm_sponsored", terminal: "true,true" },
    ],
  });
});

add_task(async function manyQueries_oneExposureResult_shown_terminal_1() {
  await doExposureTest({
    prefs: [
      ["exposureResults", suggestResultType("adm_sponsored")],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "wikipedia",
        expectedVisible: ["adm_nonsponsored"],
      },
      {
        query: "amp",
        expectedVisible: ["adm_sponsored"],
        expectedNotVisible: ["adm_nonsponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "true" }],
  });
});

add_task(async function manyQueries_oneExposureResult_shown_terminal_2() {
  await doExposureTest({
    prefs: [
      ["exposureResults", suggestResultType("adm_sponsored")],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "amp",
        expectedVisible: ["adm_sponsored"],
      },
      {
        query: "wikipedia",
        expectedVisible: ["adm_nonsponsored"],
        expectedNotVisible: ["adm_sponsored"],
      },
      {
        query: "amp",
        expectedVisible: ["adm_sponsored"],
        expectedNotVisible: ["adm_nonsponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "true" }],
  });
});

add_task(async function manyQueries_oneExposureResult_shown_nonTerminal() {
  await doExposureTest({
    prefs: [
      ["exposureResults", suggestResultType("adm_sponsored")],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "amp",
        expectedVisible: ["adm_sponsored"],
      },
      {
        query: "wikipedia",
        expectedVisible: ["adm_nonsponsored"],
        expectedNotVisible: ["adm_sponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "false" }],
  });
});

add_task(async function manyQueries_oneExposureResult_hidden_terminal_1() {
  await doExposureTest({
    prefs: [
      ["exposureResults", suggestResultType("adm_sponsored")],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "wikipedia",
        expectedVisible: ["adm_nonsponsored"],
      },
      {
        query: "amp",
        expectedNotVisible: ["adm_nonsponsored", "adm_sponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "true" }],
  });
});

add_task(async function manyQueries_oneExposureResult_hidden_terminal_2() {
  await doExposureTest({
    prefs: [
      ["exposureResults", suggestResultType("adm_sponsored")],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "amp",
        expectedNotVisible: ["adm_sponsored"],
      },
      {
        query: "wikipedia",
        expectedVisible: ["adm_nonsponsored"],
        expectedNotVisible: ["adm_sponsored"],
      },
      {
        query: "amp",
        expectedNotVisible: ["adm_nonsponsored", "adm_sponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "true" }],
  });
});

add_task(async function manyQueries_oneExposureResult_hidden_nonTerminal() {
  await doExposureTest({
    prefs: [
      ["exposureResults", suggestResultType("adm_sponsored")],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "amp",
        expectedNotVisible: ["adm_sponsored"],
      },
      {
        query: "wikipedia",
        expectedVisible: ["adm_nonsponsored"],
        expectedNotVisible: ["adm_sponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "false" }],
  });
});

add_task(async function manyQueries_manyExposureResults_shown_1() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "wikipedia",
        expectedVisible: ["adm_nonsponsored"],
      },
      {
        query: "amp",
        expectedVisible: ["adm_sponsored"],
        expectedNotVisible: ["adm_nonsponsored"],
      },
    ],
    expectedEvents: [
      { results: "adm_nonsponsored,adm_sponsored", terminal: "false,true" },
    ],
  });
});

add_task(async function manyQueries_manyExposureResults_shown_2() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "amp",
        expectedVisible: ["adm_sponsored"],
      },
      {
        query: "wikipedia",
        expectedVisible: ["adm_nonsponsored"],
        expectedNotVisible: ["adm_sponsored"],
      },
    ],
    expectedEvents: [
      { results: "adm_nonsponsored,adm_sponsored", terminal: "true,false" },
    ],
  });
});

add_task(async function manyQueries_manyExposureResults_shown_3() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "amp",
        expectedVisible: ["adm_sponsored"],
      },
      {
        query: "wikipedia",
        expectedVisible: ["adm_nonsponsored"],
        expectedNotVisible: ["adm_sponsored"],
      },
      {
        query: "amp",
        expectedVisible: ["adm_sponsored"],
        expectedNotVisible: ["adm_nonsponsored"],
      },
    ],
    expectedEvents: [
      { results: "adm_nonsponsored,adm_sponsored", terminal: "false,true" },
    ],
  });
});

add_task(async function manyQueries_manyExposureResults_shown_4() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "doesn't match",
        expectedNotVisible: ["adm_nonsponsored", "adm_sponsored"],
      },
      {
        query: "amp and wikipedia",
        // Only the sponsored result should be visible and recorded since this
        // task shows exposures, at most one Suggest result should be shown, and
        // sponsored has the higher score.
        expectedVisible: ["adm_sponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "true" }],
  });
});

add_task(async function manyQueries_manyExposureResults_shown_5() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "amp and wikipedia",
        // Only the sponsored result should be visible and recorded since this
        // task shows exposures, at most one Suggest result should be shown, and
        // sponsored has the higher score.
        expectedVisible: ["adm_sponsored"],
      },
      {
        query: "doesn't match",
        expectedNotVisible: ["adm_nonsponsored", "adm_sponsored"],
      },
    ],
    expectedEvents: [{ results: "adm_sponsored", terminal: "false" }],
  });
});

add_task(async function manyQueries_manyExposureResults_hidden_1() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "wikipedia",
        expectedNotVisible: ["adm_nonsponsored"],
      },
      {
        query: "amp",
        expectedNotVisible: ["adm_nonsponsored", "adm_sponsored"],
      },
    ],
    expectedEvents: [
      { results: "adm_nonsponsored,adm_sponsored", terminal: "false,true" },
    ],
  });
});

add_task(async function manyQueries_manyExposureResults_hidden_2() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "amp",
        expectedNotVisible: ["adm_sponsored"],
      },
      {
        query: "wikipedia",
        expectedNotVisible: ["adm_nonsponsored", "adm_sponsored"],
      },
    ],
    expectedEvents: [
      { results: "adm_nonsponsored,adm_sponsored", terminal: "true,false" },
    ],
  });
});

add_task(async function manyQueries_manyExposureResults_hidden_3() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "amp",
        expectedNotVisible: ["adm_sponsored"],
      },
      {
        query: "wikipedia",
        expectedNotVisible: ["adm_nonsponsored", "adm_sponsored"],
      },
      {
        query: "amp",
        expectedNotVisible: ["adm_nonsponsored", "adm_sponsored"],
      },
    ],
    expectedEvents: [
      { results: "adm_nonsponsored,adm_sponsored", terminal: "false,true" },
    ],
  });
});

add_task(async function manyQueries_manyExposureResults_hidden_4() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "doesn't match",
        expectedNotVisible: ["adm_nonsponsored", "adm_sponsored"],
      },
      {
        query: "amp and wikipedia",
        expectedNotVisible: ["adm_nonsponsored", "adm_sponsored"],
      },
    ],
    expectedEvents: [
      { results: "adm_nonsponsored,adm_sponsored", terminal: "true,true" },
    ],
  });
});

add_task(async function manyQueries_manyExposureResults_hidden_5() {
  await doExposureTest({
    prefs: [
      [
        "exposureResults",
        [
          suggestResultType("adm_sponsored"),
          suggestResultType("adm_nonsponsored"),
        ].join(","),
      ],
      ["showExposureResults", false],
    ],
    queries: [
      {
        query: "amp and wikipedia",
        expectedNotVisible: ["adm_nonsponsored", "adm_sponsored"],
      },
      {
        query: "doesn't match",
        expectedNotVisible: ["adm_nonsponsored", "adm_sponsored"],
      },
    ],
    expectedEvents: [
      { results: "adm_nonsponsored,adm_sponsored", terminal: "false,false" },
    ],
  });
});

add_task(async function suggestExposure_matched() {
  // TODO Bug 1961040: Convert ExposureSuggestions to DynamicSuggestions.
  info("Skipping task: TODO Bug 1961040");
  return;

  // eslint-disable-next-line no-unreachable
  await doExposureTest({
    prefs: [["quicksuggest.exposureSuggestionTypes", "aaa"]],
    queries: [
      {
        query: "aaa keyword",
        expectedNotVisible: ["exposure"],
      },
    ],
    expectedEvents: [{ results: "exposure", terminal: "true" }],
  });
});

add_task(async function suggestExposure_notMatched() {
  // TODO Bug 1961040: Convert ExposureSuggestions to DynamicSuggestions.
  info("Skipping task: TODO Bug 1961040");
  return;

  // eslint-disable-next-line no-unreachable
  await doExposureTest({
    prefs: [["quicksuggest.exposureSuggestionTypes", "aaa"]],
    queries: [
      {
        query: "bbb keyword",
        expectedNotVisible: ["exposure"],
      },
    ],
    expectedEvents: [],
  });
});

add_task(async function suggestExposure_showExposureResults() {
  // TODO Bug 1961040: Convert ExposureSuggestions to DynamicSuggestions.
  info("Skipping task: TODO Bug 1961040");
  return;

  // eslint-disable-next-line no-unreachable
  await doExposureTest({
    prefs: [
      ["quicksuggest.exposureSuggestionTypes", "aaa"],
      ["showExposureResults", true],
    ],
    queries: [
      {
        query: "aaa keyword",
        expectedNotVisible: ["exposure"],
      },
    ],
    expectedEvents: [{ results: "exposure", terminal: "true" }],
  });
});
