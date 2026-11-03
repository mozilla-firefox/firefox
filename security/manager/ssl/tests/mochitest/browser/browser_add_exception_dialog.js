/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This test makes sure that adding certificate exceptions behaves correctly
// when done from the prefs window

ChromeUtils.defineESModuleGetters(this, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
});

const EXCEPTIONS_DLG_URL = "chrome://pippki/content/exceptionDialog.xhtml";
const EXCEPTIONS_DLG_FEATURES = "chrome,centerscreen,modal";
const INVALID_CERT_DOMAIN = "self-signed.example.com";
const INVALID_CERT_LOCATION = "https://" + INVALID_CERT_DOMAIN + "/";

registerCleanupFunction(() => {
  let certOverrideService = Cc[
    "@mozilla.org/security/certoverride;1"
  ].getService(Ci.nsICertOverrideService);
  certOverrideService.clearValidityOverride(INVALID_CERT_DOMAIN, -1, {});
});

async function onCertExceptionUI(win) {
  Services.obs.removeObserver(onCertExceptionUI, "cert-exception-ui-ready");
  ok(win.gCert, "The certificate information should be available now");

  // Clicking on the View… button should open the certificate viewer.
  let viewButton = win.document.getElementById("viewCertButton");
  let tabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    url => url.startsWith("about:certificate?cert="),
    true
  );
  EventUtils.synthesizeMouseAtCenter(viewButton, {}, win);
  BrowserTestUtils.removeTab(await tabPromise);

  if (AppConstants.platform != "macosx") {
    // Pressing enter on the View… button should open the certificate viewer.
    tabPromise = BrowserTestUtils.waitForNewTab(
      gBrowser,
      url => url.startsWith("about:certificate?cert="),
      true
    );
    viewButton.focus();
    EventUtils.synthesizeKey("KEY_Enter", {}, win);
    BrowserTestUtils.removeTab(await tabPromise);
  }

  let dialog = win.document.getElementById("exceptiondialog");
  let confirmButton = dialog.getButton("extra1");
  confirmButton.click();
}

add_task(async function test_with_subdialog() {
  Services.obs.addObserver(onCertExceptionUI, "cert-exception-ui-ready");

  await BrowserTestUtils.withNewTab("about:preferences", async browser => {
    let params = {
      exceptionAdded: false,
      location: INVALID_CERT_LOCATION,
      prefetchCert: true,
    };
    await new Promise(resolve => {
      // Open the add exception dialog in the way that about:preferences does (in a sub-dialog).
      browser.contentWindow.gSubDialog.open(
        EXCEPTIONS_DLG_URL,
        { features: EXCEPTIONS_DLG_FEATURES, closedCallback: resolve },
        params
      );
    });
    ok(
      params.exceptionAdded,
      "The certificate exception should have been added"
    );
  });

  BrowserTestUtils.startLoadingURIString(gBrowser, INVALID_CERT_LOCATION);
  let loaded = await BrowserTestUtils.browserLoaded(
    gBrowser,
    false,
    INVALID_CERT_LOCATION,
    true
  );
  ok(loaded, "The certificate exception should allow the page to load");

  let certOverrideService = Cc[
    "@mozilla.org/security/certoverride;1"
  ].getService(Ci.nsICertOverrideService);
  certOverrideService.clearValidityOverride(INVALID_CERT_DOMAIN, -1, {});
});

add_task(async function test_with_dialog() {
  Services.obs.addObserver(onCertExceptionUI, "cert-exception-ui-ready");

  let params = {
    exceptionAdded: false,
    location: INVALID_CERT_LOCATION,
    prefetchCert: true,
  };

  let bWin = BrowserWindowTracker.getTopWindow();

  // Open the add exception dialog without a sub-dialog.
  bWin.openDialog(EXCEPTIONS_DLG_URL, "", EXCEPTIONS_DLG_FEATURES, params);

  ok(params.exceptionAdded, "The certificate exception should have been added");

  BrowserTestUtils.startLoadingURIString(gBrowser, INVALID_CERT_LOCATION);
  let loaded = await BrowserTestUtils.browserLoaded(
    gBrowser,
    false,
    INVALID_CERT_LOCATION,
    true
  );
  ok(loaded, "The certificate exception should allow the page to load");

  let certOverrideService = Cc[
    "@mozilla.org/security/certoverride;1"
  ].getService(Ci.nsICertOverrideService);
  certOverrideService.clearValidityOverride(INVALID_CERT_DOMAIN, -1, {});
});

add_task(async function test_schemeless_host_port_with_external_handler() {
  // Make the host look like a known external protocol scheme, as the desktop
  // portal may do for any scheme on Flatpak and Snap.
  let extProtocolSvc = Cc[
    "@mozilla.org/uriloader/external-protocol-service;1"
  ].getService(Ci.nsIExternalProtocolService);
  let handlerSvc = Cc["@mozilla.org/uriloader/handler-service;1"].getService(
    Ci.nsIHandlerService
  );
  let handlerInfo = extProtocolSvc.getProtocolHandlerInfo(INVALID_CERT_DOMAIN);
  let handler = Cc["@mozilla.org/uriloader/web-handler-app;1"].createInstance(
    Ci.nsIWebHandlerApp
  );
  handler.name = "Test handler";
  handler.uriTemplate = "https://example.com/%s";
  handlerInfo.possibleApplicationHandlers.appendElement(handler);
  handlerSvc.store(handlerInfo);
  registerCleanupFunction(() => handlerSvc.remove(handlerInfo));
  ok(
    extProtocolSvc.externalProtocolHandlerExists(INVALID_CERT_DOMAIN),
    "The host should be handled as an external protocol scheme"
  );

  Services.obs.addObserver(onCertExceptionUI, "cert-exception-ui-ready");

  let params = {
    exceptionAdded: false,
    location: INVALID_CERT_DOMAIN + ":443",
    prefetchCert: true,
  };

  let bWin = BrowserWindowTracker.getTopWindow();
  bWin.openDialog(EXCEPTIONS_DLG_URL, "", EXCEPTIONS_DLG_FEATURES, params);

  ok(params.exceptionAdded, "The certificate exception should have been added");

  BrowserTestUtils.startLoadingURIString(gBrowser, INVALID_CERT_LOCATION);
  let loaded = await BrowserTestUtils.browserLoaded(
    gBrowser,
    false,
    INVALID_CERT_LOCATION,
    true
  );
  ok(loaded, "The certificate exception should allow the page to load");

  let certOverrideService = Cc[
    "@mozilla.org/security/certoverride;1"
  ].getService(Ci.nsICertOverrideService);
  certOverrideService.clearValidityOverride(INVALID_CERT_DOMAIN, -1, {});
});
