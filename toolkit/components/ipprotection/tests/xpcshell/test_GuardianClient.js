/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { HttpServer, HTTP_404 } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
const { GuardianClient } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/fxa/GuardianClient.sys.mjs"
);
const { IPPFxaActivateAuthProvider } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/fxa/IPPFxaActivateAuthProvider.sys.mjs"
);
const { JsonSchemaValidator } = ChromeUtils.importESModule(
  "resource://gre/modules/components-utils/JsonSchemaValidator.sys.mjs"
);

do_get_profile();

function makeGuardianServer(
  arg = {
    enroll: (_request, _response) => {},
    token: (_request, _response) => {},
    status: (_request, _response) => {},
  }
) {
  const callbacks = {
    enroll: (_request, _response) => {},
    token: (_request, _response) => {},
    status: (_request, _response) => {},
    ...arg,
  };
  const server = new HttpServer();

  server.registerPathHandler("/api/v1/fpn/token", callbacks.token);
  server.registerPathHandler("/api/v1/fpn/status", callbacks.status);
  server.registerPathHandler("/api/v1/fpn/auth", callbacks.enroll);
  server.start(-1);

  return {
    server,
    [Symbol.dispose]: () => {
      server.stop(() => {});
    },
  };
}

function makeStallHandler() {
  const stalledResponses = [];
  return {
    handler: (_request, response) => {
      response.processAsync();
      stalledResponses.push(response);
    },
    [Symbol.dispose]: () => {
      stalledResponses.forEach(r => {
        try {
          r.finish();
        } catch (e) {}
      });
      stalledResponses.length = 0;
    },
  };
}

const TEST_TOKEN_HANDLE = {
  token: "test-token",
  [Symbol.dispose]: () => {},
};

function setupGuardianClient(serverWrapper) {
  const serverOrigin = `http://localhost:${serverWrapper.server.identity.primaryPort}`;
  Services.prefs.setCharPref(
    "browser.ipProtection.guardian.endpoint",
    serverOrigin
  );
  Services.prefs.setCharPref("identity.fxaccounts.remote.root", serverOrigin);

  return {
    [Symbol.dispose]() {
      Services.prefs.clearUserPref("browser.ipProtection.guardian.endpoint");
      Services.prefs.clearUserPref("identity.fxaccounts.remote.root");
    },
  };
}

add_task(async function test_fetchUserInfo() {
  const ok = data => {
    return (request, r) => {
      // Verify the Authorization header is present and correctly formatted
      const authHeader = request.getHeader("Authorization");
      Assert.ok(authHeader, "Authorization header should be present");
      Assert.equal(
        authHeader,
        "Bearer test-token",
        "Authorization header should have the correct format"
      );

      r.setStatusLine(request.httpVersion, 200, "OK");
      r.write(JSON.stringify(data));
    };
  };
  const fail = status => () => {
    throw status;
  };
  const DEFAULT_OK_RESPONSE = {
    subscribed: true,
    uid: 42,
    maxBytes: "1073741824",
    limited_bandwidth: true,
  };
  const DEFAULT_EXPECTED_VALUES = {
    subscribed: true,
    uid: 42,
    maxBytes: BigInt(1073741824),
    limitedBandwidth: true,
  };

  const testcases = [
    {
      name: "It should parse a valid response",
      sends: ok({
        ...DEFAULT_OK_RESPONSE,
      }),
      expects: {
        status: 200,
        error: null,
        validEntitlement: true,
        entitlement: {
          ...DEFAULT_EXPECTED_VALUES,
        },
      },
    },
    {
      name: "It should default limitedBandwidth to true when missing",
      sends: ok({
        subscribed: true,
        uid: 42,
        maxBytes: "1073741824",
      }),
      expects: {
        status: 200,
        error: null,
        validEntitlement: true,
        entitlement: {
          ...DEFAULT_EXPECTED_VALUES,
          limitedBandwidth: true,
        },
      },
    },
    {
      name: "It should handle a 404 response",
      sends: fail(HTTP_404),
      expects: {
        status: 404,
        error: "parse_error",
        validEntitlement: false,
      },
    },
    {
      name: "It should handle an empty response",
      sends: ok({}),
      expects: {
        status: 200,
        error: "parse_error",
        validEntitlement: false,
      },
    },
    {
      name: "It should handle a 200 response with incorrect types",
      sends: ok({
        subscribed: "true", // Incorrect type: should be boolean
        uid: "42", // Incorrect type: should be number
        maxBytes: 1234567890, // Incorrect type: should be string
      }),
      expects: {
        status: 200,
        error: "parse_error",
        validEntitlement: false, // Should fail validation due to incorrect types
      },
    },
  ];
  testcases
    .map(({ name, sends, expects }) => {
      return async () => {
        using serverWrapper = makeGuardianServer({ status: sends });
        // eslint-disable-next-line no-unused-vars
        using _setup = setupGuardianClient(serverWrapper);
        const client = new GuardianClient();

        const { status, entitlement, error } =
          await client.fetchUserInfo(TEST_TOKEN_HANDLE);

        if (expects.status !== undefined) {
          Assert.equal(status, expects.status, `${name}: status should match`);
        }

        // Check error message if it's expected
        if (expects.error !== null) {
          Assert.equal(
            error,
            expects.error,
            `${name}: error should match expected`
          );
        } else {
          Assert.equal(error, undefined, `${name}: error should be undefined`);
        }

        if (expects.validEntitlement) {
          Assert.notEqual(
            entitlement,
            null,
            `${name}: entitlement should not be null`
          );
          for (const key of Object.keys(expects.entitlement)) {
            Assert.equal(
              entitlement[key],
              expects.entitlement[key],
              `${name}: entitlement.${key} should match`
            );
          }
        } else {
          Assert.equal(
            entitlement,
            null,
            `${name}: entitlement should be null`
          );
        }
      };
    })
    .forEach(test => add_task(test));
});

add_task(async function test_fetchProxyPass() {
  const ok = (data, headers = {}) => {
    return (request, r) => {
      r.setStatusLine(request.httpVersion, 200, "OK");
      // Set default Cache-Control header (needed for ProxyPass)
      if (!headers["Cache-Control"]) {
        r.setHeader("Cache-Control", "max-age=3600", false);
      }
      // Set default quota headers
      if (!("X-Quota-Limit" in headers)) {
        r.setHeader("X-Quota-Limit", "5368709120", false);
      }
      if (!("X-Quota-Remaining" in headers)) {
        r.setHeader("X-Quota-Remaining", "4294967296", false);
      }
      if (!("X-Quota-Reset" in headers)) {
        r.setHeader("X-Quota-Reset", "2026-02-01T00:00:00.000Z", false);
      }
      // Set any custom headers (undefined values will skip setting)
      for (const [name, value] of Object.entries(headers)) {
        if (value !== undefined) {
          r.setHeader(name, value, false);
        }
      }
      r.write(JSON.stringify(data));
    };
  };
  const fail = status => () => {
    throw status;
  };
  const testcases = [
    {
      name: "It should parse a valid response with usage headers",
      sends: ok({ token: createProxyPassToken() }),
      expects: {
        status: 200,
        error: null,
        validPass: true,
        validUsage: true,
        usage: {
          max: BigInt("5368709120"),
          remaining: BigInt("4294967296"),
        },
      },
    },
    {
      name: "It should handle missing usage headers gracefully",
      sends: ok(
        { token: createProxyPassToken() },
        {
          "X-Quota-Limit": undefined,
          "X-Quota-Remaining": undefined,
          "X-Quota-Reset": undefined,
        }
      ),
      expects: {
        status: 200,
        error: null,
        validPass: true,
        validUsage: false,
      },
    },
    {
      name: "It should handle a 404 response",
      sends: fail(HTTP_404),
      expects: {
        status: 404,
        error: "unexpected_status",
        validPass: false,
        validUsage: false,
      },
    },
    {
      name: "It should handle an empty response",
      sends: ok({}),
      expects: {
        status: 200,
        error: "invalid_response",
        validPass: false,
        validUsage: true,
      },
    },
    {
      name: "It should handle an invalid token format",
      sends: ok({ token: "header.body.signature" }),
      expects: {
        status: 200,
        error: "invalid_response",
        validPass: false,
        validUsage: true,
      },
    },
  ];
  testcases
    .map(({ name, sends, expects }) => {
      return async () => {
        using serverWrapper = makeGuardianServer({ token: sends });
        // eslint-disable-next-line no-unused-vars
        using _setup = setupGuardianClient(serverWrapper);
        const client = new GuardianClient();

        const { status, pass, error, usage } =
          await client.fetchProxyPass(TEST_TOKEN_HANDLE);

        if (expects.status !== undefined) {
          Assert.equal(status, expects.status, `${name}: status should match`);
        }

        // Check error message if it's expected
        if (expects.error !== null) {
          Assert.equal(
            error,
            expects.error,
            `${name}: error should match expected`
          );
        } else {
          Assert.equal(error, undefined, `${name}: error should be undefined`);
        }

        if (expects.validPass) {
          Assert.notEqual(pass, null, `${name}: pass should not be null`);
          Assert.strictEqual(
            typeof pass.token,
            "string",
            `${name}: pass.token should be a string`
          );
          Assert.greater(
            pass.until.epochMilliseconds,
            Date.now(),
            `${name}: pass.until should be in the future`
          );
          Assert.ok(pass.isValid(), `${name}: pass should be valid`);
        } else {
          Assert.equal(pass, null, `${name}: pass should be null`);
        }

        if (expects.validUsage) {
          Assert.notEqual(usage, null, `${name}: usage should not be null`);
          if (expects.usage) {
            Assert.equal(
              usage.max,
              expects.usage.max,
              `${name}: usage.max should match`
            );
            Assert.equal(
              usage.remaining,
              expects.usage.remaining,
              `${name}: usage.remaining should match`
            );
          }
          Assert.ok(
            usage.reset && typeof usage.reset.epochMilliseconds === "number",
            `${name}: usage.reset should be Temporal.Instant`
          );
        } else if (expects.validUsage === false) {
          Assert.equal(usage, null, `${name}: usage should be null`);
        }
      };
    })
    .forEach(test => add_task(test));
});

add_task(async function test_ProxyUsage_fromResponse() {
  const testcases = [
    {
      name: "Valid quota headers",
      headers: {
        "X-Quota-Limit": "5368709120",
        "X-Quota-Remaining": "4294967296",
        "X-Quota-Reset": "2026-02-01T00:00:00.000Z",
      },
      expects: {
        validUsage: true,
        max: BigInt("5368709120"),
        remaining: BigInt("4294967296"),
        unlimited: false,
      },
    },
    {
      name: "Zero remaining (quota exceeded)",
      headers: {
        "X-Quota-Limit": "5368709120",
        "X-Quota-Remaining": "0",
        "X-Quota-Reset": "2026-02-01T00:00:00.000Z",
      },
      expects: {
        validUsage: true,
        max: BigInt("5368709120"),
        remaining: BigInt("0"),
        unlimited: false,
      },
    },
    {
      name: "Unlimited bandwidth (X-Quota-Unlimited: true, no quota headers)",
      headers: {
        "X-Quota-Unlimited": "true",
      },
      expects: {
        validUsage: true,
        max: null,
        remaining: null,
        reset: null,
        unlimited: true,
      },
    },
    {
      name: "Limited bandwidth (X-Quota-Unlimited: false)",
      headers: {
        "X-Quota-Limit": "5368709120",
        "X-Quota-Remaining": "4294967296",
        "X-Quota-Reset": "2026-02-01T00:00:00.000Z",
        "X-Quota-Unlimited": "false",
      },
      expects: {
        validUsage: true,
        max: BigInt("5368709120"),
        remaining: BigInt("4294967296"),
        unlimited: false,
      },
    },
    {
      name: "Missing X-Quota-Limit header",
      headers: {
        "X-Quota-Remaining": "1000",
        "X-Quota-Reset": "2026-02-01T00:00:00.000Z",
      },
      expects: { validUsage: false },
    },
    {
      name: "Missing X-Quota-Remaining header",
      headers: {
        "X-Quota-Limit": "5000",
        "X-Quota-Reset": "2026-02-01T00:00:00.000Z",
      },
      expects: { validUsage: false },
    },
    {
      name: "Missing X-Quota-Reset header",
      headers: {
        "X-Quota-Limit": "5000",
        "X-Quota-Remaining": "1000",
      },
      expects: { validUsage: false },
    },
    {
      name: "Invalid ISO timestamp",
      headers: {
        "X-Quota-Limit": "5000",
        "X-Quota-Remaining": "1000",
        "X-Quota-Reset": "not-a-date",
      },
      expects: { validUsage: false },
    },
    {
      name: "Invalid BigInt value",
      headers: {
        "X-Quota-Limit": "not-a-number",
        "X-Quota-Remaining": "1000",
        "X-Quota-Reset": "2026-02-01T00:00:00.000Z",
      },
      expects: { validUsage: false },
    },
  ];

  testcases.forEach(({ name, headers, expects }) => {
    info(`Running test case: ${name}`);

    const mockHeaders = new Map(Object.entries(headers));
    const mockResponse = {
      headers: {
        get(key) {
          return mockHeaders.get(key) || null;
        },
      },
    };

    if (expects.validUsage) {
      const usage = ProxyUsage.fromResponse(mockResponse);
      Assert.notEqual(usage, null, `${name}: usage should not be null`);
      Assert.equal(usage.max, expects.max, `${name}: max should match`);
      Assert.equal(
        usage.remaining,
        expects.remaining,
        `${name}: remaining should match`
      );
      if (expects.unlimited) {
        Assert.equal(usage.reset, null, `${name}: reset should be null`);
      } else {
        Assert.ok(
          usage.reset && typeof usage.reset.epochMilliseconds === "number",
          `${name}: reset should be Temporal.Instant`
        );
      }
      Assert.equal(
        usage.unlimited,
        expects.unlimited,
        `${name}: unlimited should match`
      );
      return;
    }

    Assert.throws(
      () => ProxyUsage.fromResponse(mockResponse),
      /Missing required header|invalid|must be non-negative|cannot exceed max|can't parse instant/i,
      `${name}: should throw error for invalid data`
    );
  });
});

add_task(async function test_fetchProxyPass_quotaExceeded() {
  const quota429 = (headers = {}) => {
    return (request, r) => {
      r.setStatusLine(request.httpVersion, 429, "Too Many Requests");
      for (const [name, value] of Object.entries(headers)) {
        if (value !== undefined) {
          r.setHeader(name, value, false);
        }
      }
      r.write(JSON.stringify({ error: "quota_exceeded" }));
    };
  };

  const testcases = [
    {
      name: "429 with usage headers and Retry-After",
      sends: quota429({
        "X-Quota-Limit": "5368709120",
        "X-Quota-Remaining": "0",
        "X-Quota-Reset": "2026-02-01T00:00:00.000Z",
        "Retry-After": "Sat, 01 Feb 2026 00:00:00 GMT",
      }),
      expects: {
        status: 429,
        error: "quota_exceeded",
        validPass: false,
        validUsage: true,
        usage: {
          max: BigInt("5368709120"),
          remaining: BigInt("0"),
        },
        retryAfter: "Sat, 01 Feb 2026 00:00:00 GMT",
      },
    },
    {
      name: "429 without usage headers returns quota_exceeded with null usage",
      sends: quota429({
        "Retry-After": "3600",
      }),
      expects: {
        status: 429,
        error: "quota_exceeded",
        validPass: false,
        usage: null,
        retryAfter: "3600",
      },
    },
    {
      name: "429 without Retry-After",
      sends: quota429({
        "X-Quota-Limit": "5368709120",
        "X-Quota-Remaining": "0",
        "X-Quota-Reset": "2026-02-01T00:00:00.000Z",
      }),
      expects: {
        status: 429,
        error: "quota_exceeded",
        validPass: false,
        validUsage: true,
        usage: {
          max: BigInt("5368709120"),
          remaining: BigInt("0"),
        },
        retryAfter: null,
      },
    },
  ];

  testcases
    .map(({ name, sends, expects }) => {
      return async () => {
        using serverWrapper = makeGuardianServer({ token: sends });
        // eslint-disable-next-line no-unused-vars
        using _setup = setupGuardianClient(serverWrapper);
        const client = new GuardianClient();

        const { status, pass, error, usage, retryAfter } =
          await client.fetchProxyPass(TEST_TOKEN_HANDLE);

        Assert.equal(status, expects.status, `${name}: status should match`);
        Assert.equal(error, expects.error, `${name}: error should match`);

        if (expects.validPass) {
          Assert.notEqual(pass, null, `${name}: pass should not be null`);
        } else {
          Assert.equal(pass, undefined, `${name}: pass should be undefined`);
        }

        if (expects.validUsage) {
          Assert.notEqual(usage, null, `${name}: usage should not be null`);
          Assert.equal(
            usage.max,
            expects.usage.max,
            `${name}: usage.max should match`
          );
          Assert.equal(
            usage.remaining,
            expects.usage.remaining,
            `${name}: usage.remaining should match`
          );
          Assert.ok(
            usage.reset && typeof usage.reset.epochMilliseconds === "number",
            `${name}: usage.reset should be Temporal.Instant`
          );
        } else if (expects.usage === null) {
          Assert.equal(usage, null, `${name}: usage should be null`);
        }

        if (expects.retryAfter !== undefined) {
          Assert.equal(
            retryAfter,
            expects.retryAfter,
            `${name}: retryAfter should match`
          );
        }
      };
    })
    .forEach(test => add_task(test));
});

add_task(async function test_fetchProxyUsage() {
  const ok = (headers = {}) => {
    return (request, r) => {
      r.setStatusLine(request.httpVersion, 200, "OK");
      const defaults = {
        "X-Quota-Limit": "5368709120",
        "X-Quota-Remaining": "4294967296",
        "X-Quota-Reset": "2026-02-01T00:00:00.000Z",
      };
      const merged = { ...defaults, ...headers };
      for (const [name, value] of Object.entries(merged)) {
        if (value !== undefined) {
          r.setHeader(name, value, false);
        }
      }
    };
  };

  const noHeaders = () => {
    return (request, r) => {
      r.setStatusLine(request.httpVersion, 200, "OK");
    };
  };

  const testcases = [
    {
      name: "Valid usage headers",
      sends: ok(),
      expects: {
        usage: {
          max: BigInt("5368709120"),
          remaining: BigInt("4294967296"),
        },
      },
    },
    {
      name: "Missing usage headers returns null",
      sends: noHeaders(),
      expects: {
        usage: null,
      },
    },
    {
      name: "Zero remaining quota",
      sends: ok({ "X-Quota-Remaining": "0" }),
      expects: {
        usage: {
          max: BigInt("5368709120"),
          remaining: BigInt("0"),
        },
      },
    },
  ];

  testcases
    .map(({ name, sends, expects }) => {
      return async () => {
        using serverWrapper = makeGuardianServer({ token: sends });
        // eslint-disable-next-line no-unused-vars
        using _setup = setupGuardianClient(serverWrapper);
        const client = new GuardianClient();

        const usage = await client.fetchProxyUsage(TEST_TOKEN_HANDLE);

        if (expects.usage === null) {
          Assert.equal(usage, null, `${name}: usage should be null`);
        } else {
          Assert.notEqual(usage, null, `${name}: usage should not be null`);
          Assert.equal(
            usage.max,
            expects.usage.max,
            `${name}: usage.max should match`
          );
          Assert.equal(
            usage.remaining,
            expects.usage.remaining,
            `${name}: usage.remaining should match`
          );
          Assert.ok(
            usage.reset && typeof usage.reset.epochMilliseconds === "number",
            `${name}: usage.reset should be Temporal.Instant`
          );
        }
      };
    })
    .forEach(test => add_task(test));
});

add_task(async function test_parseGuardianSuccessURL() {
  const testcases = [
    {
      name: "Valid success URL with code",
      input: "https://example.com/oauth/success?code=abc123",
      expects: { ok: true, error: undefined },
    },
    {
      name: "Error in URL",
      input: "https://example.com/oauth/success?error=generic_error",
      expects: { ok: false, error: "generic_error" },
    },
    {
      name: "Missing code in success URL",
      input: "https://example.com/oauth/success",
      expects: { ok: false, error: "missing_code" },
    },
    {
      name: "Null input",
      input: null,
      expects: { ok: false, error: "timeout" },
    },
  ];

  testcases.forEach(({ name, input, expects }) => {
    info(`Running test case: ${name}`);

    const result = GuardianClient._parseGuardianSuccessURL(input);

    Assert.equal(result.ok, expects.ok, `${name}: ok should match`);
    Assert.equal(result.error, expects.error, `${name}: error should match`);
  });
});

add_task(async function test_proxyPassShouldRotate() {
  const oneHour = Temporal.Duration.from({ hours: 1 });
  const from = Temporal.Instant.from("2025-12-08T12:00:00Z"); // Static point in time
  // The pass is valid for 1 hour from 'from'
  const until = from.add(oneHour);
  const rotationTime = ProxyPass.ROTATION_TIME;

  const testcases = [
    {
      name: "Should not rotate when before rotation time",
      currentTime: until.subtract(rotationTime).subtract({ seconds: 1 }),
      expects: { shouldRotate: false },
    },
    {
      name: "Should rotate when at rotation time",
      currentTime: until.subtract(rotationTime),
      expects: { shouldRotate: true },
    },
    {
      name: "Should rotate when after rotation time",
      currentTime: until.subtract(rotationTime).add({ seconds: 1 }),
      expects: { shouldRotate: true },
    },
    {
      name: "Should rotate when pass is expired",
      currentTime: until.add({ seconds: 1 }),
      expects: { shouldRotate: true },
    },
  ];

  testcases.forEach(({ name, currentTime, expects }) => {
    info(`Running test case: ${name}`);
    const proxyPass = new ProxyPass(createProxyPassToken(from, until));
    const result = proxyPass.shouldRotate(currentTime);
    Assert.equal(
      result,
      expects.shouldRotate,
      `${name}: shouldRotate should match`
    );
  });
});

add_task(async function test_entitlement_toString_schema_validation() {
  const entitlement = new Entitlement({
    autostart: true,
    created_at: "2024-01-15T10:30:00.000Z",
    limited_bandwidth: false,
    location_controls: true,
    subscribed: true,
    uid: 12345,
    website_inclusion: false,
    maxBytes: "1000000000",
  });

  const serialized = entitlement.toString();
  Assert.ok(serialized, "toString() should return a non-empty string");

  const parsed = JSON.parse(serialized);
  Assert.ok(parsed, "toString() output should be valid JSON");

  const result = JsonSchemaValidator.validate(parsed, Entitlement.schema);
  Assert.ok(
    result.valid,
    `toString() output should match schema. Errors: ${JSON.stringify(
      result.errors
    )}`
  );

  const recreated = new Entitlement(parsed);
  Assert.ok(recreated, "Should be able to create Entitlement from parsed data");

  for (const key of Object.keys(entitlement)) {
    const expected = entitlement[key];
    const actual = recreated[key];
    if (typeof expected === "bigint") {
      Assert.equal(
        actual.toString(),
        expected.toString(),
        `${key} matches after round-trip`
      );
    } else {
      Assert.equal(actual, expected, `${key} matches after round-trip`);
    }
  }
});

add_task(async function test_ProxyUsage_serialization() {
  const originalUsage = new ProxyUsage(
    "1000000000",
    "750000000",
    "2026-02-01T00:00:00Z"
  );

  const serialized = JSON.stringify({
    max: originalUsage.max.toString(),
    remaining: originalUsage.remaining.toString(),
    reset: originalUsage.reset.toString(),
    unlimited: originalUsage.unlimited,
  });

  Assert.greater(serialized.length, 0, "Serialization produces output");

  const data = JSON.parse(serialized);
  const deserializedUsage = new ProxyUsage(
    data.max,
    data.remaining,
    data.reset,
    data.unlimited
  );

  Assert.equal(
    deserializedUsage.max.toString(),
    originalUsage.max.toString(),
    "max preserved through serialization"
  );
  Assert.equal(
    deserializedUsage.remaining.toString(),
    originalUsage.remaining.toString(),
    "remaining preserved through serialization"
  );
  Assert.equal(
    deserializedUsage.reset.toString(),
    originalUsage.reset.toString(),
    "reset preserved through serialization"
  );
  Assert.equal(
    deserializedUsage.unlimited,
    originalUsage.unlimited,
    "unlimited preserved through serialization"
  );
});

add_task(async function test_ProxyUsage_unlimited_serialization() {
  const originalUsage = new ProxyUsage(null, null, null, true);

  Assert.equal(originalUsage.max, null, "max defaults to null when unlimited");
  Assert.equal(
    originalUsage.remaining,
    null,
    "remaining defaults to null when unlimited"
  );
  Assert.equal(
    originalUsage.reset,
    null,
    "reset defaults to null when unlimited"
  );

  const serialized = JSON.stringify({
    max: originalUsage.max?.toString() ?? null,
    remaining: originalUsage.remaining?.toString() ?? null,
    reset: originalUsage.reset?.toString() ?? null,
    unlimited: originalUsage.unlimited,
  });

  const data = JSON.parse(serialized);
  const deserializedUsage = new ProxyUsage(
    data.max,
    data.remaining,
    data.reset,
    data.unlimited
  );

  Assert.equal(deserializedUsage.max, null, "max round-trips as null");
  Assert.equal(
    deserializedUsage.remaining,
    null,
    "remaining round-trips as null"
  );
  Assert.equal(deserializedUsage.reset, null, "reset round-trips as null");
  Assert.equal(
    deserializedUsage.unlimited,
    true,
    "unlimited round-trips as true"
  );
});

add_task(async function test_ProxyUsage_equals() {
  const base = new ProxyUsage(
    "5368709120",
    "4294967296",
    "2026-02-01T00:00:00.000Z"
  );

  Assert.ok(
    base.equals(
      new ProxyUsage("5368709120", "4294967296", "2026-02-01T00:00:00.000Z")
    ),
    "Identical limited usages are equal"
  );
  Assert.ok(
    !base.equals(
      new ProxyUsage("5368709120", "4294967295", "2026-02-01T00:00:00.000Z")
    ),
    "Differing remaining is not equal"
  );
  Assert.ok(
    !base.equals(
      new ProxyUsage("5368709121", "4294967296", "2026-02-01T00:00:00.000Z")
    ),
    "Differing max is not equal"
  );
  Assert.ok(
    !base.equals(
      new ProxyUsage("5368709120", "4294967296", "2026-03-01T00:00:00.000Z")
    ),
    "Differing reset is not equal"
  );

  const unlimited = new ProxyUsage(null, null, null, true);
  Assert.ok(
    unlimited.equals(new ProxyUsage(null, null, null, true)),
    "Two unlimited usages are equal"
  );
  Assert.ok(!unlimited.equals(base), "Unlimited and limited are not equal");
  Assert.ok(!base.equals(unlimited), "Limited and unlimited are not equal");

  Assert.ok(!base.equals(null), "A usage does not equal null");
  Assert.ok(!base.equals({}), "A usage does not equal a plain object");
});

add_task(async function test_fetchProxyPass_abort() {
  using tokenHandler = makeStallHandler();
  using serverWrapper = makeGuardianServer({ token: tokenHandler.handler });

  // eslint-disable-next-line no-unused-vars
  using _setup = setupGuardianClient(serverWrapper);
  const client = new GuardianClient();
  const controller = new AbortController();
  const promise = client.fetchProxyPass(TEST_TOKEN_HANDLE, controller.signal);

  do_timeout(10, () => controller.abort());

  await Assert.rejects(
    promise,
    err => err.name === "AbortError",
    "Should reject with abort error"
  );
});

add_task(async function test_fetchUserInfo_abort() {
  using statusHandler = makeStallHandler();
  using serverWrapper = makeGuardianServer({ status: statusHandler.handler });

  // eslint-disable-next-line no-unused-vars
  using _setup = setupGuardianClient(serverWrapper);
  const client = new GuardianClient();
  const controller = new AbortController();
  const promise = client.fetchUserInfo(TEST_TOKEN_HANDLE, controller.signal);

  do_timeout(10, () => controller.abort());

  await Assert.rejects(
    promise,
    err => err.name === "AbortError",
    "Should reject with abort error"
  );
});

add_task(async function test_fetchProxyUsage_abort() {
  using tokenHandler = makeStallHandler();
  using serverWrapper = makeGuardianServer({ token: tokenHandler.handler });

  // eslint-disable-next-line no-unused-vars
  using _setup = setupGuardianClient(serverWrapper);
  const client = new GuardianClient();
  const controller = new AbortController();
  const promise = client.fetchProxyUsage(TEST_TOKEN_HANDLE, controller.signal);

  do_timeout(10, () => controller.abort());

  await Assert.rejects(
    promise,
    err => err.name === "AbortError",
    "Should reject with abort error"
  );
});

add_task(async function test_abort_before_fetch() {
  using serverWrapper = makeGuardianServer({
    token: () => {
      Assert.ok(
        false,
        "Should not make network request with pre-aborted signal"
      );
    },
  });

  // eslint-disable-next-line no-unused-vars
  using _setup = setupGuardianClient(serverWrapper);
  const client = new GuardianClient();
  const controller = new AbortController();
  controller.abort();

  await Assert.rejects(
    client.fetchProxyPass(TEST_TOKEN_HANDLE, controller.signal),
    err => err.name === "AbortError",
    "Should reject immediately with pre-aborted signal"
  );
});

add_task(async function test_getToken_abort() {
  const sandbox = sinon.createSandbox();

  try {
    const { getFxAccountsSingleton } = ChromeUtils.importESModule(
      "resource://gre/modules/FxAccounts.sys.mjs"
    );
    const fxAccounts = getFxAccountsSingleton();

    sandbox.stub(fxAccounts, "getOAuthToken").returns(new Promise(() => {}));

    const controller = new AbortController();
    const promise = IPPFxaActivateAuthProvider.getToken(controller.signal);

    do_timeout(10, () => controller.abort());

    await Assert.rejects(
      promise,
      () => true,
      "Should reject when abort signal fires during OAuth token fetch"
    );
  } finally {
    sandbox.restore();
  }
});
