/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

const DUMMY =
  "https://example.com/browser/browser/components/tabbrowser/test/browser/tabs/dummy_page.html";

function isBrowserAppTab(browser) {
  return browser.browsingContext.isAppTab;
}

// Restarts the child process by crashing it then reloading the tab
var restart = async function (browser) {
  // If the tab isn't remote this would crash the main process so skip it
  if (!browser.isRemoteBrowser) {
    return;
  }

  // Make sure the main process has all of the current tab state before crashing
  await TabStateFlusher.flush(browser);

  await BrowserTestUtils.crashFrame(browser);

  let tab = gBrowser.getTabForBrowser(browser);
  SessionStore.reviveCrashedTab(tab);

  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
};

add_task(async function navigate() {
  let tab = BrowserTestUtils.addTab(gBrowser, "about:robots");
  let browser = tab.linkedBrowser;
  gBrowser.selectedTab = tab;
  await BrowserTestUtils.browserStopped(gBrowser);
  let isAppTab = isBrowserAppTab(browser);
  ok(!isAppTab, "Docshell shouldn't think it is an app tab");

  gBrowser.pinTab(tab);
  isAppTab = isBrowserAppTab(browser);
  ok(isAppTab, "Docshell should think it is an app tab");

  BrowserTestUtils.startLoadingURIString(gBrowser, DUMMY);
  await BrowserTestUtils.browserStopped(gBrowser);
  isAppTab = isBrowserAppTab(browser);
  ok(isAppTab, "Docshell should think it is an app tab");

  gBrowser.unpinTab(tab);
  isAppTab = isBrowserAppTab(browser);
  ok(!isAppTab, "Docshell shouldn't think it is an app tab");

  gBrowser.pinTab(tab);
  isAppTab = isBrowserAppTab(browser);
  ok(isAppTab, "Docshell should think it is an app tab");

  BrowserTestUtils.startLoadingURIString(gBrowser, "about:robots");
  await BrowserTestUtils.browserStopped(gBrowser);
  isAppTab = isBrowserAppTab(browser);
  ok(isAppTab, "Docshell should think it is an app tab");

  gBrowser.removeCurrentTab();
});

add_task(async function crash() {
  if (!gMultiProcessBrowser || !AppConstants.MOZ_CRASHREPORTER) {
    return;
  }

  let tab = BrowserTestUtils.addTab(gBrowser, DUMMY);
  let browser = tab.linkedBrowser;
  gBrowser.selectedTab = tab;
  await BrowserTestUtils.browserStopped(gBrowser);
  let isAppTab = isBrowserAppTab(browser);
  ok(!isAppTab, "Docshell shouldn't think it is an app tab");

  gBrowser.pinTab(tab);
  isAppTab = isBrowserAppTab(browser);
  ok(isAppTab, "Docshell should think it is an app tab");

  await restart(browser);
  isAppTab = isBrowserAppTab(browser);
  ok(isAppTab, "Docshell should think it is an app tab");

  gBrowser.removeCurrentTab();
});
