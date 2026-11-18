/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Check evaluating eager-evaluation values.
const TEST_URI = "data:text/html;charset=utf8,<!DOCTYPE html>";
const MODULE_URI = "moz-src:///dom/base/ContentAreaDropListener.sys.mjs";

add_task(async function () {
  await pushPref("devtools.chrome.enabled", true);
  await pushPref("devtools.browsertoolbox.scope", "everything");

  // Evaluate in a new content process, as the parent process keeps the module
  // loaded and the Services property resolved once a previous run did it.
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: TEST_URI,
    forceNewProcess: true,
  });
  const pid = tab.linkedBrowser.browsingContext.currentWindowGlobal.osPid;

  info("Open the Browser Console");
  const hud = await BrowserConsoleManager.toggleBrowserConsole();

  info(`Select the content process target (pid ${pid})`);
  const processItem = await waitFor(() =>
    getContextSelectorItems(hud).find(el =>
      el.querySelector(".label")?.innerText?.startsWith(`(pid ${pid}) `)
    )
  );
  processItem.click();
  await waitFor(() =>
    hud.ui.outputNode
      .querySelector(".webconsole-evaluation-selector-button")
      .innerText.startsWith(`(pid ${pid}) `)
  );

  await executeResolveHookWithSideEffect(hud, tab.linkedBrowser);

  // Close the Browser Console while the content process it evaluates in is
  // still alive: removing the tab shuts that process down.
  await safeCloseBrowserConsole({ clearOutput: true });
  BrowserTestUtils.removeTab(tab);
});

async function executeResolveHookWithSideEffect(hud, browser) {
  // Services.droppedLinkHandler is implemented with resolve hook, which imports
  // ContentAreaDropListener.sys.mjs.
  //
  // In order to test the resolve hook behavior, ensure the module is not yet
  // loaded, which ensures the property is not yet resolved.
  is(
    await isModuleLoadedInContentProcess(browser),
    false,
    "The module isn't loaded in the new content process"
  );

  setInputValue(hud, `Services.droppedLinkHandler`);

  await wait(500);
  // Eager evaluation should fail, due to the side effect in the resolve hook.
  await waitForEagerEvaluationResult(hud, "");

  setInputValue(hud, "");
  await wait(500);

  // The property should be resolved when evaluating after the eager evaluation.
  await executeAndWaitForResultMessage(
    hud,
    `Services.droppedLinkHandler;`,
    "XPCWrappedNative_NoHelper"
  );

  is(
    await isModuleLoadedInContentProcess(browser),
    true,
    "The module got loaded by the evaluation"
  );

  // Eager evaluation should work after the property is resolved.
  setInputValue(hud, `Services.droppedLinkHandler`);
  await wait(500);
  await waitForEagerEvaluationResult(hud, /XPCWrappedNative_NoHelper/);
}

function isModuleLoadedInContentProcess(browser) {
  return SpecialPowers.spawn(browser, [MODULE_URI], uri =>
    Cu.isESModuleLoaded(uri)
  );
}
