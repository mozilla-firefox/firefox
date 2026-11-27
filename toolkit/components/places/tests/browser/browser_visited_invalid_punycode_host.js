/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Coloring a visited link must not crash the tab, even when the link's host
// is ASCII but its punycode fails UTS46 validation.
const LINK_URL = "http://xn--ab-j1t/";
const TEST_PAGE = `data:text/html,<a href="${LINK_URL}">link</a>`;

add_task(async function visited_link_with_invalid_punycode_host() {
  await PlacesTestUtils.addVisits(LINK_URL);
  registerCleanupFunction(PlacesUtils.history.clear);

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_PAGE },
    async browser => {
      await assertLinkVisitedStatus(browser, LINK_URL, {
        visitCount: 1,
        isVisited: true,
      });
    }
  );
});
