const PROXY_PREF = "network.proxy.type";

XPCOMUtils.defineLazyPreferenceGetter(this, "proxyType", PROXY_PREF);

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
AddonTestUtils.initMochitest(this);

const PERMISSIONS_URL =
  "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml";
let sitePermissionsDialog;

function waitForMessageChange(
  element,
  cb,
  opts = { attributes: true, attributeFilter: ["hidden"] }
) {
  return waitForMutation(element, opts, cb);
}

function getElement(id, doc = gBrowser.contentDocument) {
  return doc.getElementById(id);
}

function waitForMessageHidden(messageId, doc) {
  return waitForMessageChange(
    getElement(messageId, doc),
    target => target.hidden
  );
}

function waitForMessageShown(messageId, doc) {
  return waitForMessageChange(
    getElement(messageId, doc),
    target => !target.hidden
  );
}

function waitForEnableMessage(messageId, doc) {
  return waitForMessageChange(
    getElement(messageId, doc),
    target => target.classList.contains("extension-controlled-disabled"),
    { attributeFilter: ["class"], attributes: true }
  );
}

async function openNotificationsPermissionDialog() {
  let dialogOpened = promiseLoadSubDialog(PERMISSIONS_URL);

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], function () {
    let doc = content.document;
    let settingsButton = doc.getElementById("notificationSettingsButton");
    settingsButton.click();
  });

  sitePermissionsDialog = await dialogOpened;
  await sitePermissionsDialog.document.mozSubdialogReady;
}

async function disableExtensionViaClick(labelId, disableButtonId, doc) {
  let controlledLabel = doc.getElementById(labelId);

  let enableMessageShown = waitForEnableMessage(labelId, doc);
  doc.getElementById(disableButtonId).click();
  await enableMessageShown;

  let controlledDescription = controlledLabel.querySelector("description");
  is(
    doc.l10n.getAttributes(controlledDescription.querySelector("label")).id,
    "extension-controlled-enable",
    "The user is notified of how to enable the extension again."
  );

  // The user can dismiss the enable instructions.
  let hidden = waitForMessageHidden(labelId, doc);
  controlledLabel.querySelector("image:last-of-type").click();
  await hidden;
}

async function reEnableExtension(addon, labelId) {
  let controlledMessageShown = waitForMessageShown(labelId);
  await addon.enable();
  await controlledMessageShown;
}

// Bug 2084341 - reenable this test when we add support for extensioncontrolled
// to the new ETP setting
add_task(async function testExtensionControlledWebNotificationsPermission() {
  let manifest = {
    manifest_version: 2,
    name: "TestExtension",
    version: "1.0",
    description: "Testing WebNotificationsDisable",
    browser_specific_settings: { gecko: { id: "@web_notifications_disable" } },
    permissions: ["browserSettings"],
    browser_action: {
      default_title: "Testing",
    },
  };

  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  await openNotificationsPermissionDialog();

  let doc = sitePermissionsDialog.document;
  let extensionControlledContent = doc.getElementById(
    "browserNotificationsPermissionExtensionContent"
  );

  // Test that extension content is initially hidden.
  ok(
    extensionControlledContent.hidden,
    "Extension content is initially hidden"
  );

  // Install an extension that will disable web notifications permission.
  let messageShown = waitForMessageShown(
    "browserNotificationsPermissionExtensionContent",
    doc
  );
  let extension = ExtensionTestUtils.loadExtension({
    manifest,
    useAddonManager: "permanent",
    background() {
      browser.browserSettings.webNotificationsDisabled.set({ value: true });
      browser.test.sendMessage("load-extension");
    },
  });
  await extension.startup();
  await extension.awaitMessage("load-extension");
  await messageShown;

  let controlledDesc = extensionControlledContent.querySelector("description");
  Assert.deepEqual(
    doc.l10n.getAttributes(controlledDesc),
    {
      id: "extension-controlling-web-notifications",
      args: {
        name: "TestExtension",
      },
    },
    "The user is notified that an extension is controlling the web notifications permission"
  );
  is(
    extensionControlledContent.hidden,
    false,
    "The extension controlled row is not hidden"
  );

  // Disable the extension.
  doc.getElementById("disableNotificationsPermissionExtension").click();

  // Verify the user is notified how to enable the extension.
  await waitForEnableMessage(extensionControlledContent.id, doc);
  is(
    doc.l10n.getAttributes(controlledDesc.querySelector("label")).id,
    "extension-controlled-enable",
    "The user is notified of how to enable the extension again"
  );

  // Verify the enable message can be dismissed.
  let hidden = waitForMessageHidden(extensionControlledContent.id, doc);
  let dismissButton = controlledDesc.querySelector("image:last-of-type");
  dismissButton.click();
  await hidden;

  // Verify that the extension controlled content in hidden again.
  is(
    extensionControlledContent.hidden,
    true,
    "The extension controlled row is now hidden"
  );

  // Close the site permissions dialog and wait for it to finish closing
  let dialogClosed = BrowserTestUtils.waitForEvent(
    gBrowser.contentWindow.gSubDialog._dialogStack,
    "dialogclose"
  );
  sitePermissionsDialog.document
    .querySelector("dialog")
    .getButton("cancel")
    .click();
  await dialogClosed;
  sitePermissionsDialog = null;

  // Wait a tick to allow cleanup to complete
  await new Promise(resolve => Services.tm.dispatchToMainThread(resolve));

  await extension.unload();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function testExtensionControlledPasswordManager() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.settings-redesign.enabled", false]],
  });
  const PASSWORD_MANAGER_ENABLED_PREF = "signon.rememberSignons";
  const PASSWORD_MANAGER_ENABLED_DEFAULT = true;
  const CONTROLLED_BUTTON_ID = "disablePasswordManagerExtension";
  const CONTROLLED_LABEL_ID = "passwordManagerExtensionContent";
  const EXTENSION_ID = "@remember_signons";
  let manifest = {
    manifest_version: 2,
    name: "testPasswordManagerExtension",
    version: "1.0",
    description: "Testing rememberSignons",
    browser_specific_settings: { gecko: { id: EXTENSION_ID } },
    permissions: ["privacy"],
    browser_action: {
      default_title: "Testing rememberSignons",
    },
  };

  let passwordManagerEnabledPref = () =>
    Services.prefs.getBoolPref(PASSWORD_MANAGER_ENABLED_PREF);

  await SpecialPowers.pushPrefEnv({
    set: [[PASSWORD_MANAGER_ENABLED_PREF, PASSWORD_MANAGER_ENABLED_DEFAULT]],
  });
  is(
    passwordManagerEnabledPref(),
    true,
    "Password manager is enabled by default."
  );

  function verifyState(isControlled) {
    is(
      passwordManagerEnabledPref(),
      !isControlled,
      "Password manager pref is set to the expected value."
    );
    let controlledLabel =
      gBrowser.contentDocument.getElementById(CONTROLLED_LABEL_ID);
    let controlledButton =
      gBrowser.contentDocument.getElementById(CONTROLLED_BUTTON_ID);
    is(
      controlledLabel.hidden,
      !isControlled,
      "The extension's controlled row visibility is as expected."
    );
    is(
      controlledButton.hidden,
      !isControlled,
      "The extension's controlled button visibility is as expected."
    );
    if (isControlled) {
      let controlledDesc = controlledLabel.querySelector("description");
      Assert.deepEqual(
        gBrowser.contentDocument.l10n.getAttributes(controlledDesc),
        {
          id: "extension-controlling-password-saving",
          args: {
            name: "testPasswordManagerExtension",
          },
        },
        "The user is notified that an extension is controlling the remember signons pref."
      );
    }
  }

  await openPreferencesViaOpenPreferencesAPI("panePrivacy", {
    leaveOpen: true,
  });

  info("Verify that no extension is controlling the password manager pref.");
  verifyState(false);

  let extension = ExtensionTestUtils.loadExtension({
    manifest,
    useAddonManager: "permanent",
    background() {
      browser.privacy.services.passwordSavingEnabled.set({ value: false });
    },
  });
  let messageShown = waitForMessageShown(CONTROLLED_LABEL_ID);
  await extension.startup();
  await messageShown;

  info(
    "Verify that the test extension is controlling the password manager pref."
  );
  verifyState(true);

  info("Verify that the extension shows as controlled when loaded again.");
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await openPreferencesViaOpenPreferencesAPI("panePrivacy", {
    leaveOpen: true,
  });
  verifyState(true);

  await disableExtensionViaClick(
    CONTROLLED_LABEL_ID,
    CONTROLLED_BUTTON_ID,
    gBrowser.contentDocument
  );

  info(
    "Verify that disabling the test extension removes the lock on the password manager pref."
  );
  verifyState(false);

  await extension.unload();

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function testExtensionControlledProxyConfig() {
  const proxySvc = Ci.nsIProtocolProxyService;
  const PROXY_DEFAULT = proxySvc.PROXYCONFIG_SYSTEM;
  const EXTENSION_ID = "@set_proxy";
  const CONTROLLED_SECTION_ID = "proxyExtensionContent";
  const CONTROLLED_BUTTON_ID = "disableProxyExtension";
  const CONNECTION_SETTINGS_SETTING_ID = "connectionSettings";
  const PANEL_URL =
    "chrome://browser/content/preferences/dialogs/connection.xhtml";

  await SpecialPowers.pushPrefEnv({ set: [[PROXY_PREF, PROXY_DEFAULT]] });

  function background() {
    browser.proxy.settings.set({ value: { proxyType: "none" } });
  }

  function assertConnectionSettingsMessage(doc, isControlled) {
    let settingControl = getSettingControl(
      CONNECTION_SETTINGS_SETTING_ID,
      doc.documentGlobal
    );
    Assert.ok(
      settingControl,
      `Got setting for ${CONNECTION_SETTINGS_SETTING_ID}`
    );
    let messageBar = settingControl.controlledMessageBarRef.value;
    if (isControlled) {
      Assert.ok(messageBar, "Controlled message exists");
      Assert.ok(
        BrowserTestUtils.isVisible(messageBar),
        "Controlled message exists"
      );
    } else {
      Assert.ok(!messageBar, "Controlled message does not exist");
    }
  }

  async function connectionSettingsMessagePromise(doc, isControlled) {
    let settingControl = getSettingControl(
      CONNECTION_SETTINGS_SETTING_ID,
      doc.documentGlobal
    );
    Assert.ok(
      settingControl,
      `Got setting for ${CONNECTION_SETTINGS_SETTING_ID}`
    );
    let messageBar = settingControl.controlledMessageBarRef.value;
    if (isControlled != messageBar) {
      await waitForSettingChange(settingControl.setting);
    }
    assertConnectionSettingsMessage(doc, isControlled);
  }

  function verifyProxyState(doc, isControlled) {
    let isPanel = doc.getElementById(CONTROLLED_BUTTON_ID);
    is(
      proxyType === proxySvc.PROXYCONFIG_DIRECT,
      isControlled,
      "Proxy pref is set to the expected value."
    );

    if (isPanel) {
      let controlledSection = doc.getElementById(CONTROLLED_SECTION_ID);

      is(
        controlledSection.hidden,
        !isControlled,
        "The extension controlled row's visibility is as expected."
      );
      if (isPanel) {
        is(
          doc.getElementById(CONTROLLED_BUTTON_ID).hidden,
          !isControlled,
          "The disable extension button's visibility is as expected."
        );
      }
      if (isControlled) {
        let controlledDesc = controlledSection.querySelector("description");
        Assert.deepEqual(
          doc.l10n.getAttributes(controlledDesc),
          {
            id: "extension-controlling-proxy-config",
            args: {
              name: "set_proxy",
            },
          },
          "The user is notified that an extension is controlling proxy settings."
        );
      }
      function getProxyControls() {
        let controlGroup = doc.getElementById("networkProxyType");
        let manualControlContainer = controlGroup.querySelector("#proxy-grid");
        return {
          manualControls: [
            ...manualControlContainer.querySelectorAll(
              "label[data-l10n-id]:not([control=networkProxyNone])"
            ),
            ...manualControlContainer.querySelectorAll("input"),
            ...manualControlContainer.querySelectorAll("checkbox"),
            ...doc.querySelectorAll("#networkProxySOCKSVersion > radio"),
          ],
          pacControls: [doc.getElementById("networkProxyAutoconfigURL")],
          otherControls: [
            doc.querySelector("label[control=networkProxyNone]"),
            doc.getElementById("networkProxyNone"),
            ...controlGroup.querySelectorAll(":scope > radio"),
            ...doc.querySelectorAll("#ConnectionsDialogPane > checkbox"),
          ],
        };
      }
      let controlState = isControlled ? "disabled" : "enabled";
      let controls = getProxyControls();
      for (let element of controls.manualControls) {
        let disabled =
          isControlled || proxyType !== proxySvc.PROXYCONFIG_MANUAL;
        is(
          element.disabled,
          disabled,
          `Manual proxy controls should be ${controlState} - control: ${element.outerHTML}.`
        );
      }
      for (let element of controls.pacControls) {
        let disabled = isControlled || proxyType !== proxySvc.PROXYCONFIG_PAC;
        is(
          element.disabled,
          disabled,
          `PAC proxy controls should be ${controlState} - control: ${element.outerHTML}.`
        );
      }
      for (let element of controls.otherControls) {
        is(
          element.disabled,
          isControlled,
          `Other proxy controls should be ${controlState} - control: ${element.outerHTML}.`
        );
      }
    } else {
      assertConnectionSettingsMessage(doc, isControlled);
    }
  }

  async function reEnableProxyExtension(addon) {
    let messageChanged = connectionSettingsMessagePromise(mainDoc, true);
    await addon.enable();
    await messageChanged;
  }

  async function openProxyPanel() {
    let panel = await openAndLoadSubDialog(PANEL_URL);
    let closingPromise = BrowserTestUtils.waitForEvent(
      panel.document.getElementById("ConnectionsDialog"),
      "dialogclosing"
    );
    ok(panel, "Proxy panel opened.");
    return { panel, closingPromise };
  }

  async function closeProxyPanel(panelObj) {
    let dialog = panelObj.panel.document.getElementById("ConnectionsDialog");
    dialog.cancelDialog();
    let panelClosingEvent = await panelObj.closingPromise;
    ok(panelClosingEvent, "Proxy panel closed.");
  }

  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });
  let mainDoc = gBrowser.contentDocument;

  is(
    gBrowser.currentURI.spec,
    "about:preferences#general",
    "#general should be in the URI for about:preferences"
  );

  verifyProxyState(mainDoc, false);

  // Open the connections panel.
  let panelObj = await openProxyPanel();
  let panelDoc = panelObj.panel.document;

  verifyProxyState(panelDoc, false);

  await closeProxyPanel(panelObj);

  verifyProxyState(mainDoc, false);

  // Install an extension that controls proxy settings. The extension needs
  // incognitoOverride because controlling the proxy.settings requires private
  // browsing access.
  let extension = ExtensionTestUtils.loadExtension({
    incognitoOverride: "spanning",
    useAddonManager: "permanent",
    manifest: {
      name: "set_proxy",
      browser_specific_settings: { gecko: { id: EXTENSION_ID } },
      permissions: ["proxy"],
    },
    background,
  });

  let messageChanged = connectionSettingsMessagePromise(mainDoc, true);
  await extension.startup();
  await messageChanged;
  let addon = await AddonManager.getAddonByID(EXTENSION_ID);

  verifyProxyState(mainDoc, true);
  messageChanged = connectionSettingsMessagePromise(mainDoc, false);

  panelObj = await openProxyPanel();
  panelDoc = panelObj.panel.document;

  verifyProxyState(panelDoc, true);

  await disableExtensionViaClick(
    CONTROLLED_SECTION_ID,
    CONTROLLED_BUTTON_ID,
    panelDoc
  );

  verifyProxyState(panelDoc, false);

  await closeProxyPanel(panelObj);
  await messageChanged;

  verifyProxyState(mainDoc, false);

  await reEnableProxyExtension(addon);

  verifyProxyState(mainDoc, true);
  messageChanged = connectionSettingsMessagePromise(mainDoc, false);

  panelObj = await openProxyPanel();
  panelDoc = panelObj.panel.document;

  verifyProxyState(panelDoc, true);

  await disableExtensionViaClick(
    CONTROLLED_SECTION_ID,
    CONTROLLED_BUTTON_ID,
    panelDoc
  );

  verifyProxyState(panelDoc, false);

  await closeProxyPanel(panelObj);
  await messageChanged;

  verifyProxyState(mainDoc, false);

  // Enable the extension so we get the UNINSTALL event, which is needed by
  // ExtensionPreferencesManager to clean up properly.
  // TODO: BUG 1408226
  await reEnableProxyExtension(addon);

  await extension.unload();

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
