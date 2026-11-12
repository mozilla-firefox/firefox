/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global gBrowser, MozXULElement */

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const lazy = XPCOMUtils.declareLazy({
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
});

/**
 * A footer which appears in the corner of the inactive panel in split view.
 *
 * The footer displays the favicon and domain name of the site.
 */
export class MozAddressFooter extends MozXULElement {
  #initialized = false;

  #isInsecure = false;
  /** @type {HTMLSpanElement} */
  securityElement = null;

  /** @type {HTMLImageElement} */
  tabImageIconElement = null;
  #tabImageIconSrc = "";

  /** @type {HTMLSpanElement} */
  uriElement = null;
  /** @type {nsIURI} */
  #uri = null;

  #browserProgressListener = {
    QueryInterface: ChromeUtils.generateQI([
      Ci.nsIWebProgressListener,
      Ci.nsISupportsWeakReference,
    ]),
    onLocationChange: (aWebProgress, aRequest, aLocation) => {
      if (aWebProgress?.isTopLevel && aLocation) {
        this.#updateUri(aLocation);
      }
    },
    onSecurityChange: (aWebProgress, aRequest, aState) =>
      this.#toggleInsecure(
        !!(
          aState & Ci.nsIWebProgressListener.STATE_IS_INSECURE ||
          aState & Ci.nsIWebProgressListener.STATE_IS_BROKEN
        )
      ),
  };

  /** @type {MozTabbrowserTab} */
  #tab = null;

  static markup = `
    <hbox class="address-footer-security-warning" hidden="">
      <html:img class="address-footer-icon address-footer-icon-insecure" role="presentation" src="chrome://global/skin/icons/security-broken.svg" />
      <html:span data-l10n-id="urlbar-trust-icon-notsecure-label"></html:span>
    </hbox>
    <html:img class="address-footer-icon address-footer-tab-image" hidden="" role="presentation"/>
    <html:span class="address-footer-uri"></html:span>
    <toolbarbutton image="chrome://global/skin/icons/more.svg"
                   data-l10n-id="urlbar-split-view-button" />
  `;

  connectedCallback() {
    if (this.#initialized) {
      return;
    }
    this.appendChild(
      /** @type {typeof MozAddressFooter} */ (this.constructor).fragment
    );

    this.securityElement = this.querySelector(
      ".address-footer-security-warning"
    );
    this.tabImageIconElement = this.querySelector(".address-footer-tab-image");
    this.uriElement = this.querySelector(".address-footer-uri");
    this.menuButtonElement = this.querySelector("toolbarbutton");

    // Ensure these elements are up-to-date, as this info may have been set
    // prior to inserting this element into the DOM.
    this.#updateSecurityElement();
    this.#updateTabImageIconElement();
    this.#updateUriElement();

    this.addEventListener("click", this);
    this.menuButtonElement.addEventListener("command", this);

    this.#initialized = true;
  }

  disconnectedCallback() {
    this.#resetTab();
  }

  handleEvent(e) {
    switch (e.type) {
      case "click":
        e.stopPropagation();
        break;
      case "command":
        gBrowser.openSplitViewMenu(this.menuButtonElement);
        break;
      case "TabAttrModified":
        for (const attribute of e.detail.changed) {
          this.#handleTabAttrModified(attribute);
        }
        break;
    }
  }

  #handleTabAttrModified(attribute) {
    switch (attribute) {
      case "image":
        this.#updateTabImageIconSrc(this.#tab.image);
        break;
    }
  }

  /**
   * Update the insecure flag and refresh the security warning visibility.
   *
   * @param {boolean} isInsecure
   */
  #toggleInsecure(isInsecure) {
    this.#isInsecure = isInsecure;
    if (this.securityElement) {
      this.#updateSecurityElement();
    }
    if (this.tabImageIconElement) {
      this.#updateTabImageIconElement();
    }
  }

  #updateSecurityElement() {
    const isWebsite = this.#uri.schemeIs("http") || this.#uri.schemeIs("https");
    this.securityElement.hidden = !isWebsite || !this.#isInsecure;
  }

  /**
   * Update the footer icon to the given source URI.
   *
   * @param {string} iconSrc
   */
  #updateTabImageIconSrc(iconSrc) {
    this.#tabImageIconSrc = iconSrc;
    if (this.tabImageIconElement) {
      this.#updateTabImageIconElement();
    }
  }

  #updateTabImageIconElement() {
    let canShowIcon = !this.#isInsecure && this.#tabImageIconSrc;
    if (canShowIcon) {
      this.tabImageIconElement.setAttribute("src", this.#tabImageIconSrc);
    } else {
      this.tabImageIconElement.removeAttribute("src");
    }
    this.tabImageIconElement.hidden = !canShowIcon;
  }

  /**
   * Update the footer URI display with the formatted domain string.
   *
   * @param {nsIURI} uri
   */
  #updateUri(uri) {
    this.hidden = uri.specIgnoringRef === "about:opentabs";
    this.#uri = uri;
    if (this.uriElement) {
      this.#updateUriElement();
    }
    if (this.securityElement) {
      this.#updateSecurityElement();
    }
  }

  #updateUriElement() {
    const uriString = this.#uri
      ? lazy.BrowserUtils.formatURIForDisplay(this.#uri)
      : "";
    this.uriElement.textContent = uriString;
  }

  /**
   * Link the footer to the provided tab so it stays in sync.
   *
   * @param {MozTabbrowserTab} tab
   */
  setTab(tab) {
    this.#resetTab();

    // Track favicon changes.
    this.#updateTabImageIconSrc(tab.image);
    tab.addEventListener("TabAttrModified", this);

    // Track URI and security changes.
    this.#updateUri(tab.linkedBrowser.currentURI);
    const securityState = tab.linkedBrowser.securityUI.state;
    this.#toggleInsecure(
      !!(
        securityState & Ci.nsIWebProgressListener.STATE_IS_INSECURE ||
        securityState & Ci.nsIWebProgressListener.STATE_IS_BROKEN
      )
    );
    tab.linkedBrowser.addProgressListener(
      this.#browserProgressListener,
      Ci.nsIWebProgress.NOTIFY_LOCATION | Ci.nsIWebProgress.NOTIFY_SECURITY
    );

    this.#tab = tab;
  }

  /**
   * Remove the footer's association with the current tab.
   */
  #resetTab() {
    if (this.#tab) {
      this.#tab.removeEventListener("TabAttrModified", this);
      if (this.#tab.linkedBrowser?.webProgress) {
        this.#tab.linkedBrowser.removeProgressListener(
          this.#browserProgressListener
        );
      }
    }
    this.#tab = null;
  }
}

customElements.define("address-footer", MozAddressFooter);
