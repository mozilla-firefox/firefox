/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { EventEmitter } from "resource://gre/modules/EventEmitter.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ExtensionCommon: "resource://gre/modules/ExtensionCommon.sys.mjs",
});

const PREF_ROOT = "extensions.widgets.";

/**
 * @param {string} id - the id of a widget's extension
 * @returns {string} the key naming the widget's prefs
 */
function prefKeyFor(id) {
  return lazy.ExtensionCommon.makeWidgetId(id.replace(/@mozilla\.org$/, ""));
}

/**
 * @typedef {object} WidgetOptions
 * @property {string} id - the widget id (from the extension that provides it)
 * @property {string} src - moz-extension: URL of the widget page
 * @property {string} name - display name for the widget
 * @property {Array<string>} sizes - sizes the widget supports
 * @property {string} [defaultSize] - size used until one is chosen
 * @property {string} [prefBranch] - pref branch storing whether the widget is
 *   enabled and its size, in `<prefBranch>.enabled` and `<prefBranch>.size`
 * @property {string} [l10nId] - Fluent id of the display name
 * @property {boolean} [generated] - whether the widget was generated from a
 *   prompt
 */

/**
 * A self-contained, interactive widget that an extension registers for host
 * surfaces to embed. Widgets are used for presenting brief updates or
 * completing a quick task.
 */
class Widget {
  /** @type {object} the widget's properties */
  #properties;

  /**
   * @param {WidgetOptions} properties - the options it was registered with
   */
  constructor(properties) {
    this.#properties = properties;
    Object.freeze(this);
  }

  /** @returns {string} the widget's id */
  get id() {
    return this.#properties.id;
  }

  /** @returns {string} the moz-extension: URL of the widget's document */
  get src() {
    return this.#properties.src;
  }

  /** @returns {string} the widget's display name */
  get name() {
    return this.#properties.name;
  }

  /** @returns {Array<string>} every size the widget renders at */
  get sizes() {
    return [...this.#properties.sizes];
  }

  /** @returns {string|null} the Fluent id of the widget's display name */
  get l10nId() {
    return this.#properties.l10nId ?? null;
  }

  /** @returns {boolean} whether the widget was generated from a prompt */
  get generated() {
    return this.#properties.generated ?? false;
  }

  /**
   * Whether the widget is enabled.
   *
   * @type {boolean}
   */
  get enabled() {
    return Services.prefs.getBoolPref(this.#properties.enabledPref, false);
  }

  set enabled(enabled) {
    Services.prefs.setBoolPref(this.#properties.enabledPref, Boolean(enabled));
  }

  /**
   * The widget's chosen or default size.
   *
   * @type {string}
   */
  get size() {
    const { defaultSize, sizePref, sizes } = this.#properties;
    const chosen = Services.prefs.getStringPref(sizePref, "");
    return [chosen, defaultSize].find(size => sizes.includes(size)) ?? sizes[0];
  }

  set size(size) {
    if (this.#properties.sizes.includes(size)) {
      Services.prefs.setStringPref(this.#properties.sizePref, size);
    }
  }
}

const REQUIRED_OPTIONS = ["id", "src", "name", "sizes"];

/**
 * Manages the widgets registered by extensions.
 *
 * Each widget's enabled state and size are stored in prefs under its pref
 * branch, by default `extensions.widgets.<key>`.
 *
 * Emits "change" with a widget's id when the widget registers, unregisters,
 * or one of its prefs changes after init, and at init for each widget
 * registered before it.
 */
export class _WidgetManager {
  /** @type {Map<string, Widget>} registered widgets keyed by widget id */
  #widgets = new Map();

  /** @type {Map<string, string>} widget ids keyed by pref name */
  #prefHolders = new Map();

  /** @type {boolean} whether init has run */
  #isReady = false;

  /** @type {PromiseWithResolvers<void>} resolved by init */
  #ready = Promise.withResolvers();

  constructor() {
    EventEmitter.decorate(this);
  }

  init() {
    if (this.#isReady) {
      return;
    }
    this.#isReady = true;
    this.#ready.resolve();
    for (const id of this.#widgets.keys()) {
      this.emit("change", id);
    }
  }

  uninit() {
    this.#isReady = false;
    this.#ready = Promise.withResolvers();
    for (const pref of this.#prefHolders.keys()) {
      Services.prefs.removeObserver(pref, this.#onPrefChanged);
    }
    this.#prefHolders.clear();
    this.#widgets.clear();
  }

  /** @returns {boolean} whether init has run */
  get isReady() {
    return this.#isReady;
  }

  /** @returns {Promise<void>} resolves when init runs */
  get readyPromise() {
    return this.#ready.promise;
  }

  /**
   * @param {string} id - id of the widget that changed
   */
  #emitChange(id) {
    if (this.#isReady) {
      this.emit("change", id);
    }
  }

  /**
   * @param {nsISupports} subject - the pref branch
   * @param {string} topic - "nsPref:changed"
   * @param {string} name - the changed pref
   */
  #onPrefChanged = (subject, topic, name) => {
    const id = this.#prefHolders.get(name);
    if (id) {
      this.#emitChange(id);
    }
  };

  /**
   * Register a new widget.
   *
   * @param {WidgetOptions} options
   * @returns {Widget} the registered widget
   * @throws {TypeError} if the ID or one of its prefs is already registered
   * @throws {TypeError} if a required option is missing
   */
  register(options) {
    for (const key of REQUIRED_OPTIONS) {
      if (options[key] === undefined) {
        throw new TypeError(`"${key}" is required`);
      }
    }
    const { id } = options;
    if (this.#widgets.has(id)) {
      throw new TypeError(`"${id}" is already registered`);
    }
    const prefBranch = options.prefBranch ?? `${PREF_ROOT}${prefKeyFor(id)}`;
    const properties = {
      ...options,
      enabledPref: `${prefBranch}.enabled`,
      sizePref: `${prefBranch}.size`,
    };
    const prefs = [properties.enabledPref, properties.sizePref];
    for (const pref of prefs) {
      const holder = this.#prefHolders.get(pref);
      if (holder) {
        throw new TypeError(
          `The pref "${pref}" of "${id}" is used by "${holder}"`
        );
      }
    }
    const widget = new Widget(properties);
    this.#widgets.set(id, widget);
    for (const pref of prefs) {
      this.#prefHolders.set(pref, id);
      Services.prefs.addObserver(pref, this.#onPrefChanged);
    }
    this.#emitChange(id);
    return widget;
  }

  /**
   * Remove an existing widget.
   *
   * @param {string} id - a widget id
   */
  unregister(id) {
    if (!this.#widgets.delete(id)) {
      return;
    }
    for (const [pref, holder] of this.#prefHolders) {
      if (holder === id) {
        Services.prefs.removeObserver(pref, this.#onPrefChanged);
        this.#prefHolders.delete(pref);
      }
    }
    this.#emitChange(id);
  }

  /**
   * @param {string} id - a widget id
   * @returns {Widget|undefined} the widget, or undefined when none is registered
   */
  getWidgetByID(id) {
    return this.#widgets.get(id);
  }

  /**
   * @returns {Array<Widget>} every registered widget, in registration order
   */
  getWidgets() {
    return [...this.#widgets.values()];
  }
}

export const WidgetManager = new _WidgetManager();
