/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * OpenSearchLoader is used for loading OpenSearch definitions from content.
 */

/* eslint no-shadow: error, mozilla/no-aArgs: error */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  HiddenBrowserManager: "resource://gre/modules/HiddenFrame.sys.mjs",
  OpenSearchParser:
    "moz-src:///toolkit/components/search/OpenSearchParser.sys.mjs",
  SearchEngineInstallError:
    "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  logConsole: () =>
    console.createInstance({
      logModule: "OpenSearchLoader",
      maxLogLevel: lazy.SearchUtils.loggingEnabled ? "Debug" : "Warn",
    }),
});

/**
 * @import {OpenSearchProperties} from "./OpenSearchParser.sys.mjs"
 */

/**
 * Retrieves the engine data from a URI and returns it.
 *
 * @param {nsIURI} sourceURI
 *   The uri from which to load the OpenSearch engine data.
 * @param {string} [lastModified]
 *   The UTC date when the engine was last updated, if any.
 * @param {OriginAttributesDictionary} [originAttributes]
 *   The origin attributes of the site loading the manifest. If none are
 *   specified, the origin attributes will be formed of the first party domain
 *   based on the domain of the manifest.
 * @returns {Promise<OpenSearchProperties>}
 *   The properties of the loaded OpenSearch engine.
 */
export async function loadAndParseOpenSearchEngine(
  sourceURI,
  lastModified,
  originAttributes
) {
  if (!sourceURI) {
    throw new TypeError("No URI");
  }
  if (!/^https?$/i.test(sourceURI.scheme)) {
    throw new TypeError(
      "Unsupported URI scheme passed to SearchEngine constructor"
    );
  }

  lazy.logConsole.debug("Downloading OpenSearch engine from:", sourceURI.spec);

  let xmlData = await loadEngineXML(sourceURI, lastModified, originAttributes);

  lazy.logConsole.debug("Loading search plugin");

  let engineData = await parseXMLData(xmlData);

  engineData.installURL = sourceURI;
  return engineData;
}

/**
 * Parses OpenSearch XML data, using a hidden browser content process when
 * available, or directly in the current process as a fallback (e.g., xpcshell).
 *
 * @param {number[]} xmlData
 *   The loaded search engine XML data as an array of bytes.
 * @returns {Promise<OpenSearchProperties>}
 *   The extracted engine properties.
 */
async function parseXMLData(xmlData) {
  // In the xpcshell parse directly in the parent process.
  if (Cu.isInAutomation && Services.env.exists("XPCSHELL_TEST_PROFILE_DIR")) {
    let result = lazy.OpenSearchParser.parseXMLData(xmlData);
    if ("error" in result) {
      lazy.logConsole.error(
        "parseXMLData: Failed to init engine!",
        result.error
      );
      throw new lazy.SearchEngineInstallError("corrupted", result.error);
    }
    return result.data;
  }
  return parseInHiddenBrowser(xmlData);
}

/**
 * Parses OpenSearch XML data by loading it in a hidden browser and extracting
 * engine data via a child actor in the content process.
 *
 * @param {number[]} xmlData
 *   The loaded search engine XML data as an array of bytes.
 * @returns {Promise<OpenSearchProperties>}
 *   The extracted engine properties.
 */
async function parseInHiddenBrowser(xmlData) {
  return lazy.HiddenBrowserManager.withHiddenBrowser(
    async browser => {
      let { promise, resolve } = Promise.withResolvers();

      let progressListener = {
        QueryInterface: ChromeUtils.generateQI([
          "nsIWebProgressListener",
          "nsISupportsWeakReference",
        ]),
        onStateChange(webProgress, _request, flags) {
          if (
            !(flags & Ci.nsIWebProgressListener.STATE_STOP) ||
            !(flags & Ci.nsIWebProgressListener.STATE_IS_NETWORK)
          ) {
            return;
          }
          browser.removeProgressListener(progressListener);
          resolve();
        },
      };

      browser.addProgressListener(
        progressListener,
        Ci.nsIWebProgress.NOTIFY_STATE_NETWORK
      );

      browser.loadURI(Services.io.newURI("about:blank"), {
        triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
          {}
        ),
      });

      await promise;

      let actor =
        browser.browsingContext.currentWindowGlobal.getActor(
          "OpenSearchLoader"
        );
      let result = await actor.sendQuery(
        "OpenSearchLoader:getEngineData",
        xmlData
      );

      if ("error" in result) {
        lazy.logConsole.error(
          "parseInHiddenBrowser: Failed to init engine!",
          result.error
        );
        throw new lazy.SearchEngineInstallError("corrupted", result.error);
      }

      return result.data;
    },
    { messageManagerGroup: "opensearch" }
  );
}

/**
 * Loads the engine XML from the given URI.
 *
 * @param {nsIURI} sourceURI
 *   The uri from which to load the OpenSearch engine data.
 * @param {string} [lastModified]
 *   The UTC date when the engine was last updated, if any.
 * @param {object} [originAttributes]
 *   The origin attributes to use to load the manifest.
 * @returns {Promise}
 *   A promise that is resolved with the data if the engine is successfully loaded
 *   and rejected otherwise.
 */
function loadEngineXML(sourceURI, lastModified, originAttributes = null) {
  var chan = lazy.SearchUtils.makeChannel(
    sourceURI,
    // OpenSearchEngine is loading a definition file for a search engine,
    // TYPE_DOCUMENT captures that load best.
    Ci.nsIContentPolicy.TYPE_DOCUMENT,
    originAttributes
  );

  // we collect https telemetry for all top-level (document) loads.
  chan.loadInfo.httpsUpgradeTelemetry = sourceURI.schemeIs("https")
    ? Ci.nsILoadInfo.ALREADY_HTTPS
    : Ci.nsILoadInfo.NO_UPGRADE;

  if (lastModified && chan instanceof Ci.nsIHttpChannel) {
    chan.setRequestHeader("If-Modified-Since", lastModified, false);
  }
  let loadPromise = Promise.withResolvers();

  let loadHandler = data => {
    if (!data) {
      loadPromise.reject(new lazy.SearchEngineInstallError("download-failure"));
      return;
    }
    loadPromise.resolve(data);
  };

  var listener = new lazy.SearchUtils.LoadListener(
    chan,
    /(^text\/|xml$)/,
    loadHandler
  );
  chan.notificationCallbacks = listener;
  chan.asyncOpen(listener);

  return loadPromise.promise;
}
