/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// nsIAppStartup owns the firing of "quit-application-requested". These tests
// cover the parts of that which don't need a window: who fires and who doesn't.

const TOPIC = "quit-application-requested";

/**
 * Watch the topic. Returns an object with the number of notifications seen and
 * the aData of the last one, plus a stop() to unregister.
 *
 * @param {boolean} veto Whether to cancel the quit when asked.
 */
function watchQuitRequests(veto = false) {
  let state = { count: 0, lastData: null };
  let observer = {
    observe(subject, topic, data) {
      Assert.equal(topic, TOPIC, "Expected topic");
      state.count++;
      state.lastData = data;
      if (veto) {
        subject.QueryInterface(Ci.nsISupportsPRBool).data = true;
      }
    },
  };
  Services.obs.addObserver(observer, TOPIC);
  state.stop = () => Services.obs.removeObserver(observer, TOPIC);
  return state;
}

add_task(function test_canQuit_asks_and_reports_the_verdict() {
  let seen = watchQuitRequests();
  Assert.ok(Services.startup.canQuit(), "Nobody objected");
  Assert.equal(seen.count, 1, "Asked exactly once");
  seen.stop();

  let vetoed = watchQuitRequests(true);
  Assert.ok(!Services.startup.canQuit(), "Veto is reported back");
  Assert.equal(vetoed.count, 1, "Asked exactly once");
  vetoed.stop();
});

add_task(function test_canQuit_passes_the_reason_through() {
  let seen = watchQuitRequests();

  Services.startup.canQuit("restart");
  Assert.equal(seen.lastData, "restart", "Reason arrives as aData");

  Services.startup.canQuit();
  Assert.equal(seen.lastData, null, "No reason means no aData");

  Assert.equal(seen.count, 2, "Asked once per call");
  seen.stop();
});

add_task(function test_a_reentrant_ask_fires_again() {
  // Observers of the quit-application-requested signal can themselves call
  // canQuit synchronously.

  let count = 0;
  let reentrantVerdict = null;
  let observer = {
    observe() {
      count++;
      if (count == 1) {
        reentrantVerdict = Services.startup.canQuit();
      }
    },
  };
  Services.obs.addObserver(observer, TOPIC);

  Services.startup.canQuit();
  Services.obs.removeObserver(observer, TOPIC);

  Assert.equal(count, 2, "The re-entrant ask fired the topic again");
  Assert.ok(reentrantVerdict, "The re-entrant ask reported no objection");
});
