/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AITabStore } from "moz-src:///browser/components/aiwindow/ui/modules/AITabStore.sys.mjs";
import { getHeadingFont } from "moz-src:///browser/components/aiwindow/ui/modules/AITabHeadingFont.sys.mjs";
import { ConversationStore } from "moz-src:///browser/components/aiwindow/ui/modules/ConversationStore.sys.mjs";
import { getSmartPageName } from "chrome://browser/content/aiwindow/modules/TrustedInternalURLs.mjs";
import { A2UI } from "../modules/A2UI.mjs";

/**
 * A stored AI Tab record.
 *
 * @typedef {import("../modules/AITabStore.sys.mjs").AITabRow} AITabRow
 */

/**
 * A page prepared for rendering.
 *
 * @typedef {import("../modules/A2UI.mjs").HydratedAITabData} HydratedAITabData
 */

/**
 * A stored-page lookup and its URL identifiers.
 *
 * @typedef {object} AITabPageLookup
 * @property {AITabRow|null} pageData - Latest stored version, if present.
 * @property {string} pageName - Slug from the page URL.
 * @property {string} url - Full page URL.
 */

/**
 * Page data sent to the viewer, including hydrated content and a localized date.
 *
 * @typedef {AITabRow & HydratedAITabData & {
 *   createdAtLabel: string
 * }} AITabData
 */

/**
 * Page lookup response sent to the child actor.
 *
 * @typedef {{success: true, page: AITabData|null} |
 *   {success: false, error: string}} AITabPageLookupResponse
 */

const SMART_PAGE_ICON_URL =
  "chrome://browser/content/aiwindow/assets/smart-page-16.svg";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AIWINDOW_URL:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  AITab: "moz-src:///browser/components/aiwindow/models/aitab/AITab.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "fluentStrings", () => {
  return new Localization(["preview/aiWindow.ftl"], true);
});

let smartPageIconDataURI = null;

async function getSmartPageIconDataURI() {
  if (!smartPageIconDataURI) {
    const response = await fetch(SMART_PAGE_ICON_URL);
    const svg = await response.text();
    smartPageIconDataURI = Services.io.newURI(
      `data:image/svg+xml,${encodeURIComponent(svg)}`
    );
  }
  return smartPageIconDataURI;
}

/**
 * Renders the eyebrow shown above a generated page's title.
 *
 * Formatting happens here rather than in the component because the strings
 * are localized in the parent and handed to content ready to display, the
 * same way the history grid passes its timestamps down.
 *
 * "Today" means the same calendar day in the local timezone, which is what a
 * reader means by it. A rolling 24 hour window would call late yesterday
 * today, and comparing timestamps directly would break either side of
 * midnight. `now` is a parameter so tests can pin the boundary rather than
 * racing the clock.
 *
 * @param {number} createdAt - aitab_pages.created_at, in microseconds.
 * @param {number} [now] - Milliseconds to treat as the current time.
 * @returns {string} Empty for a timestamp that is not a usable date.
 */
export function formatCreatedAt(createdAt, now = Date.now()) {
  // The store writes this column as `Date.now() * 1000`.
  const created = new Date(Math.round(createdAt / 1000));
  if (!createdAt || Number.isNaN(created.valueOf())) {
    return "";
  }

  if (created.toDateString() == new Date(now).toDateString()) {
    return lazy.fluentStrings.formatValueSync("aitab-created-today");
  }

  return lazy.fluentStrings.formatValueSync("aitab-created-on", {
    date: created.getTime(),
  });
}

/**
 * Parent actor for about:smartpage. Resolves the page name from the page URL
 * into the stored page config that content renders, and owns the destructive
 * actions the page offers.
 */
export class AITabParent extends JSWindowActorParent {
  #headingFontPromise = null;
  #tab;
  #pageUuid;

  actorCreated() {
    this.#headingFontPromise = this.#loadHeadingFont();

    const browser = this.browsingContext.top.embedderElement;
    this.#setTab(browser?.documentGlobal.gBrowser?.getTabForBrowser(browser));
  }

  didDestroy() {
    this.#setTab(null);
  }

  #setTab(tab) {
    this.#tab?.removeEventListener("TabSelect", this.#onTabSelect);
    this.#tab?.linkedBrowser.removeEventListener(
      "SwapDocShells",
      this.#onSwapDocShells
    );
    this.#tab?.documentGlobal.removeEventListener(
      "activate",
      this.#onWindowActivate
    );

    this.#tab = tab;

    this.#tab?.addEventListener("TabSelect", this.#onTabSelect);
    this.#tab?.linkedBrowser.addEventListener(
      "SwapDocShells",
      this.#onSwapDocShells
    );
    this.#tab?.documentGlobal.addEventListener(
      "activate",
      this.#onWindowActivate
    );
  }

  async receiveMessage({ data, name }) {
    switch (name) {
      case "AITab:GetPage":
        return this.#handleGetPage();
      case "AITab:GetHeadingFont":
        return { font: await this.#headingFontPromise };
      case "AITab:DeletePage":
        return this.#handleDeletePage();
      case "AITab:OpenLink":
        this.#handleOpenLink(data);
        return null;
      case "AITab:RecordView":
        this.#recordView(data);
        return null;
      default:
        console.warn(`AITabParent received unknown message: ${name}`);
        return null;
    }
  }

  /**
   * The slug and URL of the page this tab is showing, read from the tab's URL.
   * The URL is kept so deleting the page also removes that exact URL from
   * history.
   *
   * Read this as soon as a message from the page arrives. The tab can close
   * while a store call is pending, and reading browsingContext after that
   * throws.
   *
   * @returns {?{pageName: string, url: string}} Null when the URL carries no
   *   usable page name.
   */
  get #currentPage() {
    const url = this.browsingContext?.currentURI?.spec;
    const pageName = url && getSmartPageName(URL.parse(url));
    return pageName ? { pageName, url } : null;
  }

  /**
   * Handles the activate event to trigger the tab refresh if the
   * tab is already focused when the window is focused again.
   */
  #onWindowActivate = () => {
    if (this.#tab?.selected) {
      this.#onTabSelect();
    }
  };

  /**
   * Handles SwapDocShells event so the window can handle events from
   * it's new tab host after switching windows.
   *
   * @param {Event} event
   */
  #onSwapDocShells = event => {
    const oldBrowser = this.browsingContext.top.embedderElement;
    if (event.currentTarget !== oldBrowser) {
      return;
    }

    const browser = event.detail;
    this.#setTab(browser.documentGlobal.gBrowser.getTabForBrowser(browser));
  };

  /**
   * Handles TabSelect events to check whether the data for the
   * Smart Page has changed. If it has a message is sent to the
   * child actor to refresh the page.
   *
   * @returns {Promise<void>} Resolves after checking for an updated version.
   */
  #onTabSelect = async () => {
    const pageDataResults = await this.#getPageData();
    if (!this.#tab) {
      return;
    }

    if (!pageDataResults || !pageDataResults.pageData) {
      return;
    }

    const { pageData } = pageDataResults;
    if (this.#pageUuid === pageData.uuid) {
      return;
    }

    const page = this.#getPageForUI(pageData);
    if (!page) {
      return;
    }

    this.sendAsyncMessage("AITab:RefreshPageData", page);
    this.#pageUuid = pageData.uuid;
  };

  /**
   * Gets the page data for a Smart Page from the AITabStore
   *
   * @returns {Promise<AITabPageLookup|null>} Lookup result, or null for an invalid page URL.
   */
  async #getPageData() {
    const currentPage = this.#currentPage;
    if (!currentPage) {
      return null;
    }

    const { pageName, url } = currentPage;
    const pageData = await AITabStore.getBySlug(pageName);

    return { pageData, pageName, url };
  }

  /**
   * Gets the hydrated surface data for rendering the Smart Page
   *
   * @param {AITabRow} pageData - Stored version to hydrate.
   *
   * @returns {AITabData|null} Renderable page, or null when no surface exists.
   */
  #getPageForUI(pageData) {
    const surface = pageData?.components?.surface;
    if (!surface) {
      return null;
    }

    const a2ui = new A2UI(surface);
    const page = a2ui.toUI();

    return {
      ...pageData,
      ...page,
      createdAtLabel: formatCreatedAt(pageData.createdAt),
    };
  }

  /**
   * @returns {Promise<AITabPageLookupResponse>} Page data or a lookup error.
   */
  async #handleGetPage() {
    try {
      const pageDataResults = await this.#getPageData();
      if (!pageDataResults) {
        return { success: false, error: "Invalid page name" };
      }

      const { pageData, url, pageName } = pageDataResults;
      if (!pageData) {
        // Leftover slugs from deleted pages should not keep coming back as a
        // history or address bar result.
        this.#removeFromHistory(pageName, url).catch(error => {
          console.error(
            "Could not remove a missing AI Tab page from history",
            error
          );
        });

        return { success: true, page: null };
      }

      const page = this.#getPageForUI(pageData);
      if (!page) {
        return { success: true, page: null };
      }

      this.#storeFavicon(url).catch(console.error);
      this.#pageUuid = pageData.uuid;
      return { success: true, page };
    } catch (error) {
      console.error("Failed to retrieve AI Tab page:", error);
      return { success: false, error: "Failed to retrieve page" };
    }
  }

  /**
   * Started when the actor is created so the download overlaps the page
   * lookup. createAITab() usually started it during generation, in which case
   * this reuses that download. Content asks for it separately and does not
   * wait for it to render.
   *
   * @returns {Promise<?ArrayBuffer>} Null when the font is unavailable.
   */
  async #loadHeadingFont() {
    try {
      return await getHeadingFont();
    } catch (error) {
      console.error("Failed to download the AI Tab heading font:", error);
      return null;
    }
  }

  /**
   * Deletes a generated page and the conversation that produced it. Every
   * page comes from its own conversation, so the two are removed together.
   *
   * They live in separate databases, so this cannot be one transaction. The
   * pages go first: a conversation with no pages is unreachable, while pages
   * that outlive their conversation would still load by slug.
   *
   * @returns {Promise<object>}
   */
  async #handleDeletePage() {
    const currentPage = this.#currentPage;
    if (!currentPage) {
      return { success: false, error: "Invalid page name" };
    }
    const { pageName, url } = currentPage;

    try {
      const page = await AITabStore.getBySlug(pageName);

      // Deleting something already gone is not an error, but the tab still
      // gets sent home: a second tab open on the same page would otherwise be
      // stranded on the unavailable state with nowhere to go.
      if (page) {
        await AITabStore.deleteBySlug(page.slug);
        Glean.smartWindow.aitabDelete.record();

        // The page is gone from here on. If clearing the conversation fails,
        // the delete the reader asked for still happened, so say so and log
        // the leftover rather than reporting a failure that did not happen.
        //
        // The generation conversation holds the text extracted from every
        // source page, so it is the one that has to go; `convId` names the
        // chat the page was requested in, which outlives it. Pages stored
        // before that id was recorded have nothing to clear.
        if (page.toolConvId) {
          try {
            await ConversationStore.deleteConversationById(page.toolConvId);
          } catch (error) {
            console.error(
              "Deleted an AI Tab page but could not delete its conversation",
              error
            );
          }
        }

        try {
          await this.#removeFromHistory(page.slug, url);
        } catch (error) {
          console.error(
            "Deleted an AI Tab page but could not remove it from history",
            error
          );
        }
      }

      // Deferred so the reply reaches content before the tab navigates away.
      Services.tm.dispatchToMainThread(() => this.#returnToSmartWindowHome());

      return { success: true };
    } catch (error) {
      console.error("Could not delete an AI Tab page", error);
      return { success: false, error: "Could not delete the page" };
    }
  }

  /**
   * Removes the history entry for a generated page, plus the tab's own URL
   * when it names the same page under a different spelling.
   *
   * @param {string} slug
   * @param {string} tabURL - The tab URL the slug was read from.
   */
  async #removeFromHistory(slug, tabURL) {
    const urls = new Set([lazy.AITab.buildViewerURL(slug), tabURL]);
    await lazy.PlacesUtils.history.remove([...urls]);
  }

  /**
   * Stores the smart page icon in Places for the page URL, so page-icon:
   * lookups (mention chips, chat links, urlbar) resolve it. The tab icon is a
   * chrome: URL, which FaviconLoader never stores. Skipped wherever the visit
   * itself would not be kept.
   *
   * @param {string} url - The tab URL, already validated by #currentPage.
   */
  async #storeFavicon(url) {
    if (!Services.prefs.getBoolPref("places.history.enabled", true)) {
      return;
    }
    await lazy.PlacesUtils.favicons.setFaviconForPage(
      Services.io.newURI(url),
      Services.io.newURI(SMART_PAGE_ICON_URL),
      await getSmartPageIconDataURI()
    );
  }

  /**
   * Opens a link from anywhere on a generated page. Kept separate from
   * AIChatContentParent's handler so AI Tab clicks stay out of chat's
   * recordUriLoad() metric. The click is recorded after the attempt, with
   * a result of "switch", "new", or "error".
   *
   * @param {object} data
   * @param {string} data.url
   * @param {boolean} [data.preferSwitchToTab]
   * @param {string} [data.component_type] - Catalog component the link was
   *   in, the same name creation telemetry records, or "unknown".
   */
  #handleOpenLink({ url, preferSwitchToTab, component_type } = {}) {
    // Links come from model output, so only http and https are opened.
    // file:, data: and javascript: URLs are dropped, as is a missing or
    // unparseable url: URL.parse returns null for those.
    const uri = URL.parse(url);
    if (uri?.protocol != "http:" && uri?.protocol != "https:") {
      return;
    }

    const window = this.browsingContext?.topChromeWindow;
    if (!window) {
      return;
    }

    // The click is the user's choice to open the link, so it is recorded
    // whether the tab switch or the new load succeeds.
    let result = "new";
    try {
      if (
        preferSwitchToTab &&
        lazy.URILoadingHelper.switchToTabHavingURI(window, url, false, {})
      ) {
        result = "switch";
      } else {
        const { userContextId } =
          window.gBrowser.selectedBrowser.browsingContext.originAttributes;
        lazy.URILoadingHelper.openWebLinkIn(window, url, "tab", {
          triggeringPrincipal:
            Services.scriptSecurityManager.createNullPrincipal({
              userContextId,
            }),
          userContextId,
          forceForeground: false,
        });
      }
    } catch (error) {
      console.error("Could not open an AI Tab link", error);
      result = "error";
    }

    Glean.smartWindow.aitabLinkClick.record({ component_type, result });
  }

  /**
   * Records the status the page settled on, and whether the heading font
   * installed. Content is untrusted, so anything other than the three
   * statuses the page sets is recorded as an error.
   *
   * @param {object} data
   * @param {string} [data.outcome]
   * @param {string} [data.font]
   */
  #recordView(data) {
    const known = ["ready", "unavailable", "error"];
    const outcome = known.includes(data?.outcome) ? data.outcome : "error";
    const font = known.includes(data?.font) ? data.font : "error";
    Glean.smartWindow.aitabView.record({ outcome, font });
  }

  /**
   * Sends the tab back to the Smart Window home page once its content has been
   * deleted, so the user is not left on a page that no longer exists.
   *
   * This has to run in the parent: the home page is a chrome: URL and
   * about:smartpage is content, which is not allowed to navigate itself there.
   */
  #returnToSmartWindowHome() {
    // about:smartpage is not MAKE_LINKABLE, so web content cannot load it at
    // all, framed or otherwise: this is always the tab's own top-level context.
    // The optional call covers the tab being closed mid-delete.
    this.browsingContext?.loadURI(Services.io.newURI(lazy.AIWINDOW_URL), {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
  }
}
