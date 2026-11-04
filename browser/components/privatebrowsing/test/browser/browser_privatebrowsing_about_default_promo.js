/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const PromoInfo = {
  VPN: { enabledPref: "browser.vpn_promo.enabled" },
  PIN: { enabledPref: "browser.promo.pin.enabled" },
};

const sandbox = sinon.createSandbox();

async function resetState() {
  await Promise.all([
    ASRouter.resetMessageState(),
    ASRouter.resetGroupsState(),
    ASRouter.unblockAll(),
    sandbox.restore(),
  ]);
}

// There is no default pb_newtab promo any more: the only remaining one is the
// pin promo, which is gated on doesAppNeedPrivatePin and so is not reliable
// across platforms. Tasks that just need a promo to render enroll their own.
function enrollPromoMessage() {
  return setupMSExperimentWithMessage({
    id: `PB_NEWTAB_PROMO_${Math.random()}`,
    template: "pb_newtab",
    content: {
      hideDefault: true,
      promoEnabled: true,
      promoLinkText: "fluent:about-private-browsing-prominent-cta",
      promoLinkType: "link",
      promoButton: {
        action: {
          data: { args: "https://example.com/", where: "tabshifted" },
          type: "OPEN_URL",
        },
      },
    },
    // Priority ensures this message is picked over the ones in
    // OnboardingMessageProvider.
    priority: 5,
    targeting: "true",
  });
}

add_setup(async function () {
  registerCleanupFunction(resetState);
  await resetState();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.promo.pin.enabled", false]],
  });
  await ASRouter.onPrefChange();
});

add_task(async function test_privatebrowsing_asrouter_messages_state() {
  await resetState();

  let pinPromoMessage = ASRouter.state.messages.find(
    m => m.id === "PB_NEWTAB_PIN_PROMO"
  );
  Assert.ok(pinPromoMessage, "Pin Promo message found");

  const initialMessages = JSON.parse(JSON.stringify(ASRouter.state.messages));

  let { win } = await openTabAndWaitForRender();

  Assert.equal(
    ASRouter.state.messages.filter(m => m.id === "PB_NEWTAB_PIN_PROMO").length,
    0,
    "Pin Promo message removed from state when Promotype Pin is disabled"
  );

  for (let msg of initialMessages) {
    let shouldPersist =
      msg.template !== "pb_newtab" ||
      Services.prefs.getBoolPref(
        PromoInfo[msg.content?.promoType]?.enabledPref,
        true
      );
    Assert.equal(
      !!ASRouter.state.messages.find(m => m.id === msg.id),
      shouldPersist,
      shouldPersist
        ? "Message persists in ASRouter state"
        : "Promo message with disabled promoType removed from ASRouter state"
    );
  }
  await BrowserTestUtils.closeWindow(win);
});

// Verify that promos are correctly removed if blocked in another tab.
// See handlePromoOnPreload() in aboutPrivateBrowsing.js
add_task(async function test_remove_promo_from_prerendered_tab_if_blocked() {
  await resetState();
  const doExperimentCleanup = await enrollPromoMessage();

  const { win, tab: tab1 } = await openTabAndWaitForRender();

  await SpecialPowers.spawn(tab1, [PROMO_SELECTORS], async function (promo) {
    // container which is present if promo message is not blocked
    const promoContainer = content.document.querySelector(promo.container);
    ok(promoContainer, "Promo is shown in tab 1");
  });

  // Open a new background tab (tab 2) while the promo message is unblocked
  win.openTrustedLinkIn(win.BROWSER_NEW_TAB_URL, "tabshifted");

  // Block the promo in tab 1
  await clickPromoDismissButton(tab1);
  await SpecialPowers.spawn(tab1, [PROMO_SELECTORS], async function (promo) {
    await ContentTaskUtils.waitForCondition(() => {
      return !content.document.querySelector(promo.container);
    }, "The promo container is removed.");
  });

  // Switch to tab 2, invoking the `visibilitychange` handler in
  // handlePromoOnPreload()
  await BrowserTestUtils.switchTab(win.gBrowser, win.gBrowser.tabs[1]);

  // Verify that the promo has now been removed from tab 2
  await SpecialPowers.spawn(
    win.gBrowser.tabs[1].linkedBrowser,
    [PROMO_SELECTORS],
    // The timing may be weird in Chaos Mode, so wait for it to be removed
    // instead of a single assertion.
    async function (promo) {
      await ContentTaskUtils.waitForCondition(
        () => !content.document.querySelector(promo.container),
        "Promo is not shown in a new tab after being dismissed in another tab"
      );
    }
  );

  await BrowserTestUtils.closeWindow(win);
  await doExperimentCleanup();
});

// Test that some default content is rendered while waiting for ASRouter to
// return a message.
add_task(async function test_default_content_deferred_message_load() {
  await resetState();
  const doExperimentCleanup = await enrollPromoMessage();

  let messageRequestedPromiseResolver;
  const messageRequestedPromise = new Promise(resolve => {
    messageRequestedPromiseResolver = resolve;
  });
  let messageReadyPromiseResolver;
  const messageReadyPromise = new Promise(resolve => {
    messageReadyPromiseResolver = resolve;
  });
  // Force ASRouter to "hang" until we resolve the promise so we can test what
  // happens when there is a delay in loading the message.
  const sendMessageStub = sandbox
    .stub(ASRouter, "sendPBNewTabMessage")
    .callsFake(async (...args) => {
      messageRequestedPromiseResolver();
      await messageReadyPromise;
      return sendMessageStub.wrappedMethod.apply(ASRouter, args);
    });

  const { win, tab } = await openAboutPrivateBrowsing();
  await messageRequestedPromise;

  await SpecialPowers.spawn(
    tab,
    [PROMO_SELECTORS, INFO_L10N],
    async function (promo, info) {
      const promoContainer = content.document.querySelector(promo.container);
      // The promo is built while hidden and only revealed once populated, so it
      // should be present but not yet visible.
      ok(
        promoContainer && ContentTaskUtils.isHidden(promoContainer),
        "Promo is hidden but not removed"
      );
      const infoContainer = content.document.querySelector(".info");
      ok(infoContainer && !infoContainer.hidden, "Info container is shown");
      const infoTitle = content.document.getElementById("info-title");
      ok(infoTitle.hidden, "Info title is hidden");
      const infoBody = content.document.getElementById("info-body");
      ok(infoBody, "Info body is shown");
      is(
        infoBody.getAttribute("data-l10n-id"),
        info.body,
        "Info body has the correct Fluent id"
      );
      await ContentTaskUtils.waitForCondition(
        () => infoBody.textContent,
        "Info body has been translated"
      );
      const infoLink = content.document.getElementById(
        "private-browsing-myths"
      );
      ok(infoLink, "Info link is shown");
      is(
        infoLink.getAttribute("data-l10n-id"),
        info.link,
        "Info link has the correct Fluent id"
      );
      await ContentTaskUtils.waitForCondition(
        () => infoLink.textContent && infoLink.href,
        "Info link has been translated"
      );
    }
  );

  messageReadyPromiseResolver();
  await messageReadyPromise;

  await SpecialPowers.spawn(tab, [PROMO_SELECTORS], async function (promo) {
    await ContentTaskUtils.waitForCondition(() => {
      const promoContainer = content.document.querySelector(promo.container);
      return promoContainer && ContentTaskUtils.isVisible(promoContainer);
    }, "The promo container is shown.");
  });

  await BrowserTestUtils.closeWindow(win);
  await doExperimentCleanup();
});
