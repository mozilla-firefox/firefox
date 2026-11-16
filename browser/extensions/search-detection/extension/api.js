/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* global ExtensionCommon, ExtensionAPI, Glean, Services, XPCOMUtils, ExtensionUtils */

const { AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);
const { WebRequest } = ChromeUtils.importESModule(
  "resource://gre/modules/WebRequest.sys.mjs"
);
var { ExtensionParent } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionParent.sys.mjs"
);
const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonSearchEngine:
    "moz-src:///toolkit/components/search/AddonSearchEngine.sys.mjs",
  ConfigSearchEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
});

// eslint-disable-next-line mozilla/reject-importGlobalProperties
XPCOMUtils.defineLazyGlobalGetters(this, ["ChannelWrapper", "URLSearchParams"]);

const SEARCH_TOPIC_ENGINE_MODIFIED = "browser-search-engine-modified";

this.addonsSearchDetection = class extends ExtensionAPI {
  getAPI(context) {
    const { extension } = context;

    // We want to temporarily store the first monitored URLs that have been
    // redirected, indexed by request IDs, so that the background script can
    // find the add-on IDs to report.
    this.firstMatchedUrls = {};

    return {
      addonsSearchDetection: {
        // `getEngines()` returns an array of engines, each with a baseUrl.
        // AppProvided engines also include a partner paramName,
        // while Addon provided engines include the addonId.
        //
        // Note: We don't return a simple list of URL patterns because the
        // background script might want to lookup add-on IDs for a given URL in
        // the case of server-side redirects.
        async getEngines() {
          const results = [];

          try {
            // Delaying accessing SearchService if we didn't get to first paint yet
            // to avoid triggering search internals from loading too soon during the
            // application startup.
            if (
              !Cu.isESModuleLoaded(
                "moz-src:///toolkit/components/search/SearchService.sys.mjs"
              )
            ) {
              await ExtensionParent.browserPaintedPromise;
            }
            // Return earlier if the extension or the application is shutting down.
            if (extension.hasShutdown || Services.startup.shuttingDown) {
              return results;
            }
            await lazy.SearchService.promiseInitialized;
            const engines = await lazy.SearchService.getEngines();

            for (let engine of engines) {
              if (
                !(engine instanceof lazy.AddonSearchEngine) &&
                !(engine instanceof lazy.ConfigSearchEngine)
              ) {
                continue;
              }

              // The search term isn't used, but avoids a warning of an empty
              // term.
              let submission = engine.getSubmission("searchTerm");
              if (submission) {
                const uri = submission.uri;
                const baseUrl = uri.prePath + uri.filePath;

                // We don't store ids for application provided search engines
                // because we don't need to report them. However, we do ensure
                // the pattern is recorded (above), so that we check for
                // redirects against those.
                const addonId = engine.extensionID;

                let paramName;
                for (let [key, value] of new URLSearchParams(uri.query)) {
                  if (value && value === engine.partnerCode) {
                    paramName = key;
                  }
                }

                results.push({ baseUrl, addonId, paramName });
              }
            }
          } catch (err) {
            console.error(err);
          }

          return results;
        },

        // `getAddonVersion()` returns the add-on version if it exists.
        async getAddonVersion(addonId) {
          const addon = await AddonManager.getAddonByID(addonId);

          return addon && addon.version;
        },

        // `getPublicSuffix()` returns the public suffix/Effective TLD Service
        // of the given URL. See: `nsIEffectiveTLDService` interface in tree.
        async getPublicSuffix(url) {
          try {
            return Services.eTLD.getBaseDomain(Services.io.newURI(url));
          } catch (err) {
            console.error(err);
            return null;
          }
        },

        reportSameSiteRedirect(extra) {
          Glean.addonsSearchDetection.sameSiteRedirect.record(extra);
        },

        reportETLDChangeOther(extra) {
          Glean.addonsSearchDetection.etldChangeOther.record(extra);
        },

        reportETLDChangeWebrequest(extra) {
          Glean.addonsSearchDetection.etldChangeWebrequest.record(extra);
        },

        // `onSearchEngineModified` is an event that occurs when the list of
        // search engines has changed, e.g., a new engine has been added or an
        // engine has been removed.
        //
        // See: https://searchfox.org/mozilla-central/rev/cb44fc4f7bb84f2a18fedba64c8563770df13e34/toolkit/components/search/SearchUtils.sys.mjs#185-193
        onSearchEngineModified: new ExtensionCommon.EventManager({
          context,
          name: "addonsSearchDetection.onSearchEngineModified",
          register: fire => {
            const onSearchEngineModifiedObserver = (
              aSubject,
              aTopic,
              aData
            ) => {
              if (
                aTopic !== SEARCH_TOPIC_ENGINE_MODIFIED ||
                // We are only interested in these modified types.
                !["engine-added", "engine-removed", "engine-changed"].includes(
                  aData
                )
              ) {
                return;
              }

              fire.async().catch(() => {}); // ignore Message Manager disconnects
            };

            Services.obs.addObserver(
              onSearchEngineModifiedObserver,
              SEARCH_TOPIC_ENGINE_MODIFIED
            );

            return () => {
              Services.obs.removeObserver(
                onSearchEngineModifiedObserver,
                SEARCH_TOPIC_ENGINE_MODIFIED
              );
            };
          },
        }).api(),

        // `onRedirected` is an event fired after a request has stopped and
        // this request has been redirected once or more. The registered
        // listeners will received the following properties:
        //
        //   - `addonId`: the add-on ID that redirected the request, if any.
        //   - `firstUrl`: the first monitored URL of the request that has
        //      been redirected.
        //   - `lastUrl`: the last URL loaded for the request, after one or
        //      more redirects.
        onRedirected: new ExtensionCommon.EventManager({
          context,
          name: "addonsSearchDetection.onRedirected",
          register: (fire, filter) => {
            const stopListener = event => {
              if (event.type != "stop") {
                return;
              }

              const wrapper = event.currentTarget;
              const { channel, id: requestId } = wrapper;

              // When we detected a redirect, we read the request property,
              // hoping to find an add-on ID corresponding to the add-on that
              // initiated the redirect. It might not return anything when the
              // redirect is a search server-side redirect but it can also be
              // caused by an error.
              let addonId;
              try {
                addonId = channel
                  ?.QueryInterface(Ci.nsIPropertyBag)
                  ?.getProperty("redirectedByExtension");
              } catch (err) {
                console.error(err);
              }

              const firstUrl = this.firstMatchedUrls[requestId];
              // We don't need this entry anymore.
              delete this.firstMatchedUrls[requestId];

              const lastUrl = wrapper.finalURL;

              if (!firstUrl || !lastUrl) {
                // Something went wrong but there is nothing we can do at this
                // point.
                return;
              }

              fire.sync({ addonId, firstUrl, lastUrl });
            };

            const remoteTab = context.xulBrowser.frameLoader.remoteTab;

            const listener = ({ requestId, url, originUrl }) => {
              // We exclude requests not originating from the location bar,
              // bookmarks and other "system-ish" requests.
              if (originUrl !== undefined) {
                return;
              }

              // Keep the first monitored URL that was redirected for the
              // request until the request has stopped.
              if (!this.firstMatchedUrls[requestId]) {
                this.firstMatchedUrls[requestId] = url;

                const wrapper = ChannelWrapper.getRegisteredChannel(
                  requestId,
                  context.extension.policy,
                  remoteTab
                );

                wrapper.addEventListener("stop", stopListener);
              }
            };

            const ensureRegisterChannel = () => {
              // onRedirected depends on ChannelWrapper.getRegisteredChannel,
              // which in turn depends on registerTraceableChannel to have been
              // called, which happens when "blocking" is used with addListener.
              //
              // A downside to a blocking webRequest listener is that it delays
              // the network request until a roundtrip to the listener in the
              // extension process has happened. Since we don't need to handle
              // the onBeforeRequest event, we avoid overhead by registering a
              // no-op listener here in the parent process.
            };

            const parsedFilter = {
              types: ["main_frame"],
              urls: ExtensionUtils.parseMatchPatterns(filter.urls),
            };

            WebRequest.onBeforeRequest.addListener(
              ensureRegisterChannel,
              parsedFilter,
              ["blocking"],
              {
                addonId: extension.id,
                policy: extension.policy,
                blockingAllowed: true,
              }
            );

            WebRequest.onBeforeRedirect.addListener(
              listener,
              parsedFilter,
              // info
              [],
              // listener details
              {
                addonId: extension.id,
                policy: extension.policy,
                blockingAllowed: false,
              }
            );

            return () => {
              WebRequest.onBeforeRequest.removeListener(ensureRegisterChannel);
              WebRequest.onBeforeRedirect.removeListener(listener);
            };
          },
        }).api(),
      },
    };
  }
};
