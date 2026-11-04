/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test the filters_changed telemetry event.
 */
add_task(async function () {
  const { monitor } = await initNetMonitor(HTTPS_SIMPLE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getDisplayedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  await waitForAllNetworkUpdateEvents();
  // Remove all data (you can check about:glean).
  Services.fog.testResetFOG();

  // Reload to have one request in the list.
  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await waitForAllNetworkUpdateEvents();
  await navigateTo(HTTPS_SIMPLE_URL);
  await onNetworkEvents;

  info("Click on the 'HTML' filter");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-html-button")
  );

  let events = Glean.devtoolsMain.filtersChangedNetmonitor.testGetValue();
  is(1, events.length);
  is("html", events[0].extra.trigger);
  is("html", events[0].extra.active);
  is("all,css,js,xhr,fonts,images,media,ws,other", events[0].extra.inactive);
  Services.fog.testResetFOG();

  info("Click on the 'CSS' filter");
  EventUtils.sendMouseEvent(
    { type: "click" },
    document.querySelector(".requests-list-filter-css-button")
  );

  events = Glean.devtoolsMain.filtersChangedNetmonitor.testGetValue();
  is(1, events.length);
  is("css", events[0].extra.trigger);
  is("html,css", events[0].extra.active);
  is("all,js,xhr,fonts,images,media,ws,other", events[0].extra.inactive);
  Services.fog.testResetFOG();

  info("Filter the output using the text filter input");
  setFreetextFilter(monitor, "nomatch");

  // Wait till the text filter is applied.
  await waitUntil(() => !getDisplayedRequests(store.getState()).length);

  events = Glean.devtoolsMain.filtersChangedNetmonitor.testGetValue();
  is(1, events.length);
  is("text", events[0].extra.trigger);
  is("html,css", events[0].extra.active);
  is("all,js,xhr,fonts,images,media,ws,other", events[0].extra.inactive);

  return teardown(monitor);
});

function setFreetextFilter(monitor, value) {
  const { document } = monitor.panelWin;

  const filterBox = document.querySelector(".devtools-filterinput");
  filterBox.focus();
  filterBox.value = "";
  typeInNetmonitor(value, monitor);
}
