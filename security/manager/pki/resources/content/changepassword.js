/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

ChromeUtils.defineLazyGetter(
  this,
  "l10n",
  () => new Localization(["security/pippki/pippki.ftl"], true)
);

var params;
var token;
var pw1;

function doPrompt(messageL10nId) {
  let msg = l10n.formatValueSync(messageL10nId);
  Services.prompt.alert(window, null, msg);
}

function onLoad() {
  document.getElementById("set_password").getButton("accept").disabled = true;
  document.addEventListener("dialogaccept", setPassword);

  pw1 = document.getElementById("pw1");
  pw1.addEventListener("input", () => {
    setPasswordStrength();
    checkPasswords();
  });

  let pw2 = document.getElementById("pw2");
  pw2.addEventListener("input", () => {
    checkPasswords();
  });

  params = window.arguments[0].QueryInterface(Ci.nsIDialogParamBlock);
  token = params.objects.GetElementAt(0).QueryInterface(Ci.nsIPK11Token);

  document.l10n.setAttributes(
    document.getElementById("tokenName"),
    "change-password-token",
    { tokenName: token.tokenName }
  );
  process();
}

function process() {
  let bundle = document.getElementById("pippki_bundle");
  let oldpwbox = document.getElementById("oldpw");
  let msgBox = document.getElementById("message");
  // If the token is unitialized, don't use the old password box.
  // Otherwise, do.
  if ((token.needsLogin() && token.needsUserInit) || !token.needsLogin()) {
    oldpwbox.hidden = true;
    msgBox.setAttribute("value", bundle.getString("password_not_set"));
    msgBox.hidden = false;

    if (!token.needsLogin()) {
      oldpwbox.setAttribute("inited", "empty");
    } else {
      oldpwbox.setAttribute("inited", "true");
    }

    // Select first password field
    document.getElementById("pw1").focus();
  } else {
    // Select old password field
    oldpwbox.hidden = false;
    msgBox.hidden = true;
    oldpwbox.setAttribute("inited", "false");
    oldpwbox.focus();
  }

  // Return value 0 means "canceled"
  params.SetInt(1, 0);

  checkPasswords();
}

function setPassword(event) {
  var oldpwbox = document.getElementById("oldpw");
  var initpw = oldpwbox.getAttribute("inited");

  var success = false;

  if (initpw == "false" || initpw == "empty") {
    try {
      var oldpw = "";
      var passok = 0;

      if (initpw == "empty") {
        passok = 1;
      } else {
        oldpw = oldpwbox.value;
        passok = token.checkPassword(oldpw);
      }

      if (passok) {
        if (initpw == "empty" && pw1.value == "") {
          // checkPasswords() should have prevented this path from being reached.
        } else {
          if (pw1.value == "") {
            var secmoddb = Cc[
              "@mozilla.org/security/pkcs11moduledb;1"
            ].getService(Ci.nsIPKCS11ModuleDB);
            if (secmoddb.isFIPSEnabled) {
              // empty passwords are not allowed in FIPS mode
              doPrompt("pippki-pw-change2empty-in-fips-mode");
              passok = 0;
            }
          }
          if (passok) {
            token.changePassword(oldpw, pw1.value);
            if (pw1.value == "") {
              doPrompt("pippki-pw-erased-ok");
            } else {
              doPrompt("pippki-pw-change-ok");
            }
            success = true;
          }
        }
      } else {
        oldpwbox.focus();
        oldpwbox.setAttribute("value", "");
        doPrompt("pippki-incorrect-pw");
      }
    } catch (e) {
      doPrompt("pippki-failed-pw-change");
    }
  } else {
    token.initPassword(pw1.value);
    if (pw1.value == "") {
      doPrompt("pippki-pw-not-wanted");
    }
    success = true;
  }

  if (success && params) {
    // Return value 1 means "successfully executed ok"
    params.SetInt(1, 1);
  }

  // Terminate dialog
  if (!success) {
    event.preventDefault();
  }
}

function setPasswordStrength() {
  // We weigh the quality of the password by checking the number of:
  //  - Characters
  //  - Numbers
  //  - Non-alphanumeric chars
  //  - Upper and lower case characters

  let pw = document.getElementById("pw1").value;

  let pwlength = pw.length;
  if (pwlength > 5) {
    pwlength = 5;
  }

  let numnumeric = pw.replace(/[0-9]/g, "");
  let numeric = pw.length - numnumeric.length;
  if (numeric > 3) {
    numeric = 3;
  }

  let symbols = pw.replace(/\W/g, "");
  let numsymbols = pw.length - symbols.length;
  if (numsymbols > 3) {
    numsymbols = 3;
  }

  let numupper = pw.replace(/[A-Z]/g, "");
  let upper = pw.length - numupper.length;
  if (upper > 3) {
    upper = 3;
  }

  let pwstrength =
    pwlength * 10 - 20 + numeric * 10 + numsymbols * 15 + upper * 10;

  // Clamp strength to [0, 100].
  if (pwstrength < 0) {
    pwstrength = 0;
  }
  if (pwstrength > 100) {
    pwstrength = 100;
  }

  let meter = document.getElementById("pwmeter");
  meter.setAttribute("value", pwstrength);
}

function checkPasswords() {
  let pw1 = document.getElementById("pw1").value;
  let pw2 = document.getElementById("pw2").value;

  var oldpwbox = document.getElementById("oldpw");
  if (oldpwbox) {
    var initpw = oldpwbox.getAttribute("inited");

    if (initpw == "empty" && pw1 == "") {
      // The token has already been initialized, therefore this dialog
      // was called with the intention to change the password.
      // The token currently uses an empty password.
      // We will not allow changing the password from empty to empty.
      document.getElementById("set_password").getButton("accept").disabled =
        true;
      return;
    }
  }

  document.getElementById("set_password").getButton("accept").disabled =
    pw1 != pw2;
}

window.addEventListener("load", onLoad);
