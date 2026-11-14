/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "autoscrollEnabled",
  "general.autoScroll",
  true
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "blockJavascript",
  "browser.link.alternative_click.block_javascript",
  true
);

export class MiddleMousePasteHandlerChild extends JSWindowActorChild {
  handleEvent(clickEvent) {
    if (
      clickEvent.defaultPrevented ||
      clickEvent.button != 1 ||
      lazy.autoscrollEnabled
    ) {
      return;
    }
    this.manager
      .getActor("ClickHandler")
      .handleClickEvent(
        clickEvent,
        /* is from middle mouse paste handler */ true
      );
  }

  onProcessedClick(data) {
    this.sendAsyncMessage("MiddleClickPaste", data);
  }
}

export class ClickHandlerChild extends JSWindowActorChild {
  handleEvent(wrapperEvent) {
    this.handleClickEvent(wrapperEvent.sourceEvent);
  }

  handleClickEvent(event, isFromMiddleMousePasteHandler = false) {
    if (event.defaultPrevented || event.button >= 2) {
      return;
    }
    // Don't do anything on editable things, we shouldn't open links in
    // contenteditables, and editor needs to possibly handle middlemouse paste
    let composedTarget = event.composedTarget;
    if (
      composedTarget.isContentEditable ||
      (composedTarget.ownerDocument &&
        composedTarget.ownerDocument.designMode == "on") ||
      ChromeUtils.getClassName(composedTarget) == "HTMLInputElement" ||
      ChromeUtils.getClassName(composedTarget) == "HTMLTextAreaElement"
    ) {
      return;
    }

    let originalTarget = event.originalTarget;
    let ownerDoc = originalTarget.ownerDocument;
    if (!ownerDoc) {
      return;
    }

    // Handle click events from about pages
    if (event.button == 0) {
      if (ownerDoc.documentURI.startsWith("about:blocked")) {
        return;
      }
    }

    // For untrusted events, require a valid transient user gesture activation.
    if (!event.isTrusted && !ownerDoc.hasValidTransientUserGestureActivation) {
      return;
    }

    let [href, node, principal] =
      lazy.BrowserUtils.hrefAndLinkNodeForClickEvent(event);

    let policyContainer = ownerDoc.policyContainer;
    if (policyContainer) {
      policyContainer =
        lazy.E10SUtils.serializePolicyContainer(policyContainer);
    }

    let referrerInfo = Cc["@mozilla.org/referrer-info;1"].createInstance(
      Ci.nsIReferrerInfo
    );
    if (node) {
      referrerInfo.initWithElement(node);
    } else {
      referrerInfo.initWithDocument(ownerDoc);
    }
    referrerInfo = lazy.E10SUtils.serializeReferrerInfo(referrerInfo);

    let json = {
      button: event.button,
      shiftKey: event.shiftKey,
      ctrlKey: event.ctrlKey,
      metaKey: event.metaKey,
      altKey: event.altKey,
      href: null,
      title: null,
      policyContainer,
      referrerInfo,
    };

    if (href && !isFromMiddleMousePasteHandler) {
      if (
        lazy.blockJavascript &&
        Services.io.extractScheme(href) == "javascript"
      ) {
        // We don't want to open new tabs or windows for javascript: links.
        return;
      }

      try {
        Services.scriptSecurityManager.checkLoadURIStrWithPrincipal(
          principal,
          href
        );
      } catch (e) {
        return;
      }

      if (
        !event.isTrusted &&
        lazy.BrowserUtils.whereToOpenLink(event) != "current"
      ) {
        // If we'll open the link, we want to consume the user gesture
        // activation to ensure that we don't allow multiple links to open
        // from one user gesture.
        // Avoid doing so for links opened in the current tab, which get
        // handled later, by gecko, as otherwise its popup blocker will stop
        // the link from opening.
        // We will do the same check (whereToOpenLink) again in the parent and
        // avoid handling the click for such links... but we still need the
        // click information in the parent because otherwise places link
        // tracking breaks. (bug 1742894 tracks improving this.)
        ownerDoc.consumeTransientUserGestureActivation();
        // We don't care about the return value because we already checked that
        // hasValidTransientUserGestureActivation was true earlier in this
        // function.
      }

      json.href = href;
      if (node) {
        json.title = node.getAttribute("title");
      }

      if (
        (ownerDoc.URL === "about:newtab" || ownerDoc.URL === "about:home") &&
        node.dataset.isSponsoredLink === "true"
      ) {
        json.globalHistoryOptions = {
          triggeringSource: "newtab",
          triggeringSponsoredURL: href,
        };
      }

      // If a link element is clicked with middle button, user wants to open
      // the link somewhere rather than pasting clipboard content.  Therefore,
      // when it's clicked with middle button, we should prevent multiple
      // actions here to avoid leaking clipboard content unexpectedly.
      // Note that whether the link will work actually or not does not matter
      // because in this case, user does not intent to paste clipboard content.
      // We also need to do this to prevent multiple tabs opening if there are
      // nested link elements.
      event.preventMultipleActions();

      this.sendAsyncMessage("Content:Click", json);
    }

    // This might be middle mouse navigation, in which case pass this back:
    if (!href && event.button == 1 && isFromMiddleMousePasteHandler) {
      this.manager.getActor("MiddleMousePasteHandler").onProcessedClick(json);
    }
  }
}
