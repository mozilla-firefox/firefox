// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var gRemovePasswordDialog = {
  _token: null,
  _okButton: null,
  _password: null,
  init() {
    this._okButton = document.getElementById("removemp").getButton("accept");
    document.l10n.setAttributes(this._okButton, "pw-remove-button");

    this._password = document.getElementById("password");
    this._password.addEventListener("input", () => this.validateInput());

    var pk11db = Cc["@mozilla.org/security/pk11tokendb;1"].getService(
      Ci.nsIPK11TokenDB
    );
    this._token = pk11db.getInternalKeyToken();

    // Initialize the enabled state of the Remove button by checking the
    // initial value of the password ("" should be incorrect).
    this.validateInput();
    document.addEventListener("dialogaccept", () => this.removePassword());
  },

  validateInput() {
    this._okButton.disabled = !this._token.checkPassword(this._password.value);
  },

  async createAlert(titleL10nId, messageL10nId) {
    const [title, message] = await document.l10n.formatValues([
      { id: titleL10nId },
      { id: messageL10nId },
    ]);
    Services.prompt.alert(window, title, message);
  },

  removePassword() {
    if (this._token.checkPassword(this._password.value)) {
      this._token.changePassword(this._password.value, "");
      this.createAlert("pw-change-success-title", "settings-pp-erased-ok");
    } else {
      this._password.value = "";
      this._password.focus();
      this.createAlert("pw-change-failed-title", "incorrect-pp");
    }
  },
};

window.addEventListener("load", () => gRemovePasswordDialog.init());
