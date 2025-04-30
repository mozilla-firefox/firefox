/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* globals browser, embedHelperLib */

embedHelperLib.initEmbedShim({
  shimId: "TiktokEmbed",
  scriptURL: "https://www.tiktok.com/embed.js",
  embedLogoURL: "https://smartblock.firefox.etp/tiktok.svg",
  embedSelector: ".tiktok-embed",
  isTestShim: false,
});
