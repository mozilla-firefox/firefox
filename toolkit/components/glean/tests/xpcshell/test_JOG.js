/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

function sleep(ms) {
  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  return new Promise(resolve => setTimeout(resolve, ms));
}

add_task(
  /* on Android FOG is set up through head.js */
  { skip_if: () => AppConstants.platform == "android" },
  function test_setup() {
    // FOG needs a profile directory to put its data in.
    do_get_profile();

    // We need to initialize it once, otherwise operations will be stuck in the pre-init queue.
    Services.fog.initializeFOG();
  }
);

add_task(function test_jog_counter_works() {
  Services.fog.testRegisterRuntimeMetric(
    "counter",
    "jog_cat",
    "jog_counter",
    ["test-ping"],
    `"ping"`,
    false
  );
  Glean.jogCat.jogCounter.add(53);
  Assert.equal(53, Glean.jogCat.jogCounter.testGetValue());
});

add_task(async function test_jog_string_works() {
  const value = "an active string!";
  Services.fog.testRegisterRuntimeMetric(
    "string",
    "jog_cat",
    "jog_string",
    ["test-ping"],
    `"ping"`,
    false
  );
  Glean.jogCat.jogString.set(value);

  Assert.equal(value, Glean.jogCat.jogString.testGetValue());
});

add_task(async function test_jog_string_list_works() {
  const value = "an active string!";
  const value2 = "a more active string!";
  const value3 = "the most active of strings.";
  Services.fog.testRegisterRuntimeMetric(
    "string_list",
    "jog_cat",
    "jog_string_list",
    ["test-ping"],
    `"ping"`,
    false
  );

  const jogList = [value, value2];
  Glean.jogCat.jogStringList.set(jogList);

  let val = Glean.jogCat.jogStringList.testGetValue();
  // Note: This is incredibly fragile and will break if we ever rearrange items
  // in the string list.
  Assert.deepEqual(jogList, val);

  Glean.jogCat.jogStringList.add(value3);
  Assert.ok(Glean.jogCat.jogStringList.testGetValue().includes(value3));
});

add_task(async function test_jog_timespan_works() {
  Services.fog.testRegisterRuntimeMetric(
    "timespan",
    "jog_cat",
    "jog_timespan",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({ time_unit: "millisecond" })
  );
  Glean.jogCat.jogTimespan.start();
  Glean.jogCat.jogTimespan.cancel();
  Assert.equal(undefined, Glean.jogCat.jogTimespan.testGetValue());

  // We start, briefly sleep and then stop.
  // That guarantees some time to measure.
  Glean.jogCat.jogTimespan.start();
  await sleep(10);
  Glean.jogCat.jogTimespan.stop();

  Assert.ok(Glean.jogCat.jogTimespan.testGetValue() > 0);
});

add_task(async function test_jog_uuid_works() {
  const kTestUuid = "decafdec-afde-cafd-ecaf-decafdecafde";
  Services.fog.testRegisterRuntimeMetric(
    "uuid",
    "jog_cat",
    "jog_uuid",
    ["test-ping"],
    `"ping"`,
    false
  );
  Glean.jogCat.jogUuid.set(kTestUuid);
  Assert.equal(kTestUuid, Glean.jogCat.jogUuid.testGetValue());

  Glean.jogCat.jogUuid.generateAndSet();
  // Since we generate v4 UUIDs, and the first character of the third group
  // isn't 4, this won't ever collide with kTestUuid.
  Assert.notEqual(kTestUuid, Glean.jogCat.jogUuid.testGetValue());
});

add_task(function test_jog_datetime_works() {
  const value = new Date("2020-06-11T12:00:00");
  Services.fog.testRegisterRuntimeMetric(
    "datetime",
    "jog_cat",
    "jog_datetime",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({ time_unit: "nanosecond" })
  );

  Glean.jogCat.jogDatetime.set(value.getTime() * 1000);

  const received = Glean.jogCat.jogDatetime.testGetValue();
  Assert.equal(received.getTime(), value.getTime());
});

add_task(function test_jog_boolean_works() {
  Services.fog.testRegisterRuntimeMetric(
    "boolean",
    "jog_cat",
    "jog_bool",
    ["test-ping"],
    `"ping"`,
    false
  );
  Glean.jogCat.jogBool.set(false);
  Assert.equal(false, Glean.jogCat.jogBool.testGetValue());
});

add_task(async function test_jog_event_works() {
  Services.fog.testRegisterRuntimeMetric(
    "event",
    "jog_cat",
    "jog_event_no_extra",
    ["test-ping"],
    `"ping"`,
    false
  );
  Glean.jogCat.jogEventNoExtra.record();
  var events = Glean.jogCat.jogEventNoExtra.testGetValue();
  Assert.equal(1, events.length);
  Assert.equal("jog_cat", events[0].category);
  Assert.equal("jog_event_no_extra", events[0].name);

  Services.fog.testRegisterRuntimeMetric(
    "event",
    "jog_cat",
    "jog_event",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({ allowed_extra_keys: ["extra1", "extra2"] })
  );
  let extra = { extra1: "can set extras", extra2: "passing more data" };
  Glean.jogCat.jogEvent.record(extra);
  events = Glean.jogCat.jogEvent.testGetValue();
  Assert.equal(1, events.length);
  Assert.equal("jog_cat", events[0].category);
  Assert.equal("jog_event", events[0].name);
  Assert.deepEqual(extra, events[0].extra);

  Services.fog.testRegisterRuntimeMetric(
    "event",
    "jog_cat",
    "jog_event_with_extra",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({
      allowed_extra_keys: ["extra1", "extra2", "extra3_longer_name"],
    })
  );
  let extra2 = {
    extra1: "can set extras",
    extra2: 37,
    extra3_longer_name: false,
  };
  Glean.jogCat.jogEventWithExtra.record(extra2);
  events = Glean.jogCat.jogEventWithExtra.testGetValue();
  Assert.equal(1, events.length);
  Assert.equal("jog_cat", events[0].category);
  Assert.equal("jog_event_with_extra", events[0].name);
  let expectedExtra = {
    extra1: "can set extras",
    extra2: "37",
    extra3_longer_name: "false",
  };
  Assert.deepEqual(expectedExtra, events[0].extra);

  // Invalid extra keys don't crash, the event is not recorded.
  let extra3 = {
    extra1_nonexistent_extra: "this does not crash",
  };
  Glean.jogCat.jogEventWithExtra.record(extra3);
  // And test methods throw appropriately
  Assert.throws(
    () => Glean.jogCat.jogEventWithExtra.testGetValue(),
    /DataError/
  );
});

add_task(async function test_jog_memory_distribution_works() {
  Services.fog.testRegisterRuntimeMetric(
    "memory_distribution",
    "jog_cat",
    "jog_memory_dist",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({ memory_unit: "megabyte" })
  );
  Glean.jogCat.jogMemoryDist.accumulate(7);
  Glean.jogCat.jogMemoryDist.accumulate(17);

  let data = Glean.jogCat.jogMemoryDist.testGetValue();
  // `data.sum` is in bytes, but the metric is in MB.
  Assert.equal(24 * 1024 * 1024, data.sum, "Sum's correct");
  for (let [bucket, count] of Object.entries(data.values)) {
    Assert.ok(
      count == 0 || (count == 1 && (bucket == 17520006 || bucket == 7053950)),
      "Only two buckets have a sample"
    );
  }
});

add_task(async function test_jog_custom_distribution_works() {
  Services.fog.testRegisterRuntimeMetric(
    "custom_distribution",
    "jog_cat",
    "jog_custom_dist",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({
      range_min: 1,
      range_max: 2147483646,
      bucket_count: 10,
      histogram_type: "linear",
    })
  );
  Glean.jogCat.jogCustomDist.accumulateSamples([7, 268435458]);

  let data = Glean.jogCat.jogCustomDist.testGetValue();
  Assert.equal(7 + 268435458, data.sum, "Sum's correct");
  for (let [bucket, count] of Object.entries(data.values)) {
    Assert.ok(
      count == 0 || (count == 1 && (bucket == 1 || bucket == 268435456)),
      `Only two buckets have a sample ${bucket} ${count}`
    );
  }

  // Negative values will not be recorded, instead an error is recorded.
  Glean.jogCat.jogCustomDist.accumulateSamples([-7]);
  Assert.throws(() => Glean.jogCat.jogCustomDist.testGetValue(), /DataError/);
});

add_task(async function test_jog_custom_pings() {
  Services.fog.testRegisterRuntimeMetric(
    "boolean",
    "jog_cat",
    "jog_ping_bool",
    ["jog-ping"],
    `"ping"`,
    false
  );
  Services.fog.testRegisterRuntimePing(
    "jog-ping",
    true,
    true,
    true,
    true,
    true,
    [],
    [],
    true,
    []
  );
  Assert.ok("jogPing" in GleanPings);
  let submitted = false;
  Glean.jogCat.jogPingBool.set(false);
  GleanPings.jogPing.testBeforeNextSubmit(() => {
    submitted = true;
    Assert.equal(false, Glean.jogCat.jogPingBool.testGetValue());
  });
  GleanPings.jogPing.submit();
  Assert.ok(submitted, "Ping was submitted, callback was called.");
  // ping-lifetime value was cleared.
  Assert.equal(undefined, Glean.jogCat.jogPingBool.testGetValue());
});

add_task(async function test_jog_timing_distribution_works() {
  Services.fog.testRegisterRuntimeMetric(
    "timing_distribution",
    "jog_cat",
    "jog_timing_dist",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({ time_unit: "microsecond" })
  );
  let t1 = Glean.jogCat.jogTimingDist.start();
  let t2 = Glean.jogCat.jogTimingDist.start();

  await sleep(5);

  let t3 = Glean.jogCat.jogTimingDist.start();
  Glean.jogCat.jogTimingDist.cancel(t1);

  await sleep(5);

  Glean.jogCat.jogTimingDist.stopAndAccumulate(t2); // 10ms
  Glean.jogCat.jogTimingDist.stopAndAccumulate(t3); // 5ms
  // samples are measured in microseconds, since that's the unit listed in metrics.yaml
  Glean.jogCat.jogTimingDist.accumulateSingleSample(5000); // 5ms
  Glean.jogCat.jogTimingDist.accumulateSamples([2000, 8000]); // 10ms

  let data = Glean.jogCat.jogTimingDist.testGetValue();
  const NANOS_IN_MILLIS = 1e6;
  // bug 1701949 - Sleep gets close, but sometimes doesn't wait long enough.
  const EPSILON = 40000;

  // Variance in timing makes getting the sum impossible to know.
  Assert.greater(data.sum, 30 * NANOS_IN_MILLIS - EPSILON);

  // No guarantees from timers means no guarantees on buckets.
  // But we can guarantee it's only five samples.
  Assert.equal(
    5,
    Object.entries(data.values).reduce((acc, [, count]) => acc + count, 0),
    "Only five buckets with samples"
  );
});

add_task(async function test_jog_labeled_boolean_works() {
  Services.fog.testRegisterRuntimeMetric(
    "labeled_boolean",
    "jog_cat",
    "jog_labeled_bool",
    ["test-ping"],
    `"ping"`,
    false
  );
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledBool.label_1.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.jogCat.jogLabeledBool.label_1.set(true);
  Glean.jogCat.jogLabeledBool.label_2.set(false);
  Assert.equal(true, Glean.jogCat.jogLabeledBool.label_1.testGetValue());
  Assert.equal(false, Glean.jogCat.jogLabeledBool.label_2.testGetValue());
  // What about invalid/__other__?
  Assert.equal(undefined, Glean.jogCat.jogLabeledBool.__other__.testGetValue());
  Glean.jogCat.jogLabeledBool.NowValidLabel.set(true);
  Assert.ok(Glean.jogCat.jogLabeledBool.NowValidLabel.testGetValue());
  Glean.jogCat.jogLabeledBool["1".repeat(112)].set(true);
  Assert.throws(
    () => Glean.jogCat.jogLabeledBool.__other__.testGetValue(),
    /DataError/,
    "Should throw because of a recording error."
  );
});

add_task(async function test_jog_labeled_boolean_with_static_labels_works() {
  Services.fog.testRegisterRuntimeMetric(
    "labeled_boolean",
    "jog_cat",
    "jog_labeled_bool_with_labels",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({ ordered_labels: ["label_1", "label_2"] })
  );
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledBoolWithLabels.label_1.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.jogCat.jogLabeledBoolWithLabels.label_1.set(true);
  Glean.jogCat.jogLabeledBoolWithLabels.label_2.set(false);
  Assert.equal(
    true,
    Glean.jogCat.jogLabeledBoolWithLabels.label_1.testGetValue()
  );
  Assert.equal(
    false,
    Glean.jogCat.jogLabeledBoolWithLabels.label_2.testGetValue()
  );
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledBoolWithLabels.__other__.testGetValue()
  );
  Glean.jogCat.jogLabeledBoolWithLabels.label_3.set(true);
  Assert.equal(
    true,
    Glean.jogCat.jogLabeledBoolWithLabels.__other__.testGetValue()
  );
  // TODO: Test that we have the right number and type of errors (bug 1683171)
});

add_task(async function test_jog_labeled_counter_works() {
  Services.fog.testRegisterRuntimeMetric(
    "labeled_counter",
    "jog_cat",
    "jog_labeled_counter",
    ["test-ping"],
    `"ping"`,
    false
  );
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledCounter.label_1.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.jogCat.jogLabeledCounter.label_1.add(1);
  Glean.jogCat.jogLabeledCounter.label_2.add(2);
  Assert.equal(1, Glean.jogCat.jogLabeledCounter.label_1.testGetValue());
  Assert.equal(2, Glean.jogCat.jogLabeledCounter.label_2.testGetValue());
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledCounter.__other__.testGetValue()
  );
  Glean.jogCat.jogLabeledCounter["1".repeat(112)].add(1);
  Assert.throws(
    () => Glean.jogCat.jogLabeledCounter.__other__.testGetValue(),
    /DataError/,
    "Should throw because of a recording error."
  );
});

add_task(async function test_jog_labeled_counter_with_static_labels_works() {
  Services.fog.testRegisterRuntimeMetric(
    "labeled_counter",
    "jog_cat",
    "jog_labeled_counter_with_labels",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({ ordered_labels: ["label_1", "label_2"] })
  );
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledCounterWithLabels.label_1.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.jogCat.jogLabeledCounterWithLabels.label_1.add(1);
  Glean.jogCat.jogLabeledCounterWithLabels.label_2.add(2);
  Assert.equal(
    1,
    Glean.jogCat.jogLabeledCounterWithLabels.label_1.testGetValue()
  );
  Assert.equal(
    2,
    Glean.jogCat.jogLabeledCounterWithLabels.label_2.testGetValue()
  );
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledCounterWithLabels.__other__.testGetValue()
  );
  Glean.jogCat.jogLabeledCounterWithLabels["1".repeat(112)].add(1);
  // TODO:(bug 1766515) - This should throw.
  /*Assert.throws(
    () => Glean.jogCat.jogLabeledCounterWithLabels.__other__.testGetValue(),
    /DataError/,
    "Should throw because of a recording error."
  );*/
  Assert.equal(
    1,
    Glean.jogCat.jogLabeledCounterWithLabels.__other__.testGetValue()
  );
});

add_task(async function test_jog_labeled_string_works() {
  Services.fog.testRegisterRuntimeMetric(
    "labeled_string",
    "jog_cat",
    "jog_labeled_string",
    ["test-ping"],
    `"ping"`,
    false
  );
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledString.label_1.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.jogCat.jogLabeledString.label_1.set("crimson");
  Glean.jogCat.jogLabeledString.label_2.set("various");
  Assert.equal("crimson", Glean.jogCat.jogLabeledString.label_1.testGetValue());
  Assert.equal("various", Glean.jogCat.jogLabeledString.label_2.testGetValue());
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledString.__other__.testGetValue()
  );
  Glean.jogCat.jogLabeledString["1".repeat(112)].set("valid");
  Assert.throws(
    () => Glean.jogCat.jogLabeledString.__other__.testGetValue(),
    /DataError/
  );
});

add_task(async function test_jog_labeled_string_with_labels_works() {
  Services.fog.testRegisterRuntimeMetric(
    "labeled_string",
    "jog_cat",
    "jog_labeled_string_with_labels",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({ ordered_labels: ["label_1", "label_2"] })
  );
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledStringWithLabels.label_1.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.jogCat.jogLabeledStringWithLabels.label_1.set("crimson");
  Glean.jogCat.jogLabeledStringWithLabels.label_2.set("various");
  Assert.equal(
    "crimson",
    Glean.jogCat.jogLabeledStringWithLabels.label_1.testGetValue()
  );
  Assert.equal(
    "various",
    Glean.jogCat.jogLabeledStringWithLabels.label_2.testGetValue()
  );
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledStringWithLabels.__other__.testGetValue()
  );
  Glean.jogCat.jogLabeledStringWithLabels["1".repeat(112)].set("valid");
  // TODO:(bug 1766515) - This should throw.
  /*Assert.throws(
    () => Glean.jogCat.jogLabeledStringWithLabels.__other__.testGetValue(),
    /DataError/
  );*/
  Assert.equal(
    "valid",
    Glean.jogCat.jogLabeledStringWithLabels.__other__.testGetValue()
  );
});

add_task(function test_jog_quantity_works() {
  Services.fog.testRegisterRuntimeMetric(
    "quantity",
    "jog_cat",
    "jog_quantity",
    ["test-ping"],
    `"ping"`,
    false
  );
  Glean.jogCat.jogQuantity.set(42);
  Assert.equal(42, Glean.jogCat.jogQuantity.testGetValue());
});

add_task(function test_jog_rate_works() {
  Services.fog.testRegisterRuntimeMetric(
    "rate",
    "jog_cat",
    "jog_rate",
    ["test-ping"],
    `"ping"`,
    false
  );
  // 1) Standard rate with internal denominator
  Glean.jogCat.jogRate.addToNumerator(22);
  Glean.jogCat.jogRate.addToDenominator(7);
  Assert.deepEqual(
    { numerator: 22, denominator: 7 },
    Glean.jogCat.jogRate.testGetValue()
  );

  Services.fog.testRegisterRuntimeMetric(
    "denominator",
    "jog_cat",
    "jog_denominator",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({
      numerators: [
        {
          name: "jog_rate_ext",
          category: "jog_cat",
          send_in_pings: ["test-ping"],
          lifetime: "ping",
          disabled: false,
        },
      ],
    })
  );
  Services.fog.testRegisterRuntimeMetric(
    "rate",
    "jog_cat",
    "jog_rate_ext",
    ["test-ping"],
    `"ping"`,
    false
  );
  // 2) Rate with external denominator
  Glean.jogCat.jogDenominator.add(11);
  Glean.jogCat.jogRateExt.addToNumerator(121);
  Assert.equal(11, Glean.jogCat.jogDenominator.testGetValue());
  Assert.deepEqual(
    { numerator: 121, denominator: 11 },
    Glean.jogCat.jogRateExt.testGetValue()
  );
});

add_task(function test_jog_dotted_categories_work() {
  Services.fog.testRegisterRuntimeMetric(
    "counter",
    "jog_cat.dotted",
    "jog_counter",
    ["test-ping"],
    `"ping"`,
    false
  );
  Glean.jogCatDotted.jogCounter.add(314);
  Assert.equal(314, Glean.jogCatDotted.jogCounter.testGetValue());
});

add_task(async function test_jog_ping_works() {
  const kReason = "reason-1";
  Services.fog.testRegisterRuntimePing(
    "my-ping",
    true,
    true,
    true,
    true,
    true,
    [],
    [kReason],
    true,
    []
  );
  let submitted = false;
  GleanPings.myPing.testBeforeNextSubmit(reason => {
    submitted = true;
    Assert.equal(kReason, reason);
  });
  GleanPings.myPing.submit("reason-1");
  Assert.ok(submitted, "Ping must have been submitted");
});

add_task(async function test_jog_noinfo_ping_works() {
  const kReason = "reason-1";
  Services.fog.testRegisterRuntimePing(
    "noinfo-ping",
    true,
    true,
    true,
    false,
    true,
    [],
    [kReason],
    true,
    []
  );
  let submitted = false;
  GleanPings.noinfoPing.testBeforeNextSubmit(reason => {
    submitted = true;
    Assert.equal(kReason, reason);
  });
  GleanPings.noinfoPing.submit("reason-1");
  Assert.ok(submitted, "Ping must have been submitted");
});

add_task(function test_jog_name_collision() {
  Assert.ok("aCounter" in Glean.testOnlyJog);
  Assert.equal(undefined, Glean.testOnlyJog.aCounter.testGetValue());
  const kValue = 42;
  Glean.testOnlyJog.aCounter.add(kValue);
  Assert.equal(kValue, Glean.testOnlyJog.aCounter.testGetValue());

  // Let's overwrite the test_only.jog.a_counter counter.
  Services.fog.testRegisterRuntimeMetric(
    "counter",
    "test_only.jog",
    "a_counter",
    ["test-ping"],
    `"ping"`,
    true // changing the metric to disabled.
  );

  Assert.ok("aCounter" in Glean.testOnlyJog);
  Assert.equal(kValue, Glean.testOnlyJog.aCounter.testGetValue());
  Glean.testOnlyJog.aCounter.add(kValue);
  Assert.equal(
    kValue,
    Glean.testOnlyJog.aCounter.testGetValue(),
    "value of now-disabled metric remains unchanged."
  );

  // Now let's mess with events:
  Assert.ok("anEvent" in Glean.testOnlyJog);
  Assert.equal(undefined, Glean.testOnlyJog.anEvent.testGetValue());
  const extra12 = {
    extra1: "a value",
    extra2: "another value",
  };
  Glean.testOnlyJog.anEvent.record(extra12);
  Assert.deepEqual(extra12, Glean.testOnlyJog.anEvent.testGetValue()[0].extra);
  Services.fog.testRegisterRuntimeMetric(
    "event",
    "test_only.jog",
    "an_event",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({ allowed_extra_keys: ["extra1", "extra2", "extra3"] }) // New extra key just dropped
  );
  const extra123 = {
    extra1: "different value",
    extra2: "another different value",
    extra3: 42,
  };
  Glean.testOnlyJog.anEvent.record(extra123);
  Assert.deepEqual(extra123, Glean.testOnlyJog.anEvent.testGetValue()[1].extra);
});

add_task(function test_enumerable_names() {
  Assert.ok(Object.keys(Glean).includes("testOnlyJog"));
  Assert.ok(Object.keys(Glean.testOnlyJog).includes("aCounter"));
  Assert.ok(Object.keys(GleanPings).includes("testPing"));
});

add_task(async function test_jog_text_works() {
  const kValue =
    "In the heart of the Opéra district in Paris, the Cédric Grolet Opéra bakery-pastry shop is a veritable temple of gourmet delights.";
  Services.fog.testRegisterRuntimeMetric(
    "text",
    "test_only.jog",
    "a_text",
    ["test-ping"],
    `"ping"`,
    false
  );
  Glean.testOnlyJog.aText.set(kValue);

  Assert.equal(kValue, Glean.testOnlyJog.aText.testGetValue());
});

add_task(async function test_jog_custom_distribution_works() {
  Services.fog.testRegisterRuntimeMetric(
    "labeled_custom_distribution",
    "jog_cat",
    "jog_labeled_custom_dist",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({
      range_min: 1,
      range_max: 2147483646,
      bucket_count: 10,
      histogram_type: "linear",
    })
  );
  Glean.jogCat.jogLabeledCustomDist.label_1.accumulateSamples([7, 268435458]);

  let data = Glean.jogCat.jogLabeledCustomDist.label_1.testGetValue();
  Assert.equal(7 + 268435458, data.sum, "Sum's correct");
  for (let [bucket, count] of Object.entries(data.values)) {
    Assert.ok(
      count == 0 || (count == 1 && (bucket == 1 || bucket == 268435456)),
      `Only two buckets have a sample ${bucket} ${count}`
    );
  }

  // Negative values will not be recorded, instead an error is recorded.
  Glean.jogCat.jogLabeledCustomDist.label_1.accumulateSamples([-7]);
  Assert.throws(
    () => Glean.jogCat.jogLabeledCustomDist.label_1.testGetValue(),
    /DataError/
  );
});

add_task(async function test_jog_labeled_memory_distribution_works() {
  Services.fog.testRegisterRuntimeMetric(
    "labeled_memory_distribution",
    "jog_cat",
    "jog_labeled_memory_dist",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({ memory_unit: "megabyte" })
  );
  Glean.jogCat.jogLabeledMemoryDist.short_term.accumulate(7);
  Glean.jogCat.jogLabeledMemoryDist.short_term.accumulate(17);

  let data = Glean.jogCat.jogLabeledMemoryDist.short_term.testGetValue();
  // `data.sum` is in bytes, but the metric is in MB.
  Assert.equal(24 * 1024 * 1024, data.sum, "Sum's correct");
  for (let [bucket, count] of Object.entries(data.values)) {
    Assert.ok(
      count == 0 || (count == 1 && (bucket == 17520006 || bucket == 7053950)),
      "Only two buckets have a sample"
    );
  }
});

add_task(async function test_jog_labeled_timing_distribution_works() {
  Services.fog.testRegisterRuntimeMetric(
    "labeled_timing_distribution",
    "jog_cat",
    "jog_labeled_timing_dist",
    ["test-ping"],
    `"ping"`,
    false,
    JSON.stringify({ time_unit: "microsecond" })
  );
  let t1 = Glean.jogCat.jogLabeledTimingDist.label1.start();
  let t2 = Glean.jogCat.jogLabeledTimingDist.label1.start();

  await sleep(5);

  let t3 = Glean.jogCat.jogLabeledTimingDist.label1.start();
  Glean.jogCat.jogLabeledTimingDist.label1.cancel(t1);

  await sleep(5);

  Glean.jogCat.jogLabeledTimingDist.label1.stopAndAccumulate(t2); // 10ms
  Glean.jogCat.jogLabeledTimingDist.label1.stopAndAccumulate(t3); // 5ms

  let data = Glean.jogCat.jogLabeledTimingDist.label1.testGetValue();
  const NANOS_IN_MILLIS = 1e6;
  // bug 1701949 - Sleep gets close, but sometimes doesn't wait long enough.
  const EPSILON = 40000;

  // Variance in timing makes getting the sum impossible to know.
  Assert.greater(data.sum, 15 * NANOS_IN_MILLIS - EPSILON);

  // No guarantees from timers means no guarantees on buckets.
  // But we can guarantee it's only two samples.
  Assert.equal(
    2,
    Object.entries(data.values).reduce((acc, [, count]) => acc + count, 0),
    "Only two buckets with samples"
  );
});

add_task(async function test_jog_labeled_quantity_works() {
  Services.fog.testRegisterRuntimeMetric(
    "labeled_quantity",
    "jog_cat",
    "jog_labeled_quantity",
    ["test-ping"],
    `"ping"`,
    false
  );
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledQuantity.label_1.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.jogCat.jogLabeledQuantity.label_1.set(9000);
  Glean.jogCat.jogLabeledQuantity.label_2.set(0);
  Assert.equal(9000, Glean.jogCat.jogLabeledQuantity.label_1.testGetValue());
  Assert.equal(0, Glean.jogCat.jogLabeledQuantity.label_2.testGetValue());
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.jogCat.jogLabeledQuantity.__other__.testGetValue()
  );
  Glean.jogCat.jogLabeledQuantity.NowValidLabel.set(100);
  Assert.equal(
    100,
    Glean.jogCat.jogLabeledQuantity.NowValidLabel.testGetValue()
  );
  Glean.jogCat.jogLabeledQuantity["1".repeat(112)].set(true);
  Assert.throws(
    () => Glean.jogCat.jogLabeledQuantity.__other__.testGetValue(),
    /DataError/,
    "Should throw because of a recording error."
  );
});

add_task(function test_disabled_ping_with_test_reset() {
  Services.fog.testResetFOG();
  Services.fog.testRegisterRuntimeMetric(
    "counter",
    "jog_cat",
    "jog_counter",
    ["disabled-ping"],
    `"ping"`,
    false
  );
  Glean.jogCat.jogCounter.add(1);
  Assert.equal(
    undefined,
    Glean.jogCat.jogCounter.testGetValue(),
    "Metric in disabled ping stores no value."
  );
  GleanPings.disabledPing.setEnabled(true);
  Glean.jogCat.jogCounter.add(42);
  Assert.equal(
    42,
    Glean.jogCat.jogCounter.testGetValue(),
    "Metric in now-enabled ping stores its value."
  );
});
