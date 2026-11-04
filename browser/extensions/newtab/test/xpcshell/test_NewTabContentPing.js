/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  NewTabContentPing: "resource://newtab/lib/NewTabContentPing.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const MIN_SUBMISSION_DELAY_PREF =
  "browser.newtabpage.activity-stream.telemetry.privatePing.minSubmissionDelayMs";
const MAX_SUBMISSION_DELAY_PREF =
  "browser.newtabpage.activity-stream.telemetry.privatePing.maxSubmissionDelayMs";

const DWELL_TIME_ENABLED_PREF =
  "browser.newtabpage.activity-stream.telemetry.privatePing.dwellTime.enabled";
const MIN_SUBMISSION_DELAY = Services.prefs.getIntPref(
  MIN_SUBMISSION_DELAY_PREF,
  1000
);
const MAX_SUBMISSION_DELAY = Services.prefs.getIntPref(
  MAX_SUBMISSION_DELAY_PREF,
  6000
);

// The ping widens the window whenever the configured maximum is closer than
// this to the minimum, which the shipped defaults are.
const SUBMISSION_DELAY_MIN_RANGE = 5000;
const EFFECTIVE_MAX_SUBMISSION_DELAY = Math.max(
  MAX_SUBMISSION_DELAY,
  MIN_SUBMISSION_DELAY + SUBMISSION_DELAY_MIN_RANGE
);

add_setup(() => {
  do_get_profile();
  Services.fog.initializeFOG();
});

/**
 * Tests that the recordEvent method will cause a delayed ping submission to be
 * scheduled, and that the right extra fields are stripped from events.
 */
add_task(async function test_recordEvent_sanitizes_and_buffers() {
  let ping = new NewTabContentPing();
  ping.test_only_resetAllStats();

  // These fields are expected to be stripped before they get recorded in the
  // event.
  let sanitizedFields = {
    newtab_visit_id: "some visit id",
    matches_selected_topic: "not-set",
    recommended_at: "1748877997039",
    received_rank: 0,
    event_source: "card",
    card_column: "3",
    is_ad_eligible_position: "true",
  };

  // These fields are expected to survive the sanitization.
  let expectedFields = {
    section: "business",
    section_position: "2",
    layout_name: "card-layout",
    position: "12",
    selected_topics: "",
    corpus_item_id: "7fc404a1-74ec-450b-8eef-4f52b45ec510",
    topic: "business",
    format: "medium-card",
    scheduled_corpus_item_id: "40f9ba69-1288-4778-8cfa-937df633819c",
    is_sponsored: "false",
    is_section_followed: "false",
  };

  ping.recordEvent("click", {
    // These should be sanitized out.
    ...sanitizedFields,
    ...expectedFields,
  });

  let extraMetrics = {
    utcOffset: "1",
    experimentBranch: "some-branch",
  };
  ping.scheduleSubmission(extraMetrics);

  await GleanPings.newtabContent.testSubmission(
    () => {
      // Test callback
      let [clickEvent] = Glean.newtabContent.click.testGetValue();
      Assert.ok(clickEvent, "Found click event.");
      for (let fieldName of Object.keys(sanitizedFields)) {
        Assert.equal(
          clickEvent.extra[fieldName],
          undefined,
          `Should not have gotten sanitized extra field: ${fieldName}`
        );
      }
      for (let fieldName of Object.keys(expectedFields)) {
        Assert.equal(
          clickEvent.extra[fieldName],
          expectedFields[fieldName],
          `Should have recorded expected extra field: ${fieldName}`
        );
      }

      for (let metricName of Object.keys(extraMetrics)) {
        Assert.equal(
          Glean.newtabContent[metricName].testGetValue(),
          extraMetrics[metricName],
          `Should have recorded metric: ${metricName}`
        );
      }
    },
    async () => {
      // Submit Callback
      let delay = await ping.testOnlyForceFlush();
      Assert.greaterOrEqual(
        delay,
        MIN_SUBMISSION_DELAY,
        "Picked a random value at or above MIN_SUBMISSION_DELAY"
      );
      Assert.lessOrEqual(
        delay,
        EFFECTIVE_MAX_SUBMISSION_DELAY,
        "Picked a random value at or below EFFECTIVE_MAX_SUBMISSION_DELAY"
      );
    }
  );
});

/**
 * Tests that the recordEvent caps the maximum number of events
 */
add_task(async function test_recordEventDaily_caps_events() {
  const MAX_EVENTS = 2;

  let ping = new NewTabContentPing();
  ping.setMaxEventsPerDay(MAX_EVENTS);
  ping.test_only_resetAllStats();

  // These fields are expected to survive the sanitization.
  let expectedFields = {
    section: "business",
    corpus_item_id: "7fc404a1-74ec-450b-8eef-4f52b45ec510",
    topic: "business",
  };

  ping.recordEvent("click", {
    ...expectedFields,
  });

  ping.recordEvent("impression", {
    ...expectedFields,
  });

  let extraMetrics = {
    utcOffset: "1",
    experimentBranch: "some-branch",
  };
  ping.scheduleSubmission(extraMetrics);

  await GleanPings.newtabContent.testSubmission(
    () => {
      // Test Callback
      let [clickEvent] = Glean.newtabContent.click.testGetValue();
      Assert.ok(clickEvent, "Found click event.");
      let [impression] = Glean.newtabContent.impression.testGetValue();
      Assert.ok(impression, "Found impression event.");
    },
    async () => {
      // Submit Callback
      await ping.testOnlyForceFlush();
    }
  );

  Assert.equal(
    ping.testOnlyCurInstanceEventCount,
    2,
    "Expected number of events sent"
  );

  ping.recordEvent("section_impression", {
    ...expectedFields,
  });
  ping.scheduleSubmission(extraMetrics);
  await ping.testOnlyForceFlush();

  Assert.equal(ping.testOnlyCurInstanceEventCount, 2, "No new events sent");

  ping = new NewTabContentPing();
  ping.setMaxEventsPerDay(MAX_EVENTS);

  Assert.equal(ping.testOnlyCurInstanceEventCount, 0, "Event count reset");

  ping.recordEvent("section_impression", {
    ...expectedFields,
  });
  ping.scheduleSubmission(extraMetrics);
  await ping.testOnlyForceFlush();

  Assert.equal(
    ping.testOnlyCurInstanceEventCount,
    0,
    "No new events after re-creating NewTabContentPing class"
  );

  // Some time has passed
  let sandbox = sinon.createSandbox();

  sandbox.stub(NewTabContentPing.prototype, "Date").returns({
    now: () => Date.now() + 3600 * 25 * 1000, // 25 hours in future
  });

  ping.scheduleSubmission(extraMetrics);

  ping.recordEvent("click", {
    ...expectedFields,
  });

  await GleanPings.newtabContent.testSubmission(
    () => {
      // Test Callback
      let [click] = Glean.newtabContent.click.testGetValue();
      Assert.ok(click, "Found click event.");
    },
    async () => {
      // Submit Callback
      await ping.testOnlyForceFlush();
    }
  );
  Assert.equal(ping.testOnlyCurInstanceEventCount, 1, "Event sending restored");
  sandbox.restore();
});

/**
 * Tests that the recordEvent caps the maximum number of click events
 */
add_task(async function test_recordEventDaily_caps_click_events() {
  const MAX_DAILY_CLICK_EVENTS = 2;
  const MAX_WEEKLY_CLICK_EVENTS = 10;

  let ping = new NewTabContentPing();
  ping.setMaxClickEventsPerDay(MAX_DAILY_CLICK_EVENTS);
  ping.setMaxClickEventsPerWeek(MAX_WEEKLY_CLICK_EVENTS);
  ping.test_only_resetAllStats();

  // These fields are expected to survive the sanitization.
  let expectedFields = {
    section: "business",
    corpus_item_id: "7fc404a1-74ec-450b-8eef-4f52b45ec510",
    topic: "business",
  };

  for (let i = 0; i < MAX_DAILY_CLICK_EVENTS * 2; i++) {
    ping.recordEvent("impression", {
      ...expectedFields,
    });
  }
  for (let i = 0; i < MAX_DAILY_CLICK_EVENTS; i++) {
    ping.recordEvent("click", {
      ...expectedFields,
    });
  }

  let extraMetrics = {
    utcOffset: "1",
    experimentBranch: "some-branch",
  };

  ping.scheduleSubmission(extraMetrics);

  await GleanPings.newtabContent.testSubmission(
    () => {
      // Test Callback
      let [clickEvent] = Glean.newtabContent.click.testGetValue();
      Assert.ok(clickEvent, "Found click event.");
      let [impression] = Glean.newtabContent.impression.testGetValue();
      Assert.ok(impression, "Found impression event.");
    },
    async () => {
      // Submit Callback
      await ping.testOnlyForceFlush();
    }
  );

  const curTotalEvents = MAX_DAILY_CLICK_EVENTS * 3;
  Assert.equal(
    ping.testOnlyCurInstanceEventCount,
    curTotalEvents,
    "Expected number of events sent"
  );

  ping.recordEvent("click", {
    ...expectedFields,
  });
  ping.scheduleSubmission(extraMetrics);
  await ping.testOnlyForceFlush();

  Assert.equal(
    ping.testOnlyCurInstanceEventCount,
    curTotalEvents,
    "Click cap enforced, no new events sent"
  );

  ping = new NewTabContentPing();
  ping.setMaxClickEventsPerDay(MAX_DAILY_CLICK_EVENTS);

  Assert.equal(ping.testOnlyCurInstanceEventCount, 0, "Event count reset");

  ping.recordEvent("click", {
    ...expectedFields,
  });
  ping.scheduleSubmission(extraMetrics);
  await ping.testOnlyForceFlush();

  Assert.equal(
    ping.testOnlyCurInstanceEventCount,
    0,
    "No new click events after re-creating NewTabContentPing class"
  );

  // Some time has passed
  let sandbox = sinon.createSandbox();

  sandbox.stub(NewTabContentPing.prototype, "Date").returns({
    now: () => Date.now() + 3600 * 25 * 1000, // 25 hours in future
  });

  ping.scheduleSubmission(extraMetrics);

  ping.recordEvent("click", {
    ...expectedFields,
  });

  await GleanPings.newtabContent.testSubmission(
    () => {
      // Test Callback
      let [click] = Glean.newtabContent.click.testGetValue();
      Assert.ok(click, "Found click event.");
    },
    async () => {
      // Submit Callback
      await ping.testOnlyForceFlush();
    }
  );
  Assert.equal(ping.testOnlyCurInstanceEventCount, 1, "Event sending restored");

  let cur_events_sent = ping.testOnlyCurInstanceEventCount;
  for (let i = 0; i < MAX_WEEKLY_CLICK_EVENTS; i++) {
    ping.recordEvent("click", {
      ...expectedFields,
    });
  }
  ping.scheduleSubmission(extraMetrics);
  await ping.testOnlyForceFlush();
  // Assert that less than MAX_WEEKLY_CLICK_EVENTS events were sent using a difference check (less) to avoid timing issues
  Assert.less(
    ping.testOnlyCurInstanceEventCount - cur_events_sent,
    MAX_WEEKLY_CLICK_EVENTS,
    "Weekly click cap enforced, no new events sent"
  );
  // Assert that at least 1 item was sent before we reached the cap
  Assert.greater(
    ping.testOnlyCurInstanceEventCount - cur_events_sent,
    0,
    "At least one event was sent before reaching the weekly cap"
  );

  sandbox.restore();

  // Some more time has passed - weekly reset
  sandbox = sinon.createSandbox();
  sandbox.stub(NewTabContentPing.prototype, "Date").returns({
    now: () => Date.now() + 3600 * 24 * 1000 * 8, // 8 days in the future
  });

  cur_events_sent = ping.testOnlyCurInstanceEventCount;
  for (let i = 0; i < MAX_WEEKLY_CLICK_EVENTS; i++) {
    ping.recordEvent("click", {
      ...expectedFields,
    });
  }
  ping.scheduleSubmission(extraMetrics);
  await ping.testOnlyForceFlush();
  Assert.equal(
    ping.testOnlyCurInstanceEventCount - cur_events_sent,
    MAX_DAILY_CLICK_EVENTS, // Only daily cap should apply after weekly reset
    "Event sending restored after weekly reset"
  );

  sandbox.restore();
});

add_task(function test_decideWithProbability() {
  Assert.equal(NewTabContentPing.decideWithProbability(-0.1), false);
  Assert.equal(NewTabContentPing.decideWithProbability(1.1), true);
});

add_task(function test_shuffleArray() {
  const shuffled = NewTabContentPing.shuffleArray([1, 3, 5]);
  Assert.equal(shuffled.length, 3);
  Assert.ok(shuffled.includes(3), "Shuffled item in array");
  Assert.ok(shuffled.includes(1), "Shuffled item in array");
  Assert.ok(shuffled.includes(5), "Shuffled item in array");
  Assert.equal(NewTabContentPing.shuffleArray([]).length, 0);
});

add_task(async function test_secureRandIntInRange() {
  for (let k = 0; k < 10; k++) {
    Assert.greater(
      10,
      NewTabContentPing.secureRandIntInRange(10),
      "Random value in range"
    );
    Assert.less(
      -1,
      NewTabContentPing.secureRandIntInRange(10),
      "Random value in range"
    );
  }
});

/**
 * Pin the random delay selection to one end of its range, so the effective
 * minimum and maximum can be read back exactly rather than sampled.
 *
 * @param {object} sandbox a sinon sandbox
 * @returns {object} a holder whose `end` is "min" or "max"
 */
function stubDelaySelection(sandbox) {
  const pick = { end: "min" };
  sandbox
    .stub(NewTabContentPing, "secureRandIntInRange")
    .callsFake(range => (pick.end === "min" ? 0 : range - 1));
  return pick;
}

/**
 * Schedule and immediately flush, returning the delay that was selected.
 *
 * @param {object} ping a NewTabContentPing
 * @returns {Promise<number>} the selected delay in milliseconds
 */
async function selectedDelayMs(ping) {
  ping.scheduleSubmission({});
  return ping.testOnlyForceFlush();
}

/**
 * The one click event in the ping that was just submitted.
 *
 * @returns {object} the recorded event
 */
function onlyClickEvent() {
  const events = Glean.newtabContent.click.testGetValue();
  Assert.equal(events.length, 1, "Exactly one click event in the ping");
  return events[0];
}

/**
 * Turn the dwell time feature on. Each test that calls this clears the pref
 * again before it ends.
 */
function enableDwellTime() {
  Services.prefs.setBoolPref(DWELL_TIME_ENABLED_PREF, true);
}

const gConsoleAPIStorage = Cc["@mozilla.org/consoleAPI-storage;1"].getService(
  Ci.nsIConsoleAPIStorage
);

/**
 * How many console.error calls since the last clearEvents() mention a
 * newtab-content submission delay.
 *
 * @returns {number}
 */
function countSubmissionDelayErrors() {
  return gConsoleAPIStorage
    .getEvents()
    .filter(
      event =>
        event.level === "error" &&
        event.arguments.some(arg => String(arg).includes("submission delay"))
    ).length;
}

add_task(async function test_submission_delay_clamps() {
  info(
    "NewTabContentPing should raise a minimum submission delay below 1000 up " +
      "to 1000, and raise a maximum that sits less than 5000 above the " +
      "minimum to exactly 5000 above it. Only a configuration that cannot be " +
      "right, a minimum below 1000 or a maximum below the minimum, is " +
      "reported as an error."
  );
  let sandbox = sinon.createSandbox();
  let pick = stubDelaySelection(sandbox);
  let ping = new NewTabContentPing();
  await ping.test_only_resetAllStats();

  // Configured minimum, configured maximum, effective minimum, effective
  // maximum, and whether it is reported. The shipped defaults are the third
  // row; the second is what older Firefox builds ship.
  const cases = [
    [10, 30000, 1000, 30000, true],
    [1000, 5000, 1000, 6000, false],
    [1000, 6000, 1000, 6000, false],
    [50000, 51000, 50000, 55000, false],
    [50000, 60000, 50000, 60000, false],
    [50000, 6000, 50000, 55000, true],
  ];
  for (const [minPref, maxPref, min, max, reported] of cases) {
    Services.prefs.setIntPref(MIN_SUBMISSION_DELAY_PREF, minPref);
    Services.prefs.setIntPref(MAX_SUBMISSION_DELAY_PREF, maxPref);
    gConsoleAPIStorage.clearEvents();
    pick.end = "min";
    Assert.equal(
      await selectedDelayMs(ping),
      min,
      `min=${minPref} max=${maxPref} never waits less than ${min}`
    );
    pick.end = "max";
    Assert.equal(
      await selectedDelayMs(ping),
      max,
      `min=${minPref} max=${maxPref} never waits more than ${max}`
    );
    Assert.equal(
      countSubmissionDelayErrors(),
      reported ? 2 : 0,
      `min=${minPref} max=${maxPref} is ${reported ? "" : "not "}reported, ` +
        "once per schedule"
    );
  }

  Services.prefs.clearUserPref(MIN_SUBMISSION_DELAY_PREF);
  Services.prefs.clearUserPref(MAX_SUBMISSION_DELAY_PREF);
  sandbox.restore();
});

add_task(async function test_dwell_time_lands_on_the_click_row() {
  info(
    "recordDwellTime should write the dwell time onto the buffered click its " +
      "token identifies, and the token itself should never be recorded"
  );
  enableDwellTime();
  let ping = new NewTabContentPing();
  await ping.test_only_resetAllStats();

  ping.recordEvent(
    "click",
    {
      corpus_item_id: "story-1",
      is_sponsored: "false",
      // Belt and braces: the payload has no business carrying this either.
      dwell_id: 7,
    },
    "port1:7"
  );
  Assert.ok(
    ping.recordDwellTime("port1:7", 12345),
    "The click was still buffered"
  );
  ping.scheduleSubmission({});

  await GleanPings.newtabContent.testSubmission(
    () => {
      const click = onlyClickEvent();
      Assert.equal(click.extra.dwell_time, "12345", "The measured dwell time");
      Assert.equal(
        click.extra.corpus_item_id,
        "story-1",
        "On the row that already disclosed the story"
      );
      Assert.equal(
        click.extra.dwell_id,
        undefined,
        "No pairing token reached the ping"
      );
    },
    async () => {
      await ping.testOnlyForceFlush();
    }
  );
  Services.prefs.clearUserPref(DWELL_TIME_ENABLED_PREF);
});

add_task(async function test_dwell_time_after_submission_is_a_no_op() {
  info(
    "recordDwellTime and trackDwellTime should report that they found " +
      "nothing once the click has been submitted, rather than throwing"
  );
  let ping = new NewTabContentPing();
  await ping.test_only_resetAllStats();

  ping.recordEvent("click", { corpus_item_id: "story-1" }, "port1:1");
  ping.scheduleSubmission({});
  await ping.testOnlyForceFlush();

  Assert.equal(
    ping.recordDwellTime("port1:1", 20000),
    false,
    "There is no row left to write a measurement on"
  );
  Assert.equal(
    ping.trackDwellTime("port1:1", () => 5000),
    false,
    "Nor one to track"
  );
});

add_task(async function test_tracked_dwell_time_is_read_at_submission() {
  info(
    "A tracked click should report whatever its reader returns when the ping " +
      "is sent, not when it was tracked, and no dwell time for a null"
  );
  enableDwellTime();
  let ping = new NewTabContentPing();
  await ping.test_only_resetAllStats();

  let readSoFar = 0;
  ping.recordEvent("click", { corpus_item_id: "read" }, "port1:1");
  ping.recordEvent("click", { corpus_item_id: "unread" }, "port1:2");
  Assert.ok(
    ping.trackDwellTime("port1:1", () => readSoFar),
    "The click was still buffered"
  );
  Assert.ok(
    ping.trackDwellTime("port1:2", () => null),
    "And so was the other"
  );
  readSoFar = 20000;
  ping.scheduleSubmission({});

  await GleanPings.newtabContent.testSubmission(
    () => {
      const clicks = Object.fromEntries(
        Glean.newtabContent.click
          .testGetValue()
          .map(click => [click.extra.corpus_item_id, click.extra])
      );
      Assert.equal(
        clicks.read.dwell_time,
        "20000",
        "The value at submission, not at tracking"
      );
      Assert.equal(
        clicks.unread.dwell_time,
        undefined,
        "A null reading sends the click without a dwell time"
      );
    },
    async () => {
      await ping.testOnlyForceFlush();
    }
  );
  Services.prefs.clearUserPref(DWELL_TIME_ENABLED_PREF);
});

add_task(async function test_a_measured_dwell_time_replaces_the_reader() {
  info(
    "A dwell time recordDwellTime measured should be reported as measured. " +
      "The user has left the page, so a reader left by trackDwellTime has " +
      "nothing more to say."
  );
  enableDwellTime();
  let ping = new NewTabContentPing();
  await ping.test_only_resetAllStats();

  ping.recordEvent("click", { corpus_item_id: "story-1" }, "port1:1");
  ping.trackDwellTime("port1:1", () => 0);
  ping.recordDwellTime("port1:1", 40000);
  ping.scheduleSubmission({});

  await GleanPings.newtabContent.testSubmission(
    () => {
      Assert.equal(
        onlyClickEvent().extra.dwell_time,
        "40000",
        "The measurement was reported whole"
      );
    },
    async () => {
      await ping.testOnlyForceFlush();
    }
  );
  Services.prefs.clearUserPref(DWELL_TIME_ENABLED_PREF);
});

add_task(async function test_dwell_time_is_dropped_when_the_feature_is_off() {
  info(
    "A click should reach the ping without its dwell time if the feature was " +
      "turned off after the dwell time was written, whether it was measured " +
      "or still being read"
  );
  let ping = new NewTabContentPing();
  await ping.test_only_resetAllStats();
  enableDwellTime();

  ping.recordEvent("click", { corpus_item_id: "measured" }, "port1:1");
  ping.recordEvent("click", { corpus_item_id: "tracked" }, "port1:2");
  Assert.ok(ping.recordDwellTime("port1:1", 10000), "Measured row buffered");
  Assert.ok(
    ping.trackDwellTime("port1:2", () => 10000),
    "Tracked row buffered"
  );
  Services.prefs.setBoolPref(DWELL_TIME_ENABLED_PREF, false);
  ping.scheduleSubmission({});

  await GleanPings.newtabContent.testSubmission(
    () => {
      const clicks = Glean.newtabContent.click.testGetValue();
      Assert.equal(clicks.length, 2, "Both clicks were still sent");
      for (const click of clicks) {
        Assert.equal(
          click.extra.dwell_time,
          undefined,
          `${click.extra.corpus_item_id} carries no dwell time`
        );
      }
    },
    async () => {
      await ping.testOnlyForceFlush();
    }
  );
  Services.prefs.clearUserPref(DWELL_TIME_ENABLED_PREF);
});
