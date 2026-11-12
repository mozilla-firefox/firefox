/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// See https://bugzilla.mozilla.org/show_bug.cgi?id=941083

const kWidgetId = "test-invalidate-wrapper-cache";

// getWidget returns null for a destroyed widget that is still in gSeenWidgets.
// Forget the widget so that the first getWidget call of this test still gets
// a wrapper when the test runs again in the same browser.
registerCleanupFunction(() => {
  CustomizableUI.getTestOnlyInternalProp("gSeenWidgets").delete(kWidgetId);
});

// Check createWidget invalidates the widget cache
add_task(function () {
  let groupWrapper = CustomizableUI.getWidget(kWidgetId);
  ok(groupWrapper, "Should get group wrapper.");
  let singleWrapper = groupWrapper.forWindow(window);
  ok(singleWrapper, "Should get single wrapper.");

  CustomizableUI.createWidget({
    id: kWidgetId,
    label: "Test invalidating widgets caching",
  });

  let newGroupWrapper = CustomizableUI.getWidget(kWidgetId);
  ok(newGroupWrapper, "Should get a group wrapper again.");
  isnot(newGroupWrapper, groupWrapper, "Wrappers shouldn't be the same.");
  isnot(
    newGroupWrapper.provider,
    groupWrapper.provider,
    "Wrapper providers shouldn't be the same."
  );

  let newSingleWrapper = newGroupWrapper.forWindow(window);
  isnot(
    newSingleWrapper,
    singleWrapper,
    "Single wrappers shouldn't be the same."
  );
  isnot(
    newSingleWrapper.provider,
    singleWrapper.provider,
    "Single wrapper providers shouldn't be the same."
  );

  CustomizableUI.destroyWidget(kWidgetId);
  ok(
    !CustomizableUI.getWidget(kWidgetId),
    "Shouldn't get a wrapper after destroying the widget."
  );
});
