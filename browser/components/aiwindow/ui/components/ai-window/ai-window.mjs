/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { installClientErrorListeners } from "chrome://browser/content/aiwindow/modules/ClientErrorTelemetry.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-prompts.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-promo.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-topsites.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/aiwindow/components/kit-mention.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-history-menu.mjs";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  Chat: "moz-src:///browser/components/aiwindow/models/Chat.sys.mjs",
  GET_PAGE_CONTENT:
    "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  MODEL_FEATURES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  openAIEngine:
    "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs",
  buildEngineForFeature:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  generateChatTitle:
    "moz-src:///browser/components/aiwindow/models/TitleGeneration.sys.mjs",
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  AIWindowUI:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs",
  EMPTY_SMARTBAR_INPUT_STATE:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowTabStatesManager.sys.mjs",
  FeedbackModal:
    "moz-src:///browser/components/aiwindow/ui/modules/FeedbackModal.sys.mjs",
  ChatConversation:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs",
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
  MEMORIES_FLAG_SOURCE:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatEnums.sys.mjs",
  MESSAGE_ROLE:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatEnums.sys.mjs",
  UserRoleOpts:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatMessage.sys.mjs",
  getRoleLabel:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatUtils.sys.mjs",
  getCurrentTabUrl:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatUtils.sys.mjs",
  NewTabStarterGenerator:
    "moz-src:///browser/components/aiwindow/models/ConversationSuggestions.sys.mjs",
  generateConversationStartersSidebar:
    "moz-src:///browser/components/aiwindow/models/ConversationSuggestions.sys.mjs",
  generateResumeActivityConversationStarters:
    "moz-src:///browser/components/aiwindow/models/ConversationSuggestions.sys.mjs",
  MAX_NUM_MEMORIES_FOR_RESUME_ACTIVITY:
    "moz-src:///browser/components/aiwindow/models/ConversationSuggestions.sys.mjs",
  constructConversationToResumeActivity:
    "moz-src:///browser/components/aiwindow/models/ConversationSuggestions.sys.mjs",
  MemoriesManager:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs",
  getAllModelsData:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  refreshModelsDataCache:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  getCurrentModelChoiceId:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  getCurrentModelName:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  ToolUI: "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs",
  AgentUI: "moz-src:///browser/components/aiwindow/ui/modules/AgentUI.sys.mjs",
  ACTION_LOG_UI_TYPE:
    "moz-src:///browser/components/aiwindow/ui/modules/ToolActionLog.sys.mjs",
  getActionLogConfigForTool:
    "moz-src:///browser/components/aiwindow/ui/modules/ToolActionLog.sys.mjs",
  buildActionLogRow:
    "moz-src:///browser/components/aiwindow/ui/modules/ToolActionLog.sys.mjs",
  UI_UPDATE_TYPES:
    "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  SmartWindowTelemetry:
    "moz-src:///browser/components/aiwindow/ui/modules/SmartWindowTelemetry.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", function () {
  return console.createInstance({
    prefix: "ChatStore",
    maxLogLevelPref: "browser.smartwindow.chatStore.loglevel",
  });
});

/**
 * @import { SmartbarAction } from "chrome://browser/content/aiwindow/components/input-cta/input-cta.mjs"
 */

/**
 * @typedef {{
 *   type: string,
 *   id: string,
 *   label: string,
 *   textOffset: number
 * }} PersistedMention
 *
 * @typedef {{
 *   text: string,
 *   mentions: PersistedMention[]
 * }} SmartbarInputState
 *
 * @typedef {{
 *   input: SmartbarInputState | false,
 *   mode: string,
 *   pageUrl: URL,
 *   conversationId: string,
 *   tab: MozTabbrowserTab
 * }} TabStateEventDetail
 */

/**
 * @typedef {{
 *   bubbles: true,
 *   detail: TabStateEventDetail
 * }} TabStateEventOptions
 */

/**
 * @typedef {CustomEvent & {
 *   detail: TabStateEventDetail
 * }} TabStateEvent
 */

/**
 * @typedef {"button" | "enter" | "follow-up" | "shortcuts" | "starter" | "suggestion"} ChatSubmitType
 */

const MODE = {
  FULLPAGE: "fullpage",
  SIDEBAR: "sidebar",
  URLBAR: "urlbar",
};

const ACTION = {
  CHAT: "chat",
  SEARCH: "search",
  NAVIGATE: "navigate",
};

const PREF_MEMORIES_CONVERSATION =
  "browser.smartwindow.memories.generateFromConversation";
const PREF_MEMORIES_HISTORY =
  "browser.smartwindow.memories.generateFromHistory";
const PREF_MEMORIES_HAS_SEEN_MEMORIES =
  "browser.smartwindow.memories.hasSeenMemories";
const PREF_MODEL_CHOICE = "browser.smartwindow.firstrun.modelChoice";
const PREF_CUSTOM_ENDPOINT = "browser.smartwindow.customEndpoint";
// TODO Bug 2053495: remove with mistral release pref
const PREF_MISTRAL_RELEASE = "browser.smartwindow.mistralRelease";
const TAB_FAVICON_CHAT =
  "chrome://browser/content/aiwindow/assets/ask-icon.svg";
const PREF_CHAT_INTERACTION_COUNT = "browser.smartwindow.chat.interactionCount";
const PREF_HIDE_TOP_SITES = "browser.smartwindow.hideTopSites";
const PREF_AGENT_ENABLED = "browser.smartwindow.agent.enabled";
const MAX_INTERACTION_COUNT = 1000;
const HISTORY_MENU_MAX_RECENT_CHATS = 6;

// Events dispatched by <smartwindow-history-menu> and handled here.
const HISTORY_MENU_EVENTS = [
  "smartwindow-history-menu:new-chat",
  "smartwindow-history-menu:open-chat",
  "smartwindow-history-menu:view-all-chats",
  "smartwindow-history-menu:open-settings",
  "smartwindow-history-menu:request-recent-chats",
];
const MAX_SIDEBAR_STARTER_CACHE_KEYS = 20;
const MAX_TOP_SITES = 8;
const MAX_PILL_COUNT = 3;

// 1-6 are MLPA spec codes; 7 is set locally for Fastly-blocked 406s.
const ERROR_TELEMETRY_NAME_BY_CODE = {
  1: "budgetExceeded",
  2: "rateLimitExceeded",
  3: "contextTooLarge",
  4: "maxUsersReached",
  5: "upstreamRateLimit",
  6: "fastlyWafRateLimit",
  7: "fastlyBlocked",
};

// Fastly errors don't have the error attribute; map the 406 to fastlyBlocked.
function getErrorCode(error) {
  return (
    error.error ??
    error.metadata?.errorMessage ??
    (error.status === 406 ? 7 : undefined)
  );
}

function resolveModelResponseError(error) {
  const httpStatus = error.status ?? 0;
  if (error.clientReason) {
    return { name: error.clientReason, httpStatus };
  }
  const code = getErrorCode(error);
  if (code in ERROR_TELEMETRY_NAME_BY_CODE) {
    return { name: ERROR_TELEMETRY_NAME_BY_CODE[code], httpStatus };
  }
  if (httpStatus) {
    return { name: "serverError", httpStatus };
  }
  return { name: error.name || "genericError", httpStatus };
}

/**
 * A custom element for managing AI Window
 *
 * @todo Bug2007583
 * Tests follow up for re-opening conversations
 */
export class AIWindow extends MozLitElement {
  static properties = {
    mode: { type: String, reflect: true }, // sidebar | fullpage
    showStarters: { type: Boolean, state: true },
    showFooter: { type: Boolean, state: true },
    promoMessage: { type: Object, state: true },
    showDisclaimer: { type: Boolean, state: true },
    isGenerating: { type: Boolean, state: true },
    availableModels: { type: Object, state: true },
    selectedModelId: { type: String, state: true },
    topSites: { type: Array, state: true },
    startersResolved: { type: Boolean, state: true },
    recentChats: { type: Array, state: true },
  };

  #browser;
  #smartbar;
  #smartbarToggleButton;
  #conversation = null;
  #memoriesButton = null;
  #memoriesToggled = null;
  #visibilityChangeHandler;
  #abortController = null;

  #starters = [];
  #starterPromptsAbortController = null;
  #smartbarReadyPromise;
  #resolveSmartbarReady;
  #sidebarStarterCache = new Map();
  #smartbarResizeObserver = null;
  #windowModeObserver = null;
  #topSitesObserver = null;
  #removeClientErrorListeners = null;
  #swapDocShellsChromeWindow = null;
  #hasMemories = false;
  // Resume starters are only eligible on the first fullpage load.
  #canLoadResumeStarters = true;
  #selectedModelChoiceId = null;
  #hasModelChoiceOverride = false;
  #isGeneratingResumeActivityConversation = false;

  get #kitMention() {
    return this.shadowRoot?.querySelector("kit-mention");
  }

  get #memoriesIconShown() {
    return (
      this.memoriesConversationPref ||
      this.memoriesHistoryPref ||
      this.#hasMemories
    );
  }

  /**
   * Flags whether the #conversation reference has been updated but the messages
   * have not been delivered via the actor.
   *
   * @type {bool}
   */
  #pendingMessageDelivery;

  /**
   * Conversation to restore when the next aichat browser signals ready.
   * Set during EndSwapDocShells when a chat tab is dragged back to a Smart Window.
   *
   * @type {ChatConversation|null}
   */
  #pendingRestoreConversation = null;

  /**
   * Gets the host browser element that embeds this AI window.
   *
   * @returns {Element|null} The host browser element, or null if not found
   * @private
   */
  get #hostBrowser() {
    return window.browsingContext?.embedderElement || null;
  }

  #detectModeFromContext() {
    return this.#hostBrowser?.id === "ai-window-browser"
      ? MODE.SIDEBAR
      : MODE.FULLPAGE;
  }

  /**
   * Stamps the current conversation ID into the fullpage history entry via
   * replaceState, enabling conversation recovery after back navigation and
   * serving as a fallback for session restore / undo-close when the
   * data-conversation-id attribute on the host <browser> is unavailable.
   *
   */
  #syncHistoryState() {
    if (!this.isConnected || this.mode !== MODE.FULLPAGE) {
      return;
    }
    window.history.replaceState(
      {
        ...window.history.state,
        conversationId: this.#conversation?.id ?? null,
      },
      ""
    );
  }

  /**
   * Checks if there's a pending conversation ID to load.
   *
   * @returns {string|null} The conversation ID or null if none exists
   * @private
   */
  #getPendingConversationId() {
    const findId =
      this.#hostBrowser?.getAttribute("data-conversation-id") ??
      window.history.state?.conversationId ??
      null;

    return findId;
  }

  /**
   * Gets the browser container element from the shadow DOM.
   *
   * @returns {Element|null} The browser container element, or null if not found
   * @private
   */
  #getBrowserContainer() {
    return this.renderRoot.querySelector("#browser-container");
  }

  async syncSmartbarMemoriesStateFromConversation() {
    if (!this.#smartbar) {
      return;
    }

    if (this.#conversation?.memoriesToggled != null) {
      this.#memoriesToggled = this.#conversation.memoriesToggled;
    }
    await this.#syncMemoriesButtonUI();
  }

  async focusSmartbar() {
    await this.#smartbarReadyPromise;
    if (!this.#smartbar) {
      return false;
    }
    this.#smartbar.focus();
    return true;
  }

  async #refreshHasMemories() {
    try {
      const memories = await lazy.MemoriesManager.getAllMemories();
      this.#hasMemories = memories?.length > 0;
    } catch (e) {
      lazy.log.error("Failed to check for existing memories", e);
      this.#hasMemories = false;
    }
  }

  async #syncMemoriesButtonUI() {
    if (!this.#memoriesButton) {
      return;
    }

    if (!this.memoriesConversationPref && !this.memoriesHistoryPref) {
      await this.#refreshHasMemories();
    }

    this.#memoriesButton.show = this.#memoriesIconShown;
    this.#memoriesButton.pressed =
      this.#memoriesIconShown &&
      (this.#memoriesToggled ?? this.#memoriesIconShown);
  }

  /**
   * Records a user chat interaction by incrementing the interaction
   * counter when users submit messages or click starter prompts.
   *
   * @private
   */
  #recordChatInteraction() {
    let interactionCount = Services.prefs.getIntPref(
      PREF_CHAT_INTERACTION_COUNT,
      0
    );

    if (interactionCount < MAX_INTERACTION_COUNT) {
      Services.prefs.setIntPref(
        PREF_CHAT_INTERACTION_COUNT,
        interactionCount + 1
      );
    }
  }

  constructor() {
    super();

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "memoriesConversationPref",
      PREF_MEMORIES_CONVERSATION,
      true,
      () => this.#syncMemoriesButtonUI()
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "memoriesHistoryPref",
      PREF_MEMORIES_HISTORY,
      true,
      () => this.#syncMemoriesButtonUI()
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "hideTopSitesPref",
      PREF_HIDE_TOP_SITES,
      false,
      () => this.#syncTopSites()
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "agentEnabledPref",
      PREF_AGENT_ENABLED,
      false
    );
    // TODO Bug 2053495: remove with mistral release pref
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "mistralReleasePref",
      PREF_MISTRAL_RELEASE,
      false,
      () => this.#onMistralReleasePrefChanged()
    );
    // defineLazyPreferenceGetter registers its pref observer on first read, so
    // touch the value here to arm the onUpdate callback above.
    void this.mistralReleasePref;

    this.userPrompt = "";
    this.#browser = null;
    this.#smartbar = null;
    this.#conversation = new lazy.ChatConversation({});
    this.#smartbarReadyPromise = new Promise(resolve => {
      this.#resolveSmartbarReady = resolve;
    });

    this.mode = this.#detectModeFromContext();
    this.showStarters = false;
    this.topSites = [];
    this.startersResolved = false;
    this.recentChats = [];
    this.showFooter = this.mode === MODE.FULLPAGE;
    this.promoMessage = null;
    this.showDisclaimer = this.mode !== MODE.FULLPAGE;
    this.isGenerating = false;
    this.#setModelChoice(lazy.getCurrentModelChoiceId());

    // Apply chat-active immediately if restoring a conversation
    if (this.#hostBrowser?.getAttribute("data-conversation-id")) {
      this.classList.add("chat-active");
    }
  }

  get #topChromeWindow() {
    return window.browsingContext?.topChromeWindow;
  }

  #attachConversationListeners() {
    if (!this.#conversation) {
      return;
    }

    this.#conversation.on(
      "chat-conversation:message-update",
      this.#onMessageUpdate
    );
    this.#conversation.on(
      "chat-conversation:message-complete",
      this.#onMessageComplete
    );
    this.#conversation.on(
      "chat-conversation:seen-urls-updated",
      this.#onSeenUrlsUpdated
    );
    lazy.AgentUI.observeMonitorChanges(this.#conversation);
  }

  #removeConversationListeners() {
    if (!this.#conversation) {
      return;
    }

    this.#conversation.off(
      "chat-conversation:message-update",
      this.#onMessageUpdate
    );
    this.#conversation.off(
      "chat-conversation:message-complete",
      this.#onMessageComplete
    );
    this.#conversation.off(
      "chat-conversation:seen-urls-updated",
      this.#onSeenUrlsUpdated
    );
    lazy.AgentUI.unobserveMonitorChanges(this.#conversation);
  }

  #onSeenUrlsUpdated = () => {
    const actor = this.#getAIChatContentActor();
    if (actor) {
      this.#dispatchSeenUrls(actor);
    }
  };

  #onMessageUpdate = (_event, message) => {
    // In fullpage, Kit must anchor to the chrome viewport (bottom of the
    // page, alongside the footer) — `position: fixed` inside the embedded
    // chat-content document would anchor to that browser's viewport, not
    // ours. So we trigger our own chrome-side kit-mention here and strip
    // the token before dispatching to content to avoid double-render.
    if (this.mode === MODE.FULLPAGE && message.kit) {
      this.#kitMention?.trigger({
        value: message.kit,
        convId: message.convId,
      });
      message = { ...message, kit: undefined };
    }

    if (message.toolUIData) {
      lazy.ToolUI.handleUIDisplayTelemetry(message.toolUIData, {
        location: this.mode,
        chat_id: this.conversationId,
        message_seq: this.#conversation?.messageCount ?? 0,
      });
    }
    this.#dispatchMessageToChatContent(message);
  };

  onMemoriesApplied() {
    Glean.smartWindow.memoryApplied.record({
      location: this.mode,
      chat_id: this.conversationId,
      message_seq: this.#conversation?.messageCount ?? 0,
    });
  }

  /**
   * Gets the conversation id from data-conversation-id attribute
   *
   * @private
   */
  #getDataConvId() {
    if (this.#conversation) {
      return this.#conversation.id;
    }

    return this.#hostBrowser?.getAttribute("data-conversation-id");
  }

  connectedCallback() {
    super.connectedCallback();
    this.setAttribute("mode", this.mode);
    this.#loadAvailableModels();

    this.ownerDocument.addEventListener("OpenConversation", this);
    this.ownerDocument.addEventListener(
      "smartbar-commit",
      this.#handleSmartbarCommit,
      true
    );
    this.ownerDocument.addEventListener(
      "smartbar-stop-generation",
      this.#handleStopGeneration
    );
    this.ownerDocument.addEventListener(
      "aiwindow-input-model-select:model-change",
      this.#handleModelChange
    );
    this.ownerDocument.addEventListener(
      "aiwindow-input-model-select:open-settings",
      this.#handleOpenModelSettings
    );
    for (const eventName of HISTORY_MENU_EVENTS) {
      this.ownerDocument.addEventListener(eventName, this.#onHistoryMenuEvent);
    }

    Services.prefs.addObserver(
      PREF_MODEL_CHOICE,
      this.#onModelChoicePrefChanged
    );
    Services.prefs.addObserver(
      PREF_CUSTOM_ENDPOINT,
      this.#onCustomEndpointPrefChanged
    );

    // AboutNewTab populates its Top Sites store asynchronously, so on a fresh
    // browser start the store can still be empty when we first read it. Reload
    // whenever it changes so the row appears once the feed is ready.
    this.#topSitesObserver = () => this.#syncTopSites();
    Services.obs.addObserver(
      this.#topSitesObserver,
      "newtab-top-sites-changed"
    );

    this.#removeClientErrorListeners = installClientErrorListeners(
      this.documentGlobal,
      (error, source) =>
        lazy.SmartWindowTelemetry.recordClientError(error, {
          source,
          context: this.getClientErrorContext(),
        })
    );
    this.#loadPendingConversation();
    this.#setupWindowModeObserver();

    // Saving the chrome window ref to avoid leaks when we drag a tab out
    this.#registerSwapDocShellsListener(
      window.browsingContext?.topChromeWindow
    );

    this.#dispatchChromeEvent(
      "ai-window:connected",
      this.#getAIWindowEventOptions()
    );

    // Ensure disconnectedCallback gets called to clean up listeners
    this.documentGlobal.addEventListener("unload", () => this.remove(), {
      once: true,
    });
  }

  get conversationId() {
    return this.#conversation?.id;
  }

  get conversationMessageCount() {
    return this.#conversation.messageCount;
  }

  /**
   * Get the current conversation object
   *
   * @returns {ChatConversation} The conversation object
   */
  get conversation() {
    return this.#conversation;
  }

  #registerSwapDocShellsListener(win) {
    if (!win) {
      return;
    }

    this.#swapDocShellsChromeWindow?.removeEventListener(
      "EndSwapDocShells",
      this.#handleEndSwapDocShells,
      true
    );

    this.#swapDocShellsChromeWindow = win;
    this.#swapDocShellsChromeWindow?.addEventListener(
      "EndSwapDocShells",
      this.#handleEndSwapDocShells,
      true
    );
  }

  handleEvent(event) {
    if (event.detail) {
      this.openConversation(event.detail);
    } else if (!this.#conversation?.messages?.length) {
      this.onCreateNewChatClick();
    }
  }

  /* Handles tab adoption (dragging out of a window) when
   * Smart tab -> Classic window
   * Smart tab -> Classic window -> Dragged back to a smart window
   */
  #handleEndSwapDocShells = () => {
    const win = window.browsingContext?.topChromeWindow;

    if (!win) {
      return;
    }

    // Re-register on the new chrome window so future swaps are caught
    this.#registerSwapDocShellsListener(win);

    // needed if a smart tab became classic and then becomes smart again via dragging
    this.#updateSmartbarAndHeaderVisibility();

    const browser = window.browsingContext.embedderElement;
    const isAIWindowActive = lazy.AIWindow.isAIWindowActive(win);
    const hasActiveChat = lazy.AIWindow.hasActiveChatInBrowser(browser);
    if (!isAIWindowActive) {
      if (!hasActiveChat) {
        // No active chat: redirect to classic new tab
        const classicNewTabURI = Services.io.newURI(win.BROWSER_NEW_TAB_URL);
        const triggeringPrincipal =
          Services.scriptSecurityManager.getSystemPrincipal();
        browser.loadURI(classicNewTabURI, {
          triggeringPrincipal,
        });
      } else {
        this.#recreateAIChatBrowser();
      }
    } else if (hasActiveChat) {
      // Dragged back to a Smart Window: actor connection was broken by swap.
      // Save the conversation so onContentReady can restore it after the new
      // aichat browser loads.
      this.#pendingRestoreConversation = this.#conversation;
      this.#recreateAIChatBrowser();
    }
  };

  #recreateAIChatBrowser() {
    const container = this.#getBrowserContainer();
    if (!container) {
      return;
    }
    this.#browser?.remove();
    this.#createAIChatBrowser(container);
  }

  #createAIChatBrowser(container) {
    const browser = this.ownerDocument.createXULElement("browser");
    browser.setAttribute("id", "aichat-browser");
    browser.setAttribute("type", "content");
    browser.setAttribute("maychangeremoteness", "true");
    browser.setAttribute("remote", "true");
    browser.setAttribute("remoteType", "privilegedabout");
    browser.setAttribute("disableglobalhistory", "true");
    browser.setAttribute("transparent", "true");
    browser.setAttribute("src", "about:aichatcontent");
    container.prepend(browser);
    this.#browser = browser;
    this.#updateBrowserTabbable();
  }

  // Keep the empty chat browser out of the tab cycle so keyboard users don't
  // hit a 0-height/contentless focus stop between the chat header and the
  // smartbar. Once a conversation is active, the browser rejoins tab order.
  #updateBrowserTabbable() {
    if (!this.#browser) {
      return;
    }
    if (this.classList.contains("chat-active")) {
      this.#browser.removeAttribute("tabindex");
    } else {
      this.#browser.setAttribute("tabindex", "-1");
    }
  }

  #setupWindowModeObserver() {
    this.#windowModeObserver = (subject, topic) => {
      if (topic === "ai-window-state-changed") {
        if (subject == window.browsingContext?.topChromeWindow) {
          this.#updateSmartbarAndHeaderVisibility();
        }
      }
    };

    Services.obs.addObserver(
      this.#windowModeObserver,
      "ai-window-state-changed"
    );
  }

  #updateSmartbarAndHeaderVisibility() {
    const chatHeader =
      this.renderRoot.querySelector(".fullpage-header") ||
      this.renderRoot.querySelector(".sidebar-header");

    if (!this.#smartbar || !this.#smartbarToggleButton) {
      return;
    }

    const isSmartWindow = lazy.AIWindow.isAIWindowActive(
      window.browsingContext.topChromeWindow
    );

    this.#smartbar.hidden = !isSmartWindow;
    this.#smartbarToggleButton.hidden = isSmartWindow;
    this.toggleAttribute("classic-mode", !isSmartWindow);
    if (chatHeader) {
      chatHeader.hidden = !isSmartWindow;
    }
  }

  disconnectedCallback() {
    // Cancel any pending inference for starter prompts so the promise chain
    // does not prevent this window from being garbage collected.
    this.#starterPromptsAbortController?.abort();
    this.#starterPromptsAbortController = null;

    this.#abortController?.abort();
    this.#abortController = null;

    // Clean up visibility change handler
    if (this.#visibilityChangeHandler) {
      this.ownerDocument.removeEventListener(
        "visibilitychange",
        this.#visibilityChangeHandler
      );
      this.#visibilityChangeHandler = null;
    }

    this.#swapDocShellsChromeWindow?.removeEventListener(
      "EndSwapDocShells",
      this.#handleEndSwapDocShells,
      true
    );
    this.#swapDocShellsChromeWindow = null;

    // Clean up window mode observer
    if (this.#windowModeObserver) {
      Services.obs.removeObserver(
        this.#windowModeObserver,
        "ai-window-state-changed"
      );
      this.#windowModeObserver = null;
    }

    // Clean up model choice preference observer
    Services.prefs.removeObserver(
      PREF_MODEL_CHOICE,
      this.#onModelChoicePrefChanged
    );
    Services.prefs.removeObserver(
      PREF_CUSTOM_ENDPOINT,
      this.#onCustomEndpointPrefChanged
    );

    // Clean up Top Sites store observer
    if (this.#topSitesObserver) {
      Services.obs.removeObserver(
        this.#topSitesObserver,
        "newtab-top-sites-changed"
      );
      this.#topSitesObserver = null;
    }

    // Clean up smartbar toggle button
    if (this.#smartbarToggleButton) {
      this.#smartbarToggleButton.remove();
      this.#smartbarToggleButton = null;
    }

    // Clean up smartbar
    this.ownerDocument.removeEventListener(
      "smartbar-commit",
      this.#handleSmartbarCommit,
      true
    );
    this.ownerDocument.removeEventListener(
      "smartbar-stop-generation",
      this.#handleStopGeneration
    );
    this.ownerDocument.removeEventListener(
      "aiwindow-input-model-select:model-change",
      this.#handleModelChange
    );
    this.ownerDocument.removeEventListener(
      "aiwindow-input-model-select:open-settings",
      this.#handleOpenModelSettings
    );
    for (const eventName of HISTORY_MENU_EVENTS) {
      this.ownerDocument.removeEventListener(
        eventName,
        this.#onHistoryMenuEvent
      );
    }
    if (this.#smartbar) {
      this.#smartbar.removeEventListener(
        "aiwindow-memories-toggle:on-change",
        this.#handleMemoriesToggle
      );
      this.#smartbar.remove();
      this.#smartbar = null;
      this.#memoriesButton = null;
    }

    // Clean up resize observer
    if (this.#smartbarResizeObserver) {
      this.#smartbarResizeObserver.disconnect();
      this.#smartbarResizeObserver = null;
    }

    // Clean up browser
    if (this.#browser) {
      this.#browser.remove();
      this.#browser = null;
    }

    // Clean up conversation
    this.#removeConversationListeners();
    this.#conversation = null;
    this.#pendingRestoreConversation = null;

    // Unblock any pending loadStarterPrompts awaiting smartbar-ready so
    // they can see isConnected=false and exit cleanly.
    this.#resolveSmartbarReady?.();

    this.ownerDocument.removeEventListener("OpenConversation", this);

    this.#removeClientErrorListeners?.();
    this.#removeClientErrorListeners = null;

    super.disconnectedCallback();
  }

  /**
   * Loads all available models.
   */
  async #loadAvailableModels() {
    const allModels = await lazy.getAllModelsData();

    // Only show custom model option if a custom endpoint has been configured
    if (lazy.openAIEngine.hasCustomEndpoint()) {
      this.availableModels = allModels;
      return;
    }
    const { 0: _unusedCustom, ...presetModels } = allModels;
    void _unusedCustom;
    this.availableModels = presetModels;
  }

  /**
   * Updates the smartbar model select with available models.
   *
   * @param {Element} smartbar - The smartbar element
   */
  #updateSmartbarModels(smartbar) {
    const modelSelect = smartbar?.querySelector("input-model-select");
    if (modelSelect && this.availableModels) {
      modelSelect.availableModels = this.availableModels;
      modelSelect.selectedModelId = this.selectedModelId;
      modelSelect.defaultModelChoiceId = lazy.getCurrentModelChoiceId();
    }
  }

  #onModelChoicePrefChanged = async () => {
    if (this.#hasModelChoiceOverride) {
      return;
    }
    const defaultModelChoiceId = lazy.getCurrentModelChoiceId();
    if (!this.availableModels[defaultModelChoiceId]) {
      return;
    }
    // Switch without override so the tab stays in sync with global setting.
    await this.#switchModel(defaultModelChoiceId, { isTabOverride: false });
    this.#updateSmartbarModels(this.#smartbar);
  };

  #handleModelChange = async event => {
    await this.#switchModel(event.detail.modelChoiceId, {
      isTabOverride: true,
    });
  };

  #onCustomEndpointPrefChanged = async () => {
    await this.#loadAvailableModels();
    const defaultModelChoiceId = lazy.getCurrentModelChoiceId();
    if (
      !this.#hasModelChoiceOverride &&
      this.availableModels[defaultModelChoiceId]
    ) {
      await this.#switchModel(defaultModelChoiceId, { isTabOverride: false });
    }

    this.#updateSmartbarModels(this.#smartbar);
  };

  // TODO Bug 2053495: remove with mistral release pref.
  #onMistralReleasePrefChanged = async () => {
    await lazy.refreshModelsDataCache();
    await this.#loadAvailableModels();
    // The model backing a choice can change across the flip, so re-resolve the
    // current choice's model to keep selectedModelId valid; otherwise the select
    // can't find the selected model and renders in a stale/blank state.
    const defaultModelChoiceId = lazy.getCurrentModelChoiceId();
    if (
      !this.#hasModelChoiceOverride &&
      this.availableModels[defaultModelChoiceId]
    ) {
      await this.#switchModel(defaultModelChoiceId, { isTabOverride: false });
    }
    this.#updateSmartbarModels(this.#smartbar);
  };

  /**
   * Sets the selected model choice.
   *
   * @param {string} modelChoiceId
   */
  #setModelChoice(modelChoiceId) {
    this.#selectedModelChoiceId = modelChoiceId;
    this.selectedModelId =
      this.availableModels?.[modelChoiceId]?.model ??
      lazy.getCurrentModelName();
  }

  async #switchModel(modelChoiceId, { isTabOverride }) {
    this.#setModelChoice(modelChoiceId);
    // Switching another model than the global default overrides the choice for
    // the current tab.
    this.#hasModelChoiceOverride =
      isTabOverride && modelChoiceId !== lazy.getCurrentModelChoiceId();

    // Update the system prompt for the new model. The engine is rebuilt on the
    // next send, so pass the freshly-selected model explicitly here.
    if (this.#conversation?.messages.length) {
      await this.#conversation.loadSystemPrompt({
        model: this.selectedModelId,
      });
    }

    if (isTabOverride) {
      this.#dispatchChromeEvent(
        "ai-window:model-changed",
        this.#getAIWindowEventOptions()
      );
    }
  }

  /**
   * Restores per tab model choice overrides.
   *
   * @param {?string} modelChoiceId - Override model choice id
   */
  restoreModelChoiceOverride(modelChoiceId) {
    this.#hasModelChoiceOverride = modelChoiceId !== null;
    this.#setModelChoice(modelChoiceId ?? lazy.getCurrentModelChoiceId());
    this.#updateSmartbarModels(this.#smartbar);
  }

  #handleOpenModelSettings = () => {
    this.#topChromeWindow?.openPreferences("personalizeSmartWindow");
  };

  /**
   * Loads a conversation if one is set on the data-conversation-id attribute.
   */
  async #loadPendingConversation() {
    const conversationId = this.#getPendingConversationId();
    if (!conversationId) {
      // No externally-provided ID — stamp the fresh constructor conversation
      // onto the host browser so navigating away and back can recover it,
      // and record it in history.state for session/undo-close restore.
      this.#hostBrowser?.setAttribute(
        "data-conversation-id",
        this.#conversation.id
      );
      this.#syncHistoryState();
      return;
    }

    const conversation =
      await lazy.AIWindow.chatStore.findConversationById(conversationId);

    conversation
      ? this.openConversation(conversation)
      : this.#resetConversationState();

    if (conversation) {
      Glean.smartWindow.chatRetrieved.record({
        location: this.mode,
        chat_id: conversation.id,
        message_seq: this.#conversation?.messageCount ?? 0,
        time_delta: Date.now() - conversation.updatedDate,
      });
    }

    if (this.#hostBrowser?.hasAttribute("data-continue-streaming")) {
      this.#hostBrowser.removeAttribute("data-continue-streaming");
      this.#continueAfterToolResult();
    }
  }

  async firstUpdated() {
    const doc = this.ownerDocument;
    const container = this.#getBrowserContainer();
    this.#createAIChatBrowser(container);

    // Create the Smartbar before any async work so it is available
    // synchronously after the first render.
    if (doc.hidden) {
      this.#visibilityChangeHandler = () => {
        if (!doc.hidden && !this.#smartbar) {
          this.#getOrCreateSmartbar(doc);
        }
      };
      doc.addEventListener("visibilitychange", this.#visibilityChangeHandler, {
        once: true,
      });
    } else {
      this.#getOrCreateSmartbar(doc);
    }

    // Now that the element is connected, run the initial swap so
    // AIWindowTabStatesManager receives ai-window:conversation-changed
    // so it can trigger the initial starter prompts loading
    this.#swapConversation(this.#conversation);

    this.#syncTopSites();

    await this.#loadPendingConversation().catch(error => {
      console.error(
        `loadPendingConversation() error: ${error.toString()}, \nstack: ${error.stack}`
      );
    });
  }

  /**
   * Update the smartbar input from a persisted input state. Restores the
   * plain text first, then re-inserts each saved mention chip at its
   * stored text-character offset.
   *
   * @param {SmartbarInputState} state
   */
  updateInput({ text, mentions }) {
    if (!this.#smartbar) {
      return;
    }

    this.#smartbar.value = text;

    if (!mentions.length) {
      return;
    }

    // Mentions are atom nodes that contribute zero text characters, so
    // inserting one in doc order doesn't shift the textOffsets of those
    // that come after. If insertNode ever starts perturbing surrounding
    // text, this iteration must reverse-walk or re-resolve offsets.
    const editor = this.#smartbar.inputField;
    for (const { type, id, label, textOffset } of mentions) {
      editor.insertMention({ type, id, label }, textOffset);
    }
  }

  /**
   * Restores the smartbar context chips from a persisted per-tab state. Called
   * on tab switch so the chips are scoped to the tab they were added in.
   *
   * @param {ContextWebsite[]} [contextChips] - The user-added chips to restore.
   * @param {boolean} [removedImplicitContextChip] - Restored dismissal of the
   *   implicit current-tab chip.
   */
  restoreContextChips(contextChips = [], removedImplicitContextChip = false) {
    this.#smartbar?.restoreContextChips(
      contextChips,
      removedImplicitContextChip
    );
  }

  /**
   * Captures the current smartbar input as a structured state suitable for
   * persistence: plain text plus the list of inline mention chips with their
   * text-character offsets.
   *
   * @returns {SmartbarInputState}
   */
  #getSmartbarInputState() {
    const editor = this.#smartbar?.inputField;
    if (!editor) {
      return lazy.EMPTY_SMARTBAR_INPUT_STATE;
    }

    const mentions = editor.getAllMentions().map(mention => {
      mention.textOffset = editor.posToTextOffset(mention.pos);
      delete mention.pos;
      return mention;
    });

    return { text: editor.plainText, mentions };
  }

  /**
   * Loads conversation starter prompts from the generator and renders them.
   * In sidebar mode, uses LLM-generated prompts based on tab context and memories.
   * In fullpage mode, uses static prompts based on tab count.
   *
   * @param {boolean} clear Clear current starter prompts?
   * @param {MozTabbrowserTab} selectedTab The selected tab when loading
   * starter prompts was triggered
   */
  async loadStarterPrompts(clear, selectedTab) {
    const currentUrl = selectedTab.linkedBrowser.currentURI.spec ?? "";

    const startersAlreadyLoading =
      this.#conversation &&
      !this.#conversation.messageCount &&
      this.#conversation.transientStarterUrl === currentUrl;
    if (startersAlreadyLoading) {
      return;
    }

    if (this.#conversation && !this.#conversation.messageCount) {
      this.#conversation.transientStarterUrl = currentUrl;
    }

    await this.#smartbarReadyPromise;
    this.#smartbarReadyPromise = null;

    // If the tab switched by the time this function was invoked, or the node is
    // not connected yet, or the conversation has already started then don't
    // trigger loading more conversation starter prompts
    if (
      selectedTab !== this.#getCurrentTab() ||
      !this.isConnected ||
      this.#conversation?.messageCount
    ) {
      return;
    }

    // Cancel any previous pending loadStarterPrompts call, and create a new
    // controller so this call can be canceled when the element disconnects
    // (preventing the pending inference promise chain from keeping the
    // window alive).
    this.#starterPromptsAbortController?.abort();
    const abortController = new AbortController();
    this.#starterPromptsAbortController = abortController;

    if (clear) {
      this.#renderStarterPrompts([], false);
    }

    let starters = [];
    try {
      const gBrowser = window.browsingContext?.topChromeWindow.gBrowser;
      const tabCount = gBrowser?.tabs.length || 0;

      const newTabStarterIds =
        await lazy.NewTabStarterGenerator.getPrompts(tabCount);

      // Kick off the sidebar generation concurrently so its request is issued
      // before the l10n await can be interrupted by a re-entrant call.
      let sidebarStartersPromise = null;
      let startersKey = null;
      let sidebarStarters = null;
      if (this.mode === MODE.SIDEBAR && gBrowser) {
        const { contextWebsites } = this.#smartbar.getCurrentContextData();
        const contextTabs = contextWebsites.map(contextWebsite => ({
          title: contextWebsite.label,
          url: contextWebsite.url,
        }));

        // Get memories setting from user preferences
        const memoriesEnabled =
          this.#memoriesToggled ?? this.#memoriesIconShown;
        startersKey = JSON.stringify({
          contextTabs,
          memoriesEnabled,
        });
        sidebarStarters = this.#sidebarStarterCache.get(startersKey);

        if (!sidebarStarters) {
          sidebarStartersPromise = lazy
            .generateConversationStartersSidebar(
              contextTabs,
              2,
              memoriesEnabled,
              this.conversationId,
              this.#starterPromptsAbortController.signal
            )
            .catch(e => {
              lazy.log.error(
                "[Prompts] Failed to generate sidebar starters:",
                e
              );
              return null;
            });
        }
      }

      let resumeStartersPromise = null;
      const shouldLoadResumeStarters =
        this.mode === MODE.FULLPAGE && this.#canLoadResumeStarters;

      if (shouldLoadResumeStarters) {
        this.#canLoadResumeStarters = false;

        // Ensure #hasMemories is current before checking it.
        if (!this.memoriesConversationPref && !this.memoriesHistoryPref) {
          await this.#refreshHasMemories();
        }
        const memoriesEnabled =
          this.#memoriesToggled ?? this.#memoriesIconShown;

        if (memoriesEnabled) {
          resumeStartersPromise =
            lazy.generateResumeActivityConversationStarters();

          // Reveal all slots together once merged below - showing static
          // starters early would risk a pill's text swapping later.
          this.#renderStarterPrompts(
            Array(MAX_PILL_COUNT).fill({ type: "skeleton" }),
            true,
            false
          );
        }
      }

      starters = await this.ownerDocument.l10n
        .formatValues(newTabStarterIds.map(({ l10nId }) => ({ id: l10nId })))
        .then(texts =>
          texts.map((text, i) => ({ text, type: newTabStarterIds[i].type }))
        )
        .catch(e => {
          lazy.log.error("[Prompts] Failed to load initial starters:", e);
          return [];
        });

      if (this.mode === MODE.SIDEBAR && gBrowser) {
        if (sidebarStartersPromise) {
          sidebarStarters = await sidebarStartersPromise;

          if (sidebarStarters) {
            this.#sidebarStarterCache.delete(startersKey);
            if (
              this.#sidebarStarterCache.size >= MAX_SIDEBAR_STARTER_CACHE_KEYS
            ) {
              const oldestKey = this.#sidebarStarterCache.keys().next().value;
              this.#sidebarStarterCache.delete(oldestKey);
            }
            this.#sidebarStarterCache.set(startersKey, sidebarStarters);
          }
        }

        // If tab switched while waiting for conversation starters
        // return, do not render the starters meant for selectedTab
        if (selectedTab !== this.#getCurrentTab()) {
          return;
        }

        if (sidebarStarters?.length) {
          starters = sidebarStarters;
        }
      } else if (resumeStartersPromise) {
        const resumeStarters = this.#resumeActivitiesToStarterPrompts(
          await resumeStartersPromise
        ).slice(0, lazy.MAX_NUM_MEMORIES_FOR_RESUME_ACTIVITY);

        if (selectedTab === this.#getCurrentTab()) {
          starters = [...resumeStarters, ...starters].slice(0, MAX_PILL_COUNT);
        }
      }
    } catch (e) {
      lazy.log.error("[Prompts] Failed to load initial starters:", e);
    }

    this.#starterPromptsAbortController = null;
    if (!abortController.signal.aborted) {
      this.#conversation.transientStarters = starters;
      this.#renderStarterPrompts(starters);
    }
  }

  #resumeActivitiesToStarterPrompts(resumeActivities) {
    return resumeActivities.flatMap(({ memory, content }) => {
      if (!content.headline.trim()) {
        return [];
      }

      return [
        {
          text: content.headline,
          type: "resume",
          previewIcons: content.previewTabs.map(({ url }) => ({
            iconSrc: `page-icon:${url}`,
          })),
          memory,
          content,
        },
      ];
    });
  }

  /**
   * Renders conversation starter prompts in the UI.
   * Sets the starters data and shows the prompts element.
   *
   * @param {Array<{text: string, type: string}>} starters - Array of starter prompt objects
   * @param {boolean} [resolved=true] - Whether starter loading has settled;
   *   false for the transient clear before an async load, which keeps Top
   *   Sites hidden until the prompts row is ready.
   * @param {boolean} [recordTelemetry=true] - Whether to record a
   *   quick_prompt_displayed event for this render; false for the
   *   transient skeleton render before resume starters resolve, so the
   *   event isn't recorded twice for the same load.
   * @private
   */
  #renderStarterPrompts(starters, resolved = true, recordTelemetry = true) {
    if (!this.isConnected) {
      return;
    }

    this.#starters = this.#conversation?.messages?.length ? [] : starters;
    this.showStarters = !!this.#starters.length;

    // Gate Top Sites on starter resolution so the prompts row and Top Sites
    // render in the same update, avoiding a layout shift where Top Sites
    // paint first and then jump down once the prompts row is inserted above.
    if (resolved) {
      this.startersResolved = true;
    }

    if (this.showStarters && recordTelemetry) {
      const resumePrompts = this.#starters.filter(
        starter => starter.type === "resume"
      ).length;
      this.onQuickPromptDisplayed(this.#starters.length, resumePrompts);
    }
    this.requestUpdate();
  }

  /**
   * Loads the user's Top Sites and renders a single row of them below the
   * Smartbar in fullpage mode. Sponsored sites are filtered out; we only keep
   * the first MAX_TOP_SITES entries to fit a single row.
   *
   * @private
   */
  /**
   * Loads or clears Top Sites based on the current mode and the
   * hideTopSites pref. Invoked on connect and whenever the pref changes
   * so every open AI window reflects the new value.
   *
   * @private
   */
  #syncTopSites() {
    if (this.mode === MODE.FULLPAGE) {
      Glean.smartWindow.topsitesEnabled.set(!this.hideTopSitesPref);
    }

    if (this.mode === MODE.FULLPAGE && !this.hideTopSitesPref) {
      // Only the visible tab can exhibit the prompts-row layout shift, so gate
      // its Top Sites on starter resolution (see #renderStarterPrompts).
      // Background tabs are hidden and never reload starters on tab switch, so
      // reveal their Top Sites immediately to avoid leaving them blank.
      if (this.ownerDocument.hidden) {
        this.startersResolved = true;
      }
      this.#loadTopSites();
    } else {
      this.topSites = [];
    }
  }

  #loadTopSites() {
    let sites = [];
    try {
      sites = lazy.AboutNewTab.getTopSites();
    } catch (e) {
      lazy.log.error("[TopSites] Failed to load top sites:", e);
    }

    if (!this.isConnected) {
      return;
    }

    this.topSites = (sites ?? [])
      .filter(site => site?.url && !site.sponsored_position)
      .slice(0, MAX_TOP_SITES);

    if (this.topSites.length) {
      Glean.smartWindow.topsitesImpression.record({
        visible_topsites: this.topSites.length,
      });
    }
  }

  /**
   * Navigates the current tab to the selected Top Site.
   *
   * @param {CustomEvent} event - The site-selected event
   * @private
   */
  #handleTopSiteSelected = event => {
    const { url, position } = event.detail;
    const win = this.#topChromeWindow;
    if (!url || !win) {
      return;
    }
    Glean.smartWindow.topsitesClick.record({
      position,
      visible_topsites: this.topSites.length,
    });
    lazy.URILoadingHelper.openTrustedLinkIn(win, url, "current");
  };

  /**
   * Helper method to get or create the smartbar element
   *
   * @param {Document} doc - The document
   */
  #getOrCreateSmartbar(doc) {
    // Find existing Smartbar or create it when we init the AI Window.
    let smartbar = this.renderRoot.querySelector("#ai-window-smartbar");

    if (!smartbar) {
      // The Smartbar can't be initialized in the shadow DOM and needs
      // to be created from the chrome document.
      smartbar = doc.createElement("moz-smartbar");
      smartbar.id = "ai-window-smartbar";
      smartbar.setAttribute("sap-name", "smartbar");
      smartbar.setAttribute("pageproxystate", "invalid");
      smartbar.setAttribute("popover", "manual");
      smartbar.classList.add("smartbar", "urlbar");

      // Listen before appending to DOM since the event fires synchronously
      // during connectedCallback.
      smartbar.addEventListener(
        "smartbar-initialized",
        () => {
          this.#resolveSmartbarReady();
          this.#setupSmartbarFocus(smartbar);
          this.#observeSmartbarHeight();
          this.#updateSmartbarModels(smartbar);
        },
        { once: true }
      );

      const smartbarWrapper = doc.createElement("div");
      smartbarWrapper.id = "smartbar-wrapper";
      smartbarWrapper.appendChild(smartbar);
      this.renderRoot.querySelector("#smartbar-slot").append(smartbarWrapper);

      // Always show the list of suggestions above input in sidebar mode and
      // below when in fullpage mode.
      smartbar.setAttribute(
        "suggestions-position",
        this.mode === MODE.SIDEBAR ? "top" : "bottom"
      );
      smartbar.isSidebarMode = this.mode == MODE.SIDEBAR;

      smartbar.addEventListener("input", this.#handleSmartbarInput);
      smartbar.addEventListener(
        "aiwindow-memories-toggle:on-change",
        this.#handleMemoriesToggle
      );
      smartbar.addEventListener(
        "smartbar-context-chips-changed",
        this.#handleContextChips
      );
    }
    this.#smartbar = smartbar;
    this.#memoriesButton = smartbar.querySelector("memories-icon-button");
    this.syncSmartbarMemoriesStateFromConversation();

    // Create toggle button, like with Smartbar above
    let toggleButton = this.renderRoot.querySelector("#smartbar-toggle-button");

    if (!toggleButton) {
      toggleButton = doc.createElement("moz-button");
      toggleButton.id = "smartbar-toggle-button";
      toggleButton.type = "primary";
      toggleButton.iconSrc =
        "chrome://browser/skin/smart-window-simplified.svg";
      toggleButton.setAttribute(
        "data-l10n-id",
        "smartwindow-switch-to-smart-window"
      );
      toggleButton.addEventListener("click", () => {
        const chromeWindow = window.browsingContext?.topChromeWindow;
        if (chromeWindow) {
          lazy.AIWindow.toggleAIWindow(chromeWindow, true);
        }
      });
      this.renderRoot.querySelector("#smartbar-slot").append(toggleButton);
    }
    this.#smartbarToggleButton = toggleButton;
    this.#updateSmartbarAndHeaderVisibility();
  }

  /**
   * Dispatches the context chips via ai-window:context-chips-changed
   * to the tab state manager to save them"
   *
   * @private
   */
  #handleContextChips = () => {
    this.#dispatchChromeEvent("ai-window:context-chips-changed", {
      bubbles: true,
      detail: {
        contextChips: this.#smartbar?.contextChips,
        removedImplicitContextChip: this.#smartbar?.removedImplicitContextChip,
        tab: this.#getEventTab(),
      },
    });
  };

  #setupSmartbarFocus(smartbar) {
    let hasAutoFocused = false;
    let isMouseClick = false;

    smartbar.addEventListener("mousedown", () => {
      isMouseClick = true;
      smartbar.toggleAttribute("suppress-focus-border", true);
    });

    smartbar.inputField.addEventListener("focus", () => {
      if (!hasAutoFocused) {
        smartbar.toggleAttribute("suppress-focus-border", true);
        hasAutoFocused = true;
      } else if (!isMouseClick) {
        smartbar.removeAttribute("suppress-focus-border");
      }
      isMouseClick = false;
    });

    smartbar.focus();
  }

  #observeSmartbarHeight() {
    const updateSmartbarHeight = () => {
      const urlbarView = this.#smartbar.querySelector(".urlbarView");
      // The height calculation for the Smartbar assumes that `.urlbarView`
      // is the only dynamically-sized child element.
      const smartbarHeightClosed =
        this.#smartbar.offsetHeight - urlbarView.offsetHeight;

      this.style.setProperty("--smartbar-height", `${smartbarHeightClosed}px`);
    };
    updateSmartbarHeight();

    this.#smartbarResizeObserver = new ResizeObserver(updateSmartbarHeight);
    this.#smartbarResizeObserver.observe(this.#smartbar);
  }

  /**
   * Handles input event from the Smartbar and dispatches
   * a ai-window:smartbar-input event to the window for
   * AIWindowTabStatesManager.sys.mjs to manage the input
   * state of the sidebar chat window.
   *
   * @private
   */
  #handleSmartbarInput = () => {
    this.#dispatchChromeEvent(
      "ai-window:smartbar-input",
      this.#getAIWindowEventOptions(this.#getSmartbarInputState())
    );
  };

  /**
   * Dispatches a TabStateEvent on the chrome window for the
   * AIWindowTabStatesManager.sys.mjs to catch state updates
   * for the ai-window.
   *
   * @param {string} eventName Name of the event
   * @param {TabStateEventOptions} [options={}] Event options/detail
   *
   * @private
   */
  #dispatchChromeEvent(eventName, options = {}) {
    const topChromeWindow = window?.browsingContext?.topChromeWindow;
    topChromeWindow?.dispatchEvent(
      new topChromeWindow.CustomEvent(eventName, options)
    );
  }

  /**
   * Handles the stop generation action from the smartbar.
   *
   * @private
   */
  #handleStopGeneration = () => {
    if (!this.#abortController) {
      return;
    }
    this.#abortController.abort();
    this.isGenerating = false;
    const lastAssistant = this.#conversation?.messages
      ?.filter(
        m => m.role == lazy.MESSAGE_ROLE.ASSISTANT && m?.content?.type == "text"
      )
      .at(-1);
    this.#dispatchMessageToChatContent({
      role: "assistant-message-complete",
      content: { id: lastAssistant?.id },
      historyResults: this.#conversation?.getHistoryResultsSnapshot() ?? [],
      citations: lastAssistant?.citations ?? [],
    });
  };

  /**
   * Handles the smartbar-commit action for the user prompt
   *
   * @param {CustomEvent} event - The smartbar-commit event
   * @private
   */
  #handleSmartbarCommit = event => {
    lazy.log.debug(
      "chatId[%s]: %s",
      this.#handleSmartbarCommit.name,
      this.conversationId
    );
    const {
      value,
      action,
      command,
      contextMentions = [],
      contextPageUrl,
      detectedIntent,
      event: triggeringEvent,
      location: sourceLocation,
      searchProvider,
      submitType: providedSubmitType,
    } = event.detail;

    const submitType =
      providedSubmitType ??
      (triggeringEvent?.type.startsWith("aiwindow-input-cta:")
        ? "button"
        : "enter");

    // Read inline @mentions before clearing input.
    const currentMentions =
      action === ACTION.CHAT
        ? this.#calculateCurrentMentions(contextMentions)
        : null;
    this.#smartbar.clearSmartbarInput();

    if (action === ACTION.CHAT) {
      if (
        lazy.AgentUI.tryHandleCommand({
          command,
          value,
          contextPageUrl,
          conversation: this.#conversation,
          window: this.#topChromeWindow,
        })
      ) {
        return;
      }

      const { mergedMentions, allUrls, inlineMentions } = currentMentions;

      if (allUrls.size) {
        this.#conversation.addSeenUrls(allUrls);
      }
      this.submitChatMessage({
        text: value,
        contextMentions: mergedMentions,
        contextPageUrl,
        detectedIntent,
        submitType,
        inlineMentionsCount: inlineMentions.length,
        sourceLocation,
      });
    } else if (action === ACTION.SEARCH) {
      Glean.smartWindow.searchSubmit.record({
        chat_id: this.conversationId,
        detected_intent: detectedIntent,
        length: value.length,
        location: sourceLocation ?? this.mode,
        message_seq: this.conversationMessageCount,
        model: this.modelName,
        provider: searchProvider,
        submit_type: submitType,
      });
    } else if (action === ACTION.NAVIGATE) {
      Glean.smartWindow.navigateSubmit.record({
        chat_id: this.conversationId,
        detected_intent: detectedIntent,
        length: value.length,
        location: sourceLocation ?? this.mode,
        message_seq: this.conversationMessageCount,
        model: this.modelName,
        submit_type: submitType,
      });
    }

    if (
      this.mode === MODE.SIDEBAR &&
      (action === ACTION.NAVIGATE || action === ACTION.SEARCH)
    ) {
      this.#dispatchChromeEvent(
        "ai-window:sidebar-navigating",
        this.#getAIWindowEventOptions()
      );
    }
  };

  /**
   * Merges "+" button chip mentions with inline "@" mentions, deduplicating
   * by URL, and returns the combined list plus the full set of URLs.
   *
   * @param {ContextWebsite[]} contextMentions - Chip mentions from the smartbar
   * @returns {{mergedMentions: ContextWebsite[], allUrls: Set<string>, inlineMentions: Array}}
   */
  #calculateCurrentMentions(contextMentions) {
    const contextUrls = new Set();
    for (const mention of contextMentions) {
      if (mention.url) {
        contextUrls.add(mention.url);
      }
    }

    const inlineMentions = this.#getInlineMentions();
    const atMentions = [];
    for (const mention of inlineMentions) {
      if (mention.id && !contextUrls.has(mention.id)) {
        atMentions.push({
          type: mention.type,
          url: mention.id,
          label: mention.label,
          iconSrc: lazy.UrlbarShared.getIconForUrl(mention.id),
        });
        contextUrls.add(mention.id);
      }
    }

    const mergedMentions = [...contextMentions, ...atMentions];

    return { mergedMentions, allUrls: contextUrls, inlineMentions };
  }

  /**
   * Returns inline @mention data from the editor's mentions plugin.
   *
   * @returns {Array<object>} Mention nodes from the editor
   */
  #getInlineMentions() {
    const editor = this.#smartbar?.inputField;
    if (!editor?.getAllMentions) {
      return [];
    }
    return editor.getAllMentions();
  }

  /**
   * @param {object} options
   * @param {string} options.text
   * @param {ChatSubmitType} options.submitType - How the request was submitted
   * @param {ContextWebsite[]} [options.contextMentions]
   * @param {?URL} [options.contextPageUrl] - Page URL string from the smartbar's current
   *   state. null means the user removed page context
   * @param {SmartbarAction} [options.detectedIntent] - The detected smarbar intent
   * @param {number} [options.inlineMentionsCount] - Number of inline mentions
   * @param {string} [options.sourceLocation] - Override smartbar location
   */
  submitChatMessage({
    text,
    submitType,
    contextMentions = [],
    contextPageUrl,
    detectedIntent,
    inlineMentionsCount = 0,
    sourceLocation,
  }) {
    const trimmed = String(text ?? "").trim();
    if (!trimmed) {
      return;
    }

    // Auto-cancel any active website confirmation when starting a new prompt
    lazy.ToolUI.autoCancelActiveConfirmation(
      this.#conversation,
      this.#topChromeWindow,
      this.mode
    ).catch(e => lazy.log.error("Failed to auto-cancel confirmation:", e));

    Glean.smartWindow.chatSubmit.record({
      chat_id: this.conversationId,
      detected_intent: detectedIntent,
      length: trimmed.length,
      location: sourceLocation ?? this.mode,
      mentions: inlineMentionsCount,
      message_seq: this.conversationMessageCount,
      model: this.modelName,
      submit_type: submitType,
      tabs: contextMentions.length,
    });

    if (this.#conversation) {
      this.#conversation.lastSubmitType = submitType;
    }

    this.#recordChatInteraction();
    this.#fetchAIResponse(trimmed, {
      ...this.#createUserRoleOpts(contextMentions),
      pageUrl: contextPageUrl,
    });
    this.#dispatchChromeEvent(
      "ai-window:smartbar-input",
      this.#getAIWindowEventOptions(lazy.EMPTY_SMARTBAR_INPUT_STATE, true)
    );
  }

  #handleMemoriesToggle = async event => {
    let memoriesCount = 0;
    try {
      const memories = await lazy.MemoriesManager.getAllMemories();
      memoriesCount = memories.length;
    } catch (e) {
      console.error("Failed to count memories", e);
    }

    Glean.smartWindow.memoriesToggle.record({
      location: this.mode,
      chat_id: this.conversationId,
      message_seq: this.#conversation?.messageCount ?? 0,
      memories: memoriesCount,
      toggle: event.detail.pressed,
    });

    lazy.log.debug(
      "chatId[%s]: %s",
      this.#handleMemoriesToggle.name,
      this.conversationId
    );

    this.#memoriesToggled = event.detail.pressed;
    this.#saveMemoriesToggleToConversation(event.detail.pressed);
    this.#syncMemoriesButtonUI();
  };

  #saveMemoriesToggleToConversation(pressed) {
    // Only save to database if conversation has messages to avoid constraint violation
    if (!this.#conversation || this.#conversation.messageCount === 0) {
      return;
    }

    this.#conversation.memoriesToggled = pressed;
    this.#updateConversation();
  }

  /**
   * Handles the prompt selection event from smartwindow-prompts.
   *
   * @param {CustomEvent} event - The prompt-selected event
   * @private
   */
  #handlePromptSelected = event => {
    if (event.detail.type === "resume") {
      this.#handleResumePromptSelected(event.detail);
      return;
    }

    this.onQuickPromptClicked(event.detail.text, true);
  };

  #handleResumePromptSelected(resumePrompt) {
    if (this.#isGeneratingResumeActivityConversation) {
      return;
    }

    this.#recordQuickPromptClicked("resume");

    this.#generateResumeActivityConversation(resumePrompt).catch(e =>
      lazy.log.error("[Prompts] Resume-activity generation failed:", e)
    );
  }

  /**
   * Builds a conversation seeded with the selected resume-activity
   * suggestion and generates its first response, attaching an open_tabs
   * confirmation card built from the pill's preview tabs. Uses a plain
   * (memory-free) conversation instead of the memory-summarizing one when
   * memories are toggled off, or if building the memory-driven version
   * fails - the confirmation card itself only needs the preview tabs, not
   * the memory content.
   *
   * @param {object} resumePrompt - The clicked resume-activity prompt,
   *   carrying the raw memory/content the generator needs.
   */
  async #generateResumeActivityConversation(resumePrompt) {
    const conversationAtClick = this.#conversation;
    let conversation = null;
    this.#isGeneratingResumeActivityConversation = true;
    try {
      if (this.#memoriesToggled ?? this.#memoriesIconShown) {
        try {
          conversation = await lazy.constructConversationToResumeActivity({
            memory: resumePrompt.memory,
            content: resumePrompt.content,
          });
        } catch (e) {
          lazy.log.error(
            "[Prompts] Failed to create resume-activity conversation:",
            e
          );
        }
      }
      if (!conversation) {
        try {
          conversation = await this.#buildPlainResumeConversation(resumePrompt);
        } catch (e) {
          lazy.log.error(
            "[Prompts] Failed to create plain resume-activity conversation:",
            e
          );
        }
      }
    } finally {
      this.#isGeneratingResumeActivityConversation = false;
    }

    // The conversation may have changed while generation was in flight.
    if (
      !conversation ||
      this.#conversation !== conversationAtClick ||
      !resumePrompt.content.previewTabs?.length
    ) {
      return;
    }

    const tabs = resumePrompt.content.previewTabs.map(
      ({ url, title }, index) => ({
        token: String(index),
        url,
        title,
        iconSrc: `page-icon:${url}`,
        checked: false,
      })
    );
    await this.reloadAndGenerate(conversation, {
      uiType: "tab-group-confirmation",
      toolCallId: `resume-activity-${resumePrompt.memory.id}`,
      properties: {
        actionType: "open_tabs",
        tabGroupLabel: resumePrompt.text,
        tabs,
      },
    });
  }

  /**
   * Builds a conversation seeded with just the resume-activity headline and
   * no memory content in the system prompt - used when memories are
   * toggled off, or as a fallback if the memory-driven builder fails.
   *
   * @param {object} resumePrompt
   * @returns {Promise<ChatConversation>}
   */
  async #buildPlainResumeConversation(resumePrompt) {
    const { engine, parameters } = await lazy.buildEngineForFeature(
      lazy.MODEL_FEATURES.CHAT,
      { flowId: null, modelChoiceIdOverride: this.#selectedModelChoiceId }
    );
    const conversation = new lazy.ChatConversation({
      title: resumePrompt.content.headline,
    });
    conversation.engine = engine;
    conversation.parameters = parameters;
    await conversation.loadSystemPrompt();
    conversation.addUserMessage(resumePrompt.content.headline);
    conversation.securityProperties.setPrivateData();
    conversation.securityProperties.setUntrustedInput();
    conversation.securityProperties.commit();
    return conversation;
  }

  /**
   * Records a quick_prompt_displayed Glean event.
   * Called for both conversation starters and follow-up suggestions.
   *
   * @param {number} prompts - Number of prompts shown
   * @param {number} [resumePrompts] - Number of those prompts that were
   *   resume pills
   */
  onQuickPromptDisplayed = (prompts, resumePrompts = 0) => {
    Glean.smartWindow.quickPromptDisplayed.record({
      location: this.mode,
      chat_id: this.conversationId,
      message_seq: this.#conversation?.messageCount ?? 0,
      prompts,
      resume_prompts: resumePrompts,
    });
  };

  /**
   * Records a quick_prompt_clicked Glean event.
   *
   * @param {"default"|"resume"|"followup"} starterType - The type of prompt
   *   that was clicked.
   */
  #recordQuickPromptClicked(starterType) {
    Glean.smartWindow.quickPromptClicked.record({
      location: this.mode,
      chat_id: this.conversationId,
      message_seq: this.#conversation?.messageCount ?? 0,
      starter: starterType !== "followup",
      starter_type: starterType,
    });
  }

  /**
   * Records a quick_prompt_clicked Glean event and submits the prompt.
   * Called for both conversation starters and follow-up suggestions.
   *
   * @param {string} text - The prompt text to submit
   * @param {boolean} starter - Whether this is a conversation starter
   * @param {ContextWebsite[]} [contextMentionsOverride] - Website context
   * supplied by the prompt
   */
  onQuickPromptClicked(text, starter, contextMentionsOverride) {
    this.#recordQuickPromptClicked(starter ? "default" : "followup");

    const { pageUrl: contextPageUrl, contextWebsites } =
      this.#smartbar.getCurrentContextData();

    const submitType = starter ? "starter" : "follow-up";
    this.submitChatMessage({
      text,
      contextMentions: contextMentionsOverride ?? contextWebsites,
      contextPageUrl,
      submitType,
    });
  }

  onOpenLink() {
    Glean.smartWindow.linkClick.record({
      location: this.mode,
      chat_id: this.conversationId,
      message_seq: this.#conversation?.messageCount ?? 0,
    });
  }

  /**
   * Creates a UserRoleOpts object with current memories settings.
   *
   * @param {ContextWebsite[]} [contextMentions]
   * @returns {UserRoleOpts} Options object with memories configuration
   * @private
   */
  #createUserRoleOpts(contextMentions) {
    return new lazy.UserRoleOpts({
      memoriesEnabled: this.#memoriesToggled ?? this.#memoriesIconShown,
      memoriesFlagSource:
        this.#memoriesToggled == null
          ? lazy.MEMORIES_FLAG_SOURCE.GLOBAL
          : lazy.MEMORIES_FLAG_SOURCE.CONVERSATION,
      contextMentions,
    });
  }

  /**
   * Persists the current conversation state to the database.
   *
   * @private
   */
  async #updateConversation() {
    await lazy.AIWindow.chatStore
      .updateConversation(this.#conversation)
      .catch(updateError => {
        lazy.log.error(`Error updating conversation: ${updateError.message}`);
      });
  }

  /**
   * Generates and sets a title for the conversation if one doesn't exist.
   *
   * @param {string} [assistantResponse] - The first assistant response text
   * @private
   */
  async #addConversationTitle(assistantResponse) {
    if (this.#conversation.title || this.#conversation.titlePromise) {
      return;
    }

    const startTime = ChromeUtils.now();

    const firstUserMessage = this.#conversation.messages.find(
      m => m.role === lazy.MESSAGE_ROLE.USER
    );

    this.#conversation.titlePromise = lazy.generateChatTitle(
      firstUserMessage?.content?.body,
      {
        url: firstUserMessage?.pageUrl?.href || "",
        title: this.#conversation.pageMeta?.title || "",
        description: this.#conversation.pageMeta?.description || "",
      },
      assistantResponse,
      this.conversationId
    );
    const title = await this.#conversation.titlePromise;
    delete this.#conversation.titlePromise;

    this.#conversation.title = title;
    document.title = title;
    this.#updateConversation();

    ChromeUtils.addProfilerMarker(
      "SmartWindow",
      { startTime },
      "Title generation"
    );
  }

  #updateTabFavicon() {
    if (this.classList.contains("chat-active") || this.mode !== MODE.FULLPAGE) {
      return;
    }
    const link = document.getElementById("tabIcon");
    link.href = TAB_FAVICON_CHAT;
  }

  #resetConversationState() {
    this.classList.remove("chat-active");
    this.#updateBrowserTabbable();
    this.#hostBrowser?.setAttribute(
      "data-conversation-id",
      this.#conversation.id
    );
    this.#syncHistoryState();
  }

  #setBrowserContainerActiveState(isActive) {
    if (isActive) {
      this.classList.add("chat-active");
      this.#updateBrowserTabbable();
      this.#smartbar?.suppressStartQuery({ permanent: true });
      this.#smartbar?.view.close();
      if (this.#smartbar?.inputField) {
        this.#smartbar.inputField.showPlaceholderAnimation = false;
      }
      return;
    }

    this.classList.remove("chat-active");
    this.#updateBrowserTabbable();
    this.#smartbar?.unsuppressStartQuery();
    if (this.#smartbar?.inputField) {
      this.#smartbar.inputField.showPlaceholderAnimation =
        this.mode === MODE.FULLPAGE;
    }
  }

  /**
   * Fetches an AI response based on the current user prompt.
   * Validates the prompt, updates conversation state, streams the response,
   * and dispatches updates to the browser actor.
   *
   * @private
   *
   * @param {string} [inputText] - The already trimmed and non-empty input text from the
   *   user. If this argument is not provided then the conversation will resume either
   *   from tool calls or from an error.
   * @param {object} [options]
   * @param {boolean} [options.skipUserDispatch=false] - If true, do not dispatch
   * a user message into chat content (used for retries to avoid duplicate
   * user messages).
   * @param {boolean} [options.memoriesEnabled] - Optional per-call override for
   * memory injection; undefined falls back to use global/default behavior.
   * @param {URL|null} [options.pageUrl] - Page URL to associate with the
   * message, or null if the user removed page context.
   * @param {boolean} [options.isRetry=false] - True when the call originated
   * from a user-initiated retry; surfaced in model_response telemetry.
   * @param {boolean} [options.ensureAssistantResponse=false] - When true and
   * no inputText is provided, add the empty assistant message the stream
   * writes into. Used when the user turn is already present in the
   * conversation (e.g. resume-activity starters), so generatePrompt is not
   * called to create it.
   * @param {boolean} [options.skipSystemPromptRefresh=false] - When true,
   * skip reloading the system prompt for this call. Used when a caller has
   * just built a bespoke system prompt (e.g. resume-activity starters) that
   * should survive this one request unchanged.
   * @param {object} [options.assistantToolUIData] - When set alongside
   * ensureAssistantResponse, attached to the empty assistant message so it
   * renders alongside the streamed response (e.g. a tab-selection card for
   * resume-activity starters).
   */
  async #fetchAIResponse(
    inputText,
    {
      skipUserDispatch = false,
      pageUrl,
      isRetry = false,
      ensureAssistantResponse = false,
      skipSystemPromptRefresh = false,
      assistantToolUIData,
      ...userOpts
    } = {}
  ) {
    // Capture conversation and browsingContext at call time so that a tab switch
    // mid-stream cannot redirect this request to the wrong target.
    const conversation = this.#conversation;
    const browsingContext = this.#getBrowsingContext();

    this.#starterPromptsAbortController?.abort();
    this.showStarters = false;
    this.showFooter = false;
    this.showDisclaimer = true;
    this.#updateTabFavicon();
    this.#setBrowserContainerActiveState(true);

    this.#abortController?.abort();
    this.#abortController = new AbortController();
    const { signal } = this.#abortController;
    const stopWatchingTabClose =
      this.#watchTabCloseForAbort(browsingContext, this.#abortController) ??
      (() => {});
    this.isGenerating = true;

    const requestStart = ChromeUtils.now();
    let firstTokenTime = null;
    const onUpdate = (_e, message) => {
      if (message.role !== lazy.MESSAGE_ROLE.ASSISTANT) {
        return;
      }
      firstTokenTime = ChromeUtils.now();
      ChromeUtils.addProfilerMarker(
        "SmartWindow",
        { startTime: requestStart },
        "Time to first token (TTFT)"
      );
      conversation?.off("chat-conversation:message-update", onUpdate);
    };
    conversation.on("chat-conversation:message-update", onUpdate);

    try {
      // Kicked off before the engine and prompt work, which it has no data
      // dependency on, so a cold or expired token doesn't add a round-trip
      // between the user hitting send and the request going out.
      const fxAccountTokenPromise = lazy.openAIEngine.getFxAccountToken();

      const { engine, parameters } = await lazy.buildEngineForFeature(
        lazy.MODEL_FEATURES.CHAT,
        {
          flowId: this.conversationId,
          modelChoiceIdOverride: this.#selectedModelChoiceId,
        }
      );
      conversation.engine = engine;
      conversation.parameters = parameters;

      // Rewrites the system prompt in place so a restored conversation gets
      // today's timestamp and the latest RS content. The engine was just built
      // for this model choice, so its model drives the v2 assembly. Skipped
      // when a caller has just built a bespoke system prompt of its own,
      // so it survives this one request unchanged.
      if (!skipSystemPromptRefresh) {
        await conversation.loadSystemPrompt();
      }

      if (inputText) {
        await conversation.generatePrompt(
          inputText,
          pageUrl,
          userOpts,
          skipUserDispatch
        );

        conversation.addAssistantMessage("text", "");

        this.#sendModelRequestTelemetryEvent();
      } else if (ensureAssistantResponse) {
        // The user turn is already in the conversation, so generatePrompt is
        // skipped; add the empty assistant message receiveResponse streams into.
        const assistantMessage = conversation.addAssistantMessage("text", "");
        if (assistantToolUIData) {
          assistantMessage.toolUIData = assistantToolUIData;
        }
        this.#sendModelRequestTelemetryEvent();
      }

      await lazy.Chat.fetchWithHistory({
        conversation,
        browsingContext,
        mode: this.mode,
        signal,
        fxAccountTokenPromise,
      });

      ChromeUtils.addProfilerMarker(
        "SmartWindow",
        { startTime: requestStart },
        "Total turnaround time"
      );

      this.#sendModelResponseTelemetryEvent(
        null,
        this.#getModelRequestLatencyAndDuration(requestStart, firstTokenTime),
        { isRetry }
      );
    } catch (e) {
      if (!signal.aborted) {
        this.showSearchingIndicator(false, null);
        this.#handleError(
          e,
          this.#getModelRequestLatencyAndDuration(requestStart, firstTokenTime),
          { isRetry }
        );
      }
      this.requestUpdate?.();
    } finally {
      stopWatchingTabClose();
      if (this.#abortController?.signal === signal) {
        this.isGenerating = false;
        this.#abortController = null;
      }
    }
  }

  /**
   * Aborts the given controller when the tab that owns the captured
   * browsingContext is closed. Sidebar mode only — in fullpage mode the AI
   * window itself owns the browsingContext, and tearing down the element
   * already stops generation. Returns a cleanup function, or null if no
   * watcher was installed.
   *
   * @param {BrowsingContext} browsingContext
   * @param {AbortController} controller
   * @returns {(() => void) | null}
   */
  #watchTabCloseForAbort(browsingContext, controller) {
    if (this.mode != MODE.SIDEBAR || !browsingContext) {
      return null;
    }
    const browser = browsingContext.embedderElement;
    const chromeWin = window.browsingContext?.topChromeWindow;
    const tab = browser && chromeWin?.gBrowser?.getTabForBrowser(browser);
    if (!tab) {
      return null;
    }
    const onTabClose = () => controller.abort();
    tab.addEventListener("TabClose", onTabClose);
    return () => tab.removeEventListener("TabClose", onTabClose);
  }

  updated(changedProps) {
    super.updated?.(changedProps);
    if (changedProps.has("isGenerating")) {
      if (this.#smartbar) {
        this.#smartbar.assistantIsGenerating = this.isGenerating;
      }
      this.#getAIChatContentActor()?.setGeneratingOnChatContent(
        this.isGenerating
      );
    }
    if (changedProps.has("availableModels")) {
      this.#setModelChoice(this.#selectedModelChoiceId);
      if (this.#smartbar) {
        this.#updateSmartbarModels(this.#smartbar);
      }
    }
  }

  #onMessageComplete = (_event, msg) => {
    this.#addConversationTitle(msg?.content?.body);

    // Check if we need to inject retry toolUIData
    // This handles the case where a user cancelled a website confirmation dialog
    // and then submitted a new prompt. The cancelled confirmation's original prompt
    // is stored in conversation.pendingRetry. When this new message completes,
    // we inject a retry UI component at the top of the message, allowing the user
    // to retry the previously cancelled action if they wish.
    const retryInjected = lazy.ToolUI.injectRetryToolUIDataIfNeeded(
      msg,
      this.#conversation
    );

    // If retry toolUIData was injected, dispatch the updated message
    if (retryInjected) {
      this.#dispatchMessageToChatContent({
        ...msg,
        role: "assistant",
        isPreviousMessage: false,
        // Deep clone toolUIData to prevent UI mutations from affecting the conversation model
        toolUIData: msg.toolUIData
          ? structuredClone(msg.toolUIData)
          : undefined,
      });
    }

    this.#dispatchMessageToChatContent({
      role: "assistant-message-complete",
      content: {
        id: msg?.id,
      },
      // Carry the history results snapshot with completion so the content page
      // renders the grid even if the streaming-time dispatch was delayed or
      // missed (its delivery races the message lifecycle).
      historyResults: this.#conversation?.getHistoryResultsSnapshot() ?? [],
      citations: msg?.citations ?? [],
    });
    const followupCount = msg?.tokens?.followup?.length;
    if (followupCount) {
      this.onQuickPromptDisplayed(followupCount);
    }
    if (msg?.memoriesApplied?.length) {
      this.onMemoriesApplied();
    }
  };

  #getModelRequestLatencyAndDuration(requestStart, firstTokenTime) {
    const duration = Math.round(ChromeUtils.now() - requestStart);
    const latency = firstTokenTime
      ? Math.round(firstTokenTime - requestStart)
      : 0;
    return { duration, latency };
  }

  get modelName() {
    return lazy.getCurrentModelName();
  }

  #getConversationLastMessageAndCount(role) {
    if (!this.#conversation) {
      return { lastMessage: null, messageCount: 0 };
    }

    let lastMessage = null;
    let messageCount = 0;
    let countAtLastMatch = 0;
    for (const message of this.#conversation.messages.slice(1)) {
      if (message.content?.type === "text") {
        messageCount++;
      }
      if (message.role === role) {
        lastMessage = message;
        countAtLastMatch = messageCount;
      }
    }
    return { lastMessage, messageCount: countAtLastMatch };
  }

  #sendModelResponseTelemetryEvent(
    error,
    { duration, latency },
    { isRetry = false } = {}
  ) {
    const { lastMessage: lastAssistantMessage, messageCount } =
      this.#getConversationLastMessageAndCount(lazy.MESSAGE_ROLE.ASSISTANT);
    const { name: errorName, httpStatus } = error
      ? resolveModelResponseError(error)
      : { name: "", httpStatus: 0 };

    Glean.smartWindow.modelResponse.record({
      location: this.mode === MODE.FULLPAGE ? "home" : MODE.SIDEBAR,
      chat_id: this.conversationId,
      message_seq: messageCount,
      request_id: lastAssistantMessage?.parentMessageId,
      intent: "chat",
      tokens: lazy.Chat.lastUsage?.completion_tokens ?? 0,
      memories: lastAssistantMessage?.memoriesApplied?.length ?? 0,
      latency,
      duration,
      error: errorName,
      http_status: httpStatus,
      model: this.modelName,
      is_retry: isRetry,
    });
  }

  /**
   * Build the correlation extras attached to smart_window.client_error events.
   * Exposed publicly so AIChatContentParent can populate them when relaying a
   * failure from the chat content document.
   *
   * @returns {{location: string, chat_id: string, message_seq: number, model: string}}
   */
  getClientErrorContext() {
    const { messageCount } = this.#getConversationLastMessageAndCount(
      lazy.MESSAGE_ROLE.ASSISTANT
    );
    return {
      location: this.mode === MODE.FULLPAGE ? "home" : MODE.SIDEBAR,
      chat_id: this.conversationId ?? "",
      message_seq: messageCount,
      model: this.modelName ?? "",
    };
  }

  #sendModelRequestTelemetryEvent() {
    const { lastMessage: lastUserMessage, messageCount } =
      this.#getConversationLastMessageAndCount(lazy.MESSAGE_ROLE.USER);

    Glean.smartWindow.modelRequest.record({
      location: this.mode === MODE.FULLPAGE ? "home" : MODE.SIDEBAR,
      chat_id: this.conversationId,
      message_seq: messageCount,
      request_id: lastUserMessage?.id,
      detected_intent: "chat",
      intent: "chat",
      tokens: lazy.Chat.lastUsage?.completion_tokens ?? 0,
      memories: lastUserMessage?.memoriesApplied?.length ?? 0,
    });
  }

  #getBrowsingContext() {
    // Use the adjacent tab's browsing context for sidebar or current for
    // fullpage for tools that need context.
    return this.mode === MODE.SIDEBAR
      ? window.browsingContext.topChromeWindow.gBrowser.selectedBrowser
          .browsingContext
      : window.browsingContext;
  }

  #handleError(error, { latency, duration }, { isRetry = false } = {}) {
    console.error(error);
    const newErrorMessage = {
      role: "",
      content: {
        isError: true,
        error: getErrorCode(error),
        httpStatus: error.status ?? 0,
        clientReason: error.clientReason,
      },
    };
    this.#sendModelResponseTelemetryEvent(
      error,
      { latency, duration },
      { isRetry }
    );
    this.#dispatchMessageToChatContent(newErrorMessage);
  }

  /**
   * A helper function to dispatches the current conversation's seen urls to the
   * chat content.
   *
   * @param {AIChatContentParent} actor
   */
  #dispatchSeenUrls(actor) {
    if (!this.#conversation?.id) {
      return;
    }
    actor.dispatchSeenUrlsToChatContent({
      conversationId: this.#conversation.id,
      seenUrls: this.#conversation.seenUrls,
    });
  }

  /**
   * Retrieves the AIChatContent actor from the browser's window global.
   *
   * @returns {Promise<object|null>} The AIChatContent actor, or null if unavailable.
   * @private
   */
  #getAIChatContentActor() {
    if (!this.#browser) {
      lazy.log.warn("AI browser not set, cannot get AIChatContent actor");
      return null;
    }

    const windowGlobal = this.#browser.browsingContext?.currentWindowGlobal;

    if (!windowGlobal) {
      lazy.log.warn("No window global found for AI browser");
      return null;
    }
    try {
      return windowGlobal.getActor("AIChatContent");
    } catch (error) {
      lazy.log.error("Failed to get AIChatContent actor:", error);
      return null;
    }
  }

  /**
   * Dispatches a message to the AIChatContent actor.
   *
   * @param {ChatMessage} message - message to dispatch to chat content actor
   * @returns
   */

  #dispatchMessageToActor(actor, message) {
    const newMessage = { ...message };
    this.#maybeSetMemoriesCalloutData(newMessage);

    if (typeof message.role !== "string") {
      const roleLabel = lazy.getRoleLabel(newMessage.role).toLowerCase();
      newMessage.role = roleLabel;
    }

    // Role is just the transport gate here. uiType gets set below and is
    // what the renderer keys off
    if (newMessage.role === "tool") {
      const cfg = lazy.getActionLogConfigForTool(
        newMessage.content?.name,
        newMessage.content?.body
      );
      if (!cfg.show) {
        return null;
      }

      newMessage.actionLog = {
        uiType: lazy.ACTION_LOG_UI_TYPE,
        pendingLabel: cfg.pendingLabel,
        row: lazy.buildActionLogRow(
          newMessage.content?.name,
          cfg.label,
          newMessage.content?.body,
          newMessage.content?.args,
          cfg.link
        ),
      };
    }

    return actor.dispatchMessageToChatContent(newMessage);
  }

  #maybeSetMemoriesCalloutData(newMessage) {
    if (
      newMessage.role !== lazy.MESSAGE_ROLE.ASSISTANT ||
      !newMessage.memoriesApplied?.length ||
      Services.prefs.getBoolPref(PREF_MEMORIES_HAS_SEEN_MEMORIES, false)
    ) {
      return;
    }

    newMessage.showMemoriesCallout = true;
    Services.prefs.setBoolPref(PREF_MEMORIES_HAS_SEEN_MEMORIES, true);
  }

  #dispatchMessageToChatContent(message) {
    const actor = this.#getAIChatContentActor();
    return actor ? this.#dispatchMessageToActor(actor, message) : null;
  }

  /**
   * Delivers messages to the child process if there are some pending when the
   * parent actor receives AIChatContent:Ready event from the child process.
   */
  onContentReady() {
    if (this.#pendingRestoreConversation) {
      const conv = this.#pendingRestoreConversation;
      this.#pendingRestoreConversation = null;
      this.openConversation(conv);
      return;
    }
    const actor = this.#getAIChatContentActor();
    if (actor) {
      if (this.#conversation?.messages?.length) {
        this.#pendingMessageDelivery = true;
      }
      this.#deliverConversationMessages(actor);
      actor.setGeneratingOnChatContent(this.isGenerating);
    }
  }

  /**
   * Delivers all of the messages of a conversation to the child process
   *
   * @param {JSActor} actor
   */
  #deliverConversationMessages(actor) {
    this.#dispatchSeenUrls(actor);

    if (!this.#pendingMessageDelivery) {
      return;
    }

    this.#pendingMessageDelivery = false;

    if (!this.#conversation || !this.#conversation.messages.length) {
      return;
    }

    this.#setBrowserContainerActiveState(true);

    // @todo Bug2013096
    // Add way to batch these messages to the actor in one message
    this.#conversation.renderState().forEach(message => {
      this.#dispatchMessageToActor(actor, {
        ...message,
        isPreviousMessage: true,
      });
    });

    // send a message to restore the scroll position after a conversation was restored
    this.#dispatchMessageToActor(actor, {
      role: "restored-all-messages-in-a-conversation",
      convId: this.#conversation.id,
    });
  }

  /**
   * Resolves the tab this ai-window instance relates to: for fullpage that's
   * the tab hosting the element; for sidebar (no owner tab), fall back to the
   * currently selected tab the sidebar reflects. Intention is to get the
   * correct reference for fullpage tabs that might be opening in the
   * background, like for session restore or tab restores.
   *
   * @returns {?MozTabbrowserTab}
   *
   * @private
   */
  #getEventTab() {
    const gBrowser = window?.browsingContext?.topChromeWindow?.gBrowser;
    const ownerTab = this.#hostBrowser
      ? gBrowser?.getTabForBrowser(this.#hostBrowser)
      : null;
    return ownerTab ?? gBrowser?.selectedTab;
  }

  /**
   * Gets event options for a TabStateEvent
   *
   * @param {false|string} [input=false] The latest input contents
   * @param {boolean} [isAsk=false] Whether the input is an ask chat message
   *
   * @returns {TabStateEventOptions}
   *
   * @private
   */
  #getAIWindowEventOptions(input = false, isAsk = false) {
    return {
      bubbles: true,
      detail: {
        input,
        isAsk,
        mode: this.mode,
        pageUrl: lazy.getCurrentTabUrl(window),
        conversation: this.#conversation,
        conversationId: this.#getDataConvId(),
        modelChoiceId: this.#hasModelChoiceOverride
          ? this.#selectedModelChoiceId
          : null,
        tab: this.#getEventTab(),
      },
    };
  }

  /**
   * Remove the event listeners from the current conversation, update the
   * conversation reference, and attach chat-conversation event listeners.
   *
   * @param {ChatConversation} conversation
   *
   * @private
   */
  #swapConversation(conversation) {
    this.#removeConversationListeners();
    this.#conversation = conversation;
    this.#attachConversationListeners();
    this.syncSmartbarMemoriesStateFromConversation();
    this.#kitMention?.reset();

    // If the new conversation is empty and already has cached starters
    // (tab switch-back), restore them synchronously so they appear without
    // waiting on the conversation-changed dedup roundtrip.
    if (
      conversation &&
      !conversation.messageCount &&
      conversation.transientStarters?.length
    ) {
      this.#renderStarterPrompts(conversation.transientStarters);
    }

    this.#dispatchChromeEvent(
      "ai-window:conversation-changed",
      this.#getAIWindowEventOptions()
    );
  }

  /**
   * Opens a new conversation and renders the conversation in the child process.
   *
   * @param {ChatConversation} conversation
   */
  openConversation(conversation) {
    if (conversation?.messageCount) {
      this.#swapConversation(conversation);

      this.#syncHistoryState();

      if (this.#conversation.title) {
        document.title = this.#conversation.title;
      }
      this.#updateTabFavicon();
      this.#hostBrowser?.setAttribute(
        "data-conversation-id",
        this.#conversation.id
      );

      // Update smartbar chips to reflect the current tab when sidebar reopens
      if (this.#smartbar && this.mode === MODE.SIDEBAR) {
        this.#smartbar.updateContextChips();
      }

      // This assumes "openConversation" opens an active conversation, possible todo to see
      // if convo has messages before hiding the footer element.
      this.showFooter = false;

      this.showDisclaimer = true;
      this.showStarters = false;
      const actor = this.#getAIChatContentActor();

      this.#pendingMessageDelivery = true;

      if (this.#browser && actor) {
        this.#deliverConversationMessages(actor);
      }
    } else {
      this.clearChat(conversation);
    }

    this.#dispatchChromeEvent(
      "ai-window:opened-conversation",
      this.#getAIWindowEventOptions()
    );
  }

  #getCurrentTab() {
    return (
      window.browsingContext?.topChromeWindow?.gBrowser?.selectedTab ?? null
    );
  }

  onCreateNewChatClick() {
    this.clearChat();
  }

  clearChat(conversation = null) {
    // Clear conversation state. The caller may provide an existing empty
    // conversation to reuse (tab switch-back case); otherwise create a fresh
    // one.
    this.#swapConversation(conversation ?? new lazy.ChatConversation({}));

    this.#syncHistoryState();

    const hostBrowser = window.browsingContext?.embedderElement;
    hostBrowser?.setAttribute("data-conversation-id", this.#conversation.id);

    // Reset memories toggle state
    this.#memoriesToggled = null;
    this.#syncMemoriesButtonUI();

    // Show Smartbar suggestions for cleared chats
    this.#smartbar?.unsuppressStartQuery();

    // Clear the conversation ID from the tab state manager
    this.#dispatchChromeEvent(
      "ai-window:clear-conversation",
      this.#getAIWindowEventOptions()
    );

    // Submitting a message with a new convoId here.
    // This will clear the chat content area in the child process via side effect.
    this.#dispatchMessageToChatContent({
      role: "clear-conversation",
      content: { body: "" },
    });

    if (this.mode !== MODE.FULLPAGE) {
      // Hide chat-active state (fullpage stays active to keep the chat layout)
      this.#setBrowserContainerActiveState(false);
    }

    // Hide starters if we don't already have cached ones to show —
    // #swapConversation restores them synchronously on tab switch-back.
    if (this.#conversation && !this.#conversation.transientStarters?.length) {
      this.showStarters = false;
    }
  }

  #onCloseSidebarClick() {
    this.#dispatchChromeEvent("ai-window:close-sidebar");
  }

  /** Loads recent conversations for the history menu. */
  async #refreshRecentChats() {
    try {
      const items = await lazy.AIWindow.chatStore.findRecentConversations(
        HISTORY_MENU_MAX_RECENT_CHATS
      );
      this.recentChats = items.map(item => ({
        id: item.id,
        title: item.title,
        pageUrl: item.pageUrl,
      }));
    } catch (e) {
      lazy.log.error("Failed to load recent chats for history menu", e);
      this.recentChats = [];
    }
  }

  /**
   * Opens a recent chat: switch to its tab if open, otherwise reopen it.
   *
   * @param {string} conversationId
   */
  async #onRecentChatSelected(conversationId) {
    const conversation =
      await lazy.AIWindow.chatStore.findConversationById(conversationId);
    if (!conversation) {
      return;
    }

    const win = this.#topChromeWindow;
    if (!win) {
      this.openConversation(conversation);
      return;
    }

    const existingTab = Array.from(win.gBrowser.tabs).find(tab => {
      const browser = tab.linkedBrowser;
      return (
        browser?.getAttribute("data-conversation-id") === conversationId &&
        browser.currentURI &&
        lazy.AIWindow.isAIWindowContentPage(browser.currentURI)
      );
    });

    if (existingTab) {
      win.gBrowser.selectedTab = existingTab;
      return;
    }

    // Tab was closed: reopen it on the page it was about.
    lazy.AIWindowUI.reopenConversationInTab(win, conversation);
  }

  /** Opens the Chats section of Firefox View. */
  #onViewAllChatsSelected() {
    this.#topChromeWindow?.FirefoxViewHandler.openTab("chats");
  }

  /** Opens the Smart Window preferences. */
  #onSmartWindowSettingsSelected() {
    this.#topChromeWindow?.openPreferences("personalizeSmartWindow");
  }

  // Handles action events from <smartwindow-history-menu>.
  #onHistoryMenuEvent = event => {
    switch (event.type) {
      case "smartwindow-history-menu:new-chat":
        this.onCreateNewChatClick();
        break;
      case "smartwindow-history-menu:open-chat":
        this.#onRecentChatSelected(event.detail.conversationId);
        break;
      case "smartwindow-history-menu:view-all-chats":
        this.#onViewAllChatsSelected();
        break;
      case "smartwindow-history-menu:open-settings":
        this.#onSmartWindowSettingsSelected();
        break;
      case "smartwindow-history-menu:request-recent-chats":
        this.#refreshRecentChats();
        break;
    }
  };

  // Renders the <smartwindow-history-menu> for the given mode.
  #historyMenu(mode) {
    return html`<smartwindow-history-menu
      mode=${mode}
      .recentChats=${this.recentChats}
    ></smartwindow-history-menu>`;
  }

  showSearchingIndicator(isSearching, searchQuery) {
    this.#dispatchMessageToChatContent({
      role: "loading",
      isSearching,
      searchQuery,
      convId: this.conversationId,
      content: { body: "" },
    });
  }

  async reloadAndContinue(conversation) {
    if (!conversation) {
      return;
    }
    this.openConversation(conversation);
    this.#continueAfterToolResult();
  }

  /**
   * Opens a pre-built conversation whose last message is a user turn and
   * generates the assistant reply. Unlike reloadAndContinue (which resumes
   * an in-flight tool turn), this adds the assistant placeholder the stream
   * needs, and skips the usual system-prompt reload since the conversation
   * was just built with its own bespoke system prompt.
   *
   * @param {ChatConversation} conversation
   * @param {object} [assistantToolUIData] - Attached to the assistant
   *   message so it renders alongside the streamed response (e.g. a
   *   tab-selection card for resume-activity starters).
   */
  async reloadAndGenerate(conversation, assistantToolUIData) {
    if (!conversation) {
      return;
    }
    const userMessage = conversation.messages.at(-1);
    await conversation.injectRealTimeContext(userMessage, {});
    this.openConversation(conversation);
    this.#fetchAIResponse(undefined, {
      ensureAssistantResponse: true,
      skipSystemPromptRefresh: true,
      assistantToolUIData,
    });
  }

  async #continueAfterToolResult() {
    // Show searching indicator if the last tool was run_search
    const lastToolCall = this.#conversation.messages
      .filter(
        m =>
          m.role === lazy.MESSAGE_ROLE.ASSISTANT &&
          m?.content?.type === "function"
      )
      .at(-1);
    const lastToolName =
      lastToolCall?.content?.body?.tool_calls?.[0]?.function?.name;
    if (lastToolName === "search_the_web") {
      const args = lastToolCall.content.body.tool_calls[0].function.arguments;
      try {
        const { query } = JSON.parse(args || "{}");
        if (query) {
          this.showSearchingIndicator(true, query);
        }
      } catch {}
    }

    this.#dispatchChromeEvent(
      "ai-window:opened-conversation",
      this.#getAIWindowEventOptions()
    );

    this.#fetchAIResponse();
  }

  handleFooterAction(data) {
    const { action, messageId, memory } = data ?? {};

    switch (action) {
      case "retry":
        this.#retryFromAssistantMessageId(messageId, undefined);
        break;

      case "retry-without-memories":
        Glean.smartWindow.retryNoMemories.record({
          location: this.mode,
          chat_id: this.conversationId,
          message_seq: this.#conversation?.messageCount ?? 0,
        });
        this.#retryFromAssistantMessageId(messageId, false);
        break;

      case "retry-after-error":
        this.#retryAfterError();
        break;

      case "remove-applied-memory":
        this.#removeAppliedMemory(messageId, memory);
        break;

      case "toggle-applied-memories":
        if (data.open) {
          Glean.smartWindow.memoryAppliedClick.record({
            location: this.mode,
            chat_id: this.conversationId,
            message_seq: this.#conversation?.messageCount ?? 0,
          });
        }
        break;

      case "manage-memories":
        this.#openMemoriesSettings();
        break;

      case "open-memories-learn-more":
        this.#openMemoriesLearnMore();
        break;

      case "thumbs-up":
      case "thumbs-down":
        this.#openFeedbackModal(action);
        break;
    }
  }

  async handleToolUIUpdate(data) {
    if (lazy.AgentUI.isAgentUpdate(data)) {
      await lazy.AgentUI.handleUpdate(
        data,
        this.#conversation,
        this.#topChromeWindow
      );
      return;
    }

    const success = await lazy.ToolUI.handleUpdate(
      data,
      this.#conversation,
      this.#topChromeWindow,
      this.mode
    );

    // Check if this was a retry prompt update
    if (success && data?.updateType === lazy.UI_UPDATE_TYPES.RETRY_PROMPT) {
      const retryPrompt = data?.updateData?.prompt;
      if (retryPrompt) {
        this.submitChatMessage({
          text: retryPrompt,
          submitType: "retry",
        });
      }
    }
  }

  #buildChatLogPayload() {
    const messages = this.#conversation?.messages ?? [];

    // Build a version of the log without page content by dropping:
    // - tool result messages that are get_page_content responses (the raw page text)
    // - assistant messages whose only tool call was get_page_content (the request to fetch it)
    const withoutPageContent = messages.filter(msg => {
      if (
        msg.role === lazy.MESSAGE_ROLE.TOOL &&
        msg.content?.name === lazy.GET_PAGE_CONTENT
      ) {
        return false;
      }
      if (msg.content?.body?.tool_calls?.length) {
        const remaining = msg.content.body.tool_calls.filter(
          tc => tc.function?.name !== lazy.GET_PAGE_CONTENT
        );
        if (!remaining.length) {
          return false;
        }
      }
      return true;
    });

    const hasPageContent = withoutPageContent.length < messages.length;
    return {
      withPageContent: { log: messages },
      withoutPageContent: hasPageContent ? { log: withoutPageContent } : null,
    };
  }

  /**
   * Cache resolved history-result page assets (thumbnail/favicon) onto the
   * conversation's pool so later message snapshots carry them. Called by the
   * actor after it resolves assets requested by a rendered grid.
   *
   * @param {string} conversationId
   * @param {Array<{url: string, image: ?string, requestedThumbnail?: boolean, hasFavicon: boolean}>} assets
   */
  applyHistoryAssets(conversationId, assets) {
    if (this.conversationId !== conversationId) {
      return;
    }

    this.#conversation?.applyHistoryAssets(assets);
  }

  #openFeedbackModal(type) {
    const browser = this.#topChromeWindow?.gBrowser?.selectedBrowser;
    if (!browser) {
      return;
    }
    // Two versions of the chat log are built so the user can toggle whether to
    // include page content in the submitted report via the preview checkbox.
    const { withPageContent, withoutPageContent } = this.#buildChatLogPayload();
    const metadata = {
      metadata: {
        model: this.modelName,
        turn_count: this.#conversation?.messageCount ?? 0,
        prompt_version: this.#conversation?.systemPromptVersion ?? "",
      },
      chatLog: withPageContent,
      chatLogWithoutPageContent: withoutPageContent,
    };
    lazy.FeedbackModal.open(browser, type, metadata);
  }

  #openMemoriesSettings() {
    this.#topChromeWindow?.openPreferences("manageMemories");
  }

  #openMemoriesLearnMore() {
    this.#topChromeWindow?.openHelpLink("smart-window-memories");
  }

  #getMessageById(id) {
    return this.#conversation.messages.find(m => m.id === id) ?? null;
  }

  #getUserMessageForAssistantId(assistantMessageId) {
    const assistantMsg = this.#getMessageById(assistantMessageId);
    if (!assistantMsg?.parentMessageId) {
      return null;
    }

    return this.#getMessageById(assistantMsg.parentMessageId) ?? null;
  }

  #retryAfterError() {
    if (this._isRetrying) {
      console.warn("ai-window: retry already in progress");
      return;
    }

    this._isRetrying = true;
    this.#fetchAIResponse("", { isRetry: true })
      .catch(error => {
        console.error("Error retrying after error:", error);
      })
      .finally(() => {
        this._isRetrying = false;
      });
  }

  async #retryFromAssistantMessageId(assistantMessageId, withMemories) {
    if (this._isRetrying) {
      console.warn("ai-window: retry already in progress");
      return;
    }

    const userMsg = this.#getUserMessageForAssistantId(assistantMessageId);
    if (!userMsg) {
      return;
    }

    this._isRetrying = true;
    const retryStart = ChromeUtils.now();
    try {
      const actor = this.#getAIChatContentActor();

      // Truncate to the retried turn so retry regenerates only that response.
      actor?.dispatchTruncateToChatContent({ messageId: assistantMessageId });

      // Retry is delete-only here; generation happens via fetchAIResponse below.
      const messagesToDelete = await this.#conversation.retryMessage(userMsg);
      await lazy.AIWindow.chatStore.deleteMessages(messagesToDelete);
      await this.#updateConversation();
      await this.#fetchAIResponse(userMsg.content.body, {
        skipUserDispatch: true,
        memoriesEnabled:
          withMemories ?? this.#memoriesToggled ?? this.#memoriesIconShown,
        contextMentions: userMsg.content.contextMentions,
        pageUrl: userMsg.pageUrl,
        isRetry: true,
      });
    } catch (e) {
      // Errors raised before #fetchAIResponse takes over (truncation,
      // retryMessage validation, chat store deletion, conversation refresh)
      // never reach the request-path catch, so route them here so they are
      // observable in model_response with is_retry=true.
      if (!e.clientReason) {
        e.clientReason = "retryOrchestrationFailure";
      }
      this.#handleError(
        e,
        this.#getModelRequestLatencyAndDuration(retryStart, null),
        { isRetry: true }
      );
    } finally {
      this._isRetrying = false;
    }
  }

  async #removeAppliedMemory(messageId, memory) {
    try {
      const memoryId = memory.id;
      const msg = this.#getMessageById(messageId);

      const remaining = msg?.memoriesApplied.filter(m => m.id !== memoryId);
      const inUse = remaining?.length ?? 0;
      const deleted = await lazy.MemoriesManager.hardDeleteMemoryById(
        memoryId,
        "assistant",
        inUse
      );
      if (!deleted) {
        console.warn("hardDeleteMemory returned false", memoryId);
        return;
      }

      if (msg) {
        msg.memoriesApplied = remaining;
      }

      const actor = this.#getAIChatContentActor();
      actor?.dispatchRemoveAppliedMemoryToChatContent({
        messageId,
        memoryId,
      });
    } catch (e) {
      console.error("Failed to delete memory", memory, e);
    }
  }

  #footerTemplate() {
    if (!this.showFooter) {
      return "";
    }
    if (this.promoMessage) {
      return html`<smartwindow-promo
        .message=${this.promoMessage}
      ></smartwindow-promo>`;
    }
    return html`<smartwindow-footer></smartwindow-footer>`;
  }

  render() {
    return html`
      <link rel="stylesheet" href="chrome://global/content/widgets.css" />
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/ai-window.css"
      />
      <!-- TODO (Bug 2008938): Make in-page Smartbar styling not dependent on chrome styles -->
      <link rel="stylesheet" href="chrome://browser/skin/smartbar.css" />
      ${this.mode === MODE.SIDEBAR
        ? html`<div class="chat-header sidebar-header">
            <moz-button
              data-l10n-id="aiwindow-new-chat"
              data-l10n-attrs="tooltiptext,aria-label"
              class="new-chat-icon-button"
              type="ghost icon"
              iconsrc="chrome://browser/content/aiwindow/assets/new-chat.svg"
              @click=${this.onCreateNewChatClick}
            ></moz-button>
            ${this.#historyMenu("sidebar")}
            <moz-button
              data-l10n-id="aiwindow-close-sidebar"
              data-l10n-attrs="tooltiptext,aria-label"
              class="close-sidebar-button"
              type="ghost icon"
              iconsrc="chrome://global/skin/icons/close.svg"
              @click=${this.#onCloseSidebarClick}
            ></moz-button>
          </div>`
        : ""}
      ${this.mode === MODE.FULLPAGE
        ? html`
            <smartwindow-heading></smartwindow-heading>
            <div class="chat-header fullpage-header">
              ${this.#historyMenu("fullpage")}
            </div>
          `
        : ""}
      <div id="browser-container"></div>
      ${this.mode === MODE.SIDEBAR
        ? html`
            ${this.showStarters
              ? html`
                  <smartwindow-prompts
                    .prompts=${this.#starters}
                    .mode=${this.mode}
                    @SmartWindowPrompt:prompt-selected=${this
                      .#handlePromptSelected}
                  ></smartwindow-prompts>
                `
              : ""}
            <div id="smartbar-slot"></div>
          `
        : html`
            <div id="smartbar-slot"></div>
            ${this.showStarters
              ? html`
                  <smartwindow-prompts
                    .prompts=${this.#starters}
                    .mode=${this.mode}
                    @SmartWindowPrompt:prompt-selected=${this
                      .#handlePromptSelected}
                  ></smartwindow-prompts>
                `
              : ""}
            ${this.startersResolved
              ? html`
                  <smartwindow-topsites
                    .sites=${this.topSites}
                    @SmartWindowTopSites:site-selected=${this
                      .#handleTopSiteSelected}
                  ></smartwindow-topsites>
                `
              : ""}
          `}
      ${this.showDisclaimer
        ? html`<div
            data-l10n-id="smartwindow-disclaimer"
            class="disclaimer"
          ></div>`
        : ""}
      ${this.#footerTemplate()}
      <kit-mention variant="fullpage"></kit-mention>
      <div
        class="sr-only"
        aria-live="polite"
        aria-atomic="true"
        data-l10n-id="aiwindow-generation-started-announcement"
        ?hidden=${!this.isGenerating}
      ></div>
    `;
  }
}

customElements.define("ai-window", AIWindow);
