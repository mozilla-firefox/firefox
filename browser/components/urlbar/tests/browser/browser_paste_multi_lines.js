/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test handling whitespace chars such as "\n”.

const TEST_DATA = [
  {
    input: "this is a\ntest",
    expected: {
      urlbar: "this is a test",
      title: "this is a test",
      type: UrlbarUtils.RESULT_TYPE.SEARCH,
    },
  },
  {
    input: "this is    a\n\ttest",
    expected: {
      urlbar: "this is    a  test",
      title: "this is    a  test",
      type: UrlbarUtils.RESULT_TYPE.SEARCH,
    },
  },
  {
    input: "http:\n//\nexample.\ncom",
    expected: {
      urlbar: "http://example.com",
      title: "http://example.com/",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "htp:example.\ncom",
    expected: {
      urlbar: "htp:example.com",
      title: "example.com/",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "example.\ncom",
    expected: {
      urlbar: "example.com",
      title: "example.com/",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "http://example.com/foo       bar/",
    expected: {
      urlbar: "http://example.com/foo       bar/",
      title: "http://example.com/foo       bar/",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "http://exam\nple.com/foo       bar/",
    expected: {
      urlbar: "http://example.com/foo       bar/",
      title: "http://example.com/foo       bar/",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "javasc\nript:\nalert(1)",
    expected: {
      urlbar: "alert(1)",
      title: "alert(1)",
      type: UrlbarUtils.RESULT_TYPE.SEARCH,
    },
  },
  {
    input: "a\nb\nc",
    expected: {
      urlbar: "a b c",
      title: "a b c",
      type: UrlbarUtils.RESULT_TYPE.SEARCH,
    },
  },
  {
    input: "lo\ncal\nhost",
    expected: {
      urlbar: "localhost",
      title: "localhost/",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "data:text/html,<iframe\n src='example\n.com'>\n</iframe>",
    expected: {
      urlbar: "data:text/html,<iframe  src='example .com'> </iframe>",
      title: "data:text/html,<iframe  src='example .com'> </iframe>",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "data:,123\n4 5\n6",
    expected: {
      urlbar: "data:,123 4 5 6",
      title: "data:,123 4 5 6",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "data:text/html;base64,123\n4 5\n6",
    expected: {
      urlbar: "data:text/html;base64,1234 56",
      title: "data:text/html;base64,123456",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "http://example.com\n",
    expected: {
      urlbar: "http://example.com",
      title: "http://example.com/",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "http://example.com\r",
    expected: {
      urlbar: "http://example.com",
      title: "http://example.com/",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "http://ex\ra\nmp\r\nle.com\r\n",
    expected: {
      urlbar: "http://example.com",
      title: "http://example.com/",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "http://example.com/titled",
    expected: {
      urlbar: "http://example.com/titled",
      title: "example title",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "127.0.0.1\r",
    expected: {
      urlbar: "127.0.0.1",
      title: "127.0.0.1/",
      type: UrlbarUtils.RESULT_TYPE.URL,
    },
  },
  {
    input: "\r\n\r\n\r\n\r\n\r\n",
    expected: {
      urlbar: "",
      title: undefined,
      type: UrlbarUtils.RESULT_TYPE.SEARCH,
    },
  },
];

add_setup(async function () {
  // Pasting only newlines leaves the input empty, and the last TEST_DATA entry
  // expects the first result to be the search shortcut Top Site, so set up the
  // configuration to make Baidu available as a search engine.
  await SearchTestUtils.updateRemoteSettingsConfig([
    {
      identifier: "google",
      base: {
        name: "Google",
        aliases: ["google"],
        urls: {
          search: {
            base: "https://www.google.com/search",
            searchTermParamName: "q",
          },
        },
      },
    },
    {
      identifier: "baidu",
      base: {
        name: "百度",
        aliases: ["百度", "baidu"],
        urls: {
          search: {
            base: "https://www.baidu.com/baidu",
            searchTermParamName: "wd",
          },
        },
      },
    },
  ]);

  await SpecialPowers.pushPrefEnv({
    set: [
      // There are cases that URLBar loses focus before assertion of this test.
      // In that case, this test will be failed since the result is closed
      // before it. We use this pref so that keep the result even if lose focus.
      ["ui.popup.disable_autohide", true],
      // baidu.com dedupes with the Baidu search shortcut that TopSites pins.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://www.baidu.com/",
      ],
      // The shortcuts to pin are region-derived, and Baidu is only listed for
      // CN, so ask for it explicitly.
      [
        "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts.searchEngines",
        "baidu",
      ],
    ],
  });

  await PlacesUtils.history.clear();
  await PlacesTestUtils.addVisits({
    uri: "http://example.com/titled",
    title: "example title",
  });

  await updateTopSites(sites => sites?.[0]?.searchTopSite, true);

  registerCleanupFunction(async function () {
    await PlacesUtils.history.clear();
    SpecialPowers.clipboardCopyString("");
  });
});

add_task(async function test_paste_onto_urlbar() {
  for (const { input, expected } of TEST_DATA) {
    gURLBar.value = "";
    gURLBar.focus();

    await paste(input);
    await assertResult(expected);

    await UrlbarTestUtils.promisePopupClose(window);
  }
});

add_task(async function test_paste_after_opening_autocomplete_panel() {
  for (const { input, expected } of TEST_DATA) {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "",
    });

    await paste(input);
    await assertResult(expected);

    await UrlbarTestUtils.promisePopupClose(window);
  }
});

add_task(async function test_paste_onto_urlbar_on_website() {
  // Deliberately use a page that is not about:blank to ensure that
  // we correctly handle pasting while on a real webpage. We also use
  // mochi.test to avoid any interfering with tests that use example.com.
  let loaded = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    "http://mochi.test:8888/"
  );
  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    "http://mochi.test:8888/"
  );
  await loaded;

  for (const { input, expected } of TEST_DATA) {
    gURLBar.handleRevert();
    gURLBar.select();

    await paste(input);
    await assertResult(expected);

    await UrlbarTestUtils.promisePopupClose(window);
  }
});

async function assertResult(expected) {
  Assert.equal(gURLBar.value, expected.urlbar, "Pasted value is correct");
  Assert.ok(gURLBar.valueIsTyped, "Pasted value counts as typed.");

  const result = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(result.title, expected.title, "Title of result is correct");
  Assert.equal(result.type, expected.type, "Type of autocomplete is correct");

  if (gURLBar.value) {
    Assert.ok(
      gURLBar.hasAttribute("usertyping"),
      "Should have usertyping attribute"
    );
    Assert.ok(
      BrowserTestUtils.isVisible(gURLBar.goButton),
      "Go button should be visible"
    );
    Assert.equal(
      gURLBar.getAttribute("pageproxystate"),
      "invalid",
      "Pageproxystate is invalid"
    );
  } else {
    Assert.ok(
      !gURLBar.hasAttribute("usertyping"),
      "Should not have usertyping attribute"
    );
    Assert.ok(
      BrowserTestUtils.isHidden(gURLBar.goButton),
      "Go button should be hidden"
    );
  }
}

async function paste(input) {
  await SimpleTest.promiseClipboardChange(input.replace(/\r\n?/g, "\n"), () => {
    clipboardHelper.copyString(input);
  });

  document.commandDispatcher
    .getControllerForCommand("cmd_paste")
    .doCommand("cmd_paste");
}
