/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
});
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "shortcutsDelay",
  "browser.ml.chat.shortcuts.longPress"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "highlightToSearchFeatureGate",
  "browser.highlightToSearch.featureGate",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "shortcutsDebounce",
  "browser.ml.chat.shortcuts.debounce",
  200
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "featureGate",
  "browser.highlightToSearch.featureGate",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "detectPageMenus",
  "browser.highlightToSearch.detectPageMenus",
  false
);

// Events to register after shortcuts are shown
const HIDE_EVENTS = ["pagehide", "resize", "scroll"];

const DRAG_THRESHOLD_PX = 3;

// A page menu is a floating element this close to the selection, no bigger
// than this, that appeared or was shown from shortly before the mouse was
// released. Earlier ones, such as a hover card, are not for the selection.
const PAGE_MENU_NEAR_PX = 150;
const PAGE_MENU_LEAD_MS = 300;
const PAGE_MENU_MAX_WIDTH_PX = 600;
const PAGE_MENU_MAX_HEIGHT_PX = 300;
const PAGE_MENU_MAX_CANDIDATES = 100;
const PAGE_MENU_WATCH_AFTER_SHOW_MS = 1000;
const PAGE_MENU_ATTRIBUTES = ["style", "class", "hidden", "open"];

/**
 * JSWindowActor to detect content page events to send GenAI related data.
 */
export class GenAIChild extends JSWindowActorChild {
  mouseUpTimeout = null;
  downSelection = null;
  downTimeStamp = 0;
  downX = 0;
  downY = 0;
  pendingHide = false;
  dropdownOpen = false;
  inClickBurst = false;
  shownSelection = "";
  #compositionActive = false;
  #menuObserver = null;
  #menuCandidates = new Map();
  #menuWatchTimeout = null;
  #releasePoint = null;
  #releaseTime = 0;

  /**
   * A flag that gets set when this actor is destroyed.
   */
  #isDestroyed = false;

  /**
   * Start collecting elements the page adds or shows, to tell whether it shows
   * its own menu for the selection.
   */
  #watchPageMenus() {
    const root = this.document.documentElement;
    if (!lazy.featureGate || !lazy.detectPageMenus || !root) {
      return;
    }
    this.#menuObserver = new this.contentWindow.MutationObserver(records => {
      for (const record of records) {
        const nodes =
          record.type == "childList" ? record.addedNodes : [record.target];
        for (const node of nodes) {
          if (node.nodeType == Node.ELEMENT_NODE) {
            this.#menuCandidates.delete(node);
            this.#menuCandidates.set(node, Date.now());
            if (this.#menuCandidates.size > PAGE_MENU_MAX_CANDIDATES) {
              this.#menuCandidates.delete(
                this.#menuCandidates.keys().next().value
              );
            }
          }
        }
      }
      if (this.pendingHide) {
        if (this.#pageShowedMenu()) {
          this.sendAsyncMessage("GenAI:SuppressedShortcuts", {
            reason: "page_menu",
          });
          this.#stopWatchingPageMenus();
        } else {
          this.#menuCandidates.clear();
        }
      }
    });
    this.#menuObserver.observe(root, {
      childList: true,
      subtree: true,
      attributes: true,
      attributeFilter: PAGE_MENU_ATTRIBUTES,
    });
  }

  #isPlainClick(event) {
    return !(
      event.button ||
      event.altKey ||
      event.ctrlKey ||
      event.metaKey ||
      event.shiftKey
    );
  }

  #stopWatchingPageMenus() {
    this.#menuObserver?.disconnect();
    this.#menuObserver = null;
    this.#menuCandidates.clear();
    this.contentWindow?.clearTimeout(this.#menuWatchTimeout);
    this.#menuWatchTimeout = null;
  }

  /**
   * @param {object} selectionInfo from getSelectionInfo
   * @param {number} delay ms between the mouse press and release
   * @param {boolean} userSelected whether the user made the selection
   * @returns {boolean} whether the release made a new selection or was a long
   *   press
   */
  #isNewSelectionOrLongPress(selectionInfo, delay, userSelected) {
    return (
      (selectionInfo.selection &&
        selectionInfo.selection !== this.downSelection &&
        userSelected) ||
      delay > lazy.shortcutsDelay
    );
  }

  #selectionBox() {
    const selection = this.contentWindow.getSelection();
    if (selection?.rangeCount) {
      const box = selection.getRangeAt(0).getBoundingClientRect();
      if (box.width || box.height) {
        return box;
      }
    }
    const { x, y } = this.#releasePoint;
    return { left: x, right: x, top: y, bottom: y };
  }

  /**
   * @returns {boolean} whether the page has shown a floating element close to
   *   the selection from shortly before the mouse was released
   */
  #pageShowedMenu() {
    if (!this.#menuCandidates.size) {
      return false;
    }
    const selection = this.#selectionBox();
    for (const [node, time] of this.#menuCandidates) {
      if (time < this.#releaseTime - PAGE_MENU_LEAD_MS) {
        continue;
      }
      const floating = this.#floatingAncestor(node);
      if (!floating) {
        continue;
      }
      const box = floating.getBoundingClientRect();
      if (
        !box.width ||
        !box.height ||
        box.width > PAGE_MENU_MAX_WIDTH_PX ||
        box.height > PAGE_MENU_MAX_HEIGHT_PX
      ) {
        continue;
      }
      const gapX = Math.max(
        selection.left - box.right,
        box.left - selection.right,
        0
      );
      const gapY = Math.max(
        selection.top - box.bottom,
        box.top - selection.bottom,
        0
      );
      if (Math.hypot(gapX, gapY) <= PAGE_MENU_NEAR_PX) {
        return true;
      }
    }
    return false;
  }

  /**
   * @param {Element} node an element the page added or changed
   * @returns {Element|null} the node or its closest ancestor that is shown and
   *   positioned over the page
   */
  #floatingAncestor(node) {
    if (
      !node.checkVisibility({ checkOpacity: true, checkVisibilityCSS: true })
    ) {
      return null;
    }
    for (let element = node; element; element = element.parentElement) {
      const { position } = this.contentWindow.getComputedStyle(element);
      if (position == "fixed" || position == "absolute") {
        return element;
      }
    }
    return null;
  }

  registerHideEvents() {
    this.document.addEventListener("selectionchange", this);
    this.document.addEventListener("compositionstart", this);
    this.document.addEventListener("compositionend", this);
    this.document.addEventListener("keydown", this, true);
    HIDE_EVENTS.forEach(ev =>
      this.contentWindow.addEventListener(ev, this, true)
    );
    this.pendingHide = true;
  }

  removeHideEvents() {
    this.document.removeEventListener("selectionchange", this);
    this.document.removeEventListener("compositionstart", this);
    this.document.removeEventListener("compositionend", this);
    this.document.removeEventListener("keydown", this, true);
    HIDE_EVENTS.forEach(ev =>
      this.contentWindow?.removeEventListener(ev, this, true)
    );
    this.pendingHide = false;
    this.dropdownOpen = false;
    this.inClickBurst = false;
    this.#compositionActive = false;
    this.#stopWatchingPageMenus();
  }

  handleEvent(event) {
    const sendHide = () => {
      // Only remove events and send message if shortcuts are actually visible
      if (this.pendingHide) {
        this.sendAsyncMessage("GenAI:HideShortcuts", event.type);
        this.removeHideEvents();
      }
    };

    switch (event.type) {
      case "mousedown":
        this.downSelection = this.getSelectionInfo().selection;
        this.downTimeStamp = event.timeStamp;
        this.downX = event.pageX;
        this.downY = event.pageY;
        if (
          lazy.highlightToSearchFeatureGate &&
          this.pendingHide &&
          event.detail >= 2
        ) {
          // The later presses of a double or triple click are the same
          // gesture still choosing what to select.
          this.inClickBurst = true;
          break;
        }
        sendHide();
        this.#stopWatchingPageMenus();
        if (this.#isPlainClick(event)) {
          this.#watchPageMenus();
        }
        break;
      case "mouseup": {
        const endedClickBurst = this.inClickBurst;
        const hideIfStale = () => {
          if (
            endedClickBurst &&
            this.getSelectionInfo().selection !== this.shownSelection
          ) {
            sendHide();
          }
        };
        this.inClickBurst = false;

        // Only handle plain clicks
        if (!this.#isPlainClick(event)) {
          this.#stopWatchingPageMenus();
          hideIfStale();
          return;
        }

        // Clear any previously scheduled mouseup actions
        if (this.mouseUpTimeout) {
          this.contentWindow.clearTimeout(this.mouseUpTimeout);
        }

        const { screenX, screenY, clientX, clientY, pageX, pageY, detail } =
          event;
        // A page can select text itself on click, so only a drag or a double
        // or triple click counts as the user making the selection.
        const userSelected =
          detail >= 2 ||
          Math.abs(pageX - this.downX) + Math.abs(pageY - this.downY) >
            DRAG_THRESHOLD_PX;
        this.#releasePoint = { x: clientX, y: clientY };
        this.#releaseTime = Date.now();
        if (
          this.#isNewSelectionOrLongPress(
            this.getSelectionInfo(),
            event.timeStamp - this.downTimeStamp,
            userSelected
          )
        ) {
          if (!this.#menuObserver) {
            this.#watchPageMenus();
          }
        } else {
          this.#stopWatchingPageMenus();
        }

        this.mouseUpTimeout = this.contentWindow.setTimeout(() => {
          if (this.#isDestroyed) {
            return;
          }

          const selectionInfo = this.getSelectionInfo();
          const delay = event.timeStamp - this.downTimeStamp;

          // Only send a message if there's a new selection or a long press
          if (
            this.#isNewSelectionOrLongPress(selectionInfo, delay, userSelected)
          ) {
            const pageMenu = this.#pageShowedMenu();
            this.showShortcuts(
              selectionInfo,
              delay,
              screenX,
              screenY,
              pageMenu
            );
            if (pageMenu) {
              this.#stopWatchingPageMenus();
            } else {
              this.#menuWatchTimeout = this.contentWindow.setTimeout(
                () => this.#stopWatchingPageMenus(),
                PAGE_MENU_WATCH_AFTER_SHOW_MS
              );
            }
          } else {
            this.#stopWatchingPageMenus();
            hideIfStale();
          }

          // Clear the timeout reference after execution
          this.mouseUpTimeout = null;
        }, lazy.shortcutsDebounce);

        break;
      }
      case "compositionstart":
        this.#compositionActive = true;
        break;
      case "compositionend":
        this.#compositionActive = false;
        break;
      case "keydown":
        if (event.key == "Escape" && lazy.highlightToSearchFeatureGate) {
          if (this.dropdownOpen) {
            this.sendAsyncMessage("GenAI:HideDropdowns", {});
          } else {
            sendHide();
          }
        }
        break;
      case "selectionchange":
        if (this.inClickBurst) {
          break;
        }
        if (this.#compositionActive) {
          // Visually hide without calling sendHide()
          // sendHide() triggers hidePopup() which issues a focus change event
          // that breaking any active IME composition
          if (this.pendingHide) {
            this.sendAsyncMessage("GenAI:HideShortcuts", "selectionchange-ime");
            this.removeHideEvents();
          }
        } else {
          sendHide();
        }
        break;
      case "pagehide":
      case "resize":
      case "scroll":
        sendHide();
        break;
    }
  }

  /**
   * The last visible piece of text in a range.
   *
   * @param {Range} range
   * @returns {{rect: DOMRect, rtl: boolean}|null}
   */
  #getRangeEnd(range) {
    const root = range.commonAncestorContainer;
    const walker = this.document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
    const endContainer = range.endContainer;
    walker.currentNode = endContainer;
    let node =
      endContainer.nodeType == Node.TEXT_NODE
        ? endContainer
        : (walker.lastChild() ?? walker.previousNode());
    while (node) {
      const isEnd = node == range.endContainer;
      const isStart = node == range.startContainer;
      const empty =
        (isEnd && range.endOffset == 0) ||
        (isStart && range.startOffset == node.length);
      if (!empty && range.intersectsNode(node)) {
        const part = this.document.createRange();
        part.selectNodeContents(node);
        if (isEnd) {
          part.setEnd(node, range.endOffset);
        }
        if (isStart) {
          part.setStart(node, range.startOffset);
        }
        const rects = Array.from(part.getClientRects()).filter(r => r.width);
        if (rects.length) {
          const style = node.parentElement
            ? this.contentWindow.getComputedStyle(node.parentElement)
            : null;
          return { rect: rects.at(-1), rtl: style?.direction == "rtl" };
        }
      }
      node = walker.previousNode();
    }
    return null;
  }

  /**
   * Where to anchor the menu, in screen coordinates. With the feature gate on
   * it hangs from the end of the selection, as it does for a selection made
   * straight through the text, whichever way the selection was made and
   * wherever the pointer was released.
   *
   * @param {number} screenX pointer position in CSS pixels
   * @param {number} screenY pointer position in CSS pixels
   * @returns {{x: number, y: number, top?: number}} CSS pixels; `y` is the
   *   bottom of the selection and `top` its top
   */
  #getMenuAnchor(screenX, screenY) {
    const pointer = { x: screenX, y: screenY };
    if (!lazy.highlightToSearchFeatureGate) {
      return pointer;
    }

    const win = this.contentWindow;
    const selection = win.getSelection();
    if (!selection?.toString().trim() || !selection.rangeCount) {
      return pointer;
    }
    const range = selection.getRangeAt(0);
    const rect = range.getBoundingClientRect();
    if (!rect.width && !rect.height) {
      return pointer;
    }

    const end = this.#getRangeEnd(range);
    const left = win.mozInnerScreenX;
    const viewportTop = win.mozInnerScreenY;
    let x = screenX - left;
    let bottom = rect.bottom;
    if (end) {
      x = end.rtl ? end.rect.left : end.rect.right;
      bottom = end.rect.bottom;
    }
    const clamp = (value, max) => Math.min(Math.max(value, 0), max);
    return {
      x: left + clamp(x, win.innerWidth),
      y: viewportTop + clamp(bottom, win.innerHeight),
      top: viewportTop + clamp(rect.top, win.innerHeight),
    };
  }

  showShortcuts(selectionInfo, delay, screenX, screenY, pageMenu = false) {
    this.shownSelection = selectionInfo.selection;
    const dpr = this.contentWindow.devicePixelRatio;
    const anchor = this.#getMenuAnchor(screenX, screenY);
    this.sendAsyncMessage("GenAI:ShowShortcuts", {
      ...selectionInfo,
      contentType: "selection",
      delay,
      pageMenu,
      screenXDevPx: anchor.x * dpr,
      screenYDevPx: anchor.y * dpr,
      selectionTopDevPx: anchor.top === undefined ? null : anchor.top * dpr,
      // For the search action's loadSearch call, mirroring how the
      // context menu derives its own triggeringPrincipal/policyContainer.
      principal: this.document.nodePrincipal,
      policyContainer: lazy.E10SUtils.serializePolicyContainer(
        this.document.policyContainer
      ),
    });
    this.registerHideEvents();
  }

  /**
   * Provide the selected text and input type.
   *
   * @returns {object} selection info
   */
  getSelectionInfo() {
    // Handle regular selection outside of inputs
    const contentSelection = this.contentWindow.getSelection();
    const selection = contentSelection?.toString().trim();
    if (selection) {
      const anchor = contentSelection.anchorNode;
      const anchorElement =
        anchor.nodeType === Node.ELEMENT_NODE ? anchor : anchor.parentElement;
      let inputType = "";
      let host;
      if (anchorElement?.closest("[contenteditable]")) {
        inputType = "contenteditable";
        host = anchorElement.getRootNode().host?.localName;
      }
      return { inputType, host, selection };
    }

    // Selection within input elements
    const { activeElement } = this.document;
    const { selectionStart, value } = activeElement;
    if (selectionStart != null && value != null) {
      return {
        inputType: activeElement.localName,
        selection: value.slice(selectionStart, activeElement.selectionEnd),
      };
    }
    return { inputType: "", selection: "" };
  }

  /**
   * Handles incoming messages from the browser
   *
   * @param {object} message - The message object containing name
   * @param {string} message.name - The name of the message
   * @param {object} message.data - The data object of the message
   */
  async receiveMessage({ name, data }) {
    switch (name) {
      case "AutoSubmit":
        return await this.autoSubmitClick(data);
      case "DropdownState":
        this.dropdownOpen = data;
        return null;
      case "ShowSelectionMenu": {
        const selectionInfo = this.getSelectionInfo();
        if (selectionInfo.selection) {
          const dpr = this.contentWindow.devicePixelRatio;
          this.showShortcuts(
            selectionInfo,
            0,
            data.screenXDevPx / dpr,
            data.screenYDevPx / dpr
          );
        }
        return null;
      }
      default:
        return null;
    }
  }

  /**
   * Find the prompt editable element within a timeout
   * Return the element or null
   *
   * @param {Window} win - the target window
   * @param {number} [tms=1000] - time in ms
   */
  async findTextareaEl(win, tms = 1000) {
    const start = win.performance.now();
    let el;
    while (
      !(el = win.document.querySelector(
        '#prompt-textarea, [contenteditable], [role="textbox"]'
      )) &&
      win.performance.now() - start < tms
    ) {
      await new Promise(r => win.requestAnimationFrame(r));
    }
    return el;
  }

  /**
   * Automatically submit the prompt
   *
   * @param {string} promptText - the prompt to send
   */
  async autoSubmitClick({ promptText = "" } = {}) {
    const win = this.contentWindow;
    if (!win || win._autosent) {
      return;
    }

    // Ensure the DOM is ready before querying elements
    if (win.document.readyState === "loading") {
      await new Promise(r =>
        win.addEventListener("DOMContentLoaded", r, { once: true })
      );
    }

    const editable = await this.findTextareaEl(win);
    if (!editable) {
      return;
    }

    if (!editable.textContent) {
      editable.textContent = promptText;
      editable.dispatchEvent(new win.InputEvent("input", { bubbles: true }));
    }

    // Explicitly wait for the button is ready
    await new Promise(r => win.requestAnimationFrame(r));

    // Simulating click to avoid SPA router rewriting (?prompt-textarea=)
    const submitBtn =
      win.document.querySelector('button[data-testid="send-button"]') ||
      win.document.querySelector('button[aria-label="Send prompt"]') ||
      win.document.querySelector('button[aria-label="Send message"]');

    if (submitBtn) {
      submitBtn.click();
      win._autosent = true;
    }

    // Ensure clean up textarea only for chatGPT and mochitest
    if (
      win._autosent &&
      (/chatgpt\.com/i.test(win.location.host) ||
        win.location.pathname.includes("file_chat-autosubmit.html"))
    ) {
      const container = editable.parentElement;
      if (!container) {
        return;
      }

      const observer = new win.MutationObserver(() => {
        // Always refetch because ChatGPT replaces editable div
        const currentEditable = container.querySelector(
          '[contenteditable="true"]'
        );
        if (!currentEditable) {
          return;
        }

        let hasText = currentEditable.textContent?.trim().length > 0;
        if (hasText) {
          currentEditable.textContent = "";
          currentEditable.dispatchEvent(
            new win.InputEvent("input", { bubbles: true })
          );
        }
      });

      observer.observe(container, { childList: true, subtree: true });

      // Disconnect once things stabilize
      win.setTimeout(() => observer.disconnect(), 2000);
    }
  }

  didDestroy() {
    this.#isDestroyed = true;
    this.#menuObserver?.disconnect();
  }
}
