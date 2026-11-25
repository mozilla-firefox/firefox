/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

interface URI;
interface nsIDocShell;
interface nsIDOMGeoPosition;
interface nsISecureBrowserUI;
interface nsISHEntry;
interface nsIScopedPrefs;
interface nsIPrintSettings;
interface nsIWebProgress;

interface mixin LoadContextMixin {
  readonly attribute WindowProxy? associatedWindow;

  readonly attribute WindowProxy? topWindow;

  readonly attribute Element? topFrameElement;

  readonly attribute boolean isContent;

  [SetterThrows]
  attribute boolean usePrivateBrowsing;

  readonly attribute boolean useRemoteTabs;

  readonly attribute boolean useRemoteSubframes;

  [BinaryName="useTrackingProtectionWebIDL", SetterThrows]
  attribute boolean useTrackingProtection;

  [NewObject, Throws]
  readonly attribute any originAttributes;
};

/**
 * Allowed CSS display modes. This needs to be kept in
 * sync with similar values in ServoStyleConsts.h
 */
enum DisplayMode {
  "browser",
  "minimal-ui",
  "standalone",
  "fullscreen",
  "picture-in-picture"
};

/**
 * CSS prefers-color-scheme values.
 */
enum PrefersColorSchemeOverride {
  "none",
  "light",
  "dark",
};

/**
 * CSS forced-colors values.
 */
enum ForcedColorsOverride {
  "none",
  "active",
};

/**
 * CSS prefers-reduced-motion values override.
 */
enum PrefersReducedMotionOverride {
  "none",
  "reduce",
  "no-preference",
};

/**
 * CSS prefers-contrast values override.
 */
enum PrefersContrastOverride {
  "none",
  "no-preference",
  "more",
  "less",
  "custom",
};

/**
 * Allowed overrides of platform/pref default behaviour for touch events.
 */
enum TouchEventsOverride {
  "disabled", // Force-disable touch events.
  "enabled", // Force-enable touch events.
  "none", // Don't override behaviour for touch events.
};

[Exposed=Window, ChromeOnly]
interface BrowsingContext {
  static BrowsingContext? get(unsigned long long aId);

  static BrowsingContext? getFromWindow(WindowProxy window);

  static BrowsingContext? getCurrentTopByBrowserId(unsigned long long aId);

  sequence<BrowsingContext> getAllBrowsingContextsInSubtree();

  readonly attribute DOMString name;

  readonly attribute BrowsingContext? parent;

  readonly attribute BrowsingContext top;

  [Cached, Frozen, Pure]
  readonly attribute sequence<BrowsingContext> children;

  readonly attribute nsIDocShell? docShell;

  readonly attribute Element? embedderElement;

  readonly attribute unsigned long long id;

  readonly attribute BrowsingContext? opener;

  readonly attribute BrowsingContextGroup group;

  readonly attribute WindowProxy? window;

  readonly attribute WindowContext? currentWindowContext;

  readonly attribute WindowContext? parentWindowContext;

  readonly attribute WindowContext? topWindowContext;

  readonly attribute boolean ancestorsAreCurrent;

  [SetterThrows] attribute [LegacyNullToEmptyString] DOMString customPlatform;

  [SetterThrows] attribute [LegacyNullToEmptyString] UTF8String customUserAgent;

  readonly attribute DOMString embedderElementType;

  readonly attribute boolean createdDynamically;

  readonly attribute boolean isInBFCache;

  readonly attribute boolean isDiscarded;

  /**
   * The sandbox flags on the browsing context. These reflect the value of the
   * sandbox attribute of the associated IFRAME or CSP-protectable content, if
   * existent. See the HTML5 spec for more details.
   * These flags on the browsing context reflect the current state of the
   * sandbox attribute, which is modifiable. They are only used when loading new
   * content, sandbox flags are also immutably set on the document when it is
   * loaded.
   * The sandbox flags of a document depend on the sandbox flags on its
   * browsing context and of its parent document, if any.
   * See nsSandboxFlags.h for the possible flags.
   */
  [SetterThrows] attribute unsigned long sandboxFlags;

  /**
   * Whether the BrowsingContext is active. That is, whether it's in a
   * foreground tab, and not minimized or fully occluded otherwise.
   */
  readonly attribute boolean isActive;

  /**
   * When set to true all channels in this browsing context or its children will report navigator.onLine = false,
   * and HTTP requests created from these browsing context will fail with NS_ERROR_OFFLINE.
   */
  [SetterThrows] attribute boolean forceOffline;

  /**
   * Sets whether this is an app tab. Non-same-origin link navigations from app
   * tabs may be forced to open in new contexts, rather than in the same context.
   */
  [SetterThrows] attribute boolean isAppTab;

  /**
   * Whether this top level browsing context is shown in a Mini Window. Web
   * content may not move or resize such a window.
   */
  [SetterThrows] attribute boolean isMiniWindow;

  /**
   * Sets whether this is BC has siblings **at the toplevel** (e.g. in a tabbed
   * browser environment). Used to determine if web content can resize the top
   * window. Never set correctly for non-top BCs.
   */
  [SetterThrows] attribute boolean hasSiblings;

  // The inRDMPane flag indicates whether or not Responsive Design Mode is
  // active for the browsing context.
  [SetterThrows] attribute boolean inRDMPane;

  [SetterThrows] attribute float fullZoom;

  [SetterThrows] attribute float textZoom;

  // Override the dots-per-CSS-pixel scaling factor in this BrowsingContext
  // and all of its descendants. May only be set on the top BC, and should
  // only be set from the parent process.
  //
  // A value of 0.0 causes us to use the global default scaling factor.
  //
  // NOTE that this override only affects a few minor things (the value exposed
  // to devicePixelRatio and some media queries in content, and responsive
  // image selection). Most notably, it does _not_ affect rendering.
  //
  // It is intended for RDM, and is probably not what you want in other cases.
  // If you want to change the actual device pixel ratio that rendering code
  // uses, you probably want to change the fullZoom.
  [SetterThrows] attribute float overrideDPPX;

  [SetterThrows] attribute boolean suspendMediaWhenInactive;

  // Default value for nsIDocumentViewer::authorStyleDisabled in any new
  // browsing contexts created as a descendant of this one.
  //
  // Valid only for top browsing contexts.
  [SetterThrows] attribute boolean authorStyleDisabledDefault;

  /**
   * Whether this docshell should save entries in global history.
   */
  [SetterThrows] attribute boolean useGlobalHistory;

  // Extension to give chrome JS the ability to set the window screen
  // dimensions override with WebDriver BiDi.
  [Throws] undefined setScreenAreaOverride(unsigned long long screenWidth, unsigned long long screenHeight);
  // Extension to give chrome JS the ability to reset the window screen
  // dimensions override with WebDriver BiDi.
  undefined resetScreenAreaOverride();

  // Extension to give chrome JS the ability to set the window screen
  // orientation override with WebDriver BiDi and DevTools.
  [Throws] undefined setOrientationOverride(OrientationType type, float rotationAngle);
  // Extension to give chrome JS the ability to reset the window screen
  // orientation override with WebDriver BiDi and DevTools.
  undefined resetOrientationOverride();

  // Extension to give chrome JS the ability to set a maxTouchPoints override
  // while in RDM.
  [Throws] undefined setRDMPaneMaxTouchPoints(octet maxTouchPoints);

  // Geolocation emulation for WebDriver BiDi and DevTools.
  // Note: in case of watchPosition emulation, the override has to be set
  // before the watcher is started.
  undefined setGeolocationServiceOverride(optional nsIDOMGeoPosition position);

  // The watchedByDevTools flag indicates whether or not DevTools are currently
  // debugging this browsing context.
  [SetterThrows] attribute boolean watchedByDevTools;

  // Enable some service workers testing features, for DevTools.
  [SetterThrows] attribute boolean serviceWorkersTestingEnabled;

  // Enable media query medium override, for DevTools.
  [SetterThrows] attribute DOMString mediumOverride;

  // Language emulation for WebDriver BiDi and DevTools. Settable only from the
  // parent process on a top-level browsing context, to a structurally valid
  // BCP 47 language tag; the empty string resets the override.
  [SetterThrows] attribute UTF8String languageOverride;

  // Timezone emulation for WebDriver BiDi and DevTools per browsing context.
  [SetterThrows] attribute DOMString timezoneOverride;

  // Color-scheme simulation, for DevTools.
  [SetterThrows] attribute PrefersColorSchemeOverride prefersColorSchemeOverride;

  // Reduced-motion simulation, for DevTools.
  [SetterThrows] attribute PrefersReducedMotionOverride prefersReducedMotionOverride;

  // Forced-colors simulation, for DevTools
  [SetterThrows] attribute ForcedColorsOverride forcedColorsOverride;

  // Contrast simulation, for DevTools.
  [SetterThrows] attribute PrefersContrastOverride prefersContrastOverride;

  // Animation playbackRate multiplier, for Devtools
  [SetterThrows] attribute double animationsPlayBackRateMultiplier;

  /**
   * A unique identifier for the browser element that is hosting this
   * BrowsingContext tree. Every BrowsingContext in the element's tree will
   * return the same ID in all processes and it will remain stable regardless of
   * process changes. When a browser element's frameloader is switched to
   * another browser element this ID will remain the same but hosted under the
   * under the new browser element.
   */
  [SetterThrows] attribute unsigned long long browserId;

  [SetterThrows] attribute DisplayMode displayMode;

  /**
   * This allows chrome to override the default choice of whether touch events
   * are available in a specific BrowsingContext and its descendents.
   */
  readonly attribute TouchEventsOverride touchEventsOverride;

  /**
   * Returns true if the top-level BrowsingContext has been configured to
   * default-target user-initiated link clicks to _blank.
   */
  readonly attribute boolean targetTopLevelLinkClicksToBlank;

  /**
   * Partially determines whether script execution is allowed in this
   * BrowsingContext. Script execution will be permitted only if this
   * attribute is true and script execution is allowed in the parent
   * WindowContext.
   *
   * May only be set in the parent process.
   */
  [SetterThrows] attribute boolean allowJavascript;

  /**
   * Determines whether we're forcing a desktop-mode viewport. Only settable in
   * the top browsing context from the parent process.
   */
  [SetterThrows] attribute boolean forceDesktopViewport;

  /*
   * Default load flags (as defined in nsIRequest) that will be set on all
   * requests made by this BrowsingContext.
   */
  [SetterThrows] attribute long defaultLoadFlags;

  /**
   * The nsID of the browsing context in the session history.
   */
  [NewObject, Throws]
  readonly attribute any historyID;

  readonly attribute ChildSHistory? childSessionHistory;

  // Resets the location change rate limit. Used for testing.
  undefined resetNavigationRateLimit();

  readonly attribute long childOffset;

  // https://wicg.github.io/document-picture-in-picture/
  // This is true both for the top-level BC of the content and chrome window
  // of a Document Picture-in-Picture window.
  [BinaryName="GetIsDocumentPiP"]
  readonly attribute boolean isDocumentPiP;
};

BrowsingContext includes LoadContextMixin;

[Exposed=Window, ChromeOnly]
interface CanonicalBrowsingContext : BrowsingContext {
  // Whether enterprise policy has disabled service workers for the top-level site.
  readonly attribute boolean serviceWorkersDisabledByPolicy;

  // Top-level only download folder override for WebDriver BiDi's.
  [SetterThrows] attribute DOMString downloadFolderOverride;

  sequence<WindowGlobalParent> getWindowGlobals();

  readonly attribute WindowGlobalParent? currentWindowGlobal;

  readonly attribute WindowProxy? topChromeWindow;

  // XXX(nika): This feels kinda hacky, but will do for now while we don't
  // synchronously create WindowGlobalParent. It can throw if somehow the
  // content process has died.
  [Throws]
  readonly attribute UTF8String? currentRemoteType;

  readonly attribute WindowGlobalParent? embedderWindowGlobal;

  undefined notifyStartDelayedAutoplayMedia();

  readonly attribute nsISecureBrowserUI? secureBrowserUI;

  /**
   * Returns an nsIWebProgress object for this BrowsingContext, if this
   * is a top-level content BC.
   *
   * Progress listeners attached to this will get notifications filtered by
   * nsBrowserStatusFilter, and don't get any notifications from sub frames.
   */
  readonly attribute nsIWebProgress? webProgress;

  static unsigned long countSiteOrigins(sequence<BrowsingContext> roots);

  /**
   * Loads a given URI.  This will give priority to loading the requested URI
   * in the object implementing this interface.  If it can't be loaded here
   * however, the URI dispatcher will go through its normal process of content
   * loading.
   *
   * @param aURI
   *        The URI to load.  No fixup will be performed on this URI.
   * @param aLoadURIOptions
   *        A JSObject defined in LoadURIOptions.webidl holding info like e.g.
   *        the triggeringPrincipal, the referrer info.
   */
  [Throws]
  undefined loadURI(URI aURI, optional LoadURIOptions aOptions = {});

  /**
   * Like `loadURI` but takes a DOMString instead. This will use nsIURIFixup
   * to "fix up" the input if it doesn't parse as a string. If an existing
   * DOM URL or nsIURI object is available to you, prefer using `loadURI`
   * directly.
   *
   * @param aURI
   *        The URI to load.  For HTTP and FTP URLs and possibly others,
   *        characters above U+007F will be converted to UTF-8 and then URL-
   *        escaped per the rules of RFC 2396.
   * @param aLoadURIOptions
   *        A JSObject defined in LoadURIOptions.webidl holding info like e.g.
   *        the triggeringPrincipal, the referrer info.
   */
  [Throws]
  undefined fixupAndLoadURIString(DOMString aURI, optional LoadURIOptions aOptions = {});

   /**
    * Print the current document.
    *
    * @param aOuterWindowID the ID of the outer window to print
    * @param aPrintSettings print settings to use; printSilent can be
    *                       set to prevent prompting.
    * @return A Promise that resolves once printing is finished.
    */
  [NewObject, BinaryName="printJS"]
  Promise<undefined> print(nsIPrintSettings aPrintSettings);

  /**
   * These methods implement the nsIWebNavigation methods of the same names
   */
  undefined goBack(optional long aCancelContentJSEpoch, optional boolean aRequireUserInteraction = false, optional boolean aUserActivation = false);
  undefined goForward(optional long aCancelContentJSEpoch, optional boolean aRequireUserInteraction  = false, optional boolean aUserActivation = false);
  undefined goToIndex(long aIndex, optional long aCancelContentJSEpoch, optional boolean aUserActivation = false);
  undefined reload(unsigned long aReloadFlags);
  undefined stop(unsigned long aStopFlags);

  readonly attribute nsISHistory? sessionHistory;
  readonly attribute nsISHEntry? activeSessionHistoryEntry;

  readonly attribute MediaController? mediaController;

  undefined resetScalingZoom();

  // The current URI loaded in this BrowsingContext according to nsDocShell.
  // This may not match the current window global's document URI in some cases.
  readonly attribute URI? currentURI;

  undefined clearRestoreState();

  // Force this browsing context, which must correspond to an app window, to
  // be active regardless of the window being minimized or fully occluded.
  [SetterThrows] attribute boolean forceAppWindowActive;

  // @see BrowsingContext.isActive.
  [SetterThrows] attribute boolean isActive;

  /**
   * This allows chrome to override the default choice of whether touch events
   * are available in a specific BrowsingContext and its descendents.
   */
  [SetterThrows] inherit attribute TouchEventsOverride touchEventsOverride;

  /**
   * Set to true to configure the top-level BrowsingContext to default-target
   * user-initiated link clicks to _blank.
   */
  [SetterThrows] inherit attribute boolean targetTopLevelLinkClicksToBlank;

  /**
   * Set the cross-group opener of this BrowsingContext. This tracks the opener
   * of a browsing context regardless if that context is opened using noopener.
   *
   * This value will be automatically set for documents created using
   * `window.open`.
   */
  [SetterThrows]
  attribute CanonicalBrowsingContext? crossGroupOpener;

  readonly attribute boolean isReplaced;


  /**
   * Notify APZ to start autoscrolling.
   *
   * (aAnchorX, aAnchorY) are the coordinates of the autoscroll anchor, in
   *                      device coordinates relative to the screen.
   * aScrollId and aPresShellId identify the scroll frame that content chose to
   *                            scroll.
   *
   * Returns whether we were successfully able to notify APZ.
   * If this function returns true, APZ (which may live in another process) may
   * still reject the autoscroll, but it's then APZ's responsibility to notify
   * content via an "autoscroll-rejected-by-apz" message.
   */
  boolean startApzAutoscroll(float aAnchorX, float aAnchorY,
                             unsigned long long aScrollId,
                             unsigned long aPresShellId);

  /**
   * Notify APZ to stop autoscrolling.
   */
  undefined stopApzAutoscroll(unsigned long long aScrollId,
                              unsigned long aPresShellId);

  readonly attribute nsISHEntry? mostRecentLoadingSessionHistoryEntry;

  /**
   * Prefs that are stored in the top-level browsing context which persist for
   * the lifetime of the tab
   */
  readonly attribute nsIScopedPrefs? scopedPrefs;

  /**
   * Indicates if the embedder element or an ancestor has hidden
   * visibility, or no frame.
   */
  readonly attribute boolean isUnderHiddenEmbedderElement;

  /**
   * Indicates whether opening a modal picker is permitted.
   */
  readonly attribute boolean canOpenModalPicker;
};

[Exposed=Window, ChromeOnly]
interface BrowsingContextGroup {
  sequence<BrowsingContext> getToplevels();

  readonly attribute unsigned long long id;
};
