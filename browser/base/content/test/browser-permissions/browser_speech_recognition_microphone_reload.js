/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const PAGE =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "speech_recognition_microphone.html";
const NOTIFICATION_ID = "webRTC-shareDevices";

function runPageCommand(browser, commandName) {
  return SpecialPowers.spawn(browser, [commandName], async name => {
    const reply = new Promise(resolve => {
      const onMessage = event => {
        if (event.data != name) {
          content.removeEventListener("message", onMessage);
          resolve(event.data);
        }
      };
      content.addEventListener("message", onMessage);
    });
    content.postMessage(name, "*");
    return reply;
  });
}

function runFrameCommand(browser, commandName) {
  return SpecialPowers.spawn(browser, [commandName], async name => {
    const frame = content.document.querySelector("iframe");
    const reply = new Promise(resolve => {
      const onMessage = event => {
        if (event.source == frame.contentWindow && event.data != name) {
          content.removeEventListener("message", onMessage);
          resolve(event.data);
        }
      };
      content.addEventListener("message", onMessage);
    });
    frame.contentWindow.postMessage(name, "*");
    return reply;
  });
}

function waitForMicrophonePrompt(browser) {
  let listener;
  const promise = new Promise(resolve => {
    listener = event => {
      if (
        PopupNotifications.getNotification(NOTIFICATION_ID, browser) &&
        (PopupNotifications.panel.state == "open" ||
          event?.detail.includes(NOTIFICATION_ID))
      ) {
        PopupNotifications.panel.removeEventListener("PanelUpdated", listener);
        resolve();
      }
    };
    PopupNotifications.panel.addEventListener("PanelUpdated", listener);
    listener();
  });
  return {
    promise,
    cancel() {
      PopupNotifications.panel.removeEventListener("PanelUpdated", listener);
    },
  };
}

function graceGrants(browser, type = "microphone") {
  return SitePermissions.getAllForBrowser(browser).filter(
    permission =>
      permission.id.startsWith(`${type}^`) &&
      permission.state == SitePermissions.ALLOW &&
      permission.scope == SitePermissions.SCOPE_TEMPORARY
  );
}

function graceGrantsForPrincipal(browser, principal) {
  return Services.perms
    .getAllForBrowser(principal, browser.browserId)
    .filter(permission => permission.type.startsWith("microphone^"));
}

function microphoneCaptureState(browser) {
  return SpecialPowers.spawn(browser, [], () => {
    const mediaManager = Cc["@mozilla.org/mediaManagerService;1"].getService(
      Ci.nsIMediaManagerService
    );
    const camera = {};
    const microphone = {};
    const screen = {};
    const window = {};
    const browserCapture = {};
    const devices = {};
    mediaManager.mediaCaptureWindowState(
      content,
      camera,
      microphone,
      screen,
      window,
      browserCapture,
      devices,
      false
    );
    return microphone.value;
  });
}

async function startAndAllow(
  browser,
  commandName = "start",
  expected = "started"
) {
  const prompt = waitForMicrophonePrompt(browser);
  const started = runPageCommand(browser, commandName);
  await prompt.promise;
  ok(
    PopupNotifications.getNotification(NOTIFICATION_ID, browser),
    "Device doorhanger shown"
  );
  const panelHidden = BrowserTestUtils.waitForPopupEvent(
    PopupNotifications.panel,
    "hidden"
  );
  PopupNotifications.panel.firstElementChild.button.click();
  is(await started, expected, "Capture started after allowing");
  await panelHidden;
}

async function startFrameAndAllow(browser) {
  const prompt = waitForMicrophonePrompt(browser);
  const started = runFrameCommand(browser, "start-gum");
  await prompt.promise;
  const panelHidden = BrowserTestUtils.waitForPopupEvent(
    PopupNotifications.panel,
    "hidden"
  );
  PopupNotifications.panel.firstElementChild.button.click();
  is(await started, "gum-started", "Frame started microphone capture");
  await panelHidden;
}

async function startWithoutPrompting(
  browser,
  commandName = "start",
  expected = "started"
) {
  const prompt = waitForMicrophonePrompt(browser);
  const started = runPageCommand(browser, commandName);
  const outcome = await Promise.race([
    started.then(reply => `session: ${reply}`),
    prompt.promise.then(() => "prompted"),
  ]);
  prompt.cancel();
  is(outcome, `session: ${expected}`, "Capture restarted without prompting");
  if (outcome == "prompted") {
    PopupNotifications.panel.firstElementChild.button.click();
    await started;
  }
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["media.webspeech.recognition.enable", true],
      ["browser.ml.modelHub.testing", true],
      ["media.webspeech.recognition.model-download.prompt.testing", true],
      ["media.navigator.streams.fake", true],
      ["media.audio_loopback_dev", ""],
      ["media.video_loopback_dev", ""],
      ["media.navigator.permission.fake", true],
      ["media.navigator.permission.disabled", true],
    ],
  });

  await BrowserTestUtils.withNewTab(PAGE, browser =>
    SpecialPowers.spawn(browser, [], () => {
      content.document.notifyUserGestureActivation();
      return content.SpeechRecognition.install({
        langs: ["en-US"],
        processLocally: true,
      });
    })
  );

  await SpecialPowers.pushPrefEnv({
    set: [["media.navigator.permission.disabled", false]],
  });

  registerCleanupFunction(() => {
    for (const origin of ["https://example.com", "https://example.org"]) {
      const principal = Services.scriptSecurityManager.createContentPrincipal(
        Services.io.newURI(origin),
        {}
      );
      for (const type of ["microphone", "camera", "MediaManagerVideo"]) {
        Services.perms.removeFromPrincipal(principal, type);
      }
    }
  });
});

add_task(
  async function test_active_recognition_grace_survives_reload_with_frame() {
    await BrowserTestUtils.withNewTab(PAGE, async browser => {
      await startAndAllow(browser);
      is(
        await microphoneCaptureState(browser),
        Ci.nsIMediaManagerService.STATE_CAPTURE_ENABLED,
        "Recognition is actively capturing before reload"
      );

      const loaded = BrowserTestUtils.browserLoaded(browser, false, PAGE);
      const reloadButton = document.getElementById("reload-button");
      await TestUtils.waitForCondition(() => !reloadButton.disabled);
      reloadButton.click();
      await loaded;

      await TestUtils.waitForCondition(
        () => graceGrants(browser).length == 1,
        "Waiting for reload to create microphone grace"
      );
      await startWithoutPrompting(browser);
      await runPageCommand(browser, "abort");
    });
  }
);

add_task(async function test_frame_gum_grace_survives_frame_removal() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    const frameBrowsingContext = browser.browsingContext.children[0];
    const framePrincipal =
      frameBrowsingContext.currentWindowGlobal.documentPrincipal;

    await startFrameAndAllow(browser);
    is(
      await microphoneCaptureState(frameBrowsingContext),
      Ci.nsIMediaManagerService.STATE_CAPTURE_ENABLED,
      "The frame is actively capturing"
    );

    const discarded = BrowserUtils.promiseObserved(
      "browsing-context-discarded",
      subject => subject == frameBrowsingContext
    );
    await SpecialPowers.spawn(browser, [], () => {
      content.document.querySelector("iframe").remove();
    });
    await discarded;

    await TestUtils.waitForCondition(
      () => graceGrants(browser).length == 1,
      "Waiting for frame removal to create microphone grace"
    );
    is(
      graceGrantsForPrincipal(browser, framePrincipal).length,
      0,
      "Frame capture uses the top-level principal for microphone grace"
    );

    await startWithoutPrompting(browser);
    await runPageCommand(browser, "abort");
  });
});

add_task(async function test_camera_grace_survives_reload_with_frame() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await startAndAllow(browser, "start-camera", "camera-started");

    const loaded = BrowserTestUtils.browserLoaded(browser, false, PAGE);
    const reloadButton = document.getElementById("reload-button");
    await TestUtils.waitForCondition(() => !reloadButton.disabled);
    reloadButton.click();
    await loaded;

    await TestUtils.waitForCondition(
      () => graceGrants(browser, "camera").length == 1,
      "Waiting for reload to create camera grace"
    );
    await startWithoutPrompting(browser, "start-camera", "camera-started");
    is(
      await runPageCommand(browser, "stop-camera"),
      "camera-stopped",
      "Camera capture ended"
    );
  });
});

async function abortAndWaitForGrace(browser, count = 1) {
  is(await runPageCommand(browser, "abort"), "ended", "Session ended");
  await TestUtils.waitForCondition(
    () => graceGrants(browser).length == count,
    `Waiting for ${count} microphone grace grants`
  );
}

add_task(async function test_session_takes_a_grace_period() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    is(graceGrants(browser).length, 0, "Baseline is no grace periods");
    await startAndAllow(browser);
    await abortAndWaitForGrace(browser);
    await startWithoutPrompting(browser);
    await abortAndWaitForGrace(browser);
  });
});

add_task(async function test_grace_period_survives_reload() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await startAndAllow(browser);
    await abortAndWaitForGrace(browser);

    const loaded = BrowserTestUtils.browserLoaded(browser, false, PAGE);
    browser.reload();
    await loaded;

    is(graceGrants(browser).length, 1, "The grace period survived the reload");
    await startWithoutPrompting(browser);
    await abortAndWaitForGrace(browser);
  });
});

add_task(async function test_grace_period_survives_same_origin_navigation() {
  const secondPage = PAGE + "?second";
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await startAndAllow(browser);
    await abortAndWaitForGrace(browser);

    const loaded = BrowserTestUtils.browserLoaded(browser, false, secondPage);
    await SpecialPowers.spawn(browser, [secondPage], url => {
      content.document.location = url;
    });
    await loaded;

    is(graceGrants(browser).length, 1, "The grace period survived navigation");
    await startWithoutPrompting(browser);
    await abortAndWaitForGrace(browser);
  });
});

add_task(async function test_cross_origin_navigation_prompts_again() {
  const otherPage = PAGE.replace("https://example.com", "https://example.org");
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await startAndAllow(browser);
    await abortAndWaitForGrace(browser);

    const loaded = BrowserTestUtils.browserLoaded(browser, false, otherPage);
    await SpecialPowers.spawn(browser, [otherPage], url => {
      content.document.location = url;
    });
    await loaded;

    is(graceGrants(browser).length, 0, "The other origin has no grace period");
    await startAndAllow(browser);
    await abortAndWaitForGrace(browser);
  });
});

add_task(async function test_denying_the_prompt_fails_the_session() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    const prompt = waitForMicrophonePrompt(browser);
    const started = runPageCommand(browser, "start");
    await prompt.promise;

    const hidden = BrowserTestUtils.waitForPopupEvent(
      PopupNotifications.panel,
      "hidden"
    );
    PopupNotifications.panel.firstElementChild.secondaryButton.click();
    is(
      await started,
      "error: not-allowed",
      "A refused microphone fails the session with not-allowed"
    );
    await hidden;
    is(graceGrants(browser).length, 0, "A refusal leaves no grace period");
  });
});

add_task(async function test_repeated_sessions_reuse_one_grace_period() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await startAndAllow(browser);
    await abortAndWaitForGrace(browser);
    const grantId = graceGrants(browser)[0].id;

    for (let i = 2; i <= 4; i++) {
      info(`Starting session ${i}`);
      await startWithoutPrompting(browser);
      await abortAndWaitForGrace(browser);
      is(
        graceGrants(browser)[0].id,
        grantId,
        `Session ${i} reused the same device grant`
      );
    }
  });
});

add_task(async function test_replacing_the_microphone_prompts_again() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await startAndAllow(browser);
    await abortAndWaitForGrace(browser);

    await SpecialPowers.pushPrefEnv({
      set: [["media.getusermedia.fake-microphone-name", "Another Microphone"]],
    });
    try {
      const prompt = waitForMicrophonePrompt(browser);
      const started = runPageCommand(browser, "start");
      await prompt.promise;
      is(
        document.getElementById("webRTC-selectMicrophone-menulist").label,
        "Another Microphone",
        "The prompt is for the replacement microphone"
      );

      const hidden = BrowserTestUtils.waitForPopupEvent(
        PopupNotifications.panel,
        "hidden"
      );
      PopupNotifications.panel.firstElementChild.button.click();
      is(await started, "started", "Session started on the new microphone");
      await hidden;
      await abortAndWaitForGrace(browser, 2);
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  });
});
