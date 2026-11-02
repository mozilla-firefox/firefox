/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  html,
  nothing,
  repeat,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { isTabGroupMember } from "chrome://browser/content/urlbar/SmartbarMentionUtils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/assistant-message-footer.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/chat-assistant-error.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/chat-assistant-loader.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/chat-assistant-citations.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/website-chip-container.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/ai-website-confirmation.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/ai-chat-message.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/ai-action-confirmation.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/kit-mention.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/agent-monitor-item.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-textarea.mjs";
import {
  dispatchClientError,
  installClientErrorListeners,
} from "chrome://browser/content/aiwindow/modules/ClientErrorTelemetry.mjs";
import { STATE_CHOOSE } from "chrome://browser/content/aiwindow/components/aitab-tool-ui.mjs";

/**
 * @typedef {import("chrome://browser/content/aiwindow/components/ai-action-confirmation.mjs").TabSelectionData} TabSelectionData
 */

const FOLLOW_UP_QTY = 2;
// Stand-in "error" for invalid message data, which has no error object of its
// own. Reusing one object lets dispatchClientError's dedup skip a burst of
// repeated invalid-data reports instead of sending an IPC message each time.
const INVALID_MESSAGE_DATA = {};
/**
 * UI labels for tool results and follow-ups.
 */
const UI_TYPES = {
  AITAB: "aitab",
  WEBSITE_CONFIRMATION: "website-confirmation",
  TAB_GROUP_CONFIRMATION: "tab-group-confirmation",
  AI_ACTION_RESULT: "ai-action-result",
  CANCELLED_COMPONENT: "cancelled-component",
  ACTION_LOG: "action-log",
  RETRY_COMPONENT: "retry-component",
  AGENT_MONITOR: "agent-monitor-item",
};
/**
 * UI update types for communicating user interactions with tool UIs back to the actor.
 */
const UI_UPDATE_TYPES = {
  OPEN_AITAB: "open-aitab",
  CONFIRMATION_TAB_SELECTION: "confirmation-tab-selection",
  CANCEL_TAB_SELECTION: "cancel-tab-selection",
  CONFIRM_TAB_GROUP_SELECTION: "confirm-tab-group-selection",
  CONFIRM_OPEN_AND_GROUP_TABS_SELECTION:
    "confirm-open-and-group-tabs-selection",
  TOGGLE_ALL_TAB_SELECTION: "toggle-all-tab-selection",
  UNDO_TAB_CLOSE: "undo-tab-close",
  UNDO_TAB_GROUP: "undo-tab-group",
  RETRY_PROMPT: "retry-prompt",
  CREATE_WATCH: "create-watch",
  CANCEL_WATCH: "cancel-watch",
  UPDATE_WATCH: "update-watch",
  DELETE_WATCH: "delete-watch",
  PAUSE_WATCH: "pause-watch",
  CHECK_WATCH: "check-watch",
  SAVE_WATCH_DRAFT: "save-watch-draft",
};

const CONFIRMATION_UI_TYPES = [
  UI_TYPES.WEBSITE_CONFIRMATION,
  UI_TYPES.TAB_GROUP_CONFIRMATION,
];

/**
 * Map action types to their corresponding undo update types
 *
 * open_tabs is deliberately absent - every outcome (opening a tab,
 * switching to one, or opening+grouping a mix of new and already-open
 * tabs) is trivially reversible through normal browsing (back button,
 * switching back, closing/ungrouping), unlike close_tabs where undo
 * exists to prevent real data loss. canUndo below checks for an entry
 * here, so its button correctly doesn't render for open_tabs.
 */
const ACTION_TYPE_TO_UNDO_UPDATE_TYPE = {
  close_tabs: UI_UPDATE_TYPES.UNDO_TAB_CLOSE,
  group_tabs: UI_UPDATE_TYPES.UNDO_TAB_GROUP,
};

/**
 * Per-actionType config for the tab-group confirmation card: which l10n
 * strings the confirm button uses, and which update type submitting it
 * dispatches. Add a new entry here to support another action type -
 * #renderTabGroupConfirmation itself shouldn't need to change.
 */
const TAB_GROUP_ACTION_CONFIG = {
  group_tabs: {
    confirmActionL10n: {
      disabled: "smart-window-confirm-group-tab",
      enabled: "smart-window-confirm-group-tabs",
    },
    updateType: UI_UPDATE_TYPES.CONFIRM_TAB_GROUP_SELECTION,
  },
  open_tabs: {
    confirmActionL10n: {
      disabled: "smart-window-confirm-open-tab",
      enabled: "smart-window-confirm-open-tabs",
    },
    updateType: UI_UPDATE_TYPES.CONFIRM_OPEN_AND_GROUP_TABS_SELECTION,
  },
};

/**
 * Mapping of cancelled UI types to their retry message L10n IDs
 */
const RETRY_MESSAGE_L10N_MAP = {
  "website-confirmation": "smartwindow-nl-retry-message",
  "tab-group-confirmation": "smartwindow-nl-retry-group-tabs-message",
};

/**
 * A custom element for managing AI Chat Content
 */
export class AIChatContent extends MozLitElement {
  static properties = {
    assistantIsLoading: { type: Boolean },
    assistantResponseAnnouncement: { type: String, state: true },
    conversationState: { type: Array },
    followUpSuggestions: { type: Array },
    errorObj: { type: Object },
    isSearching: { type: Boolean },
    tokens: { type: Object },
    seenUrls: { type: Object },
    conversationId: { type: String },
  };

  #lastScrollReq = null;
  #overflowObserver = null;
  #overflowRafId = null;
  #scrollHandler = null;
  #jumpClickHandler = null;
  #scrollRafId = null;
  #removeClientErrorListeners = null;
  #pendingAnnouncementMessageId = null;
  #scrollPositions = new Map();
  #actionResultExpandState = new Map();
  #uiRenderMap = null;
  // English fallback until connectedCallback()'s l10n lookup resolves.
  #defaultTabGroupLabel = "Tab Group";
  // Identity of the agent-monitor "create" card we have already auto-focused
  #focusedMonitorCardId = null;

  constructor() {
    super();
    this.assistantIsLoading = false;
    this.assistantResponseAnnouncement = "";
    this.conversationState = [];
    this.followUpSuggestions = [];
    this.errorObj = null;
    this.isSearching = false;

    // Initialize UI render map
    this.#uiRenderMap = {
      [UI_TYPES.AITAB]: msg => this.#renderAITab(msg),
      [UI_TYPES.TAB_GROUP_CONFIRMATION]: msg =>
        this.#renderTabGroupConfirmation(msg),
      [UI_TYPES.WEBSITE_CONFIRMATION]: msg =>
        this.#renderWebsiteConfirmation(msg),
      [UI_TYPES.AI_ACTION_RESULT]: msg => this.#renderActionResult(msg),
      [UI_TYPES.CANCELLED_COMPONENT]: () => this.#renderCancelledComponent(),
      [UI_TYPES.RETRY_COMPONENT]: msg => this.#renderRetryComponent(msg),
      [UI_TYPES.AGENT_MONITOR]: msg => this.#renderAgentMonitorComponent(msg),
    };

    /**
     * The set of URLs that have been seen by the conversation. Used for determining
     * if a URL will be unfurled or not.
     *
     * @type {Set<string>}
     */
    this.seenUrls = new Set();

    /**
     * The current conversationId for the seenUrls.
     *
     * @type {null | string}
     */
    this.conversationId = null;
  }

  connectedCallback() {
    super.connectedCallback();
    this.#initEventListeners();

    this.dispatchEvent(
      new CustomEvent("AIChatContent:Ready", { bubbles: true })
    );
    this.#initFooterActionListeners();
    this.#initOverflowObserver();
    this.#initScrollListener();
    this.#removeClientErrorListeners = installClientErrorListeners(
      window,
      (error, source) => dispatchClientError(this, error, source)
    );
    this.#scrollPositions.clear();

    this.ownerDocument.l10n
      .formatValue("smart-window-default-tab-group-label")
      .then(label => {
        if (label) {
          this.#defaultTabGroupLabel = label;
          this.requestUpdate();
        }
      })
      .catch(() => {});
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.#overflowObserver?.disconnect();
    this.#overflowObserver = null;
    if (this.#overflowRafId) {
      cancelAnimationFrame(this.#overflowRafId);
      this.#overflowRafId = null;
    }
    this.#teardownScrollListener();
    this.#removeClientErrorListeners?.();
    this.#removeClientErrorListeners = null;
  }

  updated(changedProperties) {
    super.updated(changedProperties);
    this.#maybeFocusAgentMonitorCard();
  }

  /**
   * Give keyboard users an immediate focus target when a new agent-monitor
   * "create" card is seeded into the chat (e.g. from a /watch command)
   */
  #maybeFocusAgentMonitorCard() {
    const card = this.conversationState.findLast(
      msg =>
        msg?.toolUIData?.uiType === UI_TYPES.AGENT_MONITOR &&
        (msg.toolUIData.properties?.mode ?? "create") === "create" &&
        !msg.isRestored
    );
    if (!card) {
      return;
    }
    const cardId = card.toolUIData.toolCallId ?? card.messageId;
    if (cardId == null || this.#focusedMonitorCardId === cardId) {
      return;
    }
    this.#focusedMonitorCardId = cardId;
    this.#focusMonitorNameInput(cardId);
  }

  async #focusMonitorNameInput(cardId) {
    const item = this.shadowRoot?.querySelector(
      `agent-monitor-item[data-tool-call-id="${CSS.escape(cardId)}"]`
    );
    if (!item) {
      return;
    }
    await item.updateComplete;
    // The card can be submitted or cancelled while it renders.
    if (item.isConnected && item.getAttribute("mode") === "create") {
      item.focusName();
    }
  }

  #dispatchAction(action, detail) {
    this.dispatchEvent(
      new CustomEvent("AIChatContent:DispatchAction", {
        bubbles: true,
        composed: true,
        detail: {
          action,
          ...(detail ?? {}),
        },
      })
    );
  }

  // ai-window sends its mode (sidebar/fullpage) over the actor once the content
  // is ready; reflect it as an attribute so styles can key off it.
  #handleSetMode(event) {
    const mode = event.detail?.mode;
    if (mode) {
      this.setAttribute("mode", mode);
    }
  }

  /**
   * Initialize event listeners for AI chat content events
   */
  #initEventListeners() {
    this.addEventListener(
      "aiChatContentActor:message",
      this.messageEvent.bind(this)
    );

    this.addEventListener(
      "aiChatContentActor:truncate",
      this.truncateEvent.bind(this)
    );

    this.addEventListener(
      "aiChatContentActor:remove-applied-memory",
      this.removeAppliedMemoryEvent.bind(this)
    );

    this.addEventListener(
      "aiChatContentActor:seen-urls",
      this.#handleSeenUrls.bind(this)
    );

    this.addEventListener(
      "aiChatContentActor:set-generating",
      this.#handleSetGenerating.bind(this)
    );

    this.addEventListener(
      "aiChatContentActor:assets-ready",
      this.#handleAssetsReady.bind(this)
    );

    this.addEventListener(
      "aiChatContentActor:set-mode",
      this.#handleSetMode.bind(this)
    );

    this.addEventListener(
      "aiChatError:retry-message",
      this.retryUserMessageAfterError.bind(this)
    );

    this.addEventListener(
      "SmartWindowPrompt:prompt-selected",
      this.#onFollowUpSelected.bind(this)
    );

    this.addEventListener(
      "aiChatError:new-chat",
      this.openNewChatAfterError.bind(this)
    );

    this.addEventListener(
      "aiChatError:sign-in",
      this.openAccountSignInAfterError.bind(this)
    );

    this.addEventListener("ai-chat-message:complete", event => {
      const { messageId, text } = event.detail ?? {};
      if (messageId && messageId === this.#pendingAnnouncementMessageId) {
        this.#pendingAnnouncementMessageId = null;
        this.assistantResponseAnnouncement = text || "";
      }
    });
  }

  /**
   * Initialize event listeners for footer actions (retry, copy, etc.)
   * emitted by child components.
   */

  #initFooterActionListeners() {
    this.addEventListener("copy-message", event => {
      const { messageId } = event.detail ?? {};
      const text = this.#getAssistantMessageCopyText(messageId);
      this.#dispatchAction("copy", { messageId, text });
    });

    this.addEventListener("copy-table", event => {
      const { messageId, lineRange } = event.detail ?? {};
      const text = this.#getAssistantMessageBody(messageId);
      const tableMarkdown = text
        .split("\n")
        .slice(lineRange[0], lineRange[1])
        .join("\n");
      this.#dispatchAction("copy-table", { messageId, text: tableMarkdown });
    });

    this.addEventListener("retry-message", event => {
      this.#dispatchAction("retry", event.detail);
    });

    this.addEventListener("retry-without-memories", event => {
      this.#dispatchAction("retry-without-memories", event.detail);
    });

    this.addEventListener("remove-applied-memory", event => {
      this.#dispatchAction("remove-applied-memory", event.detail);
    });

    this.addEventListener("toggle-applied-memories", event => {
      this.#dispatchAction("toggle-applied-memories", event.detail);
    });

    this.addEventListener("manage-memories", event => {
      this.#dispatchAction("manage-memories", event.detail);
    });

    this.addEventListener("open-memories-learn-more", event => {
      this.#dispatchAction("open-memories-learn-more", event.detail);
    });

    this.addEventListener("thumbs-up", event => {
      this.#dispatchAction("thumbs-up", event.detail);
    });

    this.addEventListener("thumbs-down", event => {
      this.#dispatchAction("thumbs-down", event.detail);
    });

    this.addEventListener("shown", this.#onPanelShown);
  }

  // panel-list positions itself against the viewport and is unaware of the
  // chrome chat header. If it opens into the header area, it can be covered and
  // intercept header clicks, so shift it below the header.
  #onPanelShown = event => {
    const panel = event.composedPath()[0];
    if (panel?.localName !== "panel-list") {
      return;
    }

    const bounds = panel.getBoundingClientRect();
    const overlap = this.#topSpacing() - bounds.top;
    if (overlap <= 0) {
      return;
    }

    panel.style.top = `${(parseFloat(panel.style.top) || 0) + overlap}px`;
    const height =
      panel.getAttribute("valign") === "top"
        ? bounds.height - overlap
        : window.innerHeight - (bounds.top + overlap);
    panel.style.maxHeight = `${Math.max(0, height)}px`;
  };

  // How much of the viewport's top edge the floating chat header covers, read
  // off the space the chat list already reserves for it.
  #topSpacing() {
    const innerWrapper = this.shadowRoot?.querySelector(".chat-inner-wrapper");
    if (!innerWrapper) {
      return 0;
    }
    return parseFloat(getComputedStyle(innerWrapper).paddingBlockStart) || 0;
  }

  #initOverflowObserver() {
    this.#overflowObserver = new ResizeObserver(() =>
      this.#updateOverflowState()
    );
    this.updateComplete.then(() => {
      this.#overflowObserver.observe(
        this.shadowRoot.querySelector(".chat-inner-wrapper")
      );
    });
  }

  /**
   * Toggle the scroll container's `overflowing` attribute (which drives the
   * scroll fades) to match whether it actually scrolls, and recompute the
   * jump-to-bottom button, which no scroll event covers when the change came
   * from content rather than from scrolling.
   */
  #updateOverflowState() {
    const wrapper = this.#wrapper;
    const innerWrapper = this.shadowRoot?.querySelector(".chat-inner-wrapper");

    if (!wrapper || !innerWrapper) {
      return;
    }

    const hasContent = innerWrapper.children.length;
    // Use a 10px threshold to avoid false positives from layout differences
    const thresholdPadding = 10;

    wrapper.toggleAttribute(
      "overflowing",
      hasContent &&
        wrapper.scrollHeight > wrapper.clientHeight + thresholdPadding
    );

    this.#updateJumpButtonState();
  }

  get #wrapper() {
    return this.shadowRoot?.querySelector(".chat-content-wrapper");
  }

  get #jumpButton() {
    return this.shadowRoot?.querySelector(".jump-to-bottom-button");
  }

  #initScrollListener() {
    this.updateComplete.then(() => {
      if (!this.isConnected) {
        return;
      }
      const wrapper = this.#wrapper;
      const jumpButton = this.#jumpButton;
      if (!wrapper || !jumpButton) {
        return;
      }
      this.#scrollHandler = () => {
        if (this.#scrollRafId) {
          return;
        }
        this.#scrollRafId = requestAnimationFrame(() => {
          this.#scrollRafId = null;
          this.#updateJumpButtonState();
        });
      };
      this.#jumpClickHandler = () => {
        wrapper.scrollTop = wrapper.scrollHeight;
      };
      wrapper.addEventListener("scroll", this.#scrollHandler);
      jumpButton.addEventListener("click", this.#jumpClickHandler);
    });
  }

  #updateJumpButtonState() {
    const wrapper = this.#wrapper;
    const jumpButton = this.#jumpButton;
    if (!wrapper || !jumpButton) {
      return;
    }
    const distanceFromBottom =
      wrapper.scrollHeight - wrapper.scrollTop - wrapper.clientHeight;
    const threshold = wrapper.clientHeight * 0.5;
    const show = distanceFromBottom > threshold;
    const atBottom = distanceFromBottom < 1;
    if (jumpButton.hasAttribute("visible") !== show) {
      jumpButton.toggleAttribute("visible", show);
      jumpButton.toggleAttribute("disabled", !show);
    }
    if (wrapper.hasAttribute("scrolled-to-bottom") !== atBottom) {
      wrapper.toggleAttribute("scrolled-to-bottom", atBottom);
    }
  }

  #teardownScrollListener() {
    if (this.#scrollRafId) {
      cancelAnimationFrame(this.#scrollRafId);
      this.#scrollRafId = null;
    }
    if (this.#scrollHandler) {
      this.#wrapper?.removeEventListener("scroll", this.#scrollHandler);
      this.#scrollHandler = null;
    }
    if (this.#jumpClickHandler) {
      this.#jumpButton?.removeEventListener("click", this.#jumpClickHandler);
      this.#jumpClickHandler = null;
    }
  }

  #getAssistantMessageBody(messageId) {
    if (!messageId) {
      return "";
    }

    const msg = this.conversationState.find(m => {
      return m?.role === "assistant" && m?.messageId === messageId;
    });

    return msg?.body ?? "";
  }

  /**
   * The text the copy button copies for an assistant message. A localized
   * message copies the text the user sees, since its body holds text meant
   * only for the model.
   *
   * @param {string} messageId - Id of the assistant message
   * @returns {string}
   */
  #getAssistantMessageCopyText(messageId) {
    const msg = this.conversationState.find(
      m => m?.role === "assistant" && m?.messageId === messageId
    );
    if (!msg?.messageL10n) {
      return this.#getAssistantMessageBody(messageId);
    }
    const messageEl = this.shadowRoot?.querySelector(
      `ai-chat-message[data-message-id="${CSS.escape(messageId)}"]`
    );
    return messageEl?.localizedText ?? "";
  }

  #onFollowUpSelected(event) {
    event.stopPropagation();
    this.followUpSuggestions = [];
    this.dispatchEvent(
      new CustomEvent("AIChatContent:DispatchFollowUp", {
        detail: { text: event.detail.text },
        bubbles: true,
      })
    );
  }

  /**
   * Add new seen URLs to the current conversation.
   *
   * @param {object} event
   * @param {object} event.detail
   * @param {string} event.detail.conversationId
   * @param {Set<string>} event.detail.seenUrls
   */
  #handleSeenUrls({ detail: { conversationId, seenUrls } }) {
    if (this.conversationId == conversationId) {
      this.seenUrls = this.seenUrls.union(seenUrls);
    } else {
      this.conversationId = conversationId;
      this.seenUrls = seenUrls;
    }
  }

  messageEvent(event) {
    const message = event.detail;

    // Only bail on shapes that can't be handled at all (null, non-object).
    // Unknown roles fall through to the switch's default arm below, so adding
    // a new role doesn't require touching telemetry.
    if (!message || typeof message !== "object") {
      dispatchClientError(this, INVALID_MESSAGE_DATA, "message-data");
      return;
    }

    if (message?.content?.isError) {
      this.handleErrorEvent(message?.content);
      return;
    }

    this.errorObj = null;

    switch (message.role) {
      case "loading":
        this.#checkConversationState(message);
        this.handleLoadingEvent(event);
        break;
      case "assistant":
        this.#checkConversationState(message);
        this.handleAIResponseEvent(event);
        break;
      case "tool":
        this.#checkConversationState(message);
        this.handleToolMessageEvent(event);
        break;
      case "user":
        this.#checkConversationState(message);
        this.handleUserPromptEvent(event);
        break;
      case "assistant-message-complete":
        this.#setMessageComplete(message);
        break;
      case "restored-all-messages-in-a-conversation":
        this.#restoreChatScrollPosition(message.convId);
        break;
      // Used to clear the conversation state via side effects ( new conv id )
      case "clear-conversation":
        this.#checkConversationState(message);
    }
  }

  #handleSetGenerating(event) {
    this.assistantIsLoading = !!event.detail?.isGenerating;
    if (!this.assistantIsLoading) {
      this.isSearching = false;
    }
    this.requestUpdate();
  }

  /**
   * Apply the history assets resolved by the parent (page thumbnail and favicon
   * status) to a message's history results. Reassigns a fresh
   * historyResultsMap so the ai-chat-message sees a changed reference and
   * recalculates its grid loading state.
   *
   * @param {CustomEvent} event
   * @param {string} event.detail.messageId
   * @param {Array<{url: string, image: string|null, hasFavicon: boolean}>} event.detail.images
   */
  #handleAssetsReady(event) {
    const { messageId, images } = event.detail ?? {};
    if (!messageId || !images?.length) {
      return;
    }

    const entry = this.conversationState.find(
      msg => msg?.messageId === messageId
    );

    if (!entry) {
      return;
    }

    let changed = false;

    if (entry.historyResultsMap) {
      for (const { url, image, hasFavicon } of images) {
        const record = entry.historyResultsMap.get(url);
        if (!record) {
          continue;
        }
        if (record.image !== image) {
          record.image = image;
          changed = true;
        }
        if (record.hasFavicon !== hasFavicon) {
          record.hasFavicon = hasFavicon;
          changed = true;
        }
      }
      if (changed) {
        // New Map reference so Lit sees a changed prop and ai-chat-message
        // re-renders, in-place mutations above alone won't trigger a change
        entry.historyResultsMap = new Map(entry.historyResultsMap);
      }
    }

    if (entry.citations?.length) {
      const faviconByUrl = new Map(
        images.map(({ url, hasFavicon }) => [url, hasFavicon])
      );
      let citationsChanged = false;
      const citations = entry.citations.map(citation => {
        if (!faviconByUrl.has(citation.url)) {
          return citation;
        }
        const hasFavicon = faviconByUrl.get(citation.url);
        // TODO (Bug 2060835): Citations get a default favicon when Places don’t
        // already have one stored for the URL.
        if (citation.hasFavicon === hasFavicon) {
          return citation;
        }
        citationsChanged = true;
        return { ...citation, hasFavicon };
      });
      if (citationsChanged) {
        entry.citations = citations;
        changed = true;
      }
    }

    if (!changed) {
      return;
    }

    this.requestUpdate();
  }

  /**
   * Ask the parent to resolve favicon availability for citation URLs.
   *
   * @param {string} messageId
   * @param {Array<{url: string}>} citations
   */
  #requestCitationFavicons(messageId, citations) {
    const items = citations
      .filter(citation => citation?.url && citation.hasFavicon === undefined)
      .map(citation => ({ url: citation.url }));

    if (!items.length) {
      return;
    }

    this.dispatchEvent(
      new CustomEvent("AIChatContent:RequestAssets", {
        bubbles: true,
        composed: true,
        detail: {
          conversationId: this.conversationId,
          messageId,
          items,
        },
      })
    );
  }

  async #restoreChatScrollPosition(convId) {
    await this.updateComplete;

    // Making sure we check if convId hasn't changed while we awaited
    const lastMessage = this.conversationState.findLast(
      m => m.convId === convId
    );
    if (!lastMessage) {
      return;
    }

    // Wait a frame to ensure the footer and its children are visible
    await new Promise(r =>
      requestAnimationFrame(() => requestAnimationFrame(r))
    );

    const wrapper = this.#wrapper;
    if (!wrapper) {
      return;
    }

    const savedPosition = this.#scrollPositions.get(convId);
    if (savedPosition?.contentHeight) {
      this.shadowRoot
        ?.querySelector(".chat-inner-wrapper")
        ?.style.setProperty("--content-height", savedPosition.contentHeight);
    }

    const goToBottom =
      !savedPosition ||
      savedPosition.wasAtBottom ||
      savedPosition.wasWaitingForResponse;

    if (!goToBottom) {
      wrapper.scrollTo({
        top: savedPosition.scrollTop,
        behavior: "instant",
      });
      return;
    }

    const lastChild = this.shadowRoot.querySelector(
      ".chat-inner-wrapper"
    )?.lastElementChild;
    if (lastChild) {
      lastChild.scrollIntoView({ block: "end", behavior: "instant" });
      return;
    }
    wrapper.scrollTo({ top: wrapper.scrollHeight, behavior: "instant" });
  }

  get #kitMention() {
    return this.shadowRoot?.querySelector("kit-mention");
  }

  #setMessageComplete(message) {
    const messageId = message.content?.id;
    if (!messageId) {
      return;
    }

    const assistantLastMessage = this.conversationState.findLast(
      msg => msg?.messageId === messageId
    );

    if (assistantLastMessage) {
      assistantLastMessage.isLastChunk = true;
      // Freeze the message's own snapshot from the completion event so it
      // matches the URLs it lists; later searches won't alter it.
      const records = message.historyResults;
      if (records?.length) {
        assistantLastMessage.historyResultsMap = new Map(
          records.map(record => [record.url, record])
        );
      }
      if (message.citations?.length) {
        assistantLastMessage.citations = message.citations;
        this.#requestCitationFavicons(messageId, message.citations);
      }
    }

    this.#pendingAnnouncementMessageId = messageId;
    this.assistantResponseAnnouncement = "";
    this.requestUpdate();
  }

  #clearAssistantResponseAnnouncement() {
    this.#pendingAnnouncementMessageId = null;
    this.assistantResponseAnnouncement = "";
  }

  /**
   * Check if conversationState needs to be cleared
   *
   * @param {ChatMessage} message
   */
  #checkConversationState(message) {
    // Use find/findLast instead of at(0)/at(-1) because
    // conversationState is a sparse array indexed by ordinal and
    // at() can land on a hole (undefined) after truncation.
    const lastMessage = this.conversationState.findLast(m => m);
    const firstMessage = this.conversationState.find(m => m);
    const isReloadingSameConvo =
      firstMessage &&
      firstMessage.convId === message.convId &&
      firstMessage.ordinal === message.ordinal;
    const convIdChanged = message.convId !== lastMessage?.convId;

    if (convIdChanged && lastMessage?.convId && this.#wrapper) {
      this.saveScrollPosition(lastMessage, this.#wrapper);
    }

    // If the conversation ID has changed, reset the conversation state
    if (convIdChanged || isReloadingSameConvo) {
      this.conversationState = [];
      this.followUpSuggestions = [];
      this.#clearAssistantResponseAnnouncement();
      this.isSearching = false;
      this.#kitMention?.reset();
      if (convIdChanged) {
        this.shadowRoot
          ?.querySelector(".chat-inner-wrapper")
          ?.style.removeProperty("--content-height");
      }
      this.requestUpdate();
    }
  }

  /* Saves the scroll position when we switch tabs */
  saveScrollPosition(lastMessage, wrapper) {
    const innerWrapper = this.shadowRoot.querySelector(".chat-inner-wrapper");

    // if element is near the bottom (50px or less)
    // we scroll all the way to the end as default
    let wasAtBottom = true;
    const lastChild = innerWrapper?.lastElementChild;
    if (lastChild) {
      const lastChildRect = lastChild.getBoundingClientRect();
      const wrapperRect = wrapper.getBoundingClientRect();
      wasAtBottom = lastChildRect.bottom <= wrapperRect.bottom + 50;
    }

    const wasWaitingForResponse =
      this.assistantIsLoading ||
      this.isSearching ||
      lastMessage.role !== "assistant" ||
      !lastMessage.isLastChunk;

    this.#scrollPositions.set(lastMessage.convId, {
      scrollTop: wrapper.scrollTop,
      wasAtBottom,
      wasWaitingForResponse,
      contentHeight:
        innerWrapper?.style.getPropertyValue("--content-height") || null,
    });
  }

  handleLoadingEvent(event) {
    const { isSearching } = event.detail;
    this.#clearAssistantResponseAnnouncement();
    this.isSearching = !!isSearching;
    this.requestUpdate();
  }

  handleErrorEvent(error) {
    this.isSearching = false;
    this.errorObj = error;
    this.requestUpdate();
  }

  /**
   * Handle tool role messages produced when a toolcall completes
   *
   * @param {CustomEvent} event
   */
  handleToolMessageEvent(event) {
    const { convId, ordinal, content, actionLog } = event.detail ?? {};

    if (!content?.name || !actionLog?.uiType) {
      return;
    }

    // uiTypes that this conversation knows how to render as tool UI
    const ACCEPTED_UI_TYPES = [UI_TYPES.ACTION_LOG];
    if (!ACCEPTED_UI_TYPES.includes(actionLog.uiType)) {
      return;
    }

    this.conversationState[ordinal] = {
      role: "tool",
      uiType: actionLog.uiType,
      convId,
      ordinal,
      toolCallId: content.tool_call_id,
      toolName: content.name,
      pendingLabel: actionLog.pendingLabel,
      row: actionLog.row,
    };

    this.requestUpdate();
  }

  /**
   *  Handle user prompt events
   *
   * @param {CustomEvent} event - The custom event containing the user prompt
   */

  handleUserPromptEvent(event) {
    this.followUpSuggestions = [];
    const { convId, content, ordinal, isPreviousMessage } = event.detail;
    if (!isPreviousMessage) {
      this.#clearAssistantResponseAnnouncement();
    }
    this.conversationState[ordinal] = {
      role: "user",
      body: content.body,
      contextMentions: content.contextMentions,
      pageUrl: content.contextPageUrl ?? null,
      convId,
      ordinal,
    };
    this.requestUpdate();
    if (!isPreviousMessage) {
      this.#scrollUserMessageIntoView();
    }
  }

  retryUserMessageAfterError() {
    const lastMessage = this.conversationState.findLast(m => m);

    if (!lastMessage) {
      return;
    }

    this.#dispatchAction("retry-after-error", {
      ...lastMessage,
      content: {
        type: "text",
        body: lastMessage.body,
        contextMentions: lastMessage.contextMentions,
      },
    });
  }

  #isAIResponseValid(content, toolUIData) {
    return (
      (typeof content?.body === "string" && content.body) ||
      !!content?.l10nId ||
      !!toolUIData
    );
  }

  /**
   * Handle AI response events
   *
   * @param {CustomEvent} event - The custom event containing the response
   */

  handleAIResponseEvent(event) {
    this.isSearching = false;

    const {
      convId,
      ordinal,
      id: messageId,
      content,
      memoriesApplied,
      showMemoriesCallout,
      webSearchQueries = [],
      followUpSuggestions = [],
      isPreviousMessage,
      toolUIData,
      toolUIDraft,
      kit,
      isRestored,
      historyResults = [],
      citations = [],
    } = event.detail;

    const hasRenderableResponse = this.#isAIResponseValid(content, toolUIData);
    const isToolUICleared = toolUIData === null;

    if (!hasRenderableResponse) {
      if (isToolUICleared) {
        this.conversationState = this.conversationState.filter(message => {
          const isMatchingMessage = message.messageId === messageId;
          const hasToolUI = !!message.toolUIData;
          const shouldRemoveMessage = isMatchingMessage && hasToolUI;
          return !shouldRemoveMessage;
        });
      }
      return;
    }

    // favor web search display over follow ups.
    this.followUpSuggestions = webSearchQueries.length
      ? []
      : followUpSuggestions.slice(0, FOLLOW_UP_QTY);

    const isLastChunk =
      !!isPreviousMessage || !!this.conversationState[ordinal]?.isLastChunk;

    // History results travel on the message, build this message's snapshot Map
    // from the records the parent dispatched, keyed by URL.
    const historyResultsMap = historyResults.length
      ? new Map(historyResults.map(record => [record.url, record]))
      : undefined;

    this.conversationState[ordinal] = {
      role: "assistant",
      convId,
      messageId,
      body: content.body,
      messageL10n: content.l10nId
        ? { id: content.l10nId, args: content.l10nArgs, link: content.link }
        : null,
      appliedMemories: memoriesApplied ?? [],
      showCallout: showMemoriesCallout ?? false,
      isLastChunk,
      toolUIData,
      toolUIDraft,
      historyResultsMap,
      citations,
      isRestored,
    };

    if (citations.length) {
      this.#requestCitationFavicons(messageId, citations);
    }

    if (kit && !isPreviousMessage) {
      this.#kitMention?.trigger({ value: kit, convId });
    }

    this.requestUpdate();
  }

  #scrollUserMessageIntoView() {
    let scrollReq = {};
    this.#lastScrollReq = scrollReq;
    this.updateComplete.then(() => {
      const msgs = this.shadowRoot?.querySelectorAll(".chat-bubble-user");
      if (!msgs?.length) {
        return;
      }
      let lastMessage = msgs[msgs.length - 1];
      requestAnimationFrame(() => {
        if (scrollReq !== this.#lastScrollReq) {
          return;
        }
        let elTop = lastMessage.offsetTop;
        lastMessage.parentNode.style.setProperty(
          "--content-height",
          `calc(${elTop}px + 100% - var(--smart-window-top-spacing-chat))`
        );

        requestAnimationFrame(() => {
          if (scrollReq == this.#lastScrollReq) {
            lastMessage.scrollIntoView({ block: "start" });
          }
        });
      });
    });
  }

  truncateEvent(event) {
    const { messageId } = event.detail ?? {};
    if (!messageId) {
      return;
    }

    const idx = this.conversationState.findIndex(m => {
      return m?.role === "assistant" && m?.messageId === messageId;
    });

    if (idx === -1) {
      return;
    }

    this.conversationState = this.conversationState.slice(0, idx);
    this.requestUpdate();
  }

  removeAppliedMemoryEvent(event) {
    const { messageId, memoryId } = event.detail ?? {};
    const msg = this.conversationState.find(m => {
      return m?.role === "assistant" && m?.messageId === messageId;
    });

    msg.appliedMemories = msg.appliedMemories.filter(
      memory => memory?.id !== memoryId
    );
    this.requestUpdate();
  }

  openNewChatAfterError() {
    const event = new CustomEvent("AIChatContent:DispatchNewChat", {
      bubbles: true,
      composed: true,
    });
    this.dispatchEvent(event);
  }

  /**
   * Returns the chips to display for a message, hiding the tabs expanded from
   * a tab group and suppressing the current-tab chip when the page context
   * hasn't changed since the previous user message.
   *
   * @param {object} msg - A conversationState entry.
   * @param {string|null} lastContextPageUrl - The page URL of the preceding
   * user message, or undefined if there is none.
   * @returns {ContextWebsite[]}
   */
  #getVisibleChips(msg, lastContextPageUrl) {
    // If this message is on the same page as the previous message,
    // hide the page URL chip to avoid showing duplicate page context
    if (!msg || msg.role !== "user" || !msg.contextMentions?.length) {
      return [];
    }
    const chips = msg.contextMentions.filter(chip => !isTabGroupMember(chip));
    const currentPageUrl = msg.pageUrl;
    const shouldHideDuplicatePageChip =
      currentPageUrl && currentPageUrl === lastContextPageUrl;
    if (shouldHideDuplicatePageChip) {
      return chips.filter(chip => URL.parse(chip.url)?.href !== currentPageUrl);
    }
    return chips;
  }

  openAccountSignInAfterError() {
    const event = new CustomEvent("AIChatContent:AccountSignIn", {
      bubbles: true,
      composed: true,
    });
    this.dispatchEvent(event);
  }

  #buildTabsRow(labelL10nId, tabs) {
    return tabs.length
      ? [
          {
            labelL10nId,
            items: tabs.map(tab => ({ url: tab.url, label: tab.title })),
          },
        ]
      : [];
  }

  #getCloseTabsData(confirmedData) {
    const selectedTabs = confirmedData.selectedTabs || [];
    const tabCount = selectedTabs.length;

    const rows = this.#buildTabsRow(
      "smart-window-closed-tabs-row-label",
      selectedTabs
    );

    return {
      labelL10nId: "smart-window-closed-tabs-label",
      labelL10nArgs: { count: tabCount },
      summaryL10nId: "smart-window-closed-tabs-summary",
      summaryL10nArgs: { count: tabCount },
      rows,
    };
  }

  #getRestoreTabsData(originalClosedTabs) {
    const restoredCount = originalClosedTabs.length;
    // Format rows to show both closed and restored tabs
    const rows = [
      {
        labelL10nId: "smart-window-closed-tabs-row-label",
        items: originalClosedTabs.map(({ url, title }) => ({
          url,
          label: title,
        })),
      },
      {
        labelL10nId: "smart-window-restored-row-label",
        labelL10nArgs: { count: restoredCount },
        // Design opted out of showing items here.
      },
    ];

    return {
      labelL10nId: "smart-window-closed-and-restored-label",
      summaryL10nId: "smart-window-restore-success-summary",
      summaryL10nArgs: { count: restoredCount },
      rows,
    };
  }

  #getGroupTabsData(confirmedData) {
    const selectedTabs = confirmedData.selectedTabs || [];
    const group = confirmedData.group || {};
    // The group can also hold the chat tab, which the user never picked, so
    // report the size of the group rather than the size of their selection.
    const tabCount = group.tabCount ?? selectedTabs.length;

    const rows = this.#buildTabsRow(
      "smart-window-grouped-tabs-row-label",
      selectedTabs
    );

    return {
      labelL10nId: "smart-window-grouped-tabs-label",
      labelL10nArgs: { count: tabCount },
      summaryL10nId: "smart-window-grouped-tabs-summary",
      summaryL10nArgs: {
        count: tabCount,
        label: group.label || this.#defaultTabGroupLabel,
      },
      rows,
    };
  }

  #getSwitchedTabData(tab) {
    return {
      labelL10nId: "smart-window-switched-tab-label",
      summaryL10nId: "smart-window-switched-tab-summary",
      summaryL10nArgs: { title: tab?.title || tab?.url || "" },
      rows: [],
    };
  }

  #getOpenTabsData(confirmedData) {
    // A single already-open tab was switched to, not opened - no group,
    // no "opened" wording.
    if (confirmedData.switched) {
      return this.#getSwitchedTabData(confirmedData.selectedTabs?.[0]);
    }

    const selectedTabs = confirmedData.selectedTabs || [];
    const tabCount = selectedTabs.length;

    // Every selected tab was already open - nothing was actually opened,
    // so this reads the same as a plain group_tabs result.
    if (tabCount && confirmedData.mergedCount === tabCount) {
      return this.#getGroupTabsData(confirmedData);
    }

    const group = confirmedData.group || {};
    // A tab group is only created for 2+ tabs (see
    // ToolUI#handleOpenAndGroupTabsSelection) - group.label is only ever
    // set in that case, never for a single opened tab.
    const hasGroup = !!group.label;

    const rows = this.#buildTabsRow(
      "smart-window-opened-tabs-row-label",
      selectedTabs
    );

    return {
      labelL10nId: "smart-window-opened-tabs-label",
      labelL10nArgs: { count: tabCount },
      summaryL10nId: hasGroup
        ? "smart-window-opened-tabs-summary-group"
        : "smart-window-opened-tabs-summary-single",
      summaryL10nArgs: hasGroup
        ? { count: tabCount, label: group.label || this.#defaultTabGroupLabel }
        : { count: tabCount },
      rows,
    };
  }

  #getUngroupedTabsData(originalGroupedTabs) {
    const ungroupedCount = originalGroupedTabs.length;
    // Format rows to show both grouped and ungrouped tabs
    const rows = [
      {
        labelL10nId: "smart-window-grouped-tabs-row-label",
        labelL10nArgs: {},
        items: originalGroupedTabs.map(({ url, title }) => ({
          url,
          label: title,
        })),
      },
      {
        labelL10nId: "smart-window-ungrouped-row-label",
        labelL10nArgs: { count: ungroupedCount },
        // Design opted out of showing items here, similar to restored tabs
        items: [],
      },
    ];

    return {
      labelL10nId: "smart-window-grouped-and-ungrouped-label",
      summaryL10nId: "smart-window-ungroup-success-summary",
      summaryL10nArgs: { count: ungroupedCount },
      rows,
    };
  }

  #getActionResultData(confirmedData, wasRestored) {
    const actionType = confirmedData.actionType;

    if (!actionType) {
      return null;
    }

    const methodMap = {
      group_tabs: wasRestored
        ? () =>
            this.#getUngroupedTabsData(confirmedData.originalGroupedTabs || [])
        : () => this.#getGroupTabsData(confirmedData),
      close_tabs: wasRestored
        ? () => this.#getRestoreTabsData(confirmedData.originalClosedTabs || [])
        : () => this.#getCloseTabsData(confirmedData),
      // No wasRestored branch yet - open_tabs has no undo support (see
      // ACTION_TYPE_TO_UNDO_UPDATE_TYPE above).
      open_tabs: () => this.#getOpenTabsData(confirmedData),
    };

    const method = methodMap[actionType];
    return method ? method() : null;
  }

  /**
   * Render a turn's tool calls as a single grouped action log container
   *
   * @param {Array<object>} toolMsgs - one entry per tool call this turn
   * @param {boolean} isComplete - whether the turn has finished
   * @param {number} groupIndex - this group's index in the render items
   */
  #renderActionLogGroup(toolMsgs, isComplete, groupIndex) {
    const finalMessage = {
      l10nId: "action-log-completed-steps",
      l10nArgs: { count: toolMsgs.length },
    };
    const summary = isComplete
      ? finalMessage
      : toolMsgs[toolMsgs.length - 1]?.pendingLabel;
    const key = `action-log:${toolMsgs[0]?.id ?? toolMsgs[0]?.messageId ?? groupIndex}`;
    return html`
      <ai-action-result
        .labelL10nId=${summary?.l10nId}
        .labelL10nArgs=${summary?.l10nArgs}
        .labelLink=${summary?.link ?? null}
        .rows=${this.#buildGroupedActionLogRows(toolMsgs)}
        .isLoading=${!isComplete}
        .isExpanded=${this.#actionResultExpandState.get(key) ?? false}
        @action-result-toggle=${e =>
          this.#actionResultExpandState.set(key, !!e.detail?.isExpanded)}
      ></ai-action-result>
    `;
  }

  /**
   * Render the appropriate tool UI for a tool message, if applicable.
   *
   * @param {object} msg - A conversationState entry.
   * @returns {TemplateResult|nothing} - The rendered tool UI or nothing if not applicable.
   */
  #renderToolUI(msg) {
    if (!msg.toolUIData) {
      return nothing;
    }

    const toolUIData = msg.toolUIData;

    // For restored confirmation UIs, we want to show the rety component instead of the original confirmation UI.
    // We will store the original uiType in a property called cancelledUiType so we can use it to show a unique retry message.
    if (CONFIRMATION_UI_TYPES.includes(toolUIData.uiType) && msg.isRestored) {
      toolUIData.properties = {
        ...toolUIData.properties,
        cancelledUiType: toolUIData.uiType,
      };
      toolUIData.uiType = UI_TYPES.RETRY_COMPONENT;
    }

    const renderFn = this.#uiRenderMap[toolUIData.uiType];
    return renderFn ? renderFn(msg) : nothing;
  }

  #renderAITab(msg) {
    return html`<aitab-tool-ui
      .state=${msg.toolUIData.properties?.state ?? STATE_CHOOSE}
      .title=${msg.toolUIData.properties?.title ?? ""}
      .viewerURL=${msg.toolUIData.properties?.viewerURL ?? ""}
      @aitab-open-request=${event =>
        this.#dispatchToolUIUpdate({
          messageId: msg.messageId,
          toolCallId: msg.toolUIData.toolCallId,
          updateType: UI_UPDATE_TYPES.OPEN_AITAB,
          updateData: { openTarget: event.detail.openTarget },
        })}
    ></aitab-tool-ui>`;
  }

  #handleConfirmationSubmit = (event, messageId, toolCallId) => {
    this.#dispatchToolUIUpdate({
      messageId,
      toolCallId,
      updateType: UI_UPDATE_TYPES.CONFIRMATION_TAB_SELECTION,
      updateData: event.detail,
    });
  };

  #handleConfirmationClose = (event, messageId, toolCallId) => {
    this.#dispatchToolUIUpdate({
      messageId,
      toolCallId,
      updateType: UI_UPDATE_TYPES.CANCEL_TAB_SELECTION,
      updateData: event.detail,
    });
  };

  #handleConfirmationToggleAll = (event, messageId, toolCallId) => {
    this.#dispatchToolUIUpdate({
      messageId,
      toolCallId,
      updateType: UI_UPDATE_TYPES.TOGGLE_ALL_TAB_SELECTION,
      updateData: event.detail,
    });
  };

  #handleMonitorSubmit = (event, messageId, toolCallId) => {
    // The display card reuses submit for edits; create only happens from the
    // "create" card.
    const isEdit = event.detail?.mode === "display";
    this.#dispatchToolUIUpdate({
      messageId,
      toolCallId,
      updateType: isEdit
        ? UI_UPDATE_TYPES.UPDATE_WATCH
        : UI_UPDATE_TYPES.CREATE_WATCH,
      updateData: event.detail,
    });
  };

  #handleMonitorCancel = (event, messageId, toolCallId) => {
    /* TODO: Bug 2055336 - Add cancel monitor view */
    this.#dispatchToolUIUpdate({
      messageId,
      toolCallId,
      updateType: UI_UPDATE_TYPES.CANCEL_WATCH,
      updateData: event.detail,
    });
  };

  #handleMonitorAction = (event, messageId, toolCallId, updateType) => {
    this.#dispatchToolUIUpdate({
      messageId,
      toolCallId,
      updateType,
      updateData: event.detail,
    });
  };

  #renderAgentMonitorComponent(msg) {
    const { messageId, toolUIData, toolUIDraft } = msg;
    const toolCallId = toolUIData.toolCallId;
    return html`<agent-monitor-item
      data-tool-call-id=${toolCallId ?? messageId}
      mode=${toolUIData.properties?.mode ?? "create"}
      .agent=${toolUIData.properties?.agent}
      .draft=${toolUIDraft}
      @agent-monitor-item:draft-change=${event =>
        this.#handleMonitorAction(
          event,
          messageId,
          toolCallId,
          UI_UPDATE_TYPES.SAVE_WATCH_DRAFT
        )}
      @agent-monitor-item:submit=${event =>
        this.#handleMonitorSubmit(event, messageId, toolCallId)}
      @agent-monitor-item:cancel=${event =>
        this.#handleMonitorCancel(event, messageId, toolCallId)}
      @agent-monitor-item:delete=${event =>
        this.#handleMonitorAction(
          event,
          messageId,
          toolCallId,
          UI_UPDATE_TYPES.DELETE_WATCH
        )}
      @agent-monitor-item:pause=${event =>
        this.#handleMonitorAction(
          event,
          messageId,
          toolCallId,
          UI_UPDATE_TYPES.PAUSE_WATCH
        )}
      @agent-monitor-item:check-now=${event =>
        this.#handleMonitorAction(
          event,
          messageId,
          toolCallId,
          UI_UPDATE_TYPES.CHECK_WATCH
        )}
    ></agent-monitor-item>`;
  }

  #handleTabGroupActionSubmit = (event, messageId, toolCallId, updateType) => {
    this.#dispatchToolUIUpdate({
      messageId,
      toolCallId,
      updateType,
      updateData: event.detail,
    });
  };

  #renderTabGroupConfirmation(msg) {
    const toolUIData = msg.toolUIData;
    const actionType = toolUIData.properties?.actionType || "group_tabs";
    const { confirmActionL10n, updateType } =
      TAB_GROUP_ACTION_CONFIG[actionType] ?? TAB_GROUP_ACTION_CONFIG.group_tabs;

    return html`
      <ai-website-confirmation
        .tabs=${toolUIData.properties?.tabs || []}
        .tabGroupLabel=${toolUIData.properties?.tabGroupLabel}
        .confirmActionL10n=${confirmActionL10n}
        .actionType=${actionType}
        @ai-website-confirmation:submit=${event =>
          this.#handleTabGroupActionSubmit(
            event,
            msg.messageId,
            toolUIData.toolCallId,
            updateType
          )}
        @ai-website-confirmation:close=${event =>
          this.#handleConfirmationClose(
            event,
            msg.messageId,
            toolUIData.toolCallId
          )}
        @ai-website-confirmation:toggle-all=${event =>
          this.#handleConfirmationToggleAll(
            event,
            msg.messageId,
            toolUIData.toolCallId
          )}
      ></ai-website-confirmation>
    `;
  }

  #renderWebsiteConfirmation(msg) {
    const toolUIData = msg.toolUIData;

    return html`
      <ai-website-confirmation
        .tabs=${toolUIData.properties?.tabs || []}
        .confirmActionL10n=${{
          disabled: "smart-window-confirm-close-tab",
          enabled: "smart-window-confirm-close-tabs",
        }}
        .actionType=${"close_tabs"}
        @ai-website-confirmation:submit=${event =>
          this.#handleConfirmationSubmit(
            event,
            msg.messageId,
            toolUIData.toolCallId
          )}
        @ai-website-confirmation:close=${event =>
          this.#handleConfirmationClose(
            event,
            msg.messageId,
            toolUIData.toolCallId
          )}
        @ai-website-confirmation:toggle-all=${event =>
          this.#handleConfirmationToggleAll(
            event,
            msg.messageId,
            toolUIData.toolCallId
          )}
      ></ai-website-confirmation>
    `;
  }

  #renderActionResult(msg) {
    const { messageId, toolUIData } = msg;
    // Extract the confirmed selections and operation data
    const confirmedData = toolUIData.properties?.confirmedData || {};
    const wasRestored = confirmedData.wasRestored || false;
    const actionType = confirmedData.actionType;

    if (!actionType) {
      return nothing;
    }

    // Get the data object for the action result component
    const actionResultData = this.#getActionResultData(
      confirmedData,
      wasRestored
    );

    const undoOperationIds = confirmedData.operationIds ?? [];
    const undoUpdateType = ACTION_TYPE_TO_UNDO_UPDATE_TYPE[actionType];

    // Undo needs both something to undo (recorded operation ids) and a way
    // to undo it (an update type registered for this action type - e.g.
    // open_tabs has none, so it's never undoable regardless of ids).
    let canUndo = !wasRestored && !!undoOperationIds.length && !!undoUpdateType;
    // Override can undo if explicitly dismissed
    if (toolUIData.properties?.undoDismissed) {
      canUndo = false;
    }

    const onUndo =
      canUndo && undoUpdateType
        ? () =>
            this.#dispatchToolUIUpdate({
              messageId,
              toolCallId: toolUIData.toolCallId,
              updateType: undoUpdateType,
              updateData: {
                operationIds: undoOperationIds,
                selectedTabs: confirmedData.selectedTabs || [],
                actionTimestamp: confirmedData.actionTimestamp,
              },
            })
        : undefined;
    const tabs = this.#getConfirmationTabs(confirmedData, wasRestored);

    return html`
      <ai-action-confirmation
        .labelL10nId=${actionResultData.labelL10nId}
        .labelL10nArgs=${actionResultData.labelL10nArgs}
        .tabs=${tabs}
        .canUndo=${canUndo}
        .isExpanded=${this.#actionResultExpandState.get(messageId) ?? false}
        @action-confirmation-toggle=${e =>
          this.#actionResultExpandState.set(messageId, e.detail.isExpanded)}
        @action-confirmation-undo=${onUndo}
      ></ai-action-confirmation>
    `;
  }

  /**
   * Build the list of affected tabs.
   *
   * @param {object} confirmedData - The confirmed action data
   * @param {boolean} wasRestored - Whether the action has been undone
   * @returns {Array<TabSelectionData>}
   */
  #getConfirmationTabs(confirmedData, wasRestored) {
    let sourceTabs = confirmedData.originalClosedTabs;
    if (!wasRestored) {
      sourceTabs = confirmedData.selectedTabs;
    } else if (confirmedData.actionType === "group_tabs") {
      sourceTabs = confirmedData.originalGroupedTabs;
    }

    return (sourceTabs ?? []).map(tab => ({
      url: tab.url,
      title: tab.title,
      iconSrc: tab.iconSrc || (tab.url ? `page-icon:${tab.url}` : ""),
    }));
  }

  #renderCancelledComponent() {
    return html`<div data-l10n-id="smart-window-cancelled-label"></div>`;
  }

  #renderRetryComponent(msg) {
    const cancelledUiType = msg.toolUIData?.properties?.cancelledUiType;
    const retryL10nId =
      RETRY_MESSAGE_L10N_MAP[cancelledUiType] || "smartwindow-nl-retry-message";
    const toolUIData = msg.toolUIData;
    const originalPrompt = toolUIData.properties?.originalUserPrompt || "";

    return html`
      <div>
        <p data-l10n-id=${retryL10nId}></p>
        <moz-button
          class="tool-retry-button"
          @click=${() =>
            this.#handleRetryClick(
              msg.messageId,
              toolUIData.toolCallId,
              originalPrompt
            )}
          data-l10n-id="smartwindow-nl-retry-tool-button"
        ></moz-button>
      </div>
    `;
  }

  #handleRetryClick = (messageId, toolCallId, originalPrompt) => {
    this.#dispatchToolUIUpdate({
      messageId,
      toolCallId,
      updateType: UI_UPDATE_TYPES.RETRY_PROMPT,
      updateData: { prompt: originalPrompt },
    });
  };

  #dispatchToolUIUpdate(data) {
    this.dispatchEvent(
      new CustomEvent("AIChatContent:ToolUIUpdate", {
        bubbles: true,
        composed: true,
        detail: data,
      })
    );
  }

  #renderMessage(msg, chips) {
    if (!msg) {
      return nothing;
    }

    // Check if this is a retry component that should be rendered at the top
    const isRetryComponent =
      msg.toolUIData?.uiType === UI_TYPES.RETRY_COMPONENT;

    return html`<div class=${`chat-bubble chat-bubble-${msg.role}`}>
      ${chips?.length
        ? html`<website-chip-container
            class="chat-bubble-chips"
            shouldGroupChips
            .websites=${chips}
          ></website-chip-container>`
        : nothing}
      <div class="chat-bubble-inner">
        ${msg.role === "assistant" && isRetryComponent
          ? this.#renderToolUI(msg)
          : nothing}
        <ai-chat-message
          .message=${msg.body}
          .messageL10n=${msg.messageL10n}
          .role=${msg.role}
          .messageId=${msg.messageId}
          .complete=${msg.role === "assistant" && !!msg.isLastChunk}
          .conversationId=${this.conversationId}
          .seenUrls=${this.seenUrls}
          .historyResults=${msg.historyResultsMap}
        ></ai-chat-message>
        ${msg.role === "assistant" && msg.toolUIData && !isRetryComponent
          ? this.#renderToolUI(msg)
          : nothing}
        ${msg.role === "assistant" && msg.isLastChunk && msg.citations?.length
          ? html`<chat-assistant-citations
              .citations=${msg.citations}
            ></chat-assistant-citations>`
          : nothing}
        ${msg.role === "assistant" && msg.isLastChunk
          ? // TODO(Bug 2066269): remove hideRetry once resume-activity has
            // its own retry path.
            html`
              <assistant-message-footer
                .messageId=${msg.messageId}
                .appliedMemories=${msg.appliedMemories}
                .showCallout=${msg.showCallout}
                .hideRetry=${!!msg.toolUIData?.isResumeActivity}
              ></assistant-message-footer>
            `
          : nothing}
      </div>
    </div>`;
  }

  #renderFollowUpSuggestions() {
    if (!this.followUpSuggestions?.length) {
      return nothing;
    }
    return html`<smartwindow-prompts
      .prompts=${this.followUpSuggestions.map(text => ({
        text,
        type: "followup",
      }))}
      mode="followup"
    ></smartwindow-prompts>`;
  }

  #renderLoader(suppress) {
    // The spinner is suppressed while an action log is processing (its animated
    // label already communicates progress) and once the reply is streaming (its
    // text is already visible). It only shows while waiting with nothing else on
    // screen yet.
    if (!this.assistantIsLoading || suppress) {
      return nothing;
    }
    return html`<chat-assistant-loader
      .mode=${this.isSearching ? "search" : "default"}
    ></chat-assistant-loader>`;
  }

  #renderError() {
    if (!this.errorObj) {
      return nothing;
    }
    return html`<chat-assistant-error
      .error=${this.errorObj}
    ></chat-assistant-error>`;
  }

  /**
   * Build the render list one turn at a time.
   *
   * The model creates an empty assistant placeholder before tools run so by
   * ordinal the assistant has a lower index than the toolcall messages it produces.
   * We buffer per turn so action log UI render above the assistant reply,
   * flipping that ordinal order for display
   *
   * @return {Array<{ type: string, msg: object, contextPageUrl?: string }>}
   */
  #buildTurnRenderItems() {
    const items = [];
    let lastContextPageUrl;
    let pendingActionLogs = [];
    let pendingAssistantMessage = null;
    let pendingAssistantContextUrl;

    // Commit the current turn's buffered action logs and assistant reply into
    // items. Action log render above the assistant message
    //
    // isComplete marks whether the turn has finished
    const appendPendingAssistantTurn = isComplete => {
      if (!pendingActionLogs.length && !pendingAssistantMessage) {
        return;
      }

      // Emit one grouped action-log item per turn carrying all the tool
      // messages of that turn. The renderer collapses them into a single
      // <ai-action-result> with one row per tool.
      if (pendingActionLogs.length) {
        items.push({
          type: "action-log",
          msgs: pendingActionLogs,
          isComplete,
        });
      }

      if (pendingAssistantMessage) {
        items.push({
          type: "message",
          msg: pendingAssistantMessage,
          contextPageUrl: pendingAssistantContextUrl,
        });
      }

      pendingActionLogs = [];
      pendingAssistantMessage = null;
      pendingAssistantContextUrl = undefined;
    };

    for (const msg of this.conversationState) {
      if (!msg) {
        continue;
      }

      // Hold tool UI messages for the current turn
      if (msg.uiType === UI_TYPES.ACTION_LOG) {
        pendingActionLogs.push(msg);
        continue;
      }

      // Hold the assistant reply
      // If a previous assistant is still pending, commit it first, so it isn't dropped
      if (msg.role === "assistant") {
        if (pendingAssistantMessage) {
          appendPendingAssistantTurn(true);
        }

        pendingAssistantMessage = msg;
        pendingAssistantContextUrl = lastContextPageUrl;
        continue;
      }

      // A user or any other role ends the previous turn. Commit first then push.
      appendPendingAssistantTurn(true);

      // Capture the previous context URL for this message's duplicate-chip check,
      // then update lastContextPageUrl for subsequent messages.
      const contextPageUrl = lastContextPageUrl;
      if (msg.role === "user") {
        lastContextPageUrl = msg.pageUrl;
      }

      items.push({
        type: "message",
        msg,
        contextPageUrl,
      });
    }

    // Commit anything still pending at end of loop. The action log is finished
    // once the turn's reply starts streaming (its tools are done by then) or the
    // whole turn completes, so it doesn't keep shimmering through response
    // generation.
    const replyStarted = !!pendingAssistantMessage?.body;
    appendPendingAssistantTurn(!this.assistantIsLoading || replyStarted);

    return items;
  }

  /**
   * Collect the per-tool rows for the grouped action log card
   *
   * @param {Array<object>} toolMsgs
   * @returns {Array<{ labelL10nId?: string, labelL10nArgs?: object, label?: string, items: Array }>}
   */
  #buildGroupedActionLogRows(toolMsgs) {
    return toolMsgs.map(msg => msg.row).filter(Boolean);
  }

  #renderMessages(items) {
    return repeat(
      items,
      (item, i) => this.#renderItemKey(item, i),
      (item, i) => {
        const { type, msgs, msg, isComplete, contextPageUrl } = item;
        if (type === "action-log") {
          return this.#renderActionLogGroup(msgs, isComplete, i);
        }

        const chips = this.#getVisibleChips(msg, contextPageUrl);
        return this.#renderMessage(msg, chips);
      }
    );
  }

  #renderItemKey(item, i) {
    if (item.type === "action-log") {
      const first = item.msgs?.[0];
      return `action-log:${first?.toolCallId ?? first?.messageId ?? i}`;
    }
    const { msg } = item;
    return `message:${msg?.convId ?? ""}:${msg?.ordinal ?? i}`;
  }

  render() {
    const renderItems = this.#buildTurnRenderItems();
    const actionLogInProgress = renderItems.some(
      item => item.type === "action-log" && item.isComplete === false
    );
    // Once the reply is streaming, its text is already visible, so the spinner
    // isn't needed (and shouldn't reappear now that the action log completes as
    // soon as the reply starts).
    const lastItem = renderItems.at(-1);
    const replyStreaming =
      lastItem?.type === "message" &&
      lastItem.msg?.role === "assistant" &&
      !!lastItem.msg?.body;
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/ai-chat-content.css"
      />
      <div class="chat-content-wrapper" tabindex="-1">
        <div class="chat-inner-wrapper">
          ${this.#renderMessages(renderItems)}
          ${this.#renderFollowUpSuggestions()}
          ${this.#renderLoader(actionLogInProgress || replyStreaming)}
          ${this.#renderError()}
        </div>
      </div>
      <div class="fullpage-top-box-shadow"></div>
      <kit-mention variant="sidebar"></kit-mention>
      <div
        class="assistant-response-announcer"
        role="status"
        aria-live="polite"
        aria-atomic="true"
      >
        ${this.assistantResponseAnnouncement}
      </div>
      <moz-button
        class="jump-to-bottom-button"
        data-l10n-id="aiwindow-jump-to-bottom"
        data-l10n-attrs="aria-label,tooltiptext"
        iconsrc="chrome://global/skin/icons/shaft-arrow-down.svg"
        disabled
        type="ghost icon"
      ></moz-button>
    `;
  }
}

customElements.define("ai-chat-content", AIChatContent);
