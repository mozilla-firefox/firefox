"use strict";

ChromeUtils.defineESModuleGetters(this, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const PREF_SYSTEM_SHORTCUTS_PERSONALIZATION =
  "discoverystream.shortcuts.personalization.enabled";

function attachLocalWorker(provider, WorkerMod) {
  provider._rankShortcutsWorker = {
    async post(name, args) {
      const fn = WorkerMod[name];
      if (typeof fn === "function") {
        return Array.isArray(args) ? fn(...args) : fn(args);
      }
      throw new Error(`No worker function for ${name}`);
    },
  };
}

add_task(async function test_weightedSampleTopSites_no_guid_last() {
  // Ranker are utilities we are testing
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
  );
  const Worker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );
  const provider = new Ranker.RankShortcutsProvider();

  attachLocalWorker(provider, Worker);
  // We are going to stub a database call
  const { NewTabUtils } = ChromeUtils.importESModule(
    "resource://gre/modules/NewTabUtils.sys.mjs"
  );

  await NewTabUtils.init();

  const sandbox = sinon.createSandbox();

  // Stub DB call
  sandbox
    .stub(NewTabUtils.activityStreamProvider, "executePlacesQuery")
    .resolves([
      ["a", 5, 10],
      ["b", 2, 10],
    ]);
  sandbox.stub(provider.sc_obj, "get").resolves({});
  sandbox.stub(provider.sc_obj, "set").resolves();

  // First item here intentially has no guid
  const input = [
    { url: "no-guid.com" },
    { guid: "a", url: "a.com", frecency: 10 },
    { guid: "b", url: "b.com", frecency: 5 },
  ];

  const prefValues = {
    trainhopConfig: {
      smartShortcuts: {
        enabled: true,
        features: ["frec"],
        frec_weight: 100,
        eta: 0,
        click_bonus: 10,
        positive_prior: 1,
        negative_prior: 1,
      },
    },
  };

  const result = await provider.rankTopSites(input, prefValues, {
    isStartup: false,
  });

  Assert.ok(Array.isArray(result), "returns an array");
  Assert.equal(
    result[result.length - 1].url,
    "no-guid.com",
    "top-site without GUID is last"
  );

  provider._rankShortcutsWorker?.terminate?.();
  sandbox.restore();
});

add_task(
  async function test_rankTopSites_uses_local_defaults_without_trainhop() {
    const Ranker = ChromeUtils.importESModule(
      "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
    );
    const provider = new Ranker.RankShortcutsProvider();
    const { NewTabUtils } = ChromeUtils.importESModule(
      "resource://gre/modules/NewTabUtils.sys.mjs"
    );

    await NewTabUtils.init();

    const sandbox = sinon.createSandbox();
    let weightedInput;

    sandbox
      .stub(NewTabUtils.activityStreamProvider, "executePlacesQuery")
      .resolves([
        ["a", 0, 0],
        ["b", 0, 0],
      ]);
    sandbox.stub(provider, "getLatestInteractions").resolves({});
    sandbox.stub(provider, "fetchRefreFeatures").resolves({
      rece: { a: 10, b: 1 },
      freq: null,
      refre: null,
      unid: null,
    });
    sandbox.stub(provider.sc_obj, "get").resolves({});
    sandbox.stub(provider.sc_obj, "set").resolves();
    provider._rankShortcutsWorker = {
      post(method, args) {
        const [input] = args;
        if (method === "updateWeights") {
          return input.weights;
        }
        if (method === "weightedSampleTopSites") {
          weightedInput = input;
          return {
            norms: {},
            score_map: {
              a: { final: 2, frec: 1, rece: 1, bias: 1 },
              b: { final: 1, frec: 0, rece: 0, bias: 1 },
            },
          };
        }
        throw new Error(`unexpected worker method: ${method}`);
      },
    };

    const out = await provider.rankTopSites(
      [
        { url: "https://no-guid.com" },
        { guid: "a", url: "https://a.com", frecency: 10 },
        { guid: "b", url: "https://b.com", frecency: 1 },
      ],
      {
        [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
      },
      { isStartup: true }
    );

    Assert.deepEqual(
      weightedInput.features,
      ["thom", "rece", "freq", "bmark", "ctr"],
      "built-in feature defaults are used without trainhop config"
    );
    Assert.equal(
      weightedInput.weights.thom,
      0,
      "thom is gated without enough engagement"
    );
    Assert.equal(
      weightedInput.weights.rece,
      4.66,
      "rece uses the built-in default"
    );
    Assert.equal(
      weightedInput.weights.freq,
      2,
      "freq uses the built-in default"
    );
    Assert.equal(
      weightedInput.weights.ctr,
      9.54,
      "ctr uses the built-in default"
    );
    Assert.equal(
      out[0].guid,
      "a",
      "ranking still runs without trainhop config"
    );
    Assert.equal(
      out[out.length - 1].url,
      "https://no-guid.com",
      "no-guid item stays last"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_rankTopSites_preserves_weights_after_rollout_removal() {
    // Tests a user's learned weights are preserved after rollout ends and
    // and prefs come from local files instead.
    const { RankShortcutsProvider } = ChromeUtils.importESModule(
      "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
    );
    const { updateWeights } = ChromeUtils.importESModule(
      "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
    );
    const { NewTabUtils } = ChromeUtils.importESModule(
      "resource://gre/modules/NewTabUtils.sys.mjs"
    );
    await NewTabUtils.init();

    const sandbox = sinon.createSandbox();
    try {
      const provider = new RankShortcutsProvider();
      const rollout = {
        type: "smartShortcuts",
        payload: {
          enabled: true,
          telem: false,
          force_log: true,
          eta: 10,
          click_bonus: 20,
          positive_prior: 1,
          negative_prior: 1000,
          sticky_numimps: 0,
          min_exp_clicks: 10,
          min_exp_impressions: 1000,
          features: ["thom", "rece", "freq", "bmark", "ctr"],
          frec_weight: 0,
          thom_weight: 181,
          hour_weight: 0,
          daily_weight: 0,
          bmark_weight: 0,
          rece_weight: 466,
          freq_weight: 200,
          refre_weight: 0,
          open_weight: 0,
          unid_weight: 0,
          ctr_weight: 954,
          bias_weight: 0,
        },
      };
      const prefs = {
        [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
        trainhopConfig: { [rollout.type]: rollout.payload },
      };
      const sites = [{ guid: "a", url: "https://a.com", frecency: 10 }];
      const scoreMap = {
        a: { final: 0, thom: 0.5, rece: 0.5, freq: 0.5, bmark: 1, ctr: 0.5 },
      };
      const cache = { score_map: scoreMap };
      let learningInput;
      let rankingInput;

      sandbox
        .stub(NewTabUtils.activityStreamProvider, "executePlacesQuery")
        .callsFake(async sql =>
          sql.includes("FROM moz_bookmarks") ? [["a", 1]] : [["a", 10, 1000]]
        );
      const interactions = sandbox
        .stub(provider, "getLatestInteractions")
        .resolves({ a: { clicks: 1, impressions: 0 } });
      sandbox.stub(provider, "fetchRefreFeatures").resolves({
        rece: { a: 1 },
        freq: { a: 1 },
      });
      sandbox
        .stub(provider.sc_obj, "get")
        .callsFake(async () => structuredClone(cache));
      sandbox.stub(provider.sc_obj, "set").callsFake(async (key, value) => {
        cache[key] = structuredClone(value);
      });
      provider._rankShortcutsWorker = {
        post(method, [input]) {
          if (method === "updateWeights") {
            learningInput = structuredClone(input);
            return updateWeights(structuredClone(input));
          }
          if (method === "weightedSampleTopSites") {
            rankingInput = structuredClone(input);
            return { norms: {}, score_map: scoreMap };
          }
          throw new Error(`unexpected worker method: ${method}`);
        },
      };

      await provider.rankTopSites(sites, prefs, false);
      const learnedWeights = structuredClone(cache.weights);
      const initialWeights = structuredClone(cache.init_weights);
      for (const feature of rollout.payload.features) {
        Assert.notEqual(
          learnedWeights[feature],
          initialWeights[feature],
          `${feature} learns from the click during the rollout`
        );
      }

      delete prefs.trainhopConfig;
      interactions.resolves({});
      await provider.rankTopSites(sites, prefs, false);

      Assert.deepEqual(
        learningInput.weights,
        learnedWeights,
        "removing the rollout passes learned weights into learning without a reset"
      );
      Assert.deepEqual(
        rankingInput.weights,
        learnedWeights,
        "local defaults rank with the learned weights"
      );
      Assert.deepEqual(
        cache.weights,
        learnedWeights,
        "learned weights remain cached"
      );
      Assert.deepEqual(
        cache.init_weights,
        initialWeights,
        "local initial weights match the rollout's initial weights"
      );
      Assert.deepEqual(rankingInput.features, rollout.payload.features);
      Assert.equal(learningInput.eta, rollout.payload.eta / 10000);
      Assert.equal(learningInput.click_bonus, rollout.payload.click_bonus / 10);

      interactions.resolves({ a: { clicks: 1, impressions: 0 } });
      await provider.rankTopSites(sites, prefs, false);
      Assert.deepEqual(
        learningInput.weights,
        learnedWeights,
        "local learning continues from the previously learned weights"
      );
      for (const feature of rollout.payload.features) {
        Assert.greater(
          cache.weights[feature],
          learnedWeights[feature],
          `${feature} continues learning after rollout removal`
        );
      }
    } finally {
      sandbox.restore();
    }
  }
);

add_task(async function test_rankTopSites_remote_false_overrides_local_pref() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
  );
  const provider = new Ranker.RankShortcutsProvider();
  const input = [{ guid: "a", url: "https://a.com", frecency: 10 }];

  const out = await provider.rankTopSites(
    input,
    {
      [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
      trainhopConfig: { smartShortcuts: { enabled: false } },
    },
    { isStartup: true }
  );

  Assert.equal(out, input, "explicit remote false skips ranking");
});

add_task(
  async function test_rankTopSites_partial_trainhop_falls_back_to_local_pref() {
    const Ranker = ChromeUtils.importESModule(
      "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
    );
    const provider = new Ranker.RankShortcutsProvider();
    const sandbox = sinon.createSandbox();

    sandbox.stub(provider, "getLatestInteractions").resolves({});
    sandbox.stub(provider, "fetchRefreFeatures").resolves({
      rece: { a: 10, b: 1 },
      freq: null,
      refre: null,
      unid: null,
    });
    sandbox.stub(provider.sc_obj, "get").resolves({});
    sandbox.stub(provider.sc_obj, "set").resolves();
    provider._rankShortcutsWorker = {
      post(method, args) {
        const [input] = args;
        if (method === "updateWeights") {
          return input.weights;
        }
        if (method === "weightedSampleTopSites") {
          return {
            norms: {},
            score_map: {
              a: { final: 2, frec: 1, rece: 1, bias: 1 },
              b: { final: 1, frec: 0, rece: 0, bias: 1 },
            },
          };
        }
        throw new Error(`unexpected worker method: ${method}`);
      },
    };

    const { NewTabUtils } = ChromeUtils.importESModule(
      "resource://gre/modules/NewTabUtils.sys.mjs"
    );
    await NewTabUtils.init();
    sandbox
      .stub(NewTabUtils.activityStreamProvider, "executePlacesQuery")
      .resolves([
        ["a", 0, 0],
        ["b", 0, 0],
      ]);

    const out = await provider.rankTopSites(
      [
        { guid: "a", url: "https://a.com", frecency: 10 },
        { guid: "b", url: "https://b.com", frecency: 1 },
      ],
      {
        [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
        trainhopConfig: { smartShortcuts: {} },
      },
      { isStartup: true }
    );

    Assert.equal(
      out[0].guid,
      "a",
      "partial trainhop config falls back to local defaults"
    );

    sandbox.restore();
  }
);

add_task(async function test_rankTopSites_uses_trainhop_tau() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
  );
  const provider = new Ranker.RankShortcutsProvider();
  const { NewTabUtils } = ChromeUtils.importESModule(
    "resource://gre/modules/NewTabUtils.sys.mjs"
  );

  await NewTabUtils.init();

  const sandbox = sinon.createSandbox();
  let weightedInput;

  sandbox
    .stub(NewTabUtils.activityStreamProvider, "executePlacesQuery")
    .resolves([
      ["a", 0, 0],
      ["b", 0, 0],
    ]);
  sandbox.stub(provider, "getLatestInteractions").resolves({});
  sandbox.stub(provider, "getHourlySeasonalityData").resolves({
    pvec: Array(24).fill(1 / 24),
    hists: {
      a: Array(24).fill(1),
      b: Array(24).fill(1),
    },
  });
  sandbox.stub(provider.sc_obj, "get").resolves({});
  sandbox.stub(provider.sc_obj, "set").resolves();
  provider._rankShortcutsWorker = {
    post(method, args) {
      const [input] = args;
      if (method === "updateWeights") {
        return input.weights;
      }
      if (method === "weightedSampleTopSites") {
        weightedInput = input;
        return {
          norms: {},
          score_map: {
            a: { final: 2, hour: 1 },
            b: { final: 1, hour: 0 },
          },
        };
      }
      throw new Error(`unexpected worker method: ${method}`);
    },
  };

  await provider.rankTopSites(
    [
      { guid: "a", url: "https://a.com", frecency: 10 },
      { guid: "b", url: "https://b.com", frecency: 1 },
    ],
    {
      trainhopConfig: {
        smartShortcuts: {
          enabled: true,
          features: ["hour"],
          hour_weight: 100,
          tau: 37,
        },
      },
    },
    { isStartup: true }
  );

  Assert.equal(
    weightedInput.tau,
    37,
    "seasonality tau is taken from trainhop config"
  );

  sandbox.restore();
});

add_task(async function test_sumNorm() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );
  let vec = [1, 1];
  let result = Ranker.sumNorm(vec);
  Assert.ok(
    result.every((v, i) => Math.abs(v - [0.5, 0.5][i]) < 1e-6),
    "sum norm works as expected for dense array"
  );

  vec = [0, 0];
  result = Ranker.sumNorm(vec);
  Assert.ok(
    result.every((v, i) => Math.abs(v - [0.0, 0.0][i]) < 1e-6),
    "if sum is 0.0, it should return the original vector, input is zeros"
  );

  vec = [1, -1];
  result = Ranker.sumNorm(vec);
  Assert.ok(
    result.every((v, i) => Math.abs(v - [1.0, -1.0][i]) < 1e-6),
    "if sum is 0.0, it should return the original vector, input contains negatives"
  );
});

add_task(async function test_computeLinearScore() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );

  let entry = { a: 1, b: 0, bias: 1 };
  let weights = { a: 1, b: 0, bias: 0 };
  let result = Ranker.computeLinearScore(entry, weights);
  Assert.equal(result, 1, "check linear score with one non-zero weight");

  entry = { a: 1, b: 1, bias: 1 };
  weights = { a: 1, b: 1, bias: 1 };
  result = Ranker.computeLinearScore(entry, weights);
  Assert.equal(result, 3, "check linear score with 1 everywhere");

  entry = { bias: 1 };
  weights = { a: 1, b: 1, bias: 1 };
  result = Ranker.computeLinearScore(entry, weights);
  Assert.equal(result, 1, "check linear score with empty entry, get bias");

  entry = { a: 1, b: 1, bias: 1 };
  weights = {};
  result = Ranker.computeLinearScore(entry, weights);
  Assert.equal(result, 0, "check linear score with empty weights");

  entry = { a: 1, b: 1, bias: 1 };
  weights = { a: 3 };
  result = Ranker.computeLinearScore(entry, weights);
  Assert.equal(result, 3, "check linear score with a missing weight");
});

add_task(async function test_interpolateWrappedHistogram() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );
  let hist = [1, 2];
  let t = 0.5;
  let result = Ranker.interpolateWrappedHistogram(hist, t);
  Assert.equal(result, 1.5, "test linear interpolation square in the middle");

  hist = [1, 2, 5];
  t = 2.5;
  result = Ranker.interpolateWrappedHistogram(hist, t);
  Assert.equal(
    result,
    (hist[0] + hist[2]) / 2,
    "test linear interpolation correctly wraps around"
  );

  hist = [1, 2, 5];
  t = 5;
  result = Ranker.interpolateWrappedHistogram(hist, t);
  Assert.equal(
    result,
    hist[2],
    "linear interpolation will wrap around to the last index"
  );

  Assert.equal(
    Ranker.interpolateWrappedHistogram([], 3),
    0,
    "an empty histogram interpolates to 0"
  );
});

add_task(async function test_bayesHist() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );

  let vec = [1, 2];
  let pvec = [0.5, 0.5];
  let tau = 2;
  let result = Ranker.bayesHist(vec, pvec, tau);
  // checking the math
  // (1+2*.5)/(3+2)........... 2/5
  // (2+2*.5)/(3+2)........... 3/5
  Assert.ok(
    result.every((v, i) => Math.abs(v - [0.4, 0.6][i]) < 1e-6),
    "bayes histogram is expected for typical input"
  );
  vec = [1, 2];
  pvec = [0.5, 0.5];
  tau = 0.0;
  result = Ranker.bayesHist(vec, pvec, tau);
  Assert.ok(
    result.every((v, i) => Math.abs(v - vec[i] / 3) < 1e-6), // 3 is sum of vec
    "bayes histogram is a sum norming function if tau is 0"
  );
});

add_task(async function test_normSites() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );
  let Y = {
    a: [2, 2],
    b: [3, 3],
  };
  let result = Ranker.normHistDict(Y);
  Assert.ok(
    result.a.every(
      (v, i) => Math.abs(v - [4 / (4 + 9), 4 / (4 + 9)][i]) < 1e-6
    ),
    "normSites, basic input, first array"
  );
  Assert.ok(
    result.b.every(
      (v, i) => Math.abs(v - [9 / (4 + 9), 9 / (4 + 9)][i]) < 1e-6
    ),
    "normSites, basic input, second array"
  );

  Y = [];
  result = Ranker.normHistDict(Y);
  Assert.deepEqual(result, [], "normSites handles empty array");
});

add_task(async function test_clampWeights() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );
  let weights = { a: 0, b: 1000, bias: 0 };
  let result = Ranker.clampWeights(weights, 100);
  info("clampWeights clamps a big weight vector");
  Assert.equal(result.a, 0);
  Assert.equal(result.b, 100);
  Assert.equal(result.bias, 0);

  weights = { a: 1, b: 1, bias: 1 };
  result = Ranker.clampWeights(weights, 100);
  info("clampWeights ignores a small weight vector");
  Assert.equal(result.a, 1);
  Assert.equal(result.b, 1);
  Assert.equal(result.bias, 1);
});

add_task(async function test_updateWeights_batch() {
  // Import the module that exports updateWeights
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );

  const eta = 1;
  const click_bonus = 1;
  const features = ["a", "b", "bias"];

  function sigmoid(x) {
    return 1 / (1 + Math.exp(-x));
  }
  function approxEqual(actual, expected, eps = 1e-12) {
    Assert.lessOrEqual(
      Math.abs(actual - expected),
      eps,
      `expected ${actual} ≈ ${expected}`
    );
  }

  //  single click on guid_A updates all weights
  const initial1 = { a: 0, b: 1, bias: 0.1 };
  const scores1 = {
    guid_A: { final: 1.1, a: 1, b: 1, bias: 1 },
  };

  let updated = await Ranker.updateWeights(
    {
      data: { guid_A: { clicks: 1, impressions: 0 } },
      scores: scores1,
      features,
      weights: { ...initial1 },
      eta,
      click_bonus,
    },
    false
  );

  const delta = sigmoid(1.1) - 1; // gradient term for a positive click
  approxEqual(updated.a, 0 - Number(delta) * 1);
  approxEqual(updated.b, 1 - Number(delta) * 1);
  approxEqual(updated.bias, 0.1 - Number(delta) * 1);

  // missing guid -> no-op (weights unchanged)
  const initial2 = { a: 0, b: 1000, bias: 0 };
  const scores2 = {
    guid_A: { final: 1, a: 1, b: 1e-3, bias: 1 },
  };

  updated = await Ranker.updateWeights(
    {
      data: { guid_B: { clicks: 1, impressions: 0 } }, // guid_B not in scores
      scores: scores2,
      features,
      weights: { ...initial2 },
      eta,
      click_bonus,
    },
    false
  );

  Assert.equal(updated.a, 0);
  Assert.equal(updated.b, 1000);
  Assert.equal(updated.bias, 0);
});

add_task(async function test_buildFrecencyFeatures_shape_and_empty() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );

  // empty inputs → empty feature maps
  let out = await Ranker.buildFrecencyFeatures({}, {});
  Assert.ok(
    out && "refre" in out && "rece" in out && "freq" in out,
    "returns transposed object with feature keys"
  );
  Assert.deepEqual(out.refre, {}, "refre empty");
  Assert.deepEqual(out.rece, {}, "rece  empty");
  Assert.deepEqual(out.freq, {}, "freq  empty");

  // single guid with no visits → zeros
  out = await Ranker.buildFrecencyFeatures({ guidA: [] }, { guidA: 42 });
  Assert.equal(out.refre.guidA, 0, "refre zero with no visits");
  Assert.equal(out.freq.guidA, 0, "freq  zero with no visits");
  Assert.equal(out.rece.guidA, 0, "rece  zero with no visits");
});

add_task(async function test_buildFrecencyFeatures_recency_monotonic() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );
  const sandbox = sinon.createSandbox();

  // Fix time so the decay is deterministic
  const nowMs = Date.UTC(2025, 0, 1); // Jan 1, 2025
  const clock = sandbox.useFakeTimers({ now: nowMs });

  const dayMs = 864e5;
  // visits use microseconds since epoch; function computes age with visit_date_us / 1000
  const us = v => Math.round(v * 1000);

  const visitsByGuid = {
    // one very recent visit (age ~0d)
    recent: [{ visit_date_us: us(nowMs), visit_type: 2 /* any value */ }],
    // one older visit (age 10d)
    old: [{ visit_date_us: us(nowMs - 10 * dayMs), visit_type: 2 }],
    // both recent + old → should have larger rece (sum of decays)
    both: [
      { visit_date_us: us(nowMs), visit_type: 2 },
      { visit_date_us: us(nowMs - 10 * dayMs), visit_type: 2 },
    ],
  };
  const visitCounts = { recent: 10, old: 10, both: 10 };

  const out = await Ranker.buildFrecencyFeatures(visitsByGuid, visitCounts, {
    halfLifeDays: 28, // default
  });

  Assert.greater(
    out.rece.recent,
    out.rece.old,
    "more recent visit → larger recency score"
  );
  Assert.greater(
    out.rece.both,
    out.rece.recent,
    "two visits (recent+old) → larger recency sum than just recent"
  );

  clock.restore();
  sandbox.restore();
});

add_task(
  async function test_buildFrecencyFeatures_log_scaling_and_interaction() {
    const Ranker = ChromeUtils.importESModule(
      "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
    );
    const sandbox = sinon.createSandbox();
    const clock = sandbox.useFakeTimers({ now: Date.UTC(2025, 0, 1) });

    const nowMs = Date.now();
    const us = v => Math.round(v * 1000);

    // Same single visit (type held constant) for both GUIDs → same rece and same type sum.
    // Only visit_count differs → freq/refre should scale with log1p(visit_count).
    const visitsByGuid = {
      A: [{ visit_date_us: us(nowMs), visit_type: 2 /* 'typed' typically */ }],
      B: [{ visit_date_us: us(nowMs), visit_type: 2 }],
    };
    const visitCounts = { A: 9, B: 99 }; // different lifetime counts

    const out = await Ranker.buildFrecencyFeatures(visitsByGuid, visitCounts);

    // rece should be identical (same timestamps)
    Assert.equal(
      out.rece.A,
      out.rece.B,
      "recency is independent of visit_count"
    );

    // freq and refre should scale with log1p(total)
    const ratio = Math.log1p(visitCounts.B) / Math.log1p(visitCounts.A);
    const approxEqual = (a, b, eps = 1e-9) =>
      Assert.lessOrEqual(Math.abs(a - b), eps);

    approxEqual(out.freq.B / out.freq.A, ratio, 1e-9);
    approxEqual(out.refre.B / out.refre.A, ratio, 1e-9);

    clock.restore();
    sandbox.restore();
  }
);

add_task(async function test_buildFrecencyFeatures_halfLife_effect() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );
  const sandbox = sinon.createSandbox();
  const nowMs = Date.UTC(2025, 0, 1);
  const clock = sandbox.useFakeTimers({ now: nowMs });

  const dayMs = 864e5;
  const us = v => Math.round(v * 1000);

  // Two visits: one now, one 28 days ago.
  const visits = [
    {
      visit_date_us: us(nowMs),
      visit_type: 1,
    },
    {
      visit_date_us: us(nowMs - 28 * dayMs),
      visit_type: 1,
    },
  ];
  const visitsByGuid = { X: visits };
  const visitCounts = { X: 10 };

  const outShort = await Ranker.buildFrecencyFeatures(
    visitsByGuid,
    visitCounts,
    {
      halfLifeDays: 7, // faster decay
    }
  );
  const outLong = await Ranker.buildFrecencyFeatures(
    visitsByGuid,
    visitCounts,
    {
      halfLifeDays: 56, // slower decay
    }
  );

  Assert.greater(
    outLong.rece.X,
    outShort.rece.X,
    "larger half-life → slower decay → bigger recency sum"
  );

  clock.restore();
  sandbox.restore();
});

add_task(
  async function test_buildFrecencyFeatures_unknown_types_are_zero_bonus() {
    const Ranker = ChromeUtils.importESModule(
      "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
    );
    const sandbox = sinon.createSandbox();
    const clock = sandbox.useFakeTimers({ now: Date.UTC(2025, 0, 1) });

    const nowMs = Date.now();
    const us = v => Math.round(v * 1000);

    // Use an unknown visit_type so TYPE_SCORE[visit_type] falls back to 0.
    const visitsByGuid = {
      U: [{ visit_date_us: us(nowMs), visit_type: 99999 }],
    };
    const visitCounts = { U: 100 };

    const out = await Ranker.buildFrecencyFeatures(visitsByGuid, visitCounts);

    Assert.equal(out.freq.U, 0, "unknown visit_type → zero frequency bonus");
    Assert.equal(out.refre.U, 0, "unknown visit_type → zero interaction");
    Assert.greater(out.rece.U, 0, "recency is still > 0 for a recent visit");

    clock.restore();
    sandbox.restore();
  }
);

add_task(
  async function test_weightedSampleTopSites_new_features_scores_and_norms() {
    const Ranker = ChromeUtils.importESModule(
      "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
    );

    // 2 GUIDs with simple, distinct raw values so ordering is obvious
    const guids = ["g1", "g2"];
    const input = {
      guid: guids,
      features: ["bmark", "rece", "freq", "refre", "bias"],

      // raw feature maps keyed by guid
      bmark_scores: { g1: 1, g2: 0 }, // bookmark flag/intensity
      rece_scores: { g1: 2.0, g2: 1.0 }, // recency-only raw
      freq_scores: { g1: 0.5, g2: 0.25 }, // frequency-only raw
      refre_scores: { g1: 10, g2: 5 }, // interaction raw

      // per-feature normalization state (whatever your normUpdate expects)
      norms: { bmark: null, rece: null, freq: null, refre: null },

      // weights: only new features (and bias) contribute to final
      weights: { bmark: 1, rece: 1, freq: 1, refre: 1, bias: 0 },

      // unused here (no "thom", "hour", "daily")
      clicks: [],
      impressions: [],
      alpha: 1,
      beta: 1,
      tau: 0,
      hourly_seasonality: {},
      daily_seasonality: {},
    };

    // Pre-compute what normUpdate will return for each raw vector,
    // so we can assert exact equality with weightedSampleTopSites output.
    const vecBmark = guids.map(g => input.bmark_scores[g]); // [1, 0]
    const vecRece = guids.map(g => input.rece_scores[g]); // [2, 1]
    const vecFreq = guids.map(g => input.freq_scores[g]); // [0.5, 0.25]
    const vecRefre = guids.map(g => input.refre_scores[g]); // [10, 5]

    const [expBmark, expBmarkNorm] = Ranker.normUpdate(
      vecBmark,
      input.norms.bmark
    );
    const [expRece, expReceNorm] = Ranker.normUpdate(vecRece, input.norms.rece);
    const [expFreq, expFreqNorm] = Ranker.normUpdate(vecFreq, input.norms.freq);
    const [expRefre, expRefreNorm] = Ranker.normUpdate(
      vecRefre,
      input.norms.refre
    );

    const result = await Ranker.weightedSampleTopSites(input);

    // shape
    Assert.ok(
      result && result.score_map && result.norms,
      "returns {score_map, norms}"
    );

    // per-feature, per-guid scores match normUpdate outputs
    Assert.equal(
      result.score_map.g1.bmark,
      expBmark[0],
      "bmark g1 normalized value"
    );
    Assert.equal(
      result.score_map.g2.bmark,
      expBmark[1],
      "bmark g2 normalized value"
    );

    Assert.equal(
      result.score_map.g1.rece,
      expRece[0],
      "rece g1 normalized value"
    );
    Assert.equal(
      result.score_map.g2.rece,
      expRece[1],
      "rece g2 normalized value"
    );

    Assert.equal(
      result.score_map.g1.freq,
      expFreq[0],
      "freq g1 normalized value"
    );
    Assert.equal(
      result.score_map.g2.freq,
      expFreq[1],
      "freq g2 normalized value"
    );

    Assert.equal(
      result.score_map.g1.refre,
      expRefre[0],
      "refre g1 normalized value"
    );
    Assert.equal(
      result.score_map.g2.refre,
      expRefre[1],
      "refre g2 normalized value"
    );

    // updated norms propagated back out
    Assert.deepEqual(result.norms.bmark, expBmarkNorm, "bmark norms updated");
    Assert.deepEqual(result.norms.rece, expReceNorm, "rece norms updated");
    Assert.deepEqual(result.norms.freq, expFreqNorm, "freq norms updated");

    // THIS WILL FAIL until you fix the bug in the refre block:
    //   updated_norms.rece = refre_norm;
    // should be:
    //   updated_norms.refre = refre_norm;
    Assert.deepEqual(result.norms.refre, expRefreNorm, "refre norms updated");

    // final score equals computeLinearScore with provided weights
    const expectedFinalG1 = Ranker.computeLinearScore(
      result.score_map.g1,
      input.weights
    );
    const expectedFinalG2 = Ranker.computeLinearScore(
      result.score_map.g2,
      input.weights
    );
    Assert.equal(
      result.score_map.g1.final,
      expectedFinalG1,
      "final matches linear score (g1)"
    );
    Assert.equal(
      result.score_map.g2.final,
      expectedFinalG2,
      "final matches linear score (g2)"
    );

    // sanity: g1 should outrank g2 with strictly larger raw vectors across all features
    Assert.greaterOrEqual(
      result.score_map.g1.final,
      result.score_map.g2.final,
      "g1 final ≥ g2 final as all contributing features favor g1"
    );
  }
);

add_task(async function test_rankshortcuts_queries_returning_nothing() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
  );
  const { NewTabUtils } = ChromeUtils.importESModule(
    "resource://gre/modules/NewTabUtils.sys.mjs"
  );
  await NewTabUtils.init();

  const sandbox = sinon.createSandbox();
  const stub = sandbox
    .stub(NewTabUtils.activityStreamProvider, "executePlacesQuery")
    .resolves([]); // <— mock: query returns nothing

  const topsites = [{ guid: "g1" }, { guid: "g2" }];
  const places = "moz_places";
  const hist = "moz_historyvisits";

  // fetchVisitCountsByGuid: returns {} if no rows come back
  {
    const out = await Ranker.fetchVisitCountsByGuid(topsites, places);
    Assert.deepEqual(out, {}, "visit counts: empty rows → empty map");
  }

  // fetchLast10VisitsByGuid: pre-seeds all guids → each should be []
  {
    const out = await Ranker.fetchLast10VisitsByGuid(topsites, hist, places);
    Assert.deepEqual(
      out,
      { g1: [], g2: [] },
      "last 10 visits: empty rows → each guid has []"
    );
  }

  // fetchBookmarkedFlags: ensures every guid present with false
  {
    const out = await Ranker.fetchBookmarkedFlags(
      topsites,
      "moz_bookmarks",
      places
    );
    Assert.deepEqual(
      out,
      { g1: false, g2: false },
      "bookmarks: empty rows → every guid is false"
    );
  }

  // fetchDailyVisitsSpecific: ensures every guid has a 7-bin zero hist
  {
    const out = await Ranker.fetchDailyVisitsSpecific(topsites, hist, places);
    Assert.ok(
      Array.isArray(out.g1) && out.g1.length === 7,
      "daily specific: 7 bins"
    );
    Assert.ok(
      Array.isArray(out.g2) && out.g2.length === 7,
      "daily specific: 7 bins"
    );
    Assert.ok(
      out.g1.every(v => v === 0) && out.g2.every(v => v === 0),
      "daily specific: all zeros"
    );
  }

  // fetchDailyVisitsAll: returns a 7-bin zero hist
  {
    const out = await Ranker.fetchDailyVisitsAll(hist);
    Assert.ok(Array.isArray(out) && out.length === 7, "daily all: 7 bins");
    Assert.ok(
      out.every(v => v === 0),
      "daily all: all zeros"
    );
  }

  // fetchHourlyVisitsSpecific: ensures every guid has a 24-bin zero hist
  {
    const out = await Ranker.fetchHourlyVisitsSpecific(topsites, hist, places);
    Assert.ok(
      Array.isArray(out.g1) && out.g1.length === 24,
      "hourly specific: 24 bins"
    );
    Assert.ok(
      Array.isArray(out.g2) && out.g2.length === 24,
      "hourly specific: 24 bins"
    );
    Assert.ok(
      out.g1.every(v => v === 0) && out.g2.every(v => v === 0),
      "hourly specific: all zeros"
    );
  }

  // fetchHourlyVisitsAll: returns a 24-bin zero hist
  {
    const out = await Ranker.fetchHourlyVisitsAll(hist);
    Assert.ok(Array.isArray(out) && out.length === 24, "hourly all: 24 bins");
    Assert.ok(
      out.every(v => v === 0),
      "hourly all: all zeros"
    );
  }

  // sanity: we actually exercised the stub at least once
  Assert.greater(stub.callCount, 0, "executePlacesQuery was called");

  sandbox.restore();
});

// cover the explicit "no topsites" early-return branches
add_task(async function test_rankshortcuts_no_topsites_inputs() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
  );
  const { NewTabUtils } = ChromeUtils.importESModule(
    "resource://gre/modules/NewTabUtils.sys.mjs"
  );
  await NewTabUtils.init();

  const sandbox = sinon.createSandbox();
  const stub = sandbox
    .stub(NewTabUtils.activityStreamProvider, "executePlacesQuery")
    .resolves([]); // should not matter (early return)

  // empty topsites → {}
  Assert.deepEqual(
    await Ranker.fetchVisitCountsByGuid([], "moz_places"),
    {},
    "visit counts early return {}"
  );
  Assert.deepEqual(
    await Ranker.fetchLast10VisitsByGuid([], "moz_historyvisits", "moz_places"),
    {},
    "last10 early return {}"
  );
  Assert.deepEqual(
    await Ranker.fetchBookmarkedFlags([], "moz_bookmarks", "moz_places"),
    {},
    "bookmarks early return {}"
  );
  Assert.deepEqual(
    await Ranker.fetchDailyVisitsSpecific(
      [],
      "moz_historyvisits",
      "moz_places"
    ),
    {},
    "daily specific early return {}"
  );
  Assert.deepEqual(
    await Ranker.fetchHourlyVisitsSpecific(
      [],
      "moz_historyvisits",
      "moz_places"
    ),
    {},
    "hourly specific early return {}"
  );

  // note: the *All* variants don't take topsites and thus aren't part of this early-return set.

  // make sure we didn't query DB for early-return cases
  Assert.equal(stub.callCount, 0, "no DB calls for early-return functions");

  sandbox.restore();
});

add_task(async function test_rankTopSites_sql_pipeline_happy_path() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
  );
  const Worker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );
  const { NewTabUtils } = ChromeUtils.importESModule(
    "resource://gre/modules/NewTabUtils.sys.mjs"
  );

  await NewTabUtils.init();
  const sandbox = sinon.createSandbox();

  // freeze time so recency/seasonality are stable
  const nowFixed = Date.UTC(2025, 0, 1, 12, 0, 0); // Jan 1, 2025 @ noon UTC
  const clock = sandbox.useFakeTimers({ now: nowFixed });

  // inputs: one strong site (g1), one weaker (g2), and one without guid
  const topsites = [
    { guid: "g1", url: "https://g1.com", frecency: 1000 },
    { guid: "g2", url: "https://g2.com", frecency: 10 },
    { url: "https://no-guid.com" }, // should end up last
  ];

  // provider under test
  const provider = new Ranker.RankShortcutsProvider();
  attachLocalWorker(provider, Worker);

  // keep cache interactions in-memory + observable
  const initialWeights = {
    bmark: 1,
    rece: 1,
    freq: 1,
    refre: 1,
    hour: 1,
    daily: 1,
    bias: 0,
    frec: 0,
  };
  const fakeCache = {
    weights: { ...initialWeights },
    init_weights: { ...initialWeights },
    norms: null,
    // minimal score_map so updateWeights can run even if eta > 0 (we set eta=0 below)
    score_map: { g1: { final: 0 }, g2: { final: 0 } },
  };
  sandbox.stub(provider.sc_obj, "get").resolves(fakeCache);
  const setSpy = sandbox.stub(provider.sc_obj, "set").resolves();

  // stub DB: return realistic rows per SQL shape
  const execStub = sandbox
    .stub(NewTabUtils.activityStreamProvider, "executePlacesQuery")
    .callsFake(async sql => {
      const s = String(sql);

      // --- Bookmarks (fetchBookmarkedFlags)
      if (s.includes("FROM moz_bookmarks")) {
        // rows: [key, bookmark_count]
        return [
          ["g1", 2], // bookmarked
          ["g2", 0], // not bookmarked
        ];
      }

      // --- Visit counts (fetchVisitCountsByGuid)
      if (s.includes("COALESCE(p.visit_count")) {
        // rows: [guid, visit_count]
        return [
          ["g1", 42],
          ["g2", 3],
        ];
      }

      // --- Last 10 visits (fetchLast10VisitsByGuid)
      if (s.includes("LIMIT 10") && s.includes("ORDER BY vv.visit_date DESC")) {
        // rows: [guid, visit_date_us, visit_type], ordered by guid/date DESC
        const nowUs = nowFixed * 1000;
        const dayUs = 864e8;
        // g1: two recent visits (typed + link); g2: one older link
        return [
          ["g1", nowUs, 2], // TYPED
          ["g1", nowUs - 1 * Number(dayUs), 1], // LINK (yesterday)
          ["g2", nowUs - 10 * Number(dayUs), 1], // LINK (older)
        ];
      }

      // --- Daily specific (fetchDailyVisitsSpecific): rows [key, dow, count]
      if (
        s.includes("strftime('%w'") &&
        s.includes("GROUP BY place_ids.guid")
      ) {
        return [
          ["g1", 1, 3], // Mon
          ["g1", 2, 2], // Tue
          ["g2", 1, 1], // Mon
        ];
      }

      // --- Daily all (fetchDailyVisitsAll): rows [dow, count]
      if (s.includes("strftime('%w'") && !s.includes("place_ids.guid")) {
        return [
          [0, 10],
          [1, 20],
          [2, 15],
          [3, 12],
          [4, 8],
          [5, 5],
          [6, 4],
        ];
      }

      // --- Hourly specific (fetchHourlyVisitsSpecific): rows [key, hour, count]
      if (
        s.includes("strftime('%H'") &&
        s.includes("GROUP BY place_ids.guid")
      ) {
        return [
          ["g1", 9, 5],
          ["g1", 10, 3],
          ["g2", 9, 1],
        ];
      }

      // --- Hourly all (fetchHourlyVisitsAll): rows [hour, count]
      if (s.includes("strftime('%H'") && !s.includes("place_ids.guid")) {
        return Array.from({ length: 24 }, (_, h) => [
          h,
          h >= 8 && h <= 18 ? 10 : 2,
        ]);
      }

      // --- Shortcut interactions (fetchShortcutInteractions)
      // The query varies by implementation; if your helper hits SQL, fall back to "empty"
      if (
        s.toLowerCase().includes("shortcut") ||
        s.toLowerCase().includes("smartshortcuts")
      ) {
        return [];
      }

      // default: empty for any other SQL the provider might issue
      return [];
    });

  // prefs: set eta=0 so updateWeights is a no-op (deterministic)
  const prefValues = {
    trainhopConfig: {
      smartShortcuts: {
        enabled: true,
        features: [
          "bmark",
          "rece",
          "freq",
          "refre",
          "hour",
          "daily",
          "bias",
          "frec",
        ],
        eta: 0,
        click_bonus: 10,
        positive_prior: 1,
        negative_prior: 1,
        telem: true,
      },
    },
  };

  // run
  const out = await provider.rankTopSites(topsites, prefValues, {
    isStartup: false,
  });

  // assertions
  Assert.ok(Array.isArray(out), "returns an array");
  Assert.ok(
    out.every(
      site =>
        Object.prototype.hasOwnProperty.call(site, "scores") &&
        Object.prototype.hasOwnProperty.call(site, "weights")
    ),
    "Every shortcut should expose scores and weights (even when null) because telem=true"
  );
  Assert.equal(
    out[out.length - 1].url,
    "https://no-guid.com",
    "no-guid item is last"
  );

  // If the active fset includes the “new” features, g1 should outrank g2 based on our SQL.
  const rankedGuids = out.filter(x => x.guid).map(x => x.guid);
  if (rankedGuids.length === 2) {
    // This is a soft assertion—kept as ok() instead of equal() so the test still passes
    // on builds where the active fset does not include those features.
    Assert.strictEqual(
      rankedGuids[0],
      "g1",
      `expected g1 to outrank g2 when feature set includes (bmark/rece/freq/refre/hour/daily); got ${rankedGuids}`
    );
  }

  // cache writes happened (norms + score_map)
  Assert.ok(setSpy.calledWith("norms"), "norms written to cache");
  Assert.ok(setSpy.calledWith("score_map"), "score_map written to cache");

  // sanity: DB stub exercised
  Assert.greater(execStub.callCount, 0, "executePlacesQuery was called");

  // cleanup
  provider._rankShortcutsWorker?.terminate?.();
  clock.restore();
  sandbox.restore();
});

//
// 2) weightedSampleTopSites: "open" feature sets per-guid scores and norms
//
add_task(async function test_weightedSampleTopSites_open_feature_basic() {
  const Worker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );

  // Three guids; g1 and g3 are open, g2 is closed.
  const guid = ["g1", "g2", "g3"];
  const input = {
    features: ["open", "bias"],
    guid,
    // keep only "open" contributing to final; set bias to 0 for clarity
    weights: { open: 1, bias: 0 },
    // no prior norms → normUpdate will initialize from first value
    norms: { open: null },
    // provide the open_scores dict in the format rankTopSites will pass
    open_scores: { g1: 1, g2: 0, g3: 1 },
  };

  const out = await Worker.weightedSampleTopSites(input);

  // shape
  Assert.ok(out && out.score_map && out.norms, "returns {score_map, norms}");
  Assert.ok(out.norms.open, "norms include 'open'");

  // The normalized values are opaque, but ordering should reflect inputs:
  const s1 = out.score_map.g1.open;
  const s2 = out.score_map.g2.open;
  const s3 = out.score_map.g3.open;

  Assert.greater(s1, s2, "an open guid should score higher than a closed one");
  Assert.greater(s3, s2, "another open guid should score higher than closed");
  Assert.equal(
    Math.abs(s1 - s3) < 1e-12, // normalization treats identical inputs identically
    true,
    "two open guids with identical inputs get identical normalized scores"
  );

  // final should reflect the 'open' score since bias has weight 0
  Assert.equal(out.score_map.g1.final, s1, "final = open (g1)");
  Assert.equal(out.score_map.g2.final, s2, "final = open (g2)");
  Assert.equal(out.score_map.g3.final, s3, "final = open (g3)");
});

//
// 3) weightedSampleTopSites: "open" feature honors existing running-norm state
//
add_task(async function test_weightedSampleTopSites_open_with_existing_norms() {
  const Worker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );

  const guid = ["A", "B", "C", "D"];
  const open_scores = { A: 0, B: 1, C: 0, D: 1 };

  // Seed a prior norm object to ensure running mean/var is updated & used.
  const prior = { beta: 1e-3, mean: 0.5, var: 1.0 };
  const input = {
    features: ["open", "bias"],
    guid,
    weights: { open: 1, bias: 0 },
    norms: { open: prior },
    open_scores,
  };

  const out = await Worker.weightedSampleTopSites(input);

  // Norm object should be updated (mean or var shifts minutely due to beta)
  Assert.ok(out.norms.open, "open norm returned");
  Assert.notEqual(out.norms.open.mean, prior.mean, "running mean updated");
  Assert.greater(out.norms.open.var, 0, "variance stays positive");

  // Basic ordering still holds
  const finals = guid.map(g => out.score_map[g].final);
  // open (1) items should outrank closed (0) items after normalization
  const maxClosed = Math.max(finals[0], finals[2]); // A,C
  const minOpen = Math.min(finals[1], finals[3]); // B,D
  Assert.greater(minOpen, maxClosed, "open > closed after normalization");
});

add_task(async function test_buildFrecencyFeatures_unid_counts_unique_days() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );
  const sandbox = sinon.createSandbox();

  // Fix the clock so "now" is deterministic
  const nowMs = Date.UTC(2025, 0, 10); // Jan 10, 2025
  const clock = sandbox.useFakeTimers({ now: nowMs });
  const us = ms => Math.round(ms * 1000);
  const dayMs = 864e5;

  const visitsByGuid = {
    g1: [
      { visit_date_us: us(nowMs), visit_type: 1 }, // today
      { visit_date_us: us(nowMs - 2 * dayMs), visit_type: 1 }, // 2 days ago
      { visit_date_us: us(nowMs - 2 * dayMs), visit_type: 1 }, // same day as above
    ],
    g2: [
      { visit_date_us: us(nowMs - 7 * dayMs), visit_type: 1 }, // 7 days ago
    ],
  };
  const visitCounts = { g1: 3, g2: 1 };

  const out = await Ranker.buildFrecencyFeatures(visitsByGuid, visitCounts);

  Assert.equal(out.unid.g1, 2, "g1 has visits on 2 unique days");
  Assert.equal(out.unid.g2, 1, "g2 has visits on 1 unique day");

  clock.restore();
  sandbox.restore();
});

add_task(async function test_weightedSampleTopSites_unid_feature() {
  const Worker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );

  const guids = ["a", "b"];
  const input = {
    features: ["unid", "bias"],
    guid: guids,
    // explicit scores: a=5 unique days, b=1
    unid_scores: { a: 5, b: 1 },
    norms: { unid: null },
    weights: { unid: 1, bias: 0 },
  };

  const result = await Worker.weightedSampleTopSites(input);

  Assert.ok(result.norms.unid, "norms include 'unid'");
  const ua = result.score_map.a.unid;
  const ub = result.score_map.b.unid;
  Assert.greater(ua, ub, "site with more unique days visited scores higher");
  Assert.equal(result.score_map.a.final, ua, "final equals unid score (a)");
  Assert.equal(result.score_map.b.final, ub, "final equals unid score (b)");
});

//
// 1) CTR feature: ordering + final uses normalized CTR when weighted
//
add_task(async function test_weightedSampleTopSites_ctr_basic() {
  const Worker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );

  // g1: (9+1)/(10+1+1) = .833; g2: (1+1)/(10+1+1) = .167;
  // g3: (0+1)/(0+1+1) = .5. After normalization, g1 > g3 > g2.
  const input = {
    features: ["ctr", "bias"],
    guid: ["g1", "g2", "g3"],
    clicks: [9, 1, 0],
    impressions: [10, 10, 0],
    alpha: 1,
    beta: 1,
    norms: { ctr: null },
    weights: { ctr: 1, bias: 0 },
  };

  const out = await Worker.weightedSampleTopSites(input);

  // shape
  Assert.ok(out && out.score_map && out.norms, "returns {score_map, norms}");
  Assert.ok(out.norms.ctr, "returns ctr norms");

  // ordering by normalized ctr (highest to lowest): g1 > g3 > g2
  const s1 = out.score_map.g1.ctr;
  const s2 = out.score_map.g2.ctr;
  const s3 = out.score_map.g3.ctr;

  Assert.greater(s1, s3, "g1 (.833) > g3 (.5)");
  Assert.greater(s3, s2, "g3 (.5) > g2 (.167)");

  // finals reflect ctr weight only
  Assert.equal(out.score_map.g1.final, s1, "final equals ctr (g1)");
  Assert.equal(out.score_map.g2.final, s2, "final equals ctr (g2)");
  Assert.equal(out.score_map.g3.final, s3, "final equals ctr (g3)");
});

//
// 2) CTR defaults to legacy +1/+1 smoothing when priors are omitted
//
add_task(
  async function test_weightedSampleTopSites_ctr_defaults_missing_priors() {
    const Worker = ChromeUtils.importESModule(
      "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
    );

    const input = {
      features: ["ctr"],
      guid: ["g1", "g2", "g3"],
      clicks: [9, 1, 0],
      impressions: [10, 10, 0],
      norms: { ctr: null },
      weights: { ctr: 1 },
    };

    const out = await Worker.weightedSampleTopSites(input);

    const s1 = out.score_map.g1.ctr;
    const s2 = out.score_map.g2.ctr;
    const s3 = out.score_map.g3.ctr;

    Assert.greater(s1, s3, "missing priors default to g1 > g3");
    Assert.greater(s3, s2, "missing priors default to g3 > g2");
  }
);

//
// 3) CTR smoothing edge cases: zero impressions produce finite values
//
add_task(async function test_weightedSampleTopSites_ctr_smoothing_zero_imps() {
  const Worker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );

  // gA: (0+1)/(0+1+1) = .5
  // gB: (5+1)/(0+1+1) = 3.0
  const input = {
    features: ["ctr"],
    guid: ["gA", "gB"],
    clicks: [0, 5],
    impressions: [0, 0],
    alpha: 1,
    beta: 1,
    norms: { ctr: null },
    weights: { ctr: 1 },
  };

  const out = await Worker.weightedSampleTopSites(input);

  const a = out.score_map.gA.ctr;
  const b = out.score_map.gB.ctr;

  // normalized values are opaque, but ordering must follow raw_ctr (B > A)
  Assert.greater(b, a, "higher smoothed CTR should score higher");

  // finiteness check
  Assert.ok(Number.isFinite(a) && Number.isFinite(b), "scores are finite");
});

//
// 4) CTR with zero priors: non-finite raw CTRs are clamped to zero
//
add_task(
  async function test_weightedSampleTopSites_ctr_nonfinite_clamped_to_zero() {
    const Worker = ChromeUtils.importESModule(
      "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
    );

    const input = {
      features: ["ctr"],
      guid: ["gA", "gB"],
      clicks: [0, 5],
      impressions: [0, 0],
      alpha: 0,
      beta: 0,
      norms: { ctr: null },
      weights: { ctr: 1 },
    };

    const out = await Worker.weightedSampleTopSites(input);

    const a = out.score_map.gA.ctr;
    const b = out.score_map.gB.ctr;

    Assert.ok(Number.isFinite(a) && Number.isFinite(b), "scores stay finite");
    Assert.equal(a, 0, "0/0 CTR is clamped to zero");
    Assert.equal(b, 0, "infinite CTR is clamped to zero");
  }
);

//
// 5) CTR running norm: prior norm influences (mean,var) and persists
//
add_task(async function test_weightedSampleTopSites_ctr_with_prior_norm() {
  const Worker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );

  const prior = { beta: 1e-3, mean: 0.3, var: 1.0 }; // seeded running stats
  const input = {
    features: ["ctr"],
    guid: ["x", "y"],
    clicks: [3, 0],
    impressions: [10, 10], // raw ctr: x=(3+1)/(10+1+1), y=(0+1)/(10+1+1)
    alpha: 1,
    beta: 1,
    norms: { ctr: prior },
    weights: { ctr: 1 },
  };

  const out = await Worker.weightedSampleTopSites(input);

  Assert.ok(out.norms.ctr, "ctr norm returned");
  Assert.notEqual(out.norms.ctr.mean, prior.mean, "running mean updated");
  Assert.greater(out.norms.ctr.var, 0, "variance positive");

  const x = out.score_map.x.ctr;
  const y = out.score_map.y.ctr;
  Assert.greater(x, y, "higher ctr ranks higher under prior normalization");
});

function prefsFor(features, extra = {}) {
  // Map every feature in FEATURE_META to a weight; default 0, chosen ones 100.
  const baseWeights = {
    thom_weight: 0,
    frec_weight: 0,
    hour_weight: 0,
    daily_weight: 0,
    bmark_weight: 0,
    rece_weight: 0,
    freq_weight: 0,
    refre_weight: 0,
    open_weight: 0,
    unid_weight: 0,
    ctr_weight: 0,
    bias_weight: 100,
  };
  for (const f of features) {
    const key = {
      thom: "thom_weight",
      frec: "frec_weight",
      hour: "hour_weight",
      daily: "daily_weight",
      bmark: "bmark_weight",
      rece: "rece_weight",
      freq: "freq_weight",
      refre: "refre_weight",
      open: "open_weight",
      unid: "unid_weight",
      ctr: "ctr_weight",
      bias: "bias_weight",
    }[f];
    if (key) {
      baseWeights[key] = 100;
    }
  }

  return {
    trainhopConfig: {
      smartShortcuts: {
        enabled: true,
        features,
        // make training a no-op for determinism
        eta: 0,
        click_bonus: 10,
        positive_prior: 1,
        negative_prior: 1,
        ...baseWeights,
        ...extra,
      },
    },
  };
}

function stubDB(sinon, NewTabUtils, nowFixed) {
  return sinon
    .stub(NewTabUtils.activityStreamProvider, "executePlacesQuery")
    .callsFake(async sql => {
      const s = String(sql);

      // Bookmarks
      if (s.includes("FROM moz_bookmarks")) {
        return [
          ["g1", 1],
          ["g2", 0],
          ["g3", 0],
        ];
      }

      // Visit counts
      if (s.includes("COALESCE(p.visit_count")) {
        return [
          ["g1", 20],
          ["g2", 10],
          ["g3", 5],
        ];
      }

      // Last 10 visits (guid, visit_date_us, visit_type)
      if (s.includes("LIMIT 10") && s.includes("ORDER BY vv.visit_date DESC")) {
        const us = ms => Math.round(ms * 1000);
        const day = 864e5;
        return [
          ["g1", us(nowFixed), 2], // typed now
          ["g1", us(nowFixed - day), 1], // link
          ["g2", us(nowFixed - 2 * day), 1],
          ["g3", us(nowFixed - 10 * day), 1],
        ];
      }

      // Daily specific (guid, dow, count)
      if (
        s.includes("strftime('%w'") &&
        s.includes("GROUP BY place_ids.guid")
      ) {
        return [
          ["g1", 1, 3],
          ["g2", 1, 1],
        ];
      }

      // Daily all (dow, count)
      if (s.includes("strftime('%w'") && !s.includes("place_ids.guid")) {
        return [
          [0, 10],
          [1, 20],
          [2, 15],
          [3, 12],
          [4, 8],
          [5, 5],
          [6, 4],
        ];
      }

      // Hourly specific (guid, hour, count)
      if (
        s.includes("strftime('%H'") &&
        s.includes("GROUP BY place_ids.guid")
      ) {
        return [
          ["g1", 9, 5],
          ["g2", 10, 1],
        ];
      }

      // Hourly all (hour, count)
      if (s.includes("strftime('%H'") && !s.includes("place_ids.guid")) {
        return Array.from({ length: 24 }, (_, h) => [
          h,
          h >= 8 && h <= 18 ? 10 : 2,
        ]);
      }

      // Shortcut interactions (clicks/imps aggregation over 2 months)
      if (
        s.includes("moz_newtab_shortcuts_interaction") &&
        s.includes("SUM(")
      ) {
        return [
          ["g1", 5, 10], // clicks, imps
          ["g2", 1, 10],
          ["g3", 0, 0],
        ];
      }

      // Sticky-clicks helper (only in its dedicated test)
      if (s.includes("ROW_NUMBER()") && s.includes("tile_position")) {
        // Shape: [guid, position|null]
        return [
          ["g1", 3],
          ["g2", null],
          ["g3", null],
        ];
      }

      return [];
    });
}

add_task(async function test_rankTopSites_feature_matrix() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
  );
  const Worker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcutsWorkerClass.mjs"
  );
  const { NewTabUtils } = ChromeUtils.importESModule(
    "resource://gre/modules/NewTabUtils.sys.mjs"
  );
  const { sinon } = ChromeUtils.importESModule(
    "resource://testing-common/Sinon.sys.mjs"
  );
  await NewTabUtils.init();

  const sandbox = sinon.createSandbox();
  const nowFixed = Date.UTC(2025, 0, 1, 12, 0, 0);
  const clock = sandbox.useFakeTimers({ now: nowFixed });

  // provider under test
  const provider = new Ranker.RankShortcutsProvider();
  attachLocalWorker(provider, Worker);

  // cache stubs: keep simple but real-ish
  const fakeCache = {
    weights: {},
    init_weights: {},
    norms: null,
    score_map: { g1: { final: 0 }, g2: { final: 0 }, g3: { final: 0 } },
    time_last_update: 0,
  };
  sandbox.stub(provider.sc_obj, "get").resolves(fakeCache);
  sandbox.stub(provider.sc_obj, "set").resolves();

  const dbStub = stubDB(sandbox, NewTabUtils, nowFixed);

  // input topsites
  const topsites = [
    { guid: "g1", url: "https://g1", frecency: 100 },
    { guid: "g2", url: "https://g2", frecency: 10 },
    { guid: "g3", url: "https://g3", frecency: 1 },
    { url: "https://no-guid" }, // should remain last
  ];

  // build feature lists
  const ALL = [
    "thom",
    "frec",
    "hour",
    "daily",
    "bmark",
    "rece",
    "freq",
    "refre",
    "unid",
    "ctr",
    "bias",
  ];
  const singles = ALL.map(f => [f]);
  const pairs = [
    ["frec", "bias"],
    ["thom", "bias"],
    ["ctr", "bias"],
    ["bmark", "bias"],
    ["rece", "freq"],
  ];
  const triples = [
    ["frec", "thom", "bias"],
    ["rece", "freq", "refre"],
  ];

  const cases = [...singles, ...pairs, ...triples];

  for (const features of cases) {
    const prefs = prefsFor(features);
    const out = await provider.rankTopSites(topsites, prefs, {
      isStartup: true,
    });

    // basic shape assertions
    Assert.ok(Array.isArray(out), `(${features}) returns array`);
    Assert.equal(out.length, topsites.length, `(${features}) length stable`);
    Assert.equal(
      out[out.length - 1].url,
      "https://no-guid",
      `(${features}) no-guid item is last`
    );

    // permutation check for guided items
    const inGuids = topsites
      .filter(x => x.guid)
      .map(x => x.guid)
      .sort();
    const outGuids = out
      .filter(x => x.guid)
      .map(x => x.guid)
      .sort();
    Assert.deepEqual(outGuids, inGuids, `(${features}) guids preserved`);
  }

  Assert.greater(dbStub.callCount, 0, "DB stub used at least once");
  clock.restore();
  sandbox.restore();
});

add_task(function test_roundNum_precision_and_edge_cases() {
  const { roundNum } = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
  );

  Assert.equal(
    roundNum(1.23456789),
    1.235,
    "roundNum rounds to four significant digits by default"
  );
  Assert.equal(
    roundNum(987.65, 2),
    990,
    "roundNum respects the requested precision"
  );
  Assert.equal(
    roundNum(5e-7),
    0,
    "roundNum clamps magnitudes smaller than eps to zero"
  );
  Assert.equal(
    roundNum(-5e-7),
    0,
    "roundNum clamps small negative magnitudes to zero"
  );
  Assert.equal(
    roundNum(5e-7, 4, 1e-9),
    5e-7,
    "roundNum uses the provided eps override when keeping small values"
  );
  Assert.strictEqual(
    roundNum(Number.POSITIVE_INFINITY),
    Number.POSITIVE_INFINITY,
    "roundNum leaves non-finite numbers unchanged"
  );
  Assert.ok(Number.isNaN(roundNum(NaN)), "roundNum returns NaN for NaN");
  const sentinel = { foo: "bar" };
  Assert.strictEqual(
    roundNum(sentinel),
    sentinel,
    "roundNum returns non-number inputs unchanged"
  );
});

add_task(function test_gateRuntimeConfigByEngagement_gates_below_thresholds() {
  const { gateRuntimeConfigByEngagement } = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
  );

  const runtimeConfig = {
    eta: 0.003,
    weights: { thom: 0.4, frec: 0.6, bias: 1 },
  };
  const gated = gateRuntimeConfigByEngagement(
    runtimeConfig,
    {
      min_exp_clicks: 3,
      min_exp_impressions: 10,
    },
    {
      clicks: [1, 1],
      impressions: [5, 4],
    }
  );

  Assert.equal(gated.eta, 0, "eta is disabled below the thresholds");
  Assert.equal(gated.weights.thom, 0, "thom is disabled below the thresholds");
  Assert.equal(
    gated.weights.frec,
    runtimeConfig.weights.frec,
    "other weights are preserved"
  );
  Assert.equal(runtimeConfig.eta, 0.003, "input eta is not mutated");
  Assert.equal(
    runtimeConfig.weights.thom,
    0.4,
    "input weights are not mutated"
  );
});

add_task(
  function test_gateRuntimeConfigByEngagement_preserves_values_at_threshold() {
    const { gateRuntimeConfigByEngagement } = ChromeUtils.importESModule(
      "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
    );

    const runtimeConfig = {
      eta: 0.0025,
      weights: { thom: 0.25, frec: 0.75 },
    };
    const gated = gateRuntimeConfigByEngagement(
      runtimeConfig,
      {
        min_exp_clicks: 2,
        min_exp_impressions: 7,
      },
      {
        clicks: [1, 1],
        impressions: [3, 4],
      }
    );

    Assert.strictEqual(
      gated,
      runtimeConfig,
      "runtime config stays unchanged once both thresholds are met"
    );
  }
);

add_task(function test_gateRuntimeConfigByEngagement_defaults_and_noop_cases() {
  const { gateRuntimeConfigByEngagement } = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
  );

  Assert.strictEqual(
    gateRuntimeConfigByEngagement(
      { weights: { frec: 1 } },
      {},
      { clicks: [1], impressions: [1] }
    ).weights.frec,
    1,
    "weight sets without thom are left alone"
  );

  const noGatingTargets = { click_bonus: 1 };
  Assert.strictEqual(
    gateRuntimeConfigByEngagement(noGatingTargets),
    noGatingTargets,
    "configs without eta or weights are left alone"
  );

  const gated = gateRuntimeConfigByEngagement({
    eta: 0.1,
    weights: { thom: 0.5, frec: 0.5 },
  });
  Assert.equal(gated.eta, 0, "default thresholds gate eta");
  Assert.equal(gated.weights.thom, 0, "default thresholds gate thom");
});

add_task(async function test_rankTopSites_gates_runtime_thom_weight_and_eta() {
  const Ranker = ChromeUtils.importESModule(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs"
  );
  const provider = new Ranker.RankShortcutsProvider();
  const { NewTabUtils } = ChromeUtils.importESModule(
    "resource://gre/modules/NewTabUtils.sys.mjs"
  );

  await NewTabUtils.init();

  const sandbox = sinon.createSandbox();
  let weightedInput;
  let updateInput;
  const learnedWeights = { thom: 0.4, frec: 0.6, bias: 1 };
  const initialCache = {
    weights: { ...learnedWeights },
    init_weights: { ...learnedWeights },
    score_map: {
      a: { final: 0, thom: 0, frec: 0 },
      b: { final: 0, thom: 0, frec: 0 },
    },
    norms: null,
  };

  sandbox
    .stub(NewTabUtils.activityStreamProvider, "executePlacesQuery")
    .resolves([
      ["a", 1, 80],
      ["b", 0, 80],
    ]);
  sandbox.stub(provider, "getLatestInteractions").resolves({});
  sandbox.stub(provider.sc_obj, "get").resolves(initialCache);
  const setStub = sandbox.stub(provider.sc_obj, "set").resolves();
  provider._rankShortcutsWorker = {
    post(method, args) {
      const [input] = args;
      if (method === "updateWeights") {
        updateInput = input;
        return { ...input.weights, ...learnedWeights };
      }
      if (method === "weightedSampleTopSites") {
        weightedInput = input;
        return {
          norms: {},
          score_map: {
            a: { final: 2, thom: 1, frec: 1 },
            b: { final: 1, thom: 0, frec: 0 },
          },
        };
      }
      throw new Error(`unexpected worker method: ${method}`);
    },
  };

  await provider.rankTopSites(
    [
      { guid: "a", url: "https://a.com", frecency: 10 },
      { guid: "b", url: "https://b.com", frecency: 1 },
    ],
    {
      trainhopConfig: {
        smartShortcuts: {
          enabled: true,
          features: ["thom", "frec", "bias"],
          thom_weight: 40,
          frec_weight: 60,
          bias_weight: 100,
          min_exp_clicks: 2,
          min_exp_impressions: 200,
          eta: 30,
          click_bonus: 10,
        },
      },
    },
    { isStartup: true }
  );

  Assert.equal(updateInput.eta, 0, "learning receives gated eta");
  Assert.equal(weightedInput.weights.thom, 0, "ranking receives gated thom");
  Assert.equal(
    weightedInput.weights.frec,
    learnedWeights.frec,
    "non-thom ranking weights are preserved"
  );
  Assert.equal(
    setStub.getCalls().find(call => call.args[0] === "weights").args[1].thom,
    learnedWeights.thom,
    "stored weights keep the learned thom value"
  );

  sandbox.restore();
});

add_task(async function test_rankShortcutsWorker() {
  const { BasePromiseWorker } = ChromeUtils.importESModule(
    "resource://gre/modules/PromiseWorker.sys.mjs"
  );
  const worker = new BasePromiseWorker(
    "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.worker.mjs",
    { type: "module" }
  );
  Assert.deepEqual(
    await worker.post("sumNorm", [[1, 3]]),
    [0.25, 0.75],
    "the worker dispatches to RankShortcutsWorker"
  );
  worker.terminate();
});
