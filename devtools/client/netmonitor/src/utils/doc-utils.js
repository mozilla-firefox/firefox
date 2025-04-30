/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  SUPPORTED_HTTP_CODES,
} = require("resource://devtools/client/netmonitor/src/constants.js");

/**
 * A mapping of header names to external documentation. Any header included
 * here will show a MDN link alongside it.
 */
const SUPPORTED_HEADERS = [
  "Accept",
  "Accept-Charset",
  "Accept-Encoding",
  "Accept-Language",
  "Accept-Ranges",
  "Access-Control-Allow-Credentials",
  "Access-Control-Allow-Headers",
  "Access-Control-Allow-Methods",
  "Access-Control-Allow-Origin",
  "Access-Control-Expose-Headers",
  "Access-Control-Max-Age",
  "Access-Control-Request-Headers",
  "Access-Control-Request-Method",
  "Age",
  "Allow",
  "Authorization",
  "Cache-Control",
  "Clear-Site-Data",
  "Connection",
  "Content-Disposition",
  "Content-Encoding",
  "Content-Language",
  "Content-Length",
  "Content-Location",
  "Content-Range",
  "Content-Security-Policy",
  "Content-Security-Policy-Report-Only",
  "Content-Type",
  "Cookie",
  "Cookie2",
  "Cross-Origin-Embedder-Policy",
  "Cross-Origin-Opener-Policy",
  "Cross-Origin-Resource-Policy",
  "DNT",
  "Date",
  "ETag",
  "Early-Data",
  "Expect",
  "Expect-CT",
  "Expires",
  "Feature-Policy",
  "Forwarded",
  "From",
  "Host",
  "If-Match",
  "If-Modified-Since",
  "If-None-Match",
  "If-Range",
  "If-Unmodified-Since",
  "Keep-Alive",
  "Last-Modified",
  "Link",
  "Location",
  "Origin",
  "Pragma",
  "Priority",
  "Proxy-Authenticate",
  "Public-Key-Pins",
  "Public-Key-Pins-Report-Only",
  "Range",
  "Referer",
  "Referrer-Policy",
  "Retry-After",
  "Save-Data",
  "Sec-Fetch-Dest",
  "Sec-Fetch-Mode",
  "Sec-Fetch-Site",
  "Sec-Fetch-User",
  "Sec-GPC",
  "Server",
  "Server-Timing",
  "Set-Cookie",
  "Set-Cookie2",
  "SourceMap",
  "Strict-Transport-Security",
  "TE",
  "Timing-Allow-Origin",
  "Tk",
  "Trailer",
  "Transfer-Encoding",
  "Upgrade-Insecure-Requests",
  "User-Agent",
  "Vary",
  "Via",
  "WWW-Authenticate",
  "Warning",
  "X-Content-Type-Options",
  "X-DNS-Prefetch-Control",
  "X-Forwarded-For",
  "X-Forwarded-Host",
  "X-Forwarded-Proto",
  "X-Frame-Options",
  "X-XSS-Protection",
];

const MDN_URL = "https://developer.mozilla.org/docs/";
const MDN_STATUS_CODES_LIST_URL = `${MDN_URL}Web/HTTP/Reference/Status`;
const getGAParams = (panelId = "netmonitor") => {
  return `?utm_source=mozilla&utm_medium=devtools-${panelId}&utm_campaign=default`;
};

// Base URL to DevTools user docs
const USER_DOC_URL = "https://firefox-source-docs.mozilla.org/devtools-user/";

/**
 * Get the MDN URL for the specified header.
 *
 * @param {string} header Name of the header for the baseURL to use.
 *
 * @return {string} The MDN URL for the header, or null if not available.
 */
function getHeadersURL(header) {
  const lowerCaseHeader = header.toLowerCase();
  const idx = SUPPORTED_HEADERS.findIndex(
    item => item.toLowerCase() === lowerCaseHeader
  );
  return idx > -1
    ? `${MDN_URL}Web/HTTP/Reference/Headers/${SUPPORTED_HEADERS[idx] + getGAParams()}`
    : null;
}

/**
 * Get the MDN URL for the specified HTTP status code.
 *
 * @param {string} HTTP status code for the baseURL to use.
 *
 * @return {string} The MDN URL for the HTTP status code, or null if not available.
 */
function getHTTPStatusCodeURL(statusCode, panelId) {
  return (
    (SUPPORTED_HTTP_CODES.includes(statusCode)
      ? `${MDN_URL}Web/HTTP/Reference/Status/${statusCode}`
      : MDN_STATUS_CODES_LIST_URL) + getGAParams(panelId)
  );
}

/**
 * Get the URL of the Timings tag for Network Monitor.
 *
 * @return {string} the URL of the Timings tag for Network Monitor.
 */
function getNetMonitorTimingsURL() {
  return `${USER_DOC_URL}network_monitor/request_details/#network-monitor-request-details-timings-tab`;
}

/**
 * Get the URL for Performance Analysis
 *
 * @return {string} The URL for the documentation of Performance Analysis.
 */
function getPerformanceAnalysisURL() {
  return `${USER_DOC_URL}network_monitor/performance_analysis/`;
}

/**
 * Get the URL for Filter box
 *
 * @return {string} The URL for the documentation of Filter box.
 */
function getFilterBoxURL() {
  return `${USER_DOC_URL}network_monitor/request_list/#filtering-by-properties`;
}

/**
 * Get the MDN URL for Tracking Protection
 *
 * @return {string} The MDN URL for the documentation of Tracking Protection.
 */
function getTrackingProtectionURL() {
  return `${MDN_URL}Mozilla/Firefox/Privacy/Guides/Tracking_Protection${getGAParams()}`;
}

/**
 * Get the MDN URL for CORS error reason, falls back to generic cors error page
 * if reason is not understood.
 *
 * @param {int} reason: Blocked Reason message from `netmonitor/src/constants.js`
 *
 * @returns {string} the MDN URL for the documentation of CORS errors
 */
function getCORSErrorURL(reason) {
  // Map from blocked reasons from netmonitor/src/constants.js to the correct
  // URL fragment to append to MDN_URL
  const reasonMap = new Map([
    [1001, "CORSDisabled"],
    [1002, "CORSDidNotSucceed"],
    [1003, "CORSRequestNotHttp"],
    [1004, "CORSMultipleAllowOriginNotAllowed"],
    [1005, "CORSMissingAllowOrigin"],
    [1006, "CORSNotSupportingCredentials"],
    [1007, "CORSAllowOriginNotMatchingOrigin"],
    [1008, "CORSMIssingAllowCredentials"],
    [1009, "CORSOriginHeaderNotAdded"],
    [1010, "CORSExternalRedirectNotAllowed"],
    [1011, "CORSPreflightDidNotSucceed"],
    [1012, "CORSInvalidAllowMethod"],
    [1013, "CORSMethodNotFound"],
    [1014, "CORSInvalidAllowHeader"],
    [1015, "CORSMissingAllowHeaderFromPreflight"],
  ]);
  const urlFrag = reasonMap.get(reason) || "";
  return `${MDN_URL}Web/HTTP/Guides/CORS/Errors/${urlFrag}`;
}

module.exports = {
  getHeadersURL,
  getHTTPStatusCodeURL,
  getNetMonitorTimingsURL,
  getPerformanceAnalysisURL,
  getFilterBoxURL,
  getTrackingProtectionURL,
  getCORSErrorURL,
};
