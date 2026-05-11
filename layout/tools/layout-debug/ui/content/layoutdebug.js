/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var gArgs;
var gBrowser;
var gURLBar;
var gDebugger;
var gMultiProcessBrowser = window.docShell.QueryInterface(
  Ci.nsILoadContext
).useRemoteTabs;
var gFissionBrowser = window.docShell.QueryInterface(
  Ci.nsILoadContext
).useRemoteSubframes;
var gWritingProfile = false;
var gWrittenProfile = false;

const { E10SUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/E10SUtils.sys.mjs"
);
const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  BrowserToolboxLauncher:
    "resource://devtools/client/framework/browser-toolbox/Launcher.sys.mjs",
});

const FEATURES = {
  paintDumping: "nglayout.debug.paint_dumping",
  invalidateDumping: "nglayout.debug.invalidate_dumping",
  eventDumping: "nglayout.debug.event_dumping",
  motionEventDumping: "nglayout.debug.motion_event_dumping",
  crossingEventDumping: "nglayout.debug.crossing_event_dumping",
  reflowCounts: "layout.reflow.showframecounts",
};

const SIMPLE_COMMANDS = [
  "dumpTextRuns",
  "dumpCounterManager",
  "dumpRetainedDisplayList",
  "dumpStyleSheets",
  "dumpMatchedRules",
  "dumpComputedStyles",
  "dumpReflowStats",
];

class Debugger {
  constructor() {
    this._flags = new Map();
    this._pagedMode = false;
    this._attached = false;
    this._anonymousSubtreeDumping = false;
    this._deterministicFrameDumping = false;

    for (let [name, pref] of Object.entries(FEATURES)) {
      this._flags.set(name, !!Services.prefs.getBoolPref(pref, false));
    }

    this.attachBrowser();
  }

  detachBrowser() {
    if (!this._attached) {
      return;
    }
    gBrowser.removeProgressListener(this._progressListener);
    this._progressListener = null;
    this._attached = false;
  }

  attachBrowser() {
    if (this._attached) {
      throw "already attached";
    }
    this._progressListener = new nsLDBBrowserContentListener();
    gBrowser.addProgressListener(this._progressListener);
    this._attached = true;
  }

  dumpProcessIDs() {
    let parentPid = Services.appinfo.processID;
    let [contentPid, ...framePids] = E10SUtils.getBrowserPids(
      gBrowser,
      gFissionBrowser
    );

    dump(`Parent pid: ${parentPid}\n`);
    dump(`Content pid: ${contentPid || "-"}\n`);
    if (gFissionBrowser) {
      dump(`Subframe pids: ${framePids.length ? framePids.join(", ") : "-"}\n`);
    }
  }

  get pagedMode() {
    return this._pagedMode;
  }

  set pagedMode(v) {
    v = !!v;
    this._pagedMode = v;
    this.setPagedMode(this._pagedMode);
  }

  setPagedMode(v) {
    this._sendMessage("setPagedMode", v);
  }

  get anonymousSubtreeDumping() {
    return this._anonymousSubtreeDumping;
  }

  set anonymousSubtreeDumping(v) {
    this._anonymousSubtreeDumping = !!v;
  }

  get deterministicFrameDumping() {
    return this._deterministicFrameDumping;
  }

  set deterministicFrameDumping(v) {
    this._deterministicFrameDumping = !!v;
  }

  openDevTools() {
    lazy.BrowserToolboxLauncher.init();
  }

  sendDumpContent() {
    return this._sendMessage("dumpContent", this.anonymousSubtreeDumping);
  }

  sendDumpFrames(css_pixels) {
    let flags = 0;
    if (css_pixels) {
      flags |= Ci.nsILayoutDebuggingTools.DUMP_FRAME_FLAGS_CSS_PIXELS;
    }
    if (this.deterministicFrameDumping) {
      flags |= Ci.nsILayoutDebuggingTools.DUMP_FRAME_FLAGS_DETERMINISTIC;
    }
    return this._sendMessage("dumpFrames", flags);
  }

  async _sendMessage(name, arg) {
    await this._sendMessageTo(gBrowser.browsingContext, name, arg);
  }

  async _sendMessageTo(context, name, arg) {
    let global = context.currentWindowGlobal;
    if (global) {
      await global
        .getActor("LayoutDebug")
        .sendQuery("LayoutDebug:Call", { name, arg });
    }

    for (let c of context.children) {
      await this._sendMessageTo(c, name, arg);
    }
  }
}

for (let [name, pref] of Object.entries(FEATURES)) {
  Object.defineProperty(Debugger.prototype, name, {
    get: function () {
      return this._flags.get(name);
    },
    set: function (v) {
      v = !!v;
      Services.prefs.setBoolPref(pref, v);
      this._flags.set(name, v);
      // XXX PresShell should watch for this pref change itself.
      if (name == "reflowCounts") {
        this._sendMessage("setReflowCounts", v);
      }
      this._sendMessage("forceRefresh");
    },
  });
}

for (let name of SIMPLE_COMMANDS) {
  Debugger.prototype[name] = function () {
    return this._sendMessage(name);
  };
}

Debugger.prototype.dumpContent = function () {
  return this.sendDumpContent();
};

Debugger.prototype.dumpFrames = function () {
  return this.sendDumpFrames(false);
};

Debugger.prototype.dumpFramesInCSSPixels = function () {
  return this.sendDumpFrames(true);
};

async function dumpRequestedInfo() {
  if (gArgs.dumpContent) {
    await gDebugger.dumpContent();
  }
  if (gArgs.dumpFrames) {
    await gDebugger.dumpFrames();
  }
  if (gArgs.dumpFramesCSSPixels) {
    await gDebugger.dumpFramesInCSSPixels();
  }
  if (gArgs.dumpRetainedDisplayList) {
    await gDebugger.dumpRetainedDisplayList();
  }
}

function autoCloseIfNeeded(aCrash) {
  if (!gArgs.autoclose) {
    return;
  }
  setTimeout(function () {
    if (aCrash) {
      let browser = document.createXULElement("browser");
      // FIXME(emilio): we could use gBrowser if we bothered get the process switches right.
      //
      // Doesn't seem worth for this particular case.
      document.documentElement.appendChild(browser);
      browser.loadURI(Services.io.newURI("about:crashparent"), {
        triggeringPrincipal:
          Services.scriptSecurityManager.getSystemPrincipal(),
      });
      return;
    }
    if (gArgs.profile && Services.profiler) {
      dumpProfile();
    } else {
      Services.startup.quit(Ci.nsIAppStartup.eAttemptQuit);
    }
  }, gArgs.delay * 1000);
}

function nsLDBBrowserContentListener() {
  this.init();
}

nsLDBBrowserContentListener.prototype = {
  init: function () {
    this.mStatusText = document.getElementById("status-text");
    this.mForwardButton = document.getElementById("forward-button");
    this.mBackButton = document.getElementById("back-button");
    this.mStopButton = document.getElementById("stop-button");
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsIWebProgressListener",
    "nsISupportsWeakReference",
  ]),

  // nsIWebProgressListener implementation
  onStateChange: function (aWebProgress, aRequest, aStateFlags, aStatus) {
    if (!(aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK)) {
      return;
    }

    if (aStateFlags & Ci.nsIWebProgressListener.STATE_START) {
      this.setButtonEnabled(this.mStopButton, true);
      this.setButtonEnabled(this.mForwardButton, gBrowser.canGoForward);
      this.setButtonEnabled(this.mBackButton, gBrowser.canGoBack);
      this.mStatusText.value = "loading...";
      this.mLoading = true;
    } else if (aStateFlags & Ci.nsIWebProgressListener.STATE_STOP) {
      this.setButtonEnabled(this.mStopButton, false);
      this.mStatusText.value = gURLBar.value + " loaded";
      this.mLoading = false;

      if (gDebugger.pagedMode) {
        // Change to paged mode after the page is loaded.
        gDebugger.setPagedMode(true);
      }

      if (gBrowser.currentURI.spec != "about:blank") {
        // We check for about:blank just to avoid one or two STATE_STOP
        // notifications that occur before the loadURI() call completes.
        // This does mean that --autoclose doesn't work when the URL on
        // the command line is about:blank (or not specified), but that's
        // not a big deal.
        //
        // Waiting for any dump output completes before closing layout debugger.
        dumpRequestedInfo().finally(() => autoCloseIfNeeded(false));
      }
    }
  },

  onProgressChange: function (
    aWebProgress,
    aRequest,
    aCurSelfProgress,
    aMaxSelfProgress,
    aCurTotalProgress,
    aMaxTotalProgress
  ) {},

  onLocationChange: function (aWebProgress, aRequest, aLocation, aFlags) {
    gURLBar.value = aLocation.spec;
    this.setButtonEnabled(this.mForwardButton, gBrowser.canGoForward);
    this.setButtonEnabled(this.mBackButton, gBrowser.canGoBack);
  },

  onStatusChange: function (aWebProgress, aRequest, aStatus, aMessage) {
    this.mStatusText.value = aMessage;
  },

  onSecurityChange: function (aWebProgress, aRequest, aState) {},

  onContentBlockingEvent: function (aWebProgress, aRequest, aEvent) {},

  // non-interface methods
  setButtonEnabled: function (aButtonElement, aEnabled) {
    aButtonElement.toggleAttribute("disabled", !aEnabled);
  },

  mStatusText: null,
  mForwardButton: null,
  mBackButton: null,
  mStopButton: null,

  mLoading: false,
};

function parseArguments() {
  let args = {
    url: null,
    autoclose: false,
    delay: 0,
    paged: false,
    anonymousSubtreeDumping: false,
    deterministicFrameDumping: false,
    dumpContent: false,
    dumpFrames: false,
    dumpFramesCSSPixels: false,
    dumpRetainedDisplayList: false,
  };
  if (window.arguments) {
    args.url = window.arguments[0];
    for (let i = 1; i < window.arguments.length; ++i) {
      let arg = window.arguments[i];
      if (/^autoclose=(.*)$/.test(arg)) {
        args.autoclose = true;
        args.delay = +RegExp.$1;
      } else if (/^profile=(.*)$/.test(arg)) {
        args.profile = true;
        args.profileFilename = RegExp.$1;
      } else if (/^paged$/.test(arg)) {
        args.paged = true;
      } else if (/^anonymous-subtree-dumping$/.test(arg)) {
        args.anonymousSubtreeDumping = true;
      } else if (/^deterministic-frame-dumping$/.test(arg)) {
        args.deterministicFrameDumping = true;
      } else if (arg === "dump-content") {
        args.dumpContent = true;
      } else if (arg === "dump-frames") {
        args.dumpFrames = true;
      } else if (arg === "dump-frames-css-pixels") {
        args.dumpFramesCSSPixels = true;
      } else if (arg === "dump-retained-display-list") {
        args.dumpRetainedDisplayList = true;
      } else {
        throw `Unknown option ${arg}`;
      }
    }
  }
  return args;
}

const TabCrashedObserver = {
  observe(subject, topic, data) {
    switch (topic) {
      case "ipc:content-shutdown":
        subject.QueryInterface(Ci.nsIPropertyBag2);
        if (!subject.get("abnormal")) {
          return;
        }
        break;
      case "oop-frameloader-crashed":
        break;
    }
    autoCloseIfNeeded(true);
  },
};

function OnLDBLoad() {
  window.addEventListener("close", event => OnLDBBeforeUnload(event));
  window.addEventListener("unload", OnLDBUnload);
  document
    .getElementById("tasksCommands")
    .addEventListener("command", event => {
      switch (event.target.id) {
        case "cmd_open":
          openFile();
          break;
        case "cmd_close":
          window.close();
          break;
        case "cmd_focusURLBar":
          focusURLBar();
          break;
        case "cmd_reload":
          gBrowser.reload();
          break;
        case "cmd_dumpContent":
          gDebugger.dumpContent();
          break;
        case "cmd_dumpFrames":
          gDebugger.dumpFrames();
          break;
        case "cmd_dumpFramesInCSSPixels":
          gDebugger.dumpFramesInCSSPixels();
          break;
        case "cmd_dumpTextRuns":
          gDebugger.dumpTextRuns();
          break;
        case "cmd_dumpRetainedDisplayList":
          gDebugger.dumpRetainedDisplayList();
          break;
        case "cmd_openDevTools":
          gDebugger.openDevTools();
          break;
        default:
          // Default means that we are not handling a command so we should
          // probably let people know.
          throw new Error("Unhandled command event");
      }
    });
  document
    .getElementById("layoutdebug-toggle-menu")
    .addEventListener("command", event => {
      toggle(event.target);
    });
  document
    .getElementById("layoutdebug-dump-menu")
    .addEventListener("command", event => {
      switch (event.target.id) {
        case "menu_processIDs":
          gDebugger.dumpProcessIDs();
          break;
        case "menu_dumpContent":
          gDebugger.dumpContent();
          break;
        case "menu_dumpFrames":
          gDebugger.dumpFrames();
          break;
        case "menu_dumpFramesInCSSPixels":
          gDebugger.dumpFramesInCSSPixels();
          break;
        case "menu_dumpTextRuns":
          gDebugger.dumpTextRuns();
          break;
        case "menu_dumpCounterManager":
          gDebugger.dumpCounterManager();
          break;
        case "menu_dumpRetainedDisplayList":
          gDebugger.dumpRetainedDisplayList();
          break;
        case "menu_dumpStyleSheets":
          gDebugger.dumpStyleSheets();
          break;
        case "menu_dumpMatchedRules":
          gDebugger.dumpMatchedRules();
          break;
        case "menu_dumpComputedStyles":
          gDebugger.dumpComputedStyles();
          break;
        case "menu_dumpReflowStats":
          gDebugger.dumpReflowStats();
          break;
        default:
          // Default means that we are not handling a command so we should
          // probably let people know.
          throw new Error("Unhandled command event");
      }
    });
  document.getElementById("nav-toolbar").addEventListener("command", event => {
    switch (event.target.id) {
      case "back-button":
        gBrowser.goBack();
        break;
      case "forward-button":
        gBrowser.goForward();
        break;
      case "stop-button":
        gBrowser.stop();
        break;
      default:
        // Default means that we are not handling a command so we should
        // probably let people know.
        throw new Error("Unhandled command event");
    }
  });
  document.getElementById("urlbar").addEventListener("keypress", event => {
    if (event.key == "Enter") {
      go();
    }
  });
  gBrowser = document.getElementById("browser");
  gURLBar = document.getElementById("urlbar");

  try {
    ChromeUtils.registerWindowActor("LayoutDebug", {
      child: {
        esModuleURI: "resource://gre/actors/LayoutDebugChild.sys.mjs",
      },
      allFrames: true,
    });
  } catch (ex) {
    // Only register the actor once.
  }

  gDebugger = new Debugger();

  Services.obs.addObserver(TabCrashedObserver, "ipc:content-shutdown");
  Services.obs.addObserver(TabCrashedObserver, "oop-frameloader-crashed");

  // Pretend slightly to be like a normal browser, so that SessionStore.sys.mjs
  // doesn't get too confused.  The effect is that we'll never switch process
  // type when navigating, and for layout debugging purposes we don't bother
  // about getting that right.
  gBrowser.getTabForBrowser = function () {
    return null;
  };

  gArgs = parseArguments();

  if (gArgs.profile) {
    if (Services.profiler) {
      if (!Services.env.exists("MOZ_PROFILER_SYMBOLICATE")) {
        dump(
          "Warning: MOZ_PROFILER_SYMBOLICATE environment variable not set; " +
            "profile will not be symbolicated.\n"
        );
      }
      Services.profiler.StartProfiler(
        1 << 20,
        1,
        ["default"],
        ["GeckoMain", "Compositor", "Renderer", "RenderBackend", "StyleThread"]
      );
      if (gArgs.url) {
        // Switch to the right kind of content process, and wait a bit so that
        // the profiler has had a chance to attach to it.
        loadStringURI(gArgs.url, { delayLoad: 3000 });
        return;
      }
    } else {
      dump("Cannot profile Layout Debugger; profiler was not compiled in.\n");
    }
  }

  // The URI is not loaded yet. Just set the internal variable.
  gDebugger._pagedMode = gArgs.paged;

  if (gArgs.url) {
    loadStringURI(gArgs.url);
  }

  gDebugger._anonymousSubtreeDumping = gArgs.anonymousSubtreeDumping;
  gDebugger._deterministicFrameDumping = gArgs.deterministicFrameDumping;

  // Some command line arguments may toggle menu items. Call this after
  // processing all the arguments.
  checkPersistentMenus();
}

function checkPersistentMenu(item) {
  var menuitem = document.getElementById("menu_" + item);
  menuitem.toggleAttribute("checked", gDebugger[item]);
}

function checkPersistentMenus() {
  // Restore the toggles that are stored in prefs.
  checkPersistentMenu("paintDumping");
  checkPersistentMenu("invalidateDumping");
  checkPersistentMenu("eventDumping");
  checkPersistentMenu("motionEventDumping");
  checkPersistentMenu("crossingEventDumping");
  checkPersistentMenu("reflowCounts");
  checkPersistentMenu("pagedMode");
  checkPersistentMenu("anonymousSubtreeDumping");
  checkPersistentMenu("deterministicFrameDumping");
}

function dumpProfile() {
  gWritingProfile = true;

  let cwd = Services.dirsvc.get("CurWorkD", Ci.nsIFile).path;
  let filename = PathUtils.join(cwd, gArgs.profileFilename);

  dump(`Writing profile to ${filename}...\n`);

  Services.profiler.dumpProfileToFileAsync(filename).then(function () {
    gWritingProfile = false;
    gWrittenProfile = true;
    dump(`done\n`);
    Services.startup.quit(Ci.nsIAppStartup.eAttemptQuit);
  });
}

function OnLDBBeforeUnload(event) {
  if (gArgs.profile && Services.profiler) {
    if (gWrittenProfile) {
      // We've finished writing the profile.  Allow the window to close.
      return;
    }

    event.preventDefault();

    if (gWritingProfile) {
      // Wait for the profile to finish being written out.
      return;
    }

    // The dumpProfileToFileAsync call can block for a while, so run it off a
    // timeout to avoid annoying the window manager if we're doing this in
    // response to clicking the window's close button.
    setTimeout(dumpProfile, 0);
  }
}

function OnLDBUnload() {
  gDebugger.detachBrowser();
  Services.obs.removeObserver(TabCrashedObserver, "ipc:content-shutdown");
  Services.obs.removeObserver(TabCrashedObserver, "oop-frameloader-crashed");
}

function toggle(menuitem) {
  // trim the initial "menu_"
  var feature = menuitem.id.substring(5);
  gDebugger[feature] = menuitem.hasAttribute("checked");
}

function openFile() {
  var fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);
  fp.init(window.browsingContext, "Select a File", Ci.nsIFilePicker.modeOpen);
  fp.appendFilters(Ci.nsIFilePicker.filterHTML | Ci.nsIFilePicker.filterAll);
  fp.open(rv => {
    if (
      rv == Ci.nsIFilePicker.returnOK &&
      fp.fileURL.spec &&
      fp.fileURL.spec.length > 0
    ) {
      loadURIObject(fp.fileURL);
    }
  });
}

// A simplified version of the function with the same name in tabbrowser.js.
function updateBrowserRemotenessByURL(aURL) {
  let oa = E10SUtils.predictOriginAttributes({ browser: gBrowser });
  let remoteType = E10SUtils.getRemoteTypeForURIObject(aURL, {
    multiProcess: gMultiProcessBrowser,
    remoteSubFrames: gFissionBrowser,
    preferredRemoteType: gBrowser.remoteType,
    currentURI: gBrowser.currentURI,
    originAttributes: oa,
  });
  if (gBrowser.remoteType != remoteType) {
    gDebugger.detachBrowser();
    if (remoteType == E10SUtils.NOT_REMOTE) {
      gBrowser.removeAttribute("remote");
      gBrowser.removeAttribute("remoteType");
    } else {
      gBrowser.setAttribute("remote", "true");
      gBrowser.setAttribute("remoteType", remoteType);
    }
    gBrowser.changeRemoteness({ remoteType });
    gBrowser.construct();
    gDebugger.attachBrowser();
  }
}

function loadStringURI(aURLString, aOptions) {
  let realURL;
  try {
    realURL = Services.uriFixup.getFixupURIInfo(aURLString).preferredURI;
  } catch (ex) {
    alert(
      "Couldn't work out how to create a URL from input: " +
        aURLString.substring(0, 100)
    );
    return;
  }
  return loadURIObject(realURL, aOptions);
}

async function loadURIObject(aURL, { delayLoad } = {}) {
  // We don't bother trying to handle navigations within the browser to new URLs
  // that should be loaded in a different process.
  updateBrowserRemotenessByURL(aURL);
  // When attaching the profiler we may want to delay the actual load a bit
  // after switching remoteness.
  if (delayLoad) {
    await new Promise(r => setTimeout(r, delayLoad));
  }
  gBrowser.loadURI(aURL, {
    triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
  });
}

function focusURLBar() {
  gURLBar.focus();
  gURLBar.select();
}

function go() {
  loadStringURI(gURLBar.value);
  gBrowser.focus();
}

window.addEventListener("load", OnLDBLoad);
