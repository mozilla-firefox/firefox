/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { WindowGlobalBiDiModule } from "chrome://remote/content/webdriver-bidi/modules/WindowGlobalBiDiModule.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AnimationFramePromise: "chrome://remote/content/shared/Sync.sys.mjs",
  assertTargetInViewPort:
    "chrome://remote/content/shared/webdriver/Actions.sys.mjs",
  dom: "chrome://remote/content/shared/DOM.sys.mjs",
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  event: "chrome://remote/content/shared/webdriver/Event.sys.mjs",
});

class InputModule extends WindowGlobalBiDiModule {
  constructor(messageHandler) {
    super(messageHandler);
  }

  destroy() {}

  async #deserializeElementSharedReference(sharedReference) {
    if (typeof sharedReference?.sharedId !== "string") {
      throw new lazy.error.InvalidArgumentError(
        `Expected "element" to be a SharedReference, got: ${sharedReference}`
      );
    }

    const realm = this.messageHandler.getRealm();

    const element = this.deserialize(sharedReference, realm);
    if (!lazy.dom.isElement(element)) {
      throw new lazy.error.NoSuchElementError(
        `No element found for shared id: ${sharedReference.sharedId}`
      );
    }

    return element;
  }

  _assertInViewPort(options) {
    const { target } = options;

    return lazy.assertTargetInViewPort(target, this.messageHandler.window);
  }

  async _dispatchEvent(options) {
    const { eventName, details } = options;

    try {
      switch (eventName) {
        case "synthesizeKeyDown":
          lazy.event.sendKeyDown(details.eventData, this.messageHandler.window);
          break;
        case "synthesizeKeyUp":
          lazy.event.sendKeyUp(details.eventData, this.messageHandler.window);
          break;
        case "synthesizeMouseAtPoint":
          lazy.event.synthesizeMouseAtPoint(
            details.x,
            details.y,
            details.eventData,
            this.messageHandler.window
          );
          break;
        case "synthesizeMultiTouch":
          lazy.event.synthesizeMultiTouch(
            details.eventData,
            this.messageHandler.window
          );
          break;
        case "synthesizeWheelAtPoint":
          lazy.event.synthesizeWheelAtPoint(
            details.x,
            details.y,
            details.eventData,
            this.messageHandler.window
          );
          break;
        default:
          throw new Error(
            `${eventName} is not a supported type for dispatching`
          );
      }
    } catch (e) {
      if (e.message.includes("NS_ERROR_FAILURE")) {
        // Dispatching the event failed. Inform the RootTransport
        // to retry dispatching the event.
        throw new DOMException(
          `Failed to dispatch event "${eventName}": ${e.message}`,
          "AbortError"
        );
      }

      throw e;
    }
  }

  async _finalizeAction() {
    // Terminate the current wheel transaction if there is one. Wheel
    // transactions should not live longer than a single action chain.
    ChromeUtils.endWheelTransaction();

    // Wait for the next animation frame to make sure the page's content
    // was updated.
    await lazy.AnimationFramePromise(this.messageHandler.window);
  }

  async _getClientRects(options) {
    const { element: reference } = options;

    const element = await this.#deserializeElementSharedReference(reference);
    const rects = element.getClientRects();

    // To avoid serialization and deserialization of DOMRect and DOMRectList
    // convert to plain object and Array.
    return [...rects].map(rect => {
      const { x, y, width, height, top, right, bottom, left } = rect;
      return { x, y, width, height, top, right, bottom, left };
    });
  }

  async _getElementOrigin(options) {
    const { origin } = options;

    const reference = origin.element;
    this.#deserializeElementSharedReference(reference);

    return reference;
  }

  _getInViewCentrePoint(options) {
    const { rect } = options;

    return lazy.dom.getInViewCentrePoint(rect, this.messageHandler.window);
  }

  async setFiles(options) {
    const { element: sharedReference, files } = options;

    const element =
      await this.#deserializeElementSharedReference(sharedReference);

    if (
      !HTMLInputElement.isInstance(element) ||
      element.type !== "file" ||
      element.disabled
    ) {
      throw new lazy.error.UnableToSetFileInputError(
        `Element needs to be an <input> element with type "file" and not disabled`
      );
    }

    if (files.length > 1 && !element.hasAttribute("multiple")) {
      throw new lazy.error.UnableToSetFileInputError(
        `Element should have an attribute "multiple" set when trying to set more than 1 file`
      );
    }

    const fileObjects = [];
    for (const file of files) {
      try {
        fileObjects.push(await File.createFromFileName(file));
      } catch (e) {
        throw new lazy.error.UnsupportedOperationError(
          `Failed to add file ${file} (${e})`
        );
      }
    }

    const selectedFiles = Array.from(element.files);

    const intersection = fileObjects.filter(fileObject =>
      selectedFiles.some(
        selectedFile =>
          // Compare file fields to identify if the files are equal.
          // TODO: Bug 1883856. Add check for full path or use a different way
          // to compare files when it's available.
          selectedFile.name === fileObject.name &&
          selectedFile.size === fileObject.size &&
          selectedFile.type === fileObject.type
      )
    );

    if (
      intersection.length === selectedFiles.length &&
      selectedFiles.length === fileObjects.length
    ) {
      lazy.event.cancel(element);
    } else {
      element.mozSetFileArray(fileObjects);

      lazy.event.input(element);
      lazy.event.change(element);
    }
  }
}

export const input = InputModule;
