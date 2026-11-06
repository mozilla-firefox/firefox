/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var gDialog;
var paramBlock;
var gPrintService = null;
var gPrintSettings = null;
var gStringBundle = null;
var gDoingMetric = false;

var gPrintSettingsInterface = Ci.nsIPrintSettings;
var gDoDebug = false;

// ---------------------------------------------------
function initDialog() {
  gDialog = {};

  gDialog.orientation = document.getElementById("orientation");
  gDialog.portrait = document.getElementById("portrait");
  gDialog.landscape = document.getElementById("landscape");

  gDialog.printBG = document.getElementById("printBG");

  gDialog.shrinkToFit = document.getElementById("shrinkToFit");

  gDialog.marginGroup = document.getElementById("marginGroup");

  gDialog.marginPage = document.getElementById("marginPage");
  gDialog.marginTop = document.getElementById("marginTop");
  gDialog.marginBottom = document.getElementById("marginBottom");
  gDialog.marginLeft = document.getElementById("marginLeft");
  gDialog.marginRight = document.getElementById("marginRight");

  gDialog.topInput = document.getElementById("topInput");
  gDialog.bottomInput = document.getElementById("bottomInput");
  gDialog.leftInput = document.getElementById("leftInput");
  gDialog.rightInput = document.getElementById("rightInput");

  gDialog.hLeftOption = document.getElementById("hLeftOption");
  gDialog.hCenterOption = document.getElementById("hCenterOption");
  gDialog.hRightOption = document.getElementById("hRightOption");

  gDialog.fLeftOption = document.getElementById("fLeftOption");
  gDialog.fCenterOption = document.getElementById("fCenterOption");
  gDialog.fRightOption = document.getElementById("fRightOption");

  gDialog.scalingLabel = document.getElementById("scalingInput");
  gDialog.scalingInput = document.getElementById("scalingInput");

  gDialog.enabled = false;

  document.addEventListener("dialogaccept", onAccept);
}

// ---------------------------------------------------
function isListOfPrinterFeaturesAvailable() {
  return Services.prefs.getBoolPref(
    "print.tmp.printerfeatures." +
      gPrintSettings.printerName +
      ".has_special_printerfeatures",
    false
  );
}

// ---------------------------------------------------
function checkDouble(element) {
  element.value = element.value.replace(/[^.0-9]/g, "");
}

// Theoretical paper width/height.
var gPageWidth = 8.5;
var gPageHeight = 11.0;

// ---------------------------------------------------
function setOrientation() {
  var selection = gDialog.orientation.selectedItem;

  var style = "background-color:white;";
  if (
    (selection == gDialog.portrait && gPageWidth > gPageHeight) ||
    (selection == gDialog.landscape && gPageWidth < gPageHeight)
  ) {
    // Swap width/height.
    var temp = gPageHeight;
    gPageHeight = gPageWidth;
    gPageWidth = temp;
  }
  var div = gDoingMetric ? 100 : 10;
  style +=
    "width:" +
    gPageWidth / div +
    unitString() +
    ";height:" +
    gPageHeight / div +
    unitString() +
    ";";
  gDialog.marginPage.setAttribute("style", style);
}

// ---------------------------------------------------
function unitString() {
  return gPrintSettings.paperSizeUnit ==
    gPrintSettingsInterface.kPaperSizeInches
    ? "in"
    : "mm";
}

// ---------------------------------------------------
function checkMargin(value, max, other) {
  // Don't draw this margin bigger than permitted.
  return Math.min(value, max - other.value);
}

// ---------------------------------------------------
function changeMargin(node) {
  // Correct invalid input.
  checkDouble(node);

  // Reset the margin height/width for this node.
  var val = node.value;
  var nodeToStyle;
  var attr = "width";
  if (node == gDialog.topInput) {
    nodeToStyle = gDialog.marginTop;
    val = checkMargin(val, gPageHeight, gDialog.bottomInput);
    attr = "height";
  } else if (node == gDialog.bottomInput) {
    nodeToStyle = gDialog.marginBottom;
    val = checkMargin(val, gPageHeight, gDialog.topInput);
    attr = "height";
  } else if (node == gDialog.leftInput) {
    nodeToStyle = gDialog.marginLeft;
    val = checkMargin(val, gPageWidth, gDialog.rightInput);
  } else {
    nodeToStyle = gDialog.marginRight;
    val = checkMargin(val, gPageWidth, gDialog.leftInput);
  }
  var style = attr + ":" + val / 10 + unitString() + ";";
  nodeToStyle.setAttribute("style", style);
}

// ---------------------------------------------------
function changeMargins() {
  changeMargin(gDialog.topInput);
  changeMargin(gDialog.bottomInput);
  changeMargin(gDialog.leftInput);
  changeMargin(gDialog.rightInput);
}

// ---------------------------------------------------
async function customize(node) {
  // If selection is now "Custom..." then prompt user for custom setting.
  if (node.value == 6) {
    let [title, promptText] = await document.l10n.formatValues([
      { id: "custom-prompt-title" },
      { id: "custom-prompt-prompt" },
    ]);
    var result = { value: node.custom };
    var ok = Services.prompt.prompt(window, title, promptText, result, null, {
      value: false,
    });
    if (ok) {
      node.custom = result.value;
    }
  }
}

// ---------------------------------------------------
function setHeaderFooter(node, value) {
  node.value = hfValueToId(value);
  if (node.value == 6) {
    // Remember current Custom... value.
    node.custom = value;
  } else {
    // Start with empty Custom... value.
    node.custom = "";
  }
}

var gHFValues = [];
gHFValues["&T"] = 1;
gHFValues["&U"] = 2;
gHFValues["&D"] = 3;
gHFValues["&P"] = 4;
gHFValues["&PT"] = 5;

function hfValueToId(val) {
  if (val in gHFValues) {
    return gHFValues[val];
  }
  if (val.length) {
    return 6; // Custom...
  }
  return 0; // --blank--
}

function hfIdToValue(node) {
  var result = "";
  switch (parseInt(node.value)) {
    case 0:
      break;
    case 1:
      result = "&T";
      break;
    case 2:
      result = "&U";
      break;
    case 3:
      result = "&D";
      break;
    case 4:
      result = "&P";
      break;
    case 5:
      result = "&PT";
      break;
    case 6:
      result = node.custom;
      break;
  }
  return result;
}

async function lastUsedPrinterNameOrDefault() {
  let printerList = Cc["@mozilla.org/gfx/printerlist;1"].getService(
    Ci.nsIPrinterList
  );
  let lastUsedName = gPrintService.lastUsedPrinterName;
  let printers = await printerList.printers;
  for (let printer of printers) {
    printer.QueryInterface(Ci.nsIPrinter);
    if (printer.name == lastUsedName) {
      return lastUsedName;
    }
  }
  return printerList.systemDefaultPrinterName;
}

async function setPrinterDefaultsForSelectedPrinter() {
  if (gPrintSettings.printerName == "") {
    gPrintSettings.printerName = await lastUsedPrinterNameOrDefault();
  }

  // First get any defaults from the printer
  gPrintService.initPrintSettingsFromPrinter(
    gPrintSettings.printerName,
    gPrintSettings
  );

  // now augment them with any values from last time
  gPrintService.initPrintSettingsFromPrefs(
    gPrintSettings,
    true,
    gPrintSettingsInterface.kInitSaveAll
  );

  if (gDoDebug) {
    dump(
      "pagesetup/setPrinterDefaultsForSelectedPrinter: printerName='" +
        gPrintSettings.printerName +
        "', orientation='" +
        gPrintSettings.orientation +
        "'\n"
    );
  }
}

// ---------------------------------------------------
async function loadDialog() {
  var print_orientation = 0;
  var print_margin_top = 0.5;
  var print_margin_left = 0.5;
  var print_margin_bottom = 0.5;
  var print_margin_right = 0.5;

  try {
    gPrintService = Cc["@mozilla.org/gfx/printsettings-service;1"];
    if (gPrintService) {
      gPrintService = gPrintService.getService();
      if (gPrintService) {
        gPrintService = gPrintService.QueryInterface(
          Ci.nsIPrintSettingsService
        );
      }
    }
  } catch (ex) {
    dump("loadDialog: ex=" + ex + "\n");
  }

  await setPrinterDefaultsForSelectedPrinter();

  gDialog.printBG.checked =
    gPrintSettings.printBGColors || gPrintSettings.printBGImages;

  gDialog.shrinkToFit.checked = gPrintSettings.shrinkToFit;

  gDialog.scalingLabel.disabled = gDialog.scalingInput.disabled =
    gDialog.shrinkToFit.checked;

  if (
    gPrintSettings.paperSizeUnit == gPrintSettingsInterface.kPaperSizeInches
  ) {
    document.l10n.setAttributes(
      gDialog.marginGroup,
      "margin-group-label-inches"
    );
    gDoingMetric = false;
  } else {
    document.l10n.setAttributes(
      gDialog.marginGroup,
      "margin-group-label-metric"
    );
    // Also, set global page dimensions for A4 paper, in millimeters (assumes portrait at this point).
    gPageWidth = 2100;
    gPageHeight = 2970;
    gDoingMetric = true;
  }

  print_orientation = gPrintSettings.orientation;
  print_margin_top = convertMarginInchesToUnits(
    gPrintSettings.marginTop,
    gDoingMetric
  );
  print_margin_left = convertMarginInchesToUnits(
    gPrintSettings.marginLeft,
    gDoingMetric
  );
  print_margin_right = convertMarginInchesToUnits(
    gPrintSettings.marginRight,
    gDoingMetric
  );
  print_margin_bottom = convertMarginInchesToUnits(
    gPrintSettings.marginBottom,
    gDoingMetric
  );

  if (gDoDebug) {
    dump("print_orientation   " + print_orientation + "\n");

    dump("print_margin_top    " + print_margin_top + "\n");
    dump("print_margin_left   " + print_margin_left + "\n");
    dump("print_margin_right  " + print_margin_right + "\n");
    dump("print_margin_bottom " + print_margin_bottom + "\n");
  }

  if (print_orientation == gPrintSettingsInterface.kPortraitOrientation) {
    gDialog.orientation.selectedItem = gDialog.portrait;
  } else if (
    print_orientation == gPrintSettingsInterface.kLandscapeOrientation
  ) {
    gDialog.orientation.selectedItem = gDialog.landscape;
  }

  // Set orientation the first time on a timeout so the dialog sizes to the
  // maximum height specified in the .xul file.  Otherwise, if the user switches
  // from landscape to portrait, the content grows and the buttons are clipped.
  setTimeout(setOrientation, 0);

  gDialog.topInput.value = print_margin_top.toFixed(1);
  gDialog.bottomInput.value = print_margin_bottom.toFixed(1);
  gDialog.leftInput.value = print_margin_left.toFixed(1);
  gDialog.rightInput.value = print_margin_right.toFixed(1);
  changeMargins();

  setHeaderFooter(gDialog.hLeftOption, gPrintSettings.headerStrLeft);
  setHeaderFooter(gDialog.hCenterOption, gPrintSettings.headerStrCenter);
  setHeaderFooter(gDialog.hRightOption, gPrintSettings.headerStrRight);

  setHeaderFooter(gDialog.fLeftOption, gPrintSettings.footerStrLeft);
  setHeaderFooter(gDialog.fCenterOption, gPrintSettings.footerStrCenter);
  setHeaderFooter(gDialog.fRightOption, gPrintSettings.footerStrRight);

  gDialog.scalingInput.value = (gPrintSettings.scaling * 100).toFixed(0);

  // Enable/disable widgets based in the information whether the selected
  // printer supports the matching feature or not
  if (isListOfPrinterFeaturesAvailable()) {
    if (
      Services.prefs.getBoolPref(
        "print.tmp.printerfeatures." +
          gPrintSettings.printerName +
          ".can_change_orientation"
      )
    ) {
      gDialog.orientation.removeAttribute("disabled");
    } else {
      gDialog.orientation.setAttribute("disabled", "true");
    }
  }

  // Give initial focus to the orientation radio group.
  // Done on a timeout due to to bug 103197.
  setTimeout(function () {
    gDialog.orientation.focus();
  }, 0);
}

// ---------------------------------------------------
function onLoad() {
  // Init gDialog.
  initDialog();

  if (window.arguments[0] != null) {
    gPrintSettings = window.arguments[0].QueryInterface(Ci.nsIPrintSettings);
    paramBlock = window.arguments[1].QueryInterface(Ci.nsIDialogParamBlock);
  } else if (gDoDebug) {
    alert("window.arguments[0] == null!");
  }

  // default return value is "cancel"
  paramBlock.SetInt(0, 0);

  if (gPrintSettings) {
    loadDialog();
  } else if (gDoDebug) {
    alert("Could initialize gDialog, PrintSettings is null!");
  }
}

function convertUnitsMarginToInches(aVal, aIsMetric) {
  if (aIsMetric) {
    return aVal / 25.4;
  }
  return aVal;
}

function convertMarginInchesToUnits(aVal, aIsMetric) {
  if (aIsMetric) {
    return aVal * 25.4;
  }
  return aVal;
}

// ---------------------------------------------------
function onAccept() {
  if (gPrintSettings) {
    if (gDialog.orientation.selectedItem == gDialog.portrait) {
      gPrintSettings.orientation = gPrintSettingsInterface.kPortraitOrientation;
    } else {
      gPrintSettings.orientation =
        gPrintSettingsInterface.kLandscapeOrientation;
    }

    // save these out so they can be picked up by the device spec
    gPrintSettings.marginTop = convertUnitsMarginToInches(
      gDialog.topInput.value,
      gDoingMetric
    );
    gPrintSettings.marginLeft = convertUnitsMarginToInches(
      gDialog.leftInput.value,
      gDoingMetric
    );
    gPrintSettings.marginBottom = convertUnitsMarginToInches(
      gDialog.bottomInput.value,
      gDoingMetric
    );
    gPrintSettings.marginRight = convertUnitsMarginToInches(
      gDialog.rightInput.value,
      gDoingMetric
    );

    gPrintSettings.headerStrLeft = hfIdToValue(gDialog.hLeftOption);
    gPrintSettings.headerStrCenter = hfIdToValue(gDialog.hCenterOption);
    gPrintSettings.headerStrRight = hfIdToValue(gDialog.hRightOption);

    gPrintSettings.footerStrLeft = hfIdToValue(gDialog.fLeftOption);
    gPrintSettings.footerStrCenter = hfIdToValue(gDialog.fCenterOption);
    gPrintSettings.footerStrRight = hfIdToValue(gDialog.fRightOption);

    gPrintSettings.printBGColors = gDialog.printBG.checked;
    gPrintSettings.printBGImages = gDialog.printBG.checked;

    gPrintSettings.shrinkToFit = gDialog.shrinkToFit.checked;

    var scaling = document.getElementById("scalingInput").value;
    if (scaling < 10.0) {
      scaling = 10.0;
    }
    if (scaling > 500.0) {
      scaling = 500.0;
    }
    scaling /= 100.0;
    gPrintSettings.scaling = scaling;

    if (gDoDebug) {
      dump("******* Page Setup Accepting ******\n");
      dump("print_margin_top    " + gDialog.topInput.value + "\n");
      dump("print_margin_left   " + gDialog.leftInput.value + "\n");
      dump("print_margin_right  " + gDialog.bottomInput.value + "\n");
      dump("print_margin_bottom " + gDialog.rightInput.value + "\n");
    }
  }

  // set return value to "ok"
  if (paramBlock) {
    paramBlock.SetInt(0, 1);
  } else {
    dump("*** FATAL ERROR: No paramBlock\n");
  }

  var flags =
    gPrintSettingsInterface.kInitSaveMargins |
    gPrintSettingsInterface.kInitSaveHeaderLeft |
    gPrintSettingsInterface.kInitSaveHeaderCenter |
    gPrintSettingsInterface.kInitSaveHeaderRight |
    gPrintSettingsInterface.kInitSaveFooterLeft |
    gPrintSettingsInterface.kInitSaveFooterCenter |
    gPrintSettingsInterface.kInitSaveFooterRight |
    gPrintSettingsInterface.kInitSaveBGColors |
    gPrintSettingsInterface.kInitSaveBGImages |
    gPrintSettingsInterface.kInitSaveInColor |
    gPrintSettingsInterface.kInitSaveOrientation |
    gPrintSettingsInterface.kInitSaveShrinkToFit |
    gPrintSettingsInterface.kInitSaveScaling;

  // Note that this file is Windows only code, so this doesn't handle saving
  // for other platforms.
  // XXX Should we do this in nsPrintDialogServiceWin::ShowPageSetup (the code
  // that invokes us), since ShowPageSetup is where we do the saving for the
  // other platforms?
  gPrintService.maybeSavePrintSettingsToPrefs(gPrintSettings, flags);
}

// ---------------------------------------------------
function onCancel() {
  // set return value to "cancel"
  if (paramBlock) {
    paramBlock.SetInt(0, 0);
  } else {
    dump("*** FATAL ERROR: No paramBlock\n");
  }

  return true;
}
