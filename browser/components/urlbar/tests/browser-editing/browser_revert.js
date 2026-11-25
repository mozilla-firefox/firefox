// Test reverting the urlbar value with ESC after a tab switch.

add_task(async function () {
  registerCleanupFunction(PlacesUtils.history.clear);
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "http://example.com",
    },
    async function () {
      let originalValue = gURLBar.value;
      let tab = gBrowser.selectedTab;
      info("Put a typed value.");
      gBrowser.userTypedValue = "foobar";
      info("Switch tabs.");
      gBrowser.selectedTab = gBrowser.tabs[0];
      gBrowser.selectedTab = tab;
      Assert.equal(
        gURLBar.value,
        "foobar",
        "location bar displays typed value"
      );

      gURLBar.focus();
      EventUtils.synthesizeKey("KEY_Escape");
      Assert.equal(
        gURLBar.value,
        originalValue,
        "ESC reverted the location bar value"
      );
    }
  );
});

// Tests that pasting a multi-line string into the address bar and reverting
// it works as expected.
add_task(async function paste_and_revert() {
  registerCleanupFunction(async () => {
    SpecialPowers.clipboardCopyString("");
    await PlacesUtils.history.clear();
  });
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "https://example.com",
    },
    async function () {
      gURLBar.value = "";
      gURLBar.focus();

      let input = "https://exam\n";
      await SimpleTest.promiseClipboardChange(input, () => {
        clipboardHelper.copyString(input);
      });
      document.commandDispatcher
        .getControllerForCommand("cmd_paste")
        .doCommand("cmd_paste");

      Assert.equal(
        gURLBar.value,
        "https://exam",
        "Location bar has the pasted value."
      );

      gURLBar.focus();
      EventUtils.synthesizeKey("KEY_Escape");
      EventUtils.synthesizeKey("KEY_Escape");

      Assert.equal(
        gURLBar.value,
        "example.com",
        "Location bar has the reverted value."
      );
      Assert.equal(
        gURLBar.getAttribute("pageproxystate"),
        "valid",
        "Pageproxystate is valid after revert."
      );
    }
  );
});
