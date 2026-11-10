/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { Sqlite } = ChromeUtils.importESModule(
  "resource://gre/modules/Sqlite.sys.mjs"
);

XPCOMUtils.defineLazyServiceGetter(
  this,
  "TrackingDBService",
  "@mozilla.org/tracking-db-service;1",
  Ci.nsITrackingDBService
);

ChromeUtils.defineLazyGetter(this, "TRACK_DB_PATH", function () {
  return PathUtils.join(PathUtils.profileDir, "protections.sqlite");
});

ChromeUtils.defineESModuleGetters(this, {
  ContentBlockingAllowList:
    "moz-src:///toolkit/components/antitracking/ContentBlockingAllowList.sys.mjs",
});

var { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);

async function waitForProtectionsPanelToast() {
  await BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popupshown"
  );
  Assert.ok(
    gProtectionsHandler._protectionsPopup.hasAttribute("toast"),
    "Protections panel toast is shown."
  );

  await BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popuphidden"
  );
}

async function openProtectionsPanel(toast, win = window) {
  let popupShownPromise = BrowserTestUtils.waitForEvent(
    win,
    "popupshown",
    true,
    e => e.target.id == "protections-popup"
  );
  let shieldIconContainer = win.document.getElementById(
    "tracking-protection-icon-container"
  );

  // Register a promise to wait for the tooltip to be shown.
  let tooltip = win.document.getElementById("tracking-protection-icon-tooltip");
  let tooltipShownPromise = BrowserTestUtils.waitForPopupEvent(
    tooltip,
    "shown"
  );

  // nsXULTooltipListener will fail silently if no drag service is available
  Assert.ok(
    !SpecialPowers.isHeadless,
    "openProtectionsPanel cannot be used in headless mode."
  );

  // Move out than move over the shield icon to trigger the hover event in
  // order to fetch tracker count.
  EventUtils.synthesizeMouseAtCenter(
    win.gURLBar,
    {
      type: "mousemove",
    },
    win
  );
  EventUtils.synthesizeMouseAtCenter(
    shieldIconContainer,
    {
      type: "mousemove",
    },
    win
  );

  // Wait for the tooltip to be shown.
  await tooltipShownPromise;

  if (!toast) {
    EventUtils.synthesizeMouseAtCenter(shieldIconContainer, {}, win);
  } else {
    win.gProtectionsHandler.showProtectionsPopup({ toast });
  }

  await popupShownPromise;
}

async function openProtectionsPanelWithKeyNav() {
  let popupShownPromise = BrowserTestUtils.waitForEvent(
    window,
    "popupshown",
    true,
    e => e.target.id == "protections-popup"
  );

  gURLBar.focus();

  // This will trigger the focus event for the shield icon for pre-fetching
  // the tracker count.
  EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true });
  ok(document.activeElement.classList.contains("searchmode-switcher"));
  EventUtils.synthesizeKey("KEY_ArrowRight");
  EventUtils.synthesizeKey("KEY_Enter");

  await popupShownPromise;
}

async function closeProtectionsPanel(win = window) {
  let protectionsPopup = win.document.getElementById("protections-popup");
  if (!protectionsPopup) {
    return;
  }
  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popuphidden"
  );

  PanelMultiView.hidePopup(protectionsPopup);
  await popuphiddenPromise;
}

async function addTrackerDataIntoDB(count) {
  const insertSQL =
    "INSERT INTO events (type, count, timestamp)" +
    "VALUES (:type, :count, date(:timestamp));";

  let db = await Sqlite.openConnection({ path: TRACK_DB_PATH });
  let date = new Date().toISOString();

  await db.execute(insertSQL, {
    type: TrackingDBService.TRACKERS_ID,
    count,
    timestamp: date,
  });

  await db.close();
}

async function waitForAboutProtectionsTab() {
  let tab = await BrowserTestUtils.waitForNewTab(
    gBrowser,
    "about:protections",
    true
  );

  // When the graph is built it means the messaging has finished,
  // we can close the tab.
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    await ContentTaskUtils.waitForCondition(() => {
      let bars = content.document.querySelectorAll(".graph-bar");
      return bars.length;
    }, "The graph has been built");
  });

  return tab;
}

function waitForSecurityChange(numChanges = 1, win = null) {
  if (!win) {
    win = window;
  }
  return new Promise(resolve => {
    let n = 0;
    let listener = {
      onSecurityChange() {
        n = n + 1;
        info("Received onSecurityChange event " + n + " of " + numChanges);
        if (n >= numChanges) {
          win.gBrowser.removeProgressListener(listener);
          resolve(n);
        }
      },
    };
    win.gBrowser.addProgressListener(listener);
  });
}

function waitForContentBlockingEvent(numChanges = 1, win = null) {
  if (!win) {
    win = window;
  }
  return new Promise(resolve => {
    let n = 0;
    let listener = {
      onContentBlockingEvent(webProgress, request, event) {
        n = n + 1;
        info(
          `Received onContentBlockingEvent event: ${event} (${n} of ${numChanges})`
        );
        if (n >= numChanges) {
          win.gBrowser.removeProgressListener(listener);
          resolve(n);
        }
      },
    };
    win.gBrowser.addProgressListener(listener);
  });
}
