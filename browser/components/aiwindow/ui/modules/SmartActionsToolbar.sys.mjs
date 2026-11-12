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
  generateConversationStartersSidebar:
    "moz-src:///browser/components/aiwindow/models/ConversationSuggestions.sys.mjs",
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
 * Builds the toolbar action for each type of suggestion.
 *
 * @type {Record<
 *   string,
 *   (win: ChromeWindow, suggestion: Suggestion) => ToolbarAction
 * >}
 */
const ACTION_TYPES = {
  chat: (win, { label }) => {
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
  },
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
 * @returns {Promise<ToolbarAction[]>}
 */
async function suggestActions(win, signal) {
  const suggestions = (
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
    .map(suggestion => ACTION_TYPES[suggestion.type](win, suggestion));
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
