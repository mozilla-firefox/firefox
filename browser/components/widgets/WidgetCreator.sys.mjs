/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  GeneratedWidgetsProvider:
    "moz-src:///toolkit/components/widgets/GeneratedWidgetsProvider.sys.mjs",
  WidgetGenerator:
    "moz-src:///browser/components/widgets/WidgetGenerator.sys.mjs",
});

/**
 * The codes of a WidgetCreatorError.
 */
export const CREATION_ERRORS = Object.freeze({
  /** Another widget is already being created. */
  BUSY: "busy",
  /** The generated widget can't be installed. */
  INSTALL: "install",
});

/**
 * An error from WidgetCreator, with a code from CREATION_ERRORS.
 */
export class WidgetCreatorError extends Error {
  name = "WidgetCreatorError";

  /**
   * @param {string} message
   * @param {string} code - one of CREATION_ERRORS
   * @param {Error} [cause] - the error that led to this one
   */
  constructor(message, code, cause) {
    super(message, { cause });
    this.code = code;
  }
}

/**
 * Generates and installs one widget at a time.
 *
 * @fires _WidgetCreator#"WidgetCreator:Creating"
 *  When generation starts. detail: { pending }
 * @fires _WidgetCreator#"WidgetCreator:Error"
 *  When generation or installation fails for any reason besides an
 *  AbortError. detail: { error, prompt }
 */
class _WidgetCreator extends EventTarget {
  #pending = null;

  #prompt = null;

  /**
   * @type {?AbortController} The controller for the current generation request.
   */
  #abortController = null;

  /**
   * The current create() promise, or null when idle.
   *
   * @type {?Promise<GeneratedWidgetAddonWrapper>}
   */
  get pending() {
    return this.#pending;
  }

  /** @type {?string} The current widget prompt, or null when idle. */
  get prompt() {
    return this.#prompt;
  }

  /**
   * Generates a widget from the prompt and installs it.
   *
   * @param {object} options
   * @param {string} options.prompt - The user's description of the widget.
   * @param {AbortSignal} [options.signal] - Aborting it cancels the generation
   *   step.
   * @returns {Promise<GeneratedWidgetAddonWrapper>} Resolves to the installed
   *   add-on, or rejects with a BUSY WidgetCreatorError while another widget
   *   is being created.
   */
  create({ prompt, signal }) {
    if (this.#pending) {
      return Promise.reject(
        new WidgetCreatorError(
          "A widget is already being created",
          CREATION_ERRORS.BUSY
        )
      );
    }
    this.#abortController = new AbortController();
    let signals = [this.#abortController.signal];
    if (signal) {
      signals.push(signal);
    }
    this.#prompt = prompt;
    this.#pending = this.#create({
      prompt,
      signal: AbortSignal.any(signals),
    }).then(
      addon => this.#finished(addon),
      error => this.#failed(error)
    );
    this.dispatchEvent(
      new CustomEvent("WidgetCreator:Creating", {
        detail: { pending: this.#pending },
      })
    );
    return this.#pending;
  }

  /**
   * Cancels the current widget's generation step. Does nothing when no widget
   * is being generated, including while the generated widget installs.
   */
  cancel() {
    this.#abortController?.abort();
  }

  /**
   * @param {object} options
   * @param {string} options.prompt - The user's description of the widget.
   * @param {AbortSignal} options.signal - Aborted by cancel() or the caller's
   *   signal.
   * @returns {Promise<GeneratedWidgetAddonWrapper>} Resolves to the installed
   *   add-on.
   */
  async #create({ prompt, signal }) {
    let path = await lazy.WidgetGenerator.generate({ prompt, signal });
    signal.throwIfAborted();
    this.#abortController = null;
    try {
      return await lazy.GeneratedWidgetsProvider.install(path);
    } catch (error) {
      throw new WidgetCreatorError(
        "The widget couldn't be installed",
        CREATION_ERRORS.INSTALL,
        error
      );
    }
  }

  /**
   * @param {GeneratedWidgetAddonWrapper} addon - The installed add-on.
   * @returns {GeneratedWidgetAddonWrapper} The same add-on.
   */
  #finished(addon) {
    this.#clear();
    return addon;
  }

  /**
   * @param {Error} error - The error from generation or installation.
   */
  #failed(error) {
    let prompt = this.#prompt;
    this.#clear();
    if (error.name != "AbortError") {
      this.dispatchEvent(
        new CustomEvent("WidgetCreator:Error", { detail: { error, prompt } })
      );
    }
    throw error;
  }

  #clear() {
    this.#pending = null;
    this.#prompt = null;
    this.#abortController = null;
  }
}

export const WidgetCreator = new _WidgetCreator();
