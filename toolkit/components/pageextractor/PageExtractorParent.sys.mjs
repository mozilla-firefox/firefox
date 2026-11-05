/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// @ts-check

/**
 * @import { HiddenFrame } from "resource://gre/modules/HiddenFrame.sys.mjs"
 * @import { GetTextOptions, ExtractionResult, PageMetadata } from './PageExtractor.d.ts'
 * @import { PageExtractorChild } from './PageExtractorChild.sys.mjs'
 * @import { PageExtractorPhase, TraceId } from './PageExtractorEvents.sys.mjs'
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  HiddenBrowserManager: "resource://gre/modules/HiddenFrame.sys.mjs",
  console: () =>
    console.createInstance({
      logModule: "PageExtractorChild",
      maxLogLevelPref: "browser.ml.logLevel",
    }),
  collapseWhitespace:
    "moz-src:///toolkit/components/pageextractor/DOMExtractor.sys.mjs",
  PageExtractorEvent:
    "moz-src:///toolkit/components/pageextractor/PageExtractorEvents.sys.mjs",
  Phase: () => lazy.PageExtractorEvent.Phase,
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  headlessTimeoutMs: {
    pref: "browser.ml.pageExtractor.headlessTimeoutMs",
    default: 15000,
  },
});

// NOTE: Copied from nsSandboxFlags.h.
// Blocks window.open / target="_blank" popups.
const SANDBOXED_AUXILIARY_NAVIGATION = 0x2;
// Blocks the page from navigating its own top to an attacker-controlled URL.
const SANDBOXED_TOPLEVEL_NAVIGATION = 0x4;
// Blocks form submissions.
const SANDBOXED_FORMS = 0x20;
// Blocks the Pointer Lock API.
const SANDBOXED_POINTER_LOCK = 0x40;
// Blocks automatically triggered features such as autoplay, autofocus, and
// auto-form-submission.
const SANDBOXED_AUTOMATIC_FEATURES = 0x100;
// Blocks modal dialogs (alert, confirm, prompt, print, etc).
const SANDBOXED_MODALS = 0x800;
// Blocks the Screen Orientation API from locking orientation.
const SANDBOXED_ORIENTATION_LOCK = 0x2000;
// Blocks the Presentation API.
const SANDBOXED_PRESENTATION = 0x4000;
// Blocks the Storage Access API.
const SANDBOXED_STORAGE_ACCESS = 0x8000;
// Blocks downloads initiated by the page.
const SANDBOXED_DOWNLOADS = 0x10000;

/** @type {Set<() => void>} */
const headlessDeadlines = new Set();

/** @param {string} location */
function notifyHeadlessLocation(location) {
  if (Cu.isInAutomation) {
    Services.obs.notifyObservers(
      null,
      "page-extractor-headless-location",
      location
    );
  }
}

/**
 * Whether a redirect landed on the requested site. A shared name is not
 * enough: the suffix list hands out github.io, so nytimes.github.io would
 * match nytimes.com. On a host with no suffix, each port is its own service.
 *
 * @param {nsIURI} requested
 * @param {nsIURI} location
 * @returns {boolean}
 */
function isSameSite(requested, location) {
  const { eTLD } = Services;
  if (
    !eTLD.hasKnownPublicSuffix(requested) ||
    !eTLD.hasKnownPublicSuffix(location)
  ) {
    return requested.asciiHostPort === location.asciiHostPort;
  }
  return eTLD.getSchemelessSite(requested) === eTLD.getSchemelessSite(location);
}

/**
 * Whether the document a read was bound to has been replaced. A destroyed
 * actor cannot answer for its window global, so a throw means replaced.
 *
 * @param {PageExtractorParent} actor
 * @returns {boolean}
 */
function wasReplaced(actor) {
  try {
    return !actor.manager.isCurrentGlobal;
  } catch {
    return true;
  }
}

/**
 * Extract a variety of content from pages for use in a smart window.
 */
export class PageExtractorParent extends JSWindowActorParent {
  /**
   * Waits for DOMContentLoaded.
   *
   * @see PageExtractorChild#waitForPageReady
   *
   * @param {TraceId} [traceId] - Correlates this call with an enclosing
   *   headless-extractor request's profiler markers. Internal only, not a
   *   GetTextOptions field: starts its own trace when omitted.
   * @param {number} [refreshWithinMs] - A refresh only counts as a pending
   *   navigation when it fires within this many milliseconds.
   * @param {number} [settleDelayMs] - Extra wait after the page is ready.
   * @returns {Promise<{hasPendingNavigation: boolean}>}
   */
  async waitForPageReady(traceId, refreshWithinMs, settleDelayMs) {
    return this.#trace(lazy.Phase.waitForReady, { traceId }, async event => {
      try {
        return await this.sendQuery("PageExtractorParent:WaitForPageReady", {
          traceId: event.traceId,
          refreshWithinMs,
          settleDelayMs,
        });
      } catch (error) {
        // The document was replaced or its browser torn down before it was
        // ready. A replacement is read in its place, and a teardown is
        // already reported by the phase that caused it.
        if (error?.name === "AbortError") {
          event.finish({ status: "document-unloaded" });
        }
        throw error;
      }
    });
  }

  /**
   * Get metadata related to the page.
   *
   * @see PageExtractorChild#getPageMetadata
   *
   * @returns {Promise<PageMetadata>}
   */
  async getPageMetadata() {
    return this.#trace(lazy.Phase.getPageMetadata, {}, event =>
      this.sendQuery("PageExtractorParent:GetPageMetadata", {
        traceId: event.traceId,
      })
    );
  }

  /**
   * Gets the visible text from the page. This function is a bit smarter than just
   * document.body.innerText. See GetTextOptions
   *
   * @see PageExtractorChild#getText
   *
   * @param {Partial<GetTextOptions>} options
   * @param {TraceId} [traceId] - Correlates this call with an enclosing
   *   headless-extractor request's profiler markers. Internal only, not a
   *   GetTextOptions field: starts its own trace when omitted.
   * @returns {Promise<ExtractionResult | null>}
   */
  async getText(options = {}, traceId) {
    if (options._forceRemoveBoilerplate && !Cu.isInAutomation) {
      throw new Error(
        "The _forceRemoveBoilerplate option from GetTextOptions can only be used in tests."
      );
    }

    return this.#trace(
      lazy.Phase.getText,
      { options, traceId },
      async event => {
        if (this.#isPDF()) {
          const result = await this.#getTextFromPDF(options, event.traceId);
          event.finish({
            status: result.text ? "success" : "empty",
            strategy: "pdf",
            textLength: result.text.length,
            linkCount: result.links.length,
            canvasCount: result.canvasSnapshots.length,
          });
          return result;
        }

        const result = await this.sendQuery("PageExtractorParent:GetText", {
          options,
          traceId: event.traceId,
        });
        if (!result) {
          event.finish({ status: "unavailable" });
          return null;
        }
        event.finish({
          status: result.text ? "success" : "empty",
          textLength: result.text.length,
          linkCount: result.links.length,
          canvasCount: result.canvasSnapshots.length,
        });
        return result;
      }
    );
  }

  /**
   * Call out to pdf.js to get the text content and apply the GetTextOptions.
   *
   * @param {GetTextOptions} options
   * @param {TraceId} traceId
   */
  async #getTextFromPDF(options, traceId) {
    return this.#trace(
      lazy.Phase.pdfExtract,
      { traceId, strategy: "pdf" },
      async event => {
        let text = await this.browsingContext.currentWindowGlobal
          .getActor("PdfJs")
          .getTextContent();

        if (
          options.sufficientLength &&
          text.length > options.sufficientLength
        ) {
          // Try to cut at a sentence boundary within the last 100 characters of the
          // end.
          //
          // TODO(Bug 2023932) Make this internationalized, splitting on a "." only works
          // in certain scripts like Latin.
          const truncatePoint = text.lastIndexOf(".", options.sufficientLength);
          if (truncatePoint > options.sufficientLength - 100) {
            text = text.substring(0, truncatePoint + 1);
          } else {
            text = text.substring(0, options.sufficientLength) + "…";
          }
        }

        text = lazy.collapseWhitespace(text).trim();

        event.finish({
          status: text ? "success" : "empty",
          textLength: text.length,
        });
        return { text, links: [], canvasSnapshots: [] };
      }
    );
  }

  #isPDF() {
    return (
      this.browsingContext.currentWindowGlobal.documentPrincipal
        .originNoSuffix == "resource://pdf.js"
    );
  }

  /**
   * @see PageExtractorEvent.trace for docs
   *
   * @template T
   * @param {PageExtractorPhase} phase
   * @param {Record<string, any>} data
   * @param {(event: import('./PageExtractorEvents.sys.mjs').PageExtractorEvent) => Promise<T> | T} task
   * @returns {Promise<T>}
   */
  #trace(phase, data, task) {
    return lazy.PageExtractorEvent.trace(
      phase,
      {
        process: "parent",
        innerWindowId:
          this.browsingContext?.currentWindowGlobal?.innerWindowId ?? 0,
        ...data,
      },
      task
    );
  }

  static expireHeadlessLoadsForTesting() {
    if (!Cu.isInAutomation) {
      throw new Error("Only available in automation.");
    }
    for (const onDeadline of headlessDeadlines) {
      onDeadline();
    }
  }

  /**
   * Get a Headless PageExtractor. It is available until the callback's returned
   * Promise is resolved. Then the headless browser is cleaned up.
   *
   * A redirect is only followed within the requested site, and an anonymous
   * fetch is never followed down to http. However the page got there, a
   * location off the site is refused with a "BlockedError" and never read.
   *
   * @see PageExtractorChild#getText
   *
   * @template T - The value resolved in the callback.
   *
   * @param {object} options
   * @param {string} options.urlString
   * @param {(actor: PageExtractorParent, traceId: TraceId) => Promise<T>} options.callback
   *   Called with the actor once the headless page is ready, and the trace
   *   to pass into a follow-up `actor.getText(options, traceId)` call so it
   *   correlates with this request.
   * @param {boolean} [options.anonymousFetch]
   * @param {number} [options.settleDelayMs] - Extra wait once the page is ready.
   * @param {AbortSignal} [options.signal] - Tears down the headless browser
   *   when it aborts before the page is ready.
   * @returns {Promise<T>}
   */
  static async getHeadlessExtractor({
    urlString,
    callback,
    anonymousFetch,
    settleDelayMs,
    signal,
  }) {
    const url = URL.parse(urlString);
    if (!url) {
      throw new Error("A valid URL must be provided.");
    }
    if (!["http:", "https:"].includes(url.protocol)) {
      throw new Error("Only http: and https: URLs are supported.");
    }
    if (anonymousFetch && url.protocol === "http:") {
      // Only loopback (e.g. localhost) and local network URLs are allowed to use
      // http since they do not perform external network requests.
      const principal = Services.scriptSecurityManager.createContentPrincipal(
        url.URI,
        {}
      );
      if (!principal.isLoopbackHost && !principal.isLocalIpAddress) {
        throw new Error(
          "Only https: URLs are supported for anonymous fetches."
        );
      }
    }
    // Anonymous fetches must not be downgraded to http by a redirect.
    const allowHttp = !anonymousFetch || url.protocol === "http:";

    return lazy.PageExtractorEvent.trace(
      lazy.Phase.headlessExtractor,
      {
        process: "parent",
        strategy: anonymousFetch ? "headless-anonymous" : "headless",
      },
      event => {
        const { traceId } = event;

        // Covers navigating the hidden browser up to onLocationChange,
        // before the page-ready wait starts. Whichever of three outcomes
        // (navigation commits, the actor lookup throws, or the load times
        // out) happens first finishes it; finish()'s idempotency handles
        // the rest.
        const navigateEvent = new lazy.PageExtractorEvent(
          lazy.Phase.headlessNavigate,
          { process: "parent", traceId }
        );

        // The hidden browser manager controls the lifetime of the hidden browser.
        return lazy.HiddenBrowserManager.withHiddenBrowser(
          async browser => {
            if (anonymousFetch) {
              // The goal of these settings is to fetch the page without sending
              // any user data to the origin and without letting the visit affect
              // the user's browsing profile (history, cache, trackers, etc).

              // Keep the visit out of browsing history
              // TODO (bug 2043254) - Move this into the HiddenBrowserManager so all hidden browsers don't affect global history.
              browser.setAttribute("disableglobalhistory", "true");
              // Suppress audio output from the loaded page.
              browser.browsingContext?.mediaController?.mute();
              browser.addEventListener("DidChangeBrowserRemoteness", () =>
                browser.browsingContext?.mediaController?.mute()
              );
              const { browsingContext } = browser;
              // Tracking Protection so third-party trackers on the page cannot profile the request or correlate it with the user.
              browsingContext.useTrackingProtection = true;
              browsingContext.defaultLoadFlags =
                // Strip cookies, HTTP auth, and other credentials from the request
                Ci.nsIRequest.LOAD_ANONYMOUS |
                // Don't write the response into the user's memory cache
                Ci.nsIRequest.INHIBIT_CACHING |
                // Don't write the response into the user's persistent (disk) cache
                Ci.nsIRequest.INHIBIT_PERSISTENT_CACHING;
              // Restrict what the loaded page can do.
              browsingContext.sandboxFlags |=
                SANDBOXED_AUXILIARY_NAVIGATION |
                SANDBOXED_TOPLEVEL_NAVIGATION |
                SANDBOXED_FORMS |
                SANDBOXED_POINTER_LOCK |
                SANDBOXED_AUTOMATIC_FEATURES |
                SANDBOXED_MODALS |
                SANDBOXED_ORIENTATION_LOCK |
                SANDBOXED_PRESENTATION |
                SANDBOXED_STORAGE_ACCESS |
                SANDBOXED_DOWNLOADS;
            }

            // Keep the settle wait from counting against the load timeout.
            const timeoutMs = lazy.headlessTimeoutMs + (settleDelayMs ?? 0);
            const deadline = ChromeUtils.now() + timeoutMs;
            // Recorded so a failed read can report a block instead of a stall.
            let offSiteLocation = null;
            /** @type {PromiseWithResolvers<PageExtractorParent>} */
            let actorResolver = Promise.withResolvers();

            /** @param {nsIURI} location */
            const isRequestedSite = location => {
              const isAllowedScheme =
                location.schemeIs("https") ||
                (allowHttp && location.schemeIs("http"));
              return isAllowedScheme && isSameSite(url.URI, location);
            };

            const progressFlags =
              Ci.nsIWebProgress.NOTIFY_LOCATION |
              Ci.nsIWebProgress.NOTIFY_STATE_DOCUMENT;
            const progressListener = {
              QueryInterface: ChromeUtils.generateQI([
                "nsIWebProgressListener",
                "nsISupportsWeakReference",
              ]),
              /**
               * Watched when the load starts, not when it commits: by commit the
               * document is gone and its actor has already rejected the read.
               *
               * @param {nsIWebProgress} webProgress
               * @param {nsIRequest} request
               * @param {number} stateFlags
               * @param {nsresult} _status
               */
              onStateChange(webProgress, request, stateFlags, _status) {
                const { STATE_START, STATE_IS_DOCUMENT } =
                  Ci.nsIWebProgressListener;
                if (
                  !webProgress.isTopLevel ||
                  !(stateFlags & STATE_START) ||
                  !(stateFlags & STATE_IS_DOCUMENT) ||
                  !(request instanceof Ci.nsIChannel)
                ) {
                  return;
                }
                if (!isRequestedSite(request.URI)) {
                  lazy.console.log(
                    "A load started that wasn't the same site.",
                    request.URI.spec,
                    url.href
                  );
                  offSiteLocation = request.URI.spec;
                  notifyHeadlessLocation(offSiteLocation);
                }
              },
              /**
               * @param {nsIWebProgress} webProgress
               * @param {nsIRequest} _request
               * @param {nsIURI} location
               * @param {number} _flags
               */
              onLocationChange(webProgress, _request, location, _flags) {
                if (!webProgress.isTopLevel) {
                  lazy.console.log(
                    "Headless browser had a non-top level location change."
                  );
                  return;
                }
                if (!isRequestedSite(location)) {
                  lazy.console.log(
                    "A location change happened that wasn't the same site.",
                    location.spec,
                    url.href
                  );
                  offSiteLocation = location.spec;
                  notifyHeadlessLocation(offSiteLocation);
                  // A chain may bounce through another site, the way a consent or
                  // single sign-on host hands the request back, so wait for the
                  // read to fail or time out rather than giving up here.
                  return;
                }
                offSiteLocation = null;

                /** @type {any} - This is reported as an `Element`, but it's a <browser> */
                const topBrowser = webProgress.browsingContext.topFrameElement;

                let actor;
                try {
                  actor =
                    topBrowser.browsingContext.currentWindowGlobal.getActor(
                      "PageExtractor"
                    );
                } catch (error) {
                  // TODO (Bug 2001385) - It would be nice to catch if this is the
                  // `about:neterror` page or other similar errors. This will also fail if you
                  // try to access something like `about:reader` with the same error.
                  navigateEvent.finish({
                    status: "error",
                    errorName: error.name,
                  });
                  actorResolver.reject(
                    new DOMException(
                      "PageExtractor could not run on that page or the page could not be found.",
                      error.name
                    )
                  );
                  return;
                }

                navigateEvent.finish({ status: "success" });
                // If the document is about to be replaced, the read of its
                // replacement takes over. Off-site nothing replaces it, so the
                // read fails.
                const superseded = hasPendingNavigation =>
                  !offSiteLocation &&
                  (hasPendingNavigation || wasReplaced(actor));
                actor
                  .waitForPageReady(
                    traceId,
                    deadline - ChromeUtils.now(),
                    settleDelayMs
                  )
                  .then(
                    ({ hasPendingNavigation }) => {
                      if (!superseded(hasPendingNavigation)) {
                        lazy.console.log(
                          "Headless PageExtractor is ready",
                          url
                        );
                        actorResolver.resolve(actor);
                      }
                    },
                    error => {
                      if (!superseded(false)) {
                        actorResolver.reject(error);
                      }
                    }
                  );
                notifyHeadlessLocation(location.spec);
              },
            };

            browser.addProgressListener(progressListener, progressFlags);

            lazy.console.log("Loading a headless PageExtractor", url);

            /** @type {LoadURIOptions} */
            const loadURIOptions = {
              triggeringPrincipal:
                Services.scriptSecurityManager.createNullPrincipal({}),
            };
            if (anonymousFetch) {
              // Suppress the Referer header so the origin can't learn where the
              // request came from (e.g. the SERP page that surfaced this URL).
              const referrerInfo = Cc[
                "@mozilla.org/referrer-info;1"
              ].createInstance(Ci.nsIReferrerInfo);
              referrerInfo.init(Ci.nsIReferrerInfo.NO_REFERRER, true, null);
              loadURIOptions.referrerInfo = referrerInfo;
              // Don't add an entry for this load to session history.
              loadURIOptions.loadFlags =
                Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_HISTORY;
            }

            browser.loadURI(url.URI, loadURIOptions);

            // The load may never commit on the requested host: the network can
            // stall, or bot detection can redirect to a challenge page elsewhere.
            const onDeadline = () => {
              if (offSiteLocation) {
                navigateEvent.finish({
                  status: "error",
                  errorName: "BlockedError",
                });
                actorResolver.reject(
                  new DOMException(
                    `The page ended up at ${offSiteLocation} instead of ${url.href} and never returned within ${timeoutMs}ms.`,
                    "BlockedError"
                  )
                );
                return;
              }
              navigateEvent.finish({
                status: "error",
                errorName: "TimeoutError",
              });
              actorResolver.reject(
                new DOMException(
                  `The page did not load in a headless browser within ${timeoutMs}ms: ${url.href}`,
                  "TimeoutError"
                )
              );
            };
            const timeoutId = lazy.setTimeout(onDeadline, timeoutMs);
            headlessDeadlines.add(onDeadline);

            const onAbort = () => {
              navigateEvent.finish({
                status: "error",
                errorName: "AbortError",
              });
              actorResolver.reject(new DOMException("Aborted", "AbortError"));
            };
            signal?.addEventListener("abort", onAbort, { once: true });
            if (signal?.aborted) {
              onAbort();
            }
            const ready = actorResolver.promise.finally(() =>
              signal?.removeEventListener("abort", onAbort)
            );

            try {
              return await callback(await ready, traceId);
            } catch (error) {
              if (offSiteLocation && error?.name !== "BlockedError") {
                throw new DOMException(
                  `The page navigated to ${offSiteLocation} instead of ${url.href} before its content could be read.`,
                  "BlockedError"
                );
              }
              throw error;
            } finally {
              lazy.clearTimeout(timeoutId);
              headlessDeadlines.delete(onDeadline);
              browser.removeProgressListener(progressListener, progressFlags);
            }
          },
          {
            // Create a custom message manager group for this browser so that the PageExtractor
            // actor can communicate with it. The actor is registered to use this custom
            // message manager group.
            messageManagerGroup: "headless-browsers",
          }
        );
      }
    );
  }
}
