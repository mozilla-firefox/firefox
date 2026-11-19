/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_aitab.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_aitab.js",
  this
);

const AITAB_PREF = "browser.smartwindow.aitab.enabled";
const UNKNOWN_SLUG = "unknown-slug";
const STORED_SLUG = "stored-slug";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AITAB_PAGE_TYPES:
    "moz-src:///browser/components/aiwindow/ui/modules/AITabPageTypes.sys.mjs",
  AITabStore:
    "moz-src:///browser/components/aiwindow/ui/modules/AITabStore.sys.mjs",
});

describe("about:smartpage store integration", () => {
  let tab;

  beforeEach(async () => {
    tab = null;
    await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });
    await lazy.AITabStore.destroyDatabase();
  });

  afterEach(async () => {
    if (tab) {
      await BrowserTestUtils.removeTab(tab);
      tab = null;
    }
    await lazy.AITabStore.destroyDatabase();
    await SpecialPowers.popPrefEnv();
  });

  it("shows the unavailable state for an unknown slug", async () => {
    tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      `about:smartpage?page=${UNKNOWN_SLUG}`
    );

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.customElements.whenDefined("aitab-page");
      const element = content.document.querySelector("aitab-page");
      const page = element.wrappedJSObject;

      await ContentTaskUtils.waitForCondition(
        () => page.status != "loading",
        "The page finishes its store lookup"
      );
      await page.updateComplete;

      Assert.equal(
        page.status,
        "unavailable",
        "An unknown slug produces the unavailable error state"
      );
      Assert.equal(page.page, null, "No page data is returned");
      Assert.ok(
        element.shadowRoot.querySelector("aitab-error"),
        "The error component is rendered"
      );
    });
  });

  describe("refreshing a generated page", () => {
    let context;
    let destination;

    beforeEach(async () => {
      destination = null;
      context = {};
      setupAITabGenerationMocks(context);
      await openAITabGenerationChat(context);
    });

    afterEach(async () => {
      if (destination && !destination.closed) {
        await BrowserTestUtils.closeWindow(destination);
      }
      await cleanupAITabGeneration(context);
    });

    it("renders a revision when the user returns to the page tab", async () => {
      const originalTitle = "Lisbon hotels";
      const updatedTitle = "Lisbon hotels for families";
      const viewerURL = await createTestAITab(context, originalTitle);
      const pageTab = await openGeneratedAITab(context, viewerURL);
      await waitForAITabHeading(pageTab, originalTitle);
      const windowId =
        pageTab.linkedBrowser.browsingContext.currentWindowGlobal.innerWindowId;

      await selectAITabTestTab(context.chatTab);
      await reviseTestAITab(context, viewerURL, updatedTitle);

      await selectAITabTestTab(pageTab);
      await waitForAITabHeading(pageTab, updatedTitle);

      Assert.equal(
        pageTab.linkedBrowser.browsingContext.currentWindowGlobal.innerWindowId,
        windowId,
        "The page document is unchanged after rendering the revision"
      );
    });

    describe("when the page is moved to a different window", () => {
      const originalTitle = "Lisbon hotels";
      const updatedTitle = "Lisbon hotels for families";
      let pageTab;
      let otherTab;
      let windowId;

      async function focusWindow(win) {
        await SimpleTest.promiseFocus(win);
        await TestUtils.waitForCondition(
          () => Services.focus.activeWindow === win,
          "The requested window becomes active"
        );
      }

      beforeEach(async () => {
        context.viewerURL = await createTestAITab(context, originalTitle);
        const originalTab = await openGeneratedAITab(
          context,
          context.viewerURL
        );
        await waitForAITabHeading(originalTab, originalTitle);
        windowId =
          originalTab.linkedBrowser.browsingContext.currentWindowGlobal
            .innerWindowId;

        destination = await openAIWindow();
        otherTab = destination.gBrowser.selectedTab;
        pageTab = destination.gBrowser.adoptTab(originalTab, {
          tabIndex: destination.gBrowser.tabs.length,
          selectTab: true,
        });
        Assert.ok(pageTab, "The destination window adopts the page tab");
        await waitForAITabHeading(pageTab, originalTitle);
      });

      it("refreshes the selected page when its window becomes active", async () => {
        await focusWindow(context.win);
        await reviseTestAITab(context, context.viewerURL, updatedTitle);
        Assert.equal(
          destination.gBrowser.selectedTab,
          pageTab,
          "The page remains selected while its window is inactive"
        );

        await focusWindow(destination);
        await waitForAITabHeading(pageTab, updatedTitle);

        Assert.equal(
          pageTab.linkedBrowser.browsingContext.currentWindowGlobal
            .innerWindowId,
          windowId,
          "Moving and refreshing the page preserves its document"
        );
      });

      it("refreshes when the moved page tab is selected again", async () => {
        await selectAITabTestTab(otherTab);
        await focusWindow(context.win);
        await reviseTestAITab(context, context.viewerURL, updatedTitle);

        await focusWindow(destination);
        Assert.equal(
          destination.gBrowser.selectedTab,
          otherTab,
          "The page is still in a background tab after window activation"
        );
        await selectAITabTestTab(pageTab);
        await waitForAITabHeading(pageTab, updatedTitle);

        Assert.equal(
          pageTab.linkedBrowser.browsingContext.currentWindowGlobal
            .innerWindowId,
          windowId,
          "Moving and refreshing the page preserves its document"
        );
      });
    });
  });

  describe("with a stored page", () => {
    let storedPage;

    beforeEach(async () => {
      storedPage = await lazy.AITabStore.create({
        convId: "stored-conversation",
        slug: STORED_SLUG,
        title: "Stored AI Tab",
        components: {
          surface: {
            components: [],
            dataModel: {},
          },
        },
      });
    });

    it("loads the stored page through the actor pair", async () => {
      tab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        `about:smartpage?page=${STORED_SLUG}`
      );

      await SpecialPowers.spawn(
        tab.linkedBrowser,
        [storedPage],
        async expectedPage => {
          await content.customElements.whenDefined("aitab-page");
          const element = content.document.querySelector("aitab-page");
          const pageComponent = element.wrappedJSObject;

          await ContentTaskUtils.waitForCondition(
            () => pageComponent.status != "loading",
            "The page finishes its store lookup"
          );
          await pageComponent.updateComplete;

          Assert.equal(
            pageComponent.status,
            "ready",
            "The stored page reaches the ready state"
          );
          Assert.equal(
            pageComponent.page.slug,
            expectedPage.slug,
            "The requested page is returned"
          );
          Assert.equal(
            pageComponent.page.title,
            expectedPage.title,
            "The stored title is returned"
          );
          Assert.deepEqual(
            pageComponent.page.components,
            expectedPage.components,
            "The stored components are returned"
          );
          Assert.ok(
            element.shadowRoot.querySelector(".aitab-sheet"),
            "The page content is rendered"
          );
        }
      );
    });
  });

  describe("page actions by page type", () => {
    /**
     * Opens a stored page of `pageType` and reports which page actions its
     * header offers.
     *
     * @param {number} pageType
     * @returns {Promise<{editable: boolean, refresh: boolean, delete: boolean}>}
     */
    async function openAndReadActions(pageType) {
      await lazy.AITabStore.create({
        convId: "page-type-conversation",
        slug: STORED_SLUG,
        title: "Typed AI Tab",
        pageType,
        components: { surface: makeAITabSurface("Typed AI Tab") },
      });

      tab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        `about:smartpage?page=${STORED_SLUG}`
      );

      return SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
        await content.customElements.whenDefined("aitab-page");
        const page = content.document.querySelector("aitab-page");
        await ContentTaskUtils.waitForCondition(
          () => page.wrappedJSObject.status == "ready",
          "The stored page renders"
        );
        await page.updateComplete;

        const header = page.shadowRoot.querySelector("aitab-header");
        await header.updateComplete;
        const actions = header.shadowRoot.querySelector("aitab-page-actions");
        await actions.updateComplete;

        return {
          editable: page.wrappedJSObject.page.editable,
          refresh: !!actions.shadowRoot.querySelector(".aitab-action-refresh"),
          delete: !!actions.shadowRoot.querySelector(".aitab-action-delete"),
        };
      });
    }

    it("offers refresh on a chat page", async () => {
      const actions = await openAndReadActions(lazy.AITAB_PAGE_TYPES.CHAT);

      Assert.ok(actions.editable, "A chat page is sent as editable");
      Assert.ok(actions.refresh, "Refresh is offered");
      Assert.ok(actions.delete, "Delete is offered");
    });

    it("does not offer refresh on a digest page", async () => {
      const actions = await openAndReadActions(lazy.AITAB_PAGE_TYPES.DIGEST);

      Assert.ok(!actions.editable, "A digest page is sent as not editable");
      Assert.ok(
        !actions.refresh,
        "Refresh, which rewrites the page, is hidden"
      );
      Assert.ok(actions.delete, "Delete is still offered");
    });

    it("does not offer refresh on a research page", async () => {
      const actions = await openAndReadActions(lazy.AITAB_PAGE_TYPES.RESEARCH);

      Assert.ok(!actions.editable, "A research page is sent as not editable");
      Assert.ok(
        !actions.refresh,
        "Refresh, which rewrites the page, is hidden"
      );
      Assert.ok(actions.delete, "Delete is still offered");
    });
  });
});
