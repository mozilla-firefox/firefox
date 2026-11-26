/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test that the onboarding UI is displayed when first displaying the editor mode, and
// that it can be permanentely dismissed.
// See https://bugzilla.mozilla.org/show_bug.cgi?id=1558417

"use strict";

const TEST_URI =
  "data:text/html;charset=utf-8,<!DOCTYPE html>Test onboarding UI";
const EDITOR_UI_PREF = "devtools.webconsole.input.editor";
const EDITOR_ONBOARDING_PREF = "devtools.webconsole.input.editorOnboarding";

add_task(async function () {
  // Enable editor mode and force the onboarding pref to true so it's displayed.
  await pushPref(EDITOR_UI_PREF, true);
  await pushPref(EDITOR_ONBOARDING_PREF, true);

  let hud = await openNewTabAndConsole(TEST_URI);

  info("Check that the onboarding UI is displayed");
  const onboardingElement = getOnboardingEl(hud);
  ok(onboardingElement, "The onboarding UI exists");

  const shortcuts = await waitFor(() => {
    const els = onboardingElement.querySelectorAll(
      ".editor-onboarding-shortcut"
    );
    return els.length == 2 && els[0].textContent ? els : null;
  });
  is(shortcuts[0].textContent, "Enter", "The Enter key is displayed");
  is(
    shortcuts[1].textContent,
    Services.appinfo.OS === "Darwin" ? "Cmd+Enter" : "Ctrl+Enter",
    "The run shortcut is displayed"
  );

  info("Check that the onboarding UI can be dismissed");
  const dismissButton = onboardingElement.querySelector(
    ".editor-onboarding-dismiss-button"
  );
  ok(dismissButton, "There's a dismiss button");
  dismissButton.click();

  await waitFor(() => !getOnboardingEl(hud));
  ok(true, "The onboarding UI is hidden after clicking the dismiss button");

  info("Check that the onboarding UI isn't displayed after a toolbox restart");
  await closeConsole();
  hud = await openConsole();
  is(
    getOnboardingEl(hud),
    null,
    "The onboarding UI isn't displayed after a toolbox restart after being dismissed"
  );

  Services.prefs.clearUserPref(EDITOR_UI_PREF);
  Services.prefs.clearUserPref(EDITOR_ONBOARDING_PREF);
});

function getOnboardingEl(hud) {
  return hud.ui.outputNode.querySelector(".editor-onboarding");
}
