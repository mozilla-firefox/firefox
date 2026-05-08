/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PdfJsGuessAltTextFeature } = ChromeUtils.importESModule(
  "resource://pdf.js/PdfJsAIFeature.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { MLUninstallService } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);

const PREF_ENABLED = "pdfjs.enableGuessAltText";
const PREF_ALT_TEXT_MODEL_DOWNLOAD = "pdfjs.enableAltTextModelDownload";
const PREF_ALT_TEXT_ENABLED = "pdfjs.enableAltText";

function clearGuessAltTextPrefs() {
  for (const pref of [
    PREF_ENABLED,
    PREF_ALT_TEXT_ENABLED,
    PREF_ALT_TEXT_MODEL_DOWNLOAD,
  ]) {
    if (Services.prefs.prefHasUserValue(pref)) {
      Services.prefs.clearUserPref(pref);
    }
  }
}

registerCleanupFunction(() => {
  clearGuessAltTextPrefs();
  sinon.restore();
});

add_task(function test_id_is_feature_id() {
  Assert.equal(
    PdfJsGuessAltTextFeature.id,
    "pdfjs-alt-text",
    "PdfJsGuessAltTextFeature.id should be the feature id"
  );
});

add_task(function test_contract_values() {
  Assert.ok(
    PdfJsGuessAltTextFeature.hasDistinctEnabledState,
    "PdfJsGuessAltTextFeature should expose a distinct enabled state"
  );
  Assert.ok(
    PdfJsGuessAltTextFeature.canRunOnDevice,
    "PdfJsGuessAltTextFeature should report that it can run on this device"
  );
});

add_task(function test_isEnabled_with_pref() {
  clearGuessAltTextPrefs();

  Assert.ok(
    !PdfJsGuessAltTextFeature.isEnabled,
    "With no user prefs set, isEnabled should be false"
  );

  Services.prefs.setBoolPref(PREF_ENABLED, true);
  Services.prefs.setBoolPref(PREF_ALT_TEXT_MODEL_DOWNLOAD, true);
  Services.prefs.setBoolPref(PREF_ALT_TEXT_ENABLED, true);
  Services.prefs.getBoolPref("browser.ml.enable", true);
  Assert.ok(PdfJsGuessAltTextFeature.isEnabled, "isEnabled should be true");
});

add_task(async function test_enable_sets_all_prefs_true() {
  clearGuessAltTextPrefs();

  await PdfJsGuessAltTextFeature.enable();

  Assert.equal(
    Services.prefs.getBoolPref(PREF_ENABLED, false),
    true,
    "enable() should set pdfjs.enableGuessAltText=true"
  );
  Assert.equal(
    Services.prefs.getBoolPref(PREF_ALT_TEXT_ENABLED, false),
    true,
    "enable() should set pdfjs.enableAltText=true"
  );
  Assert.equal(
    Services.prefs.getBoolPref(PREF_ALT_TEXT_MODEL_DOWNLOAD, false),
    true,
    "enable() should set pdfjs.enableAltTextModelDownload=true"
  );

  Assert.ok(
    PdfJsGuessAltTextFeature.isEnabled,
    "After enable(), isEnabled should be true"
  );
  Assert.equal(
    PdfJsGuessAltTextFeature.aiControlState,
    "enabled",
    "After enable(), aiControlState should be enabled"
  );
});

add_task(async function test_block_sets_prefs_false_and_uninstalls_models() {
  clearGuessAltTextPrefs();
  await PdfJsGuessAltTextFeature.enable();
  Assert.ok(
    PdfJsGuessAltTextFeature.isEnabled,
    "Sanity check: enabled before block()"
  );

  const uninstallStub = sinon.stub(MLUninstallService, "uninstall").resolves();

  await PdfJsGuessAltTextFeature.block();

  Assert.equal(
    Services.prefs.getBoolPref(PREF_ENABLED, true),
    false,
    "block() should set pdfjs.enableGuessAltText=false"
  );
  Assert.equal(
    Services.prefs.getBoolPref(PREF_ALT_TEXT_MODEL_DOWNLOAD, true),
    false,
    "block() should set pdfjs.enableAltTextModelDownload=false"
  );
  Assert.equal(
    Services.prefs.getBoolPref(PREF_ALT_TEXT_ENABLED, true),
    false,
    "block() should set pdfjs.enableAltText=false"
  );

  Assert.ok(
    !PdfJsGuessAltTextFeature.isEnabled,
    "After block(), isEnabled should be false"
  );
  Assert.equal(
    PdfJsGuessAltTextFeature.aiControlState,
    "blocked",
    "After block(), aiControlState should be blocked"
  );

  Assert.ok(
    uninstallStub.calledOnce,
    "block() should uninstall ML engine files via MLUninstallService.uninstall()"
  );

  const expectedEngineIds = [PdfJsGuessAltTextFeature.engineId];
  const uninstallArgs = uninstallStub.getCall(0).args[0];
  Assert.deepEqual(
    (uninstallArgs.engineIds || []).slice(),
    expectedEngineIds,
    "block() should uninstall files for the expected engine IDs"
  );
  Assert.equal(
    uninstallArgs.actor,
    PdfJsGuessAltTextFeature.engineId,
    "block() should pass the expected actor attribution"
  );

  uninstallStub.restore();
});

add_task(async function test_makeAvailable_sets_available_state() {
  clearGuessAltTextPrefs();
  await PdfJsGuessAltTextFeature.enable();

  const uninstallStub = sinon.stub(MLUninstallService, "uninstall").resolves();

  await PdfJsGuessAltTextFeature.makeAvailable();

  Assert.equal(
    Services.prefs.getBoolPref(PREF_ENABLED, true),
    false,
    "makeAvailable() should set pdfjs.enableGuessAltText=false"
  );
  Assert.equal(
    Services.prefs.getBoolPref(PREF_ALT_TEXT_MODEL_DOWNLOAD, true),
    false,
    "makeAvailable() should set pdfjs.enableAltTextModelDownload=false"
  );
  Assert.equal(
    Services.prefs.getBoolPref(PREF_ALT_TEXT_ENABLED, false),
    true,
    "makeAvailable() should set pdfjs.enableAltText=true"
  );
  Assert.ok(
    !PdfJsGuessAltTextFeature.isEnabled,
    "After makeAvailable(), isEnabled should be false"
  );
  Assert.ok(
    !PdfJsGuessAltTextFeature.isBlocked,
    "After makeAvailable(), isBlocked should be false"
  );
  Assert.equal(
    PdfJsGuessAltTextFeature.aiControlState,
    "available",
    "After makeAvailable(), aiControlState should be available"
  );
  Assert.ok(
    uninstallStub.calledOnce,
    "makeAvailable() should uninstall ML engine files"
  );

  uninstallStub.restore();
});
