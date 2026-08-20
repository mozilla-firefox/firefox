/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  AIWindowAccountAuth:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowAccountAuth.sys.mjs",
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  FirstStartup: "resource://gre/modules/FirstStartup.sys.mjs",
  HeadlessShell: "moz-src:///browser/components/shell/HeadlessShell.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  LaterRun: "resource:///modules/LaterRun.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
  SessionStartup:
    "moz-src:///browser/components/sessionstore/SessionStartup.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
  UpdatePing: "resource://gre/modules/UpdatePing.sys.mjs",
});

/**
 * Whether `openBrowserWindow` should open the window as Smart Window: the
 * feature is on and set as default, it's not an explicitly private window, and
 * the user previously signed in (approximated synchronously via the ToS consent
 * timestamp; the async FxA check happens later in `onFirstWindowReady`, which
 * prompts sign-in if they've since signed out). It deliberately avoids
 * `SessionStartup.willRestore()`/`sessionType`: called this early, before the
 * session file is read, that getter memoizes NO_SESSION and SessionStore then
 * discards the session it was about to restore. Restore keeps the saved window
 * type (`restoreWindowFeatures`); Smart-by-default only forces brand-new
 * windows. See bug 2052953.
 *
 * @param {boolean} forcePrivate
 * @returns {boolean}
 */
function canOpenAsSmartWindow(forcePrivate = false) {
  return (
    !forcePrivate &&
    lazy.AIWindow.shouldOpenAsSmartWindow() &&
    lazy.AIWindowAccountAuth.hasToSConsent
  );
}

XPCOMUtils.defineLazyServiceGetters(lazy, {
  UpdateManager: ["@mozilla.org/updates/update-manager;1", Ci.nsIUpdateManager],
  WinTaskbar: ["@mozilla.org/windows-taskbar;1", Ci.nsIWinTaskbar],
  WindowsUIUtils: ["@mozilla.org/windows-ui-utils;1", Ci.nsIWindowsUIUtils],
});

ChromeUtils.defineLazyGetter(lazy, "gSystemPrincipal", () =>
  Services.scriptSecurityManager.getSystemPrincipal()
);

ChromeUtils.defineLazyGetter(lazy, "gWindowsAlertsService", () => {
  // We might not have the Windows alerts service: e.g., on Windows 7 and Windows 8.
  if (!("nsIWindowsAlertsService" in Ci)) {
    return null;
  }
  return Cc["@mozilla.org/system-alerts-service;1"]
    ?.getService(Ci.nsIAlertsService)
    ?.QueryInterface(Ci.nsIWindowsAlertsService);
});

// One-time startup homepage override configurations
const ONCE_DOMAINS = new Set(["mozilla.org", "firefox.com"]);
const ONCE_PREF = "browser.startup.homepage_override.once";

// Index of Private Browsing icon in firefox.exe
// Must line up with the one in nsNativeAppSupportWin.h.
const PRIVATE_BROWSING_ICON_INDEX = 5;

function shouldLoadURI(aURI) {
  if (aURI && !aURI.schemeIs("chrome")) {
    return true;
  }

  dump("*** Preventing external load of chrome: URI into browser window\n");
  dump("    Use --chrome <uri> instead\n");
  return false;
}

function resolveURIInternal(aCmdLine, aArgument) {
  let principal = lazy.gSystemPrincipal;
  var uri = aCmdLine.resolveURI(aArgument);
  var uriFixup = Services.uriFixup;

  if (!(uri instanceof Ci.nsIFileURL)) {
    let prefURI = Services.uriFixup.getFixupURIInfo(
      aArgument,
      uriFixup.FIXUP_FLAG_FIX_SCHEME_TYPOS
    ).preferredURI;
    return { uri: prefURI, principal };
  }

  try {
    if (uri.file.exists()) {
      return { uri, principal };
    }
  } catch (e) {
    console.error(e);
  }

  // We have interpreted the argument as a relative file URI, but the file
  // doesn't exist. Try URI fixup heuristics: see bug 290782.

  try {
    uri = Services.uriFixup.getFixupURIInfo(aArgument).preferredURI;
  } catch (e) {
    console.error(e);
  }

  return { uri, principal };
}

let gKiosk = false;
let gMajorUpgrade = false;
let gFirstRunProfile = false;
var gFirstWindow = false;

const OVERRIDE_NONE = 0;
const OVERRIDE_NEW_PROFILE = 1;
const OVERRIDE_NEW_MSTONE = 2;
const OVERRIDE_NEW_BUILD_ID = 3;
/**
 * Determines whether a home page override is needed.
 *
 * @param {boolean} [updateMilestones=true]
 *   True if we should update the milestone prefs after comparing those prefs
 *   with the current platform version and build ID.
 *
 *   If updateMilestones is false, then this function has no side-effects.
 *
 * @returns {number}
 *   One of the following constants:
 *     OVERRIDE_NEW_PROFILE
 *       if this is the first run with a new profile.
 *     OVERRIDE_NEW_MSTONE
 *       if this is the first run with a build with a different Gecko milestone
 *       (i.e. right after an upgrade).
 *     OVERRIDE_NEW_BUILD_ID
 *       if this is the first run with a new build ID of the same Gecko
 *       milestone (i.e. after a nightly upgrade).
 *     OVERRIDE_NONE
 *       otherwise.
 */
function needHomepageOverride(updateMilestones = true) {
  var savedmstone = Services.prefs.getCharPref(
    "browser.startup.homepage_override.mstone",
    ""
  );

  if (savedmstone == "ignore") {
    return OVERRIDE_NONE;
  }

  var mstone = Services.appinfo.platformVersion;

  var savedBuildID = Services.prefs.getCharPref(
    "browser.startup.homepage_override.buildID",
    ""
  );

  var buildID = Services.appinfo.platformBuildID;

  if (mstone != savedmstone) {
    // Bug 462254. Previous releases had a default pref to suppress the EULA
    // agreement if the platform's installer had already shown one. Now with
    // about:rights we've removed the EULA stuff and default pref, but we need
    // a way to make existing profiles retain the default that we removed.
    if (savedmstone) {
      Services.prefs.setBoolPref("browser.rights.3.shown", true);

      // Remember that we saw a major version change.
      gMajorUpgrade = true;
    }

    if (updateMilestones) {
      Services.prefs.setCharPref(
        "browser.startup.homepage_override.mstone",
        mstone
      );
      Services.prefs.setCharPref(
        "browser.startup.homepage_override.buildID",
        buildID
      );
    }
    return savedmstone ? OVERRIDE_NEW_MSTONE : OVERRIDE_NEW_PROFILE;
  }

  if (buildID != savedBuildID) {
    if (updateMilestones) {
      Services.prefs.setCharPref(
        "browser.startup.homepage_override.buildID",
        buildID
      );
    }
    return OVERRIDE_NEW_BUILD_ID;
  }

  return OVERRIDE_NONE;
}

/**
 * Gets the override page for the first run after the application has been
 * updated.
 *
 * @param  update
 *         The nsIUpdate for the update that has been applied.
 * @param  defaultOverridePage
 *         The default override page
 * @param  nimbusOverridePage
 *         Nimbus provided URL
 * @param  disableWnp
 *         Boolean, disables all WNPs if true
 * @return The override page.
 */
function getPostUpdateOverridePage(
  update,
  defaultOverridePage,
  nimbusOverridePage,
  disableWnp
) {
  if (disableWnp) {
    return "";
  }

  update = update.QueryInterface(Ci.nsIWritablePropertyBag);
  let actions = update.getProperty("actions");
  // When the update doesn't specify actions fallback to the original behavior
  // of displaying the default override page.
  if (!actions) {
    return defaultOverridePage;
  }

  // The existence of silent or the non-existence of showURL in the actions both
  // mean that an override page should not be displayed.
  if (actions.includes("silent") || !actions.includes("showURL")) {
    return "";
  }

  // If a policy was set to not allow the update.xml-provided URL to be used,
  // use the default fallback (which will also be provided by the policy).
  if (!Services.policies.isAllowed("postUpdateCustomPage")) {
    return defaultOverridePage;
  }

  if (nimbusOverridePage) {
    return nimbusOverridePage;
  }

  return update.getProperty("openURL") || defaultOverridePage;
}

/**
 * Open a browser window. If this is the initial launch, this function will
 * attempt to use the navigator:blank window opened by BrowserGlue.sys.mjs during
 * early startup.
 *
 * @param cmdLine
 *        The nsICommandLine object given to nsICommandLineHandler's handle
 *        method.
 *        Used to check if we are processing the command line for the initial launch.
 * @param triggeringPrincipal
 *        The nsIPrincipal to use as triggering principal for the page load(s).
 * @param urlOrUrlList (optional)
 *        When omitted, the browser window will be opened with the default
 *        arguments, which will usually load the homepage.
 *        This can be a JS array of urls provided as strings, each url will be
 *        loaded in a tab. postData will be ignored in this case.
 *        This can be a single url to load in the new window, provided as a string.
 *        postData will be used in this case if provided.
 * @param postData (optional)
 *        An nsIInputStream object to use as POST data when loading the provided
 *        url, or null.
 * @param forcePrivate (optional)
 *        Boolean. If set to true, the new window will be a private browsing one.
 *
 * @returns {ChromeWindow}
 *          Returns the top level window opened.
 */
function openBrowserWindow(
  cmdLine,
  triggeringPrincipal,
  urlOrUrlList,
  postData = null,
  forcePrivate = false
) {
  const isStartup =
    cmdLine && cmdLine.state == Ci.nsICommandLine.STATE_INITIAL_LAUNCH;
  const openAsSmart = canOpenAsSmartWindow(forcePrivate);

  let args;
  if (!urlOrUrlList) {
    // Just pass in the defaultArgs directly. We'll use system principal on the other end.
    if (isStartup) {
      args = [gBrowserContentHandler.getFirstWindowArgs()];
    } else {
      args = [gBrowserContentHandler.getNewWindowArgs()];
    }
  } else if (Array.isArray(urlOrUrlList)) {
    // There isn't an explicit way to pass a principal here, so we load multiple URLs
    // with system principal when we get to actually loading them.
    if (
      !triggeringPrincipal ||
      !triggeringPrincipal.equals(lazy.gSystemPrincipal)
    ) {
      throw new Error(
        "Can't open multiple URLs with something other than system principal."
      );
    }
    // Passing an nsIArray for the url disables the "|"-splitting behavior.
    let uriArray = Cc["@mozilla.org/array;1"].createInstance(
      Ci.nsIMutableArray
    );
    urlOrUrlList.forEach(function (uri) {
      var sstring = Cc["@mozilla.org/supports-string;1"].createInstance(
        Ci.nsISupportsString
      );
      sstring.data = uri;
      uriArray.appendElement(sstring);
    });
    args = [uriArray];
  } else {
    let extraOptions = Cc["@mozilla.org/hash-property-bag;1"].createInstance(
      Ci.nsIWritablePropertyBag2
    );
    extraOptions.setPropertyAsBool("fromExternal", true);

    // Always pass at least 3 arguments to avoid the "|"-splitting behavior,
    // ie. avoid the loadOneOrMoreURIs function.
    // Also, we need to pass the triggering principal.
    args = [
      urlOrUrlList,
      extraOptions,
      null, // refererInfo
      postData,
      undefined, // allowThirdPartyFixup; this would be `false` but that
      // needs a conversion. Hopefully bug 1485961 will fix.
      undefined, // user context id
      null, // origin principal
      null, // origin storage principal
      triggeringPrincipal,
    ];
  }

  if (isStartup) {
    let win = gBrowserContentHandler.replaceStartupWindow(args, forcePrivate);
    if (win) {
      return win;
    }
  }

  // We can't provide arguments to openWindow as a JS array.
  if (!urlOrUrlList) {
    let [url] = args;
    let string = Cc["@mozilla.org/supports-string;1"].createInstance(
      Ci.nsISupportsString
    );
    string.data = url;

    // Smart Window needs args as an nsIMutableArray so handleAIWindowOptions can append its attributes
    if (openAsSmart) {
      let array = Cc["@mozilla.org/array;1"].createInstance(Ci.nsIMutableArray);
      array.appendElement(string);
      args = array;
    } else {
      // Single string guaranteed to not contain '|' can simply be wrapped
      // in an nsISupportsString object.
      args = string;
    }
  } else {
    // Otherwise, pass an nsIArray.
    if (args.length > 1) {
      let string = Cc["@mozilla.org/supports-string;1"].createInstance(
        Ci.nsISupportsString
      );
      string.data = args[0];
      args[0] = string;
    }
    let array = Cc["@mozilla.org/array;1"].createInstance(Ci.nsIMutableArray);
    args.forEach(a => {
      array.appendElement(a);
    });
    args = array;
  }

  return lazy.BrowserWindowTracker.openWindow({
    args,
    features: gBrowserContentHandler.getFeatures(cmdLine),
    private: forcePrivate,
    aiWindow: openAsSmart,
  });
}

function openPreferences(cmdLine) {
  openBrowserWindow(cmdLine, lazy.gSystemPrincipal, "about:preferences");
}

async function doSearch(searchText, cmdLine) {
  // XXXbsmedberg: use handURIToExistingBrowser to obey tabbed-browsing
  // preferences, but need nsIBrowserDOMWindow extensions
  // Open the window immediately as BrowserContentHandler needs to
  // be handled synchronously. Then load the search URI when the
  // SearchService has loaded.
  let win = openBrowserWindow(cmdLine, lazy.gSystemPrincipal, "about:blank");
  await lazy.BrowserUtils.promiseObserved(
    "browser-delayed-startup-finished",
    subject => subject == win
  );

  lazy.SearchUIUtils.loadSearch({
    window: win,
    searchText,
    usePrivateWindow:
      lazy.PrivateBrowsingUtils.isInTemporaryAutoStartMode ||
      lazy.PrivateBrowsingUtils.isWindowPrivate(win),
    triggeringPrincipal: lazy.gSystemPrincipal,
    policyContainer: win.gBrowser.selectedBrowser.policyContainer,
    sapSource: "system",
  }).catch(console.error);
}

function spinForLastUpdateInstalled() {
  return spinResolve(lazy.UpdateManager.lastUpdateInstalled());
}

function spinForUpdateInstalledAtStartup() {
  return spinResolve(lazy.UpdateManager.updateInstalledAtStartup());
}

function spinResolve(promise) {
  if (!(promise instanceof Promise)) {
    return promise;
  }
  let done = false;
  let result = null;
  let error = null;
  promise
    .catch(e => {
      error = e;
    })
    .then(r => {
      result = r;
      done = true;
    });

  Services.tm.spinEventLoopUntil(
    "BrowserContentHandler.sys.mjs:BCH_spinResolve",
    () => done
  );
  if (!done) {
    throw new Error("Forcefully exited event loop.");
  } else if (error) {
    throw error;
  } else {
    return result;
  }
}

export function nsBrowserContentHandler() {
  if (!gBrowserContentHandler) {
    gBrowserContentHandler = this;
  }
  return gBrowserContentHandler;
}

nsBrowserContentHandler.prototype = {
  /* nsISupports */
  QueryInterface: ChromeUtils.generateQI([
    "nsICommandLineHandler",
    "nsIBrowserHandler",
    "nsIContentHandler",
    "nsICommandLineValidator",
  ]),

  /* nsICommandLineHandler */
  handle: function bch_handle(cmdLine) {
    if (
      cmdLine.handleFlag("kiosk", false) ||
      cmdLine.handleFlagWithParam("kiosk-monitor", false)
    ) {
      gKiosk = true;
      Glean.browserStartup.kioskMode.set(true);
    }
    if (cmdLine.handleFlag("disable-pinch", false)) {
      let defaults = Services.prefs.getDefaultBranch(null);
      defaults.setBoolPref("apz.allow_zooming", false);
      Services.prefs.lockPref("apz.allow_zooming");
      defaults.setCharPref("browser.gesture.pinch.in", "");
      Services.prefs.lockPref("browser.gesture.pinch.in");
      defaults.setCharPref("browser.gesture.pinch.in.shift", "");
      Services.prefs.lockPref("browser.gesture.pinch.in.shift");
      defaults.setCharPref("browser.gesture.pinch.out", "");
      Services.prefs.lockPref("browser.gesture.pinch.out");
      defaults.setCharPref("browser.gesture.pinch.out.shift", "");
      Services.prefs.lockPref("browser.gesture.pinch.out.shift");
    }
    if (cmdLine.handleFlag("browser", false)) {
      openBrowserWindow(cmdLine, lazy.gSystemPrincipal);
      cmdLine.preventDefault = true;
    }

    var uriparam;
    try {
      while ((uriparam = cmdLine.handleFlagWithParam("new-window", false))) {
        let { uri, principal } = resolveURIInternal(cmdLine, uriparam);
        if (!shouldLoadURI(uri)) {
          continue;
        }
        openBrowserWindow(cmdLine, principal, uri.spec);
        cmdLine.preventDefault = true;
      }
    } catch (e) {
      console.error(e);
    }

    try {
      while ((uriparam = cmdLine.handleFlagWithParam("new-tab", false))) {
        let { uri, principal } = resolveURIInternal(cmdLine, uriparam);
        handURIToExistingBrowser(
          uri,
          Ci.nsIBrowserDOMWindow.OPEN_NEWTAB,
          cmdLine,
          false,
          principal
        );
        cmdLine.preventDefault = true;
      }
    } catch (e) {
      console.error(e);
    }

    var chromeParam = cmdLine.handleFlagWithParam("chrome", false);
    if (chromeParam) {
      // Handle old preference dialog URLs.
      if (
        chromeParam == "chrome://browser/content/pref/pref.xul" ||
        chromeParam == "chrome://browser/content/preferences/preferences.xul"
      ) {
        openPreferences(cmdLine);
        cmdLine.preventDefault = true;
      } else {
        try {
          let { uri: resolvedURI } = resolveURIInternal(cmdLine, chromeParam);
          let isLocal = uri => {
            let localSchemes = new Set(["chrome", "file", "resource"]);
            if (uri instanceof Ci.nsINestedURI) {
              uri = uri.QueryInterface(Ci.nsINestedURI).innerMostURI;
            }
            return localSchemes.has(uri.scheme);
          };
          if (isLocal(resolvedURI)) {
            // If the URI is local, we are sure it won't wrongly inherit chrome privs
            let features = "chrome,dialog=no,all" + this.getFeatures(cmdLine);
            // Provide 1 null argument, as openWindow has a different behavior
            // when the arg count is 0.
            let argArray = Cc["@mozilla.org/array;1"].createInstance(
              Ci.nsIMutableArray
            );
            argArray.appendElement(null);
            Services.ww.openWindow(
              null,
              resolvedURI.spec,
              "_blank",
              features,
              argArray
            );
            cmdLine.preventDefault = true;
          } else {
            dump("*** Preventing load of web URI as chrome\n");
            dump(
              "    If you're trying to load a webpage, do not pass --chrome.\n"
            );
          }
        } catch (e) {
          console.error(e);
        }
      }
    }
    if (cmdLine.handleFlag("preferences", false)) {
      openPreferences(cmdLine);
      cmdLine.preventDefault = true;
    }
    if (cmdLine.handleFlag("silent", false)) {
      cmdLine.preventDefault = true;
    }

    try {
      var privateWindowParam = cmdLine.handleFlagWithParam(
        "private-window",
        false
      );
      if (privateWindowParam) {
        let forcePrivate = true;
        let resolvedInfo;
        if (!lazy.PrivateBrowsingUtils.enabled) {
          // Load about:privatebrowsing in a normal tab, which will display an error indicating
          // access to private browsing has been disabled.
          forcePrivate = false;
          resolvedInfo = {
            uri: Services.io.newURI("about:privatebrowsing"),
            principal: lazy.gSystemPrincipal,
          };
        } else {
          resolvedInfo = resolveURIInternal(cmdLine, privateWindowParam);
        }
        handURIToExistingBrowser(
          resolvedInfo.uri,
          Ci.nsIBrowserDOMWindow.OPEN_NEWTAB,
          cmdLine,
          forcePrivate,
          resolvedInfo.principal
        );
        cmdLine.preventDefault = true;
      }
    } catch (e) {
      if (e.result != Cr.NS_ERROR_INVALID_ARG) {
        throw e;
      }
      // NS_ERROR_INVALID_ARG is thrown when flag exists, but has no param.
      if (cmdLine.handleFlag("private-window", false)) {
        openBrowserWindow(
          cmdLine,
          lazy.gSystemPrincipal,
          "about:privatebrowsing",
          null,
          lazy.PrivateBrowsingUtils.enabled
        );
        cmdLine.preventDefault = true;
      }
    }

    var searchParam = cmdLine.handleFlagWithParam("search", false);
    if (searchParam) {
      doSearch(searchParam, cmdLine);
      cmdLine.preventDefault = true;
    }

    // The global PB Service consumes this flag, so only eat it in per-window
    // PB builds.
    if (
      cmdLine.handleFlag("private", false) &&
      lazy.PrivateBrowsingUtils.enabled
    ) {
      lazy.PrivateBrowsingUtils.enterTemporaryAutoStartMode();
      if (cmdLine.state == Ci.nsICommandLine.STATE_INITIAL_LAUNCH) {
        let win = Services.wm.getMostRecentWindow("navigator:blank");
        if (win) {
          win.docShell.QueryInterface(Ci.nsILoadContext).usePrivateBrowsing =
            true;
        }
      }
    }
    if (cmdLine.handleFlag("setDefaultBrowser", false)) {
      // Note that setDefaultBrowser is an async function, but "handle" (the method being executed)
      // is an implementation of an interface method and changing it to be async would be complicated
      // and ultimately nothing here needs the result of setDefaultBrowser, so we do not bother doing
      // an await.
      lazy.ShellService.setDefaultBrowser(true).catch(e => {
        console.error("setDefaultBrowser failed:", e);
      });
    }

    if (cmdLine.handleFlag("first-startup", false)) {
      // We don't want subsequent calls to needHompageOverride to have different
      // values because the milestones in prefs got updated, so we intentionally
      // tell needHomepageOverride to leave the milestone prefs alone when doing
      // this check.
      let override = needHomepageOverride(false /* updateMilestones */);
      lazy.FirstStartup.init(override == OVERRIDE_NEW_PROFILE /* newProfile */);
    }

    var fileParam = cmdLine.handleFlagWithParam("file", false);
    if (fileParam) {
      var file = cmdLine.resolveFile(fileParam);
      var fileURI = Services.io.newFileURI(file);
      openBrowserWindow(cmdLine, lazy.gSystemPrincipal, fileURI.spec);
      cmdLine.preventDefault = true;
    }

    if (AppConstants.platform == "win") {
      // Handle "? searchterm" for Windows Vista start menu integration
      for (var i = cmdLine.length - 1; i >= 0; --i) {
        var param = cmdLine.getArgument(i);
        if (param.match(/^\? /)) {
          cmdLine.removeArguments(i, i);
          cmdLine.preventDefault = true;

          searchParam = param.substr(2);
          doSearch(searchParam, cmdLine);
        }
      }
    }
  },

  get helpInfo() {
    let info =
      "  --browser          Open a browser window.\n" +
      "  --new-window <url> Open <url> in a new window.\n" +
      "  --new-tab <url>    Open <url> in a new tab.\n" +
      "  --private-window [<url>] Open <url> in a new private window.\n";
    if (AppConstants.platform == "win") {
      info += "  --preferences      Open Options dialog.\n";
    } else {
      info += "  --preferences      Open Preferences dialog.\n";
    }
    info +=
      "  --screenshot [<path>] Save screenshot to <path> or in working directory.\n";
    info +=
      "  --window-size width[,height] Width and optionally height of screenshot.\n";
    info +=
      "  --search <term>    Search <term> with your default search engine.\n";
    info += "  --setDefaultBrowser Set this app as the default browser.\n";
    info +=
      "  --first-startup    Run post-install actions before opening a new window.\n";
    info += "  --kiosk            Start the browser in kiosk mode.\n";
    info +=
      "  --kiosk-monitor <num> Place kiosk browser window on given monitor.\n";
    info +=
      "  --disable-pinch    Disable touch-screen and touch-pad pinch gestures.\n";
    return info;
  },

  /* nsIBrowserHandler */

  get defaultArgs() {
    return this.getNewWindowArgs();
  },

  // This function is expected to be called in non-startup cases,
  // a WNP will not be retrieved within this function, but it will retrieve
  // any new profile override page(s) or regular startup page(s).
  // For the startup version of this function, please use getFirstWindowArgs().
  // See Bug 1642039 for more information.
  getNewWindowArgs(skipStartPage = false) {
    var page = lazy.LaterRun.getURL();
    if (page == "about:blank") {
      page = "";
    }
    var startPage = "";
    var prefb = Services.prefs;
    try {
      var choice = prefb.getIntPref("browser.startup.page");
      if (choice == 1 || choice == 3) {
        startPage = lazy.HomePage.get();
      }
    } catch (e) {
      console.error(e);
    }

    if (startPage == "about:blank") {
      startPage = "";
    }

    // Substitute about:home with the Smart Window URL when the user has
    // chosen Smart Window as default. Done here (rather than in
    // getFirstWindowArgs) so that BrowserHandler.defaultArgs — which
    // browser-init.js compares against to decide session-restore /
    // crash-recovery overrides — reflects the same URL. A user with
    // browser.startup.page=0 has explicitly chosen blank startup, which we
    // respect — they'll get Smart Window chrome with about:blank content.
    if (startPage === "about:home" && canOpenAsSmartWindow()) {
      startPage = lazy.AIWindow.initialStartupURL;
    }

    if (!skipStartPage && startPage) {
      if (page) {
        page += "|" + startPage;
      } else {
        page = startPage;
      }
    } else if (!page) {
      page = startPage;
    }

    return page || "about:blank";
  },

  // This function is expected to be called very early during Firefox startup,
  // It will retrieve a WNP if avaliable, before calling getNewWindowsArg()
  // to retrieve any other startup pages that needs to be displayed.
  // See Bug 1642039 for more information.
  getFirstWindowArgs() {
    var prefb = Services.prefs;

    if (!gFirstWindow) {
      gFirstWindow = true;
      if (lazy.PrivateBrowsingUtils.isInTemporaryAutoStartMode) {
        return "about:privatebrowsing";
      }
    }

    var override;
    var overridePage = "";
    var additionalPage = "";
    var willRestoreSession = false;
    try {
      // Read the old value of homepage_override.mstone before
      // needHomepageOverride updates it, so that we can later add it to the
      // URL if we do end up showing an overridePage. This makes it possible
      // to have the overridePage's content vary depending on the version we're
      // upgrading from.
      let old_mstone = Services.prefs.getCharPref(
        "browser.startup.homepage_override.mstone",
        "unknown"
      );
      let old_buildId = Services.prefs.getCharPref(
        "browser.startup.homepage_override.buildID",
        "unknown"
      );
      override = needHomepageOverride();
      // An object to measure the progress of handleUpdateSuccess
      let progress = {
        updateFetched: false,
        payloadCreated: false,
        pingFailed: false,
      };

      if (override != OVERRIDE_NONE) {
        switch (override) {
          case OVERRIDE_NEW_PROFILE:
            // New profile.
            gFirstRunProfile = true;
            overridePage = Services.urlFormatter.formatURLPref(
              "startup.homepage_welcome_url"
            );
            additionalPage = Services.urlFormatter.formatURLPref(
              "startup.homepage_welcome_url.additional"
            );
            // Turn on 'later run' pages for new profiles.
            lazy.LaterRun.enable(lazy.LaterRun.ENABLE_REASON_NEW_PROFILE);
            break;
          case OVERRIDE_NEW_MSTONE: {
            // Check whether we will restore a session. If we will, we assume
            // that this is an "update" session. This does not take crashes
            // into account because that requires waiting for the session file
            // to be read. If a crash occurs after updating, before restarting,
            // we may open the startPage in addition to restoring the session.
            willRestoreSession =
              lazy.SessionStartup.isAutomaticRestoreEnabled();

            overridePage = Services.urlFormatter.formatURLPref(
              "startup.homepage_override_url"
            );

            /*
            The update manager loads its data asynchronously, off of the main thread.
            However, making this function asynchronous would be very difficult and
            wouldn't provide any benefit. This code is part of the sequence of operations
            that must run before the first tab and its contents can be displayed.
            The user has to wait for this to complete regardless of the method or thread of execution,
            and the browser will be practically unusable until it finishes.
            Therefore, asynchronous execution does not offer any real advantages in this context.
            */
            let update = spinForLastUpdateInstalled();

            // Make sure the update is newer than the last WNP version
            // and the update is not newer than the current Firefox version.
            if (
              update &&
              (Services.vc.compare(update.platformVersion, old_mstone) <= 0 ||
                Services.vc.compare(
                  update.appVersion,
                  Services.appinfo.version
                ) > 0)
            ) {
              update = null;
              overridePage = null;
            }

            /**
             * If the override URL is provided by an experiment, is a valid
             * Firefox What's New Page URL, and the update version is less than
             * or equal to the maxVersion set by the experiment, we'll try to use
             * the experiment override URL instead of the default or the
             * update-provided URL. Additional policy checks are done in
             *
             * @see getPostUpdateOverridePage
             */
            const nimbusOverrideUrl = Services.urlFormatter.formatURLPref(
              "startup.homepage_override_url_nimbus"
            );
            // This defines the maximum allowed Fx update version to see the
            // nimbus WNP. For ex, if maxVersion is set to 127 but user updates
            // to 128, they will not qualify.
            const maxVersion = Services.prefs.getCharPref(
              "startup.homepage_override_nimbus_maxVersion",
              ""
            );
            // This defines the minimum allowed Fx update version to see the
            // nimbus WNP. For ex, if minVersion is set to 126 but user updates
            // to 124, they will not qualify.
            const minVersion = Services.prefs.getCharPref(
              "startup.homepage_override_nimbus_minVersion",
              ""
            );
            // Pref used to disable all WNPs
            const disableWNP = Services.prefs.getBoolPref(
              "startup.homepage_override_nimbus_disable_wnp",
              false
            );
            let nimbusWNP;
            // minVersion and maxVersion optional variables
            const versionMatch =
              (!maxVersion ||
                Services.vc.compare(update.appVersion, maxVersion) <= 0) &&
              (!minVersion ||
                Services.vc.compare(update.appVersion, minVersion) >= 0);

            // The update version should be less than or equal to maxVersion and
            // greater or equal to minVersion set by the experiment.
            if (nimbusOverrideUrl && versionMatch) {
              try {
                let uri = Services.io.newURI(
                  nimbusOverrideUrl.split("|")[0].trim()
                );
                // Only allow https://www.mozilla.org, https://www.mozilla.com, and https://www.firefox.com
                if (
                  uri.scheme === "https" &&
                  [
                    "www.mozilla.org",
                    "www.mozilla.com",
                    "www.firefox.com",
                  ].includes(uri.host)
                ) {
                  nimbusWNP = nimbusOverrideUrl;
                } else {
                  throw new Error("Bad URL");
                }
              } catch {
                console.error("Invalid WNP URL: ", nimbusOverrideUrl);
              }
            }

            if (
              update &&
              Services.vc.compare(update.appVersion, old_mstone) > 0
            ) {
              overridePage = getPostUpdateOverridePage(
                update,
                overridePage,
                nimbusWNP,
                disableWNP
              );
              // Record a Nimbus exposure event for the whatsNewPage feature.
              // The override page could be set in 3 ways: 1. set by Nimbus; 2.
              // set by the update file (openURL); 3. defaulting to the
              // evergreen page (set by the startup.homepage_override_url pref,
              // value depends on the Fx channel). This is done to record that
              // the control cohort could have seen the experimental What's New
              // Page (and will instead see the default What's New Page, or
              // won't see a WNP if the experiment disabled it by setting
              // disable_wnp). `recordExposureEvent` only records an event if
              // the user is enrolled in an experiment or rollout on the
              // whatsNewPage feature, so it's safe to call it unconditionally.
              if (overridePage || (versionMatch && disableWNP)) {
                let nimbusWNPFeature = lazy.NimbusFeatures.whatsNewPage;
                nimbusWNPFeature
                  .ready()
                  .then(() => nimbusWNPFeature.recordExposureEvent());
              }

              lazy.LaterRun.enable(lazy.LaterRun.ENABLE_REASON_UPDATE_APPLIED);
            }

            // Send the update ping to signal that the update was successful.
            // Only do this if the update is installed right now.
            // The following code is ran asynchronously, but we won't await on it
            // since the user may be still waiting for the browser to start up at this point.
            let handleUpdateSuccessTask =
              lazy.UpdateManager.updateInstalledAtStartup().then(
                async updateInstalledAtStartup => {
                  if (updateInstalledAtStartup) {
                    await lazy.UpdatePing.handleUpdateSuccess(
                      old_mstone,
                      old_buildId,
                      progress
                    );
                  }
                }
              );

            // Adding a shutdown blocker to ensure the
            // update ping will be sent before Firefox exits.
            lazy.AsyncShutdown.profileBeforeChange.addBlocker(
              "BrowserContentHandler: running handleUpdateSuccess",
              handleUpdateSuccessTask,
              { fetchState: () => ({ progress }) }
            );

            overridePage = overridePage.replace("%OLD_VERSION%", old_mstone);
            break;
          }
          case OVERRIDE_NEW_BUILD_ID: {
            // We must spin the events loop because `getFirstWindowArgs` cannot be
            // easily made asynchronous, having too many synchronous callers. Additionally
            // we must know the value of `updateInstalledAtStartup` immediately,
            // in order to properly enable `lazy.LaterRun`, that will be invoked shortly after this.
            let updateInstalledAtStartup = spinForUpdateInstalledAtStartup();

            if (updateInstalledAtStartup) {
              let handleUpdateSuccessTask = lazy.UpdatePing.handleUpdateSuccess(
                old_mstone,
                old_buildId,
                progress
              );

              lazy.AsyncShutdown.profileBeforeChange.addBlocker(
                "BrowserContentHandler: running handleUpdateSuccess",
                handleUpdateSuccessTask,
                { fetchState: () => ({ progress }) }
              );

              lazy.LaterRun.enable(lazy.LaterRun.ENABLE_REASON_UPDATE_APPLIED);
            }
            break;
          }
        }
      }
    } catch (ex) {}

    // formatURLPref might return "about:blank" if getting the pref fails
    if (overridePage == "about:blank") {
      overridePage = "";
    }

    // Allow showing a one-time startup override if we're not showing one
    if (overridePage == "" && prefb.prefHasUserValue(ONCE_PREF)) {
      try {
        // Show if we haven't passed the expiration or there's no expiration
        let { expire, url, feature_id, slug } = JSON.parse(
          prefb.getStringPref(ONCE_PREF)
        );
        url = Services.urlFormatter.formatURL(url);
        if (!(Date.now() > expire)) {
          // Only set allowed urls as override pages
          overridePage = url
            .split("|")
            .map(val => {
              let parsed = URL.parse(val);
              if (!parsed) {
                // Invalid URL, so filter out below
                console.error(`Invalid once url: ${val}`);
              }
              return parsed;
            })
            .filter(
              parsed =>
                parsed?.protocol == "https:" &&
                // Only accept exact hostname or subdomain; without port
                ONCE_DOMAINS.has(
                  Services.eTLD.getBaseDomainFromHost(parsed.host)
                )
            )
            .join("|");
          if (feature_id && slug) {
            lazy.NimbusFeatures[feature_id]?.recordExposureEvent({ slug });
          }
          // Be noisy as properly configured urls should be unchanged
          if (overridePage != url) {
            console.error(`Mismatched once urls: ${url}`);
          }
        }
      } catch (ex) {
        // Invalid json pref, so ignore (and clear below)
        console.error("Invalid once pref:", ex);
      } finally {
        prefb.clearUserPref(ONCE_PREF);
      }
    }

    if (!additionalPage) {
      additionalPage = lazy.LaterRun.getURL() || "";
    }

    if (additionalPage && additionalPage != "about:blank") {
      if (overridePage) {
        overridePage += "|" + additionalPage;
      } else {
        overridePage = additionalPage;
      }
    }

    let skipStartPage =
      override == OVERRIDE_NEW_PROFILE &&
      prefb.getBoolPref("browser.startup.firstrunSkipsHomepage");

    var startPage = this.getNewWindowArgs(skipStartPage && !willRestoreSession);

    if (startPage == "about:blank") {
      startPage = "";
    }

    // Only show the startPage if we're not restoring an update session and are
    // not set to skip the start page on this profile
    if (overridePage && startPage && !willRestoreSession && !skipStartPage) {
      return overridePage + "|" + startPage;
    }

    return overridePage || startPage || "about:blank";
  },

  mFeatures: null,

  getFeatures: function bch_features(cmdLine) {
    if (this.mFeatures === null) {
      this.mFeatures = "";

      if (cmdLine) {
        try {
          var width = cmdLine.handleFlagWithParam("width", false);
          var height = cmdLine.handleFlagWithParam("height", false);
          var left = cmdLine.handleFlagWithParam("left", false);
          var top = cmdLine.handleFlagWithParam("top", false);

          if (width) {
            this.mFeatures += ",width=" + width;
          }
          if (height) {
            this.mFeatures += ",height=" + height;
          }
          if (left) {
            this.mFeatures += ",left=" + left;
          }
          if (top) {
            this.mFeatures += ",top=" + top;
          }
        } catch (e) {}
      }

      // The global PB Service consumes this flag, so only eat it in per-window
      // PB builds.
      if (lazy.PrivateBrowsingUtils.isInTemporaryAutoStartMode) {
        this.mFeatures += ",private";
      }

      if (
        Services.prefs.getBoolPref("browser.suppress_first_window_animation") &&
        !Services.wm.getMostRecentWindow("navigator:browser")
      ) {
        this.mFeatures += ",suppressanimation";
      }
    }

    return this.mFeatures;
  },

  get kiosk() {
    return gKiosk;
  },

  get majorUpgrade() {
    return gMajorUpgrade;
  },

  set majorUpgrade(val) {
    gMajorUpgrade = val;
  },

  get firstRunProfile() {
    return gFirstRunProfile;
  },

  set firstRunProfile(val) {
    gFirstRunProfile = val;
  },

  /* nsIContentHandler */

  handleContent: function bch_handleContent(contentType, context, request) {
    try {
      var webNavInfo = Cc["@mozilla.org/webnavigation-info;1"].getService(
        Ci.nsIWebNavigationInfo
      );
      if (!webNavInfo.isTypeSupported(contentType)) {
        throw new Components.Exception("", Cr.NS_ERROR_WONT_HANDLE_CONTENT);
      }
    } catch (e) {
      throw new Components.Exception("", Cr.NS_ERROR_WONT_HANDLE_CONTENT);
    }

    request.QueryInterface(Ci.nsIChannel);
    handURIToExistingBrowser(
      request.URI,
      Ci.nsIBrowserDOMWindow.OPEN_DEFAULTWINDOW,
      null,
      false,
      request.loadInfo.triggeringPrincipal
    );
    request.cancel(Cr.NS_BINDING_ABORTED);
  },

  /**
   * Replace the startup UI window created in BrowserGlue with an actual window
   */
  replaceStartupWindow(args, forcePrivate) {
    let win = Services.wm.getMostRecentWindow("navigator:blank");
    if (win) {
      // Remove the windowtype of our blank window so that we don't close it
      // later on when seeing cmdLine.preventDefault is true.
      win.document.documentElement.removeAttribute("windowtype");

      if (forcePrivate) {
        win.docShell.QueryInterface(Ci.nsILoadContext).usePrivateBrowsing =
          true;

        if (
          AppConstants.platform == "win" &&
          Services.prefs.getBoolPref(
            "browser.privateWindowSeparation.enabled",
            true
          )
        ) {
          lazy.WinTaskbar.setGroupIdForWindow(
            win,
            lazy.WinTaskbar.defaultPrivateGroupId
          );
          lazy.WindowsUIUtils.setWindowIconFromExe(
            win,
            Services.dirsvc.get("XREExeF", Ci.nsIFile).path,
            // This corresponds to the definitions in
            // nsNativeAppSupportWin.h
            PRIVATE_BROWSING_ICON_INDEX
          );
        }
      }

      let openTime = win.openTime;
      win.location = AppConstants.BROWSER_CHROME_URL;
      win.arguments = args; // <-- needs to be a plain JS array here.

      ChromeUtils.addProfilerMarker("earlyBlankWindowVisible", openTime);
      lazy.BrowserWindowTracker.registerOpeningWindow(win, forcePrivate);
      return win;
    }
    return null;
  },

  /* nsICommandLineValidator */
  validate: function bch_validate(cmdLine) {
    var urlFlagIdx = cmdLine.findFlag("url", false);
    if (
      urlFlagIdx > -1 &&
      cmdLine.state == Ci.nsICommandLine.STATE_REMOTE_EXPLICIT
    ) {
      var urlParam = cmdLine.getArgument(urlFlagIdx + 1);
      if (
        cmdLine.length != urlFlagIdx + 2 ||
        /firefoxurl(-[a-f0-9]+)?:/i.test(urlParam)
      ) {
        throw Components.Exception("", Cr.NS_ERROR_ABORT);
      }
    }
  },
};
var gBrowserContentHandler = new nsBrowserContentHandler();

function handURIToExistingBrowser(
  uri,
  location,
  cmdLine,
  forcePrivate,
  triggeringPrincipal
) {
  if (!shouldLoadURI(uri)) {
    return;
  }

  let openInWindow = ({ browserDOMWindow }) => {
    browserDOMWindow.openURI(
      uri,
      null,
      location,
      Ci.nsIBrowserDOMWindow.OPEN_EXTERNAL,
      triggeringPrincipal
    );
  };

  // Unless using a private window is forced, open external links in private
  // windows only if we're in perma-private mode.
  let allowPrivate =
    forcePrivate || lazy.PrivateBrowsingUtils.permanentPrivateBrowsing;
  let navWin = lazy.BrowserWindowTracker.getTopWindow({
    private: allowPrivate,
  });

  if (navWin) {
    openInWindow(navWin);
    return;
  }

  let pending = lazy.BrowserWindowTracker.getPendingWindow({
    private: allowPrivate,
  });
  if (pending) {
    // Note that we cannot make this function async as some callers rely on
    // catching exceptions it can throw in some cases and some of those callers
    // cannot be made async.
    pending.then(openInWindow);
    return;
  }

  // if we couldn't load it in an existing window, open a new one
  openBrowserWindow(cmdLine, triggeringPrincipal, uri.spec, null, forcePrivate);
}

/**
 * If given URI is a file type or a protocol, record telemetry that
 * Firefox was invoked or launched (if `isLaunch` is truth-y).  If the
 * file type or protocol is not registered by default, record it as
 * ".<other extension>" or "<other protocol>".
 *
 * @param uri
 *        The URI Firefox was asked to handle.
 * @param isLaunch
 *        truth-y if Firefox was launched/started rather than running and invoked.
 */
function maybeRecordToHandleTelemetry(uri, isLaunch) {
  let counter = isLaunch
    ? Glean.osEnvironment.launchedToHandle
    : Glean.osEnvironment.invokedToHandle;

  if (uri instanceof Ci.nsIFileURL) {
    let extension = "." + uri.fileExtension.toLowerCase();
    // Keep synchronized with https://searchfox.org/firefox-main/source/browser/installer/windows/nsis/shared.nsh
    // and https://searchfox.org/firefox-main/source/browser/installer/windows/msix/AppxManifest.xml.in.
    let registeredExtensions = new Set([
      ".avif",
      ".htm",
      ".html",
      ".pdf",
      ".shtml",
      ".xht",
      ".xhtml",
      ".svg",
      ".webp",
    ]);
    if (registeredExtensions.has(extension)) {
      counter[extension].add(1);
    } else {
      counter[".<other extension>"].add(1);
    }
  } else if (uri) {
    let scheme = uri.scheme.toLowerCase();
    let registeredSchemes = new Set(["about", "http", "https", "mailto"]);
    if (registeredSchemes.has(scheme)) {
      counter[scheme].add(1);
    } else {
      counter["<other protocol>"].add(1);
    }
  }
}

/**
 * Records a count for Bing navigations that match the Windows Search pattern,
 * using the Bing base domain and `/search` path as heuristic signals. It also
 * records telemetry for example.com to allow the testing of the filepath.
 *
 * @param {nsIURI} uri
 *        The URI being loaded.
 * @param {bool} isLaunch
 *        Indicates whether the browser is starting (true) or already running.
 */
function maybeRecordSearchActivationTelemetry(uri, isLaunch) {
  if (AppConstants.platform != "win") {
    return;
  }

  try {
    if (
      Services.eTLD.getBaseDomain(uri) == "bing.com" &&
      uri.filePath == "/search"
    ) {
      Glean.browserEngagement.windowsStartSearchActivationCount[
        isLaunch ? "startup" : "new_tab"
      ].add(1);
    }
  } catch (_) {
    // Ignore URIs for which no registrable domain can be determined.
  }
}

export function nsDefaultCommandLineHandler() {}

nsDefaultCommandLineHandler.prototype = {
  /* nsISupports */
  QueryInterface: ChromeUtils.generateQI(["nsICommandLineHandler"]),

  _haveProfile: false,

  /**
   * @param {nsICommandLine} cmdLine
   * @returns {boolean} true if the command is handled as notification, otherwise false.
   */
  handleNotification(cmdLine) {
    if (AppConstants.platform !== "win") {
      // Only for Windows for now
      return false;
    }

    // Windows itself does disk I/O when the notification service is
    // initialized, so make sure that is lazy.
    let tag = cmdLine.handleFlagWithParam("notification-windowsTag", false);
    if (!tag) {
      return false;
    }

    // All notifications will invoke Firefox with an action.  Prior to Bug 1805514,
    // this data was extracted from the Windows toast object directly (keyed by the
    // notification ID) and not passed over the command line.  This is acceptable
    // because the data passed is chrome-controlled, but if we implement the `actions`
    // part of the DOM Web Notifications API, this will no longer be true:
    // content-controlled data might transit over the command line.  This could lead
    // to escaping bugs and overflows.  In the future, we intend to avoid any such
    // issue by once again extracting all such data from the Windows toast object.
    let notificationData = cmdLine.handleFlagWithParam(
      "notification-windowsAction",
      false
    );
    if (!notificationData) {
      return false;
    }

    let alertService = lazy.gWindowsAlertsService;
    if (!alertService) {
      console.error("Windows alert service not available.");
      return false;
    }

    // Notification handling occurs asynchronously to prevent blocking the
    // main thread. As a result we won't have the information we need to open
    // a new tab in the case of notification fallback handling before
    // returning. We call `enterLastWindowClosingSurvivalArea` to prevent
    // the browser from exiting in case early blank window is pref'd off.
    if (cmdLine.state == Ci.nsICommandLine.STATE_INITIAL_LAUNCH) {
      Services.startup.enterLastWindowClosingSurvivalArea();
    }
    this.handleNotificationImpl(cmdLine, tag, notificationData, alertService)
      .catch(e => {
        console.error(
          `Error handling Windows notification with tag '${tag}':`,
          e
        );
      })
      .finally(() => {
        if (cmdLine.state == Ci.nsICommandLine.STATE_INITIAL_LAUNCH) {
          Services.startup.exitLastWindowClosingSurvivalArea();
        }
      });
    return true;
  },

  /**
   * Returns when the signal is sent to the relevant notification handler, either:
   * 1. The service worker via nsINotificationHandler for a web notification, or
   * 2. SpecialMessageActions for a Messaging-System-invoked one
   *
   * @param {nsICommandLine} cmdLine
   * @param {string} notificationId
   * @param {string} notificationData
   * @param {nsIWindowsAlertsService} alertService
   */
  async handleNotificationImpl(
    cmdLine,
    notificationId,
    notificationData,
    alertService
  ) {
    let { tagWasHandled } = await alertService.handleWindowsTag(notificationId);

    try {
      notificationData = JSON.parse(notificationData);
    } catch (e) {
      console.error(
        `Failed to parse (notificationData=${notificationData}) for Windows notification (id=${notificationId})`
      );
    }

    // This is awkward: the relaunch data set by the caller is _wrapped_
    // into a compound object that includes additional notification data,
    // and everything is exchanged as strings.  Unwrap and parse here.
    let opaqueRelaunchData = null;
    if (notificationData?.opaqueRelaunchData) {
      try {
        opaqueRelaunchData = JSON.parse(notificationData.opaqueRelaunchData);
      } catch (e) {
        console.error(
          `Failed to parse (opaqueRelaunchData=${notificationData.opaqueRelaunchData}) for Windows notification (id=${notificationId})`
        );
      }
    }

    if (notificationData?.privilegedName) {
      Glean.browserLaunchedToHandle.systemNotification.record({
        name: notificationData.privilegedName,
      });
    }

    // If we have an action in the notification data, this will be the
    // window to perform the action in.
    let winForAction;

    // Fall back to launchUrl to not break notifications opened from
    // previous builds after browser updates, as such notification would
    // still have the old field.
    let origin = notificationData?.origin ?? notificationData?.launchUrl;
    let action = notificationData?.action;

    if (cmdLine.state == Ci.nsICommandLine.STATE_INITIAL_LAUNCH) {
      // Get a browser window where we can open a tab. Having a browser
      // window here helps:
      //
      // 1. Earlier loading without having to wait for service worker
      // 2. Makes sure a browser window loads even if no service worker exists
      //    or the worker decides to not open anything
      //
      // instead of making user to wait on the initial navigator:blank.
      winForAction = openBrowserWindow(cmdLine, lazy.gSystemPrincipal);
      await lazy.BrowserUtils.promiseObserved(
        "browser-delayed-startup-finished",
        subject => subject == winForAction
      );
    }

    if (!tagWasHandled && origin && !opaqueRelaunchData) {
      let originPrincipal =
        Services.scriptSecurityManager.createContentPrincipalFromOrigin(origin);

      const handler = Cc["@mozilla.org/notification-handler;1"].getService(
        Ci.nsINotificationHandler
      );

      await handler.respondOnClick(
        originPrincipal,
        notificationId,
        action,
        /* aAutoClosed */ true
      );
      return;
    }

    // Relaunch in private windows only if we're in perma-private mode.
    let allowPrivate = lazy.PrivateBrowsingUtils.permanentPrivateBrowsing;
    winForAction = lazy.BrowserWindowTracker.getTopWindow({
      private: allowPrivate,
      allowFromInactiveWorkspace: true,
    });

    // Note: at time of writing `opaqueRelaunchData` was only used by the
    // Messaging System; if present it could be inferred that the message
    // originated from the Messaging System. The Messaging System did not
    // act on Windows 8 style notification callbacks, so there was no risk
    // of duplicating behavior. If a non-Messaging System consumer is
    // modified to populate `opaqueRelaunchData` or the Messaging System
    // modified to use the callback directly, we will need to revisit
    // this assumption.
    if (opaqueRelaunchData && winForAction) {
      // Without dispatch, `OPEN_URL` with `where: "tab"` does not work on relaunch.
      Services.tm.dispatchToMainThread(() => {
        lazy.SpecialMessageActions.handleAction(
          opaqueRelaunchData,
          winForAction.gBrowser
        );
      });
    }
  },

  /* nsICommandLineHandler */
  handle: function dch_handle(cmdLine) {
    var urilist = [];
    var principalList = [];

    if (
      cmdLine.state != Ci.nsICommandLine.STATE_INITIAL_LAUNCH &&
      cmdLine.findFlag("os-autostart", true) != -1
    ) {
      // Relaunching after reboot (or quickly opening the application on reboot) and launch-on-login interact.  If we see an after reboot command line while already running, ignore it.
      return;
    }

    if (this.handleNotification(cmdLine)) {
      // This command is about notification and is handled already
      return;
    }

    if (
      cmdLine.state == Ci.nsICommandLine.STATE_INITIAL_LAUNCH &&
      Services.startup.wasSilentlyStarted
    ) {
      // If we are starting up in silent mode, don't open a window. We also need
      // to make sure that the application doesn't immediately exit, so stay in
      // a LastWindowClosingSurvivalArea until a window opens.
      Services.startup.enterLastWindowClosingSurvivalArea();
      Services.obs.addObserver(function windowOpenObserver() {
        Services.startup.exitLastWindowClosingSurvivalArea();
        Services.obs.removeObserver(windowOpenObserver, "domwindowopened");
      }, "domwindowopened");
      return;
    }

    if (AppConstants.platform == "win" || AppConstants.platform == "macosx") {
      // Handle the case where we don't have a profile selected yet (e.g. the
      // Profile Manager is displayed).
      // On Windows, we will crash if we open an url and then select a profile.
      // On macOS, if we open an url we don't experience a crash but a broken
      // window is opened.
      // To prevent this handle all url command line flags and set the
      // command line's preventDefault to true to prevent the display of the ui.
      // The initial command line will be retained when nsAppRunner calls
      // LaunchChild though urls launched after the initial launch will be lost.
      if (!this._haveProfile) {
        try {
          // This will throw when a profile has not been selected.
          Services.dirsvc.get("ProfD", Ci.nsIFile);
          this._haveProfile = true;
        } catch (e) {
          // eslint-disable-next-line no-empty
          while ((ar = cmdLine.handleFlagWithParam("url", false))) {}
          cmdLine.preventDefault = true;
        }
      }
    }

    // `-osint` and handling registered file types and protocols is Windows-only.
    let launchedWithArg_osint =
      AppConstants.platform == "win" && cmdLine.findFlag("osint", false) == 0;
    if (launchedWithArg_osint) {
      cmdLine.handleFlag("osint", false);
    }

    try {
      var ar;
      while ((ar = cmdLine.handleFlagWithParam("url", false))) {
        let { uri, principal } = resolveURIInternal(cmdLine, ar);
        urilist.push(uri);
        principalList.push(principal);

        if (launchedWithArg_osint) {
          launchedWithArg_osint = false;

          // We use the resolved URI here, even though it can produce
          // surprising results where-by `-osint -url test.pdf` resolves to
          // a query with search parameter "test.pdf".  But that shouldn't
          // happen when Firefox is launched by Windows itself: files should
          // exist and be resolved to file URLs.
          const isLaunch =
            cmdLine && cmdLine.state == Ci.nsICommandLine.STATE_INITIAL_LAUNCH;

          maybeRecordSearchActivationTelemetry(uri, isLaunch);
          maybeRecordToHandleTelemetry(uri, isLaunch);
        }
      }
    } catch (e) {
      console.error(e);
    }

    if (cmdLine.findFlag("screenshot", true) != -1) {
      // Shouldn't have to push principal here with the screenshot flag
      lazy.HeadlessShell.handleCmdLineArgs(
        cmdLine,
        urilist.filter(shouldLoadURI).map(u => u.spec)
      );
      return;
    }

    for (let i = 0; i < cmdLine.length; ++i) {
      var curarg = cmdLine.getArgument(i);
      if (curarg.match(/^-/)) {
        console.error("Warning: unrecognized command line flag", curarg);
        // To emulate the pre-nsICommandLine behavior, we ignore
        // the argument after an unrecognized flag.
        ++i;
      } else {
        try {
          let { uri, principal } = resolveURIInternal(cmdLine, curarg);
          urilist.push(uri);
          principalList.push(principal);
        } catch (e) {
          console.error(
            `Error opening URI ${curarg} from the command line:`,
            e
          );
        }
      }
    }

    if (urilist.length) {
      if (
        cmdLine.state != Ci.nsICommandLine.STATE_INITIAL_LAUNCH &&
        urilist.length == 1
      ) {
        // Try to find an existing window and load our URI into the
        // current tab, new tab, or new window as prefs determine.
        try {
          handURIToExistingBrowser(
            urilist[0],
            Ci.nsIBrowserDOMWindow.OPEN_DEFAULTWINDOW,
            cmdLine,
            false,
            principalList[0] ?? lazy.gSystemPrincipal
          );
          return;
        } catch (e) {}
      }

      // Can't open multiple URLs without using system principal.
      var URLlist = urilist.filter(shouldLoadURI).map(u => u.spec);
      if (URLlist.length) {
        openBrowserWindow(cmdLine, lazy.gSystemPrincipal, URLlist);
      }
    } else if (!cmdLine.preventDefault) {
      if (
        AppConstants.platform == "win" &&
        cmdLine.state != Ci.nsICommandLine.STATE_INITIAL_LAUNCH &&
        lazy.WindowsUIUtils.inWin10TabletMode
      ) {
        // In Win10's tablet mode, do not create a new window, but reuse the
        // existing one. (Win11's tablet mode is still windowed and has no need
        // for this workaround.)
        let win = lazy.BrowserWindowTracker.getTopWindow();
        if (win) {
          win.focus();
          return;
        }
      }
      openBrowserWindow(cmdLine, lazy.gSystemPrincipal);
    } else {
      // Need a better solution in the future to avoid opening the blank window
      // when command line parameters say we are not going to show a browser
      // window, but for now the blank window getting closed quickly (and
      // causing only a slight flicker) is better than leaving it open.
      let win = Services.wm.getMostRecentWindow("navigator:blank");
      if (win) {
        win.close();
      }
    }
  },

  helpInfo: "",
};
