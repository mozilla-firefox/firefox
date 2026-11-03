/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { setText, viewCertHelper, checkCertHelper } = ChromeUtils.importESModule(
  "moz-src:///security/manager/pki/pippki.sys.mjs"
);

var gDialog;
var gSecInfo;
var gCert;
var gChecking;
var gBroken;
var gNeedReset;

const { PrivateBrowsingUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/PrivateBrowsingUtils.sys.mjs"
);

function initExceptionDialog() {
  gNeedReset = false;
  gDialog = document.getElementById("exceptiondialog");
  let warningText = document.getElementById("warningText");
  document.l10n.setAttributes(warningText, "add-exception-branded-warning");
  let confirmButton = gDialog.getButton("extra1");
  let l10nUpdatedElements = [confirmButton, warningText];
  confirmButton.disabled = true;

  document
    .getElementById("locationTextBox")
    .addEventListener("input", () => handleTextChange());
  document
    .getElementById("viewCertButton")
    .addEventListener("command", () => viewCertButtonClick());

  var args = window.arguments;
  if (args && args[0]) {
    if (args[0].location) {
      // We were pre-seeded with a location.
      document.getElementById("locationTextBox").value = args[0].location;
      document.getElementById("checkCertButton").disabled = false;

      if (args[0].securityInfo) {
        gSecInfo = args[0].securityInfo;
        gCert = gSecInfo.serverCert;
        gBroken = true;
        l10nUpdatedElements = l10nUpdatedElements.concat(updateCertStatus());
      } else if (args[0].prefetchCert) {
        // We can optionally pre-fetch the certificate too.  Don't do this
        // synchronously, since it would prevent the window from appearing
        // until the fetch is completed, which could be multiple seconds.
        // Instead, let's use a timer to spawn the actual fetch, but update
        // the dialog to "checking..." state right away, so that the UI
        // is appropriately responsive.  Bug 453855
        document.getElementById("checkCertButton").disabled = true;
        gChecking = true;
        l10nUpdatedElements = l10nUpdatedElements.concat(updateCertStatus());

        window.setTimeout(checkCert, 0);
      }
    }

    // Set out parameter to false by default
    args[0].exceptionAdded = false;
  }

  for (let id of [
    "warningSupplemental",
    "certLocationLabel",
    "checkCertButton",
    "statusDescription",
    "statusLongDescription",
    "viewCertButton",
    "permanent",
  ]) {
    let element = document.getElementById(id);
    l10nUpdatedElements.push(element);
  }

  document.l10n
    .translateElements(l10nUpdatedElements)
    .then(() => window.sizeToContent());

  document.addEventListener("dialogextra1", addException);
  document.addEventListener("dialogextra2", checkCert);
}

/**
 * Helper function for checkCert. Set as the onerror/onload callbacks for an
 * XMLHttpRequest. Sets gSecInfo, gCert, gBroken, and gChecking according to
 * the load information from the request. Probably should not be used directly.
 *
 * @param {XMLHttpRequest} req
 *        The XMLHttpRequest created and sent by checkCert.
 * @param {Event} evt
 *        The load or error event.
 */
function grabCert(req, evt) {
  if (req.channel && req.channel.securityInfo) {
    gSecInfo = req.channel.securityInfo;
    gCert = gSecInfo ? gSecInfo.serverCert : null;
  }
  gBroken = evt.type == "error";
  gChecking = false;
  document.l10n
    .translateElements(updateCertStatus())
    .then(() => window.sizeToContent());
}

/**
 * Attempt to download the certificate for the location specified, and populate
 * the Certificate Status section with the result.
 */
async function checkCert() {
  gCert = null;
  gSecInfo = null;
  gChecking = true;
  gBroken = false;
  await document.l10n.translateElements(updateCertStatus());
  window.sizeToContent();

  let uri = getURI();

  if (uri) {
    checkCertHelper(uri, grabCert);
  } else {
    gChecking = false;
    await document.l10n.translateElements(updateCertStatus());
    window.sizeToContent();
  }
}

/**
 * Build and return a URI, based on the information supplied in the
 * Certificate Location fields
 *
 * @returns {nsIURI}
 *          URI constructed from the information supplied on success, null
 *          otherwise.
 */
function getURI() {
  let value = document.getElementById("locationTextBox").value.trim();
  let url = URL.parse(value);
  if (!url?.hostname) {
    url = URL.parse("https://" + value);
  }
  if (!url?.hostname) {
    return null;
  }
  url.protocol = "https:";
  return url.URI;
}

function resetDialog() {
  document.getElementById("viewCertButton").disabled = true;
  document.getElementById("permanent").disabled = true;
  gDialog.getButton("extra1").disabled = true;
  setText(document, "headerDescription", "");
  setText(document, "statusDescription", "");
  setText(document, "statusLongDescription", "");
  setText(document, "status2Description", "");
  setText(document, "status2LongDescription", "");
  setText(document, "status3Description", "");
  setText(document, "status3LongDescription", "");
  window.sizeToContent();
}

/**
 * Called by input textboxes to manage UI state
 */
function handleTextChange() {
  var checkCertButton = document.getElementById("checkCertButton");
  checkCertButton.disabled = !document.getElementById("locationTextBox").value;
  if (gNeedReset) {
    gNeedReset = false;
    resetDialog();
  }
}

function updateCertStatus() {
  var shortDesc, longDesc;
  let l10nUpdatedElements = [];
  if (gCert) {
    if (gBroken) {
      var mms = "add-exception-domain-mismatch-short";
      var mml = "add-exception-domain-mismatch-long";
      var exs = "add-exception-expired-short";
      var exl = "add-exception-expired-long";
      var uts = "add-exception-unverified-or-bad-signature-short";
      var utl = "add-exception-unverified-or-bad-signature-long";
      if (
        gSecInfo.overridableErrorCategory ==
        Ci.nsITransportSecurityInfo.ERROR_TRUST
      ) {
        shortDesc = uts;
        longDesc = utl;
      } else if (
        gSecInfo.overridableErrorCategory ==
        Ci.nsITransportSecurityInfo.ERROR_DOMAIN
      ) {
        shortDesc = mms;
        longDesc = mml;
      } else if (
        gSecInfo.overridableErrorCategory ==
        Ci.nsITransportSecurityInfo.ERROR_TIME
      ) {
        shortDesc = exs;
        longDesc = exl;
      }
      // In these cases, we do want to enable the "Add Exception" button
      gDialog.getButton("extra1").disabled = false;

      // If the Private Browsing service is available and the mode is active,
      // don't store permanent exceptions, since they would persist after
      // private browsing mode was disabled.
      var inPrivateBrowsing = inPrivateBrowsingMode();
      var pe = document.getElementById("permanent");
      pe.disabled = inPrivateBrowsing;
      pe.checked = !inPrivateBrowsing;

      let headerDescription = document.getElementById("headerDescription");
      document.l10n.setAttributes(
        headerDescription,
        "add-exception-invalid-header"
      );
      l10nUpdatedElements.push(headerDescription);
    } else {
      shortDesc = "add-exception-valid-short";
      longDesc = "add-exception-valid-long";
      gDialog.getButton("extra1").disabled = true;
      document.getElementById("permanent").disabled = true;
    }

    // We're done checking the certificate, so allow the user to check it again.
    document.getElementById("checkCertButton").disabled = false;
    document.getElementById("viewCertButton").disabled = false;

    // Notify observers about the availability of the certificate
    Services.obs.notifyObservers(window, "cert-exception-ui-ready");
  } else if (gChecking) {
    shortDesc = "add-exception-checking-short";
    longDesc = "add-exception-checking-long";
    // We're checking the certificate, so we disable the Get Certificate
    // button to make sure that the user can't interrupt the process and
    // trigger another certificate fetch.
    document.getElementById("checkCertButton").disabled = true;
    document.getElementById("viewCertButton").disabled = true;
    gDialog.getButton("extra1").disabled = true;
    document.getElementById("permanent").disabled = true;
  } else {
    shortDesc = "add-exception-no-cert-short";
    longDesc = "add-exception-no-cert-long";
    // We're done checking the certificate, so allow the user to check it again.
    document.getElementById("checkCertButton").disabled = false;
    document.getElementById("viewCertButton").disabled = true;
    gDialog.getButton("extra1").disabled = true;
    document.getElementById("permanent").disabled = true;
  }
  let statusDescription = document.getElementById("statusDescription");
  let statusLongDescription = document.getElementById("statusLongDescription");
  document.l10n.setAttributes(statusDescription, shortDesc);
  document.l10n.setAttributes(statusLongDescription, longDesc);
  l10nUpdatedElements.push(statusDescription);
  l10nUpdatedElements.push(statusLongDescription);

  gNeedReset = true;
  return l10nUpdatedElements;
}

/**
 * Handle user request to display certificate details
 */
function viewCertButtonClick() {
  if (gCert) {
    viewCertHelper(this, gCert);
  }
}

/**
 * Handle user request to add an exception for the specified cert
 */
function addException() {
  if (!gCert || !gSecInfo) {
    return;
  }

  var overrideService = Cc["@mozilla.org/security/certoverride;1"].getService(
    Ci.nsICertOverrideService
  );
  var permanentCheckbox = document.getElementById("permanent");
  var shouldStorePermanently =
    permanentCheckbox.checked && !inPrivateBrowsingMode();
  var uri = getURI();
  overrideService.rememberValidityOverride(
    uri.asciiHost,
    uri.port,
    {},
    gCert,
    !shouldStorePermanently
  );

  let args = window.arguments;
  if (args && args[0]) {
    args[0].exceptionAdded = true;
  }

  gDialog.acceptDialog();
}

/**
 * @returns {boolean} Whether this dialog is in private browsing mode.
 */
function inPrivateBrowsingMode() {
  return window.isChromeWindow
    ? PrivateBrowsingUtils.isWindowPrivate(window)
    : PrivateBrowsingUtils.isContentWindowPrivate(window);
}

window.addEventListener("load", () => initExceptionDialog());
