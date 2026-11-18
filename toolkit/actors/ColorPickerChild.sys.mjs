/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { InputPickerChildCommon } from "./InputPickerChildCommon.sys.mjs";

export class ColorPickerChild extends InputPickerChildCommon {
  initialValue = null;

  constructor() {
    super("ColorPicker");
  }

  /**
   * Cleanup function called when picker is closed.
   *
   * @param {HTMLInputElement} inputElement
   */
  closeImpl(inputElement) {
    inputElement.setOpenState(false);
    if (this.initialValue !== inputElement.value) {
      // https://html.spec.whatwg.org/multipage/input.html#common-input-element-events
      // any time the user commits the change, the user agent must queue an element
      // task on the user interaction task source given the input element to set its
      // user validity to true and fire an event named change at the input element,
      // with the bubbles attribute initialized to true.
      inputElement.dispatchEvent(
        new inputElement.documentGlobal.Event("change", { bubbles: true })
      );
    }
  }

  /**
   * Element updater function called when the picker value is changed.
   *
   * @param {ReceiveMessageArgument} aMessage
   * @param {HTMLInputElement} inputElement
   */
  pickerValueChangedImpl(aMessage, inputElement) {
    if (!aMessage.data) {
      inputElement.setUserInput(this.initialValue);
      return;
    }

    const { rgb } = aMessage.data;
    inputElement.setUserInputColor({
      component1: rgb[0],
      component2: rgb[1],
      component3: rgb[2],
      alpha: rgb[3],
    });
  }

  /**
   * Picker initialization function called when opening the picker
   *
   * @param {HTMLInputElement} inputElement
   * @returns An argument object to pass to the picker panel, or undefined to stop.
   */
  openPickerImpl(inputElement) {
    this.initialValue = inputElement.value;
    return { value: inputElement.getColor() };
  }
}
