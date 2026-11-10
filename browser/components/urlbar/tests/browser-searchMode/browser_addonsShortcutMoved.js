/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests the address bar result telling people who used the former Add-ons
 * Manager shortcut that Search Tabs now uses it.
 */

const LAST_USED_PREF = "browser.keys.openAddons.lastUsed";
const SHOWN_COUNT_PREF = "browser.keys.openAddons.movedShownCount";
const PROVIDER_NAME = "UrlbarProviderAddonsShortcutMoved";

add_setup(async function () {
  // A blank selected tab would be reused for the page the button opens.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:mozilla"
  );
  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref(LAST_USED_PREF);
    Services.prefs.clearUserPref(SHOWN_COUNT_PREF);
    await resetUrlbar();
    BrowserTestUtils.removeTab(tab);
  });
});

add_task(async function testNotUsedBefore() {
  await pressSearchTabsShortcut();
  ok(!(await findResult()), "No result without a recorded use");
  await resetUrlbar();
});

add_task(async function testMenuItem() {
  recordFormerShortcutUse();
  document.getElementById("menu_searchTabs").doCommand();
  await UrlbarTestUtils.assertSearchMode(window, {
    source: gURLBar.searchModeForToken("%").source,
    entry: "historymenu",
  });
  ok(!(await findResult()), "No result when Search Tabs is used from the menu");
  await resetUrlbar();
  Services.prefs.clearUserPref(LAST_USED_PREF);
});

add_task(async function testTyping() {
  recordFormerShortcutUse();
  await pressSearchTabsShortcut();
  ok(await findResult(), "The result is shown");
  EventUtils.sendString("a");
  await UrlbarTestUtils.promiseSearchComplete(window);
  ok(!(await findResult()), "The result goes away once something is typed");
  EventUtils.synthesizeKey("KEY_Backspace");
  await UrlbarTestUtils.promiseSearchComplete(window);
  ok(await findResult(), "The result comes back once the input is cleared");
  await resetUrlbarAfterShowing();
  is(
    Services.prefs.getIntPref(SHOWN_COUNT_PREF, 0),
    1,
    "The showing is counted once"
  );
  Services.prefs.clearUserPref(LAST_USED_PREF);
  Services.prefs.clearUserPref(SHOWN_COUNT_PREF);
});

add_task(async function testShownThreeTimes() {
  recordFormerShortcutUse();
  for (let i = 1; i <= 3; i++) {
    await pressSearchTabsShortcut();
    let details = await findResult();
    ok(details, `The result is shown, time ${i}`);
    if (i == 1) {
      is(details.index, 0, "The result comes first");
      let args = {
        searchTabsShortcut: ShortcutUtils.prettifyShortcut(
          document.getElementById("key_searchTabs")
        ),
        addonsShortcut: ShortcutUtils.prettifyShortcut(
          document.getElementById("key_openAddons")
        ),
      };
      for (let [name, id] of [
        ["title", "urlbar-addons-shortcut-moved-title"],
        ["description", "urlbar-addons-shortcut-moved-description"],
      ]) {
        let l10n = document.l10n.getAttributes(
          details.element.row.querySelector(
            `.urlbarView-dynamic-addonsShortcutMoved-${name}`
          )
        );
        is(l10n.id, id, `The ${name} has the right string`);
        Assert.deepEqual(l10n.args, args, `The ${name} names both shortcuts`);
      }
    }
    await resetUrlbarAfterShowing();
    if (i < 3) {
      is(
        Services.prefs.getIntPref(SHOWN_COUNT_PREF, 0),
        i,
        "The showing is counted"
      );
    } else {
      assertStopped();
    }
  }

  await pressSearchTabsShortcut();
  ok(!(await findResult()), "The result isn't shown a fourth time");
  await resetUrlbar();
});

add_task(async function testChangeShortcuts() {
  await testButton("change_shortcuts", "about:keyboard");
});

add_task(async function testDismiss() {
  await testButton("dismiss");
});

async function testButton(name, url) {
  recordFormerShortcutUse();
  await pressSearchTabsShortcut();
  let { element } = await findResult();
  let tabPromise = url && BrowserTestUtils.waitForNewTab(gBrowser, url);
  // The view closes on mouseup, so the button is hidden by the time the a11y
  // checks see the click.
  AccessibilityUtils.suppressClickHandling(true);
  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeMouseAtCenter(
      element.row.querySelector(`[name=${name}]`),
      {},
      window
    );
  });
  AccessibilityUtils.suppressClickHandling(false);
  if (tabPromise) {
    BrowserTestUtils.removeTab(await tabPromise);
  }
  await resetUrlbar();
  assertStopped();

  await pressSearchTabsShortcut();
  ok(!(await findResult()), "The result isn't shown after using a button");
  await resetUrlbar();
}

function recordFormerShortcutUse() {
  Services.prefs.setStringPref(LAST_USED_PREF, new Date().toISOString());
}

function assertStopped() {
  ok(
    !Services.prefs.prefHasUserValue(LAST_USED_PREF),
    "The former shortcut's last use is cleared"
  );
  ok(
    !Services.prefs.prefHasUserValue(SHOWN_COUNT_PREF),
    "The shown count is cleared"
  );
}

async function findResult() {
  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    let details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    if (details.result.providerName == PROVIDER_NAME) {
      return { ...details, index: i };
    }
  }
  return null;
}

async function pressSearchTabsShortcut() {
  EventUtils.synthesizeKey("a", { shiftKey: true, accelKey: true });
  await UrlbarTestUtils.assertSearchMode(window, {
    source: gURLBar.searchModeForToken("%").source,
    entry: "shortcut",
  });
}

async function resetUrlbar() {
  // Blur first, so the search session ends with the shown results and the
  // provider records its impression.
  await UrlbarTestUtils.promisePopupClose(window, () => gURLBar.blur());
  gURLBar.searchMode = null;
  gURLBar.handleRevert();
}

/**
 * Resets the address bar after the result was shown, and waits for the
 * provider to count the showing. The impression can arrive after the view
 * closes, since the message path records the engagement asynchronously.
 */
async function resetUrlbarAfterShowing() {
  let counted = TestUtils.waitForPrefChange(SHOWN_COUNT_PREF);
  await resetUrlbar();
  await counted;
}
