/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AIFeature } from "chrome://global/content/ml/AIFeature.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  AIWindowUI:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs",
  ASRouterTargeting: "resource:///modules/asrouter/ASRouterTargeting.sys.mjs",
  BrowserSearchTelemetry:
    "moz-src:///browser/components/search/BrowserSearchTelemetry.sys.mjs",
  ContentAnalysisUtils: "resource://gre/modules/ContentAnalysisUtils.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  EveryWindow: "resource:///modules/EveryWindow.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PrefUtils: "moz-src:///toolkit/modules/PrefUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
  SidebarManager:
    "moz-src:///browser/components/sidebar/SidebarManager.sys.mjs",
});
ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () => new Localization(["browser/genai.ftl"])
);
const PREF_CHAT_ENABLED = "browser.ml.chat.enabled";
const PREF_CHAT_PAGE = "browser.ml.chat.page";
const PREF_CHAT_PROVIDER = "browser.ml.chat.provider";
const PREF_AI_CONTROL_SIDEBAR_CHATBOT = "browser.ai.control.sidebarChatbot";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatEnabled",
  PREF_CHAT_ENABLED,
  null,
  (_pref, _old, val) => onChatEnabledChange(val)
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatHideLocalhost",
  "browser.ml.chat.hideLocalhost",
  null,
  reorderChatProviders
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatMaxLength",
  "browser.ml.chat.maxLength"
);
XPCOMUtils.defineLazyPreferenceGetter(lazy, "chatMenu", "browser.ml.chat.menu");
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatNimbus",
  "browser.ml.chat.nimbus"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatOpenSidebarOnProviderChange",
  "browser.ml.chat.openSidebarOnProviderChange",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(lazy, "chatPage", PREF_CHAT_PAGE);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatPageMenuBadge",
  "browser.ml.chat.page.menuBadge"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatPromptPrefix",
  "browser.ml.chat.prompt.prefix"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatProvider",
  PREF_CHAT_PROVIDER,
  null,
  (_pref, _old, val) => onChatProviderChange(val)
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatProviders",
  "browser.ml.chat.providers",
  "claude,chatgpt,gemini,huggingchat,lechat",
  reorderChatProviders
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatShortcuts",
  "browser.ml.chat.shortcuts",
  null,
  (_pref, _old, val) => onChatShortcutsChange(val)
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatShortcutsCustom",
  "browser.ml.chat.shortcuts.custom"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatShortcutsSmartWindow",
  "browser.ml.chat.shortcuts.smartwindow"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatShortcutsIgnoreFields",
  "browser.ml.chat.shortcuts.ignoreFields",
  "input,moz-multiline-editor",
  updateIgnoredInputs
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatSidebar",
  "browser.ml.chat.sidebar"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "highlightToSearchFeatureGate",
  "browser.highlightToSearch.featureGate",
  null,
  () => {
    recordSelectionMenuEnabled();
    updateIgnoredInputs();
  }
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "highlightToSearchEnabled",
  "browser.highlightToSearch.enabled",
  null,
  () => recordSelectionMenuEnabled()
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "highlightToSearchCopyEnabled",
  "browser.highlightToSearch.copy.enabled",
  null,
  (_pref, _old, val) => Glean.selectionMenu.copyEnabled.set(val)
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "highlightToSearchSearchEnabled",
  "browser.highlightToSearch.search.enabled",
  null,
  (_pref, _old, val) => Glean.selectionMenu.searchEnabled.set(val)
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "highlightToSearchIgnoredSites",
  "browser.highlightToSearch.ignoredSites",
  "",
  updateIgnoredSites
);
const PREF_SELECTION_CALLOUT_SEEN = "browser.highlightToSearch.callout.seen";
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "highlightToSearchCalloutSeen",
  PREF_SELECTION_CALLOUT_SEEN,
  false
);
XPCOMUtils.defineLazyPreferenceGetter(lazy, "sidebarRevamp", "sidebar.revamp");
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "sidebarTools",
  "sidebar.main.tools"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "shortcutMouseoverCount",
  "browser.ml.chat.shortcut.onboardingMouseoverCount",
  0
);

const CALLOUT_CARET = {
  half: 12.8301,
  shoulder: 3.1211,
  rise: 12.1074,
  handle: 1.5199,
  cap: 14.1043,
};
const CALLOUT_CARET_ROOM = 16;

function calloutCardRadius(callout) {
  const body = callout.querySelector(".panel-subview-body");
  return parseFloat(
    body.documentGlobal.getComputedStyle(body).borderTopLeftRadius
  );
}

/**
 * Outline of the callout card and its caret as a single closed path.
 *
 * @param {number} width card width
 * @param {number} height card height
 * @param {number} radius card corner radius
 * @param {number} caretX horizontal position the caret points at
 * @param {boolean} above whether the caret sits on the bottom edge
 * @returns {string} value for the path's d attribute
 */
function calloutOutlinePath(width, height, radius, caretX, above) {
  const n = v => Math.round(v * 100) / 100;
  const c = CALLOUT_CARET;
  const r = radius;
  const x = Math.max(r + c.half, Math.min(width - r - c.half, caretX));
  const arc = (dx, dy) => `A${r} ${r} 0 0 1 ${n(dx)} ${n(dy)}`;
  if (!above) {
    return (
      `M${n(x - c.shoulder)} ${n(-c.rise)}` +
      `C${n(x - c.handle)} ${n(-c.cap)} ${n(x + c.handle)} ${n(-c.cap)} ` +
      `${n(x + c.shoulder)} ${n(-c.rise)}` +
      `L${n(x + c.half)} 0H${n(width - r)}${arc(width, r)}` +
      `V${n(height - r)}${arc(width - r, height)}H${r}${arc(0, height - r)}` +
      `V${r}${arc(r, 0)}H${n(x - c.half)}Z`
    );
  }
  return (
    `M${r} 0H${n(width - r)}${arc(width, r)}V${n(height - r)}` +
    `${arc(width - r, height)}H${n(x + c.half)}` +
    `L${n(x + c.shoulder)} ${n(height + c.rise)}` +
    `C${n(x + c.handle)} ${n(height + c.cap)} ${n(x - c.handle)} ` +
    `${n(height + c.cap)} ${n(x - c.shoulder)} ${n(height + c.rise)}` +
    `L${n(x - c.half)} ${n(height)}H${r}${arc(0, height - r)}V${r}${arc(r, 0)}Z`
  );
}

function markSelectionCalloutSeen() {
  if (!lazy.highlightToSearchCalloutSeen) {
    Services.prefs.setBoolPref(PREF_SELECTION_CALLOUT_SEEN, true);
  }
}

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "parserUtils",
  "@mozilla.org/parserutils;1",
  Ci.nsIParserUtils
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "ClipboardHelper",
  "@mozilla.org/widget/clipboardhelper;1",
  Ci.nsIClipboardHelper
);

function recordSelectionMenuEnabled() {
  Glean.selectionMenu.enabled.set(
    lazy.highlightToSearchFeatureGate && lazy.highlightToSearchEnabled
  );
}

export const GenAI = {
  // Cache of potentially localized prompt
  chatPromptPrefix: "",

  // Any chat provider can be used and those that match the URLs in this object
  // will allow for additional UI shown such as populating dropdown with a name,
  // showing links, and other special behaviors needed for individual providers.
  chatProviders: new Map([
    [
      "https://claude.ai/new",
      {
        iconUrl: "chrome://browser/content/genai/assets/brands/claude.svg",
        id: "claude",
        link1:
          "https://www.anthropic.com/legal/archive/6370fb23-12ed-41d9-a4a2-28866dee3105",
        link2:
          "https://www.anthropic.com/legal/archive/7197103a-5e27-4ee4-93b1-f2d4c39ba1e7",
        link3:
          "https://www.anthropic.com/legal/archive/628feec9-7df9-4d38-bc69-fbf104df47b0",
        linksId: "genai-settings-chat-claude-links",
        maxLength: 14150,
        name: "Anthropic Claude",
        supportAutoSubmit: true,
        tooltipId: "genai-onboarding-claude-tooltip",
      },
    ],
    [
      "https://chatgpt.com",
      {
        iconUrl: "chrome://browser/content/genai/assets/brands/chatgpt.svg",
        id: "chatgpt",
        link1: "https://openai.com/terms",
        link2: "https://openai.com/privacy",
        linksId: "genai-settings-chat-chatgpt-links",
        maxLength: 9350,
        name: "ChatGPT",
        supportAutoSubmit: true,
        tooltipId: "genai-onboarding-chatgpt-tooltip",
      },
    ],
    [
      "https://copilot.microsoft.com/?form=MOZCMC",
      {
        iconUrl: "chrome://browser/content/genai/assets/brands/copilot.svg",
        id: "copilot",
        link1: "https://www.bing.com/new/termsofuse",
        link2: "https://go.microsoft.com/fwlink/?LinkId=521839",
        linksId: "genai-settings-chat-copilot-links",
        maxLength: 3260,
        name: "Copilot",
        tooltipId: "genai-onboarding-copilot-tooltip",
      },
    ],
    [
      "https://gemini.google.com",
      {
        header: "X-Firefox-Gemini",
        iconUrl: "chrome://browser/content/genai/assets/brands/gemini.svg",
        id: "gemini",
        link1: "https://policies.google.com/terms",
        link2: "https://policies.google.com/terms/generative-ai/use-policy",
        link3: "https://support.google.com/gemini?p=privacy_notice",
        linksId: "genai-settings-chat-gemini-links",
        // Max header length is around 55000, but spaces are encoded with %20
        // for header instead of + for query parameter
        maxLength: 45000,
        name: "Google Gemini",
        tooltipId: "genai-onboarding-gemini-tooltip",
      },
    ],
    [
      "https://huggingface.co/chat",
      {
        iconUrl: "chrome://browser/content/genai/assets/brands/huggingchat.svg",
        id: "huggingchat",
        link1: "https://huggingface.co/chat/privacy",
        link2: "https://huggingface.co/privacy",
        linksId: "genai-settings-chat-huggingchat-links",
        maxLength: 8192,
        name: "HuggingChat",
        tooltipId: "genai-onboarding-huggingchat-tooltip",
      },
    ],
    [
      "https://chat.mistral.ai/chat",
      {
        iconUrl: "chrome://browser/content/genai/assets/brands/lechat.svg",
        id: "lechat",
        link1: "https://chat.mistral.ai/legal/terms",
        link2: "https://chat.mistral.ai/legal/privacy-policy",
        linksId: "genai-settings-chat-lechat-links-2",
        maxLength: 13350,
        name: "Mistral Vibe",
        tooltipId: "genai-onboarding-lechat-tooltip-2",
      },
    ],
    [
      "http://localhost:8080",
      {
        id: "localhost",
        link1: "https://llamafile.ai",
        linksId: "genai-settings-chat-localhost-links",
        maxLength: 8192,
        name: "localhost",
      },
    ],
    [
      "https://search.yahoo.com/ays/ff?fr=firefox_widget",
      {
        iconUrl: "chrome://browser/content/genai/assets/brands/scout.svg",
        id: "scout",
        link1: "https://guce.yahoo.com/terms",
        link2: "https://guce.yahoo.com/privacy-policy",
        linksId: "genai-settings-chat-scout-links",
        maxLength: 11500,
        name: "Yahoo Scout",
        tooltipId: "genai-onboarding-scout-tooltip",
      },
    ],
  ]),

  /**
   * Retrieves the current chat provider information based on the
   * preference setting
   *
   * @returns {object} An object containing the current chat provider's
   *                   information, such as name, iconUrl, etc. If no
   *                   provider is set, returns an empty object.
   */
  get currentChatProviderInfo() {
    return {
      iconUrl: "chrome://global/skin/icons/highlights.svg",
      ...this.chatProviders.get(lazy.chatProvider),
    };
  },

  /**
   * Determine if the chatbot can be offered, whether or not the user has
   * chosen a provider yet. Clicking an entrypoint with no provider starts
   * onboarding, so surfaces that want to onboard use this instead of
   * canShowChatEntrypoint.
   *
   * @returns {bool} can offer
   */
  get canOfferChatbot() {
    return (
      lazy.chatEnabled &&
      // Chatbot needs to be a tool if new sidebar
      (!lazy.sidebarRevamp || lazy.sidebarTools.includes("aichat"))
    );
  },

  /**
   * @returns {bool} can show
   */
  get canShowChatEntrypoint() {
    return this.canOfferChatbot && lazy.chatProvider != "";
  },

  /**
   * @returns {bool} can show
   */
  get canShowAIAction() {
    return this.canOfferChatbot && lazy.chatShortcuts;
  },

  /**
   * @returns {bool} can show
   */
  get canShowCopyAction() {
    return (
      lazy.highlightToSearchFeatureGate &&
      lazy.highlightToSearchEnabled &&
      lazy.highlightToSearchCopyEnabled
    );
  },

  /**
   * @returns {bool} can show
   */
  get canShowMoreActions() {
    return lazy.highlightToSearchFeatureGate && lazy.highlightToSearchEnabled;
  },

  /**
   * @param {Window} window browser window of the context menu
   * @returns {boolean} whether the selection menu was turned off and can be
   *   turned back on from the context menu
   */
  canTurnOnSelectionMenu(window) {
    return (
      lazy.highlightToSearchFeatureGate &&
      !lazy.highlightToSearchEnabled &&
      !lazy.AIWindow.isAIWindowActive(window)
    );
  },

  /**
   * Turn the selection menu back on and show it over the selection that was
   * right-clicked. Turning every action off is what turned it off, which isn't
   * a preference worth restoring, so each action comes back too.
   *
   * @param {WindowGlobalParent} windowGlobal frame that was right-clicked
   * @param {{screenXDevPx: number, screenYDevPx: number}} point where the
   *   context menu was opened
   */
  turnOnSelectionMenu(windowGlobal, point) {
    Services.prefs.setBoolPref("browser.highlightToSearch.enabled", true);
    this.markSelectionCalloutSeen();
    if (
      !this.canShowAIAction &&
      !lazy.highlightToSearchSearchEnabled &&
      !lazy.highlightToSearchCopyEnabled
    ) {
      Services.prefs.setBoolPref("browser.ml.chat.shortcuts", true);
      Services.prefs.setBoolPref(
        "browser.highlightToSearch.search.enabled",
        true
      );
      Services.prefs.setBoolPref(
        "browser.highlightToSearch.copy.enabled",
        true
      );
    }
    Glean.selectionMenu.optionToggled.record({
      action: "master",
      enabled: true,
      source: "context_menu",
    });
    windowGlobal.getActor("GenAI").sendAsyncMessage("ShowSelectionMenu", point);
  },

  /**
   * @returns {bool} can show
   */
  get canShowSelectionMenu() {
    if (lazy.highlightToSearchFeatureGate) {
      return (
        lazy.highlightToSearchEnabled &&
        (this.canShowAIAction ||
          lazy.highlightToSearchSearchEnabled ||
          lazy.highlightToSearchCopyEnabled)
      );
    }
    return lazy.chatShortcuts && this.canShowChatEntrypoint;
  },

  markSelectionCalloutSeen,

  /**
   * Handle startup tasks like telemetry, adding listeners.
   */
  init() {
    // Allow other callers to init even though we now automatically init
    if (this._initialized) {
      return;
    }
    this._initialized = true;

    // Access getters for side effects of observing pref changes
    lazy.chatEnabled;
    lazy.chatHideLocalhost;
    lazy.chatProvider;
    lazy.chatProviders;
    lazy.chatShortcuts;
    lazy.chatShortcutsIgnoreFields;
    lazy.highlightToSearchIgnoredSites;

    // Apply initial ordering of providers
    reorderChatProviders();
    updateIgnoredInputs();
    updateIgnoredSites();

    // Handle nimbus feature pref setting
    const feature = lazy.NimbusFeatures.chatbot;
    feature.onUpdate(() => {
      const enrollment = feature.getEnrollmentMetadata();
      if (!enrollment) {
        return;
      }

      // Enforce minimum version by skipping pref changes until Firefox restarts
      // with the appropriate version
      if (
        Services.vc.compare(
          // Support betas, e.g., 132.0b1, instead of MOZ_APP_VERSION
          AppConstants.MOZ_APP_VERSION_DISPLAY,
          // Check configured version or compare with unset handled as 0
          feature.getVariable("minVersion")
        ) < 0
      ) {
        return;
      }

      // Set prefs on any branch if we have a new enrollment slug, otherwise
      // only set default branch as those only last for the session
      const slug = enrollment.slug + ":" + enrollment.branch;
      const newEnroll = slug != lazy.chatNimbus;
      const setPref = ([pref, { branch = "user", value = null }]) => {
        if (newEnroll || branch == "default") {
          lazy.PrefUtils.setPref("browser.ml.chat." + pref, value, { branch });
        }
      };
      setPref(["nimbus", { value: slug }]);
      Object.entries(feature.getVariable("prefs") ?? {}).forEach(setPref);

      // Show sidebar badge on new enrollment
      if (feature.getVariable("badgeSidebar") && newEnroll) {
        Services.prefs.setBoolPref("sidebar.notification.badge.aichat", true);
      }
    });

    // Record glean metrics after applying nimbus prefs
    Glean.genaiChatbot.badges.set(
      Object.entries({
        footer: "browser.ml.chat.page.footerBadge",
        menu: "browser.ml.chat.page.menuBadge",
        sidebar: "sidebar.notification.badge.aichat",
      })
        .reduce((acc, [key, pref]) => {
          if (Services.prefs.getBoolPref(pref)) {
            acc.push(key);
          }
          return acc;
        }, [])
        .join(",")
    );
    Glean.genaiChatbot.enabled.set(lazy.chatEnabled);
    Glean.genaiChatbot.menu.set(lazy.chatMenu);
    Glean.genaiChatbot.page.set(lazy.chatPage);
    Glean.genaiChatbot.provider.set(this.getProviderId());
    Glean.genaiChatbot.shortcuts.set(lazy.chatShortcuts);
    Glean.genaiChatbot.shortcutsCustom.set(lazy.chatShortcutsCustom);
    Glean.genaiChatbot.sidebar.set(lazy.chatSidebar);
    Glean.selectionMenu.copyEnabled.set(lazy.highlightToSearchCopyEnabled);
    Glean.selectionMenu.searchEnabled.set(lazy.highlightToSearchSearchEnabled);
    recordSelectionMenuEnabled();
  },

  /**
   * Convert provider to id.
   *
   * @param {string} provider url defaulting to current pref
   * @returns {string} id or custom or none
   */
  getProviderId(provider = lazy.chatProvider) {
    const { id } = this.chatProviders.get(provider) ?? {};
    return id ?? (provider ? "custom" : "none");
  },

  /**
   * Add chat items to menu or popup.
   *
   * @param {MozBrowser} browser providing context
   * @param {object} extraContext e.g., selection text
   * @param {Function} itemAdder creates and returns the item
   * @param {string} entry name
   * @param {Function} cleanup optional on item activation
   * @returns {object} context used for selecting prompts
   */
  async addAskChatItems(browser, extraContext, itemAdder, entry, cleanup) {
    // Prepare context used for both targeting and handling prompts
    const window = browser.documentGlobal;
    const tab = window?.gBrowser?.getTabForBrowser(browser);
    const uri = browser.currentURI;
    const context = {
      ...extraContext,
      entry,
      provider: lazy.chatProvider,
      tabTitle: (tab?.labelIsContentTitle && tab.label) || "",
      url: uri?.asciiHost + uri?.filePath,
      window,
    };

    // Add items that pass along context for handling
    (await this.getContextualPrompts(context)).forEach(promptObj => {
      const item = itemAdder(promptObj, context);
      item?.addEventListener("command", () => {
        this.handleAskChat(promptObj, context);
        cleanup?.(item);
      });
    });

    return context;
  },

  /**
   * Setup helpers and callbacks for the selection shortcut panel.
   *
   * The panel owns the state shared by its actions: whether it has been
   * initialized, how to hide it, and the selection it was opened for.
   *
   * @param {MozPanel} panel selection shortcut panel for the browser window
   * @param {string} iconSrc URL for the ai action button icon
   */
  initializeSelectionShortcutPanel(
    panel,
    iconSrc = "chrome://global/skin/icons/highlights.svg"
  ) {
    const document = panel.ownerDocument;
    const aiActionButton = panel.querySelector("#ai-action-button");
    aiActionButton.iconSrc = iconSrc;
    if (panel.initialized) {
      return;
    }
    panel.initialized = true;

    // Set the icons here rather than in markup, because the panel lives in the
    // popupset and an iconsrc there loads the image during startup while the
    // action is still hidden.
    for (const [id, src] of [
      ["#search-action-button", "chrome://global/skin/icons/search-glass.svg"],
      ["#copy-action-button", "chrome://global/skin/icons/edit-copy.svg"],
      ["#more-actions-button", "chrome://global/skin/icons/more.svg"],
    ]) {
      panel.querySelector(id).iconSrc = src;
    }

    document.l10n.setAttributes(aiActionButton, "genai-shortcut-button-3");

    const searchActionButton = panel.querySelector("#search-action-button");

    // Matches the context menu's "Search <engine> for <selection>" label.
    const truncateSelection = selection => {
      const collapsed = selection.replace(/\s+/g, " ").trim();
      if (collapsed.length <= 15) {
        return collapsed;
      }
      let truncLength = 15;
      const truncChar = collapsed[15].charCodeAt(0);
      // Handle surrogate pairs.
      if (truncChar >= 0xdc00 && truncChar <= 0xdfff) {
        truncLength++;
      }
      return collapsed.substring(0, truncLength) + Services.locale.ellipsis;
    };

    // Returns the engine the action searches with, or null if the action ends
    // up hidden, so callers can tell whether the menu has anything to offer.
    panel.setSearchButtonLabel = (selection, browser) => {
      // The default engine getters throw rather than return null until the
      // search service has initialized, and throwing here would stop the panel
      // from opening at all, so hide the action like nsContextMenu does.
      let engine = null;
      if (lazy.SearchService.hasSuccessfullyInitialized) {
        engine = lazy.PrivateBrowsingUtils.isBrowserPrivate(browser)
          ? lazy.SearchService.defaultPrivateEngine
          : lazy.SearchService.defaultEngine;
      }
      const canShow =
        !!engine &&
        lazy.highlightToSearchFeatureGate &&
        lazy.highlightToSearchEnabled &&
        lazy.highlightToSearchSearchEnabled;
      searchActionButton.hidden = !canShow;
      if (!canShow) {
        return null;
      }
      document.l10n.setAttributes(
        searchActionButton,
        "genai-shortcut-search-button",
        {
          engine: engine.name,
          selection: truncateSelection(selection),
        }
      );
      return engine;
    };

    const copyActionButton = panel.querySelector("#copy-action-button");
    const copyConfirmation = panel.querySelector("#copy-action-confirmation");
    const copyConfirmationText = copyConfirmation.querySelector(
      ".copy-action-confirmation-text"
    );
    let copyConfirmationTimer = null;
    let copyGeneration = 0;

    const moreActionsButton = panel.querySelector("#more-actions-button");
    const moreActionsSeparator = panel.querySelector("#more-actions-separator");

    // Returns whether any action ends up visible, so callers can tell whether
    // the menu has anything to offer, and the engine the Search action uses
    // (null if it is hidden).
    panel.updateActions = (selection, browser, isSmartWindow = false) => {
      if (isSmartWindow) {
        aiActionButton.hidden = false;
        searchActionButton.hidden = true;
        copyActionButton.hidden = true;
        moreActionsButton.hidden = true;
        moreActionsSeparator.hidden = true;
        return { hasActions: true, searchEngine: null };
      }
      const aiVisible = this.canShowAIAction;
      aiActionButton.hidden = !aiVisible;
      const searchEngine = panel.setSearchButtonLabel(selection, browser);
      const copyVisible = this.canShowCopyAction;
      copyActionButton.hidden = !copyVisible;
      moreActionsButton.hidden = !this.canShowMoreActions;
      const hasActions = aiVisible || !!searchEngine || copyVisible;
      moreActionsSeparator.hidden = moreActionsButton.hidden || !hasActions;
      return { hasActions, searchEngine };
    };

    panel.resetCopyConfirmation = () => {
      copyGeneration++;
      document.defaultView.clearTimeout(copyConfirmationTimer);
      copyConfirmation.removeAttribute("copied");
      panel.removeAttribute("copied");
      panel.removeAttribute("copy-fading");
      copyConfirmationText.removeAttribute("data-l10n-id");
      copyConfirmationText.textContent = "";
    };
    panel.addEventListener("popuphidden", event => {
      if (event.target == panel) {
        panel.resetCopyConfirmation();
      }
    });

    copyActionButton.addEventListener("click", async () => {
      lazy.ClipboardHelper.copyString(
        panel.selectionData.selection,
        panel.selectionData.browser.browsingContext.currentWindowContext
      );

      Glean.selectionMenu.actionClick.record({
        action: "copy",
        selection: panel.selectionData.selection.length,
        smart_window: lazy.AIWindow.isAIWindowActive(document.defaultView),
      });
      panel.markInteracted();

      const generation = ++copyGeneration;
      document.l10n.setAttributes(
        copyConfirmationText,
        "genai-shortcut-copy-confirmation"
      );
      await document.l10n.translateElements([copyConfirmationText]);
      if (generation != copyGeneration) {
        if (!copyConfirmationText.hasAttribute("data-l10n-id")) {
          copyConfirmationText.textContent = "";
        }
        return;
      }
      if (panel.state != "open") {
        panel.resetCopyConfirmation();
        return;
      }
      copyConfirmation.setAttribute("copied", "true");
      panel.setAttribute("copied", "true");
      panel.removeAttribute("copy-fading");

      document.defaultView.clearTimeout(copyConfirmationTimer);
      copyConfirmationTimer = document.defaultView.setTimeout(() => {
        panel.setAttribute("copy-fading", "");
        copyConfirmationTimer = document.defaultView.setTimeout(
          () => panel.hide(),
          240
        );
      }, 850);
    });

    searchActionButton.addEventListener("click", () => {
      const { selection, principal, policyContainer } = panel.selectionData;
      lazy.SearchUIUtils.loadSearch({
        window: document.defaultView,
        searchText: selection,
        where: "tab",
        triggeringPrincipal: principal,
        policyContainer:
          lazy.E10SUtils.deserializePolicyContainer(policyContainer),
        sapSource: "text_selection",
      });
      Glean.selectionMenu.actionClick.record({
        action: "search",
        selection: selection.length,
        smart_window: lazy.AIWindow.isAIWindowActive(document.defaultView),
      });
      panel.markInteracted();
      panel.hide();
    });
    const buttonActiveState = "icon";
    const buttonDefaultState = "icon ghost";
    const chatShortcutsOptionsPanel = document.getElementById(
      "chat-shortcuts-options-panel"
    );
    const moreActionsPanel = document.getElementById(
      "selection-more-actions-panel"
    );
    let calloutPanel = null;
    const isCalloutOpen = () =>
      !!calloutPanel && calloutPanel.state != "closed";
    const calloutTarget = () =>
      [searchActionButton, aiActionButton, copyActionButton].find(
        button => !button.hidden
      ) ?? moreActionsButton;

    const createCallout = () => {
      const popupSet = document.getElementById("mainPopupSet");
      const template = document.getElementById("selectionCalloutTemplate");
      const callout = popupSet.appendChild(
        document.importNode(template.content, true).firstElementChild
      );
      const calloutBody = callout.querySelector(".panel-subview-body");
      const calloutShape = callout.querySelector(".selection-callout-shape");
      const calloutEdge = callout.querySelector(".selection-callout-edge");
      const win = document.defaultView;

      const drawCalloutOutline = () => {
        const card = calloutBody.getBoundingClientRect();
        const target = calloutTarget().getBoundingClientRect();
        const above = card.top < target.top;
        const caretX = target.left + target.width / 2 - card.left;
        const radius = calloutCardRadius(callout);
        calloutShape.setAttribute(
          "viewBox",
          `0 ${-CALLOUT_CARET_ROOM} ${card.width} ${
            card.height + CALLOUT_CARET_ROOM * 2
          }`
        );
        calloutShape.setAttribute("width", card.width);
        calloutShape.setAttribute(
          "height",
          card.height + CALLOUT_CARET_ROOM * 2
        );
        calloutEdge.setAttribute(
          "d",
          calloutOutlinePath(card.width, card.height, radius, caretX, above)
        );
      };
      const resizeObserver = new win.ResizeObserver(drawCalloutOutline);

      callout.addEventListener("popupshown", event => {
        if (event.target != callout) {
          return;
        }
        if (panel.state != "open") {
          callout.hidePopup();
          return;
        }
        markSelectionCalloutSeen();
        drawCalloutOutline();
        resizeObserver.observe(calloutBody);
      });
      callout.addEventListener("popuphidden", event => {
        if (event.target != callout) {
          return;
        }
        resizeObserver.disconnect();
        moreActionsButton.removeAttribute("highlighted");
      });
      // The link is replaced each time the sentence is translated, so listen on
      // the sentence.
      const message = callout.querySelector(".selection-callout-message");
      const isMoreLink = event =>
        event.target.closest(".selection-callout-more-link");
      message.addEventListener("mouseover", event => {
        if (isMoreLink(event)) {
          moreActionsButton.toggleAttribute("highlighted", true);
        }
      });
      message.addEventListener("mouseout", event => {
        if (isMoreLink(event)) {
          moreActionsButton.removeAttribute("highlighted");
        }
      });
      message.addEventListener("click", event => {
        if (isMoreLink(event)) {
          moreActionsButton.click();
        }
      });
      message.addEventListener("keydown", event => {
        if (isMoreLink(event) && (event.key == "Enter" || event.key == " ")) {
          event.preventDefault();
          moreActionsButton.click();
        }
      });
      return callout;
    };

    panel.hideCallout = () => {
      calloutPanel?.hidePopup();
    };
    const shouldShowCallout = () =>
      panel.state == "open" &&
      !panel.hasAttribute("ime-hiding") &&
      moreActionsPanel.state == "closed" &&
      chatShortcutsOptionsPanel.state == "closed" &&
      !lazy.highlightToSearchCalloutSeen &&
      !moreActionsButton.hidden &&
      (!searchActionButton.hidden || !copyActionButton.hidden) &&
      !isCalloutOpen();
    panel.showCallout = async () => {
      if (!shouldShowCallout()) {
        return;
      }
      calloutPanel ??= createCallout();
      await document.l10n.translateElements([
        calloutPanel.querySelector(".selection-callout-title"),
        calloutPanel.querySelector(".selection-callout-message"),
      ]);
      if (!shouldShowCallout()) {
        return;
      }
      const strip = moreActionsButton.parentNode.getBoundingClientRect();
      const target = calloutTarget().getBoundingClientRect();
      const targetCenter = target.left + target.width / 2;
      const fromStart = Services.locale.isAppLocaleRTL
        ? strip.right - targetCenter
        : targetCenter - strip.left;
      calloutPanel.openPopup(
        moreActionsButton.parentNode,
        "after_start",
        fromStart - calloutCardRadius(calloutPanel) - CALLOUT_CARET.half,
        0
      );
    };
    panel.hideDropdowns = () => {
      chatShortcutsOptionsPanel.hidePopup();
      moreActionsPanel.hidePopup();
    };
    panel.hide = () => {
      panel.hideDropdowns();
      panel.hideCallout();
      panel.hidePopup();
    };

    // Tell the page whether a dropdown is open, so that Escape in the page
    // closes just the dropdown.
    const sendDropdownState = () => {
      if (!lazy.highlightToSearchFeatureGate) {
        return;
      }
      const open =
        chatShortcutsOptionsPanel.state != "closed" ||
        moreActionsPanel.state != "closed";
      const windowGlobal = panel.selectionData?.windowGlobal;
      if (windowGlobal?.isCurrentGlobal) {
        windowGlobal.getActor("GenAI").sendAsyncMessage("DropdownState", open);
      }
    };
    for (const dropdown of [chatShortcutsOptionsPanel, moreActionsPanel]) {
      for (const type of ["popupshown", "popuphidden"]) {
        dropdown.addEventListener(type, event => {
          if (event.target == dropdown) {
            sendDropdownState();
          }
        });
      }
    }
    document.addEventListener("keydown", event => {
      if (
        event.key != "Escape" ||
        event.defaultPrevented ||
        !lazy.highlightToSearchFeatureGate ||
        panel.state != "open"
      ) {
        return;
      }
      if (
        chatShortcutsOptionsPanel.state != "closed" ||
        moreActionsPanel.state != "closed"
      ) {
        panel.hideDropdowns();
      } else {
        panel.hide();
      }
    });
    panel.startDisplay = smartWindow => {
      panel.display = {
        start: ChromeUtils.now(),
        smartWindow,
        interacted: false,
      };
    };
    panel.markInteracted = () => {
      if (panel.display) {
        panel.display.interacted = true;
      }
    };
    // Idempotent - only the first call after startDisplay records.
    panel.recordDismissed = () => {
      if (!panel.display) {
        return;
      }
      const { start, smartWindow, interacted } = panel.display;
      panel.display = null;
      Glean.selectionMenu.dismissed.record({
        interacted,
        duration: Math.round(ChromeUtils.now() - start),
        smart_window: smartWindow,
      });
    };
    panel.addEventListener("popupshown", event => {
      if (event.target == panel) {
        panel.showCallout();
      }
    });
    panel.addEventListener("popuphidden", event => {
      if (event.target == panel) {
        panel.recordDismissed();
        panel.hideCallout();
      }
    });
    aiActionButton.setAttribute("type", buttonDefaultState);
    aiActionButton.ariaHasPopup = "menu";
    aiActionButton.ariaExpanded = "false";
    chatShortcutsOptionsPanel.addEventListener("popupshown", () => {
      aiActionButton.ariaExpanded = "true";
    });
    chatShortcutsOptionsPanel.addEventListener("popuphidden", () => {
      aiActionButton.setAttribute("type", buttonDefaultState);
      aiActionButton.ariaExpanded = "false";
    });
    chatShortcutsOptionsPanel.firstChild.id = "ask-chat-shortcuts";

    const aiToggle = document.getElementById("more-actions-ai-toggle");
    const searchToggle = document.getElementById("more-actions-search-toggle");
    const copyToggle = document.getElementById("more-actions-copy-toggle");
    const moreActionToggles = [
      { toggle: aiToggle, action: "ai", pref: "browser.ml.chat.shortcuts" },
      {
        toggle: searchToggle,
        action: "search",
        pref: "browser.highlightToSearch.search.enabled",
      },
      {
        toggle: copyToggle,
        action: "copy",
        pref: "browser.highlightToSearch.copy.enabled",
      },
    ];

    aiToggle.iconSrc = "chrome://global/skin/icons/highlights.svg";
    searchToggle.iconSrc = "chrome://global/skin/icons/search-glass.svg";
    copyToggle.iconSrc = "chrome://global/skin/icons/edit-copy.svg";

    const allOffNotice = document.getElementById("more-actions-all-off-notice");
    let turnOffWhenClosed = false;

    const renderMoreActions = () => {
      // The chatbot row is absent, not unchecked, while the chatbot is blocked.
      aiToggle.hidden = !this.canOfferChatbot;
      const providerName = this.chatProviders.get(lazy.chatProvider)?.name;
      document.l10n.setAttributes(
        aiToggle,
        providerName
          ? "genai-more-actions-ai-provider"
          : "genai-more-actions-ai-generic",
        { provider: providerName }
      );
      aiToggle.pressed = lazy.chatShortcuts;
      searchToggle.pressed = lazy.highlightToSearchSearchEnabled;
      copyToggle.pressed = lazy.highlightToSearchCopyEnabled;
      allOffNotice.hidden = true;
      turnOffWhenClosed = false;
    };

    const hasEnabledAction = () =>
      this.canShowAIAction ||
      lazy.highlightToSearchSearchEnabled ||
      lazy.highlightToSearchCopyEnabled;

    for (const { toggle, action, pref } of moreActionToggles) {
      toggle.addEventListener("toggle", () => {
        panel.editingActions = true;
        try {
          Services.prefs.setBoolPref(pref, toggle.pressed);
        } finally {
          panel.editingActions = false;
        }
        Glean.selectionMenu.optionToggled.record({
          action,
          enabled: toggle.pressed,
          source: "menu",
        });
        turnOffWhenClosed = !hasEnabledAction();
        allOffNotice.hidden = !turnOffWhenClosed;
        // Re-render the bar in place rather than waiting for the next
        // selection.
        panel.updateActions(
          panel.selectionData.selection,
          panel.selectionData.browser
        );
      });
    }

    const manageSettingsButton = document.getElementById(
      "more-actions-manage-settings"
    );
    manageSettingsButton.iconSrc = "chrome://global/skin/icons/settings.svg";
    manageSettingsButton.addEventListener("click", () => {
      Glean.selectionMenu.menuClick.record({ item: "settings" });
      document.defaultView.openPreferences("general-highlight-to-search");
      panel.hide();
    });

    moreActionsButton.setAttribute("type", buttonDefaultState);
    moreActionsButton.ariaExpanded = "false";
    moreActionsPanel.addEventListener("popupshown", event => {
      if (event.target == moreActionsPanel) {
        moreActionsButton.ariaExpanded = "true";
      }
    });
    moreActionsPanel.addEventListener("popuphidden", event => {
      if (event.target == moreActionsPanel) {
        moreActionsButton.setAttribute("type", buttonDefaultState);
        moreActionsButton.ariaExpanded = "false";
        if (turnOffWhenClosed) {
          turnOffWhenClosed = false;
          Services.prefs.setBoolPref(
            "browser.highlightToSearch.enabled",
            false
          );
          Glean.selectionMenu.optionToggled.record({
            action: "master",
            enabled: false,
            source: "uncheck_all",
          });
          panel.hide();
        }
      }
    });
    moreActionsButton.addEventListener("click", async () => {
      panel.hideCallout();
      if (moreActionsPanel.state != "closed") {
        moreActionsPanel.hidePopup();
        return;
      }
      chatShortcutsOptionsPanel.hidePopup();
      renderMoreActions();
      // Open with the AI row already named, so a screen reader doesn't land on
      // an unnamed switch while Fluent catches up.
      await document.l10n.translateElements([aiToggle]);
      if (panel.state != "open" || moreActionsPanel.state != "closed") {
        return;
      }
      moreActionsButton.setAttribute("type", buttonActiveState);
      moreActionsPanel.openPopup(panel, "after_start", 0, 10);
      const state = toggle => {
        if (toggle.hidden) {
          return "absent";
        }
        return toggle.pressed ? "on" : "off";
      };
      Glean.selectionMenu.menuShown.record({
        ai: state(aiToggle),
        search: state(searchToggle),
        copy: state(copyToggle),
      });
    });

    // Helper to show rounded warning numbers
    const roundDownToNearestHundred = number => {
      return Math.floor(number / 100) * 100;
    };

    /**
     * Create a warning message bar.
     *
     * @param {{
     *   name: string,
     *   maxLength: number,
     * }} chatProvider attributes for the warning
     * @returns { mozMessageBarEl } MozMessageBar warning message bar
     */
    const createMessageBarWarning = chatProvider => {
      const mozMessageBarEl = this.createWarningEl(
        document,
        "ask-chat-shortcut-warning",
        null
      );

      // If provider is not defined, use generic warning message
      const translationId = chatProvider?.name
        ? "genai-shortcuts-selected-warning"
        : "genai-shortcuts-selected-warning-generic";

      document.l10n.setAttributes(mozMessageBarEl, translationId, {
        provider: chatProvider?.name,
        maxLength: roundDownToNearestHundred(
          this.estimateSelectionLimit(chatProvider?.maxLength)
        ),
        selectionLength: roundDownToNearestHundred(
          panel.selectionData.selection.length
        ),
      });

      return mozMessageBarEl;
    };

    // build the ask popup
    const buildPopup = async () => {
      moreActionsPanel.hidePopup();
      panel.hideCallout();
      aiActionButton.setAttribute("type", buttonActiveState);
      const vbox = chatShortcutsOptionsPanel.querySelector("vbox");
      vbox.innerHTML = "";

      const currentIsSmartWindow = lazy.AIWindow.isAIWindowActive(
        document.defaultView
      );
      const showWarning = this.isContextTooLong(panel.selectionData.selection);
      const chatProvider = this.chatProviders.get(lazy.chatProvider);

      // Show warning if selection is too long
      if (showWarning) {
        vbox.appendChild(createMessageBarWarning(chatProvider));
      }

      const addItem = () => {
        const button = vbox.appendChild(
          document.createXULElement("toolbarbutton")
        );
        button.className = "subviewbutton";
        button.setAttribute("tabindex", "0");
        return button;
      };

      const browser = document.documentGlobal.gBrowser.selectedBrowser;
      const context = await this.addAskChatItems(
        browser,
        panel.selectionData,
        promptObj => {
          if (currentIsSmartWindow && promptObj.id === "quiz") {
            return null;
          }
          const button = addItem();
          button.textContent = promptObj.label;
          return button;
        },
        "shortcuts",
        () => {
          panel.markInteracted();
          panel.hide();
        }
      );

      // Add custom textarea box if configured
      if (lazy.chatShortcutsCustom) {
        const textAreaEl = vbox.appendChild(document.createElement("textarea"));
        let textAreaL10nId;
        if (currentIsSmartWindow) {
          textAreaL10nId = "genai-input-ask-smart-window";
        } else if (chatProvider?.name) {
          textAreaL10nId = "genai-input-ask-provider";
        } else {
          textAreaL10nId = "genai-input-ask-generic";
        }
        document.l10n.setAttributes(textAreaEl, textAreaL10nId, {
          provider: chatProvider?.name,
        });

        textAreaEl.className = "ask-chat-shortcuts-custom-prompt";
        textAreaEl.addEventListener("mouseover", () => textAreaEl.focus());
        textAreaEl.addEventListener("keydown", event => {
          if (event.key == "Enter" && !event.shiftKey) {
            this.handleAskChat({ value: textAreaEl.value }, context);
            panel.markInteracted();
            panel.hide();
          }
        });

        // For Content Analysis, we need to specify the URL that the data is being sent to.
        // In this case it's not the URL in the browsingContext (like it is in other cases),
        // but the URL of the chatProvider is close enough to where the content will eventually
        // be sent. Only applicable when an external provider is configured.
        if (lazy.chatProvider) {
          lazy.ContentAnalysisUtils.setupContentAnalysisEventsForTextElement(
            textAreaEl,
            browser.browsingContext,
            Services.io.newURI(lazy.chatProvider)
          );
        }

        const resetHeight = () => {
          textAreaEl.style.height = "auto";
          textAreaEl.style.height = textAreaEl.scrollHeight + "px";
        };

        textAreaEl.addEventListener("input", resetHeight);
        chatShortcutsOptionsPanel.addEventListener("popupshown", resetHeight, {
          once: true,
        });
      }

      // Allow hiding these shortcuts (not shown in Smart Window or with the
      // Highlight to Search feature turned on)
      if (!currentIsSmartWindow && !lazy.highlightToSearchFeatureGate) {
        vbox.appendChild(document.createXULElement("toolbarseparator"));
        const hider = addItem();
        document.l10n.setAttributes(hider, "genai-shortcuts-hide");
        hider.addEventListener("command", () => {
          Services.prefs.setBoolPref("browser.ml.chat.shortcuts", false);
          Glean.genaiChatbot.shortcutsHideClick.record({
            selection: panel.selectionData.selection.length,
          });
        });
      }

      chatShortcutsOptionsPanel.openPopup(panel, "after_start", 0, 10);
      Glean.genaiChatbot.shortcutsExpanded.record({
        selection: panel.selectionData.selection.length,
        provider: this.getProviderId(),
        warning: showWarning,
      });
      Glean.selectionMenu.actionClick.record({
        action: "ai",
        selection: panel.selectionData.selection.length,
        smart_window: currentIsSmartWindow,
      });
    };

    // ask popup shows on mouseover only in the first two times
    const hasMouseoverOnPopup = () => {
      const mouseoverCounter = lazy.shortcutMouseoverCount;
      const maxMouseoverCount = 2;

      if (mouseoverCounter >= maxMouseoverCount || isCalloutOpen()) {
        return;
      }

      if (chatShortcutsOptionsPanel.state == "closed") {
        Services.prefs.setIntPref(
          "browser.ml.chat.shortcut.onboardingMouseoverCount",
          mouseoverCounter + 1
        );
        buildPopup();
      }
    };

    aiActionButton.addEventListener("mouseover", hasMouseoverOnPopup);

    // Detect click to build and toggle the popup
    aiActionButton.addEventListener("click", async () => {
      if (chatShortcutsOptionsPanel.state != "closed") {
        chatShortcutsOptionsPanel.hidePopup();
        return;
      }
      buildPopup();
    });
  },

  /**
   * Handle messages from content to show or hide shortcuts.
   *
   * @param {string} name of message
   * @param {{
   *   inputType: string,
   *   host?: string,
   *   selection: string,
   *   delay: number,
   *   x: number,
   *   y: number,
   * }} data for the message
   * @param {MozBrowser} browser that provided the message
   * @param {nsIURI} [documentURI] URI of the frame that sent the message
   * @param {WindowGlobalParent} [windowGlobal] frame that sent the message
   */
  handleShortcutsMessage(name, data, browser, documentURI, windowGlobal) {
    const isInBrowserStack = browser?.closest(".browserStack");

    const isSmartWindow =
      browser && lazy.AIWindow.isAIWindowActive(browser.documentGlobal);
    if (
      !isInBrowserStack ||
      !browser ||
      !this.isSupportedContext(browser) ||
      this.ignoredInputs.has(data.inputType) ||
      this.ignoredInputs.has(data.host) ||
      (isSmartWindow
        ? !lazy.chatShortcutsSmartWindow
        : !this.canShowSelectionMenu)
    ) {
      return;
    }

    if (
      !isSmartWindow &&
      lazy.highlightToSearchFeatureGate &&
      this.isIgnoredSite(documentURI)
    ) {
      if (name == "GenAI:ShowShortcuts") {
        Glean.selectionMenu.suppressed.record({ reason: "site_list" });
      }
      return;
    }

    const window = browser.documentGlobal;
    const { document, devicePixelRatio } = window;
    const shortcutPanel = document.getElementById(
      "selection-shortcut-action-panel"
    );
    if (isSmartWindow) {
      this.initializeSelectionShortcutPanel(
        shortcutPanel,
        "chrome://browser/content/aiwindow/assets/new-chat.svg"
      );
    } else {
      this.initializeSelectionShortcutPanel(shortcutPanel);
    }

    switch (name) {
      case "GenAI:HideDropdowns":
        shortcutPanel.hideDropdowns();
        break;
      case "GenAI:HideShortcuts": {
        // For an IME selection change, hide via CSS
        // to avoid hidePopup() cancelling the active IME composition.
        // Any other hide reason closes the panel normally.
        const imeHiding = data === "selectionchange-ime";
        shortcutPanel.toggleAttribute("ime-hiding", imeHiding);
        if (imeHiding) {
          shortcutPanel.recordDismissed();
          shortcutPanel.hideCallout();
        } else {
          shortcutPanel.hide();
        }
        break;
      }
      case "GenAI:SuppressedShortcuts": {
        if (!isSmartWindow && shortcutPanel.state != "closed") {
          Glean.selectionMenu.suppressed.record({ reason: data.reason });
          shortcutPanel.hide();
        }
        break;
      }
      case "GenAI:ShowShortcuts": {
        // Save the latest selection so it can be used by popup
        shortcutPanel.selectionData = { ...data, browser, windowGlobal };
        shortcutPanel.resetCopyConfirmation();

        // Smart Window has its own separate gate above and only ever offers
        // the AI action.
        const { hasActions, searchEngine } = shortcutPanel.updateActions(
          data.selection,
          browser,
          isSmartWindow
        );
        if (!hasActions) {
          // Nothing to offer: don't open an empty menu or log an impression
          // of one, and close any menu left from the previous selection.
          shortcutPanel.hide();
          return;
        }
        if (!isSmartWindow && data.pageMenu) {
          Glean.selectionMenu.suppressed.record({ reason: "page_menu" });
          shortcutPanel.hide();
          return;
        }

        // Clear any CSS hide from a prior selectionchange so the panel
        // is visible when it opens for the new selection.
        shortcutPanel.removeAttribute("ime-hiding");
        shortcutPanel.showCallout();

        shortcutPanel.recordDismissed();
        Glean.genaiChatbot.shortcutsDisplayed.record({
          delay: data.delay,
          inputType: data.inputType,
          selection: data.selection.length,
        });
        Glean.selectionMenu.displayed.record({
          delay: data.delay,
          selection: data.selection.length,
          smart_window: isSmartWindow,
        });
        if (searchEngine) {
          lazy.BrowserSearchTelemetry.recordSapImpression(
            browser,
            searchEngine,
            "text_selection"
          );
        }
        shortcutPanel.startDisplay(isSmartWindow);

        // Position the shortcuts relative to the browser's top-left corner
        const screenYBase = data.screenYDevPx / devicePixelRatio;
        const safeSpace = window.outerHeight - 40;
        // Remove padding if the popup would be offscreen
        const bottomPadding = screenYBase > safeSpace ? 0 : 40;
        const screenX = data.screenXDevPx / devicePixelRatio;
        const screenY = screenYBase + bottomPadding;

        const isRTL = Services.locale.isAppLocaleRTL;
        const xOffset = isRTL
          ? browser.screenX + browser.getBoundingClientRect().width - screenX
          : screenX - browser.screenX;

        shortcutPanel.openPopup(
          browser,
          "before_start",
          xOffset,
          screenY - browser.screenY
        );
        break;
      }
    }
  },

  /**
   * Determine whether a warning should be shown depending on provider max length
   *
   * @param {string} selection selected text from context
   */
  isContextTooLong(selection) {
    const chatProvider = this.chatProviders.get(lazy.chatProvider);
    const selectionLength = selection.length;

    return (
      this.estimateSelectionLimit(chatProvider?.maxLength) < selectionLength
    );
  },

  /**
   * Create <moz-message-bar> warning element
   *
   * @param {Document} document the element
   * @param {string | null} className css class to apply
   * @param {string | null} dismissable attribute setting
   */
  createWarningEl(document, className, dismissable) {
    const mozMessageBarEl = document.createElement("moz-message-bar");

    mozMessageBarEl.dataset.l10nAttrs = "heading,message";
    mozMessageBarEl.setAttribute("type", "warning");
    if (dismissable) {
      mozMessageBarEl.setAttribute("dismissable", true);
    }

    if (className) {
      mozMessageBarEl.className = className;
    }

    return mozMessageBarEl;
  },

  /**
   * Whether the browser's window is an active Smart Window (AI window).
   *
   * @param {MozBrowser} browser browser for the context's page
   * @returns {boolean}
   */
  isSmartWindow(browser) {
    return lazy.AIWindow.isAIWindowActive(
      browser.documentGlobal.browsingContext?.topChromeWindow ??
        browser.documentGlobal
    );
  },

  /**
   * Whether the browser's context supports gen-AI surfaces at all. Shared by
   * the ask-chat entrypoints and the selection menu so that both refuse the
   * same contexts.
   *
   * @param {MozBrowser} browser browser for the context's page
   * @returns {boolean}
   */
  isSupportedContext(browser) {
    // DO NOT show when inside an extension panel
    const uri = browser.browsingContext?.currentURI.spec;
    if (uri?.startsWith("moz-extension:")) {
      return false;
    }

    // Popups don't have a sidebar, so don't show the menu.
    // Also, it's not useful for most Document Picture-in-Picture API use-cases.
    const isPopup = browser.documentGlobal.toolbar?.visible === false;
    return !browser.browsingContext?.isDocumentPiP && !isPopup;
  },

  /**
   * Whether the page is on a site whose own menu for selected text the actions
   * menu would overlap. A site in the list also matches its subdomains.
   *
   * @param {nsIURI | null} uri URI of the frame the selection is in
   * @returns {boolean}
   */
  isIgnoredSite(uri) {
    let host;
    try {
      host = uri.asciiHost;
    } catch {
      return false;
    }
    return this.ignoredSites.some(
      site => host == site || host.endsWith("." + site)
    );
  },

  /**
   * @param {MozBrowser} browser browser for the context's page
   * @param {string} source one of "page", "tab", "tool"
   * @param {MozTabbrowserTab[] | null} contextTabs tabs for a "tab" source
   * @param {object | null} selectionInfo selection details, if any
   * @returns {boolean}
   */
  canShowAskChat(browser, source, contextTabs, selectionInfo) {
    if (!this.isSupportedContext(browser)) {
      return false;
    }

    const isSmartWindow = this.isSmartWindow(browser);
    if (isSmartWindow && !selectionInfo?.text) {
      return false;
    }

    // Page feature can be shown without provider unless disabled via menu
    // or revamp sidebar excludes chatbot
    const isPageFeatureAllowed =
      lazy.chatPage &&
      (lazy.chatProvider != "" || lazy.chatMenu) &&
      (!lazy.sidebarRevamp || lazy.sidebarTools.includes("aichat"));

    const isSingleTab = contextTabs?.length === 1;
    switch (source) {
      case "page":
        return isSmartWindow
          ? true
          : this.canShowChatEntrypoint || isPageFeatureAllowed;
      case "tab":
        return isSmartWindow
          ? isSingleTab
          : isPageFeatureAllowed && isSingleTab;
      case "tool":
        return lazy.chatPage;
    }
    return false;
  },

  /**
   * Build the prompt context, using the current selection when present and
   * otherwise the page URL.
   *
   * @param {MozBrowser} browser browser for the context's page
   * @param {object | null} selectionInfo selection details, if any
   * @returns {Promise<object>} context for getContextualPrompts / handleAskChat
   */
  async buildAskChatContext(browser, selectionInfo) {
    const context = {
      contentType: "selection",
      selection: selectionInfo?.fullText ?? "",
    };
    if (lazy.chatPage && !context.selection) {
      // Get page URL for prompts when no selection
      this.addPageContext(browser, context);
    }
    return context;
  },

  /**
   * Build prompts menu to ask chat for context menu.
   *
   * @param {MozMenu} menu element to update
   * @param {object} contextMenu object containing utility and states for building the context menu
   */
  async buildAskChatMenu(menu, contextMenu) {
    const {
      browser,
      selectionInfo,
      showItem = this.showItem,
      source,
      contextTabs = null,
    } = contextMenu;

    if (!this.canShowAskChat(browser, source, contextTabs, selectionInfo)) {
      showItem(menu, false);
      return;
    }

    const isSmartWindow = this.isSmartWindow(browser);

    const provider = this.chatProviders.get(lazy.chatProvider)?.name;
    const doc = menu.ownerDocument;

    // Only "page" and "tab" contexts need a <menu> submenu
    if (source !== "tool") {
      if (isSmartWindow) {
        doc.l10n.setAttributes(menu, "genai-menu-ask-smart-window");
      } else if (provider) {
        doc.l10n.setAttributes(menu, "genai-menu-ask-provider-2", { provider });
      } else {
        doc.l10n.setAttributes(
          menu,
          lazy.chatProvider
            ? "genai-menu-ask-generic-2"
            : "genai-menu-no-provider-2"
        );
      }
      menu.menupopup?.remove();
    }

    // NOTE: Show the menu item synchronously, before any `await`.
    showItem(menu, true);

    const context = await this.buildAskChatContext(browser, selectionInfo);
    const addItem = () =>
      source === "tool"
        ? menu.appendChild(doc.createXULElement("menuitem"))
        : menu.appendItem("");
    await this.addAskChatItems(
      browser,
      context,
      promptObj => {
        if (isSmartWindow && promptObj.id === "quiz") {
          return null;
        }
        const { contentType, pageUrl } = context;
        const item = addItem();
        item.setAttribute("label", promptObj.label);

        // Disabled menu if page is invalid
        if (contentType === "page" && !pageUrl) {
          item.disabled = true;
        }
        if (promptObj.badge && lazy.chatPageMenuBadge) {
          item.setAttribute("badge", promptObj.badge);
        }

        return item;
      },
      source,
      item => {
        // Currently only summarize page shows a badge, so remove when clicked
        if (item.hasAttribute("badge")) {
          Services.prefs.setBoolPref("browser.ml.chat.page.menuBadge", false);
        }
      }
    );

    // For page which currently only shows 1 prompt, make it less empty with an
    // Open or Choose options depending on provider
    if (!isSmartWindow && context.contentType == "page") {
      const openItem = addItem();
      if (provider) {
        doc.l10n.setAttributes(openItem, "genai-menu-open-provider", {
          provider,
        });
      } else {
        doc.l10n.setAttributes(
          openItem,
          lazy.chatProvider
            ? "genai-menu-open-generic"
            : "genai-menu-choose-chatbot"
        );
      }
      openItem.addEventListener("command", () => {
        const window = browser.documentGlobal;
        window.SidebarController.show("viewGenaiChatSidebar");
        Glean.genaiChatbot.contextmenuChoose.record({
          provider: this.getProviderId(),
        });
      });
    }

    // Add remove provider option — not applicable in Smart Window
    if (isSmartWindow) {
      return;
    }
    const popup = source === "tool" ? menu : menu.menupopup;
    popup.appendChild(doc.createXULElement("menuseparator"));
    const removeItem = addItem();
    doc.l10n.setAttributes(
      removeItem,
      provider ? "genai-menu-remove-provider" : "genai-menu-remove-generic",
      { provider }
    );
    removeItem.addEventListener("command", () => {
      Glean.genaiChatbot.contextmenuRemove.record({
        provider: this.getProviderId(),
      });
      if (lazy.chatProvider) {
        Services.prefs.clearUserPref("browser.ml.chat.provider");
      } else if (source === "tool") {
        // When there's no provider set this menu should remove chatbot as a tool
        lazy.SidebarManager.updateToolsPref("aichat", true);
      } else {
        Services.prefs.setBoolPref("browser.ml.chat.menu", false);
      }
    });
  },

  /**
   *
   * Build the buildAskChatMenu item for the tab context menu
   *
   * @param {MozMenu} menu the tab menu to update
   * @param {object} tabContextMenu the tab context menu instance
   * @returns {promise} resolve when the menu item is configured
   */
  async buildTabMenu(menu, tabContextMenu) {
    const { contextTab, contextTabs } = tabContextMenu;

    const browser = contextTab?.linkedBrowser;
    await this.buildAskChatMenu(menu, {
      browser,
      selectionInfo: null,
      showItem: (item, shouldShow) => {
        const separator = item.nextElementSibling;
        this.showItem(item, shouldShow);

        if (separator && separator.localName === "menuseparator") {
          this.showItem(separator, shouldShow);
        }
      },
      source: "tab",
      contextTabs,
    });
  },

  /**
   * Build a single "Summarize Page" item for the tab context menu, used by the
   * alternate layout in place of the full ask-chat submenu. Reuses the same
   * gating, page context, prompt targeting and click handling as
   * buildAskChatMenu, but renders only the page-summarize prompt as a flat item.
   *
   * @param {MozMenuItem} item the menuitem to populate and show or hide
   * @param {object} tabContextMenu the tab context menu instance
   * @returns {promise} resolve when the item is configured
   */
  async buildTabSummarizeItem(item, tabContextMenu) {
    const { contextTab, contextTabs } = tabContextMenu;
    const browser = contextTab?.linkedBrowser;

    if (!browser || !this.canShowAskChat(browser, "tab", contextTabs, null)) {
      this.showItem(item, false);
      return;
    }

    // Reuse the ask-chat pipeline to build the full handling context (including
    // the window handleAskChat needs) and select prompts; capture the page
    // summarize prompt and render it as a flat item below rather than via the
    // pipeline's itemAdder.
    let summarize;
    const context = await this.addAskChatItems(
      browser,
      await this.buildAskChatContext(browser, null),
      promptObj => {
        if (promptObj.id === "summarize") {
          summarize = promptObj;
        }
        // addAskChatItems prepares the content but the itemAdder we pass returns null
        // here; the item is created & rendered below
        return null;
      },
      "tab"
    );
    if (!summarize) {
      this.showItem(item, false);
      return;
    }

    item.setAttribute(
      "label",
      await lazy.l10n.formatValue("genai-menu-summarize-with-ai")
    );
    if (summarize.badge && lazy.chatPageMenuBadge) {
      item.setAttribute("badge", summarize.badge);
    } else {
      item.removeAttribute("badge");
    }
    // Disabled when the page has no URL the provider can summarize.
    item.disabled = context.contentType === "page" && !context.pageUrl;
    this.showItem(item, true);

    // The item is reused across shows, so refresh the prompt/context it acts on
    // and attach the command handler only once to avoid stacking listeners.
    item.summarizePrompt = summarize;
    item.summarizeContext = context;
    if (!item.hasSummarizeHandler) {
      item.hasSummarizeHandler = true;
      item.addEventListener("command", () => {
        this.handleAskChat(item.summarizePrompt, item.summarizeContext);
        // Summarize is the only badged prompt; clear the badge once used.
        if (item.hasAttribute("badge")) {
          Services.prefs.setBoolPref("browser.ml.chat.page.menuBadge", false);
        }
      });
    }
  },

  /**
   * Toggle the visibility of the chatbot menu item
   *
   * @param {MozMenu} item the chatbot menu item element
   * @param {boolean} shouldShow whether to show or hide the item
   */
  showItem(item, shouldShow) {
    item.hidden = !shouldShow;
  },

  /**
   * Get prompts from prefs evaluated with context
   *
   * @param {object} context data used for targeting
   * @returns {promise} array of matching prompt objects
   */
  async getContextualPrompts(context) {
    // Treat prompt objects as messages to reuse targeting capabilities
    const messages = [];
    const toFormat = [];
    Services.prefs.getChildList("browser.ml.chat.prompts.").forEach(pref => {
      try {
        const promptObj = {
          label: Services.prefs.getStringPref(pref),
          targeting: "true",
          value: "",
        };
        try {
          // Prompts can be JSON with label, value, targeting and other keys
          Object.assign(promptObj, JSON.parse(promptObj.label));

          // Ignore provided id (if any) for modified prefs
          if (Services.prefs.prefHasUserValue(pref)) {
            promptObj.id = null;
          }
        } catch (ex) {}
        messages.push(promptObj);
        if (promptObj.l10nId) {
          toFormat.push(promptObj);
        }
      } catch (ex) {
        console.error("Failed to get prompt pref " + pref, ex);
      }
    });

    // Apply localized attributes for prompts
    (await lazy.l10n.formatMessages(toFormat.map(obj => obj.l10nId))).forEach(
      (msg, idx) =>
        msg?.attributes.forEach(attr => (toFormat[idx][attr.name] = attr.value))
    );

    // Specially handle page summarization prompt
    if (context.contentType == "page") {
      for (const promptObj of toFormat) {
        if (promptObj.id == "summarize") {
          const [badge, label, value] = await lazy.l10n.formatValues([
            "genai-menu-new-badge",
            "genai-menu-summarize-page",
            { id: "genai-prompts-summarize-page", args: { url: "%pageUrl%" } },
          ]);
          promptObj.badge = badge;
          promptObj.label = label;
          promptObj.value = value;
        }
      }
    }

    return lazy.ASRouterTargeting.findMatchingMessage({
      messages,
      returnAll: true,
      trigger: { context },
    });
  },

  /**
   * Approximately adjust query limit for encoding and other text in prompt,
   * e.g., page title, per-prompt instructions. Generally more conservative as
   * going over the limit results in server errors.
   *
   * @param {number} maxLength optional of the provider request URI
   * @returns {number} adjusted length estimate
   */
  estimateSelectionLimit(maxLength = lazy.chatMaxLength) {
    // Could try to be smarter including the selected text with URI encoding,
    // base URI length, other parts of the prompt (especially for custom)
    return Math.round(maxLength * 0.85) - 500;
  },

  /**
   * Updates chat prompt prefix.
   */
  async prepareChatPromptPrefix() {
    if (
      !this.chatPromptPrefix ||
      this.chatLastPrefix != lazy.chatPromptPrefix
    ) {
      try {
        // Check json for localized prefix
        const prefixObj = JSON.parse(lazy.chatPromptPrefix);
        this.chatPromptPrefix = (
          await lazy.l10n.formatMessages([
            {
              id: prefixObj.l10nId,
              args: {
                selection: `%selection|${this.estimateSelectionLimit(
                  this.chatProviders.get(lazy.chatProvider)?.maxLength
                )}%`,
                tabTitle: "%tabTitle|50%",
                url: "%url%",
              },
            },
          ])
        )[0].value;
      } catch (ex) {
        // Treat as plain text prefix
        this.chatPromptPrefix = lazy.chatPromptPrefix;
      }
      if (this.chatPromptPrefix) {
        this.chatPromptPrefix += "\n\n";
      }
      this.chatLastPrefix = lazy.chatPromptPrefix;
    }
  },

  /**
   * Build a prompt with context.
   *
   * @param {MozMenuItem} item Use value falling back to label
   * @param {object} context Placeholder keys with values to replace
   * @param {Document} document Document for sanitizing context values
   * @returns {string} Prompt with placeholders replaced
   */
  buildChatPrompt(item, context = {}, document = null) {
    // Combine prompt prefix with the item then replace placeholders from the
    // original prompt (and not from context). Page prompts skip the selection
    // prefix as they only reference the page URL.
    const prefix = context.contentType == "page" ? "" : this.chatPromptPrefix;
    const template = prefix + (item.value || item.label);
    const prompt = template.replace(
      // Handle %placeholder% as key|options
      /\%(\w+)(?:\|([^%]+))?\%/g,
      (placeholder, key, options) => {
        // Currently only supporting numeric options for slice with `undefined`
        // resulting in whole string. Also remove fake int tags from untrusted content.
        const value = context[key];
        let sanitized;

        // pageUrl is an http(s) URI spec that is already percent-encoded, and
        // sanitizing it as HTML would break the "&" in the query.
        if (key == "pageUrl" && value !== undefined) {
          sanitized = value;
        } else if (value !== undefined) {
          // Sanitize and truncate context values before sending prompt
          // otherwise return placeholder
          const contextElement = document.createElement("div");
          sanitized = lazy.parserUtils.parseFragment(
            value,
            Ci.nsIParserUtils.SanitizerDropForms |
              Ci.nsIParserUtils.SanitizerDropMedia,
            false,
            Services.io.newURI("about:blank"),
            contextElement
          ).textContent;

          if (options) {
            sanitized = sanitized.slice(0, Number(options));
          }

          sanitized = sanitized
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#39;");
        } else {
          sanitized = placeholder;
        }

        return `<${key}>${sanitized}</${key}>`;
      }
    );
    if (
      context.contentType == "page" &&
      context.pageUrl &&
      !/%pageUrl(?:\|[^%]+)?%/.test(template)
    ) {
      return `${prompt}\n\n<pageUrl>${context.pageUrl}</pageUrl>`;
    }
    return prompt;
  },

  /**
   * Get the page URL to send with page prompts. Only http(s) pages get a URL
   * as providers can't access other schemes.
   *
   * @param {MozBrowser} browser for the tab to get the URL
   * @returns {string | undefined} URL without credentials or ref
   */
  getPageUrl(browser) {
    let uri = browser?.currentURI;
    const readerOriginalUrl = uri && lazy.ReaderMode.getOriginalUrl(uri.spec);
    if (readerOriginalUrl) {
      try {
        uri = Services.io.newURI(readerOriginalUrl);
      } catch {
        return undefined;
      }
    }
    if (uri?.schemeIs("http") || uri?.schemeIs("https")) {
      return Services.io.createExposableURI(uri).specIgnoringRef;
    }
    return undefined;
  },

  /**
   * Update context with the page URL instead of the page content, so that no
   * page-controlled text is placed in the prompt.
   *
   * @param {MozBrowser} browser for the tab to get the URL
   * @param {object} context optional existing context to update
   * @returns {object} updated context
   */
  addPageContext(browser, context = {}) {
    context.contentType = "page";
    context.pageUrl = this.getPageUrl(browser);
    return context;
  },

  /**
   * Summarize the current page content.
   *
   * @param {Window} window chrome window with tabs
   * @param {string} entry name
   */
  async summarizeCurrentPage(window, entry) {
    const browser = window.gBrowser.selectedBrowser;
    const context = this.addPageContext(browser);
    if (!context.pageUrl) {
      return;
    }
    await this.addAskChatItems(
      browser,
      context,
      (promptObj, context2) => {
        if (promptObj.id === "summarize") {
          this.handleAskChat(promptObj, context2);
        }
      },
      entry
    );
  },

  /**
   * Set up automatic prompt submission for ChatGPT and Claude
   *
   * @param {Browser} browser - current browser
   * @param {string} prompt - prompt text
   * @param {object} context of how the prompt should be handled
   */
  setupAutoSubmit(browser, prompt, context) {
    const sendAutoSubmit = (br, promptText) => {
      const wgp = br.browsingContext?.currentWindowGlobal;
      const actor = wgp?.getActor("GenAI");
      if (!actor) {
        return;
      }

      try {
        actor.sendAsyncMessage("AutoSubmit", {
          promptText,
        });
      } catch (e) {
        console.error("error message: ", e);
      }
    };

    if (lazy.chatSidebar) {
      const injector = {
        async onStateChange(_wp, _req, flags) {
          const stopDoc =
            flags & Ci.nsIWebProgressListener.STATE_STOP &&
            flags & Ci.nsIWebProgressListener.STATE_IS_DOCUMENT;
          if (!stopDoc) {
            return;
          }

          const wgp = browser.browsingContext?.currentWindowGlobal;
          if (!wgp || wgp.isInitialDocument) {
            return;
          }

          try {
            browser.webProgress?.removeProgressListener(injector);
          } catch {}
          await sendAutoSubmit(browser, prompt);
        },
        QueryInterface: ChromeUtils.generateQI([
          "nsIWebProgressListener",
          "nsISupportsWeakReference",
        ]),
      };

      browser.webProgress?.addProgressListener(
        injector,
        Ci.nsIWebProgress.NOTIFY_STATE_DOCUMENT
      );
    } else {
      // Tab mode:
      const gBrowser = context.window.gBrowser;
      const targetBrowser = browser;

      const tabListener = {
        async onLocationChange(br, _wp, _req, location) {
          if (br !== targetBrowser) {
            return;
          }

          const spec = location?.spec || "";
          if (spec === "about:blank") {
            return;
          }

          try {
            gBrowser.removeTabsProgressListener(tabListener);
          } catch {}
          await sendAutoSubmit(browser, prompt);
        },
        QueryInterface: ChromeUtils.generateQI([
          "nsIwebProgressListener",
          "nsISupportsWeakReference",
        ]),
      };

      gBrowser.addTabsProgressListener(tabListener);
    }
  },

  /**
   * Handle selected prompt by opening tab or sidebar.
   *
   * @param {object} promptObj to convert to string
   * @param {object} context of how the prompt should be handled
   */
  async handleAskChat(promptObj, context) {
    // Record up to 3 types of event telemetry for backwards compatibility
    const isPageSummarizeRequest =
      promptObj.id == "summarize" && context.contentType == "page";
    if (isPageSummarizeRequest) {
      Glean.genaiChatbot.summarizePage.record({
        provider: this.getProviderId(),
        source: context.entry,
      });
    }
    if (["page", "shortcuts"].includes(context.entry)) {
      Glean.genaiChatbot[
        context.entry == "page"
          ? "contextmenuPromptClick"
          : "shortcutsPromptClick"
      ].record({
        prompt: promptObj.id ?? "custom",
        provider: this.getProviderId(),
        selection: context.selection?.length ?? 0,
      });
    }
    const win =
      context.window?.browsingContext?.topChromeWindow ?? context.window;
    Glean.genaiChatbot.promptClick.record({
      content_type: context.contentType,
      prompt: promptObj.id ?? "custom",
      provider: this.getProviderId(),
      selection: context.selection?.length ?? 0,
      smart_window: lazy.AIWindow.isAIWindowActive(win),
      source: context.entry,
    });

    // In Smart Window, send selected text or page URL with prompt label to the
    // assistant
    if (lazy.AIWindow.isAIWindowActive(win)) {
      if (!lazy.AIWindowUI.isSidebarOpen(win)) {
        const activeConversation = lazy.AIWindow.getActiveConversation(win);
        await lazy.AIWindowUI.openSidebar(win, activeConversation ?? undefined);
      }
      const aiWindowEl = lazy.AIWindowUI._getSidebarAiWindow(win);
      if (aiWindowEl) {
        // TODO (Bug 2048401): Revisit prompt construction once Smart Window prompt definitions
        // are finalized via Remote Settings.
        const content =
          context.contentType == "page" ? context.pageUrl : context.selection;
        const text = promptObj.label
          ? `${promptObj.label}: ${content}`
          : `${promptObj.value}\n\n${content}`;
        aiWindowEl.submitChatMessage({ text, submitType: "shortcuts" });
      }
      return;
    }

    // If no provider is configured, open sidebar and wait once for onboarding
    const { SidebarController } = context.window;

    if (!lazy.chatProvider) {
      await SidebarController.show("viewGenaiChatSidebar");
      await SidebarController.browser.contentWindow.onboardingPromise;
      if (!lazy.chatProvider) {
        return;
      }
    }

    // Build prompt after provider is confirmed to use correct length limits
    await this.prepareChatPromptPrefix();
    const prompt = this.buildChatPrompt(
      promptObj,
      {
        ...context,
      },
      context.window.document
    );

    // Pass the prompt via GET url ?q= param or request header. When the
    // provider supports auto-submit, deliver the prompt through the textarea
    // instead so the request URI stays short — long ?q= values combined with
    // the user's cookies can otherwise exceed the server's request-line/header
    // size limit (e.g. ChatGPT/CloudFlare returns 431 Request Header Fields
    // Too Large for long page summaries from a logged-in profile).
    const {
      header,
      queryParam = "q",
      supportAutoSubmit,
    } = this.chatProviders.get(lazy.chatProvider) ?? {};
    const url = new URL(lazy.chatProvider);
    const options = {
      inBackground: false,
      relatedToCurrent: true,
      triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
        {}
      ),
    };

    if (header) {
      options.headers = Cc[
        "@mozilla.org/io/string-input-stream;1"
      ].createInstance(Ci.nsIStringInputStream);
      options.headers.setByteStringData(
        `${header}: ${encodeURIComponent(prompt)}\r\n`
      );
    } else if (!supportAutoSubmit) {
      url.searchParams.set(queryParam, prompt);
    }

    // Get the desired browser to handle the prompt url request
    let browser;
    if (lazy.chatSidebar) {
      await SidebarController.show("viewGenaiChatSidebar");
      browser = await SidebarController.browser.contentWindow.browserPromise;
      if (!browser) {
        console.error("Failed to get chat sidebar browser");
        return;
      }
    } else {
      browser = context.window.gBrowser.addTab("", options).linkedBrowser;
    }
    browser.fixupAndLoadURIString(url, options);

    // Run autosubmit only for chatGPT, Claude, or mochitest
    if (
      supportAutoSubmit ||
      lazy.chatProvider?.includes("file_chat-autosubmit.html")
    ) {
      this.setupAutoSubmit(browser, prompt, context);
    }
  },

  get id() {
    return "sidebar-chatbot";
  },

  get hasDistinctEnabledState() {
    // The sidebar chatbot has a distinct enabled state based on choosing a
    // specific provider instead of using a single generic "Enabled" option.
    return true;
  },

  get isBlocked() {
    return !lazy.chatEnabled;
  },

  get isEnabled() {
    return lazy.chatEnabled && lazy.chatProvider != "";
  },

  get isAllowed() {
    return true;
  },

  get canRunOnDevice() {
    // The sidebar chatbot has no known restrictions based on device hardware.
    return true;
  },

  get isManagedByPolicy() {
    return (
      Services.prefs.prefIsLocked(PREF_CHAT_ENABLED) ||
      Services.prefs.prefIsLocked(PREF_CHAT_PROVIDER) ||
      Services.prefs.prefIsLocked(PREF_CHAT_PAGE)
    );
  },

  async makeAvailable() {
    // Set explicitly rather than clearing, so that a non-locked policy default
    // of "blocked" does not prevent the user from switching back to "available".
    Services.prefs.setStringPref(PREF_AI_CONTROL_SIDEBAR_CHATBOT, "available");
    Services.prefs.setBoolPref(PREF_CHAT_ENABLED, true);
    Services.prefs.clearUserPref(PREF_CHAT_PAGE);
    Services.prefs.clearUserPref(PREF_CHAT_PROVIDER);
  },

  async enable() {
    Services.prefs.setBoolPref(PREF_CHAT_ENABLED, true);
    Services.prefs.setBoolPref(PREF_CHAT_PAGE, true);
    // We don't know what to set browser.ml.chat.provider to, so really we'll be
    // "available" unless it's set elsewhere.
  },

  async block() {
    Services.prefs.setBoolPref(PREF_CHAT_ENABLED, false);
    Services.prefs.setBoolPref(PREF_CHAT_PAGE, false);
    Services.prefs.clearUserPref(PREF_CHAT_PROVIDER);
  },
};

Object.setPrototypeOf(GenAI, AIFeature);

/**
 * Ensure the chat sidebar get closed.
 *
 * @param {bool} value New pref value
 */
function onChatEnabledChange(value) {
  if (!value) {
    lazy.EveryWindow.readyWindows.forEach(({ SidebarController }) => {
      if (
        SidebarController.isOpen &&
        SidebarController.currentID == "viewGenaiChatSidebar"
      ) {
        SidebarController.hide();
      }
    });
  }
}

/**
 * Ensure the chat sidebar is shown to reflect changed provider.
 *
 * @param {string} value New pref value
 */
function onChatProviderChange(value) {
  if (value && lazy.chatEnabled && lazy.chatOpenSidebarOnProviderChange) {
    Services.wm
      .getMostRecentWindow("navigator:browser")
      ?.SidebarController.show("viewGenaiChatSidebar");
  }

  // Recalculate query limit on provider change
  GenAI.chatLastPrefix = null;

  // Refreshes the sidebar icon and label for all open windows
  lazy.EveryWindow.readyWindows.forEach(window => {
    window.SidebarController.addOrUpdateExtension("viewGenaiChatSidebar", {});
  });
}

/**
 * Ensure the chat shortcuts get hidden.
 *
 * @param {bool} value New pref value
 */
function onChatShortcutsChange(value) {
  if (!value) {
    lazy.EveryWindow.readyWindows.forEach(window => {
      const selectionShortcutActionPanel = window.document.getElementById(
        "selection-shortcut-action-panel"
      );

      if (!selectionShortcutActionPanel.editingActions) {
        selectionShortcutActionPanel.hidePopup();
      }
    });
  }
}

/**
 * Update the ordering of chat providers Map.
 */
function reorderChatProviders() {
  // Figure out which providers to include in order
  const ordered = lazy.chatProviders.split(",");
  if (!lazy.chatHideLocalhost) {
    ordered.push("localhost");
  }

  // Convert the url keys to lookup by id
  const idToKey = new Map([...GenAI.chatProviders].map(([k, v]) => [v.id, k]));

  // Remove providers in the desired order and make them shown
  const toSet = [];
  ordered.forEach(id => {
    const key = idToKey.get(id);
    const val = GenAI.chatProviders.get(key);
    if (val) {
      val.hidden = false;
      toSet.push([key, val]);
      GenAI.chatProviders.delete(key);
    }
  });

  // Hide unremoved providers before re-adding visible ones in order
  GenAI.chatProviders.forEach(val => (val.hidden = true));
  toSet.forEach(args => GenAI.chatProviders.set(...args));
}

/**
 * Update ignored sites list.
 */
function updateIgnoredSites() {
  GenAI.ignoredSites = lazy.highlightToSearchIgnoredSites
    .split(",")
    .map(site => site.trim().toLowerCase())
    .filter(site => site);
}

/**
 * Update ignored input fields Set.
 */
function updateIgnoredInputs() {
  // Skip empty string as no input type is ""
  const ignored = lazy.chatShortcutsIgnoreFields.split(",").filter(v => v);
  if (lazy.highlightToSearchFeatureGate) {
    ignored.push("textarea");
  }
  GenAI.ignoredInputs = new Set(ignored);
}

// Initialize on first import
GenAI.init();
