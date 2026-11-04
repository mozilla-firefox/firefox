/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test if the 'Same site' cookie attribute is correctly set in the cookie panel
 */
add_task(async function () {
  const { monitor } = await initNetMonitor(SET_COOKIE_SAME_SITE_SJS, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  const onDomUpdated = waitForDOM(document, ".headers-overview");
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  await onDomUpdated;

  clickOnSidebarTab(document, "cookies");

  info("Checking the SameSite property");
  const expectedValues = [
    {
      key: "foo",
      value: "",
    },
    {
      key: "samesite",
      value: '"Lax"',
    },
    {
      key: "value",
      value: '"bar"',
    },
    {
      key: "foo",
      value: '"bar"',
    },
  ];
  const labelCells = document.querySelectorAll(".treeLabelCell");
  const valueCells = document.querySelectorAll(".treeValueCell");
  is(
    valueCells.length,
    labelCells.length,
    "Number of labels " +
      labelCells.length +
      " different from number of values " +
      valueCells.length
  );

  // Go through the cookie properties and check if each one has the expected
  // label and value
  for (let index = 0; index < labelCells.length; ++index) {
    is(
      labelCells[index].innerText,
      expectedValues[index].key,
      "Actual label " +
        labelCells[index].innerText +
        " not equal to expected label " +
        expectedValues[index].key
    );
    is(
      valueCells[index].innerText,
      expectedValues[index].value,
      "Actual value " +
        valueCells[index].innerText +
        " not equal to expected value " +
        expectedValues[index].value
    );
  }

  await teardown(monitor);
});
