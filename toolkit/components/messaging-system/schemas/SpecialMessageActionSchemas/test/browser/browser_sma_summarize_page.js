/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { GenAI } = ChromeUtils.importESModule(
  "resource:///modules/GenAI.sys.mjs"
);

const SIDEBAR_REVAMP_PREF = "sidebar.revamp";
const CHAT_ENABLE_PREF = "browser.ml.chat.enable";
const CHAT_PAGE_PREF = "browser.ml.chat.page";
const CHAT_PROIVDER_PREF = "browser.ml.chat.provider";

add_task(async function test_SUMMARIZE_PAGE() {
  const stub = sinon.stub(GenAI, "handleAskChat");
  await SpecialPowers.pushPrefEnv({
    set: [
      [SIDEBAR_REVAMP_PREF, true],
      [CHAT_ENABLE_PREF, true],
      [CHAT_PAGE_PREF, true],
      [CHAT_PROIVDER_PREF, ""],
    ],
  });

  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    await SMATestUtils.executeAndValidateAction({
      type: "SUMMARIZE_PAGE",
      data: "callout",
    });

    Assert.equal(stub.firstCall.args[1].entry, "callout", "passed along entry");

    await SMATestUtils.executeAndValidateAction({ type: "SUMMARIZE_PAGE" });

    Assert.equal(
      stub.secondCall.args[1].entry,
      "message",
      "default message entry"
    );
  });

  await SpecialPowers.popPrefEnv();
  stub.restore();
});
