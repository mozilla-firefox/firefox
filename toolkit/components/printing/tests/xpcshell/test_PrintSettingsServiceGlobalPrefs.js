/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const PRINTER = "TestPrinter_GlobalPrefs";

const PSSVC = Cc["@mozilla.org/gfx/printsettings-service;1"].getService(
  Ci.nsIPrintSettingsService
);

const SAVE_FLAGS =
  Ci.nsIPrintSettings.kInitSaveAll & ~Ci.nsIPrintSettings.kInitSavePrinterName;

const ATTRS = [
  "print_headerleft",
  "print_in_color",
  "print_shrink_to_fit",
  "print_duplex",
  "print_orientation",
  "print_edge_top",
  "print_edge_left",
];

function globalPref(attr) {
  return `print.${attr}`;
}

function perPrinterPref(attr) {
  return `print.printer_${PRINTER}.${attr}`;
}

function makeSettings() {
  let ps = PSSVC.createNewPrintSettings();
  ps.printerName = PRINTER;
  return ps;
}

function readSettings() {
  let ps = makeSettings();
  PSSVC.initPrintSettingsFromPrefs(ps, true, Ci.nsIPrintSettings.kInitSaveAll);
  return ps;
}

// A locked pref reads its default-branch value, which is what an
// administrator-locked pref looks like.
function setLocked(name, value) {
  let defaults = Services.prefs.getDefaultBranch("");
  switch (typeof value) {
    case "string":
      defaults.setStringPref(name, value);
      break;
    case "boolean":
      defaults.setBoolPref(name, value);
      break;
    default:
      defaults.setIntPref(name, value);
      break;
  }
  Services.prefs.lockPref(name);
}

function setUp() {
  for (let attr of ATTRS) {
    let name = globalPref(attr);
    if (Services.prefs.prefIsLocked(name)) {
      Services.prefs.unlockPref(name);
    }
    Services.prefs.clearUserPref(name);
    Services.prefs.clearUserPref(perPrinterPref(attr));
  }
  Services.prefs.setBoolPref("print.save_print_settings", true);
}

registerCleanupFunction(() => {
  setUp();
  Services.prefs.clearUserPref("print.save_print_settings");
});

add_task(function test_global_applies_without_per_printer() {
  setUp();
  Services.prefs.setStringPref(globalPref("print_headerleft"), "GLOBAL");

  Assert.equal(readSettings().headerStrLeft, "GLOBAL");
});

add_task(function test_per_printer_overrides_unlocked_global() {
  setUp();
  Services.prefs.setStringPref(globalPref("print_headerleft"), "GLOBAL");
  Services.prefs.setStringPref(
    perPrinterPref("print_headerleft"),
    "PERPRINTER"
  );

  Assert.equal(readSettings().headerStrLeft, "PERPRINTER");
});

add_task(function test_locked_global_overrides_per_printer() {
  setUp();
  setLocked(globalPref("print_headerleft"), "LOCKED");
  Services.prefs.setStringPref(
    perPrinterPref("print_headerleft"),
    "PERPRINTER"
  );

  Assert.equal(readSettings().headerStrLeft, "LOCKED");
});

add_task(function test_locked_global_applies_to_per_printer_only_attrs() {
  setUp();
  setLocked(
    globalPref("print_duplex"),
    Ci.nsIPrintSettings.kDuplexFlipOnLongEdge
  );
  Services.prefs.setIntPref(
    perPrinterPref("print_duplex"),
    Ci.nsIPrintSettings.kDuplexNone
  );
  setLocked(
    globalPref("print_orientation"),
    Ci.nsIPrintSettings.kLandscapeOrientation
  );

  let ps = readSettings();
  Assert.equal(ps.duplex, Ci.nsIPrintSettings.kDuplexFlipOnLongEdge);
  Assert.equal(ps.orientation, Ci.nsIPrintSettings.kLandscapeOrientation);
});

add_task(function test_locked_global_survives_save() {
  setUp();
  setLocked(
    globalPref("print_duplex"),
    Ci.nsIPrintSettings.kDuplexFlipOnLongEdge
  );

  let ps = makeSettings();
  ps.duplex = Ci.nsIPrintSettings.kDuplexNone;
  PSSVC.maybeSavePrintSettingsToPrefs(ps, SAVE_FLAGS);

  Assert.equal(
    readSettings().duplex,
    Ci.nsIPrintSettings.kDuplexFlipOnLongEdge
  );
});

add_task(function test_edges_keep_global_values_for_unset_sides() {
  setUp();
  setLocked(globalPref("print_edge_top"), 50);
  Services.prefs.setIntPref(globalPref("print_edge_left"), 25);

  let ps = readSettings();
  Assert.equal(ps.edgeTop, 0.5);
  Assert.equal(ps.edgeLeft, 0.25);
});
