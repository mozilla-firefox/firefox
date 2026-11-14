/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

"use strict";

add_task(async function () {
  await pushPref("dom.events.textevent.enabled", true);

  const dbg = await initDebugger(
    "doc-event-breakpoints.html",
    "event-breakpoints.js"
  );
  await selectSource(dbg, "event-breakpoints.js");
  await waitForSelectedSource(dbg, "event-breakpoints.js");
  const eventBreakpointsSource = findSource(dbg, "event-breakpoints.js");

  // We want to set each breakpoint individually to test adding/removing breakpoints, see Bug 1748589.
  await toggleEventBreakpoint(dbg, "Mouse", "event.mouse.click");

  invokeInTab("clickHandler");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 12);

  const whyPaused = await waitFor(
    () => dbg.win.document.querySelector(".why-paused")?.innerText
  );
  is(
    whyPaused,
    `Paused on event breakpoint\nclickTargetClicked - event-breakpoints.js:12:2\nDOM 'click' event`,
    "whyPaused does state that the debugger is paused as a result of a click event breakpoint"
  );
  await resume(dbg);

  await toggleEventBreakpoint(dbg, "XHR", "event.xhr.load");
  invokeInTab("xhrHandler");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 24);
  await resume(dbg);

  await toggleEventBreakpoint(dbg, "Timer", "timer.timeout.set");
  await toggleEventBreakpoint(dbg, "Timer", "timer.timeout.fire");
  invokeInTab("timerHandler");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 31);
  await resume(dbg);

  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 32);
  await resume(dbg);

  await toggleEventBreakpoint(dbg, "Script", "script.source.firstStatement");
  invokeInTab("evalHandler");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, findSource(dbg, "eval-test.js").id, 2);
  await resume(dbg);
  await toggleEventBreakpoint(dbg, "Script", "script.source.firstStatement");

  await toggleEventBreakpoint(dbg, "Control", "event.control.focusin");
  await toggleEventBreakpoint(dbg, "Control", "event.control.focusout");
  // Not awaited here, nor for #invoker and #popover-toggle below: the invoked
  // action pauses the debuggee inside the SpecialPowers.spawn callback, so the
  // promise it returns only settles once the test resumes.
  invokeOnElement("#focus-text", "focus");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 47);
  await resume(dbg);

  // wait for focus-out event to fire
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 52);
  await resume(dbg);

  info("Deselect focus events");
  // We need to give the input focus to test composition, but we don't want the
  // focus breakpoints to fire.
  await toggleEventBreakpoint(dbg, "Control", "event.control.focusin");
  await toggleEventBreakpoint(dbg, "Control", "event.control.focusout");

  await toggleEventBreakpoint(dbg, "Control", "event.control.command");
  invokeOnElement("#invoker", "click");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 77);
  await resume(dbg);

  info("Enable closewatcher cancel and close events");
  await toggleEventBreakpoint(dbg, "CloseWatcher", "event.closewatcher.cancel");
  await toggleEventBreakpoint(dbg, "CloseWatcher", "event.closewatcher.close");
  invokeInTab("closeWatcherRequestClose");
  info("Wait for pause in cancel event listener");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 116);
  await resume(dbg);
  info("And wait for pause in close event listener after resuming");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 120);
  await resume(dbg);

  info("Enable beforetoggle and toggle events");
  await toggleEventBreakpoint(dbg, "Control", "event.control.beforetoggle");
  await toggleEventBreakpoint(dbg, "Control", "event.control.toggle");
  invokeOnElement("#popover-toggle", "click");
  info("Wait for pause in beforetoggle event listener");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 93);
  await resume(dbg);
  info("And wait for pause in toggle event listener after resuming");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 97);
  await resume(dbg);

  await toggleEventBreakpoint(
    dbg,
    "Keyboard",
    "event.keyboard.compositionstart"
  );
  await invokeOnElement("#focus-text", "focus");

  info("Type some characters during composition");
  invokeComposition();

  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 57);
  await resume(dbg);
  await flushPendingContentEvents();

  info("Deselect compositionstart and select compositionupdate");
  await toggleEventBreakpoint(
    dbg,
    "Keyboard",
    "event.keyboard.compositionstart"
  );
  await toggleEventBreakpoint(
    dbg,
    "Keyboard",
    "event.keyboard.compositionupdate"
  );

  await invokeOnElement("#focus-text", "focus");

  info("Type some characters during composition");
  invokeComposition();

  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 62);
  await resume(dbg);
  await flushPendingContentEvents();

  info("Deselect compositionupdate and select compositionend");
  await toggleEventBreakpoint(
    dbg,
    "Keyboard",
    "event.keyboard.compositionupdate"
  );
  await toggleEventBreakpoint(dbg, "Keyboard", "event.keyboard.compositionend");
  await invokeOnElement("#focus-text", "focus");

  info("Type some characters during composition");
  invokeComposition();

  info("Commit the composition");
  EventUtils.synthesizeComposition({
    type: "compositioncommitasis",
    key: { key: "KEY_Enter" },
  });

  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 67);
  await resume(dbg);

  info("Deselect compositionend");
  await toggleEventBreakpoint(dbg, "Keyboard", "event.keyboard.compositionend");
  await flushPendingContentEvents();

  info("Test textInput");
  await toggleEventBreakpoint(dbg, "Keyboard", "event.keyboard.textInput");
  await invokeOnElement("#focus-text", "focus");
  EventUtils.sendChar("N");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 102);
  await resume(dbg);
  await toggleEventBreakpoint(dbg, "Keyboard", "event.keyboard.textInput");

  info(`Check that breakpoint can be set on "scrollend"`);
  await toggleEventBreakpoint(dbg, "Control", "event.control.scrollend");

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.scrollTo({ top: 20, behavior: "smooth" });
  });

  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 72);
  await resume(dbg);

  info(`Check that breakpoint can be set on "pointerrawupdate"`);
  await toggleEventBreakpoint(dbg, "Pointer", "event.pointer.pointerrawupdate");

  SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    // EventUtils.synthesize throws when dispatching a pointerrawupdate for some reason,
    // let's build and dispatch the event directly
    content.document
      .getElementById("pointer-target")
      .dispatchEvent(
        new content.wrappedJSObject.PointerEvent("pointerrawupdate", {})
      );
  });

  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 107);
  await resume(dbg);
  await toggleEventBreakpoint(dbg, "Pointer", "event.pointer.pointerrawupdate");

  info("Check that the click event breakpoint is still enabled");
  invokeInTab("clickHandler");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 12);
  await resume(dbg);

  info("Check that disabling an event breakpoint works");
  await toggleEventBreakpoint(dbg, "Mouse", "event.mouse.click");
  invokeInTab("clickHandler");
  // wait for a bit to make sure the debugger do not pause
  await wait(100);
  assertNotPaused(dbg);

  info("Check that we can re-enable event breakpoints");
  await toggleEventBreakpoint(dbg, "Mouse", "event.mouse.click");
  invokeInTab("clickHandler");
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 12);
  await resume(dbg);

  info(
    "Test that we don't pause on event breakpoints when source is blackboxed."
  );
  await clickElement(dbg, "blackbox");
  await waitForDispatch(dbg.store, "BLACKBOX_WHOLE_SOURCES");

  invokeInTab("clickHandler");
  // wait for a bit to make sure the debugger do not pause
  await wait(100);
  assertNotPaused(dbg);

  invokeInTab("xhrHandler");
  // wait for a bit to make sure the debugger do not pause
  await wait(100);
  assertNotPaused(dbg);

  invokeInTab("timerHandler");
  // wait for a bit to make sure the debugger do not pause
  await wait(100);
  assertNotPaused(dbg);

  // The XHR load and the 50ms timer above outlive their `wait(100)` on a slow
  // machine. Turn their breakpoints off while the source is still blackboxed:
  // otherwise one of them pauses just after it is unblackboxed, the navigation
  // below then fires "beforeunload" inside that pause, and the engine refuses
  // to run the debuggee there, so the breakpoint the test waits for never hits.
  await toggleEventBreakpoint(dbg, "Mouse", "event.mouse.click");
  await toggleEventBreakpoint(dbg, "XHR", "event.xhr.load");
  await toggleEventBreakpoint(dbg, "Timer", "timer.timeout.set");
  await toggleEventBreakpoint(dbg, "Timer", "timer.timeout.fire");

  // Cleanup - unblackbox the source
  await clickElement(dbg, "blackbox");
  await waitForDispatch(dbg.store, "UNBLACKBOX_WHOLE_SOURCES");

  info(`Check that breakpoint can be set on "beforeUnload" event`);
  await toggleEventBreakpoint(dbg, "Load", "event.load.beforeunload");
  let onReload = reload(dbg);
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 82);
  await resume(dbg);
  await onReload;
  // The reload function waits for the sources to be available, but it doesn't
  // wait for the breakpoint checkboxes to be restored, and they're done
  // asynchronously.
  // Wait for the checkbox state, so that the next toggle unchecks it as a
  // cleanup.
  await waitForEventBreakpointChecked(dbg, "Load", "event.load.beforeunload");
  await toggleEventBreakpoint(dbg, "Load", "event.load.beforeunload");

  info(`Check that breakpoint can be set on "unload" event`);
  await toggleEventBreakpoint(dbg, "Load", "event.load.unload");
  onReload = reload(dbg);
  await waitForPaused(dbg);
  await assertPausedAtSourceAndLine(dbg, eventBreakpointsSource.id, 87);
  await resume(dbg);
  await onReload;
  await waitForEventBreakpointChecked(dbg, "Load", "event.load.unload");
  await toggleEventBreakpoint(dbg, "Load", "event.load.unload");
});

// Cover CmdOrCtrl+click on event breakpoints
add_task(async function () {
  const dbg = await initDebugger(
    "doc-event-breakpoints.html",
    "event-breakpoints.js"
  );

  // Toggle two distinct categories
  await toggleEventBreakpoint(dbg, "Load", "event.load.unload");
  await toggleEventBreakpoint(dbg, "Mouse", "event.mouse.click");

  info("CmdOrCtrl + click on the Timer category");
  const loadGroupCheckbox = findElementWithSelector(dbg, `input[value="Load"]`);
  const mouseGroupCheckbox = findElementWithSelector(
    dbg,
    `input[value="Mouse"]`
  );
  const timerGroupCheckbox = findElementWithSelector(
    dbg,
    `input[value="Timer"]`
  );
  is(loadGroupCheckbox.indeterminate, true);
  is(mouseGroupCheckbox.indeterminate, true);
  is(timerGroupCheckbox.checked, false);
  timerGroupCheckbox.scrollIntoView();
  EventUtils.synthesizeMouseAtCenter(
    timerGroupCheckbox,
    { [Services.appinfo.OS === "Darwin" ? "metaKey" : "ctrlKey"]: true },
    dbg.win
  );
  info("Wait for the checkboxes to update");
  await waitFor(
    () =>
      timerGroupCheckbox.checked === true &&
      loadGroupCheckbox.indeterminate === false
  );
  is(loadGroupCheckbox.indeterminate, false);
  is(mouseGroupCheckbox.indeterminate, false);
  is(timerGroupCheckbox.checked, true);
});

async function invokeOnElement(selector, action) {
  gBrowser.selectedBrowser.focus();
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [selector, action],
    (_selector, _action) => {
      content.document.querySelector(_selector)[_action]();
    }
  );
}

// An empty spawn used as a flush: composition events reach the content process at
// input priority, ahead of this round trip's normal-priority message on the same
// channel, so anything queued before it has been handled once it resolves.
function flushPendingContentEvents() {
  return SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {});
}

function invokeComposition() {
  const string = "ex";
  EventUtils.synthesizeCompositionChange({
    composition: {
      string,
      clauses: [
        {
          length: string.length,
          attr: Ci.nsITextInputProcessor.ATTR_RAW_CLAUSE,
        },
      ],
    },
    caret: { start: string.length, length: 0 },
    key: { key: string[string.length - 1] },
  });
}
