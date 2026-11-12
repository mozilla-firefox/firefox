/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ASWebAuthSessionService:
    "moz-src:///browser/modules/ASWebAuthSessionService.sys.mjs",
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
});

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const TEST_BASE = "https://example.com/browser/browser/modules/test/browser/";
const LOGIN_URL = TEST_BASE + "browser_aswebauth_form.sjs";
const CALLBACK_SCHEME = "data";
const CALLBACK_URL = "data:text/html,authcomplete?code=abc123";

function startAuthSession(params) {
  let {
    url,
    uuid,
    callbackScheme = "",
    hasCallback = false,
    ephemeral = false,
    headers = {},
    matchURLs = [],
  } = params;

  let request = {
    QueryInterface: ChromeUtils.generateQI(["nsIASWebAuthSessionRequest"]),
    uuid,
    url,
    callbackScheme,
    hasCallback,
    useEphemeralSession: ephemeral,
    get additionalHeaderNames() {
      return Object.keys(headers);
    },
    getAdditionalHeader(name) {
      return headers[name] ?? "";
    },
    matchesCallbackURL(candidate) {
      return matchURLs.includes(candidate);
    },
    complete(callbackURL) {
      Services.obs.notifyObservers(
        null,
        "aswebauthsession-test-complete",
        JSON.stringify({ uuid, callbackURL })
      );
    },
    cancel() {
      Services.obs.notifyObservers(
        null,
        "aswebauthsession-test-cancel",
        JSON.stringify({ uuid })
      );
    },
  };

  Services.obs.notifyObservers(request, "aswebauthsession-request-begin");
}

function newUUID() {
  return Services.uuid.generateUUID().toString().slice(1, -1).toUpperCase();
}

function waitForObserver(topic) {
  return TestUtils.topicObserved(topic).then(([, data]) => data);
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.userContext.enabled", true]],
  });
});

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

add_task(async function test_leftover_ephemeral_container_cleanup() {
  let container = await ContextualIdentityService.create(
    "ephemeral-aswebauth-" + newUUID(),
    "fingerprint",
    "blue"
  );

  await ASWebAuthSessionService.removeLeftoverEphemeralContainers();

  Assert.equal(
    ContextualIdentityService.getPublicIdentityFromId(container.userContextId),
    null,
    "leftover ephemeral container is removed"
  );
});

add_task(async function test_active_ephemeral_container_survives_cleanup() {
  let uuid = newUUID();
  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: true,
  });

  let win = await winPromise;
  let browser = win.gBrowser.selectedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, null, true);

  let attrs = await SpecialPowers.spawn(browser, [], () => {
    return content.document.nodePrincipal.originAttributes;
  });
  Assert.greater(attrs.userContextId, 0, "ephemeral session has a container");

  await ASWebAuthSessionService.removeLeftoverEphemeralContainers();

  Assert.ok(
    ContextualIdentityService.getPublicIdentityFromId(attrs.userContextId),
    "the container of a running session is left alone"
  );

  await BrowserTestUtils.closeWindow(win);
  let data = JSON.parse(await cancelPromise);
  Assert.equal(data.uuid, uuid, "user-cancel fires on close");
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

add_task(async function test_ephemeral_callback() {
  let uuid = newUUID();
  let completePromise = waitForObserver("aswebauthsession-test-complete");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: true,
  });

  let win = await winPromise;
  let browser = win.gBrowser.selectedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, null, true);

  let attrs = await SpecialPowers.spawn(browser, [], () => {
    return content.document.nodePrincipal.originAttributes;
  });
  Assert.equal(
    attrs.privateBrowsingId,
    1,
    "ephemeral session uses a private window"
  );
  Assert.greater(
    attrs.userContextId,
    0,
    "ephemeral session has a unique container"
  );

  let contextId = attrs.userContextId;
  let closedPromise = BrowserTestUtils.domWindowClosed(win);
  BrowserTestUtils.startLoadingURIString(browser, CALLBACK_URL);

  let data = JSON.parse(await completePromise);
  Assert.equal(data.uuid, uuid, "complete notification has the correct UUID");
  await closedPromise;

  let identity = ContextualIdentityService.getPublicIdentityFromId(contextId);
  Assert.equal(
    identity,
    null,
    "container is removed after the callback completes"
  );
});

add_task(async function test_ephemeral_cancellation() {
  let uuid = newUUID();
  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: true,
  });

  let win = await winPromise;
  let browser = win.gBrowser.selectedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, null, true);

  let attrs = await SpecialPowers.spawn(browser, [], () => {
    return content.document.nodePrincipal.originAttributes;
  });
  let contextId = attrs.userContextId;
  Assert.greater(contextId, 0, "ephemeral session has a container");

  await BrowserTestUtils.closeWindow(win);
  let data = JSON.parse(await cancelPromise);
  Assert.equal(data.uuid, uuid, "user-cancel fires on close");

  let identity = ContextualIdentityService.getPublicIdentityFromId(contextId);
  Assert.equal(identity, null, "container is removed after user cancels");
});

add_task(async function test_concurrent_ephemeral_isolation() {
  let uuid1 = newUUID();
  let uuid2 = newUUID();

  let win1Promise = BrowserTestUtils.waitForNewWindow();
  startAuthSession({
    url: LOGIN_URL,
    uuid: uuid1,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: true,
  });
  let win1 = await win1Promise;
  await BrowserTestUtils.browserLoaded(
    win1.gBrowser.selectedBrowser,
    false,
    null,
    true
  );

  let win2Promise = BrowserTestUtils.waitForNewWindow();
  startAuthSession({
    url: LOGIN_URL,
    uuid: uuid2,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: true,
  });
  let win2 = await win2Promise;
  await BrowserTestUtils.browserLoaded(
    win2.gBrowser.selectedBrowser,
    false,
    null,
    true
  );

  let attrs1 = await SpecialPowers.spawn(
    win1.gBrowser.selectedBrowser,
    [],
    () => content.document.nodePrincipal.originAttributes
  );
  let attrs2 = await SpecialPowers.spawn(
    win2.gBrowser.selectedBrowser,
    [],
    () => content.document.nodePrincipal.originAttributes
  );

  Assert.greater(attrs1.userContextId, 0, "first session has a container");
  Assert.greater(attrs2.userContextId, 0, "second session has a container");
  Assert.notEqual(
    attrs1.userContextId,
    attrs2.userContextId,
    "concurrent ephemeral sessions use different containers"
  );

  let cancel1 = waitForObserver("aswebauthsession-test-cancel");
  await BrowserTestUtils.closeWindow(win1);
  let cancelData1 = JSON.parse(await cancel1);
  Assert.equal(cancelData1.uuid, uuid1, "first session cancel has its UUID");

  let cancel2 = waitForObserver("aswebauthsession-test-cancel");
  await BrowserTestUtils.closeWindow(win2);
  let cancelData2 = JSON.parse(await cancel2);
  Assert.equal(cancelData2.uuid, uuid2, "second session cancel has its UUID");
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
