/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

add_task(async function check_document_pip_not_saved() {
  const [tab, chromePiP] = await newTabWithPiP();

  await TabStateFlusher.flushWindow(window);
  await TabStateFlusher.flushWindow(chromePiP);
  let state = SessionStore.getCurrentState(true);
  // Don't keepOnlyWorthSavingTabs as Ctrl+Shift+T can restore regardless of that
  // and the about:blank in chromePiP might not be worth saving.

  is(state.windows.length, 1, "Only one window in session store");

  // Just to be extra sure, check the only tracked window is for |tab|
  const urls = state.windows[0].tabs
    .flatMap(ssTab => ssTab.entries.map(entry => entry.url))
    .filter(url => !!url);
  // |urls| contains an unexpected about:blank (bug 2006027).
  // But if example.com is in there, the only tacked window is |tab|
  ok(
    urls.includes("https://example.com/"),
    "Window tracked in sessionstore is tab not PiP"
  );

  // Cleanup.
  await BrowserTestUtils.closeWindow(chromePiP);
  BrowserTestUtils.removeTab(tab);
});
