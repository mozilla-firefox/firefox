/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { formatRemainingBandwidth } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-utils.mjs"
);
const { BANDWIDTH } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-constants.mjs"
);

const GB = BANDWIDTH.BYTES_IN_GB;
const MB = BANDWIDTH.BYTES_IN_MB;

add_task(async function test_format_remaining_bandwidth_gb() {
  const testCases = [
    {
      label: "50 GB",
      bytes: 50 * GB,
      expectedValue: "50",
      expectedUseGB: true,
    },
    {
      label: "49.9 GB",
      bytes: Math.floor(49.9 * GB),
      expectedValue: "49.9",
      expectedUseGB: true,
    },
    {
      label: "30 GB",
      bytes: Math.floor(30 * GB),
      expectedValue: "30",
      expectedUseGB: true,
    },
    {
      label: "12.1 GB",
      bytes: Math.floor(12.1 * GB),
      expectedValue: "12.1",
      expectedUseGB: true,
    },
    {
      label: "4.9 GB",
      bytes: Math.floor(4.9 * GB),
      expectedValue: "4.9",
      expectedUseGB: true,
    },
    {
      label: "1.0 GB",
      bytes: Math.floor(1.0 * GB),
      expectedValue: "1",
      expectedUseGB: true,
    },
  ];

  for (const { label, bytes, expectedValue, expectedUseGB } of testCases) {
    const { value, useGB } = formatRemainingBandwidth(bytes);
    Assert.equal(
      value,
      expectedValue,
      `${label}: value should be ${expectedValue}`
    );
    Assert.equal(
      useGB,
      expectedUseGB,
      `${label}: useGB should be ${expectedUseGB}`
    );
  }
});

add_task(async function test_format_remaining_bandwidth_mb() {
  const testCases = [
    {
      label: "0.9 GB expressed in MB",
      bytes: Math.floor(0.9 * GB),
      expectedValue: Math.floor((0.9 * GB) / MB),
      expectedUseGB: false,
    },
    {
      label: "100 MB",
      bytes: 100 * MB,
      expectedValue: 100,
      expectedUseGB: false,
    },
    {
      label: "1 MB",
      bytes: MB,
      expectedValue: 1,
      expectedUseGB: false,
    },
  ];

  for (const { label, bytes, expectedValue, expectedUseGB } of testCases) {
    const { value, useGB } = formatRemainingBandwidth(bytes);
    Assert.equal(
      value,
      expectedValue,
      `${label}: value should be ${expectedValue}`
    );
    Assert.equal(
      useGB,
      expectedUseGB,
      `${label}: useGB should be ${expectedUseGB}`
    );
  }
});

add_task(
  async function test_format_remaining_bandwidth_rounds_to_one_decimal() {
    const testCases = [
      { bytes: Math.floor(12.14 * GB), expectedValue: "12.1" },
      { bytes: Math.floor(12.16 * GB), expectedValue: "12.2" },
      { bytes: Math.floor(4.96 * GB), expectedValue: "5" },
      { bytes: Math.floor(4.94 * GB), expectedValue: "4.9" },
    ];

    for (const { bytes, expectedValue } of testCases) {
      const { value } = formatRemainingBandwidth(bytes);
      Assert.equal(
        value,
        expectedValue,
        `${bytes} bytes should round to ${expectedValue} GB`
      );
    }
  }
);

add_task(async function test_format_remaining_bandwidth_mb_floors() {
  const bytes = Math.floor(0.99 * GB);
  const { value, useGB } = formatRemainingBandwidth(bytes);
  Assert.equal(useGB, false, "Should use MB for values below 1 GB");
  Assert.equal(
    value,
    Math.floor(bytes / MB),
    "MB value should be floored, not rounded"
  );
});

add_task(async function test_format_remaining_bandwidth_locale() {
  const { value } = formatRemainingBandwidth(Math.floor(4.7 * GB), "de");
  Assert.equal(
    value,
    "4,7",
    "German locale should use comma as decimal separator"
  );
});
