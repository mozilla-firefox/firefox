/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const kTabsToolbar = "TabsToolbar";
const kNavBar = "nav-bar";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.revamp", true]],
  });
});

/**
 * Registers a widget, runs aTask, then tears the widget back down and forgets
 * its id, since auto-add only ever runs once per id.
 *
 * @param {object} aProperties
 *   Properties to pass through to CustomizableUI.createWidget.
 * @param {Function} aTask
 *   Called with the widget id once the widget has been created.
 */
async function withWidget(aProperties, aTask) {
  let id = aProperties.id;
  CustomizableUI.createWidget(aProperties);
  try {
    await aTask(id);
  } finally {
    CustomizableUI.destroyWidget(id);
    CustomizableUI.removeWidgetFromArea(id);
    CustomizableUI.getTestOnlyInternalProp("gSeenWidgets").delete(id);
  }
}

add_task(async function test_honoured_with_vertical_tabs() {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.verticalTabs", true]],
  });

  await withWidget(
    {
      id: "test-vertical-default-area-button",
      label: "Test",
      defaultArea: CustomizableUI.AREA_TABSTRIP,
      defaultAreaVerticalTabs: CustomizableUI.AREA_NAVBAR,
      removable: true,
    },
    id => {
      is(
        CustomizableUI.getPlacementOfWidget(id)?.area,
        kNavBar,
        "The widget is auto-added to the nav-bar rather than the hidden tab strip"
      );
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_ignored_with_horizontal_tabs() {
  await withWidget(
    {
      id: "test-horizontal-default-area-button",
      label: "Test",
      defaultArea: CustomizableUI.AREA_TABSTRIP,
      defaultAreaVerticalTabs: CustomizableUI.AREA_NAVBAR,
      removable: true,
    },
    id => {
      is(
        CustomizableUI.getPlacementOfWidget(id)?.area,
        kTabsToolbar,
        "defaultArea still wins while tabs are horizontal"
      );
    }
  );
});

// A widget that doesn't set defaultAreaVerticalTabs has to keep behaving
// exactly as it did before the property existed, even though that means
// landing in the hidden tab strip. Only widgets that name a vertical-tabs area
// are meant to be rerouted, so this expects the tab strip on purpose.
add_task(async function test_unchanged_without_the_property() {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.verticalTabs", true]],
  });

  await withWidget(
    {
      id: "test-no-vertical-default-area-button",
      label: "Test",
      defaultArea: CustomizableUI.AREA_TABSTRIP,
      removable: true,
    },
    id => {
      is(
        CustomizableUI.getPlacementOfWidget(id)?.area,
        kTabsToolbar,
        "A widget that does not opt in keeps its existing behaviour"
      );
    }
  );

  await SpecialPowers.popPrefEnv();
});

// An area name that doesn't exist is dropped without a warning, so this test
// is the only thing standing between a typo and a widget silently auto-added
// into the hidden tab strip.
add_task(async function test_unknown_area_is_rejected() {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.verticalTabs", true]],
  });

  await withWidget(
    {
      id: "test-bogus-vertical-default-area-button",
      label: "Test",
      defaultArea: CustomizableUI.AREA_TABSTRIP,
      defaultAreaVerticalTabs: "not-a-real-area",
      removable: true,
    },
    id => {
      is(
        CustomizableUI.getPlacementOfWidget(id)?.area,
        kTabsToolbar,
        "An unregistered area is dropped and defaultArea is used instead"
      );
    }
  );

  await SpecialPowers.popPrefEnv();
});
