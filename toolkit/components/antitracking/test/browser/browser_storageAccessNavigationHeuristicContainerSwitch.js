/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// example.org (container 1) -> example.com (interaction) -> example.org
// -> example.com (interaction) -> test1.example.org (container 2, new tab)
//
// Navigating back to a site after interacting with another grants the latter
// storage access under the former (DynamicFpiNavigationHeuristic). The heuristic
// reads the session history and the user activation of the tab the load runs
// in. A per-site container association (Bug 2052136) retargets a load into a
// new tab in another container, which must not be granted access from those of
// the tab it is leaving.

const ORIGIN_RETURNED = "https://example.org";
const ORIGIN_INTERACTED = "https://example.com";
const HOST_BOUND = "test1.example.org";
const ORIGIN_BOUND = `https://${HOST_BOUND}`;

const OTHER_CONTAINER = 1;
const BOUND_CONTAINER = 2;

const GRANT = `3rdPartyStorage^${ORIGIN_INTERACTED}`;

function url(origin) {
  return (
    getRootDirectory(gTestPath).replace("chrome://mochitests/content", origin) +
    "page.html"
  );
}

function isGranted(origin, userContextId) {
  return (
    Services.perms.testPermissionFromPrincipal(
      Services.scriptSecurityManager.createContentPrincipal(
        Services.io.newURI(origin),
        { userContextId }
      ),
      GRANT
    ) == Services.perms.ALLOW_ACTION
  );
}

// Navigates from the page without user activation, so the navigation continues
// the extended navigation of the tab, which holds the user activation the
// heuristic reads. Injected so the page, not the system principal of the
// content task, triggers the load.
async function navigateByScript(browser, targetURL) {
  await SpecialPowers.spawn(browser, [targetURL], target => {
    let script = content.document.createElement("script");
    script.textContent = `location.href = ${JSON.stringify(target)};`;
    content.document.body.appendChild(script);
  });
}

async function interactAndLeave(browser, targetURL) {
  let loaded = BrowserTestUtils.browserLoaded(
    browser,
    false,
    url(ORIGIN_INTERACTED)
  );
  await navigateByScript(browser, url(ORIGIN_INTERACTED));
  await loaded;
  await SpecialPowers.spawn(browser, [], () => {
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.document.userInteractionForTesting();
    SpecialPowers.wrap(content.document).clearUserGestureActivation();
  });
  await navigateByScript(browser, targetURL);
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.antitracking.enableWebcompat", true],
      ["privacy.restrict3rdpartystorage.heuristic.navigation", true],
      // The heuristic reads the user activation of the extended navigation
      // which Bounce Tracking Protection records per tab.
      [
        "privacy.bounceTrackingProtection.mode",
        Ci.nsIBounceTrackingProtection.MODE_ENABLED_DRY_RUN,
      ],
      // Otherwise permissions, the storage access grant included, are shared
      // across containers.
      ["permissions.isolateBy.userContext", true],
      ["privacy.userContext.enabled", true],
      ["privacy.containers.switchDuringNavigation.enabled", true],
    ],
  });
  ContextualIdentityService.setSiteAssociation(HOST_BOUND, BOUND_CONTAINER);
  registerCleanupFunction(() => {
    ContextualIdentityService.removeSiteAssociation(HOST_BOUND);
    Services.perms.removeByType(GRANT);
  });
});

add_task(async function test_retargeted_load_is_not_granted_storage_access() {
  let tab = BrowserTestUtils.addTab(gBrowser, url(ORIGIN_RETURNED), {
    userContextId: OTHER_CONTAINER,
  });
  gBrowser.selectedTab = tab;
  let browser = tab.linkedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, url(ORIGIN_RETURNED));

  info("Without a container switch the heuristic grants access.");
  let loaded = BrowserTestUtils.browserLoaded(
    browser,
    false,
    url(ORIGIN_RETURNED)
  );
  await interactAndLeave(browser, url(ORIGIN_RETURNED));
  await loaded;
  ok(
    isGranted(ORIGIN_RETURNED, OTHER_CONTAINER),
    `${ORIGIN_INTERACTED} is granted storage access under ${ORIGIN_RETURNED} ` +
      `in container ${OTHER_CONTAINER}`
  );

  info("Retargeted into another container it must not.");
  let retargetedPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    url(ORIGIN_BOUND),
    true
  );
  await interactAndLeave(browser, url(ORIGIN_BOUND));
  let retargeted = await retargetedPromise;
  is(
    retargeted.userContextId,
    BOUND_CONTAINER,
    "The bound host landed in the container it is bound to"
  );
  ok(
    !isGranted(ORIGIN_BOUND, BOUND_CONTAINER),
    `${ORIGIN_INTERACTED} is not granted storage access under ${ORIGIN_BOUND} ` +
      `in container ${BOUND_CONTAINER}`
  );

  BrowserTestUtils.removeTab(retargeted);
  BrowserTestUtils.removeTab(tab);
});
