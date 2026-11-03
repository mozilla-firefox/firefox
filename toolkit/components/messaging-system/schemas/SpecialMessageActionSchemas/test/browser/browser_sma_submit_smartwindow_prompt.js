/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AIWindowUI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs"
);

const PROMPT =
  "Collect ingredients from recipes in my open tabs and put them in a page";

add_task(async function test_SUBMIT_SMARTWINDOW_PROMPT() {
  const stub = sinon.stub(AIWindowUI, "submitToSidebar").resolves();

  await SMATestUtils.executeAndValidateAction({
    type: "SUBMIT_SMARTWINDOW_PROMPT",
    data: { prompt: PROMPT },
  });

  Assert.equal(stub.callCount, 1, "Should submit to the sidebar once");
  Assert.equal(stub.firstCall.args[0], window, "Should use the window");
  Assert.deepEqual(
    stub.firstCall.args.slice(1),
    [PROMPT, { submitType: "fxms", aitabSource: "fxms" }],
    "Should send the prompt, attributed to the messaging system"
  );

  stub.restore();
});

add_task(async function test_SUBMIT_SMARTWINDOW_PROMPT_with_source() {
  const stub = sinon.stub(AIWindowUI, "submitToSidebar").resolves();

  await SMATestUtils.executeAndValidateAction({
    type: "SUBMIT_SMARTWINDOW_PROMPT",
    data: { prompt: PROMPT, source: "spotlight" },
  });

  Assert.deepEqual(
    stub.firstCall.args[2],
    { submitType: "spotlight", aitabSource: "spotlight" },
    "Should record the source supplied by the message"
  );

  stub.restore();
});
