/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

/**
 * This is a manual mapping from chrome:// and resource:// URIs
 * to the corresponding source files for all modules which the
 * build_paths.js script can't match automatically.
 *
 * If TypeScript or your editor can't find your (new) module in
 * `tools/@types/generated/tspaths.json` try running `mach ts update` first.
 */
exports.fixed = {
  "chrome://global/content/ml/NLPUtils.sys.mjs": [
    "toolkit/components/ml/content/nlp/Utils.sys.mjs",
  ],
  "chrome://global/content/ml/Utils.sys.mjs": [
    "toolkit/components/ml/content/Utils.sys.mjs",
  ],
  "chrome://mochikit/content/tests/SimpleTest/SimpleTest.js": [
    "testing/mochitest/tests/SimpleTest/SimpleTest.js",
  ],
  "chrome://mochitests/content/browser/remote/shared/messagehandler/test/browser/resources/modules/ModuleRegistry.sys.mjs":
    [
      "remote/shared/messagehandler/test/browser/resources/modules/ModuleRegistry.sys.mjs",
    ],
  "chrome://remote/content/webdriver-bidi/modules/ModuleRegistry.sys.mjs": [
    "remote/webdriver-bidi/modules/ModuleRegistry.sys.mjs",
  ],
  "resource:///modules/ExtensionBrowsingData.sys.mjs": [
    "browser/components/extensions/ExtensionBrowsingData.sys.mjs",
  ],
  "resource://autofill/FormAutofillPrompter.sys.mjs": [
    "toolkit/components/formautofill/default/FormAutofillPrompter.sys.mjs",
  ],
  "resource://autofill/FormAutofillStorage.sys.mjs": [
    "toolkit/components/formautofill/default/FormAutofillStorage.sys.mjs",
  ],
  "resource://devtools/foo.js": undefined,
  "resource://devtools/server/actors/descriptors/webextension.js": [
    "devtools/server/actors/descriptors/webextension.js",
  ],
  "resource://gre/modules/CrashManager.sys.mjs": [
    "toolkit/components/crashes/CrashManager.in.sys.mjs",
  ],
  "resource://gre/modules/SomeModule.sys.mjs": undefined,
  "resource://gre/modules/components-utils/ClientEnvironment.sys.mjs": [
    "toolkit/components/utils/ClientEnvironment.sys.mjs",
  ],
  "resource://gre/modules/worker/myModule.js": undefined,
  "resource://gre/modules/workers/Logger.js": undefined,
  "resource://gre/modules/workers/SimpleTest.js": undefined,
  "resource://passwordmgr/passwordstorage.sys.mjs": [
    "toolkit/components/passwordmgr/storage-desktop.sys.mjs",
  ],
  "resource://services-common/utils.sys.mjs": ["services/common/utils.sys.mjs"],
  "resource://services-settings/Utils.sys.mjs": [
    "services/settings/Utils.sys.mjs",
  ],
  "resource://services-sync/util.sys.mjs": [
    "services/sync/modules/util.sys.mjs",
  ],
  "resource://test/es6module.js": ["js/xpconnect/tests/unit/es6module.js"],
  "resource://test/module.sys.mjs": undefined,
  "resource://test/not_found.mjs": undefined,
  "resource://test/es6module_dynamic_import_syntax_error.js": undefined,
  "resource://test/es6module_parse_error.js": undefined,
  "resource://test/es6module_parse_error_in_import.js": undefined,
  "resource://testing-common/AppInfo.sys.mjs": [
    "testing/modules/AppInfo.sys.mjs",
  ],
  "resource://testing-common/AppUiTestDelegate.sys.mjs": [
    "mobile/shared/modules/test/AppUiTestDelegate.sys.mjs",
  ],
  "resource://testing-common/Assert.sys.mjs": [
    "testing/modules/Assert.sys.mjs",
  ],
  "resource://testing-common/dbg-actors.js": ["testing/xpcshell/dbg-actors.js"],
  "resource://testing-common/dom/quota/test/modules/PrefUtils.sys.mjs": [
    "dom/quota/test/modules/system/PrefUtils.sys.mjs",
  ],
  "resource://testing-common/dom/quota/test/modules/Utils.sys.mjs": [
    "dom/quota/test/modules/system/Utils.sys.mjs",
  ],
  "resource://testing-common/services/sync/utils.sys.mjs": [
    "services/sync/modules-testing/utils.sys.mjs",
  ],
  // Map types from node_modules into lit.all.mjs
  "chrome://global/content/vendor/lit.all.mjs": [
    "toolkit/content/widgets/vendor/lit.all.d.ts",
  ],
  "@lit/reactive-element": [
    "node_modules/@lit/reactive-element/reactive-element",
  ],
  "lit/*": ["node_modules/lit/*"],
  "lit-html": ["node_modules/lit-html/lit-html"],
  // Needs to be last, to prefer fixed paths above for autocomplete.
  "moz-src:///*": ["*"],
};
