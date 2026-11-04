/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const sandbox = sinon.createSandbox();

add_setup(async function () {
  await ASRouter.resetMessageState();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.promo.pin.enabled", true],
      ["browser.privateWindowRedesign.enabled", false],
    ],
  });
  await ASRouter.onPrefChange();
  // Stub out the doesAppNeedPin to true so that Pin Promo targeting evaluates true

  sandbox.stub(ShellService, "doesAppNeedPin").withArgs(true).returns(true);
  registerCleanupFunction(async () => {
    sandbox.restore();
  });
});

add_task(async function test_pin_promo() {
  let { win: win1, tab: tab1 } = await openTabAndWaitForRender();

  await SpecialPowers.spawn(tab1, [PROMO_SELECTORS], async function (promo) {
    const promoContainer = content.document.querySelector(promo.container);
    ok(promoContainer, "Pin promo is shown");
  });
  await assertPromoHeader(
    tab1,
    "about-private-browsing-pin-promo-header",
    "Correct default values are shown"
  );

  let { win: win2 } = await openTabAndWaitForRender();
  let { win: win3 } = await openTabAndWaitForRender();
  let { win: win4, tab: tab4 } = await openTabAndWaitForRender();

  // After 3 impressions the pin promo hits its frequency cap and is no longer
  // shown. Whatever promo (if any) takes its place is not this test's concern.
  await assertPromoHeaderNot(
    tab4,
    "about-private-browsing-pin-promo-header",
    "Pin promo is no longer shown after 3 impressions"
  );

  await BrowserTestUtils.closeWindow(win1);
  await BrowserTestUtils.closeWindow(win2);
  await BrowserTestUtils.closeWindow(win3);
  await BrowserTestUtils.closeWindow(win4);
});
