/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  parse,
  pemToDER,
} from "chrome://global/content/certviewer/certDecoder.mjs";

// The following parameters are parsed from the error URL:
//   e - the error code
//   s - custom CSS class to allow alternate styling/favicons
//   d - error description
//   captive - "true" to indicate we're behind a captive portal.
//             Any other value is ignored.
//   captivePortalState - the captive portal service state string

// Note that this file uses document.documentURI to get
// the URL (with the format from above). This is because
// document.location.href gets the current URI off the docshell,
// which is the URL displayed in the location bar, i.e.
// the URI that the user attempted to load.

export let searchParams = new URLSearchParams(
  document.documentURI.split("?")[1]
);
export const MDN_DOCS_HEADERS =
  "https://developer.mozilla.org/docs/Web/HTTP/Reference/Headers/";
export const COOP_MDN_DOCS = MDN_DOCS_HEADERS + "Cross-Origin-Opener-Policy";
export const COEP_MDN_DOCS = MDN_DOCS_HEADERS + "Cross-Origin-Embedder-Policy";
export const HTTPS_UPGRADES_MDN_DOCS =
  "https://support.mozilla.org/kb/https-upgrades";
export let gErrorCode = searchParams.get("e");
export let gIsCertError = gErrorCode == "nssBadCert";
export let gHasSts = gIsCertError && getCSSClass() === "badStsCert";
export let gNoConnectivity =
  gErrorCode == "dnsNotFound" && !RPMHasConnectivity();
// netOffline is the error code used when "Work Offline" mode is enabled via
// the File menu. gOffline is true for both that case and when there's no
// network connectivity.
export let gOffline = gErrorCode === "netOffline" || gNoConnectivity;
export const VPN_ACTIVE = RPMGetBoolPref(
  "browser.ipProtection.userEnabled",
  false
);

export function isCaptive() {
  return searchParams.get("captive") == "true";
}

export function getCSSClass() {
  return searchParams.get("s");
}

export function getHostName() {
  return RPMGetHostForDisplay(document);
}

/**
 * If the page that failed to load is on a loopback address (localhost,
 * *.localhost, 127.0.0.0/8 or [::1]), returns its port as a string and whether
 * the host is an IP literal. Otherwise returns null.
 *
 * Used to give developer-relevant hints when a connection to the local
 * machine is refused, since in that case the usual network, firewall and
 * proxy advice doesn't apply: nothing is listening on that port.
 *
 * @returns {{ port: string, isIPLiteral: boolean } | null}
 */
export function getLoopbackInfo() {
  let { protocol, hostname, port } = document.location;
  if (protocol != "http:" && protocol != "https:") {
    return null;
  }
  // Use hostname, not host: host includes the port (e.g. "localhost:5000").
  const host = hostname.toLowerCase();
  const isIPLiteral =
    host == "[::1]" || /^127\.\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(host);
  if (!isIPLiteral && host != "localhost" && !host.endsWith(".localhost")) {
    return null;
  }
  return {
    port: port || (protocol == "https:" ? "443" : "80"),
    isIPLiteral,
  };
}

export function getFilePath() {
  try {
    const url = new URL(document.location.href);
    if (url.protocol === "file:") {
      let path = decodeURIComponent(url.pathname);
      if (/^\/[A-Za-z]:/.test(path)) {
        path = path.substring(1);
      }
      return path;
    }
    return document.location.href;
  } catch (e) {}
  return null;
}

export function retryThis(buttonEl) {
  RPMSendAsyncMessage("Browser:EnableOnlineMode");
  buttonEl.disabled = true;
}

export async function getFailedCertificatesAsPEMString() {
  let locationUrl = document.location.href;
  let failedCertInfo = document.getFailedCertSecurityInfo();
  let errorMessage = failedCertInfo.errorMessage;
  let hasHSTS = failedCertInfo.hasHSTS.toString();
  let hasHPKP = failedCertInfo.hasHPKP.toString();
  let [hstsLabel, hpkpLabel, failedChainLabel] =
    await document.l10n.formatValues([
      { id: "cert-error-details-hsts-label", args: { hasHSTS } },
      { id: "cert-error-details-key-pinning-label", args: { hasHPKP } },
      { id: "cert-error-details-cert-chain-label" },
    ]);

  let certStrings = failedCertInfo.certChainStrings;
  let failedChainCertificates = "";
  for (let der64 of certStrings) {
    let wrapped = der64.replace(/(\S{64}(?!$))/g, "$1\r\n");
    failedChainCertificates +=
      "-----BEGIN CERTIFICATE-----\r\n" +
      wrapped +
      "\r\n-----END CERTIFICATE-----\r\n";
  }

  let details =
    locationUrl +
    "\r\n\r\n" +
    errorMessage +
    "\r\n\r\n" +
    hstsLabel +
    "\r\n" +
    hpkpLabel +
    "\r\n\r\n" +
    failedChainLabel +
    "\r\n\r\n" +
    failedChainCertificates;
  return details;
}

export async function getSubjectAltNames(failedCertInfo) {
  const serverCertBase64 = failedCertInfo.certChainStrings[0];
  const parsed = await parse(pemToDER(serverCertBase64));
  const subjectAltNamesExtension = parsed.ext.san;
  const subjectAltNames = [];
  if (subjectAltNamesExtension) {
    for (let [key, value] of subjectAltNamesExtension.altNames) {
      if (key === "DNS Name" && value.length) {
        subjectAltNames.push(value);
      }
    }
  }
  return subjectAltNames;
}

export async function recordSecurityUITelemetry(category, name, errorInfo) {
  // Truncate the error code to avoid going over the allowed
  // string size limit for telemetry events.
  let errorCode = errorInfo.errorCodeString.substring(0, 40);
  let extraKeys = {
    value: errorCode,
    is_frame: window.parent != window,
  };
  if (category == "securityUiCerterror") {
    extraKeys.has_sts = gHasSts;
  }
  if (name.startsWith("load")) {
    extraKeys.channel_status = errorInfo.channelStatus;
  }
  if (category == "securityUiCerterror" && name.startsWith("load")) {
    extraKeys.issued_by_cca = false;
    extraKeys.hyphen_compat = false;

    // We can't use HOST_NAME as that is display-formatted and can
    // include a port. We need the ascii host as that is what the cert's
    // SAN value would also contain.
    const asciiHostname = RPMGetInnermostAsciiHost();

    // This issue only applies to certificate domain name mismatch errors where
    // the first label in the domain name starts or ends with a hyphen.
    let label = asciiHostname.substring(0, asciiHostname.indexOf("."));
    if (
      errorCode == "SSL_ERROR_BAD_CERT_DOMAIN" &&
      (label.startsWith("-") || label.endsWith("-"))
    ) {
      try {
        let subjectAltNames = await getSubjectAltNames(errorInfo);
        for (let subjectAltName of subjectAltNames) {
          // If the certificate has a wildcard entry that matches the domain
          // name (e.g. '*.example.com' matches 'foo-.example.com'), then
          // this error is probably due to Firefox disallowing hyphens in
          // domain names when matching wildcard entries.
          if (
            subjectAltName.startsWith("*.") &&
            subjectAltName.substring(1) == asciiHostname.substring(label.length)
          ) {
            extraKeys.hyphen_compat = true;
            break;
          }
        }
      } catch (e) {
        console.error("error parsing certificate:", e);
      }
    }
    let issuer = errorInfo.certChainStrings.at(-1);
    if (issuer && errorCode == "SEC_ERROR_UNKNOWN_ISSUER") {
      try {
        let parsed = await parse(pemToDER(issuer));
        extraKeys.issued_by_cca =
          parsed.issuer.dn == "c=IN, o=India PKI, cn=CCA India 2022 SPL" ||
          parsed.issuer.dn == "c=IN, o=India PKI, cn=CCA India 2015 SPL";
      } catch (e) {
        console.error("error parsing issuer certificate:", e);
      }
    }
  }
  if (category == "securityUiNeterror" && name == "loadAboutneterror") {
    extraKeys.no_connectivity = gNoConnectivity;
    extraKeys.trr_only = gErrorCode == "dnsNotFound" && RPMIsTRROnlyFailure();
    extraKeys.captive_portal_state =
      searchParams.get("captivePortalState") ?? "";
  }
  RPMRecordGleanEvent(category, name, extraKeys);
}

// Returns true if the error identified by the given error code string has no
// particular action the user can take to fix it.
export function errorHasNoUserFix(errorCodeString) {
  switch (errorCodeString) {
    case "MOZILLA_PKIX_ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY":
    case "MOZILLA_PKIX_ERROR_INVALID_INTEGER_ENCODING":
    case "MOZILLA_PKIX_ERROR_ISSUER_NO_LONGER_TRUSTED":
    case "MOZILLA_PKIX_ERROR_KEY_PINNING_FAILURE":
    case "MOZILLA_PKIX_ERROR_SIGNATURE_ALGORITHM_MISMATCH":
    case "SEC_ERROR_BAD_DER":
    case "SEC_ERROR_BAD_SIGNATURE":
    case "SEC_ERROR_CERT_NOT_IN_NAME_SPACE":
    case "SEC_ERROR_EXTENSION_VALUE_INVALID":
    case "SEC_ERROR_INADEQUATE_CERT_TYPE":
    case "SEC_ERROR_INADEQUATE_KEY_USAGE":
    case "SEC_ERROR_INVALID_KEY":
    case "SEC_ERROR_PATH_LEN_CONSTRAINT_INVALID":
    case "SEC_ERROR_REVOKED_CERTIFICATE":
    case "SEC_ERROR_UNKNOWN_CRITICAL_EXTENSION":
    case "SEC_ERROR_UNSUPPORTED_EC_POINT_FORM":
    case "SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE":
    case "SEC_ERROR_UNSUPPORTED_KEYALG":
    case "SEC_ERROR_UNTRUSTED_CERT":
    case "SEC_ERROR_UNTRUSTED_ISSUER":
      return true;
    default:
      return false;
  }
}

/**
 * Detect if the user's system clock is likely wrong by comparing against
 * the remote-settings (Kinto) server time and the application build date.
 *
 * @param {object} failedCertInfo - Certificate security info from document.getFailedCertSecurityInfo()
 * @param {number} [now] - Current time in ms (default: Date.now()). Pass an explicit value when
 *   the same timestamp must be reused for display after the call.
 * @returns {boolean} true if the system clock appears to be skewed
 */
export function detectClockSkew(failedCertInfo, now = Date.now()) {
  const ONE_DAY_SECONDS = 60 * 60 * 24;
  const FIVE_DAYS_MS = 5 * ONE_DAY_SECONDS * 1000;

  const certNotBefore = failedCertInfo.certValidityRangeNotBefore;
  const certNotAfter = failedCertInfo.certValidityRangeNotAfter;
  if (certNotBefore == null || certNotAfter == null) {
    return false;
  }

  const difference = RPMGetIntPref("services.settings.clock_skew_seconds", 0);
  const lastFetched =
    RPMGetIntPref("services.settings.last_update_seconds", 0) * 1000;

  const approximateDate = now - difference * 1000;

  if (
    Math.abs(difference) > ONE_DAY_SECONDS &&
    now - lastFetched <= FIVE_DAYS_MS &&
    certNotBefore < approximateDate &&
    certNotAfter > approximateDate
  ) {
    return true;
  }

  const appBuildID = RPMGetAppBuildID();
  const year = parseInt(appBuildID.substring(0, 4), 10);
  const month = parseInt(appBuildID.substring(4, 6), 10) - 1;
  const day = parseInt(appBuildID.substring(6, 8), 10);
  const buildDate = new Date(year, month, day).getTime();

  return buildDate > now && certNotAfter > buildDate;
}

export function handleNSSFailure(callback) {
  const netErrorInfo = document.getNetErrorInfo();
  void recordSecurityUITelemetry(
    "securityUiTlserror",
    "loadAbouttlserror",
    netErrorInfo
  );
  const errorCode = netErrorInfo.errorCodeString;
  const result = {};
  switch (errorCode) {
    case "SSL_ERROR_UNSUPPORTED_VERSION":
    case "SSL_ERROR_PROTOCOL_VERSION_ALERT": {
      result.versionError = true;
    }
    // fallthrough

    case "SSL_ERROR_NO_CIPHERS_SUPPORTED":
    case "SSL_ERROR_NO_CYPHER_OVERLAP":
    case "SSL_ERROR_SSL_DISABLED":
      RPMAddMessageListener("HasChangedCertPrefs", msg => {
        if (msg.data.hasChangedCertPrefs) {
          // Configuration overrides might have caused this; offer to reset.
          callback?.();
        }
      });
      RPMSendAsyncMessage("GetChangedCertPrefs");
  }
  return result;
}
