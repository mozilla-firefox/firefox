/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import {
  parseMarkdown,
  parseMarkdownBlocks,
  CHAT_WRAPPER_ELEMENTS,
} from "chrome://browser/content/aiwindow/modules/ChatMarkdownParser.mjs";
import { dispatchClientError } from "chrome://browser/content/aiwindow/modules/ClientErrorTelemetry.mjs";
import {
  isSettingsURL,
  isSmartPageURL,
} from "chrome://browser/content/aiwindow/modules/TrustedInternalURLs.mjs";
import { parseTabGroupMentionId } from "chrome://browser/content/urlbar/SmartbarMentionUtils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/ai-chat-card.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/ai-chat-grid.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/ai-chat-table.mjs";

// The percent of default favicons allowed to show the
// grid view by default for history thumbnails, if more
// than this threshold are default favicons default to
// list view.
const MISSING_OPENGRAPH_IMAGE_THRESHOLD = 0.4;

// How many items to display at a time in the history
// thumbnail grid
const HISTORY_GRID_PAGE_SIZE = 12;

/**
 * A custom element for rendering a single chat message, either a user message or an
 * assistant message. It handles the markdown rendering and any custom link handling.
 */
export class AIChatMessage extends MozLitElement {
  #lastMessage = null;
  #lastMessageElement = null;
  #blockHtml = [];

  /**
   * The assistant message whose first-character telemetry event was most recently sent.
   * This guards against double-sending an event for streaming responses to the same message.
   *
   * @type {string|null}
   */
  #firstCharacterReportedMessageId = null;

  /**
   * Track if link unfurling needs to re-run, as it needs to manually manipulate
   * the rendered element.
   *
   * @param {boolean}
   */
  #unfurledUrlsNeedUpdating = true;

  /**
   * Track which unseen URLs are in the message to avoid unnecessary re-renders when the
   * seen URLs change.
   *
   * @type {Set<string>}
   */
  #urlsUnfurledInMessage = new Set();

  /**
   * Track grid instances so they're not recreated as responses stream causing
   * render flickers
   */
  #historyGrids = new Map();

  /**
   * One search_browsing_history result, keyed by URL in the historyResults Map.
   *
   * @typedef {object} HistoryItem
   * @property {string} title            - Sanitized page title, falls back to
   *                                       URL (set in SearchBrowsingHistory.sys.mjs
   *                                       buildHistoryRow).
   * @property {string} url              - Page URL (set in SearchBrowsingHistory.sys.mjs
   *                                       buildHistoryRow).
   * @property {string} [timestamp]      - Localized visit time (set in
   *                                       ChatConversation.sys.mjs addHistoryResults).
   * @property {string|null} [thumbnail] - og:image URL from Places preview_image_url
   *                                       (set in SearchBrowsingHistory.sys.mjs buildHistoryRow);
   *                                       input to captureThumbnail().
   * @property {string|null} [image]     - Result of captureThumbnail(thumbnail); the
   *                                       displayed image (set in ai-chat-content.mjs
   *                                       #handleAssetsReady).
   * @property {boolean} [hasFavicon]    - Whether Places has a real favicon (resolved
   *                                       in AIChatContentParent.sys.mjs #pageHasFavicon,
   *                                       set in ai-chat-content.mjs #handleAssetsReady).
   * @property {string} [faviconUrl]     - Computed page-icon: URI (set in
   *                                       ai-chat-message.mjs #calculateHistoryGridView).
   */

  static properties = {
    role: { type: String, reflect: true, attribute: "data-message-role" }, // "user" | "assistant"
    message: { type: String },
    messageL10n: { type: Object, attribute: false },
    messageId: { type: String, reflect: true, attribute: "data-message-id" },
    complete: { type: Boolean, reflect: true },
    seenUrls: { type: Object, attribute: false },
    historyResults: { type: Object, attribute: false }, // HistoryItem
    conversationId: { type: String },
  };

  constructor() {
    super();

    /**
     * The URLs seen in the conversation, used for link unfurling.
     *
     * @type {Set<string>}
     */
    this.seenUrls = new Set();

    /**
     * Records returned by the search_browsing_history tool for this message,
     * keyed by URL. Used to detect bullet lists of history results and to
     * supply per-result data (title, url, thumbnail, visitDate,
     * visitCount) to the history grid renderer.
     *
     * @type {Map<string, object>}
     */
    this.historyResults = new Map();

    /**
     * Invalidate the unfurled URL rendering when the conversation ID changes.
     *
     * @type {string}
     */
    this.conversationId = "";
  }

  connectedCallback() {
    super.connectedCallback();
    this.#initLinkNavigationListener();
  }

  #initLinkNavigationListener() {
    this.shadowRoot.addEventListener("click", event => {
      let target = event.target;
      while (target && target !== this.shadowRoot) {
        if (target.tagName === "A" && target.href) {
          event.preventDefault();
          this.dispatchEvent(
            new CustomEvent("AIChatContent:OpenLink", {
              bubbles: true,
              composed: true,
              detail: {
                url: target.href,
                shiftKey: event.shiftKey,
                metaKey: event.metaKey,
                ctrlKey: event.ctrlKey,
                altKey: event.altKey,
                button: event.button,
              },
            })
          );
          return;
        }
        target = target.parentElement;
      }
    });
  }

  /**
   * Link unfurling for unseen URLs may need to re-run.
   *
   * @param {Map} changed
   */
  willUpdate(changed) {
    if (
      changed.has("seenUrls") &&
      this.#urlsUnfurledInMessage.intersection(this.seenUrls).size
    ) {
      // A link that was unfurled is now in the "seen" set of URLs and needs updating.
      // The "seen" set of URLs can only grow, never shrink.
      this.#unfurledUrlsNeedUpdating = true;
    }

    if (changed.has("conversationId")) {
      // The conversation changed. The unfurled URLs need to be recomputed as the seen
      // urls can be completely different.
      this.#unfurledUrlsNeedUpdating = true;
    }

    if (changed.has("complete") || changed.has("historyResults")) {
      this.#unfurledUrlsNeedUpdating = true;
    }
  }

  /**
   * Reports when an assistant message first displays readable text.
   *
   * @param {Map} changed - Properties changed by this update.
   */
  #maybeReportTimeToFirstCharacter(changed) {
    // The message list can reuse this element for a different message,
    // so we need to first ensure that the context is relevant to report
    // the time to first character.
    if (
      (!changed.has("message") && !changed.has("messageId")) ||
      this.role !== "assistant" ||
      !this.messageId ||
      this.#firstCharacterReportedMessageId === this.messageId ||
      !this.message?.trim() ||
      !this.#lastMessageElement?.innerText.trim()
    ) {
      return;
    }

    this.#firstCharacterReportedMessageId = this.messageId;
    this.dispatchEvent(
      new CustomEvent("AIChatContent:FirstCharacterDisplayed", {
        bubbles: true,
        composed: true,
        detail: {
          conversationId: this.conversationId,
          messageId: this.messageId,
        },
      })
    );
  }

  updated(changed) {
    this.#maybeReportTimeToFirstCharacter(changed);

    if (changed.has("complete") && this.complete && this.role === "assistant") {
      const messageEl = this.shadowRoot?.querySelector(`.message-${this.role}`);
      const text = messageEl
        ? (messageEl.innerText || messageEl.textContent || "")
            .replace(/\s+/g, " ")
            .trim()
        : "";
      this.dispatchEvent(
        new CustomEvent("ai-chat-message:complete", {
          bubbles: true,
          composed: true,
          detail: { messageId: this.messageId, text },
        })
      );
    }
  }

  #getIconSrc = linkHref => {
    // Since we use the "page-icon:" CSP rule we can just look at the page URL for the img src
    const finalIcon = linkHref
      ? `page-icon:${linkHref}`
      : "chrome://global/skin/icons/defaultFavicon.svg";
    return finalIcon;
  };

  /**
   * Replaces "website mention" markdown links rendered as anchors with an
   * <smartwindow-chip> custom element.
   *
   * Example markdown that produces such an anchor:
   *
   *   Look up [@Google](
   *     mention:?href=https%3A%2F%2Fwww.google.com
   *   )
   *
   * After ProseMirror renders the markdown, this method transforms:
   *
   *   <a href="mention:?...">@Google</a>
   *
   * into:
   *
   *   <smartwindow-chip type="in-line" ...></smartwindow-chip>
   *
   *
   * `mention:?` is intentional: the substring after it is a query string.
   * This lets us detect mention anchors and parse href but in the future additional params
   * for example type or src via `URLSearchParams`.
   *
   * @param {Element} root
   *   Root element that already contains sanitized HTML for the message
   *   (i.e., after `setHTML()` has inserted the ProseMirror-rendered output).
   */
  #replaceWebsiteMentions(root) {
    const MAX_URL_LENGTH = 2048;
    const MENTION_PREFIX = "mention:?";
    const links = root.querySelectorAll(`a[href^="${MENTION_PREFIX}"]`);

    for (const a of links) {
      const { href } = a;

      if (!href.startsWith(MENTION_PREFIX)) {
        continue;
      }

      const params = new URLSearchParams(href.substring(MENTION_PREFIX.length));

      // Build Data
      const linkHref = params.get("href") || "";

      if (!linkHref || linkHref.length > MAX_URL_LENGTH) {
        // TODO - https://bugzilla.mozilla.org/show_bug.cgi?id=2011538
        continue;
      }

      const label = a.textContent || linkHref;

      // Create Website Chip
      const chip = root.ownerDocument.createElement("smartwindow-chip");
      chip.type = "in-line";
      chip.label = label;

      // Messages submitted before the type was serialized carry only a color.
      const color = params.get("color");
      const isTabGroup = params.get("type")
        ? params.get("type") == "tabGroup"
        : !!color;
      if (isTabGroup) {
        chip.isTabGroup = true;
        chip.tabGroupColor = color ?? "";
        chip.tabGroupId = parseTabGroupMentionId(linkHref) ?? "";
      } else {
        chip.iconSrc = this.#getIconSrc(linkHref);
        chip.href = linkHref;
      }

      a.replaceWith(chip);
    }
  }

  /**
   * This functions handles unfurling links that have not been seen by the conversation.
   * Language models can hallucinate URLs and can be forced by untrusted content to
   * generate URLs. We unfurl these unseen links so that the user has a disclosure as
   * to the contents of the links. Unseen link text is no longer clickable (but dash
   * underlined) while the link is fully displayed as an underlined and clickable link
   * surrounded by parentheses. If the text is just a raw link, then the unfurling is
   * skipped.
   *
   * @param {Element} root - The element containing rendered markdown
   */
  #unfurlUnseenLinks(root) {
    this.#urlsUnfurledInMessage = new Set();

    // Go through each link in the rendered markdown. It's important to do this after
    // the markdown library renders the links, so we don't have to manually extract
    // the links from the text. If we did this, then there may be a disagreement between
    // what is considered a link by the library and what we think is a link.
    for (const anchor of root.querySelectorAll("a[href]")) {
      const parsed = URL.parse(anchor.href);

      // Settings pages are always trusted; a generated page only when this
      // conversation produced it.
      if (
        isSettingsURL(parsed) ||
        (isSmartPageURL(parsed) && this.seenUrls.has(anchor.href))
      ) {
        continue;
      }

      // Disallowed scheme, strip href to prevent navigation.
      if (
        !parsed ||
        (parsed.protocol !== "http:" && parsed.protocol !== "https:")
      ) {
        anchor.removeAttribute("href");
        continue;
      }

      if (!this.seenUrls.has(anchor.href)) {
        // Track every URL present in the message.
        this.#urlsUnfurledInMessage.add(anchor.href);

        const { textContent, href } = anchor;
        const textUrl = URL.parse(textContent.trim());
        if (textUrl) {
          // This is just a raw URL, no disclosure is needed.
          continue;
        }

        const doc = anchor.ownerDocument;

        const label = doc.createElement("span");
        label.className = "untrusted-link-label";
        label.textContent = textContent;

        const link = doc.createElement("a");
        link.href = href;
        link.textContent = href;

        const disclosure = doc.createElement("span");
        disclosure.append(" (", link, ")");

        anchor.replaceWith(label, disclosure);
      }
    }
  }

  #isHistoryItem(li) {
    const link = li.querySelector("a[href]");
    return !!(link && this.historyResults.has(link.href));
  }

  /**
   * Manage history result lists. While the message has history context its `ul`s
   * are hidden by default (using `with-history` class) so a list that will become
   * a grid never flashes as raw bullets. On every parse:
   *  - Streaming: reveal a list as soon as a settled `<li>` (one that isn't the
   *    streaming frontier) is clearly not a history result; otherwise keep it
   *    hidden until the message completes.
   *  - Complete: convert a list whose items all link to history URLs into a grid;
   *    reveal any list that isn't a pure history list.
   *
   * @param {Element} root - The element containing rendered markdown
   */
  #replaceHistoryResults(root) {
    if (!this.historyResults?.size) {
      return;
    }

    // The last <li> in the message is the streaming frontier and may be mid-write
    // (its link not fully parsed yet), so it isn't judged while streaming.
    const allListItems = root.querySelectorAll("li");
    const frontierLi = this.complete
      ? null
      : (allListItems[allListItems.length - 1] ?? null);

    // The model may render history results as either a bulleted (ul) or
    // numbered (ol) list, so handle both.
    const lists = Array.from(root.querySelectorAll("ul, ol"));
    lists.forEach((list, index) => {
      const items = Array.from(list.querySelectorAll(":scope > li"));
      if (!items.length) {
        return;
      }

      if (this.complete) {
        if (items.every(item => this.#isHistoryItem(item))) {
          const listItems = items.map(li =>
            this.historyResults.get(li.querySelector("a[href]").href)
          );

          list.replaceWith(this.#getHistoryListGrid(listItems, index));
        } else {
          // Not a pure history list so reveal it.
          list.style.display = "revert";
        }
        return;
      }

      // Streaming: reveal once a settled item is clearly not a history result.
      const hasSettledNonHistory = items.some(
        li => li !== frontierLi && !this.#isHistoryItem(li)
      );
      if (hasSettledNonHistory) {
        list.style.display = "revert";
      }
    });
  }

  /**
   * Gets an instance of ai-chat-grid to display search_browsing_history results
   *
   * @param {Array<string> | null} gridItems
   * @param {number} index
   *
   * @returns {Node}
   */
  #getHistoryListGrid(gridItems, index) {
    const items = Array.from(gridItems || []).slice(0, HISTORY_GRID_PAGE_SIZE);
    const view = this.#calculateHistoryGridView(items);

    // The grid is loading while any item that has a thumbnail is still waiting
    // on its capture (image is undefined until the parent resolves it).
    const loading = items.some(
      item => item.thumbnail && item.image === undefined
    );

    if (this.#historyGrids.has(index)) {
      const historyGrid = this.#historyGrids.get(index);
      historyGrid.view = view;
      historyGrid.loading = loading;
      historyGrid.items = items;

      return historyGrid;
    }

    items.forEach((item, ndx) => {
      item.resultIndex = ndx;
      item.resultCount = items.length;
    });

    const grid = this.ownerDocument.createElement("ai-chat-grid");
    grid.loading = loading;
    grid.view = view;
    grid.items = items;
    grid.showSwitch = true;
    grid.gridItem = this.#renderHistoryGridTile.bind(this);
    grid.rowItem = this.#renderHistoryGridRow.bind(this);

    this.#historyGrids.set(index, grid);
    this.#requestHistoryAssets(items);

    this.dispatchEvent(
      new CustomEvent("AIChatContent:HistoryGridRender", {
        bubbles: true,
        composed: true,
        detail: {
          itemCount: items.length,
        },
      })
    );

    return grid;
  }

  /**
   * Ask the parent process to resolve this grid's history assets: the page
   * thumbnail (`moz-page-thumb://` URI) and whether each page has a real
   * favicon. Dispatched once per grid (on cache-miss); the results come back via
   * the `aiChatContentActor:assets-ready` event handled by ai-chat-content.
   *
   * @param {Array<HistoryItem>} items
   */
  #requestHistoryAssets(items) {
    // Send every item, not just ones with a thumbnail: the parent also resolves
    // favicon status, which is needed for items that have no og:image.
    const requestItems = items
      .filter(item => item?.url)
      .map(({ url, thumbnail }) => ({ url, thumbnail }));

    if (!requestItems.length) {
      return;
    }

    this.dispatchEvent(
      new CustomEvent("AIChatContent:RequestAssets", {
        bubbles: true,
        composed: true,
        detail: {
          conversationId: this.conversationId,
          messageId: this.messageId,
          items: requestItems,
        },
      })
    );
  }

  /**
   * Calculates which view to show in history thumbnail grid,
   * will show grid if there are more missing OpenGraph images
   * than the configured threshold.
   *
   * @param {Array<HistoryItem>} items - The grid's items.
   * @returns {"grid" | "list"}
   * @private
   */
  #calculateHistoryGridView(items) {
    let missingOpenGraph = 0;
    items.forEach(item => {
      if (!item.image) {
        missingOpenGraph++;
      }

      item.faviconUrl = this.#getFaviconUri(item.url);
    });

    let view = "grid";
    const percentDefaulted = missingOpenGraph / items.length;
    if (percentDefaulted > MISSING_OPENGRAPH_IMAGE_THRESHOLD) {
      view = "list";
    }

    return view;
  }

  /**
   * Renders a history thumbnail item when in grid view
   *
   * @param {HistoryItem} item
   * @private
   */
  #renderHistoryGridTile(item) {
    if (!item) {
      return nothing;
    }

    return html`
      <ai-chat-card
        title=${item.title}
        url=${item.url}
        favicon=${item.hasFavicon ? item.faviconUrl : null}
        timestamp=${item.timestamp}
        thumbnail=${item.image}
        @click=${this.itemClick.bind(this, item)}
      >
      </ai-chat-card>
    `;
  }

  /**
   * Renders a history thumbnail item when in list view
   *
   * @param {HistoryItem} item
   * @private
   */
  #renderHistoryGridRow(item) {
    if (!item) {
      return nothing;
    }

    const favicon = this.#getFaviconUri(item.url);
    return html`
      <a
        part="row"
        class="history-thumbnail-row"
        href=${item.url}
        target="_blank"
        @click=${this.itemClick.bind(this, item)}
      >
        <img part="favicon" src=${favicon} />
        <span part="title" class="history-thumbnail-title">${item.title}</span>
        <span part="timestamp">${item.timestamp}</span>
      </a>
    `;
  }

  /**
   * Handles clicks on the History Thumbnail grid items
   * and dispatches an event so telemetry can be recorded.
   *
   * @param {HistoryItem} item
   */
  itemClick(item) {
    this.dispatchEvent(
      new CustomEvent("AIChatContent:HistoryGridItemClick", {
        bubbles: true,
        composed: true,
        detail: {
          item,
        },
      })
    );
  }

  /**
   * Returns a URI for a website's favicon
   *
   * @param {string} pageUrl
   *
   * @returns {string}
   * @private
   */
  #getFaviconUri(pageUrl) {
    return `page-icon:${pageUrl}`;
  }

  /**
   * Custom sanitizer for chat messages.
   *
   * @type {Sanitizer}
   */
  static #chatMessageSanitizer;
  static {
    this.#chatMessageSanitizer = new Sanitizer();
    for (const { element, attributes } of Object.values(
      CHAT_WRAPPER_ELEMENTS
    )) {
      this.#chatMessageSanitizer.allowElement(element);
      for (const attr of attributes) {
        this.#chatMessageSanitizer.allowAttribute({
          name: attr,
          elements: [element],
        });
      }
    }
  }

  /**
   * Parse markdown content to HTML.
   *
   * @param {string} markdown the Markdown to parse
   * @param {Element} element the element in which to insert the parsed markdown.
   */
  #parseMarkdown(markdown, element) {
    try {
      element.setHTML(parseMarkdown(markdown), {
        sanitizer: AIChatMessage.#chatMessageSanitizer,
      });
    } catch (error) {
      dispatchClientError(this, error, "markdown");
      throw error;
    }
  }

  /**
   * Parse markdown and replace mentions for user messages
   *
   * @param {string} markdown
   * @param {Element} element
   */
  parseUserMarkdown(markdown, element) {
    this.#parseMarkdown(markdown, element);
    this.#replaceWebsiteMentions(element);
  }

  /**
   * Render one top-level block's HTML into a single sanitized element.
   * markdown-it renders each block to exactly one element, which we return.
   *
   * @param {string} blockHtml
   * @returns {Element|null}
   */
  #renderBlockElement(blockHtml) {
    const scratch = this.ownerDocument.createElement("div");
    try {
      scratch.setHTML(blockHtml, {
        sanitizer: AIChatMessage.#chatMessageSanitizer,
      });
    } catch (error) {
      dispatchClientError(this, error, "markdown");
      throw error;
    }
    return scratch.firstElementChild;
  }

  /**
   * Reconcile the container's children against freshly parsed block HTML. Blocks
   * whose HTML is unchanged keep their existing DOM untouched (so finished
   * tables and their observers are never rebuilt); only changed or new blocks
   * are re-rendered, and trailing removed blocks are dropped.
   *
   * @param {Element} container
   * @param {Array<string>} newHtml - One HTML string per top-level block.
   * @returns {number} How many blocks were (re)rendered this pass.
   */
  #reconcileBlocks(container, newHtml) {
    const oldHtml = this.#blockHtml;
    let rebuilt = 0;
    for (let i = 0; i < newHtml.length; i++) {
      if (container.children[i] && oldHtml[i] === newHtml[i]) {
        continue;
      }
      const node = this.#renderBlockElement(newHtml[i]);
      if (!node) {
        continue;
      }
      rebuilt++;
      if (container.children[i]) {
        container.replaceChild(node, container.children[i]);
      } else {
        container.append(node);
      }
    }
    while (container.children.length > newHtml.length) {
      container.lastElementChild.remove();
    }
    this.#blockHtml = newHtml;
    return rebuilt;
  }

  /**
   * Render the assistant message. Localized messages render from their l10n id
   * via Fluent dom overlay otherwise the markdown is parsed and unseen links
   * are unfurled. The markdown path is memoized on the message contents and
   * seen links Set to guard against unnecessary re-renders.
   *
   * @returns {HTMLElement|import("chrome://global/content/vendor/lit.all.mjs").TemplateResult}
   */
  getAssistantMessage() {
    if (this.messageL10n?.id) {
      return this.#renderL10nMessage();
    }

    // Reuse one persistent container across chunks so finished DOM
    // (and its custom elements) is never torn down; we refill it in place.
    if (!this.#lastMessageElement) {
      this.#lastMessageElement = this.ownerDocument.createElement("div");
      this.#lastMessageElement.className = "message-" + this.role;
    }
    const messageElement = this.#lastMessageElement;

    if (this.message == this.#lastMessage && !this.#unfurledUrlsNeedUpdating) {
      // The message is the same and the seen URLs haven't changed.
      return this.#lastMessageElement;
    }

    if (!this.message) {
      // There is no message to show. Use an empty message element.
      messageElement.replaceChildren();
      messageElement.classList.remove("with-history");
      this.#blockHtml = [];
      this.#lastMessage = this.message;
      return messageElement;
    }

    // seenUrls/history/completion changes can alter an already-rendered block
    // (e.g. a now-seen link must stop being unfurled), so force a full reconcile.
    if (this.#unfurledUrlsNeedUpdating) {
      this.#blockHtml = [];
    }

    // Time the render so its per-chunk cost shows up in the Firefox Profiler.
    // This runs in the content process, so use the User Timing API rather than
    // ChromeUtils markers (which are parent-process only).
    const renderStart = performance.now();
    const blockHtml = parseMarkdownBlocks(this.message).map(
      block => block.html
    );
    const rebuiltCount = this.#reconcileBlocks(messageElement, blockHtml);

    // When the conversation has history results, hide lists by default so a
    // list that will become a grid never flashes as raw bullets;
    // #replaceHistoryResults reveals non-history lists (and converts matches).
    if (this.historyResults?.size) {
      messageElement.classList.add("with-history");
    } else {
      messageElement.classList.remove("with-history");
    }

    this.#replaceHistoryResults(messageElement);
    this.#unfurlUnseenLinks(messageElement);

    performance.measure("SmartWindow chat render", {
      start: renderStart,
      detail: { rebuilt: rebuiltCount, total: blockHtml.length },
    });

    // Track the properties for memoization.
    this.#lastMessage = this.message;
    this.#unfurledUrlsNeedUpdating = false;

    return messageElement;
  }

  /**
   * Parse the markdown in a user's message. No link unfurling is necessary since
   * the contents don't come from untrusted content. This function is memoized
   * based on the message contents.
   *
   * @returns {HTMLElement}
   */
  getUserMessage() {
    const messageElement = this.ownerDocument.createElement("div");
    messageElement.className = "message-" + this.role;

    if (!this.message) {
      return messageElement;
    }

    this.parseUserMarkdown(this.message, messageElement);

    return messageElement;
  }

  /**
   * Render a localized assistant message via Fluent dom overlay. The message
   * text and any embedded '<a data-l10n-name>' link text come from the l10n id.
   * The link's href is set here.
   *
   * @returns {import("chrome://global/content/vendor/lit.all.mjs").TemplateResult}
   */
  #renderL10nMessage() {
    const { id, args, link } = this.messageL10n;
    return html`<div class="message-assistant">
      <span
        data-l10n-id=${id}
        data-l10n-args=${args ? JSON.stringify(args) : nothing}
      >
        ${link
          ? html`<a data-l10n-name=${link.l10nName} href=${link.href}></a>`
          : nothing}
      </span>
    </div>`;
  }

  /**
   * The text a localized message shows on screen, or "" when the message
   * isn't localized or hasn't rendered yet
   *
   * @type {string}
   */
  get localizedText() {
    if (!this.messageL10n?.id) {
      return "";
    }
    return (
      this.shadowRoot?.querySelector(".message-assistant [data-l10n-id]")
        ?.innerText ?? ""
    );
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/ai-chat-message.css"
      />

      <article>
        ${this.role === "user"
          ? this.getUserMessage()
          : this.getAssistantMessage()}
      </article>
    `;
  }
}

customElements.define("ai-chat-message", AIChatMessage);
