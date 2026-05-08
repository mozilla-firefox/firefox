/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  GeckoViewPrompter: "resource://gre/modules/GeckoViewPrompter.sys.mjs",
});

let l10n;

// Steps 3-5 of https://w3c.github.io/webauthn/#sctn-validating-relation-origin.
const kMaxLabels = 5;

function validateRelatedOrigins(origins, callerOrigin) {
  const labelsSeen = [];

  for (const originItem of origins) {
    let url;
    try {
      url = new URL(originItem);
    } catch {
      continue;
    }

    const host = url.hostname;
    if (!host) {
      continue;
    }

    let baseDomain;
    try {
      baseDomain = Services.eTLD.getBaseDomainFromHost(host);
    } catch {
      continue;
    }
    const dotPos = baseDomain.indexOf(".");
    const label = dotPos >= 0 ? baseDomain.slice(0, dotPos) : null;

    if (!label) {
      continue;
    }

    if (labelsSeen.length >= kMaxLabels && !labelsSeen.includes(label)) {
      continue;
    }

    if (url.origin === callerOrigin) {
      return true;
    }

    if (labelsSeen.length < kMaxLabels) {
      labelsSeen.push(label);
    }
  }

  return false;
}

// Step 2 of https://w3c.github.io/webauthn/#sctn-validating-relation-origin.
// Fetches https://{aRpId}/.well-known/webauthn, validates the response per the
// spec and the same-origin redirect requirement, and calls validateRelatedOrigins
// on the result. Returns true if the caller origin is authorized, false otherwise.
// Never throws; aborted requests return false.
async function fetchAndValidateRelatedOrigin(aRpId, aCallerOrigin, aSignal) {
  // Validate aRpId is a valid domain string: it must parse as a URL hostname
  // and must already be in its normalized ASCII form.
  let hostname;
  try {
    hostname = new URL(`https://${aRpId}`).hostname;
  } catch {
    return false;
  }
  if (hostname !== aRpId) {
    return false;
  }

  // "Requests to this well-known endpoint by WebAuthn Clients MUST be made without
  // credentials, without a referrer, and using the https: scheme. When
  // following redirects, WebAuthn Clients MUST explicitly require all redirects
  // to also use the https: scheme."
  let req;
  try {
    req = await new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open("GET", `https://${aRpId}/.well-known/webauthn`);
      xhr.timeout = 10000;
      aSignal?.addEventListener("abort", () => xhr.abort(), { once: true });
      xhr.addEventListener("load", () => resolve(xhr), { once: true });
      xhr.addEventListener("error", () => reject(new Error()), { once: true });
      xhr.addEventListener("abort", () => reject(new Error()), { once: true });
      xhr.addEventListener("timeout", () => reject(new Error()), {
        once: true,
      });
      xhr.send();
    });
  } catch {
    return false;
  }

  if (new URL(req.responseURL).protocol !== "https:") {
    return false;
  }
  for (const entry of req.channel.loadInfo.redirectChain) {
    if (entry.principal.URI.scheme !== "https") {
      return false;
    }
  }

  // Step 2.1: The fetch must have status code 200 and content type
  // application/json.
  if (req.status !== 200) {
    return false;
  }
  const contentType = req.getResponseHeader("content-type") ?? "";
  if (!contentType.includes("application/json")) {
    return false;
  }

  // Step 2.2: The body must be valid JSON.
  let body;
  try {
    body = JSON.parse(req.responseText);
  } catch {
    return false;
  }

  // Step 2.3: The origins member must be an array of strings.
  if (
    !Array.isArray(body?.origins) ||
    !body.origins.every(x => typeof x === "string")
  ) {
    return false;
  }

  // Steps 3-5: Validate the caller origin against the origins list.
  return validateRelatedOrigins(body.origins, aCallerOrigin);
}

export class WebAuthnRelatedOriginFetcher {
  _callback = null;
  _abortController = null;
  _currentPrompt = null;

  QueryInterface = ChromeUtils.generateQI(["nsIWebAuthnRelatedOriginFetcher"]);

  checkRelatedOriginRequest(
    aManager,
    aRpId,
    aIsCreate,
    aShowPrompt,
    aCallback
  ) {
    this.cancel();
    this._callback = aCallback;
    this._run(aManager, aRpId, aIsCreate, aShowPrompt);
  }

  async _run(aManager, aRpId, aIsCreate, aShowPrompt) {
    this._abortController = new AbortController();
    let ok;
    try {
      ok = await fetchAndValidateRelatedOrigin(
        aRpId,
        aManager.documentPrincipal.originNoSuffix,
        this._abortController.signal
      );
    } finally {
      this._abortController = null;
    }

    if (!this._callback) {
      return;
    }

    if (!ok) {
      this._reject();
      return;
    }

    if (!aShowPrompt) {
      this._resolve();
      return;
    }

    if (AppConstants.platform === "android") {
      this._showAndroidPrompt(aManager, aRpId, aIsCreate);
    } else {
      this._showDesktopPrompt(aManager, aRpId, aIsCreate);
    }
  }

  _showAndroidPrompt(aManager, aRpId, aIsCreate) {
    const prompter = new lazy.GeckoViewPrompter(aManager.browsingContext.top);
    this._currentPrompt = prompter;
    prompter
      .asyncShowPromptPromise({
        type: "webauthn-related-origin",
        origin: aManager.documentPrincipal.host,
        rpId: aRpId,
        isCreate: aIsCreate,
      })
      .then(result => {
        this._currentPrompt = null;
        if (!this._callback) {
          return;
        }
        if (result?.allow) {
          this._resolve();
        } else {
          this._userCancel();
        }
      });
  }

  _showDesktopPrompt(aManager, aRpId, aIsCreate) {
    if (!l10n) {
      l10n = new Localization(
        ["branding/brand.ftl", "browser/webauthnDialog.ftl"],
        true
      );
    }

    const headerId = aIsCreate
      ? "webauthn-related-origin-create-header"
      : "webauthn-related-origin-use-header";
    const message = l10n.formatValueSync(headerId, {
      origin: aManager.documentPrincipal.host,
      rpId: aRpId,
    });

    const [continueMsg, cancelMsg] = l10n.formatMessagesSync([
      { id: "webauthn-continue" },
      { id: "webauthn-cancel" },
    ]);

    const mainAction = {
      label: continueMsg.value,
      accessKey: continueMsg.attributes.find(a => a.name === "accesskey").value,
      callback: () => {
        this._currentPrompt = null;
        this._resolve();
      },
    };
    const secondaryActions = [
      {
        label: cancelMsg.value,
        accessKey: cancelMsg.attributes.find(a => a.name === "accesskey").value,
        callback: () => {
          this._currentPrompt = null;
          this._userCancel();
        },
      },
    ];

    const chromeWin = aManager.browsingContext.topChromeWindow;
    const options = {
      hideClose: true,
      persistent: true,
      eventCallback: event => {
        if (event === "removed") {
          this._currentPrompt = null;
          this._userCancel();
        }
      },
    };

    this._currentPrompt = chromeWin.PopupNotifications.show(
      aManager.browsingContext.top.embedderElement,
      "webauthn-prompt-related-origin-request",
      message,
      "webauthn-notification-icon",
      mainAction,
      secondaryActions,
      options
    );
  }

  _resolve() {
    const cb = this._callback;
    this._callback = null;
    cb?.resolved();
  }

  _reject() {
    const cb = this._callback;
    this._callback = null;
    cb?.rejected();
  }

  _userCancel() {
    const cb = this._callback;
    this._callback = null;
    cb?.userCancel();
  }

  cancel() {
    this._callback = null;

    const ac = this._abortController;
    this._abortController = null;
    ac?.abort();

    const prompt = this._currentPrompt;
    this._currentPrompt = null;
    if (AppConstants.platform === "android") {
      prompt?.dismiss();
    } else {
      prompt?.remove();
    }
  }
}
