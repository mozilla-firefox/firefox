/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests the "Ask before closing multiple tabs" prompt, which is gated by
// browser.tabs.warnOnClose in regular and permanent private windows and by
// the independent browser.tabs.warnOnClose.pbmode in transient private
// windows.

const BASE_PREF = "browser.tabs.warnOnClose";
const PBMODE_PREF = "browser.tabs.warnOnClose.pbmode";

async function closeWindow(
  onDialog = promptWindow => promptWindow.Dialog.ui.button0.click(),
  isPrivate = true
) {
  let win = await BrowserTestUtils.openNewBrowserWindow({ private: isPrivate });
  // Open a second tab so closing the window would warn about multiple tabs.
  win.BrowserCommands.openTab();
  let dialogShown = false;
  function onCommonDialogLoaded(promptWindow) {
    dialogShown = true;
    onDialog(promptWindow);
  }
  // Only observe while this close is in flight.
  Services.obs.addObserver(onCommonDialogLoaded, "common-dialog-loaded");
  let windowClosed = BrowserTestUtils.domWindowClosed(win);
  win.BrowserCommands.tryToCloseWindow();
  await windowClosed;
  Services.obs.removeObserver(onCommonDialogLoaded, "common-dialog-loaded");
  ok(win.closed, "window is closed");
  return dialogShown;
}

add_task(async function default_prefs_no_prompt() {
  // With both prefs at their default (off) a private window doesn't prompt.
  let showedPrompt = await closeWindow();
  ok(!showedPrompt, "no prompt with the default prefs");
});

add_task(async function base_pref_on_no_prompt() {
  // browser.tabs.warnOnClose alone doesn't apply to private windows.
  await SpecialPowers.pushPrefEnv({ set: [[BASE_PREF, true]] });
  let showedPrompt = await closeWindow();
  ok(!showedPrompt, "no prompt when only the base pref is on");
  await SpecialPowers.popPrefEnv();
});

add_task(async function pbmode_pref_on_prompt() {
  // browser.tabs.warnOnClose.pbmode is independent of the base pref.
  await SpecialPowers.pushPrefEnv({ set: [[PBMODE_PREF, true]] });
  let showedPrompt = await closeWindow();
  ok(showedPrompt, "prompt shown when only the pbmode pref is on");
  await SpecialPowers.popPrefEnv();
});

async function testUncheckDisablesPref({
  name,
  isPrivate,
  autostart = false,
  flipped,
}) {
  const untouched = flipped === BASE_PREF ? PBMODE_PREF : BASE_PREF;
  // Enable both prefs so only the one gating this window type may flip.
  await SpecialPowers.pushPrefEnv({
    set: [
      [BASE_PREF, true],
      [PBMODE_PREF, true],
      ["browser.privatebrowsing.autostart", autostart],
    ],
  });
  let showedPrompt = await closeWindow(promptWindow => {
    promptWindow.Dialog.ui.checkbox.click();
    ok(
      !promptWindow.Dialog.ui.checkbox.checked,
      `the ask-again checkbox is unchecked in ${name}`
    );
    promptWindow.Dialog.ui.button0.click();
  }, isPrivate);
  ok(showedPrompt, `prompt shown for ${name}`);
  is(
    Services.prefs.getBoolPref(flipped),
    false,
    `unchecking the checkbox disables ${flipped} for ${name}`
  );
  is(
    Services.prefs.getBoolPref(untouched),
    true,
    `${untouched} is left untouched for ${name}`
  );
  await SpecialPowers.popPrefEnv();
}

add_task(async function unchecking_prompt_disables_pref_regular_window() {
  await testUncheckDisablesPref({
    name: "regular window",
    isPrivate: false,
    flipped: BASE_PREF,
  });
});

add_task(async function unchecking_prompt_disables_pref_private_window() {
  await testUncheckDisablesPref({
    name: "transient private browsing",
    isPrivate: true,
    flipped: PBMODE_PREF,
  });
});

add_task(
  async function unchecking_prompt_disables_pref_permanent_private_window() {
    await testUncheckDisablesPref({
      name: "permanent private browsing",
      isPrivate: true,
      autostart: true,
      flipped: BASE_PREF,
    });
  }
);
