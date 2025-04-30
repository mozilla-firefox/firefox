/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  getUnicodeUrl,
  getUnicodeUrlPath,
  getUnicodeHostname,
} = require("resource://devtools/client/shared/unicode-url.js");

const lazy = {};
ChromeUtils.defineESModuleGetters(
  lazy,
  {
    parseJsonLossless:
      "resource://devtools/client/shared/components/reps/reps/rep-utils.mjs",
  },
  { global: "contextual" }
);

loader.lazyRequireGetter(
  this,
  "L10N",
  "resource://devtools/client/netmonitor/src/utils/l10n.js",
  true
);

const {
  UPDATE_PROPS,
} = require("resource://devtools/client/netmonitor/src/constants.js");

const CONTENT_MIME_TYPE_ABBREVIATIONS = new Map([
  ["ecmascript", "js"],
  ["javascript", "js"],
  ["x-javascript", "js"],
  ["event-stream", "eventsource"],
]);

/**
 * Extracts any urlencoded form data sections (e.g. "?foo=bar&baz=42") from a
 * POST request.
 *
 * @param {object} headers - the "requestHeaders".
 * @param {object} uploadHeaders - the "requestHeadersFromUploadStream".
 * @param {object} postData - the "requestPostData".
 * @return {array} a promise list that is resolved with the extracted form data.
 */
async function getFormDataSections(
  headers,
  uploadHeaders,
  postData,
  getLongString
) {
  const formDataSections = [];

  const requestHeaders = headers.headers;
  const payloadHeaders = uploadHeaders ? uploadHeaders.headers : [];
  const allHeaders = [...payloadHeaders, ...requestHeaders];

  const contentTypeHeader = allHeaders.find(e => {
    return e.name.toLowerCase() == "content-type";
  });

  const contentTypeLongString = contentTypeHeader
    ? contentTypeHeader.value
    : "";

  const contentType = await getLongString(contentTypeLongString);

  if (contentType && contentType.includes("x-www-form-urlencoded")) {
    const postDataLongString = postData.postData.text;
    const text = await getLongString(postDataLongString);

    for (const section of text.trim().split(/\r\n|\r|\n/)) {
      // Before displaying it, make sure this section of the POST data
      // isn't a line containing upload stream headers.
      if (payloadHeaders.every(header => !section.startsWith(header.name))) {
        formDataSections.push(section);
      }
    }
  }

  return formDataSections;
}

/**
 * Fetch headers full content from actor server
 *
 * @param {object} headers - a object presents headers data
 * @return {object} a headers object with updated content payload
 */
async function fetchHeaders(headers, getLongString) {
  for (const { value } of headers.headers) {
    headers.headers.value = await getLongString(value);
  }
  return headers;
}

/**
 * Fetch network event update packets from actor server
 * Expect to fetch a couple of network update packets from a given request.
 *
 * @param {function} requestData - requestData function for lazily fetch data
 * @param {object} request - request object
 * @param {array} updateTypes - a list of network event update types
 */
function fetchNetworkUpdatePacket(requestData, request, updateTypes) {
  const promises = [];
  if (request) {
    updateTypes.forEach(updateType => {
      // Only stackTrace will be handled differently
      if (updateType === "stackTrace") {
        if (request.cause.stacktraceAvailable && !request.stacktrace) {
          promises.push(requestData(request.id, updateType));
        }
        return;
      }

      if (request[`${updateType}Available`] && !request[updateType]) {
        promises.push(requestData(request.id, updateType));
      }
    });
  }

  return Promise.all(promises);
}

/**
 * Form a data: URI given a mime type, encoding, and some text.
 *
 * @param {string} mimeType - mime type
 * @param {string} encoding - encoding to use; if not set, the
 *                            text will be base64-encoded.
 * @param {string} text - text of the URI.
 * @return {string} a data URI
 */
function formDataURI(mimeType, encoding, text) {
  if (!encoding) {
    encoding = "base64";
    text = btoa(unescape(encodeURIComponent(text)));
  }
  return "data:" + mimeType + ";" + encoding + "," + text;
}

/**
 * Write out a list of headers into a chunk of text
 *
 * @param {array} headers - array of headers info { name, value }
 * @param {string} preHeaderText - first line of the headers request/response
 * @return {string} list of headers in text format
 */
function writeHeaderText(headers, preHeaderText) {
  let result = "";
  if (preHeaderText) {
    result += preHeaderText + "\r\n";
  }
  result += headers.map(({ name, value }) => name + ": " + value).join("\r\n");
  result += "\r\n\r\n";
  return result;
}

/**
 * Decode base64 string.
 *
 * @param {string} url - a string
 * @return {string} decoded string
 */
function decodeUnicodeBase64(string) {
  try {
    return decodeURIComponent(atob(string));
  } catch (err) {
    // Ignore error and return input string directly.
  }
  return string;
}

/**
 * Helper for getting an abbreviated string for a mime type.
 *
 * @param {string} mimeType - mime type
 * @return {string} abbreviated mime type
 */
function getAbbreviatedMimeType(mimeType) {
  if (!mimeType) {
    return "";
  }
  const abbrevType = (
    mimeType.toLowerCase().split(";")[0].split("/")[1] || ""
  ).split("+")[0];
  return CONTENT_MIME_TYPE_ABBREVIATIONS.get(abbrevType) || abbrevType;
}

/**
 * Helpers for getting a filename from a mime type.
 *
 * @param {string} baseNameWithQuery - unicode basename and query of a url
 * @return {string} unicode filename portion of a url
 */
function getFileName(baseNameWithQuery) {
  const basename = baseNameWithQuery && baseNameWithQuery.split("?")[0];
  return basename && basename.includes(".") ? basename : null;
}

/**
 * Helpers for retrieving a URL object from a string
 *
 * @param {string|URL} url - unvalidated url string or already a URL object
 * @return {URL} The URL object
 */
function getUrl(url) {
  if (URL.isInstance(url)) {
    return url;
  }
  return URL.parse(url);
}

/**
 * Helpers for retrieving the value of a URL object property
 *
 * @param {string|URL} input - unvalidated url string or URL instance
 * @param {string} string - desired property in the URL object
 * @return {string} unicode query of a url
 */
function getUrlProperty(input, property) {
  const url = getUrl(input);
  return url?.[property] ?? "";
}

/**
 * Helpers for getting the last portion of a url.
 * For example helper returns "basename" from http://domain.com/path/basename
 * If basename portion is empty, it returns the url pathname.
 *
 * @param {string|URL} url - unvalidated url string or URL instance
 * @return {string} unicode basename of a url
 */
function getUrlBaseName(url) {
  const pathname = getUrlProperty(url, "pathname");
  return getUnicodeUrlPath(pathname.replace(/\S*\//, "") || pathname || "/");
}

/**
 * Helpers for getting the query portion of a url.
 *
 * @param {string|URL} url - unvalidated url string or URL instance
 * @return {string} unicode query of a url
 */
function getUrlQuery(url) {
  return getUrlProperty(url, "search").replace(/^\?/, "");
}

/**
 * Helpers for getting unicode name and query portions of a url.
 *
 * @param {URL} urlObject - the URL instance
 * @return {string} unicode basename and query portions of a url
 */
function getUrlBaseNameWithQuery(urlObject) {
  if (urlObject.href.startsWith("data:")) {
    // For data URIs, no basename can be extracted from the URL so just reuse
    // the full url.
    return urlObject.href;
  }

  const basename = getUrlBaseName(urlObject);
  const search = getUrlProperty(urlObject, "search");
  return basename + getUnicodeUrlPath(search);
}

/**
 * Helpers for getting hostname portion of an URL.
 *
 * @param {string|URL} url - unvalidated url string or URL instance
 * @return {string} unicode hostname of a url
 */
function getUrlHostName(url) {
  return getUrlProperty(url, "hostname");
}

/**
 * Helpers for getting host portion of an URL.
 *
 * @param {string|URL} url - unvalidated url string or URL instance
 * @return {string} unicode host of a url
 */
function getUrlHost(url) {
  return getUrlProperty(url, "host");
}

/**
 * Helpers for getting the shceme portion of a url.
 * For example helper returns "http" from http://domain.com/path/basename
 *
 * @param {string|URL} url - unvalidated url string or URL instance
 * @return {string} string scheme of a url
 */
function getUrlScheme(url) {
  const protocol = getUrlProperty(url, "protocol");
  return protocol.replace(":", "").toLowerCase();
}

/**
 * Helpers for getting the full path portion of a url.
 *
 * @param {string|URL} url - unvalidated url string or URL instance
 * @return {string} string path of a url
 */
function getUrlPath(url) {
  const href = getUrlProperty(url, "href");
  const origin = getUrlProperty(url, "origin");
  return href.replace(origin, "");
}

/**
 * Extract several details fields from a URL at once.
 */
function getUrlDetails(url) {
  const urlObject = getUrl(url);
  const baseNameWithQuery = getUrlBaseNameWithQuery(urlObject);
  let host = getUrlHost(urlObject);
  const hostname = getUrlHostName(urlObject);
  const unicodeUrl = getUnicodeUrl(urlObject);
  const scheme = getUrlScheme(urlObject);
  const path = getUrlPath(urlObject);

  // If the hostname contains unreadable ASCII characters, we need to do the
  // following two steps:
  // 1. Converting the unreadable hostname to a readable Unicode domain name.
  //    For example, converting xn--g6w.xn--8pv into a Unicode domain name.
  // 2. Replacing the unreadable hostname portion in the `host` with the
  //    readable hostname.
  //    For example, replacing xn--g6w.xn--8pv:8000 with [Unicode domain]:8000
  // After finishing the two steps, we get a readable `host`.
  const unicodeHostname = getUnicodeHostname(hostname);
  if (unicodeHostname !== hostname) {
    host = host.replace(hostname, unicodeHostname);
  }

  // Mark local hosts specially, where "local" is  as defined in the W3C
  // spec for secure contexts.
  // http://www.w3.org/TR/powerful-features/
  //
  //  * If the name falls under 'localhost'
  //  * If the name is an IPv4 address within 127.0.0.0/8
  //  * If the name is an IPv6 address within ::1/128
  //
  // IPv6 parsing is a little sloppy; it assumes that the address has
  // been validated before it gets here.
  const isLocal =
    hostname.match(/(.+\.)?localhost$/) ||
    hostname.match(/^127\.\d{1,3}\.\d{1,3}\.\d{1,3}/) ||
    hostname.match(/\[[0:]+1\]/);

  return {
    baseNameWithQuery,
    host,
    scheme,
    unicodeUrl,
    isLocal,
    url,
    path,
  };
}

/**
 * Helpers for retrieving the value of a URL tooltip
 *
 * @param {object} urlDetails - a urlDetails object
 * @returns
 */
function getUrlToolTip(urlDetails) {
  const url = urlDetails.url;
  const decodedURL = urlDetails.unicodeUrl;

  // The `originalFileURL` below refers to "File" because it was initially created for use in the File column.
  // Now it is also being used in the Path and URL columns, while retaining the original name.
  const ORIGINAL_URL = L10N.getFormatStr(
    "netRequest.originalFileURL.tooltip",
    url
  );
  const DECODED_URL = L10N.getFormatStr(
    "netRequest.decodedFileURL.tooltip",
    decodedURL
  );
  const toolTip =
    url === decodedURL ? url : ORIGINAL_URL + "\n\n" + DECODED_URL;
  return toolTip;
}

/**
 * Parse a url's query string into its components
 *
 * @param {string} query - query string of a url portion
 * @return {array} array of query params { name, value }
 */
function parseQueryString(query) {
  if (!query) {
    return null;
  }
  return query
    .replace(/^[?&]/, "")
    .split("&")
    .map(e => {
      const param = e.split("=");
      return {
        name: param[0] ? getUnicodeUrlPath(param[0].replace(/\+/g, " ")) : "",
        value: param[1]
          ? getUnicodeUrlPath(param.slice(1).join("=").replace(/\+/g, " "))
          : "",
      };
    });
}

/**
 * Parse a string of formdata sections into its components
 *
 * @param {string} sections - sections of formdata joined by &
 * @return {array} array of formdata params { name, value }
 */
function parseFormData(sections) {
  if (!sections) {
    return [];
  }

  return sections
    .replace(/^&/, "")
    .split("&")
    .map(e => {
      const firstEqualSignIndex = e.indexOf("=");
      const paramName =
        firstEqualSignIndex !== -1 ? e.slice(0, firstEqualSignIndex) : e;
      const paramValue =
        firstEqualSignIndex !== -1 ? e.slice(firstEqualSignIndex + 1) : "";
      return {
        name: paramName ? getUnicodeUrlPath(paramName) : "",
        value: paramValue ? getUnicodeUrlPath(paramValue) : "",
      };
    });
}

/**
 * Reduces an IP address into a number for easier sorting
 *
 * @param {string} ip - IP address to reduce
 * @return {number} the number representing the IP address
 */
function ipToLong(ip) {
  if (!ip) {
    // Invalid IP
    return -1;
  }

  let base;
  let octets = ip.split(".");

  if (octets.length === 4) {
    // IPv4
    base = 10;
  } else if (ip.includes(":")) {
    // IPv6
    const numberOfZeroSections =
      8 - ip.replace(/^:+|:+$/g, "").split(/:+/g).length;
    octets = ip
      .replace("::", `:${"0:".repeat(numberOfZeroSections)}`)
      .replace(/^:|:$/g, "")
      .split(":");
    base = 16;
  } else {
    // Invalid IP
    return -1;
  }
  return octets
    .map((val, ix, arr) => {
      return parseInt(val, base) * Math.pow(256, arr.length - 1 - ix);
    })
    .reduce((sum, val) => {
      return sum + val;
    }, 0);
}

/**
 * Compare two objects on a subset of their properties
 */
function propertiesEqual(props, item1, item2) {
  return item1 === item2 || props.every(p => item1[p] === item2[p]);
}

/**
 * Calculate the start time of a request, which is the time from start
 * of 1st request until the start of this request.
 *
 * Without a firstRequestStartedMs argument the wrong time will be returned.
 * However, it can be omitted when comparing two start times and neither supplies
 * a firstRequestStartedMs.
 */
function getStartTime(item, firstRequestStartedMs = 0) {
  return item.startedMs - firstRequestStartedMs;
}

/**
 * Calculate the end time of a request, which is the time from start
 * of 1st request until the end of this response.
 *
 * Without a firstRequestStartedMs argument the wrong time will be returned.
 * However, it can be omitted when comparing two end times and neither supplies
 * a firstRequestStartedMs.
 */
function getEndTime(item, firstRequestStartedMs = 0) {
  const { startedMs, totalTime } = item;
  return startedMs + totalTime - firstRequestStartedMs;
}

/**
 * Calculate the response time of a request, which is the time from start
 * of 1st request until the beginning of download of this response.
 *
 * Without a firstRequestStartedMs argument the wrong time will be returned.
 * However, it can be omitted when comparing two response times and neither supplies
 * a firstRequestStartedMs.
 */
function getResponseTime(item, firstRequestStartedMs = 0) {
  const { startedMs, totalTime, eventTimings = { timings: {} } } = item;
  return (
    startedMs + totalTime - firstRequestStartedMs - eventTimings.timings.receive
  );
}

/**
 * Format the protocols used by the request.
 */
function getFormattedProtocol(item) {
  const { httpVersion = "", responseHeaders = { headers: [] } } = item;
  const protocol = [httpVersion];
  responseHeaders.headers.some(h => {
    if (h.hasOwnProperty("name") && h.name.toLowerCase() === "x-firefox-spdy") {
      /**
       * First we make sure h.value is defined and not an empty string.
       * Then check that HTTP version and x-firefox-spdy == "http/1.1".
       * If not, check that HTTP version and x-firefox-spdy have the same
       * numeric value when of the forms "http/<x>" and "h<x>" respectively.
       * If not, will push to protocol the non-standard x-firefox-spdy value.
       *
       * @see https://bugzilla.mozilla.org/show_bug.cgi?id=1501357
       */
      if (h.value !== undefined && h.value.length) {
        if (
          h.value.toLowerCase() !== "http/1.1" ||
          protocol[0].toLowerCase() !== "http/1.1"
        ) {
          if (
            parseFloat(h.value.toLowerCase().split("")[1]) !==
            parseFloat(protocol[0].toLowerCase().split("/")[1])
          ) {
            protocol.push(h.value);
            return true;
          }
        }
      }
    }
    return false;
  });
  return protocol.join("+");
}

/**
 * Get the value of a particular response header, or null if not
 * present.
 */
function getResponseHeader(item, header) {
  const { responseHeaders } = item;
  if (!responseHeaders?.headers?.length) {
    return null;
  }
  header = header.toLowerCase();
  for (const responseHeader of responseHeaders.headers) {
    if (responseHeader.name.toLowerCase() == header) {
      return responseHeader.value;
    }
  }
  return null;
}

/**
 * Get the value of a particular request header, or null if not
 * present.
 */
function getRequestHeader(item, header) {
  const { requestHeaders } = item;
  if (!requestHeaders?.headers?.length) {
    return null;
  }
  header = header.toLowerCase();
  for (const requestHeader of requestHeaders.headers) {
    if (requestHeader.name.toLowerCase() == header) {
      return requestHeader.value;
    }
  }
  return null;
}

/**
 * Extracts any urlencoded form data sections from a POST request.
 */
async function updateFormDataSections(props) {
  const { connector, request = {}, updateRequest } = props;
  let {
    id,
    formDataSections,
    requestHeaders,
    requestHeadersAvailable,
    requestHeadersFromUploadStream,
    requestPostData,
    requestPostDataAvailable,
  } = request;

  if (requestHeadersAvailable && !requestHeaders) {
    requestHeaders = await connector.requestData(id, "requestHeaders");
  }

  if (requestPostDataAvailable && !requestPostData) {
    requestPostData = await connector.requestData(id, "requestPostData");
  }

  if (
    !formDataSections &&
    requestHeaders &&
    requestPostData &&
    requestHeadersFromUploadStream
  ) {
    formDataSections = await getFormDataSections(
      requestHeaders,
      requestHeadersFromUploadStream,
      requestPostData,
      connector.getLongString
    );

    updateRequest(request.id, { formDataSections }, true);
  }
}

/**
 * This helper function helps to resolve the full payload of a message
 * that is wrapped in a LongStringActor object.
 */
async function getMessagePayload(payload, getLongString) {
  const result = await getLongString(payload);
  return result;
}

/**
 * This helper function is used for additional processing of
 * incoming network update packets. It makes sure the only valid
 * update properties and the values are correct.
 * It's used by Network and Console panel reducers.
 * @param {object} update
 *        The new update payload
 * @param {object} request
 *        The current request in the state
 */
function processNetworkUpdates(update) {
  const newRequest = {};
  for (const [key, value] of Object.entries(update)) {
    if (UPDATE_PROPS.includes(key)) {
      newRequest[key] = value;
      if (key == "requestPostData") {
        newRequest.requestHeadersFromUploadStream = value.uploadHeaders;
      }
    }
  }
  return newRequest;
}

/**
 * This method checks that the response is base64 encoded by
 * comparing these 2 values:
 * 1. The original response
 * 2. The value of doing a base64 decode on the
 * response and then base64 encoding the result.
 * If the values are different or an error is thrown,
 * the method will return false.
 */
function isBase64(payload) {
  try {
    return btoa(atob(payload)) == payload;
  } catch (err) {
    return false;
  }
}

/**
 * Checks if the payload is of JSON type.
 * This function also handles JSON with XSSI-escaping characters by stripping them
 * and returning the stripped chars in the strippedChars property
 * This function also handles Base64 encoded JSON.
 * @returns {Object} shape:
 *  {Object} json: parsed JSON object
 *  {Error} error: JSON parsing error
 *  {string} strippedChars: XSSI stripped chars removed from JSON payload
 */
function parseJSON(payloadUnclean) {
  let json;
  const jsonpRegex = /^\s*([\w$]+)\s*\(\s*([^]*)\s*\)\s*;?\s*$/;
  const [, jsonpCallback, jsonp] = payloadUnclean.match(jsonpRegex) || [];
  if (jsonpCallback && jsonp) {
    let error;
    try {
      json = parseJSON(jsonp).json;
    } catch (err) {
      error = err;
    }
    return { json, error, jsonpCallback };
  }

  let { payload, strippedChars, error } = removeXSSIString(payloadUnclean);

  try {
    json = lazy.parseJsonLossless(payload);
  } catch (err) {
    if (isBase64(payload)) {
      try {
        json = JSON.parse(atob(payload));
      } catch (err64) {
        error = err64;
      }
    } else {
      error = err;
    }
  }

  // Do not present JSON primitives (e.g. boolean, strings in quotes, numbers)
  // as JSON expandable tree.
  if (!error) {
    if (typeof json !== "object") {
      return {};
    }
  }
  return {
    json,
    error,
    strippedChars,
  };
}

/**
 * Removes XSSI prevention sequences from JSON payloads
 * @param {string} payloadUnclean: JSON payload that may or may have a
 *                                 XSSI prevention sequence
 * @returns {Object} Shape:
 *   {string} payload: the JSON witht the XSSI prevention sequence removed
 *   {string} strippedChars: XSSI string that was removed, null if no XSSI
 *                           prevention sequence was found
 *   {Error} error: error attempting to strip XSSI prevention sequence
 */
function removeXSSIString(payloadUnclean) {
  // Regex that finds the XSSI protection sequences )]}'\n for(;;); and while(1);
  const xssiRegex = /(^\)\]\}',?\n)|(^for ?\(;;\);?)|(^while ?\(1\);?)/;
  let payload, strippedChars, error;
  const xssiRegexMatch = payloadUnclean.match(xssiRegex);

  // Remove XSSI string if there was one found
  if (xssiRegexMatch?.length) {
    const xssiLen = xssiRegexMatch[0].length;
    try {
      // substring the payload by the length of the XSSI match to remove it
      // and save the match to report
      payload = payloadUnclean.substring(xssiLen);
      strippedChars = xssiRegexMatch[0];
    } catch (err) {
      error = err;
      payload = payloadUnclean;
    }
  } else {
    // if there was no XSSI match just return the raw payload
    payload = payloadUnclean;
  }
  return {
    payload,
    strippedChars,
    error,
  };
}

/**
 * Computes the request headers of an HTTP request
 *
 * @param {string} method: request method
 * @param {string} httpVersion: request http version
 * @param {object} requestHeaders: request headers
 * @param {object} urlDetails: request url details
 *
 * @return {string} the request headers
 */
function getRequestHeadersRawText(
  method,
  httpVersion,
  requestHeaders,
  urlDetails
) {
  const url = getUrl(urlDetails.url);
  const path = url ? `${url.pathname}${url.search}` : "<unknown>";
  const preHeaderText = `${method} ${path} ${httpVersion}`;
  return writeHeaderText(requestHeaders.headers, preHeaderText).trim();
}

module.exports = {
  decodeUnicodeBase64,
  getFormDataSections,
  fetchHeaders,
  fetchNetworkUpdatePacket,
  formDataURI,
  writeHeaderText,
  getAbbreviatedMimeType,
  getFileName,
  getEndTime,
  getFormattedProtocol,
  getMessagePayload,
  getRequestHeader,
  getResponseHeader,
  getResponseTime,
  getStartTime,
  getUrlBaseName,
  getUrlDetails,
  getUrlHost,
  getUrlHostName,
  getUrlQuery,
  getUrlScheme,
  getUrlToolTip,
  parseQueryString,
  parseFormData,
  updateFormDataSections,
  processNetworkUpdates,
  propertiesEqual,
  ipToLong,
  parseJSON,
  getRequestHeadersRawText,
};
