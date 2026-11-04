/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests whether keys in Params panel are sorted.
 */
add_task(async function () {
  const { tab, monitor } = await initNetMonitor(POST_ARRAY_DATA_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  // Execute requests.
  await performRequests(monitor, tab, 1);

  const onDomUpdated = waitForDOM(document, ".headers-overview");
  clickOnRequestRow(document.querySelectorAll(".request-list-item")[0]);
  await onDomUpdated;

  clickOnSidebarTab(document, "request");

  // The Params panel should render the following
  // POSTed JSON data structure:
  //
  // ▼ JSON
  //   ▼ watches: […]
  //       0: hello
  //       1: how
  //       2: are
  //       3: you
  //     ▼ 4: {…}
  //         a: 10
  //       ▼ b: […]
  //           0: "a"
  //           1: "c"
  //           2: "b"
  //         c: 15
  const expectedKeys = [
    "watches\t(5)[…]",
    `0\t"hello"`,
    `1\t"how"`,
    `2\t"are"`,
    `3\t"you"`,
    "4\t{…}",
    "a\t10",
    "b\t(3)[…]",
    `0\t"a"`,
    `1\t"c"`,
    `2\t"b"`,
    "c\t15",
  ];

  const waitForTreeRow = waitForDOM(
    document,
    ".treeTable .treeRow",
    expectedKeys.length
  );
  await waitForTreeRow;
  const actualKeys = document.querySelectorAll(".treeTable .treeRow");

  for (let i = 0; i < actualKeys.length; i++) {
    const text = actualKeys[i].innerText.trim();
    is(
      text,
      expectedKeys[i],
      "Actual value " +
        text +
        " is equal to the " +
        "expected value " +
        expectedKeys[i]
    );
  }
});
