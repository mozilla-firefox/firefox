/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test handling of X1/X2 mouse button presses.

async function test_mouse_xbuttons(url) {
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  const browser = tab.linkedBrowser;
  const { x: browserX, y: browserY } = browser.getBoundingClientRect();
  const win = browser.documentGlobal;

  // Synthesize click in the center of target with button.
  async function click(button, target, command) {
    const [targetX, targetY] = await SpecialPowers.spawn(
      browser,
      [target],
      target => {
        const targetEl = content.document.getElementById(target);
        const { x, y, width, height } = targetEl.getBoundingClientRect();
        return [x + width / 2, y + height / 2];
      }
    );
    const x = browserX + targetX;
    const y = browserY + targetY;
    win.synthesizeMouseEvent("mousedown", x, y, { button }, { toWindow: true });
    win.synthesizeMouseEvent(
      "mouseup",
      x,
      y,
      { button, command },
      { toWindow: true }
    );
  }

  // Set up session history with fragments #1 -> #2
  for (let i of [1, 2]) {
    const navigate = BrowserTestUtils.waitForLocationChange(
      gBrowser,
      `${url}#${i}`
    );
    await click(0, "link" + i);
    await navigate;
  }
  async function reset() {
    await SpecialPowers.spawn(browser, [], async () => {
      // Go to fragment #2 in session history by navigating forward
      // if necessary.
      const current = content.location.hash;
      if (current === "#1") {
        await content.navigation.forward();
      }
      // Set selection for copy
      content
        .getSelection()
        .selectAllChildren(content.document.querySelector("a"));
    });
  }

  // Set up event listeners.
  await SpecialPowers.spawn(browser, [], () => {
    content.events = [];
    for (let type of ["mousedown", "mouseup", "auxclick"]) {
      content.addEventListener(type, e => {
        if (type === content.preventDefault) {
          e.preventDefault();
        }
        content.events.push({
          type: e.type,
          button: e.button,
          target: e.target.id,
        });
      });
    }
  });

  async function runTest(button, preventDefault, command, target) {
    await SpecialPowers.spawn(browser, [preventDefault], preventDefault => {
      content.preventDefault = preventDefault;
    });
    info(
      `Test with button = ${button}, preventDefault = ${preventDefault}, command = ${command}.`
    );

    await reset();

    let waitForDefaultAction;
    if (command === "Back") {
      waitForDefaultAction = BrowserTestUtils.waitForLocationChange(
        gBrowser,
        `${url}#1`
      );
    } else if (command === "copy") {
      waitForDefaultAction = BrowserTestUtils.waitForContentEvent(
        browser,
        "copy"
      );
    } else {
      ok(false, "Invalid command.");
    }
    let didDefaultAction = false;
    waitForDefaultAction.then(() => {
      didDefaultAction = true;
    });

    const waitForAuxClick = BrowserTestUtils.waitForContentEvent(
      browser,
      "auxclick"
    );
    await click(button, "link1", command);
    await waitForAuxClick;
    const events = await SpecialPowers.spawn(browser, [], () => {
      const events = content.events;
      content.events = [];
      return events;
    });
    for (let event of events) {
      is(
        event.button,
        button,
        `${event.type} event should have the right button`
      );
      is(
        event.target,
        "link1",
        `${event.type} event should have the right target`
      );
    }
    is(
      events.map(e => e.type).join(),
      "mousedown,mouseup,auxclick",
      "Should get the right order of events"
    );

    // If mouseup or auxclick is default prevented, we shouldn't do the
    // default action. However, if just mousedown is, we should.
    if (preventDefault === "mousedown") {
      info(`Wait for default action (${command})`);
      await waitForDefaultAction;
    } else {
      // Wait a bit of time to be sure that the default action wasn't run.
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 100));
      ok(
        !didDefaultAction,
        `If ${preventDefault} is default prevented, the default ${command} action should not happen.`
      );
    }
  }

  for (let button of [3, 4]) {
    for (let preventDefault of ["mouseup", "mousedown", "auxclick"]) {
      // Test both WidgetCommandEvent and WidgetContentCommandEvent
      for (let command of ["Back", "copy"]) {
        await runTest(button, preventDefault, command);
      }
    }
  }
  BrowserTestUtils.removeTab(tab);
}

const PATH = "dom/events/test/file_mouse_x_buttons.html";
add_task(async () =>
  test_mouse_xbuttons(`https://example.com/browser/${PATH}`)
);
// Test the case where the event stays in the parent process using a chrome document.
add_task(async () =>
  test_mouse_xbuttons(`chrome://mochitests/content/browser/${PATH}`)
);
