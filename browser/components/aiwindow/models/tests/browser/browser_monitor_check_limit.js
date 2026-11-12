/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const { MonitorAgent } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs"
);

const { PURPOSES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

const MONITOR_STORE_REMOVE_DATABASE_ON_STARTUP_PREF =
  "browser.smartwindow.monitorStore.removeDatabaseOnStartup";

async function resetMonitorAgentForTesting() {
  await MonitorAgent._resetForTesting();
  if (
    Services.prefs.prefHasUserValue(
      MONITOR_STORE_REMOVE_DATABASE_ON_STARTUP_PREF
    )
  ) {
    Services.prefs.clearUserPref(MONITOR_STORE_REMOVE_DATABASE_ON_STARTUP_PREF);
  }
}

/**
 * Create a monitor watching a single URL and return its serializable snapshot.
 *
 * @param {string[]} urls
 * @param {string} prompt
 * @returns {Promise<object>}
 */
async function createMonitorWatching(urls, prompt) {
  await MonitorAgent.createMonitor({
    prompt,
    watchUrls: urls,
    schedule: { type: "interval", hours: 1 },
    source: "test",
  });
  const monitors = await MonitorAgent.listMonitors();
  return monitors.at(-1);
}

const CHECK_PAGE = `
  <!DOCTYPE html>
  <html>
    <head>
      <meta charset="utf-8" />
      <title>Product Page</title>
    </head>
    <body>
      <div>The price is $299</div>
    </body>
  </html>
`;

/**
 * Refuses the monitor's next check the way the service does, with an HTTP
 * status and the subcode saying which limit was hit.
 *
 * @param {MockEngineManager} mockEngineManager
 * @param {string} id - The monitor to check
 * @param {number} httpStatus - HTTP status the service answers with
 * @param {number} subcode - The service's error subcode
 * @returns {Promise<object[]>} The monitors as the cards see them
 */
async function refuseNextCheck(mockEngineManager, id, httpStatus, subcode) {
  const runPromise = MonitorAgent.runNow(id);
  const { reject } = await mockEngineManager.captureRequest({
    purpose: PURPOSES.MONITOR,
  });
  reject(
    Object.assign(new Error(`${httpStatus} ${subcode}`), {
      status: httpStatus,
      error: subcode,
    })
  );
  await runPromise;
  return MonitorAgent.listMonitors();
}

async function createWatchedMonitor(url, prompt) {
  const monitor = await createMonitorWatching([url], prompt);
  await MonitorAgent._waitForSnapshotForTesting(monitor.id);
  return monitor;
}

add_task(async function test_check_limit_is_account_wide() {
  const mockEngineManager = new MockEngineManager();
  const { url, server } = serveHTML(CHECK_PAGE);

  try {
    await resetMonitorAgentForTesting();
    const first = await createWatchedMonitor(url, "Check for a change.");
    await createWatchedMonitor(url, "Check for anything.");

    Assert.equal(
      (await MonitorAgent.listMonitors())[0].checkLimit,
      null,
      "Tasks that have never been refused are not waiting"
    );

    const monitors = await refuseNextCheck(mockEngineManager, first.id, 429, 2);
    Assert.ok(
      monitors.every(monitor => monitor.checkLimit?.reason === "throttle"),
      "A throttled check makes every task wait, not just the one refused"
    );
    Assert.less(
      monitors[0].checkLimit.until - Date.now(),
      15 * 60 * 1000,
      "The throttling wait is minutes, not the rest of the day"
    );

    // Scheduled checks keep running while the button is held back, so the
    // service gets to say it is answering again.
    Assert.equal(
      (await MonitorAgent.listMonitors())[0].checkLimit?.reason,
      "throttle",
      "The wait is still in force before anything gets through"
    );

    await Assert.rejects(
      MonitorAgent.runNow(first.id),
      /Checks are unavailable until/,
      "A check asked for during the wait is refused, not dropped"
    );
  } finally {
    await new Promise(resolve => server.stop(resolve));
    mockEngineManager.cleanupMocks();
    await resetMonitorAgentForTesting();
  }
});

add_task(async function test_check_limit_survives_a_restart() {
  const mockEngineManager = new MockEngineManager();
  const { url, server } = serveHTML(CHECK_PAGE);

  try {
    await resetMonitorAgentForTesting();
    const monitor = await createWatchedMonitor(url, "Check for a change.");

    const before = await refuseNextCheck(mockEngineManager, monitor.id, 429, 2);
    Assert.equal(
      before[0].checkLimit?.reason,
      "throttle",
      "The refusal holds checks back"
    );

    MonitorAgent._unloadForTesting();
    const after = await MonitorAgent.listMonitors();

    Assert.equal(
      after[0].checkLimit?.reason,
      "throttle",
      "A fresh agent still holds checks back"
    );
    Assert.equal(
      after[0].checkLimit.until,
      before[0].checkLimit.until,
      "And waits out the very same deadline, not a fresh one"
    );
  } finally {
    await new Promise(resolve => server.stop(resolve));
    mockEngineManager.cleanupMocks();
    await resetMonitorAgentForTesting();
  }
});

add_task(async function test_check_limit_outlives_the_task_it_came_from() {
  const mockEngineManager = new MockEngineManager();
  const { url, server } = serveHTML(CHECK_PAGE);

  try {
    await resetMonitorAgentForTesting();
    const refused = await createWatchedMonitor(url, "Check for a change.");
    await createWatchedMonitor(url, "Check for anything.");

    await refuseNextCheck(mockEngineManager, refused.id, 429, 1);
    Assert.equal(
      (await MonitorAgent.listMonitors())[0].checkLimit?.reason,
      "budget",
      "The refusal holds checks back"
    );

    // The only record of the refusal in history goes with the task.
    await MonitorAgent.deleteMonitor(refused.id);
    MonitorAgent._unloadForTesting();

    Assert.equal(
      (await MonitorAgent.listMonitors())[0].checkLimit?.reason,
      "budget",
      "A restart still holds checks back once that task is gone"
    );
  } finally {
    await new Promise(resolve => server.stop(resolve));
    mockEngineManager.cleanupMocks();
    await resetMonitorAgentForTesting();
  }
});

/**
 * How long checks wait depends on which refusal it was
 */
add_task(async function test_check_limit_waits_by_refusal() {
  const cases = [
    [1, "budget"],
    [2, "throttle"],
    [5, "throttle"],
    [6, "throttle"],
    [10, "throttle"],
    [11, "throttle"],
  ];

  for (const [subcode, reason] of cases) {
    const mockEngineManager = new MockEngineManager();
    const { url, server } = serveHTML(CHECK_PAGE);

    try {
      await resetMonitorAgentForTesting();
      const monitor = await createWatchedMonitor(url, "Check for a change.");

      const monitors = await refuseNextCheck(
        mockEngineManager,
        monitor.id,
        429,
        subcode
      );
      Assert.equal(
        monitors[0].checkLimit?.reason,
        reason,
        `Subcode ${subcode} is waited out as ${reason}`
      );
      Assert.greater(
        monitors[0].checkLimit.until,
        Date.now(),
        `Subcode ${subcode} reports a wait that has not passed`
      );
      if (reason === "budget") {
        const until = Temporal.Instant.fromEpochMilliseconds(
          monitors[0].checkLimit.until
        ).toZonedDateTimeISO("America/New_York");
        Assert.equal(until.dayOfWeek, 1, "The budget resumes on a Monday");
        Assert.equal(
          `${until.hour}:${until.minute}:${until.second}`,
          "0:0:0",
          "...at midnight in US/Eastern"
        );
        Assert.lessOrEqual(
          monitors[0].checkLimit.until - Date.now(),
          7 * 24 * 60 * 60 * 1000,
          "...no more than a week out"
        );
      }
    } finally {
      await new Promise(resolve => server.stop(resolve));
      mockEngineManager.cleanupMocks();
      await resetMonitorAgentForTesting();
    }
  }
});
