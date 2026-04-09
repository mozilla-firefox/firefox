/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// @ts-check

/**
 * @import { GetTextOptions, CanvasSnapshot, ExtractionResult, PageMetadata, ReaderModeDocument } from './PageExtractor.d.ts'
 * @import { PageExtractorParent } from './PageExtractorParent.sys.mjs'
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
      prefix: "PageExtractorChild",
      maxLogLevelPref: "browser.ml.logLevel",
    }),
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
  extractTextFromDOM:
    "moz-src:///toolkit/components/pageextractor/DOMExtractor.sys.mjs",
  isProbablyReaderable: "resource://gre/modules/Readerable.sys.mjs",
});

/**
 * Extract a variety of content from pages for use in a smart window.
 */
export class PageExtractorChild extends JSWindowActorChild {
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
      case "PageExtractorParent:GetText":
        await this.waitForPageReady();
        return this.getText(data);
      case "PageExtractorParent:WaitForPageReady":
        return this.waitForPageReady();
      case "PageExtractorParent:GetPageMetadata":
        if (this.isAboutReader()) {
          const document = this.browsingContext?.window?.document;
          const result = await this.getText({ removeBoilerplate: true });
          const text = result?.text ?? "";
          const language = document?.querySelector(".container")?.lang ?? "";
          const wordCount = this.#getWordCount(language, text);

          return {
            structuredDataTypes: [],
            wordCount,
            language,
            isReaderable: true,
          };
        }
        return this.getPageMetadata();
    }
    return Promise.reject(new Error("Unknown message: " + name));
  }

  /**
   * This function resolves once the page is ready after a requestIdleCallback.
   *
   * @returns {Promise<void>}
   */
  async waitForPageReady() {
    return new Promise(resolve => {
      const waitForIdle = () => {
        this.document.ownerGlobal.requestIdleCallback(() => resolve(), {
          timeout: MAX_REQUEST_IDLE_CALLBACK_DELAY_MS,
        });
      };

      if (this.document.readyState == "loading") {
        this.document.addEventListener("DOMContentLoaded", waitForIdle);
      } else {
        lazy.console.log("The page is already interactive");
        waitForIdle();
      }
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

    const structuredDataTypes = this.#extractStructuredDataTypes(document);
    const language = this.#detectLanguage(document);
    const wordCount = this.#getWordCount(language, document.body.innerText);
    const isReaderable = lazy.isProbablyReaderable(document);

    return { structuredDataTypes, wordCount, language, isReaderable };
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
   * This extracts various `@type` values within the JSON-LD structured data markup of a page.
   *
   * @param {Document} document
   * @returns {string[]}
   */
  #extractStructuredDataTypes(document) {
    const scripts = document.querySelectorAll(
      'script[type="application/ld+json" i]'
    );
    const types = new Set();

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
    }

    return Array.from(types);
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
   * @returns {Promise<ExtractionResult | null>}
   */
  async getText(options = {}) {
    const window = this.browsingContext?.window;
    /** @type {Document} */
    let document = window?.document;
    /** @type {HTMLElement} */
    let rootNode;

    if (this.isAboutReader()) {
      // If about:reader is loaded, find the proper rootNode so that we just get the
      // content and not any of the UI. This will get passed to DOMExtractor so that
      // the rest of the GetTextOptions can be applied.

      lazy.console.log("Extracting content from about:reader");
      // TODO - Explain what's different between this document and the browsing context.
      document = this.manager.contentWindow.document;

      if (!document) {
        lazy.console.log("No content document was available");
        return null;
      }

      /** @type {HTMLElement?} */
      rootNode = document.querySelector(".container");
      if (!rootNode) {
        lazy.console.log("No container was found in reader mode.");
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
        const readerModeDocument =
          await lazy.ReaderMode.parseDocument(document);
        if (readerModeDocument) {
          lazy.console.log("Document is readerable");
          const { content } = readerModeDocument;
          const parser = new DOMParser();
          document = parser.parseFromString(content, "text/html");
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
      return null;
    }

    // All of the content gets extracted using the DOMExtractor, which knows how
    // to apply certain settings in GetTextOptions.
    const { text, links, canvases } = lazy.extractTextFromDOM(
      document,
      rootNode,
      options
    );

    let canvasSnapshots = [];
    if (options.includeCanvasSnapshots && canvases.length) {
      canvasSnapshots = await this.#captureCanvases(canvases, options);
    }

    lazy.console.log("GetText", options);
    lazy.console.debug({ text, links, canvasSnapshots });

    return { text, links, canvasSnapshots };
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
   * @returns {Promise<CanvasSnapshot[]>}
   */
  async #captureCanvases(canvases, options) {
    const maxDimension = options.maxCanvasDimension ?? 1024;
    const quality = options.canvasQuality ?? 0.8;

    const results = await Promise.all(
      canvases.map(c => this.#captureCanvas(c, maxDimension, quality))
    );
    return results.filter(Boolean);
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
    const window = canvas.ownerGlobal;
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
