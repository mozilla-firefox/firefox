/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.history.showAllHistory", true],
      ["sidebar.history.sortOption", "date"],
    ],
  });
});

registerCleanupFunction(async () => {
  await PlacesUtils.history.clear();
});

add_task(async function test_unload_collapsed_date_container() {
  await PlacesUtils.history.clear();
  await PlacesUtils.history.insert({
    url: "https://example.com/",
    visits: [{ date: new Date() }],
  });

  const { component, contentWindow } = await showHistorySidebar();
  // TODO: Parameterize this test when more sort types are implemented
  const menuShown = BrowserTestUtils.waitForEvent(
    component._menu,
    "popupshown"
  );
  EventUtils.synthesizeMouseAtCenter(component.menuButton, {}, contentWindow);
  await menuShown;
  component._menu.activateItem(component._menuSortByDate);

  // Start with an expanded date card.
  const card = component.cards[0];
  const list = card.querySelector("sidebar-tab-list");
  await BrowserTestUtils.waitForMutationCondition(
    list.shadowRoot,
    { childList: true, subtree: true },
    () => list.rowEls.length === 1,
    { msg: "The expanded date card has loaded and rendered its visit" }
  );
  Assert.equal(list.tabItems.length, 1, "Tab list holds one visit");

  // Collapse the date card.
  const collapsed = BrowserTestUtils.waitForEvent(card, "toggle");
  EventUtils.synthesizeMouseAtCenter(card.summaryEl, {}, contentWindow);
  await collapsed;
  await list.updateComplete;
  Assert.ok(!card.expanded, "The card is collapsed");

  // Ensure that the visit was freed from memory.
  Assert.deepEqual(list.tabItems, [], "Tab list no longer holds the visit");

  SidebarTestUtils.closePanel(window);
});
