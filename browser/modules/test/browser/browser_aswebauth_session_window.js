/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_aswebauth.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_aswebauth.js",
  this
);

add_task(async function test_waits_for_session_restore() {
  let restored = Promise.withResolvers();
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(SessionStore, "promiseAllWindowsRestored")
    .get(() => restored.promise);

  let windowOpened = false;
  let onWindowOpen = (subject, topic) => {
    if (topic == "domwindowopened") {
      windowOpened = true;
    }
  };
  Services.ww.registerNotification(onWindowOpen);

  let uuid = newUUID();
  let winPromise = BrowserTestUtils.waitForNewWindow();
  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: false,
  });

  await TestUtils.waitForTick();
  Services.ww.unregisterNotification(onWindowOpen);
  Assert.ok(
    !windowOpened,
    "no auth window opens before session restore has finished"
  );

  restored.resolve();
  sandbox.restore();
  let win = await winPromise;
  Assert.ok(
    win.document.documentElement.hasAttribute("aswebauth"),
    "the auth window opens once session restore has finished"
  );

  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  await BrowserTestUtils.closeWindow(win);
  let data = JSON.parse(await cancelPromise);
  Assert.equal(data.uuid, uuid, "closing the auth window cancels");
});

add_task(async function test_excluded_from_session_restore() {
  let uuid = newUUID();
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: false,
  });

  let win = await winPromise;
  await BrowserTestUtils.browserLoaded(
    win.gBrowser.selectedBrowser,
    false,
    null,
    true
  );

  Assert.ok(
    !SessionStore.getWindowId(win),
    "auth window is not tracked by SessionStore (never registered)"
  );

  let state = JSON.parse(SessionStore.getBrowserState());
  let authWindowInSessionState = state.windows.some(w =>
    w.tabs?.some(tab =>
      tab.entries?.some(entry => entry.url?.startsWith(LOGIN_URL))
    )
  );
  Assert.ok(
    !authWindowInSessionState,
    "auth window is excluded from the session restore state"
  );

  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  await BrowserTestUtils.closeWindow(win);
  let data = JSON.parse(await cancelPromise);
  Assert.equal(data.uuid, uuid, "cleanup cancel has the correct UUID");
});

add_task(async function test_auth_window_geometry() {
  for (let ephemeral of [false, true]) {
    let source = await BrowserTestUtils.openNewBrowserWindow({
      private: ephemeral,
      features: "outerWidth=900,outerHeight=500,left=0,top=0",
    });
    let win;
    try {
      await SimpleTest.promiseFocus(source);
      let screen = source.screen;
      let expectedWidth = Math.floor(Math.min(600, screen.availWidth * 0.9));
      let expectedHeight = Math.floor(Math.min(700, screen.availHeight * 0.9));
      let winPromise = BrowserTestUtils.waitForNewWindow({ url: LOGIN_URL });
      startAuthSession({ url: LOGIN_URL, uuid: newUUID(), ephemeral });
      win = await winPromise;
      let expectedLeft =
        screen.availLeft + (screen.availWidth - win.outerWidth) / 2;
      let expectedTop =
        screen.availTop + (screen.availHeight - win.outerHeight) / 2;

      Assert.equal(
        win.innerWidth,
        expectedWidth,
        "Auth window has a fixed width"
      );
      Assert.equal(
        win.innerHeight,
        expectedHeight,
        "Auth window has a fixed height"
      );
      Assert.lessOrEqual(
        Math.abs(win.screenX - expectedLeft),
        1,
        "Auth window is horizontally centered in the available screen area"
      );
      Assert.lessOrEqual(
        Math.abs(win.screenY - expectedTop),
        1,
        "Auth window is vertically centered in the available screen area"
      );
      Assert.equal(
        win.windowState,
        win.STATE_NORMAL,
        "Auth window opens in the normal state"
      );
    } finally {
      if (win && !win.closed) {
        await BrowserTestUtils.closeWindow(win);
      }
      await BrowserTestUtils.closeWindow(source);
    }
  }
});
