/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { alertPromptService } = ChromeUtils.importESModule(
  "moz-src:///security/manager/pki/pippki.sys.mjs"
);

document.addEventListener("dialogaccept", onDialogAccept);

/**
 * @file Implements the functionality of load_device.xhtml: a dialog that allows
 *       a PKCS #11 module to be loaded into Firefox.
 */

async function onBrowseBtnPress() {
  let fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);
  let [loadPK11ModuleFilePickerTitle] = await document.l10n.formatValues([
    { id: "load-pk11-module-file-picker-title" },
  ]);
  fp.init(
    window.browsingContext,
    loadPK11ModuleFilePickerTitle,
    Ci.nsIFilePicker.modeOpen
  );
  fp.appendFilters(Ci.nsIFilePicker.filterAll);
  fp.open(rv => {
    if (rv == Ci.nsIFilePicker.returnOK) {
      document.getElementById("device_path").value = fp.file.path;
    }

    // This notification gets sent solely for test purposes. It should not be
    // used by production code.
    Services.obs.notifyObservers(window, "LoadPKCS11Module:FilePickHandled");
  });
}

/**
 * ondialogaccept() handler.
 *
 * @param {object} event
 *        The event causing this handler function to be called.
 */
async function onDialogAccept(event) {
  // Prevent the window from closing in case adding the module fails.
  event.preventDefault();

  let nameBox = document.getElementById("device_name");
  let pathBox = document.getElementById("device_path");
  let pkcs11ModuleDB = Cc["@mozilla.org/security/pkcs11moduledb;1"].getService(
    Ci.nsIPKCS11ModuleDB
  );

  try {
    await pkcs11ModuleDB.addModule(nameBox.value, pathBox.value, 0, 0);
    window.close();
  } catch (e) {
    addModuleFailure("add-module-failure");
  }
}

async function addModuleFailure(l10nID) {
  let [AddModuleFailure] = await document.l10n.formatValues([{ id: l10nID }]);
  alertPromptService(window, null, AddModuleFailure);
}

function validateModuleName() {
  let name = document.getElementById("device_name").value;
  let helpText = document.getElementById("helpText");
  helpText.value = "";
  let dialogNode = document.querySelector("dialog");
  dialogNode.removeAttribute("buttondisabledaccept");
  if (name == "") {
    document.l10n.setAttributes(helpText, "load-module-help-empty-module-name");
    dialogNode.setAttribute("buttondisabledaccept", true);
  }
  if (name == "Root Certs") {
    document.l10n.setAttributes(
      helpText,
      "load-module-help-root-certs-module-name"
    );
    dialogNode.setAttribute("buttondisabledaccept", true);
  }
}

window.addEventListener("load", () => {
  document
    .getElementById("device_name")
    .addEventListener("change", () => validateModuleName());
  document
    .getElementById("browse")
    .addEventListener("command", () => onBrowseBtnPress());
});
