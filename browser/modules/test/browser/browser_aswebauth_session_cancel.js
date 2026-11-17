/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_aswebauth.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_aswebauth.js",
  this
);

add_task(async function test_user_cancellation() {
  let uuid = newUUID();
  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
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

  await BrowserTestUtils.closeWindow(win);
  let data = JSON.parse(await cancelPromise);
  Assert.equal(
    data.uuid,
    uuid,
    "user-cancel fires when the user closes the window"
  );
});

add_task(async function test_user_cancellation_before_window_load() {
  let uuid = newUUID();
  let identityName = "ephemeral-aswebauth-" + uuid;
  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  let closedPromise = new Promise(resolve => {
    Services.ww.registerNotification(function observer(subject, topic) {
      if (topic != "domwindowopened") {
        return;
      }
      Services.ww.unregisterNotification(observer);
      let closed = BrowserTestUtils.domWindowClosed(subject);
      subject.close();
      resolve(closed);
    });
  });

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: true,
  });

  await closedPromise;
  let data = JSON.parse(await cancelPromise);
  Assert.equal(
    data.uuid,
    uuid,
    "user-cancel fires when the window closes before load"
  );

  let identity = ContextualIdentityService.getPublicIdentities().find(
    info => info.name == identityName
  );
  Assert.equal(
    identity,
    undefined,
    "container is removed after close before load"
  );
  if (identity) {
    ContextualIdentityService.remove(identity.userContextId);
  }
});

add_task(async function test_external_cancel() {
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

  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  let closedPromise = BrowserTestUtils.domWindowClosed(win);
  Services.obs.notifyObservers(null, "aswebauthsession-request-cancel", uuid);

  let data = JSON.parse(await cancelPromise);
  Assert.equal(data.uuid, uuid, "a native cancel is answered on the request");

  await closedPromise;
  Assert.ok(win.closed, "window closes after a native cancel notification");
});

add_task(async function test_external_cancel_before_window_opens() {
  let uuid = newUUID();
  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  let openedPromise = new Promise(resolve => {
    Services.ww.registerNotification(function observer(subject, topic) {
      if (topic != "domwindowopened") {
        return;
      }
      Services.ww.unregisterNotification(observer);
      // The window is still opening, so the session is only a pending setup.
      Services.obs.notifyObservers(
        null,
        "aswebauthsession-request-cancel",
        uuid
      );
      resolve(subject);
    });
  });

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: false,
  });

  let data = JSON.parse(await cancelPromise);
  Assert.equal(
    data.uuid,
    uuid,
    "a native cancel is answered while the window is still opening"
  );

  let win = await openedPromise;
  await BrowserTestUtils.domWindowClosed(win);
  Assert.ok(win.closed, "the half-opened auth window is closed again");
});

add_task(
  async function test_external_cancel_while_waiting_for_session_restore() {
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

    let cancelCount = 0;
    let cancelObs = {
      observe() {
        cancelCount++;
      },
    };
    Services.obs.addObserver(cancelObs, "aswebauthsession-test-cancel");

    let uuid = newUUID();
    startAuthSession({
      url: LOGIN_URL,
      uuid,
      callbackScheme: CALLBACK_SCHEME,
      ephemeral: false,
    });
    Services.obs.notifyObservers(null, "aswebauthsession-request-cancel", uuid);
    Assert.equal(cancelCount, 1, "a native cancel is answered right away");

    restored.resolve();
    sandbox.restore();
    await TestUtils.waitForTick();
    Services.ww.unregisterNotification(onWindowOpen);
    Services.obs.removeObserver(cancelObs, "aswebauthsession-test-cancel");

    Assert.equal(cancelCount, 1, "the request is answered only once");
    Assert.ok(!windowOpened, "no auth window opens for a cancelled request");
    Assert.ok(
      !ASWebAuthSessionService.pendingSetups.has(uuid),
      "the pending setup is gone"
    );
  }
);

add_task(async function test_original_auth_tab_close_cancels() {
  let uuid = newUUID();
  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: false,
  });

  let win = await winPromise;
  let browser = win.gBrowser.selectedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, null, true);

  BrowserTestUtils.addTab(win.gBrowser, "about:blank", {
    triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
  });

  let closedPromise = BrowserTestUtils.domWindowClosed(win);
  BrowserTestUtils.removeTab(win.gBrowser.getTabForBrowser(browser));

  let data = JSON.parse(await cancelPromise);
  Assert.equal(data.uuid, uuid, "closing the auth tab cancels the session");

  await closedPromise;
  Assert.ok(win.closed, "auth window closes after the auth tab is closed");
});
