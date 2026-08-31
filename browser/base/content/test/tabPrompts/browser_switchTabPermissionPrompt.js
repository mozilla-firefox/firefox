/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_check_file_prompt() {
  let initialTab = gBrowser.selectedTab;
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    await BrowserTestUtils.switchTab(gBrowser, initialTab);

    let testHelper = async function (uri, expectedValue) {
      BrowserTestUtils.startLoadingURIString(browser, uri);
      await BrowserTestUtils.browserLoaded(browser, false, uri);
      let dialogFinishedShowing = TestUtils.topicObserved(
        "common-dialog-loaded"
      );
      await SpecialPowers.spawn(browser, [], () => {
        content.setTimeout(() => {
          content.alert("Hello");
        }, 0);
      });

      let [dialogWin] = await dialogFinishedShowing;
      let checkbox = dialogWin.document.getElementById("checkbox");
      info("Got: " + checkbox.label);
      ok(
        checkbox.label.includes(expectedValue),
        `Checkbox label should mention domain (${expectedValue}).`
      );

      dialogWin.document.querySelector("dialog").acceptDialog();
    };

    await testHelper("https://example.com/1", "example.com");
    await testHelper("about:robots", "about:");
    let file = Services.io.newFileURI(
      Services.dirsvc.get("ProfD", Ci.nsIFile)
    ).spec;
    await testHelper(file, "file://");
  });
});
