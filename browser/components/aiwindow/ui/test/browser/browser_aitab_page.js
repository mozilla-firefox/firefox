/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const AITAB_PREF = "browser.smartwindow.aitab.enabled";
const PAGE_NAME = "hotels_san_francisco_1.html";
const PAGE_URL = `about:smartpage?page=${PAGE_NAME}`;

/* import-globals-from head_aitab.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_aitab.js",
  this
);

add_task(async function test_actor_registered_when_enabled() {
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });

  await BrowserTestUtils.withNewTab(PAGE_URL, browser => {
    Assert.ok(
      browser.browsingContext.currentWindowGlobal.getActor("AITab"),
      "AITab actor is registered"
    );
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_page_unavailable_when_disabled() {
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, false]] });

  Assert.throws(
    () =>
      Services.io.newChannelFromURI(
        Services.io.newURI(PAGE_URL),
        null,
        Services.scriptSecurityManager.getSystemPrincipal(),
        null,
        Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
        Ci.nsIContentPolicy.TYPE_DOCUMENT
      ),
    /NS_ERROR_NOT_AVAILABLE/,
    "about:smartpage cannot be loaded when the pref is off"
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_unknown_page_reports_unavailable() {
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });

  await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
    await SpecialPowers.spawn(browser, [PAGE_NAME], async pageName => {
      await content.customElements.whenDefined("aitab-page");
      const page = content.document.querySelector("aitab-page").wrappedJSObject;

      await ContentTaskUtils.waitForCondition(
        () => page.status != "loading",
        "The page finishes its lookup"
      );

      Assert.equal(
        page.pageName,
        pageName,
        "The page name comes from the page URL"
      );
      Assert.equal(
        page.status,
        "unavailable",
        "A name with no stored page renders the unavailable state"
      );
      const error = content.document
        .querySelector("aitab-page")
        .shadowRoot.querySelector("aitab-error");
      Assert.ok(error, "The error component is rendered");
      await error.wrappedJSObject.updateComplete;

      const reasons = error.shadowRoot.querySelectorAll(
        ".aitab-error-reasons li"
      );
      Assert.equal(reasons.length, 3, "Every possible reason is listed");
      await ContentTaskUtils.waitForCondition(
        () => [...reasons].every(reason => reason.querySelector("strong")),
        "Each reason is localized with a bold lead-in"
      );
      for (const reason of reasons) {
        Assert.ok(
          !reason.textContent.includes("{"),
          `Every term in ${reason.dataset.l10nId} resolves`
        );
      }
    });
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_path_like_page_name_rejected() {
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });

  const pathLikeName = "../../../etc/passwd";

  await BrowserTestUtils.withNewTab(
    `about:smartpage?page=${encodeURIComponent(pathLikeName)}`,
    async browser => {
      await SpecialPowers.spawn(browser, [pathLikeName], async name => {
        await content.customElements.whenDefined("aitab-page");
        const element = content.document.querySelector("aitab-page");
        const page = element.wrappedJSObject;

        await ContentTaskUtils.waitForCondition(
          () => page.status != "loading",
          "The page finishes its lookup"
        );

        Assert.equal(
          page.pageName,
          name,
          "Percent-encoding does not hide the path from the parent actor"
        );
        Assert.equal(
          page.status,
          "error",
          "A name shaped like a path is refused rather than looked up"
        );
        const error = element.shadowRoot.querySelector("aitab-error");
        Assert.ok(error, "The error message is rendered");
        await error.wrappedJSObject.updateComplete;
        Assert.ok(
          !error.shadowRoot.querySelector(".aitab-error-reasons"),
          "The reasons a page goes missing are not offered for a refused name"
        );
        Assert.ok(
          !element.shadowRoot.querySelector(".aitab-sheet"),
          "No page is rendered"
        );
      });
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_missing_page_reports_unavailable() {
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });

  await BrowserTestUtils.withNewTab("about:smartpage", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("aitab-page");
      const page = content.document.querySelector("aitab-page").wrappedJSObject;

      await ContentTaskUtils.waitForCondition(
        () => page.status != "loading",
        "The page finishes its lookup"
      );

      Assert.equal(page.pageName, null, "There is no page name in the URL");
      Assert.equal(
        page.status,
        "unavailable",
        "A URL with no page name renders the unavailable state"
      );

      Assert.ok(
        content.document
          .querySelector("aitab-page")
          .shadowRoot.querySelector("aitab-error"),
        "The error component is rendered for the unavailable state"
      );
    });
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_renders_page_config() {
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });

  await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
    await SpecialPowers.spawn(browser, [PAGE_CONFIG], async config => {
      await content.customElements.whenDefined("aitab-page");
      const element = content.document.querySelector("aitab-page");
      const page = element.wrappedJSObject;

      await ContentTaskUtils.waitForCondition(
        () => page.status != "loading",
        "The page finishes its lookup"
      );

      page.page = Cu.cloneInto(config, content);
      page.status = "ready";
      await page.updateComplete;

      const { shadowRoot } = element;
      const header = shadowRoot.querySelector("aitab-header");
      await header.updateComplete;
      Assert.equal(
        header.shadowRoot.querySelector(".aitab-title").textContent,
        config.children[0].title,
        "The header title is rendered"
      );
      Assert.equal(
        content.document.title,
        config.children[0].title,
        "The document title follows the header title so history shows it"
      );
      Assert.deepEqual(
        [...shadowRoot.querySelectorAll(".aitab-block")].map(
          block => block.dataset.blockType
        ),
        ["textblock", "timeline"],
        "Every typed block gets a placeholder that keeps its type, and a block with no type is skipped"
      );

      const footer = shadowRoot.querySelector(".aitab-sheet + aitab-footer");
      Assert.ok(footer, "The footer is rendered after the page content");
      await footer.wrappedJSObject.updateComplete;
      const brand = footer.shadowRoot.querySelector(".aitab-footer-brand");
      await ContentTaskUtils.waitForCondition(
        () => brand.textContent,
        "The footer credit is localized"
      );
      Assert.equal(
        brand.textContent,
        await content.document.l10n.formatValue("aitab-page-footer-brand"),
        "The footer credit is the Smart Window brand alone"
      );
    });
  });

  await SpecialPowers.popPrefEnv();
});

// Parent-actor branches that the states above do not reach: deleting
// something already gone, and a store that throws.

const { AITabStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AITabStore.sys.mjs"
);
const { ConversationStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ConversationStore.sys.mjs"
);
const { Conversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs"
);

/**
 * Reads the page the content document ended up with.
 *
 * @param {object} browser
 * @returns {Promise<object>} status, the resolved page title, if any, and
 *   whether the error component is rendered.
 */
function getPageState(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    await content.customElements.whenDefined("aitab-page");
    const element = content.document.querySelector("aitab-page");
    const page = element.wrappedJSObject;
    await ContentTaskUtils.waitForCondition(
      () => page.status != "loading",
      "The page finishes its lookup"
    );
    await page.updateComplete;
    return {
      status: page.status,
      title: page.page?.title ?? null,
      errorRendered: !!element.shadowRoot.querySelector("aitab-error"),
    };
  });
}

add_task(async function test_deleting_an_already_deleted_page_succeeds() {
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });

  await ConversationStore.updateConversation(
    new Conversation({ id: "conv-twice", feature: "aitab" })
  );
  await AITabStore.create({
    convId: "conv-twice",
    slug: "twice_page",
    title: "Twice",
  });

  // The tab has to be on the page being deleted: the parent reads the name
  // from the tab URL rather than from the message.
  await BrowserTestUtils.withNewTab(
    "about:smartpage?page=twice_page",
    async browser => {
      const actor =
        browser.browsingContext.currentWindowGlobal.getActor("AITab");

      const first = await actor.receiveMessage({ name: "AITab:DeletePage" });
      Assert.ok(first.success, "The first delete succeeds");

      // Deleting again is a no-op, not an error: the reader may retry, or
      // two tabs may be open on the same page.
      const second = await actor.receiveMessage({ name: "AITab:DeletePage" });
      Assert.ok(
        second.success,
        "Deleting a page that is already gone succeeds"
      );
      Assert.ok(!second.error, "and reports no error");
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_a_failing_store_surfaces_an_error() {
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });

  const original = AITabStore.getBySlug;
  AITabStore.getBySlug = () => {
    throw new Error("simulated store failure");
  };

  try {
    await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
      const state = await getPageState(browser);
      Assert.equal(
        state.status,
        "error",
        "A store that throws renders the error state, not an empty page"
      );
      Assert.ok(
        state.errorRendered,
        "The error component is rendered for the error state"
      );
    });
  } finally {
    AITabStore.getBySlug = original;
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_visit_recorded_in_history() {
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });
  await PlacesUtils.history.clear();

  const { slug } = await AITabStore.create({
    convId: "conv-history",
    slug: "history_page",
    title: "History",
  });
  const url = `about:smartpage?page=${slug}`;

  const visited = PlacesTestUtils.waitForNotification("page-visited", visits =>
    visits.some(visit => visit.url == url)
  );
  const titled = PlacesTestUtils.waitForNotification(
    "page-title-changed",
    events => events.some(event => event.url == url && event.title)
  );

  await BrowserTestUtils.withNewTab(url, async () => {
    await Promise.all([visited, titled]);
  });

  Assert.ok(
    await PlacesUtils.history.hasVisits(url),
    "Visiting a generated page records it in history"
  );
  const { title } = await PlacesUtils.history.fetch(url);
  Assert.ok(title, "The history entry has a title");

  await AITabStore.deleteBySlug(slug);
  await PlacesUtils.history.clear();
  await SpecialPowers.popPrefEnv();
});

function getStoredFavicon(url) {
  return PlacesUtils.favicons.getFaviconForPage(Services.io.newURI(url));
}

add_task(async function test_favicon_stored_and_removed_with_page() {
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });
  await PlacesUtils.history.clear();

  await ConversationStore.updateConversation(
    new Conversation({ id: "conv-favicon", feature: "aitab" })
  );
  const { slug } = await AITabStore.create({
    convId: "conv-favicon",
    slug: "favicon_page",
    title: "Favicon",
    components: { surface: { components: [], dataModel: {} } },
  });
  const url = `about:smartpage?page=${slug}`;

  const faviconChanged = PlacesTestUtils.waitForNotification(
    "favicon-changed",
    events => events.some(event => event.url == url)
  );

  await BrowserTestUtils.withNewTab(url, async browser => {
    await faviconChanged;
    const favicon = await getStoredFavicon(url);
    Assert.equal(
      favicon.uri.spec,
      "chrome://browser/content/aiwindow/assets/smart-page-16.svg",
      "The stored icon is the smart page icon"
    );

    const actor = browser.browsingContext.currentWindowGlobal.getActor("AITab");
    await actor.receiveMessage({ name: "AITab:DeletePage" });
    await TestUtils.waitForCondition(
      async () => !(await getStoredFavicon(url)),
      "Deleting the page removes its icon"
    );
  });

  await PlacesUtils.history.clear();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_missing_page_removed_from_history() {
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });
  await PlacesUtils.history.clear();

  const otherPageURL = "about:smartpage?page=some_other_page";
  await PlacesTestUtils.addVisits([otherPageURL, "https://example.com/"]);

  const visited = PlacesTestUtils.waitForNotification("page-visited", visits =>
    visits.some(visit => visit.url == PAGE_URL)
  );

  await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
    await visited;
    const state = await getPageState(browser);
    Assert.equal(state.status, "unavailable", "The page has nothing stored");

    await TestUtils.waitForCondition(
      async () => !(await PlacesUtils.history.hasVisits(PAGE_URL)),
      "The visit to the missing page is removed from history"
    );
  });

  Assert.ok(
    await PlacesUtils.history.hasVisits(otherPageURL),
    "History for a different generated page is kept"
  );
  Assert.ok(
    await PlacesUtils.history.hasVisits("https://example.com/"),
    "Unrelated history is kept"
  );

  await PlacesUtils.history.clear();
  await SpecialPowers.popPrefEnv();
});
