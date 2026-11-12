/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/no-arbitrary-setTimeout */

"use strict";
const kWidgetId = "test-981418-widget-onbeforecreated";

// Should be able to add broken view widget
add_task(async function testAddOnBeforeCreatedWidget() {
  let onBeforeCreatedCalled = false;
  let widgetSpec = {
    id: kWidgetId,
    type: "view",
    viewId: kWidgetId + "idontexistyet",
    tooltiptext: "I am an accessible name",
    onBeforeCreated(doc) {
      let view = doc.createXULElement("panelview");
      view.id = kWidgetId + "idontexistyet";
      document.getElementById("appMenu-viewCache").appendChild(view);
      onBeforeCreatedCalled = true;
    },
  };

  CustomizableUI.createWidget(widgetSpec);
  CustomizableUI.addWidgetToArea(kWidgetId, CustomizableUI.AREA_NAVBAR);

  ok(onBeforeCreatedCalled, "onBeforeCreated should have been called");

  let widgetNode = document.getElementById(kWidgetId);
  let viewNode = document.getElementById(kWidgetId + "idontexistyet");
  ok(widgetNode, "Widget should exist");
  ok(viewNode, "Panelview should exist");

  let viewShownPromise = BrowserTestUtils.waitForEvent(viewNode, "ViewShown");
  widgetNode.click();
  await viewShownPromise;

  let widgetPanel = document.getElementById("customizationui-widget-panel");
  ok(widgetPanel, "Widget panel should exist");

  let panelHiddenPromise = promisePanelElementHidden(window, widgetPanel);
  widgetPanel.hidePopup();
  await panelHiddenPromise;

  CustomizableUI.addWidgetToArea(
    kWidgetId,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
  );
  await waitForOverflowButtonShown();
  await document.getElementById("nav-bar").overflowable.show();

  viewShownPromise = BrowserTestUtils.waitForEvent(viewNode, "ViewShown");
  widgetNode.click();
  await viewShownPromise;

  let panelHidden = promiseOverflowHidden(window);
  PanelUI.overflowPanel.hidePopup();
  await panelHidden;

  CustomizableUI.destroyWidget(kWidgetId);
  viewNode.remove();
});

add_task(async function asyncCleanup() {
  await resetCustomization();
});
