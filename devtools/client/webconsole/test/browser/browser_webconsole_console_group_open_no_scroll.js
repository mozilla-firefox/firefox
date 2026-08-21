/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Check that opening a group does not scroll the console output.

const TEST_URI = `data:text/html,<!DOCTYPE html><meta charset=utf8><script>
  Array.from({length: 100}, (_, i) => console.log("log-"+i));
  console.groupCollapsed("GROUP");
  console.log("in group");
</script>`;

add_task(async function () {
  const hud = await openNewTabAndConsole(TEST_URI);
  const outputScroller = hud.ui.outputScroller;

  // Let's wait until the first message and the group are displayed.
  await waitFor(() => findConsoleAPIMessage(hud, "log-0"));
  const groupMessage = await waitFor(() => findConsoleAPIMessage(hud, "GROUP"));

  is(hasVerticalOverflow(outputScroller), true, "output node overflows");
  is(
    isScrolledToBottom(outputScroller),
    true,
    "output node is scrolled to the bottom"
  );

  await waitForStableScrollPosition(outputScroller);

  info("Expand the group");
  groupMessage.querySelector(".arrow").click();
  await waitFor(() => findConsoleAPIMessage(hud, "in group"));

  await waitFor(
    () =>
      hasVerticalOverflow(outputScroller) &&
      !isScrolledToBottom(outputScroller),
    {
      toString: () => {
        return (
          `Wait for the output node to overflow (${hasVerticalOverflow(outputScroller)}) ` +
          `and no longer be scrolled to bottom (${isScrolledToBottom(outputScroller)})`
        );
      },
    },
    50 // interval
  );
  is(hasVerticalOverflow(outputScroller), true, "output node overflows");
  is(
    isScrolledToBottom(outputScroller),
    false,
    "output node isn't scrolled to the bottom anymore"
  );

  info("Scroll to bottom");
  outputScroller.scrollTop = outputScroller.scrollHeight;
  await new Promise(r =>
    window.requestAnimationFrame(() => TestUtils.executeSoon(r))
  );

  is(
    isScrolledToBottom(outputScroller),
    true,
    "output node is scrolled to the bottom"
  );

  info(
    "Check that adding a message on an open group when scrolled to bottom scrolls " +
      "to bottom"
  );
  const onNewMessage = waitForMessageByType(hud, "new-message", ".console-api");
  SpecialPowers.spawn(gBrowser.selectedBrowser, [], function () {
    content.console.group("GROUP-2");
    content.console.log("new-message");
  });
  await onNewMessage;
  is(
    isScrolledToBottom(outputScroller),
    true,
    "output node is scrolled to the bottom after adding message in group"
  );
});

/**
 * Wait until the scrollTop / scrollHeight / clientHeight of the provided
 * element becomes stable.
 *
 * @param {HTMLElement} el
 */
async function waitForStableScrollPosition(el) {
  let previous;
  await waitFor(
    () => {
      const snapshot = [el.scrollTop, el.scrollHeight, el.clientHeight].join(
        "-"
      );
      const stable = snapshot === previous;
      previous = snapshot;
      return stable;
    },
    "Wait for the output scroll geometry to stabilize",
    100
  );
}
