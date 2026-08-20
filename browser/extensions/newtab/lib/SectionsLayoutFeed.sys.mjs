/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

const SECTION_LAYOUTS_REMOTE_SETTINGS_COLLECTION = "newtab-section-layouts";
const SECTIONS_ORDERING_REMOTE_SETTINGS_COLLECTION = "newtab-sections-ordering";

// Section ranks that may carry an ad.
const AD_ALLOWED_RANKS = new Set([0, 1, 2, 4, 6, 8, 10, 12, 14, 16, 18]);

const PREF_SECTIONS_ORDERING = "discoverystream.sections.ordering";

const PREF_TOPSTORIES_ENABLED = "feeds.section.topstories";

const DEFAULT_SECTION_LAYOUT = [
  {
    name: "7-double-row-2-ad",
    responsiveLayouts: [
      {
        columnCount: 4,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 2,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 4,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 5,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 6,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 7,
            hasAd: false,
            hasExcerpt: true,
          },
        ],
      },
      {
        columnCount: 3,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 2,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 5,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 4,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 6,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 7,
            hasAd: false,
            hasExcerpt: true,
          },
        ],
      },
      {
        columnCount: 2,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 2,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 4,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 5,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 6,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 7,
            hasAd: false,
            hasExcerpt: true,
          },
        ],
      },
      {
        columnCount: 1,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 2,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 4,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 5,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 6,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 7,
            hasAd: false,
            hasExcerpt: true,
          },
        ],
      },
    ],
  },
  {
    name: "6-small-medium-1-ad",
    responsiveLayouts: [
      {
        columnCount: 4,
        tiles: [
          {
            size: "small",
            position: 2,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 4,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 5,
            hasAd: false,
            hasExcerpt: false,
          },
        ],
      },
      {
        columnCount: 3,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 1,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 2,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 3,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 4,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 5,
            hasAd: false,
            hasExcerpt: true,
          },
        ],
      },
      {
        columnCount: 2,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 2,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 4,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 5,
            hasAd: false,
            hasExcerpt: false,
          },
        ],
      },
      {
        columnCount: 1,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 2,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 4,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 5,
            hasAd: false,
            hasExcerpt: false,
          },
        ],
      },
    ],
  },
  {
    name: "4-large-small-medium-1-ad",
    responsiveLayouts: [
      {
        columnCount: 4,
        tiles: [
          {
            size: "large",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 2,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
        ],
      },
      {
        columnCount: 3,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 2,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
        ],
      },
      {
        columnCount: 2,
        tiles: [
          {
            size: "large",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 2,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
        ],
      },
      {
        columnCount: 1,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 2,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
        ],
      },
    ],
  },
  {
    name: "4-medium-small-1-ad",
    responsiveLayouts: [
      {
        columnCount: 4,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 1,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 2,
            hasAd: false,
            hasExcerpt: true,
          },
          { size: "medium", position: 3, hasAd: true, hasExcerpt: true },
        ],
      },
      {
        columnCount: 3,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 2,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
        ],
      },
      {
        columnCount: 2,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 2,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
        ],
      },
      {
        columnCount: 1,
        tiles: [
          {
            size: "medium",
            position: 0,
            hasAd: false,
            hasExcerpt: true,
          },
          {
            size: "medium",
            position: 1,
            hasAd: true,
            hasExcerpt: true,
          },
          {
            size: "small",
            position: 2,
            hasAd: false,
            hasExcerpt: false,
          },
          {
            size: "small",
            position: 3,
            hasAd: false,
            hasExcerpt: false,
          },
        ],
      },
    ],
  },
];

export const SectionsLayoutManager = {
  DEFAULT_SECTION_LAYOUT,
  AD_ALLOWED_RANKS,
};

function isValidLayout(record) {
  if (!record.name || !Array.isArray(record.responsiveLayouts)) {
    return false;
  }
  const columnCounts = new Set(
    record.responsiveLayouts.map(layout => layout.columnCount)
  );
  return [1, 2, 3, 4].every(n => columnCounts.has(n));
}

// A sections-ordering record is a name plus a non-empty array of
// section-layout names to apply by position.
function isValidOrdering(record) {
  return (
    !!record.name &&
    Array.isArray(record.sectionLayouts) &&
    !!record.sectionLayouts.length
  );
}

/**
 * Clear `hasAd` on every tile when a section may not carry an ad: ads are kept
 * only when the section's rank is in `allowedRanks` and its `allowAds` is not
 * false. When clearing, returns a copy so shared layout records (reused across
 * sections via rotation) are left untouched.
 *
 * @param {object} layout A section-layout record.
 * @param {object} section The section, providing `receivedRank` and `allowAds`.
 * @param {Set<number>} allowedRanks Section ranks that may carry an ad.
 * @returns {object} The layout, ad-masked when required.
 */
export function maskLayoutAds(
  layout,
  { receivedRank, allowAds },
  allowedRanks
) {
  const adsAllowed = allowAds !== false && allowedRanks.has(receivedRank);
  if (adsAllowed) {
    return layout;
  }
  return {
    ...layout,
    responsiveLayouts: layout.responsiveLayouts.map(responsiveLayout => ({
      ...responsiveLayout,
      tiles: responsiveLayout.tiles.map(tile =>
        tile.hasAd ? { ...tile, hasAd: false } : tile
      ),
    })),
  };
}

export class SectionsLayoutFeed {
  constructor() {
    this._layoutsClient = null;
    this._orderingClient = null;
    this._onSync = this._onSync.bind(this);
  }

  RemoteSettings(...args) {
    return lazy.RemoteSettings(...args);
  }

  // Lazily create a Remote Settings client for `collection`, subscribing once
  // to its sync events.
  _connectClient(collection) {
    const client = this.RemoteSettings(collection);
    client.on("sync", this._onSync);
    return client;
  }

  async init(isStartup = false) {
    const prefs = this.store.getState().Prefs.values;
    const orderingKey =
      prefs.trainhopConfig?.sections?.ordering ??
      prefs[PREF_SECTIONS_ORDERING] ??
      "";
    // Sync the collections only when a sections-ordering is selected.
    const shouldSyncLayouts = prefs[PREF_TOPSTORIES_ENABLED] && !!orderingKey;

    if (shouldSyncLayouts) {
      this._layoutsClient ??= this._connectClient(
        SECTION_LAYOUTS_REMOTE_SETTINGS_COLLECTION
      );
      this._orderingClient ??= this._connectClient(
        SECTIONS_ORDERING_REMOTE_SETTINGS_COLLECTION
      );
      await this._fetchLayouts(isStartup);
    } else {
      // Disconnect listeners from a previously-active ordering so a later RS
      // sync doesn't keep firing while the feature is off.
      this.uninit();
    }
  }

  uninit() {
    this._layoutsClient?.off("sync", this._onSync);
    this._orderingClient?.off("sync", this._onSync);
    this._layoutsClient = null;
    this._orderingClient = null;
  }

  async _onSync() {
    await this._fetchLayouts(false);
  }

  async _fetchLayouts(isStartup) {
    const [layoutRecords, orderingRecords] = await Promise.all([
      this._layoutsClient.get(),
      this._orderingClient.get(),
    ]);

    const configs = {};
    for (const record of layoutRecords || []) {
      if (isValidLayout(record)) {
        configs[record.name] = record;
      }
    }

    const orderings = {};
    for (const record of orderingRecords || []) {
      if (isValidOrdering(record)) {
        orderings[record.name] = record.sectionLayouts;
      }
    }

    if (!Object.keys(configs).length && !Object.keys(orderings).length) {
      return;
    }

    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.SECTIONS_LAYOUT_UPDATE,
        data: { configs, orderings },
        meta: { isStartup },
      })
    );
  }

  async onAction(action) {
    switch (action.type) {
      case at.INIT:
        await this.init(true);
        break;
      case at.UNINIT:
        this.uninit();
        break;
      case at.PREF_CHANGED:
        if (
          action.data.name === PREF_SECTIONS_ORDERING ||
          action.data.name === PREF_TOPSTORIES_ENABLED ||
          (action.data.name === "trainhopConfig" &&
            action.data.value?.sections?.ordering !== undefined)
        ) {
          await this.init(false);
        }
        break;
    }
  }
}
