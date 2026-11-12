/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const BUTTONID = "test-XUL-wrapper-destroyWidget";

add_task(function () {
  let btn = createDummyXULButton(BUTTONID, "XUL btn");
  gNavToolbox.palette.appendChild(btn);
  let firstWrapper = CustomizableUI.getWidget(BUTTONID).forWindow(window);
  ok(firstWrapper, "Should get a wrapper");
  ok(firstWrapper.node, "Node should be there on first wrapper.");

  btn.remove();
  CustomizableUI.destroyWidget(BUTTONID);
  let secondWrapper = CustomizableUI.getWidget(BUTTONID).forWindow(window);
  isnot(
    firstWrapper,
    secondWrapper,
    "Wrappers should be different after destroyWidget call."
  );
  ok(!firstWrapper.node, "No node should be there on old wrapper.");
  ok(!secondWrapper.node, "No node should be there on new wrapper.");

  btn = createDummyXULButton(BUTTONID, "XUL btn");
  gNavToolbox.palette.appendChild(btn);
  let thirdWrapper = CustomizableUI.getWidget(BUTTONID).forWindow(window);
  ok(thirdWrapper, "Should get a wrapper");
  is(secondWrapper, thirdWrapper, "Should get the second wrapper again.");
  ok(firstWrapper.node, "Node should be there on old wrapper.");
  ok(secondWrapper.node, "Node should be there on second wrapper.");
  ok(thirdWrapper.node, "Node should be there on third wrapper.");

  btn.remove();
  CustomizableUI.destroyWidget(BUTTONID);
});
