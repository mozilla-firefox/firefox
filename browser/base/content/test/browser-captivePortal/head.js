XPCOMUtils.defineLazyServiceGetter(
  this,
  "cps",
  "@mozilla.org/network/captive-portal-service;1",
  Ci.nsICaptivePortalService
);

const CANONICAL_CONTENT = "success";
const CANONICAL_URL =
  "https://example.com/browser/browser/base/content/test/browser-captivePortal/canonical.txt";
const CANONICAL_URL_REDIRECTED =
  "https://example.com/browser/browser/base/content/test/browser-captivePortal/redirected.txt";
const PORTAL_NOTIFICATION_VALUE = "captive-portal-detected";
const BAD_CERT_PAGE = "https://expired.example.com/";

// CaptivePortalWatcher adds an https-only exception for the canonical URL
// each time a portal is detected.
registerCleanupFunction(() =>
  Services.perms.removeByType("https-only-load-insecure")
);

async function setupPrefsAndRecentWindowBehavior() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["captivedetect.canonicalURL", CANONICAL_URL],
      ["captivedetect.canonicalContent", CANONICAL_CONTENT],
    ],
  });
  // We need to test behavior when a portal is detected when there is no browser
  // window, but we can't close the default window opened by the test harness.
  // Instead, we deactivate CaptivePortalWatcher in the default window and
  // exclude it using an attribute to mask its presence.
  window.CaptivePortalWatcher.uninit();
  window.document.documentElement.setAttribute("ignorecaptiveportal", "true");

  registerCleanupFunction(function cleanUp() {
    window.CaptivePortalWatcher.init();
    window.document.documentElement.removeAttribute("ignorecaptiveportal");
  });
}

async function portalDetected() {
  Services.obs.notifyObservers(null, "captive-portal-login");
  await TestUtils.waitForCondition(() => {
    return cps.state == cps.LOCKED_PORTAL;
  }, "Waiting for Captive Portal Service to update state after portal detected.");
}

async function freePortal(aSuccess) {
  Services.obs.notifyObservers(
    null,
    "captive-portal-login-" + (aSuccess ? "success" : "abort")
  );
  await TestUtils.waitForCondition(() => {
    return cps.state != cps.LOCKED_PORTAL;
  }, "Waiting for Captive Portal Service to update state after portal freed.");
}

// If a window is provided, it will be focused. Otherwise, a new window
// will be opened and focused.
async function focusWindowAndWaitForPortalUI(aLongRecheck, win) {
  // CaptivePortalWatcher triggers a recheck when a window gains focus. If
  // the time taken for the check to complete is under PORTAL_RECHECK_DELAY_MS,
  // a tab with the login page is opened and selected. If it took longer,
  // no tab is opened. It's not reliable to time things in an async test,
  // so use a delay threshold of -1 to simulate a long recheck (so that any
  // amount of time is considered excessive), and a very large threshold to
  // simulate a short recheck.
  Services.prefs.setIntPref(
    "captivedetect.portalRecheckDelayMS",
    aLongRecheck ? -1 : 1000000
  );

  if (!win) {
    win = await BrowserTestUtils.openNewBrowserWindow();
  }
  let windowActivePromise = waitForBrowserWindowActive(win);
  win.focus();
  await windowActivePromise;

  // After a new window is opened, CaptivePortalWatcher asks for a recheck, and
  // waits for it to complete. We need to manually tell it a recheck completed.
  await TestUtils.waitForCondition(() => {
    return win.CaptivePortalWatcher._waitingForRecheck;
  }, "Waiting for CaptivePortalWatcher to trigger a recheck.");
  Services.obs.notifyObservers(null, "captive-portal-check-complete");

  let notification = await ensurePortalNotification(win);

  if (aLongRecheck) {
    ensureNoPortalTab(win);
    await testShowLoginPageButtonVisibility(notification, "visible");
    return win;
  }

  let tab = win.gBrowser.tabs[1];
  if (tab.linkedBrowser.currentURI.spec != CANONICAL_URL) {
    // The tab should load the canonical URL, wait for it.
    await BrowserTestUtils.waitForLocationChange(win.gBrowser, CANONICAL_URL);
  }
  is(
    win.gBrowser.selectedTab,
    tab,
    "The captive portal tab should be open and selected in the new window."
  );
  await testShowLoginPageButtonVisibility(notification, "hidden");
  return win;
}

function ensurePortalTab(win) {
  // For the tests that call this function, it's enough to ensure there
  // are two tabs in the window - the default tab and the portal tab.
  is(
    win.gBrowser.tabs.length,
    2,
    "There should be a captive portal tab in the window."
  );
}

async function ensurePortalNotification(win) {
  await BrowserTestUtils.waitForMutationCondition(
    win.gNavToolbox,
    { childList: true },
    () =>
      win.gNavToolbox
        .querySelector("notification-message")
        ?.getAttribute("value") == PORTAL_NOTIFICATION_VALUE
  );

  let notification = win.gNotificationBox.getNotificationWithValue(
    PORTAL_NOTIFICATION_VALUE
  );
  isnot(
    notification,
    null,
    "There should be a captive portal notification in the window."
  );
  return notification;
}

// Helper to test whether the "Show Login Page" is visible in the captive portal
// notification (it should be hidden when the portal tab is selected).
async function testShowLoginPageButtonVisibility(notification, visibility) {
  await notification.updateComplete;
  let showLoginPageButton = notification.buttonContainer.querySelector(
    "button.notification-button"
  );
  // If the visibility property was never changed from default, it will be
  // an empty string, so we pretend it's "visible" (effectively the same).
  is(
    showLoginPageButton.style.visibility || "visible",
    visibility,
    'The "Show Login Page" button should be ' + visibility + "."
  );
}

function ensureNoPortalTab(win) {
  is(
    win.gBrowser.tabs.length,
    1,
    "There should be no captive portal tab in the window."
  );
}

function ensureNoPortalNotification(win) {
  is(
    win.gNotificationBox.getNotificationWithValue(PORTAL_NOTIFICATION_VALUE),
    null,
    "There should be no captive portal notification in the window."
  );
}

/**
 * Some tests open a new window and close it later. When the window is closed,
 * the original window opened by mochitest gains focus, generating an
 * activate event. If the next test also opens a new window
 * before this event has a chance to fire, CaptivePortalWatcher picks
 * up the first one instead of the one from the new window. To avoid this
 * unfortunate intermittent timing issue, we wait for the event from
 * the original window every time we close a window that we opened.
 */
function waitForBrowserWindowActive(win) {
  return new Promise(resolve => {
    if (Services.focus.activeWindow == win) {
      resolve();
    } else {
      win.addEventListener(
        "activate",
        () => {
          resolve();
        },
        { once: true }
      );
    }
  });
}

async function closeWindowAndWaitForWindowActivate(win) {
  let activationPromises = [];
  for (let w of BrowserWindowTracker.orderedWindows) {
    if (
      w != win &&
      !win.document.documentElement.getAttribute("ignorecaptiveportal")
    ) {
      activationPromises.push(waitForBrowserWindowActive(win));
    }
  }
  await BrowserTestUtils.closeWindow(win);
  await Promise.race(activationPromises);
}

/**
 * BrowserTestUtils.openNewBrowserWindow() does not guarantee the newly
 * opened window has received focus when the promise resolves, so we
 * have to manually wait every time.
 */
async function openWindowAndWaitForFocus() {
  let win = await BrowserTestUtils.openNewBrowserWindow();
  await waitForBrowserWindowActive(win);
  return win;
}

async function openCaptivePortalErrorTab() {
  // Open a page with a cert error.
  let browser;
  let certErrorLoaded;
  let errorTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      let tab = BrowserTestUtils.addTab(gBrowser, BAD_CERT_PAGE);
      gBrowser.selectedTab = tab;
      browser = gBrowser.selectedBrowser;
      certErrorLoaded = BrowserTestUtils.waitForErrorPage(browser);
      return tab;
    },
    false
  );
  await certErrorLoaded;
  info("A cert error page was opened");
  await SpecialPowers.spawn(errorTab.linkedBrowser, [], async () => {
    let doc = content.document;
    let loginButton = doc.getElementById("openPortalLoginPageButton");
    await ContentTaskUtils.waitForCondition(
      () => loginButton && doc.body.className == "captiveportal",
      "Captive portal error page UI is visible"
    );
  });
  info("Captive portal error page UI is visible");

  return errorTab;
}

async function openCaptivePortalLoginTab(
  errorTab,
  LOGIN_PAGE_URL = CANONICAL_URL
) {
  let portalTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    LOGIN_PAGE_URL,
    true
  );

  await SpecialPowers.spawn(errorTab.linkedBrowser, [], async () => {
    let doc = content.document;
    let loginButton = doc.getElementById("openPortalLoginPageButton");
    info("Click on the login button on the captive portal error page");
    EventUtils.synthesizeMouseAtCenter(loginButton, {}, content);
  });

  let portalTab = await portalTabPromise;
  is(
    gBrowser.selectedTab,
    portalTab,
    "Captive Portal login page is now open in a new foreground tab."
  );

  return portalTab;
}
