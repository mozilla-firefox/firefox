/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const DIALOG_URL =
  "chrome://browser/content/preferences/dialogs/siteContainer.xhtml";

registerCleanupFunction(() => ContextualIdentityService.resetDefault());

async function openContainersPane() {
  await openPreferencesViaOpenPreferencesAPI("containers", { leaveOpen: true });
  return gBrowser.contentWindow;
}

async function getList(win) {
  let control = await settingControlRenders("site-containers-list", win);
  await control.controlEl.updateComplete;
  return control;
}

function getRow(control, site) {
  return control.querySelector(`moz-box-item[value="${site}"]`);
}

function getRenderedSites(control) {
  return Array.from(control.querySelectorAll("moz-box-item")).map(item =>
    item.getAttribute("value")
  );
}

async function addAssociation(site, userContextId) {
  let changed = TestUtils.topicObserved(
    "contextual-identity-site-association-changed"
  );
  ContextualIdentityService.setSiteAssociation(site, userContextId);
  await changed;
}

add_task(async function test_hidden_without_pref() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.containers.switchDuringNavigation.enabled", false]],
  });

  let win = await openContainersPane();
  let addButton = await settingControlRenders(
    "site-containers-add-button",
    win
  );

  Assert.ok(
    addButton.hidden,
    "The site-specific containers card is hidden while the feature is off"
  );

  await BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_idn_site_is_shown_decoded() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.containers.switchDuringNavigation.enabled", true]],
  });

  let [personal] = ContextualIdentityService.getPublicUserContextIds();
  await addAssociation("b\u00FCcher.example", personal);

  let win = await openContainersPane();
  let control = await getList(win);
  let row = getRow(control, "xn--bcher-kva.example");

  Assert.ok(
    row,
    "The row is keyed on the host the association is stored under"
  );
  Assert.equal(
    row.getAttribute("label"),
    "b\u00FCcher.example",
    "An IDN host is shown decoded, not in punycode"
  );

  ContextualIdentityService.removeSiteAssociation("xn--bcher-kva.example");
  await BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_list_and_dialog() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.containers.switchDuringNavigation.enabled", true]],
  });

  let [personal, work] = ContextualIdentityService.getPublicUserContextIds();
  await addAssociation("example.com", personal);

  let win = await openContainersPane();
  let control = await getList(win);

  Assert.deepEqual(
    getRenderedSites(control),
    ["example.com"],
    "The associated site is listed"
  );

  let select = getRow(control, "example.com").querySelector("container-select");
  Assert.equal(
    select.value,
    String(personal),
    "The row shows the container the site is bound to"
  );

  await changeMozSelectValue(select, String(work));

  Assert.equal(
    ContextualIdentityService.getSiteAssociation("example.com"),
    work,
    "Picking another container rebinds the site"
  );

  let dialogPromise = promiseLoadSubDialog(DIALOG_URL);
  win.document.getElementById("site-containers-add-button").click();
  let dialogWin = await dialogPromise;
  let dialogDoc = dialogWin.document;

  let siteInput = dialogDoc.querySelector("moz-input-text");
  let acceptButton = dialogDoc.querySelector("dialog").getButton("accept");

  Assert.ok(acceptButton.disabled, "The dialog opens with the button disabled");

  let dialogSelect = dialogDoc.querySelector("container-select");
  Assert.equal(
    dialogSelect.value,
    String(personal),
    "The dialog offers the first container"
  );

  let setSite = value => {
    siteInput.value = value;
    SpecialPowers.dispatchEvent(
      dialogWin,
      siteInput,
      new dialogWin.InputEvent("input", { data: value, bubbles: true })
    );
  };

  setSite("not a host");
  Assert.ok(!acceptButton.disabled, "A non-blank value enables the button");

  setSite("   ");
  Assert.ok(acceptButton.disabled, "A blank value disables the button");

  let errorLabel = dialogDoc.getElementById("siteContainerError");
  let errorId = () => errorLabel.getAttribute("data-l10n-id");

  let submitAndGetError = value => {
    setSite(value);
    Assert.equal(errorId(), null, "Typing clears the previous error");
    acceptButton.click();
    return errorId();
  };

  Assert.equal(
    submitAndGetError("not a host"),
    "containers-site-invalid-error",
    "Saving a value that is not a host reports an invalid website"
  );
  Assert.ok(
    siteInput.hasAttribute("invalid"),
    "The website field is marked invalid"
  );

  Assert.equal(
    submitAndGetError("file:///tmp/index.html"),
    "containers-site-invalid-error",
    "Saving a URL that is neither http nor https reports an invalid website"
  );

  Assert.equal(
    submitAndGetError("example.com"),
    "containers-site-duplicate-error",
    "Saving an already associated site reports a duplicate"
  );

  Assert.equal(
    // eslint-disable-next-line sdl/no-insecure-url
    submitAndGetError("http://example.com/path"),
    "containers-site-duplicate-error",
    "A pasted URL is reduced to its host, whatever the scheme"
  );

  setSite("https://mozilla.org/path");
  Assert.ok(
    !siteInput.hasAttribute("invalid"),
    "Typing clears the invalid state of the website field"
  );

  dialogSelect.value = String(work);

  let dialogClosed = BrowserTestUtils.waitForEvent(
    win.gSubDialog._dialogStack,
    "dialogclose"
  );
  acceptButton.click();
  await dialogClosed;

  await BrowserTestUtils.waitForMutationCondition(
    control,
    { childList: true, subtree: true, attributes: true },
    () => getRenderedSites(control).includes("mozilla.org")
  );

  Assert.equal(
    ContextualIdentityService.getSiteAssociation("mozilla.org"),
    work,
    "The dialog binds the host of the given URL to the chosen container"
  );

  synthesizeClick(
    getRow(control, "mozilla.org").querySelector("moz-button[action=remove]")
  );

  await BrowserTestUtils.waitForMutationCondition(
    control,
    { childList: true, subtree: true, attributes: true },
    () => !getRenderedSites(control).includes("mozilla.org")
  );

  Assert.equal(
    ContextualIdentityService.getSiteAssociation("mozilla.org"),
    0,
    "The delete button drops the association"
  );

  ContextualIdentityService.remove(work);

  await BrowserTestUtils.waitForMutationCondition(
    control,
    { childList: true, subtree: true, attributes: true },
    () => !getRenderedSites(control).length
  );

  Assert.equal(
    ContextualIdentityService.getSiteAssociation("example.com"),
    0,
    "Removing a container drops the sites bound to it"
  );

  await BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await SpecialPowers.popPrefEnv();
});
