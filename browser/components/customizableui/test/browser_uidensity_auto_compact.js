/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Tests the dynamic auto-compact behavior of gUIDensity, which overrides the
// uidensity to "compact" in small windows under nova, based on the
// browser.compactmode.auto.threshold pref (Bug 2044082).

const PREF_UI_DENSITY = "browser.uidensity";
const PREF_THRESHOLD = "browser.compactmode.auto.threshold";

// Returns a threshold string strictly below the given ratio (so the
// corresponding auto-compact check fires), and one strictly above it (so the
// check does not fire).
function below(ratio) {
  return String(ratio / 2);
}
function above(ratio) {
  return String(ratio * 2);
}

// The auto-compact height check is REFERENCE_HEIGHT / referenceHeight >
// threshold. Compute the ratio for the given window so we can pick thresholds
// that deterministically flip the trigger regardless of the window's real size.
function heightRatio(win) {
  return (
    win.gUIDensity.AUTO_COMPACT_REFERENCE_TABSTRIP_HEIGHT /
    win.gUIDensity._densityReferenceSize().height
  );
}

async function withNewWindow(callback) {
  let win = await BrowserTestUtils.openNewBrowserWindow();
  try {
    await callback(win);
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
}

function isCompact(win) {
  return win.document.documentElement.getAttribute("uidensity") == "compact";
}

// Reads a computed custom property off the window's root element.
function cssVar(win, name) {
  return win
    .getComputedStyle(win.document.documentElement)
    .getPropertyValue(name)
    .trim();
}

// Runs `callback` with only the collapsed-launcher width branch of
// _shouldAutoCompact() able to activate compact mode, so compact tracks the launcher
// state alone. Resizing the window narrow enough to cross the width threshold
// naturally isn't an option (resizeTo below the WM minimum width is unreliable
// in CI), so instead:
//   - zero out the tabstrip height, which makes the height ratio 0 so
//     that check can never exceed a threshold, and
//   - increase the launcher width to the window's width, making the
//     width ratio exactly 1, then pick a threshold below it.
async function withLauncherWidthCheckOnly(win, callback) {
  let { gUIDensity } = win;
  let originalRefHeight = gUIDensity.AUTO_COMPACT_REFERENCE_TABSTRIP_HEIGHT;
  let originalRefWidth =
    gUIDensity.AUTO_COMPACT_REFERENCE_SIDEBAR_LAUNCHER_WIDTH;
  gUIDensity.AUTO_COMPACT_REFERENCE_TABSTRIP_HEIGHT = 0;
  gUIDensity.AUTO_COMPACT_REFERENCE_SIDEBAR_LAUNCHER_WIDTH =
    gUIDensity._densityReferenceSize().width;
  Services.prefs.setCharPref(PREF_THRESHOLD, below(1));
  try {
    await callback();
  } finally {
    gUIDensity.AUTO_COMPACT_REFERENCE_TABSTRIP_HEIGHT = originalRefHeight;
    gUIDensity.AUTO_COMPACT_REFERENCE_SIDEBAR_LAUNCHER_WIDTH = originalRefWidth;
    Services.prefs.clearUserPref(PREF_THRESHOLD);
  }
}

add_task(async function test_auto_compact_engages_in_small_window() {
  await SpecialPowers.pushPrefEnv({
    clear: [[PREF_UI_DENSITY]],
  });

  await withNewWindow(async win => {
    let ratio = heightRatio(win);
    Services.prefs.setCharPref(PREF_THRESHOLD, below(ratio));
    win.gUIDensity.update();

    let density = win.gUIDensity.getCurrentDensity();
    is(
      density.mode,
      win.gUIDensity.MODE_COMPACT,
      "Auto-compact engages when the tabstrip ratio exceeds the threshold"
    );
    Assert.ok(
      density.overridden,
      "The compact density is reported as overridden"
    );
    Assert.ok(isCompact(win), "The document is marked compact");
  });

  Services.prefs.clearUserPref(PREF_THRESHOLD);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_auto_compact_disabled_above_threshold() {
  await SpecialPowers.pushPrefEnv({
    clear: [[PREF_UI_DENSITY]],
  });

  await withNewWindow(async win => {
    let ratio = heightRatio(win);
    Services.prefs.setCharPref(PREF_THRESHOLD, above(ratio));
    win.gUIDensity.update();

    let density = win.gUIDensity.getCurrentDensity();
    is(
      density.mode,
      win.gUIDensity.MODE_NORMAL,
      "Auto-compact does not engage when the ratio is below the threshold"
    );
    Assert.ok(!density.overridden, "The density is not reported as overridden");
    Assert.ok(!isCompact(win), "The document is not marked compact");
  });

  Services.prefs.clearUserPref(PREF_THRESHOLD);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_user_uidensity_disables_auto_compact() {
  await withNewWindow(async win => {
    let ratio = heightRatio(win);
    await SpecialPowers.pushPrefEnv({
      set: [
        // A triggering threshold, but the user has explicitly chosen a
        // (non-default) uidensity, which must win over auto-compact.
        [PREF_THRESHOLD, below(ratio)],
        [PREF_UI_DENSITY, win.gUIDensity.MODE_TOUCH],
      ],
    });

    win.gUIDensity.update();

    let density = win.gUIDensity.getCurrentDensity();
    is(
      density.mode,
      win.gUIDensity.MODE_TOUCH,
      "Auto-compact is skipped when the user has chosen a uidensity value"
    );
    Assert.ok(!density.overridden, "The density is not reported as overridden");
    Assert.ok(!isCompact(win), "The document is not marked compact");

    await SpecialPowers.popPrefEnv();
  });
});

add_task(async function test_threshold_zero_disables_auto_compact() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_THRESHOLD, "0"]],
    clear: [[PREF_UI_DENSITY]],
  });

  await withNewWindow(async win => {
    win.gUIDensity.update();
    is(
      win.gUIDensity.getCurrentDensity().mode,
      win.gUIDensity.MODE_NORMAL,
      "A threshold of zero disables auto-compact entirely"
    );
    Assert.ok(!isCompact(win), "The document is not marked compact");
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_threshold_change_reevaluates() {
  await SpecialPowers.pushPrefEnv({
    clear: [[PREF_UI_DENSITY]],
  });

  await withNewWindow(async win => {
    let ratio = heightRatio(win);

    // Start above the threshold: not compact.
    Services.prefs.setCharPref(PREF_THRESHOLD, above(ratio));
    await TestUtils.waitForCondition(
      () => !isCompact(win),
      "Window starts non-compact above the threshold"
    );

    // Lowering the threshold under the ratio should engage compact via the
    // pref observer, without an explicit update() call.
    Services.prefs.setCharPref(PREF_THRESHOLD, below(ratio));
    await TestUtils.waitForCondition(
      () => isCompact(win),
      "Lowering the threshold re-evaluates and engages compact"
    );

    // Raising it back should disengage compact.
    Services.prefs.setCharPref(PREF_THRESHOLD, above(ratio));
    await TestUtils.waitForCondition(
      () => !isCompact(win),
      "Raising the threshold re-evaluates and disengages compact"
    );
  });

  Services.prefs.clearUserPref(PREF_THRESHOLD);
  await SpecialPowers.popPrefEnv();
});

// A resize triggers update() only when auto-compact could change the density.
// When it can't apply, resizing can't affect density, so update() is skipped
// to keep the resize hot path cheap (bug 2049353).
function resizeCallsUpdate(win) {
  let original = win.gUIDensity.update;
  let called = false;
  win.gUIDensity.update = function (...args) {
    called = true;
    return original.apply(this, args);
  };
  try {
    win.dispatchEvent(new win.Event("resize"));
    return called;
  } finally {
    win.gUIDensity.update = original;
  }
}

add_task(
  async function test_resize_triggers_update_when_auto_compact_applies() {
    await SpecialPowers.pushPrefEnv({
      clear: [[PREF_UI_DENSITY]],
    });

    await withNewWindow(async win => {
      Assert.ok(
        resizeCallsUpdate(win),
        "A resize triggers update() when auto-compact can apply"
      );
    });

    await SpecialPowers.popPrefEnv();
  }
);

add_task(async function test_resize_skips_update_with_user_uidensity() {
  await SpecialPowers.pushPrefEnv({
    set: [
      // A non-default (touch) value so prefHasUserValue() is unambiguously true.
      [PREF_UI_DENSITY, 2],
    ],
  });

  await withNewWindow(async win => {
    Assert.ok(
      !resizeCallsUpdate(win),
      "A resize skips update() when the user has chosen a uidensity value"
    );
  });

  await SpecialPowers.popPrefEnv();
});

// Regression test for bug 2047330: update() runs on every window resize, but
// must only notify consumers (the urlbar view and tabstrip listen for
// uidensitychanged) when the resolved density actually changes. Otherwise the
// view and tabstrip flicker continuously while the window is resized.
add_task(async function test_uidensitychanged_only_on_actual_change() {
  await withNewWindow(async win => {
    win.gUIDensity.update(win.gUIDensity.MODE_NORMAL);

    let count = 0;
    let listener = () => count++;
    win.addEventListener("uidensitychanged", listener);
    try {
      // Re-applying the same mode must not notify consumers.
      win.gUIDensity.update(win.gUIDensity.MODE_NORMAL);
      Assert.equal(count, 0, "No uidensitychanged when the mode is unchanged");

      // An actual change notifies exactly once.
      win.gUIDensity.update(win.gUIDensity.MODE_COMPACT);
      Assert.equal(
        count,
        1,
        "uidensitychanged fires once when the mode changes"
      );

      // Re-applying the new mode must not notify again.
      win.gUIDensity.update(win.gUIDensity.MODE_COMPACT);
      Assert.equal(
        count,
        1,
        "No uidensitychanged when re-applying the same mode"
      );
    } finally {
      win.removeEventListener("uidensitychanged", listener);
      win.gUIDensity.update(win.gUIDensity.MODE_NORMAL);
    }
  });
});

// Resize events that don't change the resolved density must not re-dispatch
// uidensitychanged. This reproduces the bug 2047330 STR (resizing the window)
// at the event level.
add_task(async function test_resize_without_change_does_not_redispatch() {
  await SpecialPowers.pushPrefEnv({
    set: [
      // A non-default (touch) value so prefHasUserValue() is unambiguously true.
      [PREF_UI_DENSITY, 2],
    ],
  });

  await withNewWindow(async win => {
    // Settle on a stable density that a resize cannot change.
    win.gUIDensity.update();

    let dispatched = false;
    let listener = () => {
      dispatched = true;
    };
    win.addEventListener("uidensitychanged", listener);
    try {
      for (let i = 0; i < 5; i++) {
        win.dispatchEvent(new win.Event("resize"));
      }
      Assert.ok(
        !dispatched,
        "uidensitychanged is not dispatched on resize when density is unchanged"
      );
    } finally {
      win.removeEventListener("uidensitychanged", listener);
    }
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_sidebar_launcher_collapsed_requires_revamp() {
  await withNewWindow(async win => {
    await SpecialPowers.pushPrefEnv({
      set: [["sidebar.revamp", false]],
    });
    Assert.ok(
      !win.gUIDensity._isSidebarLauncherCollapsed(),
      "The collapsed-launcher check is false when sidebar.revamp is disabled"
    );
    await SpecialPowers.popPrefEnv();
  });
});

add_task(async function test_collapsed_launcher_width_triggers_compact() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", true],
    ],
    clear: [[PREF_UI_DENSITY]],
  });

  await withNewWindow(async win => {
    await TestUtils.waitForCondition(
      () => win.SidebarController?.initialized,
      "SidebarController is initialized"
    );

    // Put the launcher in its visible-but-collapsed state explicitly rather
    // than relying on the default, which can be overridden by persisted state.
    win.SidebarController._state.userLauncherVisible = true;
    win.SidebarController._state.launcherExpanded = false;

    Assert.ok(
      win.gUIDensity._isSidebarLauncherCollapsed(),
      "The launcher is visible and collapsed"
    );

    await withLauncherWidthCheckOnly(win, async () => {
      win.gUIDensity.update();
      Assert.ok(
        isCompact(win),
        "Compact engages via the collapsed-launcher width check"
      );

      // Expanding the launcher removes the collapsed condition, so compact
      // should disengage since the height check can't engage it.
      win.SidebarController._state.launcherExpanded = true;
      win.gUIDensity.update();
      Assert.ok(
        !isCompact(win),
        "Compact disengages once the launcher is expanded"
      );

      win.SidebarController._state.launcherExpanded = false;
    });
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_hiding_launcher_disengages_compact() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", false],
      ["sidebar.visibility", "hide-on-close"],
    ],
    clear: [[PREF_UI_DENSITY]],
  });

  await withNewWindow(async win => {
    await TestUtils.waitForCondition(
      () => win.SidebarController?.initialized,
      "SidebarController is initialized"
    );

    // Start from the STR's state: sidebar closed, launcher not showing.
    win.SidebarController._state.userLauncherVisible = false;

    await withLauncherWidthCheckOnly(win, async () => {
      await TestUtils.waitForCondition(
        () => !isCompact(win),
        "Window starts non-compact with the launcher hidden"
      );
      Assert.ok(!isCompact(win), "Not compact while the launcher is hidden");

      // Showing the launcher is the user's call in this mode - opening a panel
      // doesn't do it - so use the toolbar button as a user would.
      await win.SidebarController.handleToolbarButtonClick();
      await TestUtils.waitForCondition(
        () => isCompact(win),
        "Compact engages when the collapsed launcher is revealed"
      );
      Assert.ok(isCompact(win), "Compact engages when the launcher shows");

      await win.SidebarController.handleToolbarButtonClick();
      await TestUtils.waitForCondition(
        () => !isCompact(win),
        "Compact disengages once the launcher is hidden again"
      );
      Assert.ok(!isCompact(win), "Compact disengages when the launcher hides");
    });
  });

  await SpecialPowers.popPrefEnv();
});

// The expand-on-hover launcher is absolutely positioned while expanded, so it
// keeps reserving only its collapsed width. Compact must not flip on and off as
// the pointer enters and leaves the launcher.
add_task(async function test_expand_on_hover_keeps_compact() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", true],
      ["sidebar.visibility", "expand-on-hover"],
    ],
    clear: [[PREF_UI_DENSITY]],
  });

  await withNewWindow(async win => {
    await TestUtils.waitForCondition(
      () => win.SidebarController?.initialized,
      "SidebarController is initialized"
    );

    win.SidebarController._state.userLauncherVisible = true;
    win.SidebarController._state.launcherExpanded = false;

    await withLauncherWidthCheckOnly(win, async () => {
      await TestUtils.waitForCondition(
        () => isCompact(win),
        "Compact engages via the collapsed-launcher width check"
      );

      win.SidebarController._state.launcherExpanded = true;
      await TestUtils.waitForCondition(
        () => win.SidebarController._state.launcherExpanded,
        "The launcher reports itself expanded"
      );
      Assert.ok(
        win.gUIDensity._isSidebarLauncherCollapsed(),
        "A hover-expanded launcher still counts as collapsed"
      );
      Assert.ok(isCompact(win), "Compact stays engaged while hover-expanded");

      win.SidebarController._state.launcherExpanded = false;
      Assert.ok(isCompact(win), "Compact stays engaged once un-hovered");
    });
  });

  await SpecialPowers.popPrefEnv();
});

registerCleanupFunction(() => {
  // Enabling sidebar.revamp introduces the sidebar button as a side effect,
  // which persists this pref; clear it so we don't leak a changed pref.
  Services.prefs.clearUserPref(
    "browser.toolbarbuttons.introduced.sidebar-button"
  );
});
