/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_aswebauth.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_aswebauth.js",
  this
);

add_task(async function test_reject_non_https() {
  let uuid = newUUID();
  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  let windowOpened = false;
  let onWindowOpen = (subject, topic) => {
    if (topic == "domwindowopened") {
      windowOpened = true;
    }
  };
  Services.ww.registerNotification(onWindowOpen);

  startAuthSession({
    // eslint-disable-next-line sdl/no-insecure-url
    url: "http://example.com/not-secure",
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: false,
  });

  let data = JSON.parse(await cancelPromise);
  Services.ww.unregisterNotification(onWindowOpen);
  Assert.equal(data.uuid, uuid, "user-cancel fires for non-HTTPS URL");
  Assert.ok(!windowOpened, "no window opened for non-HTTPS URL");
});

add_task(async function test_missing_callback_scheme() {
  let uuid = newUUID();
  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    ephemeral: false,
  });

  let win = await winPromise;
  let browser = win.gBrowser.selectedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, null, true);

  let completeFired = false;
  let completeObs = {
    observe() {
      completeFired = true;
    },
  };
  Services.obs.addObserver(completeObs, "aswebauthsession-test-complete");

  BrowserTestUtils.startLoadingURIString(browser, CALLBACK_URL);
  await BrowserTestUtils.browserLoaded(browser, false, null, true);

  Services.obs.removeObserver(completeObs, "aswebauthsession-test-complete");

  Assert.ok(
    !completeFired,
    "complete observer does not fire without a callback scheme"
  );
  Assert.ok(!win.closed, "window stays open without a callback scheme");

  await BrowserTestUtils.closeWindow(win);
  let data = JSON.parse(await cancelPromise);
  Assert.equal(data.uuid, uuid, "user-cancel fires when the window is closed");
});

add_task(async function test_callback_matching() {
  let uuid = newUUID();
  let completePromise = waitForObserver("aswebauthsession-test-complete");
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

  let closedPromise = BrowserTestUtils.domWindowClosed(win);
  BrowserTestUtils.startLoadingURIString(browser, CALLBACK_URL);

  let data = JSON.parse(await completePromise);
  Assert.equal(data.uuid, uuid, "complete notification has the correct UUID");
  Assert.ok(
    data.callbackURL.startsWith(CALLBACK_SCHEME + ":"),
    "callback URL has the expected scheme"
  );

  await closedPromise;
  Assert.ok(win.closed, "auth window closes after the callback matches");
});

add_task(async function test_callback_matching_in_new_tab() {
  let uuid = newUUID();
  let completePromise = waitForObserver("aswebauthsession-test-complete");
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

  let closedPromise = BrowserTestUtils.domWindowClosed(win);
  BrowserTestUtils.addTab(win.gBrowser, CALLBACK_URL, {
    triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
  });

  let data = JSON.parse(await completePromise);
  Assert.equal(data.uuid, uuid, "complete notification has the correct UUID");
  Assert.equal(
    data.callbackURL,
    CALLBACK_URL,
    "callback URL from another auth window tab is forwarded"
  );

  await closedPromise;
  Assert.ok(win.closed, "auth window closes after the callback matches");
});

add_task(async function test_callback_matching_in_popup_window() {
  let uuid = newUUID();
  let callbackScheme = "testauth";
  let callbackURL = callbackScheme + "://callback?code=popup123";
  let completePromise = waitForObserver("aswebauthsession-test-complete");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme,
    ephemeral: false,
  });

  let win = await winPromise;
  let popupWin = null;
  try {
    let browser = win.gBrowser.selectedBrowser;
    await BrowserTestUtils.browserLoaded(browser, false, null, true);

    let popupPromise = BrowserTestUtils.waitForNewWindow();
    await SpecialPowers.spawn(browser, [], () => {
      content.open("about:blank", "_blank", "height=500,width=500");
    });
    popupWin = await popupPromise;

    let closedPromise = BrowserTestUtils.domWindowClosed(win);
    await SpecialPowers.spawn(
      popupWin.gBrowser.selectedBrowser,
      [callbackURL],
      url => {
        content.location.href = url;
      }
    );

    let data = JSON.parse(await completePromise);
    Assert.equal(data.uuid, uuid, "complete notification has the correct UUID");
    Assert.equal(
      data.callbackURL,
      callbackURL,
      "callback URL from an auth popup window is forwarded"
    );

    await closedPromise;
    Assert.ok(win.closed, "auth window closes after the callback matches");
  } finally {
    if (popupWin && !popupWin.closed) {
      await BrowserTestUtils.closeWindow(popupWin);
    }
    if (!win.closed) {
      await BrowserTestUtils.closeWindow(win);
    }
  }
});

add_task(async function test_https_callback_candidate_in_popup_window() {
  let uuid = newUUID();
  let callbackURL = LOGIN_URL + "?callback_candidate=" + uuid;
  let completePromise = waitForObserver("aswebauthsession-test-complete");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    hasCallback: true,
    ephemeral: false,
    matchURLs: [callbackURL],
  });

  let win = await winPromise;
  let popupWin = null;
  try {
    let browser = win.gBrowser.selectedBrowser;
    await BrowserTestUtils.browserLoaded(browser, false, null, true);

    let popupPromise = BrowserTestUtils.waitForNewWindow();
    await SpecialPowers.spawn(browser, [], () => {
      content.open("about:blank", "_blank", "height=500,width=500");
    });
    popupWin = await popupPromise;

    let closedPromise = BrowserTestUtils.domWindowClosed(win);
    BrowserTestUtils.startLoadingURIString(
      popupWin.gBrowser.selectedBrowser,
      callbackURL
    );

    let data = JSON.parse(await completePromise);
    Assert.equal(data.uuid, uuid, "complete notification has the correct UUID");
    Assert.equal(
      data.callbackURL,
      callbackURL,
      "HTTPS callback candidate from an auth popup window matches and completes"
    );

    await closedPromise;
    Assert.ok(
      win.closed,
      "auth window closes after the HTTPS callback matches"
    );
  } finally {
    if (popupWin && !popupWin.closed) {
      await BrowserTestUtils.closeWindow(popupWin);
    }
    if (!win.closed) {
      let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
      await BrowserTestUtils.closeWindow(win);
      await cancelPromise;
    }
  }
});

add_task(async function test_redirect_interception() {
  let uuid = newUUID();
  let callbackScheme = "testauth";
  let callbackURL = callbackScheme + "://callback?code=redirect123";
  let redirectURL =
    LOGIN_URL + "?redirect_to=" + encodeURIComponent(callbackURL);

  let completePromise = waitForObserver("aswebauthsession-test-complete");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme,
    ephemeral: false,
  });

  let win = await winPromise;
  let browser = win.gBrowser.selectedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, null, true);

  let closedPromise = BrowserTestUtils.domWindowClosed(win);
  BrowserTestUtils.startLoadingURIString(browser, redirectURL);

  let data = JSON.parse(await completePromise);
  Assert.equal(data.uuid, uuid, "complete notification has the correct UUID");
  Assert.equal(
    data.callbackURL,
    callbackURL,
    "callback URL from intercepted redirect is forwarded"
  );

  await closedPromise;
  Assert.ok(win.closed, "auth window closes after redirect interception");
});

add_task(async function test_js_navigation_interception() {
  let uuid = newUUID();
  let callbackScheme = "testauth";
  let callbackURL = callbackScheme + "://callback?code=js456";
  let loginURL =
    LOGIN_URL + "?js_redirect_to=" + encodeURIComponent(callbackURL);

  let completePromise = waitForObserver("aswebauthsession-test-complete");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: loginURL,
    uuid,
    callbackScheme,
    ephemeral: false,
  });

  let win = await winPromise;
  let closedPromise = BrowserTestUtils.domWindowClosed(win);

  let data = JSON.parse(await completePromise);
  Assert.equal(data.uuid, uuid, "complete notification has the correct UUID");
  Assert.equal(
    data.callbackURL,
    callbackURL,
    "callback URL from a script navigation is forwarded"
  );

  await closedPromise;
  Assert.ok(win.closed, "auth window closes after a script navigation");
});

add_task(async function test_meta_refresh_interception() {
  let uuid = newUUID();
  let callbackScheme = "testauth";
  let callbackURL = callbackScheme + "://callback?code=meta789";
  let loginURL =
    LOGIN_URL + "?meta_refresh_to=" + encodeURIComponent(callbackURL);

  let completePromise = waitForObserver("aswebauthsession-test-complete");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: loginURL,
    uuid,
    callbackScheme,
    ephemeral: false,
  });

  let win = await winPromise;
  let closedPromise = BrowserTestUtils.domWindowClosed(win);

  let data = JSON.parse(await completePromise);
  Assert.equal(data.uuid, uuid, "complete notification has the correct UUID");
  Assert.equal(
    data.callbackURL,
    callbackURL,
    "callback URL from a meta refresh is forwarded"
  );

  await closedPromise;
  Assert.ok(win.closed, "auth window closes after a meta refresh");
});

add_task(async function test_custom_headers() {
  let uuid = newUUID();
  let headerName = "X-Aswebauth-Test";
  let headerValue = "test-" + uuid;
  let url = LOGIN_URL + "?reflect_header=" + encodeURIComponent(headerName);
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url,
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: false,
    headers: { [headerName]: headerValue },
  });

  let win = await winPromise;
  let browser = win.gBrowser.selectedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, null, true);

  let title = await SpecialPowers.spawn(
    browser,
    [],
    () => content.document.title
  );
  Assert.equal(
    title,
    headerValue,
    "custom request header was applied to the auth request"
  );

  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  await BrowserTestUtils.closeWindow(win);
  let cancelData = JSON.parse(await cancelPromise);
  Assert.equal(cancelData.uuid, uuid, "cleanup cancel has the correct UUID");
});
