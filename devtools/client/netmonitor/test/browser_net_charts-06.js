/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Makes sure Pie Charts correctly handle empty source data.
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

  const pie = Chart.Pie(document, {
    data: [],
    width: 100,
    height: 100,
  });

  const { node } = pie;
  const slices = node.querySelectorAll(".pie-chart-slice");
  const labels = node.querySelectorAll(".pie-chart-label");

  is(slices.length, 1, "There should be 1 pie chart slice created.");
  ok(
    slices[0]
      .getAttribute("d")
      .match(
        /\s*M 50,50 L 50\.\d+,2\.5\d* A 47\.5,47\.5 0 1 1 49\.\d+,2\.5\d* Z/
      ),
    "The slice has the correct data."
  );

  ok(slices[0].hasAttribute("largest"), "The slice should be the largest one.");
  ok(
    slices[0].hasAttribute("smallest"),
    "The slice should also be the smallest one."
  );
  is(
    slices[0].getAttribute("data-statistic-name"),
    L10N.getStr("pieChart.unavailable"),
    "The slice's name is correct."
  );

  is(labels.length, 1, "There should be 1 pie chart label created.");
  is(labels[0].textContent, "Empty", "The label's text is correct.");

  await teardown(monitor);
});
