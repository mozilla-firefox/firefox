/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function () {
  await setupPolicyEngineWithJson({
    policies: {
      DisablePrivateBrowsing: true,
    },
  });
});

add_task(async function test_privatebrowsing_disabled() {
  is(
    PrivateBrowsingUtils.enabled,
    false,
    "Private browsing should be disabled"
  );
  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  let privateBrowsingCommand = newWin.document.getElementById(
    "Tools:PrivateBrowsing"
  );
  is(
    privateBrowsingCommand.hidden,
    true,
    "The private browsing command should be hidden"
  );
  is(
    newWin.document.getElementById("menu_newPrivateWindow").hidden,
    true,
    "The File menu private browsing item should be hidden"
  );
  await BrowserTestUtils.closeWindow(newWin);

  await testPageBlockedByPolicy("about:privatebrowsing");
});
