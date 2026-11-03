/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const PREF_UI_DENSITY = "browser.uidensity";
const PREF_AUTO_TOUCH = "browser.touchmode.auto";
const PREF_THRESHOLD = "browser.compactmode.auto.threshold";

const gUIDensity = window.gUIDensity;

async function withTabletMode({ prefs, inTablet }, callback) {
  let original = gUIDensity._inTabletMode;
  gUIDensity._inTabletMode = () => inTablet;
  await SpecialPowers.pushPrefEnv(prefs);
  try {
    gUIDensity.update();
    await callback();
  } finally {
    gUIDensity._inTabletMode = original;
    await SpecialPowers.popPrefEnv();
    gUIDensity.update();
  }
}

function isTouch() {
  return document.documentElement.getAttribute("uidensity") == "touch";
}

add_task(async function test_automatic_switches_to_touch_in_tablet_mode() {
  await withTabletMode(
    {
      // The auto-touch checkbox is off: automatic must still switch to touch.
      prefs: {
        set: [
          [PREF_THRESHOLD, "0"],
          [PREF_AUTO_TOUCH, false],
        ],
        clear: [[PREF_UI_DENSITY]],
      },
      inTablet: true,
    },
    () => {
      let density = gUIDensity.getCurrentDensity();
      is(
        density.mode,
        gUIDensity.MODE_TOUCH,
        "Automatic density switches to touch in tablet mode even when " +
          "browser.touchmode.auto is false"
      );
      Assert.ok(
        density.overridden,
        "The touch density is reported as overridden"
      );
      Assert.ok(isTouch(), "The document is marked touch");
    }
  );
});

add_task(async function test_automatic_touch_leaves_tablet_mode() {
  await withTabletMode(
    {
      prefs: {
        set: [
          [PREF_THRESHOLD, "0"],
          [PREF_AUTO_TOUCH, false],
        ],
        clear: [[PREF_UI_DENSITY]],
      },
      inTablet: false,
    },
    () => {
      let density = gUIDensity.getCurrentDensity();
      is(
        density.mode,
        gUIDensity.MODE_NORMAL,
        "Automatic density is normal when not in tablet mode"
      );
      Assert.ok(
        !density.overridden,
        "The density is not reported as overridden"
      );
      Assert.ok(!isTouch(), "The document is not marked touch");
    }
  );
});

add_task(async function test_standard_ignores_tablet_mode_without_optin() {
  await withTabletMode(
    {
      // Standard is an explicit uidensity value (MODE_NORMAL), so it is not
      // "automatic" and must not switch to touch unless the user opted in.
      prefs: {
        set: [
          [PREF_THRESHOLD, "0"],
          [PREF_UI_DENSITY, 0],
          [PREF_AUTO_TOUCH, false],
        ],
      },
      inTablet: true,
    },
    () => {
      let density = gUIDensity.getCurrentDensity();
      is(
        density.mode,
        gUIDensity.MODE_NORMAL,
        "Standard density stays normal in tablet mode without the opt-in"
      );
      Assert.ok(
        !density.overridden,
        "The density is not reported as overridden"
      );
      Assert.ok(!isTouch(), "The document is not marked touch");
    }
  );
});

add_task(async function test_standard_optin_switches_to_touch_in_tablet_mode() {
  await withTabletMode(
    {
      // The user opted into auto-touch-mode for the standard density.
      prefs: {
        set: [
          [PREF_THRESHOLD, "0"],
          [PREF_UI_DENSITY, 0],
          [PREF_AUTO_TOUCH, true],
        ],
      },
      inTablet: true,
    },
    () => {
      let density = gUIDensity.getCurrentDensity();
      is(
        density.mode,
        gUIDensity.MODE_TOUCH,
        "Standard density switches to touch in tablet mode when the user " +
          "opted into auto-touch-mode"
      );
      Assert.ok(
        density.overridden,
        "The touch density is reported as overridden"
      );
      Assert.ok(isTouch(), "The document is marked touch");
    }
  );
});

add_task(async function test_auto_touch_ignored_for_explicit_compact() {
  await withTabletMode(
    {
      // Compact is an explicit choice. The auto-touch-mode pref is the standard
      // density's opt-in, so it must not override compact to touch even when on.
      prefs: {
        set: [
          [PREF_THRESHOLD, "0"],
          [PREF_UI_DENSITY, 1],
          [PREF_AUTO_TOUCH, true],
        ],
      },
      inTablet: true,
    },
    () => {
      let density = gUIDensity.getCurrentDensity();
      is(
        density.mode,
        gUIDensity.MODE_COMPACT,
        "Explicit compact stays compact in tablet mode even with " +
          "browser.touchmode.auto enabled"
      );
      Assert.ok(
        !density.overridden,
        "The density is not reported as overridden"
      );
      Assert.ok(!isTouch(), "The document is not marked touch");
    }
  );
});
