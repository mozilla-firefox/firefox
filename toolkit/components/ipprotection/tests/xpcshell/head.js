/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

"use strict";

const { IPProtectionService, IPProtectionStates } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs"
);
const { ERRORS, IPPProxyManager, IPPProxyStates } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs"
);
const { AUTH_ERRORS } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPAuthProvider.sys.mjs"
);
const { IPPExceptionsManager, IPPPrincipalRules } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPExceptionsManager.sys.mjs"
);
const { ProxyPass, ProxyUsage, Entitlement } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/GuardianTypes.sys.mjs"
);
const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);
const { IPProtectionActivator } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPProtectionActivator.sys.mjs"
);
const { IPPDummyAuthProvider } = ChromeUtils.importESModule(
  "resource://testing-common/ipprotection/IPPDummyAuthProvider.sys.mjs"
);
IPProtectionActivator.setAuthProvider(IPPDummyAuthProvider);
IPProtectionActivator.setupHelpers();

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

function waitForEvent(target, eventName, callback = () => true) {
  return new Promise(resolve => {
    let listener = event => {
      if (callback()) {
        target.removeEventListener(eventName, listener);
        resolve(event);
      }
    };
    target.addEventListener(eventName, listener);
  });
}

/**
 * Initializes IPProtectionService and resolves once it reaches READY.
 */
async function initServiceToReady() {
  const readyEvent = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );
  IPProtectionService.init();
  await readyEvent;
}
/* exported initServiceToReady */

/**
 * Resolves once IPPProxyManager reaches the given state.
 *
 * @param {string} state - One of IPPProxyStates.
 */
function waitForProxyState(state) {
  return waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === state
  );
}
/* exported waitForProxyState */

async function putServerInRemoteSettings(
  server = {
    hostname: "test1.example.com",
    port: 443,
    quarantined: false,
  }
) {
  const TEST_US_CITY = {
    name: "Test City",
    code: "TC",
    servers: [server],
  };
  const US = {
    name: "United States",
    code: "US",
    cities: [TEST_US_CITY],
  };
  do_get_profile();
  const client = RemoteSettings("vpn-serverlist");
  await client.db.clear();
  await client.db.create(US);
  await client.db.importChanges({}, Date.now());
}
/* exported putServerInRemoteSettings */

/* exported setupStubs */

const defaultStubOptions = {
  signedIn: true,
  validProxyPass: true,
  entitlement: createTestEntitlement(),
  proxyUsage: new ProxyUsage(
    "5368709120",
    "4294967296",
    "3026-02-01T00:00:00.000Z"
  ),
};
Object.freeze(defaultStubOptions);

/**
 * @param {object} [aOptions] - Overrides for defaultStubOptions.
 */
function setupStubs(aOptions = {}) {
  const options = { ...defaultStubOptions, ...aOptions };
  IPPDummyAuthProvider.simulateSignIn(options.signedIn);
  IPPDummyAuthProvider.setEntitlement(options.entitlement, { silent: true });
  IPPDummyAuthProvider.setGetEntitlementResponse({
    entitlement: options.entitlement,
  });
  IPPDummyAuthProvider.setEnrollResponse({
    isEnrolledAndEntitled: true,
    entitlement: options.entitlement,
  });
  IPPDummyAuthProvider.setProxyPass({
    status: 200,
    error: undefined,
    pass: new ProxyPass(
      options.validProxyPass
        ? createProxyPassToken()
        : createExpiredProxyPassToken()
    ),
    usage: options.proxyUsage,
  });
  IPPDummyAuthProvider.setProxyUsage(options.proxyUsage);
  IPPDummyAuthProvider.setProxyPassError(null);
  IPPDummyAuthProvider.setProxyPassHang(false);
  IPPDummyAuthProvider.setProxyPassResolveOnAbort(null);
}

/**
 * Creates a Token that can be fed as a Network Response from Guardian
 * to simulate a Proxy Pass.
 *
 * @param {Temporal.Instant} from
 * @param {Temporal.Instant} until
 * @returns {string} JWT Token
 */
function createProxyPassToken(
  from = Temporal.Now.instant(),
  until = from.add({ hours: 24 })
) {
  const header = {
    alg: "HS256",
    typ: "JWT",
  };
  const body = {
    iat: Math.floor(from.add({ seconds: 1 }).epochMilliseconds / 1000),
    nbf: Math.floor(from.epochMilliseconds / 1000),
    exp: Math.floor(until.epochMilliseconds / 1000),
    sub: "proxy-pass-user-42",
    aud: "guardian-proxy",
    iss: "vpn.mozilla.org",
  };
  const encode = obj => btoa(JSON.stringify(obj));
  return [encode(header), encode(body), "signature"].join(".");
}
/* exported createProxyPassToken */
function createExpiredProxyPassToken() {
  return createProxyPassToken(
    Temporal.Now.instant().subtract({ hours: 2 }),
    Temporal.Now.instant().subtract({ hours: 1 })
  );
}
/* exported createExpiredProxyPassToken */

/**
 * Creates a test Entitlement with default values.
 *
 * @param {object} overrides - Optional fields to override
 * @returns {Entitlement}
 */
function createTestEntitlement(overrides = {}) {
  return new Entitlement({
    autostart: false,
    created_at: "2023-01-01T12:00:00.000Z",
    limited_bandwidth: false,
    location_controls: false,
    subscribed: false,
    uid: 42,
    website_inclusion: false,
    maxBytes: "0",
    ...overrides,
  });
}
/* exported createTestEntitlement */
