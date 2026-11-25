/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const DELAYED_RELOAD_PAGE = TEST_ROOT + "delayed-reload-page.sjs";

async function setHeld(held) {
  await fetch(`${DELAYED_RELOAD_PAGE}?${held ? "hold" : "release"}`);
}

async function assertPageReceivesEvents(browser) {
  await SpecialPowers.spawn(browser, [], () => {
    content.eventsReceived = [];
    for (let type of ["click", "keydown"]) {
      content.addEventListener(type, e => content.eventsReceived.push(e.type));
    }
  });

  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#testPageElement",
    {},
    browser
  );
  await BrowserTestUtils.synthesizeKey("a", {}, browser);

  let eventsReceived = await SpecialPowers.spawn(
    browser,
    [],
    () => content.eventsReceived
  );
  Assert.deepEqual(
    eventsReceived,
    ["click", "keydown"],
    "The reloaded page receives click and key events"
  );
}

add_task(async function test_overlay_shown_while_reloading() {
  await BrowserTestUtils.withNewTab(DELAYED_RELOAD_PAGE, async browser => {
    let helper = new ScreenshotsHelper(browser);

    await setHeld(true);
    registerCleanupFunction(() => setHeld(false));

    // beforeunload fires here, but the old document stays current until the
    // held response arrives.
    let loaded = BrowserTestUtils.browserLoaded(browser);
    await SpecialPowers.spawn(browser, [], () => {
      content.location.reload();
    });

    helper.triggerUIFromToolbar();
    await helper.waitForOverlay();

    await setHeld(false);
    await loaded;
    await helper.waitForOverlayClosed();

    await assertPageReceivesEvents(browser);
  });
});

add_task(async function test_page_stops_beforeunload_propagation() {
  await BrowserTestUtils.withNewTab(DELAYED_RELOAD_PAGE, async browser => {
    let helper = new ScreenshotsHelper(browser);

    await SpecialPowers.spawn(browser, [], () => {
      content.addEventListener("beforeunload", e =>
        e.stopImmediatePropagation()
      );
    });

    helper.triggerUIFromToolbar();
    await helper.waitForOverlay();

    let loaded = BrowserTestUtils.browserLoaded(browser);
    await SpecialPowers.spawn(browser, [], () => {
      content.location.reload();
    });
    await loaded;
    await helper.waitForOverlayClosed();

    await assertPageReceivesEvents(browser);
  });
});
