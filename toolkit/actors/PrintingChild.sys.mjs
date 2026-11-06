/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

let gPendingPreviewsMap = new Map();

export class PrintingChild extends JSWindowActorChild {
  actorCreated() {
    // When the print preview page is loaded, the actor will change, so update
    // the state/progress listener to the new actor.
    let listener = gPendingPreviewsMap.get(this.browsingContext.id);
    if (listener) {
      listener.actor = this;
    }
    this.contentWindow.addEventListener("scroll", this);
  }

  didDestroy() {
    this._scrollTask?.disarm();
    this.contentWindow?.removeEventListener("scroll", this);
  }

  handleEvent(event) {
    switch (event.type) {
      case "PrintingError": {
        let win = event.target.defaultView;
        let wbp = win.getInterface(Ci.nsIWebBrowserPrint);
        let nsresult = event.detail;
        this.sendAsyncMessage("Printing:Error", {
          isPrinting: wbp.doingPrint,
          nsresult,
        });
        break;
      }

      case "scroll":
        if (!this._scrollTask) {
          this._scrollTask = new lazy.DeferredTask(
            () => this.updateCurrentPage(),
            16,
            16
          );
        }
        this._scrollTask.arm();
        break;
    }
  }

  receiveMessage(message) {
    let data = message.data;
    switch (message.name) {
      case "Printing:Preview:Navigate": {
        this.navigate(data.navType, data.pageNum);
        break;
      }

      case "Printing:Preview:ParseDocument": {
        return this.parseDocument(
          data.URL,
          Services.wm.getOuterWindowWithId(data.windowID)
        );
      }
    }

    return undefined;
  }

  async parseDocument(URL, contentWindow) {
    // The document in 'contentWindow' will be simplified and the resulting nodes
    // will be inserted into this.contentWindow.
    let thisWindow = this.contentWindow;

    // By using ReaderMode primitives, we parse given document and place the
    // resulting JS object into the DOM of current browser.
    let article;
    try {
      article = await lazy.ReaderMode.parseDocument(contentWindow.document);
    } catch (ex) {
      console.error(ex);
    }

    let document = thisWindow.document;
    document.head.innerHTML = "";

    // Set base URI of document. Print preview code will read this value to
    // populate the URL field in print settings so that it doesn't show
    // "about:blank" as its URI.
    let headBaseElement = document.createElement("base");
    headBaseElement.setAttribute("href", URL);
    document.head.appendChild(headBaseElement);

    for (let sheet of [
      "chrome://global/skin/aboutReader.css",
      "chrome://global/content/simplifyMode.css",
    ]) {
      thisWindow.windowUtils.loadSheetUsingURIString(
        sheet,
        Ci.nsIDOMWindowUtils.AUTHOR_SHEET
      );
    }

    document.body.innerHTML = "";

    // Create container div (main element) and append it to body
    let containerElement = document.createElement("div");
    containerElement.setAttribute("class", "container");
    document.body.appendChild(containerElement);

    // Reader Mode might return null if there's a failure when parsing the document.
    // We'll render the error message for the Simplify Page document when that happens.
    if (article) {
      // Set title of document
      document.title = article.title;

      // Create header div and append it to container
      let headerElement = document.createElement("div");
      headerElement.setAttribute("class", "reader-header");
      headerElement.setAttribute("class", "header");
      containerElement.appendChild(headerElement);

      // Jam the article's title and byline into header div
      let titleElement = document.createElement("h1");
      titleElement.setAttribute("class", "reader-title");
      titleElement.textContent = article.title;
      headerElement.appendChild(titleElement);

      let bylineElement = document.createElement("div");
      bylineElement.setAttribute("class", "reader-credits credits");
      bylineElement.textContent = article.byline;
      headerElement.appendChild(bylineElement);

      // Display header element
      headerElement.style.display = "block";

      // Create content div and append it to container
      let contentElement = document.createElement("div");
      contentElement.setAttribute("class", "content");
      containerElement.appendChild(contentElement);

      // Jam the article's content into content div
      let readerContent = document.createElement("div");
      readerContent.setAttribute("class", "moz-reader-content");
      contentElement.appendChild(readerContent);

      let articleUri = Services.io.newURI(article.url);
      let parserUtils = Cc["@mozilla.org/parserutils;1"].getService(
        Ci.nsIParserUtils
      );
      let contentFragment = parserUtils.parseFragment(
        article.content,
        Ci.nsIParserUtils.SanitizerDropForms |
          Ci.nsIParserUtils.SanitizerAllowStyle,
        false,
        articleUri,
        readerContent
      );

      readerContent.appendChild(contentFragment);

      // Display reader content element
      readerContent.style.display = "block";
    } else {
      const l10n = new Localization(["toolkit/about/aboutReader.ftl"], true);
      const errorMessage = l10n.formatValueSync("about-reader-load-error");

      document.title = errorMessage;

      // Create reader message div and append it to body
      let readerMessageElement = document.createElement("div");
      readerMessageElement.setAttribute("class", "reader-message");
      readerMessageElement.textContent = errorMessage;
      containerElement.appendChild(readerMessageElement);

      // Display reader message element
      readerMessageElement.style.display = "block";
    }

    // The timer is needed for when display list invalidation doesn't invalidate.
    if (thisWindow.windowUtils.isMozAfterPaintPending) {
      await new Promise(resolve => {
        let done = () => {
          thisWindow.removeEventListener("MozAfterPaint", done);
          resolve();
        };
        thisWindow.addEventListener("MozAfterPaint", done);
        lazy.setTimeout(done, 100);
      });
    }
  }

  updateCurrentPage() {
    let cv = this.docShell.docViewer;
    cv.QueryInterface(Ci.nsIWebBrowserPrint);
    this.sendAsyncMessage("Printing:Preview:CurrentPage", {
      currentPage: cv.printPreviewCurrentPageNumber,
    });
  }

  navigate(navType, pageNum) {
    let cv = this.docShell.docViewer;
    cv.QueryInterface(Ci.nsIWebBrowserPrint);
    cv.printPreviewScrollToPage(navType, pageNum);
  }
}
