/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import {Store} from "resource://newtab/lib/Store.sys.mjs" */

import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";
import { WIDGET_REGISTRY } from "resource://newtab/common/WidgetsRegistry.mjs";
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
 * The information New Tab uses from a widget's extension.
 *
 * @typedef {object} ExtensionWidget
 * @property {string} src - URL of the widget's document
 * @property {string} name - display name of the widget
 * @property {string} telemetryName - name sent in Glean events, the widget's id
 * @property {string[]} validSizes - sizes the widget supports
 * @property {boolean} [generated] - whether the widget was generated from a
 *   prompt
 */

/**
 * Publishes the ExtensionWidget of each extension that a WIDGET_REGISTRY
 * entry names and that has a widget in WidgetManager, keyed by extension id,
 * as the ExtensionWidgets state.
 */
export class ExtensionWidgetsFeed {
  /** @type {Store} */
  store = null;

  constructor() {
    this.onChange = this.onChange.bind(this);
  }

  /**
   * Returns the ExtensionWidget of each extension that a WIDGET_REGISTRY entry
   * names and that has a widget in WidgetManager.
   *
   * @returns {{[extensionID: string]: ExtensionWidget}}
   */
  #getExtensionWidgets() {
    /** @type {{[extensionID: string]: ExtensionWidget}} */
    const extensionWidgets = {};
    if (!isWidgetManagerSupported()) {
      return extensionWidgets;
    }
    for (const { extensionID } of WIDGET_REGISTRY) {
      const widget = lazy.WidgetManager.getWidgetByID(extensionID);
      if (widget) {
        extensionWidgets[extensionID] = {
          src: widget.src,
          name: widget.name,
          telemetryName: widget.id,
          validSizes: widget.sizes,
          ...(widget.generated && { generated: true }),
        };
      }
    }
    return extensionWidgets;
  }

  /**
   * Broadcasts the ExtensionWidgets.
   *
   * @param {{[extensionID: string]: ExtensionWidget}} extensionWidgets - the
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
   * WidgetManager change listener. Broadcasts when a WIDGET_REGISTRY entry
   * names the extension of the widget that changed.
   *
   * @param {string} event - "change"
   * @param {string} id - id of the widget that changed
   */
  onChange(event, id) {
    if (WIDGET_REGISTRY.some(({ extensionID }) => extensionID === id)) {
      this.#broadcast(this.#getExtensionWidgets());
    }
  }

  onAction(action) {
    switch (action.type) {
      case at.INIT:
        if (isWidgetManagerSupported()) {
          lazy.WidgetManager.on("change", this.onChange);
        }
        this.#broadcast(this.#getExtensionWidgets(), { isStartup: true });
        break;
      case at.UNINIT:
        if (isWidgetManagerSupported()) {
          lazy.WidgetManager.off("change", this.onChange);
        }
        break;
    }
  }
}
