/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * Toolbar of actions for the http(s) page shown in a Smart Window.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const TOOLBAR_ID = "smart-actions-toolbar";

/**
 * @typedef {object} Suggestion
 * @property {string} type - Key of ACTION_TYPES.
 * @property {string} label - Button text.
 * @property {string} [detail] - Extra input for types that need more than
 *   the label, like a monitor condition.
 */

/**
 * @typedef {object} ToolbarAction
 * @property {string} label - Button text.
 * @property {string} iconSrc - Icon shown before the label.
 * @property {() => Promise<void>} run - Called on click. The button stays
 *   disabled until it settles.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  AIWindowUI:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs",
  AuthProvider:
    "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs",
  buildConversation:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  generateConversationStartersSidebar:
    "moz-src:///browser/components/aiwindow/models/ConversationSuggestions.sys.mjs",
  makeJSONSchemaBlob:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  MODEL_FEATURES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  parseAndExtractJSON:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  renderPrompt: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  sanitizeUntrustedContent:
    "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "actionCount",
  "browser.smartwindow.actionsToolbar.actionCount"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "enabled",
  "browser.smartwindow.actionsToolbar.enabled",
  undefined,
  () => SmartActionsToolbar.updateAll()
);
// Template for the selected chat model, which answers with JSON actions that
// each have a type, a label and an optional detail. Empty uses the sidebar
// conversation starters instead.
// TODO: Bug 2093449 - review the page content the default prompt includes
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "prompt",
  "browser.smartwindow.actionsToolbar.prompt",
  undefined,
  () => {
    loadedKeys = new WeakMap();
    SmartActionsToolbar.updateAll();
  }
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "refreshDelayMs",
  "browser.smartwindow.actionsToolbar.refreshDelayMs"
);

/** @type {WeakMap<ChromeWindow, object>} */
const windowStates = new WeakMap();

// TODO: Bug 2092109 - proper cache including titles to reuse actions
/**
 * Context key each window last loaded actions for.
 *
 * @type {WeakMap<ChromeWindow, ?string>}
 */
let loadedKeys = new WeakMap();

/**
 * Builds the toolbar action for each type of suggestion. Types that can run
 * their label ignore the detail, so a generated detail can't make a button do
 * something other than what it says.
 *
 * @type {Record<
 *   string,
 *   (win: ChromeWindow, label: string, detail: string) => ToolbarAction
 * >}
 */
const ACTION_TYPES = {
  chat: chatAction,
  jump: jumpAction,
  search: searchAction,
};

/**
 * @param {ChromeWindow} win
 * @param {string} label - Button text, also the message sent.
 * @returns {ToolbarAction} Starts a sidebar chat about this page.
 */
function chatAction(win, label) {
  const url = win.gBrowser.currentURI.spec;
  return {
    label,
    iconSrc: "chrome://browser/content/aiwindow/assets/ask-icon.svg",
    run: () =>
      lazy.AIWindowUI.submitToNewSidebarChat(win, {
        text: label,
        submitType: "toolbar",
        contextMentions: [
          { type: "currentTab", url, label: win.gBrowser.selectedTab.label },
        ],
        contextPageUrl: URL.parse(url),
      }),
  };
}

/**
 * @param {ChromeWindow} win
 * @param {string} label - Button text.
 * @param {string} text - The #id of a section the page links to, or text on
 *   the page to scroll to and highlight.
 * @returns {ToolbarAction} Scrolls to that part of the page.
 */
function jumpAction(win, label, text) {
  const fragment = text.startsWith("#")
    ? text
    : `#:~:text=${encodeURIComponent(text.replace(/\s+/g, " ")).replace(/-/g, "%2D")}`;
  return {
    label,
    iconSrc: "chrome://global/skin/icons/arrow-down.svg",
    run: () => {
      const browser = win.gBrowser.selectedBrowser;
      browser.loadURI(
        Services.io.newURI(win.gBrowser.currentURI.specIgnoringRef + fragment),
        { triggeringPrincipal: browser.contentPrincipal }
      );
    },
  };
}

/**
 * @param {ChromeWindow} win
 * @param {string} label - Button text, also the search terms.
 * @returns {ToolbarAction} Searches with the default engine.
 */
function searchAction(win, label) {
  return {
    label,
    iconSrc: "chrome://global/skin/icons/search-glass.svg",
    // TODO: Bug 2092650 - proper search access point instead of the assistant
    run: () => lazy.AIWindow.performSearch(label, win),
  };
}

/**
 * Readers for the placeholders a prompt can use, run in this order. Only the
 * ones the prompt uses are read. {page_text|10000} keeps that many characters,
 * {page_anchors|10} that many #ids the page links to on itself and
 * {open_tabs|5} that many tabs. A {#key}...{/key} section is dropped when that
 * value is empty. Page text goes first since it waits for the page to load, so
 * the title read after it is the loaded one.
 *
 * @type {Record<
 *   string,
 *   (
 *     win: ChromeWindow,
 *     limit: number | undefined,
 *     readPage: (sufficientLength?: number) => Promise<?object>
 *   ) => string | number | Promise<string>
 * >}
 */
const PROMPT_VALUES = {
  page_text: async (win, limit, readPage) =>
    (await readPage(limit))?.text.slice(0, limit) ?? "",
  page_anchors: async (win, limit, readPage) => {
    const page = win.gBrowser.currentURI.specIgnoringRef;
    const anchors = new Set();
    for (const href of (await readPage())?.links ?? []) {
      const url = URL.parse(href);
      if (url?.hash.length > 1 && url.href.slice(0, -url.hash.length) == page) {
        anchors.add(url.hash);
      }
    }
    return [...anchors].slice(0, limit).join("\n");
  },
  title: win => lazy.sanitizeUntrustedContent(win.gBrowser.selectedTab.label),
  url: win => win.gBrowser.currentURI.specIgnoringRef,
  open_tabs: (win, limit) =>
    win.gBrowser.tabs
      .filter(tab => {
        const uri = tab.linkedBrowser.currentURI;
        return !tab.selected && (uri.schemeIs("http") || uri.schemeIs("https"));
      })
      .sort((a, b) => b.lastAccessed - a.lastAccessed)
      .slice(0, limit)
      .map(tab => `- ${lazy.sanitizeUntrustedContent(tab.label)}`)
      .join("\n"),
  n: () => lazy.actionCount,
  locale: () => Services.locale.appLocaleAsBCP47,
  date: () => new Date().toISOString().slice(0, 10),
};

/**
 * @param {ChromeWindow} win
 * @returns {boolean} Whether the toolbar can show in this window, so whether
 *   it needs to follow navigation. Consent is checked per page instead, since
 *   granting it doesn't call update.
 */
function isActive(win) {
  return (
    lazy.enabled &&
    lazy.AIWindow.isAIWindowEnabled() &&
    lazy.AIWindow.isAIWindowActive(win)
  );
}

/**
 * @param {ChromeWindow} win
 * @returns {?string} What the actions depend on, or null when the page can't
 *   have any. Fragment changes keep the same actions.
 */
function contextKey(win) {
  const uri = win.gBrowser.currentURI;
  if (
    !lazy.AIWindow.isEnabled ||
    (!uri.schemeIs("http") && !uri.schemeIs("https"))
  ) {
    return null;
  }
  return uri.specIgnoringRef;
}

/**
 * @param {ChromeWindow} win
 * @param {AbortSignal} signal
 * @returns {Promise<Suggestion[]>}
 */
async function generateFromPrompt(win, signal) {
  // Limit of each placeholder the prompt uses, if it has one.
  const used = new Map();
  const template = lazy.prompt.replace(
    /\{(#?)(\w+)(?:\|(\d+))?\}/g,
    (_match, section, key, limit) => {
      used.set(key, limit ? Number(limit) : used.get(key));
      return `{${section}${key}}`;
    }
  );
  let extraction;
  const readPage = sufficientLength =>
    (extraction ??=
      win.gBrowser.selectedBrowser.browsingContext.currentWindowGlobal
        ?.getActor("PageExtractor")
        .getText({
          sufficientLength,
          cleanWhitespace: true,
          removeBoilerplate: true,
          useSimpleText: true,
        }));
  const values = {};
  for (const [key, read] of Object.entries(PROMPT_VALUES)) {
    if (used.has(key)) {
      values[key] = await read(win, used.get(key), readPage);
    }
  }
  signal.throwIfAborted();
  // TODO: Bug 2092649 - proper prompt feature instead of pref and chat model
  const conversation = await lazy.buildConversation(lazy.MODEL_FEATURES.CHAT);
  conversation.addUserMessage(lazy.renderPrompt(template, values));
  const result = await conversation.run({
    token: await lazy.AuthProvider.getToken(),
    signal,
    inferenceParams: {
      response_format: lazy.makeJSONSchemaBlob("SmartActions", {
        type: "object",
        properties: {
          actions: {
            type: "array",
            items: {
              type: "object",
              properties: {
                type: { type: "string", enum: Object.keys(ACTION_TYPES) },
                label: { type: "string" },
                detail: { type: "string" },
              },
              required: ["type", "label"],
            },
          },
        },
        required: ["actions"],
      }),
    },
  });
  const actions = lazy.parseAndExtractJSON(result, {})?.actions;
  return Array.isArray(actions)
    ? actions.filter(
        action =>
          Object.hasOwn(ACTION_TYPES, action?.type) &&
          typeof action.label == "string" &&
          action.label.trim()
      )
    : [];
}

/**
 * @param {ChromeWindow} win
 * @param {AbortSignal} signal
 * @returns {Promise<ToolbarAction[]>}
 */
async function suggestActions(win, signal) {
  const suggestions = lazy.prompt
    ? await generateFromPrompt(win, signal)
    : (
        await lazy.generateConversationStartersSidebar(
          [
            {
              title: win.gBrowser.selectedTab.label,
              url: win.gBrowser.currentURI.spec,
            },
          ],
          lazy.actionCount,
          false,
          null,
          signal
        )
      ).map(({ text, type }) => ({ type, label: text }));
  // Built after generating, so the page title has had time to load.
  return suggestions
    .slice(0, lazy.actionCount)
    .map(({ type, label, detail }) =>
      ACTION_TYPES[type](win, label, detail?.trim?.() || label)
    );
}

/**
 * @param {ChromeWindow} win
 * @param {?ToolbarAction[]} actions - null while loading.
 */
function render(win, actions) {
  const doc = win.document;
  let toolbar = doc.getElementById(TOOLBAR_ID);
  if (!toolbar) {
    // TODO: Bug 2092110 - proper toolbar instead of inserting it
    win.windowUtils.loadSheetUsingURIString(
      "chrome://browser/content/aiwindow/smart-actions-toolbar.css",
      Ci.nsIDOMWindowUtils.AUTHOR_SHEET
    );
    toolbar = doc.createXULElement("toolbar");
    toolbar.id = TOOLBAR_ID;
    toolbar.className = "browser-toolbar";
    toolbar.setAttribute("fullscreentoolbar", "true");
    doc.l10n.setAttributes(toolbar, "smart-actions-toolbar");
    doc.getElementById("nav-bar").after(toolbar);
  }

  if (!actions) {
    const loading = doc.createElement("span");
    loading.className = "smart-actions-loading";
    doc.l10n.setAttributes(loading, "smart-actions-loading");
    toolbar.replaceChildren(loading);
    toolbar.hidden = false;
    return;
  }
  toolbar.replaceChildren(
    ...actions.map(action => {
      const button = doc.createElement("moz-button");
      button.type = "toolbar";
      button.size = "small";
      button.iconSrc = action.iconSrc;
      button.label = action.label;
      button.title = action.label;
      button.addEventListener("click", async () => {
        button.disabled = true;
        try {
          await action.run();
        } finally {
          button.disabled = false;
        }
      });
      return button;
    })
  );
  toolbar.hidden = !actions.length;
}

export const SmartActionsToolbar = {
  /**
   * @param {ChromeWindow} win
   */
  init(win) {
    windowStates.set(win, {
      controller: null,
      listening: false,
      // Tabbrowser also sends onLocationChange on tab switches.
      progressListener: { onLocationChange: () => this.update(win) },
    });
    this.update(win);
  },

  /**
   * @param {ChromeWindow} win
   */
  uninit(win) {
    const state = windowStates.get(win);
    if (!state) {
      return;
    }
    state.controller?.abort();
    if (state.listening) {
      win.gBrowser.removeProgressListener(state.progressListener);
    }
    windowStates.delete(win);
  },

  updateAll() {
    for (const win of ChromeUtils.nondeterministicGetWeakMapKeys(
      windowStates
    )) {
      if (!win.closed) {
        this.update(win);
      }
    }
  },

  /**
   * Follows navigation only while the toolbar can show. When the context key
   * changed, hides the toolbar or loads new actions.
   *
   * @param {ChromeWindow} win
   */
  update(win) {
    const state = windowStates.get(win);
    if (!state) {
      return;
    }
    const listen = isActive(win);
    if (listen !== state.listening) {
      state.listening = listen;
      if (listen) {
        win.gBrowser.addProgressListener(state.progressListener);
      } else {
        win.gBrowser.removeProgressListener(state.progressListener);
      }
    }
    const key = listen ? contextKey(win) : null;
    if (key === loadedKeys.get(win)) {
      return;
    }
    loadedKeys.set(win, key);
    state.controller?.abort();
    if (!key) {
      const toolbar = win.document.getElementById(TOOLBAR_ID);
      if (toolbar) {
        toolbar.hidden = true;
      }
      return;
    }

    state.controller = new AbortController();
    const { signal } = state.controller;
    render(win, null);
    win.setTimeout(async () => {
      if (signal.aborted) {
        return;
      }
      let actions = [];
      try {
        actions = await suggestActions(win, signal);
      } catch (error) {
        if (!signal.aborted) {
          console.error("Failed to suggest smart actions:", error);
        }
      }
      if (!signal.aborted) {
        render(win, actions);
      }
    }, lazy.refreshDelayMs);
  },
};
