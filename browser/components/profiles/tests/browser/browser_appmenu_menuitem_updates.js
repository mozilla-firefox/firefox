/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

add_setup(() => {
  // Mock the executable process so we doon't launch a new process
  sinon.stub(SelectableProfileService, "execProcess");

  registerCleanupFunction(() => {
    sinon.restore();
  });
});

async function promiseAppMenuOpened() {
  let promiseViewShown = BrowserTestUtils.waitForEvent(
    PanelUI.panel,
    "ViewShown"
  );
  PanelUI.show();
  return promiseViewShown;
}

add_task(async function test_appmenu_updates_on_edit() {
  // We need to create a second profile for the name to be shown in the app
  // menu.
  await SelectableProfileService.createNewProfile();

  const INITIAL_NAME = "Initial name";
  const UPDATED_NAME = "Updated";

  SelectableProfileService.currentProfile.name = INITIAL_NAME;
  await promiseAppMenuOpened();
  let view = PanelMultiView.getViewNode(document, "appMenu-profiles-button");
  Assert.equal(view.label, INITIAL_NAME, "expected the initial name");

  SelectableProfileService.currentProfile.name = UPDATED_NAME;
  PanelUI.hide();
  await promiseAppMenuOpened();
  Assert.equal(view.label, UPDATED_NAME, "expected the name to be updated");
  PanelUI.hide();
});
