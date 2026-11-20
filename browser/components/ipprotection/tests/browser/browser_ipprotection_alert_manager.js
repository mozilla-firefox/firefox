/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "ipProtectionLocalization", () => {
  return new Localization(["browser/ipProtection.ftl"], true);
});
const { BANDWIDTH } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-constants.mjs"
);

function setState(state) {
  IPPProxyManager.dispatchEvent(
    new CustomEvent("IPPProxyManager:StateChanged", {
      bubbles: true,
      composed: true,
      detail: {
        state,
      },
    })
  );
}

DEFAULT_EXPERIMENT = null;

add_task(async function test_ipprotection_prompts() {
  IPProtectionAlertManager.init();
  setupService({
    isReady: true,
    canEnroll: true,
  });

  IPProtectionService.updateState();

  await IPPProxyManager.start();
  await TestUtils.waitForCondition(
    () => IPPProxyManager.state === IPPProxyStates.ACTIVE,
    "Wait for the proxy state to be active"
  );

  const usage = makeUsage();
  const maxUsage = Number(usage.max) / BANDWIDTH.BYTES_IN_GB;

  const [pausedTitle, pausedBody, closeTabsButton, continueButton] =
    lazy.ipProtectionLocalization.formatMessagesSync([
      { id: "vpn-paused-alert-title" },
      { id: "vpn-paused-alert-body", args: { maxUsage } },
      { id: "vpn-paused-alert-close-tabs-button" },
      { id: "vpn-paused-alert-continue-wo-vpn-button" },
    ]);

  const localizationMessages = {
    pausedTitle: pausedTitle.value,
    pausedBody: pausedBody.value,
    closeTabsButton: closeTabsButton.value,
    continueButton: continueButton.value,
  };

  setState(IPPProxyStates.PAUSED);

  await TestUtils.waitForCondition(
    () => window.gDialogBox.isOpen,
    "Wait for the dialog to exist"
  );

  Assert.ok(window.gDialogBox.isOpen, "Dialog exists and is open");

  await TestUtils.waitForCondition(
    () =>
      window.gDialogBox.dialog._frame.contentDocument.getElementById(
        "titleContainer"
      ),
    "Wait for the dialog to load"
  );

  let dialogDoc = window.gDialogBox.dialog._frame.contentDocument;

  Assert.equal(
    dialogDoc.getElementById("titleContainer").textContent,
    localizationMessages.pausedTitle,
    "Dialog has paused title"
  );

  Assert.equal(
    dialogDoc.getElementById("infoBody").textContent,
    localizationMessages.pausedBody,
    "Dialog has paused body"
  );

  Assert.equal(
    dialogDoc
      .getElementById("commonDialog")
      .shadowRoot.querySelector("button[dlgtype='accept']").label,
    localizationMessages.continueButton,
    "Dialog has continue button label"
  );

  Assert.equal(
    dialogDoc
      .getElementById("commonDialog")
      .shadowRoot.querySelector("button[dlgtype='cancel']").label,
    localizationMessages.closeTabsButton,
    "Dialog has cancel button label"
  );

  await IPPProxyManager.stop();

  await TestUtils.waitForCondition(
    () => !window.gDialogBox.isOpen,
    "Wait for the dialog to not exist"
  );

  Assert.ok(!window.gDialogBox.isOpen, "Dialog disappears when in ready state");

  await TestUtils.waitForTick();

  await IPPProxyManager.start();
  await TestUtils.waitForCondition(
    () => IPPProxyManager.state === IPPProxyStates.ACTIVE,
    "Wait for the proxy state to be active"
  );

  setState(IPPProxyStates.ERROR);
  await TestUtils.waitForTick();

  Assert.ok(
    !window.gDialogBox.isOpen,
    "No dialog opens in the error state, the error page explains it"
  );

  await IPPProxyManager.stop();
  cleanupService();
  IPProtectionAlertManager.uninit();
});

add_task(async function test_continue_without_vpn() {
  IPProtectionAlertManager.init();
  setupService({
    isReady: true,
    canEnroll: true,
  });
  let cleanupAlpha = await setupExperiment({ enabled: true, variant: "alpha" });
  IPPProxyManager.updateState();

  await TestUtils.waitForCondition(
    () => IPPProxyManager.state === IPPProxyStates.READY,
    "Wait for the proxy state to be ready"
  );

  await IPPProxyManager.start();

  await TestUtils.waitForCondition(
    () => IPPProxyManager.state === IPPProxyStates.ACTIVE,
    "Wait for the proxy state to be active"
  );

  Assert.equal(IPPProxyManager.state, IPPProxyStates.ACTIVE, "Proxy is active");

  BrowserTestUtils.addTab(gBrowser, "about:robots");
  BrowserTestUtils.addTab(gBrowser, "about:robots");
  BrowserTestUtils.addTab(gBrowser, "about:robots");

  // Force paused prompt to open
  setState(IPPProxyStates.PAUSED);

  await TestUtils.waitForCondition(
    () => window.gDialogBox.isOpen,
    "Wait for the dialog to exist"
  );

  Assert.ok(window.gDialogBox.isOpen, "Dialog exists and is open");

  await TestUtils.waitForCondition(
    () =>
      window.gDialogBox.dialog._frame.contentDocument.getElementById(
        "titleContainer"
      ),
    "Wait for the dialog to load"
  );

  let dialogDoc = window.gDialogBox.dialog._frame.contentDocument;

  dialogDoc
    .getElementById("commonDialog")
    .shadowRoot.querySelector("button[dlgtype='accept']")
    .click();

  await TestUtils.waitForCondition(
    () => !window.gDialogBox.isOpen,
    "Wait for the dialog to not exist"
  );

  Assert.ok(!window.gDialogBox.isOpen, "Dialog disappears after button click");

  await TestUtils.waitForCondition(() => {
    info(`State is: ${IPPProxyManager.state}`);
    return IPPProxyManager.state === IPPProxyStates.READY;
  }, "Wait for the proxy state to be ready");

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "IPPProxyManager is in the ready state"
  );

  await TestUtils.waitForTick();

  await TestUtils.waitForCondition(
    () => gBrowser.tabs.length === 4,
    "Should have 4 tabs"
  );

  Assert.equal(gBrowser.tabs.length, 4, "Should have 4 tabs");

  await cleanupAlpha();
  await cleanupExperiment();
  cleanupService();
});

add_task(async function test_close_all_tabs() {
  IPProtectionAlertManager.init();
  setupService({
    isReady: true,
    canEnroll: true,
  });
  let cleanupAlpha = await setupExperiment({ enabled: true, variant: "alpha" });
  IPPProxyManager.updateState();

  await TestUtils.waitForCondition(
    () => IPPProxyManager.state === IPPProxyStates.READY,
    "Wait for the proxy state to be ready"
  );

  await IPPProxyManager.start();

  await TestUtils.waitForCondition(
    () => IPPProxyManager.state === IPPProxyStates.ACTIVE,
    "Wait for the proxy state to be active"
  );

  Assert.equal(IPPProxyManager.state, IPPProxyStates.ACTIVE, "Proxy is active");

  BrowserTestUtils.addTab(gBrowser, "about:robots");
  BrowserTestUtils.addTab(gBrowser, "about:robots");
  BrowserTestUtils.addTab(gBrowser, "about:robots");

  // Force paused prompt to open
  setState(IPPProxyStates.PAUSED);

  await TestUtils.waitForCondition(
    () => window.gDialogBox.isOpen,
    "Wait for the dialog to exist"
  );

  Assert.ok(window.gDialogBox.isOpen, "Dialog exists and is open");

  await TestUtils.waitForCondition(
    () =>
      window.gDialogBox.dialog._frame.contentDocument.getElementById(
        "titleContainer"
      ),
    "Wait for the dialog to load"
  );

  let dialogDoc = window.gDialogBox.dialog._frame.contentDocument;

  dialogDoc
    .getElementById("commonDialog")
    .shadowRoot.querySelector("button[dlgtype='cancel']")
    .click();

  await TestUtils.waitForCondition(
    () => !window.gDialogBox.isOpen,
    "Wait for the dialog to not exist"
  );

  Assert.ok(!window.gDialogBox.isOpen, "Dialog disappears after button click");

  await TestUtils.waitForCondition(() => {
    info(`State is: ${IPPProxyManager.state}`);
    return IPPProxyManager.state === IPPProxyStates.READY;
  }, "Wait for the proxy state to be ready");

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "IPPProxyManager is in the ready state"
  );

  await TestUtils.waitForCondition(
    () => gBrowser.tabs.length === 1,
    "Wait for only 1 tab open"
  );
  await TestUtils.waitForCondition(
    () => gBrowser.currentURI.displaySpec === "about:home",
    "Wait for currentURI to be about:home"
  );

  Assert.equal(gBrowser.tabs.length, 1, "Only 1 tab remains open");
  Assert.equal(
    gBrowser.currentURI.displaySpec,
    "about:home",
    "The current uri is about:home"
  );

  await cleanupAlpha();
  await cleanupExperiment();
  cleanupService();
});

add_task(
  async function test_telemetry_alert_button_clicked_paused_continue_without_vpn() {
    Services.fog.testResetFOG();
    await Services.fog.testFlushAllChildren();

    IPProtectionAlertManager.init();
    setupService({
      isReady: true,
      canEnroll: true,
    });

    IPProtectionService.updateState();

    await IPPProxyManager.start();

    await TestUtils.waitForCondition(
      () => IPPProxyManager.state === IPPProxyStates.ACTIVE,
      "Wait for the proxy state to be active"
    );

    setState(IPPProxyStates.PAUSED);

    await TestUtils.waitForCondition(
      () => window.gDialogBox.isOpen,
      "Wait for the dialog to exist"
    );

    await TestUtils.waitForCondition(
      () =>
        window.gDialogBox.dialog._frame.contentDocument.getElementById(
          "titleContainer"
        ),
      "Wait for the dialog to load"
    );

    let dialogDoc = window.gDialogBox.dialog._frame.contentDocument;

    dialogDoc
      .getElementById("commonDialog")
      .shadowRoot.querySelector("button[dlgtype='accept']")
      .click();

    await TestUtils.waitForCondition(
      () => !window.gDialogBox.isOpen,
      "Wait for the dialog to close"
    );

    await Services.fog.testFlushAllChildren();

    let alertEvents = Glean.ipprotection.alertButtonClicked.testGetValue();
    Assert.equal(
      alertEvents.length,
      1,
      "Should have recorded one alertButtonClicked event"
    );
    Assert.equal(alertEvents[0].category, "ipprotection");
    Assert.equal(alertEvents[0].name, "alert_button_clicked");
    Assert.equal(alertEvents[0].extra.reason, "paused");
    Assert.equal(alertEvents[0].extra.buttonType, "0");

    cleanupService();
    IPProtectionAlertManager.uninit();
    Services.fog.testResetFOG();
  }
);

add_task(async function test_only_when_proxy_active() {
  IPProtectionAlertManager.init();

  setState(IPPProxyStates.PAUSED);

  await TestUtils.waitForTick();

  Assert.ok(
    !window.gDialogBox.isOpen,
    "Paused dialog does not open when proxy is not active"
  );

  IPProtectionAlertManager.uninit();
});
