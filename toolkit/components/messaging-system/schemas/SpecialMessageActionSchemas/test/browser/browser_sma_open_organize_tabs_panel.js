/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AIWindowUI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs"
);

add_task(async function test_OPEN_ORGANIZE_TABS_PANEL() {
  const stub = sinon.stub(AIWindowUI, "toggleGroupTabsPanel");

  await SMATestUtils.executeAndValidateAction({
    type: "OPEN_ORGANIZE_TABS_PANEL",
    data: { source: "callout_click" },
  });

  Assert.equal(
    stub.firstCall.args[0],
    window,
    "opened the panel in the acting window"
  );
  Assert.equal(
    stub.firstCall.args[1].source,
    "callout_click",
    "passed along source"
  );

  await SMATestUtils.executeAndValidateAction({
    type: "OPEN_ORGANIZE_TABS_PANEL",
  });

  Assert.equal(
    stub.secondCall.args[1].source,
    "message",
    "default message source"
  );

  stub.restore();
});
