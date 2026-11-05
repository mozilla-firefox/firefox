/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Various parts here are run in the content process.
/* global content */

import { TestUtils } from "resource://testing-common/TestUtils.sys.mjs";

export var Preferences = {
  init() {
    let panes = [
      ["paneSync"],
      ["paneSearch"],
      ["panePrivacy"],
      ["panePrivacy", cacheGroup],
      ["panePrivacy", clearRecentHistoryDialog],
      ["paneConnectionSecurity", connectionDialog],
      ["paneConnectionSecurity", certManager],
      ["paneConnectionSecurity", deviceManager],
      ["paneTabsBrowsing", tabsGroup],
    ];

    for (let [primary, customFn] of panes) {
      let configName = primary.replace(/^pane/, "prefs");
      if (customFn) {
        configName += "-" + customFn.name;
      }
      this.configurations[configName] = {};
      this.configurations[configName].selectors = ["#browser"];
      if (primary == "panePrivacy" && customFn) {
        this.configurations[configName].applyConfig = async () => {
          return { todo: `${configName} times out on the try server` };
        };
      } else {
        this.configurations[configName].applyConfig = prefHelper.bind(
          null,
          primary,
          customFn
        );
      }
    }
  },

  configurations: {},
};

let prefHelper = async function (primary, customFn = null) {
  let browserWindow = Services.wm.getMostRecentWindow("navigator:browser");
  let selectedBrowser = browserWindow.gBrowser.selectedBrowser;

  // close any dialog that might still be open
  await selectedBrowser.documentGlobal.SpecialPowers.spawn(
    selectedBrowser,
    [],
    async function () {
      // Check that gSubDialog is defined on the content window
      // and that there is an open dialog to close
      if (!content.window.gSubDialog || !content.window.gSubDialog._topDialog) {
        return;
      }
      content.window.gSubDialog.close();
    }
  );

  let readyPromise = null;
  if (selectedBrowser.currentURI.specIgnoringRef == "about:preferences") {
    if (
      selectedBrowser.currentURI.spec ==
      "about:preferences#" + primary.replace(/^pane/, "")
    ) {
      // We're already on the correct pane.
      readyPromise = Promise.resolve();
    } else {
      readyPromise = new Promise(r => browserWindow.requestAnimationFrame(r));
    }
  } else {
    readyPromise = TestUtils.topicObserved("sync-pane-loaded");
  }

  browserWindow.openPreferences(primary);

  await readyPromise;

  if (customFn) {
    let customPaintPromise = paintPromise(browserWindow);
    let result = await customFn(selectedBrowser);
    await customPaintPromise;
    return result;
  }
  return undefined;
};

function paintPromise(browserWindow) {
  return new Promise(resolve => {
    browserWindow.addEventListener(
      "MozAfterPaint",
      function () {
        resolve();
      },
      { once: true }
    );
  });
}

async function tabsGroup(aBrowser) {
  await aBrowser.documentGlobal.SpecialPowers.spawn(
    aBrowser,
    [],
    async function () {
      content.document
        .querySelector('setting-group[groupid="tabs"]')
        .scrollIntoView();
    }
  );
}

async function cacheGroup(aBrowser) {
  await aBrowser.documentGlobal.SpecialPowers.spawn(
    aBrowser,
    [],
    async function () {
      content.document
        .querySelector('setting-group[groupid="cookiesAndSiteData"]')
        .scrollIntoView();
    }
  );
}

async function connectionDialog(aBrowser) {
  await aBrowser.documentGlobal.SpecialPowers.spawn(
    aBrowser,
    [],
    async function () {
      content.document.getElementById("connectionSettings").click();
    }
  );
}

async function clearRecentHistoryDialog(aBrowser) {
  await aBrowser.documentGlobal.SpecialPowers.spawn(
    aBrowser,
    [],
    async function () {
      content.document.getElementById("clearSiteDataButton").click();
    }
  );
}

async function certManager(aBrowser) {
  await aBrowser.documentGlobal.SpecialPowers.spawn(
    aBrowser,
    [],
    async function () {
      content.document.getElementById("viewCertificatesButton").click();
    }
  );
}

async function deviceManager(aBrowser) {
  await aBrowser.documentGlobal.SpecialPowers.spawn(
    aBrowser,
    [],
    async function () {
      content.document.getElementById("viewSecurityDevicesButton").click();
    }
  );
}
