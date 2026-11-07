/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is a UA widget. It runs in per-origin UA widget scope,
// to be loaded by UAWidgetsChild.sys.mjs.

/*
 * This is the class of entry. It will construct the actual implementation
 * according to the value of the "controls" property.
 */
this.VideoControlsWidget = class {
  constructor(shadowRoot, prefs) {
    this.shadowRoot = shadowRoot;
    this.prefs = prefs;
    this.element = shadowRoot.host;
    this.document = this.element.ownerDocument;
    this.window = this.document.defaultView;

    this.isMobile = this.window.navigator.appVersion.includes("Android");
  }

  /*
   * Callback called by UAWidgets right after constructor.
   */
  onsetup() {
    this.switchImpl();
  }

  /*
   * Callback called by UAWidgets when the "controls" property changes.
   */
  onchange() {
    this.switchImpl();
  }

  /*
   * Actually switch the implementation.
   * - With "controls" set, the VideoControlsImplWidget controls should load.
   * - Without it, the NoControlsPictureInPictureImpleWidget should load
   *   if the video is being viewed in Picture-in-Picture.
   * - Without it, on mobile, the NoControlsMobileImplWidget should load, so
   *   the user could see the click-to-play button when the video/audio is blocked.
   */
  switchImpl() {
    let newImpl;
    let pageURI = this.document.documentURI;
    if (this.element.controls) {
      newImpl = VideoControlsImplWidget;
    } else if (VideoControlsWidget.isPictureInPictureVideo(this.element)) {
      newImpl = NoControlsPictureInPictureImplWidget;
    } else if (this.isMobile) {
      newImpl = NoControlsMobileImplWidget;
    } else if (
      pageURI.startsWith("http://") ||
      pageURI.startsWith("https://")
    ) {
      newImpl = NoControlsDesktopImplWidget;
    }

    // Skip if we are asked to load the same implementation, and
    // the underlying element state hasn't changed in ways that we
    // care about. This can happen if the property is set again
    // without a value change.
    if (this.impl && this.impl.constructor == newImpl) {
      this.impl.onchange();
      return;
    }
    if (this.impl) {
      this.impl.teardown();
      this.shadowRoot.firstChild.remove();
    }
    if (newImpl) {
      this.impl = new newImpl(this.shadowRoot, this.prefs);

      this.mDirection = "ltr";
      let intlUtils = this.window.intlUtils;
      if (intlUtils) {
        this.mDirection = intlUtils.isAppLocaleRTL() ? "rtl" : "ltr";
      }

      this.impl.onsetup(this.mDirection);
    } else {
      this.impl = undefined;
    }
  }

  teardown() {
    if (!this.impl) {
      return;
    }
    this.impl.teardown();
    this.shadowRoot.firstChild.remove();
    delete this.impl;
  }

  onPrefChange(prefName, prefValue) {
    this.prefs[prefName] = prefValue;

    if (!this.impl) {
      return;
    }

    this.impl.onPrefChange(prefName, prefValue);
  }

  // If you change this, also change SEEK_TIME_SECS in PictureInPictureChild.sys.mjs
  static SEEK_TIME_SECS = 5;

  /**
   * Cache of the control widths defined as CSS custom properties in
   * videocontrols.css, shared across all instances.
   *
   * @type {Map<string, number>}
   */
  static controlWidthCache = new Map();

  /**
   * Parsed markup of each widget, per document, keyed by an identifier of
   * the markup.
   *
   * @type {WeakMap<Document, Map<string, Element>>}
   */
  static templateCache = new WeakMap();

  /**
   * Returns the root element of the parsed `markup`, cached per document, to
   * be imported into a shadow root with importNodeAndAppendChildAt(). Callers
   * must not modify the returned element, other than from `fixup`, which runs
   * once after parsing.
   *
   * @param {Document} document
   *   Document whose widgets share the cached templates.
   * @param {string} key
   *   Cache key identifying the markup and any fixup variant.
   * @param {string} markup
   *   XML markup to parse when the template is not cached.
   * @param {function(Element): void} [fixup]
   *   Optional function that modifies the root element before it is cached.
   * @returns {Element} The cached template's root element.
   */
  static parseTemplate(document, key, markup, fixup) {
    let templates = this.templateCache.get(document);
    if (!templates) {
      templates = new Map();
      this.templateCache.set(document, templates);
    }
    let template = templates.get(key);
    if (!template) {
      const parser = new document.defaultView.DOMParser();
      template = parser.parseFromSafeString(
        markup,
        "application/xml"
      ).documentElement;
      fixup?.(template);
      templates.set(key, template);
    }
    return template;
  }

  static isPictureInPictureVideo(someVideo) {
    return someVideo.isCloningElementVisually;
  }

  /**
   * Returns true if a <video> meets the requirements to show the Picture-in-Picture
   * toggle. Those requirements currently are:
   *
   * 1. The video must be 45 seconds in length or longer.
   * 2. Neither the width or the height of the video can be less than 140px.
   * 3. The video must have audio.
   * 4. The video must not a MediaStream video (Bug 1592539)
   *
   * This can be overridden via the
   * media.videocontrols.picture-in-picture.video-toggle.always-show pref, which
   * is mostly used for testing.
   *
   * @param {object} prefs
   *   The preferences set that was passed to the UAWidget.
   * @param {Element} someVideo
   *   The <video> to test.
   * @param {object} reflowedDimensions
   *   An object representing the reflowed dimensions of the <video>. Properties
   *   are:
   *
   *     videoWidth (Number):
   *       The width of the video in pixels.
   *
   *     videoHeight (Number):
   *       The height of the video in pixels.
   *
   * @return {boolean}
   */
  static shouldShowPictureInPictureToggle(
    prefs,
    someVideo,
    reflowedDimensions
  ) {
    if (
      prefs[
        "media.videocontrols.picture-in-picture.respect-disablePictureInPicture"
      ] &&
      someVideo.disablePictureInPicture
    ) {
      return false;
    }

    if (isNaN(someVideo.duration)) {
      return false;
    }

    if (
      prefs["media.videocontrols.picture-in-picture.video-toggle.always-show"]
    ) {
      return true;
    }

    const MIN_VIDEO_LENGTH =
      prefs[
        "media.videocontrols.picture-in-picture.video-toggle.min-video-secs"
      ];

    if (someVideo.duration < MIN_VIDEO_LENGTH) {
      return false;
    }

    const MIN_VIDEO_DIMENSION = 140; // pixels
    if (
      reflowedDimensions.videoWidth < MIN_VIDEO_DIMENSION ||
      reflowedDimensions.videoHeight < MIN_VIDEO_DIMENSION
    ) {
      return false;
    }

    return true;
  }

  /**
   * Some variations on the Picture-in-Picture toggle are being experimented with.
   * These variations have slightly different setup parameters from the currently
   * shipping toggle, so this method sets up the experimental toggles in the event
   * that they're being used. It also will enable the appropriate stylesheet for
   * the preferred toggle experiment.
   *
   * @param {object} prefs
   *   The preferences set that was passed to the UAWidget.
   * @param {ShadowRoot} shadowRoot
   *   The shadowRoot of the <video> element where the video controls are.
   * @param {Element} toggle
   *   The toggle element.
   * @param {object} reflowedDimensions
   *   An object representing the reflowed dimensions of the <video>. Properties
   *   are:
   *
   *     videoWidth (Number):
   *       The width of the video in pixels.
   *
   *     videoHeight (Number):
   *       The height of the video in pixels.
   */
  static setupToggle(prefs, toggle, reflowedDimensions) {
    // These thresholds are all in pixels
    const SMALL_VIDEO_WIDTH_MAX = 320;
    const MEDIUM_VIDEO_WIDTH_MAX = 720;

    let isSmall = reflowedDimensions.videoWidth <= SMALL_VIDEO_WIDTH_MAX;
    toggle.toggleAttribute("small-video", isSmall);
    toggle.toggleAttribute(
      "medium-video",
      !isSmall && reflowedDimensions.videoWidth <= MEDIUM_VIDEO_WIDTH_MAX
    );

    toggle.setAttribute(
      "position",
      prefs["media.videocontrols.picture-in-picture.video-toggle.position"]
    );
    toggle.toggleAttribute(
      "has-used",
      prefs["media.videocontrols.picture-in-picture.video-toggle.has-used"]
    );
  }
};

this.VideoControlsImplWidget = class {
  constructor(shadowRoot, prefs) {
    this.shadowRoot = shadowRoot;
    this.prefs = prefs;
    this.element = shadowRoot.host;
    this.document = this.element.ownerDocument;
    this.window = this.document.defaultView;
  }

  onsetup(direction) {
    this.generateContent();

    this.shadowRoot.firstElementChild.setAttribute("localedir", direction);

    this.Utils = {
      debug: false,
      video: null,
      videocontrols: null,
      controlBar: null,
      playButton: null,
      muteButton: null,
      volumeControl: null,
      durationLabel: null,
      positionLabel: null,
      scrubber: null,
      progressBar: null,
      bufferBar: null,
      statusOverlay: null,
      controlsSpacer: null,
      clickToPlay: null,
      controlsOverlay: null,
      fullscreenButton: null,
      layoutControls: null,
      isShowingPictureInPictureMessage: false,
      l10n: this.l10n,

      textTracksCount: 0,
      videoEvents: [
        "play",
        "pause",
        "ended",
        "volumechange",
        "loadeddata",
        "loadstart",
        "timeupdate",
        "progress",
        "playing",
        "waiting",
        "canplay",
        "canplaythrough",
        "seeking",
        "seeked",
        "emptied",
        "loadedmetadata",
        "error",
        "suspend",
        "stalled",
        "mozvideoonlyseekbegin",
        "mozvideoonlyseekcompleted",
        "durationchange",
      ],

      firstFrameShown: false,
      timeUpdateCount: 0,
      maxCurrentTimeSeen: 0,
      isPausedByDragging: false,
      _isAudioOnly: false,

      get isAudioTag() {
        return this.video.localName == "audio";
      },

      get isAudioOnly() {
        return this._isAudioOnly;
      },
      set isAudioOnly(val) {
        this._isAudioOnly = val;
        this.setFullscreenButtonState();
        this.updatePictureInPictureToggleDisplay();

        if (!this.isTopLevelSyntheticDocument) {
          return;
        }
        if (this._isAudioOnly) {
          this.video.style.height = this.controlBarMinHeight + "px";
          this.video.style.width = "66%";
        } else {
          this.video.style.removeProperty("height");
          this.video.style.removeProperty("width");
        }
      },

      suppressError: false,

      setupStatusFader(immediate) {
        // Since the play button will be showing, we don't want to
        // show the throbber behind it. The throbber here will
        // only show if needed after the play button has been pressed.
        if (!this.clickToPlay.hidden) {
          this.startFadeOut(this.statusOverlay, true);
          return;
        }

        var show = false;
        if (
          this.video.seeking ||
          (this.video.error && !this.suppressError) ||
          this.video.networkState == this.video.NETWORK_NO_SOURCE ||
          (this.video.networkState == this.video.NETWORK_LOADING &&
            (this.video.paused || this.video.ended
              ? this.video.readyState < this.video.HAVE_CURRENT_DATA
              : this.video.readyState < this.video.HAVE_FUTURE_DATA)) ||
          (this.timeUpdateCount <= 1 &&
            !this.video.ended &&
            this.video.readyState < this.video.HAVE_FUTURE_DATA &&
            this.video.networkState == this.video.NETWORK_LOADING)
        ) {
          show = true;
        }

        // Explicitly hide the status fader if this
        // is audio only until bug 619421 is fixed.
        if (this.isAudioOnly) {
          show = false;
        }

        if (this._showThrobberTimer) {
          show = true;
        }

        this.log(
          "Status overlay: seeking=" +
            this.video.seeking +
            " error=" +
            this.video.error +
            " readyState=" +
            this.video.readyState +
            " paused=" +
            this.video.paused +
            " ended=" +
            this.video.ended +
            " networkState=" +
            this.video.networkState +
            " timeUpdateCount=" +
            this.timeUpdateCount +
            " _showThrobberTimer=" +
            this._showThrobberTimer +
            " --> " +
            (show ? "SHOW" : "HIDE")
        );
        this.startFade(this.statusOverlay, show, immediate);
      },

      /*
       * Set the initial state of the controls. The UA widget is normally created along
       * with video element, but could be attached at any point (eg, if the video is
       * removed from the document and then reinserted). Thus, some one-time events may
       * have already fired, and so we'll need to explicitly check the initial state.
       */
      setupInitialState() {
        this.setPlayButtonState(this.video.paused);

        this.setFullscreenButtonState();

        var duration = Math.round(this.video.duration * 1000); // in ms
        var currentTime = Math.round(this.video.currentTime * 1000); // in ms
        this.log(
          "Initial playback position is at " + currentTime + " of " + duration
        );
        // It would be nice to retain maxCurrentTimeSeen, but it would be difficult
        // to determine if the media source changed while we were detached.
        this.maxCurrentTimeSeen = currentTime;
        this.showPosition(currentTime, duration);

        // If we have metadata, check if this is a <video> without
        // video data, or a video with no audio track.
        if (this.video.readyState >= this.video.HAVE_METADATA) {
          if (
            this.video.localName == "video" &&
            (this.video.videoWidth == 0 || this.video.videoHeight == 0)
          ) {
            this.isAudioOnly = true;
          }

          // We have to check again if the media has audio here.
          let noAudio = !this.isAudioOnly && !this.video.mozHasAudio;
          this.muteButton.toggleAttribute("noAudio", noAudio);
          this.muteButton.disabled = noAudio;
        }

        // The video itself might not be fullscreen, but part of the
        // document might be, in which case we set this attribute to
        // apply any styles for the DOM fullscreen case.
        if (this.document.fullscreenElement) {
          this.videocontrols.setAttribute("inDOMFullscreen", true);
        }

        if (this.isAudioOnly) {
          this.startFadeOut(this.clickToPlay, true);
        }

        // If the first frame hasn't loaded, kick off a throbber fade-in.
        if (this.video.readyState >= this.video.HAVE_CURRENT_DATA) {
          this.firstFrameShown = true;
        }

        // We can't determine the exact buffering status, but do know if it's
        // fully loaded. (If it's still loading, it will fire a progress event
        // and we'll figure out the exact state then.)
        this.bufferBar.max = 100;
        if (this.video.readyState >= this.video.HAVE_METADATA) {
          this.showBuffered();
        } else {
          this.bufferBar.value = 0;
        }

        // Set the current status icon.
        if (this.hasError()) {
          this.startFadeOut(this.clickToPlay, true);
          this.statusIcon.setAttribute("type", "error");
          this.updateErrorText();
          this.setupStatusFader(true);
        }

        this.updatePictureInPictureMessage();

        if (this.video.readyState >= this.video.HAVE_METADATA) {
          // According to the spec[1], at the HAVE_METADATA (or later) state, we know
          // the video duration and dimensions, which means we can calculate whether or
          // not to show the Picture-in-Picture toggle now.
          //
          // [1]: https://www.w3.org/TR/html50/embedded-content-0.html#dom-media-have_metadata
          this.updatePictureInPictureToggleDisplay();
        }

        let adjustableControls = [
          ...this.prioritizedControls,
          this.controlBar,
          this.clickToPlay,
        ];

        for (let control of adjustableControls) {
          if (!control) {
            break;
          }

          this.defineControlProperties(control);
        }
        this.adjustControlSize();

        // Can only update the volume controls once we've computed
        // _volumeControlWidth, since the volume slider implementation
        // depends on it.
        this.updateVolumeControls();
      },

      defineControlProperties(control) {
        let throwOnGet = {
          get() {
            throw new Error("Please don't trigger reflow. See bug 1493525.");
          },
        };
        Object.defineProperties(control, {
          // We should directly access CSSOM to get pre-defined style instead of
          // retrieving computed dimensions from layout.
          minWidth: {
            get: () => {
              let controlId = control.id;
              let propertyName = `--${controlId}-width`;
              if (control.modifier) {
                propertyName += "-" + control.modifier;
              }

              // These only depend on videocontrols.css and on touch mode, so
              // they're cached and shared across all instances.
              let cacheKey = `${this.isTouchControls}:${propertyName}`;
              let cachedWidths = VideoControlsWidget.controlWidthCache;
              let width = cachedWidths.get(cacheKey);
              if (width !== undefined) {
                return width;
              }

              let preDefinedSize =
                this.controlBarComputedStyles.getPropertyValue(propertyName);

              // The stylesheet from <link> might not be loaded if the
              // element was inserted into a hidden iframe.
              // We can safely return 0 here for now, given that the controls
              // will be resized again, by the resizevideocontrols event,
              // from nsVideoFrame, when the element is visible.
              if (!preDefinedSize) {
                return 0;
              }

              width = parseInt(preDefinedSize, 10);
              cachedWidths.set(cacheKey, width);
              return width;
            },
          },
          offsetLeft: throwOnGet,
          offsetTop: throwOnGet,
          offsetWidth: throwOnGet,
          offsetHeight: throwOnGet,
          offsetParent: throwOnGet,
          clientLeft: throwOnGet,
          clientTop: throwOnGet,
          clientWidth: throwOnGet,
          clientHeight: throwOnGet,
          getClientRects: throwOnGet,
          getBoundingClientRect: throwOnGet,
          isAdjustableControl: {
            value: true,
          },
          modifier: {
            value: "",
            writable: true,
          },
          isWanted: {
            value: true,
            writable: true,
          },
          hidden: {
            set: v => {
              control._isHiddenExplicitly = v;
              control._updateHiddenAttribute();
            },
            get: () => {
              return (
                control.hasAttribute("hidden") ||
                control.classList.contains("fadeout")
              );
            },
          },
          hiddenByAdjustment: {
            set: v => {
              control._isHiddenByAdjustment = v;
              control._updateHiddenAttribute();
            },
            get: () => control._isHiddenByAdjustment,
          },
          _isHiddenByAdjustment: {
            value: false,
            writable: true,
          },
          _isHiddenExplicitly: {
            value: false,
            writable: true,
          },
          _updateHiddenAttribute: {
            value: () => {
              control.toggleAttribute(
                "hidden",
                control._isHiddenExplicitly || control._isHiddenByAdjustment
              );
            },
          },
        });
      },

      updatePictureInPictureToggleDisplay() {
        if (this.isAudioOnly) {
          this.pictureInPictureToggle.hidden = true;
          return;
        }

        // We only want to show the toggle when the closed captions menu
        // is closed, in order to avoid visual overlap.
        if (
          this.pipToggleEnabled &&
          !this.isShowingPictureInPictureMessage &&
          this.textTrackListContainer.hidden &&
          VideoControlsWidget.shouldShowPictureInPictureToggle(
            this.prefs,
            this.video,
            this.reflowedDimensions
          )
        ) {
          this.pictureInPictureToggle.hidden = false;
          VideoControlsWidget.setupToggle(
            this.prefs,
            this.pictureInPictureToggle,
            this.reflowedDimensions
          );
        } else {
          this.pictureInPictureToggle.hidden = true;
        }
      },

      setupNewLoadState() {
        // For videos with |autoplay| set, we'll leave the controls initially hidden,
        // so that they don't get in the way of the playing video. Otherwise we'll
        // go ahead and reveal the controls now, so they're an obvious user cue.
        var shouldShow =
          !this.dynamicControls || (this.video.paused && !this.video.autoplay);
        // Hide the overlay if the video time is non-zero or if an error occurred to workaround bug 718107.
        let shouldClickToPlayShow =
          shouldShow &&
          !this.isAudioOnly &&
          this.video.currentTime == 0 &&
          !this.hasError() &&
          !this.isShowingPictureInPictureMessage;
        this.startFade(this.clickToPlay, shouldClickToPlayShow, true);
        this.startFade(this.controlBar, shouldShow, true);
      },

      get dynamicControls() {
        // Don't fade controls for <audio> elements.
        var enabled = !this.isAudioOnly;

        // Allow tests to explicitly suppress the fading of controls.
        if (this.video.hasAttribute("mozNoDynamicControls")) {
          enabled = false;
        }

        // If the video hits an error, suppress controls if it
        // hasn't managed to do anything else yet.
        if (!this.firstFrameShown && this.hasError()) {
          enabled = false;
        }

        return enabled;
      },

      updateVolume() {
        const volume = this.volumeControl.value;
        this.setVolume(volume / 100);
      },

      updateVolumeControls() {
        var volume = this.video.muted ? 0 : this.video.volume;
        var volumePercentage = Math.round(volume * 100);
        this.updateMuteButtonState();
        this.volumeControl.value = volumePercentage;
      },

      /*
       * We suspend a video element's video decoder if the video
       * element is invisible. However, resuming the video decoder
       * takes time and we show the throbber UI if it takes more than
       * 250 ms.
       *
       * When an already-suspended video element becomes visible, we
       * resume its video decoder immediately and queue a video-only seek
       * task to seek the resumed video decoder to the current position;
       * meanwhile, we also file a "mozvideoonlyseekbegin" event which
       * we used to start the timer here.
       *
       * Once the queued seek operation is done, we dispatch a
       * "canplay" event which indicates that the resuming operation
       * is completed.
       */
      SHOW_THROBBER_TIMEOUT_MS: 250,
      _showThrobberTimer: null,
      _delayShowThrobberWhileResumingVideoDecoder() {
        this._showThrobberTimer = this.window.setTimeout(() => {
          this.statusIcon.setAttribute("type", "throbber");
          // Show the throbber immediately since we have waited for SHOW_THROBBER_TIMEOUT_MS.
          // We don't want to wait for another animation delay(750ms) and the
          // animation duration(300ms).
          this.setupStatusFader(true);
        }, this.SHOW_THROBBER_TIMEOUT_MS);
      },
      _cancelShowThrobberWhileResumingVideoDecoder() {
        if (this._showThrobberTimer) {
          this.window.clearTimeout(this._showThrobberTimer);
          this._showThrobberTimer = null;
        }
      },

      handleEvent(aEvent) {
        if (!aEvent.isTrusted) {
          this.log("Drop untrusted event ----> " + aEvent.type);
          return;
        }

        this.log("Got event ----> " + aEvent.type);

        if (this.videoEvents.includes(aEvent.type)) {
          this.handleVideoEvent(aEvent);
        } else {
          this.handleControlEvent(aEvent);
        }
      },

      handleVideoEvent(aEvent) {
        switch (aEvent.type) {
          case "play":
            this.setPlayButtonState(false);
            this.setupStatusFader();
            if (
              !this._triggeredByControls &&
              this.dynamicControls &&
              this.isTouchControls
            ) {
              this.startFadeOut(this.controlBar);
            }
            if (!this._triggeredByControls) {
              this.startFadeOut(this.clickToPlay, true);
            }
            this._triggeredByControls = false;
            break;
          case "pause":
            // Little white lie: if we've internally paused the video
            // while dragging the scrubber, don't change the button state.
            if (!this.scrubber.isDragging) {
              this.setPlayButtonState(true);
            }
            this.setupStatusFader();
            break;
          case "ended":
            this.setPlayButtonState(true);
            // We throttle timechange events, so the thumb might not be
            // exactly at the end when the video finishes.
            this.showPosition(
              Math.round(this.video.currentTime * 1000),
              Math.round(this.video.duration * 1000)
            );
            this.startFadeIn(this.controlBar);
            this.setupStatusFader();
            break;
          case "volumechange":
            this.updateVolumeControls();
            // Show the controls to highlight the changing volume,
            // but only if the click-to-play overlay has already
            // been hidden (we don't hide controls when the overlay is visible).
            if (this.clickToPlay.hidden && !this.isAudioOnly) {
              this.startFadeIn(this.controlBar);
              this.window.clearTimeout(this._hideControlsTimeout);
              this._hideControlsTimeout = this.window.setTimeout(
                () => this._hideControlsFn(),
                this.HIDE_CONTROLS_TIMEOUT_MS
              );
            }
            break;
          case "loadedmetadata": {
            // If a <video> doesn't have any video data, treat it as <audio>
            // and show the controls (they won't fade back out)
            if (
              this.video.localName == "video" &&
              (this.video.videoWidth == 0 || this.video.videoHeight == 0)
            ) {
              this.isAudioOnly = true;
              this.startFadeOut(this.clickToPlay, true);
              this.startFadeIn(this.controlBar);
              this.setFullscreenButtonState();
            }
            this.showPosition(
              Math.round(this.video.currentTime * 1000),
              Math.round(this.video.duration * 1000)
            );
            let noAudio = !this.isAudioOnly && !this.video.mozHasAudio;
            this.muteButton.toggleAttribute("noAudio", noAudio);
            this.muteButton.disabled = noAudio;
            this.adjustControlSize();
            this.updatePictureInPictureToggleDisplay();
            break;
          }
          case "durationchange":
            this.updatePictureInPictureToggleDisplay();
            break;
          case "loadeddata":
            this.firstFrameShown = true;
            this.setupStatusFader();
            break;
          case "loadstart":
            this.maxCurrentTimeSeen = 0;
            this.controlsSpacer.removeAttribute("aria-label");
            this.statusOverlay.removeAttribute("status");
            this.statusIcon.setAttribute("type", "throbber");
            this.isAudioOnly = this.isAudioTag;
            this.setPlayButtonState(true);
            this.setupNewLoadState();
            this.setupStatusFader();
            break;
          case "progress":
            this.statusIcon.removeAttribute("stalled");
            this.showBuffered();
            this.setupStatusFader();
            break;
          case "stalled":
            this.statusIcon.setAttribute("stalled", "true");
            this.statusIcon.setAttribute("type", "throbber");
            this.setupStatusFader();
            break;
          case "suspend":
            this.setupStatusFader();
            break;
          case "timeupdate":
            var currentTime = Math.round(this.video.currentTime * 1000); // in ms
            var duration = Math.round(this.video.duration * 1000); // in ms

            // If playing/seeking after the video ended, we won't get a "play"
            // event, so update the button state here.
            if (!this.video.paused) {
              this.setPlayButtonState(false);
            }

            this.timeUpdateCount++;
            // Whether we show the statusOverlay sometimes depends
            // on whether we've seen more than one timeupdate
            // event (if we haven't, there hasn't been any
            // "playback activity" and we may wish to show the
            // statusOverlay while we wait for HAVE_ENOUGH_DATA).
            // If we've seen more than 2 timeupdate events,
            // the count is no longer relevant to setupStatusFader.
            if (this.timeUpdateCount <= 2) {
              this.setupStatusFader();
            }

            // If the user is dragging the scrubber ignore the delayed seek
            // responses (don't yank the thumb away from the user)
            if (this.scrubber.isDragging) {
              return;
            }
            this.showPosition(currentTime, duration);
            this.showBuffered();
            break;
          case "emptied":
            this.bufferBar.value = 0;
            this.showPosition(0, 0);
            break;
          case "seeking":
            this.showBuffered();
            this.statusIcon.setAttribute("type", "throbber");
            this.setupStatusFader();
            break;
          case "waiting":
            this.statusIcon.setAttribute("type", "throbber");
            this.setupStatusFader();
            break;
          case "seeked":
          case "playing":
          case "canplay":
          case "canplaythrough":
            this.setupStatusFader();
            break;
          case "error":
            // We'll show the error status icon when we receive an error event
            // under either of the following conditions:
            // 1. The video has its error attribute set; this means we're loading
            //    from our src attribute, and the load failed, or we we're loading
            //    from source children and the decode or playback failed after we
            //    determined our selected resource was playable.
            // 2. The video's networkState is NETWORK_NO_SOURCE. This means we we're
            //    loading from child source elements, but we were unable to select
            //    any of the child elements for playback during resource selection.
            if (this.hasError()) {
              this.suppressError = false;
              this.startFadeOut(this.clickToPlay, true);
              this.statusIcon.setAttribute("type", "error");
              this.updateErrorText();
              this.setupStatusFader(true);
              // If video hasn't shown anything yet, disable the controls.
              if (!this.firstFrameShown && !this.isAudioOnly) {
                this.startFadeOut(this.controlBar);
              }
              this.controlsSpacer.removeAttribute("hideCursor");
            }
            break;
          case "mozvideoonlyseekbegin":
            this._delayShowThrobberWhileResumingVideoDecoder();
            break;
          case "mozvideoonlyseekcompleted":
            this._cancelShowThrobberWhileResumingVideoDecoder();
            this.setupStatusFader();
            break;
          default:
            this.log("!!! media event " + aEvent.type + " not handled!");
        }
      },

      handleControlEvent(aEvent) {
        switch (aEvent.type) {
          case "click":
            switch (aEvent.currentTarget) {
              case this.muteButton:
                this.toggleMute();
                break;
              case this.closedCaptionButton:
                this.toggleClosedCaption();
                break;
              case this.fullscreenButton:
                this.toggleFullscreen();
                break;
              case this.playButton:
              case this.clickToPlay:
              case this.controlsSpacer:
                this.clickToPlayClickHandler(aEvent);
                break;
              case this.textTrackList: {
                const index = +aEvent.originalTarget.getAttribute("index");
                this.changeTextTrack(index);
                this.closedCaptionButton.focus();
                break;
              }
              case this.videocontrols:
                // Prevent any click event within media controls from dispatching through to video.
                aEvent.stopPropagation();
                break;
            }
            break;
          case "dblclick":
            this.toggleFullscreen();
            break;
          case "resizevideocontrols": {
            // Since this event come from the layout, this is the only place
            // we are sure of that probing into layout won't trigger or force
            // reflow.
            // FIXME(emilio): We should rewrite this to just use
            // ResizeObserver, probably.
            this.reflowTriggeringCallValidator.isReflowTriggeringPropsAllowed = true;
            this.updateReflowedDimensions();
            this.reflowTriggeringCallValidator.isReflowTriggeringPropsAllowed = false;

            let scrubberWasHidden = this.scrubberStack.hidden;
            this.adjustControlSize();
            if (scrubberWasHidden && !this.scrubberStack.hidden) {
              // requestAnimationFrame + setTimeout of 0ms is a best effort way to avoid
              // triggering reflows, but cannot fully guarantee a reflow will not happen.
              this.window.requestAnimationFrame(() =>
                this.window.setTimeout(() => {
                  this.reflowTriggeringCallValidator.isReflowTriggeringPropsAllowed = true;
                  this.updateReflowedDimensions();
                  this.reflowTriggeringCallValidator.isReflowTriggeringPropsAllowed = false;
                }, 0)
              );
            }
            this.updatePictureInPictureToggleDisplay();
            break;
          }
          case "fullscreenchange":
            this.onFullscreenChange();
            break;
          case "keypress":
            this.keyHandler(aEvent);
            break;
          case "dragstart":
            aEvent.preventDefault(); // prevent dragging of controls image (bug 517114)
            break;
          case "input":
            switch (aEvent.currentTarget) {
              case this.scrubber:
                this.onScrubberInput(aEvent);
                break;
              case this.volumeControl:
                this.updateVolume();
                break;
            }
            break;
          case "change":
            switch (aEvent.currentTarget) {
              case this.scrubber:
                this.onScrubberChange(aEvent);
                break;
              case this.video.textTracks:
                this.setClosedCaptionButtonState();
                break;
            }
            break;
          case "mouseup":
            // add mouseup listener additionally to handle the case that `change` event
            // isn't fired when the input value before/after dragging are the same. (bug 1328061)
            this.onScrubberChange(aEvent);
            break;
          case "addtrack":
            this.onTextTrackAdd(aEvent);
            break;
          case "removetrack":
            this.onTextTrackRemove(aEvent);
            break;
          case "focusin":
            // Show the controls to highlight the focused control, but only
            // under certain conditions:
            if (
              this.prefs["media.videocontrols.keyboard-tab-to-all-controls"] &&
              // The click-to-play overlay must already be hidden (we don't
              // hide controls when the overlay is visible).
              this.clickToPlay.hidden &&
              // Don't do this if the controls are static.
              this.dynamicControls &&
              // If the mouse is hovering over the control bar, the controls
              // are already showing and they shouldn't hide, so don't mess
              // with them.
              // We use "div:hover" instead of just ":hover" so this works in
              // quirks mode documents. See
              // https://quirks.spec.whatwg.org/#the-active-and-hover-quirk
              !this.controlBar.matches("div:hover")
            ) {
              this.startFadeIn(this.controlBar);
              this.window.clearTimeout(this._hideControlsTimeout);
              this._hideControlsTimeout = this.window.setTimeout(
                () => this._hideControlsFn(),
                this.HIDE_CONTROLS_TIMEOUT_MS
              );
            }
            break;
          case "mousedown":
            // We only listen for mousedown on sliders.
            // If this slider isn't focused already, mousedown will focus it.
            // We don't want that because it will then handle additional keys.
            // For example, we don't want the up/down arrow keys to seek after
            // the scrubber is clicked. To prevent that, we need to redirect
            // focus. However, dragging only works while the slider is focused,
            // so we must redirect focus after mouseup.
            if (
              this.prefs["media.videocontrols.keyboard-tab-to-all-controls"] &&
              !aEvent.currentTarget.matches(":focus")
            ) {
              aEvent.currentTarget.addEventListener(
                "mouseup",
                aEvent => {
                  if (aEvent.currentTarget.matches(":focus")) {
                    // We can't use target.blur() because that will blur the
                    // video element as well.
                    this.video.focus();
                  }
                },
                { once: true }
              );
            }
            break;
          default:
            this.log("!!! control event " + aEvent.type + " not handled!");
        }
      },

      terminate() {
        if (this.videoEvents) {
          for (let event of this.videoEvents) {
            try {
              this.video.removeEventListener(event, this, {
                capture: true,
                mozSystemGroup: true,
              });
            } catch (ex) {}
          }
        }

        try {
          for (let { el, type, capture = false } of this.controlsEvents) {
            el.removeEventListener(type, this, {
              mozSystemGroup: true,
              capture,
            });
          }
        } catch (ex) {}

        this.window.clearTimeout(this._showControlsTimeout);
        this.window.clearTimeout(this._hideControlsTimeout);
        this._cancelShowThrobberWhileResumingVideoDecoder();

        this.log("--- videocontrols terminated ---");
      },

      hasError() {
        // We either have an explicit error, or the resource selection
        // algorithm is running and we've tried to load something and failed.
        // Note: we don't consider the case where we've tried to load but
        // there's no sources to load as an error condition, as sites may
        // do this intentionally to work around requires-user-interaction to
        // play restrictions, and we don't want to display a debug message
        // if that's the case.
        return (
          this.video.error != null ||
          (this.video.networkState == this.video.NETWORK_NO_SOURCE &&
            this.hasSources())
        );
      },

      updatePictureInPictureMessage() {
        let showMessage =
          !this.hasError() &&
          VideoControlsWidget.isPictureInPictureVideo(this.video);
        this.pictureInPictureOverlay.hidden = !showMessage;
        this.isShowingPictureInPictureMessage = showMessage;
      },

      hasSources() {
        if (
          this.video.hasAttribute("src") &&
          this.video.getAttribute("src") !== ""
        ) {
          return true;
        }
        for (
          var child = this.video.firstChild;
          child !== null;
          child = child.nextElementSibling
        ) {
          if (child instanceof this.window.HTMLSourceElement) {
            return true;
          }
        }
        return false;
      },

      updateErrorText() {
        let error;
        let v = this.video;
        // It is possible to have both v.networkState == NETWORK_NO_SOURCE
        // as well as v.error being non-null. In this case, we will show
        // the v.error.code instead of the v.networkState error.
        if (v.error) {
          switch (v.error.code) {
            case v.error.MEDIA_ERR_ABORTED:
              error = "errorAborted";
              break;
            case v.error.MEDIA_ERR_NETWORK:
              error = "errorNetwork";
              break;
            case v.error.MEDIA_ERR_DECODE:
              error = "errorDecode";
              break;
            case v.error.MEDIA_ERR_SRC_NOT_SUPPORTED:
              error =
                v.networkState == v.NETWORK_NO_SOURCE
                  ? "errorNoSource"
                  : "errorSrcNotSupported";
              break;
            default:
              error = "errorGeneric";
              break;
          }
        } else if (v.networkState == v.NETWORK_NO_SOURCE) {
          error = "errorNoSource";
        } else {
          return; // No error found.
        }

        let label = this.shadowRoot.getElementById(error);
        this.controlsSpacer.setAttribute("aria-label", label.textContent);
        this.statusOverlay.setAttribute("status", error);
      },

      formatTime(aTime) {
        aTime = Math.round(aTime / 1000);
        let hours = Math.floor(aTime / 3600);
        let mins = Math.floor((aTime % 3600) / 60);
        let secs = Math.floor(aTime % 60);
        let timeString;
        if (secs < 10) {
          secs = "0" + secs;
        }
        if (hours) {
          if (mins < 10) {
            mins = "0" + mins;
          }
          timeString = hours + ":" + mins + ":" + secs;
        } else {
          timeString = mins + ":" + secs;
        }
        return timeString;
      },

      pauseVideoDuringDragging() {
        if (
          !this.video.paused &&
          !this.isPausedByDragging &&
          this.scrubber.isDragging
        ) {
          this.isPausedByDragging = true;
          this.video.pause();
        }
      },

      onScrubberInput() {
        const duration = Math.round(this.video.duration * 1000); // in ms
        let time = this.scrubber.value;

        this.seekToPosition(time);
        this.showPosition(time, duration);
        this.updateScrubberProgress();

        this.scrubber.isDragging = true;
        this.pauseVideoDuringDragging();
      },

      onScrubberChange() {
        this.scrubber.isDragging = false;

        if (this.isPausedByDragging) {
          this.video.play().catch(() => {
            /* Do nothing on rejection */
          });
          this.isPausedByDragging = false;
        }
      },

      updateScrubberProgress() {
        const positionPercent = (this.scrubber.value / this.scrubber.max) * 100;

        if (!isNaN(positionPercent) && positionPercent != Infinity) {
          this.progressBar.value = positionPercent;
        } else {
          this.progressBar.value = 0;
        }
      },

      seekToPosition(newPosition) {
        newPosition /= 1000; // convert from ms
        this.log("+++ seeking to " + newPosition);
        this.video.currentTime = newPosition;
      },

      setVolume(newVolume) {
        this.log("*** setting volume to " + newVolume);
        this.video.volume = newVolume;
        this.video.muted = false;
      },

      showPosition(currentTimeMs, durationMs) {
        // If the duration is unknown (because the server didn't provide
        // it, or the video is a stream), then we want to fudge the duration
        // by using the maximum playback position that's been seen.
        if (currentTimeMs > this.maxCurrentTimeSeen) {
          this.maxCurrentTimeSeen = currentTimeMs;
        }
        this.log(
          "time update @ " + currentTimeMs + "ms of " + durationMs + "ms"
        );

        let durationIsInfinite = durationMs == Infinity;
        if (isNaN(durationMs) || durationIsInfinite) {
          durationMs = this.maxCurrentTimeSeen;
        }
        this.log("durationMs is " + durationMs + "ms.\n");

        let scrubberProgress = Math.abs(
          currentTimeMs / durationMs - this.scrubber.value / this.scrubber.max
        );
        let devPxProgress =
          scrubberProgress *
          this.reflowedDimensions.scrubberWidth *
          this.window.devicePixelRatio;
        // Hack: if we haven't updated the scrubber width to be non-0, but
        // the scrubber stack is visible, assume there is progress.
        // This should be rectified by the next time we do layout (see handling
        // of resizevideocontrols events in handleEvent).
        if (
          !this.reflowedDimensions.scrubberWidth &&
          !this.scrubberStack.hidden
        ) {
          devPxProgress = 1;
        }
        // Update the scrubber only if it will move by at least 1 pixel
        // Note that this.scrubber.max can be "" if unitialized,
        // and either or both of currentTimeMs or durationMs can be 0, leading
        // to NaN or Infinity values for devPxProgress.
        if (!this.scrubber.max || isNaN(devPxProgress) || devPxProgress > 0.5) {
          this.scrubber.max = durationMs;
          this.scrubber.value = currentTimeMs;
          this.updateScrubberProgress();
        }

        // If the duration is over an hour, thumb should show h:mm:ss instead
        // of mm:ss, which makes it bigger. We set the modifier prop which
        // informs CSS custom properties used elsewhere to determine minimum
        // widths we need to show stuff.
        let modifier = durationMs >= 3600000 ? "long" : "";
        this.positionDurationBox.modifier = this.durationSpan.modifier =
          modifier;

        // Update the text-based labels:
        let position = this.formatTime(currentTimeMs);
        let duration = durationIsInfinite ? "" : this.formatTime(durationMs);
        if (
          this.positionString != position ||
          this.durationString != duration
        ) {
          // Only update the DOM if there is a visible change.
          this._updatePositionLabels(position, duration);
        }
      },

      _updatePositionLabels(position, duration) {
        this.positionString = position;
        this.durationString = duration;

        this.l10n.setAttributes(
          this.positionDurationBox,
          "videocontrols-position-and-duration-labels",
          { position, duration }
        );
        this.l10n.setAttributes(
          this.scrubber,
          "videocontrols-scrubber-position-and-duration",
          { position, duration }
        );
      },

      showBuffered() {
        function bsearch(haystack, needle, cmp) {
          var length = haystack.length;
          var low = 0;
          var high = length;
          while (low < high) {
            var probe = low + ((high - low) >> 1);
            var r = cmp(haystack, probe, needle);
            if (r == 0) {
              return probe;
            } else if (r > 0) {
              low = probe + 1;
            } else {
              high = probe;
            }
          }
          return -1;
        }

        function bufferedCompare(buffered, i, time) {
          if (time > buffered.end(i)) {
            return 1;
          } else if (time >= buffered.start(i)) {
            return 0;
          }
          return -1;
        }

        var duration = Math.round(this.video.duration * 1000);
        if (isNaN(duration) || duration == Infinity) {
          duration = this.maxCurrentTimeSeen;
        }

        // Find the range that the current play position is in and use that
        // range for bufferBar.  At some point we may support multiple ranges
        // displayed in the bar.
        var currentTime = this.video.currentTime;
        var buffered = this.video.buffered;
        var index = bsearch(buffered, currentTime, bufferedCompare);
        var endTime = 0;
        if (index >= 0) {
          endTime = Math.round(buffered.end(index) * 1000);
        }
        if (this.duration == duration && this.buffered == endTime) {
          // Avoid modifying the DOM if there is no update to show.
          return;
        }

        this.bufferBar.max = this.duration = duration;
        this.bufferBar.value = this.buffered = endTime;
        // Progress bars are automatically reported by screen readers even when
        // they aren't focused, which intrudes on the audio being played.
        // Ideally, we'd just change the a11y role of bufferBar, but there's
        // no role which will let us just expose text via an ARIA attribute.
        // Therefore, we hide bufferBar for a11y and expose the info as
        // off-screen text.
        this.bufferA11yVal.textContent =
          (this.bufferBar.position * 100).toFixed() + "%";
      },

      _controlsHiddenByTimeout: false,
      _showControlsTimeout: 0,
      SHOW_CONTROLS_TIMEOUT_MS: 500,
      _showControlsFn() {
        if (this.video.matches("video:hover")) {
          this.startFadeIn(this.controlBar, false);
          this._showControlsTimeout = 0;
          this._controlsHiddenByTimeout = false;
        }
      },

      _hideControlsTimeout: 0,
      _hideControlsFn() {
        if (!this.scrubber.isDragging) {
          this.startFade(this.controlBar, false);
          this._hideControlsTimeout = 0;
          this._controlsHiddenByTimeout = true;
        }
      },
      HIDE_CONTROLS_TIMEOUT_MS: 2000,

      // By "Video" we actually mean the video controls container,
      // because we don't want to consider the padding of <video> added
      // by the web content.
      isMouseOverVideo(event) {
        // XXX: this triggers reflow too, but the layout should only be dirty
        // if the web content touches it while the mouse is moving.
        let el = this.shadowRoot.elementFromPoint(event.clientX, event.clientY);

        // As long as this is not null, the cursor is over something within our
        // Shadow DOM.
        return !!el;
      },

      isMouseOverControlBar(event) {
        // XXX: this triggers reflow too, but the layout should only be dirty
        // if the web content touches it while the mouse is moving.
        let el = this.shadowRoot.elementFromPoint(event.clientX, event.clientY);
        while (el && el !== this.shadowRoot) {
          if (el == this.controlBar) {
            return true;
          }
          el = el.parentNode;
        }
        return false;
      },

      onMouseMove(event) {
        // If the controls are static, don't change anything.
        if (!this.dynamicControls) {
          return;
        }

        this.window.clearTimeout(this._hideControlsTimeout);

        // Suppress fading out the controls until the video has rendered
        // its first frame. But since autoplay videos start off with no
        // controls, let them fade-out so the controls don't get stuck on.
        if (!this.firstFrameShown && !this.video.autoplay) {
          return;
        }

        if (this._controlsHiddenByTimeout) {
          this._showControlsTimeout = this.window.setTimeout(
            () => this._showControlsFn(),
            this.SHOW_CONTROLS_TIMEOUT_MS
          );
        } else {
          this.startFade(this.controlBar, true);
        }

        // Hide the controls if the mouse cursor is left on top of the video
        // but above the control bar and if the click-to-play overlay is hidden.
        if (
          (this._controlsHiddenByTimeout ||
            !this.isMouseOverControlBar(event)) &&
          this.clickToPlay.hidden
        ) {
          this._hideControlsTimeout = this.window.setTimeout(
            () => this._hideControlsFn(),
            this.HIDE_CONTROLS_TIMEOUT_MS
          );
        }
      },

      onMouseInOut(event) {
        // If the controls are static, don't change anything.
        if (!this.dynamicControls) {
          return;
        }

        this.window.clearTimeout(this._hideControlsTimeout);

        let isMouseOverVideo = this.isMouseOverVideo(event);

        // Suppress fading out the controls until the video has rendered
        // its first frame. But since autoplay videos start off with no
        // controls, let them fade-out so the controls don't get stuck on.
        if (
          !this.firstFrameShown &&
          !isMouseOverVideo &&
          !this.video.autoplay
        ) {
          return;
        }

        if (!isMouseOverVideo && !this.isMouseOverControlBar(event)) {
          this.adjustControlSize();

          // Keep the controls visible if the click-to-play is visible.
          if (!this.clickToPlay.hidden) {
            return;
          }

          this.startFadeOut(this.controlBar, false);
          this.hideClosedCaptionMenu();
          this.window.clearTimeout(this._showControlsTimeout);
          this._controlsHiddenByTimeout = false;
        }
      },

      startFadeIn(element, immediate) {
        this.startFade(element, true, immediate);
      },

      startFadeOut(element, immediate) {
        this.startFade(element, false, immediate);
      },

      animationMap: new WeakMap(),

      animationProps: {
        clickToPlay: {
          keyframes: [
            { transform: "scale(3)", opacity: 0 },
            { transform: "scale(1)", opacity: 0.55 },
          ],
          options: {
            easing: "ease",
            duration: 400,
            // The fill mode here and below is a workaround to avoid flicker
            // due to bug 1495350.
            fill: "both",
          },
        },
        controlBar: {
          keyframes: [{ opacity: 0 }, { opacity: 1 }],
          options: {
            easing: "ease",
            duration: 200,
            fill: "both",
          },
        },
        statusOverlay: {
          keyframes: [
            { opacity: 0 },
            { opacity: 0, offset: 0.72 }, // ~750ms into animation
            { opacity: 1 },
          ],
          options: {
            duration: 1050,
            fill: "both",
          },
        },
      },

      startFade(element, fadeIn, immediate = false) {
        let animationProp = this.animationProps[element.id];
        if (!animationProp) {
          throw new Error(
            "Element " +
              element.id +
              " has no transition. Toggle the hidden property directly."
          );
        }

        // Creating the animation is not free, so avoid doing it for immediate
        // fades (e.g. during setup) when there's nothing to cancel.
        let animation = this.animationMap.get(element);
        if (!animation && !immediate) {
          animation = new this.window.Animation(
            new this.window.KeyframeEffect(
              element,
              animationProp.keyframes,
              animationProp.options
            )
          );

          this.animationMap.set(element, animation);
        }

        if (fadeIn) {
          if (element == this.controlBar) {
            this.controlsSpacer.removeAttribute("hideCursor");
            // Ensure the Full Screen button is in the tab order.
            this.fullscreenButton.removeAttribute("tabindex");
          }

          // hidden state should be controlled by adjustControlSize
          if (element.isAdjustableControl && element.hiddenByAdjustment) {
            return;
          }

          // No need to fade in again if the hidden property returns false
          // (not hidden and not fading out.)
          if (!element.hidden) {
            return;
          }

          // Unhide
          element.hidden = false;
        } else {
          if (element == this.controlBar) {
            if (!this.hasError() && this.isVideoInFullScreen) {
              this.controlsSpacer.setAttribute("hideCursor", true);
            }
            if (
              !this.prefs["media.videocontrols.keyboard-tab-to-all-controls"]
            ) {
              // The Full Screen button is currently the only tabbable button
              // when the controls are shown. Remove it from the tab order when
              // visually hidden to prevent visual confusion.
              this.fullscreenButton.setAttribute("tabindex", "-1");
            }
          }

          // No need to fade out if the hidden property returns true
          // (hidden or is fading out)
          if (element.hidden) {
            return;
          }
        }

        element.classList.toggle("fadeout", !fadeIn);
        element.classList.toggle("fadein", fadeIn);
        let finishedPromise;
        if (!immediate) {
          // At this point, if there is a pending animation, we just stop it to avoid it happening.
          // If there is a running animation, we reverse it, to have it rewind to the beginning.
          // If there is an idle/finished animation, we schedule a new one that reverses the finished one.
          if (animation.pending) {
            // Animation is running but pending.
            // Just cancel the pending animation to stop its effect.
            animation.cancel();
            finishedPromise = Promise.resolve();
          } else {
            switch (animation.playState) {
              case "idle":
              case "finished":
                // There is no animation currently playing.
                // Schedule a new animation with the desired playback direction.
                animation.playbackRate = fadeIn ? 1 : -1;
                animation.play();
                break;
              case "running":
                // Allow the animation to play from its current position in
                // reverse to finish.
                animation.reverse();
                break;
              case "pause":
                throw new Error("Animation should never reach pause state.");
              default:
                throw new Error(
                  "Unknown Animation playState: " + animation.playState
                );
            }
            finishedPromise = animation.finished;
          }
        } else {
          // immediate
          animation?.cancel();
          finishedPromise = Promise.resolve();
        }
        finishedPromise.then(
          animation => {
            if (element == this.controlBar) {
              this.onControlBarAnimationFinished();
            }
            element.classList.remove(fadeIn ? "fadein" : "fadeout");
            if (!fadeIn) {
              element.hidden = true;
            }
            if (animation) {
              // Explicitly clear the animation effect so that filling animations
              // stop overwriting stylesheet styles. Remove when bug 1495350 is
              // fixed and animations are no longer filling animations.
              // This also stops them from accumulating (See bug 1253476).
              animation.cancel();
            }
          },
          () => {
            /* Do nothing on rejection */
          }
        );
      },

      _triggeredByControls: false,

      startPlay() {
        this._triggeredByControls = true;
        this.hideClickToPlay();
        this.video.play().catch(() => {
          /* Do nothing on rejection */
        });
      },

      togglePause() {
        if (this.video.paused || this.video.ended) {
          this.startPlay();
        } else {
          this.video.pause();
        }

        // We'll handle style changes in the event listener for
        // the "play" and "pause" events, same as if content
        // script was controlling video playback.
      },

      get isVideoWithoutAudioTrack() {
        return (
          this.video.readyState >= this.video.HAVE_METADATA &&
          !this.isAudioOnly &&
          !this.video.mozHasAudio
        );
      },

      toggleMute() {
        if (this.isVideoWithoutAudioTrack) {
          return;
        }
        this.video.muted = !this.isEffectivelyMuted;
        if (this.video.volume === 0) {
          this.video.volume = 0.5;
        }

        // We'll handle style changes in the event listener for
        // the "volumechange" event, same as if content script was
        // controlling volume.
      },

      get isVideoInFullScreen() {
        return this.video.isSameNode(
          this.video.getRootNode().fullscreenElement
        );
      },

      toggleFullscreen() {
        // audio tags cannot toggle fullscreen
        if (!this.isAudioTag) {
          this.isVideoInFullScreen
            ? this.document.exitFullscreen()
            : this.video.requestFullscreen();
        }
      },

      setFullscreenButtonState() {
        if (this.isAudioOnly || !this.document.fullscreenEnabled) {
          this.controlBar.setAttribute("fullscreen-unavailable", true);
          this.adjustControlSize();
          return;
        }
        this.controlBar.removeAttribute("fullscreen-unavailable");
        this.adjustControlSize();

        var id = this.isVideoInFullScreen
          ? "videocontrols-exitfullscreen-button"
          : "videocontrols-enterfullscreen-button";
        this.l10n.setAttributes(this.fullscreenButton, id);

        if (this.isVideoInFullScreen) {
          this.fullscreenButton.setAttribute("fullscreened", "true");
        } else {
          this.fullscreenButton.removeAttribute("fullscreened");
        }
      },

      onFullscreenChange() {
        if (this.document.fullscreenElement) {
          this.videocontrols.setAttribute("inDOMFullscreen", true);
        } else {
          this.videocontrols.removeAttribute("inDOMFullscreen");
        }

        if (this.isVideoInFullScreen) {
          this.startFadeOut(this.controlBar, true);
        }

        this.setFullscreenButtonState();
      },

      clickToPlayClickHandler(e) {
        if (e.button != 0) {
          return;
        }
        if (this.hasError() && !this.suppressError) {
          // Errors that can be dismissed should be placed here as we discover them.
          if (this.video.error.code != this.video.error.MEDIA_ERR_ABORTED) {
            return;
          }
          this.startFadeOut(this.statusOverlay, true);
          this.suppressError = true;
          return;
        }
        if (e.defaultPrevented) {
          return;
        }
        if (this.playButton.hasAttribute("paused")) {
          this.startPlay();
        } else {
          this.video.pause();
        }
      },
      hideClickToPlay() {
        let videoHeight = this.reflowedDimensions.videoHeight;
        let videoWidth = this.reflowedDimensions.videoWidth;

        // The play button will animate to 3x its size. This
        // shows the animation unless the video is too small
        // to show 2/3 of the animation.
        let animationScale = 2;
        let animationMinSize = this.clickToPlay.minWidth * animationScale;

        let immediate =
          animationMinSize > videoWidth ||
          animationMinSize > videoHeight - this.controlBarMinHeight;
        this.startFadeOut(this.clickToPlay, immediate);
      },

      setPlayButtonState(aPaused) {
        if (aPaused) {
          this.playButton.setAttribute("paused", "true");
        } else {
          this.playButton.removeAttribute("paused");
        }

        var id = aPaused
          ? "videocontrols-play-button"
          : "videocontrols-pause-button";
        this.l10n.setAttributes(this.playButton, id);
        this.l10n.setAttributes(this.clickToPlay, id);
      },

      get isEffectivelyMuted() {
        return this.video.muted || !this.video.volume;
      },

      updateMuteButtonState() {
        var muted = this.isEffectivelyMuted;
        this.muteButton.toggleAttribute("muted", muted);

        var id = muted
          ? "videocontrols-unmute-button"
          : "videocontrols-mute-button";
        this.l10n.setAttributes(this.muteButton, id);
      },

      keyboardVolumeDecrease() {
        const oldval = this.video.volume;
        this.video.volume = oldval < 0.1 ? 0 : oldval - 0.1;
        this.video.muted = false;
      },

      keyboardVolumeIncrease() {
        const oldval = this.video.volume;
        this.video.volume = oldval > 0.9 ? 1 : oldval + 0.1;
        this.video.muted = false;
      },

      keyboardSeekBack(tenPercent) {
        const oldval = this.video.currentTime;
        let newval;
        if (tenPercent) {
          newval =
            oldval -
            (this.video.duration || this.maxCurrentTimeSeen / 1000) / 10;
        } else {
          newval = oldval - VideoControlsWidget.SEEK_TIME_SECS;
        }
        this.video.currentTime = Math.max(0, newval);
      },

      keyboardSeekForward(tenPercent) {
        const oldval = this.video.currentTime;
        const maxtime = this.video.duration || this.maxCurrentTimeSeen / 1000;
        let newval;
        if (tenPercent) {
          newval = oldval + maxtime / 10;
        } else {
          newval = oldval + VideoControlsWidget.SEEK_TIME_SECS;
        }
        this.video.currentTime = Math.min(newval, maxtime);
      },

      keyHandler(event) {
        // Ignore keys when content might be providing its own.
        if (!this.video.hasAttribute("controls")) {
          return;
        }

        let keystroke = "";
        if (event.altKey) {
          keystroke += "alt-";
        }
        if (event.shiftKey) {
          keystroke += "shift-";
        }
        if (this.window.navigator.platform.startsWith("Mac")) {
          if (event.metaKey) {
            keystroke += "accel-";
          }
          if (event.ctrlKey) {
            keystroke += "control-";
          }
        } else {
          if (event.metaKey) {
            keystroke += "meta-";
          }
          if (event.ctrlKey) {
            keystroke += "accel-";
          }
        }
        if (event.key == " ") {
          keystroke += "Space";
        } else {
          keystroke += event.key;
        }

        this.log("Got keystroke: " + keystroke);

        // If unmodified cursor keys are pressed when a slider is focused, we
        // should act on that slider. For example, if we're focused on the
        // volume slider, rightArrow should increase the volume, not seek.
        // Normally, we'd just pass the keys through to the slider in this case.
        // However, the native adjustment is too small, so we override it.
        try {
          const target = event.originalTarget;
          const allTabbable =
            this.prefs["media.videocontrols.keyboard-tab-to-all-controls"];
          switch (keystroke) {
            case "Space" /* Play */:
              if (target.localName === "button" && !target.disabled) {
                break;
              }
              this.togglePause();
              break;
            case "ArrowDown" /* Volume decrease */:
              if (allTabbable && target == this.scrubber) {
                this.keyboardSeekBack(/* tenPercent */ false);
              } else if (target.classList.contains("textTrackItem")) {
                target.nextSibling?.focus();
              } else {
                this.keyboardVolumeDecrease();
              }
              break;
            case "ArrowUp" /* Volume increase */:
              if (allTabbable && target == this.scrubber) {
                this.keyboardSeekForward(/* tenPercent */ false);
              } else if (target.classList.contains("textTrackItem")) {
                target.previousSibling?.focus();
              } else {
                this.keyboardVolumeIncrease();
              }
              break;
            case "accel-ArrowDown" /* Mute */:
              this.video.muted = true;
              break;
            case "accel-ArrowUp" /* Unmute */:
              this.video.muted = false;
              break;
            case "ArrowLeft" /* Seek back 5 seconds */:
              if (allTabbable && target == this.volumeControl) {
                this.keyboardVolumeDecrease();
              } else {
                this.keyboardSeekBack(/* tenPercent */ false);
              }
              break;
            case "accel-ArrowLeft" /* Seek back 10% */:
              this.keyboardSeekBack(/* tenPercent */ true);
              break;
            case "ArrowRight" /* Seek forward 5 seconds */:
              if (allTabbable && target == this.volumeControl) {
                this.keyboardVolumeIncrease();
              } else {
                this.keyboardSeekForward(/* tenPercent */ false);
              }
              break;
            case "accel-ArrowRight" /* Seek forward 10% */:
              this.keyboardSeekForward(/* tenPercent */ true);
              break;
            case "Home" /* Seek to beginning */:
              this.video.currentTime = 0;
              break;
            case "End" /* Seek to end */:
              if (this.video.currentTime != this.video.duration) {
                this.video.currentTime =
                  this.video.duration || this.maxCurrentTimeSeen / 1000;
              }
              break;
            case "Escape" /* Escape */:
              if (
                target.classList.contains("textTrackItem") &&
                !this.textTrackListContainer.hidden
              ) {
                this.toggleClosedCaption();
                this.closedCaptionButton.focus();
              }
              break;
            default:
              return;
          }
        } catch (e) {
          /* ignore any exception from setting .currentTime */
        }

        event.preventDefault(); // Prevent page scrolling
      },

      checkTextTrackSupport(textTrack) {
        return textTrack.kind == "subtitles" || textTrack.kind == "captions";
      },

      get isClosedCaptionAvailable() {
        // There is no rendering area, no need to show the caption.
        if (this.isAudioOnly) {
          return false;
        }
        return this.overlayableTextTracks.length;
      },

      get overlayableTextTracks() {
        return Array.prototype.filter.call(
          this.video.textTracks,
          this.checkTextTrackSupport
        );
      },

      get currentTextTrackIndex() {
        const showingTT = this.overlayableTextTracks.find(
          tt => tt.mode == "showing"
        );

        // fallback to off button if there's no showing track.
        return showingTT ? showingTT.index : 0;
      },

      get isClosedCaptionOn() {
        for (let tt of this.overlayableTextTracks) {
          if (tt.mode === "showing") {
            return true;
          }
        }

        return false;
      },

      setClosedCaptionButtonState() {
        this.closedCaptionButton.toggleAttribute(
          "enabled",
          this.isClosedCaptionOn
        );
        let ttItems = this.textTrackList.childNodes;

        for (let tti of ttItems) {
          const idx = +tti.getAttribute("index");
          tti.setAttribute("aria-checked", idx == this.currentTextTrackIndex);
        }

        this.adjustControlSize();
      },

      addNewTextTrack(tt) {
        if (!this.checkTextTrackSupport(tt)) {
          return;
        }

        if (tt.index && tt.index < this.textTracksCount) {
          // Don't create items for initialized tracks. However, we
          // still need to care about mode since TextTrackManager would
          // turn on the first available track automatically.
          if (tt.mode === "showing") {
            this.changeTextTrack(tt.index);
          }
          return;
        }

        tt.index = this.textTracksCount++;

        const ttBtn = this.shadowRoot.createElementAndAppendChildAt(
          this.textTrackList,
          "button"
        );
        ttBtn.textContent = tt.label || "";

        ttBtn.classList.add("textTrackItem");
        ttBtn.setAttribute("index", tt.index);
        ttBtn.setAttribute("role", "menuitemradio");

        if (tt.mode === "showing" && tt.index) {
          this.changeTextTrack(tt.index);
        }
      },

      changeTextTrack(index) {
        for (let tt of this.overlayableTextTracks) {
          if (tt.index === index) {
            tt.mode = "showing";
          } else {
            tt.mode = "disabled";
          }
        }

        if (!this.textTrackListContainer.hidden) {
          this.toggleClosedCaption();
        }
      },

      onControlBarAnimationFinished() {
        this.hideClosedCaptionMenu();
        this.video.updateCueDisplay();
        // Note this is useful only for testing.
        this.controlBar.dispatchEvent(
          new this.window.CustomEvent("controlbarchange")
        );
        this.adjustControlSize();
      },

      hideClosedCaptionMenu() {
        this.textTrackListContainer.hidden = true;
        this.closedCaptionButton.setAttribute("aria-expanded", "false");
        this.updatePictureInPictureToggleDisplay();
      },

      showClosedCaptionMenu() {
        this.textTrackListContainer.hidden = false;
        this.closedCaptionButton.setAttribute("aria-expanded", "true");
        this.updatePictureInPictureToggleDisplay();
      },

      toggleClosedCaption() {
        if (this.textTrackListContainer.hidden) {
          this.showClosedCaptionMenu();
          if (this.prefs["media.videocontrols.keyboard-tab-to-all-controls"]) {
            // If we're about to hide the controls after focus, prevent that, as
            // that will dismiss the CC menu before the user can use it.
            this.textTrackList.firstChild.focus();
            this.window.clearTimeout(this._hideControlsTimeout);
            this._hideControlsTimeout = 0;
          }
        } else {
          this.hideClosedCaptionMenu();
          // If the CC menu was shown via the keyboard, we may have prevented
          // the controls from hiding. We can now hide them.
          if (
            this.prefs["media.videocontrols.keyboard-tab-to-all-controls"] &&
            !this.controlBar.hidden &&
            // The click-to-play overlay must already be hidden (we don't
            // hide controls when the overlay is visible).
            this.clickToPlay.hidden &&
            // Don't do this if the controls are static.
            this.dynamicControls &&
            // If the mouse is hovering over the control bar, the controls
            // shouldn't hide.
            // We use "div:hover" instead of just ":hover" so this works in
            // quirks mode documents. See
            // https://quirks.spec.whatwg.org/#the-active-and-hover-quirk
            !this.controlBar.matches("div:hover")
          ) {
            this.window.clearTimeout(this._hideControlsTimeout);
            this._hideControlsTimeout = this.window.setTimeout(
              () => this._hideControlsFn(),
              this.HIDE_CONTROLS_TIMEOUT_MS
            );
          }
        }
      },

      onTextTrackAdd(trackEvent) {
        this.addNewTextTrack(trackEvent.track);
        this.setClosedCaptionButtonState();
      },

      onTextTrackRemove(trackEvent) {
        const toRemoveIndex = trackEvent.track.index;
        const ttItems = this.textTrackList.childNodes;

        if (!ttItems) {
          return;
        }

        for (let tti of ttItems) {
          const idx = +tti.getAttribute("index");

          if (idx === toRemoveIndex) {
            tti.remove();
            this.textTracksCount--;
          }

          this.video.dispatchEvent(
            new this.window.CustomEvent("texttrackchange")
          );
        }

        this.setClosedCaptionButtonState();
      },

      initTextTracks() {
        // add 'off' button anyway as new text track might be
        // dynamically added after initialization.
        const offLabel = this.textTrackList.getAttribute("offlabel");
        this.addNewTextTrack({
          label: offLabel,
          kind: "subtitles",
        });

        for (let tt of this.overlayableTextTracks) {
          this.addNewTextTrack(tt);
        }

        this.setClosedCaptionButtonState();
        // Hide the Closed Caption menu when the user moves focus
        this.hideClosedCaptionMenu = this.hideClosedCaptionMenu.bind(this);
        this.closedCaptionButton.addEventListener(
          "focus",
          this.hideClosedCaptionMenu
        );
        this.fullscreenButton.addEventListener(
          "focus",
          this.hideClosedCaptionMenu
        );
      },

      log(msg) {
        if (this.debug) {
          this.window.console.log("videoctl: " + msg + "\n");
        }
      },

      get isTopLevelSyntheticDocument() {
        return (
          this.document.mozSyntheticDocument && this.window === this.window.top
        );
      },

      controlBarMinHeight: 40,
      controlBarMinVisibleHeight: 28,

      reflowTriggeringCallValidator: {
        isReflowTriggeringPropsAllowed: false,
        reflowTriggeringProps: Object.freeze([
          "offsetLeft",
          "offsetTop",
          "offsetWidth",
          "offsetHeight",
          "offsetParent",
          "clientLeft",
          "clientTop",
          "clientWidth",
          "clientHeight",
          "getClientRects",
          "getBoundingClientRect",
        ]),
        get(obj, prop) {
          if (
            !this.isReflowTriggeringPropsAllowed &&
            this.reflowTriggeringProps.includes(prop)
          ) {
            throw new Error("Please don't trigger reflow. See bug 1493525.");
          }
          let val = obj[prop];
          if (typeof val == "function") {
            return function () {
              return val.apply(obj, arguments);
            };
          }
          return val;
        },

        set(obj, prop, value) {
          return Reflect.set(obj, prop, value);
        },
      },

      installReflowCallValidator(element) {
        return new Proxy(element, this.reflowTriggeringCallValidator);
      },

      reflowedDimensions: {
        // Set the dimensions to intrinsic <video> dimensions before the first
        // update.
        // These values are not picked up by <audio> in adjustControlSize()
        // (except for the fact that they are non-zero),
        // it takes controlBarMinHeight and the value below instead.
        videoHeight: 150,
        videoWidth: 300,

        // <audio> takes this width to grow/shrink controls.
        // Match Layout's kFallbackIntrinsicWidthInPixels (300px), which gives
        // <audio controls> its intrinsic width regardless of the controls' size.
        videocontrolsWidth: 300,

        // Used to decide if updating the scrubber progress will make a visible
        // change (ie. make it move by at least one pixel).
        // The default value is set to Infinity so that any small change is
        // assumed to cause a visible change until updateReflowedDimensions
        // has been called. (See bug 1817604)
        scrubberWidth: Infinity,
      },

      updateReflowedDimensions() {
        this.reflowedDimensions.videoHeight = this.video.clientHeight;
        this.reflowedDimensions.videoWidth = this.video.clientWidth;
        this.reflowedDimensions.videocontrolsWidth =
          this.videocontrols.clientWidth;
        this.reflowedDimensions.scrubberWidth = this.scrubber.clientWidth;
      },

      /**
       * adjustControlSize() considers outer dimensions of the <video>/<audio> element
       * from layout, and accordingly, sets/hides the controls, and adjusts
       * the width/height of the control bar.
       *
       * It's important to remember that for <audio>, layout (specifically,
       * nsVideoFrame) rely on us to expose the intrinsic dimensions of the
       * control bar to properly size the <audio> element. We interact with layout
       * by:
       *
       * 1) When the element has a non-zero height, explicitly set the height
       *    of the control bar to a size between controlBarMinHeight and
       *    controlBarMinVisibleHeight in response.
       *    Note: the logic here is flawed and had caused the end height to be
       *    depend on its previous state, see bug 1495817.
       * 2) When the element has a outer width smaller or equal to minControlBarPaddingWidth,
       *    explicitly set the control bar to minRequiredWidth, so that when the
       *    outer width is unset, the audio element could go back to minRequiredWidth.
       *    Otherwise, set the width of the control bar to be the current outer width.
       *    Note: the logic here is also flawed; when the control bar is set to
       *    the current outer width, it never go back when the width is unset,
       *    see bug 1495821.
       */
      adjustControlSize() {
        const minControlBarPaddingWidth = 18;

        this.fullscreenButton.isWanted = !this.controlBar.hasAttribute(
          "fullscreen-unavailable"
        );
        this.closedCaptionButton.isWanted = this.isClosedCaptionAvailable;
        this.volumeStack.isWanted = !this.muteButton.hasAttribute("noAudio");

        let minRequiredWidth = this.prioritizedControls
          .filter(control => control && control.isWanted)
          .reduce(
            (accWidth, cc) => accWidth + cc.minWidth,
            minControlBarPaddingWidth
          );
        // Skip the adjustment in case the stylesheets haven't been loaded yet.
        if (!minRequiredWidth) {
          return;
        }

        let givenHeight = this.reflowedDimensions.videoHeight;
        let videoWidth =
          (this.isAudioOnly
            ? this.reflowedDimensions.videocontrolsWidth
            : this.reflowedDimensions.videoWidth) || minRequiredWidth;
        let videoHeight = this.isAudioOnly
          ? this.controlBarMinHeight
          : givenHeight;
        let videocontrolsWidth = this.reflowedDimensions.videocontrolsWidth;

        let widthUsed = minControlBarPaddingWidth;
        let preventAppendControl = false;

        for (let [index, control] of this.prioritizedControls.entries()) {
          // The "durationSpan" element is disconnected from the document during l10n so
          // we check if our reference to "durationSpan" is the connected one and if not we
          // replace it with the correct one
          if (control.id === "durationSpan" && !control.isConnected) {
            const durationSpan = this.durationSpan;
            if (durationSpan) {
              this.defineControlProperties(durationSpan);
              this.prioritizedControls[index] = durationSpan;
              control = durationSpan;
            }
          }
          if (!control.isWanted) {
            control.hiddenByAdjustment = true;
            continue;
          }

          control.hiddenByAdjustment =
            preventAppendControl || widthUsed + control.minWidth > videoWidth;

          if (control.hiddenByAdjustment) {
            preventAppendControl = true;
          } else {
            widthUsed += control.minWidth;
          }
        }

        // Use flexible spacer to separate controls when scrubber is hidden.
        // As long as muteButton hidden, which means only play button presents,
        // hide spacer and make playButton centered.
        this.controlBarSpacer.hidden =
          !this.scrubberStack.hidden || this.muteButton.hidden;

        // Since the size of videocontrols is expanded with controlBar in <audio>, we
        // should fix the dimensions in order not to recursively trigger reflow afterwards.
        if (this.isAudioTag) {
          if (givenHeight) {
            // The height of controlBar should be capped with the bounds between controlBarMinHeight
            // and controlBarMinVisibleHeight.
            let controlBarHeight = Math.max(
              Math.min(givenHeight, this.controlBarMinHeight),
              this.controlBarMinVisibleHeight
            );
            this.controlBar.style.height = `${controlBarHeight}px`;
          }
          // Bug 1367875: Set minimum required width to controlBar if the given size is smaller than padding.
          // This can help us expand the control and restore to the default size the next time we need
          // to adjust the sizing.
          if (videocontrolsWidth <= minControlBarPaddingWidth) {
            this.controlBar.style.width = `${minRequiredWidth}px`;
          } else {
            this.controlBar.style.width = `${videoWidth}px`;
          }
          return;
        }

        if (
          videoHeight < this.controlBarMinHeight ||
          widthUsed === minControlBarPaddingWidth
        ) {
          this.controlBar.setAttribute("size", "hidden");
          this.controlBar.hiddenByAdjustment = true;
        } else {
          this.controlBar.removeAttribute("size");
          this.controlBar.hiddenByAdjustment = false;
        }

        // Adjust clickToPlayButton size.
        const minVideoSideLength = Math.min(videoWidth, videoHeight);
        const clickToPlayViewRatio = 0.15;
        const clickToPlayScaledSize = Math.max(
          this.clickToPlay.minWidth,
          minVideoSideLength * clickToPlayViewRatio
        );

        if (
          clickToPlayScaledSize >= videoWidth ||
          clickToPlayScaledSize + this.controlBarMinHeight / 2 >=
            videoHeight / 2
        ) {
          this.clickToPlay.hiddenByAdjustment = true;
        } else {
          if (
            this.clickToPlay.hidden &&
            !this.video.played.length &&
            this.video.paused
          ) {
            this.clickToPlay.hiddenByAdjustment = false;
          }
          this.clickToPlay.style.width = `${clickToPlayScaledSize}px`;
          this.clickToPlay.style.height = `${clickToPlayScaledSize}px`;
        }
      },

      get pipToggleEnabled() {
        return (
          this.prefs[
            "media.videocontrols.picture-in-picture.video-toggle.enabled"
          ] && this.prefs["media.videocontrols.picture-in-picture.enabled"]
        );
      },

      get positionDurationBox() {
        return this.shadowRoot.getElementById("positionDurationBox");
      },

      get durationSpan() {
        return this.positionDurationBox?.getElementsByTagName("span")[0];
      },

      init(shadowRoot, prefs) {
        this.shadowRoot = shadowRoot;
        this.video = this.installReflowCallValidator(shadowRoot.host);
        this.videocontrols = this.installReflowCallValidator(
          shadowRoot.firstChild
        );
        this.document = this.videocontrols.ownerDocument;
        this.window = this.document.defaultView;
        this.shadowRoot = shadowRoot;
        this.prefs = prefs;

        this.controlsContainer =
          this.shadowRoot.getElementById("controlsContainer");
        this.statusIcon = this.shadowRoot.getElementById("statusIcon");
        this.controlBar = this.shadowRoot.getElementById("controlBar");
        this.playButton = this.shadowRoot.getElementById("playButton");
        this.controlBarSpacer =
          this.shadowRoot.getElementById("controlBarSpacer");
        this.muteButton = this.shadowRoot.getElementById("muteButton");
        this.volumeStack = this.shadowRoot.getElementById("volumeStack");
        this.volumeControl = this.shadowRoot.getElementById("volumeControl");
        this.progressBar = this.shadowRoot.getElementById("progressBar");
        this.bufferBar = this.shadowRoot.getElementById("bufferBar");
        this.bufferA11yVal = this.shadowRoot.getElementById("bufferA11yVal");
        this.scrubberStack = this.shadowRoot.getElementById("scrubberStack");
        this.scrubber = this.shadowRoot.getElementById("scrubber");
        this.durationLabel = this.shadowRoot.getElementById("durationLabel");
        this.positionLabel = this.shadowRoot.getElementById("positionLabel");
        this.statusOverlay = this.shadowRoot.getElementById("statusOverlay");
        this.controlsOverlay =
          this.shadowRoot.getElementById("controlsOverlay");
        this.pictureInPictureOverlay = this.shadowRoot.getElementById(
          "pictureInPictureOverlay"
        );
        this.controlsSpacer = this.shadowRoot.getElementById("controlsSpacer");
        this.clickToPlay = this.shadowRoot.getElementById("clickToPlay");
        this.fullscreenButton =
          this.shadowRoot.getElementById("fullscreenButton");
        this.closedCaptionButton = this.shadowRoot.getElementById(
          "closedCaptionButton"
        );
        this.textTrackList = this.shadowRoot.getElementById("textTrackList");
        this.textTrackListContainer = this.shadowRoot.getElementById(
          "textTrackListContainer"
        );
        this.pictureInPictureToggle = this.shadowRoot.getElementById(
          "pictureInPictureToggle"
        );

        let isMobile = this.window.navigator.appVersion.includes("Android");
        if (isMobile) {
          this.controlsContainer.classList.add("mobile");
          this.shadowRoot
            .getElementById("pictureInPicture")
            .setAttribute(
              "data-l10n-id",
              "videocontrols-status-picture-in-picture-android"
            );
        }

        // TODO: Switch to touch controls on touch-based desktops (bug 1447547)
        this.isTouchControls = isMobile;
        if (this.isTouchControls) {
          this.controlsContainer.classList.add("touch");
        }

        // XXX: Calling getComputedStyle() here by itself doesn't cause any reflow,
        // but there is no guard proventing accessing any properties and methods
        // of this saved CSSStyleDeclaration instance that could trigger reflow.
        this.controlBarComputedStyles = this.window.getComputedStyle(
          this.controlBar
        );

        // Hide and show control in certain order.
        this.prioritizedControls = [
          this.playButton,
          this.muteButton,
          this.fullscreenButton,
          this.closedCaptionButton,
          this.positionDurationBox,
          this.scrubberStack,
          this.durationSpan,
          this.volumeStack,
        ];

        this.isAudioOnly = this.isAudioTag;
        this.setupInitialState();
        this.setupNewLoadState();
        this.initTextTracks();

        // Use the handleEvent() callback for all media events.
        // Only the "error" event listener must capture, so that it can trap error
        // events from <source> children, which don't bubble. But we use capture
        // for all events in order to simplify the event listener add/remove.
        for (let event of this.videoEvents) {
          this.video.addEventListener(event, this, {
            capture: true,
            mozSystemGroup: true,
          });
        }

        this.controlsEvents = [
          { el: this.muteButton, type: "click" },
          { el: this.closedCaptionButton, type: "click" },
          { el: this.fullscreenButton, type: "click" },
          { el: this.playButton, type: "click" },
          { el: this.clickToPlay, type: "click" },

          // On touch videocontrols, tapping controlsSpacer should show/hide
          // the control bar, instead of playing the video or toggle fullscreen.
          { el: this.controlsSpacer, type: "click", nonTouchOnly: true },
          { el: this.controlsSpacer, type: "dblclick", nonTouchOnly: true },

          { el: this.textTrackList, type: "click" },

          { el: this.videocontrols, type: "resizevideocontrols" },

          { el: this.document, type: "fullscreenchange" },
          { el: this.video, type: "keypress", capture: true },

          // Prevent any click event within media controls from dispatching through to video.
          { el: this.videocontrols, type: "click", mozSystemGroup: false },

          // prevent dragging of controls image (bug 517114)
          { el: this.videocontrols, type: "dragstart" },

          { el: this.scrubber, type: "input" },
          { el: this.scrubber, type: "change" },
          // add mouseup listener additionally to handle the case that `change` event
          // isn't fired when the input value before/after dragging are the same. (bug 1328061)
          { el: this.scrubber, type: "mouseup" },
          { el: this.volumeControl, type: "input" },
          { el: this.video.textTracks, type: "addtrack" },
          { el: this.video.textTracks, type: "removetrack" },
          { el: this.video.textTracks, type: "change" },

          { el: this.controlBar, type: "focusin" },
          { el: this.scrubber, type: "mousedown" },
          { el: this.volumeControl, type: "mousedown" },
        ];

        for (let {
          el,
          type,
          nonTouchOnly = false,
          touchOnly = false,
          mozSystemGroup = true,
          capture = false,
        } of this.controlsEvents) {
          if (
            (this.isTouchControls && nonTouchOnly) ||
            (!this.isTouchControls && touchOnly)
          ) {
            continue;
          }
          el.addEventListener(type, this, { mozSystemGroup, capture });
        }

        this.log("--- videocontrols initialized ---");
      },
    };

    this.TouchUtils = {
      videocontrols: null,
      video: null,
      controlsTimer: null,
      controlsTimeout: 5000,

      get visible() {
        return (
          !this.Utils.controlBar.hasAttribute("fadeout") &&
          !this.Utils.controlBar.hidden
        );
      },

      firstShow: false,

      toggleControls() {
        if (!this.Utils.dynamicControls || !this.visible) {
          this.showControls();
        } else {
          this.delayHideControls(0);
        }
      },

      showControls() {
        if (this.Utils.dynamicControls) {
          this.Utils.startFadeIn(this.Utils.controlBar);
          this.delayHideControls(this.controlsTimeout);
        }
      },

      clearTimer() {
        if (this.controlsTimer) {
          this.window.clearTimeout(this.controlsTimer);
          this.controlsTimer = null;
        }
      },

      delayHideControls(aTimeout) {
        this.clearTimer();
        this.controlsTimer = this.window.setTimeout(
          () => this.hideControls(),
          aTimeout
        );
      },

      hideControls() {
        if (!this.Utils.dynamicControls) {
          return;
        }
        this.Utils.startFadeOut(this.Utils.controlBar);
      },

      handleEvent(aEvent) {
        switch (aEvent.type) {
          case "click":
            switch (aEvent.currentTarget) {
              case this.Utils.playButton:
                if (!this.video.paused) {
                  this.delayHideControls(0);
                } else {
                  this.showControls();
                }
                break;
              case this.Utils.muteButton:
                this.delayHideControls(this.controlsTimeout);
                break;
            }
            break;
          case "touchstart":
            this.clearTimer();
            break;
          case "touchend":
            this.delayHideControls(this.controlsTimeout);
            break;
          case "mouseup":
            if (aEvent.originalTarget == this.Utils.controlsSpacer) {
              if (this.firstShow) {
                this.Utils.video.play().catch(() => {
                  /* Do nothing on rejection */
                });
                this.firstShow = false;
              }
              this.toggleControls();
            }

            break;
        }
      },

      terminate() {
        try {
          for (let { el, type, mozSystemGroup = true } of this.controlsEvents) {
            el.removeEventListener(type, this, { mozSystemGroup });
          }
        } catch (ex) {}

        this.clearTimer();
      },

      init(shadowRoot, utils) {
        this.Utils = utils;
        this.videocontrols = this.Utils.videocontrols;
        this.video = this.Utils.video;
        this.document = this.videocontrols.ownerDocument;
        this.window = this.document.defaultView;
        this.shadowRoot = shadowRoot;

        this.controlsEvents = [
          { el: this.Utils.playButton, type: "click" },
          { el: this.Utils.scrubber, type: "touchstart" },
          { el: this.Utils.scrubber, type: "touchend" },
          { el: this.Utils.muteButton, type: "click" },
          { el: this.Utils.controlsSpacer, type: "mouseup" },
        ];

        for (let { el, type, mozSystemGroup = true } of this.controlsEvents) {
          el.addEventListener(type, this, { mozSystemGroup });
        }

        // The first time the controls appear we want to just display
        // a play button that does not fade away. The firstShow property
        // makes that happen. But because of bug 718107 this init() method
        // may be called again when we switch in or out of fullscreen
        // mode. So we only set firstShow if we're not autoplaying and
        // if we are at the beginning of the video and not already playing
        if (
          !this.video.autoplay &&
          this.Utils.dynamicControls &&
          this.video.paused &&
          this.video.currentTime === 0
        ) {
          this.firstShow = true;
        }

        // If the video is not at the start, then we probably just
        // transitioned into or out of fullscreen mode, and we don't want
        // the controls to remain visible. this.controlsTimeout is a full
        // 5s, which feels too long after the transition.
        if (this.video.currentTime !== 0) {
          this.delayHideControls(this.Utils.HIDE_CONTROLS_TIMEOUT_MS);
        }
      },
    };

    this.Utils.init(this.shadowRoot, this.prefs);
    if (this.Utils.isTouchControls) {
      this.TouchUtils.init(this.shadowRoot, this.Utils);
    }
    this._setupEventListeners();
  }

  generateContent() {
    const tabToAllControls =
      this.prefs["media.videocontrols.keyboard-tab-to-all-controls"];
    let template = VideoControlsWidget.parseTemplate(
      this.document,
      tabToAllControls ? "controls-tab-to-all" : "controls",
      `<div class="videocontrols" xmlns="http://www.w3.org/1999/xhtml" role="none">
        <link rel="stylesheet" href="resource://content-accessible/media/videocontrols.css" />
        <link rel="stylesheet" href="resource://content-accessible/media/pipToggle.css" />

        <div id="controlsContainer" class="controlsContainer" role="none">
          <div id="statusOverlay" class="statusOverlay stackItem" hidden="true">
            <div id="statusIcon" class="statusIcon"></div>
            <bdi class="statusLabel" id="errorAborted" data-l10n-id="videocontrols-error-aborted"></bdi>
            <bdi class="statusLabel" id="errorNetwork" data-l10n-id="videocontrols-error-network"></bdi>
            <bdi class="statusLabel" id="errorDecode" data-l10n-id="videocontrols-error-decode"></bdi>
            <bdi class="statusLabel" id="errorSrcNotSupported" data-l10n-id="videocontrols-error-src-not-supported"></bdi>
            <bdi class="statusLabel" id="errorNoSource" data-l10n-id="videocontrols-error-no-source"></bdi>
            <bdi class="statusLabel" id="errorGeneric" data-l10n-id="videocontrols-error-generic"></bdi>
          </div>

          <div id="pictureInPictureOverlay" class="pictureInPictureOverlay stackItem" status="pictureInPicture" hidden="true">
            <div class="statusIcon" type="pictureInPicture"></div>
            <bdi class="statusLabel" id="pictureInPicture" data-l10n-id="videocontrols-status-picture-in-picture"></bdi>
          </div>

          <div id="controlsOverlay" class="controlsOverlay stackItem" role="none">
            <div class="controlsSpacerStack">
              <div id="controlsSpacer" class="controlsSpacer stackItem" role="none"></div>
              <button id="clickToPlay" class="clickToPlay" hidden="true"></button>
            </div>

            <button id="pictureInPictureToggle" class="pip-wrapper" position="left" hidden="true">
              <div class="pip-small clickable"></div>
              <div class="pip-expanded clickable">
                <span class="pip-icon-label clickable">
                  <span class="pip-icon"></span>
                  <span class="pip-label" data-l10n-id="videocontrols-picture-in-picture-toggle-label2"></span>
                </span>
                <div class="pip-explainer clickable" data-l10n-id="videocontrols-picture-in-picture-explainer3"></div>
              </div>
              <div class="pip-icon clickable"></div>
            </button>

            <div id="controlBar" class="controlBar" role="none" hidden="true">
              <button id="playButton"
                      class="button playButton"
                      tabindex="-1"/>
              <div id="scrubberStack" class="scrubberStack progressContainer" role="none">
                <div class="progressBackgroundBar stackItem" role="none">
                  <div class="progressStack" role="none">
                    <progress id="bufferBar" class="bufferBar" value="0" max="100" aria-hidden="true"></progress>
                    <span class="a11y-only" role="status" aria-live="off">
                      <span data-l10n-id="videocontrols-buffer-bar-label"></span>
                      <span id="bufferA11yVal"></span>
                    </span>
                    <progress id="progressBar" class="progressBar" value="0" max="100" aria-hidden="true"></progress>
                  </div>
                </div>
                <input type="range" id="scrubber" class="scrubber" tabindex="-1" data-l10n-attrs="aria-valuetext" value="0"/>
              </div>
              <bdi id="positionLabel" class="positionLabel" role="presentation"></bdi>
              <bdi id="durationLabel" class="durationLabel" role="presentation"></bdi>
              <bdi id="positionDurationBox" class="positionDurationBox" aria-hidden="true">
                <span id="durationSpan" class="duration" role="none"
                      data-l10n-name="position-duration-format"></span>
              </bdi>
              <div id="controlBarSpacer" class="controlBarSpacer" hidden="true" role="none"></div>
              <button id="muteButton"
                      class="button muteButton"
                      tabindex="-1"/>
              <div id="volumeStack" class="volumeStack progressContainer" role="none">
                <input type="range" id="volumeControl" class="volumeControl" min="0" max="100" step="1" tabindex="-1"
                       data-l10n-id="videocontrols-volume-control"/>
              </div>
              <button id="closedCaptionButton" class="button closedCaptionButton" aria-controls="textTrackList"
                      aria-haspopup="menu" aria-expanded="false" data-l10n-id="videocontrols-closed-caption-button"/>
              <div id="textTrackListContainer" class="textTrackListContainer" hidden="true" role="presentation">
                <div id="textTrackList" role="menu" class="textTrackList"
                     data-l10n-id="videocontrols-closed-caption-off" data-l10n-attrs="offlabel"/>
              </div>
              <button id="fullscreenButton"
                      class="button fullscreenButton"/>
            </div>
          </div>
        </div>
      </div>`,
      template => {
        if (tabToAllControls) {
          // Make all of the individual controls tabbable.
          for (const el of template.querySelectorAll('[tabindex="-1"]')) {
            el.removeAttribute("tabindex");
          }
        }
      }
    );
    this.l10n = new this.window.DOMLocalization(
      ["branding/brand.ftl", "toolkit/global/videocontrols.ftl"],
      true
    );
    this.l10n.connectRoot(this.shadowRoot);
    this.shadowRoot.importNodeAndAppendChildAt(this.shadowRoot, template, true);
    this.l10n.translateRoots();
  }

  onchange() {
    this.Utils.updatePictureInPictureMessage();
    this.Utils.updateVolumeControls();
    this.shadowRoot.firstChild.removeAttribute("flipped");
  }

  teardown() {
    this.Utils.terminate();
    this.TouchUtils.terminate();
    this.l10n.disconnectRoot(this.shadowRoot);
    this.l10n = null;
  }

  onPrefChange(prefName, prefValue) {
    this.prefs[prefName] = prefValue;
    this.Utils.updatePictureInPictureToggleDisplay();
  }

  _setupEventListeners() {
    this.shadowRoot.firstChild.addEventListener("mouseover", event => {
      if (!this.Utils.isTouchControls) {
        this.Utils.onMouseInOut(event);
      }
    });

    this.shadowRoot.firstChild.addEventListener("mouseout", event => {
      if (!this.Utils.isTouchControls) {
        this.Utils.onMouseInOut(event);
      }
    });

    this.shadowRoot.firstChild.addEventListener("mousemove", event => {
      if (!this.Utils.isTouchControls) {
        this.Utils.onMouseMove(event);
      }
    });
  }
};

this.NoControlsMobileImplWidget = class {
  constructor(shadowRoot) {
    this.shadowRoot = shadowRoot;
    this.element = shadowRoot.host;
    this.document = this.element.ownerDocument;
    this.window = this.document.defaultView;
  }

  onsetup(direction) {
    this.generateContent();

    this.shadowRoot.firstElementChild.setAttribute("localedir", direction);

    this.Utils = {
      videoEvents: ["play", "playing"],
      videoControlEvents: ["MozNoControlsBlockedVideo"],
      terminate() {
        for (let event of this.videoEvents) {
          try {
            this.video.removeEventListener(event, this, {
              capture: true,
              mozSystemGroup: true,
            });
          } catch (ex) {}
        }

        for (let event of this.videoControlEvents) {
          try {
            this.videocontrols.removeEventListener(event, this);
          } catch (ex) {}
        }

        try {
          this.clickToPlay.removeEventListener("click", this, {
            mozSystemGroup: true,
          });
        } catch (ex) {}
      },

      hasError() {
        return (
          this.video.error != null ||
          this.video.networkState == this.video.NETWORK_NO_SOURCE
        );
      },

      handleEvent(aEvent) {
        switch (aEvent.type) {
          case "play":
            this.noControlsOverlay.hidden = true;
            break;
          case "playing":
            this.noControlsOverlay.hidden = true;
            break;
          case "MozNoControlsBlockedVideo":
            this.blockedVideoHandler();
            break;
          case "click":
            this.clickToPlayClickHandler(aEvent);
            break;
        }
      },

      blockedVideoHandler() {
        if (this.hasError()) {
          this.noControlsOverlay.hidden = true;
          return;
        }
        this.noControlsOverlay.hidden = false;
      },

      clickToPlayClickHandler(e) {
        if (e.button != 0) {
          return;
        }

        this.noControlsOverlay.hidden = true;
        this.video.play().catch(() => {
          /* Do nothing on rejection */
        });
      },

      init(shadowRoot) {
        this.shadowRoot = shadowRoot;
        this.video = shadowRoot.host;
        this.videocontrols = shadowRoot.firstChild;
        this.document = this.videocontrols.ownerDocument;
        this.window = this.document.defaultView;
        this.shadowRoot = shadowRoot;

        this.controlsContainer =
          this.shadowRoot.getElementById("controlsContainer");
        this.clickToPlay = this.shadowRoot.getElementById("clickToPlay");
        this.noControlsOverlay =
          this.shadowRoot.getElementById("controlsContainer");

        let isMobile = this.window.navigator.appVersion.includes("Android");
        if (isMobile) {
          this.controlsContainer.classList.add("mobile");
        }

        // TODO: Switch to touch controls on touch-based desktops (bug 1447547)
        this.isTouchControls = isMobile;
        if (this.isTouchControls) {
          this.controlsContainer.classList.add("touch");
        }

        this.clickToPlay.addEventListener("click", this, {
          mozSystemGroup: true,
        });

        for (let event of this.videoEvents) {
          this.video.addEventListener(event, this, {
            capture: true,
            mozSystemGroup: true,
          });
        }

        for (let event of this.videoControlEvents) {
          this.videocontrols.addEventListener(event, this);
        }
      },
    };
    this.Utils.init(this.shadowRoot);
  }

  onchange() {}

  teardown() {
    this.Utils.terminate();
  }

  onPrefChange(prefName, prefValue) {
    this.prefs[prefName] = prefValue;
  }

  generateContent() {
    let template = VideoControlsWidget.parseTemplate(
      this.document,
      "nocontrols-mobile",
      `<div class="videocontrols" xmlns="http://www.w3.org/1999/xhtml" role="none">
        <link rel="stylesheet" href="resource://content-accessible/media/videocontrols.css" />
        <div id="controlsContainer" class="controlsContainer" role="none" hidden="true">
          <div class="controlsOverlay stackItem">
            <div class="controlsSpacerStack">
              <button id="clickToPlay" class="clickToPlay"></button>
            </div>
          </div>
        </div>
      </div>`
    );
    this.shadowRoot.importNodeAndAppendChildAt(this.shadowRoot, template, true);
  }
};

this.NoControlsPictureInPictureImplWidget = class {
  constructor(shadowRoot, prefs) {
    this.shadowRoot = shadowRoot;
    this.prefs = prefs;
    this.element = shadowRoot.host;
    this.document = this.element.ownerDocument;
    this.window = this.document.defaultView;
  }

  onsetup(direction) {
    this.generateContent();

    this.shadowRoot.firstElementChild.setAttribute("localedir", direction);
  }

  onchange() {}

  teardown() {}

  onPrefChange(prefName, prefValue) {
    this.prefs[prefName] = prefValue;
  }

  generateContent() {
    let template = VideoControlsWidget.parseTemplate(
      this.document,
      "nocontrols-pip",
      `<div class="videocontrols" xmlns="http://www.w3.org/1999/xhtml" role="none">
        <link rel="stylesheet" href="resource://content-accessible/media/videocontrols.css" />
        <div id="controlsContainer" class="controlsContainer" role="none">
          <div class="pictureInPictureOverlay stackItem" status="pictureInPicture">
            <div id="statusIcon" class="statusIcon" type="pictureInPicture"></div>
            <bdi class="statusLabel" id="pictureInPicture" data-l10n-id="videocontrols-status-picture-in-picture"></bdi>
          </div>
          <div class="controlsOverlay stackItem"></div>
        </div>
      </div>`
    );
    this.shadowRoot.importNodeAndAppendChildAt(this.shadowRoot, template, true);
    let isMobile = this.window.navigator.appVersion.includes("Android");
    if (isMobile) {
      this.shadowRoot
        .getElementById("pictureInPicture")
        .setAttribute(
          "data-l10n-id",
          "videocontrols-status-picture-in-picture-android"
        );
    }
    this.l10n = new this.window.DOMLocalization([
      "branding/brand.ftl",
      "toolkit/global/videocontrols.ftl",
    ]);
    this.l10n.connectRoot(this.shadowRoot);
    this.l10n.translateRoots();
  }
};

this.NoControlsDesktopImplWidget = class {
  constructor(shadowRoot, prefs) {
    this.shadowRoot = shadowRoot;
    this.element = shadowRoot.host;
    this.document = this.element.ownerDocument;
    this.window = this.document.defaultView;
    this.prefs = prefs;
  }

  onsetup(direction) {
    this.generateContent();

    this.shadowRoot.firstElementChild.setAttribute("localedir", direction);

    this.Utils = {
      handleEvent(event) {
        switch (event.type) {
          case "fullscreenchange": {
            if (this.document.fullscreenElement) {
              this.videocontrols.setAttribute("inDOMFullscreen", true);
            } else {
              this.videocontrols.removeAttribute("inDOMFullscreen");
            }
            break;
          }
          case "resizevideocontrols": {
            this.updateReflowedDimensions();
            this.updatePictureInPictureToggleDisplay();
            break;
          }
          case "durationchange":
          // Intentional fall-through
          case "emptied":
          // Intentional fall-through
          case "loadedmetadata": {
            this.updatePictureInPictureToggleDisplay();
            break;
          }
        }
      },

      updatePictureInPictureToggleDisplay() {
        if (
          this.pipToggleEnabled &&
          VideoControlsWidget.shouldShowPictureInPictureToggle(
            this.prefs,
            this.video,
            this.reflowedDimensions
          )
        ) {
          this.pictureInPictureToggle.hidden = false;
          VideoControlsWidget.setupToggle(
            this.prefs,
            this.pictureInPictureToggle,
            this.reflowedDimensions
          );
        } else {
          this.pictureInPictureToggle.hidden = true;
        }
      },

      init(shadowRoot, prefs) {
        this.shadowRoot = shadowRoot;
        this.prefs = prefs;
        this.video = shadowRoot.host;
        this.videocontrols = shadowRoot.firstChild;
        this.document = this.videocontrols.ownerDocument;
        this.window = this.document.defaultView;
        this.shadowRoot = shadowRoot;

        this.pictureInPictureToggle = this.shadowRoot.getElementById(
          "pictureInPictureToggle"
        );

        if (this.document.fullscreenElement) {
          this.videocontrols.setAttribute("inDOMFullscreen", true);
        }

        // Default the Picture-in-Picture toggle button to being hidden. We might unhide it
        // later if we determine that this video is qualified to show it.
        this.pictureInPictureToggle.hidden = true;

        if (this.video.readyState >= this.video.HAVE_METADATA) {
          // According to the spec[1], at the HAVE_METADATA (or later) state, we know
          // the video duration and dimensions, which means we can calculate whether or
          // not to show the Picture-in-Picture toggle now.
          //
          // [1]: https://www.w3.org/TR/html50/embedded-content-0.html#dom-media-have_metadata
          this.updatePictureInPictureToggleDisplay();
        }

        this.document.addEventListener("fullscreenchange", this, {
          capture: true,
        });

        this.video.addEventListener("emptied", this);
        this.video.addEventListener("loadedmetadata", this);
        this.video.addEventListener("durationchange", this);
        this.videocontrols.addEventListener("resizevideocontrols", this);
      },

      terminate() {
        this.document.removeEventListener("fullscreenchange", this, {
          capture: true,
        });

        this.video.removeEventListener("emptied", this);
        this.video.removeEventListener("loadedmetadata", this);
        this.video.removeEventListener("durationchange", this);
        this.videocontrols.removeEventListener("resizevideocontrols", this);
      },

      updateReflowedDimensions() {
        this.reflowedDimensions.videoHeight = this.video.clientHeight;
        this.reflowedDimensions.videoWidth = this.video.clientWidth;
        this.reflowedDimensions.videocontrolsWidth =
          this.videocontrols.clientWidth;
      },

      reflowedDimensions: {
        // Set the dimensions to intrinsic <video> dimensions before the first
        // update.
        videoHeight: 150,
        videoWidth: 300,
        videocontrolsWidth: 0,
      },

      get pipToggleEnabled() {
        return (
          this.prefs[
            "media.videocontrols.picture-in-picture.video-toggle.enabled"
          ] && this.prefs["media.videocontrols.picture-in-picture.enabled"]
        );
      },
    };
    this.Utils.init(this.shadowRoot, this.prefs);
  }

  onchange() {}

  teardown() {
    this.Utils.terminate();
  }

  onPrefChange(prefName, prefValue) {
    this.prefs[prefName] = prefValue;
    this.Utils.updatePictureInPictureToggleDisplay();
  }

  generateContent() {
    let template = VideoControlsWidget.parseTemplate(
      this.document,
      "nocontrols-desktop",
      `<div class="videocontrols" xmlns="http://www.w3.org/1999/xhtml" role="none">
        <link rel="stylesheet" href="resource://content-accessible/media/videocontrols.css" />
        <link rel="stylesheet" href="resource://content-accessible/media/pipToggle.css" />

        <div id="controlsContainer" class="controlsContainer" role="none">
          <div class="controlsOverlay stackItem">
            <button id="pictureInPictureToggle" class="pip-wrapper" position="left" hidden="true">
              <div class="pip-small clickable"></div>
              <div class="pip-expanded clickable">
                <span class="pip-icon-label clickable">
                  <span class="pip-icon"></span>
                  <span class="pip-label" data-l10n-id="videocontrols-picture-in-picture-toggle-label2"></span>
                </span>
                <div class="pip-explainer clickable" data-l10n-id="videocontrols-picture-in-picture-explainer3"></div>
              </div>
              <div class="pip-icon"></div>
            </button>
          </div>
        </div>
      </div>`
    );
    this.shadowRoot.importNodeAndAppendChildAt(this.shadowRoot, template, true);
    this.l10n = new this.window.DOMLocalization([
      "branding/brand.ftl",
      "toolkit/global/videocontrols.ftl",
    ]);
    this.l10n.connectRoot(this.shadowRoot);
    this.l10n.translateRoots();
  }
};
