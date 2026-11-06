/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import {DeferredTask} from "resource://gre/modules/DeferredTask.sys.mjs"
 * @import {RemoteSettingsClient} from "resource://services-settings/RemoteSettingsClient.sys.mjs"
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

// Ideally, we'd have this be a separate JSON file that can be loaded at runtime
// using import BUNDLED_MANIFEST from "url" with { type: "json" }.
// Unfortunately, this is not yet available to system modules - see bug 1983997.
//
// Do not read this BUNDLED_MANIFEST.version constant directly. Use the static
// getter on RemoteRenderer instead, so that the bundled version number can be
// stubbed out in tests.
const BUNDLED_MANIFEST = Object.freeze({
  version: "0.0.0-alpha",
  jsHash: "tQLriCnN",
  cssHash: "BVSpgoIh",
});

const BUNDLED_SCRIPT_URI = `resource://newtab/data/content/bundled-renderer-index.js`;
const BUNDLED_STYLE_URI = `resource://newtab/data/content/bundled-renderer.css`;

const PREF_REMOTE_RENDERER_VERSION =
  "browser.newtabpage.activity-stream.remote-renderer.version";
const COLLECTION_NAME = "newtab-renderer";
const QUIT_TOPIC = "quit-application-granted";

const lazy = XPCOMUtils.declareLazy({
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  cacheStorage: () => {
    return Services.cache2.diskCacheStorage(Services.loadContextInfo.default);
  },
  remoteRendererVersion: {
    pref: PREF_REMOTE_RENDERER_VERSION,
    default: "",
  },
  logConsole: () =>
    console.createInstance({
      logModule: "RemoteRenderer",
      // @backward-compat { version 162 }
      // Older versions ignore logModule and name the module after the prefix.
      // Remove prefix: once 162 reaches Release.
      prefix: "RemoteRenderer",
      maxLogLevel: Services.prefs.getBoolPref(
        "browser.newtabpage.activity-stream.remote-renderer.log",
        false
      )
        ? "Debug"
        : "Warn",
    }),
});

/**
 * RemoteRenderer manages the retrieval and caching of newtab renderer bundles
 * from Remote Settings. It handles versioning, cache validation, and fallback
 * to bundled resources when cached content is unavailable or invalid.
 */
export class RemoteRenderer {
  /**
   * Remote Settings client for fetching renderer bundles.
   *
   * @type {RemoteSettingsClient}
   */
  #rsClient = null;

  /**
   * nsIURIs mapping to nsICacheEntry's that are unlikely to be used anymore
   * and can be doomed on shutdown.
   *
   * @type {nsIURI[]}
   */
  #cacheEntryURIsToDoomAtShutdown = [];

  /**
   * A DeferredTask to debounce revalidating against RemoteSettings, to see
   * if there is a new version to cache and serve.
   *
   * @type {DeferredTask|null}
   */
  #revalidateDebouncer = null;

  /**
   * The current bundled version of the renderer. Exposed as a static getter
   * for easier stubbing out in tests.
   *
   * @type {string}
   */
  static get BUNDLED_VERSION() {
    return BUNDLED_MANIFEST.version;
  }

  /**
   * We debounce renderer requests by this amount before we attempt a refresh
   * from RemoteSettings. Exposed as a static getter for easier stubbing out
   * in tests.
   */
  static get REVALIDATION_DEBOUNCE_RATE_MS() {
    return 500; // ms
  }

  constructor() {
    lazy.logConsole.log("RemoteRenderer constructed.");
    this.#rsClient = lazy.RemoteSettings(COLLECTION_NAME);
    this.#revalidateDebouncer = new lazy.DeferredTask(async () => {
      await this.maybeRevalidate();
    }, RemoteRenderer.REVALIDATION_DEBOUNCE_RATE_MS);

    Services.obs.addObserver(this, QUIT_TOPIC, true);
  }

  QueryInterface = ChromeUtils.generateQI([
    Ci.nsIObserver,
    Ci.nsISupportsWeakReference,
  ]);

  observe(_subject, topic, _data) {
    if (topic === QUIT_TOPIC) {
      this.onShutdown();
      Services.obs.removeObserver(this, QUIT_TOPIC);
    }
  }

  onShutdown() {
    for (let uri of this.#cacheEntryURIsToDoomAtShutdown) {
      lazy.cacheStorage.asyncDoomURI(uri, "", null);
    }
    this.#cacheEntryURIsToDoomAtShutdown = [];
  }

  /**
   * Returns true if the cache entry associated with the passed in URI is
   * scheduled to be doomed on shutdown.
   *
   * @param {nsIURI} uri
   * @returns {boolean}
   */
  willDoomOnShutdown(uri) {
    return this.#cacheEntryURIsToDoomAtShutdown.some(doomedURI =>
      doomedURI.equals(uri)
    );
  }

  /**
   * Queues a nsICacheEntry mapping to the nsIURI to be doomed on shutdown.
   *
   * @param {nsIURI} uri - Cache key nsIURI to doom on shutdown.
   */
  doomCacheEntryOnShutdown(uri) {
    this.#cacheEntryURIsToDoomAtShutdown.push(uri);
  }

  /**
   * Returns a renderer configuration based on cached or bundled resources.
   * Checks for a valid cached renderer matching the version stored in prefs.
   * If all cache entries exist (manifest, script, style), returns the cached
   * renderer configuration.
   *
   * Otherwise, clears cache prefs and returns bundled renderer configuration.
   *
   * @returns {Promise<object>} Renderer configuration with appProps containing
   *   manifest, renderUpdate flag, isCached flag, isStaleData flag, and initialState.
   */
  async assign() {
    this.#revalidateDebouncer.disarm();
    this.#revalidateDebouncer.arm();

    if (lazy.remoteRendererVersion) {
      const version = lazy.remoteRendererVersion;
      lazy.logConsole.debug(
        `Evaluating remote renderer version ${version} (bundled version: ${RemoteRenderer.BUNDLED_VERSION})`
      );

      // It's possible that the bundled version is greater than what's already
      // in the cache, in which case, we don't want to do any of these things,
      // and fall through to using the bundled version.
      if (Services.vc.compare(RemoteRenderer.BUNDLED_VERSION, version) < 0) {
        lazy.logConsole.debug(
          `Remote renderer version ${version} is higher. Attempting to use it.`
        );
        const manifestCacheURI = this.makeManifestEntryURI(version);
        let manifestExists = false;

        const scriptCacheURI = this.makeScriptEntryURI(version);
        let scriptExists = false;

        const styleCacheURI = this.makeStyleEntryURI(version);
        let styleExists = false;

        try {
          manifestExists = lazy.cacheStorage.exists(manifestCacheURI, "");
          scriptExists = lazy.cacheStorage.exists(scriptCacheURI, "");
          styleExists = lazy.cacheStorage.exists(styleCacheURI, "");
        } catch (e) {
          lazy.logConsole.warn(
            "Checking existence of cached resources failed, possibly because the " +
              "cache index was being written."
          );
        }

        if (manifestExists && scriptExists && styleExists) {
          lazy.logConsole.debug(`Evaluation passed. Using version ${version}`);
          const manifestStream =
            await this.getCachedEntryStream(manifestCacheURI);
          if (manifestStream) {
            const manifestString =
              await this.pumpInputStreamToString(manifestStream);
            const manifest = JSON.parse(manifestString);
            return {
              appProps: {
                manifest,
                renderUpdate: true,
                isCached: true,
                isStaleData: false,
                initialState: {
                  start: {
                    location: "Munchkin land",
                  },
                  path: {
                    color: "yellow",
                    material: "brick",
                    destination: "Oz",
                  },
                  toDo: [
                    {
                      isComplete: false,
                      task: "see the wizard",
                    },
                    {
                      isComplete: false,
                      task: "find a way home",
                    },
                  ],
                },
              },
            };
          }
          // Otherwise, getting the manifest stream failed.
          lazy.logConsole.warn(
            "Manifest stream could not be fetched. Falling back to bundled renderer."
          );
        }
      }
    }

    // If we got here, we're using the bundled version. If we happen to have a
    // cached renderer, blow it away.
    this.resetCache();
    lazy.logConsole.debug(
      `Using bundled version ${RemoteRenderer.BUNDLED_VERSION}`
    );
    return {
      appProps: {
        manifest: {
          version: "0.0.1",
          buildTime: "2026-02-02T20:41:31.966Z",
          file: "index.tQLriCnN.js",
          hash: "tQLriCnN",
          dataSchemaVersion: "1.2.1",
          cssFile: "",
        },
        renderUpdate: true,
        isCached: false,
        isStaleData: false,
        initialState: {
          start: {
            location: "Munchkinland",
          },
          path: {
            color: "yellow",
            material: "brick",
            destination: "Oz",
          },
          toDo: [
            {
              isComplete: false,
              task: "see the wizard",
            },
            {
              isComplete: false,
              task: "find a way home",
            },
          ],
        },
      },
    };
  }

  /**
   * Constructs an nsIURI for a specific resource type and version.
   *
   * @param {string} type
   *   Resource type ("manifest", "script", or "style")
   * @param {string} version
   *   Version identifier
   * @returns {nsIURI}
   *   The constructed nsIURI
   */
  #makeEntryURI(type, version) {
    return Services.io.newURI(
      `moz-newtab-remote-renderer://${type}/?version=${version}`
    );
  }

  /**
   * Creates an nsIURI for the manifest cache entry.
   *
   * @param {string} version - Version identifier
   * @returns {nsIURI}
   *   The constructed nsIURI for the manifest.
   */
  makeManifestEntryURI(version) {
    return this.#makeEntryURI("manifest", version);
  }

  /**
   * Creates an nsIURI for the script cache entry.
   *
   * @param {string} version
   *   Version identifier
   * @returns {nsIURI}
   *   The constructed nsIURI for the renderer script.
   */
  makeScriptEntryURI(version) {
    return this.#makeEntryURI("script", version);
  }

  /**
   * Creates an nsIURI for the style cache entry.
   *
   * @param {string} version
   *   Version identifier
   * @returns {nsIURI}
   *   The constructed nsIURI for the renderer styles
   */
  makeStyleEntryURI(version) {
    return this.#makeEntryURI("style", version);
  }

  /**
   * Clears cache preferences and schedules current cached renderer entries
   * to be doomed on shutdown.
   */
  resetCache() {
    const { remoteRendererVersion } = lazy;

    Services.prefs.clearUserPref(PREF_REMOTE_RENDERER_VERSION);

    if (remoteRendererVersion) {
      const manifestCacheURI = this.makeManifestEntryURI(remoteRendererVersion);
      const scriptCacheURI = this.makeScriptEntryURI(remoteRendererVersion);
      const styleCacheURI = this.makeStyleEntryURI(remoteRendererVersion);

      this.doomCacheEntryOnShutdown(manifestCacheURI);
      this.doomCacheEntryOnShutdown(scriptCacheURI);
      this.doomCacheEntryOnShutdown(styleCacheURI);
    }
  }

  /**
   * Writes content to a single cache entry from an ArrayBuffer.
   *
   * @param {nsIURI} uri - Cache key URI
   * @param {ArrayBufferLike} arrayBuffer - Content to write
   * @param {string} version - Version identifier
   * @returns {Promise<undefined>}
   */
  async writeCacheEntry(uri, arrayBuffer, version) {
    await new Promise((resolve, reject) => {
      lazy.cacheStorage.asyncOpenURI(
        uri,
        "",
        Ci.nsICacheStorage.OPEN_TRUNCATE,
        {
          onCacheEntryCheck() {
            return Ci.nsICacheEntryOpenCallback.ENTRY_WANTED;
          },
          async onCacheEntryAvailable(entry, isNew, status) {
            if (!Components.isSuccessCode(status)) {
              reject(new Error("Failed to open cache entry for writing"));
              return;
            }

            try {
              let inputStream = Cc[
                "@mozilla.org/io/arraybuffer-input-stream;1"
              ].createInstance(Ci.nsIArrayBufferInputStream);
              inputStream.setData(arrayBuffer, 0, arrayBuffer.byteLength);

              let outputStream = entry.openOutputStream(0, -1);

              await new Promise((resolveWrite, rejectWrite) => {
                lazy.NetUtil.asyncCopy(inputStream, outputStream, result => {
                  if (Components.isSuccessCode(result)) {
                    resolveWrite();
                  } else {
                    rejectWrite(
                      new Error(`Failed to write to cache: ${result}`)
                    );
                  }
                });
              });

              outputStream.close();

              entry.setMetaDataElement("version", version);
              entry.setMetaDataElement("timestamp", Date.now().toString());

              resolve();
            } catch (e) {
              reject(e);
            }
          },
        }
      );
    });
  }

  /**
   * Writes JS and CSS content to the HTTP cache atomically.
   * Version metadata acts as atomic commit flag - both entries written with same version.
   *
   * @param {object} content
   * @param {ArrayBufferLike} content.manifest - Renderer manifest
   * @param {ArrayBuffer} content.js - JavaScript bundle
   * @param {ArrayBuffer} content.css - CSS stylesheet
   * @param {string} content.version - Version identifier
   * @returns {Promise<undefined>}
   */
  async updateFromRemoteSettings({ manifest, js, css, version }) {
    await Promise.all([
      this.writeCacheEntry(
        this.makeManifestEntryURI(version),
        manifest,
        version
      ),
      this.writeCacheEntry(this.makeScriptEntryURI(version), js, version),
      this.writeCacheEntry(this.makeStyleEntryURI(version), css, version),
    ]);

    Services.prefs.setCharPref(PREF_REMOTE_RENDERER_VERSION, version);
  }

  /**
   * Retrieves the script resource for a given renderer configuration.
   * Attempts to load from cache based on the renderer's manifest version.
   * Falls back to bundled script if cache entry doesn't exist.
   *
   * @param {object} renderer - Renderer configuration with appProps.manifest.version
   * @returns {Promise<object>} Object with inputStream, contentType, and success flag
   */
  async getScriptResource(renderer) {
    const { version } = renderer.appProps.manifest;
    const scriptCacheURI = this.makeScriptEntryURI(version);
    let entryExists = false;
    try {
      entryExists = lazy.cacheStorage.exists(scriptCacheURI, "");
    } catch (e) {
      lazy.logConsole.warn(
        "Checking that the script resource exists failed, " +
          "probably because the cache index was being written."
      );
    }

    if (!entryExists) {
      lazy.logConsole.debug(
        "Falling back to bundled script stream because entry does not exist."
      );
      return this.#fallbackToBundledScriptStream();
    }

    const scriptStream = await this.getCachedEntryStream(scriptCacheURI);

    if (!scriptStream) {
      lazy.logConsole.debug(
        "Falling back to bundled script stream because cached entry could " +
          "not be retrieved."
      );
      return this.#fallbackToBundledScriptStream();
    }

    return {
      inputStream: scriptStream.QueryInterface(Ci.nsIInputStream),
      contentType: "application/javascript",
      success: true,
    };
  }

  /**
   * Clears the cache and loads the bundled script stream. This gets called if
   * something goes wrong attemptingn to get a cached script stream.
   *
   * @returns {Promise<object>} Object with inputStream, contentType, and success flag
   */
  async #fallbackToBundledScriptStream() {
    // Make sure the renderer cache is clear
    this.resetCache();

    try {
      // Then serve up the bundled renderer instead.
      const inputStream = Cc[
        "@mozilla.org/io/arraybuffer-input-stream;1"
      ].createInstance(Ci.nsIArrayBufferInputStream);

      const response = await fetch(BUNDLED_SCRIPT_URI);
      const buffer = await response.arrayBuffer();
      inputStream.setData(buffer, 0, buffer.byteLength);

      return {
        inputStream,
        contentType: "application/javascript",
        success: true,
      };
    } catch (e) {
      // We did our best, but for some reason couldn't get the fallback bundled
      // script. Tell the content process to cancel the channel load.
      lazy.logConsole.error("Failed to fallback to bundled script stream", e);
      return {
        success: false,
      };
    }
  }

  /**
   * Retrieves the style resource for a given renderer configuration.
   * Attempts to load from cache based on the renderer's manifest version.
   * Falls back to bundled stylesheet if cache entry doesn't exist.
   *
   * @param {object} renderer - Renderer configuration with appProps.manifest.version
   * @returns {Promise<object>} Object with inputStream, contentType, and success flag
   */
  async getStyleResource(renderer) {
    const { version } = renderer.appProps.manifest;
    const styleCacheURI = this.makeStyleEntryURI(version);
    let entryExists = false;
    try {
      entryExists = lazy.cacheStorage.exists(styleCacheURI, "");
    } catch (e) {
      lazy.logConsole.warn(
        "Checking that the style resource exists failed, " +
          "probably because the cache index was being written."
      );
    }

    if (!entryExists) {
      lazy.logConsole.debug(
        "Falling back to bundled style stream because entry does not exist."
      );
      return this.#fallbackToBundledStyleStream();
    }

    let styleStream = await this.getCachedEntryStream(styleCacheURI);

    if (!styleStream) {
      lazy.logConsole.debug(
        "Falling back to bundled style stream because cached entry could " +
          "not be retrieved."
      );
      return this.#fallbackToBundledStyleStream();
    }

    return {
      inputStream: styleStream.QueryInterface(Ci.nsIInputStream),
      contentType: "text/css",
      success: true,
    };
  }

  /**
   * Clears the cache and loads the bundled style stream. This gets called if
   * something goes wrong attemptingn to get a cached style stream.
   *
   * @returns {Promise<object>} Object with inputStream, contentType, and success flag
   */
  async #fallbackToBundledStyleStream() {
    // Make sure the renderer cache is clear
    this.resetCache();

    try {
      // Then serve up the bundled renderer instead.
      const inputStream = Cc[
        "@mozilla.org/io/arraybuffer-input-stream;1"
      ].createInstance(Ci.nsIArrayBufferInputStream);
      const response = await fetch(BUNDLED_STYLE_URI);
      const buffer = await response.arrayBuffer();
      inputStream.setData(buffer, 0, buffer.byteLength);

      return {
        inputStream,
        contentType: "text/css",
        success: true,
      };
    } catch (e) {
      lazy.logConsole.error("Failed to fallback to bundled style stream", e);
      // We did our best, but for some reason couldn't get the fallback bundled
      // styles. Tell the content process to cancel the channel load.
      return {
        success: false,
      };
    }
  }

  /**
   * Checks Remote Settings for updates and fetches new content if available.
   * Called in the background when serving stale content.
   *
   * @returns {Promise<undefined>}
   */
  async maybeRevalidate() {
    const currentVersion =
      lazy.remoteRendererVersion || RemoteRenderer.BUNDLED_VERSION;
    let latestVersion = await this.getExpectedVersionFromRemoteSettings();

    if (Services.vc.compare(currentVersion, latestVersion) < 0) {
      let newContent = await this.fetchLatestContent();

      if (
        newContent &&
        newContent.manifest &&
        newContent.js &&
        newContent.css
      ) {
        await this.updateFromRemoteSettings(newContent);
      }
    }
  }

  /**
   * Gets the expected version from Remote Settings by checking the version
   * field in the records.
   *
   * @returns {Promise<string|null>}
   */
  async getExpectedVersionFromRemoteSettings() {
    try {
      let records = await this.#rsClient.get();

      if (!records || records.length === 0) {
        return null;
      }

      // In the current model, we'll only have a single renderer published to
      // RemoteSettings at any given time, and it is expected that both the
      // script and style resources will have the same version string, so we
      // just return the first record's version.

      let version = records[0]?.version;
      return version || null;
    } catch (e) {
      console.error("Failed to get version from Remote Settings:", e);
      return null;
    }
  }

  /**
   * Fetches the latest JS and CSS bundle from Remote Settings.
   * Downloads attachments and returns them as ArrayBuffers.
   *
   * @returns {Promise<{manifest: ArrayBufferLike, js: ArrayBuffer, css: ArrayBuffer, version: string}|null>}
   */
  async fetchLatestContent() {
    try {
      const records = await this.#rsClient.get();

      if (!records || records.length === 0) {
        return null;
      }

      const jsRecord = records.find(r => r.type === "js");
      const cssRecord = records.find(r => r.type === "css");

      if (!jsRecord || !cssRecord) {
        console.error("Missing JS or CSS record in Remote Settings");
        return null;
      }

      const { version, buildTime, dataSchemaVersion, hash } = jsRecord;

      // Constructing what the renderer expects for a manifest. This may
      // change over time, but for now, this is what it wants.
      const manifestString = JSON.stringify({
        version,
        buildTime,
        hash,
        dataSchemaVersion,
        file: jsRecord.attachment.filename,
        cssFile: cssRecord.attachment.filename,
      });
      const manifest = new TextEncoder().encode(manifestString);

      let [jsAttachment, cssAttachment] = await Promise.all([
        this.#rsClient.attachments.download(jsRecord),
        this.#rsClient.attachments.download(cssRecord),
      ]);

      return {
        js: jsAttachment.buffer,
        css: cssAttachment.buffer,
        version,
        manifest: manifest.buffer,
      };
    } catch (e) {
      console.error("Failed to fetch content from Remote Settings:", e);
      return null;
    }
  }

  /**
   * @param {nsIURI} resourceUri - Cache key URI
   * @returns {Promise<nsIInputStream|null>}
   */
  async getCachedEntryStream(resourceUri) {
    const cacheEntry = await this.openCacheEntry(resourceUri);
    if (!cacheEntry) {
      return null;
    }

    return cacheEntry.openInputStream(0);
  }

  /**
   * Opens a cache entry for reading.
   *
   * @param {nsIURI} resourceURI - Cache key URI
   * @returns {Promise<nsICacheEntry|null>}
   */
  async openCacheEntry(resourceURI) {
    return new Promise(resolve => {
      lazy.cacheStorage.asyncOpenURI(
        resourceURI,
        "",
        Ci.nsICacheStorage.OPEN_READONLY,
        {
          onCacheEntryCheck() {
            return Ci.nsICacheEntryOpenCallback.ENTRY_WANTED;
          },
          onCacheEntryAvailable(entry, isNew, status) {
            if (isNew || !Components.isSuccessCode(status)) {
              resolve(null);
            } else {
              resolve(entry);
            }
          },
        }
      );
    });
  }

  /**
   * Reads an input stream completely and returns its contents as a UTF-8 string.
   * Uses NetUtil.asyncFetch to consume the stream asynchronously.
   *
   * @param {nsIInputStream} inputStream - Stream to read
   * @returns {Promise<string>} Stream contents as UTF-8 string
   */
  pumpInputStreamToString(inputStream) {
    return new Promise((resolve, reject) => {
      lazy.NetUtil.asyncFetch(inputStream, (stream, status) => {
        if (!Components.isSuccessCode(status)) {
          reject(new Error(`Failed to read cache entry: ${status}`));
          return;
        }

        try {
          let data = lazy.NetUtil.readInputStreamToString(
            stream,
            stream.available(),
            { charset: "UTF-8" }
          );
          resolve(data);
        } catch (e) {
          reject(e);
        }
      });
    });
  }
}
