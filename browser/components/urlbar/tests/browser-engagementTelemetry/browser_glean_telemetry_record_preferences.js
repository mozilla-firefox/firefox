/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for preference telemetry.

add_setup(async function () {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();

  // Create a new window in order to initialize UrlbarSession of
  // UrlbarParentController.
  const win = await BrowserTestUtils.openNewBrowserWindow();
  registerCleanupFunction(async function () {
    await BrowserTestUtils.closeWindow(win);
  });
});

add_task(async function prefMaxRichResults() {
  Assert.equal(
    Glean.urlbar.prefMaxResults.testGetValue(),
    UrlbarPrefs.get("maxRichResults"),
    "Record prefMaxResults when UrlbarParentController is initialized"
  );

  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.maxRichResults", 0]],
  });
  Assert.equal(
    Glean.urlbar.prefMaxResults.testGetValue(),
    UrlbarPrefs.get("maxRichResults"),
    "Record prefMaxResults when the maxRichResults pref is updated"
  );
});

add_task(async function boolPref() {
  const testData = [
    {
      green: "prefSuggestOnlineAvailable",
      pref: "quicksuggest.online.available",
    },
    {
      green: "prefSuggestOnlineEnabled",
      pref: "quicksuggest.online.enabled",
    },
    {
      green: "prefSuggestAll",
      pref: "suggest.quicksuggest.all",
    },
    {
      green: "prefSuggestSponsored",
      pref: "suggest.quicksuggest.sponsored",
    },
    {
      green: "prefSuggestTopsites",
      pref: "suggest.topsites",
    },
  ];

  for (const { green, pref } of testData) {
    Assert.equal(
      Glean.urlbar[green].testGetValue(),
      UrlbarPrefs.get(pref),
      `Record ${green} when UrlbarParentController is initialized`
    );

    await SpecialPowers.pushPrefEnv({
      set: [[`browser.urlbar.${pref}`, !UrlbarPrefs.get(pref)]],
    });

    Assert.equal(
      Glean.urlbar[green].testGetValue(),
      UrlbarPrefs.get(pref),
      `Record ${green} when the ${pref} pref is updated`
    );
  }
});

add_task(async function boolNimbusVariable() {
  const testData = [
    {
      glean: "prefSuggestOnlineAvailable",
      variable: "quickSuggestOnlineAvailable",
      pref: "quicksuggest.online.available",
    },
  ];

  for (const { glean, variable, pref } of testData) {
    let initialValue = UrlbarPrefs.get(pref);
    Assert.equal(
      Glean.urlbar[glean].testGetValue(),
      initialValue,
      "Metric value should be correct initially: " + glean
    );

    let nimbusCleanup = await UrlbarTestUtils.initNimbusFeature({
      [variable]: !initialValue,
    });

    Assert.equal(
      Glean.urlbar[glean].testGetValue(),
      !initialValue,
      "Metric value should be correct after installing experiment: " + glean
    );

    // Open a new window and make sure the metric is still correct. These pref
    // metrics are currently recorded by `UrlbarSession`, an instance of which
    // is created per browser window.
    let win = await BrowserTestUtils.openNewBrowserWindow();
    await TestUtils.waitForTick();

    Assert.equal(
      Glean.urlbar[glean].testGetValue(),
      !initialValue,
      "Metric value should remain correct after opening new window: " + glean
    );

    await BrowserTestUtils.closeWindow(win);
    await TestUtils.waitForTick();

    Assert.equal(
      Glean.urlbar[glean].testGetValue(),
      !initialValue,
      "Metric value should remain correct after closing new window: " + glean
    );

    await nimbusCleanup();

    Assert.equal(
      Glean.urlbar[glean].testGetValue(),
      initialValue,
      "Metric value should be reset after uninstalling experiment: " + glean
    );
  }
});
