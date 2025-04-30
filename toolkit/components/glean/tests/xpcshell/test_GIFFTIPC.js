/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { ContentTaskUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ContentTaskUtils.sys.mjs"
);
const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);
const Telemetry = Services.telemetry;
const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);

function sleep(ms) {
  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  return new Promise(resolve => setTimeout(resolve, ms));
}

function scalarValue(aScalarName, aProcessName) {
  let snapshot = Telemetry.getSnapshotForScalars();
  return aProcessName in snapshot
    ? snapshot[aProcessName][aScalarName]
    : undefined;
}

function keyedScalarValue(aScalarName, aProcessName) {
  let snapshot = Telemetry.getSnapshotForKeyedScalars();
  return aProcessName in snapshot
    ? snapshot[aProcessName][aScalarName]
    : undefined;
}

add_setup({ skip_if: () => !runningInParent }, function test_setup() {
  // Give FOG a temp profile to init within.
  do_get_profile();

  // Allows these tests to properly run on e.g. Thunderbird
  Services.prefs.setBoolPref(
    "toolkit.telemetry.testing.overrideProductsCheck",
    true
  );

  // We need to initialize it once, otherwise operations will be stuck in the pre-init queue.
  // on Android FOG is set up through head.js
  if (AppConstants.platform != "android") {
    Services.fog.initializeFOG();
  }
});

const COUNT = 42;
const CHEESY_STRING = "a very cheesy string!";
const CHEESIER_STRING = "a much cheesier string!";
const CUSTOM_SAMPLES = [3, 4];
const EVENT_EXTRA = { extra1: "so very extra" };
const MEMORIES = [13, 31];
const MEMORY_BUCKETS = ["13193", "31378"]; // buckets are strings : |
const A_LABEL_COUNT = 3;
const ANOTHER_LABEL_COUNT = 5;
const INVALID_COUNTERS = 7;
const IRATE_NUMERATOR = 44;
const IRATE_DENOMINATOR = 14;
const LABELED_MEMORY_BUCKETS = ["13509772", "32131834"];

add_task({ skip_if: () => runningInParent }, async function run_child_stuff() {
  let oldCanRecordBase = Telemetry.canRecordBase;
  Telemetry.canRecordBase = true; // Ensure we're able to record things.

  Glean.testOnlyIpc.aCounter.add(COUNT);
  Glean.testOnlyIpc.aCounterForHgram.add(COUNT);
  Glean.testOnlyIpc.aStringList.add(CHEESY_STRING);
  Glean.testOnlyIpc.aStringList.add(CHEESIER_STRING);

  Glean.testOnlyIpc.noExtraEvent.record();
  Glean.testOnlyIpc.anEvent.record(EVENT_EXTRA);

  for (let memory of MEMORIES) {
    Glean.testOnlyIpc.aMemoryDist.accumulate(memory);
  }

  let t1 = Glean.testOnlyIpc.aTimingDist.start();
  let t2 = Glean.testOnlyIpc.aTimingDist.start();

  await sleep(5);

  let t3 = Glean.testOnlyIpc.aTimingDist.start();
  Glean.testOnlyIpc.aTimingDist.cancel(t1);

  await sleep(5);

  Glean.testOnlyIpc.aTimingDist.stopAndAccumulate(t2); // 10ms
  Glean.testOnlyIpc.aTimingDist.stopAndAccumulate(t3); // 5ms

  Glean.testOnlyIpc.aCustomDist.accumulateSamples(CUSTOM_SAMPLES);

  Glean.testOnlyIpc.aLabeledCounter.a_label.add(A_LABEL_COUNT);
  Glean.testOnlyIpc.aLabeledCounter.another_label.add(ANOTHER_LABEL_COUNT);

  // Has to be different from aLabeledCounter so the error we record doesn't
  // get in the way.
  Glean.testOnlyIpc.anotherLabeledCounter["1".repeat(112)].add(
    INVALID_COUNTERS
  );

  Glean.testOnlyIpc.aLabeledCounterForHgram.true.add(1);
  Glean.testOnlyIpc.aLabeledCounterForHgram.false.add(1);
  Glean.testOnlyIpc.aLabeledCounterForHgram.false.add(1);

  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.a_label.add(
    A_LABEL_COUNT
  );
  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.another_label.add(
    ANOTHER_LABEL_COUNT
  );

  Glean.testOnlyIpc.aLabeledCounterForCategorical.CommonLabel.add(1);
  Glean.testOnlyIpc.aLabeledCounterForCategorical.Label6.add(1);
  Glean.testOnlyIpc.aLabeledCounterForCategorical.Label6.add(1);

  Glean.testOnlyIpc.irate.addToNumerator(IRATE_NUMERATOR);
  Glean.testOnlyIpc.irate.addToDenominator(IRATE_DENOMINATOR);

  Glean.testOnly.mabelsCustomLabelLengths.weird_jars.accumulateSamples(
    CUSTOM_SAMPLES
  );

  for (let memory of MEMORIES) {
    Glean.testOnly.whatDoYouRemember.breakfast.accumulate(memory);
  }

  let l1 = Glean.testOnly.whereHasTheTimeGone["long time passing"].start();
  let l2 = Glean.testOnly.whereHasTheTimeGone["long time passing"].start();

  await sleep(5);

  let l3 = Glean.testOnly.whereHasTheTimeGone["long time passing"].start();
  Glean.testOnly.whereHasTheTimeGone["long time passing"].cancel(l1);

  await sleep(5);

  Glean.testOnly.whereHasTheTimeGone["long time passing"].stopAndAccumulate(l2); // 10ms
  Glean.testOnly.whereHasTheTimeGone["long time passing"].stopAndAccumulate(l3); // 5ms

  Glean.testOnlyIpc.anUnorderedBool.set(true);

  Glean.testOnlyIpc.anUnorderedLabeledBoolean.aLabel.set(true);

  Telemetry.canRecordBase = oldCanRecordBase;
});

add_task(
  { skip_if: () => !runningInParent },
  async function test_child_metrics() {
    // Clear any stray Telemetry data
    Telemetry.clearScalars();
    Telemetry.getSnapshotForHistograms("main", true);
    Telemetry.clearEvents();

    await run_test_in_child("test_GIFFTIPC.js");

    // Wait for both IPC mechanisms to flush.
    await Services.fog.testFlushAllChildren();
    await ContentTaskUtils.waitForCondition(() => {
      let snapshot = Telemetry.getSnapshotForKeyedScalars();
      return (
        "content" in snapshot &&
        // Update this to be the mirrored-to probe of the bottom-most call in
        // run_child_stuff().
        "telemetry.test.mirror_for_unordered_labeled_bool" in snapshot.content
      );
    }, "failed to find content telemetry in parent");

    // boolean
    Assert.ok(Glean.testOnlyIpc.anUnorderedBool.testGetValue());
    Assert.ok(
      scalarValue("telemetry.test.mirror_for_unordered_bool", "content"),
      "content-process Scalar has expected bool value"
    );

    // counter
    Assert.equal(Glean.testOnlyIpc.aCounter.testGetValue(), COUNT);
    Assert.equal(
      scalarValue("telemetry.test.mirror_for_counter", "content"),
      COUNT,
      "content-process Scalar has expected count"
    );

    Assert.equal(Glean.testOnlyIpc.aCounterForHgram.testGetValue(), COUNT);
    const histSnapshot = Telemetry.getSnapshotForHistograms(
      "main",
      false,
      false
    );
    const countData = histSnapshot.content.TELEMETRY_TEST_COUNT;
    Assert.equal(COUNT, countData.sum, "Sum in histogram's correct.");

    // custom_distribution
    const customSampleSum = CUSTOM_SAMPLES.reduce((acc, a) => acc + a, 0);
    const customData = Glean.testOnlyIpc.aCustomDist.testGetValue("test-ping");
    Assert.equal(customSampleSum, customData.sum, "Sum's correct");
    for (let [bucket, count] of Object.entries(customData.values)) {
      Assert.ok(
        count == 0 || (count == CUSTOM_SAMPLES.length && bucket == 1), // both values in the low bucket
        `Only two buckets have a sample ${bucket} ${count}`
      );
    }
    const histData = histSnapshot.content.TELEMETRY_TEST_MIRROR_FOR_CUSTOM;
    Assert.equal(customSampleSum, histData.sum, "Sum in histogram's correct");
    Assert.equal(2, histData.values["1"], "Two samples in the first bucket");

    // datetime
    // Doesn't work over IPC

    // event
    var events = Glean.testOnlyIpc.noExtraEvent.testGetValue();
    Assert.equal(1, events.length);
    Assert.equal("test_only.ipc", events[0].category);
    Assert.equal("no_extra_event", events[0].name);

    events = Glean.testOnlyIpc.anEvent.testGetValue();
    Assert.equal(1, events.length);
    Assert.equal("test_only.ipc", events[0].category);
    Assert.equal("an_event", events[0].name);
    Assert.deepEqual(EVENT_EXTRA, events[0].extra);

    TelemetryTestUtils.assertEvents(
      [
        [
          "telemetry.test",
          "not_expired_optout",
          "object1",
          undefined,
          undefined,
        ],
        ["telemetry.test", "mirror_with_extra", "object1", null, EVENT_EXTRA],
      ],
      { category: "telemetry.test" },
      { process: "content" }
    );

    // labeled_boolean
    Assert.ok(
      Glean.testOnlyIpc.anUnorderedLabeledBoolean.aLabel.testGetValue()
    );
    let value = keyedScalarValue(
      "telemetry.test.mirror_for_unordered_labeled_bool",
      "content"
    );
    Assert.deepEqual({ aLabel: true }, value);

    // labeled_counter
    const counters = Glean.testOnlyIpc.aLabeledCounter;
    Assert.equal(counters.a_label.testGetValue(), A_LABEL_COUNT);
    Assert.equal(counters.another_label.testGetValue(), ANOTHER_LABEL_COUNT);

    Assert.throws(
      () => Glean.testOnlyIpc.anotherLabeledCounter.__other__.testGetValue(),
      /DataError/,
      "Invalid labels record errors, which throw"
    );

    value = keyedScalarValue(
      "telemetry.test.another_mirror_for_labeled_counter",
      "content"
    );
    Assert.deepEqual(
      {
        a_label: A_LABEL_COUNT,
        another_label: ANOTHER_LABEL_COUNT,
      },
      value
    );
    value = keyedScalarValue(
      "telemetry.test.mirror_for_labeled_counter",
      "content"
    );
    Assert.deepEqual(undefined, value);

    const boolHgramCounters = Glean.testOnlyIpc.aLabeledCounterForHgram;
    Assert.equal(boolHgramCounters.true.testGetValue(), 1);
    Assert.equal(boolHgramCounters.false.testGetValue(), 2);
    Assert.deepEqual(
      {
        bucket_count: 3,
        histogram_type: 2,
        sum: 1,
        range: [1, 2],
        values: { 0: 2, 1: 1, 2: 0 },
      },
      histSnapshot.content.TELEMETRY_TEST_BOOLEAN
    );

    const keyedHistSnapshot = Telemetry.getSnapshotForKeyedHistograms(
      "main",
      false,
      false
    );
    const keyedCountHgramCounters =
      Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram;
    Assert.equal(keyedCountHgramCounters.a_label.testGetValue(), A_LABEL_COUNT);
    Assert.equal(
      keyedCountHgramCounters.another_label.testGetValue(),
      ANOTHER_LABEL_COUNT
    );
    Assert.deepEqual(
      {
        a_label: {
          bucket_count: 3,
          histogram_type: 4,
          sum: A_LABEL_COUNT,
          range: [1, 2],
          values: { 0: 1, 1: 0 },
        },
        another_label: {
          bucket_count: 3,
          histogram_type: 4,
          sum: ANOTHER_LABEL_COUNT,
          range: [1, 2],
          values: { 0: 1, 1: 0 },
        },
      },
      keyedHistSnapshot.content.TELEMETRY_TEST_KEYED_COUNT
    );

    const catCounters = Glean.testOnlyIpc.aLabeledCounterForCategorical;
    Assert.equal(catCounters.CommonLabel.testGetValue(), 1);
    Assert.equal(catCounters.Label6.testGetValue(), 2);
    Assert.deepEqual(
      {
        bucket_count: 51,
        histogram_type: 5,
        sum: 6,
        range: [1, 50],
        values: { 0: 1, 3: 2, 4: 0 },
      },
      histSnapshot.content.TELEMETRY_TEST_CATEGORICAL_OPTOUT
    );

    // labeled_string
    // Doesn't work over IPC

    // memory_distribution
    const memoryData = Glean.testOnlyIpc.aMemoryDist.testGetValue();
    const memorySum = MEMORIES.reduce((acc, a) => acc + a, 0);
    // The sum's in bytes, but the metric's in KB
    Assert.equal(memorySum * 1024, memoryData.sum);
    for (let [bucket, count] of Object.entries(memoryData.values)) {
      // We could assert instead, but let's skip to save the logspam.
      if (count == 0) {
        continue;
      }
      Assert.ok(count == 1 && MEMORY_BUCKETS.includes(bucket));
    }

    const memoryHist = histSnapshot.content.TELEMETRY_TEST_LINEAR;
    Assert.equal(
      memorySum,
      memoryHist.sum,
      "Histogram's in `memory_unit` units"
    );
    Assert.equal(2, memoryHist.values["1"], "Samples are in the right bucket");

    // quantity
    // Doesn't work over IPC

    // rate
    Assert.deepEqual(
      { numerator: IRATE_NUMERATOR, denominator: IRATE_DENOMINATOR },
      Glean.testOnlyIpc.irate.testGetValue()
    );
    Assert.deepEqual(
      { numerator: IRATE_NUMERATOR, denominator: IRATE_DENOMINATOR },
      keyedScalarValue("telemetry.test.mirror_for_rate", "content")
    );

    // string
    // Doesn't work over IPC

    // string_list
    // Note: this will break if string list ever rearranges its items.
    const cheesyStrings = Glean.testOnlyIpc.aStringList.testGetValue();
    Assert.deepEqual(cheesyStrings, [CHEESY_STRING, CHEESIER_STRING]);
    // Note: this will break if keyed scalars rearrange their items.
    Assert.deepEqual(
      {
        [CHEESY_STRING]: true,
        [CHEESIER_STRING]: true,
      },
      keyedScalarValue("telemetry.test.keyed_boolean_kind", "content")
    );

    // timespan
    // Doesn't work over IPC

    // timing_distribution
    const NANOS_IN_MILLIS = 1e6;
    const EPSILON = 40000; // bug 1701949
    const times = Glean.testOnlyIpc.aTimingDist.testGetValue();
    Assert.greater(times.sum, 15 * NANOS_IN_MILLIS - EPSILON);
    // We can't guarantee any specific time values (thank you clocks),
    // but we can assert there are only two samples.
    Assert.equal(
      2,
      Object.entries(times.values).reduce((acc, [, count]) => acc + count, 0)
    );
    const timingHist = histSnapshot.content.TELEMETRY_TEST_EXPONENTIAL;
    Assert.greaterOrEqual(timingHist.sum, 13, "Histogram's in milliseconds.");
    // Both values, 10 and 5, are truncated by a cast in AccumulateTimeDelta
    // Minimally downcast 9. + 4. could realistically result in 13.
    Assert.equal(
      2,
      Object.entries(timingHist.values).reduce(
        (acc, [, count]) => acc + count,
        0
      ),
      "Only two samples"
    );

    // uuid
    // Doesn't work over IPC

    // labeled_custom_distribution
    const labeledCustomData =
      Glean.testOnly.mabelsCustomLabelLengths.weird_jars.testGetValue();
    Assert.equal(customSampleSum, labeledCustomData.sum, "Sum's correct");
    for (let [bucket, count] of Object.entries(labeledCustomData.values)) {
      Assert.ok(
        count == 0 || (count == CUSTOM_SAMPLES.length && bucket == 1), // both values in the low bucket
        `Only two buckets have a sample ${bucket} ${count}`
      );
    }
    const keyedHistData = keyedHistSnapshot.content.TELEMETRY_TEST_KEYED_LINEAR;
    Assert.ok("weird_jars" in keyedHistData, "Key's present");
    Assert.equal(
      customSampleSum,
      keyedHistData.weird_jars.sum,
      "Sum in histogram's correct"
    );
    Assert.equal(
      2,
      keyedHistData.weird_jars.values["1"],
      "Two samples in the first bucket"
    );

    // labeled_memory_distribution
    const labeledMemoryData =
      Glean.testOnly.whatDoYouRemember.breakfast.testGetValue();
    const labeledMemorySum = MEMORIES.reduce((acc, a) => acc + a, 0);
    // The sum's in bytes, but the metric's in MB
    Assert.equal(labeledMemorySum * 1024 * 1024, labeledMemoryData.sum);
    info(JSON.stringify(labeledMemoryData.values));
    for (let [bucket, count] of Object.entries(labeledMemoryData.values)) {
      // We could assert instead, but let's skip to save the logspam.
      if (count == 0) {
        continue;
      }
      Assert.ok(count == 1 && LABELED_MEMORY_BUCKETS.includes(bucket));
    }

    const labeledMemoryHist =
      keyedHistSnapshot.content.TELEMETRY_TEST_MIRROR_FOR_LABELED_MEMORY;
    Assert.ok("breakfast" in labeledMemoryHist, "Has key");
    Assert.equal(
      memorySum,
      labeledMemoryHist.breakfast.sum,
      "Histogram's in `memory_unit` units"
    );
    Assert.equal(
      2,
      labeledMemoryHist.breakfast.values["1"],
      "Samples are in the right bucket"
    );

    // labeled_timing_distribution
    const lTimes =
      Glean.testOnly.whereHasTheTimeGone["long time passing"].testGetValue();
    Assert.greater(lTimes.sum, 15 * NANOS_IN_MILLIS - EPSILON);
    // We can't guarantee any specific time values (thank you clocks),
    // but we can assert there are only two samples.
    Assert.equal(
      2,
      Object.entries(lTimes.values).reduce((acc, [, count]) => acc + count, 0)
    );
    const labeledTimingHist =
      keyedHistSnapshot.content.TELEMETRY_TEST_MIRROR_FOR_LABELED_TIMING;
    Assert.ok("long time passing" in labeledTimingHist);
    // Both values, 10 and 5, are truncated by a cast in AccumulateTimeDelta
    // Minimally downcast 9. + 4. could realistically result in 13.
    Assert.greaterOrEqual(
      labeledTimingHist["long time passing"].sum,
      13,
      "Histogram's in milliseconds."
    );
    Assert.equal(
      2,
      Object.entries(labeledTimingHist["long time passing"].values).reduce(
        (acc, [, count]) => acc + count,
        0
      ),
      "Only two samples"
    );
  }
);
