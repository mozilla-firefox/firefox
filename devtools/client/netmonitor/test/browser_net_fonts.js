/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if font preview is generated correctly
 */

add_task(async function () {
  const { monitor } = await initNetMonitor(FONTS_URL + "?name=fonts", {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  // Reload the page to get the font request
  const waitForRequests = waitForNetworkEvents(monitor, 3);
  await reloadSelectedTab();
  await waitForRequests;

  const onDomUpdated = waitForDOM(
    document,
    "#response-panel .response-font[src^='data:']"
  );

  const requests = document.querySelectorAll(
    ".request-list-item .requests-list-status"
  );

  // Check first font request
  clickElement(requests[1], monitor);
  clickOnSidebarTab(document, "response");

  await onDomUpdated;

  ok(true, "Font preview is shown");

  const tabpanel = document.querySelector("#response-panel");
  let image = tabpanel.querySelector(".response-font");

  await waitFor(
    () => image.complete === true && image.naturalWidth > 0,
    "Wait for the font preview to load"
  );

  ok(
    image.complete && image.naturalHeight !== 0,
    "Font preview got generated correctly"
  );

  let fontData = document.querySelectorAll(".tabpanel-summary-value");
  is(fontData[0].textContent, "Ostrich Sans Medium", "Font name is correct");
  // "font/ttf" is returned on Linux, which is the expected MIME type, though
  // "application/octet-stream" is also accepted which is returned on Windows and MacOS.
  ok(
    ["font/ttf", "application/octet-stream"].includes(fontData[1].textContent),
    "MIME type is correct"
  );

  // Check second font request
  clickElement(requests[2], monitor);

  await waitForDOM(document, "#response-panel .response-font[src^='data:']");

  image = tabpanel.querySelector(".response-font");

  await waitFor(
    () => image.complete === true && image.naturalWidth > 0,
    "Wait for the font preview to load"
  );

  ok(
    image.complete && image.naturalHeight !== 0,
    "Font preview got generated correctly"
  );

  fontData = document.querySelectorAll(".tabpanel-summary-value");
  is(fontData[0].textContent, "Ostrich Sans Black", "Font name is correct");
  // Actually expected is "font/ttf", though "application/octet-stream" is
  // ok as well and obviously returned when running the test locally.
  ok(
    ["font/ttf", "application/octet-stream"].includes(fontData[1].textContent),
    "MIME type is correct"
  );

  await teardown(monitor);
});
