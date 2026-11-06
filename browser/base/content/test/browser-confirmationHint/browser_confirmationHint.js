/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

add_task(async function test_hint_hides_on_its_own() {
  let panel = ConfirmationHint._panel;
  let shown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  ConfirmationHint.show(
    document.getElementById("PanelUI-menu-button"),
    "confirmation-hint-page-bookmarked"
  );
  await shown;

  await BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  ok(true, "The hint hid itself.");
});

// A second show() while a hint is still visible has to restart the auto-hide
// timer itself: openPopup does nothing for an already open panel, so there
// will be no popupshown event to restart the timer show() just cleared.
add_task(async function test_second_show_while_showing_still_hides() {
  let panel = ConfirmationHint._panel;
  let anchor = document.getElementById("PanelUI-menu-button");

  let shown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  ConfirmationHint.show(anchor, "confirmation-hint-page-bookmarked");
  await shown;

  ConfirmationHint.show(anchor, "confirmation-hint-send-to-device");
  is(panel.state, "open", "The hint is still showing after the second show().");
  is(
    panel.getAttribute("data-message-id"),
    "confirmation-hint-send-to-device",
    "The second hint's message replaced the first one's."
  );
  // The message text is only opaque while this attribute is set.
  ok(
    ConfirmationHint._animationBox.hasAttribute("animate"),
    "The second hint's message is visible."
  );

  await TestUtils.waitForCondition(
    () => panel.state == "closed",
    "the hint hides on its own after a second show() while it was showing",
    100,
    100
  );
});

// A second show() while a hint is still opening must leave only one auto-hide
// timer, or the extra one hides a later hint early.
add_task(async function test_second_show_while_opening_starts_one_timer() {
  let panel = ConfirmationHint._panel;
  let anchor = document.getElementById("PanelUI-menu-button");
  let timerSpy = sinon.spy(ConfirmationHint, "_startAutoHideTimer");

  let shown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  ConfirmationHint.show(anchor, "confirmation-hint-page-bookmarked");
  is(panel.state, "showing", "The first hint is still opening.");
  ConfirmationHint.show(anchor, "confirmation-hint-send-to-device");
  await shown;
  timerSpy.restore();

  is(timerSpy.callCount, 1, "Only one auto-hide timer was started.");
  is(
    panel.getAttribute("data-message-id"),
    "confirmation-hint-send-to-device",
    "The later show() call's message is shown."
  );
  let hidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  panel.hidePopup();
  await hidden;
});

// openPopup does nothing for a hiding panel, so a show() while a hint is
// hiding has to wait for it to finish hiding and then reopen it.
add_task(async function test_show_while_hiding_reopens() {
  // The panel only has a hiding state while its closing animation runs. Some
  // platforms (e.g. some Linux setups) disable panel animations, and Windows
  // skips the animation when reduced motion is preferred.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["ui.panelAnimations", 1],
      ["ui.prefersReducedMotion", 0],
    ],
  });
  let panel = ConfirmationHint._panel;
  let anchor = document.getElementById("PanelUI-menu-button");

  let shown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  ConfirmationHint.show(anchor, "confirmation-hint-page-bookmarked");
  await shown;

  panel.hidePopup(true);
  is(panel.state, "hiding", "The first hint is hiding.");
  shown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  ConfirmationHint.show(anchor, "confirmation-hint-send-to-device");
  await shown;

  is(
    panel.getAttribute("data-message-id"),
    "confirmation-hint-send-to-device",
    "The hint reopened with the new message."
  );
  ok(
    ConfirmationHint._animationBox.hasAttribute("animate"),
    "The new hint's message is visible."
  );

  let hidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  panel.hidePopup();
  await hidden;
  await SpecialPowers.popPrefEnv();
});
