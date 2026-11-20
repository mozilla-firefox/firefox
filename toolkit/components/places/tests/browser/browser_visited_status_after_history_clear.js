/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const LINK_URL = "https://example.com/";
const SENTINEL_URL = "https://example.org/";
const TEST_PAGE = `data:text/html,<a href="${LINK_URL}">link</a>`;

add_task(async function links_are_unvisited_after_history_clear() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_PAGE },
    async browser => {
      await PlacesTestUtils.addVisits(LINK_URL);
      await assertLinkVisitedStatus(browser, LINK_URL, {
        visitCount: 1,
        isVisited: true,
      });

      await PlacesUtils.history.clear();
      await assertLinkVisitedStatus(browser, LINK_URL, {
        visitCount: 0,
        isVisited: false,
      });

      info("A link added after the clear must not reuse the old status");
      await PlacesTestUtils.addVisits(SENTINEL_URL);
      let isVisited = await SpecialPowers.spawn(
        browser,
        [LINK_URL, SENTINEL_URL],
        async (href, sentinelHref) => {
          // ElementState::VISITED
          const VISITED_STATE = 1 << 18;
          let visited = el =>
            !!(content.InspectorUtils.getContentState(el) & VISITED_STATE);
          // The old link stays, so the new one reuses the status cached for
          // its URI when it registers. Links register together in an idle
          // task: once the sentinel's own query says visited, the new link's
          // state is final.
          let link = content.document.createElement("a");
          link.href = href;
          let sentinel = content.document.createElement("a");
          sentinel.href = sentinelHref;
          content.document.body.append(link, sentinel);
          await ContentTaskUtils.waitForCondition(() => visited(sentinel));
          return visited(link);
        }
      );
      Assert.ok(!isVisited, "The new link is unvisited");
    }
  );

  await PlacesUtils.history.clear();
});

add_task(async function reload_after_history_clear_is_stored() {
  const pageURL =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content/",
      "https://example.com/"
    ) + "empty_page.html";
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: pageURL },
    async browser => {
      async function reload() {
        let loaded = BrowserTestUtils.browserLoaded(browser);
        browser.reload();
        await loaded;
        await PlacesTestUtils.promiseAsyncUpdates();
      }

      await reload();
      Assert.equal(
        await PlacesTestUtils.visitsInDB(pageURL),
        1,
        "A reload of a recently visited page is not stored"
      );

      await PlacesUtils.history.clear();
      await reload();
      Assert.equal(
        await PlacesTestUtils.visitsInDB(pageURL),
        1,
        "A reload after clearing history is stored"
      );
    }
  );
});
