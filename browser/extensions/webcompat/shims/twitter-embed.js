/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* globals browser, embedHelperLib */

embedHelperLib.initEmbedShim({
  shimId: "TwitterEmbed",
  scriptURL: "https://platform.twitter.com/widgets.js",
  embedLogoURL: "https://smartblock.firefox.etp/x-logo.svg",
  embedSelector: ".twitter-tweet, .twitter-timeline",
  isTestShim: false,
});
