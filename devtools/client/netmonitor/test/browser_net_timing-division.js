/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if timing intervals are divided against seconds when appropriate.
 */
add_task(async function () {
  // Show only few columns, so there is enough space
  // for the waterfall.
  await pushPref(
    "devtools.netmonitor.visibleColumns",
    '["status", "contentSize", "waterfall"]'
  );

  const { tab, monitor } = await initNetMonitor(CUSTOM_GET_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 2);
  // Timeout needed for having enough divisions on the time scale.
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    content.wrappedJSObject.performRequests(2, null, 3000);
  });
  await onNetworkEvents;

  const milDivs = document.querySelectorAll(
    ".requests-list-timings-division[data-division-scale=millisecond]"
  );
  const secDivs = document.querySelectorAll(
    ".requests-list-timings-division[data-division-scale=second]"
  );
  const minDivs = document.querySelectorAll(
    ".requests-list-timings-division[data-division-scale=minute]"
  );

  info("Number of millisecond divisions: " + milDivs.length);
  info("Number of second divisions: " + secDivs.length);
  info("Number of minute divisions: " + minDivs.length);

  milDivs.forEach(div => info(`Millisecond division: ${div.textContent}`));
  secDivs.forEach(div => info(`Second division: ${div.textContent}`));
  minDivs.forEach(div => info(`Minute division: ${div.textContent}`));

  is(
    store.getState().requests.requests.length,
    2,
    "There should be only two requests made."
  );

  ok(
    secDivs.length,
    "There should be at least one division on the seconds time scale."
  );
  ok(
    secDivs[0].textContent.match(/\d+\.\d{2}\s\w+/),
    "The division on the seconds time scale looks legit."
  );

  return teardown(monitor);
});
