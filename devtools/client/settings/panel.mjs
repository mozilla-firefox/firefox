/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SettingChangedEvent } from "./events.mjs";
import { callbacks } from "./inventory.mjs";

/**
 * @import { Toolbox } from "./types/toolbox.d.ts"
 */

// eslint-disable-next-line no-shadow
const { require } = ChromeUtils.importESModule(
  "resource://devtools/shared/loader/Loader.sys.mjs"
);
/** @type {typeof import("../framework/devtools.js")} */
const {
  gDevTools,
} = require("resource://devtools/client/framework/devtools.js");

/** The redesigned devtools settings panel. */
export class SettingsPanel {
  #abortController = new AbortController();

  /**
   * @param {Window} iframeWindow
   * @param {Toolbox} toolbox
   */
  constructor(iframeWindow, toolbox) {
    /** @type {Toolbox | null} */
    this.toolbox = toolbox;
    this.element = iframeWindow.document?.querySelector("devtools-settings");
  }

  /** @returns {this} */
  open() {
    this.element?.setToolbox(this.toolbox);
    const { signal } = this.#abortController;
    this.element?.addEventListener(
      SettingChangedEvent.eventName,
      this.#onSettingChanged,
      { signal }
    );
    gDevTools.on("tool-registered", this.#refresh, { signal });
    gDevTools.on("tool-unregistered", this.#refresh, { signal });
    this.toolbox?.on("webextension-registered", this.#refresh, { signal });
    this.toolbox?.on("webextension-unregistered", this.#refresh, { signal });
    return this;
  }

  #refresh = () => {
    this.element?.requestUpdate();
  };

  /** @param {SettingChangedEvent} event */
  #onSettingChanged = event => {
    if (!isCallbackName(event.action)) {
      throw new Error(`Unknown settings callback "${event.action}".`);
    }
    if (!this.toolbox) {
      // the panel might have been destroyed already
      return;
    }
    callbacks[event.action]({ toolbox: this.toolbox, event });
  };

  destroy() {
    this.#abortController.abort();
    this.element?.setToolbox(null);
    this.element?.remove();
    this.element = null;
    this.toolbox = null;
  }
}

/**
 * @param {string} action
 * @returns {action is keyof typeof callbacks}
 */
function isCallbackName(action) {
  return Object.hasOwn(callbacks, action);
}
