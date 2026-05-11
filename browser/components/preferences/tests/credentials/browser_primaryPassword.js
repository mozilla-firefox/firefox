const { OSKeyStoreTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/OSKeyStoreTestUtils.sys.mjs"
);
const { OSKeyStore } = ChromeUtils.importESModule(
  "resource://gre/modules/OSKeyStore.sys.mjs"
);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["toolkit.osKeyStore.unofficialBuildOnlyLogin", ""]],
  });
});

add_task(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.settings-redesign.enabled", false]],
  });
  let prefs = await openPreferencesViaOpenPreferencesAPI("panePrivacy", {
    leaveOpen: true,
  });
  is(prefs.selectedPane, "panePrivacy", "Privacy pane was selected");

  let doc = gBrowser.contentDocument;
  // Fake the subdialog and LoginHelper
  let win = doc.defaultView;
  let dialogURL = "";
  let dialogOpened = false;
  ChromeUtils.defineLazyGetter(win, "gSubDialog", () => ({
    open(aDialogURL, { closingCallback: aCallback }) {
      dialogOpened = true;
      dialogURL = aDialogURL;
      primaryPasswordSet = primaryPasswordNextState;
      aCallback();
    },
  }));

  let primaryPasswordSet = false;
  win.LoginHelper = {
    isPrimaryPasswordSet() {
      return primaryPasswordSet;
    },
    getOSAuthEnabled() {
      return true; // Since enabled by default.
    },
  };

  let checkbox = doc.querySelector("#useMasterPassword");
  checkbox.scrollIntoView();
  ok(
    !checkbox.checked,
    "primary password checkbox should be unchecked by default"
  );
  let button = doc.getElementById("changeMasterPassword");
  ok(button.disabled, "primary password button should be disabled by default");

  let primaryPasswordNextState = false;
  if (OSKeyStoreTestUtils.canTestOSKeyStoreLogin() && OSKeyStore.canReauth()) {
    let osAuthDialogShown = OSKeyStoreTestUtils.waitForOSKeyStoreLogin(false);
    checkbox.click();
    info("waiting for os auth dialog to appear and get canceled");
    await osAuthDialogShown;
    await TestUtils.waitForCondition(
      () => !checkbox.checked,
      "wait for checkbox to get unchecked"
    );
    ok(!dialogOpened, "the dialog should not have opened");
    ok(
      !dialogURL,
      "the changemp dialog should not have been opened when the os auth dialog is canceled"
    );
    ok(
      !checkbox.checked,
      "primary password checkbox should be unchecked after canceling os auth dialog"
    );
    ok(button.disabled, "button should be disabled after canceling os auth");
  }

  primaryPasswordNextState = true;
  if (OSKeyStoreTestUtils.canTestOSKeyStoreLogin() && OSKeyStore.canReauth()) {
    let osAuthDialogShown = OSKeyStoreTestUtils.waitForOSKeyStoreLogin(true);
    checkbox.click();
    info("waiting for os auth dialog to appear");
    await osAuthDialogShown;
    info("waiting for dialogURL to get set");
    await TestUtils.waitForCondition(
      () => dialogURL,
      "wait for open to get called asynchronously"
    );
    is(
      dialogURL,
      "chrome://mozapps/content/preferences/changemp.xhtml",
      "clicking on the checkbox should open the primary password dialog"
    );
  } else {
    primaryPasswordSet = true;
    doc.defaultView.gPrivacyPane._initMasterPasswordUI();
    await TestUtils.waitForCondition(
      () => !button.disabled,
      "waiting for primary password button to get enabled"
    );
  }
  ok(!button.disabled, "primary password button should now be enabled");
  ok(checkbox.checked, "primary password checkbox should be checked now");

  dialogURL = "";
  button.doCommand();
  await TestUtils.waitForCondition(
    () => dialogURL,
    "wait for open to get called asynchronously"
  );
  is(
    dialogURL,
    "chrome://mozapps/content/preferences/changemp.xhtml",
    "clicking on the button should open the primary password dialog"
  );
  ok(!button.disabled, "primary password button should still be enabled");
  ok(checkbox.checked, "primary password checkbox should be checked still");

  // Confirm that we won't automatically respond to the dialog,
  // since we don't expect a dialog here, we want the test to fail if one appears.
  is(
    Services.prefs.getStringPref(
      "toolkit.osKeyStore.unofficialBuildOnlyLogin",
      ""
    ),
    "",
    "Pref should be set to an empty string"
  );

  primaryPasswordNextState = false;
  dialogURL = "";
  checkbox.click();
  is(
    dialogURL,
    "chrome://mozapps/content/preferences/removemp.xhtml",
    "clicking on the checkbox to uncheck primary password should show the removal dialog"
  );
  ok(button.disabled, "primary password button should now be disabled");
  ok(!checkbox.checked, "primary password checkbox should now be unchecked");

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.settings-redesign.enabled", true]],
  });
  let prefs = await openPreferencesViaOpenPreferencesAPI(
    "panePasswordsAutofill",
    {
      leaveOpen: true,
    }
  );
  is(
    prefs.selectedPane,
    "panePasswordsAutofill",
    "Passwords and Autofill pane was selected"
  );

  let doc = gBrowser.contentDocument;
  // Fake the subdialog and LoginHelper
  let win = doc.defaultView;
  let dialogURL = "";
  let dialogOpened = false;
  ChromeUtils.defineLazyGetter(win, "gSubDialog", () => ({
    open(aDialogURL, { closingCallback: aCallback }) {
      dialogOpened = true;
      dialogURL = aDialogURL;
      primaryPasswordSet = primaryPasswordNextState;
      aCallback();
    },
  }));

  let primaryPasswordSet = false;
  win.LoginHelper = {
    isPrimaryPasswordSet() {
      return primaryPasswordSet;
    },
    getOSAuthEnabled() {
      return true; // Since enabled by default.
    },
  };

  let primaryPasswordNotSet = doc.querySelector("#primaryPasswordNotSet");
  primaryPasswordNotSet.scrollIntoView();
  ok(
    primaryPasswordNotSet,
    "'Primary password not set' control should be shown by default"
  );
  let button = doc.getElementById("addPrimaryPassword");

  let primaryPasswordNextState = false;
  if (OSKeyStoreTestUtils.canTestOSKeyStoreLogin() && OSKeyStore.canReauth()) {
    let osAuthDialogShown = OSKeyStoreTestUtils.waitForOSKeyStoreLogin(false);
    button.click();
    info("waiting for os auth dialog to appear and get canceled");
    await osAuthDialogShown;
    ok(!dialogOpened, "the dialog should not have opened");
    ok(
      !dialogURL,
      "the changemp dialog should not have been opened when the os auth dialog is canceled"
    );
  }

  let primaryPasswordSetCtrl = doc.querySelector("#primaryPasswordSet");
  primaryPasswordNextState = true;
  if (OSKeyStoreTestUtils.canTestOSKeyStoreLogin() && OSKeyStore.canReauth()) {
    let osAuthDialogShown = OSKeyStoreTestUtils.waitForOSKeyStoreLogin(true);
    button.click();
    info("waiting for os auth dialog to appear");
    await osAuthDialogShown;
    info("waiting for dialogURL to get set");
    await TestUtils.waitForCondition(
      () => dialogURL,
      "wait for open to get called asynchronously"
    );
    is(
      dialogURL,
      "chrome://mozapps/content/preferences/changemp.xhtml",
      "clicking on the checkbox should open the primary password dialog"
    );
  } else {
    primaryPasswordSet = true;
    doc.defaultView.gPrivacyPane._initMasterPasswordUI();
    await TestUtils.waitForCondition(
      () => !button.disabled,
      "waiting for primary password button to get enabled"
    );
  }
  ok(
    primaryPasswordSetCtrl,
    "'primary password set control' should be visible now"
  );

  dialogURL = "";
  button.click();
  await TestUtils.waitForCondition(
    () => dialogURL,
    "wait for open to get called asynchronously"
  );
  is(
    dialogURL,
    "chrome://mozapps/content/preferences/changemp.xhtml",
    "clicking on the button should open the primary password dialog"
  );

  // Confirm that we won't automatically respond to the dialog,
  // since we don't expect a dialog here, we want the test to fail if one appears.
  is(
    Services.prefs.getStringPref(
      "toolkit.osKeyStore.unofficialBuildOnlyLogin",
      ""
    ),
    "",
    "Pref should be set to an empty string"
  );

  let removePrimaryPasswordButton = doc.querySelector(
    "#turnOffPrimaryPassword"
  );
  primaryPasswordNextState = false;
  dialogURL = "";
  removePrimaryPasswordButton.click();
  is(
    dialogURL,
    "chrome://mozapps/content/preferences/removemp.xhtml",
    "clicking on the checkbox to uncheck primary password should show the removal dialog"
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
