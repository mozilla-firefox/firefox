/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  CommandsFactory,
} = require("resource://devtools/shared/commands/commands-factory.js");

loader.lazyRequireGetter(
  this,
  "Tools",
  "resource://devtools/client/definitions.js",
  true
);
loader.lazyRequireGetter(
  this,
  "l10n",
  "resource://devtools/shared/webconsole/l10n.js"
);
loader.lazyRequireGetter(
  this,
  "BrowserConsole",
  "resource://devtools/client/webconsole/browser-console.js"
);

const BC_WINDOW_FEATURES =
  "chrome,titlebar,toolbar,centerscreen,resizable,dialog=no";

class BrowserConsoleManager {
  #browserConsole = null;
  #browserConsoleInitializing = null;
  #browserConsoleSessionState = false;

  storeBrowserConsoleSessionState() {
    this.#browserConsoleSessionState = !!this.getBrowserConsole();
  }

  getBrowserConsoleSessionState() {
    return this.#browserConsoleSessionState;
  }

  /**
   * Open a Browser Console for the current commands context.
   *
   * @param nsIDOMWindow iframeWindow
   *        The window where the browser console UI is already loaded.
   * @return object
   *         A promise object for the opening of the new BrowserConsole instance.
   */
  async openBrowserConsole(win) {
    const hud = new BrowserConsole(this.commands, win, win);
    this.#browserConsole = hud;
    await hud.init();
    return hud;
  }

  /**
   * Close the opened Browser Console
   */
  async closeBrowserConsole() {
    if (!this.#browserConsole) {
      return;
    }

    // Ensure destroying the commands,
    // even if the console throws during cleanup.
    try {
      await this.#browserConsole.destroy();
    } catch (e) {
      console.error(e);
    }
    this.#browserConsole = null;

    await this.commands.destroy();
    this.commands = null;
  }

  /**
   * Toggle the Browser Console.
   */
  async toggleBrowserConsole() {
    if (this.#browserConsole) {
      return this.closeBrowserConsole();
    }

    if (this.#browserConsoleInitializing) {
      return this.#browserConsoleInitializing;
    }

    // Temporarily cache the async startup sequence so that if toggleBrowserConsole
    // gets called again we can return this console instead of opening another one.
    this.#browserConsoleInitializing = (async () => {
      this.commands = await CommandsFactory.forBrowserConsole();
      const win = await this.openWindow();
      const browserConsole = await this.openBrowserConsole(win);
      return browserConsole;
    })();

    try {
      const browserConsole = await this.#browserConsoleInitializing;
      this.#browserConsoleInitializing = null;
      return browserConsole;
    } catch (e) {
      // Ensure always clearing this field, even in case of exception,
      // which may happen when closing during initialization.
      this.#browserConsoleInitializing = null;
      throw e;
    }
  }

  async openWindow() {
    const win = Services.ww.openWindow(
      null,
      Tools.webConsole.url,
      "_blank",
      BC_WINDOW_FEATURES,
      null
    );

    await new Promise(resolve => {
      win.addEventListener("DOMContentLoaded", resolve, { once: true });
    });

    // It's important to declare the unload *after* the initial "DOMContentLoaded",
    // otherwise, since the window is navigated to Tools.webConsole.url, an unload event
    // is fired.
    win.addEventListener("unload", this.closeBrowserConsole.bind(this), {
      once: true,
    });

    this.updateWindowTitle(win);
    return win;
  }

  /**
   * Opens or focuses the Browser Console.
   */
  openBrowserConsoleOrFocus() {
    const hud = this.getBrowserConsole();
    if (hud) {
      hud.iframeWindow.focus();
      return Promise.resolve(hud);
    }

    return this.toggleBrowserConsole();
  }

  /**
   * Get the Browser Console instance, if open.
   *
   * @return object|null
   *         A BrowserConsole instance or null if the Browser Console is not
   *         open.
   */
  getBrowserConsole() {
    return this.#browserConsole;
  }

  /**
   * Set the title of the Browser Console window.
   *
   * @param {Window} win: The BrowserConsole window
   */
  updateWindowTitle(win) {
    let title;
    const mode = Services.prefs.getCharPref(
      "devtools.browsertoolbox.scope",
      null
    );
    if (mode == "everything") {
      title = l10n.getStr("browserconsole-multiprocess-title");
    } else if (mode == "parent-process") {
      title = l10n.getStr("browserconsole-parent-process-title");
    } else {
      throw new Error("Unsupported mode: " + mode);
    }

    win.document.title = title;
  }
}

exports.BrowserConsoleManager = new BrowserConsoleManager();
