/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global PanelUI */

add_setup(async function () {
  // menu_referralsPage is hidden by default, but pin it so that a future
  // default flip does not silently change the expected layout below.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.referrals.enabled", false]],
  });
});

async function openHelpView() {
  await gCUITestUtils.openMainMenu();
  PanelUI.showHelpView(document.getElementById("PanelUI-menu-button"));
  let helpView = document.getElementById("PanelUI-helpView");
  // ViewShown rather than ViewShowing: the tasks below measure geometry, and
  // the view is not laid out yet at ViewShowing.
  await BrowserTestUtils.waitForEvent(helpView, "ViewShown");
  return helpView;
}

function getLayout(helpView) {
  let items = helpView.querySelector(".panel-subview-body");
  return [...items.children].map(node => node.id || node.localName);
}

function expectedLayout(lastItem) {
  let expected = [
    "appMenu_menu_openHelp",
    "appMenu_help_reportBrokenSite",
    "appMenu_menu_HelpPopup_reportPhishingtoolmenu",
    "toolbarseparator",
    "appMenu_helpSafeMode",
    "appMenu_troubleShooting",
    "toolbarseparator",
    "appMenu_feedbackPage",
    lastItem,
  ];
  // macOS relocates About into the native application menu, which leaves the
  // last group empty and so drops its separator too.
  if (!document.getElementById("aboutName").hidden) {
    expected.push("toolbarseparator", "appMenu_aboutName");
  }
  return expected;
}

add_task(async function testHelpViewOrderWithNova() {
  let helpView = await openHelpView();

  Assert.deepEqual(
    getLayout(helpView),
    expectedLayout("appMenu-nova-switch-device-promo"),
    "The Help and Report subview is grouped and ordered as designed"
  );

  await gCUITestUtils.hideMainMenu();
});

add_task(async function testSeparatorsAreVisible() {
  let helpView = await openHelpView();

  let separators = helpView.querySelectorAll(
    ".panel-subview-body > toolbarseparator"
  );
  let expectedCount = expectedLayout("appMenu-nova-switch-device-promo").filter(
    id => id == "toolbarseparator"
  ).length;
  Assert.equal(
    separators.length,
    expectedCount,
    `${expectedCount} group separators were inserted`
  );
  for (let separator of separators) {
    Assert.ok(!separator.hidden, "The separator is not hidden");
    Assert.greater(
      separator.getBoundingClientRect().height,
      0,
      "The separator takes up vertical space"
    );
  }

  await gCUITestUtils.hideMainMenu();
});

add_task(async function testReportBrokenSiteHasCaret() {
  let helpView = await openHelpView();

  let button = helpView.querySelector("#appMenu_help_reportBrokenSite");
  let content = getComputedStyle(button, "::after").content;
  let expected = Services.locale.isAppLocaleRTL
    ? "arrow-left.svg"
    : "arrow-right.svg";
  Assert.ok(
    content.includes(expected),
    `Report Broken Site renders a subview caret, got ${content}`
  );

  await gCUITestUtils.hideMainMenu();
});

add_task(async function testNoSeparatorBeforePromo() {
  let helpView = await openHelpView();

  let promo = helpView.querySelector("#appMenu-nova-switch-device-promo");
  Assert.ok(promo, "The Nova switch device promo is present");
  Assert.equal(
    promo.previousElementSibling.id,
    "appMenu_feedbackPage",
    "The promo sits directly below Share ideas and feedback"
  );
  Assert.notEqual(
    promo.previousElementSibling.localName,
    "toolbarseparator",
    "There is no divider between the feedback item and the promo"
  );

  await gCUITestUtils.hideMainMenu();
});

add_task(async function testUngroupedItemsAreAppendedLast() {
  // Nothing lands in the fallback bucket by default, so add an item that no
  // group lists to pin down where a future Help menu addition shows up.
  let extra = document.createXULElement("menuitem");
  extra.id = "helpViewFallbackTestItem";
  extra.setAttribute("label", "Fallback test item");
  document.getElementById("menu_HelpPopup").appendChild(extra);
  registerCleanupFunction(() => extra.remove());

  let helpView = await openHelpView();

  Assert.equal(
    getLayout(helpView).at(-1),
    "appMenu_helpViewFallbackTestItem",
    "An item that no group lists is appended after the last group"
  );

  await gCUITestUtils.hideMainMenu();
  extra.remove();
});
