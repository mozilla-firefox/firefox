/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Makes sure Table Charts have the right internal structure.
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
    title: "Table title",
    data: [
      {
        label1: 1,
        label2: 11.1,
      },
      {
        label1: 2,
        label2: 12.2,
      },
      {
        label1: 3,
        label2: 13.3,
      },
    ],
    strings: {
      label2: (value, index) => value + ["foo", "bar", "baz"][index],
    },
    totals: {
      label1: value => "Hello " + L10N.numberWithDecimals(value, 2),
      label2: value => "World " + L10N.numberWithDecimals(value, 2),
    },
    header: {
      label1: "label1header",
      label2: "label2header",
    },
  });

  const { node } = table;
  const title = node.querySelector(".table-chart-title");
  const grid = node.querySelector(".table-chart-grid");
  const totals = node.querySelector(".table-chart-totals");
  const rows = grid.querySelectorAll(".table-chart-row");
  const sums = node.querySelectorAll(".table-chart-summary-label");

  ok(
    node.classList.contains("table-chart-container") &&
      node.classList.contains("generic-chart-container"),
    "A table chart container was created successfully."
  );

  ok(title, "A title node was created successfully.");
  is(
    title.textContent,
    "Table title",
    "The title node displays the correct text."
  );

  is(
    rows.length,
    4,
    "There should be 3 table chart rows and a header created."
  );

  is(
    rows[0].querySelectorAll(".table-chart-row-label")[0].getAttribute("name"),
    "label1",
    "The first column of the header exists."
  );
  is(
    rows[0].querySelectorAll(".table-chart-row-label")[1].getAttribute("name"),
    "label2",
    "The second column of the header exists."
  );
  is(
    rows[0].querySelectorAll(".table-chart-row-label")[0].textContent,
    "label1header",
    "The first column of the header displays the correct text."
  );
  is(
    rows[0].querySelectorAll(".table-chart-row-label")[1].textContent,
    "label2header",
    "The second column of the header displays the correct text."
  );

  is(
    rows[1].querySelectorAll(".table-chart-row-label")[0].getAttribute("name"),
    "label1",
    "The first column of the first row exists."
  );
  is(
    rows[1].querySelectorAll(".table-chart-row-label")[1].getAttribute("name"),
    "label2",
    "The second column of the first row exists."
  );
  is(
    rows[1].querySelectorAll(".table-chart-row-label")[0].textContent,
    "1",
    "The first column of the first row displays the correct text."
  );
  is(
    rows[1].querySelectorAll(".table-chart-row-label")[1].textContent,
    "11.1foo",
    "The second column of the first row displays the correct text."
  );

  is(
    rows[2].querySelectorAll(".table-chart-row-label")[0].getAttribute("name"),
    "label1",
    "The first column of the second row exists."
  );
  is(
    rows[2].querySelectorAll(".table-chart-row-label")[1].getAttribute("name"),
    "label2",
    "The second column of the second row exists."
  );
  is(
    rows[2].querySelectorAll(".table-chart-row-label")[0].textContent,
    "2",
    "The first column of the second row displays the correct text."
  );
  is(
    rows[2].querySelectorAll(".table-chart-row-label")[1].textContent,
    "12.2bar",
    "The second column of the first row displays the correct text."
  );

  is(
    rows[3].querySelectorAll(".table-chart-row-label")[0].getAttribute("name"),
    "label1",
    "The first column of the third row exists."
  );
  is(
    rows[3].querySelectorAll(".table-chart-row-label")[1].getAttribute("name"),
    "label2",
    "The second column of the third row exists."
  );
  is(
    rows[3].querySelectorAll(".table-chart-row-label")[0].textContent,
    "3",
    "The first column of the third row displays the correct text."
  );
  is(
    rows[3].querySelectorAll(".table-chart-row-label")[1].textContent,
    "13.3baz",
    "The second column of the third row displays the correct text."
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
    "Hello 6",
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
    "World 36.60",
    "The second sum's value is correct."
  );

  await teardown(monitor);
});
