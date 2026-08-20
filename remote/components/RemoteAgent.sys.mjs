/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AppInfo: "chrome://remote/content/shared/AppInfo.sys.mjs",
  Deferred: "chrome://remote/content/shared/Sync.sys.mjs",
  HttpServer: "chrome://remote/content/server/httpd.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  PollPromise: "chrome://remote/content/shared/Sync.sys.mjs",
  RecommendedPreferences:
    "chrome://remote/content/shared/RecommendedPreferences.sys.mjs",
  WebDriverBiDi: "chrome://remote/content/webdriver-bidi/WebDriverBiDi.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () => lazy.Log.get());

const DEFAULT_HOST = "localhost";
const DEFAULT_PORT = 9222;

// Adds various command-line arguments as environment variables to preserve
// their values when the application is restarted internally.
const ENV_ALLOW_SYSTEM_ACCESS = "MOZ_REMOTE_ALLOW_SYSTEM_ACCESS";

const SHARED_DATA_ACTIVE_KEY = "RemoteAgent:Active";
const SHARED_DATA_IS_BROWSER_AUTOMATION_KEY =
  "RemoteAgent:IsBrowserAutomationRunning";

const PREF_DYNAMIC_START_ENABLED = "remote.experimental.dynamicstart.enabled";

const EXIT_CODE_NOT_AVAILABLE = 69;

const isRemote =
  Services.appinfo.processType == Services.appinfo.PROCESS_TYPE_CONTENT;

class RemoteAgentParentProcess {
  #allowHosts;
  #allowOrigins;
  #allowSystemAccess;
  #browserStartupFinished;
  #enabled;
  #host;
  #isBrowserAutomation;
  #port;
  #server;

  #webDriverBiDi;

  constructor() {
    this.#allowHosts = null;
    this.#allowOrigins = null;
    this.#allowSystemAccess = Services.env.get(ENV_ALLOW_SYSTEM_ACCESS) == "1";

    this.#browserStartupFinished = lazy.Deferred();
    this.#enabled = false;

    // Whether the running instance belongs to a browser automation session.
    // True for command line startup. Optional for startAtRuntime.
    this.#isBrowserAutomation = true;

    // Configuration for httpd.js
    this.#host = DEFAULT_HOST;
    this.#port = DEFAULT_PORT;
    this.#server = null;

    // Supported protocols
    this.#webDriverBiDi = null;
  }

  get allowHosts() {
    if (this.#allowHosts !== null) {
      return this.#allowHosts;
    }

    if (this.#server) {
      // If the server is bound to a hostname, not an IP address, return it as
      // allowed host.
      const hostUri = Services.io.newURI(`https://${this.#host}`);
      if (!this.#isIPAddress(hostUri)) {
        return [RemoteAgent.host];
      }

      // Following Bug 1220810 localhost is guaranteed to resolve to a loopback
      // address (127.0.0.1 or ::1) unless network.proxy.allow_hijacking_localhost
      // is set to true, which should not be the case.
      const loopbackAddresses = ["127.0.0.1", "[::1]"];

      // If the server is bound to an IP address and this IP address is a localhost
      // loopback address, return localhost as allowed host.
      if (loopbackAddresses.includes(this.#host)) {
        return ["localhost"];
      }
    }

    // Otherwise return an empty array.
    return [];
  }

  get allowOrigins() {
    return this.#allowOrigins;
  }

  get allowSystemAccess() {
    return this.#allowSystemAccess;
  }

  set allowSystemAccess(value) {
    // Return early if system access is already marked being allowed.
    // There is also no possibility to disallow once it got allowed except
    // quitting Firefox and starting it again.
    if (this.#allowSystemAccess || !value) {
      return;
    }

    this.#allowSystemAccess = true;
    Services.env.set(ENV_ALLOW_SYSTEM_ACCESS, "1");
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
    return this.#enabled;
  }

  get host() {
    return this.#host;
  }

  get port() {
    return this.#port;
  }

  get running() {
    return !!this.#server && !this.#server.isStopped();
  }

  get isBrowserAutomationRunning() {
    return this.running && this.#isBrowserAutomation;
  }

  get scheme() {
    return this.#server?.identity.primaryScheme;
  }

  get server() {
    return this.#server;
  }

  /**
   * Syncs the WebDriver active flag with the web content processes.
   *
   * @param {boolean} value - Flag indicating if Remote Agent is active or not.
   */
  updateWebdriverActiveFlag(value) {
    Services.ppmm.sharedData.set(SHARED_DATA_ACTIVE_KEY, value);
    Services.ppmm.sharedData.set(
      SHARED_DATA_IS_BROWSER_AUTOMATION_KEY,
      value && this.#isBrowserAutomation
    );
    Services.ppmm.sharedData.flush();
  }

  get webDriverBiDi() {
    return this.#webDriverBiDi;
  }

  #addShutdownObservers() {
    Services.obs.addObserver(this, "quit-application");
    Services.obs.addObserver(this, "xpcom-shutdown");
    Services.obs.addObserver(this, "xpcom-shutdown-threads");
  }

  #removeShutdownObservers() {
    Services.obs.removeObserver(this, "quit-application");
    Services.obs.removeObserver(this, "xpcom-shutdown");
    Services.obs.removeObserver(this, "xpcom-shutdown-threads");
  }

  /**
   * Handle the --remote-debugging-port command line argument.
   *
   * @param {nsICommandLine} cmdLine
   *     Instance of the command line interface.
   *
   * @returns {boolean}
   *     Return `true` if the command line argument has been found.
   */
  #handleRemoteDebuggingPortFlag(cmdLine) {
    let enabled = false;

    try {
      // Catch cases when the argument, and a port have been specified.
      const port = cmdLine.handleFlagWithParam("remote-debugging-port", false);
      if (port !== null) {
        enabled = true;

        // In case of an invalid port keep the default port
        const parsed = Number(port);
        if (!isNaN(parsed)) {
          this.#port = parsed;
        }
      }
    } catch (e) {
      // If no port has been given check for the existence of the argument.
      enabled = cmdLine.handleFlag("remote-debugging-port", false);
    }

    return enabled;
  }

  #handleAllowHostsFlag(cmdLine) {
    try {
      const hosts = cmdLine.handleFlagWithParam("remote-allow-hosts", false);
      return hosts.split(",");
    } catch (e) {
      return null;
    }
  }

  #handleAllowOriginsFlag(cmdLine) {
    try {
      const origins = cmdLine.handleFlagWithParam(
        "remote-allow-origins",
        false
      );
      return origins.split(",");
    } catch (e) {
      return null;
    }
  }

  #handleAllowSystemAccessFlag(cmdLine) {
    try {
      return cmdLine.handleFlag("remote-allow-system-access", false);
    } catch (e) {
      return false;
    }
  }

  /**
   * Check if the provided URI's host is an IP address.
   *
   * @param {nsIURI} uri
   *     The URI to check.
   * @returns {boolean}
   */
  #isIPAddress(uri) {
    try {
      // getBaseDomain throws an explicit error if the uri host is an IP address.
      Services.eTLD.getBaseDomain(uri);
    } catch (e) {
      return e.result == Cr.NS_ERROR_HOST_IS_IP_ADDRESS;
    }
    return false;
  }

  async #listen(port) {
    if (Services.appinfo.processType != Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT) {
      throw Components.Exception(
        "May only be instantiated in parent process",
        Cr.NS_ERROR_LAUNCHED_CHILD_PROCESS
      );
    }

    if (this.running) {
      return;
    }

    // Try to resolve localhost to an IPv4  and / or IPv6 address so that the
    // server can be started on a given IP. Only fallback to use localhost if
    // the hostname cannot be resolved.
    //
    // Note: This doesn't force httpd.js to use the dual stack support.
    let isIPv4Host = false;
    try {
      const addresses = await this.#resolveHostname(DEFAULT_HOST);
      lazy.logger.trace(
        `Available local IP addresses: ${addresses.join(", ")}`
      );

      // Prefer IPv4 over IPv6 addresses.
      const addressesIPv4 = addresses.filter(value => !value.includes(":"));
      isIPv4Host = !!addressesIPv4.length;
      if (isIPv4Host) {
        this.#host = addressesIPv4[0];
      } else {
        this.#host = addresses.length ? addresses[0] : DEFAULT_HOST;
      }
    } catch (e) {
      this.#host = DEFAULT_HOST;

      lazy.logger.debug(
        `Failed to resolve hostname "localhost" to IP address: ${e.message}`
      );
    }

    // nsIServerSocket uses -1 for atomic port allocation
    if (port === 0) {
      port = -1;
    }

    try {
      this.#server = new lazy.HttpServer();
      const host = isIPv4Host ? DEFAULT_HOST : this.#host;

      let error;
      await lazy.PollPromise(
        (resolve, reject) => {
          try {
            this.server._start(port, host);
            this.#port = this.server._port;
            resolve();
          } catch (e) {
            error = e;
            lazy.logger.debug(`Could not bind to port ${port} (${error.name})`);
            reject();
          }
        },
        { interval: 250, timeout: 5000 }
      );

      if (!this.#server._socket) {
        throw new Error(`Failed to start HTTP server on port ${port}`);
      }

      if (isIPv4Host) {
        // Bug 1783938: httpd.js refuses connections when started on a IPv4
        // address. As workaround start on localhost and add another identity
        // for that IP address.
        this.server.identity.add("http", this.#host, this.#port);
      }

      this.updateWebdriverActiveFlag(true);

      Services.obs.notifyObservers(null, "remote-listening", true);

      await this.#webDriverBiDi?.start();
    } catch (e) {
      await this.#stop();
      lazy.logger.error(
        `Unable to start the RemoteAgent: ${e.message}, closing`,
        e
      );

      Services.startup.quit(
        Ci.nsIAppStartup.eForceQuit,
        EXIT_CODE_NOT_AVAILABLE
      );
    }
  }

  /**
   * Resolves a hostname to one or more IP addresses.
   *
   * @param {string} hostname
   *
   * @returns {Array<string>}
   */
  #resolveHostname(hostname) {
    return new Promise((resolve, reject) => {
      let originalRequest;

      const onLookupCompleteListener = {
        onLookupComplete(request, record, status) {
          if (request === originalRequest) {
            if (!Components.isSuccessCode(status)) {
              reject({ message: ChromeUtils.getXPCOMErrorName(status) });
              return;
            }

            record.QueryInterface(Ci.nsIDNSAddrRecord);

            const addresses = [];
            while (record.hasMore()) {
              let addr = record.getNextAddrAsString();
              if (addr.includes(":") && !addr.startsWith("[")) {
                // Make sure that the IPv6 address is wrapped with brackets.
                addr = `[${addr}]`;
              }
              if (!addresses.includes(addr)) {
                // Sometimes there are duplicate records with the same IP.
                addresses.push(addr);
              }
            }

            resolve(addresses);
          }
        },
      };

      try {
        originalRequest = Services.dns.asyncResolve(
          hostname,
          Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
          Ci.nsIDNSService.RESOLVE_BYPASS_CACHE,
          null,
          onLookupCompleteListener,
          null, //Services.tm.mainThread,
          {} /* defaultOriginAttributes */
        );
      } catch (e) {
        reject({ message: e.message });
      }
    });
  }

  async #stop() {
    if (!this.running) {
      return;
    }

    // Stop each protocol before stopping the HTTP server.
    await this.#webDriverBiDi?.stop();

    try {
      await this.#server.stop();
      this.#server = null;

      this.updateWebdriverActiveFlag(false);

      Services.obs.notifyObservers(null, "remote-listening");
    } catch (e) {
      // this function must never fail
      lazy.logger.error("Unable to stop listener", e);
    }
  }

  handle(cmdLine) {
    // All supported command line arguments have to be consumed in
    // nsICommandLineHandler:handle to avoid issues on macos.
    // See Marionette.sys.mjs::handle() for more details.
    // TODO: remove after Bug 1724251 is fixed.
    try {
      cmdLine.handleFlagWithParam("remote-debugging-port", false);
    } catch (e) {
      cmdLine.handleFlag("remote-debugging-port", false);
    }

    cmdLine.handleFlag("remote-allow-system-access", false);
    cmdLine.handleFlagWithParam("remote-allow-hosts", false);
    cmdLine.handleFlagWithParam("remote-allow-origins", false);
  }

  async observe(subject, topic, data) {
    if (this.#enabled) {
      lazy.logger.trace(`Received observer notification ${topic}`);
    }

    switch (topic) {
      case "profile-after-change":
        Services.obs.addObserver(this, "command-line-startup");
        break;

      case "command-line-startup":
        Services.obs.removeObserver(this, topic);

        this.#allowHosts = this.#handleAllowHostsFlag(subject);
        this.#allowOrigins = this.#handleAllowOriginsFlag(subject);
        this.allowSystemAccess = this.#handleAllowSystemAccessFlag(subject);

        this.#enabled = this.#handleRemoteDebuggingPortFlag(subject);

        if (this.#enabled) {
          // Add annotation to crash report to indicate whether the
          // Remote Agent was active.
          Services.appinfo.annotateCrashReport("RemoteAgent", true);

          Services.obs.addObserver(this, "final-ui-startup");
          Services.obs.addObserver(this, "before-cancel-download-prompt");
          Services.obs.addObserver(this, "browser-idle-startup-tasks-finished");
          Services.obs.addObserver(this, "mail-idle-startup-tasks-finished");

          this.#addShutdownObservers();

          // Apply the common set of preferences for all supported protocols
          lazy.RecommendedPreferences.applyPreferences();

          this.#webDriverBiDi = new lazy.WebDriverBiDi(this);
          lazy.logger.debug("WebDriver BiDi enabled");
        }
        break;

      case "final-ui-startup":
        Services.obs.removeObserver(this, topic);

        try {
          await this.#listen(this.#port);
        } catch (e) {
          throw Error(`Unable to start remote agent: ${e}`);
        }

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

      // Listen for application shutdown to also shutdown the Remote Agent
      // and a possible running instance of httpd.js.
      case "quit-application":
        Services.obs.removeObserver(this, topic);
        // Remove this observer here rather than inside the handler itself,
        // because on some platforms the notification fires multiple times
        // and removing an already-removed observer would throw.
        Services.obs.removeObserver(this, "before-cancel-download-prompt");
        lazy.logger.trace(
          `Application is shutting down with reason: "${data || "unknown"}"`
        );
        this.#stop();
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

  receiveMessage({ name }) {
    switch (name) {
      case "RemoteAgent:IsRunning":
        return this.running;

      default:
        lazy.logger.warn("Unknown IPC message to parent process: " + name);
        return null;
    }
  }

  /**
   * Start RemoteAgent at runtime, unless already running via command line
   * arguments.
   *
   * @param {object=} options
   * @param {boolean=} options.isBrowserAutomation
   *     True if the server is started for regular browser automation (as
   *     opposed to tooling, agentic assisted browsing etc.). Defaults to true.
   *
   * @returns {number}
   *     The port on which RemoteAgent started.
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
        `Start aborted, RemoteAgent already running (running=${this.running})`
      );
      return -1;
    }

    if (!lazy.AppInfo.isFirefox) {
      throw new Error(
        "RemoteAgent start is only supported for Firefox Desktop"
      );
    }

    // Make sure the application window is ready.
    const win = Services.wm.getMostRecentBrowserWindow();
    if (win && win.gBrowserInit.idleTasksFinished) {
      await win.gBrowserInit.idleTasksFinished;
    } else {
      throw new Error(
        "Could not start remote agent dynamically with no window available"
      );
    }

    if (Services.startup.startingUp) {
      throw new Error(
        "Could not start remote agent dynamically, application window still starting up"
      );
    }

    // Explicitly resolve() browserStartupFinished because in most cases the
    // startup should already be done at this point and we can't monitor the
    // observer notification.
    this.#browserStartupFinished.resolve();

    this.#isBrowserAutomation = isBrowserAutomation;

    // Start the RemoteAgent server.
    this.#enabled = true;
    this.#addShutdownObservers();
    this.#webDriverBiDi = new lazy.WebDriverBiDi(this);

    try {
      await this.#listen(this.#port);
    } catch (e) {
      throw Error(`Unable to start remote agent: ${e}`);
    }

    return this.#port;
  }

  /**
   * Stop remote agent during runtime. This entry point is only meant to be
   * called if RemoteAgent was started via startAtRuntime.
   */
  async stopAtRuntime() {
    await this.#stop();
    this.#removeShutdownObservers();
    this.#enabled = false;
  }

  // XPCOM

  helpInfo = `  --remote-debugging-port [<port>] Start the Firefox Remote Agent,
                     which is a low-level remote debugging interface used for WebDriver
                     BiDi. Defaults to port 9222.
  --remote-allow-hosts <hosts> Values of the Host header to allow for incoming requests.
                     Please read security guidelines at https://firefox-source-docs.mozilla.org/remote/Security.html
  --remote-allow-origins <origins> Values of the Origin header to allow for incoming requests.
                     Please read security guidelines at https://firefox-source-docs.mozilla.org/remote/Security.html
  --remote-allow-system-access Enable privileged access to the application's parent process\n`;

  QueryInterface = ChromeUtils.generateQI([
    "nsICommandLineHandler",
    "nsIObserver",
    "nsIRemoteAgent",
  ]);
}

class RemoteAgentContentProcess {
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

  QueryInterface = ChromeUtils.generateQI(["nsIRemoteAgent"]);
}

export var RemoteAgent;
if (isRemote) {
  RemoteAgent = new RemoteAgentContentProcess();
} else {
  RemoteAgent = new RemoteAgentParentProcess();
}

// This is used by the XPCOM codepath which expects a constructor
export var RemoteAgentFactory = function () {
  return RemoteAgent;
};
