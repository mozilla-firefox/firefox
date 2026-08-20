/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { ContextualIdentityService } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs"
);

const { containerOptions } = ChromeUtils.importESModule(
  "chrome://browser/content/usercontext/container-select.mjs",
  { global: "current" }
);

let gSiteInput;
let gSiteError;
let gContainerSelect;

/**
 * The host the given input is about, or null when it cannot be associated to a
 * container. A pasted http or https URL is accepted and reduced to its host.
 *
 * @param {string} value
 * @returns {?string}
 */
function siteFromInput(value) {
  let site = value.trim();
  if (site.includes("://")) {
    let uri;
    try {
      uri = Services.io.newURI(site);
    } catch (e) {
      return null;
    }
    if (uri.scheme != "http" && uri.scheme != "https") {
      return null;
    }
    site = uri.host;
  }
  return ContextualIdentityService.normalizeSite(site);
}

function setSiteError(l10nId) {
  if (!l10nId && !gSiteError.hasAttribute("data-l10n-id")) {
    return;
  }

  gSiteInput.toggleAttribute("invalid", !!l10nId);
  if (l10nId) {
    gSiteInput.inputEl.setAttribute("aria-invalid", "true");
    document.l10n.setAttributes(gSiteError, l10nId);
  } else {
    gSiteInput.inputEl.removeAttribute("aria-invalid");
    gSiteError.removeAttribute("data-l10n-id");
    gSiteError.textContent = "";
  }
  window.resizeDialog?.();
}

function buildForm() {
  gSiteInput = document.createElement("moz-input-text");
  document.l10n.setAttributes(gSiteInput, "containers-site-label");

  gSiteError = document.createElement("div");
  gSiteError.id = "siteContainerError";
  gSiteError.className = "site-container-error";
  gSiteError.setAttribute("role", "alert");

  let siteField = document.createElement("div");
  siteField.className = "site-container-field";
  siteField.append(gSiteInput, gSiteError);

  gContainerSelect = document.createElement("container-select");
  document.l10n.setAttributes(
    gContainerSelect,
    "containers-site-container-label"
  );
  gContainerSelect.append(
    ...containerOptions().map(attrs => {
      let option = document.createElement("moz-option");
      for (let [name, value] of Object.entries(attrs)) {
        option.setAttribute(name, value);
      }
      return option;
    })
  );

  document
    .getElementById("siteContainerForm")
    .append(siteField, gContainerSelect);
}

window.addEventListener("DOMContentLoaded", async () => {
  buildForm();

  let acceptButton = document.querySelector("dialog").getButton("accept");
  gSiteInput.addEventListener("input", () => {
    acceptButton.disabled = !gSiteInput.value.trim();
    setSiteError(null);
  });

  document.addEventListener("dialogaccept", event => {
    let site = siteFromInput(gSiteInput.value);
    if (!site) {
      event.preventDefault();
      setSiteError("containers-site-invalid-error");
      gSiteInput.focus();
      return;
    }

    if (ContextualIdentityService.getSiteAssociation(site)) {
      event.preventDefault();
      setSiteError("containers-site-duplicate-error");
      gSiteInput.focus();
      return;
    }

    ContextualIdentityService.setSiteAssociation(
      site,
      parseInt(gContainerSelect.value, 10)
    );
  });

  await gSiteInput.updateComplete;
  gSiteInput.focus();
});
