/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_QUOTA_USAGE_HOST = "example.com";
const TEST_QUOTA_USAGE_ORIGIN = "https://" + TEST_QUOTA_USAGE_HOST;
const TEST_QUOTA_USAGE_URL =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    TEST_QUOTA_USAGE_ORIGIN
  ) + "/site_data_test.html";
const TEST_OFFLINE_HOST = "example.org";
const TEST_OFFLINE_ORIGIN = "https://" + TEST_OFFLINE_HOST;
const TEST_OFFLINE_URL =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    TEST_OFFLINE_ORIGIN
  ) + "/offline/offline.html";
const TEST_SERVICE_WORKER_URL =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    TEST_OFFLINE_ORIGIN
  ) + "/service_worker_test.html";

const REMOVE_DIALOG_URL =
  "chrome://browser/content/preferences/dialogs/siteDataRemoveSelected.xhtml";

ChromeUtils.defineESModuleGetters(this, {
  SiteDataTestUtils: "resource://testing-common/SiteDataTestUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "serviceWorkerManager",
  "@mozilla.org/serviceworkers/manager;1",
  Ci.nsIServiceWorkerManager
);

function promiseSiteDataManagerSitesUpdated() {
  return TestUtils.topicObserved("sitedatamanager:sites-updated", () => true);
}

function is_element_visible(aElement, aMsg) {
  isnot(aElement, null, "Element should not be null, when checking visibility");
  ok(!BrowserTestUtils.isHidden(aElement), aMsg);
}

function is_element_hidden(aElement, aMsg) {
  isnot(aElement, null, "Element should not be null, when checking visibility");
  ok(BrowserTestUtils.isHidden(aElement), aMsg);
}

function promiseLoadSubDialog(aURL) {
  return new Promise(resolve => {
    content.gSubDialog._dialogStack.addEventListener(
      "dialogopen",
      function dialogopen(aEvent) {
        if (
          aEvent.detail.dialog._frame.contentWindow.location == "about:blank"
        ) {
          return;
        }
        content.gSubDialog._dialogStack.removeEventListener(
          "dialogopen",
          dialogopen
        );

        is(
          aEvent.detail.dialog._frame.contentWindow.location.toString(),
          aURL,
          "Check the proper URL is loaded"
        );

        // Check visibility
        is_element_visible(aEvent.detail.dialog._overlay, "Overlay is visible");

        // Check that stylesheets were injected
        let expectedStyleSheetURLs =
          aEvent.detail.dialog._injectedStyleSheets.slice(0);
        for (let styleSheet of aEvent.detail.dialog._frame.contentDocument
          .styleSheets) {
          let i = expectedStyleSheetURLs.indexOf(styleSheet.href);
          if (i >= 0) {
            info("found " + styleSheet.href);
            expectedStyleSheetURLs.splice(i, 1);
          }
        }
        is(
          expectedStyleSheetURLs.length,
          0,
          "All expectedStyleSheetURLs should have been found"
        );

        // Wait for the next event tick to make sure the remaining part of the
        // testcase runs after the dialog gets ready for input.
        executeSoon(() => resolve(aEvent.detail.dialog._frame.contentWindow));
      }
    );
  });
}

function openPreferencesViaOpenPreferencesAPI(aPane, aOptions) {
  return new Promise(resolve => {
    let finalPrefPaneLoaded = TestUtils.topicObserved(
      "sync-pane-loaded",
      () => true
    );
    gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
    openPreferences(aPane);
    let newTabBrowser = gBrowser.selectedBrowser;

    newTabBrowser.addEventListener(
      "Initialized",
      function () {
        newTabBrowser.contentWindow.addEventListener(
          "load",
          async function () {
            let win = gBrowser.contentWindow;
            let selectedPane = win.gLastCategory?.category;
            await finalPrefPaneLoaded;
            if (!aOptions || !aOptions.leaveOpen) {
              gBrowser.removeCurrentTab();
            }
            resolve({ selectedPane });
          },
          { once: true }
        );
      },
      { capture: true, once: true }
    );
  });
}

async function openSiteDataSettingsDialog() {
  let doc = gBrowser.selectedBrowser.contentDocument;
  let settingsBtn = doc.getElementById("siteDataSettings");
  let dialogOverlay = content.gSubDialog._preloadDialog._overlay;
  let dialogLoadPromise = promiseLoadSubDialog(
    "chrome://browser/content/preferences/dialogs/siteDataSettings.xhtml"
  );
  let dialogInitPromise = TestUtils.topicObserved(
    "sitedata-settings-init",
    () => true
  );
  let fullyLoadPromise = Promise.all([
    dialogLoadPromise,
    dialogInitPromise,
  ]).then(() => {
    is_element_visible(dialogOverlay, "The Settings dialog should be visible");
  });
  // Wait for the dialog button to be enabled. It is disabled while
  // SiteDataManager updates the site list and usage.
  await BrowserTestUtils.waitForMutationCondition(
    settingsBtn,
    {
      attributeFilter: ["disabled"],
    },
    () => {
      return !settingsBtn.disabled;
    }
  );
  settingsBtn.click();
  await fullyLoadPromise;
}

function promiseSettingsDialogClose() {
  return new Promise(resolve => {
    let win = gBrowser.selectedBrowser.contentWindow;
    let dialogOverlay = win.gSubDialog._topDialog._overlay;
    let dialogWin = win.gSubDialog._topDialog._frame.contentWindow;
    dialogWin.addEventListener(
      "unload",
      function unload() {
        if (
          dialogWin.document.documentURI ===
          "chrome://browser/content/preferences/dialogs/siteDataSettings.xhtml"
        ) {
          is_element_hidden(
            dialogOverlay,
            "The Settings dialog should be hidden"
          );
          resolve();
        }
      },
      { once: true }
    );
  });
}

function assertSitesListed(doc, hosts) {
  let frameDoc = content.gSubDialog._topDialog._frame.contentDocument;
  let removeAllBtn = frameDoc.getElementById("removeAll");
  let sitesList = frameDoc.getElementById("sitesList");
  let totalSitesNumber = sitesList.getElementsByTagName("richlistitem").length;
  is(totalSitesNumber, hosts.length, "Should list the right sites number");
  hosts.forEach(host => {
    let site = sitesList.querySelector(`richlistitem[host="${host}"]`);
    ok(site, `Should list the site of ${host}`);
  });
  is(removeAllBtn.disabled, false, "Should enable the removeAllBtn button");
}

// Counter used by addTestData to generate unique cookie names across function
// calls.
let cookieID = 0;

async function addTestData(data) {
  let hosts = new Set();

  for (let site of data) {
    is(
      typeof site.origin,
      "string",
      "Passed an origin string into addTestData."
    );
    let principal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        site.origin
      );
    if (site.persisted) {
      await SiteDataTestUtils.persist(site.origin);
      registerCleanupFunction(() =>
        Services.perms.removeFromPrincipal(principal, "persistent-storage")
      );
    }

    if (site.usage) {
      await SiteDataTestUtils.addToIndexedDB(site.origin, site.usage);
    }

    for (let i = 0; i < (site.cookies || 0); i++) {
      SiteDataTestUtils.addToCookies({
        origin: site.origin,
        name: `cookie${cookieID++}`,
      });
    }

    hosts.add(principal.baseDomain || principal.host);
  }

  return Array.from(hosts);
}

function promiseCookiesCleared() {
  return TestUtils.topicObserved("cookie-changed", subj => {
    return (
      subj.QueryInterface(Ci.nsICookieNotification).action ==
      Ci.nsICookieNotification.ALL_COOKIES_CLEARED
    );
  });
}

async function loadServiceWorkerTestPage(url) {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  // register() resolves only once the script is stored in the DOM Cache,
  // which can be slow.
  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    let body = content.document.body;
    return ContentTaskUtils.waitForMutationCondition(
      body,
      { attributeFilter: ["data-test-service-worker-registered"] },
      () => body.getAttribute("data-test-service-worker-registered") === "true"
    );
  });
  BrowserTestUtils.removeTab(tab);
}

function promiseServiceWorkersCleared() {
  return TestUtils.waitForCondition(() => {
    let serviceWorkers = serviceWorkerManager.getAllRegistrations();
    if (!serviceWorkers.length) {
      ok(true, "Cleared all service workers");
      return true;
    }
    return false;
  }, "Should clear all service workers");
}

function promiseServiceWorkerRegisteredFor(url) {
  return TestUtils.waitForCondition(() => {
    try {
      let principal =
        Services.scriptSecurityManager.createContentPrincipalFromOrigin(url);
      let sw = serviceWorkerManager.getRegistrationByPrincipal(
        principal,
        principal.spec
      );
      if (sw) {
        ok(true, `Found the service worker registered for ${url}`);
        return true;
      }
    } catch (e) {}
    return false;
  }, `Should register service worker for ${url}`);
}
