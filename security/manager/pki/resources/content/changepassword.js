/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ProfileKekPassword: "resource://gre/modules/ProfileKekPassword.sys.mjs",
});

ChromeUtils.defineLazyGetter(
  this,
  "l10n",
  () => new Localization(["security/pippki/pippki.ftl"], true)
);

var token;

function doPrompt(messageL10nId) {
  let msg = l10n.formatValueSync(messageL10nId);
  Services.prompt.alert(window, null, msg);
}

function onLoad() {
  document.getElementById("set_password").getButton("accept").disabled = true;
  document.addEventListener("dialogaccept", setPassword);

  document.getElementById("pw1").addEventListener("input", () => {
    setPasswordStrength();
    checkPasswords();
  });

  let pw2 = document.getElementById("pw2");
  pw2.addEventListener("input", () => {
    checkPasswords();
  });

  let params = window.arguments[0].QueryInterface(Ci.nsIDialogParamBlock);
  token = params.objects.GetElementAt(0).QueryInterface(Ci.nsIPKCS11Token);

  document.l10n.setAttributes(
    document.getElementById("tokenName"),
    "change-password-token",
    { tokenName: token.tokenName }
  );
  process();
}

function process() {
  let oldpwbox = document.getElementById("oldpw");
  let msgBox = document.getElementById("message");
  // If the token doesn't have a password, don't use the old password box.
  // Otherwise, do.
  if (!token.hasPassword) {
    oldpwbox.hidden = true;
    document.l10n.setAttributes(msgBox, "change-password-not-set");
    msgBox.hidden = false;
    // Select first password field
    document.getElementById("pw1").focus();
  } else {
    // Select old password field
    oldpwbox.hidden = false;
    msgBox.hidden = true;
    oldpwbox.focus();
  }

  checkPasswords();
}

async function setPassword(event) {
  event.preventDefault();

  let oldpwbox = document.getElementById("oldpw");
  let pw1 = document.getElementById("pw1");
  if (pw1.value == "") {
    const fipsUtils = Cc["@mozilla.org/security/fipsutils;1"].getService(
      Ci.nsIFIPSUtils
    );
    if (fipsUtils.isFIPSEnabled) {
      // empty passwords are not allowed in FIPS mode
      doPrompt("pippki-pw-change2empty-in-fips-mode");
      return;
    }
  }

  let kekMoved = false;
  try {
    // Only the internal key token's password is the primary password the
    // lockstore KEK follows; any other token here is unrelated to profile
    // encryption. changePassword cannot be reverted, so the KEK moves first.
    if (
      token.isInternalKeyToken &&
      (!token.hasPassword || (await lazy.ProfileKekPassword.exists()))
    ) {
      await lazy.ProfileKekPassword.update(oldpwbox.value, pw1.value);
      kekMoved = true;
    }
    await token.changePassword(oldpwbox.value, pw1.value);
    doPrompt(pw1.value == "" ? "pippki-pw-erased-ok" : "pippki-pw-change-ok");
    window.close();
  } catch (e) {
    // kekMoved is only set once the update resolved, so reaching here with it
    // set means changePassword is what failed: the token still has the old
    // password, so put the KEK back under it.
    if (kekMoved) {
      await lazy.ProfileKekPassword.update(pw1.value, oldpwbox.value).catch(
        rollbackError =>
          console.error("Failed to roll back the lockstore KEK", rollbackError)
      );
    }
    let nssErrorsService = Cc["@mozilla.org/nss_errors_service;1"].getService(
      Ci.nsINSSErrorsService
    );
    // SEC_ERROR_BASE + 15 = SEC_ERROR_BAD_PASSWORD
    let badPasswordResult = nssErrorsService.getXPCOMFromNSSError(
      Ci.nsINSSErrorsService.NSS_SEC_ERROR_BASE + 15
    );
    // The lockstore unlock runs first and reports a wrong password as
    // NS_ERROR_ABORT rather than the token's bad-password code. Only reachable
    // with kekMoved unset: past that point the KEK is done with and the token,
    // which reports failures as NSS codes, is what failed.
    if (
      e.result == badPasswordResult ||
      (!kekMoved && e.result == Cr.NS_ERROR_ABORT)
    ) {
      oldpwbox.focus();
      oldpwbox.setAttribute("value", "");
      doPrompt("pippki-incorrect-pw");
    } else {
      doPrompt("pippki-failed-pw-change");
    }
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

  document.getElementById("set_password").getButton("accept").disabled =
    pw1 != pw2;
}

window.addEventListener("load", onLoad);
