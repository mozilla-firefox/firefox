/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// @ts-check

/**
 * @import { GetTextOptions, CanvasSnapshot, ExtractionResult, PageMetadata, ReaderModeDocument } from './PageExtractor.d.ts'
 * @import { PageExtractorParent } from './PageExtractorParent.sys.mjs'
 * @import { PageExtractorPhase, TraceId } from './PageExtractorEvents.sys.mjs'
 */

/**
 * We wait for the page to be ready before extracting content headlessly. It's hard
 * to know when a page is "ready", however the strategy here is to wait for
 * DOMContentLoaded, and then a requestIdleCallback. This way the page has time
 * to do an initial amount of work. However, if we wait too long, it will be felt by
 * the user as lag. To mitigate this, wait for at least 2 seconds for the page to settle.
 */
const MAX_REQUEST_IDLE_CALLBACK_DELAY_MS = 2000;

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  console: () =>
    console.createInstance({
      logModule: "PageExtractorChild",
      maxLogLevelPref: "browser.ml.logLevel",
    }),
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
  extractTextFromDOM:
    "moz-src:///toolkit/components/pageextractor/DOMExtractor.sys.mjs",
  shouldExtractYouTube:
    "moz-src:///toolkit/components/pageextractor/YouTubeExtraction.sys.mjs",
  getYouTubeContent:
    "moz-src:///toolkit/components/pageextractor/YouTubeExtraction.sys.mjs",
  PageExtractorEvent:
    "moz-src:///toolkit/components/pageextractor/PageExtractorEvents.sys.mjs",
  Phase: () => lazy.PageExtractorEvent.Phase,
  isProbablyReaderable: "resource://gre/modules/Readerable.sys.mjs",
  youtubeTimeoutMs: {
    pref: "browser.pageextractor.youtube.timeoutMs",
    default: 3000,
  },
});

/**
 * Extract a variety of content from pages for use in a smart window.
 */
export class PageExtractorChild extends JSWindowActorChild {
  /**
   * True once this actor has waited for its document to become page-ready.
   *
   * @type {boolean}
   */
  #isPageReady = false;

  /**
   * Route the messages coming from the parent process.
   *
   * @param {object} message
   * @param {string} message.name
   * @param {any} message.data
   *
   * @returns {Promise<unknown>}
   */
  async receiveMessage({ name, data }) {
    switch (name) {
      case "PageExtractorParent:GetText": {
        const { options, traceId } = data;
        if (!this.#isPageReady) {
          await this.waitForPageReady(traceId);
        }
        return this.getText(options, traceId);
      }
      case "PageExtractorParent:WaitForPageReady":
        return this.waitForPageReady(
          data?.traceId,
          data?.refreshWithinMs,
          data?.settleDelayMs
        );
      case "PageExtractorParent:GetPageMetadata":
        return this.#getPageMetadata(data?.traceId);
    }
    return Promise.reject(new Error("Unknown message: " + name));
  }

  /**
   * Resolves after DOMContentLoaded, an idle callback, and a double
   * requestAnimationFrame so layout and paint are committed before
   * extraction reads page geometry.
   *
   * @param {TraceId} [traceId]
   * @param {number} [refreshWithinMs] - A refresh only counts as a pending
   *   navigation when it fires within this many milliseconds.
   * @param {number} [settleDelayMs] - Extra wait after the page is ready.
   * @returns {Promise<{hasPendingNavigation: boolean}>} hasPendingNavigation
   *   is true when the document is about to be replaced by a refresh or a
   *   load that has started but not yet committed.
   */
  async waitForPageReady(traceId, refreshWithinMs = Infinity, settleDelayMs) {
    const doc = this.document;
    const win = doc.documentGlobal;
    return this.#trace(lazy.Phase.waitForReady, { traceId }, async event => {
      if (doc.readyState == "loading") {
        await new Promise(resolve => {
          doc.addEventListener("DOMContentLoaded", resolve, { once: true });
        });
      } else {
        lazy.console.log("The page is already interactive");
      }

      await new Promise(resolve => {
        win.requestIdleCallback(resolve, {
          timeout: MAX_REQUEST_IDLE_CALLBACK_DELAY_MS,
        });
      });

      if (settleDelayMs) {
        // The page's timer queue runs this after its own timers of equal delay.
        await new Promise(resolve => win.setTimeout(resolve, settleDelayMs));
      }

      const wasHidden = doc.hidden;
      if (!wasHidden) {
        await new Promise(resolve => {
          win.requestAnimationFrame(() => win.requestAnimationFrame(resolve));
        });
        this.#isPageReady = true;
      }

      event.finish({ status: wasHidden ? "document-hidden" : "success" });
      return {
        hasPendingNavigation: this.#hasPendingNavigation(refreshWithinMs),
      };
    });
  }

  /**
   * @param {number} refreshWithinMs
   * @returns {boolean}
   */
  #hasPendingNavigation(refreshWithinMs) {
    const { docShell } = this;
    if (docShell.QueryInterface(Ci.nsIRefreshURI).refreshPending) {
      // A page that reloads itself every few minutes is read as it is, rather
      // than waited on past the point where the read gives up.
      const delayMs = this.#refreshDelayMs();
      if (delayMs === null || delayMs <= refreshWithinMs) {
        return true;
      }
    }
    // During a replacement load, documentRequest is the incoming document,
    // while currentDocumentChannel remains the current one until it commits.
    const webProgress = docShell.QueryInterface(Ci.nsIWebProgress);
    return (
      webProgress.isLoadingDocument &&
      webProgress.documentRequest !== docShell.currentDocumentChannel
    );
  }

  /**
   * The shortest delay of the refreshes the document declares, in a meta
   * element or a Refresh response header, parsed the way the HTML spec's
   * shared declarative refresh steps read the time. The URL is not parsed,
   * so a declaration the docshell drops for an unparsable URL still counts.
   *
   * @returns {number | null} The delay in milliseconds, or null when no
   *   declaration can be parsed.
   */
  #refreshDelayMs() {
    const declarations = Array.from(
      this.document.querySelectorAll('meta[http-equiv="refresh" i]'),
      meta => meta.content
    );
    try {
      declarations.push(
        this.docShell.currentDocumentChannel
          .QueryInterface(Ci.nsIHttpChannel)
          .getResponseHeader("Refresh")
      );
    } catch {
      // Not an http document, or no Refresh header.
    }
    const delays = [];
    for (const declaration of declarations) {
      // ^[\t\n\f\r ]*          Skip leading ASCII whitespace.
      // (?=[\d.])              The time must start with a digit or a dot.
      // (\d*)                  Capture the whole seconds, possibly none (".5").
      // [\d.]*                 Skip any fraction; the spec ignores it.
      // (?:[;,\t\n\f\r ]|$)    The time ends at a separator or the end, else
      //                        the declaration is invalid.
      const match = /^[\t\n\f\r ]*(?=[\d.])(\d*)[\d.]*(?:[;,\t\n\f\r ]|$)/.exec(
        declaration
      );
      if (match) {
        delays.push(Number(match[1] || 0) * 1000);
      }
    }
    return delays.length ? Math.min(...delays) : null;
  }

  /**
   * @param {TraceId} [traceId]
   * @returns {Promise<PageMetadata>}
   */
  async #getPageMetadata(traceId) {
    return this.#trace(lazy.Phase.getPageMetadata, { traceId }, async event => {
      let result;
      let strategy;
      if (this.isAboutReader()) {
        const document = this.browsingContext?.window?.document;
        const extraction = await this.getText(
          { removeBoilerplate: true },
          event.traceId
        );
        const text = extraction?.text ?? "";
        const language = document?.querySelector(".container")?.lang ?? "";
        result = {
          structuredDataTypes: [],
          wordCount: this.#getWordCount(language, text),
          language,
          isReaderable: true,
          isGated: false,
        };
        strategy = "about-reader";
      } else {
        result = await this.getPageMetadata();
        strategy = "dom";
      }
      event.addData({ strategy });
      return result;
    });
  }

  /**
   * @see PageExtractorParent#getPageMetadata for docs
   *
   * @returns {Promise<PageMetadata>}
   */
  async getPageMetadata() {
    const document = this.browsingContext?.window?.document;

    if (!document) {
      return Promise.reject(
        new Error("No document available for page metadata extraction.")
      );
    }

    const { types: structuredDataTypes, isGated } =
      this.#extractStructuredData(document);
    const language = this.#detectLanguage(document);
    const wordCount = this.#getWordCount(language, document.body.innerText);
    const isReaderable = lazy.isProbablyReaderable(document);

    return { structuredDataTypes, wordCount, language, isReaderable, isGated };
  }

  /**
   * This will establish a word count of the text argument based on the provided language.
   *
   * @param {string} language
   * @param {string} text
   * @returns {number}
   */
  #getWordCount(language, text) {
    let wordCount = 0;
    const segmenter = new Intl.Segmenter(language || undefined, {
      granularity: "word",
    });
    for (const { isWordLike } of segmenter.segment(text)) {
      if (isWordLike) {
        wordCount++;
      }
    }
    return wordCount;
  }

  /**
   * Normalizes a schema.org Boolean, which may be an actual boolean, the strings
   * "true"/"false", or a schema.org URL such as "https://schema.org/False".
   *
   * @param {unknown} value
   * @returns {boolean | null} null when the value is absent or unrecognized.
   */
  #parseSchemaBoolean(value) {
    if (typeof value === "boolean") {
      return value;
    }
    if (typeof value !== "string") {
      return null;
    }
    switch (value.toLowerCase()) {
      case "true":
      case "http://schema.org/true":
      case "https://schema.org/true":
        return true;

      case "false":
      case "http://schema.org/false":
      case "https://schema.org/false":
        return false;

      default:
        return null;
    }
  }

  /**
   * This extracts various `@type` values within the JSON-LD structured data markup
   * of a page, along with whether the page declares its content to be gated.
   *
   * Publishers mark gated content with schema.org `isAccessibleForFree: false`,
   * either on the item itself or on the `hasPart` entry describing the gated
   * region. Pages without the markup are reported as not gated.
   *
   * @param {Document} document
   * @returns {{ types: string[], isGated: boolean }}
   */
  #extractStructuredData(document) {
    const scripts = document.querySelectorAll(
      'script[type="application/ld+json" i]'
    );
    const types = new Set();
    let isGated = false;

    const asArray = value => {
      if (Array.isArray(value)) {
        return value;
      }
      return value == null ? [] : [value];
    };

    for (const script of scripts) {
      const text = script.textContent?.trim();
      if (!text) {
        continue;
      }

      let parsed;
      try {
        parsed = JSON.parse(text);
      } catch {
        continue;
      }

      // JSON-LD can be:
      // - an object
      // - an array of objects
      // - an object with @graph: [...]
      const topLevelItems = asArray(parsed);
      const graphItems = topLevelItems.flatMap(x => asArray(x?.["@graph"]));
      const items = graphItems.length ? graphItems : topLevelItems;

      for (const item of items) {
        for (const t of asArray(item?.["@type"])) {
          if (typeof t === "string") {
            types.add(t);
          }
        }
      }

      // Unlike `@type`, the gating marker is also honored on a wrapper object that
      // carries an `@graph`, since publishers place it at either level.
      for (const item of [...topLevelItems, ...graphItems]) {
        for (const part of [item, ...asArray(item?.hasPart)]) {
          if (this.#parseSchemaBoolean(part?.isAccessibleForFree) === false) {
            isGated = true;
          }
        }
      }
    }

    return { types: Array.from(types), isGated };
  }

  /**
   * Query the lang tag of the document.
   *
   * @param {Document} document
   * @returns {string}
   */
  #detectLanguage(document) {
    const declared = document?.documentElement?.lang;
    if (declared) {
      try {
        return new Intl.Locale(declared).baseName;
      } catch {
        return "";
      }
    }
    return "";
  }

  /**
   * @see PageExtractorParent#getText for docs
   *
   * @param {GetTextOptions} options
   * @param {TraceId} [traceId]
   * @returns {Promise<ExtractionResult | null>}
   */
  async getText(options = {}, traceId) {
    return this.#trace(
      lazy.Phase.getText,
      { traceId, options, strategy: "dom" },
      async event => {
        let strategy = "dom";
        const window = this.browsingContext?.window;
        /** @type {Document} */
        let document = window?.document;
        /** @type {HTMLElement} */
        let rootNode;

        // YouTube extraction is a best-effort enhancement: any failure is
        // logged and degrades to an empty string so the generic page
        // extraction is used.
        let youtubeContentPromise = null;
        const sourceUrl = URL.parse(options.sourceUrl);
        if (lazy.shouldExtractYouTube(sourceUrl)) {
          youtubeContentPromise = this.#trace(
            lazy.Phase.youtubeExtract,
            { traceId: event.traceId, strategy: "youtube" },
            async youtubeEvent => {
              const result = await lazy.getYouTubeContent(document, {
                timeoutMs: lazy.youtubeTimeoutMs,
                sufficientLength:
                  options.sufficientTranscriptLength ??
                  options.sufficientLength,
                currentVideoId: sourceUrl.searchParams.get("v"),
              });
              youtubeEvent.finish({
                status: result.text ? "success" : "empty",
                textLength: result.text.length,
              });
              return result;
            }
          ).catch(error => {
            lazy.console.warn("Failed to extract YouTube content", error);
            return { text: "", replacesContent: false };
          });
        }

        if (this.isAboutReader()) {
          strategy = "about-reader";
          event.addData({ strategy });
          // If about:reader is loaded, find the proper rootNode so that we just get the
          // content and not any of the UI. This will get passed to DOMExtractor so that
          // the rest of the GetTextOptions can be applied.

          lazy.console.log("Extracting content from about:reader");
          // TODO - Explain what's different between this document and the browsing context.
          document = this.manager.contentWindow.document;

          if (!document) {
            lazy.console.log("No content document was available");
            event.finish({ status: "unavailable" });
            return null;
          }

          /** @type {HTMLElement?} */
          rootNode = document.querySelector(".container");
          if (!rootNode) {
            lazy.console.log("No container was found in reader mode.");
            event.finish({ status: "unavailable" });
            return null;
          }
        } else if (options.removeBoilerplate) {
          // Boilerplate removal is requested. See if reader mode can be applied, and then
          // use that for boilerplate removal.

          if (
            (document && lazy.isProbablyReaderable(document)) ||
            options._forceRemoveBoilerplate
          ) {
            // Run the document through reader mode, and use the DOMParser version of the
            // content.
            /** @type {ReaderModeDocument | null} */
            const readerModeDocument = await this.#trace(
              lazy.Phase.readerParse,
              { traceId: event.traceId, strategy: "reader" },
              async readerEvent => {
                const result = await lazy.ReaderMode.parseDocument(document);
                readerEvent.finish({
                  status: result ? "success" : "not-readerable",
                });
                return result;
              }
            );
            if (readerModeDocument) {
              strategy = "reader";
              event.addData({ strategy });
              lazy.console.log("Document is readerable");
              document = await this.#trace(
                lazy.Phase.readerOutputParse,
                { traceId: event.traceId, strategy },
                () =>
                  new DOMParser().parseFromString(
                    readerModeDocument.content,
                    "text/html"
                  )
              );
              rootNode = document.body;
            } else {
              lazy.console.log(
                "Document is not readerable, boilerplate will not be removed"
              );
            }
          } else {
            lazy.console.log(
              "Document is not readerable, boilerplate will not be removed"
            );
          }
        }

        if (!document || !rootNode) {
          lazy.console.log("Extracting content without boilerplate removal.");
          // No document or no root node is here, we should use the default extraction
          // strategy, of getting content directly from the hpage.
          document = window?.document;
          rootNode = document.body;
        }

        if (!document) {
          lazy.console.log("No document was found.");
          event.finish({ status: "unavailable" });
          return null;
        }

        // All of the content gets extracted using the DOMExtractor, which
        // knows how to apply certain settings in GetTextOptions.
        const extraction = await this.#trace(
          lazy.Phase.domExtract,
          { traceId: event.traceId, strategy },
          domEvent => {
            const result = lazy.extractTextFromDOM(document, rootNode, options);
            domEvent.addData({
              textLength: result.text.length,
              linkCount: result.links.length,
              canvasCount: result.canvases.length,
              siteStrategy: result.siteStrategy,
            });
            return result;
          }
        );
        const { text, links, canvases } = extraction;
        if (extraction.siteStrategy) {
          event.addData({ siteStrategy: extraction.siteStrategy });
        }

        let canvasSnapshots = [];
        if (options.includeCanvasSnapshots && canvases.length) {
          canvasSnapshots = await this.#captureCanvases(
            canvases,
            options,
            event.traceId
          );
        }

        // On YouTube a transcript block replaces the generic walk. Without
        // a transcript the generic walk is kept (so comments and other page content
        // survive) and the clean metadata block (header fields + description), which
        // that walk only captures noisily and truncated, is prepended to it.
        const youtube = youtubeContentPromise
          ? await youtubeContentPromise
          : null;
        let finalText = text;
        if (youtube?.text) {
          finalText = youtube.replacesContent
            ? youtube.text
            : [youtube.text, text].filter(Boolean).join("\n\n");
          strategy = youtube.replacesContent
            ? "youtube-transcript"
            : "youtube-dom";
          event.addData({ strategy });
        }

        lazy.console.log("GetText", options);
        lazy.console.debug({ text: finalText, links, canvasSnapshots });

        event.finish({
          status: finalText ? "success" : "empty",
          textLength: finalText.length,
          linkCount: links.length,
          canvasCount: canvasSnapshots.length,
        });
        return { text: finalText, links, canvasSnapshots };
      }
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
        process: "content",
        innerWindowId:
          this.contentWindow?.windowGlobalChild?.innerWindowId ?? 0,
        ...data,
      },
      task
    );
  }

  /**
   * Checks if about:reader is loaded, which requires special handling.
   *
   * @returns {boolean}
   */
  isAboutReader() {
    // Accessing the documentURIObject in this way does not materialize the
    // `window.location.href` and should be a cheaper check here.
    let url = this.manager.contentWindow.document.documentURIObject;
    return url.schemeIs("about") && url.pathQueryRef.startsWith("reader?");
  }

  /**
   * Capture canvas elements as WebP blobs. WebP is chosen for its superior
   * compression-to-quality ratio compared to PNG/JPEG, reducing the data sent
   * to language models while preserving visual fidelity.
   *
   * @param {HTMLCanvasElement[]} canvases
   * @param {GetTextOptions} options
   * @param {TraceId} traceId
   * @returns {Promise<CanvasSnapshot[]>}
   */
  async #captureCanvases(canvases, options, traceId) {
    const maxDimension = options.maxCanvasDimension ?? 1024;
    const quality = options.canvasQuality ?? 0.8;

    return this.#trace(lazy.Phase.canvasCapture, { traceId }, async event => {
      const results = await Promise.all(
        canvases.map(c => this.#captureCanvas(c, maxDimension, quality))
      );
      const snapshots = results.filter(Boolean);
      event.finish({ status: "success", canvasCount: snapshots.length });
      return snapshots;
    });
  }

  /**
   * Capture a canvas element as a WebP blob. Uses OffscreenCanvas to avoid
   * blocking the main thread during scaling and blob conversion. ImageBitmap
   * is used as the source to efficiently transfer pixel data from the
   * original canvas.
   *
   * @param {HTMLCanvasElement} canvas
   * @param {number} maxDimension
   * @param {number} quality
   * @returns {Promise<CanvasSnapshot | null>}
   */
  async #captureCanvas(canvas, maxDimension, quality) {
    const window = canvas.documentGlobal;
    const { width: originalWidth, height: originalHeight } = canvas;

    try {
      const bitmap = await window.createImageBitmap(canvas);

      const scale = Math.min(
        1,
        maxDimension / Math.max(originalWidth, originalHeight)
      );
      const targetWidth = Math.floor(originalWidth * scale);
      const targetHeight = Math.floor(originalHeight * scale);

      const offscreen = new window.OffscreenCanvas(targetWidth, targetHeight);
      // Alpha is enabled to preserve transparency in canvases that use it.
      // willReadFrequently is false because we only draw and convert to blob,
      // never reading pixels back, so hardware acceleration is preferred.
      const ctx = offscreen.getContext("2d", {
        alpha: true,
        willReadFrequently: false,
      });

      ctx.drawImage(bitmap, 0, 0, targetWidth, targetHeight);
      bitmap.close();

      let blob;
      try {
        blob = await offscreen.convertToBlob({
          type: "image/webp",
          quality,
        });
      } catch (securityError) {
        // Tainted canvas fall back to original canvas toBlob which works
        blob = await new Promise((resolve, reject) => {
          canvas.toBlob(
            b => (b ? resolve(b) : reject(new Error("toBlob failed"))),
            "image/webp",
            quality
          );
        });

        return {
          blob,
          width: originalWidth,
          height: originalHeight,
        };
      }

      return {
        blob,
        width: targetWidth,
        height: targetHeight,
      };
    } catch (error) {
      lazy.console.debug?.("Canvas capture failed:", error);
      return null;
    }
  }
}
