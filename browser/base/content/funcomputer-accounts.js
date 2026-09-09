/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const form = document.getElementById("auth-form");
const signedIn = document.getElementById("signed-in");
const signedInEmail = document.getElementById("signed-in-email");
const avatar = document.getElementById("avatar");
const statusEl = document.getElementById("status");
const emailEl = document.getElementById("email");
const passwordEl = document.getElementById("password");
const confirmEl = document.getElementById("confirm");
const confirmWrap = document.getElementById("confirm-wrap");
const submitEl = document.getElementById("submit");
const modeSignin = document.getElementById("mode-signin");
const modeSignup = document.getElementById("mode-signup");

let signupMode = false;

function showStatus(message, isError) {
  statusEl.hidden = !message;
  statusEl.textContent = message || "";
  statusEl.classList.toggle("error", !!isError);
}

function setSignupMode(on) {
  signupMode = on;
  form.classList.toggle("is-signup", on);
  confirmWrap.classList.toggle("hidden", !on);
  confirmEl.required = on;
  passwordEl.autocomplete = on ? "new-password" : "current-password";
  modeSignin.setAttribute("aria-selected", String(!on));
  modeSignup.setAttribute("aria-selected", String(on));
  document.l10n.setAttributes(
    submitEl,
    on
      ? "funcomputer-accounts-submit-signup"
      : "funcomputer-accounts-submit-signin"
  );
}

function render(session) {
  let signed = !!(session && session.email);
  signedIn.classList.toggle("hidden", !signed);
  form.classList.toggle("hidden", signed);
  if (signed) {
    signedInEmail.textContent = session.email;
    avatar.textContent = session.email.charAt(0).toUpperCase();
  }
}

async function refresh() {
  if (typeof window.funcomputerSession != "function") {
    setTimeout(refresh, 50);
    return;
  }
  render(await window.funcomputerSession());
}

function setBusy(on, l10nId) {
  form.classList.toggle("is-busy", on);
  if (on && l10nId) {
    document.l10n.setAttributes(submitEl, l10nId);
  } else if (!on) {
    setSignupMode(signupMode);
  }
}

modeSignin.addEventListener("click", () => {
  showStatus("");
  setSignupMode(false);
});
modeSignup.addEventListener("click", () => {
  showStatus("");
  setSignupMode(true);
});

form.addEventListener("submit", async event => {
  event.preventDefault();
  showStatus("");
  if (signupMode && passwordEl.value !== confirmEl.value) {
    showStatus(
      await document.l10n.formatValue("funcomputer-accounts-mismatch"),
      true
    );
    return;
  }
  setBusy(
    true,
    signupMode
      ? "funcomputer-accounts-busy-signup"
      : "funcomputer-accounts-busy-signin"
  );
  let result = signupMode
    ? await window.funcomputerSignup(emailEl.value.trim(), passwordEl.value)
    : await window.funcomputerLogin(emailEl.value.trim(), passwordEl.value);
  setBusy(false);
  if (!result?.ok) {
    showStatus(
      result?.error ||
        (await document.l10n.formatValue("funcomputer-accounts-error-generic")),
      true
    );
    return;
  }
  if (result.needsConfirmation) {
    showStatus(
      await document.l10n.formatValue("funcomputer-accounts-created-confirm")
    );
    setSignupMode(false);
    return;
  }
  passwordEl.value = "";
  confirmEl.value = "";
  await refresh();
});

document.getElementById("logout").addEventListener("click", async () => {
  await window.funcomputerLogout();
  passwordEl.value = "";
  confirmEl.value = "";
  showStatus("");
  await refresh();
});

setSignupMode(false);
refresh();
