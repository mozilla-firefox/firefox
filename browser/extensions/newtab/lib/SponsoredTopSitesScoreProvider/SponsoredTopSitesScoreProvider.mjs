/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PersistentCache: "resource://newtab/lib/PersistentCache.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

const PREF_LOG =
  "browser.newtabpage.activity-stream.sponsoredTopSitesScoring.log";

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "SponsoredTopSitesScoreProvider",
    // @backward-compat { version 162 }
    // Older versions ignore logModule and name the module after the prefix.
    // Remove prefix: once 162 reaches Release.
    prefix: "SponsoredTopSitesScoreProvider",
    maxLogLevel: Services.prefs.getBoolPref(PREF_LOG, false) ? "Debug" : "Warn",
  });
});

// Microseconds per day to match on visit_date
const US_PER_DAY = 24 * 60 * 60 * 1000 * 1000;

// The minimum time between score recomputes in milliseconds (24 hours).
const RELEASE_INTERVAL_MS = 24 * 60 * 60 * 1000;
const REMOTE_SETTINGS_COLLECTION = "newtab-sponsored-topsites-scoring";

// The prefix for flags to check when attempting to load the remote settings record.
const FLAG_PREFIX = "sponsored_top_site_scoring";

// Upper bound on config.privacy_parameter to limit the total privacy loss
const MAX_PRIVACY_PARAMETER = 1;
const CACHE_KEY = "sponsored_topsites_scoring_cache";
const SCORES_CACHE_KEY = "scores";
const HISTORY_CACHE_KEY = "scoreHistory";

// The number of daily private scores stored for post-processing.
const HISTORY_SIZE = 30;

const PREF_LAST_RELEASE_TIME =
  "browser.newtabpage.activity-stream.sponsoredTopSitesScoring.lastReleaseTime";

// Places events that indicate browsing history was removed.
const PLACES_EVENTS = ["history-cleared", "page-removed"];

// Held while the scores are refreshed or cleared, so the two never interleave.
const SCORES_LOCK = "newtab-sponsored-topsites-scoring";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "LAST_RELEASE_TIME_PREF_VALUE",
  PREF_LAST_RELEASE_TIME,
  0
);

/**
 * Whether any sponsored top sites scoring flag is enabled.
 *
 * @param {?object} flags The adsBackend flags.
 * @returns {boolean} Whether a scoring flag is enabled.
 */
export function hasScoringFlag(flags) {
  return Object.keys(flags ?? {}).some(
    flag => flag.startsWith(FLAG_PREFIX) && flags[flag]
  );
}

export class _SponsoredTopSitesScoreProvider {
  _getFlags = null;
  _rs = null;
  _cache = null;
  _refreshPromise = null;
  _placesListener = null;

  /**
   * Starts the scoring. This creates the cache and Remote Settings client.
   *
   * @param {Function} getFlags Returns the current adsBackend flags for
   *  selecting a configuration record through remote settings.
   */
  init(getFlags) {
    lazy.logConsole.debug("Initializing");
    this._getFlags = getFlags;
    if (!this._cache) {
      this._cache = new lazy.PersistentCache(CACHE_KEY, false);
    }
    if (!this._rs) {
      this._rs = lazy.RemoteSettings(REMOTE_SETTINGS_COLLECTION);
    }
    if (!this._placesListener) {
      this._placesListener = events => this._onPlacesEvent(events);
      lazy.PlacesUtils.observers.addListener(
        PLACES_EVENTS,
        this._placesListener
      );
    }
  }

  /**
   * Stops the scoring. This releases the Remote Settings client. Persisted
   * scores stay in the cache for the next init.
   * Invoke this when scoring is disabled or the consumer is uninitializing.
   */
  uninit() {
    lazy.logConsole.debug("Uninitializing");
    if (this._placesListener) {
      lazy.PlacesUtils.observers.removeListener(
        PLACES_EVENTS,
        this._placesListener
      );
      this._placesListener = null;
    }
    this._rs = null;
    this._getFlags = null;
  }

  /**
   * Load any active config and update the cached scores. Clears existing
   * scores when no config exists. Callers must hold SCORES_LOCK.
   *
   * @returns {Promise<void>} Resolves once the cached scores are updated.
   */
  async _refreshScores() {
    // Return early if a full release interval has not elapsed.
    const now = Math.round(Date.now() / 1000);
    if (now - this._getLastReleaseTime() < RELEASE_INTERVAL_MS / 1000) {
      lazy.logConsole.debug("Refresh skipped until the next release interval");
      return;
    }

    const config = await this._loadConfig();
    if (!config) {
      const cachedScores = await this._cache.get(SCORES_CACHE_KEY);
      if (cachedScores && Object.keys(cachedScores).length) {
        await this._setScores({});
      }
      return;
    }

    const todayDay = Math.floor((Date.now() * 1000) / US_PER_DAY);
    const domainDayCounts = await this._getDomainDayCounts(config);
    const scores = this._sponsoredScores(config, domainDayCounts);
    const scoresWithDP = this._differentiallyPrivateScores(scores, config);
    await this._appendScoreHistory(todayDay, scoresWithDP);
    const scoreHistory = await this._getScoreHistory(todayDay);
    await this._setScores(this._postProcessScores(scoreHistory, config));
    this._setLastReleaseTime(now);
  }

  /**
   * The timestamp of the last release in seconds.
   *
   * @returns {number} The last release time in seconds.
   */
  _getLastReleaseTime() {
    return lazy.LAST_RELEASE_TIME_PREF_VALUE;
  }

  /**
   * Record the time of the last release.
   *
   * @param {number} seconds The release time in seconds since the epoch.
   */
  _setLastReleaseTime(seconds) {
    Services.prefs.setIntPref(PREF_LAST_RELEASE_TIME, seconds);
  }

  /**
   * Load the Remote Settings record with the matching id of the enabled flag.
   * The privacy_parameter, repeat_visit_weight, and normalization_cap fields
   * are stored as integer hundredths and are converted to their decimal
   * values, with privacy_parameter capped at MAX_PRIVACY_PARAMETER.
   *
   * @returns {Promise<?object>} The matching config or null when not found or
   *  invalid.
   */
  async _loadConfig() {
    const recordId = this._getRecordId();
    if (!recordId) {
      lazy.logConsole.debug("No config record is enabled");
      return null;
    }

    const records = await this._rs?.get();
    const record = records?.find(r => r.id === recordId);
    if (!record || !this._isValidConfig(record)) {
      lazy.logConsole.debug(`No valid config found for ${recordId}`);
      return null;
    }
    lazy.logConsole.debug(`Loaded config ${recordId}`);

    return {
      ...record,
      privacy_parameter: Math.min(
        record.privacy_parameter / 100,
        MAX_PRIVACY_PARAMETER
      ),
      repeat_visit_weight: record.repeat_visit_weight / 100,
      normalization_cap: record.normalization_cap / 100,
    };
  }

  /**
   * Check that the numeric fields of a Remote Settings record are integers
   * within a range that produces meaningful scores.
   *
   * @param {object} record The raw Remote Settings record.
   * @returns {boolean} Whether the record can be used for scoring.
   */
  _isValidConfig(record) {
    const isIntegerInRange = (value, min, max = Infinity) =>
      Number.isInteger(value) && value >= min && value <= max;

    return (
      isIntegerInRange(record.lookback_days, 1) &&
      isIntegerInRange(record.recency_halflife_days, 1) &&
      isIntegerInRange(record.trend_days, 1) &&
      isIntegerInRange(record.allowed_targets, 0) &&
      isIntegerInRange(record.decimals, 1, 4) &&
      isIntegerInRange(record.privacy_parameter, 1) &&
      isIntegerInRange(record.normalization_cap, 1) &&
      isIntegerInRange(record.repeat_visit_weight, 0, 99)
    );
  }

  /**
   * Gets the Remote Settings record id to load by checking for an
   * enabled flag with the matching prefix. At most one flag should be
   * enabled. Returns null if multiple matching flags are enabled.
   *
   * @returns {?string} The record id to load. null when no flag is
   * enabled or more than one enabled flag is found.
   */
  _getRecordId() {
    const flags = this._getFlags?.() ?? {};
    const enabled = Object.keys(flags).filter(
      flag => flag.startsWith(FLAG_PREFIX) && flags[flag]
    );
    return enabled.length === 1 ? enabled[0] : null;
  }

  /**
   * Persist the scores.
   *
   * @param {object} scores The scores to store.
   * @returns {Promise<void>} Resolves once the scores are persisted.
   */
  async _setScores(scores) {
    await this._cache.set(SCORES_CACHE_KEY, scores);
  }

  /**
   * Returns the currently calculated scores from the cache and starts a
   * background refresh to recompute the scores once a release interval has elapsed.
   *
   * @returns {Promise<object>} A map of scores.
   */
  async getScores() {
    const scores = (await this._cache?.get(SCORES_CACHE_KEY)) ?? {};
    if (this._rs && !this._refreshPromise) {
      this._refreshPromise = locks
        .request(SCORES_LOCK, () => this._refreshScores())
        .catch(e => lazy.logConsole.debug("Refresh failed", e))
        .finally(() => {
          this._refreshPromise = null;
        });
    }
    return scores;
  }

  /**
   * Clear the stored scores and score history, and restart the release
   * interval so that no scores are released until a full interval has passed.
   * Nothing is written when no scores have been stored or released.
   *
   * @returns {Promise<void>} Resolves once the data is cleared.
   */
  async clearData() {
    if (!this._cache) {
      return;
    }
    await locks.request(SCORES_LOCK, async () => {
      const data = await this._cache.get();
      const hasData =
        Object.keys(data[SCORES_CACHE_KEY] ?? {}).length ||
        data[HISTORY_CACHE_KEY]?.length;
      if (!hasData && !this._getLastReleaseTime()) {
        return;
      }

      lazy.logConsole.debug("Clearing scores and score history");
      this._setLastReleaseTime(Math.round(Date.now() / 1000));
      await this._cache.set(SCORES_CACHE_KEY, {});
      await this._cache.set(HISTORY_CACHE_KEY, []);
    });
  }

  /**
   * Clear the data when all history is cleared or the user removes any
   * history. Removals from periodic history expiration are ignored since the
   * user did not request them.
   *
   * @param {Array<PlacesHistoryCleared|PlacesVisitRemoved>} events The batched
   *  Places events.
   */
  _onPlacesEvent(events) {
    const historyRemoved = events.some(
      event =>
        event.type === "history-cleared" ||
        (event.type === "page-removed" &&
          /** @type {PlacesVisitRemoved} */ (event).reason ===
            PlacesVisitRemoved.REASON_DELETED)
    );
    if (historyRemoved) {
      this.clearData().catch(e =>
        lazy.logConsole.error("Clearing data failed", e)
      );
    }
  }

  /**
   * Sum visits to the configured target domains by domain and day.
   *
   * @param {object} config The active Remote Settings config.
   * @returns {Promise<Map<string, Map<number, number>>>} The visit count per
   *  configured domain, keyed by day, in the lookback window.
   */
  async _getDomainDayCounts(config) {
    const prefixMap = this._revHostPrefixMap(config);
    if (!prefixMap.size) {
      return new Map();
    }

    const todayDay = Math.floor((Date.now() * 1000) / US_PER_DAY);
    const sinceDay = todayDay - (config.lookback_days - 1);
    const query = `
      SELECT
        p.rev_host AS rev_host,
        CAST(v.visit_date / ${US_PER_DAY} AS INTEGER) AS day,
        COUNT(*) AS visits
      FROM moz_historyvisits v
      JOIN moz_places p ON p.id = v.place_id
      WHERE v.visit_date >= :since_date
        AND v.visit_type IN
        (${lazy.PlacesUtils.history.TRANSITIONS.LINK},
         ${lazy.PlacesUtils.history.TRANSITIONS.TYPED},
         ${lazy.PlacesUtils.history.TRANSITIONS.BOOKMARK})
      GROUP BY p.rev_host, day
    `;

    const rows =
      await lazy.NewTabUtils.activityStreamProvider.executePlacesQuery(query, {
        columns: ["rev_host", "day", "visits"],
        params: { since_date: sinceDay * US_PER_DAY },
      });

    // Filter domains and sum by day.
    const domainDayCounts = new Map();
    for (const row of rows) {
      const domain = this._findConfiguredDomain(row.rev_host, prefixMap);
      if (!domain) {
        continue;
      }

      const days = domainDayCounts.get(domain) ?? new Map();
      const dayOffset = row.day - todayDay;
      days.set(dayOffset, (days.get(dayOffset) ?? 0) + row.visits);
      domainDayCounts.set(domain, days);
    }

    return domainDayCounts;
  }

  /**
   * Score a domain from its daily visit counts. More recent visits are
   * weighted more heavily, and more visits in a day give a higher overall score
   * depending upon the repeat_visit_weight. The score is normalized to [0, 1].
   *
   * @param {Map<number, number>} dayCounts Visit count keyed by day offset
   *  relative to today (0 is today, -1 is yesterday, ...).
   * @param {object} config The active Remote Settings config.
   * @returns {number} The domain score.
   */
  _domainScore(dayCounts, config) {
    const decay = Math.LN2 / config.recency_halflife_days;

    let score = 0;
    for (const [day, visits] of dayCounts) {
      score +=
        Math.exp(day * decay) * (1 - config.repeat_visit_weight ** visits);
    }

    let normalization = 0;
    for (let j = 0; j < config.lookback_days; j++) {
      normalization += Math.exp(-j * decay);
    }

    return score / normalization;
  }

  /**
   * Score each sponsored target. Take the sum of scores of each domain and
   * divide by config.normalization_cap to account for different sized
   * list of domains between targets.
   *
   * @param {object} config The active Remote Settings config.
   * @param {Map<string, Map<number, number>>} domainDayCounts The visit count
   *  per configured domain, keyed by day offset, from _getDomainDayCounts.
   * @returns {object} A score in [0, 1] keyed by target.
   */
  _sponsoredScores(config, domainDayCounts) {
    const scores = {};
    for (const [target, domains] of Object.entries(config.targets ?? {})) {
      let sum = 0;
      for (const domain of domains ?? []) {
        const dayCounts = domainDayCounts.get(domain.toLowerCase());
        if (dayCounts) {
          sum += this._domainScore(dayCounts, config);
        }
      }
      scores[target] =
        Math.min(sum, config.normalization_cap) / config.normalization_cap;
    }
    return scores;
  }

  /**
   * Count the maximum number of sponsored targets associated to any single domain.
   *
   * @param {object} config The active Remote Settings config.
   * @returns {number} The largest number of targets sharing one domain
   */
  _getOverlap(config) {
    const targetCounts = new Map();
    for (const domains of Object.values(config.targets ?? {})) {
      for (const domain of new Set(domains?.map(d => d.toLowerCase()))) {
        targetCounts.set(domain, (targetCounts.get(domain) ?? 0) + 1);
      }
    }
    return Math.max(0, ...targetCounts.values());
  }

  /**
   * Add Laplace noise to each target score for differential privacy.
   * The total privacy composition over all scoring will be accounted for
   * with zCDP and applying some tight accounting tricks.
   * TODO: add a link to the zCDP accounting once it is published.
   *
   * These noised scores will not leave the client, and there will instead
   * be post-processing, clamping, and rounding to avoid floating point issues
   * with the privacy-preserving scores that are ultimately sent back to the server.
   *
   * @param {object} scores A score keyed by target, from _sponsoredScores.
   * @param {object} config The active Remote Settings config.
   * @returns {object} The noised score keyed by target.
   */
  _differentiallyPrivateScores(scores, config) {
    const scale =
      Math.sqrt(this._getOverlap(config)) /
      (config.normalization_cap *
        config.lookback_days *
        config.privacy_parameter);

    const privateScores = {};
    for (const [target, score] of Object.entries(scores)) {
      privateScores[target] = score + this._sampleLaplace(scale);
    }
    return privateScores;
  }

  /**
   * Draw a sample from a Laplace distribution centered at 0.
   *
   * @param {number} scale The scale (b) of the distribution.
   * @returns {number} The sample.
   */
  _sampleLaplace(scale) {
    const randomValues = new Uint32Array(1);
    crypto.getRandomValues(randomValues);
    const u = (randomValues[0] + 0.5) / 2 ** 32;
    return u < 0.5 ? scale * Math.log(2 * u) : -scale * Math.log(2 * (1 - u));
  }

  /**
   * Persist a day's private scores, keeping only the most recent HISTORY_SIZE
   * days. A day that is not newer than the latest stored day is ignored so
   * that a single day never gets more than one noise draw.
   *
   * @param {number} todayDay Today's day number.
   * @param {object} privateScores The scores from _differentiallyPrivateScores.
   * @returns {Promise<void>} Resolves once the history is persisted.
   */
  async _appendScoreHistory(todayDay, privateScores) {
    const scoreHistory = (await this._cache.get(HISTORY_CACHE_KEY)) ?? [];
    if (scoreHistory.at(-1)?.day >= todayDay) {
      return;
    }
    scoreHistory.push({ day: todayDay, scores: privateScores });
    await this._cache.set(HISTORY_CACHE_KEY, scoreHistory.slice(-HISTORY_SIZE));
  }

  /**
   * Get the stored private scores keyed by day offset.
   *
   * @param {number} todayDay Today's day number.
   * @returns {Promise<Map<number, object>>} The private scores keyed by day
   *  offset relative to today (0 is today, -1 is yesterday, ...).
   */
  async _getScoreHistory(todayDay) {
    const scoreHistory = (await this._cache.get(HISTORY_CACHE_KEY)) ?? [];
    return new Map(
      scoreHistory.map(({ day, scores }) => [day - todayDay, scores])
    );
  }

  /**
   * Fit today's score for each target to the trend from a least-squares linear
   * fit of its private scores over the last config.trend_days.
   *
   * @param {Map<number, object>} scoreHistory The private scores keyed by day
   *  offset, from _getScoreHistory.
   * @param {object} config The active Remote Settings config.
   * @returns {object} The predicted score keyed by target.
   */
  _trendFitting(scoreHistory, config) {
    const trendScores = {};
    for (const target of Object.keys(config.targets ?? {})) {
      /** @type {Array<[number, number]>} */
      const points = [];
      for (const [day, scores] of scoreHistory) {
        if (day > -config.trend_days && target in scores) {
          points.push([day, scores[target]]);
        }
      }
      if (points.length) {
        trendScores[target] = this._linearFitAtZero(points);
      }
    }
    return trendScores;
  }

  /**
   * Fit a least-squares line through the points and evaluate it at x = 0.
   *
   * @param {Array<[number, number]>} points Non-empty list of [x, y] pairs.
   * @returns {number} The fitted value at x = 0.
   */
  _linearFitAtZero(points) {
    const n = points.length;
    const meanX = points.reduce((sum, [x]) => sum + x, 0) / n;
    const meanY = points.reduce((sum, [, y]) => sum + y, 0) / n;

    let covariance = 0;
    let varianceX = 0;
    for (const [x, y] of points) {
      covariance += (x - meanX) * (y - meanY);
      varianceX += (x - meanX) ** 2;
    }
    if (!varianceX) {
      return meanY;
    }

    const slope = covariance / varianceX;
    return meanY - slope * meanX;
  }

  /**
   * Fit the trend for today's score, keep the config.allowed_targets highest
   * scoring targets, then clamp their scores to [0, 1] and round them down
   * to config.decimals decimal places.
   *
   * @param {Map<number, object>} scoreHistory The private scores keyed by day
   *  offset, from _getScoreHistory.
   * @param {object} config The active Remote Settings config.
   * @returns {object} The rounded score keyed by target, highest first.
   */
  _postProcessScores(scoreHistory, config) {
    const factor = 10 ** config.decimals;
    return Object.fromEntries(
      Object.entries(this._trendFitting(scoreHistory, config))
        .sort(([, a], [, b]) => b - a)
        .slice(0, config.allowed_targets)
        .map(([target, score]) => [
          target,
          Math.floor(Math.min(Math.max(score, 0), 1) * factor) / factor,
        ])
    );
  }

  /**
   * Build a map of rev_host prefix to configured domain.
   *
   * @param {object} config The active Remote Settings config.
   * @returns {Map<string, string>} Reversed host prefix to domain, e.g.
   *  "moc.elpmaxe." to "example.com".
   */
  _revHostPrefixMap(config) {
    const prefixMap = new Map();
    for (const domains of Object.values(config.targets ?? {})) {
      for (const domain of domains ?? []) {
        const loweredDomain = domain.toLowerCase();
        prefixMap.set(
          lazy.PlacesUtils.getReversedHost(new URL(`http://${loweredDomain}`)),
          loweredDomain
        );
      }
    }
    return prefixMap;
  }

  /**
   * Lookup a rev_host against the configured domains by checking each label
   * boundary. The most specific configured domain wins if multiple match.
   *
   * @param {?string} revHost The rev_host from moz_places, which is nullable.
   * @param {Map<string, string>} prefixMap rev_host prefix to domain.
   * @returns {?string} The matched domain, or null when none matches.
   */
  _findConfiguredDomain(revHost, prefixMap) {
    // Trim the empty last element due to the trailing dot in rev_host
    const labels = revHost?.split(".").slice(0, -1) ?? [];
    let prefix = "";
    let match = null;
    for (const label of labels) {
      prefix += `${label}.`;
      match = prefixMap.get(prefix) ?? match;
    }
    return match;
  }
}

export const SponsoredTopSitesScoreProvider =
  new _SponsoredTopSitesScoreProvider();
