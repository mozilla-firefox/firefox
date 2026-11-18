/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
  Readerable: "resource://gre/modules/Readerable.sys.mjs",
  isProbablyReaderable: "resource://gre/modules/Readerable.sys.mjs",
});

/**
 * Represents a child actor for handling link previews in the browser.
 * Interacts with content windows and handles events related to link previews.
 *
 * @class LinkPreviewChild
 * @augments {JSWindowActorChild}
 */
export class LinkPreviewChild extends JSWindowActorChild {
  /**
   * Handles incoming messages from the parent actor.
   *
   * @param {object} message - The message object containing name and data.
   * @param {string} message.name - The name of the message.
   * @param {object} message.data - Data associated with the message.
   * @returns {Promise<object>|undefined} The result of fetchPageData if applicable.
   */
  async receiveMessage({ name, data }) {
    if (name === "LinkPreview:FetchPageData") {
      return this.fetchPageData(data.url);
    }
    //expected a return value.  consistent-return (eslint)
    return undefined;
  }

  /**
   * Fetches the HTML content from the given URL.
   *
   * @param {string} url - The URL to fetch.
   * @returns {Promise<string>} The HTML content as a string.
   * @throws {Error} If the fetch fails or the content type is invalid.
   */
  fetchHTML(url) {
    const uri = lazy.NetUtil.newURI(url);
    if (!uri.schemeIs("https")) {
      throw Components.Exception(
        "Only handling https",
        Cr.NS_ERROR_UNKNOWN_PROTOCOL
      );
    }

    // Make requests with a channel to automatically get safe browsing checks.
    // Use null principals in combination with anonymous for now ahead of
    // fetching content with cookies to handle sites requiring login.
    const principal = Services.scriptSecurityManager.createNullPrincipal(
      this.contentWindow.gBrowser.selectedBrowser.browsingContext
        .originAttributes
    );
    const channel = lazy.NetUtil.newChannel({
      contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
      loadingPrincipal: principal,
      securityFlags: Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT,
      triggeringPrincipal: principal,
      uri,
    }).QueryInterface(Ci.nsIHttpChannel);
    channel.loadFlags = Ci.nsIRequest.LOAD_ANONYMOUS;

    // Specially identify this request, e.g., for publishers to opt out
    channel.setRequestHeader("x-firefox-ai", "1", false);

    const { promise, resolve, reject } = Promise.withResolvers();
    const MAX_CONTENT_LENGTH = 5 * 1024 * 1024; // 5 MB limit

    let charset = null;
    const byteChunks = [];
    let totalLength = 0;
    channel.asyncOpen({
      onDataAvailable: (request, stream, offset, count) => {
        totalLength += count;
        if (totalLength > MAX_CONTENT_LENGTH) {
          request.cancel(Cr.NS_ERROR_FILE_TOO_BIG);
        } else {
          byteChunks.push(lazy.NetUtil.readInputStream(stream, count));
        }
      },
      onStartRequest: request => {
        const http = request.QueryInterface(Ci.nsIHttpChannel);

        // Enforce text/html if provided by server
        let contentType = "";
        try {
          contentType = http.getResponseHeader("content-type");
        } catch (ex) {}
        if (contentType && !contentType.startsWith("text/html")) {
          request.cancel(Cr.NS_ERROR_FILE_UNKNOWN_TYPE);
        }

        // Save charset without quotes or spaces for TextDecoder
        const match = contentType.match(/charset=["' ]*([^;"' ]+)/i);
        if (match) {
          charset = match[1];
        }

        // Enforce max length if provided by server
        try {
          if (http.getResponseHeader("content-length") > MAX_CONTENT_LENGTH) {
            request.cancel(Cr.NS_ERROR_FILE_TOO_BIG);
          }
        } catch (ex) {}
      },
      onStopRequest: (_request, status) => {
        if (Components.isSuccessCode(status)) {
          const bytes = new Uint8Array(totalLength);
          let offset = 0;
          for (const chunk of byteChunks) {
            bytes.set(new Uint8Array(chunk), offset);
            offset += chunk.byteLength;
          }

          const effectiveCharset = this.sniffCharset(bytes, charset);
          let decoded;
          try {
            // Use a non-fatal decode to be more robust to minor encoding errors.
            decoded = new TextDecoder(effectiveCharset).decode(bytes);
          } catch (e) {
            // Fallback to UTF-8 on decode errors or if the label was unsupported.
            decoded = new TextDecoder("utf-8").decode(bytes);
          }
          resolve(decoded);
        } else {
          reject(Components.Exception("Failed to fetch HTML", status));
        }
      },
    });
    return promise;
  }

  /**
   * Sniff an effective charset for the given response bytes using the HTML standard's precedence:
   *   1) Byte Order Mark (BOM)
   *   2) <meta charset> or http-equiv in the first 8KB of the document
   *   3) HTTP Content-Type header charset (if provided and valid)
   *   4) Default to utf-8
   *
   * @param {Uint8Array} bytes - The raw response bytes.
   * @param {string} headerCharset - The charset from the Content-Type header.
   * @returns {string} A validated, effective charset label for TextDecoder.
   */
  sniffCharset(bytes, headerCharset = "") {
    // 1. BOM detection (highest priority)
    if (
      bytes.length >= 3 &&
      bytes[0] === 0xef &&
      bytes[1] === 0xbb &&
      bytes[2] === 0xbf
    ) {
      return "utf-8";
    }
    if (bytes.length >= 2) {
      if (bytes[0] === 0xfe && bytes[1] === 0xff) {
        return "utf-16be";
      }
      if (bytes[0] === 0xff && bytes[1] === 0xfe) {
        return "utf-16le";
      }
    }

    // 2. Scan the first 8KB for a meta-declared charset. This is checked before
    // the HTTP header as a heuristic for misconfigured servers where the HTML
    // is more likely to be correct.
    try {
      const headLen = Math.min(bytes.length, 8192);
      const head = new TextDecoder("windows-1252").decode(
        bytes.subarray(0, headLen)
      );

      const metaCharsetRegex = /<meta\s+charset\s*=\s*["']?([a-z0-9_-]+)/i;
      let match = head.match(metaCharsetRegex);

      if (!match) {
        const httpEquivRegex =
          /<meta\s+http-equiv\s*=\s*["']?content-type["']?[^>]*content\s*=\s*["'][^"']*charset\s*=\s*([a-z0-9_-]+)/i;
        match = head.match(httpEquivRegex);
      }

      if (match && match[1]) {
        const norm = this.normalizeAndValidateEncodingLabel(match[1]);
        if (norm) {
          return norm;
        }
      }
    } catch (e) {
      // Ignore errors during meta scan and fall through.
    }

    // 3. Use charset from HTTP header if it's valid.
    if (headerCharset) {
      const norm = this.normalizeAndValidateEncodingLabel(headerCharset);
      if (norm) {
        return norm;
      }
    }

    // 4. Default to UTF-8 if no other charset is found.
    return "utf-8";
  }

  /**
   * Normalizes a charset label and validates it is supported by TextDecoder.
   *
   * @param {string} label - The raw encoding label from headers or meta tags.
   * @returns {string|null} The normalized, validated label, or null if invalid.
   */
  normalizeAndValidateEncodingLabel(label) {
    const l = (label || "").trim();
    if (!l) {
      return null;
    }
    try {
      // TextDecoder constructor handles aliases and validation.
      return new TextDecoder(l).encoding;
    } catch (e) {
      // The label was invalid or unsupported.
    }
    return null;
  }

  /**
   * Fetches HTML content from a URL and parses its meta tags and page text.
   *
   * @param {string} url - The URL to fetch and parse.
   * @returns {Promise<object>} An object containing meta information, page text, and HTML code.
   */
  async fetchPageData(url) {
    const ret = {
      article: {},
      rawMetaInfo: {},
      url,
    };
    try {
      const htmlCode = await this.fetchHTML(url);
      ret.urlComponents = this.extractUrlComponents(url);

      const parser = new DOMParser();
      const doc = parser.parseFromString(htmlCode, "text/html");
      ret.rawMetaInfo = this.parseMetaTagsFromDoc(doc);

      if (
        !lazy.Readerable.shouldCheckUri(lazy.NetUtil.newURI(url)) ||
        !lazy.isProbablyReaderable(doc)
      ) {
        // Add normalized metadata even if the document isn't reader-able
        ret.meta = this.extractNormalizedMetadata(ret.rawMetaInfo);
        return ret;
      }

      ret.article = await this.getArticleDataFromDoc(doc);

      ret.meta = this.extractNormalizedMetadata(ret.rawMetaInfo, ret.article);
    } catch (error) {
      console.error(`Failed to fetch and parse page data: ${error}`);
      ret.error = { message: error.message, result: error.result };
      // Add empty normalized metadata in case of error
      ret.meta = this.extractNormalizedMetadata();
    }
    return ret;
  }

  /**
   * Extracts and normalizes metadata from the page's meta tags and article content.
   *
   * @param {object} metaData - Metadata extracted from the page's meta tags (Open Graph, Twitter, HTML)
   * @param {object} articleData - Data extracted from the article content using ReaderMode
   * @returns {object} Normalized metadata containing:
   *   - title: Page title prioritizing Open Graph, Twitter, then HTML title
   *   - description: Content excerpt or meta description from various sources
   *   - imageUrl: HTTPS-only URL of the page's primary image
   *   - isMissingMetadata: Boolean flag indicating if description is missing
   */
  extractNormalizedMetadata(metaData = {}, articleData = {}) {
    const title =
      metaData["og:title"] ||
      metaData["twitter:title"] ||
      metaData["html:title"] ||
      "";

    const description =
      articleData.excerpt ||
      metaData["og:description"] ||
      metaData["twitter:description"] ||
      metaData.description ||
      "";

    let imageUrl = metaData["og:image"] || metaData["twitter:image:src"] || "";

    if (!imageUrl.startsWith("https://")) {
      imageUrl = "";
    }

    return {
      title,
      description,
      imageUrl,
    };
  }

  /**
   * Extracts URL components including domain and filename.
   *
   * @param {string} url - The URL to extract information from.
   * @returns {object} Object containing domain and filename.
   */
  extractUrlComponents(url) {
    try {
      const urlObj = new URL(url);
      const domain = urlObj.hostname;

      // Extract the filename (last part of pathname)
      let pathname = urlObj.pathname;
      // Remove trailing slash if present
      if (pathname.endsWith("/")) {
        pathname = pathname.slice(0, -1);
      }

      // Get last segment of path
      const pathParts = pathname.split("/");
      const filename = pathParts[pathParts.length - 1] || domain;

      return { domain, filename };
    } catch (e) {
      // Return both properties with same fallback value if URL is invalid
      return { domain: url, filename: url };
    }
  }

  /**
   * Parses meta tags from the provided Document into a key-value object.
   * Also extracts the title if available.
   *
   * @param {Document} doc - The parsed HTML document.
   * @returns {object} An object containing meta tag key-value pairs.
   */
  parseMetaTagsFromDoc(doc) {
    const metaTags = doc.querySelectorAll("meta");
    const metaInfo = {};

    // TODO: Define the meta tags we are interested in
    const desiredMetaNames = [
      "description",
      "og:image",
      "title",
      "og:title",
      "twitter:title",
      "og:description",
      "twitter:description",
      "twitter:image:src",
    ];

    metaTags.forEach(tag => {
      const rawName = tag.getAttribute("name") || tag.getAttribute("property");
      const content = tag.getAttribute("content");
      const key = rawName ? rawName.toLowerCase() : null;
      if (key && content) {
        if (desiredMetaNames.includes(key)) {
          metaInfo[key] = content;
        }
      }
    });

    const title = doc.querySelector("title")?.textContent;
    if (title) {
      metaInfo["html:title"] = title;
    }

    return metaInfo;
  }

  /**
   * Extracts article data from the provided Document using ReaderMode.
   *
   * @param {Document} doc - The parsed HTML document.
   * @returns {Promise<object>} The extracted article data including specified fields.
   */
  async getArticleDataFromDoc(doc) {
    try {
      const article = await lazy.ReaderMode.parseDocument(doc);
      if (article) {
        const {
          title,
          byline,
          content,
          detectedLanguage,
          length,
          siteName,
          excerpt,
          readingTimeMinsSlow,
          readingTimeMinsFast,
        } = article;

        // parseDocument return a `textContent` that strips structure and newlines, which we need for the model.
        // So we convert the HTML `content` to plain text directly, preserving formatting and newlines.
        const textContent = Cc["@mozilla.org/parserutils;1"]
          .getService(Ci.nsIParserUtils)
          .convertToPlainText(
            content,
            null,
            0 // No line-wrapping
          );

        return {
          title,
          byline,
          textContent,
          detectedLanguage,
          length,
          siteName,
          excerpt,
          readingTimeMinsFast,
          readingTimeMinsSlow,
        };
      }
    } catch (error) {
      console.error("Error parsing document with ReaderMode:", error);
    }

    return {};
  }
}
