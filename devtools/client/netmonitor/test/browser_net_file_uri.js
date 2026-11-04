/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that file URL requests are properly displayed in the network monitor.
 */
add_task(async function test_file_uris() {
  const TEST_URI = Services.io.newFileURI(
    new FileUtils.File(getTestFilePath("html_file_channel.html"))
  ).spec;

  const { monitor } = await initNetMonitor(TEST_URI, {
    requestCount: 2,
    waitForLoad: false,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 2);
  reloadSelectedTab({ waitForLoad: false });
  await onNetworkEvents;

  const htmlEntry = document.querySelectorAll(".request-list-item")[0];
  ok(
    htmlEntry
      .querySelector(".requests-list-url")
      .innerText.endsWith("file_channel.html"),
    "The url for the html request is correct"
  );
  is(
    htmlEntry.querySelector(".requests-list-scheme").innerText,
    "file",
    "The scheme for the html request is correct"
  );
  ok(hasValidSize(htmlEntry), "The request shows a valid size");

  const imageEntry = document.querySelectorAll(".request-list-item")[1];
  ok(
    imageEntry
      .querySelector(".requests-list-url")
      .innerText.endsWith("test-image.png"),
    "The url for the image request is correct"
  );
  is(
    imageEntry.querySelector(".requests-list-scheme").innerText,
    "file",
    "The scheme for the image request is correct"
  );
  ok(hasValidSize(imageEntry), "The request shows a valid size");

  const onResponseContent = monitor.panelWin.api.once(
    TEST_EVENTS.RECEIVED_RESPONSE_CONTENT
  );

  info("Check that a valid image is loaded in the response tab");
  const waitDOM = waitForDOM(document, "#response-panel .response-image");
  store.dispatch(Actions.selectRequestByIndex(1));
  document.querySelector("#response-tab").click();
  const [imageNode] = await waitDOM;
  await waitFor(
    () => imageNode.complete === true && imageNode.naturalWidth > 0,
    "Wait for the image to load"
  );
  await onResponseContent;

  info("Verify we only have 2 requests, and the chrome request was not listed");
  const sortedRequests = getSortedRequests(store.getState());
  is(sortedRequests.length, 2, "Only 2 requests are displayed");

  await teardown(monitor);
});
