/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import {Store} from "resource://newtab/lib/Store.sys.mjs" */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  PersistentCache: "resource://newtab/lib/PersistentCache.sys.mjs",
  TemporaryMerinoClientShim:
    "resource://newtab/lib/TemporaryMerinoClientShim.sys.mjs",
});

import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";
import {
  FRAMED_WIDGETS,
  isAllowedFrameUrl,
  isWidgetEnabled,
} from "resource://newtab/common/WidgetsRegistry.mjs";

const CACHE_KEY = "framed_widgets_feed";
const MERINO_CLIENT_KEY = "HNT_FRAMED_WIDGETS_FEED";

const PREF_WIDGETS_ENABLED = "widgets.enabled";
const PREF_ENVIRONMENT = "widgets.framed.environment";
// Suffixed with the environment: widgets.framed.endpoint.prod / .stage
const PREF_ENDPOINT_PREFIX = "widgets.framed.endpoint.";
const PREF_ENDPOINTS_ALLOWLIST = "discoverystream.endpoints";

/**
 * Fetches the framed widgets manifest of the selected environment (prod or
 * stage, each its own Merino endpoint) once per day, caches it, and broadcasts
 * a { [widgetId]: { url } } map of the FRAMED_WIDGETS entries the manifest
 * enables.
 * resolveWidgetFrame turns it into each widget's document URL and frame state.
 */
export class FramedWidgetsFeed {
  /** @type {Store} */
  store = null;

  constructor() {
    this.loaded = false;
    this.merino = null;
    this.manifest = null;
    this.cache = this.PersistentCache(CACHE_KEY, true);
  }

  isEnabled() {
    const { values } = this.store.getState().Prefs;
    return FRAMED_WIDGETS.some(widget =>
      isWidgetEnabled(widget, values, values[PREF_WIDGETS_ENABLED])
    );
  }

  // Guard the endpoint against the shared allowlist so a mis-set pref can't
  // point the fetch at an arbitrary host.
  getEnvironment() {
    const { values } = this.store.getState().Prefs;
    return values[PREF_ENVIRONMENT] === "stage" ? "stage" : "prod";
  }

  getEndpoint() {
    const { values } = this.store.getState().Prefs;
    const endpoint = values[PREF_ENDPOINT_PREFIX + this.getEnvironment()];
    if (!endpoint) {
      return null;
    }
    const allowed = (values[PREF_ENDPOINTS_ALLOWLIST] ?? "")
      .split(",")
      .map(item => item.trim())
      .filter(item => item);
    if (!allowed.some(prefix => endpoint.startsWith(prefix))) {
      console.error(`Framed widgets endpoint not in allowlist: ${endpoint}`);
      return null;
    }
    return endpoint;
  }

  isStale(lastUpdated) {
    if (!lastUpdated) {
      return true;
    }
    const D = this.Date();
    return new D(lastUpdated).toDateString() !== new D(D.now()).toDateString();
  }

  normalize(raw) {
    const { values } = this.store.getState().Prefs;
    const entries = Array.isArray(raw?.widgets) ? raw.widgets : [];
    const frames = {};
    for (const { id } of FRAMED_WIDGETS) {
      const entry = entries.find(item => item?.id === id);
      if (entry?.enabled === true && isAllowedFrameUrl(entry.url, values)) {
        frames[id] = { url: entry.url };
      }
    }
    return frames;
  }

  async fetch() {
    if (!this.merino) {
      this.merino = this.MerinoClient(MERINO_CLIENT_KEY);
    }
    const endpointUrl = this.getEndpoint();
    if (!endpointUrl) {
      return null;
    }
    const { data, error } = await this.merino.fetchFramedWidgets({
      source: "newtab",
      endpointUrl,
      locale: Services.locale.appLocaleAsBCP47,
    });
    if (error || !Array.isArray(data?.widgets)) {
      return null;
    }
    const entry = {
      environment: this.getEnvironment(),
      manifest: data,
      lastUpdated: this.Date().now(),
    };
    await this.cache.set("frames", entry);
    return entry;
  }

  // A cached manifest from the other environment is never served: switching
  // environments always refetches.
  async loadFrames() {
    const { frames: cached } = (await this.cache.get()) || {};
    const usable =
      cached?.environment === this.getEnvironment() ? cached : null;
    let entry = usable;
    if (this.isStale(usable?.lastUpdated)) {
      entry = (await this.fetch()) || usable;
    }
    this.manifest = entry?.manifest ?? null;
    this.update();
    this.loaded = true;
  }

  update() {
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.FRAMED_WIDGETS_UPDATE,
        data: this.normalize(this.manifest),
      })
    );
  }

  async onAction(action) {
    switch (action.type) {
      case at.INIT:
        if (this.isEnabled()) {
          await this.loadFrames();
        }
        break;
      case at.PREF_CHANGED:
        if (!this.loaded) {
          if (this.isEnabled()) {
            await this.loadFrames();
          }
        } else if (action.data?.name === PREF_ENVIRONMENT) {
          await this.loadFrames();
        }
        break;
      case at.SYSTEM_TICK:
        if (this.isEnabled()) {
          await this.loadFrames();
        }
        break;
    }
  }
}

/**
 * Thin wrappers around external tools so tests can stub them.
 */
FramedWidgetsFeed.prototype.MerinoClient = name => {
  return new lazy.TemporaryMerinoClientShim(name);
};
FramedWidgetsFeed.prototype.PersistentCache = (...args) => {
  return new lazy.PersistentCache(...args);
};
FramedWidgetsFeed.prototype.Date = () => {
  return Date;
};
