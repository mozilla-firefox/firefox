// test_newtab calls SpecialPowers.spawn, which injects ContentTaskUtils in the
// scope of the callback. Eslint doesn't know about that.
/* global ContentTaskUtils */

// If this fails it could be because of schema changes.
// `topstories.json` defines the stories shown
test_newtab({
  async before({ pushPrefs }) {
    sinon
      .stub(DiscoveryStreamFeed.prototype, "generateFeedUrl")
      .returns(
        "https://example.com/browser/browser/extensions/newtab/test/browser/topstories.json"
      );
    await pushPrefs(
      [
        "browser.newtabpage.activity-stream.discoverystream.config",
        JSON.stringify({
          collapsible: true,
          enabled: true,
          personalized: true,
        }),
      ],
      [
        "browser.newtabpage.activity-stream.discoverystream.endpoints",
        "https://example.com",
      ]
    );
  },
  test: async function test_card_render() {
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelectorAll(
          "[data-section-id='topstories'] .ds-card-link"
        ).length
    );
    let found = content.document.querySelectorAll(
      "[data-section-id='topstories'] .ds-card-link"
    ).length;
    is(found, 1, "there should be 1 topstory card");
    let cardPublisher = content.document.querySelector(
      "[data-section-id='topstories'] .source"
    ).innerText;
    is(
      cardPublisher,
      "bbc",
      `Card publisher is ${cardPublisher} instead of bbc`
    );
  },
  async after() {
    sinon.restore();
  },
});

// Regression (Bug 2051742): the story-card "..." menu items must be clickable.
// The card's hover-background overlay sets pointer-events: none and the menu
// (moz-button + panel-list) must re-enable it, or menu items receive no hover
// and a click falls THROUGH the item to the card behind (just dismissing the
// menu). A programmatic .click() bypasses hit-testing and would not catch this,
// so this checks that a click at the item's location actually lands on the item.
test_newtab({
  async before({ pushPrefs }) {
    sinon
      .stub(DiscoveryStreamFeed.prototype, "generateFeedUrl")
      .returns(
        "https://example.com/browser/browser/extensions/newtab/test/browser/topstories.json"
      );
    await pushPrefs(
      [
        "browser.newtabpage.activity-stream.discoverystream.config",
        JSON.stringify({
          collapsible: true,
          enabled: true,
          personalized: true,
        }),
      ],
      [
        "browser.newtabpage.activity-stream.discoverystream.endpoints",
        "https://example.com",
      ],
      ["browser.newtabpage.activity-stream.widgets.enabled", false]
    );
  },
  test: async function test_card_menu_item_is_clickable() {
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".ds-card moz-button"),
      "Wait for the story card menu button"
    );
    const card = content.document.querySelector(".ds-card");
    // The trigger is revealed on hover; add .active so it and the menu lay out.
    card.classList.add("active");

    // panel-list's events are untrusted, so the listener has to opt into them.
    const shown = ContentTaskUtils.waitForEvent(
      card.querySelector("panel-list"),
      "shown",
      false,
      null,
      true
    );
    card.querySelector("moz-button").click();
    await shown;

    const item = card.querySelector("panel-list panel-item");
    Assert.notStrictEqual(
      content.getComputedStyle(item).pointerEvents,
      "none",
      "story-card menu item accepts pointer events"
    );

    const r = item.getBoundingClientRect();
    const hit = content.document.elementFromPoint(
      Math.round(r.left + r.width / 2),
      Math.round(r.top + r.height / 2)
    );
    ok(
      hit &&
        (hit === item ||
          item.contains(hit) ||
          hit.closest("panel-item") === item),
      "a click at the menu item lands on the item, not the card behind it"
    );
  },
  async after() {
    sinon.restore();
  },
});
