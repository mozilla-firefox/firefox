"use strict";
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  ExtensionControlledPopup:
    "resource:///modules/ExtensionControlledPopup.sys.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
});

function createMarkup(doc) {
  let panel = ExtensionControlledPopup._getAndMaybeCreatePanel(doc);
  let popupnotification = doc.createXULElement("popupnotification");
  let attributes = {
    id: "extension-controlled-notification",
    class: "extension-controlled-notification",
    popupid: "extension-controlled",
    hidden: "true",
    label: "ExtControlled",
    buttonlabel: "Keep Changes",
    buttonaccesskey: "K",
    secondarybuttonlabel: "Restore Settings",
    secondarybuttonaccesskey: "R",
    closebuttonhidden: "true",
    dropmarkerhidden: "true",
    checkboxhidden: "true",
  };
  Object.entries(attributes).forEach(([key, value]) => {
    popupnotification.setAttribute(key, value);
  });
  let content = doc.createXULElement("popupnotificationcontent");
  content.setAttribute("orient", "vertical");
  let description = doc.createXULElement("description");
  description.setAttribute("id", "extension-controlled-description");
  content.appendChild(description);
  popupnotification.appendChild(content);
  panel.appendChild(popupnotification);

  registerCleanupFunction(function removePopup() {
    popupnotification.remove();
  });

  return { panel, popupnotification };
}

/*
 * This function is a unit test for ExtensionControlledPopup. It is also tested
 * where it is being used (currently New Tab and homepage). An empty extension
 * is used along with the expected markup as an example.
 */
add_task(async function testExtensionControlledPopup() {
  let id = "ext-controlled@mochi.test";
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: { gecko: { id } },
      name: "Ext Controlled",
    },
    // We need to be able to find the extension using AddonManager.
    useAddonManager: "temporary",
  });

  await extension.startup();
  let addon = await AddonManager.getAddonByID(id);
  await ExtensionSettingsStore.initialize();

  let confirmedType = "extension-controlled-confirmed";
  let onObserverAdded = sinon.spy();
  let onObserverRemoved = sinon.spy();
  let observerTopic = "extension-controlled-event";
  let beforeDisableAddon = sinon.spy();
  let settingType = "extension-controlled";
  let settingKey = "some-key";
  let popup = new ExtensionControlledPopup({
    confirmedType,
    observerTopic,
    popupnotificationId: "extension-controlled-notification",
    settingType,
    settingKey,
    descriptionId: "extension-controlled-description",
    descriptionMessageId: "appmenu-new-tab-controlled-message",
    learnMoreLink: "extension-controlled",
    onObserverAdded,
    onObserverRemoved,
    beforeDisableAddon,
  });

  let doc = Services.wm.getMostRecentWindow("navigator:browser").document;
  let { panel, popupnotification } = createMarkup(doc, popup);

  function openPopupWithEvent() {
    let popupShown = promisePopupShown(panel);
    Services.obs.notifyObservers(null, observerTopic);
    return popupShown;
  }

  function closePopupWithAction(action) {
    let done;
    if (action == "ignore") {
      panel.hidePopup();
    } else if (action == "button") {
      done = TestUtils.waitForCondition(() => {
        return ExtensionSettingsStore.getSetting(confirmedType, id, id).value;
      });
      popupnotification.button.click();
    } else if (action == "secondarybutton") {
      done = awaitEvent("shutdown", id);
      popupnotification.secondaryButton.click();
    }
    return done;
  }

  // No callbacks are initially called.
  ok(!onObserverAdded.called, "No observer has been added");
  ok(!onObserverRemoved.called, "No observer has been removed");
  ok(!beforeDisableAddon.called, "Settings have not been restored");

  // Add the setting and observer.
  await ExtensionSettingsStore.addSetting(
    id,
    settingType,
    settingKey,
    "controlled",
    () => "init"
  );
  await popup.addObserver(id);

  // Ensure the panel isn't open.
  ok(onObserverAdded.called, "Observing the event");
  onObserverAdded.resetHistory();
  ok(!onObserverRemoved.called, "Observing the event");
  ok(!beforeDisableAddon.called, "Settings have not been restored");
  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The panel is closed"
  );
  is(popupnotification.hidden, true, "The popup is hidden");
  is(addon.userDisabled, false, "The extension is enabled");
  is(
    await popup.userHasConfirmed(id),
    false,
    "The user is not initially confirmed"
  );

  // The popup should opened based on the observer event.
  await openPopupWithEvent();

  ok(!onObserverAdded.called, "Only one observer has been registered");
  ok(onObserverRemoved.called, "The observer was removed");
  onObserverRemoved.resetHistory();
  ok(!beforeDisableAddon.called, "Settings have not been restored");
  is(panel.getAttribute("panelopen"), "true", "The panel is open");
  is(popupnotification.hidden, false, "The popup content is visible");
  is(await popup.userHasConfirmed(id), false, "The user has not confirmed yet");

  // Verify the description is populated.
  let description = doc.getElementById("extension-controlled-description");
  is(
    description.textContent,
    "An extension,  Ext Controlled, changed the page you see when you open a new tab.",
    "The extension name is in the description"
  );

  const learnMoreEl = panel.querySelector(
    "#extension-controlled-notification .popup-notification-learnmore-link"
  );
  ok(
    BrowserTestUtils.isVisible(learnMoreEl),
    "Expect the popupnotification learnmore link to be visible"
  );

  is(
    learnMoreEl.getAttribute("href"),
    Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "extension-controlled",
    "learnmore link should have the expected url set"
  );

  // Force close the popup, as if a user clicked away from it.
  await closePopupWithAction("ignore");

  // Nothing was recorded, but we won't show it again.
  ok(!onObserverAdded.called, "The observer hasn't changed");
  ok(!onObserverRemoved.called, "The observer hasn't changed");
  is(await popup.userHasConfirmed(id), false, "The user has not confirmed");
  is(addon.userDisabled, false, "The extension is still enabled");

  // Force add the observer again to keep changes.
  await popup.addObserver(id);
  ok(onObserverAdded.called, "The observer was added again");
  onObserverAdded.resetHistory();
  ok(!onObserverRemoved.called, "The observer is still registered");
  is(await popup.userHasConfirmed(id), false, "The user has not confirmed");

  // Wait for popup.
  await openPopupWithEvent();

  // Keep the changes.
  await closePopupWithAction("button");

  // The observer is removed, but the notification is saved.
  ok(!onObserverAdded.called, "The observer wasn't added");
  ok(onObserverRemoved.called, "The observer was removed");
  onObserverRemoved.resetHistory();
  is(await popup.userHasConfirmed(id), true, "The user has confirmed");
  is(addon.userDisabled, false, "The extension is still enabled");

  // Adding the observer again for this add-on won't work, since it is
  // confirmed.
  await popup.addObserver(id);
  ok(!onObserverAdded.called, "The observer isn't added");
  ok(!onObserverRemoved.called, "The observer isn't removed");
  is(await popup.userHasConfirmed(id), true, "The user has confirmed");

  // Clear that the user was notified.
  await popup.clearConfirmation(id);
  is(
    await popup.userHasConfirmed(id),
    false,
    "The user confirmation has been cleared"
  );

  // Force add the observer again to restore changes.
  await popup.addObserver(id);
  ok(onObserverAdded.called, "The observer was added a third time");
  onObserverAdded.resetHistory();
  ok(!onObserverRemoved.called, "The observer is still active");
  ok(!beforeDisableAddon.called, "We haven't disabled the add-on yet");
  is(await popup.userHasConfirmed(id), false, "The user has not confirmed");

  // Wait for popup.
  await openPopupWithEvent();

  // Restore the settings.
  await closePopupWithAction("secondarybutton");

  // The observer is removed and the add-on is now disabled.
  ok(!onObserverAdded.called, "There is no observer");
  ok(onObserverRemoved.called, "The observer has been removed");
  ok(beforeDisableAddon.called, "The beforeDisableAddon callback was fired");
  is(await popup.userHasConfirmed(id), false, "The user has not confirmed");
  is(addon.userDisabled, true, "The extension is now disabled");

  await extension.unload();
});

add_task(async function testDescriptionEscapesAddonName() {
  let id = "ext-controlled-markup@mochi.test";
  let name = 'Tab<Hider> & <b>Bold</b> &amp; <img data-l10n-name="addon-icon">';
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: { gecko: { id } },
      name,
    },
    useAddonManager: "temporary",
  });
  await extension.startup();
  let addon = await AddonManager.getAddonByID(id);

  let popup = new ExtensionControlledPopup({
    popupnotificationId: "extension-controlled-notification",
    descriptionId: "extension-controlled-description",
    descriptionMessageId: "appmenu-new-tab-controlled-message",
  });
  let doc = Services.wm.getMostRecentWindow("navigator:browser").document;
  createMarkup(doc, popup);

  await popup.populateDescription(doc, addon);

  let description = doc.getElementById("extension-controlled-description");
  is(
    description.textContent,
    `An extension,  ${name}, changed the page you see when you open a new tab.`,
    "The extension name is shown as plain text"
  );
  is(description.children.length, 1, "Only the add-on icon element is added");
  is(
    description.firstElementChild.getAttribute("data-l10n-name"),
    "addon-icon",
    "The only element is the add-on icon"
  );

  await extension.unload();
});
