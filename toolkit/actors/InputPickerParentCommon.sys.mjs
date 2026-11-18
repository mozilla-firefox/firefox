/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const DEBUG = false;
function debug(aStr) {
  if (DEBUG) {
    dump("-*- InputPickerParent: " + aStr + "\n");
  }
}

/**
 * @typedef {object} InputPickerCallbacks
 * @property {(detail: object) => void} onValueChanged Called when the user
 *            changes the value in the picker
 * @property {() => void} onValueCleared Called when the user clears the value
 * @property {() => void} onClosed Called when the picker closes by itself
 */

/*
 * InputPickerParentCommon receives message from content side (input box) and
 * is reposible for opening, closing and updating the picker. Similarly,
 * InputPickerParentCommon listens for picker's events and notifies the content
 * side (input box) about them.
 */
export class InputPickerParentCommon extends JSWindowActorParent {
  #namespace;
  /** @type {boolean} */
  #createXULPanel;
  #picker;
  /** @type {Element | undefined} */
  #oldFocus;

  /**
   * @param {string} aNamespace Affects the input panel id, mostly to prevent
   *                            accidental mis-pairing of wrong panel and actor.
   * @param {boolean} aCreateXULPanel Whether to create a XUL panel for the
   *                                  picker.
   */
  constructor(aNamespace, aCreateXULPanel = true) {
    super();
    this.#namespace = aNamespace;
    this.#createXULPanel = aCreateXULPanel;
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

  /**
   * A picker creator function called when showing a picker
   *
   * @param {XULElement | undefined} _panel A panel element, unless the actor was
   *        created without one
   * @returns A picker object, with `openPicker(type, rect, detail,
   *          isDarkBackground, callbacks)` and `closePicker()` methods, which
   *          reports through the given {@link InputPickerCallbacks}
   */
  createPickerImpl(_panel) {
    throw new Error("Not implemented");
  }

  // Get picker from browser and show it anchored to the input box.
  showPicker(aData) {
    const { rect, type, detail, isDarkBackground } = aData;
    debug("Opening picker with details: " + JSON.stringify(detail));
    if (!this.browsingContext.canOpenModalPicker) {
      debug("Not allowed to open picker");
      return;
    }

    this.#cleanupPicker();
    let window = this.browsingContext.top.topChromeWindow;
    let doc = window.document;
    let panel;
    if (this.#createXULPanel) {
      const id = `${this.#namespace}Panel`;
      panel = doc.getElementById(id);
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
    }

    this.#oldFocus = doc.activeElement;
    const picker = this.createPickerImpl(panel);
    this.#picker = picker;
    picker.openPicker(type, rect, detail, isDarkBackground, {
      onValueChanged: value => this.#pickerValueChanged(picker, value),
      onValueCleared: () => this.#pickerValueChanged(picker, null),
      onClosed: () => this.#pickerClosed(picker),
    });
    if (panel) {
      panel.addEventListener(
        "popuphidden",
        () => {
          this.#pickerClosed(picker);
        },
        { once: true }
      );
    }
  }

  #pickerValueChanged(picker, value) {
    if (picker === this.#picker) {
      this.sendAsyncMessage("InputPicker:ValueChanged", value);
    }
  }

  #pickerClosed(picker) {
    if (picker === this.#picker) {
      this.sendAsyncMessage(`InputPicker:Closed`, {});
      this.close();
    }
  }

  #cleanupPicker() {
    if (!this.#picker) {
      return;
    }
    this.#picker.closePicker();
    this.#picker = null;
  }

  // Close the picker and do some cleanup.
  close() {
    this.#cleanupPicker();
    // Restore focus to where it was before the picker opened.
    this.#oldFocus?.focus();
    this.#oldFocus = null;
  }
}
