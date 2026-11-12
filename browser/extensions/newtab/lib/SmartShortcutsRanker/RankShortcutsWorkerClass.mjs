/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { thompsonSampleSort } from "resource://newtab/lib/SmartShortcutsRanker/ThomSample.mjs"; //"ThomSample.mjs";

/**
 * Linear interpolation of values in histogram, wraps from end to beginning
 *
 * @param {number[]} hist Defines histogram of counts
 * @param {number} t Time/Index we are interpolating to
 * @returns {number} Normalized number
 */
export function interpolateWrappedHistogram(hist, t) {
  if (!hist.length) {
    return 0;
  }
  const n = hist.length;
  const lo = Math.floor(t) % n;
  const hi = (lo + 1) % n;
  const frac = t - Math.floor(t);

  // Adjust for negative t
  const loWrapped = (lo + n) % n;
  const hiWrapped = (hi + n) % n;

  return (1 - frac) * hist[loWrapped] + frac * hist[hiWrapped];
}
/**
 * Bayesian update of vec to reflect pvec weighted by tau
 *
 * @param {number[]} vec Array of values to update
 * @param {number[]} pvec Normalized array of values to reference
 * @param {number} tau Strength of the update
 * @returns {number[]} Normalized array
 */
export function bayesHist(vec, pvec, tau) {
  if (!pvec || !vec.length || vec.length !== pvec.length) {
    return vec;
  }
  const vsum = vec.reduce((a, b) => a + b, 0);
  if (vsum + tau === 0) {
    return vec;
  }
  const bhist = vec.map((v, i) => (v + tau * pvec[i]) / (vsum + tau));
  return bhist;
}

function getCurrentHourOfDay() {
  const now = new Date();
  return now.getHours() + now.getMinutes() / 60 + now.getSeconds() / 3600;
}

function getCurrentDayOfWeek() {
  const now = new Date();
  return now.getDay(); // returns an int not a float
}

/**
 * Compute average clicks and imps over items
 * Smooth those averages towards priors
 *
 * @param {number[]} clicks Array of clicks
 * @param {number[]} imps Array of impressions
 * @param {number} pos_rate prior for clicks
 * @param {number} neg_rate prior for impressions
 * @returns {number[]} smoothed click count and impression count
 */

/**
 * Normalize values in an array by the sum of the array
 *
 * @param {number[]} vec Array of values to normalize
 * @returns {number[]} Normalized array
 */
export function sumNorm(vec) {
  if (!vec.length) {
    return vec;
  }
  const vsum = vec.reduce((a, b) => a + b, 0);

  let normed = [];
  if (!Number.isFinite(vsum) || vsum !== 0) {
    normed = vec.map(v => v / vsum);
  } else {
    normed = vec;
  }
  return normed;
}

/**
 * this function normalizes all values in vals and updates
 * the running mean and variance in normobj
 * normobj stores info to calculate a running mean and variance
 * over a feature, this is stored in the shortcut cache
 *
 * @param {number[]} vals scores to normalize
 * @param {object} input_normobj Dictionary of storing info for running mean var
 * @returns {[number[], object]} normalized features and the updated object
 */
export function normUpdate(vals, input_normobj) {
  if (!vals.length) {
    return [vals, input_normobj];
  }
  let normobj = {};
  if (
    !input_normobj ||
    !Number.isFinite(input_normobj.mean) ||
    !Number.isFinite(input_normobj.var)
  ) {
    normobj = { beta: 1e-3, mean: vals[0], var: 1.0 };
  } else {
    normobj = { ...input_normobj };
  }
  let delta = 0;
  for (const v of vals) {
    delta = v - normobj.mean;
    normobj.mean += normobj.beta * delta;
    normobj.var =
      (1 - normobj.beta) * normobj.var + normobj.beta * delta * delta;
  }
  if (normobj.var <= 1e-8) {
    normobj.var = 1e-8;
  }

  const std = Math.sqrt(normobj.var);
  const new_vals = vals.map(v => (v - normobj.mean) / std);
  return [new_vals, normobj];
}

/**
 * Normalize a dictionary of {key: hist[]} using squared values and column-wise normalization.
 * Returns {key: normedHist[]} where each hist[j] is divided by sum_k hist_k[j]^2.
 *
 * @param {{[key: string]: number[]}} dict - A dictionary mapping keys to arrays of P(t|s) values.
 * @returns {{[key: string]: number[]}} New dictionary with normalized histograms (P(s|t)).
 */
export function normHistDict(dict) {
  const keys = Object.keys(dict);
  if (keys.length === 0) {
    return {};
  }

  const t = dict[keys[0]].length;

  // square all hist values to emphasize differences
  const squared = {};
  for (const [key, hist] of Object.entries(dict)) {
    squared[key] = hist.map(v => v * v);
  }

  // compute column-wise sums
  const colSums = Array(t).fill(0);
  for (let j = 0; j < t; j++) {
    for (const key of keys) {
      colSums[j] += squared[key][j];
    }
  }

  // normalize
  /** @type {{[key: string]: number[]}} */
  const normalized = {};
  for (const [key, row] of Object.entries(squared)) {
    normalized[key] = row.map((val, j) => (colSums[j] ? val / colSums[j] : 0));
  }

  return normalized;
}

/**
 * Compute linear combination of scores, weighted
 *
 * @param {object} scores Dictionary of scores
 * @param {object} weights Dictionary of weights
 * @returns {number} Linear combination of scores*weights
 */
export function computeLinearScore(scores, weights) {
  let final = 0;
  let score = 0;
  for (const [feature, weight] of Object.entries(weights)) {
    score = scores[feature] ?? 0;
    final += score * weight;
  }
  return final;
}

export function processSeasonality(guids, input, tau, curtime) {
  const { hists } = input;
  const { pvec } = input;
  // normalize new/rare site visits using general seasonlity to control against "fliers"
  const b_hists = Object.fromEntries(
    Object.entries(hists).map(([guid, hist]) => [
      guid,
      bayesHist(hist, pvec, tau),
    ])
  );
  // convert to P(time | site), prepare for bayes
  const timegivensite = Object.fromEntries(
    Object.entries(b_hists).map(([guid, hist]) => [guid, sumNorm(hist)])
  );
  // use bayes to convert to P(site | time)
  const sitegiventime = normHistDict(timegivensite);
  // interpolate P(site | time) to weight each site
  const weights = Object.fromEntries(
    Object.entries(sitegiventime).map(([guid, hist]) => [
      guid,
      interpolateWrappedHistogram(hist, curtime),
    ])
  );
  // make it an array
  const weightsarr = guids.map(key => weights[key]);
  // normalize to account for interpolation oddities
  const normweights = sumNorm(weightsarr);
  return normweights;
}

// Visit type codes in Places
const TYPE = {
  LINK: 1,
  TYPED: 2,
  BOOKMARK: 3,
  EMBED: 4,
  REDIRECT_PERM: 5,
  REDIRECT_TEMP: 6,
  DOWNLOAD: 7,
  FRAMED_LINK: 8,
  RELOAD: 9,
};

// default bonus map; copy from frecency
const TYPE_SCORE = {
  [TYPE.TYPED]: 200,
  [TYPE.LINK]: 100,
  [TYPE.BOOKMARK]: 75,
  [TYPE.RELOAD]: 0,
  [TYPE.REDIRECT_PERM]: 0,
  [TYPE.REDIRECT_TEMP]: 0,
  [TYPE.EMBED]: 0,
  [TYPE.FRAMED_LINK]: 0,
  [TYPE.DOWNLOAD]: 0,
};
/**
 * Build features that break apart frecency into:
 *        frequency, recency, re-frecency
 * frequency total_visits*F(visit_types)
 * recency is exponential decay of last 10 visits
 * re-frecency is dot_product(frequency,recency)
 *
 * difference between frecency and re-frecency is how the
 * recency calc is done, recency is exponential decay instead
 * of the frecency buckets for interpretability
 *
 * @param {object} visitCounts guid -> {visit.type, visit.time}
 * @param {object} visitCounts guid -> total visit count
 * @param {object} opts options for controlling calc
 * @returns {Promise<{[feature in "refre" | "rece" | "freq" | "unid"]: {[guid: string]: number}}>}
 */
export async function buildFrecencyFeatures(
  visitsByGuid,
  visitCounts,
  opts = {}
) {
  const {
    halfLifeDays = 28, // 28 reproduces frecency
  } = opts;

  const nowMs = Date.now();
  const dayMs = 864e5;
  const tauDays = halfLifeDays / Math.log(2);

  // Transposed output: { refre: {guid:...}, rece: {guid:...}, freq: {guid:...} }
  const byFeature = { refre: {}, rece: {}, freq: {}, unid: {} };

  for (const [guid, visits] of Object.entries(visitsByGuid)) {
    // take the log here, original frecency grows linearly with visits
    // lets test out log growth
    const total = Math.log((visitCounts?.[guid] ?? 0) + 1);

    const time_scores = [];
    const type_scores = [];

    const days_visited = new Set([]);

    for (let i = 0; i < visits.length; i++) {
      const { visit_date_us, visit_type } = visits[i];
      const ageDays = (nowMs - visit_date_us / 1000) / dayMs;
      days_visited.add(Math.floor(ageDays));
      const t = Math.exp(-ageDays / tauDays); // exponential decay
      const b = TYPE_SCORE[visit_type] ?? 0;
      time_scores.push(t);
      type_scores.push(b);
    }
    // dot captures the interaction between time and type, basically frecency
    const dot = time_scores.reduce(
      (s, x, i) => s + x * (type_scores[i] ?? 0),
      0
    );
    // time_score is a pure recency feature
    const time_score = time_scores.reduce((s, x) => s + x, 0);
    // type_score is frequency feature weighted by how the user got to the site
    const type_score = type_scores.reduce((s, x) => s + x, 0);

    byFeature.refre[guid] = total * dot; // interaction (≈ frecency-like)
    byFeature.rece[guid] = time_score; // recency-only
    byFeature.freq[guid] = total * type_score; // frequency-only (lifetime-weighted)
    byFeature.unid[guid] = days_visited.size;
  }

  return byFeature;
}

// small helpers used only here
const _projectByGuid = (guids, dict) => guids.map(g => dict[g]);

const _applyVectorFeature = (
  namei,
  rawVec,
  norms,
  score_map,
  guids,
  updated_norms
) => {
  const [vals, n] = normUpdate(rawVec, norms[namei]);
  updated_norms[namei] = n;
  guids.forEach((g, i) => {
    score_map[g][namei] = vals[i];
  });
};

export async function weightedSampleTopSites(input) {
  const alpha = input.alpha ?? 1;
  const beta = input.beta ?? 1;
  const updated_norms = {};
  const score_map = Object.fromEntries(
    input.guid.map(guid => [
      guid,
      Object.fromEntries(input.features.map(f => [f, 0])),
    ])
  );

  // Table-driven vector features that already exist as per-guid dictionaries
  const dictFeatures = {
    bmark: () => _projectByGuid(input.guid, input.bmark_scores),
    open: () => _projectByGuid(input.guid, input.open_scores),
    rece: () => _projectByGuid(input.guid, input.rece_scores),
    freq: () => _projectByGuid(input.guid, input.freq_scores),
    refre: () => _projectByGuid(input.guid, input.refre_scores),
    unid: () => _projectByGuid(input.guid, input.unid_scores),
  };

  // 1) Simple vector features
  for (const namei of Object.keys(dictFeatures)) {
    if (input.features.includes(namei)) {
      _applyVectorFeature(
        namei,
        dictFeatures[namei](),
        input.norms,
        score_map,
        input.guid,
        updated_norms
      );
    }
  }

  // 2) CTR feature (derived vector)
  if (input.features.includes("ctr")) {
    const raw_ctr = input.impressions.map((imp, i) => {
      const ctr = (input.clicks[i] + alpha) / (imp + alpha + beta);
      return Number.isFinite(ctr) ? ctr : 0;
    });
    _applyVectorFeature(
      "ctr",
      raw_ctr,
      input.norms,
      score_map,
      input.guid,
      updated_norms
    );
  }

  // 3) Thompson feature (special case)
  if (input.features.includes("thom")) {
    const ranked_thetas = await thompsonSampleSort({
      key_array: input.guid,
      obs_positive: input.clicks,
      obs_negative: input.impressions.map((imp, i) =>
        Math.max(0, imp - input.clicks[i])
      ),
      prior_positive: input.clicks.map(() => alpha),
      prior_negative: input.impressions.map(() => beta),
      do_sort: false,
    });
    const [vals, n] = normUpdate(ranked_thetas[1], input.norms.thom);
    updated_norms.thom = n;
    input.guid.forEach((g, i) => {
      score_map[g].thom = vals[i];
    });
  }

  // 4) Frecency vector (already an array)
  if (input.features.includes("frec")) {
    const [vals, n] = normUpdate(input.frecency, input.norms.frec);
    updated_norms.frec = n;
    input.guid.forEach((g, i) => {
      score_map[g].frec = vals[i];
    });
  }

  // 5) Seasonality features
  if (input.features.includes("hour")) {
    const raw = processSeasonality(
      input.guid,
      input.hourly_seasonality,
      input.tau,
      getCurrentHourOfDay()
    );
    _applyVectorFeature(
      "hour",
      raw,
      input.norms,
      score_map,
      input.guid,
      updated_norms
    );
  }
  if (input.features.includes("daily")) {
    const raw = processSeasonality(
      input.guid,
      input.daily_seasonality,
      input.tau,
      getCurrentDayOfWeek()
    );
    _applyVectorFeature(
      "daily",
      raw,
      input.norms,
      score_map,
      input.guid,
      updated_norms
    );
  }

  // 6) Bias
  if (input.features.includes("bias")) {
    input.guid.forEach(g => {
      score_map[g].bias = 1;
    });
  }

  // 7) Training and ranking scores
  const trainingWeights = Object.fromEntries(
    (input.training_features ?? input.features).map(feature => [
      feature,
      (input.training_weights ?? input.weights)[feature] ?? 0,
    ])
  );
  const rankingWeights = Object.fromEntries(
    (input.ranking_features ?? input.features).map(feature => [
      feature,
      input.weights[feature] ?? 0,
    ])
  );
  for (const g of input.guid) {
    // Cached prediction used by the next weight update to learn from later interactions.
    score_map[g].training = computeLinearScore(score_map[g], trainingWeights);
    // Score used to sort shortcuts for the current ranking.
    score_map[g].ranking = computeLinearScore(score_map[g], rankingWeights);
  }

  return { score_map, norms: updated_norms };
}

export function clampWeights(
  weights,
  maxNorm = 100,
  features = Object.keys(weights)
) {
  const selectedFeatures = new Set(features);
  const norm = Math.hypot(
    ...[...selectedFeatures].map(key => weights[key] ?? 0)
  );
  if (norm > maxNorm) {
    const scale = maxNorm / norm;
    return Object.fromEntries(
      Object.entries(weights).map(([k, v]) => [
        k,
        selectedFeatures.has(k) ? v * scale : v,
      ])
    );
  }
  return weights;
}

/**
 * Update the logistic regression weights for shortcuts ranking using gradient descent.
 *
 * @param {object} input
 * @param {string[]} input.features
 *   The list of feature names (keys in weights and score_map[guid]).
 * @param {object} input.data
 *   Mapping guid -> { clicks: number, impressions: number }.
 * @param {object} input.scores
 *   Mapping guid -> { training: number, ranking: number, [feature]: number }.
 * @param {object} input.weights
 *   Current weight vector, keyed by feature
 * @param {number} input.eta
 *   Learning rate.
 * @param {number} [input.click_bonus=1]
 *   Multiplicative weight for click events.
 * @param {boolean} [do_clamp=true]
 *   If true, clamp weights after update.
 * @returns {object}
 *   Updated weights object.
 */
export function updateWeights(input, do_clamp = true) {
  const { features } = input;
  const { data } = input;
  const score_map = input.scores;
  let { weights } = input;
  const { eta } = input;
  const click_bonus = input.click_bonus ?? 1;

  const grads = Object.create(null);
  let total = 0;

  // init gradient accumulator
  for (let j = 0; j < features.length; j += 1) {
    grads[features[j]] = 0;
  }

  for (const guid of Object.keys(data)) {
    if (
      score_map &&
      score_map[guid] &&
      typeof score_map[guid].training === "number"
    ) {
      /* impressions without a click can happen for many reasons
      that are unrelated to the item. clicks are almost
      always a deliberate action. therefore, we should
      learn more from clicks. the click_bonus can be viewed
      as a weight on click events and biases the model
       to learning more from clicks relative to impressions
      */
      const clicks = (data[guid].clicks | 0) * click_bonus;
      const impressions = data[guid].impressions | 0;
      if (clicks === 0 && impressions === 0) {
        continue;
      }

      const z = score_map[guid].training;
      const p = 1 / (1 + Math.exp(-z)); // sigmoid
      const factor = clicks * (p - 1) + impressions * p;

      for (const feature of features) {
        const fval = score_map[guid][feature] || 0;
        grads[feature] += factor * fval;
      }

      total += clicks + impressions;
    }
  }

  if (total > 0) {
    const scale = eta / total;
    for (const feature of features) {
      weights[feature] -= scale * grads[feature];
    }
  }
  if (do_clamp) {
    weights = clampWeights(weights, 100, features);
  }
  return weights;
}

export class RankShortcutsWorker {
  async weightedSampleTopSites(input) {
    return weightedSampleTopSites(input);
  }
  async sumNorm(vec) {
    return sumNorm(vec);
  }
  async updateWeights(input) {
    return updateWeights(input);
  }
  async buildFrecencyFeatures(raw_frec, visit_totals) {
    return buildFrecencyFeatures(raw_frec, visit_totals);
  }
}
