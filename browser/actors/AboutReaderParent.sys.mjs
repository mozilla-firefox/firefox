// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PageActions: "resource:///modules/PageActions.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
});

// A set of all of the AboutReaderParent actors that exist.
// See bug 1631146 for a request for a less manual way of doing this.
let gAllActors = new Set();

// A map of message names to listeners that listen to messages
// received by the AboutReaderParent actors.
let gListeners = new Map();

// As a reader mode document could be loaded in a different process than
// the source article, temporarily cache the article data here in the
// parent while switching to it.
let gCachedArticles = new Map();

export class AboutReaderParent extends JSWindowActorParent {
  didDestroy() {
    gAllActors.delete(this);

    if (this.isReaderMode()) {
      let url = this.manager.documentURI.spec;
      url = decodeURIComponent(url.substr("about:reader?url=".length));
      gCachedArticles.delete(url);
    }
  }

  isReaderMode() {
    return this.manager.documentURI.spec.startsWith("about:reader");
  }

  static addMessageListener(name, listener) {
    if (!gListeners.has(name)) {
      gListeners.set(name, new Set([listener]));
    } else {
      gListeners.get(name).add(listener);
    }
  }

  static removeMessageListener(name, listener) {
    if (!gListeners.has(name)) {
      return;
    }

    gListeners.get(name).delete(listener);
  }

  static broadcastAsyncMessage(name, data) {
    for (let actor of gAllActors) {
      // Ignore errors for actors that might not be valid yet or anymore.
      try {
        actor.sendAsyncMessage(name, data);
      } catch (ex) {}
    }
  }

  callListeners(message) {
    let listeners = gListeners.get(message.name);
    if (!listeners) {
      return;
    }

    message.target = this.browsingContext.embedderElement;
    for (let listener of listeners.values()) {
      try {
        listener.receiveMessage(message);
      } catch (e) {
        console.error(e);
      }
    }
  }

  async receiveMessage(message) {
    switch (message.name) {
      case "Reader:EnterReaderMode": {
        gCachedArticles.set(message.data.url, message.data);
        this.enterReaderMode(message.data.url);
        break;
      }
      case "Reader:LeaveReaderMode": {
        this.leaveReaderMode();
        break;
      }
      case "Reader:GetCachedArticle": {
        let cachedArticle = gCachedArticles.get(message.data.url);
        gCachedArticles.delete(message.data.url);
        return cachedArticle;
      }
      case "Reader:FaviconRequest": {
        try {
          let preferredWidth = message.data.preferredWidth || 0;
          let uri = Services.io.newURI(message.data.url);

          let result = await lazy.PlacesUtils.favicons.getFaviconForPage(
            uri,
            preferredWidth
          );

          this.callListeners(message);
          return result && { url: uri.spec, faviconUrl: result.uri.spec };
        } catch (ex) {
          console.error(
            "Error requesting favicon URL for about:reader content: ",
            ex
          );
        }

        break;
      }

      case "Reader:UpdateReaderButton": {
        let browser = this.browsingContext.embedderElement;
        if (!browser) {
          return undefined;
        }

        if (message.data && message.data.isArticle !== undefined) {
          browser.isArticle = message.data.isArticle;
        }
        this.updateReaderButton(browser);
        this.callListeners(message);
        break;
      }

      case "RedirectTo": {
        gCachedArticles.set(message.data.newURL, message.data.article);
        // This is setup as a query so we can navigate the page after we've
        // cached the relevant info in the parent.
        return true;
      }

      default:
        this.callListeners(message);
        break;
    }

    return undefined;
  }

  static updateReaderButton(browser) {
    let windowGlobal = browser.browsingContext.currentWindowGlobal;
    let actor = windowGlobal.getActor("AboutReader");
    actor.updateReaderButton(browser);
  }

  updateReaderButton(browser) {
    let tabBrowser = browser.getTabBrowser();
    if (!tabBrowser || browser != tabBrowser.selectedBrowser) {
      return;
    }

    let doc = browser.ownerGlobal.document;
    let button = doc.getElementById("reader-mode-button");
    let menuitem = doc.getElementById("menu_readerModeItem");
    let key = doc.getElementById("key_toggleReaderMode");
    if (this.isReaderMode()) {
      gAllActors.add(this);

      button.setAttribute("readeractive", true);
      button.hidden = false;
      doc.l10n.setAttributes(button, "reader-view-close-button");

      menuitem.hidden = false;
      doc.l10n.setAttributes(menuitem, "menu-view-close-readerview");

      key.setAttribute("disabled", false);

      Services.obs.notifyObservers(null, "reader-mode-available");
    } else {
      button.removeAttribute("readeractive");
      button.hidden = !browser.isArticle;
      doc.l10n.setAttributes(button, "reader-view-enter-button");

      menuitem.hidden = !browser.isArticle;
      doc.l10n.setAttributes(menuitem, "menu-view-enter-readerview");

      key.setAttribute("disabled", !browser.isArticle);

      if (browser.isArticle) {
        Services.obs.notifyObservers(null, "reader-mode-available");
      }
    }

    if (!button.hidden) {
      lazy.PageActions.sendPlacedInUrlbarTrigger(button);
    }
  }

  static forceShowReaderIcon(browser) {
    browser.isArticle = true;
    AboutReaderParent.updateReaderButton(browser);
  }

  static toggleReaderMode(event) {
    let win = event.target.ownerGlobal;
    if (win.gBrowser) {
      let browser = win.gBrowser.selectedBrowser;

      let windowGlobal = browser.browsingContext.currentWindowGlobal;
      let actor = windowGlobal.getActor("AboutReader");
      if (actor) {
        if (actor.isReaderMode()) {
          gAllActors.delete(this);
        }
        actor.sendAsyncMessage("Reader:ToggleReaderMode", {});
      }
    }
  }

  hasReaderModeEntryAtOffset(url, offset) {
    if (Services.appinfo.sessionHistoryInParent) {
      let browsingContext = this.browsingContext;
      if (browsingContext.childSessionHistory.canGo(offset)) {
        let shistory = browsingContext.sessionHistory;
        let nextEntry = shistory.getEntryAtIndex(shistory.index + offset);
        let nextURL = nextEntry.URI.spec;
        return nextURL && (nextURL == url || !url);
      }
    }

    return false;
  }

  enterReaderMode(url) {
    let readerURL = "about:reader?url=" + encodeURIComponent(url);
    if (this.hasReaderModeEntryAtOffset(readerURL, +1)) {
      let browsingContext = this.browsingContext;
      browsingContext.childSessionHistory.go(+1);
      return;
    }

    this.sendAsyncMessage("Reader:EnterReaderMode", {});
  }

  leaveReaderMode() {
    let browsingContext = this.browsingContext;
    let url = browsingContext.currentWindowGlobal.documentURI.spec;
    let originalURL = lazy.ReaderMode.getOriginalUrl(url);
    if (this.hasReaderModeEntryAtOffset(originalURL, -1)) {
      browsingContext.childSessionHistory.go(-1);
      return;
    }

    this.sendAsyncMessage("Reader:LeaveReaderMode", {});
  }

  /**
   * Gets an article for a given URL. This method will download and parse a document.
   *
   * @param url The article URL.
   * @param browser The browser where the article is currently loaded.
   * @return {Promise}
   * @resolves JS object representing the article, or null if no article is found.
   */
  async _getArticle(url) {
    return lazy.ReaderMode.downloadAndParseDocument(url).catch(e => {
      if (e && e.newURL) {
        // Pass up the error so we can navigate the browser in question to the new URL:
        throw e;
      }
      console.error("Error downloading and parsing document: ", e);
      return null;
    });
  }
}
