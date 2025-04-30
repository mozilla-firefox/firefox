/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "localization", () => {
  return new Localization(["preview/credentialChooser.ftl"], true);
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "IDNService",
  "@mozilla.org/network/idn-service;1",
  "nsIIDNService"
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "TESTING_MODE",
  "dom.security.credentialmanagement.chooser.testing.enabled",
  false
);

/**
 * Set an image element's src attribute to a data: url of the favicon for a
 * given origin, defaulting to the browser default favicon.
 *
 * @param {HTMLImageElement} icon The image Element that should have source be the icon result.
 * @param {string} origin The origin whose favicon should be used.
 */
async function setIconToFavicon(icon, origin) {
  try {
    let iconData = await lazy.PlacesUtils.favicons.getFaviconForPage(
      lazy.PlacesUtils.toURI(origin)
    );
    icon.src = iconData.uri.spec;
  } catch {
    icon.src = "chrome://global/skin/icons/defaultFavicon.svg";
  }
}

/**
 * Class implementing the nsICredentialChooserService.
 *
 * This class shows UI to the user for the Credential Chooser for the
 * Credential Management API.
 *
 * @class CredentialChooserService
 */
export class CredentialChooserService {
  classID = Components.ID("{673ddc19-03e2-4b30-a868-06297e8fed89}");
  QueryInterface = ChromeUtils.generateQI(["nsICredentialChooserService"]);

  /**
   * @typedef {object} CredentialArgument
   * @property {string} id - The unique identifier for the credential.
   * @property {string} type - The type of the credential.
   * @property {string} [origin] - The origin associated to the credential.
   * @property {UIHints} [uiHints] - UI hints for the credential.
   */

  /**
   * @typedef {object} UIHints
   * @property {string} [name] - The display name for the credential.
   * @property {string} [iconURL] - The data URL of the icon for the credential.
   * @property {number} [expiresAfter] - The expiration time for the UI hint.
   */

  /**
   * This function displays the credential chooser UI, allowing the user to make an identity choice.
   * Once the user makes a choice from the credentials provided, or dismisses the prompt, we will
   * call the callback with that credential, or null in the case of a dismiss.
   *
   * We also support UI-less testing via choices provided by picking any credential with ID 'wpt-pick-me'
   * if the preference 'dom.security.credentialmanagement.chooser.testing.enabled' is true.
   *
   * @param {BrowsingContext} browsingContext The browsing context of the window calling the Credential Management API.
   * @param {Array<CredentialArgument>} credentials The credentials the user should choose from.
   * @param {nsICredentialChosenCallback} callback A callback to return the user's credential choice to.
   * @returns {nsresult}
   */
  async showCredentialChooser(browsingContext, credentials, callback) {
    if (!callback) {
      callback = { notify: () => {} };
    }
    if (!credentials.length) {
      return Cr.NS_ERROR_INVALID_ARG;
    }

    // If we are not an active BC, return no choice and bail out.
    if (!browsingContext?.currentWindowContext?.isActiveInTab) {
      callback.notify(null);
      return Cr.NS_OK;
    }

    if (lazy.TESTING_MODE) {
      let match = credentials.find(cred => cred.id == "wpt-pick-me");
      if (match) {
        if (browsingContext.currentWindowGlobal?.documentPrincipal) {
          Services.perms.addFromPrincipal(
            browsingContext.currentWindowGlobal.documentPrincipal,
            "credential-allow-silent-access^" + match.origin,
            Ci.nsIPermissionManager.ALLOW_ACTION,
            Ci.nsIPermissionManager.EXPIRE_SESSION
          );
          Services.perms.addFromPrincipal(
            browsingContext.currentWindowGlobal.documentPrincipal,
            "credential-allow-silent-access",
            Ci.nsIPermissionManager.ALLOW_ACTION,
            Ci.nsIPermissionManager.EXPIRE_SESSION
          );
        }
        callback.notify("wpt-pick-me");
      } else {
        callback.notify(null);
      }
      return Cr.NS_OK;
    }

    let browser = browsingContext.topFrameElement;
    if (browser?.tagName != "browser") {
      return Cr.NS_ERROR_INVALID_ARG;
    }

    let headerTextElement = browser.ownerDocument.getElementById(
      "credential-chooser-header-text"
    );
    let host = browser.ownerGlobal.gIdentityHandler.getHostForDisplay();
    browser.ownerDocument.l10n.setAttributes(
      headerTextElement,
      "credential-chooser-header",
      {
        host,
      }
    );

    let faviconPromises = [];

    let localizationPromise = lazy.localization.formatMessages([
      { id: "credential-chooser-sign-in-button" },
      { id: "credential-chooser-cancel-button" },
    ]);

    let listBox = browser.ownerDocument.getElementById(
      "credential-chooser-entry-selector-container"
    );
    while (listBox.firstChild) {
      listBox.removeChild(listBox.lastChild);
    }
    let itemTemplate = browser.ownerDocument.getElementById(
      "template-credential-entry-list-item"
    );
    for (let [index, credential] of credentials.entries()) {
      let newItem = itemTemplate.content.firstElementChild.cloneNode(true);
      // Add the new radio button, including pre-selection and the callback
      let [radio] = newItem.getElementsByClassName(
        "identity-credential-list-item-radio"
      );
      radio.value = index;
      if (index == 0) {
        radio.checked = true;
      }

      let providerURL = new URL(credential.origin);
      let displayDomain = lazy.IDNService.convertToDisplayIDN(
        providerURL.host,
        {}
      );

      let [primary] = newItem.getElementsByClassName(
        "identity-credential-list-item-label-primary"
      );
      let [secondary] = newItem.getElementsByClassName(
        "identity-credential-list-item-label-secondary"
      );
      let [icon] = newItem.getElementsByClassName(
        "identity-credential-list-item-icon"
      );

      if (
        credential.uiHints &&
        (credential.uiHints.expiresAfter == null ||
          credential.uiHints.expiresAfter > 0)
      ) {
        primary.textContent = credential.uiHints.name;
        browser.ownerDocument.l10n.setAttributes(
          secondary,
          "credential-chooser-host-descriptor",
          {
            provider: displayDomain,
          }
        );
        secondary.hidden = false;
        icon.src = credential.uiHints.iconURL;
      } else {
        let doneWithFavicon = setIconToFavicon(icon, credential.origin);
        faviconPromises.push(doneWithFavicon);
        browser.ownerDocument.l10n.setAttributes(
          primary,
          "credential-chooser-identity",
          {
            provider: displayDomain,
          }
        );
      }

      // Add the item to the DOM!
      listBox.append(newItem);
    }

    // wait for the labels to be localized before showing the panel
    let [accept, cancel] = await localizationPromise;
    let cancelLabel = cancel.attributes.find(x => x.name == "label").value;
    let cancelKey = cancel.attributes.find(x => x.name == "accesskey").value;
    let acceptLabel = accept.attributes.find(x => x.name == "label").value;
    let acceptKey = accept.attributes.find(x => x.name == "accesskey").value;

    // wait for icons to be set to prevent favicon jank
    await Promise.all(faviconPromises);

    // Construct the necessary arguments for notification behavior
    let options = {
      hideClose: true,
      removeOnDismissal: true,
      eventCallback: (topic, _nextRemovalReason, _isCancel) => {
        if (topic == "removed") {
          callback.notify(null);
        }
      },
    };
    let mainAction = {
      label: acceptLabel,
      accessKey: acceptKey,
      callback() {
        let result = listBox.querySelector(
          ".identity-credential-list-item-radio:checked"
        ).value;
        if (browsingContext.currentWindowGlobal?.documentPrincipal) {
          Services.perms.addFromPrincipal(
            browsingContext.currentWindowGlobal.documentPrincipal,
            "credential-allow-silent-access^" +
              credentials[parseInt(result)].origin,
            Ci.nsIPermissionManager.ALLOW_ACTION,
            Ci.nsIPermissionManager.EXPIRE_SESSION
          );
          Services.perms.addFromPrincipal(
            browsingContext.currentWindowGlobal.documentPrincipal,
            "credential-allow-silent-access",
            Ci.nsIPermissionManager.ALLOW_ACTION,
            Ci.nsIPermissionManager.EXPIRE_SESSION
          );
        }
        callback.notify(credentials[parseInt(result, 10)].id);
      },
    };
    let secondaryActions = [
      {
        label: cancelLabel,
        accessKey: cancelKey,
        callback() {
          callback.notify(null);
        },
      },
    ];
    browser.ownerGlobal.PopupNotifications.show(
      browser,
      "credential-chooser",
      "",
      "identity-credential-notification-icon",
      mainAction,
      secondaryActions,
      options
    );

    return Cr.NS_OK;
  }

  /**
   * Dismiss the credential chooser dialog for this browsing context's window.
   *
   * @param {BrowsingContext} browsingContext - The top browsing context of the window calling the Credential Management API
   */
  cancelCredentialChooser(browsingContext) {
    let browser = browsingContext.top.embedderElement;
    if (browser?.tagName != "browser") {
      return;
    }
    let notification = browser.ownerGlobal.PopupNotifications.getNotification(
      "credential-chooser",
      browser
    );
    if (notification) {
      browser.ownerGlobal.PopupNotifications.remove(notification, true);
    }
  }

  /**
   * A service function to help any UI. Fetches and serializes images to
   * data urls, which can be used in chrome UI.
   *
   * @param {Window} window - Window which should perform the fetch
   * @param {nsIURI} uri - Icon location to be fetched from
   * @returns {Promise<string, Error>} The data URI encoded as a string representing the icon that was loaded
   */
  async fetchImageToDataURI(window, uri) {
    if (uri.protocol === "data:") {
      return uri.href;
    }
    let request = new window.Request(uri.spec, { mode: "cors" });
    request.overrideContentPolicyType(Ci.nsIContentPolicy.TYPE_IMAGE);
    let blob;
    let response = await window.fetch(request);
    if (!response.ok) {
      return Promise.reject(new Error("HTTP failure on Fetch"));
    }
    blob = await response.blob();
    return new Promise((resolve, reject) => {
      let reader = new FileReader();
      reader.onloadend = () => resolve(reader.result);
      reader.onerror = reject;
      reader.readAsDataURL(blob);
    });
  }

  /**
   * A helper function that performs precisely the right Fetch for the well-known resource for FedCM.
   *
   * @param {nsIURI} uri - Well known resource location
   * @param {nsIPrincipal} triggeringPrincipal - Principal of the IDP triggering this request
   * @returns {Promise} A promise that will be the result of fetching the resource and parsing the body as JSON,
   *          or reject along the way.
   */
  async fetchWellKnown(uri, triggeringPrincipal) {
    let request = new Request(uri.spec, {
      mode: "no-cors",
      referrerPolicy: "no-referrer",
      // We use a Null Principal here because we don't want to send any
      // cookies or an Origin header here before we get user permission.
      triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
        triggeringPrincipal.originAttributes
      ),
      // and we want to be able to read the response, so we don't let CORS hide it
      // because we are no-cors
      neverTaint: true,
      credentials: "omit",
      headers: [["Accept", "application/json"]],
    });
    // This overrides the Sec-Fetch-Dest to `webidentity` rather than `empty`
    request.overrideContentPolicyType(Ci.nsIContentPolicy.TYPE_WEB_IDENTITY);
    let response = await fetch(request);
    return response.json();
  }

  /**
   * A helper function that performs precisely the right Fetch for the IDP configuration resource for FedCM.
   *
   * @param {nsIURI} uri - Well known resource location
   * @param {nsIPrincipal} triggeringPrincipal - Principal of the IDP triggering this request
   * @returns {Promise} A promise that will be the result of fetching the resource and parsing the body as JSON,
   *          or reject along the way.
   */
  async fetchConfig(uri, triggeringPrincipal) {
    let request = new Request(uri.spec, {
      mode: "no-cors",
      referrerPolicy: "no-referrer",
      redirect: "error",
      // We use a Null Principal here because we don't want to send any
      // cookies or an Origin header here before we get user permission.
      triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
        triggeringPrincipal.originAttributes
      ),
      // and we want to be able to read the response, so we don't let CORS hide it
      // because we are no-cors
      neverTaint: true,
      credentials: "omit",
      headers: [["Accept", "application/json"]],
    });
    // This overrides the Sec-Fetch-Dest to `webidentity` rather than `empty`
    request.overrideContentPolicyType(Ci.nsIContentPolicy.TYPE_WEB_IDENTITY);
    let response = await fetch(request);
    return response.json();
  }

  /**
   * A helper function that performs precisely the right Fetch for the account list for FedCM.
   *
   * @param {nsIURI} uri - Well known resource location
   * @param {nsIPrincipal} triggeringPrincipal - Principal of the IDP triggering this request
   * @returns {Promise} A promise that will be the result of fetching the resource and parsing the body as JSON,
   *          or reject along the way.
   */
  async fetchAccounts(uri, triggeringPrincipal) {
    let request = new Request(uri.spec, {
      mode: "no-cors",
      redirect: "error",
      referrerPolicy: "no-referrer",
      triggeringPrincipal,
      // and we want to be able to read the response, so we don't let CORS hide it
      // because we are no-cors
      neverTaint: true,
      credentials: "include",
      headers: [["Accept", "application/json"]],
    });
    // This overrides the Sec-Fetch-Dest to `webidentity` rather than `empty`
    request.overrideContentPolicyType(Ci.nsIContentPolicy.TYPE_WEB_IDENTITY);
    let response = await fetch(request);
    return response.json();
  }

  /**
   * A helper function that performs precisely the right Fetch for the token request for FedCM.
   *
   * @param {nsIURI} uri - Well known resource location
   * @param {string} body - Body to be sent with the fetch, pre-serialized.
   * @param {nsIPrincipal} triggeringPrincipal - Principal of the IDP triggering this request
   * @returns {Promise} A promise that will be the result of fetching the resource and parsing the body as JSON,
   *          or reject along the way.
   */
  async fetchToken(uri, body, triggeringPrincipal) {
    let request = new Request(uri.spec, {
      mode: "cors",
      method: "POST",
      redirect: "error",
      triggeringPrincipal,
      body,
      credentials: "include",
      headers: [["Content-type", "application/x-www-form-urlencoded"]],
    });
    // This overrides the Sec-Fetch-Dest to `webidentity` rather than `empty`
    request.overrideContentPolicyType(Ci.nsIContentPolicy.TYPE_WEB_IDENTITY);
    let response = await fetch(request);
    return response.json();
  }

  /**
   * A helper function that performs precisely the right Fetch for the disconnect request for FedCM.
   *
   * @param {nsIURI} uri - Well known resource location
   * @param {string} body - Body to be sent with the fetch, pre-serialized.
   * @param {nsIPrincipal} triggeringPrincipal - Principal of the IDP triggering this request
   * @returns {Promise} A promise that will be the result of fetching the resource and parsing the body as JSON,
   *          or reject along the way.
   */
  async fetchDisconnect(uri, body, triggeringPrincipal) {
    let request = new Request(uri.spec, {
      mode: "cors",
      method: "POST",
      redirect: "error",
      triggeringPrincipal,
      body,
      credentials: "include",
      headers: [["Content-type", "application/x-www-form-urlencoded"]],
    });
    // This overrides the Sec-Fetch-Dest to `webidentity` rather than `empty`
    request.overrideContentPolicyType(Ci.nsIContentPolicy.TYPE_WEB_IDENTITY);
    let response = await fetch(request);
    return response.json();
  }
}
