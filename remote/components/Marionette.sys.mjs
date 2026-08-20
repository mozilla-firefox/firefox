/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AppInfo: "chrome://remote/content/shared/AppInfo.sys.mjs",
  Deferred: "chrome://remote/content/shared/Sync.sys.mjs",
  EnvironmentPrefs: "chrome://remote/content/marionette/prefs.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  MarionettePrefs: "chrome://remote/content/marionette/prefs.sys.mjs",
  RecommendedPreferences:
    "chrome://remote/content/shared/RecommendedPreferences.sys.mjs",
  TCPListener: "chrome://remote/content/marionette/server.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.Log.get(lazy.Log.TYPES.MARIONETTE)
);

ChromeUtils.defineLazyGetter(lazy, "textEncoder", () => new TextEncoder());

const NOTIFY_LISTENING = "marionette-listening";
const SHARED_DATA_ACTIVE_KEY = "Marionette:Active";
const SHARED_DATA_IS_BROWSER_AUTOMATION_KEY =
  "Marionette:IsBrowserAutomationRunning";

const PREF_DYNAMIC_START_ENABLED = "remote.experimental.dynamicstart.enabled";

// Complements -marionette flag for starting the Marionette server.
// We also set this if Marionette is running in order to start the server
// again after a Firefox restart.
const ENV_ENABLED = "MOZ_MARIONETTE";

// Besides starting based on existing prefs in a profile and a command
// line flag, we also support inheriting prefs out of an env var, and to
// start Marionette that way.
//
// This allows marionette prefs to persist when we do a restart into
// a different profile in order to test things like Firefox refresh.
// The environment variable itself, if present, is interpreted as a
// JSON structure, with the keys mapping to preference names in the
// "marionette." branch, and the values to the values of those prefs. So
// something like {"port": 4444} would result in the marionette.port
// pref being set to 4444.
const ENV_PRESERVE_PREFS = "MOZ_MARIONETTE_PREF_STATE_ACROSS_RESTARTS";

const EXIT_CODE_NOT_AVAILABLE = 69;

const isRemote =
  Services.appinfo.processType == Services.appinfo.PROCESS_TYPE_CONTENT;

class MarionetteParentProcess {
  #browserStartupFinished;
  #isBrowserAutomation;

  constructor() {
    this.server = null;
    this._activePortPath;

    // Initially set the enabled state based on the environment variable.
    this.enabled = Services.env.exists(ENV_ENABLED);

    // Whether the running instance belongs to a browser automation session.
    // True for command line startup. Optional for startAtRuntime.
    this.#isBrowserAutomation = true;

    this.#browserStartupFinished = lazy.Deferred();
  }

  /**
   * A promise that resolves when the initial application window has been opened.
   *
   * @returns {Promise}
   *     Promise that resolves when the initial application window is open.
   */
  get browserStartupFinished() {
    return this.#browserStartupFinished.promise;
  }

  get enabled() {
    return this._enabled;
  }

  set enabled(value) {
    // Return early if Marionette is already marked as being enabled.
    // There is also no possibility to disable Marionette once it got enabled.
    if (this._enabled || !value) {
      return;
    }

    this._enabled = value;
    lazy.logger.info(value ? "Marionette enabled" : "Marionette disabled");
  }

  get running() {
    return !!this.server && this.server.alive;
  }

  get isBrowserAutomationRunning() {
    return this.running && this.#isBrowserAutomation;
  }

  /**
   * Syncs the Marionette active flag with the web content processes.
   *
   * @param {boolean} value - Flag indicating if Marionette is active or not.
   */
  updateWebdriverActiveFlag(value) {
    Services.ppmm.sharedData.set(SHARED_DATA_ACTIVE_KEY, value);
    Services.ppmm.sharedData.set(
      SHARED_DATA_IS_BROWSER_AUTOMATION_KEY,
      value && this.#isBrowserAutomation
    );
    Services.ppmm.sharedData.flush();
  }

  handle(cmdLine) {
    // `handle` is called too late in certain cases (eg safe mode, see comment
    // above "command-line-startup"). So the marionette command line argument
    // will always be processed in `observe`.
    // However it still needs to be consumed by the command-line-handler API,
    // to avoid issues on macos.
    // TODO: remove after Bug 1724251 is fixed.
    cmdLine.handleFlag("marionette", false);
  }

  async observe(subject, topic, data) {
    if (this.enabled) {
      lazy.logger.trace(`Received observer notification ${topic}`);
    }

    switch (topic) {
      case "profile-after-change":
        Services.obs.addObserver(this, "command-line-startup");
        break;

      // In safe mode the command line handlers are getting parsed after the
      // safe mode dialog has been closed. To allow Marionette to start
      // earlier, use the CLI startup observer notification for
      // special-cased handlers, which gets fired before the dialog appears.
      case "command-line-startup":
        Services.obs.removeObserver(this, topic);

        this.enabled = subject.handleFlag("marionette", false);

        if (this.enabled) {
          // Add annotation to crash report to indicate whether
          // Marionette was active.
          Services.appinfo.annotateCrashReport("Marionette", true);

          // Marionette needs to be initialized before any window is shown.
          Services.obs.addObserver(this, "final-ui-startup");

          // We want to suppress the modal dialog that's shown
          // when starting up in safe-mode to enable testing.
          if (Services.appinfo.inSafeMode) {
            Services.obs.addObserver(this, "domwindowopened");
          }

          lazy.RecommendedPreferences.applyPreferences();

          // Only set preferences to preserve in a new profile
          // when Marionette is enabled.
          for (let [pref, value] of lazy.EnvironmentPrefs.from(
            ENV_PRESERVE_PREFS
          )) {
            switch (typeof value) {
              case "string":
                Services.prefs.setStringPref(pref, value);
                break;
              case "boolean":
                Services.prefs.setBoolPref(pref, value);
                break;
              case "number":
                Services.prefs.setIntPref(pref, value);
                break;
              default:
                throw new TypeError(`Invalid preference type: ${typeof value}`);
            }
          }
        }
        break;

      case "domwindowopened":
        Services.obs.removeObserver(this, topic);
        this.suppressSafeModeDialog(subject);
        break;

      case "final-ui-startup":
        Services.obs.removeObserver(this, topic);

        Services.obs.addObserver(this, "before-cancel-download-prompt");
        Services.obs.addObserver(this, "browser-idle-startup-tasks-finished");
        Services.obs.addObserver(this, "mail-idle-startup-tasks-finished");

        Services.obs.addObserver(this, "quit-application");
        Services.obs.addObserver(this, "xpcom-shutdown");
        Services.obs.addObserver(this, "xpcom-shutdown-threads");

        await this.init();
        break;

      // Used to wait until the initial application window has been opened.
      case "browser-idle-startup-tasks-finished":
      case "mail-idle-startup-tasks-finished":
        Services.obs.removeObserver(
          this,
          "browser-idle-startup-tasks-finished"
        );
        Services.obs.removeObserver(this, "mail-idle-startup-tasks-finished");
        this.#browserStartupFinished.resolve();
        break;

      case "quit-application":
        Services.obs.removeObserver(this, topic);
        // Remove this observer here rather than inside the handler itself,
        // because on some platforms the notification fires multiple times
        // and removing an already-removed observer would throw.
        Services.obs.removeObserver(this, "before-cancel-download-prompt");
        lazy.logger.trace(
          `Application is shutting down with reason: "${data || "unknown"}"`
        );
        await this.uninit();
        break;

      // Before the cancel download prompt appears,
      // skip the prompt and force canceling of downloads.
      case "before-cancel-download-prompt": {
        const showPrompt = subject.QueryInterface(Ci.nsISupportsPRBool);
        showPrompt.data = false;
        break;
      }

      // Used for logging purposes to help identify slow shutdown sequences.
      case "xpcom-shutdown":
      case "xpcom-shutdown-threads":
        Services.obs.removeObserver(this, topic);
        break;
    }
  }

  suppressSafeModeDialog(win) {
    win.addEventListener(
      "load",
      () => {
        let dialog = win.document.getElementById("safeModeDialog");
        if (dialog) {
          // accept the dialog to start in safe-mode
          lazy.logger.trace("Safe mode detected, suppressing dialog");
          win.setTimeout(() => {
            dialog.getButton("accept").click();
          });
        }
      },
      { once: true }
    );
  }

  async init() {
    if (!this.enabled || this.running) {
      lazy.logger.debug(
        `Init aborted (enabled=${this.enabled}, running=${this.running})`
      );
      return;
    }

    lazy.logger.debug("Initializing Marionette");

    try {
      this.server = new lazy.TCPListener(lazy.MarionettePrefs.port);
      await this.server.start();
    } catch (e) {
      lazy.logger.fatal("Marionette server failed to start", e);
      Services.startup.quit(
        Ci.nsIAppStartup.eForceQuit,
        EXIT_CODE_NOT_AVAILABLE
      );
      return;
    }

    this.updateWebdriverActiveFlag(true);

    Services.env.set(ENV_ENABLED, "1");
    Services.obs.notifyObservers(this, NOTIFY_LISTENING, true);
    lazy.logger.debug("Marionette is listening");

    // Write Marionette port to MarionetteActivePort file within the profile.
    this._activePortPath = PathUtils.join(
      PathUtils.profileDir,
      "MarionetteActivePort"
    );

    const data = `${this.server.port}`;
    try {
      await IOUtils.write(this._activePortPath, lazy.textEncoder.encode(data));
    } catch (e) {
      lazy.logger.warn(
        `Failed to create ${this._activePortPath} (${e.message})`
      );
    }
  }

  /**
   * Start Marionette at runtime, unless it was already enabled via command line
   * arguments.
   *
   * @param {object=} options
   * @param {boolean=} options.isBrowserAutomation
   *     True if the server is started for regular browser automation (as
   *     opposed to tooling, agentic assisted browsing etc.). Defaults to true.
   *
   * @returns {number}
   *     The port on which Marionette was started. -1 if it could not be started.
   */
  async startAtRuntime(options = {}) {
    const { isBrowserAutomation = true } = options;

    if (!Services.prefs.getBoolPref(PREF_DYNAMIC_START_ENABLED, false)) {
      lazy.logger.debug(
        `Start aborted, ${PREF_DYNAMIC_START_ENABLED} is disabled`
      );
      return -1;
    }

    if (this.running) {
      lazy.logger.debug(
        `Start aborted, Marionette already running (running=${this.running})`
      );
      return -1;
    }

    if (!lazy.AppInfo.isFirefox) {
      throw new Error("Marionette start is only supported for Firefox Desktop");
    }

    lazy.logger.debug("Starting Marionette");

    // Make sure the application window is ready.
    const win = Services.wm.getMostRecentBrowserWindow();
    if (win && win.gBrowserInit.idleTasksFinished) {
      lazy.logger.debug("Waiting for gBrowserInit.idleTasksFinished");

      await win.gBrowserInit.idleTasksFinished;

      // Explicitly resolve() browserStartupFinished because in most cases the
      // startup should already be done at this point and we can't monitor the
      // observer notification.
      this.#browserStartupFinished.resolve();
    } else {
      throw new Error(
        "Could not start Marionette dynamically with no window available"
      );
    }

    if (Services.startup.startingUp) {
      throw new Error(
        "Could not start remote agent dynamically, application window still starting up"
      );
    }

    this.#isBrowserAutomation = isBrowserAutomation;

    // Enable marionette explicitly, the `running` flag is inferred from the
    // server status.
    this.enabled = true;

    Services.obs.addObserver(this, "quit-application");
    Services.obs.addObserver(this, "xpcom-shutdown");
    Services.obs.addObserver(this, "xpcom-shutdown-threads");

    try {
      await this.init();
    } catch (e) {
      throw Error(`Unable to start Marionette: ${e}`);
    }

    return this.server.port;
  }

  /**
   * Stop Marionette during runtime. This entry point is only meant to be
   * called if Marionette was started via startAtRuntime.
   */
  async stopAtRuntime() {
    await this.uninit();

    Services.obs.removeObserver(this, "quit-application");
    Services.obs.removeObserver(this, "xpcom-shutdown");
    Services.obs.removeObserver(this, "xpcom-shutdown-threads");

    // Note: directly flip the private _enabled property here, as set enabled()
    // prevents flipping the flag back to false and changing this breaks tests
    // relying on in-app restarts.
    this._enabled = false;
  }

  async uninit() {
    if (this.running) {
      await this.server.stop();
      this.updateWebdriverActiveFlag(false);

      Services.obs.notifyObservers(this, NOTIFY_LISTENING);

      try {
        await IOUtils.remove(this._activePortPath);
      } catch (e) {
        lazy.logger.warn(
          `Failed to remove ${this._activePortPath} (${e.message})`
        );
      }

      lazy.logger.debug("Marionette stopped listening");
    }
  }

  // XPCOM

  helpInfo = "  --marionette       Enable remote control server.\n";

  QueryInterface = ChromeUtils.generateQI([
    "nsICommandLineHandler",
    "nsIMarionette",
    "nsIObserver",
  ]);
}

class MarionetteContentProcess {
  get running() {
    return Services.cpmm.sharedData.get(SHARED_DATA_ACTIVE_KEY) ?? false;
  }

  get isBrowserAutomationRunning() {
    return (
      Services.cpmm.sharedData.get(SHARED_DATA_IS_BROWSER_AUTOMATION_KEY) ??
      false
    );
  }

  // XPCOM

  QueryInterface = ChromeUtils.generateQI(["nsIMarionette"]);
}

export var Marionette;
if (isRemote) {
  Marionette = new MarionetteContentProcess();
} else {
  Marionette = new MarionetteParentProcess();
}

// This is used by the XPCOM codepath which expects a constructor
export const MarionetteFactory = function () {
  return Marionette;
};
