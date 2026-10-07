/**
 * Cross-source audio focus competition tests.
 *
 * With media.audioFocus.management = true, the chrome-side AudioFocusManager
 * lets only one tab own audio focus at a time. When a new audible source
 * claims focus, the previous owner receives Stop and silences itself: an
 * HTMLMediaElement pauses, an AudioContext suspends, and a
 * SpeechSynthesisUtterance pauses.
 *
 * The Web Speech tab uses the test fake-synth voice "it-IT-noend"
 * (eSuppressEnd) so the utterance stays speaking through audio-focus
 * competition and FakeSynth's OnPause callback fires the pause event on Stop.
 * That makes the test mechanism platform-independent — real-platform Pause
 * support (macOS/Windows have it; Linux/Android don't, tracked by Bug
 * 2038329 / Bug 1238538) is exercised separately by the underlying Pause
 * code path.
 */

const PAGE_HTML_MEDIA =
  "https://example.com/browser/dom/media/mediacontrol/tests/browser/file_autoplay.html";
const PAGE_WEB_AUDIO =
  "https://example.com/browser/dom/media/mediacontrol/tests/browser/file_web_audio.html";
const PAGE_WEB_SPEECH =
  "https://example.com/browser/dom/media/mediacontrol/tests/browser/file_web_speech.html";

const HTML_MEDIA_ID = "autoplay";

// Each source describes how to open it, start it, wait for its silence
// response (when it loses audio focus) and tear it down.
const HTMLMediaSource = {
  name: "HTMLMediaElement",
  page: PAGE_HTML_MEDIA,
  start: startHTMLMediaAndWaitAudible,
  waitForSilenced: tab =>
    checkOrWaitUntilMediaStoppedPlaying(tab, HTML_MEDIA_ID),
  cleanup: () => Promise.resolve(),
};

const WebAudioSource = {
  name: "Web Audio",
  page: PAGE_WEB_AUDIO,
  start: startWebAudio,
  waitForSilenced: waitForAudioContextSuspended,
  cleanup: () => Promise.resolve(),
};

const WebSpeechSource = {
  name: "Web Speech",
  page: PAGE_WEB_SPEECH,
  start: startWebSpeechAndWaitAudible,
  waitForSilenced: waitForSpeechPaused,
  cleanup: cancelSpeech,
};

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["media.audioFocus.management", true],
      ["media.audioFocus.webaudio.enabled", true],
    ],
  });
});

add_task(async function test_html_media_vs_web_audio() {
  await testFocusStealingBetween(HTMLMediaSource, WebAudioSource);
});

add_task(async function test_html_media_vs_web_speech() {
  await testFocusStealingBetween(HTMLMediaSource, WebSpeechSource);
});

add_task(async function test_web_audio_vs_web_speech() {
  await testFocusStealingBetween(WebAudioSource, WebSpeechSource);
});

// ----- Helpers -----

// Run focus competition between two sources in both directions: the prior
// owner starts first then the new claimer takes focus and silences the prior
// owner; then swap the roles and repeat.
async function testFocusStealingBetween(a, b) {
  for (const [priorOwner, newClaimer] of [
    [a, b],
    [b, a],
  ]) {
    info(`${priorOwner.name} (tab1) vs ${newClaimer.name} (tab2)`);
    const tab1 = await createLoadedTabWrapper(priorOwner.page, {
      needCheck: false,
    });
    await priorOwner.start(tab1);

    const tab2 = await createLoadedTabWrapper(newClaimer.page, {
      needCheck: false,
    });
    await newClaimer.start(tab2);

    info(`${priorOwner.name} in tab1 should be silenced by audio focus loss`);
    await priorOwner.waitForSilenced(tab1);

    await priorOwner.cleanup(tab1);
    await newClaimer.cleanup(tab2);
    await closeTabs([tab1, tab2]);
  }
}

function waitForControllerAudible(tab) {
  const controller = tab.linkedBrowser.browsingContext.mediaController;
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

// Audio focus is only claimed once a tab becomes audible, so wait for that
// before another source competes for it.
async function startHTMLMediaAndWaitAudible(tab) {
  const becameAudible = waitForControllerAudible(tab);
  await checkOrWaitUntilMediaStartedPlaying(tab, HTML_MEDIA_ID);
  await becameAudible;
}

async function startWebAudio(tab) {
  const becameAudible = waitForControllerAudible(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.document.getElementById("start").click();
    await content.wrappedJSObject.waitForAudioContextState("running");
  });
  await becameAudible;
}

async function startWebSpeech(tab) {
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.document.getElementById("start").click();
    await content.wrappedJSObject.waitForSpeechStart();
  });
}

async function startWebSpeechAndWaitAudible(tab) {
  const becameAudible = waitForControllerAudible(tab);
  await startWebSpeech(tab);
  await becameAudible;
}

async function waitForAudioContextSuspended(tab) {
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.waitForAudioContextState("suspended");
  });
  ok(true, "AudioContext reached suspended state");
}

async function waitForSpeechPaused(tab) {
  const result = await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    return content.wrappedJSObject.waitForSpeechPause();
  });
  ok(result, "Web Speech reached paused state");
}

async function cancelSpeech(tab) {
  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    content.wrappedJSObject.cancelSpeech();
  });
}

async function closeTabs(tabs) {
  for (let tab of tabs) {
    await tab.close();
  }
}
