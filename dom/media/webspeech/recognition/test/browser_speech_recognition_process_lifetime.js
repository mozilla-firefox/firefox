/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// The HWInference utility process must not outlive the speech recognition that
// needed it: once the last session ends and the last availability/install call
// settles (and, after the grace period, nobody has come back), it is shut down
// rather than lingering until browser shutdown.

const ORIGIN = "https://example.com";
const PAGE =
  getRootDirectory(gTestPath).replace("chrome://mochitests/content", ORIGIN) +
  "empty.html";

async function hwInferenceProcesses() {
  const info = await ChromeUtils.requestProcInfo();
  return info.children.filter(
    child =>
      child.type == "utility" &&
      child.utilityActors.some(actor => actor.actorName == "hwInference")
  );
}

async function hwInferenceProcessCount() {
  return (await hwInferenceProcesses()).length;
}

function waitForHWInferenceProcessCount(expected, msg) {
  return TestUtils.waitForCondition(
    async () => (await hwInferenceProcessCount()) == expected,
    msg
  );
}

async function killHWInferenceProcess() {
  const [proc] = await hwInferenceProcesses();
  ok(proc, "Got the HWInference process");
  const ProcessTools = Cc["@mozilla.org/processtools-service;1"].getService(
    Ci.nsIProcessToolsService
  );
  ProcessTools.kill(proc.pid);
  await waitForHWInferenceProcessCount(0, "HWInference process is gone");
}

function callAvailable(browser) {
  return SpecialPowers.spawn(browser, [], () =>
    content.SpeechRecognition.available({
      langs: ["en-US"],
      processLocally: true,
    })
  );
}

// Starts a session on a fake mic track and resolves with "start", or with
// "error: <name>" if the session fails to come up. The object is parked on the
// content window so it stays alive until the tab goes away, rather than being
// collected out from under the session.
function startSession(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const stream = await content.navigator.mediaDevices.getUserMedia({
      audio: true,
    });
    const recognition = new content.SpeechRecognition();
    recognition.processLocally = true;
    recognition.lang = "en-US";
    content.wrappedJSObject._recognition = recognition;
    return new Promise(resolve => {
      recognition.onstart = () => resolve("start");
      recognition.onerror = e => resolve(`error: ${e.error}`);
      recognition.start(stream.getAudioTracks()[0]);
    });
  });
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["media.webspeech.recognition.enable", true],
      // No network/IndexedDB/downloads, and RecvInit skips model retrieval, so
      // a session can start without a real multi-hundred-MB model.
      ["browser.ml.modelHub.testing", true],
      // start() offers to install the missing model; answer that prompt from
      // media.navigator.permission.disabled instead of waiting for a click.
      ["media.webspeech.recognition.model-download.prompt.testing", true],
      // Fake mic, so start() gets a track without any device or user prompt.
      ["media.navigator.streams.fake", true],
      ["media.navigator.permission.disabled", true],
    ],
  });
});

add_task(async function test_session_then_tab_close_shuts_process_down() {
  await SpecialPowers.pushPrefEnv({
    set: [["media.webspeech.recognition.idle_shutdown_grace_ms", 0]],
  });

  const before = await hwInferenceProcessCount();
  is(before, 0, "No HWInference process before the test");

  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, PAGE);

  is(
    await startSession(tab.linkedBrowser),
    "start",
    "Recognition session started"
  );

  await waitForHWInferenceProcessCount(
    1,
    "HWInference process is running while the session is active"
  );

  BrowserTestUtils.removeTab(tab);

  await waitForHWInferenceProcessCount(
    0,
    "HWInference process is shut down once the tab owning the session is gone"
  );

  await SpecialPowers.popPrefEnv();
});

// Constructing a SpeechRecognition object does not, on its own, start the
// process: only a session or a static call does.
add_task(async function test_live_object_does_not_hold_process() {
  await SpecialPowers.pushPrefEnv({
    set: [["media.webspeech.recognition.idle_shutdown_grace_ms", 0]],
  });

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await SpecialPowers.spawn(browser, [], () => {
      content.wrappedJSObject._recognition = new content.SpeechRecognition();
    });

    is(
      await hwInferenceProcessCount(),
      0,
      "An idle SpeechRecognition object does not launch the process"
    );
  });

  await SpecialPowers.popPrefEnv();
});

// A one-shot static call is a "transaction": it holds the process for its
// duration and releases it when it settles.
add_task(async function test_transaction_releases_process() {
  await SpecialPowers.pushPrefEnv({
    set: [["media.webspeech.recognition.idle_shutdown_grace_ms", 0]],
  });

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await callAvailable(browser);

    await waitForHWInferenceProcessCount(
      0,
      "HWInference process is shut down once available() has settled"
    );
  });

  await SpecialPowers.popPrefEnv();
});

// With a grace period, the process is kept warm briefly so a stop()/start()
// cycle reuses it instead of paying for a relaunch -- but it still goes away
// on its own once the grace period elapses.
add_task(async function test_grace_period_keeps_process_warm() {
  await SpecialPowers.pushPrefEnv({
    set: [["media.webspeech.recognition.idle_shutdown_grace_ms", 5000]],
  });

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await callAvailable(browser);

    await waitForHWInferenceProcessCount(
      1,
      "HWInference process launched by available()"
    );

    // Still up right after the call settled: the shutdown is only pending.
    is(
      await hwInferenceProcessCount(),
      1,
      "HWInference process is kept warm during the grace period"
    );

    // A second call within the grace period reuses the warm process.
    await callAvailable(browser);
    is(
      await hwInferenceProcessCount(),
      1,
      "The warm HWInference process is reused rather than relaunched"
    );

    // Shorten the grace period so the pending close lands promptly.
    await SpecialPowers.pushPrefEnv({
      set: [["media.webspeech.recognition.idle_shutdown_grace_ms", 0]],
    });
    await callAvailable(browser);

    await waitForHWInferenceProcessCount(
      0,
      "HWInference process is shut down once the grace period elapses"
    );
    await SpecialPowers.popPrefEnv();
  });

  await SpecialPowers.popPrefEnv();
});

// A HWInference crash while this content process still holds its connection
// leaves ContentParent's cached keep-alive pointing at the dead process. The
// next session must relaunch and land on the live one, rather than failing
// until the child's last connection goes away.
add_task(async function test_session_after_process_crash() {
  // Long enough that the child never sends ReleaseHWInferenceConnection during
  // the test, so the parent keeps counting a connection across the crash --
  // which is exactly the window where the cached keep-alive goes stale.
  await SpecialPowers.pushPrefEnv({
    set: [["media.webspeech.recognition.idle_shutdown_grace_ms", 60000]],
  });

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    is(await startSession(browser), "start", "First session started");
    await waitForHWInferenceProcessCount(1, "HWInference process is running");

    await killHWInferenceProcess();

    // The child still holds a connection: the parent's count has not dropped,
    // so nothing has re-acquired on its behalf.
    is(
      await startSession(browser),
      "start",
      "A session started after the crash, on a relaunched process"
    );
    await waitForHWInferenceProcessCount(1, "HWInference process relaunched");

    // Drop the grace period before the tab goes away, so its connection is
    // released -- and the process shut down cleanly, spending the crash
    // recorded above -- as soon as it does.
    await SpecialPowers.pushPrefEnv({
      set: [["media.webspeech.recognition.idle_shutdown_grace_ms", 0]],
    });
  });

  await waitForHWInferenceProcessCount(
    0,
    "HWInference process shut down cleanly once the tab is gone"
  );

  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});

// The restart budget covers a crash loop rather than the whole browser
// session: a process that came back up and was then shut down cleanly starts
// the next one from a full budget.
add_task(async function test_clean_shutdown_restores_restart_budget() {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Two crashes with no clean shutdown between them give up.
      ["browser.ml.hwinference.max_restarts", 2],
      // The session itself holds the connection, so it survives the kill;
      // closing the tab releases it right away.
      ["media.webspeech.recognition.idle_shutdown_grace_ms", 0],
    ],
  });

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    is(await startSession(browser), "start", "First session started");
    await waitForHWInferenceProcessCount(1, "HWInference process is running");

    await killHWInferenceProcess();

    is(await startSession(browser), "start", "Session started on a relaunch");
    await waitForHWInferenceProcessCount(1, "HWInference process relaunched");
  });

  await waitForHWInferenceProcessCount(
    0,
    "HWInference process shut down cleanly once the tab is gone"
  );

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    is(await startSession(browser), "start", "Session started after the reset");
    await waitForHWInferenceProcessCount(1, "HWInference process is running");

    await killHWInferenceProcess();

    is(
      await startSession(browser),
      "start",
      "The clean shutdown restored the budget, so this crash is restarted from"
    );
    await waitForHWInferenceProcessCount(1, "HWInference process relaunched");
  });

  await waitForHWInferenceProcessCount(
    0,
    "HWInference process shut down cleanly once the tab is gone"
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_gives_up_after_max_restarts() {
  await SpecialPowers.pushPrefEnv({
    set: [
      // One restart, so a single kill spends the budget.
      ["browser.ml.hwinference.max_restarts", 1],
      // Keeps the parent's connection count up across the kill.
      ["media.webspeech.recognition.idle_shutdown_grace_ms", 60000],
    ],
  });

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    is(await startSession(browser), "start", "First session started");
    await waitForHWInferenceProcessCount(1, "HWInference process is running");

    await killHWInferenceProcess();

    is(
      await startSession(browser),
      "error: service-not-allowed",
      "Session fails once the restart budget is spent"
    );
    is(
      await hwInferenceProcessCount(),
      0,
      "The process was not restarted: we gave up rather than looping"
    );
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_stalled_launch_fails_then_recovers() {
  await SpecialPowers.pushPrefEnv({
    set: [
      // The launch never completes, and is abandoned right away.
      ["dom.ipc.utilityProcess.testing.skipInitCompleted", true],
      ["dom.ipc.utilityProcess.startup_timeout_ms", 1],
      ["media.webspeech.recognition.idle_shutdown_grace_ms", 0],
      // Abandoned launches spend the restart budget, which earlier tasks may
      // have partly spent already. Recovery is tested here, not the budget.
      ["browser.ml.hwinference.max_restarts", 100],
    ],
  });

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    is(
      await startSession(browser),
      "error: service-not-allowed",
      "A session whose process never came up fails instead of hanging"
    );

    await SpecialPowers.pushPrefEnv({
      clear: [
        ["dom.ipc.utilityProcess.testing.skipInitCompleted"],
        ["dom.ipc.utilityProcess.startup_timeout_ms"],
      ],
    });

    is(
      await startSession(browser),
      "start",
      "The next session starts after an abandoned launch"
    );

    await SpecialPowers.popPrefEnv();
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_stalled_launch_spends_restart_budget() {
  await waitForHWInferenceProcessCount(
    0,
    "The previous task's process shut down cleanly, restoring the budget"
  );

  await SpecialPowers.pushPrefEnv({
    set: [
      // One restart, so a single abandoned launch spends the budget.
      ["browser.ml.hwinference.max_restarts", 1],
      // The launch never completes, and is abandoned right away.
      ["dom.ipc.utilityProcess.testing.skipInitCompleted", true],
      ["dom.ipc.utilityProcess.startup_timeout_ms", 1],
      // Keeps the parent's connection count up across the failed session.
      ["media.webspeech.recognition.idle_shutdown_grace_ms", 60000],
    ],
  });

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    is(
      await startSession(browser),
      "error: service-not-allowed",
      "A session whose process never came up fails"
    );

    await SpecialPowers.pushPrefEnv({
      clear: [
        ["dom.ipc.utilityProcess.testing.skipInitCompleted"],
        ["dom.ipc.utilityProcess.startup_timeout_ms"],
      ],
    });

    is(
      await startSession(browser),
      "error: service-not-allowed",
      "Session fails once the abandoned launch spent the restart budget"
    );
    is(await hwInferenceProcessCount(), 0, "The process was not relaunched");

    await SpecialPowers.popPrefEnv();
  });

  await SpecialPowers.popPrefEnv();
});
