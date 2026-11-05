/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  TabStateFlusher:
    "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs",
  MiniWindowUtils:
    "moz-src:///browser/components/miniwindow/MiniWindowUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "prefs", () => {
  let prefs = {};
  XPCOMUtils.defineLazyPreferenceGetter(
    prefs,
    "hideDelayMs",
    "browser.mini-window.toolbar.hide-delay-ms",
    1000
  );
  XPCOMUtils.defineLazyPreferenceGetter(
    prefs,
    "edgeZonePx",
    "browser.mini-window.toolbar.edge-zone-px",
    12
  );
  XPCOMUtils.defineLazyPreferenceGetter(
    prefs,
    "hoverRevealDelayMs",
    "browser.mini-window.toolbar.hover-reveal-delay-ms",
    50
  );
  return prefs;
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    logModule: "MiniWindow",
    maxLogLevel: Services.prefs.getBoolPref("browser.mini-window.log", false)
      ? "Debug"
      : "Error",
  })
);

export const MiniWindowState = {
  OPENING: "opening",
  FRAMED: "framed",
  ACTIVE: "active",
  RESTORING: "restoring",
  CLOSING: "closing",
  CLOSED: "closed",
};

const WINDOW_EVENTS = ["unload", "activate"];

const TOOLBAR_HIDE_DELAY_FIRST_OPEN_MS = 2500;

/**
 * One always-on-top popup hosting a live moved tab. The tab keeps its
 * browsing context across the move, so the page is never reloaded, and
 * teardown adopts it back into its origin window rather than discarding it.
 */
export class MiniWindow {
  /**
   * The tab being popped out of its origin window into this popup.
   *
   * @type {MozTabbrowserTab}
   */
  #sourceTab;

  /**
   * The index #sourceTab held in originWin.
   *
   * @type {number}
   */
  #originTabIndex;

  /**
   * Whether this mini window frames a cropped region rather than the whole tab.
   *
   * @type {boolean}
   */
  #cropped;

  /**
   * Session-history listener that returns the tab home when it navigates away
   * from the cropped page.
   *
   * @type {nsISHistoryListener|null}
   */
  #historyListener = null;

  /**
   * URI the tab showed when the crop was taken. Snapshot, not live currentURI.
   *
   * @type {nsIURI|null}
   */
  #tabURI = null;

  /**
   * The session history the history listener was attached to.
   *
   * @type {nsISHistory|null}
   */
  #historyListenerSH = null;

  /**
   * Reveals the collapsed toolbar when the user hovers near the top of
   * the window.
   *
   * @type {object|null}
   */
  #edgeListener = null;

  /**
   * Set when a scroll down hides the toolbar, so that a pointer already
   * resting in the top edge doesn't immediately bring it back. Cleared when
   * the pointer leaves the edge.
   *
   * @type {boolean}
   */
  #edgeSuppressed = false;

  /**
   * Pending timer that re-hides the nav-bar after it's revealed.
   *
   * @type {number|null}
   */
  #hideToolbarTimer = null;

  /**
   * Pending timer between the pointer reaching the top edge and the toolbar
   * revealing.
   *
   * @type {number|null}
   */
  #hoverRevealTimer = null;

  /**
   * Reveals the toolbar when the popped tab navigates or reloads.
   *
   * @type {object|null}
   */
  #progressListener = null;

  /**
   * Keeps the page's inset matched to the bar's height as it changes.
   *
   * @type {ResizeObserver|null}
   */
  #toolbarHeightObserver = null;

  /**
   * Aborts all the toolbox listeners at once on teardown.
   *
   * @type {AbortController|null}
   */
  #abortController = null;

  /**
   * When this mini window was created, for the closed event's duration.
   *
   * @type {number}
   */
  #createdAt = ChromeUtils.now();

  /**
   * How this mini window ended; see metrics.yaml. Set by whichever teardown
   * path runs first, and read by uninit().
   *
   * @type {string|null}
   */
  #closeMethod = null;

  /**
   * @param {object} manager - The MiniWindowManager singleton.
   * @param {ChromeWindow} originWin - The tab's source window.
   * @param {MozTabbrowserTab} sourceTab - The tab to move.
   * @param {object|null} [cropInfo] - The page region to frame, in content CSS
   *   px. Carries left/top/width/height plus the viewport size and fullZoom it
   *   was captured against; see MiniWindowUtils. Null makes a full-tab mini
   *   window.
   */
  constructor(manager, originWin, sourceTab, cropInfo = null) {
    this.manager = manager;
    this.originWin = originWin;
    this.#sourceTab = sourceTab;
    this.#cropped = !!cropInfo;
    let browser = sourceTab.linkedBrowser;
    if (cropInfo) {
      this._cropInfo = cropInfo;
    } else {
      // A full-tab mini window frames no crop, so width/height here are just
      // the size to open at.
      let { width, height } = lazy.MiniWindowUtils.fullTabSize();
      this._cropInfo = {
        left: 0,
        top: 0,
        width,
        height,
        viewportWidth: browser.clientWidth,
        viewportHeight: browser.clientHeight,
        fullZoom: browser.fullZoom,
      };
    }
    this.#originTabIndex = originWin.gBrowser.tabs.indexOf(sourceTab);
    this.miniWin = null;
    this._state = MiniWindowState.OPENING;
  }

  /**
   * The current state of the Mini Window, one of MiniWindowState.
   *
   * @returns {string}
   */
  get state() {
    return this._state;
  }

  /**
   * Whether this Mini Window frames a cropped region rather than the whole tab.
   *
   * @returns {boolean}
   */
  get isCropped() {
    return this.#cropped;
  }

  /**
   * The browser for the current Mini Window, or undefined before the popup
   * window exists.
   *
   * @returns {XULBrowserElement|undefined}
   */
  get browser() {
    return this.miniWin?.gBrowser.selectedBrowser;
  }

  /**
   * The tab that this Mini Window holds, or undefined before the popup window
   * exists.
   *
   * @returns {MozTabbrowserTab|undefined}
   */
  get tab() {
    return this.miniWin?.gBrowser?.selectedTab;
  }

  /**
   * Moves the source tab out into a new always-on-top popup and frames the
   * crop in it.
   *
   * @returns {Promise<DOMWindow|null>} Resolves once the popup has finished
   *   delayed startup, been framed and reached state ACTIVE.
   *   Resolves with null without waiting if the tab could not be moved out,
   *   in which case the Mini Window is left CLOSED.
   */
  async open() {
    lazy.logConsole.debug("open", { crop: this._cropInfo });
    let gBrowser = this.originWin.gBrowser;
    let rect = lazy.MiniWindowUtils.computeWindowRect(
      this.originWin,
      this._cropInfo
    );

    // Pop the tab out like a normal move-to-window.

    let features = {
      alwaysontop: 1,
      lockaspectratio: this.#cropped ? 1 : 0,
      replaceLastTab: true,
      outerWidth: rect.width,
      outerHeight: rect.height,
      screenX: rect.left,
      screenY: rect.top,
    };

    // Mark the tab being moved out to make sure styling is correct.
    this.#sourceTab.setAttribute("mini-window", "true");
    if (this.#cropped) {
      this.#sourceTab.setAttribute("cropped-mini-window", "true");
    }

    this.miniWin = gBrowser.replaceTabWithWindow(this.#sourceTab, features);

    if (!this.miniWin) {
      lazy.logConsole.error(
        "open: Something went horribly wrong when opening \
          the mini window - replaceTabWithWindow returned null"
      );
      this.#sourceTab.removeAttribute("mini-window");
      this.#sourceTab.removeAttribute("cropped-mini-window");
      this._state = MiniWindowState.CLOSED;
      return null;
    }

    await lazy.BrowserUtils.promiseObserved(
      "browser-delayed-startup-finished",
      subject => subject == this.miniWin
    );
    if (this.#cropped) {
      // Only a crop needs the framing and the wheel panning that substitutes
      // for scrolling.
      this.#frame();
    }
    this._state = MiniWindowState.FRAMED;
    lazy.logConsole.debug("open: state -> FRAMED");

    this.#avoidOtherWindows();

    this.#abortController = new AbortController();
    this.#wireNavBarButtons();
    this.#wireToolbarReveal();
    this.#restrictShortcuts();

    for (let ev of WINDOW_EVENTS) {
      this.miniWin.addEventListener(ev, this);
    }

    this._state = MiniWindowState.ACTIVE;
    lazy.logConsole.debug("open: state -> ACTIVE");

    // Now that the tab is settled in the mini window, watch for it navigating away.
    this.#attachHistoryListener();
    return this.miniWin;
  }

  /**
   * Nudge the freshly-opened window clear of the other always-on-top windows.
   */
  #avoidOtherWindows() {
    if (!this.miniWin || this.miniWin.closed) {
      return;
    }
    let avoid = this.manager.windowsToAvoid(this.miniWin);
    let pos = lazy.MiniWindowUtils.repositionToAvoid(this.miniWin, avoid);
    if (pos) {
      this.miniWin.moveTo(pos.left, pos.top);
    }
  }

  /**
   * Watch a cropped mini window's session history. A crop frames one specific
   * page, so any navigation away from it - a real load or an SPA `pushState` -
   * invalidates the crop. `replaceState` and same-page anchors are ignored. This
   * only applies to cropped mini windows.
   */
  #attachHistoryListener() {
    if (!this.#cropped) {
      return;
    }
    let sessionHistory = this.browser?.browsingContext?.sessionHistory;
    if (!sessionHistory) {
      return;
    }
    this.#tabURI = this.browser.currentURI;
    this.#historyListener = {
      OnHistoryNewEntry: aNewURI => {
        if (this._state !== MiniWindowState.ACTIVE) {
          return;
        }
        // Ignore in-page anchors;
        if (this.#tabURI && aNewURI?.equalsExceptRef(this.#tabURI)) {
          return;
        }
        // Defer so we don't tear down while session history is mid-update.
        Services.tm.dispatchToMainThread(() =>
          this.returnToOriginWin(this.#shouldFocusOnReturn(), "navigated_away")
        );
      },
      OnHistoryReload: () => true,
      OnHistoryGotoIndex: () => {
        if (this._state !== MiniWindowState.ACTIVE) {
          return;
        }
        Services.tm.dispatchToMainThread(() =>
          this.returnToOriginWin(this.#shouldFocusOnReturn(), "navigated_away")
        );
      },
      OnHistoryPurge() {},
      OnHistoryTruncate() {},
      OnHistoryReplaceEntry() {},
      OnHistoryCommit() {},
      QueryInterface: ChromeUtils.generateQI([
        "nsISHistoryListener",
        "nsISupportsWeakReference",
      ]),
    };
    sessionHistory.addSHistoryListener(this.#historyListener);
    this.#historyListenerSH = sessionHistory;
  }

  /**
   * Bring the returned tab to the front only if the user was using the mini
   * window; a page navigating while they're elsewhere shouldn't steal focus.
   *
   * @returns {boolean}
   */
  #shouldFocusOnReturn() {
    return Services.focus.activeWindow === this.miniWin;
  }

  #detachHistoryListener() {
    if (!this.#historyListener) {
      return;
    }
    try {
      // Detach from the session history the listener was attached to
      this.#historyListenerSH?.removeSHistoryListener(this.#historyListener);
    } catch (e) {
      lazy.logConsole.error("#detachHistoryListener failed", e);
    }
    this.#historyListener = null;
    this.#historyListenerSH = null;
    this.#tabURI = null;
  }

  /**
   * Restrict shortcuts allowed in the miniWin.
   */
  #restrictShortcuts() {
    const ALLOWED_KEYS = new Set([
      "key_close",
      "key_reload",
      "key_reload2",
      "key_reload_skip_cache",
      "key_reload_skip_cache2",
      "key_closeWindow",
      "key_toggleMute",
    ]);
    const ALLOWED_COMMANDS = new Set([
      "cmd_fullZoomReduce",
      "cmd_fullZoomEnlarge",
      "cmd_fullZoomReset",
    ]);
    // A crop frames one specific page, so history navigation only makes sense
    // in a full-tab mini window.
    if (!this.#cropped) {
      for (let id of ["goBackKb", "goBackKb2", "goForwardKb", "goForwardKb2"]) {
        ALLOWED_KEYS.add(id);
      }
    }
    let keyset = this.miniWin.document.getElementById("mainKeyset");
    for (let key of keyset ? [...keyset.children] : []) {
      if (
        key.localName === "key" &&
        !ALLOWED_KEYS.has(key.id) &&
        !ALLOWED_COMMANDS.has(key.getAttribute("command"))
      ) {
        key.setAttribute("disabled", "true");
      }
    }

    const CLOSE_COMMAND_METHODS = {
      cmd_close: "kbd_close_tab",
      cmd_closeWindow: "kbd_close_window",
    };
    let { signal } = this.#abortController;
    for (let [id, method] of Object.entries(CLOSE_COMMAND_METHODS)) {
      this.miniWin.document.getElementById(id)?.addEventListener(
        "command",
        () => {
          this.#closeMethod = method;
        },
        { signal }
      );
    }
  }

  /**
   * Reveal and wire the two mini-window controls that live in the reused
   * real nav-bar.
   */
  #wireNavBarButtons() {
    let doc = this.miniWin.document;
    let { signal } = this.#abortController;
    let closeButton = doc.getElementById("mini-window-close-button");
    let restoreButton = doc.getElementById("mini-window-restore-button");

    closeButton?.removeAttribute("hidden");
    restoreButton?.removeAttribute("hidden");
    closeButton?.addEventListener("command", () => this.close(), { signal });
    restoreButton?.addEventListener(
      "command",
      () => this.returnToOriginWin(true),
      { signal }
    );

    this.#wireAudioButton();
  }

  /**
   * Mirror the tab's sound state onto the nav-bar's mute button.
   *
   */
  #wireAudioButton() {
    let { signal } = this.#abortController;
    let audioButton = this.miniWin.document.getElementById(
      "mini-window-audio-button"
    );
    let tab = this.tab;
    if (!audioButton || !tab) {
      return;
    }

    let sync = () => {
      audioButton.toggleAttribute(
        "soundplaying",
        tab.hasAttribute("soundplaying")
      );
      let muted = tab.hasAttribute("muted");
      audioButton.toggleAttribute("muted", muted);
      audioButton.toggleAttribute("checked", muted);
      audioButton.setAttribute(
        "data-l10n-id",
        muted ? "mini-window-audio-unmute" : "mini-window-audio-mute"
      );
    };

    // The tab can already be playing when it is popped out, so start from its
    // current state rather than waiting for the next change.
    sync();
    tab.addEventListener("TabAttrModified", sync, { signal });
    audioButton.addEventListener("command", () => tab.toggleMuteAudio(), {
      signal,
    });
  }

  /**
   * The reused nav-bar collapses away to stay out of the way of the framed
   * crop. Putting the pointer in a strip along the top edge reveals it. Every
   * reveal hides again after an idle delay.
   */
  #wireToolbarReveal() {
    let { signal } = this.#abortController;
    let toolbox = this.miniWin.gNavToolbox;

    let win = this.miniWin;
    this.#edgeListener = {
      getMouseTargetRect() {
        // getBoundsWithoutFlushing so a mouse move never forces a reflow.
        let { width } = win.windowUtils.getBoundsWithoutFlushing(
          win.document.documentElement
        );
        return {
          top: 0,
          bottom: lazy.prefs.edgeZonePx,
          left: 0,
          right: width,
        };
      },
      onMouseEnter: () => {
        // A scroll down dismissed the bar while the pointer was already at the
        // top edge; don't hand it straight back.
        if (this.#edgeSuppressed) {
          return;
        }
        this.#scheduleHoverReveal();
      },
      onMouseLeave: () => {
        this.#edgeSuppressed = false;
        this.#cancelHoverReveal();
        if (toolbox.classList.contains("mini-window-revealed")) {
          this.#startHideCountdown();
        }
      },
    };
    win.MousePosTracker.addListener(this.#edgeListener);

    this.#trackToolbarHeight(toolbox);

    // While the pointer or keyboard focus is on the toolbar, CSS holds it open
    let onToolbarLeave = event => {
      if (!toolbox.contains(event.relatedTarget)) {
        this.revealToolbar();
      }
    };
    toolbox.addEventListener("mouseout", onToolbarLeave, { signal });
    toolbox.addEventListener("focusout", onToolbarLeave, { signal });

    // Show the toolbar on navigation or reload, then let it idle away
    // A navigation also swaps in a fresh content actor, so scroll reporting
    // has to be re-armed.
    this.#progressListener = {
      onLocationChange: () => {
        this.revealToolbar();
        this.#enableScrollReveal();
      },
    };
    this.miniWin.gBrowser.addTabsProgressListener(this.#progressListener);
    this.#enableScrollReveal();

    // A doorhanger or an infobar needs the toolbar it is anchored to.
    toolbox.addEventListener("AlertActive", () => this.revealToolbar(), {
      signal,
    });

    this.#revealForFirstOpen();
  }

  /**
   * The bar should already be on screen when the mini window appears, rather
   * than sliding in after it. Suppress the transition for the frame in which
   * it is revealed, then hand animation back for every later reveal.
   */
  #revealForFirstOpen() {
    let root = this.miniWin.document.documentElement;
    root.setAttribute("mini-window-first-open", "true");
    this.revealToolbar(TOOLBAR_HIDE_DELAY_FIRST_OPEN_MS);
    // Two frames: the first still has the suppressing attribute applied, so
    // removing it any earlier would let the reveal animate after all.
    this.miniWin.requestAnimationFrame(() => {
      this.miniWin?.requestAnimationFrame(() => {
        root.removeAttribute("mini-window-first-open");
      });
    });
  }

  /**
   * The bar overlays the page, so the page has to move down by exactly the
   * bar's height or the bar covers content.
   *
   * @param {Element} toolbox
   */
  #trackToolbarHeight(toolbox) {
    let setHeight = height => {
      if (!height) {
        return;
      }

      this.miniWin?.document.documentElement.style.setProperty(
        "--mini-window-toolbar-height",
        `${height}px`
      );
    };

    // getBoundsWithoutFlushing never forces a flush, so this can be stale
    setHeight(
      this.miniWin.windowUtils.getBoundsWithoutFlushing(toolbox).height
    );

    this.#toolbarHeightObserver = new this.miniWin.ResizeObserver(entries => {
      setHeight(entries.at(-1)?.borderBoxSize?.[0]?.blockSize);
    });
    this.#toolbarHeightObserver.observe(toolbox);
  }

  /**
   * Scrolling down dismisses the toolbar at once.
   */
  hideToolbarOnScrollDown() {
    this.#hideToolbar();
    this.#edgeSuppressed = true;
  }

  /**
   * Whether something in the toolbar still needs it on screen, checked each
   * time the idle countdown comes due. A hold has no "released" signal to
   * listen for - notifications in particular only announce their arrival.
   *
   * @returns {boolean}
   */
  #toolbarHeld() {
    let doc = this.miniWin?.document;
    if (!doc) {
      return false;
    }
    // An open doorhanger, or an infobar in the toolbar's notification box.
    return !!(
      doc.getElementById("notification-popup")?.state === "open" ||
      doc
        .getElementById("notifications-toolbar")
        ?.querySelector("notification-message")
    );
  }

  /** Ask the content actor to report upward scrolls. */
  #enableScrollReveal() {
    this.#getActor()?.sendAsyncMessage("EnableScrollReveal");
  }

  /**
   * Show the toolbar and (re)start the countdown that hides it again.
   *
   * @param {number} hideDelay - how long the toolbar stays up.
   */
  revealToolbar(hideDelay = lazy.prefs.hideDelayMs) {
    this.#keepToolbarShown();
    this.#startHideCountdown(hideDelay);
  }

  /**
   * (Re)start the countdown that hides the toolbar.
   *
   * @param {number} hideDelay - how long until the toolbar hides.
   */
  #startHideCountdown(hideDelay = lazy.prefs.hideDelayMs) {
    this.#clearHideTimer();
    this.#hideToolbarTimer = this.miniWin.setTimeout(() => {
      if (this.#toolbarHeld()) {
        // Wait out another interval rather than hiding something the user is
        // still being asked about.
        this.#startHideCountdown(hideDelay);
        return;
      }
      this.#hideToolbar();
    }, hideDelay);
  }

  /** Show the toolbar with no hide countdown (something needs it open). */
  #keepToolbarShown() {
    this.#clearHideTimer();
    this.miniWin?.gNavToolbox?.classList.add("mini-window-revealed");
  }

  /** Hide the toolbar right away, dropping any pending countdown or dwell. */
  #hideToolbar() {
    this.#clearHideTimer();
    this.#cancelHoverReveal();
    this.miniWin?.gNavToolbox?.classList.remove("mini-window-revealed");
  }

  #clearHideTimer() {
    if (this.#hideToolbarTimer == null) {
      return;
    }

    this.miniWin.clearTimeout(this.#hideToolbarTimer);
    this.#hideToolbarTimer = null;
  }

  /**
   * Reveal after a short dwell on the top edge, so grazing it doesn't flash
   * the toolbar. Held open while the pointer stays in the zone.
   */
  #scheduleHoverReveal() {
    if (this.miniWin?.gNavToolbox?.classList.contains("mini-window-revealed")) {
      this.#keepToolbarShown();
      return;
    }
    if (this.#hoverRevealTimer === null) {
      this.#hoverRevealTimer = this.miniWin.setTimeout(() => {
        this.#hoverRevealTimer = null;
        this.#keepToolbarShown();
      }, lazy.prefs.hoverRevealDelayMs);
    }
  }

  #cancelHoverReveal() {
    if (this.#hoverRevealTimer == null) {
      // there is no hover reveal timer running
      return;
    }

    this.miniWin.clearTimeout(this.#hoverRevealTimer);
    this.#hoverRevealTimer = null;
  }

  #unwireToolbarReveal() {
    // The timers, the progress listener and the hover zone need explicit
    // cleanup.
    this.#clearHideTimer();
    this.#cancelHoverReveal();
    this.#toolbarHeightObserver?.disconnect();
    this.#toolbarHeightObserver = null;
    if (this.#progressListener) {
      this.miniWin?.gBrowser?.removeTabsProgressListener(
        this.#progressListener
      );
      this.#progressListener = null;
    }
    if (this.#edgeListener) {
      this.miniWin?.MousePosTracker.removeListener(this.#edgeListener);
      this.#edgeListener = null;
    }
  }

  /**
   * @param {Event} event - A WINDOW_EVENTS event from the popup window.
   */
  handleEvent(event) {
    switch (event.type) {
      case "unload":
        this.#onUnload();
        break;
      case "activate":
        if (this._state === MiniWindowState.ACTIVE) {
          this.miniWin.setTimeout(
            () => this.revealToolbar(),
            lazy.prefs.hoverRevealDelayMs
          );
        }
        break;
    }
  }

  /**
   * Frame the crop with the native APZ zoom. The content keeps laying out at the
   * viewport size the crop was captured against, so the page never reflows,
   * while its visual viewport follows the popup. The content then keeps the
   * visual viewport zoomed onto the crop as the popup resizes.
   *
   */
  #frame() {
    let actor = this.#getActor();
    if (!actor) {
      lazy.logConsole.warn(
        "No MiniWindow actor available; can't zoom the crop"
      );
      return;
    }

    this.#setWindowMinSize();
    actor.sendAsyncMessage("ApzFrame", this.#apzFrameData());
    lazy.logConsole.debug("#frame", { crop: this._cropInfo });
  }

  /**
   * @returns {object} The ApzFrame message data, in content CSS px: the crop,
   *   and the layout viewport size to hold the page at.
   */
  #apzFrameData() {
    let cropInfo = this._cropInfo;
    return {
      crop: {
        left: cropInfo.left,
        top: cropInfo.top,
        width: cropInfo.width,
        height: cropInfo.height,
      },
      layoutWidth: cropInfo.innerWidth || cropInfo.viewportWidth,
      layoutHeight: cropInfo.innerHeight || cropInfo.viewportHeight,
    };
  }

  /**
   * @returns {JSWindowActorParent|undefined} The moved tab's MiniWindow
   *   actor, or undefined if its window global is gone.
   */
  #getActor() {
    return this.browser?.browsingContext?.currentWindowGlobal?.getActor(
      "MiniWindow"
    );
  }

  /**
   * Hold the popup at a crop-aspect minimum size via a CSS min-width/min-height
   * on its root element, so it can't be resized off the crop's aspect.
   */
  #setWindowMinSize() {
    let { width, height } = this._cropInfo;
    if (!(width > 0) || !(height > 0)) {
      return;
    }

    const FLOOR_WIDTH = 100;
    const FLOOR_HEIGHT = 100;
    let aspect = width / height;
    let minWidth = Math.max(FLOOR_WIDTH, Math.round(FLOOR_HEIGHT * aspect));
    let minHeight = Math.round(minWidth / aspect);
    let root = this.miniWin.document.documentElement;
    root.style.minWidth = `${minWidth}px`;
    root.style.minHeight = `${minHeight}px`;
  }

  /**
   * When unloading, we want to preserve the tab in the originWin's history. Instead of
   * closing, we send it back home (originWin).
   */
  #onUnload() {
    if (
      this._state !== MiniWindowState.RESTORING &&
      this._state !== MiniWindowState.CLOSING &&
      this._state !== MiniWindowState.CLOSED
    ) {
      lazy.logConsole.debug("#onUnload: falling through to returnToOriginWin");
      this.returnToOriginWin(false, "unknown");
    }
  }

  /**
   * Close the popup and discard the tab. We need to return the tab to its originWin
   * so that sessionStore can remember it in the originWin's history.
   *
   * This must only be invoked when the popped window is still alive - the unload path
   * should use #onUnload instead. Use returnToOriginWin to return the tab
   * instead of closing it.
   *
   * Returns as soon as the state is CLOSING, without waiting: the tab state
   * flush, the adopt back into originWin and the teardown all happen
   * afterwards, so the popup is still open when this returns.
   *
   * @param {string} [method] - how this was reached; see metrics.yaml.
   */
  close(method = "close_button") {
    lazy.logConsole.debug("close", { state: this._state, method });
    if (
      this._state === MiniWindowState.CLOSED ||
      this._state === MiniWindowState.CLOSING
    ) {
      return;
    }
    this._state = MiniWindowState.CLOSING;
    this.#closeMethod = method;

    this.#detachHistoryListener();
    this.#abortController?.abort();
    this.#unwireToolbarReveal();

    let flushed = this.tab
      ? lazy.TabStateFlusher.flush(this.tab.linkedBrowser)
      : Promise.resolve();
    flushed
      .then(() => {
        let { tab: adopted, win } = this.#returnTabToOrigin(false);
        if (adopted && win && !win.closed) {
          win.gBrowser.removeTab(adopted, { animate: false });
        }
      })
      .catch(e => lazy.logConsole.error("close: flush/adopt failed", e))
      .finally(() => this.uninit());
  }

  /**
   * Put the tab back into its original window and tear the mini window down.
   *
   * @param {boolean} focus - whether to focus the tab in originWin.
   * @param {string} [method] - how this was reached; see metrics.yaml.
   */
  returnToOriginWin(focus, method = "put_tab_back") {
    lazy.logConsole.debug("returnToOriginWin", {
      state: this._state,
      focus,
      method,
    });
    if (
      this._state !== MiniWindowState.OPENING &&
      this._state !== MiniWindowState.FRAMED &&
      this._state !== MiniWindowState.ACTIVE
    ) {
      return;
    }
    this._state = MiniWindowState.RESTORING;
    this.#closeMethod ??= method;
    let { tab: adopted, win: targetWin } = this.#returnTabToOrigin(focus);
    this.uninit();
    // Focus wherever the tab actually landed (originWin, or the fallback
    // window if originWin already closed).
    if (focus && adopted && targetWin && !targetWin.closed) {
      targetWin.focus();
      // The page might've navigated without a user gesture - make sure the
      // user is aware where it went.
      targetWin.getAttention();
    }
  }

  /**
   * Adopt the popup's tab back into originWin at its remembered original index.
   *
   * @param {boolean} selectTab - whether to focus the tab in the target window.
   * @returns {{tab: MozTabbrowserTab|null, win: Window|null}} the adopted tab
   *   and the window it landed in, or nulls if it could not be adopted.
   */
  #returnTabToOrigin(selectTab) {
    // Undo the framing (viewport override and zoom) before the tab goes home, or
    // it stays framed there.
    this.#getActor()?.sendAsyncMessage("ResetApzFrame");
    let tab = this.tab;
    let { originWin } = this;
    let target = originWin && !originWin.closed ? originWin : null;
    // Only a user-initiated restore falls back to another window when the
    // origin is already gone; automatic teardown (origin close, quit) lets
    // the tab die with its window as usual.
    if (!target && selectTab) {
      target = lazy.BrowserWindowTracker.getTopWindow();
    }
    if (!tab || !target) {
      return { tab: null, win: null };
    }
    let tabIndex = Math.min(this.#originTabIndex, target.gBrowser.tabs.length);
    return {
      tab: target.gBrowser.adoptTab(tab, { tabIndex, selectTab }),
      win: target,
    };
  }

  /**
   * Remove any event listeners and close the window (if it already hasn't),
   * then unregister from the manager. Safe to call more than once.
   */
  uninit() {
    lazy.logConsole.debug("uninit", { state: this._state });
    if (this._state === MiniWindowState.CLOSED) {
      lazy.logConsole.debug("uninit: already closed, ignoring");
      return;
    }

    try {
      let flavour = this.#cropped ? "fragment" : "full_tab";
      let openDuration = Math.round(ChromeUtils.now() - this.#createdAt);
      Glean.miniWindow.closed.record({
        type: flavour,
        method: this.#closeMethod ?? "unknown",
        duration_ms: openDuration,
      });
      Glean.miniWindow.openDuration[flavour].accumulateSingleSample(
        openDuration
      );
    } catch (e) {
      lazy.logConsole.error("uninit: recording telemetry failed", e);
    }

    // A throw during teardown must not strand this popup registered;
    try {
      this.#detachHistoryListener();
      this.#abortController?.abort();
      this.#unwireToolbarReveal();

      for (let ev of WINDOW_EVENTS) {
        this.miniWin?.removeEventListener(ev, this);
      }
    } catch (e) {
      lazy.logConsole.error("uninit: teardown failed", e);
    } finally {
      try {
        if (this.miniWin && !this.miniWin.closed) {
          this.miniWin.close();
        }
      } catch (e) {
        lazy.logConsole.error("uninit: closing the popup failed", e);
      }
      this._state = MiniWindowState.CLOSED;
      this.manager._unregister(this);
      this.miniWin = null;
    }
  }
}
