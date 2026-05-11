/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import {
  AIWINDOW_URL,
  AIWindow,
} from "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs";

const gFadingWindows = new WeakSet();

export const AIWindowUI = {
  BOX_ID: "ai-window-box",
  SPLITTER_ID: "ai-window-splitter",
  BROWSER_ID: "ai-window-browser",
  STACK_CLASS: "ai-window-browser-stack",
  AI_WINDOW_ELEMENT_TIMEOUT: 1500,
  TAB_FADE_MS: 200,
  TAB_FADE_TIMEOUT_MS: 200 * 2 + 50,

  /**
   * @param {Window} win
   * @returns {{ chromeDoc: Document, box: Element, splitter: Element } | null}
   */
  _getSidebarElements(win) {
    if (!win) {
      return null;
    }
    const chromeDoc = win.document;
    const box = chromeDoc.getElementById(this.BOX_ID);
    const splitter = chromeDoc.getElementById(this.SPLITTER_ID);

    if (!box || !splitter) {
      return null;
    }
    return { chromeDoc, box, splitter };
  },

  /**
   * @param {Window} win
   * @returns {{ chatId: string, messageSeq: number }}
   */
  _getConversationFromSidebar(win) {
    const conversation = AIWindow.getActiveConversation(win);
    return {
      chatId: conversation?.id ?? "",
      messageSeq: conversation?.messageCount ?? 0,
    };
  },

  /**
   * Ensure the aiwindow <browser> exists under the sidebar box.
   *
   * @param {Document} chromeDoc
   * @param {Element} box
   * @returns {XULElement} browser
   */
  ensureBrowserIsAppended(chromeDoc, box) {
    const existingBrowser = chromeDoc.getElementById(this.BROWSER_ID);
    if (existingBrowser) {
      return existingBrowser;
    }

    const stack = box.querySelector(`.${this.STACK_CLASS}`);

    if (!stack.isConnected) {
      stack.className = this.STACK_CLASS;
      stack.setAttribute("flex", "1");
      box.appendChild(stack);
    }

    const browser = chromeDoc.createXULElement("browser");
    browser.id = this.BROWSER_ID;
    browser.setAttribute("transparent", "true");
    browser.setAttribute("flex", "1");
    browser.setAttribute("disablehistory", "true");
    browser.setAttribute("disablefullscreen", "true");
    browser.setAttribute("tooltip", "aHTMLTooltip");
    browser.setAttribute("src", AIWINDOW_URL);
    browser.setAttribute("type", "content");
    stack.appendChild(browser);
    return browser;
  },

  /**
   * @param {Window} win
   * @returns {boolean} whether the sidebar is open (visible)
   */
  isSidebarOpen(win) {
    const nodes = this._getSidebarElements(win);
    if (!nodes) {
      return false;
    }
    return !nodes.box.collapsed;
  },

  _showSidebarElements(box, splitter) {
    box.collapsed = false;
    splitter.collapsed = false;
    box.parentElement.collapsed = false;
  },

  /**
   * Open the AI Window in full window mode
   *
   * @param {Browser} browser
   * @param {ChatConversation} conversation The conversation to open
   */
  openInFullWindow(browser, conversation) {
    this.closeSidebar(browser.documentGlobal);

    browser.setAttribute("data-conversation-id", conversation.id);

    const { contentDocument } = browser;
    contentDocument.dispatchEvent(
      new browser.contentWindow.CustomEvent("OpenConversation", {
        detail: conversation,
      })
    );
  },

  /**
   * Open the AI Window sidebar
   *
   * @param {Window} win
   * @param {ChatConversation} conversation The conversation to open in the sidebar
   */
  async openSidebar(win, conversation) {
    const nodes = this._getSidebarElements(win);
    if (!nodes) {
      return;
    }

    const { box, splitter } = nodes;
    const aiBrowser = this.ensureBrowserIsAppended(win.document, box);

    if (!this.isSidebarOpen(win)) {
      this._showSidebarElements(box, splitter);
      this._updateAskButtonChecked(win, true);
    }

    Glean.smartWindow.sidebarOpen.record({
      chat_id: conversation?.id ?? "",
      message_seq: conversation?.messageCount ?? 0,
    });

    // Dispatch event to notify tab state manager that sidebar was toggled
    win.dispatchEvent(
      new win.CustomEvent("ai-window:sidebar-toggle", {
        detail: {
          tab: win.gBrowser.selectedTab,
          isOpen: true,
          source: "open",
        },
      })
    );

    if (conversation) {
      aiBrowser.setAttribute("data-conversation-id", conversation.id);
    } else {
      aiBrowser.removeAttribute("data-conversation-id");
    }

    const aiWindowElement = await this.getAiWindowElement(win, aiBrowser);
    if (!aiWindowElement) {
      return;
    }

    // Return early if the sidebar was closed while we were waiting for the
    // content element to load, prevents opening a conversation or creating
    // a new one if the sidebar is closed anyway.
    if (!this.isSidebarOpen(win)) {
      return;
    }

    if (conversation) {
      aiWindowElement.openConversation(conversation);
      return;
    }
    aiWindowElement.onCreateNewChatClick();
  },

  /**
   * Gets the ai-window element from the sidebar browser. Polls until the
   * custom element is defined or the timeout is reached.
   *
   * @param {Window} win
   * @param {XULElement} aiBrowser
   *
   * @returns {Promise<AIWindow>} The sidebar AIWindow component
   */
  async getAiWindowElement(win, aiBrowser) {
    const deadline = Date.now() + AIWindowUI.AI_WINDOW_ELEMENT_TIMEOUT;
    while (Date.now() < deadline) {
      const el = aiBrowser.contentDocument?.querySelector("ai-window:defined");
      if (el) {
        return el;
      }
      await new Promise(resolve => win.setTimeout(resolve, 50));
    }
    return null;
  },

  /**
   * Close the AI Window sidebar.
   *
   * @param {Window} win
   * @param {string} source
   */
  closeSidebar(win, source = null) {
    if (!this.isSidebarOpen(win)) {
      return;
    }
    const { box, splitter } = this._getSidebarElements(win);

    box.collapsed = true;
    splitter.collapsed = true;
    this._updateAskButtonChecked(win, false);

    // Dispatch event to notify tab state manager that sidebar was toggled
    win.dispatchEvent(
      new win.CustomEvent("ai-window:sidebar-toggle", {
        detail: {
          tab: win.gBrowser?.selectedTab,
          isOpen: false,
          ...(source && { source }),
        },
      })
    );

    const { chatId, messageSeq } = this._getConversationFromSidebar(win);
    Glean.smartWindow.sidebarClose.record({
      chat_id: chatId,
      message_seq: messageSeq,
    });
  },

  /**
   * Toggle the AI Window sidebar
   *
   * @param {Window} win
   * @returns {boolean} true if now open, false if now closed
   */
  toggleSidebar(win) {
    if (this.isSidebarOpen(win)) {
      this.closeSidebar(win, "toggle");
      return false;
    }

    const nodes = this._getSidebarElements(win);
    if (!nodes) {
      return false;
    }
    const { chromeDoc, box, splitter } = nodes;

    this.ensureBrowserIsAppended(chromeDoc, box);
    this._showSidebarElements(box, splitter);
    this._updateAskButtonChecked(win, true);

    // Dispatch event to notify tab state manager that sidebar was toggled
    win.dispatchEvent(
      new win.CustomEvent("ai-window:sidebar-toggle", {
        detail: {
          tab: win.gBrowser?.selectedTab,
          isOpen: true,
          source: "toggle",
        },
      })
    );

    return true;
  },

  /**
   * Restores the memories icon state on the sidebar or fullpage ai-window.
   *
   * @param {Window} win
   * @param {MozTabbrowserTab} [tab] - If provided, targets the fullpage ai-window in that tab.
   */
  restoreMemoriesState(win, tab = null) {
    const aiWindowEl = tab
      ? tab.linkedBrowser?.contentDocument?.querySelector("ai-window:defined")
      : this._getSidebarAiWindow(win);
    if (aiWindowEl) {
      aiWindowEl.syncSmartbarMemoriesStateFromConversation();
    }
  },

  /**
   * Update the Ask Button style based on the sidebar state.
   *
   * @param {Window} win
   * @param {boolean} sidebarIsOpen
   */
  _updateAskButtonChecked(win, sidebarIsOpen) {
    const askBtn = win.document.querySelector("#smartwindow-ask-button-inner");
    if (!askBtn) {
      return;
    }
    askBtn.setAttribute("aria-expanded", String(sidebarIsOpen));
  },

  /**
   * Moves a full-page AI Window conversation into the sidebar.
   *
   * @param {Window} win
   * @param {object} tab - The tab containing the full-page AI Window
   * @returns {XULElement|null} The sidebar browser element
   */
  async moveFullPageToSidebar(win, tab) {
    const fullPageBrowser = tab.linkedBrowser;
    if (
      !fullPageBrowser?.currentURI ||
      !AIWindow.isAIWindowContentPage(fullPageBrowser.currentURI)
    ) {
      return null;
    }

    let conversationId = null;
    try {
      const aiWindowEl =
        fullPageBrowser.contentDocument?.querySelector("ai-window");
      conversationId = aiWindowEl?.conversationId ?? null;
    } catch {
      // Content may not be accessible
    }

    let conversation = null;
    if (conversationId) {
      conversation =
        await AIWindow.chatStore.findConversationById(conversationId);
    }

    this.openSidebar(win, conversation);

    const nodes = this._getSidebarElements(win);
    return nodes ? nodes.chromeDoc.getElementById(this.BROWSER_ID) : null;
  },

  /**
   * Updates the sidebar input with the specified value.
   *
   * @param {Window} win
   * @param {string} value The new input value
   */
  updateSidebarInput(win, value) {
    if (!this.isSidebarOpen(win)) {
      return;
    }

    const aiWindowEl = this._getSidebarAiWindow(win);
    if (!aiWindowEl?.updateInput) {
      return;
    }

    aiWindowEl.updateInput(value);
  },

  /**
   * Triggers updating the starter prompts on the active ai-window
   * for the given chrome window. The target is resolved by mode:
   * sidebar uses the chrome window's sidebar ai-window, fullpage
   * uses the ai-window inside the owning tab's browser.
   *
   * @param {Window} win
   * @param {boolean} [clear=true] Clear current starter prompts first
   * @param {"sidebar"|"fullpage"} [mode="sidebar"]
   * @param {MozTabbrowserTab|null} [tab=null] Owner tab for fullpage mode
   */
  updateStarterPrompts(win, clear = true, mode = "sidebar", tab = null) {
    const aiWindow = this._getActiveAiWindow(win, mode, tab);
    if (!aiWindow) {
      return;
    }

    aiWindow.loadStarterPrompts(clear, win.gBrowser.selectedTab);
  },

  /**
   * Gets the sidebar instance of the ai-window component
   *
   * @param {Window} win
   *
   * @private
   */
  _getSidebarAiWindow(win) {
    if (!this.isSidebarOpen(win)) {
      return null;
    }

    const aiWindowBrowser = win.document.getElementById(this.BROWSER_ID);
    return aiWindowBrowser?.contentDocument?.querySelector("ai-window:defined");
  },

  /**
   * Resolves the ai-window instance relevant to a dispatched event,
   * routed by mode. Sidebar and fullpage are mutually exclusive for
   * a given tab — an AIWINDOW_URL tab always closes the sidebar.
   *
   * @param {Window} win
   * @param {"sidebar"|"fullpage"} mode
   * @param {MozTabbrowserTab|null} tab Owner tab for fullpage mode
   *
   * @private
   */
  _getActiveAiWindow(win, mode, tab) {
    if (mode === "sidebar") {
      return this._getSidebarAiWindow(win);
    }

    if (mode === "fullpage" && tab) {
      return tab.linkedBrowser.contentDocument.querySelector(
        "ai-window:defined"
      );
    }

    return null;
  },

  _getFadeTarget(win) {
    const tabPanels = win?.document?.getElementById("tabbrowser-tabpanels");
    return tabPanels?.selectedPanel ?? null;
  },

  _prefersReducedMotion(win) {
    return !!win?.matchMedia?.("(prefers-reduced-motion: reduce)")?.matches;
  },

  _fadeToOpacity(el, win, { to }) {
    return new Promise(resolve => {
      const onEnd = event => {
        if (event.propertyName !== "opacity") {
          return;
        }
        el.removeEventListener("transitionend", onEnd);
        win.clearTimeout(timer);
        resolve();
      };

      const timer = win.setTimeout(() => {
        el.removeEventListener("transitionend", onEnd);
        resolve();
      }, this.TAB_FADE_TIMEOUT_MS);

      el.addEventListener("transitionend", onEnd);

      el.style.transition = `opacity ${this.TAB_FADE_MS}ms ease`;
      el.style.opacity = String(to);
    });
  },

  async _runTabPanelsFade(win) {
    const target = this._getFadeTarget(win);
    if (!win || !target) {
      return;
    }
    if (this._prefersReducedMotion(win)) {
      // TODO - find alternate approach here
      // https://bugzilla.mozilla.org/show_bug.cgi?id=2024055
      return;
    }

    const prevTransition = target.style.transition;
    const prevOpacity = target.style.opacity;

    try {
      await this._fadeToOpacity(target, win, { to: "0.25" });
      target.getBoundingClientRect(); // layout flush for reliable fade-in
      await this._fadeToOpacity(target, win, { to: "1" });
    } finally {
      target.style.transition = prevTransition;
      target.style.opacity = prevOpacity;
    }
  },

  /**
   * Handle citation link click of a URL that the user is currently on
   *
   * @param {Window} win
   */
  handleSameLinkClick(win) {
    if (!win || gFadingWindows.has(win)) {
      return;
    }
    gFadingWindows.add(win);

    this._runTabPanelsFade(win).finally(() => {
      gFadingWindows.delete(win);
    });
  },
};
