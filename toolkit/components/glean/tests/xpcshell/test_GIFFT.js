/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);
const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);

const Telemetry = Services.telemetry;

function sleep(ms) {
  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  return new Promise(resolve => setTimeout(resolve, ms));
}

function scalarValue(aScalarName) {
  let snapshot = Telemetry.getSnapshotForScalars();
  return "parent" in snapshot ? snapshot.parent[aScalarName] : undefined;
}

function keyedScalarValue(aScalarName) {
  let snapshot = Telemetry.getSnapshotForKeyedScalars();
  return "parent" in snapshot ? snapshot.parent[aScalarName] : undefined;
}

add_setup(function test_setup() {
  // FOG needs a profile directory to put its data in.
  do_get_profile();

  Services.prefs.setBoolPref(
    "toolkit.telemetry.testing.overrideProductsCheck",
    true
  );

  // We need to initialize it once, otherwise operations will be stuck in the pre-init queue.
  // On Android FOG is set up through head.js.
  if (AppConstants.platform != "android") {
    Services.fog.initializeFOG();
  }
});

add_task(function test_gifft_counter() {
  Glean.testOnlyIpc.aCounter.add(20);
  Assert.equal(20, Glean.testOnlyIpc.aCounter.testGetValue());
  let telemetryValue = scalarValue("telemetry.test.mirror_for_counter");
  Assert.equal(20, telemetryValue);
  Assert.equal("number", typeof telemetryValue);

  Glean.testOnlyIpc.aCounterForHgram.add(20);
  Assert.equal(20, Glean.testOnlyIpc.aCounterForHgram.testGetValue());
  const data = Telemetry.getHistogramById("TELEMETRY_TEST_COUNT").snapshot();
  const expected = {
    bucket_count: 3,
    histogram_type: 4,
    sum: 20,
    range: [1, 2],
    values: { 0: 1, 1: 0 },
  };
  Assert.deepEqual(data, expected, "histogram successfully mirrored-to.");
});

add_task(function test_gifft_boolean() {
  Glean.testOnlyIpc.aBool.set(false);
  Assert.equal(false, Glean.testOnlyIpc.aBool.testGetValue());
  let telemetryValue = scalarValue("telemetry.test.boolean_kind");
  Assert.equal(false, telemetryValue);
  Assert.equal("boolean", typeof telemetryValue);
});

add_task(function test_gifft_datetime() {
  const dateStr = "2021-03-22T16:06:00";
  const value = new Date(dateStr);
  Glean.testOnlyIpc.aDate.set(value.getTime() * 1000);

  let received = Glean.testOnlyIpc.aDate.testGetValue();
  Assert.equal(value.getTime(), received.getTime());
  Assert.ok(scalarValue("telemetry.test.mirror_for_date").startsWith(dateStr));
});

add_task(function test_gifft_string() {
  const value = "a string!";
  Glean.testOnlyIpc.aString.set(value);

  Assert.equal(value, Glean.testOnlyIpc.aString.testGetValue());
  let telemetryValue = scalarValue("telemetry.test.multiple_stores_string");
  Assert.equal(value, telemetryValue);
  Assert.equal("string", typeof telemetryValue);
});

add_task(function test_gifft_memory_dist() {
  Glean.testOnlyIpc.aMemoryDist.accumulate(7);
  Glean.testOnlyIpc.aMemoryDist.accumulate(17);

  let data = Glean.testOnlyIpc.aMemoryDist.testGetValue();
  // `data.sum` is in bytes, but the metric is in KB.
  Assert.equal(24 * 1024, data.sum, "Sum's correct");
  for (let [bucket, count] of Object.entries(data.values)) {
    Assert.ok(
      count == 0 || (count == 1 && (bucket == 6888 || bucket == 17109)),
      `Only two buckets have a sample ${bucket} ${count}`
    );
  }

  data = Telemetry.getHistogramById("TELEMETRY_TEST_LINEAR").snapshot();
  Telemetry.getHistogramById("TELEMETRY_TEST_LINEAR").clear();
  Assert.equal(24, data.sum, "Histogram's in `memory_unit` units");
  Assert.equal(2, data.values["1"], "Both samples in a low bucket");

  // MemoryDistribution's Accumulate method to takes
  // a platform specific type (size_t).
  // Glean's, however, is i64, and, glean_memory_dist is uint64_t
  // What happens when we give accumulate dubious values?
  // This may occur on some uncommon platforms.
  // Note: there are issues in JS with numbers above 2**53
  Glean.testOnlyIpc.aMemoryDist.accumulate(36893488147419103232);
  let dubiousValue = Object.entries(
    Glean.testOnlyIpc.aMemoryDist.testGetValue().values
  )[0][1];
  Assert.equal(
    dubiousValue,
    1,
    "Greater than 64-Byte number did not accumulate correctly"
  );

  // Values lower than the out-of-range value are not clamped
  // resulting in an exception being thrown from the glean side
  // when the value exceeds the glean maximum allowed value
  Glean.testOnlyIpc.aMemoryDist.accumulate(Math.pow(2, 31));
  Assert.throws(
    () => Glean.testOnlyIpc.aMemoryDist.testGetValue(),
    /DataError/,
    "Did not accumulate correctly"
  );
});

add_task(function test_gifft_custom_dist() {
  Glean.testOnlyIpc.aCustomDist.accumulateSamples([7, 268435458]);

  let data = Glean.testOnlyIpc.aCustomDist.testGetValue();
  Assert.equal(7 + 268435458, data.sum, "Sum's correct");
  for (let [bucket, count] of Object.entries(data.values)) {
    Assert.ok(
      count == 0 || (count == 1 && (bucket == 1 || bucket == 268435456)),
      `Only two buckets have a sample ${bucket} ${count}`
    );
  }

  data = Telemetry.getHistogramById(
    "TELEMETRY_TEST_MIRROR_FOR_CUSTOM"
  ).snapshot();
  Telemetry.getHistogramById("TELEMETRY_TEST_MIRROR_FOR_CUSTOM").clear();
  Assert.equal(7 + 268435458, data.sum, "Sum in histogram is correct");
  Assert.equal(1, data.values["1"], "One sample in the low bucket");
  // Yes, the bucket is off-by-one compared to Glean.
  Assert.equal(1, data.values["268435457"], "One sample in the next bucket");
});

add_task(async function test_gifft_timing_dist() {
  let t1 = Glean.testOnlyIpc.aTimingDist.start();
  // Interleave some other metric's samples. bug 1768636.
  let ot1 = Glean.testOnly.whatTimeIsIt.start();
  let t2 = Glean.testOnlyIpc.aTimingDist.start();
  let ot2 = Glean.testOnly.whatTimeIsIt.start();
  Glean.testOnly.whatTimeIsIt.cancel(ot1);
  Glean.testOnly.whatTimeIsIt.cancel(ot2);

  await sleep(5);

  let t3 = Glean.testOnlyIpc.aTimingDist.start();
  Glean.testOnlyIpc.aTimingDist.cancel(t1);

  await sleep(5);

  Glean.testOnlyIpc.aTimingDist.stopAndAccumulate(t2); // 10ms
  Glean.testOnlyIpc.aTimingDist.stopAndAccumulate(t3); // 5ms

  let data = Glean.testOnlyIpc.aTimingDist.testGetValue();
  const NANOS_IN_MILLIS = 1e6;
  // bug 1701949 - Sleep gets close, but sometimes doesn't wait long enough.
  const EPSILON = 40000;

  // Variance in timing makes getting the sum impossible to know.
  // 10 and 5 input value can be trunacted to 4. + 9. >= 13. from cast
  Assert.greater(data.sum, 13 * NANOS_IN_MILLIS - EPSILON);

  // No guarantees from timers means no guarantees on buckets.
  // But we can guarantee it's only two samples.
  Assert.equal(
    2,
    Object.entries(data.values).reduce((acc, [, count]) => acc + count, 0),
    "Only two buckets with samples"
  );

  data = Telemetry.getHistogramById("TELEMETRY_TEST_EXPONENTIAL").snapshot();
  Assert.greaterOrEqual(
    data.sum,
    13 * NANOS_IN_MILLIS - EPSILON,
    "Histogram's in nanos"
  );
  Assert.equal(
    2,
    Object.entries(data.values).reduce((acc, [, count]) => acc + count, 0),
    "Only two samples"
  );

  // How about those raw sample APIs?
  Glean.testOnly.whatTimeIsIt.accumulateSingleSample(42);
  Glean.testOnly.whatTimeIsIt.accumulateSamples([255, 2047]);
  data = Glean.testOnly.whatTimeIsIt.testGetValue();
  Assert.equal(
    data.sum,
    (42 + 255 + 2047) * 1000,
    "test.only.what_time_is_it is in microseconds and testGetValue returns you nanos."
  );

  data = Telemetry.getHistogramById(
    "TELEMETRY_TEST_MIRROR_FOR_TIMING"
  ).snapshot();
  Assert.equal(
    data.sum,
    42 + 255 + 2047,
    "Telemetry gives back exactly what's put in."
  );
});

add_task(function test_gifft_string_list_works() {
  const value = "a string!";
  const value2 = "another string!";
  const value3 = "yet another string.";

  // `set` doesn't work in the mirror, so use `add`
  Glean.testOnlyIpc.aStringList.add(value);
  Glean.testOnlyIpc.aStringList.add(value2);
  Glean.testOnlyIpc.aStringList.add(value3);

  let val = Glean.testOnlyIpc.aStringList.testGetValue();
  // Note: This is incredibly fragile and will break if we ever rearrange items
  // in the string list.
  Assert.deepEqual([value, value2, value3], val);

  val = keyedScalarValue("telemetry.test.keyed_boolean_kind");
  // This too may be fragile.
  Assert.deepEqual(
    {
      [value]: true,
      [value2]: true,
      [value3]: true,
    },
    val
  );
});

add_task(function test_gifft_events() {
  Glean.testOnlyIpc.noExtraEvent.record();
  var events = Glean.testOnlyIpc.noExtraEvent.testGetValue();
  Assert.equal(1, events.length);
  Assert.equal("test_only.ipc", events[0].category);
  Assert.equal("no_extra_event", events[0].name);

  let extra = {
    value: "a value for Telemetry",
    extra1: "can set extras",
    extra2: "passing more data",
  };
  // Since `value` will be stripped and used for the value in the Legacy Telemetry API, we need
  // a copy without it in order to test with `assertEvents` later.
  let { extra1, extra2 } = extra;
  let telExtra = { extra1, extra2 };
  Glean.testOnlyIpc.anEvent.record(extra);
  // Also test a value of "".
  Glean.testOnlyIpc.anEvent.record({ value: "" });
  events = Glean.testOnlyIpc.anEvent.testGetValue();
  Assert.equal(2, events.length);
  Assert.equal("test_only.ipc", events[0].category);
  Assert.equal("an_event", events[0].name);
  Assert.deepEqual(extra, events[0].extra);

  TelemetryTestUtils.assertEvents(
    [
      ["telemetry.test", "not_expired_optout", "object1", undefined, undefined],
      ["telemetry.test", "mirror_with_extra", "object1", extra.value, telExtra],
      ["telemetry.test", "mirror_with_extra", "object1", "", undefined],
    ],
    { category: "telemetry.test" }
  );
});

add_task(function test_gifft_uuid() {
  const kTestUuid = "decafdec-afde-cafd-ecaf-decafdecafde";
  Glean.testOnlyIpc.aUuid.set(kTestUuid);
  Assert.equal(kTestUuid, Glean.testOnlyIpc.aUuid.testGetValue());
  Assert.equal(kTestUuid, scalarValue("telemetry.test.string_kind"));
});

add_task(function test_gifft_labeled_counter() {
  Assert.equal(
    undefined,
    Glean.testOnlyIpc.aLabeledCounter.a_label.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.testOnlyIpc.aLabeledCounter.a_label.add(1);
  Glean.testOnlyIpc.aLabeledCounter.another_label.add(2);
  Glean.testOnlyIpc.aLabeledCounter.a_label.add(3);
  Assert.equal(4, Glean.testOnlyIpc.aLabeledCounter.a_label.testGetValue());
  Assert.equal(
    2,
    Glean.testOnlyIpc.aLabeledCounter.another_label.testGetValue()
  );
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.testOnlyIpc.aLabeledCounter.__other__.testGetValue()
  );
  Glean.testOnlyIpc.aLabeledCounter["1".repeat(112)].add(3);
  Assert.throws(
    () => Glean.testOnlyIpc.aLabeledCounter.__other__.testGetValue(),
    /DataError/,
    "Can't get the value when you're error'd"
  );

  let value = keyedScalarValue(
    "telemetry.test.another_mirror_for_labeled_counter"
  );
  Assert.deepEqual(
    {
      a_label: 4,
      another_label: 2,
    },
    value
  );

  // What about a labeled_counter that maps to a boolean hgram?
  Assert.equal(
    undefined,
    Glean.testOnlyIpc.aLabeledCounterForHgram.true.testGetValue()
  );
  Assert.equal(
    undefined,
    Glean.testOnlyIpc.aLabeledCounterForHgram.false.testGetValue()
  );

  Glean.testOnlyIpc.aLabeledCounterForHgram.true.add(1);
  Glean.testOnlyIpc.aLabeledCounterForHgram.true.add(1);
  Glean.testOnlyIpc.aLabeledCounterForHgram.false.add(1);

  Assert.equal(
    2,
    Glean.testOnlyIpc.aLabeledCounterForHgram.true.testGetValue()
  );
  Assert.equal(
    1,
    Glean.testOnlyIpc.aLabeledCounterForHgram.false.testGetValue()
  );

  value = Telemetry.getHistogramById("TELEMETRY_TEST_BOOLEAN");
  Assert.deepEqual(
    {
      bucket_count: 3,
      histogram_type: 2,
      sum: 2,
      range: [1, 2],
      values: { 0: 1, 1: 2, 2: 0 },
    },
    value.snapshot()
  );

  // What about a labeled_counter that maps to a keyed count hgram?
  Assert.equal(
    undefined,
    Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.aLabel.testGetValue()
  );
  Assert.equal(
    undefined,
    Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.anotherLabel.testGetValue()
  );

  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.aLabel.add(2);
  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.anotherLabel.add(1);

  Assert.equal(
    2,
    Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.aLabel.testGetValue()
  );
  Assert.equal(
    1,
    Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.anotherLabel.testGetValue()
  );

  value = Telemetry.getKeyedHistogramById("TELEMETRY_TEST_KEYED_COUNT");
  Assert.deepEqual(
    {
      aLabel: {
        bucket_count: 3,
        histogram_type: 4,
        sum: 2,
        range: [1, 2],
        values: { 0: 1, 1: 0 },
      },
      anotherLabel: {
        bucket_count: 3,
        histogram_type: 4,
        sum: 1,
        range: [1, 2],
        values: { 0: 1, 1: 0 },
      },
    },
    value.snapshot()
  );

  // What about a labeled_counter that maps to a categorical hgram?
  Assert.equal(
    undefined,
    Glean.testOnlyIpc.aLabeledCounterForCategorical.CommonLabel.testGetValue()
  );
  Assert.equal(
    undefined,
    Glean.testOnlyIpc.aLabeledCounterForCategorical.Label4.testGetValue()
  );

  Glean.testOnlyIpc.aLabeledCounterForCategorical.CommonLabel.add(1);
  Glean.testOnlyIpc.aLabeledCounterForCategorical.Label4.add(1);

  Assert.equal(
    1,
    Glean.testOnlyIpc.aLabeledCounterForCategorical.CommonLabel.testGetValue()
  );
  Assert.equal(
    1,
    Glean.testOnlyIpc.aLabeledCounterForCategorical.Label4.testGetValue()
  );

  value = Telemetry.getHistogramById("TELEMETRY_TEST_CATEGORICAL_OPTOUT");
  Assert.deepEqual(
    {
      bucket_count: 51,
      histogram_type: 5,
      sum: 1,
      range: [1, 50],
      values: { 0: 1, 1: 1, 2: 0 },
    },
    value.snapshot()
  );
});

add_task(async function test_gifft_timespan() {
  // We start, briefly sleep and then stop.
  // That guarantees some time to measure.
  Glean.testOnly.mirrorTime.start();
  await sleep(10);
  Glean.testOnly.mirrorTime.stop();

  const NANOS_IN_MILLIS = 1e6;
  // bug 1931539 - Sleep gets close, but sometimes doesn't wait long enough.
  const EPSILON = 50000;
  Assert.greater(
    Glean.testOnly.mirrorTime.testGetValue(),
    10 * NANOS_IN_MILLIS - EPSILON
  );
  // Mirrored to milliseconds.
  Assert.greaterOrEqual(scalarValue("telemetry.test.mirror_for_timespan"), 9);
});

add_task(async function test_gifft_timespan_raw() {
  Glean.testOnly.mirrorTimeNanos.setRaw(15 /*ns*/);

  Assert.equal(15, Glean.testOnly.mirrorTimeNanos.testGetValue());
  // setRaw, unlike start/stop, mirrors the raw value directly.
  Assert.equal(scalarValue("telemetry.test.mirror_for_timespan_nanos"), 15);
});

add_task(async function test_gifft_labeled_boolean() {
  Assert.equal(
    undefined,
    Glean.testOnly.mirrorsForLabeledBools.a_label.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.testOnly.mirrorsForLabeledBools.a_label.set(true);
  Glean.testOnly.mirrorsForLabeledBools.another_label.set(false);
  Assert.equal(
    true,
    Glean.testOnly.mirrorsForLabeledBools.a_label.testGetValue()
  );
  Assert.equal(
    false,
    Glean.testOnly.mirrorsForLabeledBools.another_label.testGetValue()
  );
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.testOnly.mirrorsForLabeledBools.__other__.testGetValue()
  );
  Glean.testOnly.mirrorsForLabeledBools["1".repeat(112)].set(true);
  Assert.throws(
    () => Glean.testOnly.mirrorsForLabeledBools.__other__.testGetValue(),
    /DataError/,
    "Should throw because of a recording error."
  );

  // In Telemetry there is no invalid label
  let value = keyedScalarValue("telemetry.test.mirror_for_labeled_bool");
  Assert.deepEqual(
    {
      a_label: true,
      another_label: false,
    },
    value
  );
});

add_task(function test_gifft_boolean() {
  Glean.testOnly.meaningOfLife.set(42);
  Assert.equal(42, Glean.testOnly.meaningOfLife.testGetValue());
  Assert.equal(42, scalarValue("telemetry.test.mirror_for_quantity"));
});

add_task(function test_gifft_rate() {
  Glean.testOnlyIpc.irate.addToNumerator(22);
  Glean.testOnlyIpc.irate.addToDenominator(7);
  Assert.deepEqual(
    { numerator: 22, denominator: 7 },
    Glean.testOnlyIpc.irate.testGetValue()
  );
  Assert.deepEqual(
    { numerator: 22, denominator: 7 },
    keyedScalarValue("telemetry.test.mirror_for_rate")
  );
});

add_task(function test_gifft_numeric_limits() {
  // Glean and Telemetry don't share the same storage sizes or signedness.
  // Check the edges.

  // 0) Reset everything
  Services.fog.testResetFOG();
  Services.telemetry.getSnapshotForHistograms("main", true /* aClearStore */);
  Services.telemetry.getSnapshotForScalars("main", true /* aClearStore */);
  Services.telemetry.getSnapshotForKeyedScalars("main", true /* aClearStore */);

  // 1) Counter: i32 (saturates), mirrored to uint Scalar: u32 (overflows)
  // 1.1) Negative parameters refused.
  Glean.testOnlyIpc.aCounter.add(-20);
  // Unfortunately we can't check what the error was, due to API design.
  // (chutten blames chutten for his shortsightedness)
  Assert.throws(
    () => Glean.testOnlyIpc.aCounter.testGetValue(),
    /DataError/,
    "Can't get the value when you're error'd"
  );
  Assert.equal(undefined, scalarValue("telemetry.test.mirror_for_counter"));
  // Clear the error state
  Services.fog.testResetFOG();

  // 1.2) Values that sum larger than u32::max saturate (counter) and overflow (Scalar)
  // Sums to 2^32 + 1
  Glean.testOnlyIpc.aCounter.add(Math.pow(2, 31) - 1);
  Glean.testOnlyIpc.aCounter.add(1);
  Glean.testOnlyIpc.aCounter.add(Math.pow(2, 31) - 1);
  Glean.testOnlyIpc.aCounter.add(2);
  // Glean doesn't actually throw on saturation (bug 1751469),
  // so we can just check the saturation value.
  Assert.equal(Math.pow(2, 31) - 1, Glean.testOnlyIpc.aCounter.testGetValue());
  // Telemetry will have wrapped around to 1
  Assert.equal(1, scalarValue("telemetry.test.mirror_for_counter"));

  // 2) Quantity: i64 (saturates), mirrored to uint Scalar: u32 (overflows)
  // 2.1) Negative parameters refused.
  Glean.testOnly.meaningOfLife.set(-42);
  // Glean will error on this.
  Assert.throws(
    () => Glean.testOnly.meaningOfLife.testGetValue(),
    /DataError/,
    "Can't get the value when you're error'd"
  );
  // GIFFT doesn't tell Telemetry about the weird value at all.
  Assert.equal(undefined, scalarValue("telemetry.test.mirror_for_quantity"));
  // Clear the error state
  Services.fog.testResetFOG();

  // 2.2) A parameter larger than u32::max is passed to Glean unchanged,
  //      but is clamped to u32::max before being passed to Telemetry.
  Glean.testOnly.meaningOfLife.set(Math.pow(2, 32));
  Assert.equal(Math.pow(2, 32), Glean.testOnly.meaningOfLife.testGetValue());
  Assert.equal(
    Math.pow(2, 32) - 1,
    scalarValue("telemetry.test.mirror_for_quantity")
  );

  // 3) Rate: two i32 (saturates), mirrored to keyed uint Scalar: u32s (overflow)
  // 3.1) Negative parameters refused.
  Glean.testOnlyIpc.irate.addToNumerator(-22);
  Glean.testOnlyIpc.irate.addToDenominator(7);
  Assert.throws(
    () => Glean.testOnlyIpc.irate.testGetValue(),
    /DataError/,
    "Can't get the value when you're error'd"
  );
  Assert.deepEqual(
    { denominator: 7 },
    keyedScalarValue("telemetry.test.mirror_for_rate")
  );
  // Clear the error state
  Services.fog.testResetFOG();
  // Clear the partial Telemetry value
  Services.telemetry.getSnapshotForKeyedScalars("main", true /* aClearStore */);

  // Now the denominator:
  Glean.testOnlyIpc.irate.addToNumerator(22);
  Glean.testOnlyIpc.irate.addToDenominator(-7);
  Assert.throws(
    () => Glean.testOnlyIpc.irate.testGetValue(),
    /DataError/,
    "Can't get the value when you're error'd"
  );
  Assert.deepEqual(
    { numerator: 22 },
    keyedScalarValue("telemetry.test.mirror_for_rate")
  );

  // 4) Timespan
  // ( Can't overflow time without finding a way to get TimeStamp to think
  // we're 2^32 milliseconds later without waiting a month )

  // 5) TimingDistribution
  // ( Can't overflow time with start() and stopAndAccumulate() without
  // waiting for ages. But we _do_ have a test-only raw API...)
  // The max sample for timing_distribution is 600000000000.
  // The type for timing_distribution samples is i64.
  // This means when we explore the edges of GIFFT's limits, we're well past
  // Glean's limits. All we can get out of Glean is errors.
  // (Which is good for data, difficult for tests.)
  // But GIFFT should properly saturate in Telemetry at i32::max,
  // so we shall test that.
  Glean.testOnlyIpc.aTimingDist.testAccumulateRawMillis(Math.pow(2, 31) + 1);
  Glean.testOnlyIpc.aTimingDist.testAccumulateRawMillis(Math.pow(2, 32) + 1);
  Assert.throws(
    () => Glean.testOnlyIpc.aTimingDist.testGetValue(),
    /DataError/,
    "Can't get the value when you're error'd"
  );
  let snapshot = Telemetry.getHistogramById(
    "TELEMETRY_TEST_EXPONENTIAL"
  ).snapshot();
  Assert.equal(
    snapshot.values["2147483646"],
    2,
    "samples > i32::max should end up in the top bucket"
  );
});

add_task(function test_gifft_url() {
  const value = "https://www.example.com";
  Glean.testOnlyIpc.aUrl.set(value);

  Assert.equal(value, Glean.testOnlyIpc.aUrl.testGetValue());
  Assert.equal(value, scalarValue("telemetry.test.mirror_for_url"));
});

add_task(function test_gifft_url_cropped() {
  const value = `https://example.com${"/test".repeat(47)}`;
  Glean.testOnlyIpc.aUrl.set(value);

  Assert.equal(value, Glean.testOnlyIpc.aUrl.testGetValue());
  // We expect the mirrored URL to be truncated at the maximum
  // length supported by string scalars.
  Assert.equal(
    value.substring(0, 50),
    scalarValue("telemetry.test.mirror_for_url")
  );
});

add_task(function test_gifft_labeled_custom_dist() {
  Glean.testOnly.mabelsCustomLabelLengths.rubbermaid.accumulateSamples([
    7, 268435458,
  ]);

  let data = Glean.testOnly.mabelsCustomLabelLengths.rubbermaid.testGetValue();
  Assert.equal(7 + 268435458, data.sum, "Sum's correct");
  for (let [bucket, count] of Object.entries(data.values)) {
    Assert.ok(
      count == 0 || (count == 1 && (bucket == 1 || bucket == 268435456)),
      `Only two buckets have a sample ${bucket} ${count}`
    );
  }

  data = Telemetry.getKeyedHistogramById(
    "TELEMETRY_TEST_KEYED_LINEAR"
  ).snapshot();
  Telemetry.getKeyedHistogramById("TELEMETRY_TEST_KEYED_LINEAR").clear();
  Assert.ok("rubbermaid" in data, "Mirror has key");
  Assert.equal(
    7 + 268435458,
    data.rubbermaid.sum,
    "Sum in histogram is correct"
  );
  Assert.equal(1, data.rubbermaid.values["1"], "One sample in the low bucket");
  Assert.equal(
    1,
    data.rubbermaid.values["250000"],
    "One sample in the high bucket"
  );
});

add_task(function test_gifft_labeled_memory_dist() {
  Glean.testOnly.whatDoYouRemember.childhood.accumulate(7);
  Glean.testOnly.whatDoYouRemember.childhood.accumulate(17);

  let data = Glean.testOnly.whatDoYouRemember.childhood.testGetValue();
  // `data.sum` is in bytes, but the metric is in MB.
  Assert.equal(24 * 1024 * 1024, data.sum, "Sum's correct");
  for (let [bucket, count] of Object.entries(data.values)) {
    Assert.ok(
      count == 0 || (count == 1 && (bucket == 7053950 || bucket == 17520006)),
      `Only two buckets have a sample ${bucket} ${count}`
    );
  }

  data = Telemetry.getKeyedHistogramById(
    "TELEMETRY_TEST_MIRROR_FOR_LABELED_MEMORY"
  ).snapshot();
  Telemetry.getKeyedHistogramById(
    "TELEMETRY_TEST_MIRROR_FOR_LABELED_MEMORY"
  ).clear();
  Assert.ok("childhood" in data, "Key's present");
  Assert.equal(24, data.childhood.sum, "Histogram's in `memory_unit` units");
  Assert.equal(2, data.childhood.values["1"], "Both samples in a low bucket");
});

add_task(async function test_gifft_labeled_timing_dist() {
  let t1 = Glean.testOnly.whereHasTheTimeGone["down the drain"].start();
  let t2 = Glean.testOnly.whereHasTheTimeGone["down the drain"].start();

  await sleep(5);

  let t3 = Glean.testOnly.whereHasTheTimeGone["down the drain"].start();
  Glean.testOnly.whereHasTheTimeGone["down the drain"].cancel(t1);

  await sleep(5);

  Glean.testOnly.whereHasTheTimeGone["down the drain"].stopAndAccumulate(t2); // 10ms
  Glean.testOnly.whereHasTheTimeGone["down the drain"].stopAndAccumulate(t3); // 5ms

  let data =
    Glean.testOnly.whereHasTheTimeGone["down the drain"].testGetValue();
  const NANOS_IN_MILLIS = 1e6;
  // bug 1701949 - Sleep gets close, but sometimes doesn't wait long enough.
  const EPSILON = 40000;

  // Variance in timing makes getting the sum impossible to know.
  // 10 and 5 input value can be trunacted to 4. + 9. >= 13. from cast
  Assert.greater(data.sum, 13 * NANOS_IN_MILLIS - EPSILON);

  // No guarantees from timers means no guarantees on buckets.
  // But we can guarantee it's only two samples.
  Assert.equal(
    2,
    Object.entries(data.values).reduce((acc, [, count]) => acc + count, 0),
    "Only two buckets with samples"
  );

  data = Telemetry.getKeyedHistogramById(
    "TELEMETRY_TEST_MIRROR_FOR_LABELED_TIMING"
  ).snapshot();
  Assert.ok("down the drain" in data, "Has the key");
  data = data["down the drain"];
  // Suffers from same cast truncation issue of 9.... and 4.... values
  Assert.greaterOrEqual(data.sum, 13, "Histogram's in milliseconds");
  Assert.equal(
    2,
    Object.entries(data.values).reduce((acc, [, count]) => acc + count, 0),
    "Only two samples"
  );

  // Ensure that `labeled_timing_distribution` mirrors calls to
  // * accumulateSamples
  // * accumulateSingleSamples
  Glean.testOnly.whereHasTheTimeGone["hourglass sands"].accumulateSamples([
    1, 2, 3,
  ]);
  Glean.testOnly.whereHasTheTimeGone["hourglass sands"].accumulateSingleSample(
    1
  );

  data = Telemetry.getKeyedHistogramById(
    "TELEMETRY_TEST_MIRROR_FOR_LABELED_TIMING"
  ).snapshot();
  Assert.ok("hourglass sands" in data, "Has the key");
  data = data["hourglass sands"];
  Assert.equal(data.sum, 7);
});

add_task(async function test_gifft_labeled_quantity() {
  Assert.equal(
    undefined,
    Glean.testOnly.buttonJars.pants.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.testOnly.buttonJars.pants.set(42);
  Glean.testOnly.buttonJars.whoseGot.set(1);
  Assert.equal(42, Glean.testOnly.buttonJars.pants.testGetValue());
  Assert.equal(1, Glean.testOnly.buttonJars.whoseGot.testGetValue());
  // What about invalid/__other__?
  Assert.equal(undefined, Glean.testOnly.buttonJars.__other__.testGetValue());
  Glean.testOnly.buttonJars["1".repeat(112)].set(9000);
  Assert.throws(
    () => Glean.testOnly.buttonJars.__other__.testGetValue(),
    /DataError/,
    "Should throw because of a recording error."
  );

  info(JSON.stringify(Telemetry.getSnapshotForKeyedScalars()));
  // In Telemetry there is no invalid label
  let value = keyedScalarValue("telemetry.test.mirror_for_labeled_quantity");
  Assert.deepEqual(
    {
      pants: 42,
      whoseGot: 1,
    },
    value
  );
});
