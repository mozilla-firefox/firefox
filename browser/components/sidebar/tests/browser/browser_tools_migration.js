/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.main.tools", "syncedtabs,bookmarks,history"],
      ["sidebar.newTool.migration.bookmarks", "{}"],
      ["browser.ml.chat.enabled", false],
      [
        "sidebar.newTool.migration.aichat",
        JSON.stringify({
          visibilityPref: "browser.ml.chat.enabled",
        }),
      ],
    ],
  });
});

add_task(async function test_duplicate_tool() {
  const sidebar = document.querySelector("sidebar-main");
  let tools = Services.prefs.getStringPref("sidebar.main.tools").split(",");
  is(tools.length, 3, "Three tools are in the sidebar.main.tools pref");
  is(
    tools.filter(tool => tool == "bookmarks").length,
    1,
    "Bookmarks has only been added once"
  );
  is(
    sidebar.toolButtons.length,
    3,
    "Three default tools are visible in the launcher"
  );
});

add_task(async function test_one_time_tool_migration() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.main.tools", "syncedtabs,history"],
      ["sidebar.newTool.migration.bookmarks", "{}"],
    ],
  });
  const sidebar = document.querySelector("sidebar-main");
  let tools = Services.prefs.getStringPref("sidebar.main.tools");
  is(
    tools.split(",").length,
    3,
    "Three tools are in the sidebar.main.tools pref"
  );
  is(
    sidebar.toolButtons.length,
    3,
    "Three default tools are visible in the launcher"
  );

  await toggleSidebarPanel(window, "viewCustomizeSidebar");
  let customizeDocument = SidebarController.browser.contentDocument;
  const customizeComponent =
    customizeDocument.querySelector("sidebar-customize");
  let bookmarksInput = Array.from(customizeComponent.toolInputs).find(
    input => input.name === "viewBookmarksSidebar"
  );
  ok(
    bookmarksInput.checked,
    "The bookmarks input is checked in the Customize Sidebar menu."
  );
  let prefValue = JSON.parse(
    Services.prefs.getStringPref("sidebar.newTool.migration.bookmarks")
  );
  bookmarksInput.click();

  await BrowserTestUtils.waitForMutationCondition(
    bookmarksInput,
    { attributes: true, attributeFilter: ["checked"] },
    () => !bookmarksInput.checked
  );

  ok(
    prefValue.alreadyShown,
    "Pref property has been updated after being added to tools."
  );

  is(sidebar.toolButtons.length, 2, "Two tools are now shown in the launcher");
});

add_task(async function test_check_visibility_enabled() {
  const sidebar = document.querySelector("sidebar-main");
  let tools = Services.prefs.getStringPref("sidebar.main.tools");
  is(
    tools.split(",").length,
    2,
    "Two tools are in the sidebar.main.tools pref"
  );

  is(sidebar.toolButtons.length, 2, "Two tools are shown in the launcher");

  let prefValue = JSON.parse(
    Services.prefs.getStringPref("sidebar.newTool.migration.aichat")
  );

  ok(!prefValue.alreadyShown, "aichat pref property was not already shown.");

  // enable aichat panel visibility
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.enabled", true]],
  });

  let customizeDocument = SidebarController.browser.contentDocument;
  const customizeComponent =
    customizeDocument.querySelector("sidebar-customize");

  let aichatInput = Array.from(customizeComponent.toolInputs).find(
    input => input.name === "viewGenaiChatSidebar"
  );

  await BrowserTestUtils.waitForMutationCondition(
    aichatInput,
    { attributes: true, attributeFilter: ["checked"] },
    () => aichatInput.checked
  );

  let bookmarksInput = Array.from(customizeComponent.toolInputs).find(
    input => input.name === "viewBookmarksSidebar"
  );
  ok(
    !bookmarksInput.checked,
    "The bookmarks input is not checked in the Customize Sidebar menu."
  );

  is(
    sidebar.toolButtons.length,
    3,
    "Three tools are now shown in the launcher"
  );

  prefValue = JSON.parse(
    Services.prefs.getStringPref("sidebar.newTool.migration.aichat")
  );

  ok(
    prefValue.alreadyShown,
    "aichat pref property is now marked as already shown."
  );
});
