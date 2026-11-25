/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  GENERATION_ERRORS:
    "moz-src:///browser/components/widgets/WidgetGenerator.sys.mjs",
  WidgetCreator: "moz-src:///browser/components/widgets/WidgetCreator.sys.mjs",
  WidgetCreatorError:
    "moz-src:///browser/components/widgets/WidgetCreator.sys.mjs",
  WidgetGeneratorError:
    "moz-src:///browser/components/widgets/WidgetGenerator.sys.mjs",
  WidgetManager: "moz-src:///toolkit/components/widgets/WidgetManager.sys.mjs",
});

const ORDER_PREF = "browser.widgets.studio.order";

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
 * Keeps a host element in sync with WidgetCreator and the WidgetManager,
 * re-rendering it whenever a widget creation in any window starts or settles
 * and whenever a generated widget changes.
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

  /** @type {Array<Widget>} registered generated widgets in the saved order */
  #widgets = [];

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
   * The ids of the generated widgets in the order they are listed, saved in
   * ORDER_PREF.
   *
   * @type {Array<string>}
   */
  get order() {
    return Services.prefs
      .getStringPref(ORDER_PREF, "")
      .split(",")
      .filter(Boolean);
  }

  /**
   * @param {Array<string>} ids - the widget ids in the order to list them
   */
  set order(ids) {
    Services.prefs.setStringPref(ORDER_PREF, ids.join(","));
  }

  /**
   * All generated widgets.
   *
   * @type {Array<Widget>}
   */
  get all() {
    return this.#widgets;
  }

  /**
   * @type {Array<Widget>} Enabled generated widgets.
   */
  get enabled() {
    return this.all.filter(widget => widget.enabled);
  }

  /**
   * @type {Array<Widget>} Disabled generated widgets.
   */
  get disabled() {
    return this.all.filter(widget => !widget.enabled);
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

  /**
   * Reads the generated widgets, appends the ones missing from the saved
   * order to it, sorts them by that order and updates the host.
   */
  #update = () => {
    let widgets = lazy.WidgetManager.getWidgets().filter(
      widget => widget.generated
    );
    let order = new Set(this.order);
    let registered = new Set(widgets.map(widget => widget.id));
    let ids = order.union(registered);
    if (ids.size > order.size) {
      this.order = [...ids];
    }
    let index = new Map([...ids].map((id, i) => [id, i]));
    this.#widgets = widgets.sort((a, b) => index.get(a.id) - index.get(b.id));
    this.host.requestUpdate();
  };

  /**
   * Removes the ids of widgets that are no longer installed from the saved
   * order.
   */
  async #prune() {
    if (Services.appinfo.inSafeMode) {
      return;
    }
    let { signal } = this.#listeners;
    let addons = await lazy.AddonManager.getAddonsByTypes(["generatedwidget"]);
    if (signal.aborted) {
      return;
    }
    let installed = new Set(addons.map(addon => addon.id));
    let { order } = this;
    let kept = order.filter(
      id => installed.has(id) || lazy.WidgetManager.getWidgetByID(id)
    );
    if (kept.length < order.length) {
      this.order = kept;
    }
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
    lazy.WidgetManager.on("change", this.#update);
    Services.prefs.addObserver(ORDER_PREF, this.#update);
    this.#update();
    this.#prune().catch(console.error);
    let { pending } = lazy.WidgetCreator;
    if (pending) {
      this.#watch(pending);
    }
  }

  hostDisconnected() {
    lazy.WidgetManager.off("change", this.#update);
    Services.prefs.removeObserver(ORDER_PREF, this.#update);
    this.#listeners?.abort();
    this.#listeners = null;
  }
}
