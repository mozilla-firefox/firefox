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

add_setup(
  /* on Android FOG is set up through head.js */
  { skip_if: () => AppConstants.platform == "android" },
  function test_setup() {
    // FOG needs a profile directory to put its data in.
    do_get_profile();

    // We need to initialize it once, otherwise operations will be stuck in the pre-init queue.
    Services.fog.initializeFOG();
  }
);

add_task(function test_fog_counter_works() {
  Glean.testOnly.badCode.add(31);
  Assert.equal(31, Glean.testOnly.badCode.testGetValue("test-ping"));
});

add_task(async function test_fog_string_works() {
  Assert.equal(null, Glean.testOnly.cheesyString.testGetValue());

  // Setting `undefined` will be ignored.
  Glean.testOnly.cheesyString.set(undefined);
  Assert.equal(null, Glean.testOnly.cheesyString.testGetValue());

  const value = "a cheesy string!";
  Glean.testOnly.cheesyString.set(value);

  Assert.equal(value, Glean.testOnly.cheesyString.testGetValue("test-ping"));
});

add_task(async function test_fog_string_list_works() {
  const value = "a cheesy string!";
  const value2 = "a cheesier string!";
  const value3 = "the cheeziest of strings.";

  const cheeseList = [value, value2];
  Glean.testOnly.cheesyStringList.set(cheeseList);

  let val = Glean.testOnly.cheesyStringList.testGetValue();
  // Note: This is incredibly fragile and will break if we ever rearrange items
  // in the string list.
  Assert.deepEqual(cheeseList, val);

  Glean.testOnly.cheesyStringList.add(value3);
  Assert.ok(Glean.testOnly.cheesyStringList.testGetValue().includes(value3));
});

add_task(async function test_fog_timespan_works() {
  Glean.testOnly.canWeTimeIt.start();
  Glean.testOnly.canWeTimeIt.cancel();
  Assert.equal(undefined, Glean.testOnly.canWeTimeIt.testGetValue());

  // We start, briefly sleep and then stop.
  // That guarantees some time to measure.
  Glean.testOnly.canWeTimeIt.start();
  await sleep(10);
  Glean.testOnly.canWeTimeIt.stop();

  Assert.ok(Glean.testOnly.canWeTimeIt.testGetValue("test-ping") > 0);
});

add_task(async function test_fog_timespan_throws_on_stop_wout_start() {
  Glean.testOnly.canWeTimeIt.stop();
  Assert.throws(
    () => Glean.testOnly.canWeTimeIt.testGetValue(),
    /DataError/,
    "Should throw because stop was called without start."
  );
});

add_task(async function test_fog_uuid_works() {
  const kTestUuid = "decafdec-afde-cafd-ecaf-decafdecafde";
  Glean.testOnly.whatIdIt.set(kTestUuid);
  Assert.equal(kTestUuid, Glean.testOnly.whatIdIt.testGetValue("test-ping"));

  Glean.testOnly.whatIdIt.generateAndSet();
  // Since we generate v4 UUIDs, and the first character of the third group
  // isn't 4, this won't ever collide with kTestUuid.
  Assert.notEqual(kTestUuid, Glean.testOnly.whatIdIt.testGetValue("test-ping"));
});

add_task(function test_fog_datetime_works() {
  const value = new Date("2020-06-11T12:00:00");

  Glean.testOnly.whatADate.set(value.getTime() * 1000);

  const received = Glean.testOnly.whatADate.testGetValue("test-ping");
  Assert.equal(received.getTime(), value.getTime());
});

add_task(function test_fog_boolean_works() {
  Glean.testOnly.canWeFlagIt.set(false);
  Assert.equal(false, Glean.testOnly.canWeFlagIt.testGetValue("test-ping"));
  // While you're here, might as well test that the ping name's optional.
  Assert.equal(false, Glean.testOnly.canWeFlagIt.testGetValue());
});

add_task(async function test_fog_event_works() {
  Glean.testOnlyIpc.noExtraEvent.record();
  var events = Glean.testOnlyIpc.noExtraEvent.testGetValue();
  Assert.equal(1, events.length);
  Assert.equal("test_only.ipc", events[0].category);
  Assert.equal("no_extra_event", events[0].name);

  let extra = { extra1: "can set extras", extra2: "passing more data" };
  Glean.testOnlyIpc.anEvent.record(extra);
  events = Glean.testOnlyIpc.anEvent.testGetValue();
  Assert.equal(1, events.length);
  Assert.equal("test_only.ipc", events[0].category);
  Assert.equal("an_event", events[0].name);
  Assert.deepEqual(extra, events[0].extra);

  // Corner case: Event with extra with `undefined` value.
  // Should pretend that extra key isn't there.
  extra = { extra1: undefined, extra2: "defined" };
  Glean.testOnlyIpc.anEvent.record(extra);
  events = Glean.testOnlyIpc.anEvent.testGetValue();
  Assert.equal(2, events.length);
  Assert.deepEqual({ extra2: "defined" }, events[1].extra);

  let extra2 = {
    extra1: "can set extras",
    extra2: 37,
    extra3_longer_name: false,
  };
  Glean.testOnlyIpc.eventWithExtra.record(extra2);
  events = Glean.testOnlyIpc.eventWithExtra.testGetValue();
  Assert.equal(1, events.length);
  Assert.equal("test_only.ipc", events[0].category);
  Assert.equal("event_with_extra", events[0].name);
  let expectedExtra = {
    extra1: "can set extras",
    extra2: "37",
    extra3_longer_name: "false",
  };
  Assert.deepEqual(expectedExtra, events[0].extra);

  // camelCase extras work.
  let extra5 = {
    extra4CamelCase: false,
  };
  Glean.testOnlyIpc.eventWithExtra.record(extra5);
  events = Glean.testOnlyIpc.eventWithExtra.testGetValue();
  Assert.equal(2, events.length, "Recorded one event too many.");
  expectedExtra = {
    extra4CamelCase: "false",
  };
  Assert.deepEqual(expectedExtra, events[1].extra);

  // Passing `null` works.
  Glean.testOnlyIpc.eventWithExtra.record(null);
  events = Glean.testOnlyIpc.eventWithExtra.testGetValue();
  Assert.equal(3, events.length, "Recorded another event.");
  Assert.equal(events[2].extra, null);

  // Invalid extra keys don't crash, the event is not recorded,
  // but an error is recorded.
  let extra3 = {
    extra1_nonexistent_extra: "this does not crash",
  };
  Glean.testOnlyIpc.eventWithExtra.record(extra3);
  Assert.throws(
    () => Glean.testOnlyIpc.eventWithExtra.testGetValue(),
    /DataError/,
    "Should throw because of a recording error."
  );

  // Supplying extras when there aren't any defined results in the event not
  // being recorded, but an error is.
  Glean.testOnlyIpc.noExtraEvent.record(extra3);
  Assert.throws(
    () => Glean.testOnlyIpc.eventWithExtra.testGetValue(),
    /DataError/,
    "Should throw because of a recording error."
  );
});

add_task(async function test_fog_memory_distribution_works() {
  Glean.testOnly.doYouRemember.accumulate(7);
  Glean.testOnly.doYouRemember.accumulate(17);

  let data = Glean.testOnly.doYouRemember.testGetValue("test-ping");
  Assert.equal(2, data.count, "Count of entries is correct");
  // `data.sum` is in bytes, but the metric is in MB.
  Assert.equal(24 * 1024 * 1024, data.sum, "Sum's correct");
  for (let [bucket, count] of Object.entries(data.values)) {
    Assert.ok(
      count == 0 || (count == 1 && (bucket == 17520006 || bucket == 7053950)),
      "Only two buckets have a sample"
    );
  }
});

add_task(async function test_fog_custom_distribution_works() {
  Glean.testOnlyIpc.aCustomDist.accumulateSamples([7, 268435458]);

  let data = Glean.testOnlyIpc.aCustomDist.testGetValue("test-ping");
  Assert.equal(2, data.count, "Count of entries is correct");
  Assert.equal(7 + 268435458, data.sum, "Sum's correct");
  for (let [bucket, count] of Object.entries(data.values)) {
    Assert.ok(
      count == 0 || (count == 1 && (bucket == 1 || bucket == 268435456)),
      `Only two buckets have a sample ${bucket} ${count}`
    );
  }

  // Negative values will not be recorded, instead an error is recorded.
  Glean.testOnlyIpc.aCustomDist.accumulateSamples([-7]);
  Assert.throws(
    () => Glean.testOnlyIpc.aCustomDist.testGetValue(),
    /DataError/
  );
});

add_task(function test_fog_custom_pings() {
  Assert.ok("onePingOnly" in GleanPings);
  let submitted = false;
  Glean.testOnly.onePingOneBool.set(false);
  GleanPings.onePingOnly.testBeforeNextSubmit(() => {
    submitted = true;
    Assert.equal(false, Glean.testOnly.onePingOneBool.testGetValue());
  });
  GleanPings.onePingOnly.submit();
  Assert.ok(submitted, "Ping was submitted, callback was called.");
});

add_task(function test_recursive_testBeforeNextSubmit() {
  Assert.ok("onePingOnly" in GleanPings);
  let submitted = 0;
  let rec = () => {
    submitted++;
    GleanPings.onePingOnly.testBeforeNextSubmit(rec);
  };
  GleanPings.onePingOnly.testBeforeNextSubmit(rec);
  GleanPings.onePingOnly.submit();
  GleanPings.onePingOnly.submit();
  GleanPings.onePingOnly.submit();
  Assert.equal(3, submitted, "Ping was submitted 3 times");
  // Be kind and remove the callback.
  GleanPings.onePingOnly.testBeforeNextSubmit(() => {});
});

add_task(async function test_fog_timing_distribution_works() {
  let t1 = Glean.testOnly.whatTimeIsIt.start();
  let t2 = Glean.testOnly.whatTimeIsIt.start();

  await sleep(5);

  let t3 = Glean.testOnly.whatTimeIsIt.start();
  Glean.testOnly.whatTimeIsIt.cancel(t1);

  await sleep(5);

  Glean.testOnly.whatTimeIsIt.stopAndAccumulate(t2); // 10ms
  Glean.testOnly.whatTimeIsIt.stopAndAccumulate(t3); // 5ms
  // samples are measured in microseconds, since that's the unit listed in metrics.yaml
  Glean.testOnly.whatTimeIsIt.accumulateSingleSample(5000); // 5ms
  Glean.testOnly.whatTimeIsIt.accumulateSamples([2000, 8000]); // 10ms

  let data = Glean.testOnly.whatTimeIsIt.testGetValue();

  // Cancelled timers should not be counted.
  Assert.equal(5, data.count, "Count of entries is correct");

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

add_task(async function test_fog_labels_conform() {
  Glean.testOnly.mabelsLabelMaker.singleword.set("portmanteau");
  Assert.equal(
    "portmanteau",
    Glean.testOnly.mabelsLabelMaker.singleword.testGetValue()
  );
  Glean.testOnly.mabelsLabelMaker.snake_case.set("snek");
  Assert.equal(
    "snek",
    Glean.testOnly.mabelsLabelMaker.snake_case.testGetValue()
  );
  Glean.testOnly.mabelsLabelMaker["dash-character"].set("Dash Rendar");
  Assert.equal(
    "Dash Rendar",
    Glean.testOnly.mabelsLabelMaker["dash-character"].testGetValue()
  );
  Glean.testOnly.mabelsLabelMaker["dot.separated"].set("dot product");
  Assert.equal(
    "dot product",
    Glean.testOnly.mabelsLabelMaker["dot.separated"].testGetValue()
  );
  Glean.testOnly.mabelsLabelMaker.camelCase.set("wednesday");
  Assert.equal(
    "wednesday",
    Glean.testOnly.mabelsLabelMaker.camelCase.testGetValue()
  );
  const veryLong = "1".repeat(112);
  Glean.testOnly.mabelsLabelMaker[veryLong].set("seventy-two");
  Assert.throws(
    () => Glean.testOnly.mabelsLabelMaker[veryLong].testGetValue(),
    /DataError/,
    "Should throw because of an invalid label."
  );
  // This test should _now_ throw because we are calling data after an invalid
  // label has been set.
  Assert.throws(
    () => Glean.testOnly.mabelsLabelMaker["dot.separated"].testGetValue(),
    /DataError/,
    "Should throw because of an invalid label."
  );
});

add_task(async function test_fog_labeled_boolean_works() {
  Assert.equal(
    undefined,
    Glean.testOnly.mabelsLikeBalloons.at_parties.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.testOnly.mabelsLikeBalloons.at_parties.set(true);
  Glean.testOnly.mabelsLikeBalloons.at_funerals.set(false);
  Assert.equal(
    true,
    Glean.testOnly.mabelsLikeBalloons.at_parties.testGetValue()
  );
  Assert.equal(
    false,
    Glean.testOnly.mabelsLikeBalloons.at_funerals.testGetValue()
  );
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.testOnly.mabelsLikeBalloons.__other__.testGetValue()
  );
  Glean.testOnly.mabelsLikeBalloons["1".repeat(112)].set(true);
  Assert.throws(
    () => Glean.testOnly.mabelsLikeBalloons.__other__.testGetValue(),
    /DataError/,
    "Should throw because of a recording error."
  );
});

add_task(async function test_fog_labeled_counter_works() {
  Assert.equal(
    undefined,
    Glean.testOnly.mabelsKitchenCounters.near_the_sink.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.testOnly.mabelsKitchenCounters.near_the_sink.add(1);
  Glean.testOnly.mabelsKitchenCounters.with_junk_on_them.add(2);
  Assert.equal(
    1,
    Glean.testOnly.mabelsKitchenCounters.near_the_sink.testGetValue()
  );
  Assert.equal(
    2,
    Glean.testOnly.mabelsKitchenCounters.with_junk_on_them.testGetValue()
  );
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.testOnly.mabelsKitchenCounters.__other__.testGetValue()
  );
  Glean.testOnly.mabelsKitchenCounters["1".repeat(112)].add(1);
  Assert.throws(
    () => Glean.testOnly.mabelsKitchenCounters.__other__.testGetValue(),
    /DataError/,
    "Should throw because of a recording error."
  );
});

add_task(async function test_fog_labeled_string_works() {
  Assert.equal(
    undefined,
    Glean.testOnly.mabelsBalloonStrings.colour_of_99.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.testOnly.mabelsBalloonStrings.colour_of_99.set("crimson");
  Glean.testOnly.mabelsBalloonStrings.string_lengths.set("various");
  Assert.equal(
    "crimson",
    Glean.testOnly.mabelsBalloonStrings.colour_of_99.testGetValue()
  );
  Assert.equal(
    "various",
    Glean.testOnly.mabelsBalloonStrings.string_lengths.testGetValue()
  );
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.testOnly.mabelsBalloonStrings.__other__.testGetValue()
  );
  Glean.testOnly.mabelsBalloonStrings["1".repeat(112)].set("valid");
  Assert.throws(
    () => Glean.testOnly.mabelsBalloonStrings.__other__.testGetValue(),
    /DataError/
  );
});

add_task(function test_fog_quantity_works() {
  Glean.testOnly.meaningOfLife.set(42);
  Assert.equal(42, Glean.testOnly.meaningOfLife.testGetValue());
});

add_task(function test_fog_rate_works() {
  // 1) Standard rate with internal denominator
  Glean.testOnlyIpc.irate.addToNumerator(22);
  Glean.testOnlyIpc.irate.addToDenominator(7);
  Assert.deepEqual(
    { numerator: 22, denominator: 7 },
    Glean.testOnlyIpc.irate.testGetValue()
  );

  // 2) Rate with external denominator
  Glean.testOnlyIpc.anExternalDenominator.add(11);
  Glean.testOnlyIpc.rateWithExternalDenominator.addToNumerator(121);
  Assert.equal(11, Glean.testOnlyIpc.anExternalDenominator.testGetValue());
  Assert.deepEqual(
    { numerator: 121, denominator: 11 },
    Glean.testOnlyIpc.rateWithExternalDenominator.testGetValue()
  );
});

add_task(async function test_fog_url_works() {
  const value = "https://www.example.com/fog";
  Glean.testOnlyIpc.aUrl.set(value);

  Assert.equal(value, Glean.testOnlyIpc.aUrl.testGetValue("test-ping"));
});

add_task(async function test_fog_text_works() {
  const value =
    "Before the risin' sun, we fly, So many roads to choose, We'll start out walkin' and learn to run, (We've only just begun)";
  Glean.testOnlyIpc.aText.set(value);

  let rslt = Glean.testOnlyIpc.aText.testGetValue();

  Assert.equal(value, rslt);

  Assert.equal(121, rslt.length);
});

add_task(async function test_fog_text_works_unusual_character() {
  const value =
    "The secret to Dominique Ansel's viennoiserie is the use of Isigny Sainte-Mère butter and Les Grands Moulins de Paris flour";
  Glean.testOnlyIpc.aText.set(value);

  let rslt = Glean.testOnlyIpc.aText.testGetValue();

  Assert.equal(value, rslt);

  Assert.greater(rslt.length, 100);
});

add_task(async function test_fog_object_works() {
  Assert.equal(
    undefined,
    Glean.testOnly.balloons.testGetValue(),
    "No object stored"
  );

  // Can't store not-objects.
  let invalidValues = [1, "str", false, undefined, null, NaN, Infinity];
  for (let value of invalidValues) {
    Assert.throws(
      () => Glean.testOnly.balloons.set(value),
      /is not an object/,
      "Should throw a type error"
    );
  }

  // No invalid value will be stored.
  Assert.equal(
    undefined,
    Glean.testOnly.balloons.testGetValue(),
    "No object stored"
  );

  // `JS_Stringify` internally throws
  // an `TypeError: cyclic object value` exception.
  // That's cleared and `set` should not throw on it.
  // This eventually should log a proper error in Glean.
  let selfref = {};
  selfref.a = selfref;
  Glean.testOnly.balloons.set(selfref);
  Assert.equal(
    undefined,
    Glean.testOnly.balloons.testGetValue(),
    "No object stored"
  );

  let balloons = [
    { colour: "red", diameter: 5 },
    { colour: "blue", diameter: 7 },
    { colour: "orange" },
  ];
  Glean.testOnly.balloons.set(balloons);

  let result = Glean.testOnly.balloons.testGetValue();
  let expected = [
    { colour: "red", diameter: 5 },
    { colour: "blue", diameter: 7 },
    { colour: "orange" },
  ];
  Assert.deepEqual(expected, result);
});

add_task(
  // FIXME(bug 1947194): JOG object metrics don't do schema validation yet
  {
    skip_if: () =>
      Services.prefs.getBoolPref("telemetry.fog.artifact_build", false),
  },
  async function test_fog_object_verifies_structure() {
    // These values are coerced to null or removed.
    let balloons = [
      { colour: "inf", diameter: Infinity },
      { colour: "negative-inf", diameter: -1 / 0 },
      { colour: "nan", diameter: NaN },
      { colour: "undef", diameter: undefined },
    ];
    Glean.testOnly.balloons.set(balloons);
    let result = Glean.testOnly.balloons.testGetValue();
    let expected = [
      { colour: "inf" },
      { colour: "negative-inf" },
      { colour: "nan" },
      { colour: "undef" },
    ];
    Assert.deepEqual(expected, result);

    // colour != color.
    let invalid = [{ color: "orange" }, { color: "red", diameter: "small" }];
    Glean.testOnly.balloons.set(invalid);
    Assert.throws(
      () => Glean.testOnly.balloons.testGetValue(),
      /invalid_value/,
      "Should throw because last object was invalid."
    );

    Services.fog.testResetFOG();
    // set again to ensure it's stored
    balloons = [
      { colour: "red", diameter: 5 },
      { colour: "blue", diameter: 7 },
    ];
    Glean.testOnly.balloons.set(balloons);
    result = Glean.testOnly.balloons.testGetValue();
    Assert.deepEqual(balloons, result);

    invalid = [{ colour: "red", diameter: 5, extra: "field" }];
    Glean.testOnly.balloons.set(invalid);
    Assert.throws(
      () => Glean.testOnly.balloons.testGetValue(),
      /invalid_value/,
      "Should throw because last object was invalid."
    );
  }
);

add_task(async function test_fog_complex_object_works() {
  if (!Glean.testOnly.crashStack) {
    // FIXME(bug 1883857): object metric type not available, e.g. in artifact builds.
    // Skipping this test.
    return;
  }

  Assert.equal(
    undefined,
    Glean.testOnly.crashStack.testGetValue(),
    "No object stored"
  );

  Glean.testOnly.crashStack.set({});
  let result = Glean.testOnly.crashStack.testGetValue();
  Assert.deepEqual({}, result);

  let stack = {
    status: "OK",
    crash_info: {
      typ: "main",
      address: "0xf001ba11",
      crashing_thread: 1,
    },
    main_module: 0,
    modules: [
      {
        base_addr: "0x00000000",
        end_addr: "0x00004000",
      },
    ],
  };

  Glean.testOnly.crashStack.set(stack);
  result = Glean.testOnly.crashStack.testGetValue();
  Assert.deepEqual(stack, result);

  stack = {
    status: "OK",
    modules: [
      {
        base_addr: "0x00000000",
        end_addr: "0x00004000",
      },
    ],
  };
  Glean.testOnly.crashStack.set(stack);
  result = Glean.testOnly.crashStack.testGetValue();
  Assert.deepEqual(stack, result);

  // FIXME(bug 1947194): JOG object metrics don't do schema validation yet
  if (!Services.prefs.getBoolPref("telemetry.fog.artifact_build", false)) {
    stack = {
      status: "OK",
      modules: [],
    };
    Glean.testOnly.crashStack.set(stack);
    result = Glean.testOnly.crashStack.testGetValue();
    Assert.deepEqual({ status: "OK" }, result);

    stack = {
      status: "OK",
    };
    Glean.testOnly.crashStack.set(stack);
    result = Glean.testOnly.crashStack.testGetValue();
    Assert.deepEqual(stack, result);
  }
});

add_task(
  // FIXME(1898464): ride-along pings are not handled correctly in artifact builds.
  {
    skip_if: () =>
      Services.prefs.getBoolPref("telemetry.fog.artifact_build", false),
  },
  function test_fog_ride_along_pings() {
    Assert.equal(null, Glean.testOnly.badCode.testGetValue("test-ping"));
    Assert.equal(null, Glean.testOnly.badCode.testGetValue("ride-along-ping"));

    Glean.testOnly.badCode.add(37);
    Assert.equal(37, Glean.testOnly.badCode.testGetValue("test-ping"));
    Assert.equal(37, Glean.testOnly.badCode.testGetValue("ride-along-ping"));

    let testPingSubmitted = false;

    GleanPings.testPing.testBeforeNextSubmit(() => {
      testPingSubmitted = true;
    });
    // FIXME(bug 1896356):
    // We can't use `testBeforeNextSubmit` for `ride-along-ping`
    // because it's triggered internally, but the callback would only be available
    // in the C++ bits, not in the internal Rust parts.

    // Submit only a single ping, the other will ride along.
    GleanPings.testPing.submit();

    Assert.ok(
      testPingSubmitted,
      "Test ping was submitted, callback was called."
    );

    // Both pings have been submitted, so the values should be cleared.
    Assert.equal(null, Glean.testOnly.badCode.testGetValue("test-ping"));
    Assert.equal(null, Glean.testOnly.badCode.testGetValue("ride-along-ping"));
  }
);

add_task(async function test_fog_labeled_custom_distribution_works() {
  Assert.equal(
    undefined,
    Glean.testOnly.mabelsCustomLabelLengths.monospace.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.testOnly.mabelsCustomLabelLengths.monospace.accumulateSamples([1, 42]);
  Glean.testOnly.mabelsCustomLabelLengths.sanserif.accumulateSingleSample(13);
  let monospace =
    Glean.testOnly.mabelsCustomLabelLengths.monospace.testGetValue();
  Assert.equal(2, monospace.count);
  Assert.equal(43, monospace.sum);
  Assert.deepEqual({ 0: 0, 1: 2, 268435456: 0 }, monospace.values);
  let sanserif =
    Glean.testOnly.mabelsCustomLabelLengths.sanserif.testGetValue();
  Assert.equal(1, sanserif.count);
  Assert.equal(13, sanserif.sum);
  Assert.deepEqual({ 0: 0, 1: 1, 268435456: 0 }, sanserif.values);
  // What about invalid/__other__?
  Assert.equal(
    undefined,
    Glean.testOnly.mabelsCustomLabelLengths.__other__.testGetValue()
  );
  Glean.testOnly.mabelsCustomLabelLengths[
    "1".repeat(112)
  ].accumulateSingleSample(3);
  Assert.throws(
    () => Glean.testOnly.mabelsCustomLabelLengths.__other__.testGetValue(),
    /DataError/
  );
});

add_task(async function test_fog_labeled_memory_distribution_works() {
  Glean.testOnly.whatDoYouRemember.twenty_years_ago.accumulate(7);
  Glean.testOnly.whatDoYouRemember.twenty_years_ago.accumulate(17);

  let data = Glean.testOnly.whatDoYouRemember.twenty_years_ago.testGetValue();
  Assert.equal(2, data.count, "Count of entries is correct");
  // `data.sum` is in bytes, but the metric is in MB.
  Assert.equal(24 * 1024 * 1024, data.sum, "Sum's correct");
  for (let [bucket, count] of Object.entries(data.values)) {
    Assert.ok(
      count == 0 || (count == 1 && (bucket == 17520006 || bucket == 7053950)),
      "Only two buckets have a sample"
    );
  }
});

add_task(async function test_labeled_timing_distribution_works() {
  let t1 = Glean.testOnly.whereHasTheTimeGone.west.start();
  let t2 = Glean.testOnly.whereHasTheTimeGone.west.start();

  await sleep(5);

  let t3 = Glean.testOnly.whereHasTheTimeGone.west.start();
  Glean.testOnly.whereHasTheTimeGone.west.cancel(t1);

  await sleep(5);

  Glean.testOnly.whereHasTheTimeGone.west.stopAndAccumulate(t2); // 10ms
  Glean.testOnly.whereHasTheTimeGone.west.stopAndAccumulate(t3); // 5ms

  let data = Glean.testOnly.whereHasTheTimeGone.west.testGetValue();

  // Cancelled timers should not be counted.
  Assert.equal(2, data.count, "Count of entries is correct");

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

add_task(async function test_fog_labeled_quantity_works() {
  Assert.equal(
    undefined,
    Glean.testOnly.buttonJars.up.testGetValue(),
    "New labels with no values should return undefined"
  );
  Glean.testOnly.buttonJars.up.set(2);
  Glean.testOnly.buttonJars.curling.set(0);
  Assert.equal(2, Glean.testOnly.buttonJars.up.testGetValue());
  Assert.equal(0, Glean.testOnly.buttonJars.curling.testGetValue());
  // What about invalid/__other__?
  Assert.equal(undefined, Glean.testOnly.buttonJars.__other__.testGetValue());
  Glean.testOnly.buttonJars["1".repeat(112)].set(0);
  Assert.throws(
    () => Glean.testOnly.buttonJars.__other__.testGetValue(),
    /DataError/,
    "Should throw because of a recording error."
  );
});

add_task(async function test_submit_throws() {
  GleanPings.onePingOnly.testBeforeNextSubmit(() => {
    throw new Error("inside callback");
  });

  Assert.throws(
    () => GleanPings.onePingOnly.submit(),
    /inside callback/,
    "Should throw inside callback"
  );
});

add_task(function test_collection_disabled_pings_work() {
  // This test should work equally for full builds and artifact builds.

  Assert.ok("collectionDisabledPing" in GleanPings);

  // collection-enabled=false pings are disabled by default.
  // No data is collected for metrics going into that ping.
  Glean.testOnly.collectionDisabledCounter.add(1);
  Assert.equal(
    undefined,
    Glean.testOnly.collectionDisabledCounter.testGetValue()
  );

  // After enabling a ping we can record data into it
  GleanPings.collectionDisabledPing.setEnabled(true);
  Glean.testOnly.collectionDisabledCounter.add(2);
  Assert.equal(2, Glean.testOnly.collectionDisabledCounter.testGetValue());

  let submitted = false;
  GleanPings.collectionDisabledPing.testBeforeNextSubmit(() => {
    submitted = true;
    Assert.equal(2, Glean.testOnly.collectionDisabledCounter.testGetValue());
  });
  GleanPings.collectionDisabledPing.submit();
  Assert.ok(submitted, "Ping was submitted, callback was called.");
  Assert.equal(
    undefined,
    Glean.testOnly.collectionDisabledCounter.testGetValue()
  );
});
