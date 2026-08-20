/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests results with `suggestedIndex` and `isSuggestedIndexRelativeToGroup`.

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const MAX_RESULTS = 10;

// Default result groups used in the tests below.
const RESULT_GROUPS = {
  children: [
    {
      group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
      flexChildren: true,
      children: [
        {
          flex: 1,
          group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
        },
        {
          flex: 1,
          group: UrlbarShared.RESULT_GROUP.GENERAL,
        },
        {
          flex: 1,
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
        },
      ],
    },
  ],
};

let sandbox;
add_setup(async () => {
  sandbox = lazy.sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });
});

add_task(async function test() {
  // Create the default non-suggestedIndex results we'll use for tests that
  // don't specify `otherResults`.
  let basicResults = [
    ...makeHistoryResults(),
    ...makeFormHistoryResults(),
    ...makeRemoteSuggestionResults(),
  ];

  // Test cases follow. Each object in `tests` has the following properties:
  //
  // * {string} desc
  // * {object} suggestedIndexResults
  //   Describes the suggestedIndex results the test provider should return.
  //   Properties:
  //     * {number} suggestedIndex
  //     * {UrlbarShared.RESULT_GROUP} group
  //       This will force the result to have the given group.
  // * {array} expected
  //   Describes the expected results the muxer should return, i.e., the search
  //   results. Each object in the array describes a slice of expected results.
  //   The size of the slice is defined by the `count` property.
  //     * {UrlbarShared.RESULT_GROUP} group
  //       The expected group of the results in the slice.
  //     * {number} count
  //       The number of results in the slice.
  //     * {number} [offset]
  //       Can be used to offset the starting index of the slice in the results.
  // * {array} [otherResults]
  //   An array of results besides the group-relative suggestedIndex results
  //   that the provider should return. If not specified `basicResults` is used.
  // * {array} [resultGroups]
  //   The result groups to use. If not specified `RESULT_GROUPS` is used.
  // * {number} [maxRichResults]
  //   The `maxRichResults` pref will be set to this value. If not specified
  //   `MAX_RESULTS` is used.
  let tests = [
    {
      desc: "First result in GENERAL",
      suggestedIndexResults: [
        {
          suggestedIndex: 0,
          group: UrlbarShared.RESULT_GROUP.GENERAL,
        },
      ],
      expected: [
        {
          group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
          count: 4,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          suggestedIndex: 0,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          count: 2,
        },
        {
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
          // The muxer will remove the first 4 remote suggestions because they
          // dupe the earlier form history.
          offset: 4,
          count: 3,
        },
      ],
    },

    {
      desc: "Last result in GENERAL",
      suggestedIndexResults: [
        {
          suggestedIndex: -1,
          group: UrlbarShared.RESULT_GROUP.GENERAL,
        },
      ],
      expected: [
        {
          group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
          count: 4,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          count: 2,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          suggestedIndex: -1,
        },
        {
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
          // The muxer will remove the first 4 remote suggestions because they
          // dupe the earlier form history.
          offset: 4,
          count: 3,
        },
      ],
    },

    {
      desc: "First result in GENERAL_PARENT",
      suggestedIndexResults: [
        {
          suggestedIndex: 0,
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
        },
      ],
      expected: [
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
          suggestedIndex: 0,
        },
        {
          group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
          count: 3,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          count: 3,
        },
        {
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
          // The muxer will remove the first 3 remote suggestions because they
          // dupe the earlier form history.
          offset: 3,
          count: 3,
        },
      ],
    },

    {
      desc: "Last result in GENERAL_PARENT",
      suggestedIndexResults: [
        {
          suggestedIndex: -1,
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
        },
      ],
      expected: [
        {
          group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
          count: 3,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          count: 3,
        },
        {
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
          // The muxer will remove the first 3 remote suggestions because they
          // dupe the earlier form history.
          offset: 3,
          count: 3,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
          suggestedIndex: -1,
        },
      ],
    },

    {
      desc: "First and last results in GENERAL",
      suggestedIndexResults: [
        {
          suggestedIndex: 0,
          group: UrlbarShared.RESULT_GROUP.GENERAL,
        },
        {
          suggestedIndex: -1,
          group: UrlbarShared.RESULT_GROUP.GENERAL,
        },
      ],
      expected: [
        {
          group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
          count: 4,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          suggestedIndex: 0,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          count: 1,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          suggestedIndex: -1,
        },
        {
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
          // The muxer will remove the first 4 remote suggestions because they
          // dupe the earlier form history.
          offset: 4,
          count: 3,
        },
      ],
    },

    {
      desc: "First and last results in GENERAL_PARENT",
      suggestedIndexResults: [
        {
          suggestedIndex: 0,
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
        },
        {
          suggestedIndex: -1,
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
        },
      ],
      expected: [
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
          suggestedIndex: 0,
        },
        {
          group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
          count: 3,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          count: 3,
        },
        {
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
          // The muxer will remove the first 3 remote suggestions because they
          // dupe the earlier form history.
          offset: 3,
          count: 2,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
          suggestedIndex: -1,
        },
      ],
    },

    {
      desc: "First result in GENERAL_PARENT, first result in GENERAL",
      suggestedIndexResults: [
        {
          suggestedIndex: 0,
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
        },
        {
          suggestedIndex: 0,
          group: UrlbarShared.RESULT_GROUP.GENERAL,
        },
      ],
      expected: [
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
          suggestedIndex: 0,
        },
        {
          group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
          count: 3,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          suggestedIndex: 0,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          count: 2,
        },
        {
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
          // The muxer will remove the first 3 remote suggestions because they
          // dupe the earlier form history.
          offset: 3,
          count: 3,
        },
      ],
    },

    {
      desc: "Results in sibling group, no other results in same group",
      otherResults: makeFormHistoryResults(),
      suggestedIndexResults: [
        {
          suggestedIndex: -1,
          group: UrlbarShared.RESULT_GROUP.GENERAL,
        },
      ],
      expected: [
        {
          group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
          count: 9,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          suggestedIndex: -1,
        },
      ],
    },

    {
      desc: "Results in sibling group, no other results in same group, has child group",
      resultGroups: {
        flexChildren: true,
        children: [
          {
            flex: 2,
            group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
          },
          {
            flex: 1,
            group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
            children: [{ group: UrlbarShared.RESULT_GROUP.GENERAL }],
          },
        ],
      },
      otherResults: makeRemoteSuggestionResults(),
      suggestedIndexResults: [
        {
          suggestedIndex: -1,
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
        },
      ],
      expected: [
        {
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
          count: 9,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
          suggestedIndex: -1,
        },
      ],
    },

    {
      desc: "Complex group nesting with global suggestedIndex with resultSpan",
      resultGroups: {
        children: [
          {
            maxResultCount: 1,
            children: [{ group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST }],
          },
          {
            flexChildren: true,
            children: [
              {
                flex: 2,
                group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
              },
              {
                flex: 1,
                group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
                children: [{ group: UrlbarShared.RESULT_GROUP.GENERAL }],
              },
            ],
          },
        ],
      },
      otherResults: [
        // heuristic
        new UrlbarResult({
          type: UrlbarShared.RESULT_TYPE.SEARCH,
          source: UrlbarShared.RESULT_SOURCE.SEARCH,
          heuristic: true,
          group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST,
          payload: {
            engine: "test",
            suggestion: "foo",
            lowerCaseSuggestion: "foo",
          },
        }),
        // global suggestedIndex with resultSpan = 2
        new UrlbarResult({
          type: UrlbarShared.RESULT_TYPE.SEARCH,
          source: UrlbarShared.RESULT_SOURCE.SEARCH,
          suggestedIndex: 1,
          resultSpan: 2,
          payload: {
            engine: "test",
          },
        }),
        // remote suggestions
        ...makeRemoteSuggestionResults(),
      ],
      suggestedIndexResults: [
        {
          suggestedIndex: -1,
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
        },
      ],
      expected: [
        {
          group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST,
          count: 1,
        },
        {
          group: UrlbarShared.RESULT_GROUP.SUGGESTED_INDEX,
          suggestedIndex: 1,
          resultSpan: 2,
          count: 1,
        },
        {
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
          count: 6,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
          suggestedIndex: -1,
        },
      ],
    },

    {
      desc: "Last result in REMOTE_SUGGESTION, maxRichResults too small to add any REMOTE_SUGGESTION",
      maxRichResults: 2,
      suggestedIndexResults: [
        {
          suggestedIndex: -1,
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
        },
      ],
      expected: [
        {
          group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
          count: 1,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          count: 1,
        },
        // The suggestedIndex result should not be added.
      ],
    },

    {
      desc: "Last result in REMOTE_SUGGESTION, maxRichResults just big enough to show one REMOTE_SUGGESTION",
      maxRichResults: 3,
      suggestedIndexResults: [
        {
          suggestedIndex: -1,
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
        },
      ],
      expected: [
        {
          group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
          count: 1,
        },
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL,
          count: 1,
        },
        {
          group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
          suggestedIndex: -1,
        },
      ],
    },
  ];

  let controller = UrlbarTestUtils.newMockController();

  for (let {
    desc,
    suggestedIndexResults,
    expected,
    resultGroups,
    otherResults,
    maxRichResults = MAX_RESULTS,
  } of tests) {
    info(`Running test: ${desc}`);

    setResultGroups(resultGroups || RESULT_GROUPS);

    UrlbarPrefs.set("maxRichResults", maxRichResults);

    // Make the array of all results and do a search.
    let results = (otherResults || basicResults).concat(
      makeSuggestedIndexResults(suggestedIndexResults)
    );
    let provider = registerBasicTestProvider(results);
    let context = createContext(undefined, { providers: [provider.name] });
    let providersManager = ProvidersManager.getInstanceForSap("urlbar");
    await providersManager.startQuery(context, controller);

    // Make the list of expected results.
    let expectedResults = [];
    for (let { group, offset, count, suggestedIndex } of expected) {
      // Find the index in `results` of the expected result.
      let index = results.findIndex(
        r =>
          UrlbarShared.getResultGroup(r) == group &&
          r.suggestedIndex === suggestedIndex
      );
      Assert.notEqual(
        index,
        -1,
        "Sanity check: Expected result is in `results`"
      );
      if (offset) {
        index += offset;
      }

      // Extract the expected number of results from `results` and append them
      // to the expected results array.
      count = count || 1;
      expectedResults.push(...results.slice(index, index + count));
    }

    Assert.deepEqual(context.results, expectedResults);

    providersManager.unregisterProvider(provider);
  }
});

function makeHistoryResults(count = MAX_RESULTS) {
  let results = [];
  for (let i = 0; i < count; i++) {
    results.push(
      new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.URL,
        source: UrlbarShared.RESULT_SOURCE.HISTORY,
        payload: { url: "http://example.com/" + i },
      })
    );
  }
  return results;
}

function makeRemoteSuggestionResults(count = MAX_RESULTS) {
  let results = [];
  for (let i = 0; i < count; i++) {
    results.push(
      new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.SEARCH,
        source: UrlbarShared.RESULT_SOURCE.SEARCH,
        payload: {
          engine: "test",
          query: "test",
          suggestion: "test " + i,
          lowerCaseSuggestion: "test " + i,
        },
      })
    );
  }
  return results;
}

function makeFormHistoryResults(count = MAX_RESULTS) {
  let results = [];
  for (let i = 0; i < count; i++) {
    results.push(
      new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.SEARCH,
        source: UrlbarShared.RESULT_SOURCE.HISTORY,
        payload: {
          engine: "test",
          suggestion: "test " + i,
          lowerCaseSuggestion: "test " + i,
        },
      })
    );
  }
  return results;
}

function makeSuggestedIndexResults(objects) {
  return objects.map(({ suggestedIndex, group }) =>
    Object.assign(
      new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.URL,
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        group,
        suggestedIndex,
        isSuggestedIndexRelativeToGroup: true,
        payload: {
          url: "http://example.com/si " + suggestedIndex,
        },
      })
    )
  );
}

function setResultGroups(resultGroups) {
  sandbox.restore();
  if (resultGroups) {
    sandbox.stub(UrlbarPrefs, "getResultGroups").returns(resultGroups);
  }
}
