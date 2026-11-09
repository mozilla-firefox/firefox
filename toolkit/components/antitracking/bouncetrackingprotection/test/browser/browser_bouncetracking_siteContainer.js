/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Bounce Tracking Protection vs. the per-site container association of Bug
 * 2052136: a top-level navigation to a site bound to a container is retargeted
 * into a new tab in that container.
 *
 * BTP keys its state on OriginAttributes and holds the extended navigation
 * record per tab, in a BounceTrackingState which snapshots the tab's
 * OriginAttributes once. Retargeting splits one extended navigation across two
 * tabs, so a chain's hops need not all run in the same container. The record
 * follows the load into the tab it is retargeted into, and each hop is recorded
 * with the container it ran in: classification and purging follow the container
 * each hop actually ran in, and an extended navigation keeps its meaning across
 * the switch: its destination stays exempt, its intermediate hops stay
 * classifiable.
 *
 * Each task's comment starts with its navigation chain. A hop's container is in
 * parentheses where it matters, and "new tab" marks where the retarget moves the
 * load out of the tab it started in.
 */

requestLongerTimeout(6);

// Any two distinct default containers.
const BOUND_CONTAINER = 2;
const OTHER_CONTAINER = 1;

const OA_DEFAULT = {};
const OA_BOUND = { userContextId: BOUND_CONTAINER };
const OA_OTHER = { userContextId: OTHER_CONTAINER };
// A container no tab in this file runs in.
const OA_UNUSED = { userContextId: 3 };

// A host under SITE_B, to which an association can be bound on its own.
const SITE_SUB_B = `test1.${SITE_B}`;
const ORIGIN_SUB_B = `https://${SITE_SUB_B}`;

const bounceTrackingProtection = Cc[
  "@mozilla.org/bounce-tracking-protection;1"
].getService(Ci.nsIBounceTrackingProtection);

function originIn(origin, originAttributes) {
  return Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(origin),
    originAttributes
  ).origin;
}

const ORIGIN_TRACKER_BOUND = originIn(ORIGIN_TRACKER, OA_BOUND);

function url(origin, file = "file_start.html") {
  return getBaseUrl(origin) + file;
}

function candidateHosts(originAttributes) {
  return bounceTrackingProtection
    .testGetBounceTrackerCandidateHosts(originAttributes)
    .map(entry => entry.siteHost)
    .sort();
}

function userActivationHosts(originAttributes) {
  return bounceTrackingProtection
    .testGetUserActivationHosts(originAttributes)
    .map(entry => entry.siteHost)
    .sort();
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "privacy.bounceTrackingProtection.mode",
        Ci.nsIBounceTrackingProtection.MODE_ENABLED,
      ],
      ["privacy.bounceTrackingProtection.bounceTrackingGracePeriodSec", 0],
      // Every extended navigation here is ended explicitly, so the client
      // bounce detection timeout must not flush a record halfway through.
      [
        "privacy.bounceTrackingProtection.clientBounceDetectionTimerPeriodMS",
        120000,
      ],
      ["privacy.userContext.enabled", true],
      ["privacy.containers.switchDuringNavigation.enabled", true],
    ],
  });
  bounceTrackingProtection.clearAll();
});

/**
 * Binds sites to containers for the duration of aFn, then restores the
 * associations and clears BTP and site data so tasks can't leak into each other.
 *
 * @param {Array<Array>} aAssociations - [site, userContextId] pairs to bind.
 * @param {Function} aFn - Test body.
 */
async function withSiteAssociations(aAssociations, aFn) {
  for (let [site, container] of aAssociations) {
    ContextualIdentityService.setSiteAssociation(site, container);
  }
  try {
    await aFn();
  } finally {
    for (let [site] of aAssociations) {
      ContextualIdentityService.removeSiteAssociation(site);
    }
    bounceTrackingProtection.clearAll();
    await SiteDataTestUtils.clear();
  }
}

/**
 * Asserts that none of the containers this file uses hold bounce tracker
 * candidates.
 *
 * @param {string} msg - Assertion message prefix.
 * @param {Array<number>} [except] - userContextIds to skip.
 */
function assertNoBTPState(msg, except = []) {
  for (let [name, container, oa] of [
    ["normal browsing", 0, OA_DEFAULT],
    [`container ${OTHER_CONTAINER}`, OTHER_CONTAINER, OA_OTHER],
    [`container ${BOUND_CONTAINER}`, BOUND_CONTAINER, OA_BOUND],
  ]) {
    if (except.includes(container)) {
      continue;
    }
    Assert.deepEqual(
      candidateHosts(oa),
      [],
      `${msg}: no bounce tracker candidates in ${name}`
    );
  }
}

/**
 * Opens a tab in the given container. aUrl must not be bound to another
 * container or the load which fills the tab would itself be retargeted.
 *
 * @param {number} userContextId - Container to open the tab in.
 * @param {string} aUrl - URL to load.
 * @returns {Promise<MozTabbrowserTab>} The opened tab.
 */
async function openTabIn(userContextId, aUrl = url(ORIGIN_A)) {
  let tab = BrowserTestUtils.addTab(gBrowser, aUrl, { userContextId });
  gBrowser.selectedTab = tab;
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, aUrl);
  return tab;
}

/**
 * Runs aFn and collects the calls of RecordStatefulBounces for aBrowserId
 * meanwhile, as reported by OBSERVER_MSG_RECORD_BOUNCES_FINISHED. Each call ends
 * the tab's extended navigation and classifies its record. A tab which holds no
 * record produces no call at all, e.g. the tab a retargeted load left behind, so
 * collect the calls rather than waiting for one.
 *
 * @param {number} aBrowserId - browserId of the tab to collect calls for.
 * @param {Function} aFn - Ends the tab's extended navigation.
 * @returns {Promise<Array<number>>} How many bounce tracker candidates each
 * call for the tab classified, empty if it held no record.
 */
async function collectRecordBouncesRuns(aBrowserId, aFn) {
  let candidateCounts = [];
  let observer = {
    observe(subject) {
      let propBag = subject.QueryInterface(Ci.nsIPropertyBag2);
      if (propBag.getProperty("browserId") == aBrowserId) {
        candidateCounts.push(
          propBag.getProperty("bounceTrackerCandidateCount")
        );
      }
    },
  };
  Services.obs.addObserver(observer, OBSERVER_MSG_RECORD_BOUNCES_FINISHED);
  try {
    await aFn();
    await TestUtils.waitForTick();
  } finally {
    Services.obs.removeObserver(observer, OBSERVER_MSG_RECORD_BOUNCES_FINISHED);
  }
  info(
    `RecordStatefulBounces ran ${candidateCounts.length} time(s) for browser ` +
      `${aBrowserId}, classifying: ${JSON.stringify(candidateCounts)}`
  );
  return candidateCounts;
}

/**
 * Ends the extended navigation of aBrowser's tab: a user-activated navigation
 * runs RecordStatefulBounces for whatever record the tab holds. The target is
 * an unbound site, so this navigation is never itself retargeted.
 *
 * @param {MozBrowser} aBrowser - Browser whose extended navigation to end.
 * @returns {Promise<Array<number>>} See collectRecordBouncesRuns.
 */
async function endExtendedNavigation(aBrowser) {
  let target = new URL(url(ORIGIN_A) + "?end");
  return collectRecordBouncesRuns(
    aBrowser.browsingContext.browserId,
    async () => {
      let loaded = BrowserTestUtils.browserLoaded(aBrowser, false, target.href);
      await navigateLinkClick(aBrowser, target);
      await loaded;
    }
  );
}

/**
 * Ends the extended navigation of aTab by closing it, which flushes the record
 * when the browsing context is discarded. Required wherever the tab's document
 * belongs to a host the task makes expectations about: endExtendedNavigation
 * clicks a link in that document, which records user activation for its site and
 * would exempt the site from classification for the next 45 days.
 *
 * @param {MozTabbrowserTab} aTab - Tab whose extended navigation to end.
 * @returns {Promise<Array<number>>} See collectRecordBouncesRuns.
 */
async function endExtendedNavigationByClosing(aTab) {
  return collectRecordBouncesRuns(
    aTab.linkedBrowser.browsingContext.browserId,
    () => BrowserTestUtils.removeTab(aTab)
  );
}

/**
 * Waits for aBrowser's load to stop in the parent process, which is where BTP
 * sets the final host. The load event BrowserTestUtils.browserLoaded waits for
 * is reported by the content process and can arrive first. If the load is still
 * running, its STATE_STOP has not been dispatched yet and can't be missed.
 *
 * @param {MozBrowser} aBrowser - Browser whose load to wait for.
 */
async function waitForParentLoadStop(aBrowser) {
  if (!aBrowser.browsingContext.webProgress.isLoadingDocument) {
    return;
  }
  await BrowserTestUtils.browserStopped(aBrowser);
  // Let every listener of the STATE_STOP run, BTP's included.
  await TestUtils.waitForTick();
}

/**
 * Starts a navigation expected to be retargeted into a new tab.
 *
 * @param {MozBrowser} aBrowser - Browser to start the navigation in.
 * @param {URL} aTarget - URL to navigate to.
 * @param {URL} aWantLoad - URL the new tab is expected to end up at.
 * @returns {Promise<MozTabbrowserTab>} The tab the load was retargeted into.
 */
async function navigateRetargeted(aBrowser, aTarget, aWantLoad = aTarget) {
  let retargetedPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    aWantLoad.href,
    true
  );
  await navigateLinkClick(aBrowser, aTarget);
  let retargeted = await retargetedPromise;
  await waitForParentLoadStop(retargeted.linkedBrowser);
  return retargeted;
}

// example.com -> itisatracker.org -> example.org (container 2, new tab)
//
// A tracker which commits its own document before bouncing on is an
// intermediate hop of the extended navigation, not its destination. Moving the
// destination into another tab must not promote the tracker to destination: the
// tracker must be classified and purged, and the destination must not be
// classified in its place.
add_task(async function test_client_bounce_classifies_only_the_tracker() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the bounce");

    let tab = await openTabIn(0);
    let targetURL = new URL(url(ORIGIN_B));
    let retargeted = await navigateRetargeted(
      tab.linkedBrowser,
      getBounceURL({
        bounceType: "client",
        targetURL,
        setState: "cookie-client",
      }),
      targetURL
    );

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The destination landed in the container it is bound to"
    );
    is(
      tab.linkedBrowser.currentURI.host,
      SITE_TRACKER,
      "The tab which ran the chain still shows the tracker's document"
    );
    ok(
      SiteDataTestUtils.hasCookies(ORIGIN_TRACKER),
      "The tracker wrote its state in the container the bounce ran in"
    );

    // The tab shows the tracker's own document, so its extended navigation has
    // to end without interacting with it.
    Assert.deepEqual(
      await endExtendedNavigationByClosing(tab),
      [],
      "The extended navigation left with the load, the tab holds no record"
    );
    // Flushed by closing, so that ending it does not record user activation for
    // the destination and exempt it from the check below.
    await endExtendedNavigationByClosing(retargeted);

    ok(
      candidateHosts(OA_DEFAULT).includes(SITE_TRACKER),
      `${SITE_TRACKER} must be classified: it is an intermediate hop of the ` +
        `extended navigation, and the retarget of the destination does not ` +
        `make the tracker the destination`
    );
    ok(
      !candidateHosts(OA_DEFAULT).includes(SITE_B),
      `The destination ${SITE_B} must not be classified in the tracker's place`
    );

    let purged = await bounceTrackingProtection.testRunPurgeBounceTrackers();
    info(
      `Purge log: ${JSON.stringify(
        bounceTrackingProtection
          .testGetRecentlyPurgedTrackers(OA_DEFAULT)
          .map(entry => ({
            siteHost: entry.siteHost,
            initialHost: entry.bounceTrackingRecord?.initialHost,
            finalHost: entry.bounceTrackingRecord?.finalHost,
          }))
      )}`
    );

    ok(purged.includes(SITE_TRACKER), `${SITE_TRACKER} must be purged`);
    ok(
      !SiteDataTestUtils.hasCookies(ORIGIN_TRACKER),
      `The state ${SITE_TRACKER} collected while bouncing must be purged`
    );
  });
});

// example.com -> itisatracker.org (server) -> example.org (container 2, new tab)
//
// The destination of an extended navigation must stay exempt from classification
// wherever the container switch puts its document, and the container the load was
// retargeted into has no chain of its own to classify. A tracker which commits
// nothing, bouncing server side, must still be classified.
add_task(async function test_server_bounce_classifies_only_the_tracker() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the bounce");

    let tab = await openTabIn(0);
    let targetURL = new URL(url(ORIGIN_B));
    let retargeted = await navigateRetargeted(
      tab.linkedBrowser,
      getBounceURL({ bounceType: "server", targetURL }),
      targetURL
    );

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The destination landed in the container it is bound to"
    );
    is(
      tab.linkedBrowser.currentURI.spec,
      url(ORIGIN_A),
      "The tab which ran the chain kept its own document"
    );

    Assert.deepEqual(
      await endExtendedNavigation(tab.linkedBrowser),
      [],
      "The extended navigation left with the load, the tab holds no record"
    );
    // Flushed by closing, so that ending it does not record user activation for
    // the destination and exempt it from the check below.
    await endExtendedNavigationByClosing(retargeted);

    ok(
      candidateHosts(OA_DEFAULT).includes(SITE_TRACKER),
      `${SITE_TRACKER} must be classified: its hop ran in this container`
    );

    ok(
      !candidateHosts(OA_DEFAULT).includes(SITE_B),
      `The destination ${SITE_B} must not be classified in the container the ` +
        `navigation started in: it is the destination of the extended ` +
        `navigation, not an intermediate hop`
    );

    Assert.deepEqual(
      candidateHosts(OA_BOUND),
      [],
      "The container the load was retargeted into must classify nothing"
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// example.com -> itisatracker.org (container 2, new tab) -> example.org
//
// A tracker bound to a container runs its hops, and writes its state, in that
// container. It must be classified against the container its state lives in, so
// that a purge can reach it. Starting the record of the tab it was retargeted
// into must not exempt it.
add_task(async function test_bound_tracker_is_classified_in_its_container() {
  await withSiteAssociations([[SITE_TRACKER, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the bounce");

    let tab = await openTabIn(0);
    let targetURL = new URL(url(ORIGIN_B));
    let retargeted = await navigateRetargeted(
      tab.linkedBrowser,
      getBounceURL({
        bounceType: "client",
        targetURL,
        setState: "cookie-client",
      }),
      targetURL
    );

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The tracker's own load landed in the container it is bound to"
    );
    ok(
      SiteDataTestUtils.hasCookies(ORIGIN_TRACKER_BOUND),
      "The tracker holds state in the container it is bound to"
    );
    ok(
      !SiteDataTestUtils.hasCookies(ORIGIN_TRACKER),
      "The tracker holds no state in the container the navigation started in"
    );

    await endExtendedNavigation(retargeted.linkedBrowser);
    await endExtendedNavigation(tab.linkedBrowser);

    ok(
      candidateHosts(OA_BOUND).includes(SITE_TRACKER),
      `${SITE_TRACKER} must be classified in the container its hops ran in ` +
        `and its state lives in`
    );
    ok(
      !candidateHosts(OA_DEFAULT).includes(SITE_TRACKER),
      `${SITE_TRACKER} must not be classified against a container it never ` +
        `ran in, where a purge can reach nothing of its state`
    );

    BrowserTestUtils.removeTab(retargeted);
    BrowserTestUtils.removeTab(tab);
  });
});

// example.com (container 1) -> example.org (container 2, new tab)
//
// A site the user deliberately navigates to is the destination of that
// navigation, not an intermediate hop. Moving its document into another tab must
// not turn it into a bounce tracker in the container the user navigated from.
add_task(async function test_navigating_to_a_bound_site_classifies_nothing() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the navigation");

    let tab = await openTabIn(OTHER_CONTAINER);
    let targetURL = new URL(url(ORIGIN_B));
    let retargeted = await navigateRetargeted(tab.linkedBrowser, targetURL);

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The navigation landed in the container the site is bound to"
    );

    // Flushed by closing, so that ending it does not record user activation for
    // the destination and exempt it.
    await endExtendedNavigationByClosing(retargeted);

    assertNoBTPState(
      "A site the user navigated to deliberately must not be classified as a " +
        "bounce tracker in any container"
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// example.com (container 1) -> example.org (container 2, new tab + interaction)
// -> example.net
//
// Interaction with a site must exempt it from classification wherever the
// container switch put its document. The activation is recorded against the
// container the document runs in, which is not the one the navigation started
// in, so the two must not be able to disagree about whether the site is exempt.
add_task(async function test_user_activation_exempts_the_retargeted_site() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the navigation");

    let tab = await openTabIn(OTHER_CONTAINER);
    let targetURL = new URL(url(ORIGIN_B));
    let retargeted = await navigateRetargeted(tab.linkedBrowser, targetURL);

    info("Interact with the retargeted tab.");
    await SpecialPowers.spawn(retargeted.linkedBrowser, [], () => {
      SpecialPowers.wrap(content.document).notifyUserGestureActivation();
      content.document.userInteractionForTesting();
    });

    ok(
      userActivationHosts(OA_BOUND).includes(SITE_B),
      `User activation for ${SITE_B} is recorded in the container it runs in`
    );
    ok(
      !userActivationHosts(OA_OTHER).includes(SITE_B),
      `No user activation for ${SITE_B} in the container it was navigated from`
    );

    // Leave without transient activation, so the site becomes a bounce of the
    // extended navigation instead of ending it, and only its recorded user
    // activation can exempt it.
    let finalURL = new URL(url(ORIGIN_C));
    let loaded = BrowserTestUtils.browserLoaded(
      retargeted.linkedBrowser,
      false,
      finalURL.href
    );
    await SpecialPowers.spawn(
      retargeted.linkedBrowser,
      [finalURL.href],
      finalURL => {
        SpecialPowers.wrap(content.document).clearUserGestureActivation();
        let script = content.document.createElement("script");
        script.textContent = `location.href = ${JSON.stringify(finalURL)};`;
        content.document.body.appendChild(script);
      }
    );
    await loaded;
    await waitForParentLoadStop(retargeted.linkedBrowser);

    Assert.deepEqual(
      await endExtendedNavigationByClosing(retargeted),
      [0],
      "The retargeted tab holds the extended navigation and classifies nothing"
    );

    assertNoBTPState(
      "Interacting with the site must exempt it from classification, wherever " +
        "the container switch put the document"
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// about:blank (container 1, closed) -> example.org (container 2, new tab)
//
// Opening a tab and going straight to a bound site is the flow the feature is
// built for, and closing the tab the load left behind (Bug 2052136 part 6) ends
// that tab's extended navigation immediately. Ending it must not classify the
// site the user asked for.
add_task(async function test_closing_the_disposable_tab_classifies_nothing() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the navigation");

    let tab = await openTabIn(OTHER_CONTAINER, "about:blank");
    let targetURL = new URL(url(ORIGIN_B));

    // The record would be flushed when the browsing context is discarded, which
    // happens while the tab is being removed. Whether the closed tab classified
    // anything at all is part of what this task measures.
    let retargeted;
    let candidateCounts = await collectRecordBouncesRuns(
      tab.linkedBrowser.browsingContext.browserId,
      async () => {
        let retargetedPromise = BrowserTestUtils.waitForNewTab(
          gBrowser,
          targetURL.href,
          true
        );
        await navigateSystemPrincipalLoad(tab.linkedBrowser, targetURL);
        retargeted = await retargetedPromise;

        await TestUtils.waitForCondition(
          () => !tab.isConnected,
          "The tab which only existed for the load was closed"
        );
      }
    );

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The load landed in the container the site is bound to"
    );
    ok(
      candidateCounts.every(count => count == 0),
      "The extended navigation left with the load, closing the tab classified " +
        "nothing"
    );
    ok(
      !candidateHosts(OA_OTHER).includes(SITE_B),
      `Opening a tab and going to a bound site must not classify ${SITE_B} ` +
        `in the container of the tab that was closed`
    );

    BrowserTestUtils.removeTab(retargeted);
  });
});

// example.com -> itisatracker.org -> example.org (container 2, new tab)
// -> example.net
//
// The container is re-decided on every hop and sticks once it changes: a chain
// which redirects through a bound site is retargeted into that container, and
// the hops after it stay there even though they are bound to nothing. The chain
// is therefore split across two containers, and each hop must be classified
// against the one it actually ran in.
add_task(async function test_each_hop_is_classified_in_its_own_container() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the bounce");

    let tab = await openTabIn(0);
    let finalURL = new URL(url(ORIGIN_C));

    // example.com -> itisatracker.org -> example.org (bound) -> example.net
    let boundHopURL = new URL(url(ORIGIN_B, "file_bounce.sjs"));
    boundHopURL.searchParams.set("statusCode", 302);
    boundHopURL.searchParams.set("target", finalURL.href);
    boundHopURL.searchParams.set("setState", "cookie-server");

    let retargeted = await navigateRetargeted(
      tab.linkedBrowser,
      getBounceURL({ bounceType: "server", targetURL: boundHopURL }),
      finalURL
    );

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The chain was retargeted into the container the bound hop is bound to"
    );
    is(
      tab.linkedBrowser.currentURI.spec,
      url(ORIGIN_A),
      "The tab which started the chain kept its own document"
    );
    is(tab.userContextId, 0, "and its container");
    ok(
      SiteDataTestUtils.hasCookies(originIn(ORIGIN_B, OA_BOUND)),
      "The bound hop ran in the container it is bound to"
    );

    await endExtendedNavigation(tab.linkedBrowser);
    // Flushed by closing: this task makes expectations about the destination,
    // and ending its navigation by clicking a link would record user activation
    // for it and exempt it from classification.
    await endExtendedNavigationByClosing(retargeted);

    ok(
      candidateHosts(OA_DEFAULT).includes(SITE_TRACKER),
      `${SITE_TRACKER} is classified in the container its hop ran in`
    );
    ok(
      !candidateHosts(OA_DEFAULT).includes(SITE_B),
      `The bound hop must not be classified in a container it never ran in`
    );
    ok(
      candidateHosts(OA_BOUND).includes(SITE_B),
      `The bound hop must be classified in the container its hop ran in`
    );
    ok(
      !candidateHosts(OA_DEFAULT).includes(SITE_C),
      `${SITE_C} is the destination of the extended navigation, and it never ran ` +
        `in the container the chain started in, so it must not be classified there`
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// example.com -> example.org (container 2, new tab) -> itisatracker.org
// -> example.net
//
// A tracker bound to no container still runs in the container a preceding bound
// hop switched the chain into, and collects its state there. It has to be
// classified against that container: otherwise a purge looks in the container
// the chain started in, where the tracker holds nothing, and the state it
// collected survives. Every other expectation here is a site being classified
// or purged when it should not be; this one is a tracker escaping both.
add_task(async function test_unbound_tracker_is_classified_where_it_ran() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the bounce");

    let tab = await openTabIn(0);
    let finalURL = new URL(url(ORIGIN_C));

    // The bound hop only redirects, so the tracker after it inherits the
    // container the chain was switched into rather than the tab's.
    let boundHopURL = new URL(url(ORIGIN_B, "file_bounce.sjs"));
    boundHopURL.searchParams.set("statusCode", 302);
    boundHopURL.searchParams.set(
      "target",
      getBounceURL({
        bounceType: "server",
        targetURL: finalURL,
        setState: "cookie-server",
      }).href
    );

    let retargeted = await navigateRetargeted(
      tab.linkedBrowser,
      boundHopURL,
      finalURL
    );

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The chain was retargeted into the container the bound hop is bound to"
    );
    ok(
      SiteDataTestUtils.hasCookies(ORIGIN_TRACKER_BOUND),
      `${SITE_TRACKER}, bound to no container, collected its state in the ` +
        `container the chain was switched into`
    );
    ok(
      !SiteDataTestUtils.hasCookies(ORIGIN_TRACKER),
      "and none in the container the chain started in"
    );

    await endExtendedNavigation(tab.linkedBrowser);
    await endExtendedNavigationByClosing(retargeted);

    ok(
      candidateHosts(OA_BOUND).includes(SITE_TRACKER),
      `${SITE_TRACKER} must be classified in the container its hop ran in and ` +
        `its state lives in`
    );
    ok(
      !candidateHosts(OA_DEFAULT).includes(SITE_TRACKER),
      `${SITE_TRACKER} must not be classified against the container the chain ` +
        `started in, where it holds no state a purge could clear`
    );

    await bounceTrackingProtection.testRunPurgeBounceTrackers();

    ok(
      !SiteDataTestUtils.hasCookies(ORIGIN_TRACKER_BOUND),
      `The purge must reach the state ${SITE_TRACKER} collected in the ` +
        `container it ran in`
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// itisatracker.org -> example.org (container 2, new tab) -> itisatracker.org
// -> example.net
//
// The initial site of an extended navigation is exempt from classification
// because the user was on it, but only in the container the user was on it in.
// A tracker the user left must not be able to bounce through a bound site's
// redirect back to itself and collect state in the bound container unclassified,
// linking the user's identities across containers.
add_task(async function test_initial_site_is_exempt_only_in_its_container() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the bounce");

    let tab = await openTabIn(0, url(ORIGIN_TRACKER));
    let finalURL = new URL(url(ORIGIN_C));

    let boundHopURL = new URL(url(ORIGIN_B, "file_bounce.sjs"));
    boundHopURL.searchParams.set("statusCode", 302);
    boundHopURL.searchParams.set(
      "target",
      getBounceURL({
        bounceType: "server",
        targetURL: finalURL,
        setState: "cookie-server",
      }).href
    );

    let retargeted = await navigateRetargeted(
      tab.linkedBrowser,
      boundHopURL,
      finalURL
    );

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The chain was retargeted into the container the bound hop is bound to"
    );
    ok(
      SiteDataTestUtils.hasCookies(ORIGIN_TRACKER_BOUND),
      `${SITE_TRACKER} collected state in the container the chain was ` +
        `switched into`
    );

    await endExtendedNavigationByClosing(retargeted);

    ok(
      candidateHosts(OA_BOUND).includes(SITE_TRACKER),
      `${SITE_TRACKER} must be classified in the container its hop ran in, ` +
        `although it is the initial site in the container the user was in`
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// example.com -> example.org (container 2, new tab) -> itisatracker.org (client)
// -> example.net
//
// A tracker which commits its own document after the switch bounces on from the
// tab the load was retargeted into. The record that tab took over must keep
// collecting bounces, and classify them in the container they ran in.
add_task(async function test_client_bounce_after_the_switch_is_classified() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the bounce");

    let tab = await openTabIn(0);
    let finalURL = new URL(url(ORIGIN_C));

    let boundHopURL = new URL(url(ORIGIN_B, "file_bounce.sjs"));
    boundHopURL.searchParams.set("statusCode", 302);
    boundHopURL.searchParams.set(
      "target",
      getBounceURL({
        bounceType: "client",
        targetURL: finalURL,
        setState: "cookie-client",
      }).href
    );

    let retargeted = await navigateRetargeted(
      tab.linkedBrowser,
      boundHopURL,
      finalURL
    );

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The chain was retargeted into the container the bound hop is bound to"
    );
    ok(
      SiteDataTestUtils.hasCookies(ORIGIN_TRACKER_BOUND),
      `${SITE_TRACKER} wrote its state from its document in the tab the load ` +
        `was retargeted into`
    );
    ok(
      !SiteDataTestUtils.hasCookies(ORIGIN_TRACKER),
      "and none in the container the chain started in"
    );

    Assert.deepEqual(
      await endExtendedNavigation(tab.linkedBrowser),
      [],
      "The extended navigation left with the load, the tab holds no record"
    );
    await endExtendedNavigationByClosing(retargeted);

    Assert.deepEqual(
      candidateHosts(OA_BOUND),
      [SITE_B, SITE_TRACKER],
      `The bound hop and the client bounce of ${SITE_TRACKER} after it are ` +
        `classified in the container they ran in`
    );
    Assert.deepEqual(
      candidateHosts(OA_DEFAULT),
      [],
      "Nothing is classified in the container the chain started in"
    );

    await bounceTrackingProtection.testRunPurgeBounceTrackers();

    ok(
      !SiteDataTestUtils.hasCookies(ORIGIN_TRACKER_BOUND),
      `The purge must reach the state ${SITE_TRACKER} collected in the ` +
        `container it ran in`
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// example.com -> itisatracker.org (server) -> example.org (container 2, new tab,
// client) -> example.net (container 1, new tab)
//
// A tab the load was retargeted into can have its own load retargeted again. The
// record has to follow it a second time, so that every hop is classified in the
// container it ran in and the destination stays exempt.
add_task(async function test_record_follows_two_switches() {
  await withSiteAssociations(
    [
      [SITE_B, BOUND_CONTAINER],
      [SITE_C, OTHER_CONTAINER],
    ],
    async () => {
      assertNoBTPState("Before the bounce");

      let tab = await openTabIn(0);
      let finalURL = new URL(url(ORIGIN_C));
      let boundHopURL = getBounceURL({
        bounceType: "client",
        bounceOrigin: ORIGIN_B,
        targetURL: finalURL,
        setState: "cookie-client",
      });

      // The bound hop's client bounce can start before its document's load
      // event, which then never fires, so only wait for its tab to open.
      let firstRetargetedPromise = BrowserTestUtils.waitForNewTab(gBrowser);
      let secondRetargetedPromise = BrowserTestUtils.waitForNewTab(
        gBrowser,
        finalURL.href,
        true,
        true
      );
      await navigateLinkClick(
        tab.linkedBrowser,
        getBounceURL({
          bounceType: "server",
          targetURL: boundHopURL,
          setState: "cookie-server",
        })
      );
      let firstRetargeted = await firstRetargetedPromise;
      let secondRetargeted = await secondRetargetedPromise;
      await waitForParentLoadStop(secondRetargeted.linkedBrowser);

      is(
        firstRetargeted.userContextId,
        BOUND_CONTAINER,
        "The first switch landed in the container the bound hop is bound to"
      );
      is(
        firstRetargeted.linkedBrowser.currentURI.host,
        SITE_B,
        "and that tab still shows the bound hop's document"
      );
      is(
        secondRetargeted.userContextId,
        OTHER_CONTAINER,
        "The second switch landed in the container the destination is bound to"
      );
      ok(
        SiteDataTestUtils.hasCookies(ORIGIN_TRACKER),
        `${SITE_TRACKER} wrote its state in the container the chain started in`
      );
      ok(
        SiteDataTestUtils.hasCookies(originIn(ORIGIN_B, OA_BOUND)),
        "The bound hop wrote its state in the container it is bound to"
      );

      Assert.deepEqual(
        await endExtendedNavigation(tab.linkedBrowser),
        [],
        "The tab which started the chain holds no record"
      );
      // Closed, since interacting with it would record user activation for the
      // bound hop and exempt it.
      Assert.deepEqual(
        await endExtendedNavigationByClosing(firstRetargeted),
        [],
        "The tab of the first switch passed the record on and holds none"
      );
      await endExtendedNavigationByClosing(secondRetargeted);

      Assert.deepEqual(
        candidateHosts(OA_DEFAULT),
        [SITE_TRACKER],
        `${SITE_TRACKER} is classified in the container its hop ran in`
      );
      Assert.deepEqual(
        candidateHosts(OA_BOUND),
        [SITE_B],
        "The bound hop is classified in the container it ran in"
      );
      Assert.deepEqual(
        candidateHosts(OA_OTHER),
        [],
        `The destination ${SITE_C} must not be classified`
      );

      await bounceTrackingProtection.testRunPurgeBounceTrackers();

      ok(
        !SiteDataTestUtils.hasCookies(ORIGIN_TRACKER),
        `The purge must reach the state ${SITE_TRACKER} collected`
      );
      ok(
        !SiteDataTestUtils.hasCookies(originIn(ORIGIN_B, OA_BOUND)),
        "The purge must reach the state the bound hop collected"
      );

      BrowserTestUtils.removeTab(tab);
    }
  );
});

/**
 * Navigates from example.com in container 1 to example.org, which is bound to
 * container 2, without ending the extended navigation.
 *
 * @param {boolean} aBounce - Whether to bounce through itisatracker.org with a
 * server redirect on the way.
 * @returns {Promise<Array<MozTabbrowserTab>>} The tab the navigation started in
 * and the tab it was retargeted into.
 */
async function navigateIntoBoundContainer(aBounce) {
  let tab = await openTabIn(OTHER_CONTAINER);
  let targetURL = new URL(url(ORIGIN_B));
  let retargeted = await navigateRetargeted(
    tab.linkedBrowser,
    aBounce ? getBounceURL({ bounceType: "server", targetURL }) : targetURL,
    targetURL
  );
  is(
    retargeted.userContextId,
    BOUND_CONTAINER,
    "The destination landed in the container it is bound to"
  );
  return [tab, retargeted];
}

// example.com (container 1) -> itisatracker.org (server) -> example.org
// (container 2, new tab), then container 3 is cleared
//
// Control for the task below: the tab the load was retargeted into holds the
// bounce which ran in container 1, and clearing a container its record does not
// touch leaves the record alone.
add_task(async function test_clearing_another_container_keeps_moved_bounces() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the bounce");

    let [tab, retargeted] = await navigateIntoBoundContainer(true);

    bounceTrackingProtection.clearByOriginAttributesPattern(
      JSON.stringify(OA_UNUSED)
    );

    Assert.deepEqual(
      await endExtendedNavigationByClosing(retargeted),
      [1],
      "The tab the load was retargeted into kept its record and classified " +
        "the tracker"
    );
    Assert.deepEqual(
      candidateHosts(OA_OTHER),
      [SITE_TRACKER],
      `${SITE_TRACKER} is classified in the container its hop ran in`
    );
    assertNoBTPState("Nothing else is classified", [OTHER_CONTAINER]);

    BrowserTestUtils.removeTab(tab);
  });
});

// example.com (container 1) -> itisatracker.org (server) -> example.org
// (container 2, new tab)
//
// The tab the load was retargeted into holds bounces which ran in the container
// the navigation started in. Clearing that container's data has to drop that
// tab's record too, or ending the extended navigation would classify the cleared
// sites in it again.
add_task(async function test_clearing_a_container_clears_moved_bounces() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the bounce");

    let [tab, retargeted] = await navigateIntoBoundContainer(true);

    bounceTrackingProtection.clearByOriginAttributesPattern(
      JSON.stringify(OA_OTHER)
    );

    Assert.deepEqual(
      await endExtendedNavigationByClosing(retargeted),
      [],
      "The tab holding bounces of the cleared container dropped its record"
    );

    assertNoBTPState(
      `${SITE_TRACKER} bounced in container ${OTHER_CONTAINER} before its data ` +
        `was cleared, so it must not be classified anywhere`
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// example.com (container 1) -> example.org (container 2, new tab)
//
// A record can touch a container through its initial site alone. Clearing that
// container has to drop the record as well.
add_task(
  async function test_clearing_the_initial_sites_container_drops_record() {
    await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
      let [tab, retargeted] = await navigateIntoBoundContainer(false);
      Assert.deepEqual(
        await endExtendedNavigationByClosing(retargeted),
        [0],
        "Without a clear, the tab the load was retargeted into holds a record " +
          "which classifies nothing"
      );
      BrowserTestUtils.removeTab(tab);

      [tab, retargeted] = await navigateIntoBoundContainer(false);

      bounceTrackingProtection.clearByOriginAttributesPattern(
        JSON.stringify(OA_OTHER)
      );

      Assert.deepEqual(
        await endExtendedNavigationByClosing(retargeted),
        [],
        `Clearing container ${OTHER_CONTAINER}, where the initial site ran, ` +
          `dropped the record`
      );

      BrowserTestUtils.removeTab(tab);
    });
  }
);

/**
 * Runs example.com (container 1) -> itisatracker.org -> example.org (container
 * 2, new tab) -> example.net and purges, so that one bounce chain record is
 * held by purge log entries in both containers.
 */
async function purgeChainAcrossContainers() {
  let tab = await openTabIn(OTHER_CONTAINER);
  let finalURL = new URL(url(ORIGIN_C));

  let boundHopURL = new URL(url(ORIGIN_B, "file_bounce.sjs"));
  boundHopURL.searchParams.set("statusCode", 302);
  boundHopURL.searchParams.set("target", finalURL.href);

  let retargeted = await navigateRetargeted(
    tab.linkedBrowser,
    getBounceURL({ bounceType: "server", targetURL: boundHopURL }),
    finalURL
  );
  await endExtendedNavigation(tab.linkedBrowser);
  await endExtendedNavigationByClosing(retargeted);
  BrowserTestUtils.removeTab(tab);

  Assert.deepEqual(
    (await bounceTrackingProtection.testRunPurgeBounceTrackers()).sort(),
    [SITE_B, SITE_TRACKER],
    "The tracker and the bound hop are purged, each in its own container"
  );
  Assert.equal(
    bounceTrackingProtection.getRecentPurgedChainEntriesForSite(SITE_A).length,
    2,
    `Both purge log entries name ${SITE_A} as the initial site of their chain`
  );
}

// example.com (container 1) -> itisatracker.org -> example.org (container 2,
// new tab) -> example.net, then container 1 is cleared
//
// The chain record is kept with the purge log entries of both containers, and
// Report Broken Site sends it. Clearing a container must not leave its sites in
// the records another container holds.
add_task(async function test_clearing_a_container_drops_its_chain_records() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    await purgeChainAcrossContainers();

    bounceTrackingProtection.clearByOriginAttributesPattern(
      JSON.stringify(OA_OTHER)
    );

    Assert.deepEqual(
      bounceTrackingProtection.getRecentPurgedChainEntriesForSite(SITE_A),
      [],
      `No purge log entry names ${SITE_A} after its container was cleared`
    );
    Assert.deepEqual(
      bounceTrackingProtection.getRecentPurgedChainEntriesForSite(SITE_TRACKER),
      [],
      `No purge log entry names ${SITE_TRACKER} after its container was cleared`
    );
    let [entry, ...rest] =
      bounceTrackingProtection.testGetRecentlyPurgedTrackers(OA_BOUND);
    is(rest.length, 0, "The other container keeps its purge log entry");
    is(entry.siteHost, SITE_B, `for ${SITE_B}`);
    is(entry.bounceTrackingRecord, null, "but no longer its chain record");
  });
});

// example.com (container 1) -> itisatracker.org -> example.org (container 2,
// new tab) -> example.net, then example.com is cleared in container 1
//
// Clearing a site must drop the chain records which name it, also those another
// container holds, but only where the site ran in a cleared container.
add_task(async function test_clearing_a_site_drops_its_chain_records() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    await purgeChainAcrossContainers();

    bounceTrackingProtection.clearBySiteHostAndOriginAttributes(
      SITE_A,
      OA_BOUND
    );
    Assert.equal(
      bounceTrackingProtection.getRecentPurgedChainEntriesForSite(SITE_A)
        .length,
      2,
      `Clearing ${SITE_A} in a container it did not run in keeps the records`
    );

    bounceTrackingProtection.clearBySiteHostAndOriginAttributes(
      SITE_A,
      OA_OTHER
    );
    Assert.deepEqual(
      bounceTrackingProtection.getRecentPurgedChainEntriesForSite(SITE_A),
      [],
      `No purge log entry names ${SITE_A} after it was cleared where it ran`
    );
    for (let oa of [OA_OTHER, OA_BOUND]) {
      let entries = bounceTrackingProtection.testGetRecentlyPurgedTrackers(oa);
      is(entries.length, 1, "Each container keeps its purge log entry");
      is(entries[0].bounceTrackingRecord, null, "but not its chain record");
    }
  });
});

// example.com -> itisatracker.org -> example.org, all in container 1
//
// A site bound to the container its tab is already in needs no switch, so the
// chain must behave exactly as it does without the feature: only the bounce
// tracker is classified, in the tab's container.
add_task(async function test_same_container_association_changes_nothing() {
  await withSiteAssociations([[SITE_B, OTHER_CONTAINER]], async () => {
    assertNoBTPState("Before the bounce");

    let tab = await openTabIn(OTHER_CONTAINER);
    let targetURL = new URL(url(ORIGIN_B));

    let tabCount = gBrowser.tabs.length;
    let loaded = BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false,
      targetURL.href
    );
    await navigateLinkClick(
      tab.linkedBrowser,
      getBounceURL({ bounceType: "server", targetURL })
    );
    await loaded;

    is(gBrowser.tabs.length, tabCount, "The load was not retargeted");

    await endExtendedNavigation(tab.linkedBrowser);

    Assert.deepEqual(
      candidateHosts(OA_OTHER),
      [SITE_TRACKER],
      "Only the bounce tracker is classified, in the tab's container"
    );
    assertNoBTPState("A chain within one container", [OTHER_CONTAINER]);

    BrowserTestUtils.removeTab(tab);
  });
});

// example.com -> itisatracker.org (container 2, new tab) -> example.org
//
// A purge must clear the state a tracker actually collected. A tracker bound to
// a container retargets the chain into it as soon as the load starts, and the
// unbound destination stays there, so the tracker's state lives in that
// container. That is where the tracker must be classified and where the purge
// has to reach.
add_task(async function test_purge_reaches_the_state_in_the_bound_container() {
  await withSiteAssociations([[SITE_TRACKER, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the bounce");

    let tab = await openTabIn(0);
    let targetURL = new URL(url(ORIGIN_B));

    let retargeted = await navigateRetargeted(
      tab.linkedBrowser,
      getBounceURL({
        bounceType: "server",
        targetURL,
        setState: "cookie-server",
      }),
      targetURL
    );

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The chain was retargeted into the container the tracker is bound to"
    );
    ok(
      SiteDataTestUtils.hasCookies(ORIGIN_TRACKER_BOUND),
      "The tracker's hop set its cookie in the container it is bound to"
    );
    ok(
      !SiteDataTestUtils.hasCookies(ORIGIN_TRACKER),
      "The tracker set no cookie in the container the chain started in"
    );

    await endExtendedNavigation(tab.linkedBrowser);
    await endExtendedNavigationByClosing(retargeted);

    ok(
      candidateHosts(OA_BOUND).includes(SITE_TRACKER),
      `${SITE_TRACKER} must be classified in the container its hop ran in`
    );
    ok(
      !candidateHosts(OA_DEFAULT).includes(SITE_TRACKER),
      `${SITE_TRACKER} must not be classified against the container the chain ` +
        `started in, where it holds no state a purge could clear`
    );

    let purged = await bounceTrackingProtection.testRunPurgeBounceTrackers();
    ok(
      purged.includes(SITE_TRACKER),
      `${SITE_TRACKER} must be purged, whichever container it is classified in`
    );
    ok(
      !purged.includes(SITE_B),
      `The destination ${SITE_B} must not be purged: it is where the extended ` +
        `navigation ended, not an intermediate hop`
    );

    ok(
      !SiteDataTestUtils.hasCookies(ORIGIN_TRACKER_BOUND),
      `The purge must reach the state ${SITE_TRACKER} collected in the ` +
        `container its hop ran in`
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// example.com -> example.org (container 2, new tab)
//
// A purge must only clear state for a site which actually bounced. Subframes are
// never retargeted, so a bound site keeps writing partitioned third-party state in
// the container the user browses in, and the purge pattern wildcards partitionKey:
// that state, and any first party state from before the association, must survive
// an ordinary navigation to the site.
add_task(async function test_purge_spares_the_container_navigated_from() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the navigation");

    // First-party state from before the site was bound to a container.
    SiteDataTestUtils.addToCookies({
      host: SITE_B,
      originAttributes: OA_DEFAULT,
      name: "btp",
      value: "before-the-association",
    });
    // State the site holds as a third party under an unbound top level, which
    // no association can move because subframes are never retargeted.
    SiteDataTestUtils.addToCookies({
      host: SITE_B,
      originAttributes: { ...OA_DEFAULT, partitionKey: `(https,${SITE_A})` },
      name: "btp",
      value: "partitioned",
    });
    ok(
      SiteDataTestUtils.hasCookies(ORIGIN_B, [
        { key: "btp", value: "before-the-association" },
      ]),
      "The site holds first party state in normal browsing"
    );
    ok(
      SiteDataTestUtils.hasCookies(
        ORIGIN_B,
        [{ key: "btp", value: "partitioned" }],
        { ...OA_DEFAULT, partitionKey: `(https,${SITE_A})` }
      ),
      "The site holds partitioned third party state in normal browsing"
    );

    let tab = await openTabIn(0);
    let targetURL = new URL(url(ORIGIN_B));
    let retargeted = await navigateRetargeted(tab.linkedBrowser, targetURL);

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The navigation landed in the container the site is bound to"
    );

    // Flushed by closing, so that ending it does not record user activation for
    // the destination and exempt it.
    await endExtendedNavigationByClosing(retargeted);

    let purged = await bounceTrackingProtection.testRunPurgeBounceTrackers();
    ok(
      !purged.includes(SITE_B),
      `Navigating to ${SITE_B} must not get it purged`
    );
    ok(
      SiteDataTestUtils.hasCookies(ORIGIN_B, [
        { key: "btp", value: "before-the-association" },
      ]),
      `The first party state ${SITE_B} holds in the container the user ` +
        `navigated from must survive`
    );
    ok(
      SiteDataTestUtils.hasCookies(
        ORIGIN_B,
        [{ key: "btp", value: "partitioned" }],
        { ...OA_DEFAULT, partitionKey: `(https,${SITE_A})` }
      ),
      `The partitioned third party state ${SITE_B} holds in that container, ` +
        `which no association moves, must survive`
    );
    ok(
      !bounceTrackingProtection.hasRecentlyPurgedSite(SITE_B),
      `${SITE_B} must not be reported as recently purged, which is the flag ` +
        `Report Broken Site sends for the site`
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// example.com -> example.org (container 2, new tab, open across the purge)
//
// A site the user has open at the top level must be left alone by the purge, which
// recognises it through the BounceTrackingStates of open tabs. A tab the load was
// retargeted into is such a tab and has to be recognised as one.
add_task(async function test_purge_spares_a_site_open_in_a_retargeted_tab() {
  await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the navigation");

    SiteDataTestUtils.addToCookies({
      host: SITE_B,
      originAttributes: OA_BOUND,
      name: "btp",
      value: "open-tab",
    });
    ok(
      SiteDataTestUtils.hasCookies(originIn(ORIGIN_B, OA_BOUND), [
        { key: "btp", value: "open-tab" },
      ]),
      "The site holds state in the container it is bound to"
    );
    // A candidate an earlier bounce inside the bound container would have left.
    bounceTrackingProtection.testAddBounceTrackerCandidate(
      OA_BOUND,
      SITE_B,
      Date.now() * 1000
    );

    let tab = await openTabIn(0);
    let retargeted = await navigateRetargeted(
      tab.linkedBrowser,
      new URL(url(ORIGIN_B))
    );

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The site is the document of a tab in the container it is bound to"
    );
    ok(retargeted.selected, "and that tab is the one the user is looking at");

    let purged = await bounceTrackingProtection.testRunPurgeBounceTrackers();

    ok(
      !purged.includes(SITE_B),
      `${SITE_B} must not be purged while it is open at the top level`
    );
    ok(
      SiteDataTestUtils.hasCookies(originIn(ORIGIN_B, OA_BOUND), [
        { key: "btp", value: "open-tab" },
      ]),
      `The state of a site the user has open must survive the purge`
    );

    BrowserTestUtils.removeTab(retargeted);
    BrowserTestUtils.removeTab(tab);
  });
});

// example.com -> test1.example.org (container 2, new tab)
//
// Associations are per host while BTP classifies and purges whole schemeless
// sites. Binding one host must not put its eTLD+1 on the candidate list, so the
// registrable domain, which is bound to no container, must keep its data.
add_task(async function test_host_association_spares_the_rest_of_the_site() {
  await withSiteAssociations([[SITE_SUB_B, BOUND_CONTAINER]], async () => {
    assertNoBTPState("Before the navigation");

    SiteDataTestUtils.addToCookies({
      host: SITE_B,
      originAttributes: OA_DEFAULT,
      name: "btp",
      value: "sibling-host",
    });
    ok(
      SiteDataTestUtils.hasCookies(ORIGIN_B, [
        { key: "btp", value: "sibling-host" },
      ]),
      `${SITE_B}, bound to no container, holds state in normal browsing`
    );

    let tab = await openTabIn(0);
    let targetURL = new URL(url(ORIGIN_SUB_B));
    let retargeted = await navigateRetargeted(tab.linkedBrowser, targetURL);

    is(
      retargeted.userContextId,
      BOUND_CONTAINER,
      "The bound host landed in the container it is bound to"
    );

    // Flushed by closing, so that ending it does not record user activation for
    // the destination and exempt it.
    await endExtendedNavigationByClosing(retargeted);

    assertNoBTPState(`Binding ${SITE_SUB_B} must not classify ${SITE_B}`);

    await bounceTrackingProtection.testRunPurgeBounceTrackers();

    ok(
      SiteDataTestUtils.hasCookies(ORIGIN_B, [
        { key: "btp", value: "sibling-host" },
      ]),
      `The state of ${SITE_B}, which is bound to no container, must survive`
    );

    BrowserTestUtils.removeTab(tab);
  });
});

// example.com -> itisatracker.org -> example.org, all in container 0 (pref off)
//
// With the feature off an association must have no effect at all: the destination
// commits in the tab it was loaded from, so only the bounce tracker is classified
// and the bound container stays empty.
add_task(async function test_feature_disabled_changes_nothing() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.containers.switchDuringNavigation.enabled", false]],
  });
  try {
    await withSiteAssociations([[SITE_B, BOUND_CONTAINER]], async () => {
      assertNoBTPState("Before the bounce");

      let tab = await openTabIn(0);
      let targetURL = new URL(url(ORIGIN_B));

      let tabCount = gBrowser.tabs.length;
      let loaded = BrowserTestUtils.browserLoaded(
        tab.linkedBrowser,
        false,
        targetURL.href
      );
      await navigateLinkClick(
        tab.linkedBrowser,
        getBounceURL({ bounceType: "server", targetURL })
      );
      await loaded;

      is(gBrowser.tabs.length, tabCount, "The load was not retargeted");

      await endExtendedNavigation(tab.linkedBrowser);

      Assert.deepEqual(
        candidateHosts(OA_DEFAULT),
        [SITE_TRACKER],
        "Only the bounce tracker is classified"
      );
      assertNoBTPState("With the feature off", [0]);

      BrowserTestUtils.removeTab(tab);
    });
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});
