/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

registerCleanupFunction(() => ContextualIdentityService.resetDefault());

function identityColor(userContextId) {
  return ContextualIdentityService.getPublicIdentityFromId(userContextId).color;
}

function colorClasses(element) {
  return [...element.classList].filter(cls =>
    cls.startsWith("identity-color-")
  );
}

function panelItems(select) {
  return [...select.panelList.querySelectorAll("panel-item")];
}

async function openSelect() {
  await openPreferencesViaOpenPreferencesAPI("containers", { leaveOpen: true });
  let win = gBrowser.contentWindow;
  let control = await settingControlRenders("site-containers-list", win);
  await control.controlEl.updateComplete;

  let select = control.querySelector(
    'moz-box-item[value="example.com"] container-select'
  );
  await select.updateComplete;
  return select;
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.containers.switchDuringNavigation.enabled", true]],
  });

  let [personal] = ContextualIdentityService.getPublicUserContextIds();
  let changed = TestUtils.topicObserved(
    "contextual-identity-site-association-changed"
  );
  ContextualIdentityService.setSiteAssociation("example.com", personal);
  await changed;
});

add_task(async function test_options_carry_the_container_color() {
  let select = await openSelect();
  let identities = ContextualIdentityService.getPublicIdentities();

  Assert.deepEqual(
    select.options.map(option => option.itemClass),
    identities.map(identity => `identity-color-${identity.color}`),
    "Every option carries the color class of the container it offers"
  );
  Assert.deepEqual(
    panelItems(select).map(item => colorClasses(item)),
    identities.map(identity => [`identity-color-${identity.color}`]),
    "Every panel row is classed with the color of its option"
  );

  await BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function test_host_follows_the_selected_option() {
  let [personal, work] = ContextualIdentityService.getPublicUserContextIds();
  let select = await openSelect();

  Assert.deepEqual(
    colorClasses(select),
    [`identity-color-${identityColor(personal)}`],
    "The select is classed with the color of the bound container"
  );

  await changeMozSelectValue(select, String(work));
  await select.updateComplete;

  Assert.deepEqual(
    colorClasses(select),
    [`identity-color-${identityColor(work)}`],
    "Picking another container replaces the color class"
  );

  await BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function test_recoloring_a_container_repaints_the_options() {
  let select = await openSelect();
  let userContextId = parseInt(select.value, 10);
  let index = select.options.findIndex(option => option.value == select.value);

  let identity =
    ContextualIdentityService.getPublicIdentityFromId(userContextId);
  let newColor = ContextualIdentityService.containerColors.find(
    color => color != identity.color
  );
  ContextualIdentityService.update(
    userContextId,
    ContextualIdentityService.getUserContextLabel(userContextId),
    identity.icon,
    newColor
  );

  await TestUtils.waitForCondition(
    () => select.options[index]?.itemClass == `identity-color-${newColor}`,
    "The option picks up the new color"
  );
  await select.updateComplete;

  Assert.deepEqual(
    colorClasses(panelItems(select)[index]),
    [`identity-color-${newColor}`],
    "The panel row of the recolored container drops the previous color class"
  );
  Assert.deepEqual(
    colorClasses(select),
    [`identity-color-${newColor}`],
    "The select follows the recolored container it is bound to"
  );

  await BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
