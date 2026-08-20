/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
Smart Shortcuts uses experimental prefs on newtabTrainhopConfig.
These prefs can be accessed at prefValues.trainhopConfig.smartShortcuts

* enabled: do smart shortcuts (TopSitesFeed)
* over_sample_multiplier: number of rows of shortcuts to consider for smart shortcuts
*          a user has n rows, we then query for n*over_sample_multiplier items to rank
*          (TopSitesFeed)
* force_log: log shortcuts interactions regardless of enabled (SmartShortcutsFeed)
* features: arry of feature name strings
* eta: learning rate for feature weights
* click_bonus: multiplier applied to clicks
* positive_prior: thompson sampling alpha
* negative_prior: thompson sampling beta
* min_exp_clicks: minimum recent clicks before exploration is enabled
* min_exp_impressions: minimum recent impressions before exploration is enabled
* tau: seasonality smoothing strength
* thom_weight: weight of thompson sampling. divided by 100
* frec_weight: weight of frecency. divided by 100
* hour_weight: weight of hourly seasonality. divided by 100
* daily_weight: weight of daily seasonality. divided by 100
* bmark_weight: weight of is_bookmark. divided by 100
* rece_weight: weight of recency. divided by 100
* freq_weight: weight of frequency. divided by 100
* refre_weight: weight of re-done frecency. divided by 100
* open_weight: weight of is_open. divided by 100
* unid_weight: weight of unique days visited. divided by 100
* ctr_weight: weight of ctr. divided by 100
* bias_weight: weight of bias. divided by 100

*/

const SHORTCUT_TABLE = "moz_newtab_shortcuts_interaction";
const PLACES_TABLE = "moz_places";
const VISITS_TABLE = "moz_historyvisits";
const BOOKMARK_TABLE = "moz_bookmarks";
const BASE_SEASONALITY_CACHE_EXPIRATION = 1e3 * 60 * 60 * 24 * 7; // 7 day in miliseconds
const ETA = 0;
const CLICK_BONUS = 10;
const PREF_SYSTEM_SHORTCUTS_PERSONALIZATION =
  "discoverystream.shortcuts.personalization.enabled";

const FEATURE_META = {
  thom: { pref: "thom_weight", def: 181 },
  frec: { pref: "frec_weight", def: 0 },
  hour: { pref: "hour_weight", def: 0 },
  daily: { pref: "daily_weight", def: 0 },
  bmark: { pref: "bmark_weight", def: 0 },
  rece: { pref: "rece_weight", def: 466 },
  freq: { pref: "freq_weight", def: 200 },
  refre: { pref: "refre_weight", def: 0 },
  open: { pref: "open_weight", def: 0 },
  unid: { pref: "unid_weight", def: 0 },
  ctr: { pref: "ctr_weight", def: 954 },
  bias: { pref: "bias_weight", def: 0 },
};

const FEATURES = ["thom", "rece", "freq", "ctr"];
const SHORTCUT_POSITIVE_PRIOR = 1;
const SHORTCUT_NEGATIVE_PRIOR = 1000;
const SHORTCUT_TAU = 100;
const SMART_TELEM = false;
const MIN_EXP_IMPRESSIONS = 1000;
const MIN_EXP_CLICKS = 10;

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BasePromiseWorker: "resource://gre/modules/PromiseWorker.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  PersistentCache: "resource://newtab/lib/PersistentCache.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
});

import { sortKeysValues } from "resource://newtab/lib/SmartShortcutsRanker/ThomSample.mjs";

/**
 * Gates exploration-related runtime config until local shortcut engagement
 * reaches the configured click and impression thresholds.
 *
 * @param {object} runtimeConfig
 * @param {object} prefs
 * @param {object} engagement
 * @returns {object}
 */
export function gateRuntimeConfigByEngagement(
  runtimeConfig = {},
  prefs = {},
  engagement = {}
) {
  if (!runtimeConfig?.weights && !Object.hasOwn(runtimeConfig, "eta")) {
    return runtimeConfig;
  }
  // get recent engagement totals
  const totalImpressions = (engagement.impressions ?? []).reduce(
    (sum, value) => sum + (Number.isFinite(value) ? value : 0),
    0
  );
  const totalClicks = (engagement.clicks ?? []).reduce(
    (sum, value) => sum + (Number.isFinite(value) ? value : 0),
    0
  );
  // use nimbus prefs first and fallback to local
  const minImpressions = prefs.min_exp_impressions ?? MIN_EXP_IMPRESSIONS;
  const minClicks = prefs.min_exp_clicks ?? MIN_EXP_CLICKS;

  // if adequate engagement, change nothing
  if (totalImpressions >= minImpressions && totalClicks >= minClicks) {
    return runtimeConfig;
  }
  // copy the input configs to prepare for mutation
  const gatedConfig = { ...runtimeConfig };

  // zero out thompson sampling weight
  if (gatedConfig.weights) {
    gatedConfig.weights = { ...gatedConfig.weights };
    if (gatedConfig.weights?.thom) {
      gatedConfig.weights.thom = 0;
    }
  }

  // zero out learning rate
  if (Object.hasOwn(gatedConfig, "eta")) {
    gatedConfig.eta = 0;
  }

  return gatedConfig;
}
function smartshortcutsEnabled(prefValues) {
  // first check nimbus pref, fall back to local pref
  const experimentVariable =
    prefValues?.trainhopConfig?.smartShortcuts?.enabled;
  if (typeof experimentVariable === "boolean") {
    return experimentVariable;
  }
  return !!prefValues?.[PREF_SYSTEM_SHORTCUTS_PERSONALIZATION];
}

// helper for lowering precision of numbers, save space in telemetry
// longest string i can come up with out of this function:
//               -0.000009999 which is 12 characters
export const roundNum = (x, sig = 4, eps = 1e-6) => {
  if (typeof x !== "number" || !isFinite(x)) {
    return x;
  }

  // clip very small absolute values to zero
  if (Math.abs(x) < eps) {
    return 0;
  }

  const n = Number(x.toPrecision(sig));

  // normalize -0 to 0
  return Object.is(n, -0) ? 0 : n;
};

export async function getOpenTabURLsFromSessionLive() {
  // Ensure SessionStore is ready (important at early startup)
  if (lazy.SessionStore.promiseInitialized) {
    await lazy.SessionStore.promiseInitialized;
  }
  const stateJSON = lazy.SessionStore.getBrowserState(); // sync string
  const state = JSON.parse(stateJSON); // { windows: [...] }

  const urls = [];
  for (const win of state?.windows ?? []) {
    for (const tab of win?.tabs ?? []) {
      const i = Math.max(0, (tab.index ?? 1) - 1); // current entry is 1-based
      const entry = tab.entries?.[i] ?? tab.entries?.[tab.entries.length - 1];
      const url = entry?.url;
      if (url) {
        urls.push(url);
      }
    }
  }
  return urls;
}

export async function getOpenTabsWithPlacesFromSessionLive() {
  const urls = await getOpenTabURLsFromSessionLive();
  const out = [];
  for (const url of urls) {
    let guid = null;
    if (url.startsWith("http")) {
      try {
        guid = (await lazy.PlacesUtils.history.fetch(url))?.guid ?? null;
      } catch {}
    }
    out.push({ url, guid });
  }
  return out;
}

/**
 * For each input places GUID, report if it is currently open
 * note there is a < 30 second delay between a guid opening and this function
 * registering that change
 *
 * @param {string[]} guids Array of guid stirngs
 * @returns {Promise<object>} Map of guid -> is open
 */
export async function getIsOpen(guids, isStartup) {
  if (!isStartup?.isStartup) {
    // Grab all currently open tabs with GUIDs
    const openTabs = await getOpenTabsWithPlacesFromSessionLive();

    // Build a Set of GUIDs for fast lookup
    const openGuids = new Set(
      openTabs.map(t => t.guid).filter(Boolean) // skip nulls
    );

    // Map each input guid to 1/0
    const result = {};
    for (const g of guids) {
      result[g] = openGuids.has(g) ? 1 : 0;
    }
    return result;
  }

  // During startup: just return all 0s
  const result = {};
  for (const g of guids) {
    result[g] = 0;
  }
  return result;
}

/**
 * For each input places GUID, report the total visits
 *
 * @param {object[]} topsites Array of objects with a `guid` field (moz_places.guid)
 * @param {string} [placesTable='moz_places'] Table name for places
 * @returns {Promise<object>} Map of guid -> visit total
 */
export async function fetchVisitCountsByGuid(topsites, placeTable) {
  if (!topsites?.length) {
    return {};
  }

  const guidList = topsites.map(site => site.guid);

  // Safely quote each guid for VALUES(), escaping single quotes
  const values = guidList
    .map(guid => `('${String(guid).replace(/'/g, "''")}')`)
    .join(", ");

  const sql = `
    WITH input(guid) AS (VALUES ${values})
    SELECT i.guid, COALESCE(p.visit_count, 0) AS visit_count
    FROM input i
    LEFT JOIN ${placeTable} p ON p.guid = i.guid
    ORDER BY i.guid;
  `;

  const { activityStreamProvider } = lazy.NewTabUtils;
  const rows = await activityStreamProvider.executePlacesQuery(sql);
  const out = Object.create(null);
  for (const [guid, visit_count] of rows) {
    out[guid] = visit_count;
  }
  return out; // { guid: visit_count }
}

/**
 * For each input places GUID, return the time and type of the last 10 visits
 * this is a replication of what is done during frecency calculation
 *
 * @param {object[]} topsites Array of objects with a `guid` field (moz_places.guid)
 * @param {string} [table='moz_historyvisits'] Table name for history
 * @param {string} [placeTable='moz_places'] Table name for places
 * @returns {Promise<object>} Map of guid -> {visit_time, visit_type}
 */
export async function fetchLast10VisitsByGuid(topsites, table, placeTable) {
  if (!topsites?.length) {
    return {};
  }

  const guids = topsites.map(s => String(s.guid));
  const valuesClause = guids
    .map(g => `('${g.replace(/'/g, "''")}')`)
    .join(", ");

  // Mirrors Firefox's pattern:
  // SELECT ... FROM moz_historyvisits WHERE place_id = h.id ORDER BY visit_date DESC LIMIT 10
  const sql = `
    WITH input(guid) AS (VALUES ${valuesClause})
    SELECT
      i.guid,
      v.visit_date AS visit_date_us,
      v.visit_type
    FROM input i
    JOIN ${placeTable} h ON h.guid = i.guid
    JOIN ${table} v ON v.place_id = h.id
    WHERE v.id IN (
      SELECT vv.id
      FROM ${table} vv
      WHERE vv.place_id = h.id
      ORDER BY vv.visit_date DESC
      LIMIT 10  /* limit to the last 10 visits */
    )
    ORDER BY i.guid, v.visit_date DESC;
  `;

  const { activityStreamProvider } = lazy.NewTabUtils;
  const rows = await activityStreamProvider.executePlacesQuery(sql);
  // `guids` is the array you queried with
  // `rows` is the result from runQuery(sql) -> Array<[guid, visit_date_us, visit_type]>

  const out = Object.fromEntries(guids.map(g => [g, []]));

  for (const [guid, visit_date_us, visit_type] of rows) {
    // rows are already ordered by guid, visit_date DESC per your SQL
    out[guid].push({ visit_date_us, visit_type });
  }

  // `out` is: { [guid]: [ { visit_date_us, visit_type }, ... ] }
  return out;
}

/**
 * For each input places GUID, report whether it is bookmarked.
 * Walks guid -> moz_places.id -> moz_bookmarks.fk (type=1).
 *
 * @param {object[]} topsites Array of objects with a `guid` field (moz_places.guid)
 * @param {string} [placesTable='moz_places'] Table name for places
 * @param {string} [bookmarksTable='moz_bookmarks'] Table name for bookmarks
 * @returns {Promise<object>} Map of guid -> boolean (true if bookmarked)
 */
export async function fetchBookmarkedFlags(
  topsites,
  bookmarksTable = "moz_bookmarks",
  placesTable = "moz_places"
) {
  if (!topsites.length) {
    return {};
  }

  const guidList = topsites.map(site => site.guid);

  // Safely quote each guid for VALUES(), escaping single quotes
  const valuesClause = guidList
    .map(guid => `('${String(guid).replace(/'/g, "''")}')`)
    .join(", ");

  // We LEFT JOIN so every input guid appears once, even if not found/bookmarked.
  const sql = `
    WITH input_keys(guid) AS (
      VALUES ${valuesClause}
    )
    SELECT
      ik.guid AS key,
      COALESCE(COUNT(b.id), 0) AS bookmark_count
    FROM input_keys AS ik
    LEFT JOIN ${placesTable} AS p
      ON p.guid = ik.guid
    LEFT JOIN ${bookmarksTable} AS b
      ON b.fk = p.id
      AND b.type = 1            -- only actual bookmark items
    GROUP BY ik.guid
    ORDER BY ik.guid;
  `;

  const { activityStreamProvider } = lazy.NewTabUtils;
  const rows = await activityStreamProvider.executePlacesQuery(sql);

  // rows: [key, bookmark_count]
  const result = {};
  for (const [key, count] of rows) {
    if (key) {
      result[key] = count > 0;
    }
  }

  // Ensure every requested guid is present (defensive)
  for (const site of topsites) {
    if (!(site.guid in result)) {
      result[site.guid] = false;
    }
  }

  return result;
}

/**
 * Get histogram of all site visits over day-of-week
 *
 * @param {object[]} topsites Array of topsites objects
 * @param {string} table Table to query
 * @param {string} placeTable Table to map guid->place_id
 * @returns {result: object} Dictionary of histograms of day-of-week site opens
 */
export async function fetchDailyVisitsSpecific(topsites, table, placeTable) {
  if (!topsites.length) {
    return {};
  }
  const guidList = topsites.map(site => site.guid);

  const valuesClause = guidList
    .map(guid => `('${guid.replace(/'/g, "''")}')`)
    .join(", ");

  const sql = `
      WITH input_keys(guid) AS (
        VALUES ${valuesClause}
      ),
      place_ids AS (
        SELECT input_keys.guid, pTable.id AS place_id
        FROM input_keys
        LEFT JOIN ${placeTable} as pTable ON pTable.guid = input_keys.guid
      )
      SELECT
        place_ids.guid AS key,
        CAST(strftime('%w', dTable.visit_date / 1e6, 'unixepoch') AS INTEGER) AS day_of_week,
        COUNT(dTable.visit_date) AS visit_count
      FROM place_ids
      LEFT JOIN ${table} as dTable
        ON dTable.place_id = place_ids.place_id
        AND dTable.visit_date >= 1e6 * strftime('%s', 'now', '-2 months')
      GROUP BY place_ids.guid, day_of_week
      ORDER BY place_ids.guid, day_of_week;
      `;
  const { activityStreamProvider } = lazy.NewTabUtils;
  const rows = await activityStreamProvider.executePlacesQuery(sql);
  const histograms = {};
  for (const [key, day_of_week, visit_count] of rows) {
    if (!histograms[key]) {
      histograms[key] = Array(7).fill(0);
    }
    if (day_of_week !== null) {
      histograms[key][day_of_week] = visit_count;
    }
  }
  for (const site of topsites) {
    if (!histograms[site.guid]) {
      histograms[site.guid] = Array(7).fill(0);
    }
  }
  return histograms;
}

/**
 * Get histogram of all site visits over day-of-week
 *
 * @param {string} table Table to query
 * @returns {number[]} Histogram of day-of-week site opens
 */
export async function fetchDailyVisitsAll(table) {
  const sql = `
    SELECT
      CAST(strftime('%w', ${table}.visit_date / 1e6, 'unixepoch') AS INTEGER) AS day_of_week,
      COUNT(*) AS visit_count
    FROM ${table}
    WHERE ${table}.visit_date >= 1e6 * strftime('%s', 'now', '-6 months')
    GROUP BY day_of_week
    ORDER BY day_of_week;
  `;

  const { activityStreamProvider } = lazy.NewTabUtils;
  const rows = await activityStreamProvider.executePlacesQuery(sql);
  const histogram = Array(7).fill(0);
  for (const [day_of_week, visit_count] of rows) {
    if (day_of_week !== null) {
      histogram[day_of_week] = visit_count;
    }
  }
  return histogram;
}
/**
 * Get histogram of all site visits over hour-of-day
 *
 * @param {object[]} topsites Array of topsites objects
 * @param {string} table Table to query
 * @param {string} placeTable Table to map guid->place_id
 * @returns {object} Dictionary of histograms of hour-of-day site opens
 */
export async function fetchHourlyVisitsSpecific(topsites, table, placeTable) {
  if (!topsites.length) {
    return {};
  }
  const guidList = topsites.map(site => site.guid);

  const valuesClause = guidList
    .map(guid => `('${guid.replace(/'/g, "''")}')`)
    .join(", ");

  const sql = `
      WITH input_keys(guid) AS (
        VALUES ${valuesClause}
      ),
      place_ids AS (
        SELECT input_keys.guid, pTable.id AS place_id
        FROM input_keys
        LEFT JOIN ${placeTable} as pTable ON pTable.guid = input_keys.guid
      )
      SELECT
        place_ids.guid AS key,
        CAST(strftime('%H', hTable.visit_date / 1e6, 'unixepoch') AS INTEGER) AS hour_of_day,
        COUNT(hTable.visit_date) AS visit_count
      FROM place_ids
      LEFT JOIN ${table} as hTable
        ON hTable.place_id = place_ids.place_id
        AND hTable.visit_date >= 1e6 * strftime('%s', 'now', '-2 months')
      GROUP BY place_ids.guid, hour_of_day
      ORDER BY place_ids.guid, hour_of_day;
      `;
  const { activityStreamProvider } = lazy.NewTabUtils;
  const rows = await activityStreamProvider.executePlacesQuery(sql);
  const histograms = {};
  for (const [key, hour_of_day, visit_count] of rows) {
    if (!histograms[key]) {
      histograms[key] = Array(24).fill(0);
    }
    if (hour_of_day !== null) {
      histograms[key][hour_of_day] = visit_count;
    }
  }
  for (const site of topsites) {
    if (!histograms[site.guid]) {
      histograms[site.guid] = Array(24).fill(0);
    }
  }
  return histograms;
}

/**
 * Get histogram of all site visits over hour-of-day
 *
 * @param {string} table Table to query
 * @returns {number[]} Histogram of hour-of-day site opens
 */
export async function fetchHourlyVisitsAll(table) {
  const sql = `
    SELECT
      CAST(strftime('%H', ${table}.visit_date / 1e6, 'unixepoch') AS INTEGER) AS hour_of_day,
      COUNT(*) AS visit_count
    FROM ${table}
    WHERE ${table}.visit_date >= 1e6 * strftime('%s', 'now', '-6 months')
    GROUP BY hour_of_day
    ORDER BY hour_of_day;
  `;

  const { activityStreamProvider } = lazy.NewTabUtils;
  const rows = await activityStreamProvider.executePlacesQuery(sql);
  const histogram = Array(24).fill(0);
  for (const [hour_of_day, visit_count] of rows) {
    if (hour_of_day !== null) {
      histogram[hour_of_day] = visit_count;
    }
  }
  return histogram;
}

/**
 * Build weights object only for the requested features.
 *
 * @param {object} prefValues - contains trainhopConfig.smartShortcuts
 * @param {string[]} features - e.g. ["thom","frec"] (bias optional)
 */
function initShortcutWeights(prefValues, features) {
  const cfg = prefValues?.trainhopConfig?.smartShortcuts ?? {}; // remove second config
  const out = {};

  for (const f of features) {
    const meta = FEATURE_META[f];
    if (!meta) {
      continue;
    } // unknown feature: skip

    const raw = cfg[meta.pref];
    const percent = Number.isFinite(raw) ? raw : meta.def;
    out[f] = percent / 100;
  }

  return out;
}
/**
 * Check for bad numerical weights or changes in init config
 *
 * @param {object} all_weights Dictionary of weights from cache
 * @param {string[]} features List of features to have weights
 * @returns {object[]} current weights and the init weights
 */
function checkWeights(all_weights, features) {
  if (
    !all_weights.current ||
    !all_weights.old_init ||
    Object.keys(all_weights.current).length === 0
  ) {
    return [all_weights.new_init, all_weights.new_init];
  }
  for (const fkey of features) {
    if (
      !Number.isFinite(all_weights.current[fkey]) ||
      all_weights.old_init[fkey] !== all_weights.new_init[fkey]
    ) {
      return [all_weights.new_init, all_weights.new_init];
    }
  }
  return [all_weights.current, all_weights.old_init];
}

/**
 * Get clicks and impressions for sites in topsites array
 *
 * @param {object[]} topsites Array of topsites objects
 * @param {string} table Table for shortcuts interactions
 * @param {string} placeTable moz_places table
 * @returns {clicks: [number[], impressions: number[]]} Clicks and impressions for each site in topsites
 */
async function fetchShortcutInteractions(topsites, table, placeTable) {
  if (!topsites.length) {
    // Return empty clicks and impressions arrays
    return [[], []];
  }

  const guidList = topsites.map(site => site.guid);

  const valuesClause = guidList
    .map(guid => `('${guid.replace(/'/g, "''")}')`)
    .join(", ");

  // Only get records in the last 2 months!
  // Join no places table to map guid to place_id
  const sql = `
    WITH input_keys(guid) AS (
      VALUES ${valuesClause}
    ),
    place_ids AS (
      SELECT input_keys.guid, ${placeTable}.id AS place_id
      FROM input_keys
      JOIN ${placeTable} ON ${placeTable}.guid = input_keys.guid
    )
    SELECT
      place_ids.guid AS key,
      COALESCE(SUM(${table}.event_type), 0) AS total_clicks,
      COALESCE(SUM(1 - ${table}.event_type), 0) AS total_impressions
    FROM place_ids
    LEFT JOIN ${table} ON ${table}.place_id = place_ids.place_id
      AND ${table}.timestamp_s >= strftime('%s', 'now', '-2 months')
    GROUP BY place_ids.guid;
  `;

  const { activityStreamProvider } = lazy.NewTabUtils;
  const interactions = await activityStreamProvider.executePlacesQuery(sql);
  const interactionMap = new Map(
    interactions.map(row => {
      // Destructure the array into variables
      const [key, total_clicks, total_impressions] = row;
      return [key, { clicks: total_clicks, impressions: total_impressions }];
    })
  );

  // Rebuild aligned arrays in same order as input
  const clicks = guidList.map(guid =>
    interactionMap.has(guid) ? interactionMap.get(guid).clicks : 0
  );

  const impressions = guidList.map(guid =>
    interactionMap.has(guid) ? interactionMap.get(guid).impressions : 0
  );
  return [clicks, impressions];
}

export class RankShortcutsProvider {
  constructor() {
    this.sc_obj = new lazy.PersistentCache("shortcut_cache", true);
  }
  get rankShortcutsWorker() {
    if (!this._rankShortcutsWorker) {
      this._rankShortcutsWorker = new lazy.BasePromiseWorker(
        "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.worker.mjs",
        { type: "module" }
      );
    }
    return this._rankShortcutsWorker;
  }

  /**
   * Get hourly seasonality priors and per-site histograms.
   *
   * @param {Array<object>} topsites
   * @param {object} shortcut_cache
   * @param {object} isStartup stores the boolean isStartup
   * @returns {Promise<{pvec: number[]|null, hists: any}>}
   */
  async getHourlySeasonalityData(topsites, shortcut_cache, isStartup) {
    const cache = (shortcut_cache && shortcut_cache.hourly_seasonality) || null;
    const startup = isStartup.isStartup;

    let hourly_prob = null;

    const expired =
      cache &&
      Date.now() - (cache.timestamp || 0) > BASE_SEASONALITY_CACHE_EXPIRATION;
    const missing = !cache || !cache.pvec;

    if (!startup && (missing || expired)) {
      const all_hourly_hist = await fetchHourlyVisitsAll(VISITS_TABLE);
      hourly_prob = await this.rankShortcutsWorker.post("sumNorm", [
        all_hourly_hist,
      ]);
      // persist fresh prior
      await this.sc_obj.set("hourly_seasonality", {
        pvec: hourly_prob,
        timestamp: Date.now(),
      });
    } else {
      // safe read with optional chaining + null fallback
      hourly_prob = cache?.pvec ?? null;
    }

    // Per-topsite histograms are needed regardless
    const hourly_hists = await fetchHourlyVisitsSpecific(
      topsites,
      VISITS_TABLE,
      PLACES_TABLE
    );

    return { pvec: hourly_prob, hists: hourly_hists };
  }

  /**
   * Get daily seasonality priors and per-site histograms.
   *
   * @param {Array<object>} topsites
   * @param {object} shortcut_cache
   * @param {object} isStartup stores the boolean isStartup
   * @returns {Promise<{pvec: number[]|null, hists: any}>}
   */
  async getDailySeasonalityData(topsites, shortcut_cache, isStartup) {
    const cache = shortcut_cache?.daily_seasonality ?? null;
    const startup = isStartup.isStartup;

    let daily_prob = null;

    const expired =
      cache &&
      Date.now() - (cache.timestamp || 0) > BASE_SEASONALITY_CACHE_EXPIRATION;
    const missing = !cache || !cache.pvec;

    if (!startup && (missing || expired)) {
      const all_daily_hist = await fetchDailyVisitsAll(VISITS_TABLE);
      daily_prob = await this.rankShortcutsWorker.post("sumNorm", [
        all_daily_hist,
      ]);
      // persist fresh prior
      await this.sc_obj.set("daily_seasonality", {
        pvec: daily_prob,
        timestamp: Date.now(),
      });
    } else {
      daily_prob = cache?.pvec ?? null;
    }

    // Per-topsite histograms are needed regardless
    const daily_hists = await fetchDailyVisitsSpecific(
      topsites,
      VISITS_TABLE,
      PLACES_TABLE
    );

    return { pvec: daily_prob, hists: daily_hists };
  }

  /**
   * Check the shortcut interaction table for new events since
   * the last time we updated the model weights
   *
   * @param {object} cahce_data shortcut cache
   * @param {string} table Shortcuts interaction table
   * @param {string} placeTable moz_places table
   * @returns {Promise<object>} Map of guid -> clicks and impression counts
   */
  async getLatestInteractions(cache_data, table, placeTable = "moz_places") {
    const now_s = Math.floor(Date.now() / 1000);
    let tlu = Number(cache_data.time_last_update ?? 0);
    if (tlu > 1e11) {
      tlu = Math.floor(tlu / 1000);
    } // ms -> s
    const since = Math.max(tlu, now_s - 24 * 60 * 60);

    const { activityStreamProvider } = lazy.NewTabUtils;

    const rows = await activityStreamProvider.executePlacesQuery(
      `
      SELECT
          p.guid AS guid,
          SUM(CASE WHEN e.event_type = 1 THEN 1 ELSE 0 END) AS clicks,
          SUM(CASE WHEN e.event_type = 0 THEN 1 ELSE 0 END) AS impressions
        FROM ${table} e
        JOIN ${placeTable} p ON p.id = e.place_id
        WHERE e.timestamp_s >= ${since}
        GROUP BY p.guid  
      `
    );

    const dict = Object.create(null);

    for (const r of Array.isArray(rows) ? rows : (rows ?? [])) {
      const guid = r.guid ?? (Array.isArray(r) ? r[0] : undefined);
      if (!guid) {
        continue;
      }
      const clicks = Number(r.clicks ?? (Array.isArray(r) ? r[1] : 0)) || 0;
      const impressions =
        Number(r.impressions ?? (Array.isArray(r) ? r[2] : 0)) || 0;
      dict[guid] = { clicks, impressions };
    }

    await this.sc_obj.set("time_last_update", now_s);

    return dict;
  }

  /**
   * Get "frecency" features: frequency, recency, re-frecency, unique days visited
   *
   * @param {Array<object>} withGuid topsites we are building features for
   * @returns {Promise<{}>} guid -> rece, freq, and refre features
   */
  async fetchRefreFeatures(withGuid) {
    const raw_frec = await fetchLast10VisitsByGuid(
      withGuid,
      VISITS_TABLE,
      PLACES_TABLE
    );
    const visit_totals = await fetchVisitCountsByGuid(withGuid, PLACES_TABLE);
    const output = await this.rankShortcutsWorker.post(
      "buildFrecencyFeatures",
      [raw_frec, visit_totals]
    );

    return output;
  }

  /**
  /**
   * Smart Shortcuts ranking main call
   *
   * @param {Array<object>} topsites
   * @param {object} prefValues
   * @param {object} isStartup stores the boolean isStartup
   * @returns {Promise<{}>} topsites reordered
   */
  async rankTopSites(topsites, prefValues, isStartup) {
    if (!smartshortcutsEnabled(prefValues)) {
      return topsites;
    }
    // get our feature set
    const features =
      prefValues.trainhopConfig?.smartShortcuts?.features ?? FEATURES;

    // split topsites into two arrays, we only rank those with guid
    const [withGuid, withoutGuid] = topsites.reduce(
      ([withG, withoutG], site) => {
        if (site.guid && typeof site.guid === "string") {
          withG.push(site);
        } else {
          withoutG.push(site);
        }
        return [withG, withoutG];
      },
      [[], []]
    );

    // query for interactions, sql cant be on promise
    // always do this but only used for thompson and ctr
    const [clicks, impressions] = await fetchShortcutInteractions(
      withGuid,
      SHORTCUT_TABLE,
      PLACES_TABLE
    );

    // cache stores weights and the last feature values used to produce ranking
    // PersistentCache r/w cant be on promise
    const sc_cache = await this.sc_obj.get();

    // check for bad weights (numerical) or change in init configs
    let [weights, init_weights] = checkWeights(
      {
        current: sc_cache.weights,
        new_init: initShortcutWeights(prefValues, features),
        old_init: sc_cache.init_weights,
      },
      features
    );

    // update our weights
    const latest_interaction_data = await this.getLatestInteractions(
      sc_cache,
      SHORTCUT_TABLE
    );
    const engagement = { clicks, impressions };
    const learningConfig = gateRuntimeConfigByEngagement(
      {
        eta: (prefValues.trainhopConfig?.smartShortcuts?.eta ?? ETA) / 10000,
      },
      prefValues.trainhopConfig?.smartShortcuts,
      engagement
    );

    weights = await this.rankShortcutsWorker.post("updateWeights", [
      {
        data: latest_interaction_data,
        scores: sc_cache.score_map,
        features,
        weights,
        eta: learningConfig.eta,
        click_bonus:
          (prefValues.trainhopConfig?.smartShortcuts?.click_bonus ??
            CLICK_BONUS) / 10,
      },
    ]);

    const rankingConfig = gateRuntimeConfigByEngagement(
      { weights },
      prefValues.trainhopConfig?.smartShortcuts,
      engagement
    );
    const rankingWeights = rankingConfig.weights;

    // write the weights and init... sometimes redundant
    await this.sc_obj.set("weights", weights);
    await this.sc_obj.set("init_weights", init_weights);

    // feature data
    const hourly_seasonality = features?.includes?.("hour")
      ? await this.getHourlySeasonalityData(withGuid, sc_cache, isStartup)
      : null;
    const daily_seasonality = features?.includes?.("daily")
      ? await this.getDailySeasonalityData(withGuid, sc_cache, isStartup)
      : null;
    const bmark_scores = features?.includes?.("bmark")
      ? await fetchBookmarkedFlags(withGuid, BOOKMARK_TABLE, PLACES_TABLE)
      : null;
    const refrec_scores = ["rece", "freq", "refre", "unid"].some(f =>
      features.includes(f)
    )
      ? await this.fetchRefreFeatures(withGuid, features)
      : { rece: null, freq: null, refre: null, unid: null };
    const open_scores = features?.includes?.("open")
      ? await getIsOpen(
          withGuid.map(t => t.guid),
          isStartup
        )
      : null;
    // call to the promise worker to do the ranking
    const frecency_scores = withGuid.map(t => t.frecency);
    const output = await this.rankShortcutsWorker.post(
      "weightedSampleTopSites",
      [
        {
          features,
          alpha:
            prefValues.trainhopConfig?.smartShortcuts?.positive_prior ??
            SHORTCUT_POSITIVE_PRIOR,
          beta:
            prefValues.trainhopConfig?.smartShortcuts?.negative_prior ??
            SHORTCUT_NEGATIVE_PRIOR,
          tau: prefValues.trainhopConfig?.smartShortcuts?.tau ?? SHORTCUT_TAU,
          guid: withGuid.map(t => t.guid),
          clicks,
          impressions,
          norms:
            sc_cache.norms ??
            Object.fromEntries(features.map(key => [key, null])),
          weights: rankingWeights,
          frecency: frecency_scores,
          hourly_seasonality,
          daily_seasonality,
          bmark_scores,
          open_scores,
          rece_scores: refrec_scores?.rece,
          freq_scores: refrec_scores?.freq,
          refre_scores: refrec_scores?.refre,
          unid_scores: refrec_scores?.unid,
        },
      ]
    );
    // update the cache
    await this.sc_obj.set("norms", output.norms);
    await this.sc_obj.set("score_map", output.score_map);

    // final score for ranking as an array
    let final_scores = withGuid.map(g => output.score_map[g.guid].final);
    //catch nan errors
    if (final_scores.some(x => Number.isNaN(x))) {
      final_scores = frecency_scores;
    }

    // sort by scores
    const sortedSitesVals = sortKeysValues(final_scores, withGuid);
    let [sortedSites] = sortedSitesVals;

    // grab topsites without guid
    const combined = sortedSites.concat(withoutGuid);

    // tack weights and scores so they can pass through to telemetry
    if (prefValues?.trainhopConfig?.smartShortcuts?.telem || SMART_TELEM) {
      // store a version of weights that is rounded
      const roundWeights = Object.fromEntries(
        Object.entries(rankingWeights ?? {}).map(([key, v]) => [
          key,
          typeof v === "number" && isFinite(v) ? roundNum(v) : (v ?? null),
        ])
      );
      // do the tacking
      combined.forEach(s => {
        const raw = output?.score_map?.[s.guid];
        s.scores =
          raw && typeof raw === "object"
            ? Object.fromEntries(
                Object.entries(raw).map(([k, v]) => [
                  k,
                  typeof v === "number" && isFinite(v)
                    ? roundNum(v)
                    : (v ?? null),
                ])
              )
            : null;
        s.weights = roundWeights;
      });
    }
    return combined;
  }
}
