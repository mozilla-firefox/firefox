/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// ClientAuthDialogService implements nsIClientAuthDialogService, and aims to
// open a dialog asking the user to select a client authentication certificate.
// Ideally the dialog will be tab-modal to the tab corresponding to the load
// that resulted in the request for the client authentication certificate.
export function ClientAuthDialogService() {}

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PromptUtils: "resource://gre/modules/PromptUtils.sys.mjs",
});

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

if (AppConstants.platform == "android") {
  ChromeUtils.defineESModuleGetters(lazy, {
    GeckoViewPrompter:
      "moz-src:///mobile/shared/components/geckoview/GeckoViewPrompter.sys.mjs",
  });
}

// Given a loadContext (CanonicalBrowsingContext), attempts to return a
// TabDialogBox for the browser corresponding to loadContext.
function getTabDialogBoxForLoadContext(loadContext) {
  let tabBrowser = loadContext?.topFrameElement?.getTabBrowser();
  if (!tabBrowser) {
    return null;
  }
  for (let browser of tabBrowser.browsers) {
    if (browser.browserId == loadContext.top?.browserId) {
      return tabBrowser.getTabDialogBox(browser);
    }
  }
  return null;
}

ClientAuthDialogService.prototype = {
  classID: Components.ID("{d7d2490d-2640-411b-9f09-a538803c11ee}"),
  QueryInterface: ChromeUtils.generateQI(["nsIClientAuthDialogService"]),

  chooseCertificate: function ClientAuthDialogService_chooseCertificate(
    hostname,
    certArray,
    loadContext,
    caNames,
    callback
  ) {
    // On Android, the OS implements the prompt. However, we have to plumb the
    // relevant information through to the frontend, which will return the
    // alias of the certificate, or null if none was selected.
    if (AppConstants.platform == "android") {
      const prompt = new lazy.GeckoViewPrompter(
        loadContext.topFrameElement.documentGlobal
      );
      let issuers = null;
      if (caNames.length) {
        issuers = [];
        for (let caName of caNames) {
          issuers.push(btoa(caName.map(b => String.fromCharCode(b)).join("")));
        }
      }
      prompt.asyncShowPrompt(
        { type: "certificate", host: hostname, issuers },
        result => {
          let certDB = Cc["@mozilla.org/security/x509certdb;1"].getService(
            Ci.nsIX509CertDB
          );
          let certificate = null;
          if (result.alias) {
            try {
              certificate = certDB.getAndroidCertificateFromAlias(result.alias);
            } catch (e) {
              console.error("couldn't get certificate from alias", e);
            }
          }
          // The UI provided by the OS has no option to choose how long to
          // remember the decision. The most broadly useful default is to
          // remember the decision for the session.
          callback.certificateChosen(
            certificate,
            Ci.nsIClientAuthRememberService.Session
          );
        }
      );

      return;
    }

    const clientAuthAskURI = "chrome://pippki/content/clientauthask.xhtml";
    let retVals = {
      cert: null,
      rememberDuration: Ci.nsIClientAuthRememberService.Session,
    };
    let args = lazy.PromptUtils.objectToPropBag({
      hostname,
      certArray,
      retVals,
    });

    // First attempt to find a TabDialogBox for the loadContext. This allows
    // for a tab-modal dialog specific to the tab causing the load, which is a
    // better user experience.
    let tabDialogBox = getTabDialogBoxForLoadContext(loadContext);
    if (tabDialogBox) {
      tabDialogBox.open(clientAuthAskURI, {}, args).closedPromise.then(() => {
        callback.certificateChosen(retVals.cert, retVals.rememberDuration);
      });
      return;
    }
    // Otherwise, attempt to open a window-modal dialog on the window that at
    // least has the tab the load is occurring in.
    let browserWindow = loadContext?.topFrameElement?.documentGlobal;
    // Failing that, open a window-modal dialog on the most recent window.
    if (!browserWindow?.gDialogBox) {
      browserWindow = Services.wm.getMostRecentBrowserWindow();
    }

    if (browserWindow?.gDialogBox) {
      browserWindow.gDialogBox.open(clientAuthAskURI, args).then(() => {
        callback.certificateChosen(retVals.cert, retVals.rememberDuration);
      });
      return;
    }

    let mostRecentWindow = Services.wm.getMostRecentWindow("");
    Services.ww.openWindow(
      mostRecentWindow,
      clientAuthAskURI,
      "_blank",
      "centerscreen,chrome,modal,titlebar",
      args
    );
    callback.certificateChosen(retVals.cert, retVals.rememberDuration);
  },
};
