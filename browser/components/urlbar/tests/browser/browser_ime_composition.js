/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests ime composition handling.

function composeAndCheckPanel(string, isPopupOpen) {
  EventUtils.synthesizeCompositionChange({
    composition: {
      string,
      clauses: [
        {
          length: string.length,
          attr: Ci.nsITextInputProcessor.ATTR_RAW_CLAUSE,
        },
      ],
    },
    caret: { start: string.length, length: 0 },
    key: { key: string ? string[string.length - 1] : "KEY_Backspace" },
  });
  Assert.equal(
    UrlbarTestUtils.isPopupOpen(window),
    isPopupOpen,
    "Check panel open state"
  );
}

add_task(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.tabToSearch.onboard.interactionsLeft", 0],
      ["browser.urlbar.suggest.quickactions", false],
    ],
  });

  await PlacesUtils.history.clear();
  // Add at least one typed entry for the empty results set. Also clear history
  // so that this can be over the autofill threshold.
  await PlacesTestUtils.addVisits({
    uri: "http://mozilla.org/",
    transition: PlacesUtils.history.TRANSITIONS.TYPED,
  });
  // Add a bookmark to ensure we autofill the engine domain for tab-to-search.
  let bm = await PlacesUtils.bookmarks.insert({
    url: "http://example.com/",
    parentGuid: PlacesUtils.bookmarks.menuGuid,
  });
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  await SearchTestUtils.installSearchExtension(
    {
      name: "Test",
      keyword: "@test",
    },
    { setAsDefault: true }
  );

  registerCleanupFunction(async () => {
    await PlacesUtils.bookmarks.remove(bm);
    await PlacesUtils.history.clear();
  });

  // Test both pref values.
  for (let val of [true, false]) {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.urlbar.keepPanelOpenDuringImeComposition", val]],
    });
    await test_composition(val);
    await test_composition_searchMode_preview(val);
    await test_composition_tabToSearch(val);
    await test_composition_autofill(val);
    await test_composition_cancel_autofill(val);
    await test_spurious_empty_composition(val);
  }
  await UrlbarTestUtils.promisePopupClose(window, () => gURLBar.blur());
});

async function test_composition(keepPanelOpenDuringImeComposition) {
  gURLBar.focus();
  await UrlbarTestUtils.promisePopupClose(window);

  info("Check the panel state during composition");
  composeAndCheckPanel("I", false);
  Assert.equal(gURLBar.value, "I", "Check urlbar value");
  composeAndCheckPanel("In", false);
  Assert.equal(gURLBar.value, "In", "Check urlbar value");

  info("Committing composition should open the panel.");
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeComposition({
      type: "compositioncommitasis",
      key: { key: "KEY_Enter" },
    });
  });
  Assert.equal(gURLBar.value, "In", "Check urlbar value");

  info("Check the panel state starting from an open panel.");
  Assert.ok(UrlbarTestUtils.isPopupOpen(window), "Popup should be open");
  composeAndCheckPanel("t", keepPanelOpenDuringImeComposition);
  Assert.equal(gURLBar.value, "Int", "Check urlbar value");
  composeAndCheckPanel("te", keepPanelOpenDuringImeComposition);
  Assert.equal(gURLBar.value, "Inte", "Check urlbar value");

  // Committing composition should open the popup.
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeComposition({
      type: "compositioncommitasis",
      key: { key: "KEY_Enter" },
    });
  });
  Assert.equal(gURLBar.value, "Inte", "Check urlbar value");

  info("If composition is cancelled, the value shouldn't be changed.");
  Assert.ok(UrlbarTestUtils.isPopupOpen(window), "Popup should be open");
  composeAndCheckPanel("r", keepPanelOpenDuringImeComposition);
  Assert.equal(gURLBar.value, "Inter", "Check urlbar value");
  composeAndCheckPanel("", keepPanelOpenDuringImeComposition);
  Assert.equal(gURLBar.value, "Inte", "Check urlbar value");
  // Canceled compositionend should reopen the popup.
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeComposition({
      type: "compositioncommit",
      data: "",
      key: { key: "KEY_Escape" },
    });
  });
  Assert.equal(gURLBar.value, "Inte", "Check urlbar value");

  info(
    "If composition replaces some characters and canceled, the search string should be the latest value."
  );
  Assert.ok(UrlbarTestUtils.isPopupOpen(window), "Popup should be open");
  EventUtils.synthesizeKey("VK_LEFT", { shiftKey: true });
  EventUtils.synthesizeKey("VK_LEFT", { shiftKey: true });
  composeAndCheckPanel("t", keepPanelOpenDuringImeComposition);
  Assert.equal(gURLBar.value, "Int", "Check urlbar value");
  composeAndCheckPanel("te", keepPanelOpenDuringImeComposition);
  Assert.equal(gURLBar.value, "Inte", "Check urlbar value");
  composeAndCheckPanel("", keepPanelOpenDuringImeComposition);
  Assert.equal(gURLBar.value, "In", "Check urlbar value");

  // Canceled compositionend should search the result with the latest value.
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeComposition({
      type: "compositioncommitasis",
      key: { key: "KEY_Escape" },
    });
  });
  Assert.equal(gURLBar.value, "In", "Check urlbar value");

  Assert.ok(UrlbarTestUtils.isPopupOpen(window), "Popup should be open");
  info(
    "Removing all characters should leave the popup open, Esc should then close it."
  );
  EventUtils.synthesizeKey("KEY_Backspace", {});
  EventUtils.synthesizeKey("KEY_Backspace", {});
  Assert.ok(UrlbarTestUtils.isPopupOpen(window), "Popup should be open");
  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape", {});
  });
  Assert.equal(gURLBar.value, "", "Check urlbar value");

  info("Composition which is canceled shouldn't cause opening the popup.");
  Assert.ok(!UrlbarTestUtils.isPopupOpen(window), "Popup should be closed");
  composeAndCheckPanel("I", false);
  Assert.equal(gURLBar.value, "I", "Check urlbar value");
  composeAndCheckPanel("In", false);
  Assert.equal(gURLBar.value, "In", "Check urlbar value");
  composeAndCheckPanel("", false);
  Assert.equal(gURLBar.value, "", "Check urlbar value");

  info("Canceled compositionend shouldn't open the popup if it was closed.");
  await UrlbarTestUtils.promisePopupClose(window);
  EventUtils.synthesizeComposition({
    type: "compositioncommitasis",
    key: { key: "KEY_Escape" },
  });
  Assert.ok(!UrlbarTestUtils.isPopupOpen(window), "Popup should be closed");
  Assert.equal(gURLBar.value, "", "Check urlbar value");

  info("Down key should open the popup even if the editor is empty.");
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeKey("KEY_ArrowDown", {});
  });
  Assert.equal(gURLBar.value, "", "Check urlbar value");

  info(
    "If popup is open at starting composition, the popup should be reopened after composition anyway."
  );
  Assert.ok(UrlbarTestUtils.isPopupOpen(window), "Popup should be open");
  composeAndCheckPanel("I", keepPanelOpenDuringImeComposition);
  Assert.equal(gURLBar.value, "I", "Check urlbar value");
  composeAndCheckPanel("In", keepPanelOpenDuringImeComposition);
  Assert.equal(gURLBar.value, "In", "Check urlbar value");
  composeAndCheckPanel("", keepPanelOpenDuringImeComposition);
  Assert.equal(gURLBar.value, "", "Check urlbar value");
  // A canceled compositionend should open the popup if it was open.
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeComposition({
      type: "compositioncommitasis",
      key: { key: "KEY_Escape" },
    });
  });
  Assert.equal(gURLBar.value, "", "Check urlbar value");

  info("Type normally, and hit escape, the popup should be closed.");
  Assert.ok(UrlbarTestUtils.isPopupOpen(window), "Popup should be open");
  EventUtils.synthesizeKey("I", {});
  EventUtils.synthesizeKey("n", {});
  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape", {});
  });
  Assert.equal(gURLBar.value, "In", "Check urlbar value");
  // Clear typed chars.
  EventUtils.synthesizeKey("KEY_Backspace", {});
  EventUtils.synthesizeKey("KEY_Backspace", {});
  Assert.equal(gURLBar.value, "", "Check urlbar value");

  info("With autofill, compositionstart shouldn't open the popup");
  Assert.ok(!UrlbarTestUtils.isPopupOpen(window), "Popup should be closed");
  composeAndCheckPanel("M", false);
  Assert.equal(gURLBar.value, "M", "Check urlbar value");
  composeAndCheckPanel("Mo", false);
  Assert.equal(gURLBar.value, "Mo", "Check urlbar value");
  // Committing composition should open the popup.
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeComposition({
      type: "compositioncommitasis",
      key: { key: "KEY_Enter" },
    });
  });
  Assert.equal(gURLBar.value, "Mozilla.org/", "Check urlbar value");
}

async function test_composition_searchMode_preview(
  keepPanelOpenDuringImeComposition
) {
  if (UrlbarPrefs.getScotchBonnetPref("searchRestrictKeywords.featureGate")) {
    await SpecialPowers.pushPrefEnv({
      set: [
        // Restrict keyword results adds 4 rows to the result panel, reaching
        // the maximum number of results displayed.
        // We need to increase the result limit to ensure the custom engine is
        // visible and not hidden.
        ["browser.urlbar.maxRichResults", 99],
      ],
    });
  }
  info("Check Search Mode preview is retained by composition");

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "@",
  });

  while (gURLBar.searchMode?.engineName != "Test") {
    EventUtils.synthesizeKey("KEY_ArrowDown", {}, window);
  }
  let expectedSearchMode = {
    engineName: "Test",
    isPreview: true,
    entry: "keywordoffer",
  };
  await UrlbarTestUtils.assertSearchMode(window, expectedSearchMode);
  composeAndCheckPanel("I", keepPanelOpenDuringImeComposition);
  Assert.equal(gURLBar.value, "I", "Check urlbar value");
  if (keepPanelOpenDuringImeComposition) {
    await UrlbarTestUtils.promiseSearchComplete(window);
  }
  // Test that we are in confirmed search mode.
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "Test",
    entry: "keywordoffer",
  });
  await UrlbarTestUtils.exitSearchMode(window);
  if (UrlbarPrefs.getScotchBonnetPref("searchRestrictKeywords.featureGate")) {
    await SpecialPowers.popPrefEnv();
  }
}

async function test_composition_tabToSearch(keepPanelOpenDuringImeComposition) {
  info("Check Tab-to-Search is retained by composition");

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "exa",
    fireInputEvent: true,
  });

  while (gURLBar.searchMode?.engineName != "Test") {
    EventUtils.synthesizeKey("KEY_Tab", {}, window);
  }
  let expectedSearchMode = {
    engineName: "Test",
    isPreview: true,
    entry: "tabtosearch",
  };
  await UrlbarTestUtils.assertSearchMode(window, expectedSearchMode);
  composeAndCheckPanel("I", keepPanelOpenDuringImeComposition);
  Assert.equal(gURLBar.value, "I", "Check urlbar value");
  if (keepPanelOpenDuringImeComposition) {
    await UrlbarTestUtils.promiseSearchComplete(window);
  }
  // Test that we are in confirmed search mode.
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "Test",
    entry: "tabtosearch",
  });
  await UrlbarTestUtils.exitSearchMode(window);
}

async function test_composition_autofill(keepPanelOpenDuringImeComposition) {
  info("Check whether autofills or not");
  await UrlbarTestUtils.promisePopupClose(window);

  info("Check the urlbar value during composition");
  composeAndCheckPanel("m", false);

  if (keepPanelOpenDuringImeComposition) {
    info("Wait for search suggestions");
    await UrlbarTestUtils.promiseSearchComplete(window);
  }

  Assert.equal(
    gURLBar.value,
    "m",
    "The urlbar value is not autofilled while turning IME on"
  );

  info("Check the urlbar value after committing composition");
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeComposition({
      type: "compositioncommitasis",
      key: { key: "KEY_Enter" },
    });
  });
  await UrlbarTestUtils.promiseSearchComplete(window);
  Assert.equal(gURLBar.value, "mozilla.org/", "The urlbar value is autofilled");

  // Clean-up.
  gURLBar.value = "";
}

async function test_composition_cancel_autofill(
  keepPanelOpenDuringImeComposition
) {
  info("Canceled composition should not prevent autofill");

  gURLBar.focus();
  await UrlbarTestUtils.promisePopupClose(window);
  composeAndCheckPanel("m", false);
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeComposition({
      type: "compositioncommitasis",
      key: { key: "KEY_Enter" },
    });
  });

  // Start a composition with "o".
  composeAndCheckPanel("o", keepPanelOpenDuringImeComposition);

  // Cancel the composition: the composed "o" is removed and the bar reverts to
  // "m". The input event should allow autofill regardless of event.data being empty.
  if (!keepPanelOpenDuringImeComposition) {
    await UrlbarTestUtils.promisePopupOpen(window, () => {
      EventUtils.synthesizeComposition({
        type: "compositioncommit",
        data: "",
      });
    });
  } else {
    EventUtils.synthesizeComposition({
      type: "compositioncommit",
      data: "",
    });
  }
  await UrlbarTestUtils.promiseSearchComplete(window);
  Assert.equal(
    gURLBar.value,
    "mozilla.org/",
    "Autofill is restored after composition cancel"
  );

  await UrlbarTestUtils.promisePopupClose(window, () => gURLBar.blur());
  gURLBar.value = "";
}

async function test_spurious_empty_composition(
  keepPanelOpenDuringImeComposition
) {
  if (keepPanelOpenDuringImeComposition) {
    // When the panel stays open during composition, compositionClosedPopup is
    // never set, so this scenario doesn't apply.
    return;
  }

  info(
    "Spurious empty composition after an open popup should not keep it closed"
  );

  gURLBar.focus();
  await UrlbarTestUtils.promisePopupClose(window);
  composeAndCheckPanel("moz", false);
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeComposition({
      type: "compositioncommitasis",
      key: { key: "KEY_Enter" },
    });
  });

  // Simulate a spurious empty composition: the popup closes on compositionstart
  // and should reopen on compositionend.
  composeAndCheckPanel("", false);
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeComposition({
      type: "compositioncommit",
      data: "",
    });
  });

  await UrlbarTestUtils.promisePopupClose(window, () => gURLBar.blur());
}
