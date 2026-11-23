/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

const RS_AITAB_COLLECTION = "ai-window-aitab";
export const HEADING_FONT_RECORD_NAME = "MozillaHeadline-CondensedSemiBold.ttf";

let headingFontPromise = null;

/**
 * Downloads the heading font from Remote Settings. about:smartpage has a null
 * origin, so it cannot load the CDN file itself: font loads are CORS checked.
 * The download is shared by every page and cached on disk by Remote Settings.
 * If the font record is missing or the download fails,
 * the next call tries the download again.
 *
 * @returns {Promise<?ArrayBuffer>} Null when the record is not available.
 */
export function getHeadingFont() {
  if (!headingFontPromise) {
    headingFontPromise = (async () => {
      const client = lazy.RemoteSettings(RS_AITAB_COLLECTION);
      const [record] = await client.get({
        filters: { name: HEADING_FONT_RECORD_NAME },
      });
      if (!record) {
        headingFontPromise = null;
        return null;
      }
      const { buffer } = await client.attachments.download(record, {
        fallbackToCache: true,
      });
      return buffer;
    })().catch(error => {
      headingFontPromise = null;
      throw error;
    });
  }
  return headingFontPromise;
}

export function resetHeadingFontForTesting() {
  headingFontPromise = null;
}
