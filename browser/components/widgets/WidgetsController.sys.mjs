/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  WidgetCreator: "moz-src:///browser/components/widgets/WidgetCreator.sys.mjs",
});

/**
 * Keeps a host element in sync with WidgetCreator, re-rendering it whenever a
 * widget creation in any window starts or settles.
 */
export class WidgetsController {
  /**
   * Aborted on disconnect to remove the event listeners and stop pending
   * callbacks from updating the host.
   *
   * @type {?AbortController}
   */
  #listeners = null;

  /**
   * @param {ReactiveControllerHost} host - the element to update
   */
  constructor(host) {
    this.host = host;
    host.addController(this);
  }

  /**
   * @type {boolean} True while a widget is being generated or installed.
   */
  get generating() {
    return !!lazy.WidgetCreator.pending;
  }

  /**
   * Generates a widget and installs it.
   *
   * @param {string} prompt - The user's description of the widget.
   */
  async create(prompt) {
    try {
      await lazy.WidgetCreator.create({ prompt });
    } catch (error) {
      if (error.name != "AbortError") {
        console.error(error);
      }
    }
  }

  handleEvent(event) {
    this.#watch(event.detail.pending);
  }

  /**
   * Updates the connected host when the request starts and when it settles.
   *
   * @param {Promise<GeneratedWidgetAddonWrapper>} pending - The promise
   *   returned by WidgetCreator.create().
   */
  #watch(pending) {
    let { signal } = this.#listeners;
    let update = () => {
      if (signal.aborted) {
        return;
      }
      this.host.requestUpdate();
    };
    update();
    pending.then(update, update);
  }

  hostConnected() {
    this.#listeners = new AbortController();
    let { signal } = this.#listeners;
    lazy.WidgetCreator.addEventListener("WidgetCreator:Creating", this, {
      signal,
    });
    this.host.documentGlobal.addEventListener(
      "unload",
      () => this.hostDisconnected(),
      { signal }
    );
    let { pending } = lazy.WidgetCreator;
    if (pending) {
      this.#watch(pending);
    }
  }

  hostDisconnected() {
    this.#listeners?.abort();
    this.#listeners = null;
  }
}
