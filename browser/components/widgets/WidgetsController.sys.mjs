/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  GENERATION_ERRORS:
    "moz-src:///browser/components/widgets/WidgetGenerator.sys.mjs",
  WidgetCreator: "moz-src:///browser/components/widgets/WidgetCreator.sys.mjs",
  WidgetCreatorError:
    "moz-src:///browser/components/widgets/WidgetCreator.sys.mjs",
  WidgetGeneratorError:
    "moz-src:///browser/components/widgets/WidgetGenerator.sys.mjs",
});

/**
 * The prompt and error code of a failed widget creation request.
 *
 * @fires CreationFailure#update
 *  When set() or clear() is called.
 */
class CreationFailure extends EventTarget {
  /** @type {?{prompt: string, error: string}} */
  value = null;

  /**
   * @param {?{prompt: string, error: string}} value - The failed request's
   *   prompt and error code, or null.
   */
  set(value) {
    this.value = value;
    this.dispatchEvent(new Event("update"));
  }

  clear() {
    this.set(null);
  }
}

/**
 * Keeps a host element in sync with WidgetCreator, re-rendering it whenever a
 * widget creation in any window starts or settles.
 */
export class WidgetsController {
  /** @type {CreationFailure} */
  static lastFailure = new CreationFailure();

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
   * @type {?string} The active request's prompt, or null when idle.
   */
  get prompt() {
    return lazy.WidgetCreator.prompt;
  }

  /**
   * @type {?{prompt: string, error: string}} The last failed request's prompt
   *   and error code, or null.
   */
  get failed() {
    return WidgetsController.lastFailure.value;
  }

  /**
   * Generates a widget and installs it.
   *
   * @param {string} prompt - The user's description of the widget.
   */
  async create(prompt) {
    WidgetsController.lastFailure.clear();
    try {
      await lazy.WidgetCreator.create({ prompt });
    } catch (error) {
      if (error.name == "AbortError") {
        return;
      }
      console.error(error);
      WidgetsController.lastFailure.set({
        prompt,
        error:
          error instanceof lazy.WidgetGeneratorError ||
          error instanceof lazy.WidgetCreatorError
            ? error.code
            : lazy.GENERATION_ERRORS.SERVICE,
      });
    }
  }

  /**
   * Cancels generation even if another window started it. Once installation
   * starts, cancel() has no effect.
   */
  cancel() {
    lazy.WidgetCreator.cancel();
  }

  /**
   * Clears lastFailure.
   */
  dismiss() {
    WidgetsController.lastFailure.clear();
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
    WidgetsController.lastFailure.addEventListener(
      "update",
      () => this.host.requestUpdate(),
      { signal }
    );
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
