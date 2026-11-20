/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests that a capture prompt for a request made on the opener follows the
// user into the document picture-in-picture window it was triggered from.

const { DOMFullscreenTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/DOMFullscreenTestUtils.sys.mjs"
);

DOMFullscreenTestUtils.init(this, window);

/**
 * Open a tab and a document PiP window from it, and give the PiP document a
 * content-privileged helper to request capture with. Requesting from the
 * sandbox instead would make the call privileged, which skips the prompt.
 *
 * @returns {Promise<[MozTabbrowserTab, Window]>} The tab and the PiP window.
 */
async function openTabWithPiP() {
  let rootDir = getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content/",
    "https://example.com/"
  );
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    rootDir + "get_user_media.html"
  );

  let pipWinPromise = BrowserTestUtils.waitForNewWindow();
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.document.notifyUserGestureActivation();
    await content.documentPictureInPicture.requestWindow();
  });
  let pipWin = await pipWinPromise;
  await SimpleTest.promiseFocus(pipWin);

  await SpecialPowers.spawn(pipWin.gBrowser.selectedBrowser, [], () => {
    let script = content.document.createElement("script");
    script.textContent = `
      window.requestCapture = (target, kind) => {
        let win = target == "opener" ? window.opener : window;
        let md = win.navigator.mediaDevices;
        let promise = kind == "screen"
          ? md.getDisplayMedia({ video: true })
          : md.getUserMedia({ [kind == "camera" ? "video" : "audio"]: true });
        // Exposed for the test to await. It settles only once the prompt is
        // answered, and resolves either way so awaiting it can't produce an
        // unhandled rejection. Parked on the target window, which for
        // "opener" outlives this one, along with any stream for the test to
        // stop.
        win.captureOutcome = promise.then(
          stream => {
            win.captureStream = stream;
            return "granted";
          },
          error => error.name
        );
      };
    `;
    content.document.body.appendChild(script);
  });

  return [tab, pipWin];
}

/**
 * Request capture from inside the PiP document, on either window's
 * mediaDevices. A user gesture in the PiP propagates transient activation to
 * the opener, so getDisplayMedia() is allowed on either.
 *
 * @param {Window} aPipWin - The PiP chrome window.
 * @param {"opener"|"pip"} aTarget - Whose mediaDevices to call.
 * @param {"screen"|"camera"|"microphone"} aKind - What to capture.
 */
function requestCaptureFromPiP(aPipWin, aTarget, aKind) {
  return SpecialPowers.spawn(
    aPipWin.gBrowser.selectedBrowser,
    [aTarget, aKind],
    (target, kind) => {
      content.document.notifyUserGestureActivation();
      content.wrappedJSObject.requestCapture(target, kind);
    }
  );
}

/**
 * Deny any outstanding request and tear down the tab and PiP window.
 *
 * @param {MozTabbrowserTab} aTab - The opener tab.
 * @param {Window} aPipWin - The PiP chrome window.
 */
async function cleanUp(aTab, aPipWin) {
  for (let win of [aPipWin, window]) {
    win.PopupNotifications.getNotification("webRTC-shareDevices")?.remove();
  }
  await BrowserTestUtils.closeWindow(aPipWin);
  BrowserTestUtils.removeTab(aTab);
  await checkNotSharing();
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_PERMISSION_FAKE, true],
      [PREF_AUDIO_LOOPBACK, ""],
      [PREF_VIDEO_LOOPBACK, ""],
      [PREF_FAKE_STREAMS, true],
      [PREF_FOCUS_SOURCE, false],
      ["dom.documentpip.enabled", true],
    ],
  });
});

add_task(async function testScreenOnOpenerPromptsInFocusedPiP() {
  let [tab, pipWin] = await openTabWithPiP();

  let shown = promisePopupNotificationShown(
    "webRTC-shareDevices",
    null,
    pipWin
  );
  await requestCaptureFromPiP(pipWin, "opener", "screen");
  await shown;

  is(
    pipWin.PopupNotifications.getNotification("webRTC-shareDevices").anchorID,
    "webRTC-shareScreen-notification-icon",
    "anchored to the screen sharing icon in the PiP window"
  );
  ok(
    !PopupNotifications.getNotification("webRTC-shareDevices"),
    "no prompt in the opener window"
  );
  checkDeviceSelectors(["screen"], pipWin);

  await cleanUp(tab, pipWin);
});

add_task(async function testClosingPiPDeniesPendingRequest() {
  let [tab, pipWin] = await openTabWithPiP();

  let shown = promisePopupNotificationShown(
    "webRTC-shareDevices",
    null,
    pipWin
  );
  await requestCaptureFromPiP(pipWin, "opener", "screen");
  await shown;

  // Closing a window doesn't fire TabClose, so PopupNotifications won't deny
  // the request for us.
  await BrowserTestUtils.closeWindow(pipWin);

  let result = await SpecialPowers.spawn(
    tab.linkedBrowser,
    [],
    () => content.wrappedJSObject.captureOutcome
  );
  is(result, "NotAllowedError", "closing the PiP denies the pending request");

  BrowserTestUtils.removeTab(tab);
  await checkNotSharing();
});

add_task(async function testClosingOpenerWithPromptInPiP() {
  let [tab, pipWin] = await openTabWithPiP();

  let shown = promisePopupNotificationShown(
    "webRTC-shareDevices",
    null,
    pipWin
  );
  await requestCaptureFromPiP(pipWin, "opener", "screen");
  await shown;

  // Closing the opener destroys the window global backing the actor, and
  // closes the PiP with it. The PiP unload handler must not try to deny
  // through the dead actor.
  let consoleErrors = [];
  let listener = message => {
    if (message instanceof Ci.nsIScriptError) {
      consoleErrors.push(message.errorMessage);
    }
  };
  Services.console.registerListener(listener);

  let pipClosed = BrowserTestUtils.windowClosed(pipWin);
  BrowserTestUtils.removeTab(tab);
  await pipClosed;
  ok(true, "closing the opener closed the PiP window");

  // The error is reported asynchronously, so let it land before we look.
  await TestUtils.waitForTick();
  Services.console.unregisterListener(listener);
  Assert.deepEqual(
    consoleErrors.filter(error => error.includes("JSWindowActorParent")),
    [],
    "no attempt to send through the dead actor"
  );

  await checkNotSharing();
});

add_task(async function testBlockInPiPAppliesToOpener() {
  let [tab, pipWin] = await openTabWithPiP();

  let shown = promisePopupNotificationShown(
    "webRTC-shareDevices",
    null,
    pipWin
  );
  await requestCaptureFromPiP(pipWin, "opener", "screen");
  await shown;

  await activateSecondaryAction(kActionDeny, pipWin);
  await TestUtils.waitForCondition(
    () => !pipWin.PopupNotifications.getNotification("webRTC-shareDevices"),
    "prompt is dismissed"
  );

  let principal = tab.linkedBrowser.contentPrincipal;
  is(
    SitePermissions.getForPrincipal(principal, "screen", tab.linkedBrowser)
      .state,
    SitePermissions.BLOCK,
    "block is recorded against the opener browser, which owns the stream"
  );
  is(
    SitePermissions.getForPrincipal(
      principal,
      "screen",
      pipWin.gBrowser.selectedBrowser
    ).state,
    SitePermissions.UNKNOWN,
    "not against the PiP browser the prompt was anchored to"
  );

  SitePermissions.removeFromPrincipal(principal, "screen", tab.linkedBrowser);
  await cleanUp(tab, pipWin);
});

add_task(async function testScreenOnPiPPromptsInPiP() {
  let [tab, pipWin] = await openTabWithPiP();

  let shown = promisePopupNotificationShown(
    "webRTC-shareDevices",
    null,
    pipWin
  );
  await requestCaptureFromPiP(pipWin, "pip", "screen");
  await shown;

  ok(
    !PopupNotifications.getNotification("webRTC-shareDevices"),
    "no prompt in the opener window"
  );
  checkDeviceSelectors(["screen"], pipWin);

  await cleanUp(tab, pipWin);
});

add_task(async function testScreenOnOpenerPromptsInFocusedOpener() {
  let [tab, pipWin] = await openTabWithPiP();
  await SimpleTest.promiseFocus(window);

  let shown = promisePopupNotificationShown("webRTC-shareDevices");
  await requestCaptureFromPiP(pipWin, "opener", "screen");
  await shown;

  ok(
    !pipWin.PopupNotifications.getNotification("webRTC-shareDevices"),
    "no prompt in the unfocused PiP window"
  );
  checkDeviceSelectors(["screen"]);

  await cleanUp(tab, pipWin);
});

for (let kind of ["camera", "microphone"]) {
  add_task(async function testCameraOrMicOnOpenerPromptsInFocusedPiP() {
    let [tab, pipWin] = await openTabWithPiP();

    let shown = promisePopupNotificationShown(
      "webRTC-shareDevices",
      null,
      pipWin
    );
    await requestCaptureFromPiP(pipWin, "opener", kind);
    await shown;

    ok(
      !PopupNotifications.getNotification("webRTC-shareDevices"),
      `no ${kind} prompt in the opener window`
    );
    checkDeviceSelectors([kind], pipWin);

    await cleanUp(tab, pipWin);
  });
}

add_task(async function testGrantedCameraOnOpenerNeedsNoOpenerFocus() {
  let [tab, pipWin] = await openTabWithPiP();
  let principal = tab.linkedBrowser.contentPrincipal;
  SitePermissions.setForPrincipal(principal, "camera", SitePermissions.ALLOW);
  clearDevicePermissionsAtEnd(principal);

  // Like a meeting site unmuting its camera from the PiP. Grants are checked
  // when the prompt starts showing, so this used to wait for opener focus.
  await requestCaptureFromPiP(pipWin, "opener", "camera");
  let result = await SpecialPowers.spawn(
    tab.linkedBrowser,
    [],
    () => content.wrappedJSObject.captureOutcome
  );
  is(result, "granted", "granted without focusing the opener");
  is(Services.focus.activeWindow, pipWin, "PiP window kept focus");

  // Let the sharing UI catch up before stopping, so cleanUp() waits for it to
  // go away rather than passing before it shows.
  let { webrtcUI } = ChromeUtils.importESModule(
    "resource:///modules/webrtcUI.sys.mjs"
  );
  await TestUtils.waitForCondition(
    () => webrtcUI.showCameraIndicator,
    "camera indicator shows"
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [], () =>
    content.wrappedJSObject.captureStream
      .getTracks()
      .forEach(track => track.stop())
  );

  await cleanUp(tab, pipWin);
});

add_task(async function testFullscreenOpenerKeepsItsPrompt() {
  // A user clicking another window doesn't exit fullscreen, but chrome calling
  // focus() does (nsFocusManager's FLAG_RAISE path), so opt out of that to
  // reach the state a user can reach by hand.
  await SpecialPowers.pushPrefEnv({
    set: [["full-screen-api.exit-on.windowRaise", false]],
  });

  let [tab, pipWin] = await openTabWithPiP();

  // Entering DOM fullscreen requires the opener to be the active window, and
  // opening a PiP from fullscreen exits it, so this is the only usable order.
  await SimpleTest.promiseFocus(window);
  await DOMFullscreenTestUtils.changeFullscreen(tab.linkedBrowser, true);
  ok(document.fullscreenElement, "opener is in DOM fullscreen");

  await SimpleTest.promiseFocus(pipWin);
  ok(document.fullscreenElement, "opener is still in DOM fullscreen");

  await requestCaptureFromPiP(pipWin, "opener", "screen");
  await TestUtils.waitForCondition(
    () =>
      PopupNotifications.getNotification(
        "webRTC-shareDevices",
        tab.linkedBrowser
      ),
    "prompt stays on the opener browser"
  );
  ok(
    !pipWin.PopupNotifications.getNotification("webRTC-shareDevices"),
    "prompt is not redirected to the PiP while the opener is fullscreen"
  );

  // Leave fullscreen before the tab goes away, otherwise removing the tab
  // exits it for us and there is no state change left to wait for.
  PopupNotifications.getNotification(
    "webRTC-shareDevices",
    tab.linkedBrowser
  ).remove();
  await DOMFullscreenTestUtils.changeFullscreen(tab.linkedBrowser, false);
  await cleanUp(tab, pipWin);
  await SpecialPowers.popPrefEnv();
});
