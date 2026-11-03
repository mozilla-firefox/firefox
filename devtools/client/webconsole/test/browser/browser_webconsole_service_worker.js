/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests that console api usage in service worker show in the console

"use strict";

const TEST_URI =
  "https://example.com/browser/devtools/client/webconsole/" +
  "test/browser/test-service-worker.html";

add_task(async function () {
  // Required to have the service worker fully debuggable from the console
  await pushPref("dom.worker.console.dispatch_events_to_main_thread", false);

  const hud = await openNewTabAndConsole(TEST_URI);

  info("Wait for the service worker to be registered");
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
    await content.wrappedJSObject.registrationPromise;
  });

  await waitFor(() =>
    findConsoleAPIMessage(hud, "message from service worker")
  );

  info("Wait for service worker unregistration before teardown");
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
    const registration = await content.wrappedJSObject.registrationPromise;
    await registration.unregister();
  });
});
