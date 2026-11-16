/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { BANDWIDTH } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-constants.mjs"
);

const mockLocation = {
  name: "United States",
  code: "us",
};

async function setupBandwidthPrecisionTest(maxBytes, remaining) {
  let usage = makeUsage(maxBytes, remaining);
  setupService({
    isReady: true,
    canEnroll: true,
    proxyPass: {
      status: 200,
      error: undefined,
      pass: makePass(),
      usage,
    },
    usageInfo: usage,
  });
  await IPPProxyManager.refreshUsage();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.bandwidth.enabled", true]],
  });
}

async function cleanupBandwidthPrecisionTest() {
  await SpecialPowers.popPrefEnv();
  cleanupService();
}

async function getBandwidthEl(content) {
  const bandwidthEl = content.bandwidthUsageEl;
  await bandwidthEl.updateComplete;
  return bandwidthEl;
}

add_task(async function test_bandwidth_percent_bucketing() {
  const maxBytes = BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB;

  const testCases = [
    { usagePercent: 50, expectedBucket: 50 },
    { usagePercent: 74, expectedBucket: 74 },
    { usagePercent: 75, expectedBucket: 75 },
    { usagePercent: 76, expectedBucket: 75 },
    { usagePercent: 89, expectedBucket: 75 },
    { usagePercent: 90, expectedBucket: 90 },
    { usagePercent: 91, expectedBucket: 90 },
    { usagePercent: 95, expectedBucket: 90 },
  ];

  for (const testCase of testCases) {
    const remaining = Math.floor(maxBytes * (1 - testCase.usagePercent / 100));
    await setupBandwidthPrecisionTest(String(maxBytes), String(remaining));

    let content = await openPanel({
      location: mockLocation,
      isProtectionEnabled: true,
    });

    const bandwidthEl = await getBandwidthEl(content);

    Assert.equal(
      bandwidthEl.bandwidthPercent,
      testCase.expectedBucket,
      `${testCase.usagePercent}% usage should bucket to ${testCase.expectedBucket}%`
    );

    await closePanel();
    await cleanupBandwidthPrecisionTest();
  }
});

add_task(async function test_bandwidth_decimal_precision_at_75_percent() {
  const maxBytes = BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB;

  const testCases = [
    {
      name: "12.1 GB remaining at 75% bucket",
      remaining: Math.floor(12.1 * BANDWIDTH.BYTES_IN_GB),
      expectedPercent: 75,
      expectedRounded: 12.1,
    },
    {
      name: "12.5 GB remaining at 75% bucket",
      remaining: Math.floor(12.5 * BANDWIDTH.BYTES_IN_GB),
      expectedPercent: 75,
      expectedRounded: 12.5,
    },
    {
      name: "10.7 GB remaining at 75% bucket",
      remaining: Math.floor(10.7 * BANDWIDTH.BYTES_IN_GB),
      expectedPercent: 75,
      expectedRounded: 10.7,
    },
  ];

  for (const testCase of testCases) {
    await setupBandwidthPrecisionTest(
      String(maxBytes),
      String(testCase.remaining)
    );

    let content = await openPanel({
      location: mockLocation,
      isProtectionEnabled: true,
    });

    const bandwidthEl = await getBandwidthEl(content);

    Assert.equal(
      bandwidthEl.bandwidthPercent,
      testCase.expectedPercent,
      `${testCase.name}: bandwidthPercent should be ${testCase.expectedPercent}`
    );

    Assert.equal(
      bandwidthEl.remainingRounded,
      testCase.expectedRounded,
      `${testCase.name}: remainingRounded should be ${testCase.expectedRounded}`
    );

    await closePanel();
    await cleanupBandwidthPrecisionTest();
  }
});

add_task(async function test_bandwidth_decimal_precision_outside_75_percent() {
  const maxBytes = BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB;

  const testCases = [
    {
      name: "4.9 GB at 90% bucket rounds to 4.9",
      remaining: Math.floor(4.9 * BANDWIDTH.BYTES_IN_GB),
      expectedPercent: 90,
      expectedRounded: 4.9,
    },
    {
      name: "30 GB below 75% bucket rounds to nearest 0.1",
      remaining: Math.floor(30 * BANDWIDTH.BYTES_IN_GB),
      expectedPercent: 40,
      expectedRounded: 30,
    },
  ];

  for (const testCase of testCases) {
    await setupBandwidthPrecisionTest(
      String(maxBytes),
      String(testCase.remaining)
    );

    let content = await openPanel({
      location: mockLocation,
      isProtectionEnabled: true,
    });

    const bandwidthEl = await getBandwidthEl(content);

    Assert.equal(
      bandwidthEl.bandwidthPercent,
      testCase.expectedPercent,
      `${testCase.name}: bandwidthPercent should be ${testCase.expectedPercent}`
    );

    Assert.equal(
      bandwidthEl.remainingRounded,
      testCase.expectedRounded,
      `${testCase.name}: remainingRounded should be ${testCase.expectedRounded}`
    );

    await closePanel();
    await cleanupBandwidthPrecisionTest();
  }
});

add_task(async function test_bandwidth_gb_display_with_less_than_1gb_used() {
  const maxBytes = BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB;

  const remaining = Math.floor(maxBytes - 100 * BANDWIDTH.BYTES_IN_MB);
  await setupBandwidthPrecisionTest(String(maxBytes), String(remaining));

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: true,
  });

  const bandwidthEl = await getBandwidthEl(content);

  Assert.less(
    bandwidthEl.bandwidthUsedGB,
    1,
    "bandwidthUsedGB should be less than 1"
  );

  Assert.equal(
    bandwidthEl.remainingRounded,
    parseFloat((remaining / BANDWIDTH.BYTES_IN_GB).toFixed(1)),
    "Less than 1 GB used, remainingRounded should be GB value rounded to nearest 0.1"
  );

  await closePanel();
  await cleanupBandwidthPrecisionTest();
});

add_task(async function test_bandwidth_mb_display_below_1gb() {
  const maxBytes = BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB;

  const remaining = Math.floor(0.9 * BANDWIDTH.BYTES_IN_GB);
  await setupBandwidthPrecisionTest(String(maxBytes), String(remaining));

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: true,
  });

  const bandwidthEl = await getBandwidthEl(content);

  Assert.less(bandwidthEl.remainingGB, 1, "remainingGB should be less than 1");

  Assert.equal(
    bandwidthEl.remainingRounded,
    Math.floor(0.9 * (BANDWIDTH.BYTES_IN_GB / BANDWIDTH.BYTES_IN_MB)),
    "Below 1 GB, remainingRounded should be floored MB value"
  );

  await closePanel();
  await cleanupBandwidthPrecisionTest();
});

add_task(async function test_progress_bar_value_always_in_gb() {
  const maxBytes = BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB;

  const usedBytes = {
    MINIMAL: Math.floor(0.23 * BANDWIDTH.BYTES_IN_GB),
    LESS_THAN_1_GB: Math.floor(0.97 * BANDWIDTH.BYTES_IN_GB),
    USAGE_50_PCT: maxBytes * 0.5,
    USAGE_75_PCT: maxBytes * 0.75,
    USAGE_75_PCT_BUCKET: maxBytes * 0.85,
    USAGE_90_PCT: maxBytes * 0.9,
    USAGE_90_PCT_BUCKET: maxBytes * 0.95,
    NEAR_MAX: maxBytes - Math.floor(0.123 * BANDWIDTH.BYTES_IN_GB),
  };

  const testCases = [
    {
      name: "minimal usage (0.23 GB used)",
      remaining: maxBytes - usedBytes.MINIMAL,
      expectedValue: 0.2,
    },
    {
      name: "less than 1 GB used (0.97 GB used)",
      remaining: maxBytes - usedBytes.LESS_THAN_1_GB,
      expectedValue: 1.0,
    },
    {
      name: "50% usage",
      remaining: maxBytes - usedBytes.USAGE_50_PCT,
      expectedValue: 25.0,
    },
    {
      name: "75% usage",
      remaining: maxBytes - usedBytes.USAGE_75_PCT,
      expectedValue: 37.5,
    },
    {
      name: "75% usage bucket (7.5 GB remaining)",
      remaining: maxBytes - usedBytes.USAGE_75_PCT_BUCKET,
      expectedValue: 42.5,
    },
    {
      name: "90% usage",
      remaining: maxBytes - usedBytes.USAGE_90_PCT,
      expectedValue: 45,
    },
    {
      name: "90% usage bucket (2.5 GB remaining)",
      remaining: maxBytes - usedBytes.USAGE_90_PCT_BUCKET,
      expectedValue: 47.5,
    },
    {
      name: "approaching max usage (0.123 GB remaining)",
      remaining: maxBytes - usedBytes.NEAR_MAX,
      expectedValue: 49.9,
    },
  ];

  for (const testCase of testCases) {
    await setupBandwidthPrecisionTest(
      String(maxBytes),
      String(testCase.remaining)
    );

    let content = await openPanel({
      location: mockLocation,
      isProtectionEnabled: true,
    });

    const bandwidthEl = await getBandwidthEl(content);
    const usedGB = bandwidthEl.bandwidthUsedGB;

    Assert.greaterOrEqual(
      usedGB,
      0,
      `${testCase.name}: used GB should not be negative`
    );
    Assert.lessOrEqual(
      usedGB,
      bandwidthEl.maxGB,
      `${testCase.name}: used GB should not exceed maxGB`
    );
    Assert.equal(
      parseFloat(usedGB.toFixed(1)),
      testCase.expectedValue,
      `${testCase.name}: progress bar value should be ${testCase.expectedValue} GB`
    );

    await closePanel();
    await cleanupBandwidthPrecisionTest();
  }
});
