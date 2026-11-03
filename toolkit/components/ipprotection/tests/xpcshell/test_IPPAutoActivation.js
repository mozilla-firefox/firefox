/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { IPPAutoActivation, IPPAutoActivationSingleton } =
  ChromeUtils.importESModule(
    "moz-src:///toolkit/components/ipprotection/IPPAutoActivation.sys.mjs"
  );
const { IPProtectionServerlist } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPProtectionServerlist.sys.mjs"
);

const FEATURE_PREF = "browser.ipProtection.features.autoStart";
const AUTOSTART_PREF = "browser.ipProtection.autoStartEnabled";
const AUTO_RESTORE_PREF = "browser.ipProtection.autoRestoreEnabled";
const USER_ENABLED_PREF = "browser.ipProtection.userEnabled";
const MATCH_PATTERNS_PREF = "browser.ipProtection.inclusion.match_patterns";
const EGRESS_LOCATION_PREF = "browser.ipProtection.egressLocation";
const REASON_PREFS = [FEATURE_PREF, AUTOSTART_PREF, USER_ENABLED_PREF];
const PERM_NAME = "ipp-vpn";
const SITE = "https://always-on.example.com";

/**
 * Turns auto-start on. The pref that carries an observer goes first, so an
 * instance still listening from an earlier task sees the feature off.
 */
function enableAutoStart() {
  Services.prefs.setBoolPref(AUTOSTART_PREF, true);
  Services.prefs.setBoolPref(FEATURE_PREF, true);
}

/**
 * Marks SITE with a rule, or removes it with IPPPrincipalRules.DEFAULT.
 *
 * @param {string} rule
 */
function setSiteRule(rule) {
  IPPPermissionRules.setRule(
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(SITE),
    rule
  );
}

/**
 * Runs `body` against a fresh instance and tears it down afterwards.
 *
 * An instance keeps its pref observers for as long as it is alive, so one a
 * previous task finished with still reacts. Each task therefore starts from
 * nothing owed and turns its own reason on.
 *
 * @param {(autoActivation: IPPAutoActivationSingleton) => Promise<void>} body
 * @param {object} [options]
 * @param {boolean} [options.signedIn] False to leave the service unable to serve.
 */
async function withAutoActivation(body, { signedIn = true } = {}) {
  setupStubs({ signedIn });
  REASON_PREFS.forEach(pref => Services.prefs.setBoolPref(pref, false));

  if (signedIn) {
    await initServiceToReady();
    await IPProtectionServerlist.maybeFetchList();
  } else {
    IPProtectionService.init();
  }

  const autoActivation = new IPPAutoActivationSingleton();
  try {
    await body(autoActivation);
  } finally {
    autoActivation.uninit();
    await IPPProxyManager.stop(false);
    IPProtectionService.uninit();
    REASON_PREFS.forEach(pref => Services.prefs.clearUserPref(pref));
    Services.prefs.clearUserPref(MATCH_PATTERNS_PREF);
    Services.perms.removeByType(PERM_NAME);
  }
}

add_setup(async function () {
  await putServerInRemoteSettings();
  IPProtectionService.uninit();

  // The module instance is a core helper; this file drives fresh instances.
  const sandbox = sinon.createSandbox();
  for (const method of ["init", "initOnStartupCompleted", "uninit"]) {
    sandbox.stub(IPPAutoActivation, method);
  }
  registerCleanupFunction(() => sandbox.restore());
  // Auto-restore stays available throughout; auto-start is switched on per
  // task, because it suppresses auto-restore by design.
  Services.prefs.setBoolPref(AUTO_RESTORE_PREF, true);

  registerCleanupFunction(() => {
    [...REASON_PREFS, AUTO_RESTORE_PREF, MATCH_PATTERNS_PREF].forEach(pref =>
      Services.prefs.clearUserPref(pref)
    );
    Services.perms.removeByType(PERM_NAME);
  });
});

/**
 * Tests that auto-start asks for full protection as it is initialized.
 */
add_task(async function test_autostart_asks_at_init() {
  await withAutoActivation(async autoActivation => {
    Assert.equal(
      autoActivation.desiredMode(),
      null,
      "Nothing should be wanted before it is initialized"
    );

    enableAutoStart();
    const activated = waitForProxyState(IPPProxyStates.ACTIVE);
    autoActivation.init();

    Assert.ok(
      IPPProxyManager.channelFilter()?.active,
      "Traffic should be held as soon as it is initialized"
    );

    await activated;

    Assert.equal(
      IPPProxyManager.mode,
      IPPMode.MODE_FULL,
      "Auto-start should ask for full protection"
    );
  });
});

/**
 * Tests that with the feature off there is nothing to do.
 */
add_task(async function test_autostart_disabled_asks_for_nothing() {
  await withAutoActivation(async autoActivation => {
    autoActivation.init();

    Assert.equal(
      autoActivation.desiredMode(),
      null,
      "Nothing should be wanted with the feature off"
    );
    Assert.ok(
      !IPPProxyManager.channelFilter(),
      "No filter should be registered"
    );
  });
});

/**
 * Tests that turning the VPN off is not overruled, because the reason to bring
 * it up is consumed when it is asked for.
 */
add_task(async function test_a_user_stop_is_not_overruled() {
  await withAutoActivation(async autoActivation => {
    enableAutoStart();
    const activated = waitForProxyState(IPPProxyStates.ACTIVE);
    autoActivation.init();
    await activated;

    await IPPProxyManager.stop(true);

    Assert.equal(
      autoActivation.desiredMode(),
      null,
      "Nothing should be wanted once the activation has been asked for"
    );
    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.READY,
      "The proxy should stay off after the user turned it off"
    );
  });
});

/**
 * Tests that a service which goes away and comes back is owed one again.
 */
add_task(async function test_owed_again_when_the_service_returns() {
  await withAutoActivation(async autoActivation => {
    enableAutoStart();
    const activated = waitForProxyState(IPPProxyStates.ACTIVE);
    autoActivation.init();
    await activated;
    await IPPProxyManager.stop(false);

    const wentNotReady = waitForProxyState(IPPProxyStates.NOT_READY);
    setupStubs({ signedIn: false });
    IPProtectionService.updateState();
    await wentNotReady;

    setupStubs({ signedIn: true });
    const reactivated = waitForProxyState(IPPProxyStates.ACTIVE);
    IPProtectionService.updateState();
    await reactivated;

    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.ACTIVE,
      "The proxy should come back up once the service does"
    );
  });
});

/**
 * Tests that the VPN is restored when the user left it on.
 */
add_task(async function test_restore_when_user_left_it_on() {
  await withAutoActivation(async autoActivation => {
    Services.prefs.setBoolPref(USER_ENABLED_PREF, true);
    autoActivation.init();
    Services.obs.notifyObservers(null, "sessionstore-restoring-on-startup");

    Assert.ok(
      autoActivation.willRestore,
      "A restore should be held open for this session"
    );
    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.ACTIVATING,
      "The proxy should be coming up"
    );

    autoActivation.initOnStartupCompleted();

    Assert.ok(
      !autoActivation.willRestore,
      "There should be nothing left to restore once startup has completed"
    );
  });
});

/**
 * Tests that the VPN is not restored when the user left it off.
 */
add_task(async function test_no_restore_when_user_left_it_off() {
  await withAutoActivation(async autoActivation => {
    autoActivation.init();
    Services.obs.notifyObservers(null, "sessionstore-restoring-on-startup");

    Assert.ok(!autoActivation.willRestore, "Nothing should be restored");
    Assert.notEqual(
      IPPProxyManager.state,
      IPPProxyStates.ACTIVATING,
      "The proxy should be left alone"
    );
  });
});

/**
 * Tests that a service which cannot serve a connection is not restored into.
 */
add_task(async function test_no_restore_when_service_cannot_serve() {
  await withAutoActivation(
    async autoActivation => {
      Services.prefs.setBoolPref(USER_ENABLED_PREF, true);

      Assert.equal(
        IPProtectionService.state,
        IPProtectionStates.UNAUTHENTICATED,
        "The service should report that it needs the user"
      );

      autoActivation.init();
      Services.obs.notifyObservers(null, "sessionstore-restoring-on-startup");

      Assert.ok(!autoActivation.willRestore, "Nothing should be restored");
      Assert.ok(
        !IPPProxyManager.channelFilter(),
        "No traffic should be held for it"
      );
    },
    { signedIn: false }
  );
});

/**
 * Tests that a site marked as always using the VPN brings the proxy up for the
 * included sites only.
 */
add_task(async function test_inclusion_asks_for_a_narrow_connection() {
  await withAutoActivation(async autoActivation => {
    setSiteRule(IPPPrincipalRules.INCLUDED);

    Assert.equal(
      autoActivation.desiredMode(),
      IPPMode.MODE_INCLUSION,
      "An included site should want a narrow connection"
    );

    const activated = waitForProxyState(IPPProxyStates.ACTIVE);
    autoActivation.init();
    await activated;

    Assert.equal(
      IPPProxyManager.mode,
      IPPMode.MODE_INCLUSION,
      "The proxy should only route the included sites"
    );
  });
});

/**
 * Tests that auto-activation routes through the location the user picked
 * rather than the recommended one.
 */
add_task(async function test_autoactivation_uses_selected_location() {
  const sandbox = sinon.createSandbox();
  Services.prefs.setStringPref(EGRESS_LOCATION_PREF, "US");
  try {
    await withAutoActivation(async autoActivation => {
      const getLocationSpy = sandbox.spy(IPProtectionServerlist, "getLocation");
      const getRecommendedSpy = sandbox.spy(
        IPProtectionServerlist,
        "getRecommendedLocation"
      );
      setSiteRule(IPPPrincipalRules.INCLUDED);

      const activated = waitForProxyState(IPPProxyStates.ACTIVE);
      autoActivation.init();
      await activated;

      Assert.ok(
        getLocationSpy.calledWith("US"),
        "The selected location should be looked up"
      );
      Assert.ok(
        getRecommendedSpy.notCalled,
        "The recommended location should not be used"
      );
    });
  } finally {
    sandbox.restore();
    Services.prefs.clearUserPref(EGRESS_LOCATION_PREF);
  }
});

/**
 * Tests that an exclusion wants nothing, while an inclusion counts whichever
 * provider holds it.
 */
add_task(async function test_inclusion_from_any_provider() {
  await withAutoActivation(async autoActivation => {
    setSiteRule(IPPPrincipalRules.EXCLUDED);
    Assert.equal(
      autoActivation.desiredMode(),
      null,
      "An exclusion is not an inclusion"
    );

    Services.prefs.setStringPref(
      MATCH_PATTERNS_PREF,
      JSON.stringify(["*://always-on.example.com/*"])
    );
    Assert.equal(
      autoActivation.desiredMode(),
      IPPMode.MODE_INCLUSION,
      "A site included through the match patterns pref should want a narrow connection"
    );
  });
});

/**
 * Tests that an included site is a standing reason: turning the VPN off leaves
 * the included sites covered by a narrow connection.
 */
add_task(async function test_inclusion_comes_back_after_a_stop() {
  await withAutoActivation(async autoActivation => {
    setSiteRule(IPPPrincipalRules.INCLUDED);
    const activated = waitForProxyState(IPPProxyStates.ACTIVE);
    autoActivation.init();
    await activated;

    // Widened the way the panel does, so the restart is observable.
    IPPProxyManager.setMode(IPPMode.MODE_FULL);
    await IPPProxyManager.stop(true);
    await waitForProxyState(IPPProxyStates.ACTIVE);

    Assert.equal(
      IPPProxyManager.mode,
      IPPMode.MODE_INCLUSION,
      "Stopping should leave a connection for the included sites"
    );
  });
});

/**
 * Tests that a restore replaces a narrow activation still in flight, since
 * full protection wins.
 */
add_task(async function test_restore_replaces_a_pending_inclusion() {
  await withAutoActivation(async autoActivation => {
    Services.prefs.setBoolPref(USER_ENABLED_PREF, true);
    setSiteRule(IPPPrincipalRules.INCLUDED);

    autoActivation.init();
    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.ACTIVATING,
      "The narrow activation should be in flight"
    );

    Services.obs.notifyObservers(null, "sessionstore-restoring-on-startup");
    await waitForProxyState(IPPProxyStates.ACTIVE);

    Assert.equal(
      IPPProxyManager.mode,
      IPPMode.MODE_FULL,
      "The restore should have replaced the narrow activation"
    );
  });
});

/**
 * Makes the next proxy pass request fail.
 */
function failProxyPass() {
  IPPDummyAuthProvider.setProxyPass({
    status: 500,
    error: "test_error",
    usage: null,
  });
}

/**
 * Tests that an inclusion activation that fails is not retried: the proxy
 * stays in ERROR, even as more rules come in.
 */
add_task(async function test_failed_inclusion_is_not_retried() {
  await withAutoActivation(async autoActivation => {
    failProxyPass();
    setSiteRule(IPPPrincipalRules.INCLUDED);
    const startSpy = sinon.spy(IPPProxyManager, "start");

    try {
      const failed = waitForProxyState(IPPProxyStates.ERROR);
      autoActivation.init();
      await failed;

      Services.prefs.setStringPref(
        MATCH_PATTERNS_PREF,
        JSON.stringify(["*://other.example.com/*"])
      );

      Assert.equal(startSpy.callCount, 1, "The proxy should be asked once");
      Assert.equal(
        IPPProxyManager.state,
        IPPProxyStates.ERROR,
        "The proxy should stay in ERROR"
      );
    } finally {
      startSpy.restore();
    }
  });
});

/**
 * Tests that the last inclusion going away clears a failed inclusion
 * activation.
 */
add_task(async function test_failed_inclusion_cleared_by_last_rule() {
  await withAutoActivation(async autoActivation => {
    failProxyPass();
    setSiteRule(IPPPrincipalRules.INCLUDED);
    const failed = waitForProxyState(IPPProxyStates.ERROR);
    autoActivation.init();
    await failed;

    const stopped = waitForProxyState(IPPProxyStates.READY);
    setSiteRule(IPPPrincipalRules.DEFAULT);
    await stopped;

    Assert.ok(
      !IPPProxyManager.channelFilter(),
      "The channel filter should be gone"
    );
  });
});

/**
 * Fails an inclusion activation, then stops the proxy out of ERROR with a
 * working proxy pass back in place, so a restart would succeed.
 *
 * @param {IPPAutoActivationSingleton} autoActivation
 */
async function stopFailedInclusion(autoActivation) {
  failProxyPass();
  setSiteRule(IPPPrincipalRules.INCLUDED);
  const startSpy = sinon.spy(IPPProxyManager, "start");
  try {
    autoActivation.init();
    // Settled, not just in ERROR, so the activation is no longer in flight.
    await startSpy.firstCall.returnValue;
  } finally {
    startSpy.restore();
  }
  Assert.equal(IPPProxyManager.state, IPPProxyStates.ERROR);

  setupStubs();
  await IPPProxyManager.stop(true);
}

/**
 * Tests that a user retry out of ERROR that fails too is not followed by an
 * inclusion attempt of its own.
 */
add_task(async function test_failed_user_retry_is_not_followed_up() {
  await withAutoActivation(async autoActivation => {
    failProxyPass();
    setSiteRule(IPPPrincipalRules.INCLUDED);
    const startSpy = sinon.spy(IPPProxyManager, "start");
    try {
      autoActivation.init();
      await startSpy.firstCall.returnValue;
      Assert.equal(IPPProxyManager.state, IPPProxyStates.ERROR);

      await IPPProxyManager.start(true);
      await new Promise(resolve => executeSoon(resolve));

      Assert.equal(
        startSpy.callCount,
        2,
        "Only the inclusion and the user retry should be attempted"
      );
      Assert.equal(IPPProxyManager.state, IPPProxyStates.READY);
    } finally {
      startSpy.restore();
    }
  });
});

/**
 * Tests that stopping the proxy out of ERROR is not overruled by the
 * inclusion that failed.
 */
add_task(async function test_stop_out_of_error_is_not_overruled() {
  await withAutoActivation(async autoActivation => {
    await stopFailedInclusion(autoActivation);

    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.READY,
      "The proxy should stay idle"
    );
  });
});

/**
 * Tests that a rule change after stopping out of ERROR asks for the narrow
 * connection again.
 */
add_task(async function test_rule_change_after_error_stop_activates() {
  await withAutoActivation(async autoActivation => {
    await stopFailedInclusion(autoActivation);

    const activated = waitForProxyState(IPPProxyStates.ACTIVE);
    Services.prefs.setStringPref(
      MATCH_PATTERNS_PREF,
      JSON.stringify(["*://other.example.com/*"])
    );
    await activated;

    Assert.equal(
      IPPProxyManager.mode,
      IPPMode.MODE_INCLUSION,
      "A rule change should bring the narrow connection back"
    );
  });
});

/**
 * Tests that a connection coming up after a failure re-arms the inclusion:
 * turning that connection off brings the narrow one back.
 */
add_task(async function test_working_connection_rearms_after_error() {
  await withAutoActivation(async autoActivation => {
    await stopFailedInclusion(autoActivation);

    await IPPProxyManager.start(true);
    Assert.equal(IPPProxyManager.state, IPPProxyStates.ACTIVE);

    const restarted = waitForProxyState(IPPProxyStates.ACTIVE);
    await IPPProxyManager.stop(true);
    await restarted;
    Assert.equal(
      IPPProxyManager.mode,
      IPPMode.MODE_INCLUSION,
      "Turning off a working connection should bring the narrow one back"
    );
  });
});

/**
 * Tests that the restart after a stop does not leave later listeners with an
 * event for a state the proxy has already left.
 */
add_task(async function test_restart_after_stop_sends_no_stale_events() {
  await withAutoActivation(async autoActivation => {
    setSiteRule(IPPPrincipalRules.INCLUDED);
    const activated = waitForProxyState(IPPProxyStates.ACTIVE);
    autoActivation.init();
    await activated;

    const stale = [];
    const listener = event => {
      if (event.detail.state !== IPPProxyManager.state) {
        stale.push(event.detail.state);
      }
    };
    IPPProxyManager.addEventListener("IPPProxyManager:StateChanged", listener);
    try {
      const restarted = waitForProxyState(IPPProxyStates.ACTIVE);
      await IPPProxyManager.stop(true);
      await restarted;
    } finally {
      IPPProxyManager.removeEventListener(
        "IPPProxyManager:StateChanged",
        listener
      );
    }

    Assert.deepEqual(stale, [], "Every event should match the current state");
  });
});

/**
 * Tests that rules changing mid-session are followed: the first included site
 * brings a narrow connection up, and the last one going away takes it down.
 */
add_task(async function test_inclusion_follows_the_rules() {
  await withAutoActivation(async autoActivation => {
    autoActivation.init();
    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.READY,
      "The proxy should start idle"
    );

    setSiteRule(IPPPrincipalRules.INCLUDED);
    await waitForProxyState(IPPProxyStates.ACTIVE);
    Assert.equal(
      IPPProxyManager.mode,
      IPPMode.MODE_INCLUSION,
      "A new inclusion should bring a narrow connection up"
    );

    // Armed first: the last rule going stops the proxy before it yields.
    const stopped = waitForProxyState(IPPProxyStates.READY);
    setSiteRule(IPPPrincipalRules.DEFAULT);
    await stopped;
    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.READY,
      "The last inclusion going away should take the connection down"
    );
  });
});
