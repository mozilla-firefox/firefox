/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* globals browser, embedHelperLib */

if (!window.smartblockInstagramShimInitialized) {
  // Guard against this script running multiple times
  window.smartblockInstagramShimInitialized = true;

  embedHelperLib.initEmbedShim({
    shimId: "InstagramEmbed",
    scriptURL: "https://www.instagram.com/embed.js",
    embedLogoURL: "https://smartblock.firefox.etp/instagram.svg",
    embedSelector: ".instagram-media",
    isTestShim: false,
  });
}
