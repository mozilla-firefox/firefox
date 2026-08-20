/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_receive_punycode_result() {
  let url = "https://www.اختبار.اختبار.org:5000/";

  // eslint-disable-next-line jsdoc/require-jsdoc
  class ResultWithHighlightsProvider extends UrlbarTestUtils.TestProvider {
    startQuery(context, addCallback) {
      let result = new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.URL,
        source: UrlbarShared.RESULT_SOURCE.HISTORY,
        suggestedIndex: 0,
        payload: {
          url,
        },
        highlights: {
          url: UrlbarShared.HIGHLIGHT.TYPED,
        },
      });
      addCallback(this, result);
    }

    getViewUpdate(_result) {
      return {};
    }
  }
  let provider = new ResultWithHighlightsProvider();

  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  registerCleanupFunction(async () => {
    providersManager.unregisterProvider(provider);
    await UrlbarTestUtils.promisePopupClose(window, () => gURLBar.blur());
    gURLBar.handleRevert();
  });
  providersManager.registerProvider(provider);

  info("Open the result popup");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    value: "org",
    window,
    fireInputEvent: true,
  });
  let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 0);
  is(row.result.type, UrlbarShared.RESULT_TYPE.URL, "row.result.type");
  is(
    row.result.getDisplayableValueAndHighlights("url", { isURL: true }).value,
    "اختبار.اختبار.org:5000",
    "Result is trimmed and formatted correctly."
  );
  is(
    row.result.getDisplayableValueAndHighlights("title").value,
    "www.اختبار.اختبار.org:5000",
    "Result is trimmed and formatted correctly."
  );

  let firstRow = document.querySelector(".urlbarView-row");
  let firstRowUrl = firstRow.querySelector(".urlbarView-url");

  is(
    firstRowUrl.innerHTML.charAt(0),
    "\u200e",
    "UrlbarView row url contains LRM"
  );
  // Tests if highlights are correct after inserting lrm symbol
  is(
    firstRowUrl.querySelector("strong")?.innerText,
    "org",
    "Correct part of url is highlighted"
  );
  is(
    firstRow.querySelector(".urlbarView-title strong")?.innerText,
    "org",
    "Correct part of title is highlighted"
  );
});
