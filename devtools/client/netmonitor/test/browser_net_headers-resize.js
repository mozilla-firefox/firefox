/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests resizing of columns in NetMonitor.
 */
add_task(async function () {
  await testForGivenDir("ltr");

  await testForGivenDir("rtl");
});

async function testForGivenDir(dir) {
  if (dir === "rtl") {
    await pushPref("intl.l10n.pseudo", "bidi");
  } else {
    await pushPref("intl.l10n.pseudo", "");
  }

  // Reset visibleColumns so we only get the default ones
  // and not all that are set in head.js
  Services.prefs.clearUserPref("devtools.netmonitor.visibleColumns");

  const initialColumnData = Services.prefs.getCharPref(
    "devtools.netmonitor.columnsData"
  );

  // Init network monitor
  const { monitor } = await initNetMonitor(SIMPLE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");
  let visibleColumns = getCurrentVisibleColumns(monitor);
  const { document, windowRequire, store } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  // Wait for network events (to have some requests in the table)
  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  const headers = document.querySelector(".requests-list-headers");
  const parentWidth = headers.getBoundingClientRect().width;

  // 1. Change File column from 25% (default) to 20%
  // Size column should then change from 5% (default) to 10%
  // When File width changes, contentSize should compensate the change.
  info("Resize file & check changed prefs...");
  const fileHeader = document.querySelector(`#requests-list-file-header-box`);

  resizeColumn(fileHeader, 20, parentWidth, dir);

  // after resize - get fresh prefs for tests
  let columnsData = JSON.parse(
    Services.prefs.getCharPref("devtools.netmonitor.columnsData")
  );
  checkColumnsData(columnsData, "file", 20);
  checkColumnsData(columnsData, "contentSize", 10);
  checkSumOfVisibleColumns(columnsData, visibleColumns);

  // 2. Change Waterfall column width and check that the size
  // of waterfall changed correctly and all the other columns changed size.
  info("Resize waterfall & check changed prefs...");
  const waterfallHeader = document.querySelector(
    `#requests-list-waterfall-header-box`
  );
  // before resizing waterfall -> save old columnsData for later testing
  const oldColumnsData = JSON.parse(
    Services.prefs.getCharPref("devtools.netmonitor.columnsData")
  );
  resizeWaterfallColumn(waterfallHeader, 30, parentWidth, dir); // 30 fails currently!

  // after resize - get fresh prefs for tests
  columnsData = JSON.parse(
    Services.prefs.getCharPref("devtools.netmonitor.columnsData")
  );

  checkColumnsData(columnsData, "waterfall", 30);
  checkSumOfVisibleColumns(columnsData, visibleColumns);
  checkAllColumnsChanged(columnsData, oldColumnsData, visibleColumns);

  // 3. Check that all rows have the right column sizes.
  info("Checking alignment of columns and headers...");
  const requestsContainer = document.querySelector(".requests-list-row-group");
  testColumnsAlignment(headers, requestsContainer);

  // 4. Hide all columns but size and waterfall
  // and check that they resize correctly. Then resize
  // waterfall to 50% => size should take up 50%
  info("Hide all but 2 columns - size & waterfall and check resizing...");
  await hideMoreColumns(monitor, [
    "status",
    "method",
    "domain",
    "file",
    "initiator",
    "type",
    "transferred",
  ]);

  resizeWaterfallColumn(waterfallHeader, 50, parentWidth, dir);
  // after resize - get fresh prefs for tests
  columnsData = JSON.parse(
    Services.prefs.getCharPref("devtools.netmonitor.columnsData")
  );
  visibleColumns = getCurrentVisibleColumns(monitor);

  checkColumnsData(columnsData, "contentSize", 50);
  checkColumnsData(columnsData, "waterfall", 50);
  checkSumOfVisibleColumns(columnsData, visibleColumns);

  // 5. Hide all columns but domain and file
  // and resize domain to 50% => file should be 50%
  info("Hide all but 2 columns - domain & file and check resizing...");
  await showMoreColumns(monitor, ["domain", "file"]);
  await hideMoreColumns(monitor, ["contentSize", "waterfall"]);

  const domainHeader = document.querySelector(
    `#requests-list-domain-header-box`
  );
  resizeColumn(domainHeader, 50, parentWidth, dir);

  // after resize - get fresh prefs for tests
  columnsData = JSON.parse(
    Services.prefs.getCharPref("devtools.netmonitor.columnsData")
  );

  visibleColumns = JSON.parse(
    Services.prefs.getCharPref("devtools.netmonitor.visibleColumns")
  );

  checkColumnsData(columnsData, "domain", 50);
  checkColumnsData(columnsData, "file", 50);
  checkSumOfVisibleColumns(columnsData, visibleColumns);

  // Done: clean up.
  Services.prefs.setCharPref(
    "devtools.netmonitor.columnsData",
    initialColumnData
  );
  return teardown(monitor);
}

async function hideMoreColumns(monitor, arr) {
  for (let i = 0; i < arr.length; i++) {
    await hideColumn(monitor, arr[i]);
  }
}

async function showMoreColumns(monitor, arr) {
  for (let i = 0; i < arr.length; i++) {
    await showColumn(monitor, arr[i]);
  }
}

function checkColumnsData(columnsData, column, expectedWidth) {
  const widthInPref = Math.round(getWidthFromPref(columnsData, column));
  is(widthInPref, expectedWidth, "Column " + column + " has expected size.");
}

function checkSumOfVisibleColumns(columnsData, visibleColumns) {
  let sum = 0;
  visibleColumns.forEach(column => {
    sum += getWidthFromPref(columnsData, column);
  });
  sum = Math.round(sum);
  is(sum, 100, "All visible columns cover 100%.");
}

function getWidthFromPref(columnsData, column) {
  const widthInPref = columnsData.find(function (element) {
    return element.name === column;
  }).width;
  return widthInPref;
}

function checkAllColumnsChanged(columnsData, oldColumnsData, visibleColumns) {
  const oldWaterfallWidth = getWidthFromPref(oldColumnsData, "waterfall");
  const newWaterfallWidth = getWidthFromPref(columnsData, "waterfall");
  visibleColumns.forEach(column => {
    // do not test waterfall against waterfall
    if (column !== "waterfall") {
      const oldWidth = getWidthFromPref(oldColumnsData, column);
      const newWidth = getWidthFromPref(columnsData, column);

      // Test that if waterfall is smaller all other columns are bigger
      if (oldWaterfallWidth > newWaterfallWidth) {
        is(
          oldWidth < newWidth,
          true,
          "Column " + column + " has changed width correctly."
        );
      }
      // Test that if waterfall is bigger all other columns are smaller
      if (oldWaterfallWidth < newWaterfallWidth) {
        is(
          oldWidth > newWidth,
          true,
          "Column " + column + " has changed width correctly."
        );
      }
    }
  });
}
