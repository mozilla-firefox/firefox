/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

global.EventManager = ExtensionCommon.EventManager;

extensions.registerModules({
  downloads: {
    url: "chrome://extensions/content/child/ext-downloads.js",
    scopes: ["addon_child"],
    paths: [["downloads", "onDeterminingFilename"]],
  },
  backgroundPage: {
    url: "chrome://extensions/content/child/ext-backgroundPage.js",
    scopes: ["addon_child"],
    manifest: ["background"],
    paths: [
      ["extension", "getBackgroundPage"],
      ["runtime", "getBackgroundPage"],
    ],
  },
  contentScripts: {
    url: "chrome://extensions/content/child/ext-contentScripts.js",
    scopes: ["addon_child"],
    paths: [["contentScripts"]],
  },
  declarativeNetRequest: {
    url: "chrome://extensions/content/child/ext-declarativeNetRequest.js",
    scopes: ["addon_child"],
    paths: [["declarativeNetRequest"]],
  },
  extension: {
    url: "chrome://extensions/content/child/ext-extension.js",
    scopes: ["addon_child", "content_child", "devtools_child"],
    paths: [["extension"]],
  },
  i18n: {
    url: "chrome://extensions/content/parent/ext-i18n.js",
    scopes: ["addon_child", "content_child", "devtools_child"],
    paths: [["i18n"]],
  },
  publicSuffix: {
    url: "chrome://extensions/content/child/ext-publicSuffix.js",
    scopes: ["addon_child"],
    paths: [["publicSuffix"]],
  },
  runtime: {
    url: "chrome://extensions/content/child/ext-runtime.js",
    scopes: ["addon_child", "content_child", "devtools_child"],
    paths: [["runtime"]],
  },
  scripting: {
    url: "chrome://extensions/content/child/ext-scripting.js",
    scopes: ["addon_child"],
    paths: [["scripting"]],
  },
  storage: {
    url: "chrome://extensions/content/child/ext-storage.js",
    scopes: ["addon_child", "content_child", "devtools_child"],
    paths: [["storage"]],
  },
  test: {
    url: "chrome://extensions/content/child/ext-test.js",
    scopes: ["addon_child", "content_child", "devtools_child"],
    paths: [["test"]],
  },
  userScripts: {
    url: "chrome://extensions/content/child/ext-userScripts.js",
    scopes: ["addon_child"],
    paths: [["userScripts", "register"]],
  },
  userScriptsContent: {
    url: "chrome://extensions/content/child/ext-userScripts-content.js",
    scopes: ["content_child"],
    paths: [["userScripts", "onBeforeScript"]],
  },
  webRequest: {
    url: "chrome://extensions/content/child/ext-webRequest.js",
    scopes: ["addon_child"],
    paths: [["webRequest"]],
  },
});

if (AppConstants.MOZ_BUILD_APP === "browser") {
  extensions.registerModules({
    identity: {
      url: "chrome://extensions/content/child/ext-identity.js",
      scopes: ["addon_child"],
      paths: [["identity"]],
    },
  });
}
