/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { LoginTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/LoginTestUtils.sys.mjs"
);

const { LoginBreaches } = ChromeUtils.importESModule(
  "resource:///modules/LoginBreaches.sys.mjs"
);

const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);

const { OSKeyStoreTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/OSKeyStoreTestUtils.sys.mjs"
);

const { OSKeyStore } = ChromeUtils.importESModule(
  "resource://gre/modules/OSKeyStore.sys.mjs"
);

const nsLoginInfo = new Components.Constructor(
  "@mozilla.org/login-manager/loginInfo;1",
  Ci.nsILoginInfo,
  "init"
);

const gBrowserGlue = Cc["@mozilla.org/browser/browserglue;1"].getService(
  Ci.nsIObserver
);

ChromeUtils.defineESModuleGetters(this, {
  LoginBreaches: "resource:///modules/LoginBreaches.sys.mjs",
});

const BREACH_EXAMPLE = {
  AddedDate: "2018-12-20T23:56:26Z",
  BreachDate: "2018-12-16",
  Domain: "breached.com",
  Name: "breached",
  PwnCount: 1643100,
  DataClasses: ["Email addresses", "Usernames", "Passwords", "IP addresses"],
  _status: "synced",
  id: "047940fe-d2fd-4314-b636-b4a952ee0044",
  last_modified: "1541615610052",
  schema: "1541615609018",
};

const BREACHED_LOGIN = LoginTestUtils.testData.formLogin({
  origin: "https://www.breached.com",
  formActionOrigin: "https://www.breached.com",
  username: "username",
  password: "password",
  timePasswordChanged: new Date("2018-12-15").getTime(),
});

const TEST_LOGIN_1 = LoginTestUtils.testData.formLogin({
  username: "bob",
  password: "pass1",
  origin: "https://example1.com",
});

const TEST_LOGIN_2 = LoginTestUtils.testData.formLogin({
  username: "sally",
  password: "pass2",
  origin: "https://example2.com",
});

const TEST_LOGIN_3 = LoginTestUtils.testData.formLogin({
  username: "ned",
  password: "pass3",
  origin: "https://example3.com",
});

const DEFAULT_MOCK_LOGINS = [TEST_LOGIN_1, TEST_LOGIN_2, TEST_LOGIN_3];

class MockView {
  #snapshots = [];

  get snapshots() {
    return this.#snapshots;
  }

  set snapshots(snapshots) {
    this.#snapshots = snapshots;
  }

  messageFromViewModel(message, args) {
    const functionName = `receive${message}`;
    this[functionName]?.(args);

    info(`${functionName} message received by view.`);
  }

  async receiveShowSnapshots({ snapshots }) {
    this.snapshots = snapshots;
  }
}

async function addMockPasswords() {
  info("Adding mock passwords");

  for (let login of DEFAULT_MOCK_LOGINS) {
    info(`Saving login: ${login.username}, ${login.password}, ${login.origin}`);
    await LoginTestUtils.addLogin(login);
  }
}

async function addBreach() {
  info("Adding breach");
  async function emitSync() {
    await RemoteSettings(LoginBreaches.REMOTE_SETTINGS_COLLECTION).emit(
      "sync",
      { data: { current: [BREACH_EXAMPLE] } }
    );
  }

  gBrowserGlue.observe(null, "browser-glue-test", "add-breaches-sync-handler");
  const db = RemoteSettings(LoginBreaches.REMOTE_SETTINGS_COLLECTION).db;
  await db.importChanges({}, Date.now(), [BREACH_EXAMPLE]);
  await emitSync();
}

async function openPasswordsSidebar() {
  info("Open Passwords sidebar");
  await SidebarController.show("viewCPMSidebar");
  const sidebar = document.getElementById("sidebar");
  const megalist =
    sidebar.contentDocument.querySelector("megalist-alpha").shadowRoot;
  return megalist;
}

async function checkAllLoginsRendered(megalist) {
  info("Check that all logins are rendered.");
  const logins = await Services.logins.getAllLogins();
  await BrowserTestUtils.waitForCondition(() => {
    const passwordsList = megalist.querySelector(".passwords-list");
    return (
      passwordsList?.querySelectorAll("password-card").length === logins.length
    );
  }, "Password cards failed to render");

  ok(true, `${logins.length} password cards are rendered.`);
}

async function addLocalOriginLogin() {
  LoginTestUtils.addLogin({
    username: "john",
    password: "pass4",
    origin: "about:preferences#privacy",
  });
}

function waitForNotification(megalist, notificationId) {
  info(`Wait for notification with id ${notificationId}.`);
  const notifcationPromise = BrowserTestUtils.waitForCondition(() => {
    const notifMsgBars = Array.from(
      megalist.querySelectorAll("notification-message-bar")
    );
    return notifMsgBars?.find(
      notifMsgBar => notifMsgBar.notification.id === notificationId
    );
  }, `Notification with id ${notificationId} did not render.`);
  return notifcationPromise;
}

async function checkNotificationAndTelemetry(
  megalist,
  notificationId,
  eventIndex = 0
) {
  const notificationMessageBar = await waitForNotification(
    megalist,
    notificationId
  );
  const events = Glean.contextualManager.notificationShown.testGetValue();
  assertCPMGleanEvent(events[eventIndex], {
    notification_detail: notificationId.replaceAll("-", "_"),
  });
  return notificationMessageBar;
}

async function checkNotificationInteractionTelemetry(
  notifMsgBar,
  buttonId,
  expectedEvent,
  eventIndex = 0
) {
  info(`Click on ${buttonId} button and test telemetry.`);
  const mozMessageBar = notifMsgBar.shadowRoot.querySelector("moz-message-bar");
  const element = mozMessageBar.querySelector(`#${buttonId}`);
  element.click();
  const events = Glean.contextualManager.notificationInteraction.testGetValue();
  assertCPMGleanEvent(events[eventIndex], expectedEvent);
}

function setInputValue(loginForm, fieldElement, value) {
  info(`Filling ${fieldElement} with value '${value}'.`);
  const field = loginForm.shadowRoot.querySelector(fieldElement);
  field.input.value = value;
  field.input.dispatchEvent(
    new InputEvent("input", {
      composed: true,
      bubbles: true,
    })
  );
}

function getMegalistParent() {
  const megalistChromeWindow = gBrowser.ownerGlobal[0];
  return megalistChromeWindow.browsingContext.currentWindowGlobal.getActor(
    "Megalist"
  );
}

async function waitForReauth(callBackFn) {
  const authExpirationTime = getMegalistParent().authExpirationTime();
  let reauthObserved = Promise.resolve();

  if (OSKeyStore.canReauth() && Date.now() > authExpirationTime) {
    reauthObserved = OSKeyStoreTestUtils.waitForOSKeyStoreLogin(true);
  }
  await callBackFn();
  return reauthObserved;
}

function waitForPasswordReveal(passwordLine) {
  const revealBtnPromise = BrowserTestUtils.waitForMutationCondition(
    passwordLine.loginLine,
    {
      attributeFilter: ["inputtype"],
    },
    () => passwordLine.loginLine.getAttribute("inputtype") === "text"
  );
  info("click on reveal button");
  passwordLine.revealBtn.click();
  return revealBtnPromise;
}

function waitForSnapshots() {
  info("Wait for headers.");
  const sidebar = document.getElementById("sidebar");
  const megalistComponent =
    sidebar.contentDocument.querySelector("megalist-alpha");
  return BrowserTestUtils.waitForCondition(
    () => megalistComponent.header,
    "Megalist header loaded."
  );
}

async function checkEmptyState(selector, megalist) {
  return await BrowserTestUtils.waitForCondition(() => {
    const emptyStateCard = megalist.querySelector(".empty-state-card");
    return !!emptyStateCard?.querySelector(selector);
  }, "Empty state card failed to render");
}

function assertCPMGleanEvent(actualEvent, expectedEvent) {
  info("Asserting CPM Glean event");

  for (let key of Object.keys(expectedEvent)) {
    Assert.equal(
      actualEvent.extra[key],
      expectedEvent[key],
      `${actualEvent.extra[key]} is the recorded ${key}.
        Expected: '${expectedEvent[key]}'.`
    );
  }
}

async function resetTelemetryIfKeyStoreTestable() {
  if (!OSKeyStoreTestUtils.canTestOSKeyStoreLogin()) {
    ok(true, "Cannot test OSAuth.");
    return false;
  }
  Services.fog.testResetFOG();
  await Services.fog.testFlushAllChildren();
  return true;
}

function mockServicePrompt(buttonClicked = 0, checked = true) {
  return {
    async asyncConfirmEx(
      browsingContext,
      win,
      title,
      message,
      _flags,
      _button0,
      _button1,
      _button2,
      _checkLabel,
      _checkValue,
      _extraArgs
    ) {
      info(`Prompt title ${title}`);
      info(`Prompt message ${message}`);
      return {
        // eslint-disable-next-line mozilla/use-chromeutils-generateqi
        QueryInterface() {
          const propBag = new Map();
          propBag.set("buttonNumClicked", buttonClicked);
          propBag.set("checked", checked);
          return propBag;
        },
      };
    },
  };
}
