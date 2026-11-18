/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export class InputPickerPanelCommon {
  #element;
  #filename;
  #pickerState;
  #type;
  #detail;
  /** @type {AbortController} */
  #abortController;

  /**
   * @param {XULElement} element
   * @param {string} filename
   */
  constructor(element, filename) {
    this.#element = element;
    this.#filename = filename;
  }

  get #popupFrame() {
    const id = `${this.#element.id}PopupFrame`;
    let frame = this.#element.ownerDocument.getElementById(id);
    if (!frame) {
      frame = this.#element.ownerDocument.createXULElement("iframe");
      frame.id = id;
      this.#element.appendChild(frame);
    }
    return frame;
  }

  openPicker(type, rect, detail, isDarkBackground) {
    const impl = this.openPickerImpl(type);
    this.#pickerState = {};
    // TODO: Resize picker according to content zoom level
    this.#element.style.fontSize = "10px";
    // The color scheme is inherited by the popup frame's document, so the
    // picker's system colors match the input's.
    this.#element.style.colorScheme = isDarkBackground ? "dark" : "light";
    this.#type = impl.type;
    this.#detail = detail;
    this.#abortController = new AbortController();
    this.#popupFrame.addEventListener("load", this, {
      capture: true,
      signal: this.#abortController.signal,
    });
    this.#popupFrame.setAttribute("src", this.#filename);
    this.#popupFrame.style.width = impl.width;
    this.#popupFrame.style.height = impl.height;
    this.#element.openPopupAtScreenRect(
      "after_start",
      rect.left,
      rect.top,
      rect.width,
      rect.height,
      false,
      false
    );
  }

  /**
   * @typedef {object} OpenPickerInfo
   * @property {string} type The picker type
   * @property {string} width The picker width in CSS value
   * @property {string} height The picker height in CSS value
   *
   * Picker window initialization function called when opening the picker
   *
   * @param {string} _type The input element type
   * @returns {OpenPickerInfo}
   */
  openPickerImpl(_type) {
    throw new Error("Not implemented");
  }

  closePicker(clear) {
    if (clear) {
      this.#element.dispatchEvent(new CustomEvent(`InputPickerValueCleared`));
    }
    this.#pickerState = {};
    this.#type = undefined;
    this.#abortController.abort();
    this.#popupFrame.setAttribute("src", "");
    this.#element.hidePopup();
  }

  initPicker(detail) {
    const implDetail = this.initPickerImpl(this.#type, detail);
    this.postMessageToPicker({
      name: "PickerInit",
      detail: implDetail,
    });
  }

  /**
   * Popup frame initialization function called when the picker window is loaded
   *
   * @param {string} _type The picker type
   * @param {object} _detail The argument from the child actor's openPickerImpl
   * @returns An argument object to pass to the popup frame
   */
  initPickerImpl(_type, _detail) {
    throw new Error("Not implemented");
  }

  sendPickerValueChanged() {
    let detail = this.sendPickerValueChangedImpl(this.#type, this.#pickerState);
    this.#element.dispatchEvent(
      new CustomEvent(`InputPickerValueChanged`, {
        detail,
      })
    );
  }

  /**
   * Input element state updater function called when the picker value is changed
   *
   * @param {string} _type
   * @param {object} _pickerState
   */
  sendPickerValueChangedImpl(_type, _pickerState) {
    throw new Error("Not implemented");
  }

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "load": {
        this.initPicker(this.#detail);
        this.#popupFrame.contentWindow.addEventListener("message", this, {
          signal: this.#abortController.signal,
        });
        break;
      }
      case "message": {
        this.handleMessage(aEvent);
        break;
      }
    }
  }

  handleMessage(aEvent) {
    if (!this.#popupFrame.contentDocument.nodePrincipal.isSystemPrincipal) {
      return;
    }

    switch (aEvent.data.name) {
      case "PickerPopupChanged": {
        this.#pickerState = aEvent.data.detail;
        this.sendPickerValueChanged();
        break;
      }
      case "ClosePopup": {
        this.closePicker(aEvent.data.detail);
        break;
      }
    }
  }

  postMessageToPicker(data) {
    if (this.#popupFrame.contentDocument.nodePrincipal.isSystemPrincipal) {
      this.#popupFrame.contentWindow.postMessage(data, "*");
    }
  }
}
