/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function toggle_readOnly() {
  gURLBar.focus();
  Assert.ok(gURLBar.hasAttribute("focused"), "Gets focused attribute");
  gURLBar.readOnly = true;
  Assert.ok(
    gURLBar.inputField.readOnly,
    "Input field reflects read-only state"
  );
  Assert.ok(
    !gURLBar.hasAttribute("focused"),
    "Loses focused attribute when becoming read-only"
  );

  // autoOpen should bail out early when read-only.
  let opened = gURLBar.view.autoOpen({
    event: new MouseEvent("mousedown"),
  });
  Assert.ok(!opened, "view.autoOpen returns false when read-only");
  Assert.ok(!gURLBar.view.isOpen, "view is not open");

  gURLBar.readOnly = false;
  Assert.ok(!gURLBar.inputField.readOnly, "inputField is not read-only again");

  // After re-enabling, the view should open normally again.
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  await UrlbarTestUtils.promisePopupClose(window, () => {
    gURLBar.blur();
  });
});
