/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-env mozilla/remote-page */
/* eslint-disable import/no-unassigned-import */

import { NetErrorCard } from "chrome://global/content/net-error-card.mjs";
import {
  gIsCertError,
  gErrorCode,
  gHasSts,
  searchParams,
  getHostName,
  getSubjectAltNames,
  getFailedCertificatesAsPEMString,
  recordSecurityUITelemetry,
  getCSSClass,
} from "chrome://global/content/aboutNetErrorHelpers.mjs";

const formatter = new Intl.DateTimeFormat();

const HOST_NAME = getHostName();

const FELT_PRIVACY_REFRESH = RPMGetBoolPref(
  "security.certerrors.felt-privacy-v1",
  false
);

// Used to check if we have a specific localized message for an error.
const KNOWN_ERROR_TITLE_IDS = new Set([
  // Error titles:
  "connectionFailure-title",
  "deniedPortAccess-title",
  "dnsNotFound-title",
  "dns-not-found-trr-only-title2",
  "internet-connection-offline-title",
  "fileNotFound-title",
  "fileAccessDenied-title",
  "generic-title",
  "captivePortal-title",
  "malformedURI-title",
  "netInterrupt-title",
  "notCached-title",
  "netOffline-title",
  "contentEncodingError-title",
  "unsafeContentType-title",
  "netReset-title",
  "netTimeout-title",
  "httpErrorPage-title",
  "serverError-title",
  "unknownProtocolFound-title",
  "proxyConnectFailure-title",
  "proxyResolveFailure-title",
  "redirectLoop-title",
  "unknownSocketType-title",
  "nssFailure2-title",
  "csp-xfo-error-title",
  "corruptedContentErrorv2-title",
  "sslv3Used-title",
  "inadequateSecurityError-title",
  "blockedByPolicy-title",
  "blocked-by-corp-headers-title",
  "clockSkewError-title",
  "networkProtocolError-title",
  "nssBadCert-title",
  "nssBadCert-sts-title",
  "certerror-mitm-title",
  "general-body-title",
  "problem-with-this-site-title",
]);

/* The error message IDs from nsserror.ftl get processed into
 * aboutNetErrorCodes.js which is loaded before we are: */
/* global KNOWN_ERROR_MESSAGE_IDS */
const ERROR_MESSAGES_FTL = "toolkit/neterror/nsserrors.ftl";

const MDN_DOCS_HEADERS =
  "https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/";
const COOP_MDN_DOCS = MDN_DOCS_HEADERS + "Cross-Origin-Opener-Policy";
const COEP_MDN_DOCS = MDN_DOCS_HEADERS + "Cross-Origin-Embedder-Policy";
const HTTPS_UPGRADES_MDN_DOCS = "https://support.mozilla.org/kb/https-upgrades";

// If the location of the favicon changes, FAVICON_CERTERRORPAGE_URL and/or
// FAVICON_ERRORPAGE_URL in toolkit/components/places/nsFaviconService.idl
// should also be updated.
document.getElementById("favicon").href =
  gIsCertError || gErrorCode == "nssFailure2"
    ? "chrome://global/skin/icons/warning.svg"
    : "chrome://global/skin/icons/info.svg";

function getDescription() {
  return searchParams.get("d");
}

function isCaptive() {
  return searchParams.get("captive") == "true";
}

/**
 * We don't actually know what the MitM is called (since we don't
 * maintain a list), so we'll try and display the common name of the
 * root issuer to the user. In the worst case they are as clueless as
 * before, in the best case this gives them an actionable hint.
 * This may be revised in the future.
 */
function getMitmName(failedCertInfo) {
  return failedCertInfo.issuerCommonName;
}

function retryThis(buttonEl) {
  RPMSendAsyncMessage("Browser:EnableOnlineMode");
  buttonEl.disabled = true;
}

function showPrefChangeContainer() {
  const panel = document.getElementById("prefChangeContainer");
  panel.hidden = false;
  document.getElementById("netErrorButtonContainer").hidden = true;
  document
    .getElementById("prefResetButton")
    .addEventListener("click", function resetPreferences() {
      RPMSendAsyncMessage("Browser:ResetSSLPreferences");
    });
  setFocus("#prefResetButton", "beforeend");
}

function toggleCertErrorDebugInfoVisibility(shouldShow) {
  let debugInfo = document.getElementById("certificateErrorDebugInformation");
  let copyButton = document.getElementById("copyToClipboardTop");

  if (shouldShow === undefined) {
    shouldShow = debugInfo.hidden;
  }
  debugInfo.hidden = !shouldShow;
  if (shouldShow) {
    copyButton.scrollIntoView({ block: "start", behavior: "smooth" });
    copyButton.focus();
  }
}

function setupAdvancedButton() {
  // Get the hostname and add it to the panel
  var panel = document.getElementById("badCertAdvancedPanel");

  // Register click handler for the weakCryptoAdvancedPanel
  document
    .getElementById("advancedButton")
    .addEventListener("click", togglePanelVisibility);

  function togglePanelVisibility() {
    if (panel.hidden) {
      // Reveal
      revealAdvancedPanelSlowlyAsync();
    } else {
      // Hide
      panel.hidden = true;
    }
  }

  if (getCSSClass() == "expertBadCert") {
    revealAdvancedPanelSlowlyAsync();
  }
}

async function revealAdvancedPanelSlowlyAsync() {
  const badCertAdvancedPanel = document.getElementById("badCertAdvancedPanel");
  const exceptionDialogButton = document.getElementById(
    "exceptionDialogButton"
  );

  // Toggling the advanced panel must ensure that the debugging
  // information panel is hidden as well, since it's opened by the
  // error code link in the advanced panel.
  toggleCertErrorDebugInfoVisibility(false);

  // Reveal, but disabled (and grayed-out) for 3.0s.
  badCertAdvancedPanel.hidden = false;
  exceptionDialogButton.disabled = true;

  // -

  if (exceptionDialogButton.resetReveal) {
    exceptionDialogButton.resetReveal(); // Reset if previous is pending.
  }
  let wasReset = false;
  exceptionDialogButton.resetReveal = () => {
    wasReset = true;
  };

  // Wait for 10 frames to ensure that the warning text is rendered
  // and gets all the way to the screen for the user to read it.
  // This is only ~0.160s at 60Hz, so it's not too much extra time that we're
  // taking to ensure that we're caught up with rendering, on top of the
  // (by default) whole second(s) we're going to wait based on the
  // security.dialog_enable_delay pref.
  // The catching-up to rendering is the important part, not the
  // N-frame-delay here.
  for (let i = 0; i < 10; i++) {
    await new Promise(requestAnimationFrame);
  }

  // Wait another Nms (default: 1000) for the user to be very sure. (Sorry speed readers!)
  const securityDelayMs = RPMGetIntPref("security.dialog_enable_delay", 1000);
  await new Promise(go => setTimeout(go, securityDelayMs));

  if (wasReset) {
    return;
  }

  // Enable and un-gray-out.
  exceptionDialogButton.disabled = false;
}

function disallowCertOverridesIfNeeded() {
  // Disallow overrides if this is a Strict-Transport-Security
  // host and the cert is bad (STS Spec section 7.3) or if the
  // certerror is in a frame (bug 633691).
  const failedCertInfo = document.getFailedCertSecurityInfo();
  if (gHasSts || window != top || !failedCertInfo.errorIsOverridable) {
    document.getElementById("exceptionDialogButton").hidden = true;
  }
  if (gHasSts) {
    const stsExplanation = document.getElementById("badStsCertExplanation");
    document.l10n.setAttributes(
      stsExplanation,
      "certerror-what-should-i-do-bad-sts-cert-explanation",
      { hostname: HOST_NAME }
    );
    stsExplanation.hidden = false;

    document.l10n.setAttributes(
      document.getElementById("returnButton"),
      "neterror-return-to-previous-page-button"
    );
    document.l10n.setAttributes(
      document.getElementById("advancedPanelReturnButton"),
      "neterror-return-to-previous-page-button"
    );
  }
}

function recordTRREventTelemetry(
  warningPageType,
  trrMode,
  trrDomain,
  skipReason
) {
  RPMRecordGleanEvent("securityDohNeterror", "loadDohwarning", {
    value: warningPageType,
    mode: trrMode,
    provider_key: trrDomain,
    skip_reason: skipReason,
  });

  const netErrorButtonDiv = document.getElementById("netErrorButtonContainer");
  const buttons = netErrorButtonDiv.querySelectorAll("button");
  for (let b of buttons) {
    b.addEventListener("click", function (e) {
      let target = e.originalTarget;
      let telemetryId = target.dataset.telemetryId;
      RPMRecordGleanEvent(
        "securityDohNeterror",
        "click" +
          telemetryId
            .split("_")
            .map(word => word[0].toUpperCase() + word.slice(1))
            .join(""),
        {
          value: warningPageType,
          mode: trrMode,
          provider_key: trrDomain,
          skip_reason: skipReason,
        }
      );
    });
  }
}

function setResponseStatus(shortDesc) {
  let responseStatus;
  let responseStatusText;
  try {
    const netErrorInfo = document.getNetErrorInfo();
    responseStatus = netErrorInfo.responseStatus;
    responseStatusText = netErrorInfo.responseStatusText;
  } catch (ex) {
    return;
  }

  if (responseStatus >= 400) {
    let responseStatusLabel = document.createElement("p");
    responseStatusLabel.id = "response-status-label"; // id for testing
    document.l10n.setAttributes(
      responseStatusLabel,
      "neterror-response-status-code",
      {
        responsestatus: responseStatus,
        responsestatustext: responseStatusText ?? "",
      }
    );
    shortDesc.appendChild(responseStatusLabel);
  }
}

// Returns pageTitleId, bodyTitle, bodyTitleId, and longDesc as an object
function initTitleAndBodyIds(baseURL, isTRROnlyFailure) {
  let bodyTitle = document.querySelector(".title-text");
  let longDesc = document.getElementById("errorLongDesc");
  const tryAgain = document.getElementById("netErrorButtonContainer");
  tryAgain.hidden = false;
  const learnMore = document.getElementById("learnMoreContainer");
  const learnMoreLink = document.getElementById("learnMoreLink");
  learnMoreLink.setAttribute("href", baseURL + "connection-not-secure");

  let pageTitleId = "neterror-page-title";
  let bodyTitleId = gErrorCode + "-title";

  switch (gErrorCode) {
    case "basicHttpAuthDisabled":
      bodyTitleId = "general-body-title";
      tryAgain.hidden = true;
      break;
    case "blockedByPolicy":
      pageTitleId = "neterror-blocked-by-policy-page-title";
      document.body.classList.add("blocked");

      // Remove the "Try again" button from pages that don't need it.
      // For pages blocked by policy, trying again won't help.
      tryAgain.hidden = true;
      break;
    case "blockedByCOOP":
    case "blockedByCOEP": {
      bodyTitleId = "general-body-title";
      document.body.classList.add("blocked");
      tryAgain.hidden = true;
      break;
    }
    case "invalidHeaderValue": {
      bodyTitleId = "problem-with-this-site-title";
      tryAgain.hidden = true;
      break;
    }
    case "cspBlocked":
    case "xfoBlocked": {
      bodyTitleId = "csp-xfo-error-title";

      // Remove the "Try again" button for XFO and CSP violations,
      // since it's almost certainly useless. (Bug 553180)
      tryAgain.hidden = true;

      // Adding a button for opening websites blocked for CSP and XFO violations
      // in a new window. (Bug 1461195)
      document.getElementById("errorShortDesc").hidden = true;

      document.l10n.setAttributes(longDesc, "csp-xfo-blocked-long-desc", {
        hostname: HOST_NAME,
      });
      longDesc = null;

      // Add a learn more link
      learnMore.hidden = false;
      learnMoreLink.setAttribute("href", baseURL + "xframe-neterror-page");
      break;
    }

    case "dnsNotFound":
      pageTitleId = "neterror-dns-not-found-title";
      if (!isTRROnlyFailure) {
        RPMCheckAlternateHostAvailable();
      }

      break;
    case "inadequateSecurityError":
      // Remove the "Try again" button from pages that don't need it.
      // For HTTP/2 inadequate security, trying again won't help.
      tryAgain.hidden = true;
      break;

    case "malformedURI":
      pageTitleId = "neterror-malformed-uri-page-title";
      // Remove the "Try again" button from pages that don't need it.
      tryAgain.hidden = true;
      break;

    // TLS errors and non-overridable certificate errors (e.g. pinning
    // failures) are of type nssFailure2.
    case "nssFailure2": {
      learnMore.hidden = false;

      const netErrorInfo = document.getNetErrorInfo();
      void recordSecurityUITelemetry(
        "securityUiTlserror",
        "loadAbouttlserror",
        netErrorInfo
      );
      const errorCode = netErrorInfo.errorCodeString;
      switch (errorCode) {
        case "SSL_ERROR_UNSUPPORTED_VERSION":
        case "SSL_ERROR_PROTOCOL_VERSION_ALERT": {
          const tlsNotice = document.getElementById("tlsVersionNotice");
          tlsNotice.hidden = false;
          document.l10n.setAttributes(tlsNotice, "cert-error-old-tls-version");
        }
        // fallthrough

        case "SSL_ERROR_NO_CIPHERS_SUPPORTED":
        case "SSL_ERROR_NO_CYPHER_OVERLAP":
        case "SSL_ERROR_SSL_DISABLED":
          RPMAddMessageListener("HasChangedCertPrefs", msg => {
            if (msg.data.hasChangedCertPrefs) {
              // Configuration overrides might have caused this; offer to reset.
              showPrefChangeContainer();
            }
          });
          RPMSendAsyncMessage("GetChangedCertPrefs");
      }

      break;
    }

    case "sslv3Used":
      learnMore.hidden = false;
      document.body.className = "certerror";
      break;
  }

  return { pageTitleId, bodyTitle, bodyTitleId, longDesc };
}

function initPage() {
  // We show an offline support page in case of a system-wide error,
  // when a user cannot connect to the internet and access the SUMO website.
  // For example, clock error, which causes certerrors across the web or
  // a security software conflict where the user is unable to connect
  // to the internet.
  // The URL that prompts us to show an offline support page should have the following
  // format: "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/supportPageSlug",
  // so we can extract the support page slug.
  let baseURL = RPMGetFormatURLPref("app.support.baseURL");
  if (document.location.href.startsWith(baseURL)) {
    let supportPageSlug = document.location.pathname.split("/").pop();
    RPMSendAsyncMessage("DisplayOfflineSupportPage", {
      supportPageSlug,
    });
  }

  const className = getCSSClass();
  if (className) {
    document.body.classList.add(className);
  }

  const isTRROnlyFailure = gErrorCode == "dnsNotFound" && RPMIsTRROnlyFailure();
  let noConnectivity = gErrorCode == "dnsNotFound" && !RPMHasConnectivity();

  const docTitle = document.querySelector("title");
  const shortDesc = document.getElementById("errorShortDesc");

  if (gIsCertError) {
    const bodyTitle = document.querySelector(".title-text");
    const isStsError = window !== window.top || gHasSts;
    const errArgs = { hostname: HOST_NAME };
    if (isCaptive()) {
      document.l10n.setAttributes(
        docTitle,
        "neterror-captive-portal-page-title"
      );
      document.l10n.setAttributes(bodyTitle, "captivePortal-title");
      document.l10n.setAttributes(
        shortDesc,
        "neterror-captive-portal",
        errArgs
      );
      initPageCaptivePortal();
    } else {
      if (isStsError) {
        document.l10n.setAttributes(docTitle, "certerror-sts-page-title");
        document.l10n.setAttributes(bodyTitle, "nssBadCert-sts-title");
        document.l10n.setAttributes(shortDesc, "certerror-sts-intro", errArgs);
      } else {
        document.l10n.setAttributes(docTitle, "certerror-page-title");
        document.l10n.setAttributes(bodyTitle, "nssBadCert-title");
        document.l10n.setAttributes(shortDesc, "certerror-intro", errArgs);
      }
      initPageCertError();
    }

    initCertErrorPageActions();
    setTechnicalDetailsOnCertError();
    return;
  }

  document.body.classList.add("neterror");

  const tryAgain = document.getElementById("netErrorButtonContainer");
  tryAgain.hidden = false;
  const learnMoreLink = document.getElementById("learnMoreLink");
  learnMoreLink.setAttribute("href", baseURL + "connection-not-secure");
  let { pageTitleId, bodyTitle, bodyTitleId, longDesc } = initTitleAndBodyIds(
    baseURL,
    isTRROnlyFailure
  );

  // We can handle the offline page separately.
  if (noConnectivity) {
    pageTitleId = "neterror-dns-not-found-title";
    bodyTitleId = "internet-connection-offline-title";
  }

  // bodyTitle is set to null if it has already been set in initTitleAndBodyIds
  if (!KNOWN_ERROR_TITLE_IDS.has(bodyTitleId)) {
    console.error("No strings exist for error:", gErrorCode);
    bodyTitleId = "generic-title";
  }

  // The TRR errors may present options that direct users to settings only available on Firefox Desktop
  if (RPMIsFirefox()) {
    if (isTRROnlyFailure && !noConnectivity) {
      pageTitleId = "neterror-dns-not-found-title";
      document.l10n.setAttributes(docTitle, pageTitleId);
      if (bodyTitle) {
        bodyTitleId = "dnsNotFound-title";
        document.l10n.setAttributes(bodyTitle, bodyTitleId);
      }

      shortDesc.textContent = "";
      let skipReason = RPMGetTRRSkipReason();

      // enable buttons
      let trrExceptionButton = document.getElementById("trrExceptionButton");
      trrExceptionButton.addEventListener("click", () => {
        RPMSendQuery("Browser:AddTRRExcludedDomain", {
          hostname: HOST_NAME,
        }).then(() => {
          retryThis(trrExceptionButton);
        });
      });

      let isTrrServerError = true;
      if (RPMIsSiteSpecificTRRError()) {
        // Only show the exclude button if the failure is specific to this
        // domain. If the TRR server is inaccessible we don't want to allow
        // the user to add an exception just for this domain.
        trrExceptionButton.hidden = false;
        isTrrServerError = false;
      }
      let trrSettingsButton = document.getElementById("trrSettingsButton");
      trrSettingsButton.addEventListener("click", () => {
        RPMSendAsyncMessage("OpenTRRPreferences");
      });
      trrSettingsButton.hidden = false;
      let message = document.getElementById("trrOnlyMessage");
      document.l10n.setAttributes(
        message,
        "neterror-dns-not-found-trr-only-reason2",
        {
          hostname: HOST_NAME,
        }
      );

      let descriptionTag = "neterror-dns-not-found-trr-unknown-problem";
      let args = { trrDomain: RPMGetTRRDomain() };
      if (
        skipReason == "TRR_FAILED" ||
        skipReason == "TRR_CHANNEL_DNS_FAIL" ||
        skipReason == "TRR_UNKNOWN_CHANNEL_FAILURE" ||
        skipReason == "TRR_NET_REFUSED" ||
        skipReason == "TRR_NET_INTERRUPT" ||
        skipReason == "TRR_NET_INADEQ_SEQURITY"
      ) {
        descriptionTag = "neterror-dns-not-found-trr-only-could-not-connect";
      } else if (skipReason == "TRR_TIMEOUT") {
        descriptionTag = "neterror-dns-not-found-trr-only-timeout";
      } else if (
        skipReason == "TRR_NO_ANSWERS" ||
        skipReason == "TRR_NXDOMAIN" ||
        skipReason == "TRR_RCODE_FAIL"
      ) {
        descriptionTag = "neterror-dns-not-found-trr-unknown-host2";
      } else if (
        skipReason == "TRR_DECODE_FAILED" ||
        skipReason == "TRR_SERVER_RESPONSE_ERR"
      ) {
        descriptionTag = "neterror-dns-not-found-trr-server-problem";
      } else if (skipReason == "TRR_BAD_URL") {
        descriptionTag = "neterror-dns-not-found-bad-trr-url";
      } else if (skipReason == "TRR_SYSTEM_SLEEP_MODE") {
        descriptionTag = "neterror-dns-not-found-system-sleep";
      }

      let trrMode = RPMGetIntPref("network.trr.mode");
      recordTRREventTelemetry(
        "TRROnlyFailure",
        trrMode,
        args.trrDomain,
        skipReason
      );

      let description = document.getElementById("trrOnlyDescription");
      document.l10n.setAttributes(description, descriptionTag, args);

      const trrLearnMoreContainer = document.getElementById(
        "trrLearnMoreContainer"
      );
      trrLearnMoreContainer.hidden = false;
      let trrOnlyLearnMoreLink = document.getElementById(
        "trrOnlylearnMoreLink"
      );
      if (isTrrServerError) {
        // Go to DoH settings page
        trrOnlyLearnMoreLink.href = "about:preferences#privacy-doh";
        trrOnlyLearnMoreLink.addEventListener("click", event => {
          event.preventDefault();
          RPMSendAsyncMessage("OpenTRRPreferences");
          RPMRecordGleanEvent("securityDohNeterror", "clickSettingsButton", {
            value: "TRROnlyFailure",
            mode: trrMode,
            provider_key: args.trrDomain,
            skip_reason: skipReason,
          });
        });
      } else {
        // This will be replaced at a later point with a link to an offline support page
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1806257
        trrOnlyLearnMoreLink.href =
          RPMGetFormatURLPref("network.trr_ui.skip_reason_learn_more_url") +
          skipReason.toLowerCase().replaceAll("_", "-");
      }

      let div = document.getElementById("trrOnlyContainer");
      div.hidden = false;

      return;
    }
  }

  document.l10n.setAttributes(docTitle, pageTitleId);
  if (bodyTitle) {
    document.l10n.setAttributes(bodyTitle, bodyTitleId);
  }

  shortDesc.textContent = getDescription();
  setFocus("#netErrorButtonContainer > .try-again");

  if (longDesc) {
    const parts = getNetErrorDescParts(noConnectivity);
    setNetErrorMessageFromParts(longDesc, parts);
  }

  setResponseStatus(shortDesc);
  setNetErrorMessageFromCode();
}

/**
 * Builds HTML elements from `parts` and appends them to `parentElement`.
 *
 * @param {HTMLElement} parentElement
 * @param {Array<["li" | "p" | "span" | "a", string, Record<string, string> | undefined]>} parts
 */
function setNetErrorMessageFromParts(parentElement, parts) {
  let list = null;

  for (let [tag, l10nId, l10nArgsOrHref] of parts) {
    const elem = document.createElement(tag);
    elem.dataset.l10nId = l10nId;
    if (l10nArgsOrHref) {
      if (tag === "a") {
        elem.href = l10nArgsOrHref;
      } else {
        elem.dataset.l10nArgs = JSON.stringify(l10nArgsOrHref);
      }
    }

    if (tag === "li") {
      if (!list) {
        list = document.createElement("ul");
        parentElement.appendChild(list);
      }
      list.appendChild(elem);
    } else {
      if (list) {
        list = null;
      }
      parentElement.appendChild(elem);
    }
  }
}

/**
 * Returns an array of tuples determining the parts of an error message:
 * - HTML tag name
 * - l10n id
 * - l10n args (if the tag is not "a", optional)
 * - href (if the tag is "a", optional)
 *
 * @param {boolean} noConnectivity - if true, the browser has no active network interfaces
 * @returns { Array<["li" | "p" | "span" | "a", string, Record<string, string> | undefined]> }
 */
function getNetErrorDescParts(noConnectivity) {
  switch (gErrorCode) {
    case "connectionFailure":
    case "netInterrupt":
    case "netReset":
    case "netTimeout": {
      let errorTags = [
        ["li", "neterror-load-error-try-again"],
        ["li", "neterror-load-error-connection"],
        ["li", "neterror-load-error-firewall"],
      ];
      if (RPMShowOSXLocalNetworkPermissionWarning()) {
        errorTags.push(["li", "neterror-load-osx-permission"]);
      }
      return errorTags;
    }

    case "httpErrorPage": // 4xx
      return [["li", "neterror-http-error-page"]];
    case "serverError": // 5xx
      return [["li", "neterror-load-error-try-again"]];
    case "blockedByCOOP": {
      return [
        ["p", "certerror-blocked-by-corp-headers-description"],
        ["a", "certerror-coop-learn-more", COOP_MDN_DOCS],
      ];
    }
    case "blockedByCOEP": {
      return [
        ["p", "certerror-blocked-by-corp-headers-description"],
        ["a", "certerror-coep-learn-more", COEP_MDN_DOCS],
      ];
    }
    case "blockedByPolicy":
    case "deniedPortAccess":
    case "malformedURI":
      return [];

    case "captivePortal":
      return [["p", ""]];
    case "contentEncodingError":
      return [["li", "neterror-content-encoding-error"]];
    case "corruptedContentErrorv2":
      return [
        ["p", "neterror-corrupted-content-intro"],
        ["li", "neterror-corrupted-content-contact-website"],
      ];
    case "dnsNotFound":
      if (noConnectivity) {
        return [
          ["span", "neterror-dns-not-found-offline-hint-header"],
          ["li", "neterror-dns-not-found-offline-hint-different-device"],
          ["li", "neterror-dns-not-found-offline-hint-modem"],
          ["li", "neterror-dns-not-found-offline-hint-reconnect"],
        ];
      }
      return [
        ["span", "neterror-dns-not-found-hint-header"],
        ["li", "neterror-dns-not-found-hint-try-again"],
        ["li", "neterror-dns-not-found-hint-check-network"],
        ["li", "neterror-dns-not-found-hint-firewall"],
      ];
    case "fileAccessDenied":
      return [["li", "neterror-access-denied"]];
    case "fileNotFound":
      return [
        ["li", "neterror-file-not-found-filename"],
        ["li", "neterror-file-not-found-moved"],
      ];
    case "inadequateSecurityError":
      return [
        ["p", "neterror-inadequate-security-intro", { hostname: HOST_NAME }],
        ["p", "neterror-inadequate-security-code"],
      ];
    case "invalidHeaderValue": {
      return [["li", "neterror-http-error-page"]];
    }
    case "mitm": {
      const failedCertInfo = document.getFailedCertSecurityInfo();
      const errArgs = {
        hostname: HOST_NAME,
        mitm: getMitmName(failedCertInfo),
      };
      return [["span", "certerror-mitm", errArgs]];
    }
    case "netOffline":
      return [["li", "neterror-net-offline"]];
    case "networkProtocolError":
      return [
        ["p", "neterror-network-protocol-error-intro"],
        ["li", "neterror-network-protocol-error-contact-website"],
      ];
    case "notCached":
      return [
        ["p", "neterror-not-cached-intro"],
        ["li", "neterror-not-cached-sensitive"],
        ["li", "neterror-not-cached-try-again"],
      ];
    case "nssFailure2":
      return [
        ["li", "neterror-nss-failure-not-verified"],
        ["li", "neterror-nss-failure-contact-website"],
      ];
    case "proxyConnectFailure":
      return [
        ["li", "neterror-proxy-connect-failure-settings"],
        ["li", "neterror-proxy-connect-failure-contact-admin"],
      ];
    case "proxyResolveFailure":
      return [
        ["li", "neterror-proxy-resolve-failure-settings"],
        ["li", "neterror-proxy-resolve-failure-connection"],
        ["li", "neterror-proxy-resolve-failure-firewall"],
      ];
    case "redirectLoop":
      return [["li", "neterror-redirect-loop"]];
    case "sslv3Used":
      return [["span", "neterror-sslv3-used"]];
    case "unknownProtocolFound":
      return [["li", "neterror-unknown-protocol"]];
    case "unknownSocketType":
      return [
        ["li", "neterror-unknown-socket-type-psm-installed"],
        ["li", "neterror-unknown-socket-type-server-config"],
      ];
    case "unsafeContentType":
      return [["li", "neterror-unsafe-content-type"]];
    case "basicHttpAuthDisabled":
      return [
        ["li", "neterror-basic-http-auth", { hostname: HOST_NAME }],
        ["a", "neterror-learn-more-link", HTTPS_UPGRADES_MDN_DOCS],
      ];
    default:
      return [["p", "neterror-generic-error"]];
  }
}

function setNetErrorMessageFromCode() {
  let errorCode;
  try {
    errorCode = document.getNetErrorInfo().errorCodeString;
  } catch (ex) {
    return;
  }

  if (!errorCode) {
    return;
  }

  let errorMessage;
  if (errorCode) {
    const l10nId = errorCode.replace(/_/g, "-").toLowerCase();
    if (KNOWN_ERROR_MESSAGE_IDS.has(l10nId)) {
      const l10n = new Localization([ERROR_MESSAGES_FTL], true);
      errorMessage = l10n.formatValueSync(l10nId);
    }

    const shortDesc2 = document.getElementById("errorShortDesc2");
    document.l10n.setAttributes(shortDesc2, "cert-error-code-prefix", {
      error: errorCode,
    });
  } else {
    console.warn("This error page has no error code in its security info");
  }

  let hostname = HOST_NAME;
  const { port } = document.location;
  if (port && port != 443) {
    hostname += ":" + port;
  }

  const shortDesc = document.getElementById("errorShortDesc");
  document.l10n.setAttributes(shortDesc, "cert-error-ssl-connection-error", {
    errorMessage: errorMessage ?? errorCode ?? "",
    hostname,
  });
}

function initPageCaptivePortal() {
  document.body.className = "captiveportal";
  document.getElementById("returnButton").hidden = true;
  const openButton = document.getElementById("openPortalLoginPageButton");
  openButton.hidden = false;
  openButton.addEventListener("click", () => {
    RPMSendAsyncMessage("Browser:OpenCaptivePortalPage");
  });

  setFocus("#openPortalLoginPageButton");
  setupAdvancedButton();
  disallowCertOverridesIfNeeded();

  // When the portal is freed, an event is sent by the parent process
  // that we can pick up and attempt to reload the original page.
  RPMAddMessageListener("AboutNetErrorCaptivePortalFreed", () => {
    document.location.reload();
  });
}

function initPageCertError() {
  document.body.classList.add("certerror");

  setFocus("#returnButton");
  setupAdvancedButton();
  disallowCertOverridesIfNeeded();

  const hideAddExceptionButton = RPMGetBoolPref(
    "security.certerror.hideAddException",
    false
  );
  if (hideAddExceptionButton) {
    document.getElementById("exceptionDialogButton").hidden = true;
  }

  const els = document.querySelectorAll("[data-telemetry-id]");
  for (let el of els) {
    el.addEventListener("click", recordClickTelemetry);
  }

  const failedCertInfo = document.getFailedCertSecurityInfo();
  void recordSecurityUITelemetry(
    "securityUiCerterror",
    "loadAboutcerterror",
    failedCertInfo
  );

  setCertErrorDetails();
}

function recordClickTelemetry(e) {
  let target = e.originalTarget;
  let telemetryId = target.dataset.telemetryId;
  let failedCertInfo = document.getFailedCertSecurityInfo();
  void recordSecurityUITelemetry(
    "securityUiCerterror",
    "click" +
      telemetryId
        .split("_")
        .map(word => word[0].toUpperCase() + word.slice(1))
        .join(""),
    failedCertInfo
  );
}

function initCertErrorPageActions() {
  document.getElementById("certErrorAndCaptivePortalButtonContainer").hidden =
    false;
  document
    .getElementById("returnButton")
    .addEventListener("click", onReturnButtonClick);
  document
    .getElementById("advancedPanelReturnButton")
    .addEventListener("click", onReturnButtonClick);
  document
    .getElementById("copyToClipboardTop")
    .addEventListener("click", copyPEMToClipboard);
  document
    .getElementById("copyToClipboardBottom")
    .addEventListener("click", copyPEMToClipboard);
  document
    .getElementById("exceptionDialogButton")
    .addEventListener("click", addCertException);
}

function addCertException() {
  const isPermanent =
    !RPMIsWindowPrivate() &&
    RPMGetBoolPref("security.certerrors.permanentOverride");
  document.addCertException(!isPermanent).then(
    () => {
      location.reload();
    },
    () => {}
  );
}

function onReturnButtonClick() {
  RPMSendAsyncMessage("Browser:SSLErrorGoBack");
}

function copyPEMToClipboard() {
  const errorText = document.getElementById("certificateErrorText");
  navigator.clipboard.writeText(errorText.textContent);
}

function setCertErrorDetails() {
  // Check if the connection is being man-in-the-middled. When the parent
  // detects an intercepted connection, the page may be reloaded with a new
  // error code (MOZILLA_PKIX_ERROR_MITM_DETECTED).
  const failedCertInfo = document.getFailedCertSecurityInfo();
  const mitmPrimingEnabled = RPMGetBoolPref(
    "security.certerrors.mitm.priming.enabled"
  );
  if (
    mitmPrimingEnabled &&
    failedCertInfo.errorCodeString == "SEC_ERROR_UNKNOWN_ISSUER" &&
    // Only do this check for top-level failures.
    window.parent == window
  ) {
    RPMSendAsyncMessage("Browser:PrimeMitm");
  }

  document.body.setAttribute("code", failedCertInfo.errorCodeString);

  const learnMore = document.getElementById("learnMoreContainer");
  learnMore.hidden = false;
  const learnMoreLink = document.getElementById("learnMoreLink");
  const baseURL = RPMGetFormatURLPref("app.support.baseURL");
  learnMoreLink.href = baseURL + "connection-not-secure";

  const bodyTitle = document.querySelector(".title-text");
  const shortDesc = document.getElementById("errorShortDesc");
  const shortDesc2 = document.getElementById("errorShortDesc2");

  let whatToDoParts = null;

  switch (failedCertInfo.errorCodeString) {
    case "SSL_ERROR_BAD_CERT_DOMAIN":
      whatToDoParts = [
        ["p", "certerror-bad-cert-domain-what-can-you-do-about-it"],
      ];
      break;

    case "SEC_ERROR_OCSP_INVALID_SIGNING_CERT": // FIXME - this would have thrown?
      break;

    case "SEC_ERROR_UNKNOWN_ISSUER":
      whatToDoParts = [
        ["p", "certerror-unknown-issuer-what-can-you-do-about-it-website"],
        [
          "p",
          "certerror-unknown-issuer-what-can-you-do-about-it-contact-admin",
        ],
      ];
      break;

    // This error code currently only exists for the Symantec distrust
    // in Firefox 63, so we add copy explaining that to the user.
    // In case of future distrusts of that scale we might need to add
    // additional parameters that allow us to identify the affected party
    // without replicating the complex logic from certverifier code.
    case "MOZILLA_PKIX_ERROR_ADDITIONAL_POLICY_CONSTRAINT_FAILED": {
      document.l10n.setAttributes(
        shortDesc2,
        "cert-error-symantec-distrust-description",
        { hostname: HOST_NAME }
      );

      // FIXME - this does nothing
      const adminDesc = document.createElement("p");
      document.l10n.setAttributes(
        adminDesc,
        "cert-error-symantec-distrust-admin"
      );

      learnMoreLink.href = baseURL + "symantec-warning";
      break;
    }

    case "MOZILLA_PKIX_ERROR_MITM_DETECTED": {
      const autoEnabledEnterpriseRoots = RPMGetBoolPref(
        "security.enterprise_roots.auto-enabled",
        false
      );
      if (mitmPrimingEnabled && autoEnabledEnterpriseRoots) {
        RPMSendAsyncMessage("Browser:ResetEnterpriseRootsPref");
      }

      learnMoreLink.href = baseURL + "security-error";

      document.l10n.setAttributes(bodyTitle, "certerror-mitm-title");

      document.l10n.setAttributes(shortDesc, "certerror-mitm", {
        hostname: HOST_NAME,
        mitm: getMitmName(failedCertInfo),
      });

      const id3 = gHasSts
        ? "certerror-mitm-what-can-you-do-about-it-attack-sts"
        : "certerror-mitm-what-can-you-do-about-it-attack";
      whatToDoParts = [
        ["li", "certerror-mitm-what-can-you-do-about-it-antivirus"],
        ["li", "certerror-mitm-what-can-you-do-about-it-corporate"],
        ["li", id3, { mitm: getMitmName(failedCertInfo) }],
      ];
      break;
    }

    case "MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT":
      learnMoreLink.href = baseURL + "security-error";
      break;

    // In case the certificate expired we make sure the system clock
    // matches the remote-settings service (blocklist via Kinto) ping time
    // and is not before the build date.
    case "SEC_ERROR_EXPIRED_CERTIFICATE":
    case "SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE":
    case "MOZILLA_PKIX_ERROR_NOT_YET_VALID_CERTIFICATE":
    case "MOZILLA_PKIX_ERROR_NOT_YET_VALID_ISSUER_CERTIFICATE": {
      learnMoreLink.href = baseURL + "time-errors";

      // We check against the remote-settings server time first if available, because that allows us
      // to give the user an approximation of what the correct time is.
      const difference = RPMGetIntPref(
        "services.settings.clock_skew_seconds",
        0
      );
      const lastFetched =
        RPMGetIntPref("services.settings.last_update_seconds", 0) * 1000;

      // This is set to true later if the user's system clock is at fault for this error.
      let clockSkew = false;

      const now = Date.now();
      const certRange = {
        notBefore: failedCertInfo.certValidityRangeNotBefore,
        notAfter: failedCertInfo.certValidityRangeNotAfter,
      };
      const approximateDate = now - difference * 1000;
      // If the difference is more than a day, we last fetched the date in the last 5 days,
      // and adjusting the date per the interval would make the cert valid, warn the user:
      if (
        Math.abs(difference) > 60 * 60 * 24 &&
        now - lastFetched <= 60 * 60 * 24 * 5 * 1000 &&
        certRange.notBefore < approximateDate &&
        certRange.notAfter > approximateDate
      ) {
        clockSkew = true;
        // If there is no clock skew with Kinto servers, check against the build date.
        // (The Kinto ping could have happened when the time was still right, or not at all)
      } else {
        const appBuildID = RPMGetAppBuildID();
        const year = parseInt(appBuildID.substr(0, 4), 10);
        const month = parseInt(appBuildID.substr(4, 2), 10) - 1;
        const day = parseInt(appBuildID.substr(6, 2), 10);

        const buildDate = new Date(year, month, day);

        // We don't check the notBefore of the cert with the build date,
        // as it is of course almost certain that it is now later than the build date,
        // so we shouldn't exclude the possibility that the cert has become valid
        // since the build date.
        if (buildDate > now && new Date(certRange.notAfter) > buildDate) {
          clockSkew = true;
        }
      }

      if (clockSkew) {
        document.body.classList.add("clockSkewError");
        document.l10n.setAttributes(bodyTitle, "clockSkewError-title");
        document.l10n.setAttributes(shortDesc, "neterror-clock-skew-error", {
          hostname: HOST_NAME,
          now,
        });
        document.getElementById("returnButton").hidden = true;
        document.getElementById("certErrorTryAgainButton").hidden = false;
        document.getElementById("advancedButton").hidden = true;

        document.getElementById("advancedPanelReturnButton").hidden = true;
        document.getElementById("advancedPanelTryAgainButton").hidden = false;
        document.getElementById("exceptionDialogButton").hidden = true;
        break;
      }

      document.l10n.setAttributes(shortDesc, "certerror-expired-cert-intro", {
        hostname: HOST_NAME,
      });

      // The secondary description mentions expired certificates explicitly
      // and should only be shown if the certificate has actually expired
      // instead of being not yet valid.
      if (failedCertInfo.errorCodeString == "SEC_ERROR_EXPIRED_CERTIFICATE") {
        const sd2Id = gHasSts
          ? "certerror-expired-cert-sts-second-para"
          : "certerror-expired-cert-second-para";
        document.l10n.setAttributes(shortDesc2, sd2Id);
        if (
          Math.abs(difference) <= 60 * 60 * 24 &&
          now - lastFetched <= 60 * 60 * 24 * 5 * 1000
        ) {
          whatToDoParts = [
            ["p", "certerror-bad-cert-domain-what-can-you-do-about-it"],
          ];
        }
      }

      whatToDoParts ??= [
        [
          "p",
          "certerror-expired-cert-what-can-you-do-about-it-clock",
          { hostname: HOST_NAME, now },
        ],
        [
          "p",
          "certerror-expired-cert-what-can-you-do-about-it-contact-website",
        ],
      ];
      break;
    }
    case "MOZILLA_PKIX_ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY":
      whatToDoParts = [
        [
          "p",
          "cert-error-trust-certificate-transparency-what-can-you-do-about-it",
        ],
      ];
      break;
    case "SEC_ERROR_REVOKED_CERTIFICATE":
      whatToDoParts = [
        [
          "p",
          // This string was added for the certificate transparency error case,
          // but it applies in other cases as well, such as this one.
          "cert-error-trust-certificate-transparency-what-can-you-do-about-it",
        ],
      ];
      break;
  }

  if (whatToDoParts) {
    setNetErrorMessageFromParts(
      document.getElementById("errorWhatToDoText"),
      whatToDoParts
    );
    document.getElementById("errorWhatToDo").hidden = false;
  }
}

// The optional argument is only here for testing purposes.
function setTechnicalDetailsOnCertError(
  failedCertInfo = document.getFailedCertSecurityInfo()
) {
  let technicalInfo = document.getElementById("badCertTechnicalInfo");
  technicalInfo.textContent = "";

  function addLabel(l10nId, args = null, attrs = null) {
    let elem = document.createElement("label");
    technicalInfo.appendChild(elem);

    let newLines = document.createTextNode("\n \n");
    technicalInfo.appendChild(newLines);

    if (attrs) {
      let link = document.createElement("a");
      for (let [attr, value] of Object.entries(attrs)) {
        link.setAttribute(attr, value);
      }
      elem.appendChild(link);
    }

    document.l10n.setAttributes(elem, l10nId, args);
  }

  function addErrorCodeLink() {
    addLabel(
      "cert-error-code-prefix-link",
      { error: failedCertInfo.errorCodeString },
      {
        title: failedCertInfo.errorCodeString,
        id: "errorCode",
        "data-l10n-name": "error-code-link",
        "data-telemetry-id": "error_code_link",
        href: "#certificateErrorDebugInformation",
      }
    );

    // We're attaching the event listener to the parent element and not on
    // the errorCodeLink itself because event listeners cannot be attached
    // to fluent DOM overlays.
    technicalInfo.addEventListener("click", event => {
      if (event.target.id === "errorCode") {
        event.preventDefault();
        toggleCertErrorDebugInfoVisibility();
        recordClickTelemetry(event);
      }
    });
  }

  let hostname = HOST_NAME;
  const { port } = document.location;
  if (port && port != 443) {
    hostname += ":" + port;
  }

  switch (failedCertInfo.overridableErrorCategory) {
    case "trust-error":
      switch (failedCertInfo.errorCodeString) {
        case "MOZILLA_PKIX_ERROR_MITM_DETECTED":
          addLabel("cert-error-mitm-intro");
          addLabel("cert-error-mitm-mozilla");
          addLabel("cert-error-mitm-connection");
          break;
        case "SEC_ERROR_UNKNOWN_ISSUER":
          addLabel("cert-error-trust-unknown-issuer-intro");
          addLabel("cert-error-trust-unknown-issuer", { hostname });
          break;
        case "SEC_ERROR_CA_CERT_INVALID":
          addLabel("cert-error-intro", { hostname });
          addLabel("cert-error-trust-cert-invalid");
          break;
        case "SEC_ERROR_UNTRUSTED_ISSUER":
          addLabel("cert-error-intro", { hostname });
          addLabel("cert-error-trust-untrusted-issuer");
          break;
        case "SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED":
          addLabel("cert-error-intro", { hostname });
          addLabel("cert-error-trust-signature-algorithm-disabled");
          break;
        case "SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE":
          addLabel("cert-error-intro", { hostname });
          addLabel("cert-error-trust-expired-issuer");
          break;
        case "MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT":
          addLabel("cert-error-intro", { hostname });
          addLabel("cert-error-trust-self-signed");
          break;
        case "MOZILLA_PKIX_ERROR_ADDITIONAL_POLICY_CONSTRAINT_FAILED":
          addLabel("cert-error-intro", { hostname });
          addLabel("cert-error-trust-symantec");
          break;
        case "MOZILLA_PKIX_ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY":
          addLabel("cert-error-trust-certificate-transparency", { hostname });
          break;
        default:
          addLabel("cert-error-intro", { hostname });
          addLabel("cert-error-untrusted-default");
      }
      addErrorCodeLink();
      break;

    case "expired-or-not-yet-valid": {
      const notBefore = failedCertInfo.validNotBefore;
      const notAfter = failedCertInfo.validNotAfter;
      if (notBefore && Date.now() < notAfter) {
        addLabel("cert-error-not-yet-valid-now", {
          hostname,
          "not-before-local-time": formatter.format(new Date(notBefore)),
        });
      } else {
        addLabel("cert-error-expired-now", {
          hostname,
          "not-after-local-time": formatter.format(new Date(notAfter)),
        });
      }
      addErrorCodeLink();
      break;
    }

    case "domain-mismatch":
      getSubjectAltNames(failedCertInfo).then(subjectAltNames => {
        if (!subjectAltNames.length) {
          addLabel("cert-error-domain-mismatch", { hostname });
        } else if (subjectAltNames.length > 1) {
          const names = subjectAltNames.join(", ");
          addLabel("cert-error-domain-mismatch-multiple", {
            hostname,
            "subject-alt-names": names,
          });
        } else {
          const altName = subjectAltNames[0];

          // If the alt name is a wildcard domain ("*.example.com")
          // let's use "www" instead.  "*.example.com" isn't going to
          // get anyone anywhere useful. bug 432491
          const okHost = altName.replace(/^\*\./, "www.");

          // Let's check if we want to make this a link.
          const showLink =
            /* case #1:
             * example.com uses an invalid security certificate.
             *
             * The certificate is only valid for www.example.com
             *
             * Make sure to include the "." ahead of thisHost so that a
             * MitM attack on paypal.com doesn't hyperlink to "notpaypal.com"
             *
             * We'd normally just use a RegExp here except that we lack a
             * library function to escape them properly (bug 248062), and
             * domain names are famous for having '.' characters in them,
             * which would allow spurious and possibly hostile matches.
             */
            okHost.endsWith("." + HOST_NAME) ||
            /* case #2:
             * browser.garage.maemo.org uses an invalid security certificate.
             *
             * The certificate is only valid for garage.maemo.org
             */
            HOST_NAME.endsWith("." + okHost);

          const l10nArgs = { hostname, "alt-name": altName };
          if (showLink) {
            // Set the link if we want it.
            const proto = document.location.protocol + "//";
            addLabel("cert-error-domain-mismatch-single", l10nArgs, {
              href: proto + okHost,
              "data-l10n-name": "domain-mismatch-link",
              id: "cert_domain_link",
            });

            // If we set a link, meaning there's something helpful for
            // the user here, expand the section by default
            if (getCSSClass() != "expertBadCert") {
              revealAdvancedPanelSlowlyAsync();
            }
          } else {
            addLabel("cert-error-domain-mismatch-single-nolink", l10nArgs);
          }
        }
        addErrorCodeLink();
      });
      break;
  }

  if (failedCertInfo.errorCodeString == "SEC_ERROR_REVOKED_CERTIFICATE") {
    addLabel("cert-error-revoked", { hostname });
    addErrorCodeLink();
  }

  getFailedCertificatesAsPEMString().then(pemString => {
    const errorText = document.getElementById("certificateErrorText");
    errorText.textContent = pemString;
  });
}

/* Only focus if we're the toplevel frame; otherwise we
   don't want to call attention to ourselves!
*/
function setFocus(selector, position = "afterbegin") {
  if (window.top == window) {
    var button = document.querySelector(selector);
    button.parentNode.insertAdjacentElement(position, button);
    // It's possible setFocus was called via the DOMContentLoaded event
    // handler and that the button has no frame. Things without a frame cannot
    // be focused. We use a requestAnimationFrame to queue up the focus to occur
    // once the button has its frame.
    requestAnimationFrame(() => {
      button.focus({ focusVisible: false });
    });
  }
}

function shouldUseFeltPrivacyRefresh() {
  if (!FELT_PRIVACY_REFRESH) {
    return false;
  }

  let failedCertInfo;
  try {
    failedCertInfo = document.getFailedCertSecurityInfo();
  } catch {
    return false;
  }

  return NetErrorCard.ERROR_CODES.has(failedCertInfo.errorCodeString);
}

if (!shouldUseFeltPrivacyRefresh()) {
  for (let button of document.querySelectorAll(".try-again")) {
    button.addEventListener("click", function () {
      retryThis(this);
    });
  }

  initPage();

  // Dispatch this event so tests can detect that we finished loading the error page.
  document.dispatchEvent(
    new CustomEvent("AboutNetErrorLoad", { bubbles: true })
  );
} else {
  customElements.define("net-error-card", NetErrorCard);
  document.body.classList.add("felt-privacy-body");
  document.body.replaceChildren(document.createElement("net-error-card"));
}
