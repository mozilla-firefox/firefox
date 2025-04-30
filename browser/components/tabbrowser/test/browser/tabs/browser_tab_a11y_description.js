/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

async function waitForFocusAfterKey(ariaFocus, element, key, accel = false) {
  let event = ariaFocus ? "AriaFocus" : "focus";
  let friendlyKey = key;
  if (accel) {
    friendlyKey = "Accel+" + key;
  }
  key = "KEY_" + key;
  let focused = BrowserTestUtils.waitForEvent(element, event);
  EventUtils.synthesizeKey(key, { accelKey: accel });
  await focused;
  ok(true, element.label + " got " + event + " after " + friendlyKey);
}

function getA11yDescription(element) {
  let descId = element.getAttribute("aria-describedby");
  if (!descId) {
    return null;
  }
  let descElem = document.getElementById(descId);
  if (!descElem) {
    return null;
  }
  return descElem.textContent;
}

add_task(async function testTabA11yDescription() {
  const tab1 = await addTab("http://mochi.test:8888/1", { userContextId: 1 });
  tab1.label = "tab1";
  const context1 = ContextualIdentityService.getUserContextLabel(1);
  const tab2 = await addTab("http://mochi.test:8888/2", { userContextId: 2 });
  tab2.label = "tab2";
  const context2 = ContextualIdentityService.getUserContextLabel(2);
  const tab3 = await addTab("http://mochi.test:8888/3", { userContextId: 3 });
  const context3 = ContextualIdentityService.getUserContextLabel(3);
  const tab4 = await addTab("http://mochi.test:8888/4");
  const tab5 = await addTab("http://mochi.test:8888/5");
  const tabGroupName = crypto.randomUUID();
  const group1 = gBrowser.addTabGroup([tab3, tab4, tab5], {
    label: tabGroupName,
  });

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  let focused = BrowserTestUtils.waitForEvent(tab1, "focus");
  tab1.focus();
  await focused;
  ok(true, "tab1 initially focused");
  ok(
    getA11yDescription(tab1).includes(context1),
    "tab1 has correct a11y description"
  );
  ok(!getA11yDescription(tab2), "tab2 has no a11y description");

  info("Moving DOM focus to tab2");
  await waitForFocusAfterKey(false, tab2, "ArrowRight");
  ok(
    getA11yDescription(tab2).includes(context2),
    "tab2 has correct a11y description"
  );
  ok(!getA11yDescription(tab1), "tab1 has no a11y description");

  info("Moving ARIA focus to tab1");
  await waitForFocusAfterKey(true, tab1, "ArrowLeft", true);
  ok(
    getA11yDescription(tab1).includes(context1),
    "tab1 has correct a11y description"
  );
  ok(!getA11yDescription(tab2), "tab2 has no a11y description");

  info("Removing ARIA focus (reverting to DOM focus)");
  await waitForFocusAfterKey(true, tab2, "ArrowRight");
  ok(
    getA11yDescription(tab2).includes(context2),
    "tab2 has correct a11y description"
  );
  ok(!getA11yDescription(tab1), "tab1 has no a11y description");

  info("Moving past the tab group label");
  await waitForFocusAfterKey(true, group1.labelElement, "ArrowRight");

  info("Moving DOM focus to first tab in tab group");
  await waitForFocusAfterKey(true, tab3, "ArrowRight");
  ok(
    getA11yDescription(tab3).includes(tabGroupName),
    "tab3 (first tab in tab group) should have the tab group name in its description"
  );
  ok(
    getA11yDescription(tab3).includes(context3),
    "tab3 (first tab in tab group) should have its container name in its description"
  );

  info("Moving DOM focus to middle tab in tab group");
  await waitForFocusAfterKey(true, tab4, "ArrowRight");
  ok(
    !getA11yDescription(tab4).includes(tabGroupName),
    "tab4 (middle tab in tab group) should NOT have the tab group label in its description"
  );

  info("Moving DOM focus to last tab in tab group");
  await waitForFocusAfterKey(true, tab5, "ArrowRight");
  ok(
    getA11yDescription(tab5).includes(tabGroupName),
    "tab5 (last tab in tab group) should have the tab group label in its description"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  await removeTabGroup(group1);
});
