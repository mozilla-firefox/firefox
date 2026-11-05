/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  BackgroundPageThumbs: "resource://gre/modules/BackgroundPageThumbs.sys.mjs",
  PageThumbs: "resource://gre/modules/PageThumbs.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    logModule: "HistoryThumbnails",
    maxLogLevelPref: "browser.smartwindow.conversation.logLevel",
  })
);

/**
 * Captures (or reuses a disk-cached) page thumbnail for an og:image URL and
 * returns a `moz-page-thumb://` URI, or null when there is no usable thumbnail.
 *
 * Uses BackgroundPageThumbs so captures are cached on disk and reused across
 * conversations. Must run in the parent process.
 *
 * @param {string} thumbnail an og:image url
 * @returns {Promise<string|null>}
 *   A `moz-page-thumb://` URI on success, or null when there was no thumbnail
 *   URL, the capture threw, or the cached file is the empty-failure marker.
 */
export async function captureThumbnail(thumbnail) {
  if (!thumbnail) {
    return null;
  }

  try {
    await lazy.BackgroundPageThumbs.captureIfMissing(thumbnail, {
      isImage: true,
      backgroundColor: "#F9F9FA",
      settleWaitTime: 0,
      timeout: 10000,
    });

    const path = lazy.PageThumbs.getThumbnailPath(thumbnail);
    const { size } = await IOUtils.stat(path);
    if (size > 0) {
      return lazy.PageThumbs.getThumbnailURL(thumbnail);
    }
  } catch (err) {
    lazy.console.warn(`History image capture failed for ${thumbnail}`, err);
  }

  return null;
}
