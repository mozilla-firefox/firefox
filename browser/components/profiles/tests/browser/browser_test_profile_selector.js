/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { CustomizableUITestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/CustomizableUITestUtils.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

async function promiseFocus(win) {
  if (!win.document.hasFocus()) {
    await BrowserTestUtils.waitForEvent(win.document, "focus");
  }
}

const execProcess = sinon.fake();
const sendCommandLine = sinon.fake.throws(Cr.NS_ERROR_NOT_AVAILABLE);

add_setup(() => {
  sinon.replace(
    SelectableProfileService,
    "sendCommandLine",
    (path, args, raise) => sendCommandLine(path, [...args], raise)
  );
  sinon.replace(SelectableProfileService, "execProcess", execProcess);

  registerCleanupFunction(() => {
    sinon.restore();
  });
});

add_task(async function test_selector_window() {
  await initGroupDatabase();
  let profile = SelectableProfileService.currentProfile;

  let cuiTestUtils = new CustomizableUITestUtils(window);

  await cuiTestUtils.openMainMenu();

  let profilesButton = PanelMultiView.getViewNode(
    document,
    "appMenu-profiles-button"
  );
  let subview = PanelMultiView.getViewNode(document, "PanelUI-profiles");

  let viewShown = BrowserTestUtils.waitForEvent(subview, "ViewShown");
  profilesButton.click();
  await viewShown;

  let manageButton = PanelMultiView.getViewNode(
    document,
    "profiles-manage-profiles-button"
  );

  let windowOpened = BrowserTestUtils.domWindowOpenedAndLoaded();

  await cuiTestUtils.hidePanelMultiView(cuiTestUtils.PanelUI.panel, () =>
    manageButton.click()
  );

  let dialog = await windowOpened;
  await promiseFocus(dialog);
  Assert.equal(dialog.location, "about:profilemanager");

  let deactivated = BrowserTestUtils.waitForEvent(dialog, "deactivate");
  await SimpleTest.promiseFocus(window);
  await deactivated;

  await cuiTestUtils.openMainMenu();
  viewShown = BrowserTestUtils.waitForEvent(subview, "ViewShown");
  profilesButton.click();
  await viewShown;

  let activated = promiseFocus(dialog);
  await cuiTestUtils.hidePanelMultiView(cuiTestUtils.PanelUI.panel, () =>
    manageButton.click()
  );
  await activated;

  let closed = BrowserTestUtils.domWindowClosed(dialog);

  const profileSelector = dialog.document.querySelector("profile-selector");
  await profileSelector.updateComplete;

  Assert.ok(profileSelector.checkbox.checked, "Checkbox should be checked");

  Assert.ok(
    !profileSelector.checkbox.querySelector('[slot="description"]'),
    "Description slot should not exist when checkbox is checked"
  );

  let asyncFlushCalled = false;
  gProfileService.asyncFlush = () => (asyncFlushCalled = true);

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  Services.telemetry.clearEvents();
  is(
    null,
    Glean.profilesSelectorWindow.showAtStartup.testGetValue(),
    "We have not recorded any Glean data yet"
  );

  profileSelector.checkbox.click();
  await BrowserTestUtils.waitForCondition(
    () => asyncFlushCalled,
    "Expected asyncFlush to be called"
  );
  asyncFlushCalled = false;

  Assert.ok(
    !profileSelector.checkbox.checked,
    "Checkbox should not be checked"
  );
  Assert.ok(
    !gProfileService.groupProfile.showProfileSelector,
    "Profile selector should be disabled"
  );

  Assert.ok(
    profileSelector.checkbox.querySelector('[slot="description"]'),
    "Description slot should exist when checkbox is unchecked"
  );

  await assertGlean(
    "profiles",
    "selector_window",
    "show_at_startup",
    "disabled"
  );

  // Simulate matching state.
  gProfileService.groupProfile.showProfileSelector = true;

  profileSelector.checkbox.click();
  await BrowserTestUtils.waitForCondition(
    () => asyncFlushCalled,
    "Expected asyncFlush to be called"
  );
  asyncFlushCalled = false;

  Assert.ok(profileSelector.checkbox.checked, "Checkbox should not be checked");
  Assert.ok(
    gProfileService.groupProfile.showProfileSelector,
    "Profile selector should be disabled"
  );

  Assert.ok(
    !profileSelector.checkbox.querySelector('[slot="description"]'),
    "Description slot should not exist when checkbox is checked again"
  );

  const profiles = profileSelector.profileCards;

  Assert.equal(profiles.length, 1, "There is one profile card");
  Assert.ok(
    profileSelector.createProfileCard,
    "The create profile card exists"
  );

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  Services.telemetry.clearEvents();
  is(
    null,
    Glean.profilesSelectorWindow.launch.testGetValue(),
    "We have not recorded any Glean data yet"
  );

  sendCommandLine.resetHistory();

  profileSelector.profileCards[0].click();

  let expected = ["--profiles-activate"];

  Assert.equal(
    sendCommandLine.callCount,
    1,
    "Should have attempted to remote to one instance"
  );
  Assert.deepEqual(
    sendCommandLine.firstCall.args,
    [profile.path, expected, true],
    "Expected sendCommandLine arguments"
  );

  expected.unshift("--profile", profile.path);

  if (Services.appinfo.OS === "Darwin") {
    expected.unshift("-foreground");
  }

  // Our mock remote service claims the instance is not running so we will fall back to launching
  // a new process.

  Assert.equal(execProcess.callCount, 1, "Should have called execProcess once");
  Assert.deepEqual(
    execProcess.firstCall.args,
    [expected],
    "Expected execProcess arguments"
  );

  await assertGlean("profiles", "selector_window", "launch");

  await closed;
});
