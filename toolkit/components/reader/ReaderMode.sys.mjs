// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Constants for telemetry.
const DOWNLOAD_SUCCESS = 0;
const DOWNLOAD_ERROR_XHR = 1;
const DOWNLOAD_ERROR_NO_DOC = 2;

const PARSE_SUCCESS = 0;
const PARSE_ERROR_TOO_MANY_ELEMENTS = 1;
const PARSE_ERROR_WORKER = 2;
const PARSE_ERROR_NO_ARTICLE = 3;

// Class names to preserve in the readerized output. We preserve these class
// names so that rules in aboutReader.css can match them.
const CLASSES_TO_PRESERVE = [
  "caption",
  "emoji",
  "hidden",
  "invisible",
  "sr-only",
  "visually-hidden",
  "visuallyhidden",
  "wp-caption",
  "wp-caption-text",
  "wp-smiley",
];

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  LanguageDetector:
    "resource://gre/modules/translations/LanguageDetector.sys.mjs",
  ReaderWorker: "moz-src:///toolkit/components/reader/ReaderWorker.sys.mjs",
  Readerable: "resource://gre/modules/Readerable.sys.mjs",
});

const gIsFirefoxDesktop =
  Services.appinfo.ID == "{ec8030f7-c20a-464f-9b0e-13a3a9e97384}";

export var ReaderMode = {
  DEBUG: 0,

  // For time spent telemetry
  enterTime: undefined,
  leaveTime: undefined,

  /**
   * Enter the reader mode by going forward one step in history if applicable,
   * if not, append the about:reader page in the history instead.
   */
  enterReaderMode(docShell, win) {
    this.enterTime = Date.now();

    Glean.readermode.viewOn.record({
      subcategory: "feature",
    });

    let url = win.document.location.href;
    let readerURL = "about:reader?url=" + encodeURIComponent(url);

    if (!Services.appinfo.sessionHistoryInParent) {
      let webNav = docShell.QueryInterface(Ci.nsIWebNavigation);
      let sh = webNav.sessionHistory;
      if (webNav.canGoForward) {
        let forwardEntry = sh.legacySHistory.getEntryAtIndex(sh.index + 1);
        let forwardURL = forwardEntry.URI.spec;
        if (forwardURL && (forwardURL == readerURL || !readerURL)) {
          webNav.goForward();
          return;
        }
      }
    }

    // This could possibly move to the parent. See bug 1664982.
    win.document.location = readerURL;
  },

  /**
   * Exit the reader mode by going back one step in history if applicable,
   * if not, append the original page in the history instead.
   */
  leaveReaderMode(docShell, win) {
    this.leaveTime = Date.now();

    // Measured in seconds (whole number)
    let timeSpentInReaderMode = Math.floor(
      (this.leaveTime - this.enterTime) / 1000
    );

    // Measured as percentage (whole number)
    let scrollPosition = Math.floor(
      ((win.scrollY + win.innerHeight) / win.document.body.clientHeight) * 100
    );

    Glean.readermode.viewOff.record({
      subcategory: "feature",
      reader_time: `${timeSpentInReaderMode}`,
      scroll_position: `${scrollPosition}`,
    });

    let url = win.document.location.href;
    let originalURL = this.getOriginalUrl(url);
    let webNav = docShell.QueryInterface(Ci.nsIWebNavigation);

    if (!Services.appinfo.sessionHistoryInParent) {
      let sh = webNav.sessionHistory;
      if (webNav.canGoBack) {
        let prevEntry = sh.legacySHistory.getEntryAtIndex(sh.index - 1);
        let prevURL = prevEntry.URI.spec;
        if (prevURL && (prevURL == originalURL || !originalURL)) {
          webNav.goBack();
          return;
        }
      }
    }

    let referrerURI, principal;
    try {
      referrerURI = Services.io.newURI(url);
      principal = Services.scriptSecurityManager.createContentPrincipal(
        referrerURI,
        win.document.nodePrincipal.originAttributes
      );
    } catch (e) {
      console.error(e);
      return;
    }
    let loadFlags = webNav.LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL;
    let ReferrerInfo = Components.Constructor(
      "@mozilla.org/referrer-info;1",
      "nsIReferrerInfo",
      "init"
    );
    let loadURIOptions = {
      triggeringPrincipal: principal,
      loadFlags,
      referrerInfo: new ReferrerInfo(
        Ci.nsIReferrerInfo.EMPTY,
        true,
        referrerURI
      ),
    };
    // This could possibly move to the parent. See bug 1664982.
    webNav.fixupAndLoadURIString(originalURL, loadURIOptions);
  },

  /**
   * Returns original URL from an about:reader URL.
   *
   * @param url An about:reader URL.
   * @return The original URL for the article, or null if we did not find
   *         a properly formatted about:reader URL.
   */
  getOriginalUrl(url) {
    if (!url.startsWith("about:reader?")) {
      return null;
    }

    let outerHash = "";
    try {
      let uriObj = Services.io.newURI(url);
      url = uriObj.specIgnoringRef;
      outerHash = uriObj.ref;
    } catch (ex) {
      /* ignore, use the raw string */
    }

    let searchParams = new URLSearchParams(
      url.substring("about:reader?".length)
    );
    if (!searchParams.has("url")) {
      return null;
    }
    let originalUrl = searchParams.get("url");
    if (outerHash) {
      try {
        let uriObj = Services.io.newURI(originalUrl);
        uriObj = Services.io.newURI("#" + outerHash, null, uriObj);
        originalUrl = uriObj.spec;
      } catch (ex) {}
    }
    return originalUrl;
  },

  getOriginalUrlObjectForDisplay(url) {
    let originalUrl = this.getOriginalUrl(url);
    if (originalUrl) {
      let uriObj;
      try {
        uriObj = Services.uriFixup.getFixupURIInfo(originalUrl).preferredURI;
      } catch (ex) {
        return null;
      }
      try {
        return Services.io.createExposableURI(uriObj);
      } catch (ex) {
        return null;
      }
    }
    return null;
  },

  /**
   * Gets an article from a loaded browser's document. This method will not attempt
   * to parse certain URIs (e.g. about: URIs).
   *
   * @param doc A document to parse.
   * @return {Promise}
   * @resolves JS object representing the article, or null if no article is found.
   */
  parseDocument(doc) {
    if (
      !lazy.Readerable.shouldCheckUri(doc.documentURIObject) ||
      !lazy.Readerable.shouldCheckUri(doc.baseURIObject, true)
    ) {
      this.log("Reader mode disabled for URI");
      return null;
    }

    return this._readerParse(doc);
  },

  /**
   * Downloads and parses a document from a URL.
   *
   * @param url URL to download and parse.
   * @param attrs OriginAttributes to use for the request.
   * @return {Promise}
   * @resolves JS object representing the article, or null if no article is found.
   */
  async downloadAndParseDocument(url, attrs = {}, docContentType = "document") {
    let result = await this._downloadDocument(url, attrs, docContentType);
    if (!result?.doc) {
      return null;
    }
    let { doc, newURL } = result;
    if (
      !lazy.Readerable.shouldCheckUri(doc.documentURIObject) ||
      !lazy.Readerable.shouldCheckUri(doc.baseURIObject, true)
    ) {
      this.log("Reader mode disabled for URI");
      return null;
    }

    let article = await this._readerParse(doc);
    // If we have to redirect, reject to the caller with the parsed article,
    // so we can update the URL before displaying it.
    if (newURL) {
      return Promise.reject({ newURL, article });
    }
    // Otherwise, we can just continue with the article.
    return article;
  },

  _downloadDocument(url, attrs = {}, docContentType = "document") {
    let uri;
    try {
      uri = Services.io.newURI(url);
      if (!lazy.Readerable.shouldCheckUri(uri)) {
        return null;
      }
    } catch (ex) {
      console.error(
        new Error(`Couldn't create URI from ${url} to download: ${ex}`)
      );
      return null;
    }
    try {
      attrs.firstPartyDomain = Services.eTLD.getSchemelessSite(uri);
    } catch (e) {
      console.error("Failed to get first party domain for about:reader", e);
    }
    return new Promise((resolve, reject) => {
      let xhr = new XMLHttpRequest({ mozAnon: false });
      xhr.open("GET", url, true);
      xhr.setOriginAttributes(attrs);
      xhr.onerror = evt => reject(evt.error);
      xhr.responseType = docContentType === "text/plain" ? "text" : "document";
      xhr.onload = () => {
        if (xhr.status !== 200) {
          reject("Reader mode XHR failed with status: " + xhr.status);
          Glean.readermode.downloadResult.accumulateSingleSample(
            DOWNLOAD_ERROR_XHR
          );
          return;
        }

        let doc =
          xhr.responseType === "text" ? xhr.responseText : xhr.responseXML;
        if (!doc) {
          reject("Reader mode XHR didn't return a document");
          Glean.readermode.downloadResult.accumulateSingleSample(
            DOWNLOAD_ERROR_NO_DOC
          );
          return;
        }

        let responseURL = xhr.responseURL;
        let givenURL = url;
        // Convert these to real URIs to make sure the escaping (or lack
        // thereof) is identical:
        try {
          responseURL = Services.io.newURI(responseURL).specIgnoringRef;
        } catch (ex) {
          /* Ignore errors - we'll use what we had before */
        }
        try {
          givenURL = Services.io.newURI(givenURL).specIgnoringRef;
        } catch (ex) {
          /* Ignore errors - we'll use what we had before */
        }

        if (xhr.responseType != "document") {
          let initialText = doc;
          let parser = new DOMParser();
          doc = parser.parseFromString(`<pre></pre>`, "text/html");
          doc.querySelector("pre").textContent = initialText;
        }

        // We treat redirects as download successes here:
        Glean.readermode.downloadResult.accumulateSingleSample(
          DOWNLOAD_SUCCESS
        );

        let result = { doc };
        if (responseURL != givenURL) {
          result.newURL = xhr.responseURL;
        }

        resolve(result);
      };
      xhr.send();
    });
  },

  log(msg) {
    if (this.DEBUG) {
      dump("Reader: " + msg);
    }
  },

  /**
   * Attempts to parse a document into an article. Heavy lifting happens
   * in Reader.worker.js.
   *
   * @param doc The document to parse.
   * @return {Promise}
   * @resolves JS object representing the article, or null if no article is found.
   */
  async _readerParse(doc) {
    if (this.parseNodeLimit) {
      let numTags = doc.getElementsByTagName("*").length;
      if (numTags > this.parseNodeLimit) {
        this.log(
          "Aborting parse for " +
            doc.baseURIObject.spec +
            "; " +
            numTags +
            " elements found"
        );
        Glean.readermode.parseResult.accumulateSingleSample(
          PARSE_ERROR_TOO_MANY_ELEMENTS
        );
        return null;
      }
    }

    // Fetch this here before we send `doc` off to the worker thread, as later on the
    // document might be nuked but we will still want the URI.
    let { documentURI } = doc;

    let uriParam;
    uriParam = {
      spec: doc.baseURIObject.spec,
      prePath: doc.baseURIObject.prePath,
      scheme: doc.baseURIObject.scheme,

      // Fallback
      host: documentURI,
      pathBase: documentURI,
    };

    // nsIURI.host throws an exception if a host doesn't exist.
    try {
      uriParam.host = doc.baseURIObject.host;
      uriParam.pathBase = Services.io.newURI(".", null, doc.baseURIObject).spec;
    } catch (ex) {
      // Fall back to the initial values we assigned.
      console.warn("Error accessing host name: ", ex);
    }

    // convert text/plain document, if any, to XHTML format
    if (this._isDocumentPlainText(doc)) {
      doc = this._convertPlainTextDocument(doc);
    }

    let serializer = new XMLSerializer();
    let serializedDoc = serializer.serializeToString(doc);
    // Explicitly null out doc to make it clear it might not be available from this
    // point on.
    doc = null;

    let options = {
      classesToPreserve: CLASSES_TO_PRESERVE,
      debug: Services.prefs.getBoolPref("reader.debug", false),
    };

    let article = null;
    try {
      article = await lazy.ReaderWorker.post("parseDocument", [
        uriParam,
        serializedDoc,
        options,
      ]);
    } catch (e) {
      console.error("Error in ReaderWorker: ", e);
      Glean.readermode.parseResult.accumulateSingleSample(PARSE_ERROR_WORKER);
    }

    if (!article) {
      this.log("Worker did not return an article");
      Glean.readermode.parseResult.accumulateSingleSample(
        PARSE_ERROR_NO_ARTICLE
      );
      return null;
    }

    // Readability returns a URI object based on the baseURI, but we only care
    // about the original document's URL from now on. This also avoids spoofing
    // attempts where the baseURI doesn't match the domain of the documentURI
    article.url = documentURI;
    delete article.uri;

    let flags =
      Ci.nsIDocumentEncoder.OutputSelectionOnly |
      Ci.nsIDocumentEncoder.OutputAbsoluteLinks;
    article.title = Cc["@mozilla.org/parserutils;1"]
      .getService(Ci.nsIParserUtils)
      .convertToPlainText(article.title, flags, 0);
    if (gIsFirefoxDesktop) {
      await this._assignLanguage(article);
      this._maybeAssignTextDirection(article);
    }

    this._assignReadTime(article);

    Glean.readermode.parseResult.accumulateSingleSample(PARSE_SUCCESS);
    return article;
  },

  /**
   * Sets a global language string value if the result is confident
   *
   * @return Promise
   * @resolves when the language is detected
   */
  _assignLanguage(article) {
    return lazy.LanguageDetector.detectLanguage(article.textContent).then(
      result => {
        article.detectedLanguage = result.confident ? result.language : null;
      }
    );
  },

  _maybeAssignTextDirection(article) {
    // Assign `article.dir` a value if not set and if we have a valid detected language.
    if (
      !article.dir &&
      typeof article.detectedLanguage === "string" &&
      article.detectedLanguage
    ) {
      article.dir = Services.intl.getScriptDirection(article.detectedLanguage);
    }
  },

  /**
   * Assigns the estimated reading time range of the article to the article object.
   *
   * @param article the article object to assign the reading time estimate to.
   */
  _assignReadTime(article) {
    let lang = article.detectedLanguage || "en";
    const readingSpeed = this._getReadingSpeedForLanguage(lang);
    const charactersPerMinuteLow = readingSpeed.cpm - readingSpeed.variance;
    const charactersPerMinuteHigh = readingSpeed.cpm + readingSpeed.variance;
    const length = article.length;

    article.readingTimeMinsSlow = Math.ceil(length / charactersPerMinuteLow);
    article.readingTimeMinsFast = Math.ceil(length / charactersPerMinuteHigh);
  },

  /**
   * Returns the reading speed of a selection of languages with likely variance.
   *
   * Reading speed estimated from a study done on reading speeds in various languages.
   * study can be found here: http://iovs.arvojournals.org/article.aspx?articleid=2166061
   *
   * @return object with characters per minute and variance. Defaults to English
   *         if no suitable language is found in the collection.
   */
  _getReadingSpeedForLanguage(lang) {
    const readingSpeed = new Map([
      ["en", { cpm: 987, variance: 118 }],
      ["ar", { cpm: 612, variance: 88 }],
      ["de", { cpm: 920, variance: 86 }],
      ["es", { cpm: 1025, variance: 127 }],
      ["fi", { cpm: 1078, variance: 121 }],
      ["fr", { cpm: 998, variance: 126 }],
      ["he", { cpm: 833, variance: 130 }],
      ["it", { cpm: 950, variance: 140 }],
      ["ja", { cpm: 357, variance: 56 }],
      ["nl", { cpm: 978, variance: 143 }],
      ["pl", { cpm: 916, variance: 126 }],
      ["pt", { cpm: 913, variance: 145 }],
      ["ru", { cpm: 986, variance: 175 }],
      ["sl", { cpm: 885, variance: 145 }],
      ["sv", { cpm: 917, variance: 156 }],
      ["tr", { cpm: 1054, variance: 156 }],
      ["zh", { cpm: 255, variance: 29 }],
    ]);

    return readingSpeed.get(lang) || readingSpeed.get("en");
  },
  /**
   *
   * Check if the document to be parsed is text document.
   * @param doc the doc object to be parsed.
   * @return boolean
   *
   */
  _isDocumentPlainText(doc) {
    return doc.contentType == "text/plain";
  },
  /**
   *
   * The document to be parsed is text document and is converted to HTML format.
   * @param doc the doc object to be parsed.
   * @return doc
   *
   */
  _convertPlainTextDocument(doc) {
    let preTag = doc.querySelector("pre");
    let docFrag = doc.createDocumentFragment();
    let content = preTag.textContent;
    let paragraphs = content.split(/\r?\n\r?\n/);
    for (let para of paragraphs) {
      let pElem = doc.createElement("p");
      let lines = para.split(/\n/);
      for (let line of lines) {
        pElem.append(line);
        let brElem = doc.createElement("br");
        pElem.append(brElem);
      }
      docFrag.append(pElem);
    }
    // Clone the document to avoid the original document being affected
    // (which shows up when exiting reader mode again).
    let clone = doc.documentElement.cloneNode(true);
    clone.querySelector("pre").replaceWith(docFrag);
    return clone;
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  ReaderMode,
  "maxElemsToParse",
  "reader.parse-node-limit",
  0
);
