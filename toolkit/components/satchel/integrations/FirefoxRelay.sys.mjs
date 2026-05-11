/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  LoginHelper,
  OptInFeature,
  ParentAutocompleteOption,
} from "resource://gre/modules/LoginHelper.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { TelemetryUtils } from "resource://gre/modules/TelemetryUtils.sys.mjs";
import { showConfirmation } from "resource://gre/modules/FillHelpers.sys.mjs";

const lazy = {};

// Static configuration
const gConfig = (function () {
  const baseUrl = Services.prefs.getStringPref(
    "signon.firefoxRelay.base_url",
    undefined
  );
  return {
    scope: ["profile", "https://identity.mozilla.com/apps/relay"],
    addressesUrl: baseUrl + `relayaddresses/`,
    acceptTermsUrl: baseUrl + `terms-accepted-user/`,
    profilesUrl: baseUrl + `profiles/`,
    learnMoreURL: Services.urlFormatter.formatURLPref(
      "signon.firefoxRelay.learn_more_url"
    ),
    manageURL: Services.urlFormatter.formatURLPref(
      "signon.firefoxRelay.manage_url"
    ),
    relayFeaturePref: "signon.firefoxRelay.feature",
    termsOfServiceUrl: Services.urlFormatter.formatURLPref(
      "signon.firefoxRelay.terms_of_service_url"
    ),
    privacyPolicyUrl: Services.urlFormatter.formatURLPref(
      "signon.firefoxRelay.privacy_policy_url"
    ),
    allowListForFirstOfferPref: "signon.firefoxRelay.allowListForFirstOffer",
    denyListForFutureOffersPref: "signon.firefoxRelay.denyListForFutureOffers",
    allowListRemoteSettingsCollectionPref:
      "signon.firefoxRelay.allowListRemoteSettingsCollection",
    denyListRemoteSettingsCollectionPref:
      "signon.firefoxRelay.denyListRemoteSettingsCollection",
  };
})();

ChromeUtils.defineLazyGetter(lazy, "log", () =>
  LoginHelper.createLogger("FirefoxRelay")
);
ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () =>
  ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton()
);
ChromeUtils.defineLazyGetter(lazy, "fxAccountsCommon", () =>
  ChromeUtils.importESModule("resource://gre/modules/FxAccountsCommon.sys.mjs")
);
ChromeUtils.defineLazyGetter(lazy, "strings", function () {
  return new Localization([
    "branding/brand.ftl",
    "browser/firefoxRelay.ftl",
    "toolkit/branding/brandings.ftl",
  ]);
});
ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  RemoteSettingsClient:
    "resource://services-settings/RemoteSettingsClient.sys.mjs",
});

if (Services.appinfo.processType !== Services.appinfo.PROCESS_TYPE_DEFAULT) {
  throw new Error("FirefoxRelay.sys.mjs should only run in the parent process");
}

// Using 418 to avoid conflict with other standard http error code
const AUTH_TOKEN_ERROR_CODE = 418;

let gFlowId;
let gAllowListCollection;
let gDenyListCollection;

async function getRelayTokenAsync() {
  try {
    return await lazy.fxAccounts.getOAuthToken({ scope: gConfig.scope });
  } catch (e) {
    console.error(`There was an error getting the user's token: ${e.message}`);
    return undefined;
  }
}

async function hasFirefoxAccountAsync() {
  if (!lazy.fxAccounts.constructor.config.isProductionConfig()) {
    return false;
  }
  return lazy.fxAccounts.hasLocalSession();
}

async function fetchWithReauth(
  browser,
  createRequest,
  canGetFreshOAuthToken = true
) {
  const relayToken = await getRelayTokenAsync();
  if (!relayToken) {
    if (browser) {
      await showErrorAsync(browser, "firefox-relay-must-login-to-account");
    }
    return undefined;
  }

  const headers = new Headers({
    Authorization: `Bearer ${relayToken}`,
    Accept: "application/json",
    "Accept-Language": Services.locale.requestedLocales,
    "Content-Type": "application/json",
  });

  const request = createRequest(headers);
  const response = await fetch(request);

  if (canGetFreshOAuthToken && response.status == 401) {
    await lazy.fxAccounts.removeCachedOAuthToken({ token: relayToken });
    return fetchWithReauth(browser, createRequest, false);
  }
  return response;
}

async function getReusableMasksAsync(browser, _origin) {
  const response = await fetchWithReauth(
    browser,
    headers =>
      new Request(gConfig.addressesUrl, {
        method: "GET",
        headers,
      })
  );

  if (!response) {
    // fetchWithReauth only returns undefined if login / obtaining a token failed.
    // Otherwise, it will return a response object.
    return [undefined, AUTH_TOKEN_ERROR_CODE];
  }

  if (response.ok) {
    return [await response.json(), response.status];
  }

  lazy.log.error(
    `failed to find reusable Relay masks: ${response.status}:${response.statusText}`
  );
  await showErrorAsync(browser, "firefox-relay-get-reusable-masks-failed", {
    status: response.status,
  });

  return [undefined, response.status];
}

/**
 * Show localized notification.
 *
 * @param {*} browser
 * @param {*} messageId from browser/firefoxRelay.ftl
 * @param {object} messageArgs
 */
async function showErrorAsync(browser, messageId, messageArgs) {
  const { PopupNotifications } = browser.documentGlobal.wrappedJSObject;
  const [message] = await lazy.strings.formatValues([
    { id: messageId, args: messageArgs },
  ]);
  PopupNotifications.show(
    browser,
    "relay-integration-error",
    message,
    "password-notification-icon",
    null,
    null,
    {
      autofocus: true,
      removeOnDismissal: true,
      hideClose: true,
      popupIconURL: "chrome://browser/content/logos/relay.svg",
      learnMoreURL: gConfig.learnMoreURL,
    }
  );
}

function customizeNotificationHeader(notification) {
  if (!notification) {
    return;
  }
  const document = notification.owner.panel.ownerDocument;
  const description = document.querySelector(
    `description[popupid=${notification.id}]`
  );
  const headerTemplate = document.getElementById(
    "firefox-relay-header-with-domain-and-value-prop"
  );
  description.replaceChildren(headerTemplate.firstChild.cloneNode(true));
}

async function formatMessages(...ids) {
  for (let i in ids) {
    if (typeof ids[i] == "string") {
      ids[i] = { id: ids[i] };
    }
  }

  const messages = await lazy.strings.formatMessages(ids);
  return messages.map(message => {
    if (message.attributes) {
      return message.attributes.reduce(
        (result, { name, value }) => ({ ...result, [name]: value }),
        {}
      );
    }
    return message.value;
  });
}

function getPostpone(postponeStrings, feature) {
  return {
    label: postponeStrings.label,
    accessKey: postponeStrings.accesskey,
    dismiss: true,
    callback() {
      lazy.log.info(
        "user decided not to decide about Firefox Relay integration"
      );
      feature.markAsOffered();
      Glean.relayIntegration.postponedOptInPanel.record({ value: gFlowId });
    },
  };
}

function getDisableIntegration(disableStrings, feature) {
  return {
    label: disableStrings.label,
    accessKey: disableStrings.accesskey,
    dismiss: true,
    callback() {
      lazy.log.info("user opted out from Firefox Relay integration");
      feature.markAsDisabled();
      Glean.relayIntegration.disabledOptInPanel.record({ value: gFlowId });
    },
  };
}

async function showReusableMasksAsync(browser, origin, error) {
  const [reusableMasks, status] = await getReusableMasksAsync(browser, origin);
  if (!reusableMasks) {
    Glean.relayIntegration.shownReusePanel.record({
      value: gFlowId,
      error_code: status,
    });
    return null;
  }

  // Parse the mask count from the error message
  let maskCount = 50;
  if (error?.detail) {
    const match = error.detail.match(/(\d+)\s+(?:free\s+)?email\s+masks?/i);
    if (match) {
      maskCount = parseInt(match[1], 10);
    }
  }

  let fillUsername;
  const fillUsernamePromise = new Promise(resolve => (fillUsername = resolve));
  const [
    seeAllMasksStrings,
    dismissStrings,
    headerMessage,
    bodyMessage,
    selectLabelMessage,
  ] = await formatMessages(
    "firefox-relay-see-all-masks",
    "firefox-relay-dismiss",
    { id: "firefox-relay-reuse-masks-header", args: { count: maskCount } },
    "firefox-relay-reuse-masks-description-v2",
    "firefox-relay-reuse-masks-select-label"
  );
  const seeAllMasks = {
    label: seeAllMasksStrings.label,
    accessKey: seeAllMasksStrings.accesskey,
    dismiss: true,
    async callback() {
      Glean.relayIntegration.getUnlimitedMasksReusePanel.record({
        value: gFlowId,
      });
      browser.documentGlobal.openWebLinkIn(gConfig.manageURL, "tab");
    },
  };
  const dismiss = {
    label: dismissStrings.label,
    accessKey: dismissStrings.accesskey,
    dismiss: true,
    callback() {
      // Just dismiss the notification
    },
  };

  let notification;

  function getReusableMasksList() {
    return notification?.owner.panel.getElementsByClassName(
      "reusable-relay-masks"
    )[0];
  }

  function notificationShown() {
    if (!notification) {
      return;
    }

    // Set custom header with dynamic mask count
    const doc = notification.owner.panel.ownerDocument;
    const description = doc.querySelector(
      `description[popupid=${notification.id}]`
    );

    const headerDiv = doc.createElement("div");
    headerDiv.className = "relay-integration-header-variation";
    headerDiv.style.marginBottom = "0";
    const headerP = doc.createElement("p");
    headerP.textContent = headerMessage;
    headerP.style.marginBottom = "0";
    headerDiv.appendChild(headerP);
    description.replaceChildren(headerDiv);

    // Set body message with learn more link
    const errorMessageEl =
      notification.owner.panel.getElementsByClassName("error-message")[0];
    errorMessageEl.textContent = "";
    errorMessageEl.style.marginTop = "8px";
    const bodyP = doc.createElement("p");
    bodyP.style.marginTop = "0";
    bodyP.style.marginBottom = "0";

    // Parse the message and create link for the labeled part
    const parts = bodyMessage.split(/<label[^>]*>|<\/label>/);
    parts.forEach((part, index) => {
      if (index % 2 === 0) {
        bodyP.appendChild(doc.createTextNode(part));
      } else {
        const link = doc.createElement("a");
        link.href = gConfig.manageURL;
        link.className = "text-link";
        link.textContent = part;
        link.addEventListener("click", event => {
          event.preventDefault();
          browser.documentGlobal.openWebLinkIn(gConfig.manageURL, "tab");
        });
        bodyP.appendChild(link);
      }
    });

    errorMessageEl.appendChild(bodyP);

    // Add section label
    const selectLabel = doc.createElement("p");
    selectLabel.textContent = selectLabelMessage;
    selectLabel.style.fontWeight = "600";
    selectLabel.style.fontSize = "1.1em";
    selectLabel.style.marginTop = "24px";
    selectLabel.style.marginBottom = "8px";
    errorMessageEl.appendChild(selectLabel);

    // rebuild "reuse mask" buttons list
    const list = getReusableMasksList();
    list.innerHTML = "";

    const document = list.ownerDocument;
    const fragment = document.createDocumentFragment();
    reusableMasks
      .filter(mask => mask.enabled)
      .slice(0, 5)
      .forEach(mask => {
        const button = document.createElement("button");

        const maskDescription = document.createElement("span");
        const raw = mask.description || mask.generated_for || mask.used_on;
        maskDescription.textContent = URL.parse(raw)?.hostname ?? raw ?? "";
        button.appendChild(maskDescription);

        const maskFullAddress = document.createElement("span");
        maskFullAddress.textContent = mask.full_address;
        button.appendChild(maskFullAddress);

        button.addEventListener(
          "click",
          () => {
            notification.remove();
            lazy.log.info("Reusing Relay mask");
            fillUsername(mask.full_address);
            showConfirmation(
              browser,
              "confirmation-hint-firefox-relay-mask-reused"
            );
            Glean.relayIntegration.reuseMaskReusePanel.record({
              value: gFlowId,
            });
          },
          { once: true }
        );
        fragment.appendChild(button);
      });
    list.appendChild(fragment);

    // Style buttons to be the same size and aligned right
    const panel = notification.owner.panel;
    const buttonContainer = panel.querySelector(
      ".popup-notification-footer-container"
    );
    const mainAction = panel.querySelector(
      ".popup-notification-primary-button"
    );
    const secondaryActions = panel.querySelectorAll(
      ".popup-notification-secondary-button"
    );

    if (buttonContainer) {
      buttonContainer.style.justifyContent = "flex-end";
      buttonContainer.style.gap = "8px";
    }

    if (mainAction) {
      mainAction.style.flex = "0 0 auto";
      mainAction.style.minWidth = "120px";
    }

    if (secondaryActions.length) {
      secondaryActions.forEach(button => {
        button.style.flex = "0 0 auto";
        button.style.minWidth = "120px";
      });
    }
  }

  function notificationRemoved() {
    const list = getReusableMasksList();
    list.innerHTML = "";
  }

  function onNotificationEvent(event) {
    switch (event) {
      case "removed":
        notificationRemoved();
        break;
      case "shown":
        notificationShown();
        Glean.relayIntegration.shownReusePanel.record({
          value: gFlowId,
          error_code: 0,
        });
        break;
    }
  }

  const { PopupNotifications } = browser.documentGlobal.wrappedJSObject;
  notification = PopupNotifications.show(
    browser,
    "relay-integration-reuse-masks",
    "", // content is provided after popup shown
    "password-notification-icon",
    seeAllMasks,
    [dismiss],
    {
      autofocus: true,
      removeOnDismissal: true,
      hideClose: true,
      persistent: true,
      eventCallback: onNotificationEvent,
    }
  );

  return fillUsernamePromise;
}

async function generateUsernameAsync(browser, origin) {
  const body = JSON.stringify({
    enabled: true,
    description: origin.substr(0, 64),
    generated_for: origin.substr(0, 255),
    used_on: origin,
  });

  const response = await fetchWithReauth(
    browser,
    headers =>
      new Request(gConfig.addressesUrl, {
        method: "POST",
        headers,
        body,
      })
  );

  if (!response) {
    Glean.relayIntegration.shownFillUsername.record({
      value: gFlowId,
      error_code: AUTH_TOKEN_ERROR_CODE,
    });
    return undefined;
  }

  if (response.ok) {
    lazy.log.info(`generated Relay mask`);
    const result = await response.json();
    showConfirmation(browser, "confirmation-hint-firefox-relay-mask-created");
    return result.full_address;
  }

  if (response.status == 403) {
    const error = await response.json();
    if (error?.error_code == "free_tier_limit") {
      Glean.relayIntegration.shownFillUsername.record({
        value: gFlowId,
        error_code: error.error_code,
      });
      return showReusableMasksAsync(browser, origin, error);
    }
  }

  lazy.log.error(
    `failed to generate Relay mask: ${response.status}:${response.statusText}`
  );

  await showErrorAsync(browser, "firefox-relay-mask-generation-failed", {
    status: response.status,
  });

  Glean.relayIntegration.shownReusePanel.record({
    value: gFlowId,
    error_code: response.status,
  });

  return undefined;
}

function isSignup(scenarioName) {
  return scenarioName == "SignUpFormScenario";
}

// Helper to load/cache RemoteSettings collections
async function getListCollection({
  cache,
  setCache,
  collectionPref,
  defaultCollection,
}) {
  if (!cache()) {
    const collectionName = Services.prefs.getStringPref(
      gConfig[collectionPref],
      defaultCollection
    );
    try {
      const list = await lazy.RemoteSettings(collectionName).get();
      setCache(list);
      lazy.RemoteSettings(collectionName).on("sync", () => {
        setCache(null);
      });
    } catch (ex) {
      if (ex instanceof lazy.RemoteSettingsClient.UnknownCollectionError) {
        lazy.log.warn(
          "Could not get Remote Settings collection.",
          collectionPref,
          ex
        );
      }
      throw ex;
    }
  }
  return cache();
}

/**
 * Checks if the origin matches a record in the list according to Relay rules:
 * using flexible normalization and PSL via Services.uriFixup.
  +---------------------------+-----------------------------------+--------+
  |        list               |           origin                  | Result |
  +---------------------------+-----------------------------------+--------+
  | google.com                | https://google.com                | True   |
  | google.com                | https://www.google.com            | True   |
  | www.google.com            | https://www.google.com            | True   |
  | google.com.ar             | https://accounts.google.com.ar    | True   |
  | google.com.ar             | https://google.com                | False  |
  | google.com                | https://google.com.ar             | False  |
  | mozilla.org               | https://vpn.mozilla.org           | True   |
  | vpn.mozilla.org           | https://vpn.mozilla.org           | True   |
  | substack.com              | https://hunterharris.substack.com | True   |
  | hunterharris.substack.com | https://hunterharris.substack.com | True   |
  | hunterharris.substack.com | https://other.substack.com        | False  |
  | example.co.uk             | https://foo.example.co.uk         | True   |
  | localhost                 | http://localhost                  | True   |
  | google.com.ar             | https://mail.google.com.br        | False  |
  +---------------------------+-----------------------------------+--------+
 *
 * Note: Cross-TLD matching (e.g., google.com matching google.com.ar) requires
 * explicit list entries or Related Realms integration. See bug 1996332.
 *
 * @param {Array} list   Array of {domain: ...} records. Each domain is a string.
 * @param {string} origin Origin URL (e.g., https://www.google.com.ar).
 * @returns {boolean}
 */
function isOriginInList(list, origin) {
  let host;
  try {
    // PSL-aware, normalized results via uriFixup
    const { fixedURI } = Services.uriFixup.getFixupURIInfo(origin);
    if (!fixedURI) {
      return false;
    }
    host = fixedURI.host;
  } catch {
    return false;
  }

  // 1. Exact host match (e.g. 'www.foo.com' in list)
  if (list.some(record => record.domain === host)) {
    return true;
  }

  // 2. PSL-aware subdomain/root match
  if (
    list.some(record => {
      try {
        return Services.eTLD.hasRootDomain(host, record.domain);
      } catch {
        return false;
      }
    })
  ) {
    return true;
  }

  return false;
}

async function shouldNotShowRelay(origin) {
  const denyListForFutureOffers = Services.prefs.getBoolPref(
    gConfig.denyListForFutureOffersPref,
    true
  );
  if (!denyListForFutureOffers) {
    return false;
  }
  if (!origin) {
    return true;
  }
  const list = await getListCollection({
    cache: () => gDenyListCollection,
    setCache: v => {
      gDenyListCollection = v;
    },
    collectionPref: "denyListRemoteSettingsCollectionPref",
    defaultCollection: "fxrelay-denylist",
  });
  return isOriginInList(list, origin);
}

async function shouldShowRelay(origin) {
  const allowListForFirstOffer = Services.prefs.getBoolPref(
    gConfig.allowListForFirstOfferPref,
    true
  );
  if (!allowListForFirstOffer) {
    return true;
  }
  if (!origin) {
    return false;
  }
  const list = await getListCollection({
    cache: () => gAllowListCollection,
    setCache: v => {
      gAllowListCollection = v;
    },
    collectionPref: "allowListRemoteSettingsCollectionPref",
    defaultCollection: "fxrelay-allowlist",
  });
  return isOriginInList(list, origin);
}

class RelayOffered {
  async *autocompleteItemsAsync(origin, scenarioName, hasInput) {
    const originOnDenyList = await shouldNotShowRelay(origin);
    if (originOnDenyList) {
      return;
    }
    const relayShouldShow = await shouldShowRelay(origin);
    const relayFeaturePrefUnlocked = !Services.prefs.prefIsLocked(
      gConfig.relayFeaturePref
    );
    if (
      !hasInput &&
      isSignup(scenarioName) &&
      relayFeaturePrefUnlocked &&
      relayShouldShow
    ) {
      const [title, subtitle] = await formatMessages(
        "firefox-relay-opt-in-title-b",
        "firefox-relay-opt-in-subtitle-b"
      );
      yield new ParentAutocompleteOption(
        "chrome://browser/content/asrouter/assets/glyph-mail-mask-16.svg",
        title,
        subtitle,
        "PasswordManager:offerRelayIntegration",
        {
          telemetry: {
            flowId: gFlowId,
            scenarioName,
          },
        }
      );
      Glean.relayIntegration.shownOfferRelay.record({
        value: gFlowId,
        scenario: scenarioName,
      });
    }
  }

  async notifyServerTermsAcceptedAsync(browser) {
    const response = await fetchWithReauth(
      browser,
      headers =>
        new Request(gConfig.acceptTermsUrl, {
          method: "POST",
          headers,
        })
    );

    if (!response?.ok) {
      lazy.log.error(
        `failed to notify server that terms are accepted : ${response?.status}:${response?.statusText}`
      );

      let error;
      try {
        error = await response?.json();
      } catch {}
      await showErrorAsync(browser, "firefox-relay-mask-generation-failed", {
        status: error?.detail || response.status,
      });
      return false;
    }

    return true;
  }

  async offerRelayIntegration(feature, browser, origin) {
    const fxaUser = await lazy.fxAccounts.getSignedInUser();
    if (!fxaUser) {
      return this.offerRelayIntegrationToSignedOutUser(
        feature,
        browser,
        origin
      );
    }
    return this.offerRelayIntegrationToFxAUser(
      feature,
      browser,
      origin,
      fxaUser
    );
  }

  async offerRelayIntegrationToSignedOutUser(feature, browser, origin) {
    const { PopupNotifications } = browser.documentGlobal.wrappedJSObject;
    let fillUsername;
    const fillUsernamePromise = new Promise(
      resolve => (fillUsername = resolve)
    );
    const [enableStrings, disableStrings, postponeStrings] =
      await formatMessages(
        "firefox-relay-and-fxa-opt-in-confirmation-enable-button-with-domain-and-value-prop",
        "firefox-relay-and-fxa-opt-in-confirmation-disable",
        "firefox-relay-and-fxa-opt-in-confirmation-postpone"
      );
    const enableIntegration = {
      label: enableStrings.label,
      accessKey: enableStrings.accesskey,
      dismiss: true,
      callback: async () => {
        lazy.log.info(
          "user opted in to Mozilla account and Firefox Relay integration"
        );
        // Capture the flowId here since async operations might take some time to resolve
        // and by then gFlowId might have another value
        const flowId = gFlowId;
        Glean.relayIntegration.enabledOptInPanel.record({ value: flowId });

        // Capture the selected tab panel ID so we can come back to it after the
        // user finishes FXA sign-in
        const tabPanelId =
          browser.documentGlobal.gBrowser.selectedTab.linkedPanel;

        // TODO: add some visual treatment to the tab and/or the form field to
        // indicate to the user that they need to complete sign-in to receive a
        // mask

        // Add an observer for ONVERIFIED_NOTIFICATION
        // to detect if a new FxA user verifies their email during sign-up,
        // and add an observer for ONLOGIN_NOTIFICATION
        // to detect if an existing FxA user logs in.
        const notificationsToObserve = [
          lazy.fxAccountsCommon.ONVERIFIED_NOTIFICATION,
          lazy.fxAccountsCommon.ONLOGIN_NOTIFICATION,
        ];
        const obs = async (_subject, topic) => {
          // When a user first signs up for FxA, Firefox receives an
          // ONLOGIN_NOTIFICATION *before* the user verifies their email
          // address. We can't forward any Relay emails until they verify their
          // email address, so we shouldn't call notifyServerTermsAcceptedAsync.
          // So, ignore login notifications for unverified users.
          if (topic == lazy.fxAccountsCommon.ONLOGIN_NOTIFICATION) {
            const fxaUser = await lazy.fxAccounts.getSignedInUser();
            if (!fxaUser || !fxaUser.verified) {
              return;
            }
          }
          // Remove the observers to prevent them from running again
          for (const observedNotification of notificationsToObserve) {
            Services.obs.removeObserver(obs, observedNotification);
          }

          // Go back to the tab with the form that started the FXA sign-in flow
          const tabToFocus = Array.from(
            browser.documentGlobal.gBrowser.tabs
          ).find(tab => tab.linkedPanel === tabPanelId);
          if (!tabToFocus) {
            // If the tab has been closed, return
            // TODO: figure out the real UX here?
            return;
          }

          // TODO: Update the visual treatment to the form field to indicate to
          // the user that we are hiding their email address.

          browser.documentGlobal.gBrowser.selectedTab = tabToFocus;

          // Create the relay user, mark feature enabled, fill in the username
          // field with a mask
          // FIXME: If the Relay server user record is corrupted (See MPP-3512),
          // notifyServerTermsAcceptedAsync receives a 500 error from Relay
          // server. But we can't use fxAccounts.listAttachedOAuthClients to
          // detect if the user already has Desktop Relay, because Desktop
          // Relay does not show up as an OAuth client
          if (await this.notifyServerTermsAcceptedAsync(browser)) {
            feature.markAsEnabled();
            fillUsername(await generateUsernameAsync(browser, origin));
            Glean.relayIntegration.placedEmailMask.record({ value: gFlowId });
          }
        };
        for (const notificationToObserve of notificationsToObserve) {
          Services.obs.addObserver(obs, notificationToObserve);
        }

        // Open tab to sign up for FxA and Relay
        const fxaUrl =
          await lazy.fxAccounts.constructor.config.promiseConnectAccountURI(
            "relay_integration",
            {
              service: "relay",
              utm_source: "relay-integration",
              utm_medium: "firefox-desktop",
            }
          );
        browser.documentGlobal.openWebLinkIn(fxaUrl, "tab");
      },
    };
    const postpone = getPostpone(postponeStrings, feature);
    const disableIntegration = getDisableIntegration(disableStrings, feature);
    let notification;
    feature.markAsOffered();
    notification = PopupNotifications.show(
      browser,
      "fxa-and-relay-integration-offer-with-domain-and-value-prop",
      "", // content is provided after popup shown
      "password-notification-icon",
      enableIntegration,
      [postpone, disableIntegration],
      {
        autofocus: true,
        removeOnDismissal: true,
        hideClose: true,
        eventCallback: event => {
          switch (event) {
            case "shown": {
              const document = notification.owner.panel.ownerDocument;
              customizeNotificationHeader(notification);
              document.querySelector(
                '[data-l10n-name="firefox-relay-learn-more-url"]'
              ).href = gConfig.learnMoreURL;
              const baseDomain = Services.eTLD.getBaseDomain(
                Services.io.newURI(origin)
              );
              document.querySelector(
                '[data-l10n-name="firefox-fxa-and-relay-offer-domain"]'
              ).textContent = baseDomain;
              const tosLink = document.querySelector(
                ".firefox-fxa-and-relay-offer-tos-url"
              );
              if (tosLink) {
                tosLink.href = gConfig.termsOfServiceUrl;
              }
              const privacyPolicyLink = document.querySelector(
                ".firefox-fxa-and-relay-offer-privacy-url"
              );
              if (privacyPolicyLink) {
                privacyPolicyLink.href = gConfig.privacyPolicyUrl;
              }
              Glean.relayIntegration.shownOptInPanel.record({ value: gFlowId });
              break;
            }
          }
        },
      }
    );
    return fillUsernamePromise;
  }

  async offerRelayIntegrationToFxAUser(feature, browser, origin, fxaUser) {
    const { PopupNotifications } = browser.documentGlobal.wrappedJSObject;
    let fillUsername;
    const fillUsernamePromise = new Promise(
      resolve => (fillUsername = resolve)
    );
    const [enableStrings, disableStrings, postponeStrings] =
      await formatMessages(
        "firefox-relay-opt-in-confirmation-enable-button",
        "firefox-relay-opt-in-confirmation-disable",
        "firefox-relay-opt-in-confirmation-postpone"
      );
    const enableIntegration = {
      label: enableStrings.label,
      accessKey: enableStrings.accesskey,
      dismiss: true,
      callback: async () => {
        lazy.log.info("user opted in to Firefox Relay integration");
        // Capture the flowId here since async operations might take some time to resolve
        // and by then gFlowId might have another value
        const flowId = gFlowId;
        Glean.relayIntegration.enabledOptInPanel.record({ value: flowId });

        if (await this.notifyServerTermsAcceptedAsync(browser)) {
          feature.markAsEnabled();
          fillUsername(await generateUsernameAsync(browser, origin));
          Glean.relayIntegration.placedEmailMask.record({ value: gFlowId });
        }
      },
    };
    const postpone = getPostpone(postponeStrings, feature);
    const disableIntegration = getDisableIntegration(disableStrings, feature);
    let notification;
    feature.markAsOffered();
    notification = PopupNotifications.show(
      browser,
      "relay-integration-offer",
      "", // content is provided after popup shown
      "password-notification-icon",
      enableIntegration,
      [postpone, disableIntegration],
      {
        autofocus: true,
        removeOnDismissal: true,
        hideClose: false,
        eventCallback: event => {
          switch (event) {
            case "shown": {
              const document = notification.owner.panel.ownerDocument;
              customizeNotificationHeader(notification);
              const learnMore = document.querySelector(
                '[data-l10n-name="firefox-relay-learn-more-url"]'
              );
              if (learnMore) {
                learnMore.href = gConfig.learnMoreURL;
              }
              const baseDomain = Services.eTLD.getBaseDomain(
                Services.io.newURI(origin)
              );
              document.querySelector(
                '[data-l10n-name="firefox-fxa-and-relay-offer-domain"]'
              ).textContent = baseDomain;
              const tosLink = document.querySelector(
                ".firefox-fxa-and-relay-offer-tos-url"
              );
              if (tosLink) {
                tosLink.href = gConfig.termsOfServiceUrl;
              }
              const privacyLink = document.querySelector(
                ".firefox-fxa-and-relay-offer-privacy-url"
              );
              if (privacyLink) {
                privacyLink.href = gConfig.privacyPolicyUrl;
              }
              document.l10n.setAttributes(
                document
                  .querySelector(
                    `popupnotification[id=${notification.id}-notification] popupnotificationcontent`
                  )
                  .querySelector(
                    "[id=firefox-relay-offer-what-relay-provides]"
                  ),
                "firefox-relay-offer-what-relay-provides",
                {
                  useremail: fxaUser.email,
                }
              );
              Glean.relayIntegration.shownOptInPanel.record({ value: gFlowId });
              break;
            }
          }
        },
      }
    );
    getRelayTokenAsync();
    return fillUsernamePromise;
  }
}

class RelayEnabled {
  async *autocompleteItemsAsync(origin, scenarioName, hasInput) {
    const originOnDenyList = await shouldNotShowRelay(origin);
    if (!hasInput && isSignup(scenarioName) && !originOnDenyList) {
      if (AppConstants.MOZ_GECKOVIEW) {
        // GeckoView manages FxA and rendering details.
        yield new ParentAutocompleteOption(
          null,
          "",
          "",
          "PasswordManager:firefoxRelay",
          { telemetry: { flowId: gFlowId } }
        );
      } else {
        const hasFxA = await hasFirefoxAccountAsync();
        const [title] = await formatMessages("firefox-relay-use-mask-title-1");
        yield new ParentAutocompleteOption(
          "chrome://browser/content/asrouter/assets/glyph-mail-mask-16.svg",
          title,
          "", // no subtitle when enabled
          hasFxA
            ? "PasswordManager:generateRelayUsername"
            : "PasswordManager:offerRelayIntegration",
          { telemetry: { flowId: gFlowId } }
        );
      }
      Glean.relayIntegration.shownFillUsername.record({
        value: gFlowId,
        error_code: 0,
      });
    }
  }

  async generateUsername(browser, origin) {
    Glean.relayIntegration.placedEmailMask.record({ value: gFlowId });
    return generateUsernameAsync(browser, origin);
  }
}

class RelayDisabled {}

class RelayFeature extends OptInFeature {
  constructor() {
    super(RelayOffered, RelayEnabled, RelayDisabled, gConfig.relayFeaturePref);
    // Update the config when the signon.firefoxRelay.base_url pref is changed.
    // This is added mainly for tests.
    Services.prefs.addObserver(
      "signon.firefoxRelay.base_url",
      this.updateConfig
    );
  }

  get learnMoreUrl() {
    return gConfig.learnMoreURL;
  }

  updateConfig() {
    const newBaseUrl = Services.prefs.getStringPref(
      "signon.firefoxRelay.base_url"
    );
    gConfig.addressesUrl = newBaseUrl + `relayaddresses/`;
    gConfig.profilesUrl = newBaseUrl + `profiles/`;
    gConfig.acceptTermsUrl = newBaseUrl + `terms-accepted-user/`;
  }

  async autocompleteItemsAsync({ origin, scenarioName, hasInput }) {
    const result = [];

    // Generate a flowID to unique identify a series of user action. FlowId
    // allows us to link users' interaction on different UI component (Ex. autocomplete, notification)
    // We can use flowID to build the Funnel Diagram
    // This value need to always be regenerated in the entry point of an user
    // action so we overwrite the previous one.
    gFlowId = TelemetryUtils.generateUUID();

    if (this.implementation.autocompleteItemsAsync) {
      for await (const item of this.implementation.autocompleteItemsAsync(
        origin,
        scenarioName,
        hasInput
      )) {
        result.push(item);
      }
    }

    return result;
  }

  async generateUsername(browser, origin) {
    return this.implementation.generateUsername?.(browser, origin);
  }

  async offerRelayIntegration(browser, origin) {
    return this.implementation.offerRelayIntegration?.(this, browser, origin);
  }

  async getRelayProfileInfo() {
    if (!lazy.fxAccounts.constructor.config.isProductionConfig()) {
      return null;
    }

    const hasSession = await lazy.fxAccounts.hasLocalSession();
    if (!hasSession) {
      return null;
    }

    try {
      const token = await getRelayTokenAsync();
      if (!token) {
        return null;
      }

      const profileResponse = await fetch(gConfig.profilesUrl, {
        method: "GET",
        headers: {
          Authorization: `Bearer ${token}`,
          Accept: "application/json",
        },
      });

      if (!profileResponse.ok) {
        return null;
      }

      const profiles = await profileResponse.json();
      const profile =
        Array.isArray(profiles) && profiles.length ? profiles[0] : profiles;

      const masksResponse = await fetch(gConfig.addressesUrl, {
        method: "GET",
        headers: {
          Authorization: `Bearer ${token}`,
          Accept: "application/json",
        },
      });

      let masksCount = 0;
      if (masksResponse.ok) {
        const masks = await masksResponse.json();
        masksCount = Array.isArray(masks) ? masks.length : 0;
      }

      return {
        has_premium: profile?.has_premium || false,
        has_phone: profile?.has_phone || false,
        has_vpn: profile?.has_vpn || false,
        masksCount,
      };
    } catch (e) {
      console.error("Error fetching Relay profile:", e);
      return null;
    }
  }
}

export { isOriginInList };
export const FirefoxRelay = new RelayFeature();
