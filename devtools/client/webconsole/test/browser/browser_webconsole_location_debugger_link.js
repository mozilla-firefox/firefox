/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test that message source links for js errors and console API calls open in
// the jsdebugger when clicked.

"use strict";

// There are shutdown issues for which multiple rejections are left uncaught.
// See bug 1018184 for resolving these issues.
PromiseTestUtils.allowMatchingRejectionsGlobally(/this\.worker is null/);

requestLongerTimeout(2);

const TEST_URI =
  "http://example.com/browser/devtools/client/webconsole/" +
  "test/browser/test-location-debugger-link.html";

add_task(async function () {
  await pushPref("devtools.webconsole.filter.error", true);
  await pushPref("devtools.webconsole.filter.log", true);

  // On e10s, the exception thrown in test-location-debugger-link-errors.js
  // is triggered in child process and is ignored by test harness
  if (!Services.appinfo.browserTabsRemoteAutostart) {
    expectUncaughtException();
  }

  const hud = await openNewTabAndConsole(TEST_URI);
  const toolbox = gDevTools.getToolboxForTab(gBrowser.selectedTab);

  await testOpenInDebugger(hud, {
    text: "document.bar",
    typeSelector: ".error",
  });

  info("Selecting the console again");
  await toolbox.selectTool("webconsole");
  await testOpenInDebugger(hud, {
    text: "Blah Blah",
    typeSelector: ".console-api",
  });

  // // check again the first node.
  info("Selecting the console again");
  await toolbox.selectTool("webconsole");
  await testOpenInDebugger(hud, {
    text: "document.bar",
    typeSelector: ".error",
  });

  info("Check location of evaluation error");
  await toolbox.selectTool("webconsole");
  await execute(
    hud,
    `const x = {};
     x.foo.bar;`
  );

  await testOpenInDebugger(hud, {
    text: "x.foo is undefined",
    typeSelector: ".error",
    // "debugger eval code" isn't an actual URL and is not stored as such in the Debugger
    // state, so don't check it.
    expectUrl: false,
  });
});
