/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
const { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);
const { IPPGpiAuthProviderSingleton } = ChromeUtils.importESModule(
  "resource://testing-common/ipprotection/IPPGpiAuthProvider.sys.mjs"
);

const AUTH_JWT_PREF = "browser.ipProtection.gpi.authJwt";
const AUTH_JWT_EXPIRES_AT_PREF = "browser.ipProtection.gpi.authJwtExpiresAt";
const AUTH_JWT_RENEW_AFTER_PREF = "browser.ipProtection.gpi.authJwtRenewAfter";
const GUARDIAN_ENDPOINT_PREF = "browser.ipProtection.guardian.endpoint";

do_get_profile();

add_setup(function () {
  Services.fog.initializeFOG();
});

function makeGuardianServer(
  arg = {
    enrollment: (_request, _response) => {},
    token: (_request, _response) => {},
  }
) {
  const callbacks = {
    enrollment: (_request, _response) => {},
    token: (_request, _response) => {},
    ...arg,
  };
  const server = new HttpServer();
  server.registerPathHandler("/api/v1/gpn/enrollment", callbacks.enrollment);
  server.registerPathHandler("/api/v1/gpn/token", callbacks.token);
  server.start(-1);
  return {
    server,
    [Symbol.dispose]: () => server.stop(() => {}),
  };
}

/**
 * Creates a provider instance with all Android-layer methods stubbed out.
 *
 * @param {object} sandbox - sinon sandbox
 * @param {string|null} [gpiToken="fake-gpi-token"] - value _fetchGpiToken resolves to
 */
function makeProvider(sandbox, gpiToken = "fake-gpi-token") {
  const provider = new IPPGpiAuthProviderSingleton();
  sandbox.stub(provider, "_registerGpiListener");
  sandbox.stub(provider, "_unregisterGpiListener");
  sandbox.stub(provider, "_dispatchGpiWarmUp");
  sandbox.stub(provider, "_onGpiWarmUpCompleted");
  sandbox.stub(provider, "_onGpiWarmUpFailed");
  sandbox.stub(provider, "_fetchGpiToken").resolves(gpiToken);
  return provider;
}

function setupServer(serverWrapper) {
  const origin = `http://localhost:${serverWrapper.server.identity.primaryPort}`;
  Services.prefs.setCharPref(GUARDIAN_ENDPOINT_PREF, origin);
  return {
    [Symbol.dispose]() {
      Services.prefs.clearUserPref(GUARDIAN_ENDPOINT_PREF);
      Services.prefs.clearUserPref(AUTH_JWT_PREF);
      Services.prefs.clearUserPref(AUTH_JWT_EXPIRES_AT_PREF);
      Services.prefs.clearUserPref(AUTH_JWT_RENEW_AFTER_PREF);
    },
  };
}

const FUTURE_MS = Date.now() + 60_000;
const PAST_MS = Date.now() - 1_000;

const QUOTA_HEADERS = {
  "X-Quota-Limit": "5368709120",
  "X-Quota-Remaining": "4294967296",
  "X-Quota-Reset": "2026-02-01T00:00:00.000Z",
};

function enrollmentOk({ deviceSessionJwt, expiresAt, renewAfter } = {}) {
  return (request, response) => {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.write(JSON.stringify({ deviceSessionJwt, expiresAt, renewAfter }));
  };
}

function enrollmentFail(status = 500) {
  return (request, response) => {
    response.setStatusLine(request.httpVersion, status, "");
  };
}

function tokenOk(headers = QUOTA_HEADERS) {
  return (request, response) => {
    response.setStatusLine(request.httpVersion, 200, "OK");
    for (const [name, value] of Object.entries(headers)) {
      response.setHeader(name, value, false);
    }
    response.write(JSON.stringify({ token: createProxyPassToken() }));
  };
}

function tokenFail(status) {
  return (request, response) => {
    response.setStatusLine(request.httpVersion, status, "");
  };
}

// --- isReady ---
// IPPGpiAuthProvider is ready only when warmed up or when it has an AuthJWT in
// prefs.

add_task(async function test_isReady_no_jwt_gpi_not_warmed_up() {
  const sandbox = sinon.createSandbox();
  const provider = makeProvider(sandbox);
  Assert.ok(!provider.isReady, "Not ready without JWT and before GPI warm-up");
  sandbox.restore();
});

add_task(async function test_isReady_becomes_true_after_warmup_event() {
  const sandbox = sinon.createSandbox();
  const provider = makeProvider(sandbox);
  provider._onGpiWarmUpCompleted.restore();
  provider.init();
  Assert.ok(
    !provider.isReady,
    "Not ready before GeckoView:IPProtection:GPI:WarmUpCompleted"
  );
  provider._onGpiWarmUpCompleted();
  Assert.ok(
    provider.isReady,
    "Ready after GeckoView:IPProtection:GPI:WarmUpCompleted"
  );
  provider.uninit();
  sandbox.restore();
});

add_task(async function test_isReady_true_with_valid_jwt() {
  const sandbox = sinon.createSandbox();
  const provider = makeProvider(sandbox);
  Services.prefs.setCharPref(AUTH_JWT_PREF, "valid-jwt");
  Services.prefs.setCharPref(AUTH_JWT_EXPIRES_AT_PREF, String(FUTURE_MS));
  Assert.ok(provider.isReady, "Ready when a non-expired JWT is stored");
  Services.prefs.clearUserPref(AUTH_JWT_PREF);
  Services.prefs.clearUserPref(AUTH_JWT_EXPIRES_AT_PREF);
  sandbox.restore();
});

add_task(async function test_isReady_false_and_clears_expired_jwt() {
  const sandbox = sinon.createSandbox();
  const provider = makeProvider(sandbox);
  Services.prefs.setCharPref(AUTH_JWT_PREF, "expired-jwt");
  Services.prefs.setCharPref(AUTH_JWT_EXPIRES_AT_PREF, String(PAST_MS));
  Assert.ok(!provider.isReady, "Not ready with an expired JWT");
  Assert.equal(
    Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
    "",
    "Expired JWT cleared from prefs"
  );
  Services.prefs.clearUserPref(AUTH_JWT_EXPIRES_AT_PREF);
  sandbox.restore();
});

add_task(async function test_isReady_false_after_uninit() {
  const sandbox = sinon.createSandbox();
  const provider = makeProvider(sandbox);
  provider._onGpiWarmUpCompleted.restore();
  provider.init();
  provider._onGpiWarmUpCompleted();
  Assert.ok(provider.isReady, "Ready after warm-up");
  provider.uninit();
  Assert.ok(!provider.isReady, "Not ready after uninit (GPI flag cleared)");
  sandbox.restore();
});

// --- aboutToStart ---

// Scenario: we are about to activate the VPN, but we do not have an AuthJWT
// yet. A request for a GPI Token is sent, a new AuthJWT is requested and
// stored in prefs.
add_task(async function test_aboutToStart_requests_gpi_token_then_auth_jwt() {
  const sandbox = sinon.createSandbox();
  let enrollmentBody = null;
  const expiresAt = FUTURE_MS + 3600_000;
  const renewAfter = FUTURE_MS + 1800_000;
  using serverWrapper = makeGuardianServer({
    enrollment: (request, response) => {
      const body = NetUtil.readInputStreamToString(
        request.bodyInputStream,
        request.bodyInputStream.available()
      );
      enrollmentBody = JSON.parse(body);
      enrollmentOk({ deviceSessionJwt: "new-jwt", expiresAt, renewAfter })(
        request,
        response
      );
    },
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox, "my-gpi-token");
  const result = await provider.aboutToStart();
  Assert.equal(result, null, "No error after enrollment");
  Assert.ok(provider._fetchGpiToken.calledOnce, "GPI token was requested");
  Assert.equal(
    enrollmentBody?.integrityToken,
    "my-gpi-token",
    "GPI token sent to enrollment endpoint"
  );
  Assert.equal(
    Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
    "new-jwt",
    "JWT stored in prefs"
  );
  Assert.equal(
    Services.prefs.getCharPref(AUTH_JWT_EXPIRES_AT_PREF, ""),
    String(expiresAt),
    "expiresAt stored in prefs"
  );
  Assert.equal(
    Services.prefs.getCharPref(AUTH_JWT_RENEW_AFTER_PREF, ""),
    String(renewAfter),
    "renewAfter stored in prefs"
  );
  sandbox.restore();
});

// Scenario: we do have a valid AuthJWT in prefs. The aboutToStart becomes a
// no-op.
add_task(async function test_aboutToStart_skips_enroll_with_valid_jwt() {
  const sandbox = sinon.createSandbox();
  using serverWrapper = makeGuardianServer();
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  Services.prefs.setCharPref(AUTH_JWT_PREF, "valid-jwt");
  Services.prefs.setCharPref(AUTH_JWT_EXPIRES_AT_PREF, String(FUTURE_MS));
  const result = await provider.aboutToStart();
  Assert.equal(result, null, "No error when valid JWT exists");
  Assert.ok(
    provider._fetchGpiToken.notCalled,
    "GPI token not requested when JWT is valid"
  );
  sandbox.restore();
});

// Scenario: enrollment fails during activation. The prefs are reset and an
// error is returned.
add_task(async function test_aboutToStart_enrollment_failed_clears_prefs() {
  const sandbox = sinon.createSandbox();
  using serverWrapper = makeGuardianServer({
    enrollment: enrollmentFail(),
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  // Pre-populate with a stale JWT that triggered renewal.
  Services.prefs.setCharPref(AUTH_JWT_PREF, "stale-jwt");
  Services.prefs.setCharPref(AUTH_JWT_RENEW_AFTER_PREF, String(PAST_MS));
  const result = await provider.aboutToStart();
  Assert.deepEqual(result, { error: "enrollment_failed" });
  Assert.equal(
    Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
    "",
    "JWT cleared from prefs after enrollment failure"
  );
  sandbox.restore();
});

// Scenario: after a failed enrollment the JWT pref is cleared, so the next
// aboutToStart() call re-enrolls successfully.
add_task(
  async function test_aboutToStart_retries_enroll_after_previous_failure() {
    const sandbox = sinon.createSandbox();
    // First call: enrollment server down.
    using serverWrapper = makeGuardianServer({
      enrollment: enrollmentFail(),
    });
    // eslint-disable-next-line no-unused-vars
    using _setup = setupServer(serverWrapper);
    const provider = makeProvider(sandbox);
    await provider.aboutToStart();
    Assert.equal(
      Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
      "",
      "No JWT after first failure"
    );

    // Second call: server recovers.
    serverWrapper.server.registerPathHandler(
      "/api/v1/gpn/enrollment",
      enrollmentOk({ deviceSessionJwt: "recovered-jwt" })
    );
    const result = await provider.aboutToStart();
    Assert.equal(result, null, "No error on retry");
    Assert.equal(
      Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
      "recovered-jwt",
      "JWT stored after successful retry"
    );
    sandbox.restore();
  }
);

// Scenario: init() schedules a renewal timer when renewAfter is in the past;
// the timer fires and fetches a new JWT.

add_task(async function test_renewal_timer_fires_and_refreshes_jwt() {
  const sandbox = sinon.createSandbox();
  const newJwt = "renewed-jwt";
  using serverWrapper = makeGuardianServer({
    enrollment: enrollmentOk({ deviceSessionJwt: newJwt }),
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);

  // Set renewAfter in the past so the renewal timer fires immediately at init().
  Services.prefs.setCharPref(AUTH_JWT_PREF, "old-jwt");
  Services.prefs.setCharPref(AUTH_JWT_RENEW_AFTER_PREF, String(PAST_MS));
  provider.init(); // schedules the renewal timer with delay 0

  await TestUtils.waitForCondition(
    () => Services.prefs.getCharPref(AUTH_JWT_PREF, "") === newJwt,
    "Waiting for renewal to update the JWT pref"
  );

  Assert.equal(
    Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
    newJwt,
    "JWT updated by renewal timer"
  );
  provider.uninit();
  sandbox.restore();
});

// Scenario: if enrollment is in flight, fetchProxyPass waits for it to
// complete before using the resulting JWT.
add_task(async function test_fetchProxyPass_waits_for_in_flight_enrollment() {
  const sandbox = sinon.createSandbox();

  // The enrollment request will stall until we release it.
  let releaseEnrollment;
  const enrollmentStalled = new Promise(
    resolve => (releaseEnrollment = resolve)
  );

  using serverWrapper = makeGuardianServer({
    enrollment: (request, response) => {
      response.processAsync();
      enrollmentStalled.then(() => {
        response.setStatusLine(request.httpVersion, 200, "OK");
        response.write(JSON.stringify({ deviceSessionJwt: "enrolled-jwt" }));
        response.finish();
      });
    },
    token: tokenOk(),
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);

  // Start enrollment in the background (simulates renewal timer firing).
  const enrollPromise = provider.aboutToStart();

  // fetchProxyPass is called while enrollment is still in flight.
  const passPromise = provider.fetchProxyPass();

  // Release the stalled enrollment server response.
  releaseEnrollment();

  const [enrollResult, { pass, error }] = await Promise.all([
    enrollPromise,
    passPromise,
  ]);

  Assert.equal(enrollResult, null, "Enrollment succeeded");
  Assert.ok(!error, "fetchProxyPass has no error");
  Assert.ok(pass?.isValid(), "fetchProxyPass returned a valid ProxyPass");
  Assert.equal(
    Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
    "enrolled-jwt",
    "JWT set before fetchProxyPass used it"
  );
  sandbox.restore();
});

// Scenario: renewAfter has passed; aboutToStart() re-enrolls proactively.
add_task(async function test_aboutToStart_reenrolls_when_renewAfter_passed() {
  const sandbox = sinon.createSandbox();
  const newJwt = "renewed-jwt";
  using serverWrapper = makeGuardianServer({
    enrollment: enrollmentOk({ deviceSessionJwt: newJwt }),
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  // JWT is still valid (not expired) but renewAfter has passed.
  Services.prefs.setCharPref(AUTH_JWT_PREF, "old-jwt");
  Services.prefs.setCharPref(AUTH_JWT_EXPIRES_AT_PREF, String(FUTURE_MS));
  Services.prefs.setCharPref(AUTH_JWT_RENEW_AFTER_PREF, String(PAST_MS));
  const result = await provider.aboutToStart();
  Assert.equal(result, null, "No error after renewal");
  Assert.equal(
    Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
    newJwt,
    "JWT updated after renewal"
  );
  sandbox.restore();
});
// --- fetchProxyPass ---

// Scenario: no AuthJWT in prefs; fetchProxyPass returns login_needed.
add_task(async function test_fetchProxyPass_no_jwt_returns_login_needed() {
  const sandbox = sinon.createSandbox();
  using serverWrapper = makeGuardianServer();
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  const { error, usage } = await provider.fetchProxyPass();
  Assert.equal(error, "login_needed");
  Assert.equal(usage, null);
  sandbox.restore();
});

// Scenario: valid AuthJWT in prefs; fetchProxyPass succeeds and returns a ProxyPass.
add_task(async function test_fetchProxyPass_success() {
  const sandbox = sinon.createSandbox();
  using serverWrapper = makeGuardianServer({ token: tokenOk() });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  Services.prefs.setCharPref(AUTH_JWT_PREF, "valid-jwt");
  const { pass, usage, error } = await provider.fetchProxyPass();
  Assert.ok(!error, "No error");
  Assert.ok(pass?.isValid(), "Valid ProxyPass returned");
  Assert.equal(usage?.max, BigInt("5368709120"), "Usage returned");
  sandbox.restore();
});

// Scenario: Guardian returns 401 on a token request; the provider re-enrolls
// and retries successfully.
add_task(async function test_fetchProxyPass_401_reenrolls_and_retries() {
  const sandbox = sinon.createSandbox();
  let tokenCallCount = 0;
  using serverWrapper = makeGuardianServer({
    enrollment: enrollmentOk({ deviceSessionJwt: "refreshed-jwt" }),
    token: (request, response) => {
      tokenCallCount++;
      if (tokenCallCount === 1) {
        tokenFail(401)(request, response);
      } else {
        tokenOk()(request, response);
      }
    },
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  Services.prefs.setCharPref(AUTH_JWT_PREF, "stale-jwt");
  const { pass, error } = await provider.fetchProxyPass();
  Assert.ok(!error, "No error after re-enrollment");
  Assert.ok(pass?.isValid(), "Valid ProxyPass after re-enrollment");
  Assert.equal(tokenCallCount, 2, "Token endpoint hit twice");
  Assert.equal(
    Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
    "refreshed-jwt",
    "JWT updated after re-enrollment"
  );
  sandbox.restore();
});

// Scenario: Guardian returns 401 and re-enrollment fails; the JWT is cleared
// and an error is returned.
add_task(async function test_fetchProxyPass_401_reenroll_fails_clears_jwt() {
  const sandbox = sinon.createSandbox();
  using serverWrapper = makeGuardianServer({
    enrollment: enrollmentFail(),
    token: tokenFail(401),
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  Services.prefs.setCharPref(AUTH_JWT_PREF, "stale-jwt");
  const { error, status } = await provider.fetchProxyPass();
  Assert.equal(error, "unauthorized");
  Assert.equal(status, 401);
  Assert.equal(
    Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
    "",
    "JWT cleared after failed re-enrollment so aboutToStart() retries next time"
  );
  sandbox.restore();
});

// Scenario: during the token request, the server returns a 429 quota limit.
add_task(async function test_fetchProxyPass_429_quota_exceeded() {
  const sandbox = sinon.createSandbox();
  using serverWrapper = makeGuardianServer({
    token: (request, response) => {
      response.setStatusLine(request.httpVersion, 429, "Too Many Requests");
      response.setHeader("X-Quota-Limit", "5368709120", false);
      response.setHeader("X-Quota-Remaining", "0", false);
      response.setHeader("X-Quota-Reset", "2026-02-01T00:00:00.000Z", false);
      response.setHeader("Retry-After", "3600", false);
    },
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  Services.prefs.setCharPref(AUTH_JWT_PREF, "valid-jwt");
  const { error, status, retryAfter, usage } = await provider.fetchProxyPass();
  Assert.equal(error, "quota_exceeded");
  Assert.equal(status, 429);
  Assert.equal(retryAfter, "3600");
  Assert.equal(usage?.remaining, BigInt(0));
  sandbox.restore();
});

// --- fetchProxyUsage ---

// The scenarios below mirror the fetchProxyPass ones above.

add_task(async function test_fetchProxyUsage_no_jwt_returns_null() {
  const sandbox = sinon.createSandbox();
  using serverWrapper = makeGuardianServer();
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  Assert.equal(await provider.fetchProxyUsage(), null);
  sandbox.restore();
});

add_task(async function test_fetchProxyUsage_success() {
  const sandbox = sinon.createSandbox();
  using serverWrapper = makeGuardianServer({
    token: (request, response) => {
      response.setStatusLine(request.httpVersion, 200, "OK");
      for (const [name, value] of Object.entries(QUOTA_HEADERS)) {
        response.setHeader(name, value, false);
      }
    },
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  Services.prefs.setCharPref(AUTH_JWT_PREF, "valid-jwt");
  const usage = await provider.fetchProxyUsage();
  Assert.equal(usage?.max, BigInt("5368709120"));
  Assert.equal(usage?.remaining, BigInt("4294967296"));
  sandbox.restore();
});

add_task(async function test_fetchProxyUsage_401_reenrolls_and_retries() {
  const sandbox = sinon.createSandbox();
  let headCallCount = 0;
  using serverWrapper = makeGuardianServer({
    enrollment: enrollmentOk({ deviceSessionJwt: "refreshed-jwt" }),
    token: (request, response) => {
      headCallCount++;
      if (headCallCount === 1) {
        tokenFail(401)(request, response);
      } else {
        response.setStatusLine(request.httpVersion, 200, "OK");
        for (const [name, value] of Object.entries(QUOTA_HEADERS)) {
          response.setHeader(name, value, false);
        }
      }
    },
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  Services.prefs.setCharPref(AUTH_JWT_PREF, "stale-jwt");
  const usage = await provider.fetchProxyUsage();
  Assert.ok(usage, "Usage returned after re-enrollment");
  Assert.equal(headCallCount, 2, "Token endpoint hit twice");
  sandbox.restore();
});

add_task(async function test_fetchProxyUsage_401_reenroll_fails_clears_jwt() {
  const sandbox = sinon.createSandbox();
  using serverWrapper = makeGuardianServer({
    enrollment: enrollmentFail(),
    token: tokenFail(401),
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  Services.prefs.setCharPref(AUTH_JWT_PREF, "stale-jwt");
  const usage = await provider.fetchProxyUsage();
  Assert.equal(usage, null, "null after failed re-enrollment");
  Assert.equal(
    Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
    "",
    "JWT cleared after failed re-enrollment so aboutToStart() retries next time"
  );
  sandbox.restore();
});

add_task(async function test_fetchProxyUsage_waits_for_in_flight_enrollment() {
  const sandbox = sinon.createSandbox();

  let releaseEnrollment;
  const enrollmentStalled = new Promise(
    resolve => (releaseEnrollment = resolve)
  );

  using serverWrapper = makeGuardianServer({
    enrollment: (request, response) => {
      response.processAsync();
      enrollmentStalled.then(() => {
        response.setStatusLine(request.httpVersion, 200, "OK");
        response.write(JSON.stringify({ deviceSessionJwt: "enrolled-jwt" }));
        response.finish();
      });
    },
    token: (request, response) => {
      response.setStatusLine(request.httpVersion, 200, "OK");
      for (const [name, value] of Object.entries(QUOTA_HEADERS)) {
        response.setHeader(name, value, false);
      }
    },
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);

  const enrollPromise = provider.aboutToStart();
  const usagePromise = provider.fetchProxyUsage();

  releaseEnrollment();

  const [enrollResult, usage] = await Promise.all([
    enrollPromise,
    usagePromise,
  ]);

  Assert.equal(enrollResult, null, "Enrollment succeeded");
  Assert.ok(usage, "fetchProxyUsage returned usage after waiting");
  Assert.equal(
    Services.prefs.getCharPref(AUTH_JWT_PREF, ""),
    "enrolled-jwt",
    "JWT set before fetchProxyUsage used it"
  );
  sandbox.restore();
});

// --- Authorization header ---

add_task(async function test_fetchProxyPass_sends_auth_header() {
  const sandbox = sinon.createSandbox();
  let authHeader = null;
  using serverWrapper = makeGuardianServer({
    token: (request, response) => {
      authHeader = request.getHeader("Authorization");
      tokenOk()(request, response);
    },
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  Services.prefs.setCharPref(AUTH_JWT_PREF, "my-jwt");
  await provider.fetchProxyPass();
  Assert.equal(
    authHeader,
    "Bearer my-jwt",
    "Correct Authorization header sent"
  );
  sandbox.restore();
});

// --- Telemetry ---

add_task(async function test_gpi_enrollment_records_failure_reason() {
  Services.fog.testResetFOG();
  const sandbox = sinon.createSandbox();
  using serverWrapper = makeGuardianServer({
    enrollment: (request, response) => {
      response.setStatusLine(request.httpVersion, 400, "Bad Request");
      response.write(JSON.stringify({ reason: "missing-package-name" }));
    },
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  await provider.aboutToStart();

  const events = Glean.ipprotection.gpiEnrollment.testGetValue();
  Assert.equal(events?.length, 1, "Exactly one enrollment event recorded");
  const { extra } = events[0];
  Assert.equal(
    extra.reason,
    "missing_package_name",
    "Server reason mapped to coarse category"
  );
  Assert.equal(extra.httpStatus, "400", "HTTP status recorded");
  Assert.equal(extra.hadPreviousJwt, "false", "hadPreviousJwt=false recorded");
  sandbox.restore();
});

add_task(async function test_fetchProxyUsage_sends_auth_header() {
  const sandbox = sinon.createSandbox();
  let authHeader = null;
  using serverWrapper = makeGuardianServer({
    token: (request, response) => {
      authHeader = request.getHeader("Authorization");
      response.setStatusLine(request.httpVersion, 200, "OK");
      for (const [name, value] of Object.entries(QUOTA_HEADERS)) {
        response.setHeader(name, value, false);
      }
    },
  });
  // eslint-disable-next-line no-unused-vars
  using _setup = setupServer(serverWrapper);
  const provider = makeProvider(sandbox);
  Services.prefs.setCharPref(AUTH_JWT_PREF, "my-jwt");
  await provider.fetchProxyUsage();
  Assert.equal(
    authHeader,
    "Bearer my-jwt",
    "Correct Authorization header sent"
  );
  sandbox.restore();
});
