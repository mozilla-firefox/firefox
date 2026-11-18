/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const DEBUG = false;
function debug(aStr) {
  if (DEBUG) {
    dump("-*- InputPickerParent: " + aStr + "\n");
  }
}

/*
 * InputPickerParentCommon receives message from content side (input box) and
 * is reposible for opening, closing and updating the picker. Similarly,
 * InputPickerParentCommon listens for picker's events and notifies the content
 * side (input box) about them.
 */
export class InputPickerParentCommon extends JSWindowActorParent {
  #namespace;
  #picker;
  /** @type {Element | undefined} */
  #oldFocus;
  /** @type {AbortController} */
  #abortController;

  /**
   * @param {string} namespace Affects the input panel id, mostly to prevent
   *                           accidental mis-pairing of wrong panel and actor.
   */
  constructor(namespace) {
    super();
    this.#namespace = namespace;
  }

  receiveMessage(aMessage) {
    debug("receiveMessage: " + aMessage.name);
    switch (aMessage.name) {
      case `InputPicker:Open`: {
        this.showPicker(aMessage.data);
        break;
      }
      case `InputPicker:Close`: {
        if (!this.#picker) {
          return;
        }
        this.close();
        break;
      }
      default:
        break;
    }
  }

  handleEvent(aEvent) {
    debug("handleEvent: " + aEvent.type);
    switch (aEvent.type) {
      case "InputPickerValueCleared": {
        this.sendAsyncMessage("InputPicker:ValueChanged", null);
        break;
      }
      case "InputPickerValueChanged": {
        this.sendAsyncMessage("InputPicker:ValueChanged", aEvent.detail);
        break;
      }
      case "popuphidden": {
        this.sendAsyncMessage(`InputPicker:Closed`, {});
        this.close();
        break;
      }
      default:
        break;
    }
  }

  /**
   * A panel creator function called when showing a picker
   *
   * @param {XULElement} _panel A panel element
   * @returns A panel object that manages the element
   */
  createPickerImpl(_panel) {
    throw new Error("Not implemented");
  }

  // Get picker from browser and show it anchored to the input box.
  showPicker(aData) {
    let rect = aData.rect;
    let type = aData.type;
    let detail = aData.detail;

    debug("Opening picker with details: " + JSON.stringify(detail));
    if (!this.browsingContext.canOpenModalPicker) {
      debug("Not allowed to open picker");
      return;
    }

    this.#cleanupPicker();
    let window = this.browsingContext.top.topChromeWindow;
    let doc = window.document;
    const id = `${this.#namespace}Panel`;
    let panel = doc.getElementById(id);
    if (!panel) {
      panel = doc.createXULElement("panel");
      panel.id = id;
      panel.setAttribute("type", "arrow");
      panel.setAttribute("orient", "vertical");
      panel.setAttribute("ignorekeys", "true");
      panel.setAttribute("noautofocus", "true");
      // This ensures that clicks on the anchored input box are never consumed.
      panel.setAttribute("consumeoutsideclicks", "never");
      panel.setAttribute("level", "parent");
      panel.setAttribute("tabspecific", "true");
      let container =
        doc.getElementById("mainPopupSet") ||
        doc.querySelector("popupset") ||
        doc.documentElement.appendChild(doc.createXULElement("popupset"));
      container.appendChild(panel);
    }
    this.#oldFocus = doc.activeElement;
    this.#picker = this.createPickerImpl(panel);
    this.#picker.openPicker(type, rect, detail, aData.isDarkBackground);
    this.addPickerListeners(panel);
  }

  #cleanupPicker() {
    if (!this.#picker) {
      return;
    }
    this.#picker.closePicker();
    this.#abortController.abort();
    this.#picker = null;
  }

  // Close the picker and do some cleanup.
  close() {
    this.#cleanupPicker();
    // Restore focus to where it was before the picker opened.
    this.#oldFocus?.focus();
    this.#oldFocus = null;
  }

  // Listen to picker's event.
  addPickerListeners(panel) {
    if (!this.#picker) {
      return;
    }
    this.#abortController = new AbortController();
    const { signal } = this.#abortController;
    panel.addEventListener("popuphidden", this, { signal });
    panel.addEventListener("InputPickerValueChanged", this, {
      signal,
    });
    panel.addEventListener("InputPickerValueCleared", this, {
      signal,
    });
  }
}
