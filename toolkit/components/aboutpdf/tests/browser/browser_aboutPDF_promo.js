/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const PROMO_DISMISSED_PREF = "browser.aboutpdf.promo.dismissed";

registerCleanupFunction(() => {
  Services.prefs.clearUserPref(PROMO_DISMISSED_PREF);
});

add_task(async function testPromoHiddenWhenPrefDismissed() {
  await SpecialPowers.pushPrefEnv({
    set: [[PROMO_DISMISSED_PREF, true]],
  });

  const tab = await openAboutPDF();
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const promo = content.document.getElementById("promo");
    await ContentTaskUtils.waitForCondition(
      () => promo.hidden,
      "promo is hidden when dismissed pref is true"
    );
    ok(promo.hidden, "promo is hidden when dismissed pref is true");
  });
  BrowserTestUtils.removeTab(tab);

  await SpecialPowers.popPrefEnv();
});

add_task(async function testDismissButtonHidesPromoAndSetsPref() {
  Services.prefs.clearUserPref(PROMO_DISMISSED_PREF);

  const tab = await openAboutPDF();
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const promo = content.document.getElementById("promo");
    const dismiss = content.document.getElementById("dismiss-promo");

    // Force-show the promo so we can exercise the dismiss handler regardless
    // of platform / default-handler state.
    promo.hidden = false;

    dismiss.click();
    await ContentTaskUtils.waitForCondition(
      () => promo.hidden,
      "promo hidden after dismiss click"
    );
    ok(promo.hidden, "promo hidden after dismiss click");
  });

  await TestUtils.waitForCondition(
    () => Services.prefs.getBoolPref(PROMO_DISMISSED_PREF, false) === true,
    "dismissed pref persisted"
  );
  is(
    Services.prefs.getBoolPref(PROMO_DISMISSED_PREF, false),
    true,
    "dismissed pref persisted"
  );

  BrowserTestUtils.removeTab(tab);
  Services.prefs.clearUserPref(PROMO_DISMISSED_PREF);
});

// Open about:pdf with ShellService mocked so the promo is offered without
// touching the real OS default. setAsDefaultPDFHandler only reports that the
// attempt was made; the promo hides based on a fresh isDefaultHandlerFor check.
// `becomesDefault` models whether Firefox is the default after the attempt
// (the user confirming vs declining the macOS consent dialog): the set stub
// flips isDefaultHandlerFor accordingly, the way the real OS state would.
async function openPromoWithMockedShellService(sandbox, { becomesDefault }) {
  Services.prefs.clearUserPref(PROMO_DISMISSED_PREF);

  const { ShellService } = ChromeUtils.importESModule(
    // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
    "moz-src:///browser/components/shell/ShellService.sys.mjs"
  );

  sandbox.stub(ShellService, "canSetAsDefaultPDFHandler").get(() => true);
  // Not the default while the promo is first shown.
  const isDefaultHandlerForStub = sandbox
    .stub(ShellService, "isDefaultHandlerFor")
    .returns(false);
  const setStub = sandbox
    .stub(ShellService, "setAsDefaultPDFHandler")
    .callsFake(async () => {
      // The OS consent completes; Firefox becomes the default iff confirmed.
      isDefaultHandlerForStub.returns(becomesDefault);
      return true;
    });

  let tab;
  try {
    tab = await openAboutPDF();
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      const promo = content.document.getElementById("promo");
      await ContentTaskUtils.waitForCondition(
        () => !promo.hidden,
        "promo is shown when Firefox is not the default PDF handler"
      );
      // Make sure the button is in view for the synthesized click below.
      content.document.getElementById("set-default").scrollIntoView();
    });
  } catch (e) {
    if (tab) {
      BrowserTestUtils.removeTab(tab);
    }
    throw e;
  }

  isDefaultHandlerForStub.resetHistory();
  return { tab, setStub, isDefaultHandlerForStub };
}

// A trusted click is required: RPMSetDefaultPDFHandler enforces user
// activation, which a synthesized mouse event provides but a scripted .click()
// does not.
function clickSetDefault(tab) {
  return clickInAboutPDF(tab, "#set-default");
}

add_task(async function testSetDefaultHidesPromoWhenConfirmed() {
  const sandbox = sinon.createSandbox();
  let tab;
  let setStub;
  try {
    ({ tab, setStub } = await openPromoWithMockedShellService(sandbox, {
      becomesDefault: true,
    }));
    await clickSetDefault(tab);
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      const promo = content.document.getElementById("promo");
      await ContentTaskUtils.waitForCondition(
        () => promo.hidden,
        "promo hides once the set is confirmed"
      );
      ok(promo.hidden, "promo hidden after a confirmed set");
    });
    ok(setStub.calledOnce, "Called ShellService.setAsDefaultPDFHandler once");
  } finally {
    if (tab) {
      BrowserTestUtils.removeTab(tab);
    }
    sandbox.restore();
    Services.prefs.clearUserPref(PROMO_DISMISSED_PREF);
  }
});

add_task(async function testSetDefaultKeepsPromoWhenDeclined() {
  const sandbox = sinon.createSandbox();
  let tab;
  let setStub;
  let isDefaultHandlerForStub;
  try {
    ({ tab, setStub, isDefaultHandlerForStub } =
      await openPromoWithMockedShellService(sandbox, {
        becomesDefault: false,
      }));
    await clickSetDefault(tab);
    await TestUtils.waitForCondition(
      () => setStub.calledOnce,
      "set was attempted"
    );
    await TestUtils.waitForCondition(
      () => isDefaultHandlerForStub.called,
      "promo visibility was rechecked after the declined set"
    );

    const promoHidden = await SpecialPowers.spawn(
      tab.linkedBrowser,
      [],
      async () => content.document.getElementById("promo").hidden
    );
    ok(!promoHidden, "promo stays visible after a declined set");
  } finally {
    if (tab) {
      BrowserTestUtils.removeTab(tab);
    }
    sandbox.restore();
    Services.prefs.clearUserPref(PROMO_DISMISSED_PREF);
  }
});
