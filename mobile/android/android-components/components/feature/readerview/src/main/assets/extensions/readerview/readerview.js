/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from readability/readability-0.4.2.js */
/* import-globals-from readability/JSDOMParser-0.4.2.js */

// Class names to preserve in the readerized output. We preserve these class
// names so that rules in readerview.css can match them. This list is taken from Fennec:
// https://dxr.mozilla.org/mozilla-central/rev/7d47e7fa2489550ffa83aae67715c5497048923f/toolkit/components/reader/ReaderMode.jsm#21
const preservedClasses = [
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

class ReaderView {
  static get MIN_FONT_SIZE() {
    return 1;
  }

  static get MAX_FONT_SIZE() {
    return 9;
  }

  /**
   * Shows a reader view for the provided document. This method is used when activating
   * reader view on the original page. In this case, we already have the DOM (passed
   * through in the message from the background script) and can parse it directly.
   *
   * @param doc the document to make readerable.
   * @param url the url of the article.
   * @param options the fontSize, fontType and colorScheme to use.
   */
  show(
    doc,
    url,
    options = { fontSize: 4, fontType: "sans-serif", colorScheme: "light" }
  ) {
    let result = new Readability(doc, {
      classesToPreserve: preservedClasses,
    }).parse();
    result.language = doc.documentElement.getAttribute("lang") || "";
    document.title = result.title;
    if (result.language) {
      document.documentElement.lang = result.language;
    }

    let article = Object.assign(
      result,
      { url: new URL(url) },
      { readingTime: this.getReadingTime(result.length, result.language) },
      { byline: this.getByline(result) },
      { dir: this.getTextDirection(result) },
      { title: this.getTitle(result) }
    );

    const sanitizer = new Sanitizer({
      allowAttributes: {
        class: ["*"],
        id: ["*"],
        src: ["img", "audio", "video", "source"],
        srcset: ["img", "source"],
        sizes: ["img", "source"],
        alt: ["img"],
        href: ["a"],
        rel: ["a"],
        dir: ["*"],
        lang: ["*"],
        title: ["*"],
        width: ["img", "video", "table", "td", "th", "col", "colgroup"],
        height: ["img", "video"],
        colspan: ["td", "th"],
        rowspan: ["td", "th"],
        scope: ["th"],
        datetime: ["del", "ins", "time"],
        cite: ["blockquote", "del", "ins", "q"],
        start: ["ol"],
        reversed: ["ol"],
        value: ["li"],
        open: ["details"],
        type: ["a", "ol", "li", "source"],
        target: ["a"],
      },
    });

    document.body.setHTML(this.createHtmlBody(article), { sanitizer });

    this.setFontSize(options.fontSize);
    this.setFontType(options.fontType);
    this.setColorScheme(options.colorScheme);
    if (options.scrollY) {
      window.scrollTo({ top: options.scrollY, left: 0, behavior: "instant" });
    }
  }

  /**
   * Allows adjusting the font size in discrete steps between ReaderView.MIN_FONT_SIZE
   * and ReaderView.MAX_FONT_SIZE.
   *
   * @param changeAmount e.g. +1, or -1.
   */
  changeFontSize(changeAmount) {
    var size = Math.max(
      ReaderView.MIN_FONT_SIZE,
      Math.min(ReaderView.MAX_FONT_SIZE, this.fontSize + changeAmount)
    );
    this.setFontSize(size);
  }

  /**
   * Sets the font size.
   *
   * @param fontSize must be value between ReaderView.MIN_FONT_SIZE
   * and ReaderView.MAX_FONT_SIZE.
   */
  setFontSize(fontSize) {
    let size = 10 + 2 * fontSize + "px";
    let readerView = document.getElementById("mozac-readerview-container");
    readerView.style.setProperty("font-size", size);
    this.fontSize = fontSize;
  }

  /**
   * Sets the font type.
   *
   * @param fontType the font type to use.
   */
  setFontType(fontType) {
    let bodyClasses = document.body.classList;

    if (this.fontType) {
      bodyClasses.remove(this.fontType);
    }

    this.fontType = fontType;
    bodyClasses.add(this.fontType);
  }

  /**
   * Sets the color scheme.
   *
   * @param colorScheme the color scheme to use, must be either light, dark
   * or sepia.
   */
  setColorScheme(colorScheme) {
    if (!["light", "sepia", "dark"].includes(colorScheme)) {
      console.error(`Invalid color scheme specified: ${colorScheme}`);
      return;
    }

    let bodyClasses = document.body.classList;

    if (this.colorScheme) {
      bodyClasses.remove(this.colorScheme);
    }

    this.colorScheme = colorScheme;
    bodyClasses.add(this.colorScheme);
  }

  /**
   * Create the reader view HTML body.
   *
   * @param article a JSONObject representing the article to show.
   */
  createHtmlBody(article) {
    const safeDir = this.escapeHTML(article.dir);
    const safeTitle = this.escapeHTML(article.title);
    const safeByline = this.escapeHTML(article.byline);
    const safeReadingTime = this.escapeHTML(article.readingTime);
    return `
      <body class="mozac-readerview-body">
        <div id="mozac-readerview-container" class="container" dir="${safeDir}">
          <div class="header">
            <a class="domain" href="${article.url.href}">${article.url.hostname}</a>
            <div class="domain-border"></div>
            <h1>${safeTitle}</h1>
            <div class="credits">${safeByline}</div>
            <div>
              <div>${safeReadingTime}</div>
            </div>
          </div>
          <hr>

          <div class="content">
            <div class="mozac-readerview-content">${article.content}</div>
          </div>
        </div>
      </body>
    `;
  }

  /**
   * Returns the estimated reading time as localized string.
   *
   * @param length of the article (number of chars).
   * @param optional language of the article, defaults to en.
   */
  getReadingTime(length, lang = "en") {
    const [readingSpeed, readingSpeedLang] =
      this.getReadingSpeedForLanguage(lang);
    const charactersPerMinuteLow = readingSpeed.cpm - readingSpeed.variance;
    const charactersPerMinuteHigh = readingSpeed.cpm + readingSpeed.variance;
    const readingTimeMinsSlow = Math.ceil(length / charactersPerMinuteLow);
    const readingTimeMinsFast = Math.ceil(length / charactersPerMinuteHigh);

    // Construct a localized and "humanized" reading time in minutes.
    // If we have both a fast and slow reading time we'll show both e.g.
    // "2 - 4 minutes", otherwise we'll just show "4 minutes".
    try {
      var parts = new Intl.RelativeTimeFormat(readingSpeedLang).formatToParts(
        readingTimeMinsSlow,
        "minute"
      );
      if (parts.length == 3) {
        // No need to use part[0] which represents the literal "in".
        var readingTime = parts[1].value; // reading time in minutes
        var minutesLiteral = parts[2].value; // localized singular or plural literal of 'minute'
        var readingTimeString = `${readingTime} ${minutesLiteral}`;
        if (readingTimeMinsSlow != readingTimeMinsFast) {
          readingTimeString = `${readingTimeMinsFast} - ${readingTimeString}`;
        }
        return readingTimeString;
      }
    } catch (error) {
      console.error(`Failed to format reading time: ${error}`);
    }

    return "";
  }

  /**
   * Returns the reading speed of a selection of languages with likely variance.
   *
   * Reading speed estimated from a study done on reading speeds in various languages.
   * study can be found here: http://iovs.arvojournals.org/article.aspx?articleid=2166061
   *
   * @return object with characters per minute and variance. Defaults to English
   * if no suitable language is found in the collection.
   */
  getReadingSpeedForLanguage(lang) {
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

    return readingSpeed.has(lang)
      ? [readingSpeed.get(lang), lang]
      : [readingSpeed.get("en"), "en"];
  }

  getByline(article) {
    return article.byline || "";
  }

  /**
   * Attempts to read the optional text direction from the article and uses
   * language mapping to detect rtl, if missing.
   */
  getTextDirection(article) {
    if (article.dir) {
      return article.dir;
    }

    if (["ar", "fa", "he", "ug", "ur"].includes(article.language)) {
      return "rtl";
    }

    return "ltr";
  }

  getTitle(article) {
    return article.title || "";
  }

  escapeHTML(text) {
    return text
      .replace(/\&/g, "&amp;")
      .replace(/\</g, "&lt;")
      .replace(/\>/g, "&gt;")
      .replace(/\"/g, "&quot;")
      .replace(/\'/g, "&#039;");
  }
}

function fetchDocument(url) {
  return new Promise((resolve, reject) => {
    let xhr = new XMLHttpRequest();
    xhr.open("GET", url, true);
    xhr.onerror = evt => reject(evt.error);
    xhr.responseType = "document";
    xhr.onload = _evt => {
      if (xhr.status !== 200) {
        reject("Reader mode XHR failed with status: " + xhr.status);
        return;
      }
      let doc = xhr.responseXML;
      if (!doc) {
        reject("Reader mode XHR didn't return a document");
        return;
      }
      resolve(doc);
    };
    xhr.send();
  });
}

function getPreparedDocument(id, url) {
  return new Promise((resolve, reject) => {
    browser.runtime
      .sendMessage({ action: "getSerializedDoc", id })
      .then(serializedDoc => {
        if (serializedDoc) {
          // eslint-disable-next-line no-undef
          let doc = new JSDOMParser().parse(serializedDoc, url);
          resolve(doc);
        } else {
          reject();
        }
      });
  });
}

let readerView = new ReaderView();
connectNativePort();
prepareBody();

function connectNativePort() {
  let url = new URL(window.location.href);
  let articleUrl = url.searchParams.get("url");
  let id = url.searchParams.get("id");
  let baseUrl = browser.runtime.getURL("/");

  let port = browser.runtime.connectNative("mozacReaderviewActive");
  port.onMessage.addListener(message => {
    switch (message.action) {
      case "show": {
        async function showAsync(options) {
          try {
            let doc;
            if (typeof Promise.any === "function") {
              doc = await Promise.any([
                fetchDocument(articleUrl),
                getPreparedDocument(id, articleUrl),
              ]);
            } else {
              try {
                doc = await getPreparedDocument(id, articleUrl);
              } catch (e) {
                doc = await fetchDocument(articleUrl);
              }
            }
            readerView.show(doc, articleUrl, options);
          } catch (e) {
            // eslint-disable-next-line no-console
            console.log(e);
            // We weren't able to find the prepared document and also
            // failed to fetch it. Let's load the original page which
            // will make sure we show an appropriate error page.
            window.location.href = articleUrl;
          }
        }
        showAsync(message.value);
        break;
      }
      case "hide":
        window.location.href = articleUrl;
        break;
      case "setColorScheme":
        readerView.setColorScheme(message.value.toLowerCase());
        break;
      case "changeFontSize":
        readerView.changeFontSize(message.value);
        break;
      case "setFontType":
        readerView.setFontType(message.value.toLowerCase());
        break;
      case "checkReaderState":
        port.postMessage({
          baseUrl,
          activeUrl: articleUrl,
          readerable: true,
        });
        break;
      default:
        console.error(`Received invalid action ${message.action}`);
    }
  });
}

/**
 * Applies the configured color scheme to the HTML body while reader view is loading. This is to
 * prevent "flashes" caused by having to change the color later.
 */
function prepareBody() {
  let url = new URL(window.location.href);
  let colorScheme = url.searchParams.get("colorScheme");
  let body = document.createElement("body");
  body.classList.add("mozac-readerview-body");
  body.classList.add(colorScheme);
  document.body = body;
}
