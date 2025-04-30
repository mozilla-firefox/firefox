/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
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
    const principal = Services.scriptSecurityManager.createNullPrincipal({});
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

    let charset = "utf-8";
    const byteChunks = [];
    let totalLength = 0;
    channel.asyncOpen({
      onDataAvailable(request, stream, offset, count) {
        totalLength += count;
        if (totalLength > MAX_CONTENT_LENGTH) {
          request.cancel(Cr.NS_ERROR_FILE_TOO_BIG);
        } else {
          byteChunks.push(lazy.NetUtil.readInputStream(stream, count));
        }
      },
      onStartRequest(request) {
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
      onStopRequest(_request, status) {
        if (Components.isSuccessCode(status)) {
          const bytes = new Uint8Array(totalLength);
          let offset = 0;
          for (const chunk of byteChunks) {
            bytes.set(new Uint8Array(chunk), offset);
            offset += chunk.byteLength;
          }

          const decoder = new TextDecoder(charset);
          resolve(decoder.decode(bytes));
        } else {
          reject(Components.Exception("Failed to fetch HTML", status));
        }
      },
    });
    return promise;
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

      if (!this.isProbablyReaderable(doc)) {
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
      const name = tag.getAttribute("name") || tag.getAttribute("property");
      const content = tag.getAttribute("content");
      if (name && content) {
        if (desiredMetaNames.includes(name.toLowerCase())) {
          metaInfo[name] = content;
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
            Ci.nsIDocumentEncoder.OutputSelectionOnly | // Use only selected reader-view fragment
              Ci.nsIDocumentEncoder.OutputAbsoluteLinks |
              Ci.nsIDocumentEncoder.OutputFormatted, // Pretty-print formatting
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

  /**
   * Decides whether or not the document is reader-able without parsing the whole thing.
   *
   * @param {Document} doc - The document to check for readability
   * @param {object} [options={}] Configuration object.
   * @param {number} [options.minContentLength=140] The minimum node content length used to decide if the document is readerable.
   * @param {number} [options.minScore=20] The minumum cumulated 'score' used to determine if the document is readerable.
   * @param {Function} [options.visibilityChecker=isNodeVisible] The function used to determine if a node is visible.
   * @returns {boolean} Whether or not we suspect Readability.parse() will suceeed at returning an article object.
   */
  isProbablyReaderable(doc, options = {}) {
    // For backward compatibility reasons 'options' can either be a configuration object or the function used
    // to determine if a node is visible.
    if (typeof options == "function") {
      options = { visibilityChecker: options };
    }

    var defaultOptions = {
      minScore: 20,
      minContentLength: 140,
      visibilityChecker: this.isNodeVisible,
    };
    options = Object.assign(defaultOptions, options);

    var nodes = doc.querySelectorAll("p, pre, article");

    // Get <div> nodes which have <br> node(s) and append them into the `nodes` variable.
    // Some articles' DOM structures might look like
    // <div>
    //   Sentences<br>
    //   <br>
    //   Sentences<br>
    // </div>
    var brNodes = doc.querySelectorAll("div > br");
    if (brNodes.length) {
      var set = new Set(nodes);
      [].forEach.call(brNodes, function (node) {
        set.add(node.parentNode);
      });
      nodes = Array.from(set);
    }

    var score = 0;
    // This is a little cheeky, we use the accumulator 'score' to decide what to return from
    // this callback:
    return [].some.call(nodes, function (node) {
      if (!options.visibilityChecker(node)) {
        return false;
      }

      var REGEXPS = {
        // NOTE: These two regular expressions are duplicated in
        // Readability.js. Please keep both copies in sync.
        unlikelyCandidates:
          /-ad-|ai2html|banner|breadcrumbs|combx|comment|community|cover-wrap|disqus|extra|footer|gdpr|header|legends|menu|related|remark|replies|rss|shoutbox|sidebar|skyscraper|social|sponsor|supplemental|ad-break|agegate|pagination|pager|popup|yom-remote/i,
        okMaybeItsACandidate: /and|article|body|column|content|main|shadow/i,
      };
      var matchString = node.className + " " + node.id;
      if (
        REGEXPS.unlikelyCandidates.test(matchString) &&
        !REGEXPS.okMaybeItsACandidate.test(matchString)
      ) {
        return false;
      }

      if (node.matches("li p")) {
        return false;
      }

      var textContentLength = node.textContent.trim().length;
      if (textContentLength < options.minContentLength) {
        return false;
      }

      score += Math.sqrt(textContentLength - options.minContentLength);

      if (score > options.minScore) {
        return true;
      }
      return false;
    });
  }
  /**
   * Determines whether a node is visible in the document.
   *
   * @param {Node} node - The DOM node to check for visibility
   * @returns {boolean} True if the node is considered visible, false otherwise
   *
   * This method checks several visibility attributes:
   * - Verifies the node's display style is not 'none'
   * - Checks that the node doesn't have a 'hidden' attribute
   * - Ensures the aria-hidden attribute is not 'true' (with an exception for fallback images)
   */
  isNodeVisible(node) {
    // Have to null-check node.style and node.className.includes to deal with SVG and MathML nodes.
    return (
      (!node.style || node.style.display != "none") &&
      !node.hasAttribute("hidden") &&
      //check for "fallback-image" so that wikimedia math images are displayed
      (!node.hasAttribute("aria-hidden") ||
        node.getAttribute("aria-hidden") != "true" ||
        (node.className &&
          node.className.includes &&
          node.className.includes("fallback-image")))
    );
  }
}
