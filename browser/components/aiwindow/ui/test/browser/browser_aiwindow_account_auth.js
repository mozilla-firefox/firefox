/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AIWindowAccountAuth } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindowAccountAuth.sys.mjs"
);

const { FunComputerAccounts } = ChromeUtils.importESModule(
  "resource:///modules/FunComputerAccounts.sys.mjs"
);

add_task(async function test_prompt_succeeds_when_funcomputer_signed_in() {
  const stub = sinon.stub(FunComputerAccounts, "getSession").resolves({
    email: "user@funcomputer.test",
    access_token: "tok",
  });

  try {
    const ok = await AIWindowAccountAuth.promptSignIn(gBrowser.selectedBrowser);
    Assert.ok(ok, "promptSignIn succeeds when FunComputer session exists");
    Assert.ok(AIWindowAccountAuth.hasToSConsent, "ToS consent is recorded");
  } finally {
    stub.restore();
  }
});

add_task(async function test_prompt_opens_about_funcomputer() {
  const stub = sinon.stub(FunComputerAccounts, "getSession").resolves(null);

  const pending = AIWindowAccountAuth.promptSignIn(gBrowser.selectedBrowser);
  await BrowserTestUtils.waitForCondition(
    () => gBrowser.currentURI.spec.startsWith("about:funcomputer"),
    "FunComputer account page opened"
  );

  stub.restore();
  sinon.stub(FunComputerAccounts, "getSession").resolves({
    email: "user@funcomputer.test",
    access_token: "tok",
  });
  Services.obs.notifyObservers(null, FunComputerAccounts.TOPIC);

  Assert.ok(await pending, "sign-in completes after FunComputer session");
  FunComputerAccounts.getSession.restore();
});
