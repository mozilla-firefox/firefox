/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Each reserved focus action id is queued in the parent until a newer focus
// action supersedes it, so window.focus() calls that do nothing must not
// reserve one each (bug 1961478).
add_task(async function test_noop_window_focus_reserves_no_action_ids() {
  const N = 1000;
  await BrowserTestUtils.withNewTab(
    "data:text/html,<script>function focusMany(n) { for (let i = 0; i < n; i++) { window.focus(); } }</script>",
    async browser => {
      await SpecialPowers.spawn(browser, [N], n => {
        const before = Services.focus.reservedFocusActionIdCount;
        // Call from a page function so that window.focus() sees a
        // non-system caller, as it would on the web.
        content.wrappedJSObject.focusMany(n);
        Assert.equal(
          Services.focus.reservedFocusActionIdCount,
          before,
          "No focus action ids were reserved"
        );
      });
    }
  );
});
