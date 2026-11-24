/**
 * @file Reject uses of unknown interfaces on Ci and properties of those
 * interfaces.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import os from "os";
import helpers from "../helpers.mjs";

// These interfaces are all platform specific, so may be not present
// on all platforms.
const platformSpecificInterfaces = new Map([
  ["nsIAboutThirdParty", "windows"],
  ["nsIAboutWindowsMessages", "windows"],
  ["nsIDefaultAgent", "windows"],
  ["nsIJumpListBuilder", "windows"],
  ["nsITaskbarWindowPreview", "windows"],
  ["nsIWindowsAlertsService", "windows"],
  ["nsIWindowsAlertNotification", "windows"],
  ["nsIWindowsMutexFactory", "windows"],
  ["nsIWinAppHelper", "windows"],
  ["nsIWinBackgroundTaskRegistrar", "windows"],
  ["nsIWinTaskbar", "windows"],
  ["nsIWinTaskSchedulerService", "windows"],
  ["nsIWindowsRegKey", "windows"],
  ["nsIWindowsPackageManager", "windows"],
  ["nsIWindowsShellService", "windows"],
  ["nsIWindowsTestDebug", "windows"],
  ["nsIWindowsUIUtils", "windows"],
  ["nsIASWebAuthSessionRequest", "darwin"],
  ["nsIAccessibleMacEvent", "darwin"],
  ["nsIAccessibleMacInterface", "darwin"],
  ["nsICocoaAppShellTestUtils", "darwin"],
  ["nsILocalFileMac", "darwin"],
  ["nsIAccessibleMacEvent", "darwin"],
  ["nsIKeychainMigrationUtils", "darwin"],
  ["nsIMacAttributionService", "darwin"],
  ["nsIMacShellService", "darwin"],
  ["nsIMacDockSupport", "darwin"],
  ["nsIMacFinderProgress", "darwin"],
  ["nsIMacPreferencesReader", "darwin"],
  ["nsIMacSharingService", "darwin"],
  ["nsIMacUserActivityUpdater", "darwin"],
  ["nsIMacWebAppUtils", "darwin"],
  ["nsISecondaryTileService", "windows"],
  ["nsISecondaryTileListener", "windows"],
  ["nsIStandaloneNativeMenu", "darwin"],
  ["nsITouchBarHelper", "darwin"],
  ["nsITouchBarInput", "darwin"],
  ["nsITouchBarUpdater", "darwin"],
  ["mozISandboxReporter", "linux"],
  ["nsIApplicationChooser", "linux"],
  ["nsIAvailableMemoryWatcherTestingLinux", "linux"],
  ["nsIGNOMEShellService", "linux"],
  ["nsIGtkTaskbarProgress", "linux"],

  // These are used in the ESLint test code.
  ["amIFoo", "any"],
  ["nsIMeh", "any"],
  // Can't easily detect android builds from ESLint at the moment.
  ["nsIGeckoViewBridge", "any"],
  ["nsIGeckoViewView", "any"],
  ["nsIOhttpClientTest", "any"],
  // Code coverage is enabled only for certain builds (MOZ_CODE_COVERAGE).
  ["nsICodeCoverage", "any"],
  // Layout debugging is enabled only for certain builds (MOZ_LAYOUT_DEBUGGER).
  ["nsILayoutDebuggingTools", "any"],
  // Sandbox test is only enabled for certain configurations (MOZ_SANDBOX,
  // MOZ_DEBUG, ENABLE_TESTS).
  ["mozISandboxTest", "any"],
  // These are for MOZ_ENTERPRISE
  ["nsIImeiProvider", "any"],
]);

function interfaceHasProperty(interfaceName, propertyName) {
  // `Ci.nsIFoo.number` is valid, it returns the iid.
  if (propertyName == "number") {
    return true;
  }

  let interfaceInfo = helpers.xpidlData.get(interfaceName);

  if (!interfaceInfo) {
    return true;
  }

  // If the property is not in the lists of consts for this interface, check
  // any parents as well.
  if (!interfaceInfo.consts.find(e => e.name === propertyName)) {
    if (interfaceInfo.parent && interfaceInfo.parent != "nsISupports") {
      return interfaceHasProperty(interfaceName.parent, propertyName);
    }
    return false;
  }
  return true;
}

export default {
  meta: {
    docs: {
      url: "https://firefox-source-docs.mozilla.org/code-quality/lint/linters/eslint-plugin-mozilla/rules/valid-ci-uses.html",
    },
    messages: {
      missingInterface:
        "{{ interface }} is defined in this rule's platform specific list, but is not available",
      unknownInterface: "Use of unknown interface Ci.{{ interface }}",
      unknownProperty:
        "Use of unknown property Ci.{{ interface }}.{{ property }}",
    },
    schema: [],
    type: "problem",
  },

  create(context) {
    return {
      MemberExpression(node) {
        if (
          node.computed === false &&
          node.type === "MemberExpression" &&
          node.object.type === "Identifier" &&
          node.object.name === "Ci" &&
          node.property.type === "Identifier" &&
          node.property.name.includes("I")
        ) {
          if (!helpers.xpidlData.get(node.property.name)) {
            let platformSpecific = platformSpecificInterfaces.get(
              node.property.name
            );
            if (!platformSpecific) {
              context.report({
                node,
                messageId: "unknownInterface",
                data: {
                  interface: node.property.name,
                },
              });
            } else if (platformSpecific == os.platform()) {
              context.report({
                node,
                messageId: "missingInterface",
                data: {
                  interface: node.property.name,
                },
              });
            }
          }
        }

        if (
          node.computed === false &&
          node.object.type === "MemberExpression" &&
          node.object.object.type === "Identifier" &&
          node.object.object.name === "Ci" &&
          node.object.property.type === "Identifier" &&
          node.object.property.name.includes("I") &&
          node.property.type === "Identifier"
        ) {
          if (
            !interfaceHasProperty(node.object.property.name, node.property.name)
          ) {
            context.report({
              node,
              messageId: "unknownProperty",
              data: {
                interface: node.object.property.name,
                property: node.property.name,
              },
            });
          }
        }
      },
    };
  },
};
