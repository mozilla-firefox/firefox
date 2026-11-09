/* Tests the focus behavior of the Trust Panel. */

async function focusTrustIconContainer() {
  gURLBar.inputField.focus();
  is(document.activeElement, gURLBar.inputField, "urlbar should be focused");
  const focused = BrowserTestUtils.waitForEvent(
    document.getElementById("trust-icon-container"),
    "focus"
  );
  EventUtils.synthesizeKey("VK_TAB", { shiftKey: true });
  ok(document.activeElement.classList.contains("searchmode-switcher"));
  EventUtils.synthesizeKey("ArrowRight");
  await focused;
}

// Access the trustpanel via mouseclick. Focus should not be moved inside.
add_task(async function testIdentityPopupFocusClick() {
  await SpecialPowers.pushPrefEnv({ set: [["accessibility.tabfocus", 7]] });
  await BrowserTestUtils.withNewTab("https://example.com", async function () {
    await UrlbarTestUtils.openTrustPanel(window);
    isnot(Services.focus.focusedElement.id, "trustpanel-popup-connection");
  });
});

// Access the trustpanel via keyboard. Focus should be moved inside.
add_task(async function testIdentityPopupFocusKeyboard() {
  await SpecialPowers.pushPrefEnv({ set: [["accessibility.tabfocus", 7]] });
  await BrowserTestUtils.withNewTab("https://example.com", async function () {
    await focusTrustIconContainer();
    let shown = BrowserTestUtils.waitForEvent(window, "popupshown");
    EventUtils.synthesizeKey("KEY_Enter");
    await shown;
    is(Services.focus.focusedElement.id, "trustpanel-popup-connection");
  });
});
