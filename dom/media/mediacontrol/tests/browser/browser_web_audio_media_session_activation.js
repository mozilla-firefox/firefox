const PAGE_WEB_AUDIO =
  "https://example.com/browser/dom/media/mediacontrol/tests/browser/file_web_audio.html";

/**
 * A page can declare a Media Session and produce its audio purely through the
 * Web Audio API, with no HTMLMediaElement anywhere in the document. The
 * controller should activate for such a page when it has both declared itself
 * playing through a real Media Session and registered real
 * play/pause action handlers, while the tab is actually audible. Without a
 * registered handler there is no default action to fall back to for Web
 * Audio the way there is for an HTMLMediaElement, so the platform's
 * play/pause keys would otherwise do nothing.
 */

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["media.mediacontrol.webaudio.activation.enabled", true]],
  });
});

add_task(async function activatesWithHandlersRegisteredBeforeAudible() {
  const tab = await createLoadedTabWrapper(PAGE_WEB_AUDIO, {
    needCheck: false,
  });

  await declareMediaSessionPlaying(tab);
  await registerActionHandler(tab, "play");
  await registerActionHandler(tab, "pause");

  const activated = new Promise(r => (tab.controller.onactivated = r));
  await startWebAudio(tab);
  await activated;
  ok(tab.controller.isActive, "controller activated once audible");

  await tab.close();
});

add_task(async function activatesWithHandlersRegisteredAfterAudible() {
  const tab = await createLoadedTabWrapper(PAGE_WEB_AUDIO, {
    needCheck: false,
  });

  await declareMediaSessionPlaying(tab);
  await startWebAudio(tab);
  ok(
    !tab.controller.isActive,
    "not active yet: audible and playing, but no handlers registered"
  );

  await registerActionHandler(tab, "play");
  ok(
    !tab.controller.isActive,
    "still not active with only one of play/pause registered"
  );

  const activated = new Promise(r => (tab.controller.onactivated = r));
  await registerActionHandler(tab, "pause");
  await activated;
  ok(
    tab.controller.isActive,
    "controller activates once both handlers are registered, even though " +
      "the tab was already audible and playing"
  );

  await tab.close();
});

add_task(async function doesNotActivateWithoutActionHandlers() {
  const tab = await createLoadedTabWrapper(PAGE_WEB_AUDIO, {
    needCheck: false,
  });

  await declareMediaSessionPlaying(tab);
  await startWebAudio(tab);
  ok(
    !tab.controller.isActive,
    "must not activate: no play/pause handlers means the platform's " +
      "media keys would have nothing to act on"
  );

  await tab.close();
});

add_task(async function doesNotActivateWithoutMediaSession() {
  const tab = await createLoadedTabWrapper(PAGE_WEB_AUDIO, {
    needCheck: false,
  });

  // Deliberately never touches navigator.mediaSession.
  await startWebAudio(tab);
  ok(
    !tab.controller.isActive,
    "audible Web Audio with no Media Session at all must not activate"
  );

  await tab.close();
});

add_task(async function doesNotActivateWhileInaudible() {
  const tab = await createLoadedTabWrapper(PAGE_WEB_AUDIO, {
    needCheck: false,
  });

  await registerActionHandler(tab, "play");
  await registerActionHandler(tab, "pause");
  await declareMediaSessionPlaying(tab);
  ok(
    !tab.controller.isActive,
    "a declared-playing session with real handlers must not activate " +
      "while the tab is inaudible"
  );

  await tab.close();
});

add_task(async function doesNotActivateWhilePlayingSilently() {
  const tab = await createLoadedTabWrapper(PAGE_WEB_AUDIO, {
    needCheck: false,
  });

  await startSilentWebAudio(tab);
  await registerActionHandler(tab, "play");
  await registerActionHandler(tab, "pause");
  await declareMediaSessionPlaying(tab);

  ok(
    !tab.controller.isAudible,
    "a running but silent web audio graph must not make the tab audible"
  );
  ok(
    !tab.controller.isActive,
    "a declared-playing session with real handlers must not activate while " +
      "its web audio is playing silently"
  );

  await tab.close();
});

add_task(async function staysActiveAcrossPauseAndResume() {
  const tab = await createLoadedTabWrapper(PAGE_WEB_AUDIO, {
    needCheck: false,
  });

  await registerActionHandler(tab, "play");
  await registerActionHandler(tab, "pause");
  await declareMediaSessionPlaying(tab);
  await startWebAudio(tab);
  ok(tab.controller.isActive, "controller activates while audible");

  // What pressing pause on the platform's controls amounts to: the page
  // declares itself paused and its audio stops.
  const becameInaudible = waitForControllerInaudible(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.navigator.mediaSession.playbackState = "paused";
    await content.wrappedJSObject.suspendAudioContext();
  });
  await becameInaudible;

  ok(
    tab.controller.isActive,
    "controller must stay active while paused, otherwise there is nothing " +
      "left to receive the play that follows"
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.resumeAudioContext();
    content.navigator.mediaSession.playbackState = "playing";
  });
  await waitForControllerAudible(tab);
  ok(tab.controller.isActive, "controller is still active after resuming");

  await tab.close();
});

add_task(async function doesNotActivateWhenPrefIsDisabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["media.mediacontrol.webaudio.activation.enabled", false]],
  });

  const tab = await createLoadedTabWrapper(PAGE_WEB_AUDIO, {
    needCheck: false,
  });

  await registerActionHandler(tab, "play");
  await registerActionHandler(tab, "pause");
  await declareMediaSessionPlaying(tab);
  await startWebAudio(tab);
  ok(
    !tab.controller.isActive,
    "must not activate while the pref is disabled, even with everything " +
      "else in place"
  );

  await tab.close();
  await SpecialPowers.popPrefEnv();
});

/**
 * The following are helper functions.
 */
function declareMediaSessionPlaying(tab) {
  return SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    content.navigator.mediaSession.playbackState = "playing";
  });
}

function registerActionHandler(tab, action) {
  return SpecialPowers.spawn(tab.linkedBrowser, [action], action => {
    content.navigator.mediaSession.setActionHandler(action, () => {});
  });
}

function waitForControllerInaudible(tab) {
  const controller = tab.controller;
  if (!controller.isAudible) {
    return Promise.resolve();
  }
  return new Promise(resolve => {
    controller.addEventListener("audiblechange", function handler() {
      if (!controller.isAudible) {
        controller.removeEventListener("audiblechange", handler);
        resolve();
      }
    });
  });
}

function waitForControllerAudible(tab) {
  const controller = tab.controller;
  if (controller.isAudible) {
    return Promise.resolve();
  }
  return new Promise(resolve => {
    controller.addEventListener("audiblechange", function handler() {
      if (controller.isAudible) {
        controller.removeEventListener("audiblechange", handler);
        resolve();
      }
    });
  });
}

async function startSilentWebAudio(tab) {
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.document.getElementById("startSilent").click();
    await content.wrappedJSObject.waitForAudioContextState("running");
  });
}

async function startWebAudio(tab) {
  const becameAudible = waitForControllerAudible(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.document.getElementById("start").click();
    await content.wrappedJSObject.waitForAudioContextState("running");
  });
  await becameAudible;
}
