/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* globals ExtensionAPI, Services, XPCOMUtils */

const { ComponentUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/ComponentUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  AboutHomeStartupCache: "resource:///modules/AboutHomeStartupCache.sys.mjs",
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  PerTestCoverageUtils:
    "resource://testing-common/PerTestCoverageUtils.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "resProto",
  "@mozilla.org/network/protocol;1?name=resource",
  Ci.nsISubstitutingProtocolHandler
);

// These are not automagically defined for us because we are an extension.
//
// eslint-disable-next-line mozilla/reject-importGlobalProperties
Cu.importGlobalProperties(["IOUtils", "PathUtils"]);

const Cm = Components.manager.QueryInterface(Ci.nsIComponentRegistrar);

let frameScriptURL;
let profilerSubtestStartTime;

function TalosPowersService() {
  this.wrappedJSObject = this;

  this.init();
}

TalosPowersService.prototype = {
  factory: ComponentUtils.generateSingletonFactory(TalosPowersService),
  classDescription: "Talos Powers",
  classID: Components.ID("{f5d53443-d58d-4a2f-8df0-98525d4f91ad}"),
  contractID: "@mozilla.org/talos/talos-powers-service;1",
  QueryInterface: ChromeUtils.generateQI([]),

  register() {
    Cm.registerFactory(
      this.classID,
      this.classDescription,
      this.contractID,
      this.factory
    );

    void Cc[this.contractID].getService();
  },

  unregister() {
    Cm.unregisterFactory(this.classID, this.factory);
  },

  init() {
    if (!frameScriptURL) {
      throw new Error("Cannot find frame script url (extension not started?)");
    }
    Services.mm.loadFrameScript(frameScriptURL, true);
    Services.mm.addMessageListener("Talos:ForceQuit", this);
    Services.mm.addMessageListener("TalosContentProfiler:Command", this);
    Services.mm.addMessageListener("TalosPowersContent:ForceCCAndGC", this);
    Services.mm.addMessageListener("TalosPowersContent:GetStartupInfo", this);
    Services.mm.addMessageListener("TalosPowers:ParentExec:QueryMsg", this);
  },

  receiveMessage(message) {
    switch (message.name) {
      case "Talos:ForceQuit": {
        this.forceQuit(message.data);
        break;
      }
      case "TalosContentProfiler:Command": {
        this.receiveProfileCommand(message);
        break;
      }
      case "TalosPowersContent:ForceCCAndGC": {
        Cu.forceGC();
        Cu.forceCC();
        Cu.forceShrinkingGC();
        break;
      }
      case "TalosPowersContent:GetStartupInfo": {
        this.receiveGetStartupInfo(message);
        break;
      }
      case "TalosPowers:ParentExec:QueryMsg": {
        this.RecieveParentExecCommand(message);
        break;
      }
    }
  },

  /**
   * Enable the Gecko Profiler with some settings.
   *
   * @param data (object)
   *        A JavaScript object with the following properties:
   *
   *        entries (int):
   *          The sampling buffer size in bytes.
   *
   *        interval (int):
   *          The sampling interval in milliseconds.
   *
   *        threadsArray (array of strings):
   *          The thread names to sample.
   */
  profilerBegin(data) {
    Services.profiler.StartProfiler(
      data.entries,
      data.interval,
      data.featuresArray,
      data.threadsArray
    );
  },

  /**
   * Assuming the Profiler is running, dumps the Profile from all sampled
   * processes and threads to the disk. The Profiler will be stopped once
   * the profiles have been dumped. This method returns a Promise that
   * will resolve once this has occurred.
   *
   * @param profileDir (string)
   *        The name of the directory to write the profile in.
   * @param profileFile (string)
   *        The name of the file to write to.
   *
   * @returns Promise
   */
  profilerFinish(profileDir, profileFile) {
    const profilePath = PathUtils.join(profileDir, profileFile);
    return new Promise((resolve, reject) => {
      Services.profiler.Pause();
      Services.profiler.getProfileDataAsync().then(
        profile =>
          IOUtils.writeJSON(profilePath, profile, {
            tmpPath: `${profilePath}.tmp`,
          }).then(() => {
            Services.profiler.StopProfiler();
            resolve();
            Services.obs.notifyObservers(null, "talos-profile-gathered");
          }),
        error => {
          console.error("Failed to gather profile:", error);
          // FIXME: We should probably send a message down to the
          // child which causes it to reject the waiting Promise.
          reject();
        }
      );
    });
  },

  /**
   * Add a parent process marker to the profiler to indicate the subtest duration.
   *
   * @param marker (string, optional)
   *        Marker name.
   */
  profilerSubtestEnd(marker = null, startTime = undefined) {
    if (marker) {
      this.addIntervalMarker(marker, startTime ?? profilerSubtestStartTime);
    }
  },

  /**
   * * Add a parent process marker to the profiler to indicate the start of the subtest.
   *
   * @param marker (string, optional)
   *        Marker name.
   */
  profilerSubtestStart(marker = null) {
    profilerSubtestStartTime = ChromeUtils.now();

    if (marker) {
      this.addInstantMarker(marker);
    }
  },

  /**
   * Adds an instant marker to the Profile in the parent process.
   *
   * @param marker (string)  A marker to set.
   */
  addInstantMarker(marker) {
    ChromeUtils.addProfilerMarker("Talos", { category: "Test" }, marker);
  },

  /**
   * Adds a marker to the Profile in the parent process.
   *
   * @param marker (string)
   *        A marker to set.
   *
   * @param startTime (number)
   *        Start time, used to create an interval profile marker. If
   *        undefined, a single instance marker will be placed.
   */
  addIntervalMarker(marker, startTime) {
    ChromeUtils.addProfilerMarker(
      "Talos",
      { startTime, category: "Test" },
      marker
    );
  },

  receiveProfileCommand(message) {
    const ACK_NAME = "TalosContentProfiler:Response";
    let mm = message.target.messageManager;
    let name = message.data.name;
    let data = message.data.data;

    switch (name) {
      case "Profiler:Begin": {
        this.profilerBegin(data);
        // profilerBegin will cause the parent to send an async message to any
        // child processes to start profiling. Because messages are serviced
        // in order, we know that by the time that the child services the
        // ACK message, that the profiler has started in its process.
        mm.sendAsyncMessage(ACK_NAME, { name });
        break;
      }

      case "Profiler:Finish": {
        // The test is done. Dump the profile.
        this.profilerFinish(data.profileDir, data.profileFile).then(() => {
          mm.sendAsyncMessage(ACK_NAME, { name });
        });
        break;
      }

      case "Profiler:SubtestEnd": {
        this.profilerSubtestEnd(data.marker, data.startTime);
        mm.sendAsyncMessage(ACK_NAME, { name });
        break;
      }

      case "Profiler:SubtestStart": {
        this.profilerSubtestStart(data.marker);
        mm.sendAsyncMessage(ACK_NAME, { name });
        break;
      }

      case "Profiler:Marker": {
        this.profilerMarker(data.marker, data.startTime);
        mm.sendAsyncMessage(ACK_NAME, { name });
        break;
      }
    }
  },

  async forceQuit(messageData) {
    if (messageData && messageData.waitForStartupFinished) {
      // We can wait for various startup items here to complete during
      // the getInfo.html step for Talos so that subsequent runs don't
      // have to do things like re-request the SafeBrowsing list.
      let { SafeBrowsing } = ChromeUtils.importESModule(
        "resource://gre/modules/SafeBrowsing.sys.mjs"
      );

      // Speed things up in case nobody else called this:
      SafeBrowsing.init();

      try {
        await SafeBrowsing.addMozEntriesFinishedPromise;
      } catch (e) {
        // We don't care if things go wrong here - let's just shut down.
      }

      // We wait for the AboutNewTab's TopSitesFeed (and its "Contile"
      // integration, which shows the sponsored Top Sites) to finish
      // being enabled here. This is because it's possible for getInfo.html
      // to run so quickly that the feed will still be initializing, and
      // that would cause us to write a mostly empty cache to the
      // about:home startup cache on shutdown, which causes that test
      // to break periodically.
      //
      // There aren't currently any easily observable notifications or
      // events to let us know when the feed is ready, so we'll just poll
      // for now.
      let pollForFeed = async function () {
        let foundFeed = AboutNewTab.activityStream?.store.feeds.get(
          "feeds.system.topsites"
        );
        if (!foundFeed) {
          await new Promise(resolve => setTimeout(resolve, 500));
          return pollForFeed();
        }
        return foundFeed;
      };
      let feed = await pollForFeed();
      await feed._contile.refresh();
      await feed.refresh({ broadcast: true });
      // Only write the about:home cache if the cache machinery has been
      // initialized. In tests where the about:home cache is disabled, init
      // early-returns before this.log is set, so calling cacheNow() crashes.
      if (AboutHomeStartupCache.initted) {
        await AboutHomeStartupCache.cacheNow();
      }
    }

    await SessionStore.promiseAllWindowsRestored;

    // Check to see if the top-most browser window still needs to fire its
    // idle tasks notification. If so, we'll wait for it before shutting
    // down, since some caching that can influence future runs in this profile
    // keys off of that notification.
    let topWin = BrowserWindowTracker.getTopWindow();
    if (topWin && topWin.gBrowserInit) {
      await topWin.gBrowserInit.idleTasksFinished.promise;
    }

    for (let domWindow of Services.wm.getEnumerator(null)) {
      domWindow.close();
    }

    try {
      Services.startup.quit(Services.startup.eForceQuit);
    } catch (e) {
      dump("Force Quit failed: " + e);
    }
  },

  receiveGetStartupInfo(message) {
    let mm = message.target.messageManager;
    let startupInfo = Services.startup.getStartupInfo();

    if (!startupInfo.firstPaint2) {
      // It's possible that we were called early enough that
      // the firstPaint2 measurement hasn't been set yet. In
      // that case, we set up an observer for the next time
      // a window is painted and re-retrieve the startup info.
      let obs = function (subject, topic) {
        Services.obs.removeObserver(this, topic);
        startupInfo = Services.startup.getStartupInfo();
        mm.sendAsyncMessage(
          "TalosPowersContent:GetStartupInfo:Result",
          startupInfo
        );
      };
      Services.obs.addObserver(obs, "widget-first-paint");
    } else {
      mm.sendAsyncMessage(
        "TalosPowersContent:GetStartupInfo:Result",
        startupInfo
      );
    }
  },

  // These services are exposed to local unprivileged content.
  // Each service is a function which accepts an argument, a callback for sending
  // the reply (possibly async), and the parent window as a utility.
  // arg/reply semantice are service-specific.
  // To add a service: add a method at ParentExecServices here, then at the content:
  // <script src="chrome://talos-powers-content/content/TalosPowersContent.js"></script>
  // and then e.g. TalosPowersParent.exec("sampleParentService", myArg, myCallback)
  // Sample service:
  /*
    // arg: anything. return: sample reply
    sampleParentService: function(arg, callback, win) {
      win.setTimeout(function() {
        callback("sample reply for: " + arg);
      }, 500);
    },
  */
  ParentExecServices: {
    ping(arg, callback) {
      callback();
    },

    // arg: ignored. return: handle (number) for use with stopFrameTimeRecording
    startFrameTimeRecording(arg, callback, win) {
      var rv = win.windowUtils.startFrameTimeRecording();
      callback(rv);
    },

    // arg: handle from startFrameTimeRecording. return: array with composition intervals
    stopFrameTimeRecording(arg, callback, win) {
      var rv = win.windowUtils.stopFrameTimeRecording(arg);
      callback(rv);
    },

    requestDumpCoverageCounters(arg, callback) {
      PerTestCoverageUtils.afterTest().then(callback);
    },

    requestResetCoverageCounters(arg, callback) {
      PerTestCoverageUtils.beforeTest().then(callback);
    },

    dumpAboutSupport(arg, callback) {
      const { Troubleshoot } = ChromeUtils.importESModule(
        "resource://gre/modules/Troubleshoot.sys.mjs"
      );
      Troubleshoot.snapshot().then(snapshot => {
        dump("about:support\t" + JSON.stringify(snapshot) + "\n");
        callback();
      });
    },
  },

  RecieveParentExecCommand(msg) {
    function sendResult(result) {
      let mm = msg.target.messageManager;
      mm.sendAsyncMessage("TalosPowers:ParentExec:ReplyMsg", {
        id: msg.data.id,
        result,
      });
    }

    let command = msg.data.command;
    if (!this.ParentExecServices.hasOwnProperty(command.name)) {
      throw new Error(
        "TalosPowers:ParentExec: Invalid service '" + command.name + "'"
      );
    }

    this.ParentExecServices[command.name](
      command.data,
      sendResult,
      msg.target.documentGlobal
    );
  },
};

this.talos_powers = class extends ExtensionAPI {
  onStartup() {
    let uri = Services.io.newURI("content/", null, this.extension.rootURI);
    resProto.setSubstitutionWithFlags(
      "talos-powers",
      uri,
      resProto.ALLOW_CONTENT_ACCESS
    );

    frameScriptURL = this.extension.rootURI.resolve(
      "chrome/talos-powers-content.js"
    );

    TalosPowersService.prototype.register();
  }

  onShutdown() {
    TalosPowersService.prototype.unregister();

    frameScriptURL = null;
    resProto.setSubstitution("talos-powers", null);
  }
};
