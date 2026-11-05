/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

let lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

/**
 * AboutHomeStartupCache is responsible for reading and writing the
 * initial about:home document from the HTTP cache as a startup
 * performance optimization. It only works when the "privileged about
 * content process" is enabled and when ENABLED_PREF is set to true.
 *
 * See https://firefox-source-docs.mozilla.org/browser/extensions/newtab/docs/v2-system-addon/about_home_startup_cache.html
 * for further details.
 */
export var AboutHomeStartupCache = {
  ABOUT_HOME_URI_STRING: "about:home",
  SCRIPT_EXTENSION: "script",
  ENABLED_PREF: "browser.startup.homepage.abouthome_cache.enabled",
  PRELOADED_NEWTAB_PREF: "browser.newtab.preload",
  LOG_LEVEL_PREF: "browser.startup.homepage.abouthome_cache.loglevel",

  // It's possible that the layout of about:home will change such that
  // we want to invalidate any pre-existing caches. We do this by setting
  // this meta key in the nsICacheEntry for the page.
  //
  // The version is currently set to the build ID, meaning that the cache
  // is invalidated after every upgrade (like the main startup cache).
  CACHE_VERSION_META_KEY: "version",

  LOG_NAME: "AboutHomeStartupCache",

  // These messages are used to request the "privileged about content process"
  // to create the cached document, and then to receive that document.
  CACHE_REQUEST_MESSAGE: "AboutHomeStartupCache:CacheRequest",
  CACHE_RESPONSE_MESSAGE: "AboutHomeStartupCache:CacheResponse",
  CACHE_USAGE_RESULT_MESSAGE: "AboutHomeStartupCache:UsageResult",

  // When a "privileged about content process" is launched, this message is
  // sent to give it some nsIInputStream's for the about:home document they
  // should load.
  SEND_STREAMS_MESSAGE: "AboutHomeStartupCache:InputStreams",

  // This time in ms is used to debounce messages that are broadcast to
  // all about:newtab's, or the preloaded about:newtab. We use those
  // messages as a signal that it's likely time to refresh the cache.
  CACHE_DEBOUNCE_RATE_MS: 5000,

  // This is how long we'll block the AsyncShutdown while waiting for
  // the cache to write. If we fail to write within that time, we will
  // allow the shutdown to proceed.
  SHUTDOWN_CACHE_WRITE_TIMEOUT_MS: 1000,

  // The following values are possible values for the
  // browser.startup.abouthome_cache_result Glean metric. Keep these in sync
  // with the metric definition in browser/components/metrics.yaml.
  // See setDeferredResult for more information.
  CACHE_RESULT_SCALARS: {
    UNSET: 0,
    DOES_NOT_EXIST: 1,
    CORRUPT_PAGE: 2,
    CORRUPT_SCRIPT: 3,
    INVALIDATED: 4,
    LATE: 5,
    VALID_AND_USED: 6,
    DISABLED: 7,
    NOT_LOADING_ABOUTHOME: 8,
    PRELOADING_DISABLED: 9,
  },

  // This will be set to one of the values of CACHE_RESULT_SCALARS
  // once it is determined which result best suits what occurred.
  _cacheDeferredResultScalar: -1,

  // A reference to the nsICacheEntry to read from and write to.
  _cacheEntry: null,

  // These nsIPipe's are sent down to the "privileged about content process"
  // immediately after the process launches. This allows us to race the loading
  // of the cache entry in the parent process with the load of the about:home
  // page in the content process, since we'll connect the InputStream's to
  // the pipes as soon as the nsICacheEntry is available.
  //
  // The page pipe is for the HTML markup for the page.
  _pagePipe: null,
  // The script pipe is for the JavaScript that the HTML markup loads
  // to set its internal state.
  _scriptPipe: null,
  _cacheDeferred: null,

  _enabled: false,
  _initted: false,
  _hasWrittenThisSession: false,
  _finalized: false,
  _firstPrivilegedProcessCreated: false,

  init() {
    if (this._initted) {
      throw new Error("AboutHomeStartupCache already initted.");
    }

    if (
      Services.startup.isInOrBeyondShutdownPhase(
        Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
      )
    ) {
      // Stay not initted, such that using us will reject or be a no-op.
      return;
    }

    this.setDeferredResult(this.CACHE_RESULT_SCALARS.UNSET);

    this._enabled = Services.prefs.getBoolPref(
      "browser.startup.homepage.abouthome_cache.enabled"
    );

    if (!this._enabled) {
      this.recordResult(this.CACHE_RESULT_SCALARS.DISABLED);
      return;
    }

    this.log = console.createInstance({
      logModule: this.LOG_NAME,
      maxLogLevelPref: this.LOG_LEVEL_PREF,
    });

    this.log.trace("Initting.");

    // If the user is not configured to load about:home at startup, then
    // let's not bother with the cache - loading it needlessly is more likely
    // to hinder what we're actually trying to load.
    let willLoadAboutHome =
      !lazy.HomePage.overridden &&
      Services.prefs.getIntPref("browser.startup.page") === 1;

    if (!willLoadAboutHome) {
      this.log.trace("Not configured to load about:home by default.");
      this.recordResult(this.CACHE_RESULT_SCALARS.NOT_LOADING_ABOUTHOME);
      return;
    }

    if (!Services.prefs.getBoolPref(this.PRELOADED_NEWTAB_PREF, false)) {
      this.log.trace("Preloaded about:newtab disabled.");
      this.recordResult(this.CACHE_RESULT_SCALARS.PRELOADING_DISABLED);
      return;
    }

    Services.obs.addObserver(this, "ipc:content-created");
    Services.obs.addObserver(this, "process-type-set");
    Services.obs.addObserver(this, "ipc:content-shutdown");
    Services.obs.addObserver(this, "intl:app-locales-changed");

    this.log.trace("Constructing pipes.");
    this._pagePipe = this.makePipe();
    this._scriptPipe = this.makePipe();

    this._cacheEntryPromise = new Promise(resolve => {
      this._cacheEntryResolver = resolve;
    });

    let lci = Services.loadContextInfo.default;
    let storage = Services.cache2.diskCacheStorage(lci);
    try {
      storage.asyncOpenURI(
        this.aboutHomeURI,
        "",
        Ci.nsICacheStorage.OPEN_PRIORITY,
        this
      );
    } catch (e) {
      this.log.error("Failed to open about:home cache entry", e);
    }

    this._cacheTask = new lazy.DeferredTask(async () => {
      await this.cacheNow();
    }, this.CACHE_DEBOUNCE_RATE_MS);

    this._shutdownBlocker = async () => {
      await this.onShutdown();
    };

    lazy.AsyncShutdown.appShutdownConfirmed.addBlocker(
      "AboutHomeStartupCache: Writing cache",
      this._shutdownBlocker,
      () => this._cacheProgress
    );

    this._cacheDeferred = null;
    this._initted = true;
    this.log.trace("Initialized.");
  },

  get initted() {
    return this._initted;
  },

  uninit() {
    if (!this._enabled) {
      return;
    }

    try {
      Services.obs.removeObserver(this, "ipc:content-created");
      Services.obs.removeObserver(this, "process-type-set");
      Services.obs.removeObserver(this, "ipc:content-shutdown");
      Services.obs.removeObserver(this, "intl:app-locales-changed");
    } catch (e) {
      // If we failed to initialize and register for these observer
      // notifications, then attempting to remove them will throw.
      // It's fine to ignore that case on shutdown.
    }

    if (this._cacheTask) {
      this._cacheTask.disarm();
      this._cacheTask = null;
    }

    this._pagePipe = null;
    this._scriptPipe = null;
    this._initted = false;
    this._cacheEntry = null;
    this._hasWrittenThisSession = false;
    this._cacheEntryPromise = null;
    this._cacheEntryResolver = null;
    this._cacheDeferredResultScalar = -1;

    if (this.log) {
      this.log.trace("Uninitialized.");
      this.log = null;
    }

    this._procManager = null;
    this._procManagerID = null;
    this._appender = null;
    this._cacheDeferred = null;
    this._finalized = false;
    this._firstPrivilegedProcessCreated = false;

    lazy.AsyncShutdown.appShutdownConfirmed.removeBlocker(
      this._shutdownBlocker
    );
    this._shutdownBlocker = null;
  },

  _aboutHomeURI: null,

  get aboutHomeURI() {
    if (this._aboutHomeURI) {
      return this._aboutHomeURI;
    }

    this._aboutHomeURI = Services.io.newURI(this.ABOUT_HOME_URI_STRING);
    return this._aboutHomeURI;
  },

  // For the AsyncShutdown blocker, this is used to populate the progress
  // value.
  _cacheProgress: "Not yet begun",

  /**
   * Called by the AsyncShutdown blocker on quit-application
   * to potentially flush the most recent cache to disk. If one was
   * never written during the session, one is generated and written
   * before the async function resolves.
   *
   * @param {boolean} withTimeout
   *   Whether or not the timeout mechanism should be used. Defaults
   *   to true.
   * @returns {Promise<boolean>}
   *   If a cache has never been written, or a cache write is in
   *   progress, resolves true when the cache has been written. Also
   *   resolves to true if a cache didn't need to be written.
   *
   *   Resolves to false if a cache write unexpectedly timed out.
   */
  async onShutdown(withTimeout = true) {
    // If we never wrote this session, arm the task so that the next
    // step can finalize.
    if (!this._hasWrittenThisSession) {
      this.log.trace("Never wrote a cache this session. Arming cache task.");
      this._cacheTask.arm();
    }

    Glean.browserStartup.abouthomeCacheShutdownwrite.set(
      this._cacheTask.isArmed
    );

    if (this._cacheTask.isArmed) {
      this.log.trace("Finalizing cache task on shutdown");
      this._finalized = true;

      // To avoid hanging shutdowns, we'll ensure that we wait a maximum of
      // SHUTDOWN_CACHE_WRITE_TIMEOUT_MS millseconds before giving up.
      const TIMED_OUT = Symbol();
      let timeoutID = 0;

      let timeoutPromise = new Promise(resolve => {
        timeoutID = lazy.setTimeout(
          () => resolve(TIMED_OUT),
          this.SHUTDOWN_CACHE_WRITE_TIMEOUT_MS
        );
      });

      let promises = [this._cacheTask.finalize()];
      if (withTimeout) {
        this.log.trace("Using timeout mechanism.");
        promises.push(timeoutPromise);
      } else {
        this.log.trace("Skipping timeout mechanism.");
      }

      let result = await Promise.race(promises);
      this.log.trace("Done blocking shutdown.");
      lazy.clearTimeout(timeoutID);
      if (result === TIMED_OUT) {
        this.log.error("Timed out getting cache streams. Skipping cache task.");
        return false;
      }
    }
    this.log.trace("onShutdown is exiting");
    return true;
  },

  /**
   * Called by the _cacheTask DeferredTask to actually do the work of
   * caching the about:home document.
   *
   * @returns {Promise<undefined>}
   *   Resolves when a fresh version of the cache has been written.
   */
  async cacheNow() {
    this.log.trace("Caching now.");
    this._cacheProgress = "Getting cache streams";

    let { pageInputStream, scriptInputStream } = await this.requestCache();

    if (!pageInputStream || !scriptInputStream) {
      this.log.trace("Failed to get cache streams.");
      this._cacheProgress = "Failed to get streams";
      return;
    }

    this.log.trace("Got cache streams.");

    this._cacheProgress = "Writing to cache";

    try {
      this.log.trace("Populating cache.");
      await this.populateCache(pageInputStream, scriptInputStream);
    } catch (e) {
      this._cacheProgress = "Failed to populate cache";
      this.log.error("Populating the cache failed: ", e);
      return;
    }

    this._cacheProgress = "Done";
    this.log.trace("Done writing to cache.");
    this._hasWrittenThisSession = true;
  },

  /**
   * Requests the cached document streams from the "privileged about content
   * process".
   *
   * @returns {Promise<object>}
   *   Resolves with an Object with the following properties:
   *
   *   pageInputStream (nsIInputStream)
   *     The page content to write to the cache, or null if request the streams
   *     failed.
   *
   *   scriptInputStream (nsIInputStream)
   *     The script content to write to the cache, or null if request the streams
   *     failed.
   */
  requestCache() {
    this.log.trace("Parent is requesting Activity Stream state object.");

    if (!this._initted) {
      this.log.error("requestCache called despite not initted!");
      return { pageInputStream: null, scriptInputStream: null };
    }

    if (!this._procManager) {
      this.log.error("requestCache called with no _procManager!");
      return { pageInputStream: null, scriptInputStream: null };
    }

    if (
      this._procManager.remoteType != lazy.E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE
    ) {
      this.log.error("Somehow got the wrong process type.");
      return { pageInputStream: null, scriptInputStream: null };
    }

    let state = lazy.AboutNewTab.activityStream.store.getState();
    return new Promise(resolve => {
      this._cacheDeferred = resolve;
      this.log.trace("Parent is requesting cache streams.");
      this._procManager.sendAsyncMessage(this.CACHE_REQUEST_MESSAGE, { state });
    });
  },

  /**
   * Helper function that returns a newly constructed nsIPipe instance.
   *
   * @returns {nsIPipe}
   */
  makePipe() {
    let pipe = Cc["@mozilla.org/pipe;1"].createInstance(Ci.nsIPipe);
    pipe.init(
      true /* non-blocking input */,
      true /* non-blocking output */,
      0 /* segment size */,
      0 /* max segments */
    );
    return pipe;
  },

  get pagePipe() {
    return this._pagePipe;
  },

  get scriptPipe() {
    return this._scriptPipe;
  },

  /**
   * Called when the nsICacheEntry has been accessed. If the nsICacheEntry
   * has content that we want to send down to the "privileged about content
   * process", then we connect that content to the nsIPipe's that may or
   * may not have already been sent down to the process.
   *
   * In the event that the nsICacheEntry doesn't contain anything usable,
   * the nsInputStreams on the nsIPipe's are closed.
   */
  connectToPipes() {
    this.log.trace(`Connecting nsICacheEntry to pipes.`);

    // If the cache doesn't yet exist, we'll know because the version metadata
    // won't exist yet.
    let version;
    try {
      this.log.trace("");
      version = this._cacheEntry.getMetaDataElement(
        this.CACHE_VERSION_META_KEY
      );
    } catch (e) {
      if (e.result == Cr.NS_ERROR_NOT_AVAILABLE) {
        this.log.debug("Cache meta data does not exist. Closing streams.");
        this.pagePipe.outputStream.close();
        this.scriptPipe.outputStream.close();
        this.setDeferredResult(this.CACHE_RESULT_SCALARS.DOES_NOT_EXIST);
        return;
      }

      throw e;
    }

    this.log.info("Version retrieved is", version);

    if (version != Services.appinfo.appBuildID) {
      this.log.info("Version does not match! Dooming and closing streams.\n");
      // This cache is no good - doom it, and prepare for a new one.
      this.clearCache();
      this.pagePipe.outputStream.close();
      this.scriptPipe.outputStream.close();
      this.setDeferredResult(this.CACHE_RESULT_SCALARS.INVALIDATED);
      return;
    }

    let cachePageInputStream;

    try {
      cachePageInputStream = this._cacheEntry.openInputStream(0);
    } catch (e) {
      this.log.error("Failed to open main input stream for cache entry", e);
      this.pagePipe.outputStream.close();
      this.scriptPipe.outputStream.close();
      this.setDeferredResult(this.CACHE_RESULT_SCALARS.CORRUPT_PAGE);
      return;
    }

    this.log.trace("Connecting page stream to pipe.");
    lazy.NetUtil.asyncCopy(
      cachePageInputStream,
      this.pagePipe.outputStream,
      () => {
        this.log.info("Page stream connected to pipe.");
      }
    );

    let cacheScriptInputStream;
    try {
      this.log.trace("Connecting script stream to pipe.");
      cacheScriptInputStream =
        this._cacheEntry.openAlternativeInputStream("script");
      lazy.NetUtil.asyncCopy(
        cacheScriptInputStream,
        this.scriptPipe.outputStream,
        () => {
          this.log.info("Script stream connected to pipe.");
        }
      );
    } catch (e) {
      if (e.result == Cr.NS_ERROR_NOT_AVAILABLE) {
        // For some reason, the script was not available. We'll close the pipe
        // without sending anything into it. The privileged about content process
        // will notice that there's nothing available in the pipe, and fall back
        // to dynamically generating the page.
        this.log.error("Script stream not available! Closing pipe.");
        this.scriptPipe.outputStream.close();
        this.setDeferredResult(this.CACHE_RESULT_SCALARS.CORRUPT_SCRIPT);
      } else {
        throw e;
      }
    }

    this.setDeferredResult(this.CACHE_RESULT_SCALARS.VALID_AND_USED);
    this.log.trace("Streams connected to pipes.");
  },

  /**
   * Called when we have received a the cache values from the "privileged
   * about content process". The page and script streams are written to
   * the nsICacheEntry.
   *
   * This writing is asynchronous, and if a write happens to already be
   * underway when this function is called, that latter call will be
   * ignored.
   *
   * @param {nsIInputStream} pageInputStream
   *   A stream containing the HTML markup to be saved to the cache.
   * @param {nsIInputStream} scriptInputStream
   *   A stream containing the JS hydration script to be saved to the cache.
   * @returns {Promise<undefined, Error>}
   *   When the cache has been successfully written to.

   *   Rejects with a JS Error if writing any part of the cache happens to
   *   fail.
   */
  async populateCache(pageInputStream, scriptInputStream) {
    await this.ensureCacheEntry();

    await new Promise((resolve, reject) => {
      // Doom the old cache entry, so we can start writing to a new one.
      this.log.trace("Populating the cache. Dooming old entry.");
      this.clearCache();

      this.log.trace("Opening the page output stream.");
      let pageOutputStream;
      try {
        pageOutputStream = this._cacheEntry.openOutputStream(0, -1);
      } catch (e) {
        reject(e);
        return;
      }

      this.log.info("Writing the page cache.");
      lazy.NetUtil.asyncCopy(pageInputStream, pageOutputStream, pageResult => {
        if (!Components.isSuccessCode(pageResult)) {
          this.log.error("Failed to write page. Result: " + pageResult);
          reject(new Error(pageResult));
          return;
        }

        this.log.trace(
          "Writing the page data is complete. Now opening the " +
            "script output stream."
        );

        let scriptOutputStream;
        try {
          scriptOutputStream = this._cacheEntry.openAlternativeOutputStream(
            "script",
            -1
          );
        } catch (e) {
          reject(e);
          return;
        }

        this.log.info("Writing the script cache.");
        lazy.NetUtil.asyncCopy(
          scriptInputStream,
          scriptOutputStream,
          scriptResult => {
            if (!Components.isSuccessCode(scriptResult)) {
              this.log.error("Failed to write script. Result: " + scriptResult);
              reject(new Error(scriptResult));
              return;
            }

            this.log.trace(
              "Writing the script cache is done. Setting version."
            );
            try {
              this._cacheEntry.setMetaDataElement(
                "version",
                Services.appinfo.appBuildID
              );
            } catch (e) {
              this.log.error("Failed to write version.");
              reject(e);
              return;
            }
            this.log.trace(`Version is set to ${Services.appinfo.appBuildID}.`);
            this.log.info("Caching of page and script is done.");
            resolve();
          }
        );
      });
    });

    this.log.trace("populateCache has finished.");
  },

  /**
   * Returns a Promise that resolves once the nsICacheEntry for the cache
   * is available to write to and read from.
   *
   * @returns {Promise<nsICacheEntry, string>}
   *   Resolves once the cache entry has become available.
   *
   *   Rejects with an error message if getting the cache entry is attempted
   *   before the AboutHomeStartupCache component has been initialized.
   */
  ensureCacheEntry() {
    if (!this._initted) {
      return Promise.reject(
        "Cannot ensureCacheEntry - AboutHomeStartupCache is not initted"
      );
    }

    return this._cacheEntryPromise;
  },

  /**
   * Clears the contents of the cache.
   */
  clearCache() {
    this.log.trace("Clearing the cache.");
    this._cacheEntry = this._cacheEntry.recreate();
    this._cacheEntryPromise = new Promise(resolve => {
      resolve(this._cacheEntry);
    });
    this._hasWrittenThisSession = false;
  },

  /**
   * Clears the contents of the cache, and then completely uninitializes the
   * AboutHomeStartupCache caching mechanism until the next time it's
   * initialized (which outside of testing scenarios, is the next browser
   * start).
   */
  clearCacheAndUninit() {
    if (this._enabled && this.initted) {
      this.log.trace("Clearing the cache and uninitializing.");
      this.clearCache();
      this.uninit();
    }
  },

  /**
   * Called when a content process is created. If this is the "privileged
   * about content process", then the cache streams will be sent to it.
   *
   * @param {number} childID
   *   The unique ID for the content process that was created, as passed by
   *   ipc:content-created.
   * @param {ProcessMessageManager} procManager
   *   The ProcessMessageManager for the created content process.
   * @param {nsIDOMProcessParent} processParent
   *   The nsIDOMProcessParent for the tab.
   */
  onContentProcessCreated(childID, procManager, processParent) {
    if (procManager.remoteType == lazy.E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE) {
      if (this._finalized) {
        this.log.trace(
          "Ignoring privileged about content process launch after finalization."
        );
        return;
      }

      if (this._firstPrivilegedProcessCreated) {
        this.log.trace(
          "Ignoring non-first privileged about content processes."
        );
        return;
      }

      this.log.trace(
        `A privileged about content process is launching with ID ${childID}.`
      );

      this.log.info("Sending input streams down to content process.");
      let actor = processParent.getActor("BrowserProcess");
      actor.sendAsyncMessage(this.SEND_STREAMS_MESSAGE, {
        pageInputStream: this.pagePipe.inputStream,
        scriptInputStream: this.scriptPipe.inputStream,
      });

      procManager.addMessageListener(this.CACHE_RESPONSE_MESSAGE, this);
      procManager.addMessageListener(this.CACHE_USAGE_RESULT_MESSAGE, this);
      this._procManager = procManager;
      this._procManagerID = childID;
      this._firstPrivilegedProcessCreated = true;
    }
  },

  /**
   * Called when a content process is destroyed. Either it shut down normally,
   * or it crashed. If this is the "privileged about content process", then some
   * internal state is cleared.
   *
   * @param {number} childID
   *   The unique ID for the content process that was created, as passed by
   *   ipc:content-shutdown.
   */
  onContentProcessShutdown(childID) {
    this.log.info(`Content process shutdown: ${childID}`);
    if (this._procManagerID == childID) {
      this.log.info("It was the current privileged about process.");
      if (this._cacheDeferred) {
        this.log.error(
          "A privileged about content process shut down while cache streams " +
            "were still en route."
        );
        // The crash occurred while we were waiting on cache input streams to
        // be returned to us. Resolve with null streams instead.
        this._cacheDeferred({ pageInputStream: null, scriptInputStream: null });
        this._cacheDeferred = null;
      }

      this._procManager.removeMessageListener(
        this.CACHE_RESPONSE_MESSAGE,
        this
      );
      this._procManager.removeMessageListener(
        this.CACHE_USAGE_RESULT_MESSAGE,
        this
      );
      this._procManager = null;
      this._procManagerID = null;
    }
  },

  /**
   * Called externally by ActivityStreamMessageChannel anytime
   * a message is broadcast to all about:newtabs, or sent to the
   * preloaded about:newtab. This is used to determine if we need
   * to refresh the cache.
   */
  onPreloadedNewTabMessage() {
    if (!this._initted || !this._enabled) {
      return;
    }

    if (this._finalized) {
      this.log.trace("Ignoring preloaded newtab update after finalization.");
      return;
    }

    this.log.trace("Preloaded about:newtab was updated.");

    this._cacheTask.disarm();
    this._cacheTask.arm();
  },

  /**
   * Stores the CACHE_RESULT_SCALARS value that most accurately represents
   * the current notion of how the cache has operated so far. It is stored
   * temporarily like this because we need to hear from the privileged
   * about content process to hear whether or not retrieving the cache
   * actually worked on that end. The success state reported back from
   * the privileged about content process will be compared against the
   * deferred result scalar to compute what will be recorded to
   * Telemetry.
   *
   * Note that this value will only be recorded if its value is GREATER
   * than the currently recorded value. This is because it's possible for
   * certain functions that record results to re-enter - but we want to record
   * the _first_ condition that caused the cache to not be read from.
   *
   * @param {number} result
   *   One of the CACHE_RESULT_SCALARS values. If this value is less than
   *   the currently recorded value, it is ignored.
   */
  setDeferredResult(result) {
    if (this._cacheDeferredResultScalar < result) {
      this._cacheDeferredResultScalar = result;
    }
  },

  /**
   * Records the final result of how the cache operated for the user
   * during this session to Telemetry.
   *
   * @param {number} result
   *   One of the result constants from CACHE_RESULT_SCALARS.
   */
  recordResult(result) {
    // Note: this can be called very early on in the lifetime of
    // AboutHomeStartupCache, so things like this.log might not exist yet.
    Glean.browserStartup.abouthomeCacheResult.set(result);
  },

  /**
   * Called when the parent process receives a message from the privileged
   * about content process saying whether or not reading from the cache
   * was successful.
   *
   * @param {boolean} success
   *   True if reading from the cache succeeded.
   */
  onUsageResult(success) {
    this.log.trace(`Received usage result. Success = ${success}`);
    if (success) {
      if (
        this._cacheDeferredResultScalar !=
        this.CACHE_RESULT_SCALARS.VALID_AND_USED
      ) {
        this.log.error(
          "Somehow got a success result despite having never " +
            "successfully sent down the cache streams"
        );
        this.recordResult(this._cacheDeferredResultScalar);
      } else {
        this.recordResult(this.CACHE_RESULT_SCALARS.VALID_AND_USED);
      }

      return;
    }

    if (
      this._cacheDeferredResultScalar ==
      this.CACHE_RESULT_SCALARS.VALID_AND_USED
    ) {
      // We failed to read from the cache despite having successfully
      // sent it down to the content process. We presume then that the
      // streams just didn't provide any bytes in time.
      this.recordResult(this.CACHE_RESULT_SCALARS.LATE);
    } else {
      // We failed to read the cache, but already knew why. We can
      // now record that value.
      this.recordResult(this._cacheDeferredResultScalar);
    }
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsICacheEntryOpenallback",
    "nsIObserver",
  ]),

  /* MessageListener */

  receiveMessage(message) {
    // Only the privileged about content process can write to the cache.
    if (
      message.target.remoteType != lazy.E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE
    ) {
      this.log.error(
        "Received a message from a non-privileged content process!"
      );
      return;
    }

    switch (message.name) {
      case this.CACHE_RESPONSE_MESSAGE: {
        this.log.trace("Parent received cache streams.");
        if (!this._cacheDeferred) {
          this.log.error("Parent doesn't have _cacheDeferred set up!");
          return;
        }

        this._cacheDeferred(message.data);
        this._cacheDeferred = null;
        break;
      }
      case this.CACHE_USAGE_RESULT_MESSAGE: {
        this.onUsageResult(message.data.success);
        break;
      }
    }
  },

  /* nsIObserver */

  observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      case "intl:app-locales-changed": {
        this.clearCache();
        break;
      }
      case "process-type-set":
      // Intentional fall-through
      case "ipc:content-created": {
        let childID = aData;
        let procManager = aSubject
          .QueryInterface(Ci.nsIInterfaceRequestor)
          .getInterface(Ci.nsIMessageSender);
        let pp = aSubject.QueryInterface(Ci.nsIDOMProcessParent);
        this.onContentProcessCreated(childID, procManager, pp);
        break;
      }

      case "ipc:content-shutdown": {
        let childID = aData;
        this.onContentProcessShutdown(childID);
        break;
      }
    }
  },

  /* nsICacheEntryOpenCallback */

  onCacheEntryCheck() {
    return Ci.nsICacheEntryOpenCallback.ENTRY_WANTED;
  },

  onCacheEntryAvailable(aEntry) {
    this.log.trace("Cache entry is available.");

    this._cacheEntry = aEntry;
    this.connectToPipes();
    this._cacheEntryResolver(this._cacheEntry);
  },
};
