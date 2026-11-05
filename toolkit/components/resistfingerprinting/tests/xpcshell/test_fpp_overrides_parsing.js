/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Tests the parsing of the privacy.fingerprintingProtection.overrides and
// privacy.baselineFingerprintingProtection.overrides prefs.

const { Targets } = ChromeUtils.importESModule(
  "resource://gre/modules/RFPTargetConstants.sys.mjs"
);

const FPP_PREF = "privacy.fingerprintingProtection.overrides";
const BASELINE_PREF = "privacy.baselineFingerprintingProtection.overrides";

function hasTarget(targetSet, name) {
  const value = Targets[name];
  Assert.notEqual(value, undefined, `${name} is a known RFPTarget`);
  const part = targetSet.getNth32BitSet(Math.floor(value / 32));
  return !!(part & (1 << (value % 32)));
}

function fppTargets() {
  return Services.rfp.enabledFingerprintingProtections;
}

function baselineTargets() {
  return Services.rfp.enabledFingerprintingProtectionsBaseline;
}

add_setup(() => {
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(FPP_PREF);
    Services.prefs.clearUserPref(BASELINE_PREF);
  });
});

// These targets are FPP defaults on every platform, so removing them is
// observable.
const DEFAULTS = [
  "CanvasRandomization",
  "JSMathFdlibm",
  "FontVisibilityLangPack",
];

add_task(function test_defaults() {
  Services.prefs.clearUserPref(FPP_PREF);
  for (let name of DEFAULTS) {
    ok(hasTarget(fppTargets(), name), `${name} is enabled by default`);
  }
  ok(!hasTarget(fppTargets(), "Gamepad"), "Gamepad is not enabled by default");
});

add_task(function test_whitespace_is_ignored() {
  const cases = [
    "-CanvasRandomization,-JSMathFdlibm,-FontVisibilityLangPack,+Gamepad",
    "-CanvasRandomization, -JSMathFdlibm, -FontVisibilityLangPack, +Gamepad",
    " -CanvasRandomization ,\t-JSMathFdlibm,\n-FontVisibilityLangPack , +Gamepad ",
    "-CanvasRandomization,,-JSMathFdlibm, ,-FontVisibilityLangPack,+Gamepad,",
  ];

  for (let value of cases) {
    info(`Testing overrides ${JSON.stringify(value)}`);
    Services.prefs.setStringPref(FPP_PREF, value);
    for (let name of DEFAULTS) {
      ok(!hasTarget(fppTargets(), name), `${name} was removed`);
    }
    ok(hasTarget(fppTargets(), "Gamepad"), "Gamepad was added");
    Services.prefs.clearUserPref(FPP_PREF);
  }
});

add_task(function test_whitespace_is_ignored_baseline() {
  Services.prefs.setStringPref(BASELINE_PREF, "+Gamepad, +PointerEvents");
  ok(hasTarget(baselineTargets(), "Gamepad"), "Gamepad was added to baseline");
  ok(
    hasTarget(baselineTargets(), "PointerEvents"),
    "PointerEvents was added to baseline"
  );
  Services.prefs.clearUserPref(BASELINE_PREF);
});

add_task(function test_invalid_entries_are_skipped() {
  Services.prefs.setStringPref(
    FPP_PREF,
    "Gamepad, +NotARealTarget, -, +, -CanvasRandomization"
  );
  ok(
    !hasTarget(fppTargets(), "Gamepad"),
    "An entry without a +/- prefix is ignored"
  );
  ok(
    !hasTarget(fppTargets(), "CanvasRandomization"),
    "Valid entries after invalid ones are still applied"
  );
  ok(hasTarget(fppTargets(), "JSMathFdlibm"), "Other defaults are untouched");
  Services.prefs.clearUserPref(FPP_PREF);
});
