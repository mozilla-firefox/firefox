/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async () => {
  // New tab preloading breaks BrowserTestUtils.waitForNewTab.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtab.preload", false]],
  });

  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
  });
});

add_task(
  async function test_browser_remains_open_after_closing_last_profiles_tab() {
    if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
      // `mochitest-browser` suite `add_task` does not yet support
      // `properties.skip_if`.
      ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
      return;
    }
    await initGroupDatabase();

    for (let URI of [
      "about:editprofile",
      "about:newprofile",
      "about:deleteprofile",
    ]) {
      let browserLoaded = BrowserTestUtils.browserLoaded(
        gBrowser.selectedBrowser,
        false,
        URI
      );
      BrowserTestUtils.startLoadingURIString(gBrowser.selectedBrowser, URI);
      await browserLoaded;

      let newTabLoaded = BrowserTestUtils.waitForNewTab(gBrowser);
      let tabClosed = BrowserTestUtils.waitForTabClosing(gBrowser.selectedTab);

      await SpecialPowers.spawn(
        gBrowser.selectedBrowser,
        [URI],
        async currentURI => {
          let selector = "edit-profile-card";
          if (currentURI === "about:newprofile") {
            selector = "new-profile-card";
          } else if (currentURI === "about:deleteprofile") {
            selector = "delete-profile-card";
          }
          let profileCard =
            content.document.querySelector(selector).wrappedJSObject;

          await ContentTaskUtils.waitForCondition(
            () => profileCard.initialized,
            `Waiting for ${selector} to be initialized`
          );

          await profileCard.updateComplete;

          if (currentURI === "about:newprofile") {
            // Fill in the input so we don't hit the beforeunload warning
            profileCard.nameInput.value = "test";
          }

          let button = "doneButton";
          if (currentURI === "about:deleteprofile") {
            button = "cancelButton";
          }

          EventUtils.synthesizeMouseAtCenter(profileCard[button], {}, content);
        }
      );

      await tabClosed;
      await newTabLoaded;

      is(
        gBrowser.currentURI.spec,
        "about:newtab",
        "The current uri is 'about:newtab' and the browser didn't close"
      );
      is(gBrowser.visibleTabs.length, 1, "There is only 1 tab open");
    }
  }
);
