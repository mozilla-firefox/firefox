/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests for the bookmark star being correct displayed for results matching
 * tags.
 */

add_task(async function () {
  registerCleanupFunction(async function () {
    await PlacesUtils.bookmarks.eraseEverything();
  });

  async function addTagItem(tagName) {
    let url = `http://example.com/this/is/tagged/${tagName}`;
    await PlacesUtils.bookmarks.insert({
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
      url,
      title: `test ${tagName}`,
    });
    PlacesUtils.tagging.tagURI(Services.io.newURI(url), [tagName]);
    await PlacesTestUtils.addVisits({
      uri: url,
      title: `Test page with tag ${tagName}`,
    });
  }

  // We use different tags for each part of the test, as otherwise the
  // autocomplete code tries to be smart by using the previously cached element
  // without updating it (since all parameters it knows about are the same).

  let testcases = [
    {
      description: "Test with suggest.bookmark=true",
      tagName: "tagtest1",
      prefs: {
        "suggest.bookmark": true,
      },
      input: "tagtest1",
      expected: {
        typeImageVisible: true,
      },
    },
    {
      description: "Test with suggest.bookmark=false",
      tagName: "tagtest2",
      prefs: {
        "suggest.bookmark": false,
      },
      input: "tagtest2",
      expected: {
        typeImageVisible: false,
      },
    },
    {
      description: "Test with suggest.bookmark=true (again)",
      tagName: "tagtest3",
      prefs: {
        "suggest.bookmark": true,
      },
      input: "tagtest3",
      expected: {
        typeImageVisible: true,
      },
    },
    {
      description: "Test with bookmark restriction token",
      tagName: "tagtest4",
      prefs: {
        "suggest.bookmark": true,
      },
      input: "* tagtest4",
      expected: {
        typeImageVisible: true,
      },
    },
    {
      description: "Test with history restriction token",
      tagName: "tagtest5",
      prefs: {
        "suggest.bookmark": true,
      },
      input: "^ tagtest5",
      expected: {
        typeImageVisible: false,
      },
    },
    {
      description: "Test partial tag and casing",
      tagName: "tagtest6",
      prefs: {
        "suggest.bookmark": true,
      },
      input: "TeSt6",
      expected: {
        typeImageVisible: true,
      },
    },
  ];

  for (let testcase of testcases) {
    info(`Test case: ${testcase.description}`);

    await addTagItem(testcase.tagName);
    for (let prefName of Object.keys(testcase.prefs)) {
      Services.prefs.setBoolPref(
        `browser.urlbar.${prefName}`,
        testcase.prefs[prefName]
      );
    }

    let context = await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: testcase.input,
    });

    // If testcase.input triggers local search mode, there won't be a heuristic.
    let resultIndex =
      context.searchMode && !context.searchMode.engineName ? 0 : 1;

    Assert.greaterOrEqual(
      UrlbarTestUtils.getResultCount(window),
      resultIndex + 1,
      `Should be at least ${resultIndex + 1} results`
    );

    let result = await UrlbarTestUtils.getDetailsOfResultAt(
      window,
      resultIndex
    );

    Assert.equal(
      result.type,
      UrlbarShared.RESULT_TYPE.URL,
      "Should have a URL result type"
    );
    // The Quantum Bar differs from the legacy urlbar in the fact that, if
    // bookmarks are filtered out, it won't show tags for history results.
    let expected_tags = !testcase.expected.typeImageVisible
      ? []
      : [testcase.tagName];
    Assert.deepEqual(
      result.tags,
      expected_tags,
      "Should have the expected tag"
    );

    if (testcase.expected.typeImageVisible) {
      Assert.equal(
        result.displayed.typeIcon,
        'url("chrome://browser/skin/urlbar/badge-bookmark.svg#ltr-icon")',
        "Should have the star image displayed or not as expected"
      );
    } else {
      Assert.equal(
        result.displayed.typeIcon,
        'url("chrome://browser/skin/urlbar/badge-history.svg#ltr-icon")',
        "Should have the star image displayed or not as expected"
      );
    }

    await UrlbarTestUtils.promisePopupClose(window);
    gURLBar.handleRevert();
  }
});
