/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AuthImplSelector } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/auth/AuthImplSelector.sys.mjs"
);
const { AuthProvider } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs"
);
const { FxaAuthProviderImpl } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/auth/FxaAuthProviderImpl.sys.mjs"
);
const { FxaSignedInWatcher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/auth/FxaSignedInWatcher.sys.mjs"
);
const { GuestAuthProviderImpl } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/auth/GuestAuthProviderImpl.sys.mjs"
);
const { ONLOGOUT_NOTIFICATION } = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccountsCommon.sys.mjs"
);
const { NimbusFeatures } = ChromeUtils.importESModule(
  "resource://nimbus/ExperimentAPI.sys.mjs"
);
const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const PREF_AUTH_IMPL = "browser.smartwindow.auth.impl";
const PREF_GUEST_TOKEN = "browser.smartwindow.auth.guest.token";
const PREF_GUEST_MODE = "browser.smartwindow.auth.guest.mode";

NimbusTestUtils.init(this);

let sb;
let watcherInit;
let signedIn = false;

add_setup(async () => {
  do_get_profile();
  const { cleanup } = await NimbusTestUtils.setupTest();
  NimbusFeatures.smartWindow.onUpdate(AuthImplSelector.applyNimbusAuthImpl);
  sb = sinon.createSandbox();
  watcherInit = sb.stub(FxaSignedInWatcher, "init").resolves();
  sb.stub(FxaSignedInWatcher, "signedIn").get(() => signedIn);
  sb.stub(FxaAuthProviderImpl.prototype, "getToken").resolves("fxa-token");
  registerCleanupFunction(() => {
    sb.restore();
    Services.prefs.clearUserPref(PREF_AUTH_IMPL);
    Services.prefs.clearUserPref(PREF_GUEST_TOKEN);
  });
  registerCleanupFunction(async () => {
    NimbusFeatures.smartWindow.offUpdate(AuthImplSelector.applyNimbusAuthImpl);
    await cleanup();
  });
});

/**
 * Simulates a change of the FxA signed-in state.
 *
 * @param {boolean} value
 */
function setSignedIn(value) {
  signedIn = value;
  FxaSignedInWatcher.dispatchEvent(
    new Event("FxaSignedInWatcher:StateChanged")
  );
}

/**
 * Sets up the selection inputs and returns what a new selector settles on.
 *
 * @param {object} state
 * @param {string} [state.impl] Value of the auth impl pref.
 * @param {string} [state.guestToken] Value of the guest token pref.
 * @param {boolean} state.fxa Whether a verified FxA account is signed in.
 * @returns {Promise<object>} The selector, its impl and the resulting pref
 *   value.
 */
async function select({ impl = "", guestToken = "", fxa }) {
  signedIn = fxa;
  Services.prefs.setStringPref(PREF_GUEST_TOKEN, guestToken);
  Services.prefs.setStringPref(PREF_AUTH_IMPL, impl);
  const selector = new AuthImplSelector();
  return {
    selector,
    impl: await selector.getImpl(),
    pref: Services.prefs.getStringPref(PREF_AUTH_IMPL),
  };
}

add_task(async function test_AuthProvider_selection() {
  Services.prefs.setStringPref(PREF_AUTH_IMPL, "guest");
  Services.prefs.setStringPref(PREF_AUTH_IMPL, "");
  setSignedIn(false);
  Assert.ok(watcherInit.notCalled, "Nothing is selected before first use");

  Assert.equal(await AuthProvider.getToken(), null, "Guest without a token");
  Assert.ok(watcherInit.calledOnce, "The impl is selected on first use");

  setSignedIn(true);
  Assert.equal(
    await AuthProvider.getToken(),
    "fxa-token",
    "Signing in to FxA switches to the FxA impl"
  );
  Assert.equal(
    Services.prefs.getStringPref(PREF_AUTH_IMPL),
    "fxa",
    "The FxA choice is persisted"
  );

  let loggedOut = false;
  AuthProvider.addEventListener(
    "AuthProvider:Logout",
    () => (loggedOut = true),
    { once: true }
  );
  Services.obs.notifyObservers(null, ONLOGOUT_NOTIFICATION);
  setSignedIn(false);
  Assert.ok(loggedOut, "AuthProvider fires AuthProvider:Logout");
  Assert.equal(
    await AuthProvider.getToken(),
    "fxa-token",
    "Signing out of FxA keeps the FxA impl"
  );
  Assert.equal(
    Services.prefs.getStringPref(PREF_AUTH_IMPL),
    "fxa",
    "The FxA choice is kept"
  );
});

add_task(async function test_select_unset() {
  let result = await select({ fxa: false });
  Assert.ok(
    result.impl instanceof GuestAuthProviderImpl,
    "Guest when signed out"
  );
  Assert.equal(result.pref, "", "The guest choice is not persisted");

  result = await select({ fxa: true });
  Assert.ok(result.impl instanceof FxaAuthProviderImpl, "FxA when signed in");
  Assert.equal(result.pref, "fxa", "The FxA choice is persisted");

  result = await select({ guestToken: "guest-token", fxa: true });
  Assert.ok(
    result.impl instanceof FxaAuthProviderImpl,
    "FxA when signed in, even with a guest token"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue(PREF_GUEST_TOKEN),
    "The stale guest token is cleared"
  );
});

add_task(async function test_select_guest() {
  let result = await select({
    impl: "guest",
    guestToken: "guest-token",
    fxa: false,
  });
  Assert.ok(
    result.impl instanceof GuestAuthProviderImpl,
    "Guest when signed out"
  );
  Assert.equal(result.pref, "guest", "The pref is unchanged");
  Assert.equal(
    Services.prefs.getStringPref(PREF_GUEST_TOKEN),
    "guest-token",
    "The guest token is kept"
  );

  result = await select({ impl: "guest", fxa: true });
  Assert.ok(
    result.impl instanceof GuestAuthProviderImpl,
    "Guest is forced, even when signed in to FxA"
  );
  Assert.equal(result.pref, "guest", "The pref is unchanged");
});

add_task(async function test_select_fxa() {
  const result = await select({
    impl: "fxa",
    guestToken: "guest-token",
    fxa: false,
  });
  Assert.ok(
    result.impl instanceof FxaAuthProviderImpl,
    "FxA when the pref says so, even when signed out"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue(PREF_GUEST_TOKEN),
    "The stale guest token is cleared"
  );
});

add_task(async function test_select_unknown() {
  let result = await select({ impl: "unknown", fxa: false });
  Assert.ok(
    result.impl instanceof GuestAuthProviderImpl,
    "An unknown value is handled as unset: guest when signed out"
  );

  result = await select({ impl: "unknown", fxa: true });
  Assert.ok(
    result.impl instanceof FxaAuthProviderImpl,
    "An unknown value is handled as unset: FxA when signed in"
  );
  Assert.equal(result.pref, "fxa", "The FxA choice is persisted");
});

add_task(async function test_impl_is_kept() {
  const { selector, impl: guest } = await select({ fxa: false });
  Services.prefs.setStringPref(PREF_AUTH_IMPL, "guest");
  Assert.equal(
    await selector.getImpl(),
    guest,
    "The guest impl is kept when the selection does not change"
  );

  setSignedIn(true);
  Assert.equal(
    await selector.getImpl(),
    guest,
    "Signing in to FxA does not switch a forced guest impl"
  );

  Services.prefs.setStringPref(PREF_GUEST_TOKEN, "guest-token");
  Services.prefs.setStringPref(PREF_AUTH_IMPL, "");
  const fxa = await selector.getImpl();
  Assert.ok(
    fxa instanceof FxaAuthProviderImpl,
    "Signing in to FxA switches from guest to FxA"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue(PREF_GUEST_TOKEN),
    "Switching to FxA clears the guest token"
  );

  setSignedIn(false);
  Assert.equal(
    await selector.getImpl(),
    fxa,
    "The FxA impl is kept after signing out"
  );
});

add_task(async function test_fxa_logout() {
  const { selector } = await select({ impl: "fxa", fxa: false });
  let loggedOut = false;
  selector.addEventListener(
    "AuthImplSelector:Logout",
    () => (loggedOut = true),
    { once: true }
  );
  Services.obs.notifyObservers(null, ONLOGOUT_NOTIFICATION);
  Assert.ok(loggedOut, "The selector fires AuthImplSelector:Logout");
  Assert.ok(
    (await selector.getImpl()) instanceof FxaAuthProviderImpl,
    "The selector keeps the FxA impl"
  );
  Assert.equal(
    Services.prefs.getStringPref(PREF_AUTH_IMPL),
    "fxa",
    "The FxA choice is kept"
  );
});

add_task(async function test_fallback() {
  const { selector, impl } = await select({ impl: "guest", fxa: false });
  impl.dispatchEvent(new Event("AuthProviderImpl:FallbackNeeded"));
  Assert.equal(
    Services.prefs.getStringPref(PREF_AUTH_IMPL),
    "fxa",
    "The fallback switches the pref to FxA"
  );
  Assert.ok(
    (await selector.getImpl()) instanceof FxaAuthProviderImpl,
    "The selector switches to FxA"
  );
});

/**
 * Enrolls in an experiment on the smartWindow feature.
 *
 * @param {object} value The feature value
 * @param {string} slug
 * @returns {Promise<Function>} Unenrolls from the experiment
 */
function enrollSmartWindow(value, slug = "auth-experiment") {
  return NimbusTestUtils.enrollWithFeatureConfig(
    { featureId: "smartWindow", value },
    { slug }
  );
}

add_task(async function test_nimbus_authImpl_applied_on_enrollment() {
  const { selector } = await select({ impl: "fxa", fxa: false });
  Services.prefs.clearUserPref(PREF_AUTH_IMPL);

  const unenroll = await enrollSmartWindow({ authImpl: "" });
  Assert.equal(
    Services.prefs.getStringPref(PREF_AUTH_IMPL),
    "",
    "The enrollment value is written to the pref"
  );
  Assert.ok(
    (await selector.getImpl()) instanceof GuestAuthProviderImpl,
    "The selector switches to guest"
  );

  setSignedIn(true);
  Assert.ok(
    (await selector.getImpl()) instanceof FxaAuthProviderImpl,
    "Signing in to FxA during the enrollment switches to FxA"
  );
  Assert.equal(
    Services.prefs.getStringPref(PREF_AUTH_IMPL),
    "fxa",
    "The FxA choice is persisted during the enrollment"
  );

  const restarted = new AuthImplSelector();
  Assert.ok(
    (await restarted.getImpl()) instanceof FxaAuthProviderImpl,
    "A new selector keeps FxA for the same enrollment while signed in"
  );

  await unenroll();
});

add_task(async function test_nimbus_authImpl_kept_after_unenrollment() {
  const { selector } = await select({ impl: "fxa", fxa: false });
  Services.prefs.clearUserPref(PREF_AUTH_IMPL);

  const unenroll = await enrollSmartWindow({ authImpl: "" });
  await unenroll();
  Assert.equal(
    Services.prefs.getStringPref(PREF_AUTH_IMPL),
    "",
    "Unenrolling leaves the pref alone"
  );

  const guest = await selector.getImpl();
  Assert.ok(guest instanceof GuestAuthProviderImpl, "The guest impl is kept");
  guest.dispatchEvent(new Event("AuthProviderImpl:FallbackNeeded"));
  Assert.ok(
    !Services.prefs.prefHasUserValue(PREF_AUTH_IMPL),
    "A rejected guest token switches the pref back to its default"
  );
  Assert.ok(
    (await selector.getImpl()) instanceof FxaAuthProviderImpl,
    "The selector switches to FxA"
  );
});

add_task(async function test_nimbus_authImpl_overrides_user_value() {
  const { selector } = await select({ impl: "", fxa: false });

  const unenroll = await enrollSmartWindow({ authImpl: "fxa" });
  Assert.equal(
    Services.prefs.getStringPref(PREF_AUTH_IMPL),
    "fxa",
    "The enrollment value is written over a user value"
  );
  Assert.ok(
    (await selector.getImpl()) instanceof FxaAuthProviderImpl,
    "The selector switches to FxA"
  );

  await unenroll();
});

add_task(async function test_nimbus_guestMode() {
  const unenroll = await enrollSmartWindow({ guestMode: "tab" });
  Assert.equal(
    Services.prefs.getStringPref(PREF_GUEST_MODE),
    "tab",
    "The enrollment sets the guest mode pref"
  );

  await unenroll();
  Assert.equal(
    Services.prefs.getStringPref(PREF_GUEST_MODE),
    "fetch",
    "Unenrolling restores the guest mode pref"
  );
});
