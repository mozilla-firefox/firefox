const TEST_ORIGIN = "https://example.org";
const TEST_URL_PATH = `${TEST_ORIGIN}${DIRECTORY_PATH}form_basic_login.html`;
const PREF = "browser.autocomplete.removeRecords.enabled";
const AC_L10N = new Localization(
  ["toolkit/main-window/autocomplete.ftl"],
  true
);

const LOGINS_DATA = [
  { origin: TEST_ORIGIN, username: "user1", password: "pass1" },
  { origin: TEST_ORIGIN, username: "user2", password: "pass2" },
];

add_setup(async () => {
  await Services.logins.addLogins(
    LOGINS_DATA.map(login => LoginTestUtils.testData.formLogin(login))
  );
});

// Every flyout delete dispatches a real reauthentication request. Stub it for
// the whole file so no task can raise a genuine OS auth prompt and wedge the
// browser.
let gReauthAuthorized = false;
let gReauthCalls = [];

add_setup(async () => {
  const { LoginHelper } = ChromeUtils.importESModule(
    "resource://gre/modules/LoginHelper.sys.mjs"
  );
  const original = LoginHelper.requestReauth;
  LoginHelper.requestReauth = async (...args) => {
    gReauthCalls.push(args);
    // A real prompt takes focus, which tears the dropdown down before it
    // resolves. Do the same so callers see the ordering they see in the wild.
    const popup = document.getElementById("PopupAutoComplete");
    if (popup.state != "closed") {
      popup.hidePopup();
      await TestUtils.waitForCondition(
        () => popup.state == "closed",
        "Wait for the dropdown to close before reauthentication resolves"
      );
    }
    return { isAuthorized: gReauthAuthorized, telemetryEvent: null };
  };
  registerCleanupFunction(() => {
    LoginHelper.requestReauth = original;
  });
});

function getSecondaryAction(popup, index) {
  const item = popup.firstChild.getItemAtIndex(index);
  const rowItem = item.querySelector("autocomplete-row-item");
  const button = rowItem.shadowRoot.querySelector(
    "moz-button.secondary-action"
  );
  return { item, rowItem, button };
}

async function selectRow(item, index) {
  for (let i = 0; i <= index; i++) {
    await EventUtils.synthesizeKey("KEY_ArrowDown");
  }
  await TestUtils.waitForCondition(
    () => item.hasAttribute("selected"),
    "Wait for the login row to become active"
  );
}

function waitForFlyout(itemLabel) {
  return TestUtils.waitForCondition(
    () =>
      [...document.querySelectorAll("menupopup")].find(
        m =>
          m.state == "open" &&
          [...m.querySelectorAll("menuitem")].some(
            mi => mi.getAttribute("label") === itemLabel
          )
      ),
    "Wait for the flyout menu to open"
  );
}

async function openFlyoutByKeyboard(item, rowItem) {
  await selectRow(item, 0);
  await EventUtils.synthesizeKey("KEY_Tab");
  await TestUtils.waitForCondition(
    () => rowItem.hasAttribute("subfocused"),
    "Wait for the secondary action to become sub-focused"
  );
  await EventUtils.synthesizeKey("KEY_Enter");
  return waitForFlyout(rowItem.actions.secondary.actions[0].label);
}

async function openFlyout(popup, button, label) {
  const menuShown = BrowserTestUtils.waitForEvent(
    popup,
    "popupshown",
    false,
    event => event.target.localName == "menupopup"
  );
  const panelHidden = BrowserTestUtils.waitForEvent(
    popup,
    "popuphidden",
    false,
    event => event.target == popup
  );

  await EventUtils.promiseElementReadyForUserInput(button, window, info);

  await TestUtils.waitForCondition(
    () => button.checkVisibility({ checkVisibilityCSS: true }),
    "Wait for the secondary action button to be visible"
  );
  EventUtils.synthesizeMouseAtCenter(button, {}, window);

  const event = await Promise.race([menuShown, panelHidden]);
  Assert.equal(
    event.type,
    "popupshown",
    "The click reached the secondary action button instead of the row"
  );

  const menupopup = event.target;
  Assert.ok(
    [...menupopup.querySelectorAll("menuitem")].some(
      mi => mi.getAttribute("label") === label
    ),
    "The flyout belongs to the row's secondary action"
  );
  return menupopup;
}

// Checks every autocomplete event since the last reset shares one flow id.
function assertOneFlow() {
  const events = [
    Glean.autocomplete.dropdownShown,
    Glean.autocomplete.dropdownClosed,
    Glean.autocomplete.recordInteraction,
    Glean.autocomplete.recordDialogInteraction,
  ].flatMap(metric => metric.testGetValue() ?? []);
  const flowIds = new Set(events.map(e => e.extra.flow_id));
  Assert.equal(flowIds.size, 1, "Every event shares one flow id");
  const [flowId] = flowIds;
  Assert.ok(flowId, "The shared flow id is not empty");
  return flowId;
}

// Waits until only the Manage Passwords footer remains.
function waitForFooterOnly(popup) {
  return TestUtils.waitForCondition(
    () =>
      popup.view.matchCount === 1 &&
      popup.view.getStyleAt(0) === "loginsFooter",
    "Wait for only the footer to remain"
  );
}

function waitForPopupHidden(popup) {
  return BrowserTestUtils.waitForEvent(
    popup,
    "popuphidden",
    false,
    event => event.target === popup
  );
}

async function closeWithEscape(popup) {
  const hidden = waitForPopupHidden(popup);
  await EventUtils.synthesizeKey("KEY_Escape");
  await hidden;
}

function assertClosedAs(closeReason, message) {
  const closedEvents = Glean.autocomplete.dropdownClosed.testGetValue();
  Assert.equal(
    closedEvents?.length,
    1,
    "One dropdown closed event is recorded"
  );
  Assert.equal(
    closedEvents[0].extra.record_type,
    "password",
    "The event names the password record type"
  );
  Assert.equal(closedEvents[0].extra.close_reason, closeReason, message);
}

async function activateDelete(popup) {
  const { item, button } = getSecondaryAction(popup, 0);
  await selectRow(item, 0);
  const menupopup = await openFlyout(popup, button, DELETE_LABEL);
  const menuitem = [...menupopup.querySelectorAll("menuitem")].find(
    mi => mi.getAttribute("label") === DELETE_LABEL
  );
  menupopup.activateItem(menuitem);
}

add_task(async function test_edit_icon_when_pref_disabled() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, false]] });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { rowItem, button } = getSecondaryAction(popup, 0);

      Assert.equal(
        rowItem.actions.secondary.type,
        "edit",
        "Login row keeps the edit secondary action when the pref is off"
      );
      Assert.ok(
        button.iconSrc.endsWith("edit.svg"),
        "The secondary action shows the edit icon"
      );

      await closePopup(popup);
    }
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_flyout_when_pref_enabled() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      Services.fog.testResetFOG();
      await openACPopup(popup, browser, "#form-basic-username");
      const shownEvents = Glean.autocomplete.dropdownShown.testGetValue();
      Assert.equal(
        shownEvents?.length,
        1,
        "One dropdown shown event is recorded"
      );
      Assert.equal(
        shownEvents[0].extra.record_type,
        "password",
        "The event names the password record type"
      );
      Assert.equal(
        shownEvents[0].extra.reopened_after_removal,
        "false",
        "A user-opened dropdown is not marked as reopened after a removal"
      );

      const { item, rowItem, button } = getSecondaryAction(popup, 0);

      Assert.equal(
        rowItem.actions.secondary.type,
        "menupopup",
        "Login row shows the flyout secondary action when the pref is on"
      );
      Assert.ok(
        button.iconSrc.endsWith("more.svg"),
        "The secondary action shows the more icon"
      );

      await selectRow(item, 0);
      await TestUtils.waitForCondition(
        () => button.checkVisibility({ checkVisibilityCSS: true }),
        "Secondary action is visible when the row is active"
      );

      const [editAction, deleteAction] = rowItem.actions.secondary.actions;

      const menupopup = await openFlyout(popup, button, editAction.label);

      const labels = [...menupopup.querySelectorAll("menuitem")].map(mi =>
        mi.getAttribute("label")
      );
      Assert.deepEqual(
        labels,
        [editAction.label, deleteAction.label],
        "Flyout contains the Edit and Delete items"
      );

      Assert.ok(
        popup.contains(menupopup),
        "The flyout is hosted inside the autocomplete panel"
      );
      Assert.equal(popup.state, "open", "The autocomplete popup stays open");

      const menuHidden = BrowserTestUtils.waitForEvent(
        menupopup,
        "popuphiding"
      );
      menupopup.hidePopup();
      await menuHidden;

      await TestUtils.waitForCondition(
        () => !popup.contains(menupopup),
        "The flyout menupopup is removed after closing"
      );
      Assert.equal(
        popup.state,
        "open",
        "The autocomplete popup remains open after the flyout closes"
      );

      // The flyout lives inside the panel, so its popup events bubble up to
      // the panel's own listeners and to AutoCompleteParent's. Neither may
      // mistake them for the panel itself opening or closing, otherwise the
      // actor tears itself down and every later secondary action no-ops.
      Assert.ok(
        popup.mPopupOpen,
        "The panel still considers itself open after the flyout closes"
      );
      const { AutoCompleteParent } = ChromeUtils.importESModule(
        "moz-src:///toolkit/actors/AutoCompleteParent.sys.mjs"
      );
      Assert.ok(
        AutoCompleteParent.getCurrentActor(),
        "The autocomplete actor is still current after the flyout closes"
      );

      await closePopup(popup);
      const closedEvents = Glean.autocomplete.dropdownClosed.testGetValue();
      Assert.equal(
        closedEvents?.length,
        1,
        "One dropdown closed event is recorded"
      );
      Assert.equal(
        closedEvents[0].extra.record_type,
        "password",
        "The event names the password record type"
      );
      Assert.equal(
        closedEvents[0].extra.close_reason,
        "dismissed",
        "Closing the panel records a dismissed close"
      );
      Assert.ok(closedEvents[0].extra.flow_id, "The event carries a flow id");
      Assert.equal(
        closedEvents[0].extra.flow_id,
        shownEvents[0].extra.flow_id,
        "The closed event's flow id matches the shown event for the same dropdown"
      );
    }
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_selecting_a_login_records_dropdown_closed() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { item } = getSecondaryAction(popup, 0);
      await selectRow(item, 0);

      Services.fog.testResetFOG();
      const popupHidden = BrowserTestUtils.waitForEvent(
        popup,
        "popuphidden",
        false,
        event => event.target == popup
      );
      await EventUtils.synthesizeKey("KEY_Enter");
      await popupHidden;

      const closedEvents = Glean.autocomplete.dropdownClosed.testGetValue();
      Assert.equal(
        closedEvents?.length,
        1,
        "One dropdown closed event is recorded"
      );
      Assert.equal(
        closedEvents[0].extra.record_type,
        "password",
        "The event names the password record type"
      );
      Assert.equal(
        closedEvents[0].extra.close_reason,
        "selected",
        "Picking a login records a selected close"
      );
    }
  );
});

add_task(async function test_typing_past_logins_records_typed_through() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      Services.fog.testResetFOG();
      for (const ch of ["z", "z", "z"]) {
        await BrowserTestUtils.synthesizeKey(ch, {}, browser);
      }
      await waitForFooterOnly(popup);
      Assert.equal(popup.state, "open", "The footer keeps the dropdown open");

      await closeWithEscape(popup);
      assertClosedAs(
        "typed_through",
        "Closing once only the footer remains records typed_through"
      );
    }
  );
});

add_task(async function test_backspace_back_to_logins_records_dismissed() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      Services.fog.testResetFOG();
      await BrowserTestUtils.synthesizeKey("u", {}, browser);
      await BrowserTestUtils.synthesizeKey("z", {}, browser);
      await waitForFooterOnly(popup);

      await BrowserTestUtils.synthesizeKey("KEY_Backspace", {}, browser);
      await TestUtils.waitForCondition(
        () => popup.view.getStyleAt(0) === "loginWithOrigin",
        "Wait for the logins to come back"
      );

      await closeWithEscape(popup);
      assertClosedAs(
        "dismissed",
        "Closing once the logins are back records dismissed"
      );
    }
  );
});

add_task(async function test_typing_in_password_field_records_typed_through() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-password");

      Services.fog.testResetFOG();
      const hidden = waitForPopupHidden(popup);
      await BrowserTestUtils.synthesizeKey("z", {}, browser);
      await hidden;
      assertClosedAs(
        "typed_through",
        "Typing in the password field closes with typed_through"
      );
    }
  );
});

add_task(
  async function test_footer_after_typing_past_logins_records_selected() {
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: TEST_URL_PATH },
      async function (browser) {
        const popup = document.getElementById("PopupAutoComplete");
        await openACPopup(popup, browser, "#form-basic-username");

        await BrowserTestUtils.synthesizeKey("z", {}, browser);
        await waitForFooterOnly(popup);

        Services.fog.testResetFOG();
        const hidden = waitForPopupHidden(popup);
        const passwordManager = await openPasswordManager(async () => {
          await EventUtils.synthesizeKey("KEY_ArrowDown");
          await EventUtils.synthesizeKey("KEY_Enter");
        }, false);
        await hidden;
        assertClosedAs(
          "selected",
          "Picking the footer records a selected close"
        );
        passwordManager.close();
      }
    );
  }
);

add_task(async function test_flyout_opens_via_keyboard() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { item, rowItem } = getSecondaryAction(popup, 0);
      await selectRow(item, 0);

      await EventUtils.synthesizeKey("KEY_Tab");
      await TestUtils.waitForCondition(
        () => rowItem.hasAttribute("subfocused"),
        "Wait for the secondary action to become sub-focused"
      );

      const editLabel = rowItem.actions.secondary.actions[0].label;
      await EventUtils.synthesizeKey("KEY_Enter");

      const menupopup = await TestUtils.waitForCondition(
        () =>
          [...document.querySelectorAll("menupopup")].find(m =>
            [...m.querySelectorAll("menuitem")].some(
              mi => mi.getAttribute("label") === editLabel
            )
          ),
        "Wait for the flyout to open from the keyboard"
      );

      Assert.equal(popup.state, "open", "The autocomplete popup stays open");

      const menuHidden = BrowserTestUtils.waitForEvent(
        menupopup,
        "popuphiding"
      );
      menupopup.hidePopup();
      await menuHidden;

      await closePopup(popup);
    }
  );
  await SpecialPowers.popPrefEnv();
});

// The action button must stay pinned to the row for as long as the flyout is
// open, not only while the row is hovered or selected.
add_task(async function test_action_button_persists_while_flyout_open() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { item, rowItem, button } = getSecondaryAction(popup, 0);
      await selectRow(item, 0);

      const editLabel = rowItem.actions.secondary.actions[0].label;
      const menupopup = await openFlyout(popup, button, editLabel);

      Assert.ok(
        rowItem.hasAttribute("menuopen"),
        "The row is marked open while the flyout is showing"
      );

      const menuHidden = BrowserTestUtils.waitForEvent(
        menupopup,
        "popuphiding"
      );
      menupopup.hidePopup();
      await menuHidden;

      await TestUtils.waitForCondition(
        () => !rowItem.hasAttribute("menuopen"),
        "The open marker is cleared after the flyout closes"
      );

      await closePopup(popup);
    }
  );
  await SpecialPowers.popPrefEnv();
});

// Popup should route each item to its own action index through
// the parent's dispatch path.
add_task(async function test_flyout_actions_dispatch_by_index() {
  const { AutoCompleteParent } = ChromeUtils.importESModule(
    "moz-src:///toolkit/actors/AutoCompleteParent.sys.mjs"
  );
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { item, rowItem } = getSecondaryAction(popup, 0);
      await selectRow(item, 0);

      const calls = [];
      const original = AutoCompleteParent.prototype.selectAutoCompleteEntry;
      AutoCompleteParent.prototype.selectAutoCompleteEntry = function (
        ...args
      ) {
        calls.push(args);
      };

      try {
        const { actions } = rowItem.actions.secondary;
        Assert.equal(actions.length, 2, "Flyout has two actions");

        actions[0].action();
        Assert.deepEqual(
          calls.at(-1),
          [true, 0],
          "First flyout item dispatches secondary action index 0"
        );

        actions[1].action();
        Assert.deepEqual(
          calls.at(-1),
          [true, 1],
          "Second flyout item dispatches secondary action index 1"
        );
      } finally {
        AutoCompleteParent.prototype.selectAutoCompleteEntry = original;
      }

      await closePopup(popup);
    }
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_flyout_edit_opens_about_logins() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF, true],
      ["test.wait300msAfterTabSwitch", true],
    ],
  });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { item, rowItem, button } = getSecondaryAction(popup, 0);
      await selectRow(item, 0);

      const editLabel = AC_L10N.formatValueSync("autocomplete-edit-password");
      const menupopup = await openFlyout(popup, button, editLabel);
      const editItem = [...menupopup.querySelectorAll("menuitem")].find(
        mi => mi.getAttribute("label") === editLabel
      );

      const logins = await Services.logins.getAllLogins();
      const expectedGuid = logins.find(
        login => login.username == rowItem.value
      ).guid;

      const tabOpened = BrowserTestUtils.waitForNewTab(
        gBrowser,
        url => url.startsWith("about:logins"),
        true
      );

      // about:logins redirects to drop the entryPoint param, so the opening URL
      // has to be captured before the load settles.
      const originalAddTrustedTab = gBrowser.addTrustedTab;
      let openedURL;
      gBrowser.addTrustedTab = (url, ...rest) => {
        openedURL = url;
        return originalAddTrustedTab.call(gBrowser, url, ...rest);
      };

      Services.fog.testResetFOG();
      let tab;
      try {
        menupopup.activateItem(editItem);
        tab = await tabOpened;
      } finally {
        gBrowser.addTrustedTab = originalAddTrustedTab;
      }

      const events = Glean.autocomplete.recordInteraction.testGetValue();
      Assert.equal(events?.length, 1, "One interaction event is recorded");
      Assert.equal(
        events[0].extra.interaction_type,
        "edit",
        "The flyout edit item records an edit interaction"
      );
      Assert.equal(
        events[0].extra.record_type,
        "password",
        "The event names the password record type"
      );

      Assert.equal(
        openedURL,
        "about:logins?entryPoint=Autocomplete",
        "about:logins is opened with the autocomplete entry point"
      );

      await SpecialPowers.spawn(
        tab.linkedBrowser,
        [expectedGuid],
        async guid => {
          const loginList =
            content.document.querySelector("login-list").shadowRoot;
          await ContentTaskUtils.waitForCondition(
            () =>
              loginList.querySelector("login-list-item[aria-selected='true']")
                ?.dataset?.guid === guid,
            "Wait for the edited login to be preselected"
          );
        }
      );

      BrowserTestUtils.removeTab(tab);
      await closePopup(popup);
    }
  );
  await SpecialPowers.popPrefEnv();
});

// Closing the autocomplete panel should tear down any flyout a row left open.
add_task(async function test_flyout_closes_with_panel() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { item, rowItem, button } = getSecondaryAction(popup, 0);
      await selectRow(item, 0);

      const editLabel = rowItem.actions.secondary.actions[0].label;
      const menupopup = await openFlyout(popup, button, editLabel);

      await closePopup(popup);

      await TestUtils.waitForCondition(
        () => !popup.contains(menupopup),
        "The flyout is removed when the autocomplete panel closes"
      );
      Assert.ok(
        !rowItem.hasAttribute("menuopen"),
        "The row's open marker is cleared when the panel closes"
      );
    }
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_activating_flyout_item_keeps_panel_open() {
  const { AutoCompleteParent } = ChromeUtils.importESModule(
    "moz-src:///toolkit/actors/AutoCompleteParent.sys.mjs"
  );
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { item, rowItem } = getSecondaryAction(popup, 0);
      const menupopup = await openFlyoutByKeyboard(item, rowItem);

      const menuitems = [...menupopup.querySelectorAll("menuitem")];
      for (const menuitem of menuitems) {
        Assert.equal(
          menuitem.getAttribute("closemenu"),
          "single",
          "Flyout items close only their own menu, not the popup chain"
        );
      }

      const original = AutoCompleteParent.prototype.selectAutoCompleteEntry;
      AutoCompleteParent.prototype.selectAutoCompleteEntry = () => {};

      try {
        const menuHidden = BrowserTestUtils.waitForEvent(
          menupopup,
          "popuphiding"
        );
        menupopup.activateItem(menuitems[0]);
        await menuHidden;

        Assert.equal(
          popup.state,
          "open",
          "The autocomplete panel stays open after a flyout item is activated"
        );
      } finally {
        AutoCompleteParent.prototype.selectAutoCompleteEntry = original;
      }

      await closePopup(popup);
    }
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_subfocus_tracks_the_flyout() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { item, rowItem } = getSecondaryAction(popup, 0);
      const menupopup = await openFlyoutByKeyboard(item, rowItem);

      Assert.ok(
        rowItem.hasAttribute("subfocused"),
        "The button keeps its focus indicator while the flyout is showing"
      );
      Assert.ok(
        rowItem.hasAttribute("menuopen"),
        "The row is marked open while the flyout is showing"
      );

      const menuHidden = BrowserTestUtils.waitForEvent(
        menupopup,
        "popuphiding"
      );
      menupopup.hidePopup();
      await menuHidden;

      await TestUtils.waitForCondition(
        () => !rowItem.hasAttribute("subfocused"),
        "Wait for the sub-selection to drop when the flyout closes"
      );
      Assert.ok(
        !popup._secondaryActionFocused,
        "The panel and the row agree that the sub-selection is gone"
      );
      Assert.equal(popup.state, "open", "The autocomplete panel stays open");

      await closePopup(popup);
    }
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_secondary_action_menu_semantics() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { item, rowItem, button } = getSecondaryAction(popup, 0);
      const { label, tooltip } = rowItem.actions.secondary;

      Assert.ok(
        label.includes("user1"),
        `The button is named after the row it belongs to, got "${label}"`
      );
      Assert.ok(
        !tooltip.includes("user1"),
        `The tooltip stays short and omits the row, got "${tooltip}"`
      );
      Assert.ok(
        label.startsWith(tooltip),
        "The accessible name starts with the tooltip text"
      );

      const innerButton = button.shadowRoot.querySelector("#main-button");
      Assert.equal(
        innerButton.getAttribute("aria-label"),
        label,
        "The button's accessible name names the row"
      );
      Assert.equal(
        innerButton.getAttribute("title"),
        tooltip,
        "The tooltip is the short string, not the accessible name"
      );
      Assert.equal(
        innerButton.getAttribute("aria-haspopup"),
        "menu",
        "The button advertises that it opens a menu"
      );
      Assert.equal(
        innerButton.getAttribute("aria-expanded"),
        "false",
        "The button reports its menu as collapsed"
      );

      const menupopup = await openFlyoutByKeyboard(item, rowItem);

      Assert.equal(
        menupopup.getAttribute("aria-label"),
        label,
        "The flyout has an accessible name"
      );
      await TestUtils.waitForCondition(
        () => innerButton.getAttribute("aria-expanded") === "true",
        "Wait for the button to report its menu as expanded"
      );

      const menuHidden = BrowserTestUtils.waitForEvent(
        menupopup,
        "popuphiding"
      );
      menupopup.hidePopup();
      await menuHidden;

      await TestUtils.waitForCondition(
        () => innerButton.getAttribute("aria-expanded") === "false",
        "Wait for the button to report its menu as collapsed again"
      );

      await closePopup(popup);
    }
  );
  await SpecialPowers.popPrefEnv();
});

// Moving the pointer into the flyout takes it off the row, which clears the
// dropdown's selection. The flyout must restore it, otherwise AutoCompleteParent
// has no entry to dispatch the delete action for.
add_task(async function test_delete_works_after_pointer_leaves_row() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  gReauthAuthorized = true;
  gReauthCalls = [];
  let sawDialog = false;

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { item, rowItem, button } = getSecondaryAction(popup, 0);
      const richlistbox = popup.richlistbox;

      await TestUtils.waitForCondition(() => {
        EventUtils.synthesizeMouseAtCenter(item, { type: "mousemove" });
        return richlistbox.hasAttribute("pointerselected");
      }, "Wait for the row to be selected by the pointer");
      Assert.equal(popup.selectedIndex, 0, "The hovered row is selected");

      const deleteLabel = rowItem.actions.secondary.actions[1].label;
      const menupopup = await openFlyout(popup, button, deleteLabel);
      const menuitem = [...menupopup.querySelectorAll("menuitem")].find(
        mi => mi.getAttribute("label") === deleteLabel
      );

      richlistbox.dispatchEvent(
        new MouseEvent("mouseout", { bubbles: true, relatedTarget: null })
      );
      Assert.equal(
        popup.selectedIndex,
        -1,
        "The dropdown drops its selection once the pointer leaves the row"
      );

      const dialogClosed = BrowserTestUtils.promiseAlertDialog(
        null,
        undefined,
        {
          callback: win => {
            sawDialog = true;
            win.document.querySelector("dialog").getButton("cancel").click();
          },
        }
      );

      menupopup.activateItem(menuitem);
      await dialogClosed;

      Assert.equal(gReauthCalls.length, 1, "Reauthentication was requested");
      Assert.ok(
        sawDialog,
        "The delete action still resolves to the row the flyout belongs to"
      );

      await TestUtils.waitForCondition(
        () => popup.state == "open",
        "Wait for the dropdown to come back after the confirmation"
      );

      await closePopup(popup);
    }
  );
  gReauthAuthorized = false;
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_delete_reauthenticates_then_confirms() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  gReauthAuthorized = true;
  gReauthCalls = [];
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { item, rowItem, button } = getSecondaryAction(popup, 0);
      await selectRow(item, 0);

      const deleteLabel = rowItem.actions.secondary.actions[1].label;
      const menupopup = await openFlyout(popup, button, deleteLabel);
      const menuitem = [...menupopup.querySelectorAll("menuitem")].find(
        mi => mi.getAttribute("label") === deleteLabel
      );

      Services.fog.testResetFOG();
      let dialogWin;
      const dialogClosed = BrowserTestUtils.promiseAlertDialog(
        null,
        undefined,
        {
          callback: async win => {
            dialogWin = win;
            const [title, message, confirmButton] = AC_L10N.formatValuesSync([
              { id: "autocomplete-delete-password-title" },
              { id: "autocomplete-remove-record-message" },
              { id: "autocomplete-delete-record-button" },
            ]);
            Assert.equal(
              win.document.getElementById("infoTitle").textContent,
              title,
              "The confirmation asks whether to remove the password"
            );
            Assert.equal(
              win.document.getElementById("infoBody").textContent,
              message,
              "The confirmation warns the action cannot be undone"
            );
            const dialog = win.document.querySelector("dialog");
            Assert.equal(
              dialog.getButton("accept").label,
              confirmButton,
              "The accept button is labelled Remove"
            );
            dialog.getButton("cancel").click();
          },
        }
      );

      menupopup.activateItem(menuitem);
      await dialogClosed;

      Assert.equal(gReauthCalls.length, 1, "Reauthentication was requested");
      Assert.equal(
        gReauthCalls[0][4],
        "delete_autocomplete",
        "Reauthentication is recorded against the autocomplete delete"
      );
      Assert.ok(dialogWin, "The confirmation dialog was shown");

      const events = Glean.autocomplete.recordDialogInteraction.testGetValue();
      Assert.equal(
        events?.length,
        1,
        "One dialog interaction event is recorded"
      );
      Assert.equal(
        events[0].extra.interaction_type,
        "cancel",
        "Cancelling the dialog records a cancel interaction"
      );
      Assert.equal(
        events[0].extra.record_type,
        "password",
        "The event names the password record type"
      );
      Assert.equal(
        events[0].extra.update_type,
        "delete",
        "The update type is delete"
      );

      const clickEvents = Glean.autocomplete.recordInteraction.testGetValue();
      Assert.equal(clickEvents?.length, 1, "One interaction event is recorded");
      Assert.equal(
        clickEvents[0].extra.interaction_type,
        "delete",
        "The flyout delete item records a delete interaction"
      );
      Assert.equal(
        clickEvents[0].extra.record_type,
        "password",
        "The event names the password record type"
      );
      Assert.ok(clickEvents[0].extra.flow_id, "The interaction has a flow id");
      Assert.equal(
        events[0].extra.flow_id,
        clickEvents[0].extra.flow_id,
        "The dialog event shares the interaction's flow id"
      );

      await TestUtils.waitForCondition(
        () => popup.state == "open",
        "Wait for the dropdown to come back after the confirmation"
      );
      Assert.equal(
        popup.state,
        "open",
        "The dropdown is restored once the confirmation is dismissed"
      );
      const closedEvents = Glean.autocomplete.dropdownClosed.testGetValue();
      Assert.deepEqual(
        closedEvents?.map(e => e.extra.close_reason),
        ["removal_prompt"],
        "The prompt hiding the dropdown records a removal_prompt close"
      );
      const shownEvents = Glean.autocomplete.dropdownShown.testGetValue();
      Assert.deepEqual(
        shownEvents?.map(e => e.extra.reopened_after_removal),
        ["true"],
        "The reopened dropdown is marked as reopened after a removal"
      );
      assertOneFlow();

      const logins = await Services.logins.getAllLogins();
      Assert.equal(
        logins.length,
        LOGINS_DATA.length,
        "Cancelling the confirmation leaves the login in place"
      );

      await closePopup(popup);
    }
  );
  gReauthAuthorized = false;
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_delete_skips_confirm_when_reauth_fails() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  gReauthAuthorized = false;
  gReauthCalls = [];
  let sawDialog = false;
  const observer = () => {
    sawDialog = true;
  };
  Services.obs.addObserver(observer, "common-dialog-loaded");
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      Services.fog.testResetFOG();
      await openACPopup(popup, browser, "#form-basic-username");

      const { item, rowItem, button } = getSecondaryAction(popup, 0);
      await selectRow(item, 0);

      const deleteLabel = rowItem.actions.secondary.actions[1].label;
      const menupopup = await openFlyout(popup, button, deleteLabel);
      const menuitem = [...menupopup.querySelectorAll("menuitem")].find(
        mi => mi.getAttribute("label") === deleteLabel
      );

      menupopup.activateItem(menuitem);
      await TestUtils.waitForCondition(
        () => gReauthCalls.length,
        "Wait for reauthentication to be requested"
      );
      await TestUtils.waitForTick();

      Assert.ok(!sawDialog, "No confirmation is shown when reauth is declined");
      Assert.equal(
        Glean.autocomplete.recordDialogInteraction.testGetValue(),
        null,
        "No dialog interaction is recorded when reauth is declined"
      );
      const clickEvents = Glean.autocomplete.recordInteraction.testGetValue();
      Assert.equal(clickEvents?.length, 1, "One interaction event is recorded");
      Assert.equal(
        clickEvents[0].extra.interaction_type,
        "delete",
        "The flyout delete item records a delete interaction before reauth"
      );
      Assert.equal(
        clickEvents[0].extra.record_type,
        "password",
        "The event names the password record type"
      );

      await TestUtils.waitForCondition(
        () => popup.state == "open",
        "The dropdown comes back even when reauthentication is declined"
      );
      Assert.equal(
        popup.state,
        "open",
        "The dropdown is restored when reauthentication is declined"
      );

      await closePopup(popup);
      const shownEvents = Glean.autocomplete.dropdownShown.testGetValue();
      Assert.deepEqual(
        shownEvents?.map(e => e.extra.reopened_after_removal),
        ["false", "true"],
        "Only the automatic reopen is marked as reopened after a removal"
      );
      const closedEvents = Glean.autocomplete.dropdownClosed.testGetValue();
      Assert.deepEqual(
        closedEvents?.map(e => e.extra.close_reason),
        ["removal_prompt", "dismissed"],
        "The reauthentication prompt and the final close are both recorded"
      );
      Assert.equal(
        assertOneFlow(),
        shownEvents[1].extra.flow_id,
        "The reopened dropdown keeps the flow id of the interaction"
      );
    }
  );
  Services.obs.removeObserver(observer, "common-dialog-loaded");
  await SpecialPowers.popPrefEnv();
});

// Name the flyout item by its string rather than its position, so adding
// another action to the menu later does not move this test's target.
const DELETE_LABEL = AC_L10N.formatValueSync("autocomplete-delete-password");

add_task(async function test_delete_removes_the_login() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  gReauthAuthorized = true;
  gReauthCalls = [];
  const before = await Services.logins.getAllLogins();
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const { item, button } = getSecondaryAction(popup, 0);
      await selectRow(item, 0);

      const menupopup = await openFlyout(popup, button, DELETE_LABEL);
      const menuitem = [...menupopup.querySelectorAll("menuitem")].find(
        mi => mi.getAttribute("label") === DELETE_LABEL
      );

      Services.fog.testResetFOG();
      const dialogClosed = BrowserTestUtils.promiseAlertDialog("accept");
      menupopup.activateItem(menuitem);
      await dialogClosed;

      await TestUtils.waitForCondition(async () => {
        const logins = await Services.logins.getAllLogins();
        return logins.length == before.length - 1;
      }, "Wait for the confirmed removal to reach storage");

      const events = Glean.autocomplete.recordInteraction.testGetValue();
      Assert.equal(events?.length, 1, "One interaction event is recorded");
      Assert.equal(
        events[0].extra.interaction_type,
        "delete",
        "The flyout delete item records a delete interaction"
      );
      Assert.equal(
        events[0].extra.record_type,
        "password",
        "The event names the password record type"
      );
      const dialogEvents =
        Glean.autocomplete.recordDialogInteraction.testGetValue();
      Assert.equal(
        dialogEvents?.length,
        1,
        "One dialog interaction event is recorded"
      );
      Assert.equal(
        dialogEvents[0].extra.interaction_type,
        "confirm",
        "Accepting the dialog records a confirm interaction"
      );
      Assert.equal(
        dialogEvents[0].extra.record_type,
        "password",
        "The event names the password record type"
      );
      Assert.equal(
        dialogEvents[0].extra.update_type,
        "delete",
        "The update type is delete"
      );

      const remaining = await Services.logins.getAllLogins();
      const removed = before.find(
        login => !remaining.some(kept => kept.guid == login.guid)
      );
      Assert.ok(removed, "Confirming the removal deleted exactly one login");
      registerCleanupFunction(() => Services.logins.addLoginAsync(removed));

      await TestUtils.waitForCondition(
        () => popup.state == "open",
        "Wait for the dropdown to come back after the removal"
      );
      await closePopup(popup);
    }
  );
  gReauthAuthorized = false;
  await SpecialPowers.popPrefEnv();
});

// The row's guid is authored in the content process, so an owned content
// process can name any login it likes. The parent must resolve it against the
// logins the document's own origin is allowed to see.
add_task(async function test_delete_refuses_a_foreign_guid() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  gReauthAuthorized = true;
  gReauthCalls = [];

  const [foreignLogin] = await Services.logins.addLogins([
    LoginTestUtils.testData.formLogin({
      origin: "https://example.com",
      username: "foreign-user",
      password: "foreign-pass",
    }),
  ]);
  registerCleanupFunction(() =>
    Services.logins.removeLoginAsync(foreignLogin).catch(() => {})
  );

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      await openACPopup(popup, browser, "#form-basic-username");

      const actor =
        browser.browsingContext.currentWindowGlobal.getActor("LoginManager");
      const dialogClosed = BrowserTestUtils.promiseAlertDialog("accept");
      await actor.onAutoCompleteEntrySelected("PasswordManager:DeleteLogin", {
        loginGuid: foreignLogin.guid,
      });
      await dialogClosed;

      const [stillThere] = await Services.logins.searchLoginsAsync({
        guid: foreignLogin.guid,
      });
      Assert.ok(
        stillThere,
        "A guid for an origin the document cannot see is not removed"
      );

      await TestUtils.waitForCondition(
        () => popup.state == "open",
        "Wait for the dropdown to come back after the refused removal"
      );
      await closePopup(popup);
    }
  );
  gReauthAuthorized = false;
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_removals_in_one_dropdown_share_a_flow_id() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  gReauthAuthorized = true;
  const before = await Services.logins.getAllLogins();
  await Services.logins.addLogins(
    ["user3", "user4"].map(username =>
      LoginTestUtils.testData.formLogin({
        origin: TEST_ORIGIN,
        username,
        password: "pass",
      })
    )
  );
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      Services.fog.testResetFOG();
      await openACPopup(popup, browser, "#form-basic-username");

      for (const button of ["accept", "accept", "cancel"]) {
        const count = (await Services.logins.getAllLogins()).length;
        const dialogClosed = BrowserTestUtils.promiseAlertDialog(button);
        await activateDelete(popup);
        await dialogClosed;
        if (button == "accept") {
          await TestUtils.waitForCondition(
            async () => (await Services.logins.getAllLogins()).length < count,
            "Wait for the confirmed removal to reach storage"
          );
        }
        await TestUtils.waitForCondition(
          () => popup.state == "open",
          "Wait for the dropdown to come back after the removal"
        );
      }
      await closePopup(popup);

      Assert.equal(
        Glean.autocomplete.recordInteraction.testGetValue()?.length,
        3,
        "Three interaction events are recorded"
      );
      Assert.equal(
        Glean.autocomplete.recordDialogInteraction.testGetValue()?.length,
        3,
        "Three dialog interaction events are recorded"
      );
      Assert.deepEqual(
        Glean.autocomplete.dropdownShown
          .testGetValue()
          ?.map(e => e.extra.reopened_after_removal),
        ["false", "true", "true", "true"],
        "The dropdown is shown once by the user and reopened after each removal"
      );
      Assert.deepEqual(
        Glean.autocomplete.dropdownClosed
          .testGetValue()
          ?.map(e => e.extra.close_reason),
        ["removal_prompt", "removal_prompt", "removal_prompt", "dismissed"],
        "Each removal prompt and the final close are recorded"
      );
      assertOneFlow();
    }
  );

  const after = await Services.logins.getAllLogins();
  for (const login of after) {
    if (!before.some(kept => kept.guid == login.guid)) {
      await Services.logins.removeLoginAsync(login);
    }
  }
  for (const login of before) {
    if (!after.some(kept => kept.guid == login.guid)) {
      await Services.logins.addLoginAsync(login);
    }
  }
  gReauthAuthorized = false;
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_reopening_after_removal_starts_a_new_flow() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  gReauthAuthorized = true;
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_URL_PATH },
    async function (browser) {
      const popup = document.getElementById("PopupAutoComplete");
      Services.fog.testResetFOG();
      await openACPopup(popup, browser, "#form-basic-username");

      const dialogClosed = BrowserTestUtils.promiseAlertDialog("cancel");
      await activateDelete(popup);
      await dialogClosed;
      await TestUtils.waitForCondition(
        () => popup.state == "open",
        "Wait for the dropdown to come back after the removal"
      );
      await closePopup(popup);
      const removalFlowId = assertOneFlow();

      await SpecialPowers.spawn(browser, [], () =>
        content.document.querySelector("#form-basic-username").blur()
      );
      Services.fog.testResetFOG();
      await openACPopup(popup, browser, "#form-basic-username");
      const shownEvents = Glean.autocomplete.dropdownShown.testGetValue();
      Assert.equal(shownEvents?.length, 1, "One dropdown shown event");
      Assert.equal(
        shownEvents[0].extra.reopened_after_removal,
        "false",
        "Opening the dropdown again is not a reopen after a removal"
      );
      Assert.notEqual(
        shownEvents[0].extra.flow_id,
        removalFlowId,
        "Opening the dropdown again starts a new flow"
      );
      await closePopup(popup);
    }
  );
  gReauthAuthorized = false;
  await SpecialPowers.popPrefEnv();
});
