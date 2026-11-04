/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Makes sure Pie+Table Charts have the right internal structure.
 */

add_task(async function () {
  const {
    L10N,
  } = require("resource://devtools/client/netmonitor/src/utils/l10n.js");

  const { monitor } = await initNetMonitor(HTTPS_SIMPLE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, windowRequire } = monitor.panelWin;
  const { Chart } = windowRequire("devtools/client/shared/widgets/Chart");

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await navigateTo(HTTPS_SIMPLE_URL);
  await onNetworkEvents;

  const chart = Chart.PieTable(document, {
    title: "Table title",
    data: [
      {
        size: 1,
        label: 11.1,
      },
      {
        size: 2,
        label: 12.2,
      },
      {
        size: 3,
        label: 13.3,
      },
    ],
    strings: {
      label2: (value, index) => value + ["foo", "bar", "baz"][index],
    },
    totals: {
      size: value => "Hello " + L10N.numberWithDecimals(value, 2),
      label: value => "World " + L10N.numberWithDecimals(value, 2),
    },
    header: {
      label1: "",
      label2: "",
    },
  });

  ok(chart.pie, "The pie chart proxy is accessible.");
  ok(chart.table, "The table chart proxy is accessible.");

  const { node } = chart;
  const rows = node.querySelectorAll(".table-chart-row");
  const sums = node.querySelectorAll(".table-chart-summary-label");

  ok(
    node.classList.contains("pie-table-chart-container"),
    "A pie+table chart container was created successfully."
  );

  ok(
    node.querySelector(".table-chart-title"),
    "A title node was created successfully."
  );
  ok(
    node.querySelector(".pie-chart-container"),
    "A pie chart was created successfully."
  );
  ok(
    node.querySelector(".table-chart-container"),
    "A table chart was created successfully."
  );

  is(
    rows.length,
    4,
    "There should be 3 pie chart slices and 1 header created."
  );
  is(
    rows.length,
    4,
    "There should be 3 table chart rows and 1 header created."
  );
  is(sums.length, 2, "There should be 2 total summaries and 1 header created.");

  await teardown(monitor);
});
