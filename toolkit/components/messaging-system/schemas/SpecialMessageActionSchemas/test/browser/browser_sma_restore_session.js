/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const { _LastSession } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs"
);

const state = {
  windows: [
    {
      tabs: [
        {
          entries: [
            {
              url: "https://example.org/",
              triggeringPrincipal_base64: E10SUtils.SERIALIZED_SYSTEMPRINCIPAL,
            },
          ],
        },
      ],
      selected: 1,
    },
  ],
};

add_task(async function test_RESTORE_SESSION() {
  let win = await BrowserTestUtils.openNewBrowserWindow();
  await BrowserTestUtils.loadURIString({
    browser: win.gBrowser.selectedBrowser,
    uriString: "about:asrouter",
  });

  let tabCreated = BrowserTestUtils.waitForEvent(win, "TabOpen");
  _LastSession.setState(state);
  is(win.gBrowser.tabs.length, 1, "Number of tabs is 1");

  await SMATestUtils.executeAndValidateAction({ type: "RESTORE_SESSION" });
  info("validated");

  await tabCreated;
  info("all tabs restored");
  is(win.gBrowser.tabs.length, 2, "Number of tabs is 2");
  await BrowserTestUtils.closeWindow(win);
});
