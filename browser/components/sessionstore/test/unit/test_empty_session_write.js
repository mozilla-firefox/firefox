/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Ensures SessionFile.write() records an empty_session_write event when it
 * writes a session with no tabs, but only for the first write since the
 * session file was read, a write after one with tabs, or the final write.
 */

"use strict";

do_get_profile();

const { SessionFile } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/SessionFile.sys.mjs"
);
const { RunState } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/RunState.sys.mjs"
);

const EMPTY_STATE = { windows: [], _closedWindows: [] };
const EMPTY_WINDOW_STATE = { windows: [{ tabs: [] }], _closedWindows: [] };
const TABS_STATE = {
  windows: [
    {
      tabs: [
        { entries: [{ url: "https://example.com/" }], index: 1 },
        { entries: [{ url: "https://example.org/" }], index: 1 },
      ],
    },
  ],
  _closedWindows: [{ tabs: [] }],
};

function getEvents() {
  return (Glean.sessionRestore.emptySessionWrite.testGetValue() ?? []).map(
    e => e.extra
  );
}

async function resetSession() {
  await SessionFile.wipe();
  await SessionFile.read();
  Services.fog.testResetFOG();
}

add_setup(async function () {
  Services.fog.initializeFOG();
  await SessionFile.read();
});

add_task(async function test_first_write_empty() {
  await resetSession();

  await SessionFile.write(EMPTY_WINDOW_STATE);

  let events = getEvents();
  Assert.equal(events.length, 1, "an empty first write is recorded");
  let [extra] = events;
  Assert.equal(extra.windows, "1", "the window with no tabs is counted");
  Assert.equal(extra.closed_windows, "0");
  Assert.greater(Number(extra.size_bytes), 0, "size_bytes is recorded");
  Assert.equal(extra.is_final_write, "false");
  Assert.equal(extra.run_state, "stopped");
  Assert.ok(
    !("prev_tabs" in extra) && !("prev_windows" in extra),
    "there is no previous write to report"
  );
});

add_task(async function test_writes_with_tabs_not_recorded() {
  await resetSession();

  await SessionFile.write(TABS_STATE);
  await SessionFile.write(TABS_STATE);

  Assert.deepEqual(getEvents(), [], "writes with tabs are not recorded");
});

add_task(async function test_tabs_lost() {
  await resetSession();

  await SessionFile.write(TABS_STATE);
  await SessionFile.write(EMPTY_STATE);
  await SessionFile.write(EMPTY_STATE);

  let events = getEvents();
  Assert.equal(
    events.length,
    1,
    "only the write that lost the tabs is recorded"
  );
  let [extra] = events;
  Assert.equal(extra.windows, "0");
  Assert.equal(extra.prev_windows, "1");
  Assert.equal(extra.prev_tabs, "2");
  Assert.greater(
    Number(extra.prev_size_bytes),
    Number(extra.size_bytes),
    "the previous write was larger"
  );
});

add_task(async function test_empty_again_after_tabs() {
  await resetSession();

  await SessionFile.write(EMPTY_STATE);
  await SessionFile.write(TABS_STATE);
  await SessionFile.write(EMPTY_STATE);

  let events = getEvents();
  Assert.equal(events.length, 2, "each loss of tabs is recorded");
  Assert.equal(events[1].prev_tabs, "2");
});

// Must run last, as no writes are accepted after the final write.
add_task(async function test_final_write_always_recorded() {
  await resetSession();
  await SessionFile.write(EMPTY_STATE);

  RunState.setRunning();
  RunState.setQuitting();
  RunState.setClosing();
  await SessionFile.write(EMPTY_STATE);

  let events = getEvents();
  Assert.equal(
    events.length,
    2,
    "the empty final write is recorded despite following an empty write"
  );
  Assert.equal(events[1].is_final_write, "true");
  Assert.equal(events[1].run_state, "closing");
  Assert.equal(events[1].prev_tabs, "0");
});
