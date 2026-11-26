/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { Spotlight } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/Spotlight.sys.mjs"
);
const { PanelTestProvider } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/PanelTestProvider.sys.mjs"
);

const PILLS = [
  { id: "one", label: "One", icon: "chrome://browser/skin/tabs.svg" },
  { id: "two", label: "Two", icon: "chrome://global/skin/icons/edit.svg" },
  { id: "three", label: "Three", icon: "chrome://global/skin/icons/more.svg" },
];

function carouselScreen() {
  return {
    id: "CAROUSEL_NAV_TEST",
    content: {
      position: "center",
      // The pills only carry icons in the wide layout
      screen_style: { width: "800px" },
      title: { raw: "Carousel" },
      tiles: {
        type: "single-select",
        layout: "carousel",
        selected: PILLS[0].id,
        data: PILLS.map(({ id, label, icon }) => ({
          id,
          inert: true,
          type: "carousel-card",
          pill: { label: { raw: label }, icon },
          label: { raw: `${label} card` },
          tilebutton: {
            label: { raw: "Try it now" },
            style: "secondary",
            action: { dismiss: true },
          },
        })),
      },
      primary_button: { label: { raw: "Done" }, action: { dismiss: true } },
    },
  };
}

async function dialogClosed(browser) {
  await TestUtils.waitForCondition(
    () => !browser.documentGlobal.gDialogBox?.isOpen,
    "Waiting for dialog to close"
  );
}

async function showCarousel() {
  let message = (await PanelTestProvider.getMessages()).find(
    m => m.id === "MULTISTAGE_SPOTLIGHT_MESSAGE"
  );
  message.content.screens = [carouselScreen()];

  const browser = gBrowser.selectedBrowser;
  await dialogClosed(browser);
  Spotlight.showSpotlightDialog(browser, message, () => {});
  const [win] = await TestUtils.topicObserved("subdialog-loaded");

  await TestUtils.waitForCondition(
    () => win.document.querySelector("moz-segmented-control"),
    "Waiting for the pill row to render"
  );
  const control = win.document.querySelector("moz-segmented-control");
  const pills = [...control.querySelectorAll("moz-segmented-control-item")];
  await Promise.all([control, ...pills].map(el => el.updateComplete));

  return { win, control, pills };
}

function radioFor(win, id) {
  return win.document.querySelector(`input[name="select-item-${id}"]`);
}

async function keyboardNavigate(control, direction, win) {
  EventUtils.synthesizeKey(`KEY_Arrow${direction}`, {}, win);
  await control.updateComplete;
}

add_task(async function test_pills_render() {
  const { win, control, pills } = await showCarousel();

  Assert.ok(
    win.customElements.get("moz-segmented-control"),
    "moz-segmented-control is defined in the Spotlight document"
  );

  Assert.deepEqual(
    pills.map(pill => pill.value),
    PILLS.map(pill => pill.id),
    "Renders one pill per card, in card order"
  );
  PILLS.forEach(({ label, icon }, index) => {
    Assert.equal(pills[index].label, label, `Pill ${index} has its label`);
    Assert.equal(pills[index].iconSrc, icon, `Pill ${index} has its icon`);
  });

  Assert.equal(
    control.value,
    PILLS[0].id,
    "The pill row starts on the selected card"
  );
  Assert.ok(pills[0].checked, "The first pill is checked");
  Assert.equal(
    pills[0].buttonEl.getAttribute("aria-checked"),
    "true",
    "The checked pill has the correct aria-checked value"
  );

  await TestUtils.waitForCondition(
    () => control.ariaLabel,
    "Waiting for Fluent to name the pill group"
  );
  Assert.ok(control.ariaLabel, "The pill group has an accessible name");

  await win.close();
});

add_task(async function test_pill_selects_card() {
  const { win, control, pills } = await showCarousel();

  pills[2].click();
  await control.updateComplete;

  Assert.equal(control.value, PILLS[2].id, "Clicking a pill moves the group");
  Assert.ok(pills[2].checked, "The clicked pill is checked");
  await TestUtils.waitForCondition(
    () => radioFor(win, PILLS[2].id)?.checked,
    "Waiting for the pill's card to become selected"
  );
  Assert.ok(
    !radioFor(win, PILLS[0].id).checked,
    "The previously selected card is no longer selected"
  );

  await win.close();
});

add_task(async function test_only_the_selected_card_offers_its_button() {
  const { win, control, pills } = await showCarousel();
  const buttonFor = id =>
    win.document.getElementById(`tile-button-select-item-${id}`);

  Assert.ok(
    !buttonFor(PILLS[0].id).hasAttribute("inert"),
    "The selected card's button is reachable"
  );
  PILLS.slice(1).forEach(({ id }) =>
    Assert.ok(
      buttonFor(id).hasAttribute("inert"),
      `The peek card ${id} keeps its button out of the tab order`
    )
  );

  pills[2].click();
  await control.updateComplete;
  await TestUtils.waitForCondition(
    () => !buttonFor(PILLS[2].id).hasAttribute("inert"),
    "Waiting for the newly selected card to offer its button"
  );
  Assert.ok(
    buttonFor(PILLS[0].id).hasAttribute("inert"),
    "The card that lost the selection withdraws its button"
  );

  await win.close();
});

add_task(async function test_pills_rtl() {
  await SpecialPowers.pushPrefEnv({ set: [["intl.l10n.pseudo", "bidi"]] });
  const { win, control, pills } = await showCarousel();

  Assert.equal(
    win.document.documentElement.getAttribute("dir"),
    "rtl",
    "The Spotlight document is RTL"
  );
  Assert.greater(
    pills[0].getBoundingClientRect().left,
    pills[2].getBoundingClientRect().left,
    "The first pill renders to the inline-start, on the right"
  );

  pills[0].click();
  await control.updateComplete;
  Assert.equal(
    win.document.activeElement,
    pills[0],
    "Clicking a pill focuses it"
  );

  await keyboardNavigate(control, "Left", win);
  Assert.equal(control.value, PILLS[1].id, "ArrowLeft advances in RTL");

  await keyboardNavigate(control, "Right", win);
  Assert.equal(control.value, PILLS[0].id, "ArrowRight goes back in RTL");

  await win.close();
  await SpecialPowers.popPrefEnv();
});
