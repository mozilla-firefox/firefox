/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { GuestAuthProviderImpl } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/auth/GuestAuthProviderImpl.sys.mjs"
);
const { FxaAuthProviderImpl } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/auth/FxaAuthProviderImpl.sys.mjs"
);
const { FxaSignedInWatcher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/auth/FxaSignedInWatcher.sys.mjs"
);
const { ToUSpotlight } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ToUSpotlight.sys.mjs"
);
const { Spotlight } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/Spotlight.sys.mjs"
);

const PREF_TOKEN = "browser.smartwindow.auth.guest.token";
const PREF_MODE = "browser.smartwindow.auth.guest.mode";
const PREF_AUTH_IMPL = "browser.smartwindow.auth.impl";
const PREF_FIRSTRUN_COMPLETED = "browser.smartwindow.firstrun.hasCompleted";
const PREF_TOU_CONSENT_TIME = "browser.smartwindow.tos.consentTime";
const SUCCESS_PATH = "/verify/v1/guest/success";

/**
 * Runs a task against a mock MLPA server set as the Smart Window endpoint.
 *
 * @param {string} mode Value of the guest sign-in mode pref.
 * @param {Function} onVerify Called with the state and origin of each
 *   /verify/v1/guest request. Returns the URL to redirect to, an object to
 *   serve as JSON, an HTTP error status, or null to serve a plain page.
 * @param {Function} task Called with the origin of the mock server.
 */
async function withMockMLPA(mode, onVerify, task) {
  const server = new HttpServer();
  server.registerPathHandler("/verify/v1/guest", (request, response) => {
    const state = new URLSearchParams(request.queryString).get("state");
    const location = onVerify(state, origin);
    if (typeof location === "number") {
      response.setStatusLine(request.httpVersion, location, "Error");
      return;
    }
    if (location && typeof location === "object") {
      response.setHeader("Content-Type", "application/json", false);
      response.write(JSON.stringify(location));
      return;
    }
    if (location) {
      response.setStatusLine(request.httpVersion, 302, "Found");
      response.setHeader("Location", location, false);
      return;
    }
    response.setHeader("Content-Type", "text/html", false);
    response.write("<!doctype html><title>verify</title>");
  });
  server.registerPathHandler(SUCCESS_PATH, (_request, response) => {
    response.setHeader("Content-Type", "text/html", false);
    response.write("<!doctype html><title>success</title>");
  });
  server.start(-1);

  // eslint-disable-next-line sdl/no-insecure-url
  const origin = `http://localhost:${server.identity.primaryPort}`;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.endpoint", `${origin}/v1`],
      [PREF_MODE, mode],
    ],
  });

  try {
    await task(origin);
  } finally {
    Services.prefs.clearUserPref(PREF_TOKEN);
    await SpecialPowers.popPrefEnv();
    await new Promise(resolve => server.stop(resolve));
  }
}

function assertSwitched(from, to, reason) {
  const events = Glean.smartWindow.authImplSwitched.testGetValue();
  Assert.equal(events?.length, 1, "One auth_impl_switched was recorded");
  Assert.deepEqual(
    events[0].extra,
    { from, to, reason },
    `auth_impl_switched records ${from} -> ${to} (${reason})`
  );
  Assert.equal(
    Glean.smartWindow.authImpl.testGetValue(),
    to,
    `auth_impl is ${to}`
  );
}

function successURL(origin, params) {
  return `${origin}${SUCCESS_PATH}#${new URLSearchParams(params)}`;
}

function waitForSuccessTab(origin) {
  return BrowserTestUtils.waitForNewTab(
    gBrowser,
    url => url.startsWith(`${origin}${SUCCESS_PATH}`),
    true
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_TOU_CONSENT_TIME, 1735689600]],
  });
});

add_task(async function test_signIn_fetch_stores_token() {
  await withMockMLPA(
    "fetch",
    () => ({ access_token: "guest-token", token_type: "Bearer" }),
    async () => {
      const impl = new GuestAuthProviderImpl();
      const tabCount = gBrowser.tabs.length;

      Assert.ok(await impl.signIn(gBrowser.selectedBrowser), "signIn");
      Assert.equal(await impl.getToken(), "guest-token", "getToken");
      Assert.ok(await impl.isSignedIn(), "Signed in after signIn");
      Assert.equal(gBrowser.tabs.length, tabCount, "No tab is opened");
    }
  );
});

add_task(async function test_signIn_fetch_invalid_response() {
  await withMockMLPA(
    "fetch",
    () => ({ token_type: "Bearer" }),
    async () => {
      const sb = sinon.createSandbox();
      const showUnavailable = sb
        .stub(ToUSpotlight, "showUnavailable")
        .resolves(true);
      try {
        const impl = new GuestAuthProviderImpl();
        await Assert.rejects(
          impl.signIn(gBrowser.selectedBrowser),
          /Invalid guest token response/,
          "signIn rejects without an access token"
        );
        Assert.equal(await impl.getToken(), null, "No token is cached");
        Assert.ok(
          showUnavailable.calledOnce,
          "The unavailable Spotlight is shown"
        );
      } finally {
        sb.restore();
      }
    }
  );
});

add_task(async function test_signIn_fetch_http_error() {
  await withMockMLPA(
    "fetch",
    () => 500,
    async () => {
      const sb = sinon.createSandbox();
      const showUnavailable = sb
        .stub(ToUSpotlight, "showUnavailable")
        .resolves(true);
      try {
        const impl = new GuestAuthProviderImpl();
        await Assert.rejects(
          impl.signIn(gBrowser.selectedBrowser),
          /Guest token request failed: 500/,
          "signIn rejects on an HTTP error"
        );
        Assert.equal(await impl.getToken(), null, "No token is cached");
        Assert.ok(
          showUnavailable.calledOnce,
          "The unavailable Spotlight is shown"
        );
      } finally {
        sb.restore();
      }
    }
  );
});

add_task(async function test_signIn_fetch_unavailable_rejects() {
  await withMockMLPA(
    "fetch",
    () => 500,
    async () => {
      const sb = sinon.createSandbox();
      const showUnavailable = sb
        .stub(ToUSpotlight, "showUnavailable")
        .rejects(new Error("Spotlight failure"));
      try {
        const impl = new GuestAuthProviderImpl();
        await Assert.rejects(
          impl.signIn(gBrowser.selectedBrowser),
          /Guest token request failed: 500/,
          "signIn rejects with the original error"
        );
        Assert.ok(
          showUnavailable.calledOnce,
          "The unavailable Spotlight is attempted"
        );
      } finally {
        sb.restore();
      }
    }
  );
});

add_task(async function test_signIn_fetch_success_no_unavailable() {
  const sb = sinon.createSandbox();
  const showUnavailable = sb.stub(ToUSpotlight, "showUnavailable");
  try {
    await withMockMLPA(
      "fetch",
      () => ({ access_token: "guest-token", token_type: "Bearer" }),
      async () => {
        const impl = new GuestAuthProviderImpl();
        Assert.ok(await impl.signIn(gBrowser.selectedBrowser), "signIn");
        Assert.ok(
          showUnavailable.notCalled,
          "The unavailable Spotlight is not shown"
        );
      }
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_signIn_stores_token_and_closes_tab() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_FIRSTRUN_COMPLETED, true]] });

  await withMockMLPA(
    "tab",
    (state, origin) =>
      successURL(origin, { state, access_token: "guest-token" }),
    async () => {
      const impl = new GuestAuthProviderImpl();
      const tabCount = gBrowser.tabs.length;

      Assert.ok(!(await impl.isSignedIn()), "Not signed in before signIn");

      const didSignIn = await impl.signIn(gBrowser.selectedBrowser);

      Assert.ok(didSignIn, "signIn resolves to true");
      Assert.equal(
        Services.prefs.getStringPref(PREF_TOKEN, ""),
        "guest-token",
        "The access token is cached in the pref"
      );
      Assert.equal(await impl.getToken(), "guest-token", "getToken");
      Assert.ok(await impl.isSignedIn(), "Signed in after signIn");
      Assert.equal(gBrowser.tabs.length, tabCount, "The guest tab is closed");
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_signIn_keeps_tab_during_firstrun() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_FIRSTRUN_COMPLETED, false]] });

  await withMockMLPA(
    "tab",
    (state, origin) =>
      successURL(origin, { state, access_token: "guest-token" }),
    async origin => {
      const impl = new GuestAuthProviderImpl();
      const tabPromise = waitForSuccessTab(origin);

      const didSignIn = await impl.signIn(gBrowser.selectedBrowser);
      const tab = await tabPromise;

      Assert.ok(didSignIn, "signIn resolves to true");
      Assert.ok(tab.isConnected, "The guest tab stays open during first run");
      BrowserTestUtils.removeTab(tab);
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_signIn_ignores_mismatched_state() {
  await withMockMLPA(
    "tab",
    (_state, origin) =>
      successURL(origin, { state: "not-our-state", access_token: "evil" }),
    async origin => {
      const impl = new GuestAuthProviderImpl();
      const tabPromise = waitForSuccessTab(origin);

      const signInPromise = impl.signIn(gBrowser.selectedBrowser);
      const tab = await tabPromise;
      BrowserTestUtils.removeTab(tab);

      Assert.ok(!(await signInPromise), "signIn resolves to false");
      Assert.equal(await impl.getToken(), null, "No token is cached");
    }
  );
});

add_task(async function test_signIn_ignores_other_origins() {
  let verifyState = null;

  await withMockMLPA(
    "tab",
    state => {
      verifyState = state;
      return null;
    },
    async origin => {
      const impl = new GuestAuthProviderImpl();
      const tabPromise = BrowserTestUtils.waitForNewTab(
        gBrowser,
        url => url.startsWith(origin),
        true
      );

      const signInPromise = impl.signIn(gBrowser.selectedBrowser);
      const tab = await tabPromise;
      Assert.ok(verifyState, "The verify page received a state");

      const otherURL = successURL("https://example.com", {
        state: verifyState,
        access_token: "evil",
      });
      const loaded = BrowserTestUtils.browserLoaded(
        tab.linkedBrowser,
        false,
        otherURL
      );
      BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, otherURL);
      await loaded;
      BrowserTestUtils.removeTab(tab);

      Assert.ok(!(await signInPromise), "signIn resolves to false");
      Assert.equal(await impl.getToken(), null, "No token is cached");
    }
  );
});

add_task(async function test_signIn_tab_closed() {
  await withMockMLPA(
    "tab",
    () => null,
    async origin => {
      const impl = new GuestAuthProviderImpl();
      const tabPromise = BrowserTestUtils.waitForNewTab(
        gBrowser,
        url => url.startsWith(origin),
        true
      );

      const signInPromise = impl.signIn(gBrowser.selectedBrowser);
      BrowserTestUtils.removeTab(await tabPromise);

      Assert.ok(!(await signInPromise), "signIn resolves to false");
      Assert.ok(!(await impl.isSignedIn()), "Not signed in");
    }
  );
});

add_task(async function test_invalidateToken() {
  const impl = new GuestAuthProviderImpl();
  let fallbacks = 0;
  impl.addEventListener("AuthProviderImpl:FallbackNeeded", () => fallbacks++);
  Services.prefs.setStringPref(PREF_TOKEN, "guest-token");

  await impl.invalidateToken("other-token");
  Assert.equal(
    await impl.getToken(),
    "guest-token",
    "A different token does not clear the cached one"
  );
  Assert.equal(fallbacks, 0, "A different token does not need a fallback");

  await impl.invalidateToken("guest-token");
  Assert.equal(await impl.getToken(), null, "The cached token is cleared");
  Assert.ok(!(await impl.isSignedIn()), "Not signed in after invalidation");
  Assert.equal(fallbacks, 1, "A fallback is requested");

  await impl.invalidateToken("guest-token");
  Assert.equal(fallbacks, 1, "The fallback is requested only once");

  Services.prefs.clearUserPref(PREF_TOKEN);
});

add_task(async function test_auth_impl_set_on_setImpl() {
  Services.fog.testResetFOG();
  try {
    AuthProvider.setImpl(new GuestAuthProviderImpl());
    Assert.equal(
      Glean.smartWindow.authImpl.testGetValue(),
      "guest",
      "auth_impl is guest"
    );
    AuthProvider.setImpl(new FxaAuthProviderImpl());
    Assert.equal(
      Glean.smartWindow.authImpl.testGetValue(),
      "fxa",
      "auth_impl is fxa"
    );
  } finally {
    AuthProvider.setImpl(new FxaAuthProviderImpl());
  }
});

add_task(async function test_auth_impl_pref_change_switches_to_fxa() {
  const sb = sinon.createSandbox();
  sb.stub(FxaAuthProviderImpl.prototype, "getToken").resolves("fxa-token");
  Services.prefs.setStringPref(PREF_TOKEN, "guest-token");
  AuthProvider.setImpl(new GuestAuthProviderImpl());
  Services.fog.testResetFOG();

  try {
    await SpecialPowers.pushPrefEnv({ set: [[PREF_AUTH_IMPL, "guest"]] });
    Assert.equal(
      await AuthProvider.getToken(),
      "guest-token",
      "Setting the pref to guest keeps the guest impl"
    );

    await SpecialPowers.pushPrefEnv({ set: [[PREF_AUTH_IMPL, "fxa"]] });
    Assert.equal(
      await AuthProvider.getToken(),
      "fxa-token",
      "Setting the pref to fxa switches to the FxA impl"
    );
    assertSwitched("guest", "fxa", "other");

    await SpecialPowers.popPrefEnv();
    await SpecialPowers.popPrefEnv();
  } finally {
    sb.restore();
    Services.prefs.clearUserPref(PREF_TOKEN);
  }
});

add_task(async function test_rejected_token_switches_to_fxa() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_AUTH_IMPL, "guest"]] });
  AuthProvider.setImpl(new GuestAuthProviderImpl());
  const sb = sinon.createSandbox();
  const fxaGetToken = sb
    .stub(FxaAuthProviderImpl.prototype, "getToken")
    .resolves("fxa-token");
  Services.prefs.setStringPref(PREF_TOKEN, "guest-token");
  Services.fog.testResetFOG();

  try {
    Assert.equal(await AuthProvider.getToken(), "guest-token", "Guest token");

    await AuthProvider.invalidateToken("guest-token");

    Assert.equal(
      Services.prefs.getStringPref(PREF_AUTH_IMPL),
      "fxa",
      "The auth impl pref is switched to FxA"
    );
    assertSwitched("guest", "fxa", "token_rejected");
    Assert.equal(
      await AuthProvider.getToken(),
      "fxa-token",
      "AuthProvider uses FxA after the guest token is rejected"
    );
    Assert.ok(fxaGetToken.calledOnce, "The FxA impl provides the token");
  } finally {
    sb.restore();
    Services.prefs.clearUserPref(PREF_TOKEN);
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_fxa_sign_in_switches_to_fxa() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_AUTH_IMPL, ""]] });
  AuthProvider.setImpl(new GuestAuthProviderImpl());
  Services.fog.testResetFOG();
  const sb = sinon.createSandbox();
  sb.stub(FxaSignedInWatcher, "signedIn").get(() => true);

  try {
    FxaSignedInWatcher.dispatchEvent(
      new Event("FxaSignedInWatcher:StateChanged")
    );

    Assert.equal(
      Services.prefs.getStringPref(PREF_AUTH_IMPL),
      "fxa",
      "The auth impl pref is set to FxA"
    );
    Assert.equal(
      await AuthProvider.getProviderName(),
      "fxa",
      "AuthProvider uses FxA after the account signs in"
    );
    assertSwitched("guest", "fxa", "fxa_signed_in");
  } finally {
    sb.restore();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_rejected_token_with_guest_disabled() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_AUTH_IMPL, "fxa"]] });
  const sb = sinon.createSandbox();
  const fxaGetToken = sb
    .stub(FxaAuthProviderImpl.prototype, "getToken")
    .resolves("fxa-token");
  Services.prefs.setStringPref(PREF_TOKEN, "guest-token");
  AuthProvider.setImpl(new GuestAuthProviderImpl());

  try {
    await AuthProvider.invalidateToken("guest-token");

    Assert.equal(
      await AuthProvider.getToken(),
      null,
      "AuthProvider keeps the guest impl when the pref is out of sync"
    );
    Assert.ok(fxaGetToken.notCalled, "The FxA impl is not used");
  } finally {
    sb.restore();
    Services.prefs.clearUserPref(PREF_TOKEN);
    AuthProvider.setImpl(new FxaAuthProviderImpl());
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_signIn_declined_tou() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF_TOU_CONSENT_TIME, 0]] });
  const sb = sinon.createSandbox();
  const show = sb.stub(ToUSpotlight, "show").resolves(false);
  let verifyRequests = 0;

  try {
    await withMockMLPA(
      "fetch",
      () => {
        verifyRequests++;
        return { access_token: "guest-token", token_type: "Bearer" };
      },
      async () => {
        const impl = new GuestAuthProviderImpl();
        Assert.ok(
          !(await impl.signIn(gBrowser.selectedBrowser)),
          "signIn resolves to false"
        );
        Assert.ok(show.calledOnce, "The ToU Spotlight is shown");
        Assert.equal(verifyRequests, 0, "MLPA is not contacted");
        Assert.equal(await impl.getToken(), null, "No token is cached");
        Assert.equal(
          Services.prefs.getIntPref(PREF_TOU_CONSENT_TIME),
          0,
          "No consent is recorded"
        );
      }
    );
  } finally {
    sb.restore();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_signIn_accepted_tou() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_TOU_CONSENT_TIME, 0],
      [PREF_AUTH_IMPL, "guest"],
    ],
  });
  AuthProvider.setImpl(new GuestAuthProviderImpl());
  const sb = sinon.createSandbox();
  const show = sb.stub(ToUSpotlight, "show").resolves(true);

  try {
    await withMockMLPA(
      "fetch",
      () => ({ access_token: "guest-token", token_type: "Bearer" }),
      async () => {
        Assert.ok(
          await AuthProvider.promptSignIn(gBrowser.selectedBrowser),
          "promptSignIn"
        );
        Assert.ok(show.calledOnce, "The ToU Spotlight is shown");
        Assert.greater(
          Services.prefs.getIntPref(PREF_TOU_CONSENT_TIME),
          0,
          "AuthProvider records the consent"
        );
        Assert.equal(await AuthProvider.getToken(), "guest-token", "getToken");
      }
    );
  } finally {
    sb.restore();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_signIn_skips_tou_with_consent() {
  const sb = sinon.createSandbox();
  const show = sb.stub(ToUSpotlight, "show").resolves(false);

  try {
    await withMockMLPA(
      "fetch",
      () => ({ access_token: "guest-token", token_type: "Bearer" }),
      async () => {
        const impl = new GuestAuthProviderImpl();
        Assert.ok(await impl.signIn(gBrowser.selectedBrowser), "signIn");
        Assert.ok(show.notCalled, "The ToU Spotlight is not shown");
      }
    );
  } finally {
    sb.restore();
  }
});

/**
 * Runs ToUSpotlight.show with the Spotlight dialog stubbed out.
 *
 * @param {boolean} accept Whether the stubbed dialog accepts the ToU.
 * @returns {Promise<object>} The result of show and the Spotlight stub.
 */
async function runToUSpotlight(accept) {
  const sb = sinon.createSandbox();
  const showSpotlightDialog = sb
    .stub(Spotlight, "showSpotlightDialog")
    .callsFake(async () => {
      if (accept) {
        Services.obs.notifyObservers(null, "smartwindow-tou-accepted");
      }
      return true;
    });

  try {
    const accepted = await ToUSpotlight.show(gBrowser.selectedBrowser);
    return { accepted, showSpotlightDialog };
  } finally {
    sb.restore();
  }
}

add_task(async function test_tou_spotlight_accepted() {
  const { accepted, showSpotlightDialog } = await runToUSpotlight(true);
  Assert.ok(accepted, "show resolves to true");
  Assert.ok(showSpotlightDialog.calledOnce, "The Spotlight is shown");
  Assert.equal(
    showSpotlightDialog.firstCall.args[1].id,
    "AI_WINDOW_TOU_GUEST_MODAL",
    "The guest ToU message is shown"
  );
});

add_task(async function test_tou_spotlight_declined() {
  const { accepted, showSpotlightDialog } = await runToUSpotlight(false);
  Assert.ok(!accepted, "show resolves to false");
  Assert.ok(showSpotlightDialog.calledOnce, "The Spotlight is shown");
});

add_task(async function test_unavailable_spotlight() {
  const sb = sinon.createSandbox();
  const showSpotlightDialog = sb
    .stub(Spotlight, "showSpotlightDialog")
    .resolves(true);

  try {
    Assert.ok(
      await ToUSpotlight.showUnavailable(gBrowser.selectedBrowser),
      "showUnavailable resolves to true"
    );
    Assert.ok(showSpotlightDialog.calledOnce, "The Spotlight is shown");
    Assert.equal(
      showSpotlightDialog.firstCall.args[1].id,
      "AI_WINDOW_GUEST_UNAVAILABLE_MODAL",
      "The unavailable message is shown"
    );
    const screen =
      showSpotlightDialog.firstCall.args[1].content.screens[0].content;
    Assert.deepEqual(
      screen.above_button_content[0].link_keys,
      ["sign_in"],
      "The body has a sign in link"
    );
    const { orderedExecution, actions } = screen.sign_in.action.data;
    Assert.ok(orderedExecution, "The sign in actions run in order");
    Assert.deepEqual(
      actions.map(a => a.type),
      ["FXA_SIGNIN_FLOW", "FXA_AIWINDOW_SIGNIN_FLOW"],
      "The sign in link signs in to FxA, then launches Smart Window"
    );
    Assert.ok(
      actions[1].requiresPrevious,
      "Smart Window is only launched after a successful FxA sign-in"
    );
    Assert.ok(screen.sign_in.action.dismiss, "The sign in link dismisses");
  } finally {
    sb.restore();
  }
});
