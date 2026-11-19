/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { NavigableManager } = ChromeUtils.importESModule(
  "chrome://remote/content/shared/NavigableManager.sys.mjs"
);
const { UserContextManagerClass } = ChromeUtils.importESModule(
  "chrome://remote/content/shared/UserContextManager.sys.mjs"
);

add_task(async function test_invalid() {
  const userContextManager = new UserContextManagerClass();

  // Check invalid types for hasUserContextId/getInternalIdById which expects
  // a string.
  for (const value of [null, undefined, 1, [], {}]) {
    is(userContextManager.hasUserContextId(value), false);
    is(userContextManager.getInternalIdById(value), null);
  }

  // Check an invalid value for APIs which expect either "default" or a UUID
  // from Services.uuid.generateUUID.
  is(userContextManager.hasUserContextId("foo"), false);
  is(userContextManager.getInternalIdById("foo"), null);

  // Check invalid types for getIdByInternalId which expects a number.
  for (const value of [null, undefined, "foo", [], {}]) {
    is(userContextManager.getIdByInternalId(value), null);
  }

  // Check invalid values/types for getIdByNavigableId  all return null.
  for (const value of [null, undefined, "foo", [], {}]) {
    is(userContextManager.getIdByNavigableId(value), null);
  }

  userContextManager.destroy();
});

add_task(async function test_default_context() {
  const userContextManager = new UserContextManagerClass();
  ok(
    userContextManager.hasUserContextId("default"),
    `Context id default is known by the manager`
  );
  ok(
    userContextManager.getUserContextIds().includes("default"),
    `Context id default is listed by the manager`
  );
  is(
    userContextManager.getInternalIdById("default"),
    0,
    "Default user context has the expected internal id"
  );

  userContextManager.destroy();
});

add_task(async function test_new_internal_contexts() {
  info("Create a new user context with ContextualIdentityService");
  const beforeInternalId =
    ContextualIdentityService.create("before").userContextId;

  info("Create the UserContextManager");
  const userContextManager = new UserContextManagerClass();

  const beforeContextId =
    userContextManager.getIdByInternalId(beforeInternalId);
  assertContextAvailable(userContextManager, beforeContextId, beforeInternalId);

  info("Create another user context with ContextualIdentityService");
  const afterInternalId =
    ContextualIdentityService.create("after").userContextId;
  const afterContextId = userContextManager.getIdByInternalId(afterInternalId);
  assertContextAvailable(userContextManager, afterContextId, afterInternalId);

  info("Delete both user contexts");
  ContextualIdentityService.remove(beforeInternalId);
  ContextualIdentityService.remove(afterInternalId);
  assertContextRemoved(userContextManager, afterContextId, afterInternalId);
  assertContextRemoved(userContextManager, beforeContextId, beforeInternalId);

  userContextManager.destroy();
});

add_task(async function test_create_remove_context() {
  NavigableManager.startTracking();
  const userContextManager = new UserContextManagerClass();

  try {
    for (const closeContextTabs of [true, false]) {
      info("Create two contexts via createContext");
      const userContextId1 = userContextManager.createContext();
      const internalId1 = userContextManager.getInternalIdById(userContextId1);
      assertContextAvailable(userContextManager, userContextId1);

      const userContextId2 = userContextManager.createContext();
      const internalId2 = userContextManager.getInternalIdById(userContextId2);
      assertContextAvailable(userContextManager, userContextId2);

      info("Create tabs in various user contexts");
      const url = "https://example.com/document-builder.sjs?html=tab";
      const tabDefault = await addTab(gBrowser, url);
      const tabContext1 = await addTab(gBrowser, url, {
        userContextId: internalId1,
      });
      const tabContext2 = await addTab(gBrowser, url, {
        userContextId: internalId2,
      });
      is(
        userContextManager.getIdByNavigableId(
          NavigableManager.getIdForBrowsingContext(
            tabDefault.linkedBrowser.browsingContext
          )
        ),
        "default"
      );
      is(
        userContextManager.getIdByNavigableId(
          NavigableManager.getIdForBrowsingContext(
            tabContext1.linkedBrowser.browsingContext
          )
        ),
        userContextId1
      );
      is(
        userContextManager.getIdByNavigableId(
          NavigableManager.getIdForBrowsingContext(
            tabContext2.linkedBrowser.browsingContext
          )
        ),
        userContextId2
      );

      info("Remove the user context 1 via removeUserContext");
      userContextManager.removeUserContext(userContextId1, {
        closeContextTabs,
      });

      assertContextRemoved(userContextManager, userContextId1, internalId1);
      if (closeContextTabs) {
        ok(!gBrowser.tabs.includes(tabContext1), "Tab context 1 is closed");
      } else {
        ok(gBrowser.tabs.includes(tabContext1), "Tab context 1 is not closed");
      }
      ok(gBrowser.tabs.includes(tabDefault), "Tab default is not closed");
      ok(gBrowser.tabs.includes(tabContext2), "Tab context 2 is not closed");

      info("Remove the user context 2 via removeUserContext");
      userContextManager.removeUserContext(userContextId2, {
        closeContextTabs,
      });
      assertContextRemoved(userContextManager, userContextId2, internalId2);
      if (closeContextTabs) {
        ok(!gBrowser.tabs.includes(tabContext2), "Tab context 2 is closed");
      } else {
        ok(gBrowser.tabs.includes(tabContext2), "Tab context 2 is not closed");
      }
      ok(gBrowser.tabs.includes(tabDefault), "Tab default is not closed");
    }
  } finally {
    NavigableManager.stopTracking();
    userContextManager.destroy();
  }
});

add_task(async function test_create_context_prefix() {
  const userContextManager = new UserContextManagerClass();

  info("Create a context with a custom prefix via createContext");
  const userContextId = userContextManager.createContext({
    prefix: "test_prefix",
  });
  const internalId = userContextManager.getInternalIdById(userContextId);
  const identity =
    ContextualIdentityService.getPublicIdentityFromId(internalId);
  ok(
    identity.name.startsWith("test_prefix"),
    "The new identity used the provided prefix"
  );

  userContextManager.removeUserContext(userContextId);
  userContextManager.destroy();
});

add_task(async function test_create_context_icon_and_color() {
  const userContextManager = new UserContextManagerClass();

  info("Create a context without icon or color via createContext");
  const userContextId1 = userContextManager.createContext();
  const internalId1 = userContextManager.getInternalIdById(userContextId1);
  const identity1 =
    ContextualIdentityService.getPublicIdentityFromId(internalId1);
  is(identity1.icon, "circle", "The new identity uses the default icon");
  is(identity1.color, "gray", "The new identity uses the default color");

  userContextManager.removeUserContext(userContextId1);

  info("Create a context with a custom icon and color via createContext");
  const userContextId2 = userContextManager.createContext({
    color: "red",
    icon: "robot",
  });
  const internalId2 = userContextManager.getInternalIdById(userContextId2);
  const identity2 =
    ContextualIdentityService.getPublicIdentityFromId(internalId2);
  is(identity2.icon, "robot", "The new identity used the provided icon");
  is(identity2.color, "red", "The new identity used the provided color");

  userContextManager.removeUserContext(userContextId2);
  userContextManager.destroy();
});

add_task(async function test_create_context_name() {
  const userContextManager = new UserContextManagerClass();

  info("Create a context with a custom name via createContext");
  const userContextId1 = userContextManager.createContext({
    name: "foo",
  });
  const internalId1 = userContextManager.getInternalIdById(userContextId1);
  const identity1 =
    ContextualIdentityService.getPublicIdentityFromId(internalId1);
  is(identity1.name, "foo", "The new identity used the provided name");
  is(
    userContextManager.getUserContextIdsByName(identity1.name).length,
    1,
    "getUserContextIdsByName returned the expected number of context ids"
  );
  ok(
    userContextManager
      .getUserContextIdsByName(identity1.name)
      .includes(userContextId1),
    "getUserContextIdsByName returns the expected user context id"
  );

  info("Create another context with the same custom name via createContext");
  const userContextId2 = userContextManager.createContext({
    name: "foo",
  });
  const internalId2 = userContextManager.getInternalIdById(userContextId2);
  const identity2 =
    ContextualIdentityService.getPublicIdentityFromId(internalId2);
  is(identity2.name, "foo", "The new identity used the (same) provided name");
  is(
    userContextManager.getUserContextIdsByName(identity2.name).length,
    2,
    "getUserContextIdsByName returned the expected number of context ids"
  );
  ok(
    userContextManager
      .getUserContextIdsByName(identity2.name)
      .includes(userContextId2),
    "getUserContextIdsByName returns the expected user context id"
  );

  info("Create another context with another custom name");
  const userContextId3 = userContextManager.createContext({
    name: "bar",
  });
  const internalId3 = userContextManager.getInternalIdById(userContextId3);
  const identity3 =
    ContextualIdentityService.getPublicIdentityFromId(internalId3);
  is(identity3.name, "bar", "The new identity used the provided name");
  is(
    userContextManager.getUserContextIdsByName(identity3.name).length,
    1,
    "getUserContextIdsByName returned the expected number of context ids"
  );
  ok(
    userContextManager
      .getUserContextIdsByName(identity3.name)
      .includes(userContextId3),
    "getUserContextIdsByName returns the expected user context id"
  );

  info("Create another context with a custom name and a prefix");
  const userContextId4 = userContextManager.createContext({
    name: "baz",
    prefix: "should-be-ignored",
  });
  const internalId4 = userContextManager.getInternalIdById(userContextId4);
  const identity4 =
    ContextualIdentityService.getPublicIdentityFromId(internalId4);
  is(
    identity4.name,
    "baz",
    "The new identity used the provided name without using the prefix"
  );
  is(
    userContextManager.getUserContextIdsByName(identity4.name).length,
    1,
    "getUserContextIdsByName returned the expected number of context ids"
  );
  ok(
    userContextManager
      .getUserContextIdsByName(identity4.name)
      .includes(userContextId4),
    "getUserContextIdsByName returns the expected user context id"
  );

  for (const invalidName of [null, undefined, "", "   "]) {
    info("Create a context with an invalid name");
    const invalidUserContextId = userContextManager.createContext({
      name: invalidName,
      prefix: "should-be-used",
    });
    const invalidInternalId =
      userContextManager.getInternalIdById(invalidUserContextId);
    const invalidIdentity =
      ContextualIdentityService.getPublicIdentityFromId(invalidInternalId);
    ok(
      invalidIdentity.name.startsWith("should-be-used"),
      "The identity used the prefix"
    );
    is(
      userContextManager.getUserContextIdsByName(invalidIdentity.name).length,
      1,
      "getUserContextIdsByName returned the expected number of context ids"
    );
    ok(
      userContextManager
        .getUserContextIdsByName(invalidIdentity.name)
        .includes(invalidUserContextId),
      "getUserContextIdsByName returns the expected user context id"
    );
    userContextManager.removeUserContext(invalidUserContextId);
  }

  userContextManager.removeUserContext(userContextId1);
  userContextManager.removeUserContext(userContextId2);
  userContextManager.removeUserContext(userContextId3);
  userContextManager.removeUserContext(userContextId4);
  userContextManager.destroy();
});

add_task(async function test_events() {
  const manager = new UserContextManagerClass();
  const userContextCreatedEvents = [];
  const userContextDeletedEvents = [];
  manager.on("user-context-created", (name, data) =>
    userContextCreatedEvents.push(data)
  );
  manager.on("user-context-deleted", (name, data) =>
    userContextDeletedEvents.push(data)
  );

  const contextId1 = manager.createContext();
  is(userContextCreatedEvents.length, 1);
  is(userContextCreatedEvents[0].userContextId, contextId1);
  is(userContextDeletedEvents.length, 0);

  manager.removeUserContext(contextId1);
  is(userContextCreatedEvents.length, 1);
  is(userContextDeletedEvents.length, 1);
  is(userContextDeletedEvents[0].userContextId, contextId1);

  manager.destroy();
});

add_task(async function test_several_managers() {
  const manager1 = new UserContextManagerClass();
  const manager2 = new UserContextManagerClass();

  info("Create a context via manager1");
  const contextId1 = manager1.createContext();
  const internalId = manager1.getInternalIdById(contextId1);
  assertContextUnknown(manager2, contextId1);

  info("Retrieve the corresponding user context id in manager2");
  const contextId2 = manager2.getIdByInternalId(internalId);
  is(
    typeof contextId2,
    "string",
    "manager2 has a valid id for the user context created by manager 1"
  );

  Assert.notEqual(
    contextId1,
    contextId2,
    "manager1 and manager2 have different ids for the same internal context id"
  );

  info("Remove the user context via manager2");
  manager2.removeUserContext(contextId2);

  info("Check that the user context is removed from both managers");
  assertContextRemoved(manager1, contextId1, internalId);
  assertContextRemoved(manager2, contextId2, internalId);

  manager1.destroy();
  manager2.destroy();
});

function assertContextAvailable(manager, contextId, expectedInternalId = null) {
  ok(
    manager.getUserContextIds().includes(contextId),
    `Context id ${contextId} is listed by the manager`
  );
  ok(
    manager.hasUserContextId(contextId),
    `Context id ${contextId} is known by the manager`
  );

  const internalId = manager.getInternalIdById(contextId);
  if (expectedInternalId != null) {
    is(internalId, expectedInternalId, "Internal id has the expected value");
  }

  is(
    typeof internalId,
    "number",
    `Context id ${contextId} corresponds to a valid internal id (${internalId})`
  );
  is(
    manager.getIdByInternalId(internalId),
    contextId,
    `Context id ${contextId} is returned for internal id ${internalId}`
  );
  ok(
    ContextualIdentityService.getPublicUserContextIds().includes(internalId),
    `User context for context id ${contextId} is found by ContextualIdentityService`
  );
}

function assertContextUnknown(manager, contextId) {
  ok(
    !manager.getUserContextIds().includes(contextId),
    `Context id ${contextId} is not listed by the manager`
  );
  ok(
    !manager.hasUserContextId(contextId),
    `Context id ${contextId} is not known by the manager`
  );
  is(
    manager.getInternalIdById(contextId),
    null,
    `Context id ${contextId} does not match any internal id`
  );
}

function assertContextRemoved(manager, contextId, internalId) {
  assertContextUnknown(manager, contextId);
  is(
    manager.getIdByInternalId(internalId),
    null,
    `Internal id ${internalId} cannot be converted to user context id`
  );
  ok(
    !ContextualIdentityService.getPublicUserContextIds().includes(internalId),
    `Internal id ${internalId} is not found in ContextualIdentityService`
  );
}

add_task(async function test_external_deletion_emits_event() {
  const manager = new UserContextManagerClass();
  const userContextId = manager.createContext({ prefix: "test" });
  const internalId = manager.getInternalIdById(userContextId);

  const deletedPromise = new Promise(resolve => {
    manager.on("user-context-deleted", function onDeleted(eventName, data) {
      manager.off("user-context-deleted", onDeleted);
      resolve(data);
    });
  });

  info("Remove the user context externally via ContextualIdentityService");
  ContextualIdentityService.remove(internalId);

  const data = await deletedPromise;
  is(
    data.userContextId,
    userContextId,
    "Event payload contains correct userContextId"
  );
  is(data.internalId, internalId, "Event payload contains correct internalId");

  manager.destroy();
});
