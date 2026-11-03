/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { InfoBar } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/InfoBar.sys.mjs"
);
const { ASRouter } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouter.sys.mjs"
);
const { SpecialMessageActions } = ChromeUtils.importESModule(
  "resource://messaging-system/lib/SpecialMessageActions.sys.mjs"
);
const { RemoteL10n } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/RemoteL10n.sys.mjs"
);

const UNIVERSAL_MESSAGE = {
  id: "universal-infobar",
  content: {
    type: "universal",
    text: "t",
    buttons: [],
  },
};

// TODO: It might be cleaner to have a testing version of
// removeUniversalInfobars defined on InfoBar that does this cleanup?
const cleanupInfobars = () => {
  InfoBar._universalInfobars = [];
  InfoBar._activeInfobar = null;
  if (InfoBar._observingWindowOpened) {
    InfoBar._observingWindowOpened = false;
    Services.obs.removeObserver(InfoBar, "domwindowopened");
  }
};

const makeFakeWin = ({
  closed = false,
  toolbarVisible = true,
  taskbarTab = false,
  readyState = "complete",
  selectedBrowser,
} = {}) => {
  const win = {
    closed,
    toolbar: { visible: toolbarVisible },
    document: {
      readyState,
      documentElement: {
        hasAttribute: name => (name === "taskbartab" ? taskbarTab : false),
      },
    },
    gBrowser: { selectedBrowser },
  };

  const browser = { documentGlobal: win, id: selectedBrowser };
  win.gBrowser = { selectedBrowser: browser };
  return win;
};

add_setup(async function () {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(PrivateBrowsingUtils, "isWindowPrivate")
    .callsFake(win => !!win?.isPrivate);

  registerCleanupFunction(() => {
    sandbox.restore();
  });
});

add_task(async function showNotificationAllWindows() {
  const sandbox = sinon.createSandbox();
  let fakeNotification = { showNotification: sandbox.stub().resolves() };
  let fakeWins = [
    makeFakeWin({ selectedBrowser: "win1" }),
    makeFakeWin({ selectedBrowser: "win2" }),
    makeFakeWin({ selectedBrowser: "win3" }),
  ];

  sandbox.stub(InfoBar, "maybeLoadCustomElement");
  sandbox.stub(InfoBar, "maybeInsertFTL");

  let origWinManager = Services.wm;
  // Using sinon.stub won’t work here, because Services.wm is a frozen,
  // non-configurable object and its methods cannot be replaced via typical JS
  // property assignment.
  Object.defineProperty(Services, "wm", {
    value: { getEnumerator: () => fakeWins[Symbol.iterator]() },
    configurable: true,
    writable: true,
  });

  await InfoBar.showNotificationAllWindows(fakeNotification);

  Assert.equal(fakeNotification.showNotification.callCount, 3);
  Assert.equal(fakeNotification.showNotification.getCall(0).args[0].id, "win1");
  Assert.equal(fakeNotification.showNotification.getCall(1).args[0].id, "win2");
  Assert.equal(fakeNotification.showNotification.getCall(2).args[0].id, "win3");

  // Cleanup
  cleanupInfobars();
  sandbox.restore();
  Object.defineProperty(Services, "wm", {
    value: origWinManager,
    configurable: true,
    writable: true,
  });
});

add_task(async function removeUniversalInfobars() {
  const sandbox = sinon.createSandbox();
  let browser = BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser;
  let origBox = browser.documentGlobal.gNotificationBox;
  browser.documentGlobal.gNotificationBox = {
    appendNotification: sandbox.stub().resolves(document.createElement("span")),
    removeNotification: sandbox.stub(),
  };

  sandbox
    .stub(InfoBar, "showNotificationAllWindows")
    .callsFake(async notification => {
      await notification.showNotification(browser);
    });

  let notification = await InfoBar.showInfoBarMessage(
    browser,
    UNIVERSAL_MESSAGE,
    sandbox.stub()
  );

  Assert.equal(InfoBar._universalInfobars.length, 1);
  notification.removeUniversalInfobars();

  Assert.ok(
    browser.documentGlobal.gNotificationBox.removeNotification.calledWith(
      notification.notification
    )
  );

  Assert.deepEqual(InfoBar._universalInfobars, []);

  // Cleanup
  cleanupInfobars();
  browser.documentGlobal.gNotificationBox = origBox;
  sandbox.restore();
});

add_task(async function initialUniversal_showsAllWindows_andSendsTelemetry() {
  const sandbox = sinon.createSandbox();
  let browser = BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser;
  let origBox = browser.documentGlobal.gNotificationBox;
  browser.documentGlobal.gNotificationBox = {
    appendNotification: sandbox.stub().resolves(document.createElement("span")),
    removeNotification: sandbox.stub(),
  };

  let showAll = sandbox
    .stub(InfoBar, "showNotificationAllWindows")
    .callsFake(async notification => {
      await notification.showNotification(browser);
    });

  let dispatch1 = sandbox.stub();
  let dispatch2 = sandbox.stub();

  await InfoBar.showInfoBarMessage(browser, UNIVERSAL_MESSAGE, dispatch1);
  await InfoBar.showInfoBarMessage(browser, UNIVERSAL_MESSAGE, dispatch2, true);

  Assert.ok(showAll.calledOnce);
  Assert.equal(InfoBar._universalInfobars.length, 2);

  // Dispatch impression (as this is the first universal infobar) and telemetry
  // ping
  Assert.equal(dispatch1.callCount, 2);

  // Do not send telemetry for subsequent appearance of the message
  Assert.equal(dispatch2.callCount, 0);

  // Cleanup
  cleanupInfobars();
  browser.documentGlobal.gNotificationBox = origBox;
  sandbox.restore();
});

add_task(async function observe_domwindowopened_withLoadEvent() {
  const sandbox = sinon.createSandbox();
  let stub = sandbox.stub(InfoBar, "showInfoBarMessage").resolves();

  InfoBar._activeInfobar = {
    message: { content: { type: "universal" } },
    dispatch: sandbox.stub(),
  };

  let subject = makeFakeWin({ readyState: "loading", selectedBrowser: "b" });
  subject.addEventListener = function (event, cb) {
    subject.document.readyState = "complete";
    cb();
  };

  InfoBar.observe(subject, "domwindowopened");

  Assert.ok(stub.calledOnce);
  // Called with universalInNewWin true
  Assert.equal(stub.firstCall.args[3], true);

  // Cleanup
  cleanupInfobars();
  sandbox.restore();
});

add_task(async function observe_skips_nonUniversal() {
  const sandbox = sinon.createSandbox();
  let stub = sandbox.stub(InfoBar, "showInfoBarMessage").resolves();
  InfoBar._activeInfobar = {
    message: { content: { type: "global" } },
    dispatch: sandbox.stub(),
  };
  InfoBar.observe({}, "domwindowopened");
  Assert.ok(stub.notCalled);

  // Cleanup
  cleanupInfobars();
  sandbox.restore();
});

add_task(async function infobarCallback_dismissed_universal() {
  const sandbox = sinon.createSandbox();
  const browser = BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser;
  const dispatch = sandbox.stub();

  sandbox
    .stub(InfoBar, "showNotificationAllWindows")
    .callsFake(async notif => await notif.showNotification(browser));

  let infobar = await InfoBar.showInfoBarMessage(
    browser,
    UNIVERSAL_MESSAGE,
    dispatch
  );
  // Reset the dispatch count to just watch for the DISMISSED ping
  dispatch.reset();

  infobar.infobarCallback("not‑removed‑event");

  Assert.equal(dispatch.callCount, 1);
  Assert.equal(dispatch.firstCall.args[0].data.event, "DISMISSED");
  Assert.deepEqual(InfoBar._universalInfobars, []);

  // Cleanup
  cleanupInfobars();
  sandbox.restore();
});

add_task(async function removeObserver_on_removeUniversalInfobars() {
  const sandbox = sinon.createSandbox();

  sandbox.stub(InfoBar, "showNotificationAllWindows").resolves();

  let browser = BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser;
  let dispatch = sandbox.stub();

  // Show the universal infobar so it registers the observer
  let infobar = await InfoBar.showInfoBarMessage(
    browser,
    UNIVERSAL_MESSAGE,
    dispatch
  );
  Assert.ok(infobar, "Got an InfoBar notification");

  // Swap out Services.obs so removeObserver is spyable
  let origObs = Services.obs;
  let removeSpy = sandbox.spy();
  Services.obs = {
    addObserver: origObs.addObserver.bind(origObs),
    removeObserver: removeSpy,
    notifyObservers: origObs.notifyObservers.bind(origObs),
  };

  infobar.removeUniversalInfobars();

  Assert.ok(
    removeSpy.calledWith(InfoBar, "domwindowopened"),
    "removeObserver was invoked for domwindowopened"
  );

  // Cleanup. Make sure we remove the observer that removeSpy left behind.
  Services.obs = origObs;
  InfoBar._observingWindowOpened = true;
  cleanupInfobars();
  sandbox.restore();
});

add_task(async function universalInfobar_persists_original_window_closure() {
  const sandbox = sinon.createSandbox();
  // Fake window so we can safely close it
  let fakeWindow = makeFakeWin({ selectedBrowser: "win1" });

  InfoBar._activeInfobar = {
    message: UNIVERSAL_MESSAGE,
    dispatch: sandbox.stub(),
  };
  InfoBar._universalInfobars = [{ box: {}, notification: {}, win: fakeWindow }];

  Assert.ok(InfoBar._activeInfobar, "Got a universal infobar");

  // Mock closing the original window
  fakeWindow.closed = true;

  Assert.ok(
    InfoBar._activeInfobar,
    "_activeInfobar should persist through window closure"
  );

  let fakeNewWindow = makeFakeWin({ selectedBrowser: "win2" });
  fakeNewWindow.addEventListener = (event, cb) => cb();

  makeFakeWin({ selectedBrowser: "win2" });

  let showInfobarStub = sandbox.stub(InfoBar, "showInfoBarMessage").resolves();
  InfoBar.observe(fakeNewWindow, "domwindowopened");
  Assert.ok(
    showInfobarStub.calledOnce,
    "New window should receive the universal infobar"
  );

  // Cleanup
  cleanupInfobars();
  sandbox.restore();
});

add_task(async function test_universalInfobar_skips_popup_window() {
  const sandbox = sinon.createSandbox();
  const popupWin = makeFakeWin({
    toolbarVisible: false, // Simulate a popup window
    selectedBrowser: "popup-win",
  });

  const dispatch = sandbox.stub();
  const infobar = await InfoBar.showInfoBarMessage(
    popupWin.gBrowser.selectedBrowser,
    UNIVERSAL_MESSAGE,
    dispatch
  );

  Assert.equal(infobar, null, "Infobar not shown in popup window");
  Assert.equal(dispatch.callCount, 0, "No impression sent");

  // Cleanup
  cleanupInfobars();
  sandbox.restore();
});

add_task(async function test_universalInfobar_skips_taskbar_window() {
  const sandbox = sinon.createSandbox();
  const win = BrowserWindowTracker.getTopWindow();
  win.document.documentElement.setAttribute("taskbartab", "");

  const dispatch = sandbox.stub();
  const infobar = await InfoBar.showInfoBarMessage(
    win.gBrowser.selectedBrowser,
    UNIVERSAL_MESSAGE,
    dispatch
  );

  Assert.equal(infobar, null, "Infobar not visible for taskbar-tab window");
  Assert.equal(dispatch.callCount, 0, "No impression sent");

  win.document.documentElement.removeAttribute("taskbartab");
  cleanupInfobars();
  sandbox.restore();
});

add_task(async function universal_inline_anchor_dismiss_multiple_windows() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();
  const browser1 = win1.gBrowser.selectedBrowser;
  const win2 = await BrowserTestUtils.openNewBrowserWindow();
  const browser2 = win2.gBrowser.selectedBrowser;

  sandbox.stub(InfoBar, "maybeLoadCustomElement");
  sandbox.stub(InfoBar, "maybeInsertFTL");

  sandbox.stub(InfoBar, "showNotificationAllWindows").callsFake(async notif => {
    await notif.showNotification(browser1);
  });

  sandbox
    .stub(RemoteL10n, "formatLocalizableText")
    .resolves('<a data-l10n-name="test">Open</a>');

  const handle = sandbox.stub(SpecialMessageActions, "handleAction");

  const message = {
    id: "TEST_UNIVERSAL_INLINE_DISMISS_TWO_WINS",
    content: {
      type: "universal",
      text: { string_id: "test" },
      linkUrls: { test: "https://example.com/u" },
      linkActions: {
        test: {
          type: "SET_PREF",
          data: { pref: { name: "embedded-link-sma", value: true } },
          dismiss: true,
        },
      },
      buttons: [],
    },
  };

  const dispatch1 = sandbox.stub();
  const dispatch2 = sandbox.stub();

  await InfoBar.showInfoBarMessage(browser1, message, dispatch1);

  await InfoBar.showInfoBarMessage(
    browser2,
    message,
    dispatch2,
    true // universal in new window
  );

  const getNotification1 = () =>
    win1.gNotificationBox.getNotificationWithValue(message.id);
  const getNotification2 = () =>
    win2.gNotificationBox.getNotificationWithValue(message.id);

  await TestUtils.waitForCondition(
    () => !!getNotification1(),
    "Infobar present in window 1"
  );
  await TestUtils.waitForCondition(
    () => !!getNotification2(),
    "Infobar present in window 2"
  );

  // Ignore impression pings.
  dispatch1.resetHistory();
  dispatch2.resetHistory();

  const link = getNotification1()
    .querySelector(':scope > [slot="message"]')
    .querySelector('a[data-l10n-name="test"]');
  Assert.ok(link, "Inline anchor exists in window 1");
  EventUtils.synthesizeMouseAtCenter(link, {}, win1);

  await TestUtils.waitForCondition(
    () => !getNotification1(),
    "Infobar removed in window 1"
  );
  await TestUtils.waitForCondition(
    () => !getNotification2(),
    "Infobar removed in window 2"
  );

  Assert.equal(handle.callCount, 2, "Two SMAs handled (OPEN_URL and SET_PREF)");

  Assert.ok(
    dispatch1.calledWith(
      sinon.match({
        type: "INFOBAR_TELEMETRY",
        data: sinon.match.has("event", "DISMISSED"),
      })
    ),
    "DISMISSED telemetry send from Infobar where link was clicked"
  );

  Assert.ok(
    !dispatch2.calledWith(
      sinon.match({
        type: "INFOBAR_TELEMETRY",
        data: sinon.match.has("event", "DISMISSED"),
      })
    ),
    "DISMISSED telemetry was not sent from other instance of the Infobar"
  );

  // Cleanup
  win2.close();
  sandbox.restore();
  cleanupInfobars();
});

add_task(async function universal_dismiss_on_pref_change_multiple_windows() {
  const sandbox = sinon.createSandbox();
  const PREF = "messaging-system-action.dismissOnChange.universal";

  const win1 = BrowserWindowTracker.getTopWindow();
  const browser1 = win1.gBrowser.selectedBrowser;
  const win2 = await BrowserTestUtils.openNewBrowserWindow();
  const browser2 = win2.gBrowser.selectedBrowser;

  sandbox.stub(InfoBar, "maybeLoadCustomElement");
  sandbox.stub(InfoBar, "maybeInsertFTL");

  sandbox.stub(InfoBar, "showNotificationAllWindows").callsFake(async notif => {
    await notif.showNotification(browser1);
  });

  const message = {
    id: "TEST_UNIVERSAL_DISMISS_ON_PREF_MULTI",
    content: {
      type: "universal",
      text: "universal pref change",
      dismissable: true,
      buttons: [],
      dismissOnPrefChange: PREF,
    },
  };

  const dispatch1 = sandbox.stub();
  const dispatch2 = sandbox.stub();

  await InfoBar.showInfoBarMessage(browser1, message, dispatch1);
  await InfoBar.showInfoBarMessage(browser2, message, dispatch2, true);

  const getInfobar1 = () =>
    win1.gNotificationBox.getNotificationWithValue(message.id);
  const getInfobart2 = () =>
    win2.gNotificationBox.getNotificationWithValue(message.id);

  await TestUtils.waitForCondition(
    () => !!getInfobar1(),
    "Infobar present in window 1"
  );
  await TestUtils.waitForCondition(
    () => !!getInfobart2(),
    "Infobar present in window 2"
  );

  // Ignore impression pings
  dispatch1.resetHistory();
  dispatch2.resetHistory();

  Services.prefs.setBoolPref(PREF, true);

  await TestUtils.waitForCondition(
    () => !getInfobar1(),
    "Infobar removed in window 1"
  );
  await TestUtils.waitForCondition(
    () => !getInfobart2(),
    "Infobar removed in window 2"
  );

  // Cleanup
  await BrowserTestUtils.closeWindow(win2);
  sandbox.restore();
  cleanupInfobars();
  Services.prefs.clearUserPref(PREF);
});

const removeByIdInWin = (win, id) => {
  const n = win.gNotificationBox.getNotificationWithValue(id);
  if (n) {
    win.gNotificationBox.removeNotification(n);
  }
};

add_task(async function replace_universal_with_universal_across_two_windows() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();
  const browser1 = win1.gBrowser.selectedBrowser;
  const win2 = await BrowserTestUtils.openNewBrowserWindow();

  const firstUniversalMessage = {
    id: "TEST_REPLACE_UNIVERSAL_WITH_UNIVERSAL_FIRST",
    content: {
      type: "universal",
      text: "first universal message",
      buttons: [],
      canReplace: ["TEST_REPLACE_UNIVERSAL_WITH_UNIVERSAL_SECOND"],
    },
  };

  const secondUniversalMessage = {
    id: "TEST_REPLACE_UNIVERSAL_WITH_UNIVERSAL_SECOND",
    content: {
      type: "universal",
      text: "second universal message",
      buttons: [],
      canReplace: ["TEST_REPLACE_UNIVERSAL_WITH_UNIVERSAL_FIRST"],
    },
  };

  const getFromWin1 = id => win1.gNotificationBox.getNotificationWithValue(id);
  const getFromWin2 = id => win2.gNotificationBox.getNotificationWithValue(id);

  const dispatchFirstUniversal = sandbox.stub();
  await InfoBar.showInfoBarMessage(
    browser1,
    firstUniversalMessage,
    dispatchFirstUniversal
  );

  await TestUtils.waitForCondition(
    () => !!getFromWin1(firstUniversalMessage.id),
    "First universal visible in window 1"
  );
  await TestUtils.waitForCondition(
    () => !!getFromWin2(firstUniversalMessage.id),
    "First universal visible in window 2"
  );
  Assert.ok(
    dispatchFirstUniversal.calledWith(
      sinon.match({
        type: "IMPRESSION",
        data: sinon.match.has("id", firstUniversalMessage.id),
      })
    ),
    "Impression recorded for the first universal"
  );

  const dispatchSecondUniversal = sandbox.stub();
  await InfoBar.showInfoBarMessage(
    browser1,
    secondUniversalMessage,
    dispatchSecondUniversal
  );

  await TestUtils.waitForCondition(
    () => !!getFromWin1(secondUniversalMessage.id),
    "Second universal visible in window 1"
  );
  await TestUtils.waitForCondition(
    () => !!getFromWin2(secondUniversalMessage.id),
    "Second universal visible in window 2"
  );
  await TestUtils.waitForCondition(
    () => !getFromWin1(firstUniversalMessage.id),
    "First universal removed in window 1"
  );
  await TestUtils.waitForCondition(
    () => !getFromWin2(firstUniversalMessage.id),
    "First universal removed in window 2"
  );
  Assert.ok(
    dispatchSecondUniversal.calledWith(
      sinon.match({
        type: "IMPRESSION",
        data: sinon.match.has("id", secondUniversalMessage.id),
      })
    ),
    "Impression recorded for the second universal"
  );

  const dispatchFirstUniversalAgain = sandbox.stub();
  await InfoBar.showInfoBarMessage(
    browser1,
    firstUniversalMessage,
    dispatchFirstUniversalAgain
  );

  await TestUtils.waitForCondition(
    () => !!getFromWin1(firstUniversalMessage.id),
    "First universal visible again in window 1"
  );
  await TestUtils.waitForCondition(
    () => !!getFromWin2(firstUniversalMessage.id),
    "First universal visible again in window 2"
  );
  await TestUtils.waitForCondition(
    () => !getFromWin1(secondUniversalMessage.id),
    "Second universal removed in window 1"
  );
  await TestUtils.waitForCondition(
    () => !getFromWin2(secondUniversalMessage.id),
    "Second universal removed in window 2"
  );
  Assert.ok(
    dispatchFirstUniversalAgain.calledWith(
      sinon.match({
        type: "IMPRESSION",
        data: sinon.match.has("id", firstUniversalMessage.id),
      })
    ),
    "Impression recorded again for the first universal when shown again"
  );

  // Cleanup
  removeByIdInWin(win1, firstUniversalMessage.id);
  removeByIdInWin(win2, firstUniversalMessage.id);
  removeByIdInWin(win1, secondUniversalMessage.id);
  removeByIdInWin(win2, secondUniversalMessage.id);
  await BrowserTestUtils.closeWindow(win2);
  sandbox.restore();
  cleanupInfobars();
});

const getNotificationFromWin = (win, id) =>
  win?.gNotificationBox?.getNotificationWithValue?.(id);

add_task(async function universal_replaces_global_across_windows() {
  const sandbox = sinon.createSandbox();

  const firstWindow = BrowserWindowTracker.getTopWindow();
  const firstBrowser = firstWindow.gBrowser.selectedBrowser;
  const secondWindow = await BrowserTestUtils.openNewBrowserWindow();

  const globalMessage = {
    id: "TEST_GLOBAL_ORIGINAL_FOR_UNIVERSAL_REPLACEMENT",
    content: {
      type: "global",
      text: "original global",
      buttons: [],
      // no canReplace
    },
  };

  const universalReplacement = {
    id: "TEST_UNIVERSAL_REPLACES_GLOBAL",
    content: {
      type: "universal",
      text: "replacement universal",
      buttons: [],
      canReplace: [globalMessage.id],
    },
  };

  const dispatchGlobal = sandbox.stub();
  await InfoBar.showInfoBarMessage(firstBrowser, globalMessage, dispatchGlobal);

  await TestUtils.waitForCondition(
    () => !!getNotificationFromWin(firstWindow, globalMessage.id),
    "Global infobar visible in the first window"
  );
  Assert.ok(
    !getNotificationFromWin(secondWindow, globalMessage.id),
    "Global infobar is NOT shown in the second window"
  );
  Assert.ok(
    dispatchGlobal.calledWith(
      sinon.match({
        type: "IMPRESSION",
        data: sinon.match.has("id", globalMessage.id),
      })
    ),
    "Recorded impression for the original global message"
  );

  const dispatchUniversal = sandbox.stub();
  await InfoBar.showInfoBarMessage(
    firstBrowser,
    universalReplacement,
    dispatchUniversal
  );

  await TestUtils.waitForCondition(
    () => !!getNotificationFromWin(firstWindow, universalReplacement.id),
    "Universal replacement visible in the first window"
  );
  await TestUtils.waitForCondition(
    () => !!getNotificationFromWin(secondWindow, universalReplacement.id),
    "Universal replacement visible in the second window"
  );
  await TestUtils.waitForCondition(
    () => !getNotificationFromWin(firstWindow, globalMessage.id),
    "Original global infobar removed from the first window"
  );

  Assert.ok(
    dispatchUniversal.calledWith(
      sinon.match({
        type: "IMPRESSION",
        data: sinon.match.has("id", universalReplacement.id),
      })
    ),
    "Recorded impression for the universal replacement (first appearance only)"
  );

  // Cleanup
  removeByIdInWin(firstWindow, universalReplacement.id);
  removeByIdInWin(secondWindow, universalReplacement.id);
  removeByIdInWin(firstWindow, globalMessage.id);
  await BrowserTestUtils.closeWindow(secondWindow);
  sandbox.restore();
  cleanupInfobars();
});

add_task(async function global_replaces_universal_across_windows() {
  const sandbox = sinon.createSandbox();

  const firstWindow = BrowserWindowTracker.getTopWindow();
  const firstBrowser = firstWindow.gBrowser.selectedBrowser;
  const secondWindow = await BrowserTestUtils.openNewBrowserWindow();

  const universalOriginal = {
    id: "TEST_UNIVERSAL_ORIGINAL_FOR_GLOBAL_REPLACEMENT",
    content: {
      type: "universal",
      text: "original universal",
      buttons: [],
    },
  };

  const globalReplacement = {
    id: "TEST_GLOBAL_REPLACES_UNIVERSAL",
    content: {
      type: "global",
      text: "replacement global",
      buttons: [],
      canReplace: [universalOriginal.id],
    },
  };

  const dispatchUniversal = sandbox.stub();
  await InfoBar.showInfoBarMessage(
    firstBrowser,
    universalOriginal,
    dispatchUniversal
  );

  await TestUtils.waitForCondition(
    () => !!getNotificationFromWin(firstWindow, universalOriginal.id),
    "Original universal visible in the first window"
  );
  await TestUtils.waitForCondition(
    () => !!getNotificationFromWin(secondWindow, universalOriginal.id),
    "Original universal visible in the second window"
  );
  Assert.ok(
    dispatchUniversal.calledWith(
      sinon.match({
        type: "IMPRESSION",
        data: sinon.match.has("id", universalOriginal.id),
      })
    ),
    "Recorded impression for the original universal"
  );

  const originalObs = Services.obs;
  const removeSpy = sandbox.spy();
  Services.obs = {
    addObserver: originalObs.addObserver.bind(originalObs),
    notifyObservers: originalObs.notifyObservers.bind(originalObs),
    removeObserver(subject, topic) {
      removeSpy(subject, topic);
      return originalObs.removeObserver(subject, topic);
    },
  };
  registerCleanupFunction(() => {
    Services.obs = originalObs;
  });

  const dispatchGlobal = sandbox.stub();
  await InfoBar.showInfoBarMessage(
    firstBrowser,
    globalReplacement,
    dispatchGlobal
  );

  await TestUtils.waitForCondition(
    () => !getNotificationFromWin(firstWindow, universalOriginal.id),
    "Universal removed from the first window"
  );
  await TestUtils.waitForCondition(
    () => !getNotificationFromWin(secondWindow, universalOriginal.id),
    "Universal removed from the second window"
  );
  await TestUtils.waitForCondition(
    () => !!getNotificationFromWin(firstWindow, globalReplacement.id),
    "Global replacement visible in the first window"
  );
  Assert.ok(
    !getNotificationFromWin(secondWindow, globalReplacement.id),
    "Global replacement does NOT appear in the second window"
  );

  await TestUtils.waitForCondition(
    () =>
      removeSpy
        .getCalls()
        .some(c => c.args[0] === InfoBar && c.args[1] === "domwindowopened"),
    "removeObserver was invoked for domwindowopened"
  );

  Assert.ok(
    dispatchGlobal.calledWith(
      sinon.match({
        type: "IMPRESSION",
        data: sinon.match.has("id", globalReplacement.id),
      })
    ),
    "Recorded impression for the global replacement"
  );

  // Cleanup
  removeByIdInWin(firstWindow, globalReplacement.id);
  removeByIdInWin(firstWindow, universalOriginal.id);
  removeByIdInWin(secondWindow, universalOriginal.id);
  await BrowserTestUtils.closeWindow(secondWindow);
  sandbox.restore();
  cleanupInfobars();
});

add_task(async function universal_survives_originating_window_closure() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();
  let win2 = await BrowserTestUtils.openNewBrowserWindow();

  const incumbent = {
    id: "TEST_UNIVERSAL_HANDOVER_INCUMBENT",
    content: { type: "universal", text: "incumbent", buttons: [] },
  };
  const replacement = {
    id: "TEST_UNIVERSAL_HANDOVER_REPLACEMENT",
    content: {
      type: "universal",
      text: "replacement",
      buttons: [],
      canReplace: [incumbent.id],
    },
  };

  try {
    const incumbentNotification = await InfoBar.showInfoBarMessage(
      win2.gBrowser.selectedBrowser,
      incumbent,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, incumbent.id),
      "Incumbent visible in the surviving window"
    );

    await BrowserTestUtils.closeWindow(win2);
    win2 = null;

    Assert.equal(
      InfoBar._activeInfobar?.notification,
      incumbentNotification,
      "The active infobar still names the notification that owns the bars"
    );

    await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      replacement,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, replacement.id),
      "Replacement visible in the surviving window"
    );
    await TestUtils.waitForCondition(
      () => !getNotificationFromWin(win1, incumbent.id),
      "The incumbent was evicted rather than left stacked under the replacement"
    );
  } finally {
    removeByIdInWin(win1, replacement.id);
    removeByIdInWin(win1, incumbent.id);
    if (win2) {
      await BrowserTestUtils.closeWindow(win2);
    }
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function stale_unload_does_not_resurrect_replaced_infobar() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();
  let win2 = await BrowserTestUtils.openNewBrowserWindow();

  const first = {
    id: "TEST_UNIVERSAL_STALE_FIRST",
    content: { type: "universal", text: "first", buttons: [] },
  };
  const second = {
    id: "TEST_UNIVERSAL_STALE_SECOND",
    content: {
      type: "universal",
      text: "second",
      buttons: [],
      canReplace: [first.id],
    },
  };
  const third = {
    id: "TEST_UNIVERSAL_STALE_THIRD",
    content: {
      type: "universal",
      text: "third",
      buttons: [],
      canReplace: [second.id],
    },
  };

  try {
    // Shown from win2, so win2's unload listener closes over `first`.
    await InfoBar.showInfoBarMessage(
      win2.gBrowser.selectedBrowser,
      first,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, first.id),
      "First message visible in both windows"
    );

    const secondNotification = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      second,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win2, second.id),
      "Second message replaced the first"
    );

    await BrowserTestUtils.closeWindow(win2);
    win2 = null;

    Assert.equal(
      InfoBar._activeInfobar?.message?.id,
      second.id,
      "The stale unload left the replacement as the active infobar"
    );
    Assert.equal(
      InfoBar._activeInfobar?.notification,
      secondNotification,
      "The active infobar still names the replacement's notification"
    );

    await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      third,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, third.id),
      "Third message visible in the surviving window"
    );
    await TestUtils.waitForCondition(
      () => !getNotificationFromWin(win1, second.id),
      "The second message was evicted rather than left stacked"
    );
  } finally {
    removeByIdInWin(win1, third.id);
    removeByIdInWin(win1, second.id);
    removeByIdInWin(win1, first.id);
    if (win2) {
      await BrowserTestUtils.closeWindow(win2);
    }
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function stale_unload_does_not_clear_active_global_infobar() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();
  let win2 = await BrowserTestUtils.openNewBrowserWindow();

  const original = {
    id: "TEST_GLOBAL_STALE_ORIGINAL",
    content: { type: "global", text: "original", buttons: [] },
  };
  const replacement = {
    id: "TEST_GLOBAL_STALE_REPLACEMENT",
    content: {
      type: "global",
      text: "replacement",
      buttons: [],
      canReplace: [original.id],
    },
  };
  const unrelated = {
    id: "TEST_GLOBAL_STALE_UNRELATED",
    content: { type: "global", text: "unrelated", buttons: [] },
  };

  try {
    await InfoBar.showInfoBarMessage(
      win2.gBrowser.selectedBrowser,
      original,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win2, original.id),
      "Original visible in the window it was shown from"
    );

    const replacementNotification = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      replacement,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, replacement.id),
      "Replacement visible in the first window"
    );

    await BrowserTestUtils.closeWindow(win2);
    win2 = null;

    Assert.equal(
      InfoBar._activeInfobar?.notification,
      replacementNotification,
      "Closing the original's window left the replacement active"
    );

    const shown = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      unrelated,
      sandbox.stub()
    );
    Assert.equal(
      shown,
      null,
      "A message that replaces nothing is still refused"
    );
    Assert.ok(
      !getNotificationFromWin(win1, unrelated.id),
      "It did not stack under the replacement"
    );
  } finally {
    removeByIdInWin(win1, unrelated.id);
    removeByIdInWin(win1, replacement.id);
    if (win2) {
      await BrowserTestUtils.closeWindow(win2);
    }
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function reshown_universal_stays_tracked() {
  const sandbox = sinon.createSandbox();
  // Keep removal pending until the replacement is appended.
  await SpecialPowers.pushPrefEnv({ set: [["ui.prefersReducedMotion", 0]] });

  const win1 = BrowserWindowTracker.getTopWindow();
  const win2 = await BrowserTestUtils.openNewBrowserWindow();

  const message = {
    id: "TEST_UNIVERSAL_RESHOWN",
    content: {
      type: "universal",
      text: "reshown",
      buttons: [],
      canReplace: ["TEST_UNIVERSAL_RESHOWN"],
    },
  };

  try {
    await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      message,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win2, message.id),
      "First showing visible in both windows"
    );

    const reshown = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      message,
      sandbox.stub()
    );

    Assert.equal(
      InfoBar._activeInfobar?.notification,
      reshown,
      "The re-shown notification owns the active infobar"
    );
    Assert.equal(
      InfoBar._universalInfobars.length,
      2,
      "Both of the re-shown bars are tracked"
    );

    reshown.removeUniversalInfobars();
    await TestUtils.waitForCondition(
      () =>
        !getNotificationFromWin(win1, message.id) &&
        !getNotificationFromWin(win2, message.id),
      "The re-shown bars can still be removed"
    );
  } finally {
    removeByIdInWin(win1, message.id);
    removeByIdInWin(win2, message.id);
    await BrowserTestUtils.closeWindow(win2);
    await SpecialPowers.popPrefEnv();
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function universal_dismissed_from_later_window() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();

  const message = {
    id: "TEST_UNIVERSAL_DISMISS_LATER_WINDOW",
    content: { type: "universal", text: "later window", buttons: [] },
  };

  let win2;
  try {
    await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      message,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, message.id),
      "Visible in the window it was shown from"
    );

    win2 = await BrowserTestUtils.openNewBrowserWindow();
    // Mirror observe() for a newly opened window.
    await InfoBar.showInfoBarMessage(
      win2.gBrowser.selectedBrowser,
      message,
      sandbox.stub(),
      true
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win2, message.id),
      "Visible in the window that got its bar later"
    );

    getNotificationFromWin(win2, message.id).dismiss();

    await TestUtils.waitForCondition(
      () => !getNotificationFromWin(win2, message.id),
      "Dismissed in the window it was dismissed from"
    );
    await TestUtils.waitForCondition(
      () => !getNotificationFromWin(win1, message.id),
      "Dismissed in the other window too"
    );
    Assert.ok(
      !InfoBar._activeInfobar,
      "The active infobar was cleared by the dismissal"
    );
    Assert.ok(
      !InfoBar._observingWindowOpened,
      "The new window observer was unregistered"
    );
  } finally {
    removeByIdInWin(win1, message.id);
    if (win2) {
      removeByIdInWin(win2, message.id);
      await BrowserTestUtils.closeWindow(win2);
    }
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function universal_reaches_a_newly_opened_window() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();

  const message = {
    id: "TEST_UNIVERSAL_NEW_WINDOW",
    content: { type: "universal", text: "new window", buttons: [] },
  };

  let win2;
  try {
    await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      message,
      sandbox.stub()
    );

    win2 = await BrowserTestUtils.openNewBrowserWindow();
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win2, message.id),
      "Visible in the newly opened window"
    );
  } finally {
    removeByIdInWin(win1, message.id);
    if (win2) {
      removeByIdInWin(win2, message.id);
      await BrowserTestUtils.closeWindow(win2);
    }
    sandbox.restore();
    cleanupInfobars();
  }
});

add_task(async function failed_show_does_not_hold_the_active_slot() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();

  const doomed = {
    id: "TEST_UNIVERSAL_DOOMED",
    content: {
      type: "universal",
      text: "doomed",
      buttons: [],
      priority: 99,
    },
  };
  const followUp = {
    id: "TEST_UNIVERSAL_FOLLOW_UP",
    content: { type: "universal", text: "follow up", buttons: [] },
  };

  try {
    await Assert.rejects(
      InfoBar.showInfoBarMessage(
        win1.gBrowser.selectedBrowser,
        doomed,
        sandbox.stub()
      ),
      /Invalid notification priority/,
      "The show reports its failure"
    );

    Assert.ok(!InfoBar._activeInfobar, "The active infobar slot was released");
    Assert.equal(
      InfoBar._universalInfobars.length,
      0,
      "No universal infobars are left tracked"
    );

    const shown = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      followUp,
      sandbox.stub()
    );
    Assert.ok(shown, "A later message is not refused by the failed one");
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, followUp.id),
      "The later message is visible"
    );
  } finally {
    removeByIdInWin(win1, followUp.id);
    removeByIdInWin(win1, doomed.id);
    sandbox.restore();
    cleanupInfobars();
  }
});

// Pause the first append until release() is called, optionally with an error.
const holdFirstAppend = (sandbox, box) => {
  let release;
  const held = new Promise(resolve => {
    release = resolve;
  });
  const realAppend = box.appendNotification.bind(box);
  let calls = 0;
  sandbox.stub(box, "appendNotification").callsFake(async (...args) => {
    if (++calls > 1) {
      return realAppend(...args);
    }
    const rejection = await held;
    if (rejection) {
      throw rejection;
    }
    return realAppend(...args);
  });
  return release;
};

add_task(async function superseded_failed_show_spares_its_successor() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();

  const doomed = {
    id: "TEST_UNIVERSAL_SUPERSEDED_DOOMED",
    content: { type: "universal", text: "doomed", buttons: [] },
  };
  const successor = {
    id: "TEST_UNIVERSAL_SUPERSEDED_SUCCESSOR",
    content: {
      type: "universal",
      text: "successor",
      buttons: [],
      canReplace: [doomed.id],
    },
  };

  try {
    const release = holdFirstAppend(sandbox, win1.gNotificationBox);

    const pending = InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      doomed,
      sandbox.stub()
    );

    const successorNotification = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      successor,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, successor.id),
      "The successor took the slot while the first was still appending"
    );

    release(new Error("held append failed"));
    await Assert.rejects(
      pending,
      /held append failed/,
      "The superseded show reports its failure"
    );

    Assert.ok(
      getNotificationFromWin(win1, successor.id),
      "The successor's bar survived the failure"
    );
    Assert.equal(
      InfoBar._activeInfobar?.notification,
      successorNotification,
      "The successor still owns the active infobar"
    );
    Assert.equal(
      InfoBar._universalInfobars.length,
      1,
      "The successor's bar is still tracked"
    );
    Assert.ok(
      InfoBar._observingWindowOpened,
      "The successor's new window observer is still registered"
    );
  } finally {
    sandbox.restore();
    removeByIdInWin(win1, successor.id);
    removeByIdInWin(win1, doomed.id);
    cleanupInfobars();
  }
});

add_task(async function superseded_show_does_not_leave_its_bar_behind() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();

  const first = {
    id: "TEST_UNIVERSAL_SUPERSEDED_FIRST",
    content: { type: "universal", text: "first", buttons: [] },
  };
  const second = {
    id: "TEST_UNIVERSAL_SUPERSEDED_SECOND",
    content: {
      type: "universal",
      text: "second",
      buttons: [],
      canReplace: [first.id],
    },
  };

  try {
    const release = holdFirstAppend(sandbox, win1.gNotificationBox);

    const pending = InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      first,
      sandbox.stub()
    );

    const secondNotification = await InfoBar.showInfoBarMessage(
      win1.gBrowser.selectedBrowser,
      second,
      sandbox.stub()
    );
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win1, second.id),
      "The replacement took the slot while the first was still appending"
    );

    release();
    await pending;

    Assert.ok(
      !getNotificationFromWin(win1, first.id),
      "The superseded show did not leave its bar behind"
    );
    Assert.equal(
      InfoBar._activeInfobar?.notification,
      secondNotification,
      "The replacement still owns the active infobar"
    );
    Assert.equal(
      InfoBar._universalInfobars.length,
      1,
      "Only the replacement's bar is tracked"
    );
  } finally {
    sandbox.restore();
    removeByIdInWin(win1, first.id);
    removeByIdInWin(win1, second.id);
    cleanupInfobars();
  }
});

add_task(async function stale_entries_are_pruned_on_unload() {
  const win1 = BrowserWindowTracker.getTopWindow();
  let win2 = await BrowserTestUtils.openNewBrowserWindow();

  const message = {
    id: "TEST_UNIVERSAL_PRUNE_ON_UNLOAD",
    content: { type: "universal", text: "prune", buttons: [] },
  };

  try {
    // Shown from win2, so win2 owns the unload listener.
    await InfoBar.showInfoBarMessage(
      win2.gBrowser.selectedBrowser,
      message,
      () => {}
    );
    await TestUtils.waitForCondition(
      () => InfoBar._universalInfobars.length === 2,
      "One entry per window"
    );

    await BrowserTestUtils.closeWindow(win2);
    win2 = null;

    Assert.equal(
      InfoBar._universalInfobars.length,
      1,
      "The closing window's entry was pruned"
    );
    Assert.ok(
      InfoBar._activeInfobar,
      "The notification stays active while a bar survives"
    );
  } finally {
    removeByIdInWin(win1, message.id);
    if (win2) {
      await BrowserTestUtils.closeWindow(win2);
    }
    cleanupInfobars();
  }
});

add_task(async function closing_the_last_universal_window_clears_active() {
  const win1 = BrowserWindowTracker.getTopWindow();
  let win2 = await BrowserTestUtils.openNewBrowserWindow();

  const message = {
    id: "TEST_UNIVERSAL_LAST_WINDOW",
    content: { type: "universal", text: "last window", buttons: [] },
  };

  try {
    await InfoBar.showInfoBarMessage(
      win2.gBrowser.selectedBrowser,
      message,
      () => {}
    );
    await TestUtils.waitForCondition(
      () => InfoBar._universalInfobars.length === 2,
      "One entry per window"
    );

    // Leave win2 as the only window still showing a bar, so its unload has to
    // recognize that nothing survives it.
    removeByIdInWin(win1, message.id);
    InfoBar._universalInfobars = InfoBar._universalInfobars.filter(
      entry => entry.win !== win1
    );

    await BrowserTestUtils.closeWindow(win2);
    win2 = null;

    Assert.equal(
      InfoBar._activeInfobar,
      null,
      "The active infobar was released once no bar survived"
    );
  } finally {
    removeByIdInWin(win1, message.id);
    if (win2) {
      await BrowserTestUtils.closeWindow(win2);
    }
    cleanupInfobars();
  }
});

add_task(async function universal_reaches_a_window_still_loading() {
  const sandbox = sinon.createSandbox();
  const win1 = BrowserWindowTracker.getTopWindow();

  const message = {
    id: "TEST_UNIVERSAL_LOADING_WINDOW",
    content: { type: "universal", text: "loading window", buttons: [] },
  };

  // Open this window before showing the message so the new-window observer
  // cannot handle it.
  let win2 = win1.OpenBrowserWindow();
  const started = TestUtils.topicObserved(
    "browser-delayed-startup-finished",
    subject => subject === win2
  );
  try {
    await BrowserTestUtils.waitForEvent(win2, "DOMContentLoaded");
    Assert.equal(
      win2.document.readyState,
      "interactive",
      "The new window is still loading"
    );

    // Exclude the harness window so enumeration reaches win2 before yielding.
    // This models startup, when the loading window is the only eligible one.
    win1.document.documentElement.setAttribute("taskbartab", "true");
    try {
      await InfoBar.showInfoBarMessage(
        win2.gBrowser.selectedBrowser,
        message,
        sandbox.stub()
      );
    } finally {
      win1.document.documentElement.removeAttribute("taskbartab");
    }

    await started;
    await TestUtils.waitForCondition(
      () => !!getNotificationFromWin(win2, message.id),
      "Shown in the window once it loaded"
    );
  } finally {
    await started;
    removeByIdInWin(win1, message.id);
    removeByIdInWin(win2, message.id);
    await BrowserTestUtils.closeWindow(win2);
    sandbox.restore();
    cleanupInfobars();
  }
});
