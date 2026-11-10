/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-disable import-x/no-unassigned-import */

import { NetErrorCard } from "chrome://global/content/net-error-card.mjs";
import {
  gIsCertError,
  isCaptive,
  gErrorCode,
  gHasSts,
  gOffline,
  searchParams,
  getHostName,
  getLoopbackInfo,
  getSubjectAltNames,
  getFailedCertificatesAsPEMString,
  recordSecurityUITelemetry,
  getCSSClass,
  gNoConnectivity,
  retryThis,
  handleNSSFailure,
  detectClockSkew,
} from "chrome://global/content/aboutNetErrorHelpers.mjs";
import { initializeRegistry } from "chrome://global/content/errors/error-registry.mjs";
import {
  errorHasNoUserFix,
  getResolvedErrorConfig,
} from "chrome://global/content/errors/error-lookup.mjs";

const formatter = new Intl.DateTimeFormat();

const HOST_NAME = getHostName();

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
    case "blockedByCOEP":
    case "blockedByConnectionAllowlist": {
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
      const result = handleNSSFailure(showPrefChangeContainer);
      if (result.versionError) {
        const tlsNotice = document.getElementById("tlsVersionNotice");
        tlsNotice.hidden = false;
        document.l10n.setAttributes(tlsNotice, "cert-error-old-tls-version");
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

  // nssFailure2 are TLS errors which are tracked by load_abouttlserror
  if (gErrorCode !== "nssFailure2" && !isCaptive()) {
    try {
      let netErrorInfo = document.getNetErrorInfo();
      if (!netErrorInfo.errorCodeString) {
        netErrorInfo.errorCodeString = gErrorCode;
      }
      void recordSecurityUITelemetry(
        "securityUiNeterror",
        "loadAboutneterror",
        netErrorInfo
      );
    } catch (ex) {}
  }

  const tryAgain = document.getElementById("netErrorButtonContainer");
  tryAgain.hidden = false;
  const learnMoreLink = document.getElementById("learnMoreLink");
  learnMoreLink.setAttribute("href", baseURL + "connection-not-secure");
  let { pageTitleId, bodyTitle, bodyTitleId, longDesc } = initTitleAndBodyIds(
    baseURL,
    isTRROnlyFailure
  );

  // We can handle the offline page separately.
  if (gNoConnectivity) {
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
    if (isTRROnlyFailure && !gNoConnectivity) {
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
        RPMSendQuery("Browser:AddTRRExcludedDomain").then(() => {
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
    const parts = getNetErrorDescParts(gNoConnectivity);
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
  // Build context for resolving description parts
  const context = {
    hostname: HOST_NAME,
    noConnectivity,
    showOSXPermissionWarning: RPMShowOSXLocalNetworkPermissionWarning(),
    offline: gOffline,
    // Only a refused connection means "nothing is listening"; timeouts and
    // resets on loopback can have other causes.
    loopback: gErrorCode == "connectionFailure" ? getLoopbackInfo() : null,
  };

  // The legacy page looks up configs by gErrorCode (the URL e= parameter).
  // MITM is registered under its C++ error string, not the URL code.
  let configId = gErrorCode;
  if (gErrorCode === "mitm") {
    configId = "MOZILLA_PKIX_ERROR_MITM_DETECTED";
    try {
      const failedCertInfo = document.getFailedCertSecurityInfo();
      context.mitmName = getMitmName(failedCertInfo);
    } catch {
      context.mitmName = "";
    }
  }

  const config = getResolvedErrorConfig(configId, context);

  // Convert config descriptionParts to legacy tuple format
  const parts = config.descriptionParts || [];
  return parts.map(part => {
    if (part.tag === "a") {
      return [part.tag, part.dataL10nId, part.href];
    }
    return [part.tag, part.dataL10nId, part.dataL10nArgs];
  });
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

  const shortDesc = document.getElementById("errorShortDesc");
  document.l10n.setAttributes(shortDesc, "cert-error-ssl-connection-error", {
    errorMessage: errorMessage ?? errorCode ?? "",
    hostname: HOST_NAME,
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

    case "SEC_ERROR_UNKNOWN_ISSUER":
      whatToDoParts = [
        ["p", "certerror-unknown-issuer-what-can-you-do-about-it-website"],
        [
          "p",
          "certerror-unknown-issuer-what-can-you-do-about-it-contact-admin",
        ],
      ];
      break;

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

      const difference = RPMGetIntPref(
        "services.settings.clock_skew_seconds",
        0
      );
      const lastFetched =
        RPMGetIntPref("services.settings.last_update_seconds", 0) * 1000;
      const now = Date.now();

      if (detectClockSkew(failedCertInfo, now)) {
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
  }

  if (errorHasNoUserFix(failedCertInfo.errorCodeString)) {
    // "cert-error-trust-certificate-transparency-what-can-you-do-about-it" was
    // originally added for certificate transparency errors, but it's general
    // enough to apply in many cases.
    whatToDoParts = [
      [
        "p",
        "cert-error-trust-certificate-transparency-what-can-you-do-about-it",
      ],
    ];
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
          addLabel("cert-error-trust-unknown-issuer", { hostname: HOST_NAME });
          break;
        case "SEC_ERROR_CA_CERT_INVALID":
          addLabel("cert-error-intro", { hostname: HOST_NAME });
          addLabel("cert-error-trust-cert-invalid");
          break;
        case "SEC_ERROR_UNTRUSTED_ISSUER":
          addLabel("cert-error-intro", { hostname: HOST_NAME });
          addLabel("cert-error-trust-untrusted-issuer");
          break;
        case "SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED":
          addLabel("cert-error-intro", { hostname: HOST_NAME });
          addLabel("cert-error-trust-signature-algorithm-disabled");
          break;
        case "SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE":
          addLabel("cert-error-intro", { hostname: HOST_NAME });
          addLabel("cert-error-trust-expired-issuer");
          break;
        case "MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT":
          addLabel("cert-error-intro", { hostname: HOST_NAME });
          addLabel("cert-error-trust-self-signed");
          break;
        case "MOZILLA_PKIX_ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY":
          addLabel("cert-error-trust-certificate-transparency", {
            hostname: HOST_NAME,
          });
          break;
        default:
          addLabel("cert-error-intro", { hostname: HOST_NAME });
          addLabel("cert-error-untrusted-default");
      }
      addErrorCodeLink();
      break;

    case "expired-or-not-yet-valid": {
      const notBefore = failedCertInfo.validNotBefore;
      const notAfter = failedCertInfo.validNotAfter;
      if (notBefore && Date.now() < notAfter) {
        addLabel("cert-error-not-yet-valid-now", {
          hostname: HOST_NAME,
          "not-before-local-time": formatter.format(new Date(notBefore)),
        });
      } else {
        addLabel("cert-error-expired-now", {
          hostname: HOST_NAME,
          "not-after-local-time": formatter.format(new Date(notAfter)),
        });
      }
      addErrorCodeLink();
      break;
    }

    case "domain-mismatch":
      getSubjectAltNames(failedCertInfo).then(subjectAltNames => {
        if (!subjectAltNames.length) {
          addLabel("cert-error-domain-mismatch", { hostname: HOST_NAME });
        } else if (subjectAltNames.length > 1) {
          const names = subjectAltNames.join(", ");
          addLabel("cert-error-domain-mismatch-multiple", {
            hostname: HOST_NAME,
            "subject-alt-names": names,
          });
        } else {
          const altName = subjectAltNames[0];

          // If the alt name is a wildcard domain ("*.example.com")
          // let's use "www" instead.  "*.example.com" isn't going to
          // get anyone anywhere useful. bug 432491
          const okHost = altName.replace(/^\*\./, "www.");

          // We can't use HOST_NAME for the comparison, as that is
          // display-formatted and can include a port. We need the ascii host as
          // that is what the cert's SAN value would also contain.
          const asciiHostname = RPMGetInnermostAsciiHost();

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
            okHost.endsWith("." + asciiHostname) ||
            /* case #2:
             * browser.garage.maemo.org uses an invalid security certificate.
             *
             * The certificate is only valid for garage.maemo.org
             */
            asciiHostname.endsWith("." + okHost);

          const l10nArgs = { hostname: HOST_NAME, "alt-name": altName };
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

  const nonoverridableErrorCodeToLabelMap = {
    SEC_ERROR_BAD_DER: "cert-error-bad-der",
    SEC_ERROR_BAD_SIGNATURE: "cert-error-bad-signature",
    SEC_ERROR_CERT_NOT_IN_NAME_SPACE: "cert-error-cert-not-in-name-space",
    SEC_ERROR_EXTENSION_VALUE_INVALID: "cert-error-extension-value-invalid",
    SEC_ERROR_INADEQUATE_CERT_TYPE: "cert-error-inadequate-cert-type",
    // NB: SEC_ERROR_INADEQUATE_KEY_USAGE intentionally uses the same error
    // message as SEC_ERROR_INADEQUATE_CERT_TYPE
    SEC_ERROR_INADEQUATE_KEY_USAGE: "cert-error-inadequate-cert-type",
    SEC_ERROR_INVALID_KEY: "cert-error-invalid-key",
    SEC_ERROR_PATH_LEN_CONSTRAINT_INVALID:
      "cert-error-path-len-constraint-invalid",
    SEC_ERROR_REVOKED_CERTIFICATE: "cert-error-revoked-certificate",
    SEC_ERROR_UNKNOWN_CRITICAL_EXTENSION:
      "cert-error-unknown-critical-extension",
    // NB: SEC_ERROR_UNSUPPORTED_EC_POINT_FORM intentionally uses the same
    // error message as SEC_ERROR_UNSUPPORTED_KEYALG
    SEC_ERROR_UNSUPPORTED_EC_POINT_FORM: "cert-error-unsupported-keyalg",
    // NB: SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE intentionally uses the same
    // error message as SEC_ERROR_UNSUPPORTED_KEYALG
    SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE: "cert-error-unsupported-keyalg",
    SEC_ERROR_UNSUPPORTED_KEYALG: "cert-error-unsupported-keyalg",
    SEC_ERROR_UNTRUSTED_CERT: "cert-error-untrusted-cert",
    SEC_ERROR_UNTRUSTED_ISSUER: "cert-error-untrusted-issuer",
    MOZILLA_PKIX_ERROR_INVALID_INTEGER_ENCODING:
      "cert-error-invalid-integer-encoding",
    MOZILLA_PKIX_ERROR_ISSUER_NO_LONGER_TRUSTED:
      "cert-error-issuer-no-longer-trusted",
    MOZILLA_PKIX_ERROR_KEY_PINNING_FAILURE: "cert-error-key-pinning-failure",
    MOZILLA_PKIX_ERROR_SIGNATURE_ALGORITHM_MISMATCH:
      "cert-error-signature-algorithm-mismatch",
  };
  if (failedCertInfo.errorCodeString in nonoverridableErrorCodeToLabelMap) {
    addLabel(
      nonoverridableErrorCodeToLabelMap[failedCertInfo.errorCodeString],
      { hostname: HOST_NAME }
    );
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

async function getCertErrorCode() {
  try {
    return document.getFailedCertSecurityInfo().errorCodeString;
  } catch (e) {
    return undefined;
  }
}

async function retryCertErrorCode() {
  return new Promise(res => {
    setTimeout(() => {
      res(getCertErrorCode());
    }, 100);
  });
}

async function ensureCertErrorCode() {
  if (!gIsCertError) {
    return;
  }
  let errorCode = await getCertErrorCode();
  let i = 0;
  while (!errorCode && i < 3) {
    i++;
    errorCode = await retryCertErrorCode();
  }
}

function maybeShowSSLKeyLoggingWarning() {
  if (!RPMIsSSLKeyLoggingEnabled()) {
    return;
  }
  const warning = document.getElementById("sslKeyLoggingWarning");
  if (warning && RPMGetBoolPref("network.sslkeylog_warning")) {
    warning.hidden = false;
  }
}

async function main() {
  await ensureCertErrorCode();
  if (!NetErrorCard.isSupported()) {
    // Initialize the error registry for legacy path
    initializeRegistry();

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
    const warning = document.getElementById("sslKeyLoggingWarning");
    document.body.replaceChildren(document.createElement("net-error-card"));
    if (warning) {
      document.body.prepend(warning);
    }
  }
  maybeShowSSLKeyLoggingWarning();
}

main();
