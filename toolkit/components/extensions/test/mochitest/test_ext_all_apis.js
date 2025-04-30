/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

// Tests whether not too many APIs are visible by default.
// This file is used by test_ext_all_apis.html in browser/ and mobile/android/,
// which may modify the following variables to add or remove expected APIs.
/* globals expectedContentApisTargetSpecific */
/* globals expectedBackgroundApisTargetSpecific */

// Generates a list of expectations.
function generateExpectations(list) {
  return list
    .reduce((allApis, path) => {
      return allApis.concat(`browser.${path}`, `chrome.${path}`);
    }, [])
    .sort();
}

let expectedCommonApis = [
  "extension.getURL",
  "extension.inIncognitoContext",
  "extension.lastError",
  "i18n.detectLanguage",
  "i18n.getAcceptLanguages",
  "i18n.getMessage",
  "i18n.getUILanguage",
  "runtime.OnInstalledReason",
  "runtime.OnRestartRequiredReason",
  "runtime.PlatformArch",
  "runtime.PlatformOs",
  "runtime.RequestUpdateCheckStatus",
  "runtime.getManifest",
  "runtime.connect",
  "runtime.getFrameId",
  "runtime.getURL",
  "runtime.id",
  "runtime.lastError",
  "runtime.onConnect",
  "runtime.onMessage",
  "runtime.sendMessage",
  // browser.test is only available in xpcshell or when
  // Cu.isInAutomation is true.
  "test.assertDeepEq",
  "test.assertEq",
  "test.assertFalse",
  "test.assertRejects",
  "test.assertThrows",
  "test.assertTrue",
  "test.fail",
  "test.log",
  "test.notifyFail",
  "test.notifyPass",
  "test.onMessage",
  "test.runTests",
  "test.sendMessage",
  "test.succeed",
  "test.withHandlingUserInput",
];

let expectedContentApis = [
  ...expectedCommonApis,
  ...expectedContentApisTargetSpecific,
];

let expectedBackgroundApis = [
  ...expectedCommonApis,
  ...expectedBackgroundApisTargetSpecific,
  "contentScripts.register",
  "experiments.APIChildScope",
  "experiments.APIEvent",
  "experiments.APIParentScope",
  "extension.ViewType",
  "extension.getBackgroundPage",
  "extension.getViews",
  "extension.isAllowedFileSchemeAccess",
  "extension.isAllowedIncognitoAccess",
  // Note: extensionTypes is not visible in Chrome.
  "extensionTypes.CSSOrigin",
  "extensionTypes.ExecutionWorld",
  "extensionTypes.ImageFormat",
  "extensionTypes.RunAt",
  "management.ExtensionDisabledReason",
  "management.ExtensionInstallType",
  "management.ExtensionType",
  "management.getSelf",
  "management.uninstallSelf",
  "permissions.getAll",
  "permissions.contains",
  "permissions.request",
  "permissions.remove",
  "permissions.onAdded",
  "permissions.onRemoved",
  "runtime.ContextType",
  "runtime.getBackgroundPage",
  "runtime.getBrowserInfo",
  "runtime.getContexts",
  "runtime.getPlatformInfo",
  "runtime.onConnectExternal",
  "runtime.onInstalled",
  "runtime.onMessageExternal",
  "runtime.onPerformanceWarning",
  "runtime.onStartup",
  "runtime.onSuspend",
  "runtime.onSuspendCanceled",
  "runtime.onUpdateAvailable",
  "runtime.openOptionsPage",
  "runtime.reload",
  "runtime.setUninstallURL",
  "runtime.OnPerformanceWarningCategory",
  "runtime.OnPerformanceWarningSeverity",
  "theme.getCurrent",
  "theme.onUpdated",
  "types.LevelOfControl",
  "types.SettingScope",
];

// APIs that are exposed to MV2 by default, but not to MV3.
const mv2onlyBackgroundApis = new Set([
  "extension.getURL",
  "extension.lastError",
  "contentScripts.register",
  "tabs.executeScript",
  "tabs.insertCSS",
  "tabs.removeCSS",
]);
let expectedBackgroundApisMV3 = expectedBackgroundApis.filter(
  path => !mv2onlyBackgroundApis.has(path)
);

function sendAllApis() {
  function isEvent(key, val) {
    if (!/^on[A-Z]/.test(key)) {
      return false;
    }
    let eventKeys = [];
    for (let prop in val) {
      eventKeys.push(prop);
    }
    eventKeys = eventKeys.sort().join();
    return eventKeys === "addListener,hasListener,removeListener";
  }
  // Some items are removed from the namespaces in the lazy getters after the first get.  This
  // in one case, the events namespace, leaves a namespace that is empty.  Make sure we don't
  // consider those as a part of our testing.
  function isEmptyObject(val) {
    return val !== null && typeof val == "object" && !Object.keys(val).length;
  }
  function mayRecurse(key, val) {
    if (Object.keys(val).filter(k => !/^[A-Z\-0-9_]+$/.test(k)).length === 0) {
      // Don't recurse on constants and empty objects.
      return false;
    }
    return !isEvent(key, val);
  }

  let results = [];
  function diveDeeper(path, obj) {
    for (const [key, val] of Object.entries(obj)) {
      if (typeof val == "object" && val !== null && mayRecurse(key, val)) {
        diveDeeper(`${path}.${key}`, val);
      } else if (val !== undefined && !isEmptyObject(val)) {
        results.push(`${path}.${key}`);
      }
    }
  }
  diveDeeper("browser", browser);
  diveDeeper("chrome", chrome);
  browser.test.sendMessage("allApis", results.sort());
  browser.test.sendMessage("namespaces", browser === chrome);
}

add_task(async function setup() {
  // This test enumerates all APIs and may access a deprecated API. Just log a
  // warning instead of throwing.
  await ExtensionTestUtils.failOnSchemaWarnings(false);
});

add_task(async function test_enumerate_content_script_apis() {
  let extensionData = {
    manifest: {
      content_scripts: [
        {
          matches: ["http://mochi.test/*/file_sample.html"],
          js: ["contentscript.js"],
          run_at: "document_start",
        },
      ],
    },
    files: {
      "contentscript.js": sendAllApis,
    },
  };
  let extension = ExtensionTestUtils.loadExtension(extensionData);
  await extension.startup();

  let win = window.open("file_sample.html");
  let actualApis = await extension.awaitMessage("allApis");
  win.close();
  let expectedApis = generateExpectations(expectedContentApis);
  isDeeply(actualApis, expectedApis, "content script APIs");

  let sameness = await extension.awaitMessage("namespaces");
  ok(sameness, "namespaces are same object");

  await extension.unload();
});

add_task(async function test_enumerate_background_script_apis() {
  let extensionData = {
    background: sendAllApis,
  };
  let extension = ExtensionTestUtils.loadExtension(extensionData);
  await extension.startup();

  let actualApis = await extension.awaitMessage("allApis");
  let expectedApis = generateExpectations(expectedBackgroundApis);
  isDeeply(actualApis, expectedApis, "background script APIs");

  let sameness = await extension.awaitMessage("namespaces");
  ok(!sameness, "namespaces are different objects");

  await extension.unload();
});

add_task(async function test_enumerate_background_script_apis_mv3() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.manifestV3.enabled", true]],
  });
  let extensionData = {
    background: sendAllApis,
    manifest: {
      manifest_version: 3,

      // Features that expose APIs in MV2, but should not do anything with MV3.
      browser_action: {},
      user_scripts: {},
    },
  };
  let extension = ExtensionTestUtils.loadExtension(extensionData);
  await extension.startup();

  let actualApis = await extension.awaitMessage("allApis");
  let expectedApis = generateExpectations(expectedBackgroundApisMV3);
  isDeeply(actualApis, expectedApis, "background script APIs in MV3");

  let sameness = await extension.awaitMessage("namespaces");
  ok(sameness, "namespaces are same object");

  await extension.unload();
  await SpecialPowers.popPrefEnv();
});
