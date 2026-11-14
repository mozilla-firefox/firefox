/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

add_task(async function test_editMenu_useSelectionForFind() {
  let menuitem = document.getElementById("menu_findSelection");
  ok(menuitem, "menu_findSelection exists");
  is(menuitem.getAttribute("key"), "key_findSelection", "Uses Cmd-E key");
  is(
    menuitem.getAttribute("command"),
    "cmd_findSelection",
    "Uses cmd_findSelection"
  );

  await BrowserTestUtils.withNewTab(
    "data:text/html,<p id='p'>needle haystack</p>",
    async browser => {
      await SpecialPowers.spawn(browser, [], () => {
        let range = content.document.createRange();
        range.setStart(content.document.getElementById("p").firstChild, 0);
        range.setEnd(content.document.getElementById("p").firstChild, 6);
        content.getSelection().removeAllRanges();
        content.getSelection().addRange(range);
      });

      menuitem.doCommand();

      let findbar = await gBrowser.getFindBar();
      await TestUtils.waitForCondition(
        () => findbar._findField.value == "needle",
        "Find field is filled with the selection"
      );
      ok(findbar.hidden, "Find bar stays closed");
    }
  );
});
