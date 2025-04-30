/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(2);

const MULTIPAGE_IFRAME_URL = HAR_EXAMPLE_URL + "html_har_multipage_iframe.html";
const MULTIPAGE_PAGE_URL = HAR_EXAMPLE_URL + "html_har_multipage_page.html";

/**
 * Tests HAR export with navigations and multipage support
 */
add_task(async function () {
  await testHARWithNavigation({ enableMultipage: false, filter: false });
  await testHARWithNavigation({ enableMultipage: true, filter: false });
  await testHARWithNavigation({ enableMultipage: false, filter: true });
  await testHARWithNavigation({ enableMultipage: true, filter: true });
});

async function testHARWithNavigation({ enableMultipage, filter }) {
  await pushPref("devtools.netmonitor.persistlog", true);
  await pushPref("devtools.netmonitor.har.multiple-pages", enableMultipage);

  const { tab, monitor } = await initNetMonitor(MULTIPAGE_PAGE_URL + "?page1", {
    requestCount: 1,
  });

  info("Starting test... ");

  const { store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  info("Perform 3 additional requests");
  let onNetworkEvents = waitForNetworkEvents(monitor, 3);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    content.wrappedJSObject.sendRequests(3);
  });
  await onNetworkEvents;

  info("Navigate to a second page where we will not perform any extra request");
  onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await navigateTo(MULTIPAGE_PAGE_URL + "?page2");
  await onNetworkEvents;

  info("Navigate to a third page where we will not perform any extra request");
  onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await navigateTo(MULTIPAGE_PAGE_URL + "?page3");
  await onNetworkEvents;

  info("Perform 2 additional requests");
  onNetworkEvents = waitForNetworkEvents(monitor, 2);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    content.wrappedJSObject.sendRequests(2);
  });
  await onNetworkEvents;

  info("Create an iframe which will perform 2 additional requests");
  onNetworkEvents = waitForNetworkEvents(monitor, 2);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [MULTIPAGE_IFRAME_URL],
    async function (url) {
      const iframe = content.document.createElement("iframe");
      const onLoad = new Promise(resolve =>
        iframe.addEventListener("load", resolve, { once: true })
      );
      content.content.document.body.appendChild(iframe);
      iframe.setAttribute("src", url);
      await onLoad;
    }
  );
  await onNetworkEvents;

  if (filter) {
    info("Start filtering requests");
    store.dispatch(Actions.setRequestFilterText("?request"));
  }

  info("Trigger Copy All As HAR from the context menu");
  const har = await copyAllAsHARWithContextMenu(monitor);

  // Check out the HAR log.
  isnot(har.log, null, "The HAR log must exist");

  if (enableMultipage) {
    is(har.log.pages.length, 3, "There must be three pages");
  } else {
    is(har.log.pages.length, 1, "There must be one page");
  }

  if (!filter) {
    // Expect 9 requests:
    // - 3 requests performed with sendRequests on the first page
    // - 1 navigation request to the second page
    // - 1 navigation request to the third page
    // - 2 requests performed with sendRequests on the third page
    // - 1 request to load an iframe on the third page
    // - 1 request from the iframe on the third page
    is(har.log.entries.length, 9, "There must be 9 requests");
  } else {
    // Same but we only expect the fetch requests
    is(har.log.entries.length, 6, "There must be 6 requests");
  }

  if (enableMultipage) {
    // With multipage enabled, check that the page entries are valid and that
    // requests are referencing the expected page id.
    assertPageDetails(
      har.log.pages[0],
      "page_0",
      `${MULTIPAGE_PAGE_URL}?page1`
    );
    assertPageRequests(har.log.entries, 0, 2, har.log.pages[0].id);

    assertPageDetails(
      har.log.pages[1],
      "page_1",
      `${MULTIPAGE_PAGE_URL}?page2`
    );
    if (filter) {
      // When filtering, we don't expect any request to match page_1
    } else {
      assertPageRequests(har.log.entries, 3, 3, har.log.pages[1].id);
    }

    assertPageDetails(
      har.log.pages[2],
      "page_2",
      `${MULTIPAGE_PAGE_URL}?page3`
    );
    if (filter) {
      assertPageRequests(har.log.entries, 3, 5, har.log.pages[2].id);
    } else {
      assertPageRequests(har.log.entries, 4, 8, har.log.pages[2].id);
    }
  } else {
    is(har.log.pages[0].id, "page_1");
    // Without multipage, all requests are associated with the only page entry.
    for (const entry of har.log.entries) {
      is(entry.pageref, "page_1");
    }
  }

  // Clean up
  store.dispatch(Actions.setRequestFilterText(""));
  return teardown(monitor);
}

function assertPageDetails(page, expectedId, expectedTitle) {
  is(page.id, expectedId, "Page has the expected id");
  is(page.title, expectedTitle, "Page has the expected title");
}

function assertPageRequests(entries, startIndex, endIndex, expectedPageId) {
  for (let i = startIndex; i < endIndex + 1; i++) {
    const entry = entries[i];
    is(
      entry.pageref,
      expectedPageId,
      `Entry ${i} is attached to page id: ${expectedPageId}`
    );
  }
}
