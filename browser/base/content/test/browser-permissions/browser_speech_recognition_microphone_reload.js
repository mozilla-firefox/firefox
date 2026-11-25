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
    const principal = Services.scriptSecurityManager.createContentPrincipal(
      Services.io.newURI(PAGE),
      {}
    );
    Services.perms.removeFromPrincipal(principal, "microphone");
    Services.perms.removeFromPrincipal(principal, "camera");
    Services.perms.removeFromPrincipal(principal, "MediaManagerVideo");
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
