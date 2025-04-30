/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ASRouterTargeting: "resource:///modules/asrouter/ASRouterTargeting.sys.mjs",
  ContentAnalysisUtils: "resource://gre/modules/ContentAnalysisUtils.sys.mjs",
  EveryWindow: "resource:///modules/EveryWindow.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PrefUtils: "resource://normandy/lib/PrefUtils.sys.mjs",
});
ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () => new Localization(["browser/genai.ftl"])
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatEnabled",
  "browser.ml.chat.enabled",
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
  "chatNimbus",
  "browser.ml.chat.nimbus"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatOpenSidebarOnProviderChange",
  "browser.ml.chat.openSidebarOnProviderChange",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatPromptPrefix",
  "browser.ml.chat.prompt.prefix"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatProvider",
  "browser.ml.chat.provider",
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
  "chatShortcutsIgnoreFields",
  "browser.ml.chat.shortcuts.ignoreFields",
  "input",
  updateIgnoredInputs
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chatSidebar",
  "browser.ml.chat.sidebar"
);
XPCOMUtils.defineLazyPreferenceGetter(lazy, "sidebarRevamp", "sidebar.revamp");
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "sidebarTools",
  "sidebar.main.tools"
);

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
        choiceIds: [
          "genai-onboarding-claude-generate",
          "genai-onboarding-claude-analyze",
          "genai-onboarding-claude-price",
        ],
        iconUrl: "chrome://browser/content/genai/assets/brands/claude.svg",
        id: "claude",
        learnId: "genai-onboarding-claude-learn",
        learnLink: "https://www.anthropic.com/claude",
        link1:
          "https://www.anthropic.com/legal/archive/6370fb23-12ed-41d9-a4a2-28866dee3105",
        link2:
          "https://www.anthropic.com/legal/archive/7197103a-5e27-4ee4-93b1-f2d4c39ba1e7",
        link3:
          "https://www.anthropic.com/legal/archive/628feec9-7df9-4d38-bc69-fbf104df47b0",
        linksId: "genai-settings-chat-claude-links",
        maxLength: 15020,
        name: "Anthropic Claude",
        tooltipId: "genai-onboarding-claude-tooltip",
      },
    ],
    [
      "https://chatgpt.com",
      {
        choiceIds: [
          "genai-onboarding-chatgpt-generate",
          "genai-onboarding-chatgpt-analyze",
          "genai-onboarding-chatgpt-price",
        ],
        iconUrl: "chrome://browser/content/genai/assets/brands/chatgpt.svg",
        id: "chatgpt",
        learnId: "genai-onboarding-chatgpt-learn",
        learnLink: "https://help.openai.com/articles/6783457-what-is-chatgpt",
        link1: "https://openai.com/terms",
        link2: "https://openai.com/privacy",
        linksId: "genai-settings-chat-chatgpt-links",
        maxLength: 14140,
        name: "ChatGPT",
        tooltipId: "genai-onboarding-chatgpt-tooltip",
      },
    ],
    [
      "https://copilot.microsoft.com",
      {
        choiceIds: [
          "genai-onboarding-copilot-generate",
          "genai-onboarding-copilot-analyze",
          "genai-onboarding-copilot-price",
        ],
        iconUrl: "chrome://browser/content/genai/assets/brands/copilot.svg",
        id: "copilot",
        learnId: "genai-onboarding-copilot-learn",
        learnLink: "https://www.microsoft.com/microsoft-copilot/learn/",
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
        choiceIds: [
          "genai-onboarding-gemini-generate",
          "genai-onboarding-gemini-analyze",
          "genai-onboarding-gemini-price",
        ],
        header: "X-Firefox-Gemini",
        iconUrl: "chrome://browser/content/genai/assets/brands/gemini.svg",
        id: "gemini",
        learnId: "genai-onboarding-gemini-learn",
        learnLink: "https://gemini.google.com/faq",
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
        choiceIds: [
          "genai-onboarding-huggingchat-generate",
          "genai-onboarding-huggingchat-switch",
          "genai-onboarding-huggingchat-price-2",
        ],
        iconUrl: "chrome://browser/content/genai/assets/brands/huggingchat.svg",
        id: "huggingchat",
        learnId: "genai-onboarding-huggingchat-learn",
        learnLink: "https://huggingface.co/chat/privacy/",
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
        choiceIds: [
          "genai-onboarding-lechat-generate",
          "genai-onboarding-lechat-price",
        ],
        iconUrl: "chrome://browser/content/genai/assets/brands/lechat.svg",
        id: "lechat",
        learnId: "genai-onboarding-lechat-learn",
        learnLink: "https://help.mistral.ai/collections/272960-le-chat",
        link1: "https://mistral.ai/terms/#terms-of-service-le-chat",
        link2: "https://mistral.ai/terms/#privacy-policy",
        linksId: "genai-settings-chat-lechat-links",
        maxLength: 3680,
        name: "Le Chat Mistral",
        tooltipId: "genai-onboarding-lechat-tooltip",
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
   * Determine if chat entrypoints can be shown
   *
   * @returns {bool} can show
   */
  get canShowChatEntrypoint() {
    return (
      lazy.chatEnabled &&
      lazy.chatProvider != "" &&
      // Chatbot needs to be a tool if new sidebar
      (!lazy.sidebarRevamp || lazy.sidebarTools.includes("aichat"))
    );
  },

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

    // Apply initial ordering of providers
    reorderChatProviders();
    updateIgnoredInputs();

    // Handle nimbus feature pref setting
    const featureId = "chatbot";
    lazy.NimbusFeatures[featureId].onUpdate(() => {
      // Prefer experiments over rollouts
      const feature = { featureId };
      const enrollment =
        lazy.ExperimentAPI.getExperimentMetaData(feature) ??
        lazy.ExperimentAPI.getRolloutMetaData(feature);
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
          lazy.NimbusFeatures[featureId].getVariable("minVersion")
        ) < 0
      ) {
        return;
      }

      // Set prefs on any branch if we have a new enrollment slug, otherwise
      // only set default branch as those only last for the session
      const slug = enrollment.slug + ":" + enrollment.branch.slug;
      const anyBranch = slug != lazy.chatNimbus;
      const setPref = ([pref, { branch = "user", value = null }]) => {
        if (anyBranch || branch == "default") {
          lazy.PrefUtils.setPref("browser.ml.chat." + pref, value, { branch });
        }
      };
      setPref(["nimbus", { value: slug }]);
      Object.entries(
        lazy.NimbusFeatures[featureId].getVariable("prefs")
      ).forEach(setPref);
    });

    // Record glean metrics after applying nimbus prefs
    Glean.genaiChatbot.enabled.set(lazy.chatEnabled);
    Glean.genaiChatbot.provider.set(this.getProviderId());
    Glean.genaiChatbot.shortcuts.set(lazy.chatShortcuts);
    Glean.genaiChatbot.shortcutsCustom.set(lazy.chatShortcutsCustom);
    Glean.genaiChatbot.sidebar.set(lazy.chatSidebar);
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
    const window = browser.ownerGlobal;
    const tab = window.gBrowser.getTabForBrowser(browser);
    const uri = browser.currentURI;
    const context = {
      ...extraContext,
      entry,
      provider: lazy.chatProvider,
      tabTitle: (tab?._labelIsContentTitle && tab?.label) || "",
      url: uri.asciiHost + uri.filePath,
      window,
    };

    // Add items that pass along context for handling
    (await this.getContextualPrompts(context)).forEach(promptObj =>
      itemAdder(promptObj).addEventListener("command", () => {
        this.handleAskChat(promptObj, context);
        cleanup?.();
      })
    );
    return context;
  },

  /**
   * Setup helpers and callbacks for ai shortcut button.
   *
   * @param {MozButton} aiActionButton instance for the browser window
   */
  initializeAIShortcut(aiActionButton) {
    if (aiActionButton.initialized) {
      return;
    }
    aiActionButton.initialized = true;

    const document = aiActionButton.ownerDocument;
    const buttonActiveState = "icon";
    const buttonDefaultState = "icon ghost";
    const chatShortcutsOptionsPanel = document.getElementById(
      "chat-shortcuts-options-panel"
    );
    const selectionShortcutActionPanel = document.getElementById(
      "selection-shortcut-action-panel"
    );
    aiActionButton.hide = () => {
      chatShortcutsOptionsPanel.hidePopup();
      selectionShortcutActionPanel.hidePopup();
    };
    aiActionButton.iconSrc = "chrome://global/skin/icons/highlights.svg";
    aiActionButton.setAttribute("type", buttonDefaultState);
    chatShortcutsOptionsPanel.addEventListener("popuphidden", () =>
      aiActionButton.setAttribute("type", buttonDefaultState)
    );
    chatShortcutsOptionsPanel.firstChild.id = "ask-chat-shortcuts";

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
      const mozMessageBarEl = document.createElement("moz-message-bar");

      // Create MozMessageBar
      mozMessageBarEl.dataset.l10nAttrs = "heading,message";
      mozMessageBarEl.setAttribute("type", "warning");
      mozMessageBarEl.className = "ask-chat-shortcut-warning";

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
          aiActionButton.data.selection.length
        ),
      });

      return mozMessageBarEl;
    };

    // Detect hover to build and open the popup
    aiActionButton.addEventListener("mouseover", async () => {
      if (chatShortcutsOptionsPanel.state != "closed") {
        return;
      }

      aiActionButton.setAttribute("type", buttonActiveState);
      const vbox = chatShortcutsOptionsPanel.querySelector("vbox");
      vbox.innerHTML = "";

      const chatProvider = this.chatProviders.get(lazy.chatProvider);
      const selectionLength = aiActionButton.data.selection.length;
      const showWarning =
        this.estimateSelectionLimit(chatProvider?.maxLength) < selectionLength;

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

      const browser = document.ownerGlobal.gBrowser.selectedBrowser;
      const context = await this.addAskChatItems(
        browser,
        aiActionButton.data,
        promptObj => {
          const button = addItem();
          button.textContent = promptObj.label;
          return button;
        },
        "shortcuts",
        aiActionButton.hide
      );

      // Add custom textarea box if configured
      if (lazy.chatShortcutsCustom) {
        const textAreaEl = vbox.appendChild(document.createElement("textarea"));
        document.l10n.setAttributes(
          textAreaEl,
          chatProvider?.name
            ? "genai-input-ask-provider"
            : "genai-input-ask-generic",
          { provider: chatProvider?.name }
        );

        textAreaEl.className = "ask-chat-shortcuts-custom-prompt";
        textAreaEl.addEventListener("mouseover", () => textAreaEl.focus());
        textAreaEl.addEventListener("keydown", event => {
          if (event.key == "Enter" && !event.shiftKey) {
            this.handleAskChat({ value: textAreaEl.value }, context);
            aiActionButton.hide();
          }
        });

        // For Content Analysis, we need to specify the URL that the data is being sent to.
        // In this case it's not the URL in the browsingContext (like it is in other cases),
        // but the URL of the chatProvider is close enough to where the content will eventually
        // be sent.
        lazy.ContentAnalysisUtils.setupContentAnalysisEventsForTextElement(
          textAreaEl,
          browser.browsingContext,
          Services.io.newURI(lazy.chatProvider)
        );

        const resetHeight = () => {
          textAreaEl.style.height = "auto";
          textAreaEl.style.height = textAreaEl.scrollHeight + "px";
        };

        textAreaEl.addEventListener("input", resetHeight);
        chatShortcutsOptionsPanel.addEventListener("popupshown", resetHeight, {
          once: true,
        });
      }

      // Allow hiding these shortcuts
      vbox.appendChild(document.createXULElement("toolbarseparator"));
      const hider = addItem();
      document.l10n.setAttributes(hider, "genai-shortcuts-hide");
      hider.addEventListener("command", () => {
        Services.prefs.setBoolPref("browser.ml.chat.shortcuts", false);
        Glean.genaiChatbot.shortcutsHideClick.record({
          selection: aiActionButton.data.selection.length,
        });
      });

      chatShortcutsOptionsPanel.openPopup(
        selectionShortcutActionPanel,
        "after_start",
        0,
        10
      );
      Glean.genaiChatbot.shortcutsExpanded.record({
        selection: aiActionButton.data.selection.length,
        provider: this.getProviderId(),
        warning: showWarning,
      });
    });
  },

  /**
   * Handle messages from content to show or hide shortcuts.
   *
   * @param {string} name of message
   * @param {{
   *   inputType: string,
   *   selection: string,
   *   delay: number,
   *   x: number,
   *   y: number,
   * }} data for the message
   * @param {MozBrowser} browser that provided the message
   */
  handleShortcutsMessage(name, data, browser) {
    const isInBrowserStack = browser?.closest(".browserStack");

    if (
      !isInBrowserStack ||
      !browser ||
      this.ignoredInputs.has(data.inputType) ||
      !lazy.chatShortcuts ||
      !this.canShowChatEntrypoint
    ) {
      return;
    }

    const window = browser.ownerGlobal;
    const { document, devicePixelRatio } = window;
    const aiActionButton = document.getElementById("ai-action-button");
    this.initializeAIShortcut(aiActionButton);

    switch (name) {
      case "GenAI:HideShortcuts":
        aiActionButton.hide();
        break;
      case "GenAI:ShowShortcuts": {
        // Save the latest selection so it can be used by popup
        aiActionButton.data = data;

        Glean.genaiChatbot.shortcutsDisplayed.record({
          delay: data.delay,
          inputType: data.inputType,
          selection: data.selection.length,
        });

        // Position the shortcuts relative to the browser's top-left corner
        const screenYBase = data.screenYDevPx / devicePixelRatio;
        const safeSpace = window.outerHeight - 40;
        // Remove padding if the popup would be offscreen
        const bottomPadding = screenYBase > safeSpace ? 0 : 40;
        const screenX = data.screenXDevPx / devicePixelRatio;
        const screenY = screenYBase + bottomPadding;

        aiActionButton
          .closest("panel")
          .openPopup(
            browser,
            "before_start",
            screenX - browser.screenX,
            screenY - browser.screenY
          );
        break;
      }
    }
  },

  /**
   * Build prompts menu to ask chat for context menu.
   *
   * @param {MozMenu} menu element to update
   * @param {nsContextMenu} nsContextMenu helpers for context menu
   */
  async buildAskChatMenu(menu, nsContextMenu) {
    nsContextMenu.showItem(menu, false);
    if (!this.canShowChatEntrypoint) {
      return;
    }
    const provider = this.chatProviders.get(lazy.chatProvider)?.name;
    const doc = menu.ownerDocument;
    doc.l10n.setAttributes(
      menu,
      provider ? "genai-menu-ask-provider" : "genai-menu-ask-generic",
      { provider }
    );
    menu.menupopup?.remove();
    await this.addAskChatItems(
      nsContextMenu.browser,
      { selection: nsContextMenu.selectionInfo.fullText ?? "" },
      promptObj => menu.appendItem(promptObj.label),
      "menu"
    );

    // Add separator and remove provider option
    const hasPrompts = menu.itemCount > 0;
    if (hasPrompts) {
      menu.menupopup.appendChild(doc.createXULElement("menuseparator"));
      const removeItem = menu.appendItem("");
      doc.l10n.setAttributes(
        removeItem,
        provider ? "genai-menu-remove-provider" : "genai-menu-remove-generic",
        { provider }
      );
      removeItem.addEventListener("command", () => {
        Glean.genaiChatbot.contextmenuRemove.record({
          provider: this.getProviderId(),
        });
        Services.prefs.clearUserPref("browser.ml.chat.provider");
      });
    }

    nsContextMenu.showItem(menu, hasPrompts);
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
  estimateSelectionLimit(maxLength = 8000) {
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
                tabTitle: "%tabTitle%",
                selection: `%selection|${this.estimateSelectionLimit(
                  this.chatProviders.get(lazy.chatProvider)?.maxLength
                )}%`,
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
   * @returns {string} Prompt with placeholders replaced
   */
  buildChatPrompt(item, context = {}) {
    // Combine prompt prefix with the item then replace placeholders from the
    // original prompt (and not from context)
    return (this.chatPromptPrefix + (item.value || item.label)).replace(
      // Handle %placeholder% as key|options
      /\%(\w+)(?:\|([^%]+))?\%/g,
      (placeholder, key, options) =>
        // Currently only supporting numeric options for slice with `undefined`
        // resulting in whole string
        `<${key}>${context[key]?.slice(0, options) ?? placeholder}</${key}>`
    );
  },

  /**
   * Handle selected prompt by opening tab or sidebar.
   *
   * @param {object} promptObj to convert to string
   * @param {object} context of how the prompt should be handled
   */
  async handleAskChat(promptObj, context) {
    Glean.genaiChatbot[
      context.entry == "menu"
        ? "contextmenuPromptClick"
        : "shortcutsPromptClick"
    ].record({
      prompt: promptObj.id ?? "custom",
      provider: this.getProviderId(),
      selection: context.selection?.length ?? 0,
    });

    await this.prepareChatPromptPrefix();
    const prompt = this.buildChatPrompt(promptObj, context);

    // Pass the prompt via GET url ?q= param or request header
    const { header, queryParam = "q" } =
      this.chatProviders.get(lazy.chatProvider) ?? {};
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
    } else {
      url.searchParams.set(queryParam, prompt);
    }

    // Get the desired browser to handle the prompt url request
    let browser;
    if (lazy.chatSidebar) {
      const { SidebarController } = context.window;
      await SidebarController.show("viewGenaiChatSidebar");
      browser = await SidebarController.browser.contentWindow.browserPromise;
    } else {
      browser = context.window.gBrowser.addTab("", options).linkedBrowser;
    }
    browser.fixupAndLoadURIString(url, options);
  },
};

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

      selectionShortcutActionPanel.hidePopup();
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
 * Update ignored input fields Set.
 */
function updateIgnoredInputs() {
  GenAI.ignoredInputs = new Set(
    // Skip empty string as no input type is ""
    lazy.chatShortcutsIgnoreFields.split(",").filter(v => v)
  );
}

// Initialize on first import
GenAI.init();
