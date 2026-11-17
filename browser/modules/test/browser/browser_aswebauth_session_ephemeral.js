/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_aswebauth.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_aswebauth.js",
  this
);

add_task(async function test_leftover_ephemeral_container_cleanup() {
  let container = await ContextualIdentityService.create(
    "ephemeral-aswebauth-" + newUUID(),
    "fingerprint",
    "blue"
  );

  await ASWebAuthSessionService.removeLeftoverEphemeralContainers();

  Assert.equal(
    ContextualIdentityService.getPublicIdentityFromId(container.userContextId),
    null,
    "leftover ephemeral container is removed"
  );
});

add_task(async function test_active_ephemeral_container_survives_cleanup() {
  let uuid = newUUID();
  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: true,
  });

  let win = await winPromise;
  let browser = win.gBrowser.selectedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, null, true);

  let attrs = await SpecialPowers.spawn(browser, [], () => {
    return content.document.nodePrincipal.originAttributes;
  });
  Assert.greater(attrs.userContextId, 0, "ephemeral session has a container");

  await ASWebAuthSessionService.removeLeftoverEphemeralContainers();

  Assert.ok(
    ContextualIdentityService.getPublicIdentityFromId(attrs.userContextId),
    "the container of a running session is left alone"
  );

  await BrowserTestUtils.closeWindow(win);
  let data = JSON.parse(await cancelPromise);
  Assert.equal(data.uuid, uuid, "user-cancel fires on close");
});

add_task(async function test_ephemeral_callback() {
  let uuid = newUUID();
  let completePromise = waitForObserver("aswebauthsession-test-complete");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: true,
  });

  let win = await winPromise;
  let browser = win.gBrowser.selectedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, null, true);

  let attrs = await SpecialPowers.spawn(browser, [], () => {
    return content.document.nodePrincipal.originAttributes;
  });
  Assert.equal(
    attrs.privateBrowsingId,
    1,
    "ephemeral session uses a private window"
  );
  Assert.greater(
    attrs.userContextId,
    0,
    "ephemeral session has a unique container"
  );

  let contextId = attrs.userContextId;
  let closedPromise = BrowserTestUtils.domWindowClosed(win);
  BrowserTestUtils.startLoadingURIString(browser, CALLBACK_URL);

  let data = JSON.parse(await completePromise);
  Assert.equal(data.uuid, uuid, "complete notification has the correct UUID");
  await closedPromise;

  let identity = ContextualIdentityService.getPublicIdentityFromId(contextId);
  Assert.equal(
    identity,
    null,
    "container is removed after the callback completes"
  );
});

add_task(async function test_ephemeral_cancellation() {
  let uuid = newUUID();
  let cancelPromise = waitForObserver("aswebauthsession-test-cancel");
  let winPromise = BrowserTestUtils.waitForNewWindow();

  startAuthSession({
    url: LOGIN_URL,
    uuid,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: true,
  });

  let win = await winPromise;
  let browser = win.gBrowser.selectedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, null, true);

  let attrs = await SpecialPowers.spawn(browser, [], () => {
    return content.document.nodePrincipal.originAttributes;
  });
  let contextId = attrs.userContextId;
  Assert.greater(contextId, 0, "ephemeral session has a container");

  await BrowserTestUtils.closeWindow(win);
  let data = JSON.parse(await cancelPromise);
  Assert.equal(data.uuid, uuid, "user-cancel fires on close");

  let identity = ContextualIdentityService.getPublicIdentityFromId(contextId);
  Assert.equal(identity, null, "container is removed after user cancels");
});

add_task(async function test_concurrent_ephemeral_isolation() {
  let uuid1 = newUUID();
  let uuid2 = newUUID();

  let win1Promise = BrowserTestUtils.waitForNewWindow();
  startAuthSession({
    url: LOGIN_URL,
    uuid: uuid1,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: true,
  });
  let win1 = await win1Promise;
  await BrowserTestUtils.browserLoaded(
    win1.gBrowser.selectedBrowser,
    false,
    null,
    true
  );

  let win2Promise = BrowserTestUtils.waitForNewWindow();
  startAuthSession({
    url: LOGIN_URL,
    uuid: uuid2,
    callbackScheme: CALLBACK_SCHEME,
    ephemeral: true,
  });
  let win2 = await win2Promise;
  await BrowserTestUtils.browserLoaded(
    win2.gBrowser.selectedBrowser,
    false,
    null,
    true
  );

  let attrs1 = await SpecialPowers.spawn(
    win1.gBrowser.selectedBrowser,
    [],
    () => content.document.nodePrincipal.originAttributes
  );
  let attrs2 = await SpecialPowers.spawn(
    win2.gBrowser.selectedBrowser,
    [],
    () => content.document.nodePrincipal.originAttributes
  );

  Assert.greater(attrs1.userContextId, 0, "first session has a container");
  Assert.greater(attrs2.userContextId, 0, "second session has a container");
  Assert.notEqual(
    attrs1.userContextId,
    attrs2.userContextId,
    "concurrent ephemeral sessions use different containers"
  );

  let cancel1 = waitForObserver("aswebauthsession-test-cancel");
  await BrowserTestUtils.closeWindow(win1);
  let cancelData1 = JSON.parse(await cancel1);
  Assert.equal(cancelData1.uuid, uuid1, "first session cancel has its UUID");

  let cancel2 = waitForObserver("aswebauthsession-test-cancel");
  await BrowserTestUtils.closeWindow(win2);
  let cancelData2 = JSON.parse(await cancel2);
  Assert.equal(cancelData2.uuid, uuid2, "second session cancel has its UUID");
});
