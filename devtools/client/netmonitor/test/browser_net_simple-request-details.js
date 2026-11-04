/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if requests render correct information in the details UI.
 */

add_task(async function () {
  const {
    L10N,
  } = require("resource://devtools/client/netmonitor/src/utils/l10n.js");

  const { monitor } = await initNetMonitor(SIMPLE_SJS, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { PANELS } = windowRequire("devtools/client/netmonitor/src/constants");
  const { getSelectedRequest, getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(Actions.batchEnable(false));

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  is(
    getSelectedRequest(store.getState()),
    undefined,
    "There shouldn't be any selected item in the requests menu."
  );
  is(
    store.getState().requests.requests.length,
    1,
    "The requests menu should not be empty after the first request."
  );
  is(
    !!document.querySelector(".network-details-bar"),
    false,
    "The network details panel should still be hidden after first request."
  );

  const waitForHeaders = waitForDOM(document, ".headers-overview");

  store.dispatch(Actions.toggleNetworkDetails());

  isnot(
    getSelectedRequest(store.getState()),
    undefined,
    "There should be a selected item in the requests menu."
  );
  is(
    getSelectedIndex(store.getState()),
    0,
    "The first item should be selected in the requests menu."
  );
  is(
    !!document.querySelector(".network-details-bar"),
    true,
    "The network details panel should not be hidden after toggle button was pressed."
  );

  await waitForHeaders;

  await testHeadersTab();
  await testCookiesTab();
  await testParamsTab();
  await testResponseTab();
  await testTimingsTab();
  await closePanelOnEsc();
  return teardown(monitor);

  function getSelectedIndex(state) {
    if (!state.requests.selectedId) {
      return -1;
    }
    return getSortedRequests(state).findIndex(
      r => r.id === state.requests.selectedId
    );
  }

  async function testHeadersTab() {
    const tabEl = document.querySelectorAll(
      ".network-details-bar .tabs-menu a"
    )[0];
    const tabpanel = document.querySelector("#headers-panel");

    is(
      tabEl.getAttribute("aria-selected"),
      "true",
      "The headers tab in the network details pane should be selected."
    );
    // Request URL
    is(
      tabpanel.querySelector(".url-preview .url").innerText,
      SIMPLE_SJS,
      "The url summary value is incorrect."
    );

    // Request method
    is(
      tabpanel.querySelectorAll(".treeLabel")[0].innerText,
      "GET",
      "The method summary value is incorrect."
    );
    // Status code
    is(
      tabpanel.querySelector(".requests-list-status-code").innerText,
      "200",
      "The status summary code is incorrect."
    );
    is(
      tabpanel.querySelector(".status").childNodes[1].textContent,
      "Och Aye",
      "The status summary value is incorrect."
    );
    // Version
    is(
      tabpanel.querySelectorAll(".tabpanel-summary-value")[1].innerText,
      "HTTP/1.1",
      "The HTTP version is incorrect."
    );

    await waitForRequestData(store, ["requestHeaders", "responseHeaders"]);

    is(
      tabpanel.querySelectorAll(".accordion-item").length,
      2,
      "There should be 2 header scopes displayed in this tabpanel."
    );

    is(
      tabpanel.querySelectorAll(".accordion .treeLabelCell").length,
      25,
      "There should be 25 header values displayed in this tabpanel."
    );

    const headersTable = tabpanel.querySelector(".accordion");
    const responseScope = headersTable.querySelectorAll(
      "tr[id^='/responseHeaders']"
    );
    const requestScope = headersTable.querySelectorAll(
      "tr[id^='/requestHeaders']"
    );

    const headerLabels = headersTable.querySelectorAll(
      ".accordion-item .accordion-header-label"
    );

    ok(
      headerLabels[0].innerHTML.match(
        new RegExp(L10N.getStr("responseHeaders") + " \\([0-9]+ .+\\)")
      ),
      "The response headers scope doesn't have the correct title."
    );

    ok(
      headerLabels[1].innerHTML.includes(L10N.getStr("requestHeaders") + " ("),
      "The request headers scope doesn't have the correct title."
    );

    const responseHeaders = [
      {
        name: "cache-control",
        value: "no-cache, no-store, must-revalidate",
        pos: "first",
        index: 1,
      },
      {
        name: "connection",
        value: "close",
        pos: "second",
        index: 2,
      },
      {
        name: "content-length",
        value: "12",
        pos: "third",
        index: 3,
      },
      {
        name: "content-type",
        value: "text/plain; charset=utf-8",
        pos: "fourth",
        index: 4,
      },
      {
        name: "foo-bar",
        value: "baz",
        pos: "seventh",
        index: 7,
      },
    ];
    responseHeaders.forEach(header => {
      is(
        responseScope[header.index - 1].querySelector(".treeLabel").innerHTML,
        header.name,
        `The ${header.pos} response header name was incorrect.`
      );
      is(
        responseScope[header.index - 1].querySelector(".objectBox").innerHTML,
        `${header.value}`,
        `The ${header.pos} response header value was incorrect.`
      );
    });

    const requestHeaders = [
      {
        name: "Cache-Control",
        value: "no-cache",
        pos: "fourth",
        index: 4,
      },
      {
        name: "Connection",
        value: "keep-alive",
        pos: "fifth",
        index: 5,
      },
      {
        name: "Host",
        value: "example.com",
        pos: "seventh",
        index: 7,
      },
      {
        name: "Pragma",
        value: "no-cache",
        pos: "eighth",
        index: 8,
      },
    ];
    requestHeaders.forEach(header => {
      is(
        requestScope[header.index - 1].querySelector(".treeLabel").innerHTML,
        header.name,
        `The ${header.pos} request header name was incorrect.`
      );
      is(
        requestScope[header.index - 1].querySelector(".objectBox").innerHTML,
        `${header.value}`,
        `The ${header.pos} request header value was incorrect.`
      );
    });
  }

  async function testCookiesTab() {
    const tabpanel = await selectTab(PANELS.COOKIES, 1);

    const cookieAccordion = tabpanel.querySelector(".accordion");

    is(
      cookieAccordion.querySelectorAll(".accordion-item").length,
      2,
      "There should be 2 cookie scopes displayed in this tabpanel."
    );
    // 2 Cookies in response - 1 httpOnly and 1 value for each cookie - total 6

    const resCookiesTable = cookieAccordion.querySelector(
      "li[id='responseCookies'] .accordion-content .treeTable"
    );
    is(
      resCookiesTable.querySelectorAll("tr.treeRow").length,
      6,
      "There should be 6 rows displayed in response cookies table"
    );

    const reqCookiesTable = cookieAccordion.querySelector(
      "li[id='requestCookies'] .accordion-content .treeTable"
    );
    is(
      reqCookiesTable.querySelectorAll("tr.treeRow").length,
      2,
      "There should be 2 cookie values displayed in request cookies table."
    );
  }

  async function testParamsTab() {
    const tabpanel = await selectTab(PANELS.REQUEST, 2);

    is(
      tabpanel.querySelectorAll(".panel-container").length,
      0,
      "There should be no param scopes displayed in this tabpanel."
    );
    is(
      tabpanel.querySelectorAll(".empty-notice").length,
      1,
      "The empty notice should be displayed in this tabpanel."
    );
  }

  async function testResponseTab() {
    const tabpanel = await selectTab(PANELS.RESPONSE, 3);
    await waitForDOM(document, "#response-panel .source-editor-mount");

    is(
      tabpanel.querySelectorAll(
        "#response-panel .raw-data-toggle-input .devtools-checkbox-toggle"
      ).length,
      0,
      "The raw data toggle should not be shown in this tabpanel."
    );
    is(
      tabpanel.querySelectorAll(".source-editor-mount").length,
      1,
      "The response payload should be shown initially."
    );
  }

  async function testTimingsTab() {
    const tabpanel = await selectTab(PANELS.TIMINGS, 4);

    const displayFormat = new RegExp(/[0-9]+ ms$/);
    const propsToVerify = [
      "blocked",
      "dns",
      "connect",
      "ssl",
      "send",
      "wait",
      "receive",
    ];

    // To ensure that test case for a new property is written, otherwise this
    // test will fail
    is(
      tabpanel.querySelectorAll(".tabpanel-summary-container").length,
      propsToVerify.length,
      `There should be exactly ${propsToVerify.length} values
      displayed in this tabpanel`
    );

    propsToVerify.forEach(propName => {
      ok(
        tabpanel
          .querySelector(
            `#timings-summary-${propName}
      .requests-list-timings-total`
          )
          .innerHTML.match(displayFormat),
        `The ${propName} timing info does not appear to be correct.`
      );
    });
  }

  async function selectTab(tabName, pos) {
    const tabEl = document.querySelectorAll(
      ".network-details-bar .tabs-menu a"
    )[pos];

    const onPanelOpen = waitForDOM(document, `#${tabName}-panel`);
    clickOnSidebarTab(
      document,
      tabEl.id.substring(0, tabEl.id.indexOf("-tab"))
    );
    await onPanelOpen;

    is(
      tabEl.getAttribute("aria-selected"),
      "true",
      `The ${tabName} tab in the network details pane should be selected.`
    );

    return document.querySelector(".network-details-bar .tab-panel");
  }

  // This test will timeout on failure
  async function closePanelOnEsc() {
    EventUtils.sendKey("ESCAPE", window);

    await waitUntil(() => {
      return document.querySelector(".network-details-bar") == null;
    });

    is(
      document.querySelectorAll(".network-details-bar").length,
      0,
      "Network details panel should close on ESC key"
    );
  }
});
