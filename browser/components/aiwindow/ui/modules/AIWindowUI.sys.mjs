/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import {
  AIWINDOW_URL,
  AIWindow,
} from "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AutoTabGrouping:
    "moz-src:///browser/components/aiwindow/ui/modules/AutoTabGrouping.sys.mjs",
  MonitorPanel:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorPanel.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
});

const gFadingWindows = new WeakSet();
const gSidebarAnimations = new WeakMap();
const gSidebarWidthHandlers = new WeakMap();

/**
 * @typedef {import("../components/ai-window/ai-window.mjs").SmartbarInputState} SmartbarInputState
 */

export const AIWindowUI = {
  BOX_ID: "ai-window-box",
  SPLITTER_ID: "ai-window-splitter",
  BROWSER_ID: "ai-window-browser",
  STACK_CLASS: "ai-window-browser-stack",
  AI_WINDOW_ELEMENT_TIMEOUT: 1500,
  TAB_FADE_MS: 200,
  TAB_FADE_TIMEOUT_MS: 200 * 2 + 50,
  SIDEBAR_ANIMATION_MS: 200,

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
   * Sets a max width for the draggable sidebar.
   *
   * @param {Window} win
   */
  updateSidebarMaxWidth(win) {
    const nodes = this._getSidebarElements(win);
    if (!nodes) {
      return;
    }
    const maxWidthRatio = parseFloat(
      win
        .getComputedStyle(win.document.documentElement)
        .getPropertyValue("--ai-window-sidebar-max-width-ratio")
    );
    nodes.box.style.setProperty(
      "--ai-window-sidebar-max-width",
      `${Math.round(win.innerWidth * maxWidthRatio)}px`
    );

    if (!gSidebarWidthHandlers.has(win)) {
      const sidebarResizeHandler = () => this.updateSidebarMaxWidth(win);
      gSidebarWidthHandlers.set(win, sidebarResizeHandler);
      win.addEventListener("resize", sidebarResizeHandler);
    }
  },

  /**
   * Stop tracking the sidebar width for a window.
   *
   * @param {Window} win
   */
  _removeSidebarWidthHandler(win) {
    const handler = gSidebarWidthHandlers.get(win);
    if (handler) {
      win.removeEventListener("resize", handler);
      gSidebarWidthHandlers.delete(win);
    }
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
    // While closing, the box stays uncollapsed until the slide finishes, so
    // rely on the intended state recorded by _setSidebarCollapsed.
    return nodes.box._aiWindowOpen ?? !nodes.box.collapsed;
  },

  /**
   * Open or close the sidebar, animating the slide when possible. The box slides
   * with a transform and the content area's apparent width with a clip-path, both
   * compositor-driven, so the content area isn't reflowed on every frame; the real
   * layout flips once, when the slide finishes. See the "Animating the content area"
   * performance best practice.
   *
   * The slide is reserved for explicit user toggles (the Ask/Close button, which
   * animate); every other path (tab switch, session restore, mode changes) passes
   * `animate: false` so the sidebar doesn't slide on navigation.
   *
   * @param {Window} win
   * @param {Element} box
   * @param {Element} splitter
   * @param {boolean} collapse
   * @param {object} [options]
   * @param {boolean} [options.animate=true] Whether to slide instead of committing instantly.
   */
  _setSidebarCollapsed(win, box, splitter, collapse, { animate = true } = {}) {
    box._aiWindowOpen = !collapse;

    // Give the content area its minimum width while the sidebar is open.
    win.document
      .getElementById("tabbrowser-tabbox")
      .toggleAttribute("ai-window-open", !collapse);

    if (!collapse) {
      this.updateSidebarMaxWidth(win);
    } else {
      this._removeSidebarWidthHandler(win);
    }

    const reduceMotion = win.matchMedia(
      "(prefers-reduced-motion: reduce)"
    ).matches;
    if (!animate || reduceMotion) {
      this._cancelSidebarAnimation(box);
      this._commitSidebarCollapsed(box, splitter, collapse);
      return;
    }

    this._animateSidebarToggle(win, box, splitter, collapse);
  },

  _commitSidebarCollapsed(box, splitter, collapse) {
    this._clearSidebarAnimationStyles(box);
    box.collapsed = collapse;
    splitter.collapsed = collapse;
    if (!collapse) {
      box.parentElement.collapsed = false;
    }
  },

  _clearSidebarAnimationStyles(box) {
    box.style.position =
      box.style.top =
      box.style.bottom =
      box.style.left =
      box.style.right =
      box.style.width =
        "";
    box.parentElement.style.overflow = "";
  },

  _cancelSidebarAnimation(box) {
    const animations = gSidebarAnimations.get(box);
    if (animations) {
      gSidebarAnimations.delete(box);
      animations.forEach(animation => animation.cancel());
    }
  },

  _animateSidebarToggle(win, box, splitter, collapse) {
    const browserEl = box.parentElement;
    const tabbox = win.document.getElementById("tabbrowser-tabbox");

    this._cancelSidebarAnimation(box);
    this._clearSidebarAnimationStyles(box);

    box.collapsed = false;
    splitter.collapsed = false;
    browserEl.collapsed = false;

    const browserStyle = win.getComputedStyle(browserEl);
    const boxRect = box.getBoundingClientRect();
    const browserRect = browserEl.getBoundingClientRect();
    const tabboxRect = tabbox.getBoundingClientRect();
    const onRight = boxRect.left >= tabboxRect.right;
    const paddingLeft = parseFloat(browserStyle.paddingLeft);
    const paddingRight = parseFloat(browserStyle.paddingRight);

    // Distance from the box's outer edge to #browser's matching edge.
    const edgeGap = onRight
      ? browserRect.right - boxRect.right
      : boxRect.left - browserRect.left;

    const clipAmount = onRight
      ? browserRect.right - paddingRight - tabboxRect.right
      : tabboxRect.left - (browserRect.left + paddingLeft);

    if (boxRect.width <= 0 || clipAmount <= 0) {
      this._commitSidebarCollapsed(box, splitter, collapse);
      return;
    }

    // Take the box out of flow, pinned where it sits, so the content area can
    // fill the full width regardless of Nova vs. non-Nova space calculations. The
    // splitter stays hidden until the slide settles, and #browser is clipped so
    // the box can slide past its edge.
    box.style.position = "absolute";
    box.style.top = `${boxRect.top - browserRect.top}px`;
    box.style.bottom = `${browserRect.bottom - boxRect.bottom}px`;
    box.style.width = `${boxRect.width}px`;
    box.style[onRight ? "right" : "left"] = `${edgeGap}px`;
    splitter.collapsed = true;
    browserEl.style.overflow = "clip";

    const slide = `${(onRight ? 1 : -1) * (boxRect.width + edgeGap)}px 0 0`;
    const clipped = onRight
      ? `inset(0 ${clipAmount}px 0 0)`
      : `inset(0 0 0 ${clipAmount}px)`;
    const full = "inset(0 0 0 0)";

    const options = {
      duration: this.SIDEBAR_ANIMATION_MS,
      easing: "ease-in-out",
    };
    const boxFrames = collapse
      ? [{ translate: "0" }, { translate: slide }]
      : [{ translate: slide }, { translate: "0" }];
    const contentFrames = collapse
      ? [{ clipPath: clipped }, { clipPath: full }]
      : [{ clipPath: full }, { clipPath: clipped }];

    const animations = [
      box.animate(boxFrames, options),
      tabbox.animate(contentFrames, options),
    ];
    gSidebarAnimations.set(box, animations);

    Promise.allSettled(animations.map(animation => animation.finished)).then(
      () => {
        if (gSidebarAnimations.get(box) !== animations) {
          return;
        }
        gSidebarAnimations.delete(box);
        this._commitSidebarCollapsed(box, splitter, collapse);
      }
    );
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
   * Reopens a conversation in a tab: loads the page it was last about (or the
   * new tab page) and restores the conversation there once the browser exists.
   *
   * @param {Window} win
   * @param {ChatConversation} conversation
   * @param {string} [where="tab"] Destination, as for openTrustedLinkIn.
   */
  reopenConversationInTab(win, conversation, where = "tab") {
    const mostRecentPage = conversation.getMostRecentPageVisited();
    const url = mostRecentPage?.href ?? win.BROWSER_NEW_TAB_URL;
    lazy.URILoadingHelper.openTrustedLinkIn(win, url, where, {
      resolveOnContentBrowserCreated: async targetBrowser => {
        if (url === win.BROWSER_NEW_TAB_URL) {
          this.openInFullWindow(targetBrowser, conversation);
        } else {
          AIWindow.restoreTabConversation(targetBrowser, conversation);
          this.openSidebar(targetBrowser.documentGlobal, conversation);
        }
      },
    });
  },

  /**
   * Open the AI Window sidebar
   *
   * The slide is reserved for the Ask button, which opens via toggleSidebar; every
   * other opener (tab switch, restore, menus, mode changes) routes through here and
   * commits instantly.
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
      this._setSidebarCollapsed(win, box, splitter, false, { animate: false });
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

  async focusSidebar(win, aiBrowser = null) {
    aiBrowser ??= win.document.getElementById(this.BROWSER_ID);
    if (!aiBrowser || !this.isSidebarOpen(win)) {
      return false;
    }

    aiBrowser.focus();

    const aiWindowElement = await this.getAiWindowElement(win, aiBrowser);
    if (!aiWindowElement || !this.isSidebarOpen(win)) {
      return false;
    }

    aiWindowElement.focusSmartbar?.();
    return true;
  },

  /**
   * Close the AI Window sidebar.
   *
   * Only the Close button animates the slide; it is the sole caller that passes
   * `source === "toggle"`. Tab switches and mode changes close instantly.
   *
   * @param {Window} win
   * @param {string} source
   */
  closeSidebar(win, source = null) {
    if (!this.isSidebarOpen(win)) {
      return;
    }
    const { box, splitter } = this._getSidebarElements(win);

    this._setSidebarCollapsed(win, box, splitter, true, {
      animate: source === "toggle",
    });
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
   * Toggle the "Organize Tabs" panel anchored to its toolbar button.
   *
   * @param {Window} win
   * @param {object} [options] - Passed through to AutoTabGrouping, which uses
   *   options.source to record what asked for the panel.
   */
  toggleGroupTabsPanel(win, options) {
    lazy.AutoTabGrouping.toggleGroupTabsPanel(win, options);
  },

  /**
   * Toggle the monitor creation panel anchored to its toolbar button.
   *
   * @param {Window} win
   */
  toggleMonitorPanel(win) {
    lazy.MonitorPanel.toggleMonitorPanel(win);
  },

  /**
   * Open the monitor panel straight to the create form.
   *
   * @param {Window} win
   */
  showMonitorCreateForm(win) {
    lazy.MonitorPanel.showCreateForm(win);
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
    this._setSidebarCollapsed(win, box, splitter, false);
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

    this.focusSidebar(win);

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

    await this.openSidebar(win, conversation);

    const nodes = this._getSidebarElements(win);
    if (!nodes) {
      return null;
    }
    const aiBrowser = nodes.chromeDoc.getElementById(this.BROWSER_ID);
    await this.focusSidebar(win, aiBrowser);
    return aiBrowser;
  },

  /**
   * Updates the sidebar input with the specified state.
   *
   * @param {Window} win
   * @param {SmartbarInputState} state
   *   The structured input state to restore: plain text plus the list of
   *   inline mention chips with their text-character offsets.
   */
  updateSidebarInput(win, state) {
    if (!this.isSidebarOpen(win)) {
      return;
    }

    const aiWindowEl = this._getSidebarAiWindow(win);
    if (!aiWindowEl?.updateInput) {
      return;
    }

    aiWindowEl.updateInput(state);
  },

  /**
   * Restores the per-tab model selection on the sidebar ai-window. A null
   * modelChoiceId falls back to the global default model.
   *
   * @param {Window} win
   * @param {?string} modelChoiceId
   */
  updateSidebarModel(win, modelChoiceId) {
    if (!this.isSidebarOpen(win)) {
      return;
    }

    const aiWindowEl = this._getSidebarAiWindow(win);
    if (!aiWindowEl?.restoreModelChoiceOverride) {
      return;
    }

    aiWindowEl.restoreModelChoiceOverride(modelChoiceId);
  },

  /**
   * Restores the per-tab context chips on the sidebar ai-window.
   *
   * @param {Window} win
   * @param {ContextWebsite[]} [contextChips] - The user-added chips to restore.
   * @param {boolean} [removedImplicitContextChip] - Restored dismissal of the
   *   implicit current-tab chip.
   */
  updateSidebarContextChips(
    win,
    contextChips = [],
    removedImplicitContextChip = false
  ) {
    if (!this.isSidebarOpen(win)) {
      return;
    }

    const aiWindowEl = this._getSidebarAiWindow(win);
    if (!aiWindowEl?.restoreContextChips) {
      return;
    }

    aiWindowEl.restoreContextChips(contextChips, removedImplicitContextChip);
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
   * Sends a request to the sidebar chat, opening the sidebar on the selected
   * tab's conversation first if it is closed.
   *
   * @param {Window} win
   * @param {string} text
   * @param {object} options
   * @param {string} options.submitType - How the request was made, for
   *   telemetry.
   * @param {string} [options.aitabSource] - Where an AI Tab page was requested
   *   from, for telemetry.
   */
  async submitToSidebar(win, text, { submitType, aitabSource }) {
    if (!this.isSidebarOpen(win)) {
      await this.openSidebar(
        win,
        AIWindow.getActiveConversation(win) ?? undefined
      );
    }
    this._getSidebarAiWindow(win)?.submitChatMessage({
      text,
      submitType,
      aitabSource,
    });
  },

  /**
   * Opens the sidebar on a new chat and submits a message to it.
   *
   * @param {Window} win
   * @param {object} message - Options for the ai-window submitChatMessage.
   */
  async submitToNewSidebarChat(win, message) {
    await this.openSidebar(win);
    this._getSidebarAiWindow(win)?.submitChatMessage(message);
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
