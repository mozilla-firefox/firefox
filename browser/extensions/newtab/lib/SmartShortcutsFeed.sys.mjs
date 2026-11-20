/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import {Store} from "resource://newtab/lib/Store.sys.mjs" */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  pruneShortcutInteractions:
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs",
});

import { actionTypes as at } from "resource://newtab/common/Actions.mjs";

const PREF_SYSTEM_SHORTCUTS_PERSONALIZATION =
  "discoverystream.shortcuts.personalization.enabled";

const PREF_SYSTEM_SHORTCUTS_LOG = "discoverystream.shortcuts.force_log.enabled";

function smartshortcutsEnabled(values) {
  // nimbus pref, if its there, overrides the local pref
  const experimentVariable = values.trainhopConfig?.smartShortcuts?.enabled;
  if (typeof experimentVariable === "boolean") {
    return experimentVariable;
  }
  return !!values[PREF_SYSTEM_SHORTCUTS_PERSONALIZATION];
}

const INTERACTION_FLUSH_DELAY_MS = 1000;
const INTERACTION_FLUSH_MAX_IDLE_WAIT_MS = 5000;
const TOPIC_IDLE_DAILY = "idle-daily";

function timeMSToSeconds(timeMS) {
  return Math.round(timeMS / 1000);
}

/**
 * A feature that periodically generates an interest vector for personalized shortcuts.
 */
export class SmartShortcutsFeed {
  /** @type {Store} */
  store = null;

  constructor() {
    this.loaded = false;
    this.pendingInteractions = [];
    this.flushTask = null;
    this.observingIdleDaily = false;
  }

  isEnabled() {
    // enabled by smartshortcuts being enabled or force_log=true in nimbus pref
    const { values } = this.store.getState().Prefs;
    const systemLogPref = values[PREF_SYSTEM_SHORTCUTS_LOG];
    const experimentLogPref = values.trainhopConfig?.smartShortcuts?.force_log;

    return smartshortcutsEnabled(values) || systemLogPref || experimentLogPref;
  }

  async init() {
    this.loaded = this.isEnabled();
  }

  observe(subject, topic) {
    if (topic === TOPIC_IDLE_DAILY) {
      this.pruneInteractions();
    }
  }

  async pruneInteractions() {
    if (Services.startup.shuttingDown) {
      return;
    }
    await lazy.pruneShortcutInteractions(this.Date().now());
  }

  async reset() {
    const task = this.flushTask;
    this.flushTask = null;
    await task?.finalize();
    this.loaded = false;
  }

  queueShortcutsInteraction(event_type, data) {
    // We don't need to worry about interactions that don't have a guid.
    if (!data.guid) {
      return;
    }
    this.pendingInteractions.push({
      guid: data.guid,
      event_type,
      timestamp_s: timeMSToSeconds(this.Date().now()),
      pinned: data.isPinned ? 1 : 0,
      tile_position: data.position,
    });
    this.flushTask ??= new lazy.DeferredTask(
      () => this.flushShortcutsInteractions(),
      INTERACTION_FLUSH_DELAY_MS,
      INTERACTION_FLUSH_MAX_IDLE_WAIT_MS
    );
    this.flushTask.arm();
  }

  async flushShortcutsInteractions() {
    const rows = this.pendingInteractions;
    this.pendingInteractions = [];
    if (!rows.length) {
      return;
    }

    let sql = `
      INSERT INTO moz_newtab_shortcuts_interaction (
        place_id, event_type, timestamp_s, pinned, tile_position
      )
      SELECT
        id, :event_type, :timestamp_s, :pinned, :tile_position
      FROM moz_places
      WHERE guid = :guid
    `;

    await lazy.PlacesUtils.withConnectionWrapper(
      "newtab/lib/SmartShortcutsFeed.sys.mjs: flushShortcutsInteractions",
      async db => {
        await db.executeCached(sql, rows);
      }
    );
  }

  handleTopSitesOrganicImpressionStats(action) {
    switch (action.data?.type) {
      case "impression": {
        this.queueShortcutsInteraction(0, action.data);
        break;
      }
      case "click": {
        this.queueShortcutsInteraction(1, action.data);
        break;
      }
    }
  }

  async onPrefChangedAction(action) {
    switch (action.data.name) {
      case PREF_SYSTEM_SHORTCUTS_PERSONALIZATION: {
        await this.init();
        break;
      }
    }
  }

  async onAction(action) {
    switch (action.type) {
      case at.INIT:
        if (!this.observingIdleDaily) {
          Services.obs.addObserver(this, TOPIC_IDLE_DAILY);
          this.observingIdleDaily = true;
        }
        await this.init();
        break;
      case at.UNINIT:
        if (this.observingIdleDaily) {
          Services.obs.removeObserver(this, TOPIC_IDLE_DAILY);
          this.observingIdleDaily = false;
        }
        await this.reset();
        break;
      case at.TOP_SITES_ORGANIC_IMPRESSION_STATS:
        if (this.isEnabled()) {
          this.handleTopSitesOrganicImpressionStats(action);
        }
        break;
      case at.PREF_CHANGED:
        this.onPrefChangedAction(action);
        if (action.data.name === "trainhopConfig") {
          await this.init();
        }
        break;
    }
  }
}

/**
 * Creating a thin wrapper around Date.
 * This makes it easier for us to write automated tests that simulate responses.
 */
SmartShortcutsFeed.prototype.Date = () => {
  return Date;
};
