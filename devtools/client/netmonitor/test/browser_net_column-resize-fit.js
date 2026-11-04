/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests resizing of columns in NetMonitor.
 */
add_task(async function () {
  // Reset visibleColumns so we only get the default ones
  // and not all that are set in head.js
  Services.prefs.clearUserPref("devtools.netmonitor.visibleColumns");

  // Init network monitor
  const { monitor } = await initNetMonitor(SIMPLE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document } = monitor.panelWin;

  const visibleColumns = getCurrentVisibleColumns(monitor);

  // Wait for network events (to have some requests in the table)
  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  info("Testing column resize to fit using double-click on draggable resizer");
  const fileHeader = document.querySelector(`#requests-list-file-header-box`);
  const fileColumnResizer = fileHeader.querySelector(".column-resizer");

  EventUtils.sendMouseEvent({ type: "dblclick" }, fileColumnResizer);

  // After resize - get fresh prefs for tests.
  let columnsData = JSON.parse(
    Services.prefs.getCharPref("devtools.netmonitor.columnsData")
  );

  // `File` column before resize: 25%, after resize: 11.25%
  // `Transferred` column before resize: 10%, after resize: 10%
  checkColumnsData(columnsData, "file", 12);
  checkSumOfVisibleColumns(columnsData, visibleColumns);

  info(
    "Testing column resize to fit using context menu `Resize Column To Fit Content`"
  );

  // Resizing `transferred` column.
  EventUtils.sendMouseEvent(
    { type: "contextmenu" },
    document.querySelector("#requests-list-transferred-button")
  );

  await selectContextMenuItem(
    monitor,
    "request-list-header-resize-column-to-fit-content"
  );

  columnsData = JSON.parse(
    Services.prefs.getCharPref("devtools.netmonitor.columnsData")
  );

  // `Transferred` column before resize: 10%, after resize: 2.97%
  checkColumnsData(columnsData, "transferred", 3);
  checkSumOfVisibleColumns(columnsData, visibleColumns);

  // Done: clean up.
  return teardown(monitor);
});

function checkColumnsData(columnsData, column, expectedWidth) {
  const width = getWidthFromPref(columnsData, column);
  const widthsDiff = Math.abs(width - expectedWidth);
  Assert.less(
    widthsDiff,
    2,
    `Column ${column} has expected size. Got ${width}, Expected ${expectedWidth}`
  );
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
