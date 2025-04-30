/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

registerCleanupFunction(async function resetToolbar() {
  await CustomizableUI.reset();
  Services.prefs.clearUserPref(kPrefCustomizationState);
  Services.prefs.clearUserPref(kPrefCustomizationHorizontalTabstrip);
  Services.prefs.clearUserPref(kPrefCustomizationNavBarWhenVerticalTabs);
});

/**
 * Test that with some widgets customized into the TabsToolbar,
 * they are moved to the nav-bar as expected when the verticalTabs pref is enabled,
 * and confirm everything was restored as expected when we flip back.
 */
add_task(async function moveAndRestoreTabsToolbarWidgets() {
  const defaultNavbarPlacements = CustomizableUI.getWidgetIdsInArea(
    CustomizableUI.AREA_NAVBAR
  );
  const defaultHorizontalTabStripPlacements = CustomizableUI.getWidgetIdsInArea(
    CustomizableUI.AREA_TABSTRIP
  );
  CustomizableUI.addWidgetToArea(
    "home-button",
    CustomizableUI.AREA_TABSTRIP,
    0
  );
  CustomizableUI.addWidgetToArea("panic-button", CustomizableUI.AREA_TABSTRIP);
  CustomizableUI.addWidgetToArea(
    "privatebrowsing-button",
    CustomizableUI.AREA_TABSTRIP
  );

  const horizontalTabStripPlacements = CustomizableUI.getWidgetIdsInArea(
    CustomizableUI.AREA_TABSTRIP
  );
  Assert.equal(
    horizontalTabStripPlacements.length,
    defaultHorizontalTabStripPlacements.length + 3,
    "tabstrip has 3 new widget placements"
  );
  await SpecialPowers.pushPrefEnv({ set: [[VERTICAL_TABS_PREF, true]] });

  Assert.ok(
    CustomizableUI.verticalTabsEnabled,
    "CustomizableUI verticalTabsEnabled getter reflects pref value"
  );
  Assert.ok(
    BrowserTestUtils.isHidden(document.getElementById("TabsToolbar")),
    "#TabsToolbar is now hidden"
  );

  // The tabs-widget should be in the vertical tabs area
  Assert.ok(
    CustomizableUI.getWidgetIdsInArea(
      CustomizableUI.AREA_VERTICAL_TABSTRIP
    ).includes("tabbrowser-tabs"),
    "The tabs container moved to the vertical tabs area"
  );

  // The widgets we added to TabsToolbar should have been appened to nav-bar
  let newNavbarPlacements = CustomizableUI.getWidgetIdsInArea(
    CustomizableUI.AREA_NAVBAR
  );
  let expectedMovedWidgetIds = [
    "alltabs-button",
    "panic-button",
    "privatebrowsing-button",
  ];
  Assert.deepEqual(
    newNavbarPlacements.slice(
      newNavbarPlacements.length - expectedMovedWidgetIds.length
    ),
    expectedMovedWidgetIds,
    "All the tabstrip widgets were appended to the nav-bar"
  );

  let prefSnapshot = JSON.parse(
    Services.prefs.getStringPref("browser.uiCustomization.horizontalTabstrip")
  );
  Assert.deepEqual(
    prefSnapshot,
    horizontalTabStripPlacements,
    "The previous tab strip placements were stored in the pref"
  );

  await SpecialPowers.pushPrefEnv({ set: [[VERTICAL_TABS_PREF, false]] });

  Assert.ok(
    !CustomizableUI.verticalTabsEnabled,
    "CustomizableUI verticalTabsEnabled getter reflects pref value"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(document.getElementById("TabsToolbar")),
    "#TabsToolbar is now visible"
  );
  Assert.ok(
    CustomizableUI.getWidgetIdsInArea(CustomizableUI.AREA_TABSTRIP).includes(
      "tabbrowser-tabs"
    ),
    "The tabs container moved back to the tab strip"
  );

  Assert.deepEqual(
    CustomizableUI.getWidgetIdsInArea(CustomizableUI.AREA_TABSTRIP),
    horizontalTabStripPlacements,
    "The tabstrip widgets were restored in the expected order"
  );
  Assert.deepEqual(
    CustomizableUI.getWidgetIdsInArea(CustomizableUI.AREA_NAVBAR),
    defaultNavbarPlacements,
    "The nav-bar widgets are back in their original state"
  );
});

add_task(async function checkNewTabButtonAbsenceWithVerticalTabs() {
  await SpecialPowers.pushPrefEnv({ set: [[VERTICAL_TABS_PREF, true]] });

  const verticalNavBarPlacements = CustomizableUI.getWidgetIdsInArea(
    CustomizableUI.AREA_NAVBAR
  );

  Assert.ok(
    !verticalNavBarPlacements.includes("new-tab-button"),
    "The nav-bar does not include the new-tab-button when vertical tabs is enabled."
  );
});
