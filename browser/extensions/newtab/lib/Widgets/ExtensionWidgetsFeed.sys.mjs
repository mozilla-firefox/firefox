/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import {Store} from "resource://newtab/lib/Store.sys.mjs" */

import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";
import {
  PREF_WIDGETS_GENERATED_ENABLED,
  PREF_WIDGETS_ORDER,
  resolveWidgetOrder,
} from "resource://newtab/common/WidgetsRegistry.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  WidgetManager: "moz-src:///toolkit/components/widgets/WidgetManager.sys.mjs",
});

/**
 * @backward-compat { version 160 }
 * WidgetManager is a host module added in 160, absent when New Tab train-hops
 * onto an older Firefox. Remove this guard once 160 reaches Release.
 */
function isWidgetManagerSupported() {
  return Services.vc.compare(AppConstants.MOZ_APP_VERSION, "160.0a1") >= 0;
}

/**
 * A widget registered with WidgetManager.
 *
 * @typedef {object} ExtensionWidget
 * @property {string} id - the widget's extension id
 * @property {string} extensionID - the widget's extension id
 * @property {string} src - URL of the widget's document
 * @property {string} name - display name of the widget
 * @property {string} telemetryName - name sent in Glean events, "generated"
 *   or the widget's id
 * @property {string[]} validSizes - sizes the widget supports
 * @property {boolean} enabled - whether the widget is enabled
 * @property {string} size - the widget's chosen or default size
 * @property {boolean} [generated] - whether the widget was generated from a
 *   prompt
 * @property {string} [systemEnabledPref] - pref that makes a generated widget
 *   available
 */

/**
 * Publishes the ExtensionWidget of each widget in WidgetManager, keyed by
 * widget id, as the ExtensionWidgets state.
 */
export class ExtensionWidgetsFeed {
  /** @type {Store} */
  store = null;

  /**
   * Returns the ExtensionWidget of each widget in WidgetManager.
   *
   * @returns {{[id: string]: ExtensionWidget}}
   */
  #getExtensionWidgets() {
    /** @type {{[id: string]: ExtensionWidget}} */
    const extensionWidgets = {};
    if (!isWidgetManagerSupported()) {
      return extensionWidgets;
    }
    for (const widget of lazy.WidgetManager.getWidgets()) {
      let extensionWidget = {
        id: widget.id,
        extensionID: widget.id,
        src: widget.src,
        name: widget.name,
        validSizes: widget.sizes,
        enabled: widget.enabled,
        size: widget.size,
        telemetryName: widget.id,
      };
      if (widget.generated) {
        extensionWidget.telemetryName = "generated";
        extensionWidget.generated = true;
        extensionWidget.systemEnabledPref = PREF_WIDGETS_GENERATED_ENABLED;
      }
      extensionWidgets[widget.id] = extensionWidget;
    }
    return extensionWidgets;
  }

  /**
   * Returns the ids in the widgets.order pref.
   *
   * @returns {string[]}
   */
  #getOrder() {
    let order = this.store.getState().Prefs.values[PREF_WIDGETS_ORDER];
    return (order ?? "").split(",").filter(id => id);
  }

  /**
   * Writes the widgets.order pref.
   *
   * @param {string[]} order - widget ids, in order
   */
  #setOrder(order) {
    this.store.dispatch(ac.SetPref(PREF_WIDGETS_ORDER, order.join(",")));
  }

  /**
   * Adds the ids missing from the widgets.order pref to its start. An empty
   * pref starts from the effective order, so a trainhop order is kept.
   *
   * @param {string[]} ids - widget ids
   */
  #prependToOrder(ids) {
    let order = this.#getOrder();
    if (!order.length) {
      order = resolveWidgetOrder(this.store.getState().Prefs.values);
    }
    let added = ids.filter(id => !order.includes(id));
    if (!added.length) {
      return;
    }
    this.#setOrder([...added, ...order]);
  }

  /**
   * Broadcasts the ExtensionWidgets.
   *
   * @param {{[id: string]: ExtensionWidget}} extensionWidgets - the
   *   ExtensionWidgets to broadcast
   * @param {object} [meta] - meta of the broadcast action
   */
  #broadcast(extensionWidgets, meta) {
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.WIDGETS_EXTENSIONS_UPDATE,
        data: extensionWidgets,
        meta,
      })
    );
  }

  /**
   * Adds new generated widgets to the widgets.order pref and broadcasts every
   * widget. Other extension widgets render under their registry id.
   *
   * @param {object} [meta] - meta of the broadcast action
   */
  #publish(meta) {
    let extensionWidgets = this.#getExtensionWidgets();
    this.#prependToOrder(
      Object.values(extensionWidgets)
        .filter(widget => widget.generated)
        .map(widget => widget.id)
    );
    this.#broadcast(extensionWidgets, meta);
  }

  /**
   * Sets a widget's enabled state or size in WidgetManager.
   *
   * @param {{id: string, enabled?: boolean, size?: string}} data - the
   *   WIDGETS_EXTENSIONS_SET data
   */
  #set({ id, enabled, size }) {
    let widget = lazy.WidgetManager.getWidgetByID(id);
    if (!widget) {
      return;
    }
    if (typeof enabled === "boolean") {
      widget.enabled = enabled;
    }
    if (typeof size === "string") {
      widget.size = size;
    }
  }

  /** WidgetManager change listener. Publishes the ExtensionWidgets. */
  onChange = () => {
    this.#publish();
  };

  /**
   * WidgetManager cleanupPrefsDone listener. Removes the widget's id from the
   * widgets.order pref.
   *
   * @param {string} event - "cleanupPrefsDone"
   * @param {string} id - id of the widget whose prefs were cleaned up.
   */
  onCleanupPrefsDone = (event, id) => {
    let order = this.#getOrder();
    if (!order.includes(id)) {
      return;
    }
    this.#setOrder(order.filter(item => item !== id));
  };

  onAction(action) {
    switch (action.type) {
      case at.INIT:
        if (isWidgetManagerSupported()) {
          lazy.WidgetManager.on("change", this.onChange);
          lazy.WidgetManager.on("cleanupPrefsDone", this.onCleanupPrefsDone);
        }
        this.#publish({ isStartup: true });
        break;
      case at.UNINIT:
        if (isWidgetManagerSupported()) {
          lazy.WidgetManager.off("change", this.onChange);
          lazy.WidgetManager.off("cleanupPrefsDone", this.onCleanupPrefsDone);
        }
        break;
      case at.WIDGETS_EXTENSIONS_SET:
        if (isWidgetManagerSupported()) {
          this.#set(action.data);
        }
        break;
    }
  }
}
