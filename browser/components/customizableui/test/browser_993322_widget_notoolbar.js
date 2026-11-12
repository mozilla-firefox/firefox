/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const BUTTONID = "test-API-created-widget-toolbar-gone";
const TOOLBARID = "test-API-created-extra-toolbar";

add_task(async function () {
  let toolbar = createToolbarWithPlacements(TOOLBARID, []);
  CustomizableUI.addWidgetToArea(BUTTONID, TOOLBARID);
  is(
    CustomizableUI.getPlacementOfWidget(BUTTONID).area,
    TOOLBARID,
    "Should be on toolbar"
  );
  is(toolbar.children.length, 0, "Toolbar has no kid");

  CustomizableUI.unregisterArea(TOOLBARID);
  CustomizableUI.createWidget({
    id: BUTTONID,
    label: "Test widget toolbar gone",
  });

  let currentWidget = CustomizableUI.getWidget(BUTTONID);

  await startCustomizing();
  let buttonNode = document.getElementById(BUTTONID);
  ok(buttonNode, "Should find button in window");
  if (buttonNode) {
    is(
      buttonNode.parentNode.localName,
      "toolbarpaletteitem",
      "Node should be wrapped"
    );
    is(
      buttonNode.parentNode.getAttribute("place"),
      "palette",
      "Node should be in palette"
    );
    is(
      buttonNode,
      gNavToolbox.palette.querySelector("#" + BUTTONID),
      "Node should really be in palette."
    );
  }
  is(
    currentWidget.forWindow(window).node,
    buttonNode,
    "Should have the same node for customize mode"
  );
  await endCustomizing();

  CustomizableUI.destroyWidget(BUTTONID);
  CustomizableUI.unregisterArea(TOOLBARID, true);
  toolbar.remove();
  gAddedToolbars.clear();
  // Destroyed widgets stay in gSeenWidgets, which would make adding the
  // widget to the toolbar fail if the test ran again in the same browser.
  CustomizableUI.getTestOnlyInternalProp("gSeenWidgets").delete(BUTTONID);
});
