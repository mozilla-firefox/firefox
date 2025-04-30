/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* globals browser, embedHelperLib */

embedHelperLib.initEmbedShim({
  shimId: "EmbedTestShim",
  scriptURL:
    "https://itisatracker.org/browser/browser/extensions/webcompat/tests/browser/embed_test.js",
  embedLogoURL: "https://smartblock.firefox.etp/instagram.svg", // Use Instagram logo as test shim
  embedSelector: ".broken-embed-content",
  isTestShim: true,
});
