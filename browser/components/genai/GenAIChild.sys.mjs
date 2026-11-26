/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "shortcutsDelay",
  "browser.ml.chat.shortcuts.longPress"
);

// Events to register after shortcuts are shown
const HIDE_EVENTS = ["pagehide", "resize", "scroll"];

/**
 * JSWindowActor to detect content page events to send GenAI related data.
 */
export class GenAIChild extends JSWindowActorChild {
  mouseUpTimeout = null;
  downSelection = null;
  downTimeStamp = 0;
  debounceDelay = 200;
  pendingHide = false;
  #compositionActive = false;

  /**
   * A flag that gets set when this actor is destroyed.
   */
  #isDestroyed = false;

  registerHideEvents() {
    this.document.addEventListener("selectionchange", this);
    this.document.addEventListener("compositionstart", this);
    this.document.addEventListener("compositionend", this);
    HIDE_EVENTS.forEach(ev =>
      this.contentWindow.addEventListener(ev, this, true)
    );
    this.pendingHide = true;
  }

  removeHideEvents() {
    this.document.removeEventListener("selectionchange", this);
    this.document.removeEventListener("compositionstart", this);
    this.document.removeEventListener("compositionend", this);
    HIDE_EVENTS.forEach(ev =>
      this.contentWindow?.removeEventListener(ev, this, true)
    );
    this.pendingHide = false;
    this.#compositionActive = false;
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
        sendHide();
        break;
      case "mouseup": {
        // Only handle plain clicks
        if (
          event.button ||
          event.altKey ||
          event.ctrlKey ||
          event.metaKey ||
          event.shiftKey
        ) {
          return;
        }

        // Clear any previously scheduled mouseup actions
        if (this.mouseUpTimeout) {
          this.contentWindow.clearTimeout(this.mouseUpTimeout);
        }

        const { screenX, screenY } = event;

        this.mouseUpTimeout = this.contentWindow.setTimeout(() => {
          if (this.#isDestroyed) {
            return;
          }

          const selectionInfo = this.getSelectionInfo();
          const delay = event.timeStamp - this.downTimeStamp;

          // Only send a message if there's a new selection or a long press
          if (
            (selectionInfo.selection &&
              selectionInfo.selection !== this.downSelection) ||
            delay > lazy.shortcutsDelay
          ) {
            this.sendAsyncMessage("GenAI:ShowShortcuts", {
              ...selectionInfo,
              contentType: "selection",
              delay,
              screenXDevPx: screenX * this.contentWindow.devicePixelRatio,
              screenYDevPx: screenY * this.contentWindow.devicePixelRatio,
            });
            this.registerHideEvents();
          }

          // Clear the timeout reference after execution
          this.mouseUpTimeout = null;
        }, this.debounceDelay);

        break;
      }
      case "compositionstart":
        this.#compositionActive = true;
        break;
      case "compositionend":
        this.#compositionActive = false;
        break;
      case "selectionchange":
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
   * Provide the selected text and input type.
   *
   * @returns {object} selection info
   */
  getSelectionInfo() {
    // Handle regular selection outside of inputs
    const { activeElement } = this.document;
    const selection = this.contentWindow.getSelection()?.toString().trim();
    if (selection) {
      return {
        inputType: activeElement.closest("[contenteditable]")
          ? "contenteditable"
          : "",
        selection,
      };
    }

    // Selection within input elements
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
  }
}
