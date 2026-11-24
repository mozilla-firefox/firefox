/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const { BrowserLoader } = ChromeUtils.importESModule(
  "resource://devtools/shared/loader/browser-loader.sys.mjs"
);

// Module Loader
const require = BrowserLoader({
  baseURI: "resource://devtools/client/dom/",
  window,
}).require;

XPCOMUtils.defineConstant(this, "require", require);

// Localization
const domL10n = new Localization(["devtools/client/dom.ftl"], true);
this.l10n = { getStr: id => domL10n.formatValueSync(id) };

// Load DOM panel content
require("resource://devtools/client/dom/content/dom-view.js");
