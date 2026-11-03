/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { viewCertHelper } = ChromeUtils.importESModule(
  "moz-src:///security/manager/pki/pippki.sys.mjs"
);

/**
 * @file Implements the functionality of downloadcert.xhtml: a dialog that allows
 *       a user to confirm whether to import a certificate, and if so what trust
 *       to give it.
 * @param {nsISupports} window.arguments.0
 *           Certificate to confirm import of, queryable to nsIX509Cert.
 * @param {nsISupports} window.arguments.1
 *           Object to set the return values of calling the dialog on, queryable
 *           to the underlying type of DownloadCertReturnValues.
 */

/**
 * @typedef DownloadCertReturnValues
 * @type {nsIWritablePropertyBag2}
 * @property {boolean} importConfirmed
 *           Set to true if the user confirmed import of the cert and accepted
 *           the dialog, false otherwise.
 * @property {boolean} trustForSSL
 *           Set to true if the cert should be trusted for SSL, false otherwise.
 *           Undefined value if |importConfirmed| is not true.
 * @property {boolean} trustForEmail
 *           Set to true if the cert should be trusted for e-mail, false
 *           otherwise. Undefined value if |importConfirmed| is not true.
 */

/**
 * The cert to potentially import.
 *
 * @type {nsIX509Cert}
 */
var gCert;

/**
 * onload() handler.
 */
async function onLoad() {
  gCert = window.arguments[0].QueryInterface(Ci.nsIX509Cert);

  document.addEventListener("dialogaccept", onDialogAccept);
  document.addEventListener("dialogcancel", onDialogCancel);

  document.getElementById("viewC-button").addEventListener("command", () => {
    viewCertHelper(window, gCert, "window");
  });

  let caName = gCert.commonName;
  if (!caName.length) {
    caName = await document.l10n.formatValue("download-cert-unnamed-ca");
  }

  let trustHeader = document.getElementById("trustHeader");
  document.l10n.setAttributes(trustHeader, "download-cert-trust-header", {
    caName,
  });
  await document.l10n.translateElements([trustHeader]);
}

/**
 * ondialogaccept() handler.
 */
function onDialogAccept() {
  let checkSSL = document.getElementById("trustSSL");
  let checkEmail = document.getElementById("trustEmail");

  let retVals = window.arguments[1].QueryInterface(Ci.nsIWritablePropertyBag2);
  retVals.setPropertyAsBool("importConfirmed", true);
  retVals.setPropertyAsBool("trustForSSL", checkSSL.checked);
  retVals.setPropertyAsBool("trustForEmail", checkEmail.checked);
}

/**
 * ondialogcancel() handler.
 */
function onDialogCancel() {
  let retVals = window.arguments[1].QueryInterface(Ci.nsIWritablePropertyBag2);
  retVals.setPropertyAsBool("importConfirmed", false);
}

window.addEventListener("load", () => {
  onLoad().then(() => {
    Services.obs.notifyObservers(window, "psm:download-cert-loaded");
  });
});
