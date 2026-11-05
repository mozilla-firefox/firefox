/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContentDOMReference: "resource://gre/modules/ContentDOMReference.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  KEYBOARD_CONTROLS:
    "moz-src:///toolkit/components/pictureinpicture/PictureInPictureControls.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  Rect: "resource://gre/modules/Geometry.sys.mjs",
  TOGGLE_POLICIES:
    "moz-src:///toolkit/components/pictureinpicture/PictureInPictureControls.sys.mjs",
  TOGGLE_POLICY_STRINGS:
    "moz-src:///toolkit/components/pictureinpicture/PictureInPictureControls.sys.mjs",
});

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { WebVTT } from "moz-src:///dom/media/webvtt/vtt.sys.mjs";
import { setTimeout, clearTimeout } from "resource://gre/modules/Timer.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "DISPLAY_TEXT_TRACKS_PREF",
  "media.videocontrols.picture-in-picture.display-text-tracks.enabled",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "IMPROVED_CONTROLS_ENABLED_PREF",
  "media.videocontrols.picture-in-picture.improved-video-controls.enabled",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "MIN_VIDEO_LENGTH",
  "media.videocontrols.picture-in-picture.video-toggle.min-video-secs",
  45
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "PIP_TOGGLE_ALWAYS_SHOW",
  "media.videocontrols.picture-in-picture.video-toggle.always-show",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "PIP_URLBAR_BUTTON",
  "media.videocontrols.picture-in-picture.urlbar-button.enabled",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "PIP_AUTO_CLOSE",
  "media.videocontrols.picture-in-picture.auto-close.enabled",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "EMPTIED_TIMEOUT_MS",
  "media.videocontrols.picture-in-picture.auto-close.timeoutMs",
  1000
);

const PIP_ENABLED_PREF = "media.videocontrols.picture-in-picture.enabled";
const TOGGLE_ENABLED_PREF =
  "media.videocontrols.picture-in-picture.video-toggle.enabled";
const TOGGLE_FIRST_SEEN_PREF =
  "media.videocontrols.picture-in-picture.video-toggle.first-seen-secs";
const TOGGLE_FIRST_TIME_DURATION_DAYS = 28;
const TOGGLE_HAS_USED_PREF =
  "media.videocontrols.picture-in-picture.video-toggle.has-used";
const TOGGLE_TESTING_PREF =
  "media.videocontrols.picture-in-picture.video-toggle.testing";
const TOGGLE_VISIBILITY_THRESHOLD_PREF =
  "media.videocontrols.picture-in-picture.video-toggle.visibility-threshold";
const TEXT_TRACK_FONT_SIZE =
  "media.videocontrols.picture-in-picture.display-text-tracks.size";

const MOUSEMOVE_PROCESSING_DELAY_MS = 50;
const TOGGLE_HIDING_TIMEOUT_MS = 3000;
// If you change this, also change VideoControlsWidget.SEEK_TIME_SECS:
const SEEK_TIME_SECS = 5;

// The ToggleChild does not want to capture events from the PiP
// windows themselves. This set contains all currently open PiP
// players' content windows
var gPlayerContents = new WeakSet();

// Maps originating video elements to actor PictureInPictureChild
// PictureInPictureChild is the actor for the PIP window, not the original window
let gOriginatingVideoMap = new WeakMap();

// Same-process hand-off from the launcher actor (in the originating browser)
// to the player actor (in the cloned-about:blank browser) for the PiP web API
// path. The player browser is configured to share the originating browser's
// content process, so this WeakMap is visible to both actors. Keyed by the
// originating <video>; value is its PictureInPictureWindow JS instance.
let gVideoToPipWindow = new WeakMap();

// To make it easier to write tests, we have a process-global
// WeakSet of all <video> elements that are being tracked for
// mouseover
var gWeakIntersectingVideosForTesting = new WeakSet();

// Overrides are expected to stay constant for the lifetime of a
// content process, so we set this as a lazy process global.
// See PictureInPictureToggleChild.getSiteOverrides for a
// sense of what the return types are.
ChromeUtils.defineLazyGetter(lazy, "gSiteOverrides", () => {
  return PictureInPictureToggleChild.getSiteOverrides();
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    logModule: "PictureInPictureChild",
    maxLogLevel: Services.prefs.getBoolPref(
      "media.videocontrols.picture-in-picture.log",
      false
    )
      ? "Debug"
      : "Error",
  });
});

/**
 * Creates and returns an instance of the PictureInPictureChildVideoWrapper class responsible
 * for applying site-specific wrapper methods around the original video.
 *
 * The Picture-In-Picture add-on can use this to provide site-specific wrappers for
 * sites that require special massaging to control.
 *
 * @param {object} pipChild reference to PictureInPictureChild class calling this function
 * @param {Element} originatingVideo
 *   The <video> element to wrap.
 * @returns {PictureInPictureChildVideoWrapper} instance of PictureInPictureChildVideoWrapper
 */
function applyWrapper(pipChild, originatingVideo) {
  let override = getSiteOverrideForDocument(
    originatingVideo.ownerDocument.documentURI
  );
  let wrapperPath = override?.videoWrapperScriptPath;
  return new PictureInPictureChildVideoWrapper(
    wrapperPath,
    originatingVideo,
    pipChild
  );
}

/**
 * Returns the site override object for the given document, or null
 * if no site override applies.
 *
 * @param {string} documentURI
 * @returns { object | null }
 */
function getSiteOverrideForDocument(documentURI) {
  // gSiteOverrides is a list of tuples where the first element is the MatchPattern
  // for a supported site and the second is the actual overrides object for it.
  let overrides = lazy.gSiteOverrides.find(([matcher]) =>
    matcher.matches(documentURI)
  );
  return overrides?.[1] ?? null;
}

export class PictureInPictureLauncherChild extends JSWindowActorChild {
  handleEvent(event) {
    switch (event.type) {
      case "MozTogglePictureInPicture": {
        if (event.isTrusted) {
          this.togglePictureInPicture({
            video: event.target,
            reason: event.detail?.reason,
            eventExtraKeys: event.detail?.eventExtraKeys,
          });
        }
        break;
      }
    }
  }

  receiveMessage(message) {
    switch (message.name) {
      case "PictureInPicture:KeyToggle": {
        this.keyToggle();
        break;
      }
      case "PictureInPicture:AutoToggle": {
        this.autoToggle();
        break;
      }
    }
  }

  /**
   * Tells the parent to open a Picture-in-Picture window hosting
   * a clone of the passed video. If we know about a pre-existing
   * Picture-in-Picture window existing, this tells the parent to
   * close it before opening the new one.
   *
   * @param {object} pipObject
   * @param {HTMLVideoElement} pipObject.video
   * @param {string} pipObject.reason What toggled PiP, e.g. "shortcut"
   * @param {PictureInPictureWindow} pipObject.pictureInPictureWindow The PictureInPictureWindow instance, exposed via webidl
   * @param {object} pipObject.eventExtraKeys Extra telemetry keys to record
   * @param {boolean} autoFocus Autofocus the PiP window (default: true)
   *
   * @returns {Promise<void>}
   *   Resolved once the new Picture-in-Picture window has been requested.
   */
  async togglePictureInPicture(pipObject, autoFocus = true) {
    let {
      video,
      reason,
      pictureInPictureWindow,
      eventExtraKeys = {},
    } = pipObject;
    if (video.isCloningElementVisually) {
      // The only way we could have entered here for the same video is if
      // we are toggling via the context menu or via the urlbar button,
      // since we hide the inline Picture-in-Picture toggle when a video
      // is being displayed in Picture-in-Picture. Turn off PiP in this case
      const stopPipEvent = new this.contentWindow.CustomEvent(
        "MozStopPictureInPicture",
        {
          bubbles: true,
          detail: { reason },
        }
      );
      this.contentWindow.windowUtils.dispatchEventToChromeOnly(
        video,
        stopPipEvent
      );
      return;
    }

    if (!PictureInPictureChild.videoWrapper) {
      PictureInPictureChild.videoWrapper = applyWrapper(
        PictureInPictureChild,
        video
      );
    }

    let timestamp = undefined;
    let scrubberPosition = undefined;

    if (lazy.IMPROVED_CONTROLS_ENABLED_PREF) {
      timestamp = PictureInPictureChild.videoWrapper.formatTimestamp(
        PictureInPictureChild.videoWrapper.getCurrentTime(video),
        PictureInPictureChild.videoWrapper.getDuration(video)
      );

      // Scrubber is hidden if undefined, so only set it to something else
      // if the timestamp is not undefined.
      scrubberPosition =
        timestamp === undefined
          ? undefined
          : PictureInPictureChild.videoWrapper.getCurrentTime(video) /
            PictureInPictureChild.videoWrapper.getDuration(video);
    }

    // All other requests to toggle PiP should open a new PiP
    // window
    const videoRef = lazy.ContentDOMReference.get(video);
    // For the PiP web API path, stash the PictureInPictureWindow keyed by the
    // originating video. pictureInPictureWindow is a non-node and can't be used as a ContentDOMReference.
    if (pictureInPictureWindow) {
      gVideoToPipWindow.set(video, pictureInPictureWindow);
    }

    const res = this.sendQuery("PictureInPicture:Request", {
      isMuted: PictureInPictureChild.videoIsMuted(video),
      playing: PictureInPictureChild.videoIsPlaying(video),
      videoHeight: video.videoHeight,
      videoWidth: video.videoWidth,
      videoRef,
      isPipApiRequest: !!pictureInPictureWindow,
      ccEnabled: lazy.DISPLAY_TEXT_TRACKS_PREF,
      webVTTSubtitles: !!video.textTracks?.length,
      scrubberPosition,
      timestamp,
      volume: PictureInPictureChild.videoWrapper.getVolume(video),
      playbackRate: PictureInPictureChild.videoWrapper.getPlaybackRate(video),
      autoFocus,
    });

    // Some tests don't bother setting a reason.
    if (reason) {
      Glean.pictureinpicture["openedMethod" + reason].record({
        firstTimeToggle: !Services.prefs.getBoolPref(TOGGLE_HAS_USED_PREF),
        ...eventExtraKeys,
      });
    }
    await res;
  }

  /**
   * The keyboard was used to attempt to open Picture-in-Picture.
   * Note that we assume that this method will only be called for the focused
   * document.
   */
  keyToggle() {
    let doc = this.document;
    if (doc) {
      let video = this.findVideoToPiP(doc);
      if (video) {
        this.togglePictureInPicture({ video, reason: "Shortcut" });
      }
    }
  }

  /**
   * If a video is focused, select that video. Otherwise find the first playing
   * video, or if none, the largest dimension video. We suspect this heuristic
   * will handle most cases, though we might refine this later on.
   *
   * @param {HTMLDocument} doc The HTML document to search for a video element in.
   * @returns {HTMLVideoElement} The selected HTML video element to enter PiP mode.
   */
  findVideoToPiP(doc) {
    let video = doc.activeElement;
    if (!HTMLVideoElement.isInstance(video)) {
      let listOfVideos = [...doc.querySelectorAll("video")].filter(
        v => !isNaN(v.duration)
      );
      // Get the first non-paused video, otherwise the longest video. This
      // fallback is designed to skip over "preview"-style videos on sidebars.
      video =
        listOfVideos.filter(v => !v.paused)[0] ||
        listOfVideos.sort((a, b) => b.duration - a.duration)[0];
    }
    return video;
  }

  /**
   * Automatically toggle Picture-in-Picture if a video tab has been
   * backgrounded.
   */
  autoToggle() {
    let doc = this.document;
    if (doc) {
      let video = this.findVideoToPiP(doc);
      if (
        video &&
        PictureInPictureChild.videoIsPlaying(video) &&
        PictureInPictureChild.videoIsPiPEligible(video)
      ) {
        this.togglePictureInPicture({ video, reason: "AutoPip" }, false);
      }
    }
  }
}

/**
 * The PictureInPictureToggleChild is responsible for displaying the overlaid
 * Picture-in-Picture toggle over top of <video> elements that the mouse is
 * hovering.
 */
export class PictureInPictureToggleChild extends JSWindowActorChild {
  constructor() {
    super();
    // We need to maintain some state about various things related to the
    // Picture-in-Picture toggles - however, for now, the same
    // PictureInPictureToggleChild might be re-used for different documents.
    // We keep the state stashed inside of this WeakMap, keyed on the document
    // itself.
    this.weakDocStates = new WeakMap();
    this.toggleEnabled =
      Services.prefs.getBoolPref(TOGGLE_ENABLED_PREF) &&
      Services.prefs.getBoolPref(PIP_ENABLED_PREF);
    this.toggleTesting = Services.prefs.getBoolPref(TOGGLE_TESTING_PREF, false);

    // Bug 1570744 - JSWindowActorChild's cannot be used as nsIObserver's
    // directly, so we create a new function here instead to act as our
    // nsIObserver, which forwards the notification to the observe method.
    this.observerFunction = (subject, topic, data) => {
      this.observe(subject, topic, data);
    };
    Services.prefs.addObserver(TOGGLE_ENABLED_PREF, this.observerFunction);
    Services.prefs.addObserver(PIP_ENABLED_PREF, this.observerFunction);
    Services.prefs.addObserver(TOGGLE_FIRST_SEEN_PREF, this.observerFunction);
    Services.cpmm.sharedData.addEventListener("change", this);

    this.urlbarToggleEligiblePipVideos = new WeakSet();
    this.trackingVideos = new WeakSet();
  }

  receiveMessage(message) {
    switch (message.name) {
      case "PictureInPicture:UrlbarToggle": {
        this.urlbarToggle(message.data);
        break;
      }
    }
    return null;
  }

  didDestroy() {
    this.stopTrackingMouseOverVideos();
    Services.prefs.removeObserver(TOGGLE_ENABLED_PREF, this.observerFunction);
    Services.prefs.removeObserver(PIP_ENABLED_PREF, this.observerFunction);
    Services.prefs.removeObserver(
      TOGGLE_FIRST_SEEN_PREF,
      this.observerFunction
    );
    Services.cpmm.sharedData.removeEventListener("change", this);

    // remove the observer on the <video> element
    let state = this.docState;
    if (state?.intersectionObserver) {
      state.intersectionObserver.disconnect();
    }

    // ensure the sandbox created by the video is destroyed
    this.videoWrapper?.destroy();
    this.videoWrapper = null;

    for (let video of ChromeUtils.nondeterministicGetWeakSetKeys(
      this.urlbarToggleEligiblePipVideos
    )) {
      video.removeEventListener("emptied", this);
      video.removeEventListener("loadedmetadata", this);
      video.removeEventListener("durationchange", this);
    }

    for (let video of ChromeUtils.nondeterministicGetWeakSetKeys(
      this.trackingVideos
    )) {
      video.removeEventListener("emptied", this);
      video.removeEventListener("loadedmetadata", this);
      video.removeEventListener("durationchange", this);
    }

    // ensure we don't access the state
    this.isDestroyed = true;
  }

  observe(subject, topic, data) {
    if (topic != "nsPref:changed") {
      return;
    }

    switch (data) {
      case TOGGLE_FIRST_SEEN_PREF: {
        const firstSeenSeconds = Services.prefs.getIntPref(
          TOGGLE_FIRST_SEEN_PREF
        );
        if (!firstSeenSeconds || firstSeenSeconds < 0) {
          return;
        }
        this.changeToIconIfDurationEnd(firstSeenSeconds);
        break;
      }
    }
  }

  /**
   * Returns the state for the current document referred to via
   * this.document. If no such state exists, creates it, stores it
   * and returns it.
   */
  get docState() {
    if (this.isDestroyed || !this.document) {
      return false;
    }

    let state = this.weakDocStates.get(this.document);

    let visibilityThresholdPref = Services.prefs.getFloatPref(
      TOGGLE_VISIBILITY_THRESHOLD_PREF,
      "1.0"
    );

    if (!state) {
      state = {
        // A reference to the IntersectionObserver that's monitoring for videos
        // to become visible.
        intersectionObserver: null,
        // A WeakSet of videos that are supposedly visible, according to the
        // IntersectionObserver.
        weakVisibleVideos: new WeakSet(),
        // The number of videos that are supposedly visible, according to the
        // IntersectionObserver
        visibleVideosCount: 0,
        // The DeferredTask that we'll arm every time a mousemove event occurs
        // on a page where we have one or more visible videos.
        mousemoveDeferredTask: null,
        // A weak reference to the last video we displayed the toggle over.
        weakOverVideo: null,
        // True if the user is in the midst of clicking the toggle.
        isClickingToggle: false,
        // Set to the original target element on pointerdown if the user is clicking
        // the toggle - this way, we can determine if a "click" event will need to be
        // suppressed ("click" events don't fire if a "mouseup" occurs on a different
        // element from the "pointerdown" / "mousedown" event).
        clickedElement: null,
        // This is a DeferredTask to hide the toggle after a period of mouse
        // inactivity.
        hideToggleDeferredTask: null,
        // If we reach a point where we're tracking videos for mouse movements,
        // then this will be true. If there are no videos worth tracking, then
        // this is false.
        isTrackingVideos: false,
        togglePolicy: lazy.TOGGLE_POLICIES.DEFAULT,
        toggleVisibilityThreshold: visibilityThresholdPref,
        // The documentURI that has been checked with toggle policies and
        // visibility thresholds for this document. Note that the documentURI
        // might change for a document via the history API, so we remember
        // the last checked documentURI to determine if we need to check again.
        checkedPolicyDocumentURI: null,
        isUnloaded: false,
      };
      this.weakDocStates.set(this.document, state);
    }

    return state;
  }

  /**
   * Returns the video that the user was last hovering with the mouse if it
   * still exists.
   *
   * @return {Element} the <video> element that the user was last hovering,
   * or null if there was no such <video>, or the <video> no longer exists.
   */
  getWeakOverVideo() {
    let { weakOverVideo } = this.docState;
    if (weakOverVideo) {
      // Bug 800957 - Accessing weakrefs at the wrong time can cause us to
      // throw NS_ERROR_XPC_BAD_CONVERT_NATIVE
      try {
        return weakOverVideo.get();
      } catch (e) {
        return null;
      }
    }
    return null;
  }

  handleEvent(event) {
    if (!event.isTrusted) {
      // We don't care about synthesized events that might be coming from
      // content JS.
      return;
    }

    // Don't capture events from Picture-in-Picture content windows
    if (gPlayerContents.has(this.contentWindow)) {
      return;
    }

    switch (event.type) {
      case "touchstart": {
        // Even if this is a touch event, there may be subsequent click events.
        // Suppress those events after selecting the toggle to prevent playback changes
        // when opening the Picture-in-Picture window.
        if (this.docState.isClickingToggle) {
          event.stopImmediatePropagation();
          event.preventDefault();
        }
        break;
      }
      case "change": {
        const { changedKeys } = event;
        if (changedKeys.includes("PictureInPicture:SiteOverrides")) {
          // For now we only update our cache if the site overrides change.
          // the user will need to refresh the page for changes to apply.
          try {
            lazy.gSiteOverrides =
              PictureInPictureToggleChild.getSiteOverrides();
          } catch (e) {
            // Ignore resulting TypeError if gSiteOverrides is still unloaded
            if (!(e instanceof TypeError)) {
              throw e;
            }
          }
        }
        break;
      }
      case "UAWidgetSetupOrChange": {
        if (
          this.contentWindow.HTMLVideoElement.isInstance(event.target) &&
          event.target.ownerDocument == this.document
        ) {
          this.registerVideo(event.target);
        }
        break;
      }
      case "contextmenu": {
        if (this.toggleEnabled) {
          this.checkContextMenu(event);
        }
        break;
      }
      case "mouseout": {
        this.onMouseOut(event);
        break;
      }
      case "click":
        if (event.detail == 0) {
          let shadowRoot = event.originalTarget.containingShadowRoot;
          let toggle = this.getToggleElement(shadowRoot);
          if (event.originalTarget == toggle) {
            this.startPictureInPicture(event, shadowRoot.host, toggle);
            return;
          }
        }
      // fall through
      case "mousedown":
      case "pointerup":
      case "mouseup": {
        this.onMouseButtonEvent(event);
        break;
      }
      case "pointerdown": {
        this.onPointerDown(event);
        break;
      }
      case "mousemove": {
        this.onMouseMove(event);
        break;
      }
      case "pageshow": {
        this.onPageShow(event);
        break;
      }
      case "pagehide": {
        this.onPageHide(event);
        break;
      }
      case "visibilitychange": {
        this.onVisibilityChange(event);
        break;
      }
      case "durationchange":
      // Intentional fall-through
      case "emptied":
      // Intentional fall-through
      case "loadedmetadata": {
        this.updateUrlbarPipVideoEligibility(event.target);
        break;
      }
    }
  }

  /**
   * Adds a <video> to the IntersectionObserver so that we know when it becomes
   * visible.
   *
   * @param {Element} video The <video> element to register.
   */
  registerVideo(video) {
    let state = this.docState;
    if (!state.intersectionObserver) {
      let fn = this.onIntersection.bind(this);
      state.intersectionObserver = new this.contentWindow.IntersectionObserver(
        fn,
        {
          threshold: [0.0, 0.5],
        }
      );
    }

    state.intersectionObserver.observe(video);

    if (!lazy.PIP_URLBAR_BUTTON) {
      return;
    }

    video.addEventListener("emptied", this);
    video.addEventListener("loadedmetadata", this);
    video.addEventListener("durationchange", this);

    this.trackingVideos.add(video);

    this.updateUrlbarPipVideoEligibility(video);
  }

  updateUrlbarPipVideoEligibility(video) {
    // Bug 2041113: use a video wrapper to exclude YouTube preview videos
    // from being marked as urlbar PiP eligible videos. But only apply
    // the wrapper lazily if the hasUrlbarEligibilityOverride override exists.
    let override = getSiteOverrideForDocument(this.document.documentURI);
    if (!this.videoWrapper && override?.hasUrlbarEligibilityOverride) {
      this.videoWrapper = applyWrapper(this, video);
    }
    let isWrapperEligible =
      this.videoWrapper?.isUrlbarToggleEligible(video) ?? true;
    let isEligible =
      isWrapperEligible && PictureInPictureChild.videoIsPiPEligible(video);
    if (isEligible) {
      if (!this.urlbarToggleEligiblePipVideos.has(video)) {
        this.urlbarToggleEligiblePipVideos.add(video);

        let mutationObserver = new this.contentWindow.MutationObserver(
          mutationList => {
            this.handleEligiblePipVideoMutation(mutationList);
          }
        );
        mutationObserver.observe(video.parentElement, { childList: true });
      }
    } else if (this.urlbarToggleEligiblePipVideos.has(video)) {
      this.urlbarToggleEligiblePipVideos.delete(video);
    }

    let videos = ChromeUtils.nondeterministicGetWeakSetKeys(
      this.urlbarToggleEligiblePipVideos
    );

    this.sendAsyncMessage("PictureInPicture:UpdateEligiblePipVideoCount", {
      pipCount: videos.length,
      pipDisabledCount: videos.reduce(
        (accumulator, currentVal) =>
          accumulator + (currentVal.disablePictureInPicture ? 1 : 0),
        0
      ),
    });
  }

  handleEligiblePipVideoMutation(mutationList) {
    for (let mutationRecord of mutationList) {
      let video = mutationRecord.removedNodes[0];
      this.urlbarToggleEligiblePipVideos.delete(video);
    }

    let videos = ChromeUtils.nondeterministicGetWeakSetKeys(
      this.urlbarToggleEligiblePipVideos
    );

    this.sendAsyncMessage("PictureInPicture:UpdateEligiblePipVideoCount", {
      pipCount: videos.length,
      pipDisabledCount: videos.reduce(
        (accumulator, currentVal) =>
          accumulator + (currentVal.disablePictureInPicture ? 1 : 0),
        0
      ),
    });
  }

  urlbarToggle(eventExtraKeys) {
    let video = ChromeUtils.nondeterministicGetWeakSetKeys(
      this.urlbarToggleEligiblePipVideos
    )[0];
    if (video) {
      let pipEvent = new this.contentWindow.CustomEvent(
        "MozTogglePictureInPicture",
        {
          bubbles: true,
          detail: { reason: "UrlBar", eventExtraKeys },
        }
      );
      this.contentWindow.windowUtils.dispatchEventToChromeOnly(video, pipEvent);
    }
  }

  /**
   * Changes from the first-time toggle to the icon toggle if the Nimbus variable `displayDuration`'s
   * end date is reached when hovering over a video. The end date is calculated according to the timestamp
   * indicating when the PiP toggle was first seen.
   *
   * @param {number} firstSeenStartSeconds the timestamp in seconds indicating when the PiP toggle was first seen
   */
  changeToIconIfDurationEnd(firstSeenStartSeconds) {
    const { displayDuration } =
      lazy.NimbusFeatures.pictureinpicture.getAllVariables({
        defaultValues: {
          displayDuration: TOGGLE_FIRST_TIME_DURATION_DAYS,
        },
      });
    if (!displayDuration || displayDuration < 0) {
      return;
    }

    let daysInSeconds = displayDuration * 24 * 60 * 60;
    let firstSeenEndSeconds = daysInSeconds + firstSeenStartSeconds;
    let currentDateSeconds = Math.round(Date.now() / 1000);

    lazy.logConsole.debug(
      "Toggle duration experiment - first time toggle seen on:",
      new Date(firstSeenStartSeconds * 1000).toLocaleDateString()
    );
    lazy.logConsole.debug(
      "Toggle duration experiment - first time toggle will change on:",
      new Date(firstSeenEndSeconds * 1000).toLocaleDateString()
    );
    lazy.logConsole.debug(
      "Toggle duration experiment - current date:",
      new Date(currentDateSeconds * 1000).toLocaleDateString()
    );

    if (currentDateSeconds >= firstSeenEndSeconds) {
      this.sendAsyncMessage("PictureInPicture:SetHasUsed", {
        hasUsed: true,
      });
    }
  }

  /**
   * Called by the IntersectionObserver callback once a video becomes visible.
   * This adds some fine-grained checking to ensure that a sufficient amount of
   * the video is visible before we consider showing the toggles on it. For now,
   * that means that the entirety of the video must be in the viewport.
   *
   * @param {IntersectionEntry} intersectionEntry An IntersectionEntry passed to
   * the IntersectionObserver callback.
   * @return bool Whether or not we should start tracking mousemove events for
   * this registered video.
   */
  worthTracking(intersectionEntry) {
    return intersectionEntry.isIntersecting;
  }

  /**
   * Called by the IntersectionObserver once a video crosses one of the
   * thresholds dictated by the IntersectionObserver configuration.
   *
   * @param {Array<IntersectionEntry>} A collection of one or more
   * IntersectionEntry's for <video> elements that might have entered or exited
   * the viewport.
   */
  onIntersection(entries) {
    // The IntersectionObserver will also fire when a previously intersecting
    // element is removed from the DOM. We know, however, that the node is
    // still alive and referrable from the WeakSet because the
    // IntersectionObserverEntry holds a strong reference to the video.
    let state = this.docState;
    if (!state) {
      return;
    }
    let oldVisibleVideosCount = state.visibleVideosCount;
    for (let entry of entries) {
      let video = entry.target;
      if (this.worthTracking(entry)) {
        if (!state.weakVisibleVideos.has(video)) {
          state.weakVisibleVideos.add(video);
          state.visibleVideosCount++;
          if (this.toggleTesting) {
            gWeakIntersectingVideosForTesting.add(video);
          }
        }
      } else if (state.weakVisibleVideos.has(video)) {
        state.weakVisibleVideos.delete(video);
        state.visibleVideosCount--;
        if (this.toggleTesting) {
          gWeakIntersectingVideosForTesting.delete(video);
        }
      }
    }

    // For testing, especially in debug or asan builds, we might not
    // run this idle callback within an acceptable time. While we're
    // testing, we'll bypass the idle callback performance optimization
    // and run our callbacks as soon as possible during the next idle
    // period.
    if (!oldVisibleVideosCount && state.visibleVideosCount) {
      if (this.toggleTesting || !this.contentWindow) {
        this.beginTrackingMouseOverVideos();
      } else {
        this.contentWindow.requestIdleCallback(() => {
          this.beginTrackingMouseOverVideos();
        });
      }
    } else if (oldVisibleVideosCount && !state.visibleVideosCount) {
      if (this.toggleTesting || !this.contentWindow) {
        this.stopTrackingMouseOverVideos();
      } else {
        this.contentWindow.requestIdleCallback(() => {
          this.stopTrackingMouseOverVideos();
        });
      }
    }
  }

  addMouseButtonListeners() {
    // We want to try to cancel the mouse events from continuing
    // on into content if the user has clicked on the toggle, so
    // we don't use the mozSystemGroup here, and add the listener
    // to the parent target of the window, which in this case,
    // is the windowRoot. Since this event listener is attached to
    // part of the outer window, we need to also remove it in a
    // pagehide event listener in the event that the page unloads
    // before stopTrackingMouseOverVideos fires.
    this.contentWindow.windowRoot.addEventListener("pointerdown", this, {
      capture: true,
    });
    this.contentWindow.windowRoot.addEventListener("mousedown", this, {
      capture: true,
    });
    this.contentWindow.windowRoot.addEventListener("mouseup", this, {
      capture: true,
    });
    this.contentWindow.windowRoot.addEventListener("pointerup", this, {
      capture: true,
    });
    this.contentWindow.windowRoot.addEventListener("click", this, {
      capture: true,
    });
    this.contentWindow.windowRoot.addEventListener("mouseout", this, {
      capture: true,
    });
    this.contentWindow.windowRoot.addEventListener("touchstart", this, {
      capture: true,
    });
  }

  removeMouseButtonListeners() {
    // This can be null when closing the tab, but the event
    // listeners should be removed in that case already.
    if (!this.contentWindow || !this.contentWindow.windowRoot) {
      return;
    }

    this.contentWindow.windowRoot.removeEventListener("pointerdown", this, {
      capture: true,
    });
    this.contentWindow.windowRoot.removeEventListener("mousedown", this, {
      capture: true,
    });
    this.contentWindow.windowRoot.removeEventListener("mouseup", this, {
      capture: true,
    });
    this.contentWindow.windowRoot.removeEventListener("pointerup", this, {
      capture: true,
    });
    this.contentWindow.windowRoot.removeEventListener("click", this, {
      capture: true,
    });
    this.contentWindow.windowRoot.removeEventListener("mouseout", this, {
      capture: true,
    });
    this.contentWindow.windowRoot.removeEventListener("touchstart", this, {
      capture: true,
    });
  }

  /**
   * One of the challenges of displaying this toggle is that many sites put
   * things over top of <video> elements, like custom controls, or images, or
   * all manner of things that might intercept mouseevents that would normally
   * fire directly on the <video>. In order to properly detect when the mouse
   * is over top of one of the <video> elements in this situation, we currently
   * add a mousemove event handler to the entire document, and stash the most
   * recent mousemove that fires. At periodic intervals, that stashed mousemove
   * event is checked to see if it's hovering over one of our registered
   * <video> elements.
   *
   * This sort of thing will not be necessary once bug 1539652 is fixed.
   */
  beginTrackingMouseOverVideos() {
    let state = this.docState;
    if (!state.mousemoveDeferredTask) {
      state.mousemoveDeferredTask = new lazy.DeferredTask(() => {
        this.checkLastMouseMove();
      }, MOUSEMOVE_PROCESSING_DELAY_MS);
    }
    this.document.addEventListener("mousemove", this, {
      mozSystemGroup: true,
      capture: true,
    });
    this.contentWindow.addEventListener("pageshow", this, {
      mozSystemGroup: true,
    });
    this.contentWindow.addEventListener("pagehide", this, {
      mozSystemGroup: true,
    });
    lazy.logConsole.debug("Adding visibilitychange event handler");
    this.contentWindow.addEventListener("visibilitychange", this, {
      mozSystemGroup: true,
    });
    this.addMouseButtonListeners();
    state.isTrackingVideos = true;
  }

  /**
   * If we no longer have any interesting videos in the viewport, we deregister
   * the mousemove and click listeners, and also remove any toggles that might
   * be on the page still.
   */
  stopTrackingMouseOverVideos() {
    let state = this.docState;
    // We initialize `mousemoveDeferredTask` in `beginTrackingMouseOverVideos`.
    // If it doesn't exist, that can't have happened. Nothing else ever sets
    // this value (though we arm/disarm in various places). So we don't need
    // to do anything else here and can return early.
    if (!state.mousemoveDeferredTask) {
      return;
    }
    state.mousemoveDeferredTask.disarm();
    this.document.removeEventListener("mousemove", this, {
      mozSystemGroup: true,
      capture: true,
    });
    if (this.contentWindow) {
      this.contentWindow.removeEventListener("pageshow", this, {
        mozSystemGroup: true,
      });
      this.contentWindow.removeEventListener("pagehide", this, {
        mozSystemGroup: true,
      });
      lazy.logConsole.debug("Removing visibilitychange event handler");
      this.contentWindow.removeEventListener("visibilitychange", this, {
        mozSystemGroup: true,
      });
    }
    this.removeMouseButtonListeners();
    let oldOverVideo = this.getWeakOverVideo();
    if (oldOverVideo) {
      this.onMouseLeaveVideo(oldOverVideo);
    }
    state.isTrackingVideos = false;
  }

  /**
   * This pageshow event handler will get called if and when we complete a tab
   * tear out or in. If we happened to be tracking videos before the tear
   * occurred, we re-add the mouse event listeners so that they're attached to
   * the right WindowRoot.
   */
  onPageShow() {
    let state = this.docState;
    state.isUnloaded = false;
    if (state.isTrackingVideos) {
      this.addMouseButtonListeners();
    }
  }

  /**
   * This pagehide event handler will get called if and when we start a tab
   * tear out or in. If we happened to be tracking videos before the tear
   * occurred, we remove the mouse event listeners. We'll re-add them when the
   * pageshow event fires.
   */
  onPageHide() {
    let state = this.docState;
    state.isUnloaded = true;
    if (state.isTrackingVideos) {
      this.removeMouseButtonListeners();
    }
  }

  onVisibilityChange() {
    // Ignore if the document was unloaded or unloading
    let state = this.docState;
    if (state.isUnloaded) {
      return;
    }

    // We don't listen for visibilityState = "visible" because we set the pip
    // browser.docShellIsActive = true after auto pip-ing so we will never get
    // a visibilityState change when switching back to the tab. Instead,
    // PictureInPicture listens for TabSelect events and unpips the video if
    // the video was auto pip'd
    if (this.document.visibilityState == "hidden") {
      this.sendAsyncMessage("PictureInPicture:VideoTabHidden");
    }
  }

  /**
   * If we're tracking <video> elements, this pointerdown event handler is run anytime
   * a pointerdown occurs on the document. This function is responsible for checking
   * if the user clicked on the Picture-in-Picture toggle. It does this by first
   * checking if the video is visible beneath the point that was clicked. Then
   * it tests whether or not the pointerdown occurred within the rectangle of the
   * toggle. If so, the event's propagation is stopped, and Picture-in-Picture is
   * triggered.
   *
   * @param {Event} event The mousemove event.
   */
  onPointerDown(event) {
    // The toggle ignores non-primary mouse clicks.
    // Ignore ctrl+click for macOS too, since it acts like right click.
    if (
      event.button != 0 ||
      (AppConstants.platform == "macosx" && event.button == 0 && event.ctrlKey)
    ) {
      return;
    }

    let video = this.getWeakOverVideo();
    if (!video) {
      return;
    }

    let shadowRoot = video.openOrClosedShadowRoot;
    if (!shadowRoot) {
      return;
    }

    let state = this.docState;

    let overVideo = (() => {
      let { clientX, clientY } = event;
      let winUtils = this.contentWindow.windowUtils;
      // We use winUtils.nodesFromRect instead of document.elementsFromPoint,
      // since document.elementsFromPoint always flushes layout. The 1's in that
      // function call are for the size of the rect that we want, which is 1x1.
      //
      // We pass the aOnlyVisible boolean argument to check that the video isn't
      // occluded by anything visible at the point of mousedown. If it is, we'll
      // ignore the mousedown.
      let elements = winUtils.nodesFromRect(
        clientX,
        clientY,
        1,
        1,
        1,
        1,
        true,
        false,
        /* aOnlyVisible = */ true,
        state.toggleVisibilityThreshold
      );

      for (let element of elements) {
        if (element == video || element.containingShadowRoot == shadowRoot) {
          return true;
        }
      }

      return false;
    })();

    if (!overVideo) {
      return;
    }

    let toggle = this.getToggleElement(shadowRoot);
    if (this.isMouseOverToggle(toggle, event)) {
      state.isClickingToggle = true;
      state.clickedElement = Cu.getWeakReference(event.originalTarget);
      event.stopImmediatePropagation();

      this.startPictureInPicture(event, video, toggle);
    }
  }

  startPictureInPicture(event, video) {
    let pipEvent = new this.contentWindow.CustomEvent(
      "MozTogglePictureInPicture",
      {
        bubbles: true,
        detail: { reason: "Toggle" },
      }
    );
    this.contentWindow.windowUtils.dispatchEventToChromeOnly(video, pipEvent);

    // Since we've initiated Picture-in-Picture, we can go ahead and
    // hide the toggle now.
    this.onMouseLeaveVideo(video);
  }

  /**
   * Called for mousedown, pointerup, mouseup and click events. If we
   * detected that the user is clicking on the Picture-in-Picture toggle,
   * these events are cancelled in the capture-phase before they reach
   * content. The state for suppressing these events is cleared on the
   * click event (unless the mouseup occurs on a different element from
   * the mousedown, in which case, the state is cleared on mouseup).
   *
   * @param {Event} event A mousedown, pointerup, mouseup or click event.
   */
  onMouseButtonEvent(event) {
    // The toggle ignores non-primary mouse clicks.
    // Ignore ctrl+click for macOS too, since it acts like right click.
    if (
      event.button != 0 ||
      (AppConstants.platform == "macosx" && event.button == 0 && event.ctrlKey)
    ) {
      return;
    }

    let state = this.docState;
    if (state.isClickingToggle) {
      event.stopImmediatePropagation();

      // If this is a mouseup event, check to see if we have a record of what
      // the original target was on pointerdown. If so, and if it doesn't match
      // the mouseup original target, that means we won't get a click event, and
      // we can clear the "clicking the toggle" state right away.
      //
      // Otherwise, we wait for the click event to do that.
      let isMouseUpOnOtherElement =
        event.type == "mouseup" &&
        (!state.clickedElement ||
          state.clickedElement.get() != event.originalTarget);

      if (
        isMouseUpOnOtherElement ||
        event.type == "click" ||
        // pointerup event still triggers after a touchstart event. We just need to detect
        // the pointer type and determine if we got to this part of the code through a touch event.
        event.pointerType == "touch"
      ) {
        // The click is complete, so now we reset the state so that
        // we stop suppressing these events.
        state.isClickingToggle = false;
        state.clickedElement = null;
      }
    }
  }

  /**
   * Called on mouseout events to determine whether or not the mouse has
   * exited the window.
   *
   * @param {Event} event The mouseout event.
   */
  onMouseOut(event) {
    if (!event.relatedTarget) {
      // For mouseout events, if there's no relatedTarget (which normally
      // maps to the element that the mouse entered into) then this means that
      // we left the window.
      let video = this.getWeakOverVideo();
      if (!video) {
        return;
      }

      this.onMouseLeaveVideo(video);
    }
  }

  /**
   * Called for each mousemove event when we're tracking those events to
   * determine if the cursor is hovering over a <video>.
   *
   * @param {Event} event The mousemove event.
   */
  onMouseMove(event) {
    let state = this.docState;

    if (state.hideToggleDeferredTask) {
      state.hideToggleDeferredTask.disarm();
      state.hideToggleDeferredTask.arm();
    }

    state.lastMouseMoveEvent = event;
    state.mousemoveDeferredTask.arm();
  }

  /**
   * Called by the DeferredTask after MOUSEMOVE_PROCESSING_DELAY_MS
   * milliseconds. Checked to see if that mousemove happens to be overtop of
   * any interesting <video> elements that we want to display the toggle
   * on. If so, puts the toggle on that video.
   */
  checkLastMouseMove() {
    let state = this.docState;
    let event = state.lastMouseMoveEvent;
    let { clientX, clientY } = event;
    lazy.logConsole.debug("Visible videos count:", state.visibleVideosCount);
    lazy.logConsole.debug("Tracking videos:", state.isTrackingVideos);
    let winUtils = this.contentWindow.windowUtils;
    // We use winUtils.nodesFromRect instead of document.elementsFromPoint,
    // since document.elementsFromPoint always flushes layout. The 1's in that
    // function call are for the size of the rect that we want, which is 1x1.
    let elements = winUtils.nodesFromRect(
      clientX,
      clientY,
      1,
      1,
      1,
      1,
      true,
      false,
      /* aOnlyVisible = */ true
    );

    for (let element of elements) {
      lazy.logConsole.debug("Element id under cursor:", element.id);
      lazy.logConsole.debug(
        "Node name of an element under cursor:",
        element.nodeName
      );
      lazy.logConsole.debug(
        "Supported <video> element:",
        state.weakVisibleVideos.has(element)
      );
      lazy.logConsole.debug(
        "PiP window is open:",
        element.isCloningElementVisually
      );

      // Check for hovering over the video controls or so too, not only
      // directly over the video.
      for (let el = element; el; el = el.containingShadowRoot?.host) {
        if (state.weakVisibleVideos.has(el) && !el.isCloningElementVisually) {
          lazy.logConsole.debug("Found supported element");
          this.onMouseOverVideo(el, event);
          return;
        }
      }
    }

    let oldOverVideo = this.getWeakOverVideo();
    if (oldOverVideo) {
      this.onMouseLeaveVideo(oldOverVideo);
    }
  }

  /**
   * Called once it has been determined that the mouse is overtop of a video
   * that is in the viewport.
   *
   * @param {Element} video The video the mouse is over.
   */
  onMouseOverVideo(video, event) {
    let oldOverVideo = this.getWeakOverVideo();
    let shadowRoot = video.openOrClosedShadowRoot;

    if (shadowRoot.firstChild && video != oldOverVideo) {
      // TODO: Maybe this should move to videocontrols.js somehow.
      shadowRoot.firstChild.toggleAttribute(
        "flipped",
        video.getTransformToViewport().a == -1
      );
    }

    // It seems from automated testing that if it's still very early on in the
    // lifecycle of a <video> element, it might not yet have a shadowRoot,
    // in which case, we can bail out here early.
    if (!shadowRoot) {
      if (oldOverVideo) {
        // We also clear the hover state on the old video we were hovering,
        // if there was one.
        this.onMouseLeaveVideo(oldOverVideo);
      }

      return;
    }

    let state = this.docState;
    let toggle = this.getToggleElement(shadowRoot);
    let controlsOverlay = shadowRoot.querySelector(".controlsOverlay");

    if (state.checkedPolicyDocumentURI != this.document.documentURI) {
      state.togglePolicy = lazy.TOGGLE_POLICIES.DEFAULT;
      // We cache the matchers process-wide. We'll skip this while running tests to make that
      // easier.
      let siteOverrides = this.toggleTesting
        ? PictureInPictureToggleChild.getSiteOverrides()
        : lazy.gSiteOverrides;

      let visibilityThresholdPref = Services.prefs.getFloatPref(
        TOGGLE_VISIBILITY_THRESHOLD_PREF,
        "1.0"
      );

      if (!this.videoWrapper) {
        this.videoWrapper = applyWrapper(this, video);
      }

      // Do we have any toggle overrides? If so, try to apply them.
      for (let [override, { policy, visibilityThreshold }] of siteOverrides) {
        if (
          (policy || visibilityThreshold) &&
          override.matches(this.document.documentURI)
        ) {
          state.togglePolicy = this.videoWrapper?.shouldHideToggle(video)
            ? lazy.TOGGLE_POLICIES.HIDDEN
            : policy || lazy.TOGGLE_POLICIES.DEFAULT;
          state.toggleVisibilityThreshold =
            visibilityThreshold || visibilityThresholdPref;
          break;
        }
      }

      state.checkedPolicyDocumentURI = this.document.documentURI;
    }

    // The built-in <video> controls are along the bottom, which would overlap the
    // toggle if the override is set to BOTTOM, so we ignore overrides that set
    // a policy of BOTTOM for <video> elements with controls.
    if (
      state.togglePolicy != lazy.TOGGLE_POLICIES.DEFAULT &&
      !(state.togglePolicy == lazy.TOGGLE_POLICIES.BOTTOM && video.controls)
    ) {
      toggle.setAttribute(
        "policy",
        lazy.TOGGLE_POLICY_STRINGS[state.togglePolicy]
      );
    } else {
      toggle.removeAttribute("policy");
    }

    // nimbusExperimentVariables will be defaultValues when the experiment is disabled
    const nimbusExperimentVariables =
      lazy.NimbusFeatures.pictureinpicture.getAllVariables({
        defaultValues: {
          oldToggle: true,
          title: null,
          message: false,
          showIconOnly: false,
          displayDuration: TOGGLE_FIRST_TIME_DURATION_DAYS,
        },
      });

    /**
     * If a Nimbus variable exists for the first-time PiP toggle design,
     * override the old design via a classname "experiment".
     */
    if (!nimbusExperimentVariables.oldToggle) {
      let controlsContainer = shadowRoot.querySelector(".controlsContainer");
      let pipWrapper = shadowRoot.querySelector(".pip-wrapper");

      controlsContainer.classList.add("experiment");
      pipWrapper.classList.add("experiment");
    } else {
      let controlsContainer = shadowRoot.querySelector(".controlsContainer");
      let pipWrapper = shadowRoot.querySelector(".pip-wrapper");

      controlsContainer.classList.remove("experiment");
      pipWrapper.classList.remove("experiment");
    }

    if (nimbusExperimentVariables.title) {
      let pipExplainer = shadowRoot.querySelector(".pip-explainer");
      let pipLabel = shadowRoot.querySelector(".pip-label");

      if (pipExplainer && nimbusExperimentVariables.message) {
        pipExplainer.innerText = nimbusExperimentVariables.message;
      }
      pipLabel.innerText = nimbusExperimentVariables.title;
    } else if (nimbusExperimentVariables.showIconOnly) {
      // We only want to show the PiP icon in this experiment scenario
      let pipExpanded = shadowRoot.querySelector(".pip-expanded");
      pipExpanded.style.display = "none";
      let pipSmall = shadowRoot.querySelector(".pip-small");
      pipSmall.style.opacity = "1";

      let pipIcon = shadowRoot.querySelectorAll(".pip-icon")[1];
      pipIcon.style.display = "block";
    }

    controlsOverlay.removeAttribute("hidetoggle");

    // The hideToggleDeferredTask we create here is for automatically hiding
    // the toggle after a period of no mousemove activity for
    // TOGGLE_HIDING_TIMEOUT_MS. If the mouse moves, then the DeferredTask
    // timer is reset.
    //
    // We disable the toggle hiding timeout during testing to reduce
    // non-determinism from timers when testing the toggle.
    if (!state.hideToggleDeferredTask && !this.toggleTesting) {
      state.hideToggleDeferredTask = new lazy.DeferredTask(() => {
        controlsOverlay.setAttribute("hidetoggle", true);
      }, TOGGLE_HIDING_TIMEOUT_MS);
    }

    if (oldOverVideo) {
      if (oldOverVideo == video) {
        // If we're still hovering the old video, we might have entered or
        // exited the toggle region.
        this.checkHoverToggle(toggle, event);
        return;
      }

      // We had an old video that we were hovering, and we're not hovering
      // it anymore. Let's leave it.
      this.onMouseLeaveVideo(oldOverVideo);
    }

    state.weakOverVideo = Cu.getWeakReference(video);
    controlsOverlay.classList.add("hovering");

    if (
      state.togglePolicy != lazy.TOGGLE_POLICIES.HIDDEN &&
      !toggle.hasAttribute("hidden")
    ) {
      const hasUsedPiP = Services.prefs.getBoolPref(TOGGLE_HAS_USED_PREF);
      Glean.pictureinpicture.sawToggleToggle.record({
        firstTime: !hasUsedPiP,
      });
      // only record if this is the first time seeing the toggle
      if (!hasUsedPiP) {
        lazy.NimbusFeatures.pictureinpicture.recordExposureEvent();

        const firstSeenSeconds = Services.prefs.getIntPref(
          TOGGLE_FIRST_SEEN_PREF,
          0
        );

        if (!firstSeenSeconds || firstSeenSeconds < 0) {
          let firstTimePiPStartDate = Math.round(Date.now() / 1000);
          this.sendAsyncMessage("PictureInPicture:SetFirstSeen", {
            dateSeconds: firstTimePiPStartDate,
          });
        } else if (nimbusExperimentVariables.displayDuration) {
          this.changeToIconIfDurationEnd(firstSeenSeconds);
        }
      }
    }

    // Now that we're hovering the video, we'll check to see if we're
    // hovering the toggle too.
    this.checkHoverToggle(toggle, event);
  }

  /**
   * Checks if a mouse event is happening over a toggle element. If it is,
   * sets the hovering class on it. Otherwise, it clears the hovering
   * class.
   *
   * @param {Element} toggle The Picture-in-Picture toggle to check.
   * @param {MouseEvent} event A MouseEvent to test.
   */
  checkHoverToggle(toggle, event) {
    toggle.classList.toggle("hovering", this.isMouseOverToggle(toggle, event));
  }

  /**
   * Called once it has been determined that the mouse is no longer overlapping
   * a video that we'd previously called onMouseOverVideo with.
   *
   * It is possible that the <video> at this point is no longer connected to
   * the DOM (e.g. if it is being reparented by a framework). Thus, care should
   * be taken when accessing its shadow root.
   *
   * @param {Element} video The video that the mouse left.
   */
  onMouseLeaveVideo(video) {
    let state = this.docState;
    let shadowRoot = video.openOrClosedShadowRoot;

    if (shadowRoot) {
      let controlsOverlay = shadowRoot.querySelector(".controlsOverlay");
      let toggle = this.getToggleElement(shadowRoot);
      controlsOverlay?.classList.remove("hovering");
      toggle?.classList.remove("hovering");
    }

    state.weakOverVideo = null;

    if (!this.toggleTesting) {
      state.hideToggleDeferredTask.disarm();
      state.mousemoveDeferredTask.disarm();
    }

    state.hideToggleDeferredTask = null;
  }

  /**
   * Given a reference to a Picture-in-Picture toggle element, determines
   * if a MouseEvent event is occurring within its bounds.
   *
   * @param {Element} toggle The Picture-in-Picture toggle.
   * @param {MouseEvent} event A MouseEvent to test.
   *
   * @return {boolean}
   */
  isMouseOverToggle(toggle, event) {
    let toggleRect =
      toggle.documentGlobal.windowUtils.getBoundsWithoutFlushing(toggle);

    // The way the toggle is currently implemented with
    // absolute positioning, the root toggle element bounds don't actually
    // contain all of the toggle child element bounds. Until we find a way to
    // sort that out, we workaround the issue by having each clickable child
    // elements of the toggle have a clicklable class, and then compute the
    // smallest rect that contains all of their bounding rects and use that
    // as the hitbox.
    toggleRect = lazy.Rect.fromRect(toggleRect);
    let clickableChildren = toggle.querySelectorAll(".clickable");
    for (let child of clickableChildren) {
      let childRect = lazy.Rect.fromRect(
        child.documentGlobal.windowUtils.getBoundsWithoutFlushing(child)
      );
      toggleRect.expandToContain(childRect);
    }

    // If the toggle has no dimensions, we're definitely not over it.
    if (!toggleRect.width || !toggleRect.height) {
      return false;
    }

    let { clientX, clientY } = event;

    return (
      clientX >= toggleRect.left &&
      clientX <= toggleRect.right &&
      clientY >= toggleRect.top &&
      clientY <= toggleRect.bottom
    );
  }

  /**
   * Checks a contextmenu event to see if the mouse is currently over the
   * Picture-in-Picture toggle. If so, sends a message to the parent process
   * to open up the Picture-in-Picture toggle context menu.
   *
   * @param {MouseEvent} event A contextmenu event.
   */
  checkContextMenu(event) {
    let video = this.getWeakOverVideo();
    if (!video) {
      return;
    }

    let shadowRoot = video.openOrClosedShadowRoot;
    if (!shadowRoot) {
      return;
    }

    let toggle = this.getToggleElement(shadowRoot);
    if (this.isMouseOverToggle(toggle, event)) {
      let devicePixelRatio = toggle.documentGlobal.devicePixelRatio;
      this.sendAsyncMessage("PictureInPicture:OpenToggleContextMenu", {
        screenXDevPx: event.screenX * devicePixelRatio,
        screenYDevPx: event.screenY * devicePixelRatio,
        inputSource: event.inputSource,
      });
      event.stopImmediatePropagation();
      event.preventDefault();
    }
  }

  /**
   * Returns the appropriate root element for the Picture-in-Picture toggle,
   * depending on whether or not we're using the experimental toggle preference.
   *
   * @param {Element} shadowRoot The shadowRoot of the video element.
   * @returns {Element} The toggle element.
   */
  getToggleElement(shadowRoot) {
    return shadowRoot.getElementById("pictureInPictureToggle");
  }

  /**
   * This is a test-only function that returns true if a video is being tracked
   * for mouseover events after having intersected the viewport.
   */
  static isTracking(video) {
    return gWeakIntersectingVideosForTesting.has(video);
  }

  /**
   * Gets any Picture-in-Picture site-specific overrides stored in the
   * sharedData struct, and returns them as an Array of two-element Arrays,
   * where the first element is a MatchPattern and the second element is an
   * object of the form { policy, disabledKeyboardControls } (where each property
   * may be missing or undefined).
   *
   * @returns {Array<Array<2>>} Array of 2-element Arrays where the first element
   * is a MatchPattern and the second element is an object with optional policy
   * and/or disabledKeyboardControls properties.
   */
  static getSiteOverrides() {
    let result = [];
    let patterns = Services.cpmm.sharedData.get(
      "PictureInPicture:SiteOverrides"
    );
    for (let pattern in patterns) {
      let matcher = new MatchPattern(pattern);
      result.push([matcher, patterns[pattern]]);
    }
    return result;
  }
}

export class PictureInPictureChild extends JSWindowActorChild {
  #subtitlesEnabled = false;
  // A weak reference to this PiP window's video element
  weakVideo = null;

  // A weak reference to this PiP window's content window
  weakPlayerContent = null;

  // A reference to current WebVTT track currently displayed on the content window
  _currentWebVTTTrack = null;

  // The TextTrackList currently observed for track lifecycle changes.
  #textTrackList = null;

  // Avoid registering duplicate wrapper observers on later setupTextTracks calls.
  #captionChangeListenerRegistered = false;

  // A weak reference to the PictureInPictureWindow (the one exposed to web content)
  #weakPictureInPictureWindow;

  observerFunction = null;

  observe(subject, topic, data) {
    if (topic != "nsPref:changed") {
      return;
    }

    switch (data) {
      case "media.videocontrols.picture-in-picture.display-text-tracks.enabled": {
        const originatingVideo = this.getWeakVideo();
        let isTextTrackPrefEnabled = Services.prefs.getBoolPref(
          "media.videocontrols.picture-in-picture.display-text-tracks.enabled"
        );

        // Enable or disable text track support
        if (isTextTrackPrefEnabled) {
          this.setupTextTracks(originatingVideo);
        } else {
          this.removeTextTracks(originatingVideo);
        }
        break;
      }
    }
  }

  /**
   * Creates a link element with a reference to the css stylesheet needed
   * for text tracks responsive styling.
   *
   * @returns {Element} the link element containing text tracks stylesheet.
   */
  createTextTracksStyleSheet() {
    let headStyleElement = this.document.createElement("link");
    headStyleElement.setAttribute("rel", "stylesheet");
    headStyleElement.setAttribute(
      "href",
      "chrome://global/skin/pictureinpicture/texttracks.css"
    );
    headStyleElement.setAttribute("type", "text/css");
    return headStyleElement;
  }

  /**
   * Listens for changes to the originating video's TextTrackList, including
   * tracks added or removed after Picture-in-Picture opens, and displays any
   * active WebVTT cues. A site wrapper caption observer may coexist with these
   * listeners, but active WebVTT cues take priority over wrapper captions in
   * PictureInPictureChildVideoWrapper.updatePiPTextTracks().
   *
   * @param originatingVideo {Element|null}
   *  The <video> being displayed in Picture-in-Picture mode, or null if that <video> no longer exists.
   */
  setupTextTracks(originatingVideo) {
    const textTracks = originatingVideo.textTracks;

    if (this.#textTrackList !== textTracks) {
      this.removeTextTrackListeners(this.#textTrackList);
      this.#textTrackList = textTracks;
      this.addTextTrackListeners(this.#textTrackList);
    }

    this.syncWebVTTTextTrack(originatingVideo);

    if (!this._currentWebVTTTrack && !this.#captionChangeListenerRegistered) {
      this.setUpCaptionChangeListener(originatingVideo);
    }
  }

  addTextTrackListeners(textTrackList) {
    textTrackList?.addEventListener("change", this);
    textTrackList?.addEventListener("addtrack", this);
    textTrackList?.addEventListener("removetrack", this);
  }

  removeTextTrackListeners(textTrackList) {
    textTrackList?.removeEventListener("change", this);
    textTrackList?.removeEventListener("addtrack", this);
    textTrackList?.removeEventListener("removetrack", this);
  }

  /**
   * Updates the active WebVTT track for the current TextTrackList state.
   *
   * @param originatingVideo {Element}
   *  The <video> being displayed in Picture-in-Picture mode.
   */
  syncWebVTTTextTrack(originatingVideo) {
    const previousWebVTTTrack = this._currentWebVTTTrack;
    this._currentWebVTTTrack?.removeEventListener(
      "cuechange",
      this.onCueChange
    );

    this.setActiveTextTrack(originatingVideo.textTracks);

    if (this._currentWebVTTTrack) {
      this._currentWebVTTTrack.addEventListener("cuechange", this.onCueChange);
      this.updateWebVTTTextTracksDisplay(this._currentWebVTTTrack.activeCues);
      return;
    }

    if (previousWebVTTTrack) {
      this.updateWebVTTTextTracksDisplay(null);
      if (!this.#captionChangeListenerRegistered) {
        this.setUpCaptionChangeListener(originatingVideo);
      }
    }
  }

  /**
   * Toggle the visibility of the subtitles in the PiP window
   */
  toggleTextTracks() {
    let textTracks = this.document.getElementById("texttracks");
    textTracks.style.display =
      textTracks.style.display === "none" ? "" : "none";
  }

  /**
   * Removes existing text tracks on the Picture in Picture window.
   *
   * If the originating video supports WebVTT, clear references to active
   * tracks and cues. No longer listen for any track or cue changes.
   *
   * @param originatingVideo {Element|null}
   *  The <video> being displayed in Picture-in-Picture mode, or null if that <video> no longer exists.
   */
  removeTextTracks(originatingVideo) {
    this.removeCaptionChangeListener(originatingVideo);

    // No longer listen for changes to tracks and active cues
    this.removeTextTrackListeners(this.#textTrackList);
    this.#textTrackList = null;
    this._currentWebVTTTrack?.removeEventListener(
      "cuechange",
      this.onCueChange
    );
    this._currentWebVTTTrack = null;
    this.updateWebVTTTextTracksDisplay(null);
  }

  /**
   * Moves the text tracks container position above the pip window's video controls
   * if their positions visually overlap. Since pip controls are within the parent
   * process, we determine if pip video controls and text tracks visually overlap by
   * comparing their relative positions with DOMRect.
   *
   * If overlap is found, set attribute "overlap-video-controls" to move text tracks
   * and define a new relative bottom position according to pip window size and the
   * position of video controls.
   *
   *  @param {object} data args needed to determine if text tracks must be moved
   */
  moveTextTracks(data) {
    const {
      isFullscreen,
      isVideoControlsShowing,
      playerBottomControlsDOMRect,
      isScrubberShowing,
    } = data;
    let textTracks = this.document.getElementById("texttracks");
    const originatingWindow = this.getWeakVideo().documentGlobal;
    const isReducedMotionEnabled = originatingWindow.matchMedia(
      "(prefers-reduced-motion: reduce)"
    ).matches;
    const textTracksFontScale = this.document
      .querySelector(":root")
      .style.getPropertyValue("--font-scale");

    if (isFullscreen || isReducedMotionEnabled) {
      textTracks.removeAttribute("overlap-video-controls");
      return;
    }

    if (isVideoControlsShowing) {
      let playerVideoRect = textTracks.parentElement.getBoundingClientRect();
      let isOverlap =
        playerVideoRect.bottom - textTracksFontScale * playerVideoRect.height >
        playerBottomControlsDOMRect.top;

      if (isOverlap) {
        const root = this.document.querySelector(":root");
        if (isScrubberShowing) {
          root.style.setProperty("--player-controls-scrubber-height", "30px");
        } else {
          root.style.setProperty("--player-controls-scrubber-height", "0px");
        }
        textTracks.setAttribute("overlap-video-controls", true);
      } else {
        textTracks.removeAttribute("overlap-video-controls");
      }
    } else {
      textTracks.removeAttribute("overlap-video-controls");
    }
  }

  /**
   * Updates the text content for the container that holds and displays text tracks
   * on the pip window.
   *
   * @param textTrackCues {TextTrackCueList|null}
   *  Collection of TextTrackCue objects containing text displayed, or null if there is no cue to display.
   */
  updateWebVTTTextTracksDisplay(textTrackCues) {
    let pipWindowTracksContainer = this.document.getElementById("texttracks");
    let playerVideo = this.document.getElementById("playervideo");
    let playerVideoWindow = playerVideo.documentGlobal;

    // To prevent overlap with previous cues, clear all text from the pip window
    pipWindowTracksContainer.replaceChildren();

    if (!textTrackCues) {
      return;
    }

    if (!this.isSubtitlesEnabled) {
      this.isSubtitlesEnabled = true;
      this.sendAsyncMessage("PictureInPicture:EnableSubtitlesButton");
    }

    let allCuesArray = [...textTrackCues];
    // Re-order cues
    this.getOrderedWebVTTCues(allCuesArray);
    // Parse through WebVTT cue using vtt.js to ensure
    // semantic markup like <b> and <i> tags are rendered.
    allCuesArray.forEach(cue => {
      let text = cue.text;
      // Trim extra newlines and whitespaces
      const re = /(\s*\n{2,}\s*)/g;
      text = text.trim();
      text = text.replace(re, "\n");
      let cueTextNode = WebVTT.convertCueToDOMTree(playerVideoWindow, text);
      let cueDiv = this.document.createElement("div");
      cueDiv.appendChild(cueTextNode);
      pipWindowTracksContainer.appendChild(cueDiv);
    });
  }

  /**
   * Re-orders list of multiple active cues to ensure cues are rendered in the correct order.
   * How cues are ordered depends on the VTTCue.line value of the cue.
   *
   * If line is string "auto", we want to reverse the order of cues.
   * Cues are read from top to bottom in a vtt file, but are inserted into a video from bottom to top.
   * Ensure this order is followed.
   *
   * If line is an integer or percentage, we want to order cues according to numeric value.
   * Assumptions:
   *  1) all active cues are numeric
   *  2) all active cues are in range 0..100
   *  3) all actives cue are horizontal (no VTTCue.vertical)
   *  4) all active cues with VTTCue.line integer have VTTCue.snapToLines = true
   *  5) all active cues with VTTCue.line percentage have VTTCue.snapToLines = false
   *
   * vtt.sys.mjs currently sets snapToLines to false if line is a percentage value, but
   * cues are still ordered by line. In most cases, snapToLines is set to true by default,
   * unless intentionally overridden.
   *
   * @param allCuesArray {Array<VTTCue>} array of active cues
   */
  getOrderedWebVTTCues(allCuesArray) {
    if (!allCuesArray || allCuesArray.length <= 1) {
      return;
    }

    let allCuesHaveNumericLines = allCuesArray.find(cue => cue.line !== "auto");

    if (allCuesHaveNumericLines) {
      allCuesArray.sort((cue1, cue2) => cue1.line - cue2.line);
    } else if (allCuesArray.length >= 2) {
      allCuesArray.reverse();
    }
  }

  getPictureInPictureWindow() {
    if (this.#weakPictureInPictureWindow) {
      try {
        return this.#weakPictureInPictureWindow.get();
      } catch (e) {}
    }
    return null;
  }

  /**
   * Returns a reference to the PiP's <video> element being displayed in Picture-in-Picture
   * mode.
   *
   * @return {Element} The <video> being displayed in Picture-in-Picture mode, or null
   * if that <video> no longer exists.
   */
  getWeakVideo() {
    if (this.weakVideo) {
      // Bug 800957 - Accessing weakrefs at the wrong time can cause us to
      // throw NS_ERROR_XPC_BAD_CONVERT_NATIVE
      try {
        return this.weakVideo.get();
      } catch (e) {
        return null;
      }
    }
    return null;
  }

  /**
   * Returns a reference to the inner window of the about:blank document that is
   * cloning the originating <video> in the always-on-top player <xul:browser>.
   *
   * @return {Window} The inner window of the about:blank player <xul:browser>, or
   * null if that window has been closed.
   */
  getWeakPlayerContent() {
    if (this.weakPlayerContent) {
      // Bug 800957 - Accessing weakrefs at the wrong time can cause us to
      // throw NS_ERROR_XPC_BAD_CONVERT_NATIVE
      try {
        return this.weakPlayerContent.get();
      } catch (e) {
        return null;
      }
    }
    return null;
  }

  /**
   * Returns true if the passed video happens to be the one that this
   * content process is running in a Picture-in-Picture window.
   *
   * @param {Element} video The <video> element to check.
   *
   * @return {boolean}
   */
  inPictureInPicture(video) {
    return this.getWeakVideo() === video;
  }

  static videoIsPlaying(video) {
    return !!(!video.paused && !video.ended && video.readyState > 2);
  }

  static videoIsMuted(video) {
    return this.videoWrapper.isMuted(video);
  }

  /**
   * Returns true if a video passes heuristics indicating that it'd be a good
   * candidate for the Picture-in-Picture feature.
   *
   * @param {Element} video
   *   The <video> element to evaluate.
   * @returns {boolean}
   */
  static videoIsPiPEligible(video) {
    if (lazy.PIP_TOGGLE_ALWAYS_SHOW) {
      return true;
    }

    if (isNaN(video.duration) || video.duration < lazy.MIN_VIDEO_LENGTH) {
      return false;
    }

    const MIN_VIDEO_DIMENSION = 140; // pixels
    if (
      video.clientWidth < MIN_VIDEO_DIMENSION ||
      video.clientHeight < MIN_VIDEO_DIMENSION
    ) {
      return false;
    }

    if (!video.mozHasAudio) {
      return false;
    }

    if (!video.checkVisibility()) {
      return false;
    }

    return true;
  }

  handleEvent(event) {
    switch (event.type) {
      case "MozStopPictureInPicture": {
        const video = this.getWeakVideo();
        if (event.isTrusted && event.target === video) {
          const reason = event.detail?.reason || "VideoElRemove";
          if (reason === "VideoElRemove") {
            this.closePictureInPictureIfDisconnected({ reason, video });
          } else {
            this.closePictureInPicture({ reason });
          }
        }
        break;
      }
      case "pagehide": {
        // The originating video's content document has unloaded,
        // so close Picture-in-Picture.
        this.closePictureInPicture({ reason: "Pagehide" });
        break;
      }
      case "MozDOMFullscreen:Request": {
        this.closePictureInPicture({ reason: "Fullscreen" });
        break;
      }
      case "playing":
      // Intentional fall-through
      case "play": {
        this.sendAsyncMessage("PictureInPicture:Playing");
        break;
      }
      case "pause": {
        this.sendAsyncMessage("PictureInPicture:Paused");
        break;
      }
      case "volumechange": {
        let video = this.getWeakVideo();

        // Just double-checking that we received the event for the right
        // video element.
        if (video !== event.target) {
          lazy.logConsole.error(
            "PictureInPictureChild received volumechange for " +
              "the wrong video!"
          );
          return;
        }

        if (this.constructor.videoIsMuted(video)) {
          this.sendAsyncMessage("PictureInPicture:Muting");
        } else {
          this.sendAsyncMessage("PictureInPicture:Unmuting");
        }
        this.sendAsyncMessage("PictureInPicture:VolumeChange", {
          volume: this.videoWrapper.getVolume(video),
        });
        break;
      }
      case "ratechange": {
        let video = this.getWeakVideo();
        if (video === event.target) {
          this.sendAsyncMessage("PictureInPicture:PlaybackRateChange", {
            playbackRate: this.videoWrapper.getPlaybackRate(video),
          });
        }
        break;
      }
      case "resize": {
        let video = event.target;
        if (this.inPictureInPicture(video)) {
          this.sendAsyncMessage("PictureInPicture:Resize", {
            videoHeight: video.videoHeight,
            videoWidth: video.videoWidth,
          });
        }
        this.setupTextTracks(video);
        break;
      }
      case "emptied": {
        this.isSubtitlesEnabled = false;
        if (this.emptiedTimeout) {
          clearTimeout(this.emptiedTimeout);
          this.emptiedTimeout = null;
        }
        if (lazy.PIP_AUTO_CLOSE) {
          let video = this.getWeakVideo();
          // We may want to keep the pip window open if the video
          // is still in DOM. But if video src is no longer defined,
          // close Picture-in-Picture.
          this.emptiedTimeout = setTimeout(() => {
            if (!video || !video.src) {
              this.closePictureInPicture({ reason: "VideoElEmptied" });
            }
          }, lazy.EMPTIED_TIMEOUT_MS);
        }
        break;
      }
      case "change":
      case "addtrack":
      case "removetrack": {
        const originatingVideo = this.getWeakVideo();
        if (originatingVideo && event.target === this.#textTrackList) {
          this.syncWebVTTTextTrack(originatingVideo);
        }
        break;
      }
      case "timeupdate":
      case "durationchange": {
        let video = this.getWeakVideo();
        let currentTime = this.videoWrapper.getCurrentTime(video);
        let duration = this.videoWrapper.getDuration(video);
        let scrubberPosition = currentTime === 0 ? 0 : currentTime / duration;
        let timestamp = this.videoWrapper.formatTimestamp(
          currentTime,
          duration
        );
        // There's no point in sending this message unless we have a
        // reasonable timestamp.
        if (timestamp !== undefined && lazy.IMPROVED_CONTROLS_ENABLED_PREF) {
          this.sendAsyncMessage(
            "PictureInPicture:SetTimestampAndScrubberPosition",
            {
              scrubberPosition,
              timestamp,
            }
          );
        }
        break;
      }
    }
  }

  /**
   * Tells the parent to close a pre-existing Picture-in-Picture
   * window.
   *
   * @returns {Promise<void>}
   *   Resolves once the pre-existing Picture-in-Picture window has unloaded.
   */
  async closePictureInPicture({ reason }) {
    let video = this.getWeakVideo();
    if (video) {
      gOriginatingVideoMap.delete(video);
      this.untrackOriginatingVideo(video);
    }
    const query = this.sendQuery("PictureInPicture:Close", {
      reason,
    });

    let playerContent = this.getWeakPlayerContent();
    if (playerContent) {
      if (!playerContent.closed) {
        await new Promise(resolve => {
          playerContent.addEventListener("unload", resolve, {
            once: true,
          });
        });
      }
      // Nothing should be holding a reference to the Picture-in-Picture
      // player window content at this point, but just in case, we'll
      // clear the weak reference directly so nothing else can get a hold
      // of it from this angle.
      this.weakPlayerContent = null;
    }
    await query;
  }

  /**
   * Closes the Picture-in-Picture window when the source video is removed from
   * the DOM.
   *
   * This function yields execution briefly to detect if the video element was
   * merely being moved (re-parented) rather than destroyed. If the video
   * is found to be re-connected, the visual stream is re-cloned and the
   * window remains open.
   *
   * @param {object} options
   * @param {string} options.reason The reason code for the potential closure.
   * @param {Element} options.video The source video element to check.
   */
  closePictureInPictureIfDisconnected({ reason, video }) {
    this.contentWindow.requestAnimationFrame(() =>
      Services.tm.dispatchToMainThread(() => {
        if (video?.isConnected) {
          const playerVideo = this.document.getElementById("playervideo");
          if (playerVideo) {
            video.cloneElementVisually(playerVideo);
            this.stylePlayerVideo(video, playerVideo);
            return;
          }
        }
        this.closePictureInPicture({ reason });
      })
    );
  }

  async receiveMessage(message) {
    switch (message.name) {
      case "PictureInPicture:SetupPlayer": {
        const { videoRef, isPipApiRequest, initDimension } = message.data;
        return await this.setupPlayer(videoRef, isPipApiRequest, initDimension);
      }
      case "PictureInPicture:Play": {
        this.play();
        break;
      }
      case "PictureInPicture:Pause": {
        if (message.data && message.data.reason == "pip-closed") {
          let video = this.getWeakVideo();

          // Currently in Firefox srcObjects are MediaStreams. However, by spec a srcObject
          // can be either a MediaStream, MediaSource or Blob. In case of future changes
          // we do not want to pause MediaStream srcObjects and we want to maintain current
          // behavior for non-MediaStream srcObjects.
          if (video && MediaStream.isInstance(video.srcObject)) {
            break;
          }
        }
        this.pause();
        break;
      }
      case "PictureInPicture:Mute": {
        this.mute();
        break;
      }
      case "PictureInPicture:Unmute": {
        this.unmute();
        break;
      }
      case "PictureInPicture:SeekForward":
      case "PictureInPicture:SeekBackward": {
        let selectedTime;
        let video = this.getWeakVideo();
        let currentTime = this.videoWrapper.getCurrentTime(video);
        if (message.name == "PictureInPicture:SeekBackward") {
          selectedTime = currentTime - SEEK_TIME_SECS;
          selectedTime = selectedTime >= 0 ? selectedTime : 0;
        } else {
          const maxtime = this.videoWrapper.getDuration(video);
          selectedTime = currentTime + SEEK_TIME_SECS;
          selectedTime = selectedTime <= maxtime ? selectedTime : maxtime;
        }
        this.videoWrapper.setCurrentTime(video, selectedTime);
        break;
      }
      case "PictureInPicture:KeyDown": {
        this.keyDown(message.data);
        break;
      }
      case "PictureInPicture:EnterFullscreen":
      case "PictureInPicture:ExitFullscreen": {
        let textTracks = this.document.getElementById("texttracks");
        if (textTracks) {
          this.moveTextTracks(message.data);
        }
        break;
      }
      case "PictureInPicture:ShowVideoControls":
      case "PictureInPicture:HideVideoControls": {
        let textTracks = this.document.getElementById("texttracks");
        if (textTracks) {
          this.moveTextTracks(message.data);
        }
        break;
      }
      case "PictureInPicture:ToggleTextTracks": {
        this.toggleTextTracks();
        break;
      }
      case "PictureInPicture:ChangeFontSizeTextTracks": {
        this.setTextTrackFontSize();
        break;
      }
      case "PictureInPicture:SetVideoTime": {
        const { scrubberPosition, wasPlaying } = message.data;
        this.setVideoTime(scrubberPosition, wasPlaying);
        break;
      }
      case "PictureInPicture:SetVolume": {
        const { volume } = message.data;
        let video = this.getWeakVideo();
        this.videoWrapper.setVolume(video, volume);
        break;
      }
      case "PictureInPicture:SetPlaybackRate": {
        const { playbackRate } = message.data;
        let video = this.getWeakVideo();
        this.videoWrapper.setPlaybackRate(video, playbackRate);
        break;
      }
    }
    return undefined;
  }

  /**
   * Set the current time of the video based of the position of the scrubber
   *
   * @param {number} scrubberPosition A number between 0 and 1 representing the position of the scrubber
   */
  setVideoTime(scrubberPosition, wasPlaying) {
    const video = this.getWeakVideo();
    let duration = this.videoWrapper.getDuration(video);
    let currentTime = scrubberPosition * duration;
    this.videoWrapper.setCurrentTime(video, currentTime, wasPlaying);
  }

  /**
   * @returns {boolean} true if a textTrack with mode "hidden" should be treated as "showing"
   */
  shouldShowHiddenTextTracks() {
    const video = this.getWeakVideo();
    if (!video) {
      return false;
    }
    const { documentURI } = video.ownerDocument;
    if (!documentURI) {
      return false;
    }
    for (let [override, { showHiddenTextTracks }] of lazy.gSiteOverrides) {
      if (override.matches(documentURI) && showHiddenTextTracks) {
        return true;
      }
    }
    return false;
  }

  /**
   * Updates this._currentWebVTTTrack if an active track is found
   * for the originating video.
   *
   * @param {TextTrackList} textTrackList list of text tracks
   */
  setActiveTextTrack(textTrackList) {
    this._currentWebVTTTrack = null;

    for (let i = 0; i < textTrackList.length; i++) {
      let track = textTrackList[i];
      let isCCText = track.kind === "subtitles" || track.kind === "captions";
      let shouldShowTrack =
        track.mode === "showing" ||
        (track.mode === "hidden" && this.shouldShowHiddenTextTracks());
      if (isCCText && shouldShowTrack && track.cues) {
        this._currentWebVTTTrack = track;
        break;
      }
    }
  }

  /**
   * Set the font size on the PiP window using the current font size value from
   * the "media.videocontrols.picture-in-picture.display-text-tracks.size" pref
   */
  setTextTrackFontSize() {
    const fontSize = Services.prefs.getStringPref(
      TEXT_TRACK_FONT_SIZE,
      "medium"
    );
    const root = this.document.querySelector(":root");
    if (fontSize === "small") {
      root.style.setProperty("--font-scale", "0.03");
    } else if (fontSize === "large") {
      root.style.setProperty("--font-scale", "0.09");
    } else {
      root.style.setProperty("--font-scale", "0.06");
    }
  }

  /**
   * Keeps an eye on the originating video's document. If it ever
   * goes away, this will cause the Picture-in-Picture window for any
   * of its content to go away as well.
   */
  trackOriginatingVideo(originatingVideo) {
    this.observerFunction = (subject, topic, data) => {
      this.observe(subject, topic, data);
    };
    Services.prefs.addObserver(
      "media.videocontrols.picture-in-picture.display-text-tracks.enabled",
      this.observerFunction
    );

    let originatingWindow = originatingVideo.documentGlobal;
    if (originatingWindow) {
      originatingWindow.addEventListener("pagehide", this);
      originatingVideo.addEventListener("play", this);
      originatingVideo.addEventListener("playing", this);
      originatingVideo.addEventListener("pause", this);
      originatingVideo.addEventListener("volumechange", this);
      originatingVideo.addEventListener("ratechange", this);
      originatingVideo.addEventListener("resize", this);
      originatingVideo.addEventListener("emptied", this);
      originatingVideo.addEventListener("timeupdate", this);

      if (lazy.DISPLAY_TEXT_TRACKS_PREF) {
        this.setupTextTracks(originatingVideo);
      }

      let chromeEventHandler = originatingWindow.docShell.chromeEventHandler;
      chromeEventHandler.addEventListener(
        "MozDOMFullscreen:Request",
        this,
        true
      );
      chromeEventHandler.addEventListener(
        "MozStopPictureInPicture",
        this,
        true
      );
    }
  }

  setUpCaptionChangeListener(originatingVideo) {
    if (this.videoWrapper) {
      this.videoWrapper.setCaptionContainerObserver(originatingVideo, this);
      this.#captionChangeListenerRegistered = true;
    }
  }

  removeCaptionChangeListener(originatingVideo) {
    if (this.videoWrapper) {
      this.videoWrapper.removeCaptionContainerObserver(originatingVideo, this);
      this.#captionChangeListenerRegistered = false;
    }
  }

  /**
   * Stops tracking the originating video's document. This should
   * happen once the Picture-in-Picture window goes away (or is about
   * to go away), and we no longer care about hearing when the originating
   * window's document unloads.
   */
  untrackOriginatingVideo(originatingVideo) {
    Services.prefs.removeObserver(
      "media.videocontrols.picture-in-picture.display-text-tracks.enabled",
      this.observerFunction
    );

    let originatingWindow = originatingVideo.documentGlobal;
    if (originatingWindow) {
      originatingWindow.removeEventListener("pagehide", this);
      originatingVideo.removeEventListener("play", this);
      originatingVideo.removeEventListener("playing", this);
      originatingVideo.removeEventListener("pause", this);
      originatingVideo.removeEventListener("volumechange", this);
      originatingVideo.removeEventListener("ratechange", this);
      originatingVideo.removeEventListener("resize", this);
      originatingVideo.removeEventListener("emptied", this);
      originatingVideo.removeEventListener("timeupdate", this);

      if (lazy.DISPLAY_TEXT_TRACKS_PREF) {
        this.removeTextTracks(originatingVideo);
      }

      let chromeEventHandler = originatingWindow.docShell?.chromeEventHandler;
      if (chromeEventHandler) {
        chromeEventHandler.removeEventListener(
          "MozDOMFullscreen:Request",
          this,
          true
        );
        chromeEventHandler.removeEventListener(
          "MozStopPictureInPicture",
          this,
          true
        );
      }
    }
  }

  /**
   * Runs in an instance of PictureInPictureChild for the
   * player window's content, and not the originating video
   * content. Sets up the player so that it clones the originating
   * video. If anything goes wrong during set up, a message is
   * sent to the parent to close the Picture-in-Picture window.
   *
   * @param videoRef {ContentDOMReference}
   *    A reference to the video element that a Picture-in-Picture window
   *    is being created for
   * @param isPipApiRequest {boolean}
   *    True when the PiP window was requested via HTMLVideoElement.requestPictureInPicture().
   *    In that case the PictureInPictureWindow instance is recovered from
   *    gVideoToPipWindow, which the launcher actor populated in the same
   *    content process.
   * @param initWindowDimension {{width: number, height:number}}
   *    The initial window dimensions. Needed to set PIP Window (for PIP API) dimensions
   *    so that web content has correct dimensions before first resize.
   *
   * @returns {Promise<void>}
   *   Resolves once the player window has been set up properly, or a pre-existing
   *   Picture-in-Picture window has gone away due to an unexpected error.
   */
  async setupPlayer(videoRef, isPipApiRequest, initWindowDimension) {
    const video = await lazy.ContentDOMReference.resolve(videoRef);

    this.weakVideo = Cu.getWeakReference(video);
    let originatingVideo = this.getWeakVideo();
    if (!originatingVideo) {
      // If the video element has gone away before we've had a chance to set up
      // Picture-in-Picture for it, tell the parent to close the Picture-in-Picture
      // window.
      await this.closePictureInPicture({ reason: "SetupFailure" });
      return;
    }
    // Now we can lookup this actor, using `video` element, which lives in the web content window
    gOriginatingVideoMap.set(video, this);

    this.videoWrapper = applyWrapper(this, originatingVideo);

    if (isPipApiRequest) {
      const pipInstance = gVideoToPipWindow.get(originatingVideo);
      gVideoToPipWindow.delete(originatingVideo);
      if (pipInstance) {
        const { width, height } = initWindowDimension;
        this.setPictureInPictureWindowInstance(pipInstance);
        // We notify of dimension change, otherwise it'll report 0,0 as window size until first resize.
        // Note, that we haven't given this instance to web content yet,
        // so no listener will have been added, so no script should run here.
        pipInstance.notifyDimensionsChanged(width, height);
      }
    }

    // On Android the embedder has already loaded a document in the player session, so there is
    // nothing to wait for. Reloading would swap the WindowGlobal and destroy this actor before
    // it could reply to PictureInPicture:SetupPlayer.
    if (AppConstants.platform != "android") {
      let loadPromise = new Promise(resolve => {
        this.contentWindow.addEventListener("load", resolve, {
          once: true,
          mozSystemGroup: true,
          capture: true,
        });
      });
      this.contentWindow.location.reload();
      await loadPromise;
    }

    // We're committed to adding the video to this window now. Ensure we track
    // the content window before we do so, so that the toggle actor can
    // distinguish this new video we're creating from web-controlled ones.
    this.weakPlayerContent = Cu.getWeakReference(this.contentWindow);
    gPlayerContents.add(this.contentWindow);

    let doc = this.document;
    let playerVideo = doc.createElement("video");
    playerVideo.id = "playervideo";
    let textTracks = doc.createElement("div");

    doc.body.style.overflow = "hidden";
    doc.body.style.margin = "0";

    // Force the player video to assume maximum height and width of the
    // containing window
    playerVideo.style.height = "100vh";
    playerVideo.style.width = "100vw";
    playerVideo.style.backgroundColor = "#000";

    // Load text tracks container in the content process so that
    // we can load text tracks without having to constantly
    // access the parent process.
    textTracks.id = "texttracks";
    // When starting pip, player controls are expected to appear.
    textTracks.setAttribute("overlap-video-controls", true);
    doc.body.appendChild(playerVideo);
    doc.body.appendChild(textTracks);
    // Load text tracks stylesheet
    let textTracksStyleSheet = this.createTextTracksStyleSheet();
    doc.head.appendChild(textTracksStyleSheet);

    this.setTextTrackFontSize();

    originatingVideo.cloneElementVisually(playerVideo);
    this.stylePlayerVideo(originatingVideo, playerVideo);

    this.onCueChange = this.onCueChange.bind(this);
    this.trackOriginatingVideo(originatingVideo);

    // A request to open PIP implies that the user intends to be interacting
    // with the page, even if they open PIP by some means outside of the page
    // itself (e.g., the keyboard shortcut or the page action button). So we
    // manually record that the document has been activated via user gesture
    // to make sure the video can be played regardless of autoplay permissions.
    // N.B: If the PIP window was created using the PIP API, it consumed a user
    // activation it's unclear that we should be granting it another one.
    if (!isPipApiRequest) {
      originatingVideo.ownerDocument.notifyUserGestureActivation();
    }

    this.contentWindow.addEventListener(
      "unload",
      () => {
        let v = this.getWeakVideo();
        if (v) {
          this.untrackOriginatingVideo(v);
          v.stopCloningElementVisually();
        }
        this.weakVideo = null;
      },
      { once: true }
    );
  }

  #onResizeNotifyPictureInPictureWindowInstance() {
    const pipWindow = this.getPictureInPictureWindow();
    if (pipWindow) {
      pipWindow.notifyDimensionsChanged(
        this.contentWindow.innerWidth,
        this.contentWindow.innerHeight
      );
    }
  }

  setPictureInPictureWindowInstance(pipWindowInstance) {
    this.#weakPictureInPictureWindow = pipWindowInstance
      ? Cu.getWeakReference(pipWindowInstance)
      : null;

    if (pipWindowInstance) {
      this.contentWindow.addEventListener("resize", () =>
        this.#onResizeNotifyPictureInPictureWindowInstance()
      );
    }
  }

  /**
   * Styles the player video in the Picture-in-Picture window based on the
   * styles of the originating video (i.e. mirroring).
   *
   * @param {Element} originatingVideo
   *   The source video element.
   * @param {Element} playerVideo
   *   The <video> element inside the PiP window.
   */
  stylePlayerVideo(originatingVideo, playerVideo) {
    const shadowRoot = originatingVideo.openOrClosedShadowRoot;
    if (originatingVideo.getTransformToViewport().a == -1) {
      shadowRoot.firstChild.setAttribute("flipped", true);
      playerVideo.style.transform = "scaleX(-1)";
    }
  }

  play() {
    let video = this.getWeakVideo();
    if (video && this.videoWrapper) {
      this.videoWrapper.play(video);
    }
  }

  pause() {
    let video = this.getWeakVideo();
    if (video && this.videoWrapper) {
      this.videoWrapper.pause(video);
    }
  }

  mute() {
    let video = this.getWeakVideo();
    if (video && this.videoWrapper) {
      this.videoWrapper.setMuted(video, true);
    }
  }

  unmute() {
    let video = this.getWeakVideo();
    if (video && this.videoWrapper) {
      this.videoWrapper.setMuted(video, false);
    }
  }

  onCueChange() {
    if (!lazy.DISPLAY_TEXT_TRACKS_PREF) {
      this.updateWebVTTTextTracksDisplay(null);
    } else {
      const cues = this._currentWebVTTTrack.activeCues;
      this.updateWebVTTTextTracksDisplay(cues);
    }
  }

  /**
   * This checks if a given keybinding has been disabled for the specific site
   * currently being viewed.
   */
  isKeyDisabled(key) {
    const video = this.getWeakVideo();
    if (!video) {
      return false;
    }
    const { documentURI } = video.ownerDocument;
    if (!documentURI) {
      return true;
    }
    for (let [override, { disabledKeyboardControls }] of lazy.gSiteOverrides) {
      if (
        disabledKeyboardControls !== undefined &&
        override.matches(documentURI)
      ) {
        if (disabledKeyboardControls === lazy.KEYBOARD_CONTROLS.ALL) {
          return true;
        }
        return !!(disabledKeyboardControls & key);
      }
    }
    return false;
  }

  /**
   * This reuses the keyHandler logic in the VideoControlsWidget
   * https://searchfox.org/mozilla-central/rev/cfd1cc461f1efe0d66c2fdc17c024a203d5a2fd8/toolkit/content/widgets/videocontrols.js#1687-1810.
   * There are future plans to eventually combine the two implementations.
   */
  /* eslint-disable complexity */
  keyDown({ altKey, shiftKey, metaKey, ctrlKey, keyCode }) {
    let video = this.getWeakVideo();
    if (!video) {
      return;
    }

    var keystroke = "";
    if (altKey) {
      keystroke += "alt-";
    }
    if (shiftKey) {
      keystroke += "shift-";
    }
    if (this.contentWindow.navigator.platform.startsWith("Mac")) {
      if (metaKey) {
        keystroke += "accel-";
      }
      if (ctrlKey) {
        keystroke += "control-";
      }
    } else {
      if (metaKey) {
        keystroke += "meta-";
      }
      if (ctrlKey) {
        keystroke += "accel-";
      }
    }

    switch (keyCode) {
      case this.contentWindow.KeyEvent.DOM_VK_UP:
        keystroke += "upArrow";
        break;
      case this.contentWindow.KeyEvent.DOM_VK_DOWN:
        keystroke += "downArrow";
        break;
      case this.contentWindow.KeyEvent.DOM_VK_LEFT:
        keystroke += "leftArrow";
        break;
      case this.contentWindow.KeyEvent.DOM_VK_RIGHT:
        keystroke += "rightArrow";
        break;
      case this.contentWindow.KeyEvent.DOM_VK_HOME:
        keystroke += "home";
        break;
      case this.contentWindow.KeyEvent.DOM_VK_END:
        keystroke += "end";
        break;
      case this.contentWindow.KeyEvent.DOM_VK_SPACE:
        keystroke += "space";
        break;
      case this.contentWindow.KeyEvent.DOM_VK_W:
        keystroke += "w";
        break;
    }

    const isVideoStreaming = this.videoWrapper.isLive(video);
    var oldval, newval;

    try {
      switch (keystroke) {
        case "space" /* Toggle Play / Pause */:
          if (this.isKeyDisabled(lazy.KEYBOARD_CONTROLS.PLAY_PAUSE)) {
            return;
          }

          if (
            this.videoWrapper.getPaused(video) ||
            this.videoWrapper.getEnded(video)
          ) {
            this.videoWrapper.play(video);
          } else {
            this.videoWrapper.pause(video);
          }

          break;
        case "accel-w" /* Close video */:
          if (this.isKeyDisabled(lazy.KEYBOARD_CONTROLS.CLOSE)) {
            return;
          }
          this.pause();
          this.closePictureInPicture({ reason: "ClosePlayerShortcut" });
          break;
        case "downArrow" /* Volume decrease */:
          if (
            this.isKeyDisabled(lazy.KEYBOARD_CONTROLS.VOLUME) ||
            this.videoWrapper.isMuted(video)
          ) {
            return;
          }
          oldval = this.videoWrapper.getVolume(video);
          newval = oldval < 0.1 ? 0 : oldval - 0.1;
          this.videoWrapper.setVolume(video, newval);
          this.videoWrapper.setMuted(video, newval === 0);
          break;
        case "upArrow" /* Volume increase */:
          if (this.isKeyDisabled(lazy.KEYBOARD_CONTROLS.VOLUME)) {
            return;
          }
          oldval = this.videoWrapper.getVolume(video);
          this.videoWrapper.setVolume(video, oldval > 0.9 ? 1 : oldval + 0.1);
          this.videoWrapper.setMuted(video, false);
          break;
        case "accel-downArrow" /* Mute */:
          if (this.isKeyDisabled(lazy.KEYBOARD_CONTROLS.MUTE_UNMUTE)) {
            return;
          }
          this.videoWrapper.setMuted(video, true);
          break;
        case "accel-upArrow" /* Unmute */:
          if (this.isKeyDisabled(lazy.KEYBOARD_CONTROLS.MUTE_UNMUTE)) {
            return;
          }
          this.videoWrapper.setMuted(video, false);
          break;
        case "leftArrow": /* Seek back 5 seconds */
        case "accel-leftArrow" /* Seek back 10% */:
          if (
            this.isKeyDisabled(lazy.KEYBOARD_CONTROLS.SEEK) ||
            (isVideoStreaming &&
              this.isKeyDisabled(lazy.KEYBOARD_CONTROLS.LIVE_SEEK))
          ) {
            return;
          }

          oldval = this.videoWrapper.getCurrentTime(video);
          if (keystroke == "leftArrow") {
            newval = oldval - SEEK_TIME_SECS;
          } else {
            newval = oldval - this.videoWrapper.getDuration(video) / 10;
          }
          this.videoWrapper.setCurrentTime(video, newval >= 0 ? newval : 0);
          break;
        case "rightArrow": /* Seek forward 5 seconds */
        case "accel-rightArrow" /* Seek forward 10% */: {
          if (
            this.isKeyDisabled(lazy.KEYBOARD_CONTROLS.SEEK) ||
            (isVideoStreaming &&
              this.isKeyDisabled(lazy.KEYBOARD_CONTROLS.LIVE_SEEK))
          ) {
            return;
          }

          oldval = this.videoWrapper.getCurrentTime(video);
          var maxtime = this.videoWrapper.getDuration(video);
          if (keystroke == "rightArrow") {
            newval = oldval + SEEK_TIME_SECS;
          } else {
            newval = oldval + maxtime / 10;
          }
          let selectedTime = newval <= maxtime ? newval : maxtime;
          this.videoWrapper.setCurrentTime(video, selectedTime);
          break;
        }
        case "home" /* Seek to beginning */:
          if (this.isKeyDisabled(lazy.KEYBOARD_CONTROLS.SEEK)) {
            return;
          }
          if (!isVideoStreaming) {
            this.videoWrapper.setCurrentTime(video, 0);
          }
          break;
        case "end" /* Seek to end */: {
          if (this.isKeyDisabled(lazy.KEYBOARD_CONTROLS.SEEK)) {
            return;
          }

          let duration = this.videoWrapper.getDuration(video);
          if (
            !isVideoStreaming &&
            this.videoWrapper.getCurrentTime(video) != duration
          ) {
            this.videoWrapper.setCurrentTime(video, duration);
          }
          break;
        }
        default:
      }
    } catch (e) {
      /* ignore any exception from setting video.currentTime */
    }
  }

  get isSubtitlesEnabled() {
    return this.#subtitlesEnabled;
  }

  get hasActiveWebVTTTrack() {
    return !!this._currentWebVTTTrack;
  }

  set isSubtitlesEnabled(val) {
    if (val) {
      Glean.pictureinpicture.subtitlesShownSubtitles.record({
        webVTTSubtitles: !!this.getWeakVideo().textTracks?.length,
      });
    } else {
      this.sendAsyncMessage("PictureInPicture:DisableSubtitlesButton");
    }
    this.#subtitlesEnabled = val;
  }
}

/**
 * The PictureInPictureChildVideoWrapper class handles providing a path to a script that
 * defines a "site wrapper" for the original <video> (or other controls API provided
 * by the site) to command it.
 *
 * This "site wrapper" provided to PictureInPictureChildVideoWrapper is a script file that
 * defines a class called `PictureInPictureVideoWrapper` and exports it. These scripts can
 * be found under "browser/extensions/pictureinpicture/video-wrappers" as part of the
 * Picture-In-Picture addon.
 *
 * Site wrappers need to adhere to a specific interface to work properly with
 * PictureInPictureChildVideoWrapper:
 *
 * - The "site wrapper" script must export a class called "PictureInPictureVideoWrapper"
 * - Method names on a site wrapper class should match its caller's name
 *   (i.e: PictureInPictureChildVideoWrapper.play will only call `play` on a site-wrapper, if available)
 */
class PictureInPictureChildVideoWrapper {
  #sandbox;
  #siteWrapper;
  #PictureInPictureChild;

  /**
   * Create a wrapper for the original <video>
   *
   * @param {string | null} videoWrapperScriptPath
   *        Path to a wrapper script from the Picture-in-Picture addon. If a wrapper isn't
   *        provided to the class, then we fallback on a default implementation for
   *        commanding the original <video>.
   * @param {HTMLVideoElement} video
   *        The original <video> we want to create a wrapper class for.
   * @param {object} pipChild
   *        Reference to PictureInPictureChild class calling this function.
   */
  constructor(videoWrapperScriptPath, video, pipChild) {
    this.#sandbox = videoWrapperScriptPath
      ? this.#createSandbox(videoWrapperScriptPath, video)
      : null;
    this.#PictureInPictureChild = pipChild;
  }

  /**
   * Handles calling methods defined on the site wrapper class to perform video
   * controls operations on the source video. If the method doesn't exist,
   * or if an error is thrown while calling it, use a fallback implementation.
   *
   * @param {string} methodInfo.name
   *        The method name to call.
   * @param {Array} methodInfo.args
   *        Arguments to pass to the site wrapper method being called.
   * @param {Function} methodInfo.fallback
   *        A fallback function that's invoked when a method doesn't exist on the site
   *        wrapper class or an error is thrown while calling a method
   * @param {Function} methodInfo.validateReturnVal
   *        Validates whether or not the return value of the wrapper method is correct.
   *        If this isn't provided or if it evaluates false for a return value, then
   *        return null.
   *
   * @returns The expected output of the wrapper function.
   */
  #callWrapperMethod({ name, args = [], fallback = () => {}, validateRetVal }) {
    try {
      const wrappedMethod = this.#siteWrapper?.[name];
      if (typeof wrappedMethod === "function") {
        let retVal = wrappedMethod.call(this.#siteWrapper, ...args);

        if (!validateRetVal) {
          lazy.logConsole.error(
            `No return value validator was provided for method ${name}(). Returning null.`
          );
          return null;
        }

        if (!validateRetVal(retVal)) {
          lazy.logConsole.error(
            `Calling method ${name}() returned an unexpected value: ${retVal}. Returning null.`
          );
          return null;
        }

        return retVal;
      }
    } catch (e) {
      lazy.logConsole.error(
        `There was an error while calling ${name}(): `,
        e.message
      );
    }

    return fallback();
  }

  /**
   * Creates a sandbox with Xray vision to execute content code in an unprivileged
   * context. This way, privileged code (PictureInPictureChild) can call into the
   * sandbox to perform video controls operations on the originating video
   * (content code) and still be protected from direct access by it.
   *
   * @param {string} videoWrapperScriptPath
   *        Path to a wrapper script from the Picture-in-Picture addon.
   * @param {HTMLVideoElement} video
   *        The source video element whose window to create a sandbox for.
   */
  #createSandbox(videoWrapperScriptPath, video) {
    const addonPolicy = WebExtensionPolicy.getByID(
      "pictureinpicture@mozilla.org"
    );
    let wrapperScriptUrl = addonPolicy.getURL(videoWrapperScriptPath);
    let originatingWin = video.documentGlobal;
    let originatingDoc = video.ownerDocument;

    let sandbox = Cu.Sandbox([originatingDoc.nodePrincipal], {
      sandboxName: "Picture-in-Picture video wrapper sandbox",
      sandboxPrototype: originatingWin,
      sameZoneAs: originatingWin,
      wantXrays: false,
    });

    try {
      Services.scriptloader.loadSubScriptWithOptions(wrapperScriptUrl, {
        target: sandbox,
        allowUnsafeURL: true,
      });
    } catch (e) {
      Cu.nukeSandbox(sandbox);
      lazy.logConsole.error(
        "Error loading wrapper script for Picture-in-Picture",
        e
      );
      return null;
    }

    // The prototype of the wrapper class instantiated from the sandbox with Xray
    // vision is `Object` and not actually `PictureInPictureVideoWrapper`. But we
    // need to be able to access methods defined on this class to perform site-specific
    // video control operations otherwise we fallback to a default implementation.
    // Because of this, we need to "waive Xray vision" by adding `.wrappedObject` to the
    // end.
    this.#siteWrapper = new sandbox.PictureInPictureVideoWrapper(
      video
    ).wrappedJSObject;

    return sandbox;
  }

  #isBoolean(val) {
    return typeof val === "boolean";
  }

  #isNumber(val) {
    return typeof val === "number";
  }

  /**
   * Destroys the sandbox for the site wrapper class
   */
  destroy() {
    if (this.#sandbox) {
      Cu.nukeSandbox(this.#sandbox);
    }
  }

  /**
   * Function to display the captions on the PiP window
   *
   * @param {string} text - Raw text to be displayed
   * @param {string} type - Optional type of text track. If "vtt" or "html", the text
   * will be parsed and displayed as a WebVTT cue. If not provided, the text will
   * be displayed as plain text.
   */
  updatePiPTextTracks(text, type) {
    if (this.#PictureInPictureChild.hasActiveWebVTTTrack) {
      return;
    }

    if (!this.#PictureInPictureChild.isSubtitlesEnabled && text) {
      this.#PictureInPictureChild.isSubtitlesEnabled = true;
      this.#PictureInPictureChild.sendAsyncMessage(
        "PictureInPicture:EnableSubtitlesButton"
      );
    }
    let pipWindowTracksContainer =
      this.#PictureInPictureChild.document.getElementById("texttracks");

    /* Clear any existing children */
    pipWindowTracksContainer.innerHTML = "";

    switch (type) {
      case "vtt":
      case "html": {
        const node = WebVTT.convertCueToDOMTree(
          this.#PictureInPictureChild,
          text
        );
        pipWindowTracksContainer.appendChild(node);
        break;
      }
      default:
        pipWindowTracksContainer.textContent = text;
        break;
    }
  }

  /* Video methods to be used for video controls from the PiP window. */

  /**
   * OVERRIDABLE - calls the play() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to handle video
   * behaviour when a video is played.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   */
  play(video) {
    return this.#callWrapperMethod({
      name: "play",
      args: [video],
      fallback: () => video.play(),
      validateRetVal: retVal => retVal == null,
    });
  }

  /**
   * OVERRIDABLE - calls the pause() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to handle video
   * behaviour when a video is paused.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   */
  pause(video) {
    return this.#callWrapperMethod({
      name: "pause",
      args: [video],
      fallback: () => video.pause(),
      validateRetVal: retVal => retVal == null,
    });
  }

  /**
   * OVERRIDABLE - calls the getPaused() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to determine if
   * a video is paused or not.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @returns {boolean} Boolean value true if paused, or false if video is still playing
   */
  getPaused(video) {
    return this.#callWrapperMethod({
      name: "getPaused",
      args: [video],
      fallback: () => video.paused,
      validateRetVal: retVal => this.#isBoolean(retVal),
    });
  }

  /**
   * OVERRIDABLE - calls the getEnded() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to determine if
   * video playback or streaming has stopped.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @returns {boolean} Boolean value true if the video has ended, or false if still playing
   */
  getEnded(video) {
    return this.#callWrapperMethod({
      name: "getEnded",
      args: [video],
      fallback: () => video.ended,
      validateRetVal: retVal => this.#isBoolean(retVal),
    });
  }

  /**
   * OVERRIDABLE - calls the getDuration() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to get the current
   * duration of a video in seconds.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @returns {number} Duration of the video in seconds
   */
  getDuration(video) {
    return this.#callWrapperMethod({
      name: "getDuration",
      args: [video],
      fallback: () => video.duration,
      validateRetVal: retVal => this.#isNumber(retVal),
    });
  }

  /**
   * OVERRIDABLE - calls the getCurrentTime() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to get the current
   * time of a video in seconds.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @returns {number} Current time of the video in seconds
   */
  getCurrentTime(video) {
    return this.#callWrapperMethod({
      name: "getCurrentTime",
      args: [video],
      fallback: () => video.currentTime,
      validateRetVal: retVal => this.#isNumber(retVal),
    });
  }

  /**
   * OVERRIDABLE - calls the setCurrentTime() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to set the current
   * time of a video.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @param {number} position
   *  The current playback time of the video
   * @param {boolean} wasPlaying
   *  True if the video was playing before seeking else false
   */
  setCurrentTime(video, position, wasPlaying) {
    return this.#callWrapperMethod({
      name: "setCurrentTime",
      args: [video, position, wasPlaying],
      fallback: () => {
        video.currentTime = position;
      },
      validateRetVal: retVal => retVal == null,
    });
  }

  /**
   * Return hours, minutes, and seconds from seconds
   *
   * @param {number} aSeconds
   *  The time in seconds
   * @returns {string} Timestamp string
   */
  timeFromSeconds(aSeconds) {
    aSeconds = isNaN(aSeconds) ? 0 : Math.round(aSeconds);
    let seconds = Math.floor(aSeconds % 60),
      minutes = Math.floor((aSeconds / 60) % 60),
      hours = Math.floor(aSeconds / 3600);
    seconds = seconds < 10 ? "0" + seconds : seconds;
    minutes = hours > 0 && minutes < 10 ? "0" + minutes : minutes;
    return aSeconds < 3600
      ? `${minutes}:${seconds}`
      : `${hours}:${minutes}:${seconds}`;
  }

  /**
   * Format a timestamp from current time and total duration,
   * output as a string in the form '0:00 / 0:00'
   *
   * @param {number} aCurrentTime
   *  The current time in seconds
   * @param {number} aDuration
   *  The total duration in seconds
   * @returns {string} Formatted timestamp
   */
  formatTimestamp(aCurrentTime, aDuration) {
    // We can't format numbers that can't be represented as decimal digits.
    if (!Number.isFinite(aCurrentTime) || !Number.isFinite(aDuration)) {
      return undefined;
    }

    return `${this.timeFromSeconds(aCurrentTime)} / ${this.timeFromSeconds(
      aDuration
    )}`;
  }

  /**
   * OVERRIDABLE - calls the getVolume() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to get the volume
   * value of a video.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @returns {number} Volume of the video between 0 (muted) and 1 (loudest)
   */
  getVolume(video) {
    return this.#callWrapperMethod({
      name: "getVolume",
      args: [video],
      fallback: () => video.volume,
      validateRetVal: retVal => this.#isNumber(retVal),
    });
  }

  /**
   * OVERRIDABLE - calls the setVolume() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to set the volume
   * value of a video.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @param {number} volume
   *  Value between 0 (muted) and 1 (loudest)
   */
  setVolume(video, volume) {
    return this.#callWrapperMethod({
      name: "setVolume",
      args: [video, volume],
      fallback: () => {
        video.volume = volume;
      },
      validateRetVal: retVal => retVal == null,
    });
  }

  /**
   * OVERRIDABLE - calls the getPlaybackRate() method defined in the site wrapper script. Runs a fallback
   * implementation if the method does not exist or if an error is thrown while calling it. This method is
   * meant to get the playback rate of a video.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @returns {number} Playback rate of the video, where 1 is normal speed
   */
  getPlaybackRate(video) {
    return this.#callWrapperMethod({
      name: "getPlaybackRate",
      args: [video],
      fallback: () => video.playbackRate,
      validateRetVal: retVal => this.#isNumber(retVal),
    });
  }

  /**
   * OVERRIDABLE - calls the setPlaybackRate() method defined in the site wrapper script. Runs a fallback
   * implementation if the method does not exist or if an error is thrown while calling it. This method is
   * meant to set the playback rate of a video.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @param {number} playbackRate
   *  Playback rate of the video, where 1 is normal speed
   */
  setPlaybackRate(video, playbackRate) {
    return this.#callWrapperMethod({
      name: "setPlaybackRate",
      args: [video, playbackRate],
      fallback: () => {
        video.playbackRate = playbackRate;
      },
      validateRetVal: retVal => retVal == null,
    });
  }

  /**
   * OVERRIDABLE - calls the isMuted() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to get the mute
   * state a video.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @param {boolean} shouldMute
   *  Boolean value true to mute the video, or false to unmute the video
   */
  isMuted(video) {
    return this.#callWrapperMethod({
      name: "isMuted",
      args: [video],
      fallback: () => video.muted,
      validateRetVal: retVal => this.#isBoolean(retVal),
    });
  }

  /**
   * OVERRIDABLE - calls the setMuted() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to mute or unmute
   * a video.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @param {boolean} shouldMute
   *  Boolean value true to mute the video, or false to unmute the video
   */
  setMuted(video, shouldMute) {
    return this.#callWrapperMethod({
      name: "setMuted",
      args: [video, shouldMute],
      fallback: () => {
        video.muted = shouldMute;
      },
      validateRetVal: retVal => retVal == null,
    });
  }

  /**
   * OVERRIDABLE - calls the setCaptionContainerObserver() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to listen for any cue changes in a
   * video's caption container and execute a callback function responsible for updating the pip window's text tracks container whenever
   * a cue change is triggered {@link updatePiPTextTracks()}.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @param {Function} _callback
   *  The callback function to be executed when cue changes are detected
   */
  setCaptionContainerObserver(video, _callback) {
    return this.#callWrapperMethod({
      name: "setCaptionContainerObserver",
      args: [
        video,
        (text, type) => {
          this.updatePiPTextTracks(text, type);
        },
      ],
      fallback: () => {},
      validateRetVal: retVal => retVal == null,
    });
  }

  /**
   * OVERRIDABLE - calls the removeCaptionContainerObserver() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to remove any caption observers that
   * may have been set in setCaptionContainerObserver().
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @param {Function} _callback
   *  The callback function to be executed when cue changes are detected
   */
  removeCaptionContainerObserver(video, _callback) {
    return this.#callWrapperMethod({
      name: "removeCaptionContainerObserver",
      args: [video],
      fallback: () => {},
      validateRetVal: retVal => retVal == null,
    });
  }

  /**
   * OVERRIDABLE - calls the shouldHideToggle() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to determine if the pip toggle
   * for a video should be hidden by the site wrapper.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @returns {boolean} Boolean value true if the pip toggle should be hidden by the site wrapper, or false if it should not
   */
  shouldHideToggle(video) {
    return this.#callWrapperMethod({
      name: "shouldHideToggle",
      args: [video],
      fallback: () => false,
      validateRetVal: retVal => this.#isBoolean(retVal),
    });
  }

  /**
   * OVERRIDABLE - calls the isUrlbarToggleEligible() method defined in the site wrapper script.
   * Runs a fallback that returns true if the method is not overridden. This method
   * is meant to let sites with multiple seemingly eligible videos (e.g. YouTube's preview players)
   * identify whether a video should be counted toward the urlbar PiP toggle criteria.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   * @returns {boolean} True if the video should be counted toward the urlbar PiP toggle criteria.
   */
  isUrlbarToggleEligible(video) {
    return this.#callWrapperMethod({
      name: "isUrlbarToggleEligible",
      args: [video],
      fallback: () => true,
      validateRetVal: retVal => this.#isBoolean(retVal),
    });
  }

  /**
   * OVERRIDABLE - calls the isLive() method defined in the site wrapper script. Runs a fallback implementation
   * if the method does not exist or if an error is thrown while calling it. This method is meant to get if the
   * video is a live stream.
   *
   * @param {HTMLVideoElement} video
   *  The originating video source element
   */
  isLive(video) {
    return this.#callWrapperMethod({
      name: "isLive",
      args: [video],
      fallback: () => video.duration === Infinity,
      validateRetVal: retVal => this.#isBoolean(retVal),
    });
  }
}

export function getActorFor(videoElement) {
  return gOriginatingVideoMap.get(videoElement);
}
