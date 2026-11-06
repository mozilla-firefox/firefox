/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { isJsonlMimeType } from "resource://devtools/client/shared/jsonl-mime-types.mjs";

const gPrefs = {};

XPCOMUtils.defineLazyPreferenceGetter(
  gPrefs,
  "gEnabled",
  "devtools.jsonview.enabled"
);

const JSON_VIEW_MIME_TYPE = "application/vnd.mozilla.json.view";
const JSONLINES_VIEW_MIME_TYPE = "application/vnd.mozilla.jsonlines.view";

// Content types that say nothing about the actual format, either because the
// server doesn't know the .jsonl extension or sent no Content-Type at all.
// The extension is only trusted for these, so an explicit type such as
// text/html served at a .jsonl URL is honored (bug 2088590).
const GENERIC_CONTENT_TYPES = new Set([
  "",
  "text/plain",
  "application/octet-stream",
  "application/x-unknown-content-type",
]);

function getContentDisposition(channel) {
  try {
    return channel.contentDisposition;
  } catch (e) {
    // Channel doesn't support content dispositions
    return null;
  }
}

/**
 * This component represents a sniffer (implements nsIContentSniffer
 * interface) responsible for changing top level 'application/json'
 * and JSON Lines document types to internal types consumed by the
 * JSON View component.
 *
 * This is done in the .js file rather than a .sys.mjs to avoid creating
 * a compartment at startup when no JSON is being viewed.
 */
export class Sniffer {
  getMIMETypeFromContent(request) {
    if (request instanceof Ci.nsIChannel) {
      let contentType;
      try {
        contentType = request.contentType;
      } catch (e) {
        // There is no content type when no response was received, e.g. when
        // the load failed.
        return "";
      }

      // JSON View is enabled only for top level loads only.
      if (
        gPrefs.gEnabled &&
        request.loadInfo?.isTopLevelLoad &&
        request.loadFlags & Ci.nsIChannel.LOAD_DOCUMENT_URI &&
        getContentDisposition(request) != Ci.nsIChannel.DISPOSITION_ATTACHMENT
      ) {
        // Check the response content type and if it's a valid type
        // such as application/json or application/manifest+json
        // change it to new internal type consumed by JSON View.
        if (/^application\/(?:.+\+)?json$/.test(contentType)) {
          return JSON_VIEW_MIME_TYPE;
        }

        // Check if this is a JSON Lines content-type (one JSON value per line).
        let isJsonlines = isJsonlMimeType(contentType);
        if (!isJsonlines && GENERIC_CONTENT_TYPES.has(contentType)) {
          try {
            isJsonlines =
              request.URI.QueryInterface(
                Ci.nsIURL
              ).fileExtension.toLowerCase() === "jsonl";
          } catch (ex) {
            // The URI scheme doesn't support nsIURL (e.g. some data: URIs).
          }
        }
        if (isJsonlines) {
          return JSONLINES_VIEW_MIME_TYPE;
        }
      } else if (contentType === JSON_VIEW_MIME_TYPE) {
        return "application/json";
      } else if (contentType === JSONLINES_VIEW_MIME_TYPE) {
        return "application/jsonl";
      }
    }

    return "";
  }
}

Sniffer.prototype.QueryInterface = ChromeUtils.generateQI([
  "nsIContentSniffer",
]);
