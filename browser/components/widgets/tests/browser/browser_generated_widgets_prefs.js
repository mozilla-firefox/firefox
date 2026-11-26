/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { GeneratedWidgetsFeature } = ChromeUtils.importESModule(
  "moz-src:///browser/components/widgets/GeneratedWidgetsFeature.sys.mjs"
);
const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);
const { WidgetManager } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/widgets/WidgetManager.sys.mjs"
);

const LABS_PREF = "browser.widgets.generated.enabled";
const PROVIDER_PREF = "extensions.widgets.generated.enabled";
const NEWTAB_PREF =
  "browser.newtabpage.activity-stream.widgets.generated.enabled";

add_setup(mockAuthentication);

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("browser.widgets.studio.order");
});

add_task(async function test_opt_in_out_in_keeps_both_prefs() {
  await SpecialPowers.pushPrefEnv({
    clear: [[LABS_PREF], [PROVIDER_PREF], [NEWTAB_PREF]],
  });
  let cleanupEnrollment;
  try {
    GeneratedWidgetsFeature.init();
    ok(
      !Services.prefs.getBoolPref(PROVIDER_PREF, false),
      "Initialization leaves the provider off without Labs"
    );
    ok(
      !Services.prefs.getBoolPref(NEWTAB_PREF, false),
      "Initialization leaves widgets off on New Tab without Labs"
    );

    cleanupEnrollment = await NimbusTestUtils.enrollWithFeatureConfig(
      { featureId: "generatedWidgets", value: { enabled: true } },
      { slug: "custom-widgets-labs-prefs" }
    );
    await TestUtils.waitForCondition(
      () => Services.prefs.getBoolPref(PROVIDER_PREF, false),
      "Opting in enables the provider"
    );
    ok(
      Services.prefs.getBoolPref(NEWTAB_PREF, false),
      "Opting in enables widgets on New Tab"
    );

    let id = await installGeneratedWidget({ name: "Widget A" });
    await cleanupEnrollment();
    cleanupEnrollment = null;
    ok(
      !Services.prefs.getBoolPref(LABS_PREF, false),
      "Opting out clears the Labs pref"
    );
    ok(
      Services.prefs.getBoolPref(PROVIDER_PREF, false),
      "Opting out keeps the provider enabled"
    );
    ok(
      Services.prefs.getBoolPref(NEWTAB_PREF, false),
      "Opting out keeps widgets enabled on New Tab"
    );
    ok(WidgetManager.getWidgetByID(id), "The widget remains registered");

    cleanupEnrollment = await NimbusTestUtils.enrollWithFeatureConfig(
      { featureId: "generatedWidgets", value: { enabled: true } },
      { slug: "custom-widgets-labs-prefs" }
    );
    ok(
      Services.prefs.getBoolPref(PROVIDER_PREF, false),
      "Opting in again keeps the provider enabled"
    );
    ok(
      Services.prefs.getBoolPref(NEWTAB_PREF, false),
      "Opting in again keeps widgets enabled on New Tab"
    );
    let panel = await openWidgetsSidebar();
    await TestUtils.waitForCondition(
      () => panel.querySelector(`moz-widget[widget-id="${id}"]`),
      "Widget Studio shows the same widget after opting in again"
    );
  } finally {
    SidebarController.hide();
    await cleanupEnrollment?.();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_init_acts_on_current_value() {
  await SpecialPowers.pushPrefEnv({
    clear: [[LABS_PREF], [PROVIDER_PREF], [NEWTAB_PREF]],
  });
  try {
    Services.prefs.setBoolPref(LABS_PREF, true);
    Services.prefs.clearUserPref(PROVIDER_PREF);
    Services.prefs.clearUserPref(NEWTAB_PREF);
    GeneratedWidgetsFeature.init();
    ok(
      Services.prefs.getBoolPref(PROVIDER_PREF, false),
      "Initialization enables the provider for an existing opt-in"
    );
    ok(
      Services.prefs.getBoolPref(NEWTAB_PREF, false),
      "Initialization enables widgets on New Tab for an existing opt-in"
    );

    Services.prefs.clearUserPref(PROVIDER_PREF);
    Services.prefs.clearUserPref(NEWTAB_PREF);
    GeneratedWidgetsFeature.init();
    ok(
      Services.prefs.getBoolPref(PROVIDER_PREF, false),
      "Repeated initialization enables the provider again"
    );
    ok(
      Services.prefs.getBoolPref(NEWTAB_PREF, false),
      "Repeated initialization enables widgets on New Tab again"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(function test_startup_category_registered() {
  is(
    Services.catMan.getCategoryEntry(
      "browser-idle-startup",
      "moz-src:///browser/components/widgets/GeneratedWidgetsFeature.sys.mjs"
    ),
    "GeneratedWidgetsFeature.init",
    "The feature initializes at idle startup"
  );
});
