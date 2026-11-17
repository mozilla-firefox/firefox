/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

let { ForgetAboutSite } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/forgetaboutsite/ForgetAboutSite.sys.mjs"
);

const domains = ["mochi.test:8888", "www.example.com"];

const addPath = "/browser/dom/indexedDB/test/browser_forgetThisSiteAdd.html";
const getPath = "/browser/dom/indexedDB/test/browser_forgetThisSiteGet.html";

const testPageURL1 = "http://" + domains[0] + addPath;
const testPageURL2 = "http://" + domains[1] + addPath;
const testPageURL3 = "http://" + domains[0] + getPath;
const testPageURL4 = "http://" + domains[1] + getPath;

// The Add page only reports a result from upgradeneeded, so a database left at
// version 11 by a previous run would make it wait forever. Clear by site rather
// than by the origins in `domains`, because HTTPS-First upgrades the
// www.example.com page.
registerCleanupFunction(async () => {
  for (let site of ["mochi.test", "example.com"]) {
    await new Promise(resolve =>
      Services.clearData.deleteDataFromSite(
        site,
        {},
        false,
        Services.clearData.CLEAR_DOM_QUOTA,
        resolve
      )
    );
  }
});

add_task(async function test1() {
  requestLongerTimeout(2);
  // Avoids the prompt
  setPermission(testPageURL1, "indexedDB");
  setPermission(testPageURL2, "indexedDB");

  // Set database version for domain 1
  gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser);
  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    testPageURL1
  );
  await waitForMessage(11, gBrowser);
  gBrowser.removeCurrentTab();
});

add_task(async function test2() {
  // Set database version for domain 2
  gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser);
  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    testPageURL2
  );
  await waitForMessage(11, gBrowser);
  gBrowser.removeCurrentTab();
});

add_task(async function test3() {
  // Remove database from domain 2
  await ForgetAboutSite.removeDataFromBaseDomain(domains[1]);
  setPermission(testPageURL4, "indexedDB");
});

add_task(async function test4() {
  // Get database version for domain 1
  gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser);
  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    testPageURL3
  );
  await waitForMessage(11, gBrowser);
  gBrowser.removeCurrentTab();
});

add_task(async function test5() {
  // Get database version for domain 2
  gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser);
  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    testPageURL4
  );
  await waitForMessage(1, gBrowser);
  gBrowser.removeCurrentTab();
});
