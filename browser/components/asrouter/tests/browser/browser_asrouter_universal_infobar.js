/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { InfoBar } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/InfoBar.sys.mjs"
);
const { CFRMessageProvider } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/CFRMessageProvider.sys.mjs"
);
const { ASRouter } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouter.sys.mjs"
);

const UNIVERSAL_MESSAGE = {
  id: "universal-infobar",
  content: {
    type: "universal",
    text: "t",
    buttons: [],
  },
};

const cleanupInfobars = () => {
  InfoBar._universalInfobars = [];
  InfoBar._activeInfobar = null;
};

add_task(async function showNotificationAllWindows() {
  let fakeNotification = { showNotification: sinon.stub().resolves() };
  let fakeWins = [
    { gBrowser: { selectedBrowser: "win1" } },
    { gBrowser: { selectedBrowser: "win2" } },
    { gBrowser: { selectedBrowser: "win3" } },
  ];

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
  Assert.ok(fakeNotification.showNotification.calledWith("win1"));
  Assert.ok(fakeNotification.showNotification.calledWith("win2"));
  Assert.ok(fakeNotification.showNotification.calledWith("win3"));

  // Cleanup
  cleanupInfobars();
  sinon.restore();
  Object.defineProperty(Services, "wm", {
    value: origWinManager,
    configurable: true,
    writable: true,
  });
});

add_task(async function removeUniversalInfobars() {
  let browser = BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser;
  let origBox = browser.ownerGlobal.gNotificationBox;
  browser.ownerGlobal.gNotificationBox = {
    appendNotification: sinon.stub().resolves({}),
    removeNotification: sinon.stub(),
  };

  sinon
    .stub(InfoBar, "showNotificationAllWindows")
    .callsFake(async notification => {
      await notification.showNotification(browser);
    });

  let notification = await InfoBar.showInfoBarMessage(
    browser,
    UNIVERSAL_MESSAGE,
    sinon.stub()
  );

  Assert.equal(InfoBar._universalInfobars.length, 1);
  notification.removeUniversalInfobars();

  Assert.ok(
    browser.ownerGlobal.gNotificationBox.removeNotification.calledWith(
      notification.notification
    )
  );

  Assert.deepEqual(InfoBar._universalInfobars, []);

  // Cleanup
  cleanupInfobars();
  browser.ownerGlobal.gNotificationBox = origBox;
  sinon.restore();
});

add_task(async function initialUniversal_showsAllWindows_andSendsTelemetry() {
  let browser = BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser;
  let origBox = browser.ownerGlobal.gNotificationBox;
  browser.ownerGlobal.gNotificationBox = {
    appendNotification: sinon.stub().resolves({}),
    removeNotification: sinon.stub(),
  };

  let showAll = sinon
    .stub(InfoBar, "showNotificationAllWindows")
    .callsFake(async notification => {
      await notification.showNotification(browser);
    });

  let dispatch1 = sinon.stub();
  let dispatch2 = sinon.stub();

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
  browser.ownerGlobal.gNotificationBox = origBox;
  sinon.restore();
  Services.obs.removeObserver(InfoBar, "domwindowopened");
});

add_task(async function observe_domwindowopened_withLoadEvent() {
  let stub = sinon.stub(InfoBar, "showInfoBarMessage").resolves();

  InfoBar._activeInfobar = {
    message: { content: { type: "universal" } },
    dispatch: sinon.stub(),
  };

  let subject = {
    document: { readyState: "loading" },
    gBrowser: { selectedBrowser: "b" },
    addEventListener(event, cb) {
      subject.document.readyState = "complete";
      cb();
    },
  };

  InfoBar.observe(subject, "domwindowopened");

  Assert.ok(stub.calledOnce);
  // Called with universalInNewWin true
  Assert.equal(stub.firstCall.args[3], true);

  // Cleanup
  cleanupInfobars();
  sinon.restore();
});

add_task(async function observe_domwindowopened() {
  let stub = sinon.stub(InfoBar, "showInfoBarMessage").resolves();

  InfoBar._activeInfobar = {
    message: { content: { type: "universal" } },
    dispatch: sinon.stub(),
  };

  let win = BrowserWindowTracker.getTopWindow();
  InfoBar.observe(win, "domwindowopened");

  Assert.ok(stub.calledOnce);
  Assert.equal(stub.firstCall.args[3], true);

  // Cleanup
  cleanupInfobars();
  sinon.restore();
});

add_task(async function observe_skips_nonUniversal() {
  let stub = sinon.stub(InfoBar, "showInfoBarMessage").resolves();
  InfoBar._activeInfobar = {
    message: { content: { type: "global" } },
    dispatch: sinon.stub(),
  };
  InfoBar.observe({}, "domwindowopened");
  Assert.ok(stub.notCalled);

  // Cleanup
  cleanupInfobars();
  stub.restore();
});

add_task(async function infobarCallback_dismissed_universal() {
  const browser = BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser;
  const dispatch = sinon.stub();

  sinon
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
  sinon.restore();
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

  // Cleanup
  Services.obs = origObs;
  sandbox.restore();
  cleanupInfobars();
});
