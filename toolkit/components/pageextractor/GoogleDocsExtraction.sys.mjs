/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// @ts-check

// The Google Docs editor draws the document onto a canvas, so the live page has
// toolbar text but none of the document's text. Every document also has a
// /mobilebasic view that renders it as plain HTML, so we read that instead.

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  enabled: {
    pref: "browser.pageextractor.googledocs.enabled",
    default: true,
  },
});

const DOCS_HOST = "docs.google.com";

// Without these the view can show a different tab, fail for a link-shared
// document (resourcekey), or open under the wrong signed-in account (authuser).
const CARRIED_PARAMS = ["tab", "resourcekey", "authuser"];

// Images are links in the view, so even large documents are around 1 MiB.
export const MAX_DOWNLOAD_BYTES = 8 * 1024 * 1024;

/** @type {Set<() => void>} */
const viewDeadlines = new Set();

export function expireViewReadsForTesting() {
  if (!Cu.isInAutomation) {
    throw new Error("Only available in automation.");
  }
  for (const onDeadline of viewDeadlines) {
    onDeadline();
  }
}

/**
 * Returns the document path up to its id, keeping the optional /u/{n} account
 * selector, e.g. "/document/u/1/d/{id}". Returns null for anything else.
 *
 * @param {string} pathname
 * @returns {string | null}
 */
function getDocumentPath(pathname) {
  const segments = pathname.split("/");
  if (segments[1] !== "document") {
    return null;
  }
  const dIndex = segments[2] === "u" && segments[3] ? 4 : 2;
  const id = segments[dIndex + 1];
  // Published documents (/document/d/e/{id}/pub) are already plain HTML.
  if (segments[dIndex] !== "d" || !id || id === "e") {
    return null;
  }
  return segments.slice(0, dIndex + 2).join("/");
}

/**
 * @param {URL | null} url - The caller's source URL for the page.
 * @returns {boolean} Whether to read the page through its /mobilebasic view.
 */
export function shouldExtractGoogleDoc(url) {
  return (
    lazy.enabled &&
    url?.protocol === "https:" &&
    url.hostname === DOCS_HOST &&
    getDocumentPath(url.pathname) !== null
  );
}

/**
 * Rejects on a network error, a timeout, or a redirect off docs.google.com,
 * which is where Google sends a profile that can't open the document.
 *
 * @param {Window} window - The content window of the document page.
 * @param {number} timeoutMs - How long to wait for the whole view.
 * @returns {Promise<{ document: Document | null, status: string }>} status is
 *   "success" or "truncated" with a document, or "not-a-document", "http-error"
 *   or "not-html" without one.
 */
export async function getGoogleDocContent(window, timeoutMs) {
  // Build the view URL from the live page, not the caller's source URL, so the
  // request can never leave the page's own origin.
  const documentUrl = new URL(window.location.href);
  const documentPath = getDocumentPath(documentUrl.pathname);
  if (!documentPath) {
    return { document: null, status: "not-a-document" };
  }
  const viewUrl = new URL(`${documentPath}/mobilebasic`, documentUrl.origin);
  for (const name of CARRIED_PARAMS) {
    const value = documentUrl.searchParams.get(name);
    if (value !== null) {
      viewUrl.searchParams.set(name, value);
    }
  }

  // Not the page's own fetch: that would show up in the page's resource timing
  // and go through its service worker. A sandbox with the page's principal
  // still sends the tab's cookies and container, but the page can't see it.
  const sandbox = Cu.Sandbox(window.document.nodePrincipal, {
    sandboxName: "PageExtractor Google Docs",
    wantComponents: false,
    wantGlobalProperties: ["AbortController", "fetch"],
  });
  // The fetch options must be created inside the sandbox. An object passed in
  // from here is opaque to it, and fetch silently ignores its signal, so the
  // timeout would never fire. AbortSignal.timeout() needs a window, which the
  // sandbox lacks, hence the manual timer.
  // "same-origin" still follows Google's own redirects (it drops an authuser
  // the profile isn't signed in to), but fails a redirect to the sign-in page
  // without sending a request there.
  const sandboxFetch = Cu.evalInSandbox(
    `(url, signal) => fetch(url, { signal, mode: "same-origin" })`,
    sandbox
  );
  const controller = new sandbox.AbortController();
  const onDeadline = () =>
    controller.abort(
      new DOMException("The Google Docs view timed out.", "TimeoutError")
    );
  const timer = lazy.setTimeout(onDeadline, timeoutMs);
  viewDeadlines.add(onDeadline);
  let content;
  try {
    const response = await sandboxFetch(viewUrl.href, controller.signal);
    if (!response.ok) {
      return { document: null, status: "http-error" };
    }
    const contentType = response.headers.get("Content-Type") ?? "";
    if (!contentType.toLowerCase().startsWith("text/html")) {
      return { document: null, status: "not-html" };
    }
    content = await readCapped(response);
  } catch (error) {
    // The sandbox's errors become dead objects once it is nuked below.
    const ownError = new Error(error?.message);
    ownError.name = error?.name;
    throw ownError;
  } finally {
    lazy.clearTimeout(timer);
    viewDeadlines.delete(onDeadline);
    // Cancels the body download when we returned early without reading it.
    controller.abort();
    Cu.nukeSandbox(sandbox);
  }

  // A truncated view still parses: the parser closes whatever the cut left
  // open, so we get part of the document rather than none.
  const document = new DOMParser().parseFromString(content.html, "text/html");

  // A parsed document has no URL, so relative links and heading anchors would
  // resolve to nothing without a base.
  const base = document.createElement("base");
  base.href = documentUrl.href;
  document.head.prepend(base);

  unwrapRedirectLinks(document);

  return { document, status: content.truncated ? "truncated" : "success" };
}

/**
 * @param {Response} response
 * @returns {Promise<{ html: string, truncated: boolean }>} The body decoded as
 *   UTF-8, cut at MAX_DOWNLOAD_BYTES.
 */
async function readCapped(response) {
  // The body belongs to the sandbox and its async iterator isn't reachable
  // through Xrays, so read it with an explicit reader.
  const reader = response.body.getReader();
  const decoder = new TextDecoder();
  let html = "";
  let received = 0;
  for (
    let chunk = await reader.read();
    !chunk.done;
    chunk = await reader.read()
  ) {
    const remaining = MAX_DOWNLOAD_BYTES - received;
    if (chunk.value.byteLength > remaining) {
      await reader.cancel();
      html += decoder.decode(chunk.value.subarray(0, remaining));
      return { html, truncated: true };
    }
    received += chunk.value.byteLength;
    html += decoder.decode(chunk.value, { stream: true });
  }
  return { html: html + decoder.decode(), truncated: false };
}

/**
 * The view routes external links through google.com/url?q={target}. Points
 * them back at the real destination so the text and links don't carry the
 * redirect.
 *
 * @param {Document} document
 */
function unwrapRedirectLinks(document) {
  for (const anchor of document.querySelectorAll("a[href]")) {
    const url = URL.parse(anchor.href);
    if (url?.hostname !== "www.google.com" || url.pathname !== "/url") {
      continue;
    }
    const target = URL.parse(url.searchParams.get("q") ?? "");
    if (target?.protocol === "https:" || target?.protocol === "http:") {
      anchor.href = target.href;
    }
  }
}
