/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Makes sure Table Charts correctly handle empty source data.
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

  const table = Chart.Table(document, {
    data: [],
    totals: {
      label1: value => "Hello " + L10N.numberWithDecimals(value, 2),
      label2: value => "World " + L10N.numberWithDecimals(value, 2),
    },
    header: {
      label1: "",
      label2: "",
    },
  });

  const { node } = table;
  const grid = node.querySelector(".table-chart-grid");
  const totals = node.querySelector(".table-chart-totals");
  const rows = grid.querySelectorAll(".table-chart-row");
  const sums = node.querySelectorAll(".table-chart-summary-label");

  is(rows.length, 2, "There should be 1 table chart row and 1 header created.");

  is(
    rows[1].querySelectorAll(".table-chart-row-label")[0].getAttribute("name"),
    "size",
    "The first column of the first row exists."
  );
  is(
    rows[1].querySelectorAll(".table-chart-row-label")[1].getAttribute("name"),
    "label",
    "The second column of the first row exists."
  );
  is(
    rows[1].querySelectorAll(".table-chart-row-label")[0].textContent,
    "",
    "The first column of the first row displays the correct text."
  );
  is(
    rows[1].querySelectorAll(".table-chart-row-label")[1].textContent,
    L10N.getStr("tableChart.unavailable"),
    "The second column of the first row displays the correct text."
  );

  is(sums.length, 2, "There should be 2 total summaries created.");

  is(
    totals
      .querySelectorAll(".table-chart-summary-label")[0]
      .getAttribute("name"),
    "label1",
    "The first sum's type is correct."
  );
  is(
    totals.querySelectorAll(".table-chart-summary-label")[0].textContent,
    "Hello 0",
    "The first sum's value is correct."
  );

  is(
    totals
      .querySelectorAll(".table-chart-summary-label")[1]
      .getAttribute("name"),
    "label2",
    "The second sum's type is correct."
  );
  is(
    totals.querySelectorAll(".table-chart-summary-label")[1].textContent,
    "World 0",
    "The second sum's value is correct."
  );

  await teardown(monitor);
});
