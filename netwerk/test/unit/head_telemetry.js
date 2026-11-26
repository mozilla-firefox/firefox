/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

"use strict";

var HandshakeTelemetryHelpers = {
  FLAVORS: ["", "_FIRST_TRY", "_CONSERVATIVE", "_ECH", "_ECH_GREASE"],
  // Subset of tls_handshake.result from security/manager/ssl/metrics.yaml
  // that is actually read. Every testGetValue() call on
  // tls_handshake.result is a separate blocking read, and reading all of
  // labels for every flavor made tests time out on slow machines.
  // Any result a test expects must be listed here.
  TRACKED_RESULTS: [
    "Success",
    "PR_CONNECT_RESET_ERROR",
    "PR_END_OF_FILE_ERROR",
    "PR_CONNECT_REFUSED_ERROR",
    "SSL_ERROR_NO_CYPHER_OVERLAP",
    "SSL_ERROR_BAD_CERT_DOMAIN",
    "SSL_ERROR_HANDSHAKE_FAILURE_ALERT",
    "SSL_ERROR_PROTOCOL_VERSION_ALERT",
    "SSL_ERROR_ECH_RETRY_WITH_ECH",
    "SSL_ERROR_ECH_RETRY_WITHOUT_ECH",
    "SSL_ERROR_ECH_FAILED",
    "SSL_ERROR_ECH_REQUIRED_ALERT",
    "SEC_ERROR_UNKNOWN_ISSUER",
    "MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT",
    "__other__",
  ],

  _baseline: null,
  _fogInitialized: false,

  /**
   * Gathers the current values of the Glean dual labeled counter
   * metric tls_handshake.result where the key is the given flavor.
   */
  resultMetricValues(flavor) {
    let metrics = { values: {} };
    let key;
    switch (flavor) {
      case "":
        key = "all";
        break;
      case "_FIRST_TRY":
        key = "first_try";
        break;
      case "_CONSERVATIVE":
        key = "conservative";
        break;
      case "_ECH":
        key = "ech";
        break;
      case "_ECH_GREASE":
        key = "ech_grease";
        break;
      default:
        Assert.ok(false, `Unknown result flavor: ${flavor}`);
        return null;
    }
    for (let category of this.TRACKED_RESULTS) {
      metrics.values[category] = Glean.tlsHandshake.result
        .get(key, category)
        .testGetValue();
    }
    return metrics;
  },

  /**
   * Returns the Glean ssl.time_until_ready_* metric for a given flavor.
   */
  timingMetric(flavor) {
    switch (flavor) {
      case "":
        return Glean.ssl.timeUntilReady;
      case "_FIRST_TRY":
        return Glean.ssl.timeUntilReadyFirstTry;
      case "_CONSERVATIVE":
        return Glean.ssl.timeUntilReadyConservative;
      case "_ECH":
        return Glean.ssl.timeUntilReadyEch;
      case "_ECH_GREASE":
        return Glean.ssl.timeUntilReadyEchGrease;
      default:
        Assert.ok(false, `Unknown timing flavor: ${flavor}`);
        return null;
    }
  },

  /**
   * Computes the per-bucket difference between two distribution snapshots.
   * Returns a values object containing only non-zero deltas.
   */
  _deltaValues(current, baseline) {
    let result = {};
    let currentValues = current?.values ?? {};
    let baselineValues = baseline?.values ?? {};
    let allKeys = new Set([
      ...Object.keys(currentValues),
      ...Object.keys(baselineValues),
    ]);
    for (let k of allKeys) {
      let delta = (currentValues[k] ?? 0) - (baselineValues[k] ?? 0);
      if (delta !== 0) {
        result[k] = delta;
      }
    }
    return result;
  },

  /**
   * Returns a delta snapshot for a result metric since the last
   * resetHistograms() call. The returned object has a .values property
   * compatible with assertHistogramMap.
   */
  resultDelta(flavor) {
    let current = this.resultMetricValues(flavor);
    let baseline = this._baseline?.[`result${flavor}`];
    return { values: this._deltaValues(current, baseline) };
  },

  /**
   * Returns the delta sample count for a timing metric since the last
   * resetHistograms() call.
   */
  timingDeltaCount(flavor) {
    let current = this.timingMetric(flavor).testGetValue();
    let baseline = this._baseline?.[`timing${flavor}`];
    return (current?.count ?? 0) - (baseline?.count ?? 0);
  },

  /**
   * Assert that the distribution values match expected entries exactly.
   * All non-expected bucket entries must be zero.
   *
   * @param {object} snapshot A distribution snapshot with a .values property.
   * @param {Map} expectedEntries Map of bucket index (string) to expected count.
   */
  assertHistogramMap(snapshot, expectedEntries) {
    let values = JSON.parse(JSON.stringify(snapshot));
    for (let [Tk, Tv] of expectedEntries.entries()) {
      let found = false;
      for (let [i, val] of Object.entries(values.values)) {
        if (i == Tk) {
          found = true;
          Assert.equal(val, Tv, `expected counts should match at index ${i}`);
          values.values[i] = 0; // Reset the value
        }
      }
      Assert.ok(found, `Should have found an entry at index ${Tk}`);
    }
    for (let k in values.values) {
      Assert.equal(
        values.values[k],
        0,
        `Should NOT have found an entry at index ${k} of value ${values.values[k]}`
      );
    }
  },

  /**
   * Snapshots current metric values as a baseline. Subsequent check methods
   * compare against this baseline, measuring only what changed.
   */
  resetHistograms() {
    if (!this._fogInitialized) {
      Services.fog.initializeFOG();
      this._fogInitialized = true;
    }
    info("Snapshotting TLS handshake metric baseline");
    this._baseline = {};
    for (let f of this.FLAVORS) {
      this._baseline[`result${f}`] = this.resultMetricValues(f);
      this._baseline[`timing${f}`] = this.timingMetric(f).testGetValue();
    }
  },

  /**
   * Checks that all TLS Handshake metrics of a particular flavor have
   * exactly resultCount new entries for the resultCode and no other new
   * entries since the last resetHistograms() call.
   *
   * @param {Array} flavors An array of strings corresponding to which types
   *                        of metrics should have entries. See
   *                        HandshakeTelemetryHelpers.FLAVORS.
   * @param {string} resultString The expected result string, see sslerr.h.
   *                              "Success" is success, all others are errors.
   * @param {number} resultCount The number of handshake results expected.
   */
  checkEntry(flavors, resultString, resultCount) {
    Assert.ok(
      this.TRACKED_RESULTS.includes(resultString),
      `${resultString} must be in HandshakeTelemetryHelpers.TRACKED_RESULTS`
    );
    for (let f of flavors) {
      let delta = this.resultDelta(f);
      info(
        `checkEntry: result${f} expecting ${resultCount}x result ${resultString}, ` +
          `got delta: ${JSON.stringify(delta.values)}`
      );
      this.assertHistogramMap(delta, new Map([[resultString, resultCount]]));
    }

    // Timing metrics should only contain values if we expected success.
    for (let f of flavors) {
      let deltaCount = this.timingDeltaCount(f);
      let expectedCount = resultString === "Success" ? resultCount : 0;
      info(
        `checkEntry: timing${f} expecting delta count=${expectedCount}, ` +
          `got delta count=${deltaCount}`
      );
      Assert.strictEqual(
        deltaCount,
        expectedCount,
        resultString === "Success"
          ? "Timing entry count correct"
          : "No timing entries expected"
      );
    }
  },

  checkSuccess(flavors, resultCount = 1) {
    this.checkEntry(flavors, "Success", resultCount);
  },

  checkEmpty(flavors) {
    for (let f of flavors) {
      let resultDelta = this.resultDelta(f);
      let resultDeltaCount = Object.values(resultDelta.values).reduce(
        (a, b) => a + b,
        0
      );
      info(
        `checkEmpty: result${f} expecting no new entries, ` +
          `got delta count=${resultDeltaCount}`
      );
      Assert.strictEqual(
        resultDeltaCount,
        0,
        `No new result entries expected for '${f}'. Delta: ${JSON.stringify(
          resultDelta.values
        )}`
      );
      let timingDeltaCount = this.timingDeltaCount(f);
      info(
        `checkEmpty: timing${f} expecting no new entries, ` +
          `got delta count=${timingDeltaCount}`
      );
      Assert.strictEqual(
        timingDeltaCount,
        0,
        `No new timing entries expected for '${f}'.`
      );
    }
  },
};
