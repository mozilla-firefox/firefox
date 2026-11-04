/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

add_setup(async function () {
  ASRouter.resetMessageState();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.promo.pin.enabled", false]],
  });
  await ASRouter.onPrefChange();
});

add_task(async function test_experiment_messaging_system_dismiss() {
  let doExperimentCleanup = await setupMSExperimentWithMessage({
    id: `PB_NEWTAB_MESSAGING_SYSTEM_${Math.random()}`,
    template: "pb_newtab",
    content: {
      hideDefault: true,
      promoEnabled: true,
      promoLinkText: "fluent:about-private-browsing-prominent-cta",
      promoLinkType: "link",
      promoButton: {
        action: {
          data: {
            args: "http://bar.example.com/%LOCALE%",
            where: "tabshifted",
          },
          type: "OPEN_URL",
        },
      },
    },
    // Priority ensures this message is picked over the one in
    // OnboardingMessageProvider
    priority: 5,
    targeting: "true",
  });

  let { win: win1, tab: tab1 } = await openTabAndWaitForRender();

  await clickPromoDismissButton(tab1);

  let { win: win2, tab: tab2 } = await openTabAndWaitForRender();

  await SpecialPowers.spawn(tab2, [PROMO_SELECTORS], async function (promo) {
    is(
      content.document.querySelector(promo.container),
      null,
      "should no longer render the experiment message after dismissing"
    );
  });

  await BrowserTestUtils.closeWindow(win1);
  await BrowserTestUtils.closeWindow(win2);
  await doExperimentCleanup();
});
