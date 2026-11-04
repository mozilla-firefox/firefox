/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const PROMO_HEADER = "Nova promo header";
const PROMO_TITLE = "Nova promo body text";
const PROMO_LINK_TEXT = "Nova learn more";
const PROMO_IMAGE = "chrome://browser/content/assets/private-promo-asset.svg";

function novaPromoMessage(contentOverrides = {}) {
  return {
    id: `PB_NEWTAB_NOVA_PROMO_${Math.random()}`,
    template: "pb_newtab",
    content: {
      hideDefault: true,
      promoEnabled: true,
      promoSectionStyle: "bottom",
      promoHeader: PROMO_HEADER,
      promoTitleEnabled: true,
      promoTitle: PROMO_TITLE,
      promoLinkText: PROMO_LINK_TEXT,
      promoLinkType: "link",
      promoImageLarge: PROMO_IMAGE,
      promoButton: {
        action: {
          data: { args: "https://example.com/", where: "tabshifted" },
          type: "OPEN_URL",
        },
      },
      ...contentOverrides,
    },
    // Priority ensures this message is picked over the ones in
    // OnboardingMessageProvider.
    priority: 5,
    targeting: "true",
  };
}

add_setup(async function () {
  ASRouter.resetMessageState();
});

add_task(async function test_nova_promo_renders() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.nova.enabled", true]],
  });
  let doExperimentCleanup =
    await setupMSExperimentWithMessage(novaPromoMessage());

  let { win, tab } = await openTabAndWaitForRender();

  await SpecialPowers.spawn(
    tab,
    [{ PROMO_HEADER, PROMO_TITLE, PROMO_LINK_TEXT, PROMO_IMAGE }],
    async function (expected) {
      const promoEl = content.document.getElementById("nova-promo");
      ok(promoEl, "Nova moz-promo element is rendered");
      is(
        content.document.querySelector(".promo"),
        null,
        "Legacy promo markup is not used when Nova is enabled"
      );

      // The moz-promo properties are set on the underlying JS object, which is
      // not visible through the content task's Xray wrapper. Assert on the
      // rendered shadow DOM instead, which is what the user sees.
      const shadow = promoEl.shadowRoot;
      await ContentTaskUtils.waitForCondition(
        () => shadow.querySelector(".heading")?.textContent,
        "Promo heading rendered"
      );
      is(
        shadow.querySelector(".heading").textContent,
        expected.PROMO_HEADER,
        "Promo heading is rendered"
      );
      ok(
        shadow
          .querySelector(".message")
          .textContent.includes(expected.PROMO_TITLE),
        "Promo message is rendered"
      );
      is(
        shadow.querySelector(".image-container img").getAttribute("src"),
        expected.PROMO_IMAGE,
        "Promo image is rendered"
      );
      is(
        content.document.getElementById("nova-promo-link").textContent,
        expected.PROMO_LINK_TEXT,
        "Promo link text is set"
      );
      is(
        content.document.getElementById("nova-promo-button"),
        null,
        "The button call to action is removed for a link promo"
      );
      const closeButton = shadow.querySelector("moz-button.close");
      ok(closeButton, "The promo has a close button");
      const closeButtonEl = closeButton.shadowRoot.querySelector("button");
      await ContentTaskUtils.waitForCondition(
        () => closeButtonEl.getAttribute("aria-label"),
        "Close button is localized"
      );
      is(
        closeButtonEl.getAttribute("aria-label"),
        "Close",
        "Close button has an accessible name"
      );

      // Regardless of the requested "bottom" section style, the Nova promo is
      // always shown below the search box.
      const wrapper = content.document.querySelector(".nova-promo-wrapper");
      ok(!wrapper.hidden, "Nova promo wrapper is visible");
      is(
        wrapper.previousElementSibling,
        content.document.querySelector(".search-inner-wrapper"),
        "Nova promo is inserted directly below the search box"
      );
    }
  );

  await BrowserTestUtils.closeWindow(win);
  await doExperimentCleanup();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_nova_promo_link_dispatches_action() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.nova.enabled", true]],
  });
  let doExperimentCleanup =
    await setupMSExperimentWithMessage(novaPromoMessage());

  let { win, tab } = await openTabAndWaitForRender();

  let newTabOpened = BrowserTestUtils.waitForNewTab(
    win.gBrowser,
    "https://example.com/"
  );

  await SpecialPowers.spawn(tab, [], async function () {
    content.document.getElementById("nova-promo-link").click();
  });

  let newTab = await newTabOpened;
  ok(newTab, "Clicking the Nova promo link dispatched the OPEN_URL action");

  BrowserTestUtils.removeTab(newTab);
  await BrowserTestUtils.closeWindow(win);
  await doExperimentCleanup();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_nova_promo_button_renders_and_dispatches_action() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.nova.enabled", true]],
  });
  let doExperimentCleanup = await setupMSExperimentWithMessage(
    novaPromoMessage({ promoLinkType: "button" })
  );

  let { win, tab } = await openTabAndWaitForRender();

  await SpecialPowers.spawn(tab, [PROMO_LINK_TEXT], async function (linkText) {
    const buttonEl = content.document.getElementById("nova-promo-button");
    ok(buttonEl, "Button call to action is rendered for a button promo");
    is(
      content.document.getElementById("nova-promo-link"),
      null,
      "The link call to action is removed for a button promo"
    );
    // moz-button reflects its label to an attribute, which is visible through
    // the content task's Xray wrapper unlike the underlying JS property.
    await ContentTaskUtils.waitForCondition(
      () => buttonEl.getAttribute("label") === linkText,
      "Promo button label is set"
    );
  });

  let newTabOpened = BrowserTestUtils.waitForNewTab(
    win.gBrowser,
    "https://example.com/"
  );

  await SpecialPowers.spawn(tab, [], async function () {
    content.document.getElementById("nova-promo-button").click();
  });

  let newTab = await newTabOpened;
  ok(newTab, "Clicking the Nova promo button dispatched the OPEN_URL action");

  BrowserTestUtils.removeTab(newTab);
  await BrowserTestUtils.closeWindow(win);
  await doExperimentCleanup();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_nova_promo_dismiss() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.nova.enabled", true]],
  });
  let doExperimentCleanup =
    await setupMSExperimentWithMessage(novaPromoMessage());

  let { win: win1, tab: tab1 } = await openTabAndWaitForRender();

  await SpecialPowers.spawn(tab1, [], async function () {
    ok(
      content.document.getElementById("nova-promo"),
      "Nova promo is shown before dismissing"
    );
  });
  await clickPromoDismissButton(tab1);

  let { win: win2, tab: tab2 } = await openTabAndWaitForRender();

  await SpecialPowers.spawn(tab2, [], async function () {
    is(
      content.document.getElementById("nova-promo"),
      null,
      "Nova promo is no longer shown after dismissing"
    );
  });

  await BrowserTestUtils.closeWindow(win1);
  await BrowserTestUtils.closeWindow(win2);
  await doExperimentCleanup();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_nova_promo_respects_impression_cap() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.nova.enabled", true]],
  });
  const message = novaPromoMessage();
  message.frequency = { lifetime: 2 };
  let doExperimentCleanup = await setupMSExperimentWithMessage(message);

  for (let i = 1; i <= 2; i++) {
    let { win, tab } = await openTabAndWaitForRender();
    await SpecialPowers.spawn(tab, [i], async function (impression) {
      ok(
        content.document.getElementById("nova-promo"),
        `Nova promo is shown on impression ${impression}`
      );
    });
    await BrowserTestUtils.closeWindow(win);
  }

  let { win, tab } = await openTabAndWaitForRender();
  await SpecialPowers.spawn(tab, [], async function () {
    is(
      content.document.getElementById("nova-promo"),
      null,
      "Nova promo is no longer shown once the impression cap is reached"
    );
  });

  await BrowserTestUtils.closeWindow(win);
  await doExperimentCleanup();
  await SpecialPowers.popPrefEnv();
});

add_task(
  async function test_nova_promo_removed_from_prerendered_tab_if_blocked() {
    // The PB newtab is pre-rendered. If the promo is dismissed in one tab, the
    // handlePromoOnPreload() visibilitychange handler removes it from the
    // already-rendered tab when it becomes visible. See aboutPrivateBrowsing.js.
    await SpecialPowers.pushPrefEnv({
      set: [["browser.nova.enabled", true]],
    });
    let doExperimentCleanup =
      await setupMSExperimentWithMessage(novaPromoMessage());

    const { win, tab: tab1 } = await openTabAndWaitForRender();

    await SpecialPowers.spawn(tab1, [], async function () {
      ok(
        content.document.getElementById("nova-promo"),
        "Nova promo is shown in tab 1"
      );
    });

    // Open a background tab while the promo is still unblocked, so it pre-renders
    // with the promo present.
    win.openTrustedLinkIn(win.BROWSER_NEW_TAB_URL, "tabshifted");

    // Dismiss (block) the promo in tab 1.
    await clickPromoDismissButton(tab1);
    await SpecialPowers.spawn(tab1, [], async function () {
      await ContentTaskUtils.waitForCondition(
        () => !content.document.querySelector(".nova-promo-wrapper"),
        "The promo is removed from tab 1 after dismissing"
      );
    });

    // Switch to the background tab, triggering its visibilitychange handler.
    await BrowserTestUtils.switchTab(win.gBrowser, win.gBrowser.tabs[1]);

    await SpecialPowers.spawn(
      win.gBrowser.tabs[1].linkedBrowser,
      [],
      async () => {
        await ContentTaskUtils.waitForCondition(
          () => !content.document.querySelector(".nova-promo-wrapper"),
          "Nova promo is removed from a new tab after being dismissed in another tab"
        );
      }
    );

    await BrowserTestUtils.closeWindow(win);
    await doExperimentCleanup();
    await SpecialPowers.popPrefEnv();
  }
);

add_task(async function test_nova_promo_is_hidden_until_it_has_content() {
  // The user should never see an empty promo. The page builds the promo while
  // it is still marked `hidden`, then reveals it once it has content. This test
  // guards that the `hidden` promo really is invisible to the user: previously
  // the styling ignored the `hidden` attribute, so an empty promo was shown
  // before its heading, message and link were filled in.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.nova.enabled", true]],
  });
  let doExperimentCleanup =
    await setupMSExperimentWithMessage(novaPromoMessage());

  let { win, tab } = await openTabAndWaitForRender();

  await SpecialPowers.spawn(tab, [], async function () {
    const promo = content.document.querySelector(".nova-promo-wrapper");

    ok(
      ContentTaskUtils.isVisible(promo),
      "The populated promo is shown to the user"
    );

    promo.hidden = true;
    ok(
      ContentTaskUtils.isHidden(promo),
      "A promo marked hidden is not shown to the user"
    );
  });

  await BrowserTestUtils.closeWindow(win);
  await doExperimentCleanup();
  await SpecialPowers.popPrefEnv();
});
