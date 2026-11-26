/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_PAGE = "https://example.com/document-builder.sjs?html=tab";

const { assertUpdate, createSessionDataUpdate, getUpdates } =
  SessionDataUpdateHelpers;

// Test session data update scenarios using the UserContext ContextDescriptor type.
add_task(async function test_session_data_update_user_contexts() {
  info("Open a new tab");
  const tab = await addTab(TEST_PAGE);
  const browsingContext = tab.linkedBrowser.browsingContext;
  const root = createRootMessageHandler("session-data-update-user-contexts");

  info("Add three items: one globally, one in a context one in a user context");
  await root.updateSessionData([
    createSessionDataUpdate(["global"], "add", "category1"),
    createSessionDataUpdate(["browsing context"], "add", "category1", {
      type: ContextDescriptorType.TopBrowsingContext,
      id: browsingContext.browserId,
    }),
    createSessionDataUpdate(["user context"], "add", "category1", {
      type: ContextDescriptorType.UserContext,
      id: browsingContext.originAttributes.userContextId,
    }),
  ]);

  let processedUpdates = await getUpdates(root, browsingContext);
  is(processedUpdates.length, 1);
  assertUpdate(
    processedUpdates.at(-1),
    ["global", "browsing context", "user context"],
    "category1"
  );

  info("Open a new tab on the same test URL");
  const tab2 = await addTab(TEST_PAGE);
  const browsingContext2 = tab2.linkedBrowser.browsingContext;

  // Make sure that this browsing context has only 2 items
  processedUpdates = await getUpdates(root, browsingContext2);
  is(processedUpdates.length, 1);
  assertUpdate(
    processedUpdates.at(-1),
    ["global", "user context"],
    "category1"
  );

  root.destroy();
});

add_task(async function test_session_data_update_with_two_user_contexts() {
  info("Open a new tab in the default user context");
  const tab1 = await addTab(TEST_PAGE);
  const browsingContext1 = tab1.linkedBrowser.browsingContext;

  info("Create a non-default user context");
  const userContext = ContextualIdentityService.create("test");
  const userContextId = userContext.userContextId;
  registerCleanupFunction(() =>
    ContextualIdentityService.remove(userContextId)
  );

  info("Open a new tab in the non-default user context");
  const tab2 = BrowserTestUtils.addTab(gBrowser, TEST_PAGE, {
    userContextId,
  });
  const browsingContext2 = tab2.linkedBrowser.browsingContext;

  const root = createRootMessageHandler(
    "session-data-update-two-user-contexts"
  );

  info("Add an item in the non-default user context");
  await root.updateSessionData([
    createSessionDataUpdate(["non-default user context"], "add", "category1", {
      type: ContextDescriptorType.UserContext,
      id: userContextId,
    }),
  ]);

  let processedUpdates = await getUpdates(root, browsingContext1);
  is(processedUpdates.length, 0);

  processedUpdates = await getUpdates(root, browsingContext2);
  is(processedUpdates.length, 1);
  assertUpdate(
    processedUpdates.at(-1),
    ["non-default user context"],
    "category1"
  );

  info("Open a new tab in the non-default user context");
  const tab3 = BrowserTestUtils.addTab(gBrowser, TEST_PAGE, {
    userContextId,
  });
  await BrowserTestUtils.browserLoaded(tab3.linkedBrowser);
  const browsingContext3 = tab3.linkedBrowser.browsingContext;

  // Make sure that this browsing context also has the session data item
  processedUpdates = await getUpdates(root, browsingContext3);
  is(processedUpdates.length, 1);
  assertUpdate(
    processedUpdates.at(-1),
    ["non-default user context"],
    "category1"
  );

  root.destroy();

  BrowserTestUtils.removeTab(tab2);
  BrowserTestUtils.removeTab(tab3);
});
