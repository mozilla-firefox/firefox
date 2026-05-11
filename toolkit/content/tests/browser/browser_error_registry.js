/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests for the error registry foundation.
 * These tests verify that the error registry provides correct configurations
 * for network and certificate errors.
 */

const REGISTRY_URL = "chrome://global/content/errors/error-registry.mjs";
const LOOKUP_URL = "chrome://global/content/errors/error-lookup.mjs";
const CERT_ERRORS_URL = "chrome://global/content/errors/cert-errors.mjs";
const PKIX_ERRORS_URL = "chrome://global/content/errors/pkix-errors.mjs";
const SSL_ERRORS_URL = "chrome://global/content/errors/ssl-errors.mjs";
const NET_ERRORS_URL = "chrome://global/content/errors/net-errors.mjs";

add_task(async function test_registry_exports() {
  const registry = ChromeUtils.importESModule(REGISTRY_URL);

  Assert.strictEqual(
    typeof registry.getErrorConfig,
    "function",
    "getErrorConfig should be exported"
  );
  Assert.strictEqual(
    typeof registry.registerError,
    "function",
    "registerError should be exported"
  );
  Assert.strictEqual(
    typeof registry.isErrorSupported,
    "function",
    "isErrorSupported should be exported"
  );
});

add_task(async function test_lookup_exports() {
  const lookup = ChromeUtils.importESModule(LOOKUP_URL);

  Assert.strictEqual(
    typeof lookup.errorHasNoUserFix,
    "function",
    "errorHasNoUserFix should be exported"
  );
  Assert.strictEqual(
    typeof lookup.getResolvedErrorConfig,
    "function",
    "getResolvedErrorConfig should be exported"
  );
  Assert.strictEqual(
    typeof lookup.isFeltPrivacySupported,
    "function",
    "isFeltPrivacySupported should be exported"
  );
});

add_task(async function test_unknown_error_returns_default() {
  const { getErrorConfig } = ChromeUtils.importESModule(REGISTRY_URL);

  const config = getErrorConfig("UNKNOWN_ERROR_12345");

  Assert.strictEqual(
    config,
    undefined,
    "Unknown error should return undefined"
  );
});

add_task(async function test_register_and_get_error() {
  const { registerError, getErrorConfig, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);

  _testOnlyClearRegistry();

  const testConfig = {
    id: "TEST_ERROR_123",
    errorCode: "TEST_ERROR_123",
    category: "cert",
    pageTitleL10nId: "test-page-title",
    bodyTitleL10nId: "test-body-title",
    buttons: {
      showTryAgain: false,
      showGoBack: true,
    },
    hasNoUserFix: true,
  };

  registerError(testConfig);

  const retrieved = getErrorConfig("TEST_ERROR_123");
  Assert.equal(retrieved.errorCode, "TEST_ERROR_123");
  Assert.equal(retrieved.category, "cert");
  Assert.equal(retrieved.pageTitleL10nId, "test-page-title");
  Assert.equal(retrieved.hasNoUserFix, true);

  _testOnlyClearRegistry();
});

add_task(async function test_is_error_supported() {
  const { registerError, isErrorSupported, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);

  _testOnlyClearRegistry();

  Assert.ok(
    !isErrorSupported("TEST_ERROR_456"),
    "Unregistered error should not be supported"
  );

  registerError({
    id: "TEST_ERROR_456",
    errorCode: "TEST_ERROR_456",
    category: "net",
    pageTitleL10nId: "test-title",
    bodyTitleL10nId: "test-body",
    buttons: {},
  });

  Assert.ok(
    isErrorSupported("TEST_ERROR_456"),
    "Registered error should be supported"
  );

  _testOnlyClearRegistry();
});

add_task(async function test_error_has_no_user_fix() {
  const { registerError, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);
  const { errorHasNoUserFix } = ChromeUtils.importESModule(LOOKUP_URL);

  _testOnlyClearRegistry();

  registerError({
    id: "NO_FIX_ERROR",
    errorCode: "NO_FIX_ERROR",
    category: "cert",
    pageTitleL10nId: "test-title",
    bodyTitleL10nId: "test-body",
    buttons: {},
    hasNoUserFix: true,
  });

  registerError({
    id: "HAS_FIX_ERROR",
    errorCode: "HAS_FIX_ERROR",
    category: "cert",
    pageTitleL10nId: "test-title",
    bodyTitleL10nId: "test-body",
    buttons: {},
    hasNoUserFix: false,
  });

  Assert.ok(
    errorHasNoUserFix("NO_FIX_ERROR"),
    "Error with hasNoUserFix=true should return true"
  );
  Assert.ok(
    !errorHasNoUserFix("HAS_FIX_ERROR"),
    "Error with hasNoUserFix=false should return false"
  );
  Assert.ok(
    !errorHasNoUserFix("UNKNOWN_ERROR"),
    "Unknown error should return false (default)"
  );

  _testOnlyClearRegistry();
});

add_task(async function test_resolve_l10n_args() {
  const { resolveL10nArgs } = ChromeUtils.importESModule(LOOKUP_URL);

  const config = {
    dataL10nId: "test-l10n-id",
    dataL10nArgs: { hostname: null, otherArg: "static" },
  };

  const context = { hostname: "example.com" };
  const resolved = resolveL10nArgs(config, context);

  Assert.equal(resolved.dataL10nId, "test-l10n-id");
  Assert.equal(resolved.dataL10nArgs.hostname, "example.com");
  Assert.equal(resolved.dataL10nArgs.otherArg, "static");
});

add_task(async function test_resolve_l10n_args_null_input() {
  const { resolveL10nArgs } = ChromeUtils.importESModule(LOOKUP_URL);

  const resolved = resolveL10nArgs(null, { hostname: "example.com" });
  Assert.equal(resolved, null, "Null config should return null");
});

add_task(async function test_get_resolved_error_config() {
  const { registerError, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);
  const { getResolvedErrorConfig } = ChromeUtils.importESModule(LOOKUP_URL);

  _testOnlyClearRegistry();

  registerError({
    id: "RESOLVE_TEST_ERROR",
    errorCode: "RESOLVE_TEST_ERROR",
    category: "cert",
    pageTitleL10nId: "test-title",
    bodyTitleL10nId: "test-body",
    introContent: {
      dataL10nId: "test-intro",
      dataL10nArgs: { hostname: null },
    },
    shortDescription: {
      dataL10nId: "test-short-desc",
      dataL10nArgs: { hostname: null },
    },
    buttons: {},
    hasNoUserFix: false,
  });

  const context = { hostname: "test.example.com" };
  const resolved = getResolvedErrorConfig("RESOLVE_TEST_ERROR", context);

  Assert.equal(resolved.errorCode, "RESOLVE_TEST_ERROR");
  Assert.equal(resolved.introContent.dataL10nArgs.hostname, "test.example.com");
  Assert.equal(
    resolved.shortDescription.dataL10nArgs.hostname,
    "test.example.com"
  );

  _testOnlyClearRegistry();
});

add_task(async function test_get_errors_by_category() {
  const { registerErrors, getErrorsByCategory, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);

  _testOnlyClearRegistry();

  registerErrors([
    {
      id: "CERT_ERROR_1",
      errorCode: "CERT_ERROR_1",
      category: "cert",
      pageTitleL10nId: "t1",
      bodyTitleL10nId: "b1",
      buttons: {},
    },
    {
      id: "CERT_ERROR_2",
      errorCode: "CERT_ERROR_2",
      category: "cert",
      pageTitleL10nId: "t2",
      bodyTitleL10nId: "b2",
      buttons: {},
    },
    {
      id: "NET_ERROR_1",
      errorCode: "NET_ERROR_1",
      category: "net",
      pageTitleL10nId: "t3",
      bodyTitleL10nId: "b3",
      buttons: {},
    },
  ]);

  const certErrors = getErrorsByCategory("cert");
  const netErrors = getErrorsByCategory("net");

  Assert.equal(certErrors.length, 2, "Should have 2 cert errors");
  Assert.equal(netErrors.length, 1, "Should have 1 net error");

  _testOnlyClearRegistry();
});

add_task(async function test_is_felt_privacy_supported() {
  const { registerError, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);
  const { isFeltPrivacySupported } = ChromeUtils.importESModule(LOOKUP_URL);

  _testOnlyClearRegistry();

  registerError({
    id: "FP_SUPPORTED_ERROR",
    errorCode: "FP_SUPPORTED_ERROR",
    category: "cert",
    pageTitleL10nId: "test-title",
    bodyTitleL10nId: "test-body",
    introContent: { id: "fp-intro" },
    buttons: {},
  });

  registerError({
    id: "fp-another-supported-error",
    category: "net",
    pageTitleL10nId: "test-title",
    bodyTitleL10nId: "test-body",
    buttons: {},
  });

  Assert.ok(
    isFeltPrivacySupported("FP_SUPPORTED_ERROR"),
    "Registered error should support Felt Privacy"
  );
  Assert.ok(
    isFeltPrivacySupported("fp-another-supported-error"),
    "All registered errors should support Felt Privacy"
  );
  Assert.ok(
    !isFeltPrivacySupported("NONEXISTENT_ERROR"),
    "Unregistered error should not support Felt Privacy"
  );

  _testOnlyClearRegistry();
});

add_task(async function test_register_error_throws_without_id() {
  const { registerError } = ChromeUtils.importESModule(REGISTRY_URL);

  Assert.throws(
    () => registerError({}),
    /Error configuration must have an id/,
    "registerError should throw when config has no id"
  );
});

add_task(async function test_registered_config_is_frozen() {
  const { registerError, getErrorConfig, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);

  _testOnlyClearRegistry();

  registerError({
    id: "FROZEN_TEST_ERROR",
    errorCode: "FROZEN_TEST_ERROR",
    category: "cert",
    pageTitleL10nId: "test-title",
    bodyTitleL10nId: "test-body",
    buttons: {},
  });

  const config = getErrorConfig("FROZEN_TEST_ERROR");
  Assert.ok(Object.isFrozen(config), "Registered config should be frozen");

  _testOnlyClearRegistry();
});

add_task(async function test_duplicate_registration_throws() {
  const { registerError, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);

  _testOnlyClearRegistry();

  registerError({
    id: "DUP_TEST_ERROR",
    errorCode: "DUP_TEST_ERROR",
    category: "cert",
    pageTitleL10nId: "test-title",
    bodyTitleL10nId: "test-body",
    buttons: {},
  });

  Assert.throws(
    () =>
      registerError({
        id: "DUP_TEST_ERROR",
        errorCode: "DUP_TEST_ERROR",
        category: "net",
        pageTitleL10nId: "test-title-2",
        bodyTitleL10nId: "test-body-2",
        buttons: {},
      }),
    /Duplicate error registration/,
    "registerError should throw when a duplicate id is registered"
  );

  _testOnlyClearRegistry();
});

add_task(async function test_initialize_registry_count() {
  const { initializeRegistry, getErrorCount, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);
  const { CERT_ERRORS } = ChromeUtils.importESModule(CERT_ERRORS_URL);
  const { PKIX_ERRORS } = ChromeUtils.importESModule(PKIX_ERRORS_URL);
  const { SSL_ERRORS } = ChromeUtils.importESModule(SSL_ERRORS_URL);
  const { NET_ERRORS } = ChromeUtils.importESModule(NET_ERRORS_URL);

  _testOnlyClearRegistry();
  initializeRegistry();

  const expectedCount =
    CERT_ERRORS.length +
    PKIX_ERRORS.length +
    SSL_ERRORS.length +
    NET_ERRORS.length;
  Assert.equal(
    getErrorCount(),
    expectedCount,
    "initializeRegistry should register all errors from all sources"
  );

  _testOnlyClearRegistry();
});

add_task(async function test_resolve_l10n_args_resolution() {
  const { resolveL10nArgs } = ChromeUtils.importESModule(LOOKUP_URL);

  const config = {
    dataL10nId: "x",
    dataL10nArgs: { hostname: "" },
  };
  const resolved = resolveL10nArgs(config, { hostname: "example.com" });
  Assert.equal(
    resolved.dataL10nArgs.hostname,
    "example.com",
    "Empty string should be replaced with runtime value"
  );
});

add_task(async function test_dataL10nId_configured_as_function() {
  const { resolveL10nArgs } = ChromeUtils.importESModule(LOOKUP_URL);

  const fn = ctx => (ctx.cssClass === "badStsCert" ? "sts-id" : "normal-id");
  const config1 = { dataL10nId: fn, dataL10nArgs: {} };
  const config2 = { dataL10nId: fn, dataL10nArgs: {} };

  const resolved1 = resolveL10nArgs(config1, { cssClass: "badStsCert" });
  Assert.equal(
    resolved1.dataL10nId,
    "sts-id",
    "Function dataL10nId should resolve with badStsCert context"
  );

  const resolved2 = resolveL10nArgs(config2, { cssClass: "other" });
  Assert.equal(
    resolved2.dataL10nId,
    "normal-id",
    "Function dataL10nId should resolve with non-badStsCert context"
  );
});

add_task(async function test_dataL10nArgs_param_as_function() {
  const { resolveL10nArgs } = ChromeUtils.importESModule(LOOKUP_URL);

  const config = {
    dataL10nId: "x",
    dataL10nArgs: { date: ctx => ctx.errorInfo.validNotAfter },
  };
  const resolved = resolveL10nArgs(config, {
    errorInfo: { validNotAfter: 12345 },
  });
  Assert.equal(
    resolved.dataL10nArgs.date,
    12345,
    "Configured function in l10n args should be replaced with its return value"
  );
});

add_task(async function test_resolve_many_l10n_args() {
  const { resolveManyL10nArgs } = ChromeUtils.importESModule(LOOKUP_URL);

  const configs = [
    { dataL10nId: "a", dataL10nArgs: { hostname: null } },
    { dataL10nId: "b", dataL10nArgs: { hostname: null } },
  ];
  const resolved = resolveManyL10nArgs(configs, { hostname: "example.com" });
  Assert.equal(
    resolved[0].dataL10nArgs.hostname,
    "example.com",
    "First config hostname should be resolved"
  );
  Assert.equal(
    resolved[1].dataL10nArgs.hostname,
    "example.com",
    "Second config hostname should be resolved"
  );

  const nullResult = resolveManyL10nArgs(null, { hostname: "example.com" });
  Assert.strictEqual(nullResult, null, "Null input should return null");
});

add_task(async function test_resolve_description_parts_static_array() {
  const { resolveDescriptionParts } = ChromeUtils.importESModule(LOOKUP_URL);

  const parts = [
    { dataL10nId: "part-one", dataL10nArgs: { hostname: null } },
    { dataL10nId: "part-two", dataL10nArgs: { hostname: null } },
  ];
  const resolved = resolveDescriptionParts(parts, { hostname: "example.com" });
  Assert.equal(
    resolved[0].dataL10nArgs.hostname,
    "example.com",
    "First description part hostname should be resolved"
  );
  Assert.equal(
    resolved[1].dataL10nArgs.hostname,
    "example.com",
    "Second description part hostname should be resolved"
  );
});

add_task(async function test_resolve_description_parts_function_online() {
  const { resolveDescriptionParts } = ChromeUtils.importESModule(LOOKUP_URL);
  const { DESCRIPTION_PARTS_MAP } = ChromeUtils.importESModule(NET_ERRORS_URL);

  const result = resolveDescriptionParts(
    DESCRIPTION_PARTS_MAP.dnsNotFoundDescription,
    { noConnectivity: false }
  );
  Assert.ok(
    result.some(p => p.dataL10nId === "neterror-dns-not-found-hint-header"),
    "Online branch should include neterror-dns-not-found-hint-header"
  );
});

add_task(async function test_resolve_description_parts_function_offline() {
  const { resolveDescriptionParts } = ChromeUtils.importESModule(LOOKUP_URL);
  const { DESCRIPTION_PARTS_MAP } = ChromeUtils.importESModule(NET_ERRORS_URL);

  const result = resolveDescriptionParts(
    DESCRIPTION_PARTS_MAP.dnsNotFoundDescription,
    { noConnectivity: true }
  );
  Assert.ok(
    result.some(
      p => p.dataL10nId === "neterror-dns-not-found-offline-hint-header"
    ),
    "Offline branch should include neterror-dns-not-found-offline-hint-header"
  );
});

add_task(async function test_resolve_description_parts_null() {
  const { resolveDescriptionParts } = ChromeUtils.importESModule(LOOKUP_URL);

  const result = resolveDescriptionParts(null, {});
  Assert.deepEqual(
    result,
    [],
    "Null descriptionParts should return empty array"
  );
});

add_task(async function test_resolve_advanced_config() {
  const { registerError, getErrorConfig, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);
  const { resolveAdvancedConfig } = ChromeUtils.importESModule(LOOKUP_URL);

  _testOnlyClearRegistry();

  registerError({
    id: "ADV_TEST_ERROR",
    errorCode: "ADV_TEST_ERROR",
    category: "cert",
    pageTitleL10nId: "test-title",
    bodyTitleL10nId: "test-body",
    buttons: {},
    advanced: {
      whyDangerous: { dataL10nId: "x", dataL10nArgs: { hostname: null } },
      whatCanYouDo: { dataL10nId: "y", dataL10nArgs: { hostname: null } },
      learnMore: { dataL10nId: "z", dataL10nArgs: { hostname: null } },
    },
  });

  const config = getErrorConfig("ADV_TEST_ERROR");
  const resolved = resolveAdvancedConfig(config.advanced, {
    hostname: "example.com",
  });

  Assert.equal(
    resolved.whyDangerous.dataL10nArgs.hostname,
    "example.com",
    "whyDangerous hostname should be resolved"
  );
  Assert.equal(
    resolved.whatCanYouDo.dataL10nArgs.hostname,
    "example.com",
    "whatCanYouDo hostname should be resolved"
  );
  Assert.equal(
    resolved.learnMore.dataL10nArgs.hostname,
    "example.com",
    "learnMore hostname should be resolved"
  );

  _testOnlyClearRegistry();
});

add_task(async function test_resolve_advanced_config_null() {
  const { resolveAdvancedConfig } = ChromeUtils.importESModule(LOOKUP_URL);

  const result = resolveAdvancedConfig(null, {});
  Assert.strictEqual(result, null, "Null advancedConfig should return null");
});

add_task(async function test_ssl_error_rx_record_too_long_registered() {
  const { initializeRegistry, getErrorConfig, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);

  _testOnlyClearRegistry();
  initializeRegistry();

  const config = getErrorConfig("SSL_ERROR_RX_RECORD_TOO_LONG");
  Assert.ok(config, "SSL_ERROR_RX_RECORD_TOO_LONG should be registered");
  Assert.equal(config.category, "cert", "Should be in the cert category");
  Assert.equal(
    config.bodyTitleL10nId,
    "nssFailure2-title",
    "Should have the nssFailure2 title for consistency with other SSL errors"
  );
  Assert.ok(
    Array.isArray(config.introContent),
    "introContent should be an array"
  );
  Assert.equal(
    config.introContent[0].dataL10nId,
    "cert-error-ssl-connection-error",
    "First intro content should be the SSL connection error message"
  );
  Assert.equal(
    config.introContent[1].dataL10nId,
    "ssl-error-rx-record-too-long",
    "Second intro content should be the specific error message"
  );
  Assert.ok(config.buttons.showTryAgain, "Should show try again button");
  Assert.ok(config.buttons.showAdvanced, "Should show advanced section");
  Assert.equal(config.hasNoUserFix, true, "Should have no user fix");

  _testOnlyClearRegistry();
});

add_task(async function test_denied_port_access_intro_content() {
  const { initializeRegistry, getErrorConfig, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);

  _testOnlyClearRegistry();
  initializeRegistry();

  const config = getErrorConfig("deniedPortAccess");
  Assert.ok(config, "deniedPortAccess should be registered");
  Assert.equal(
    config.introContent.dataL10nId,
    "fp-neterror-denied-port-access",
    "Should use the denied port access l10n id, not the offline intro"
  );
  Assert.strictEqual(
    config.introContent.dataL10nArgs,
    undefined,
    "Should not have l10n args (no hostname needed)"
  );

  _testOnlyClearRegistry();
});

add_task(async function test_clock_skew_errors_have_checkClockSkew() {
  const { initializeRegistry, getErrorConfig, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);

  _testOnlyClearRegistry();
  initializeRegistry();

  const clockSkewErrorIds = [
    "SEC_ERROR_EXPIRED_CERTIFICATE",
    "SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE",
    "MOZILLA_PKIX_ERROR_NOT_YET_VALID_CERTIFICATE",
    "MOZILLA_PKIX_ERROR_NOT_YET_VALID_ISSUER_CERTIFICATE",
  ];

  for (const id of clockSkewErrorIds) {
    const config = getErrorConfig(id);
    Assert.ok(config, `${id} should be registered`);
    Assert.equal(
      config.checkClockSkew,
      true,
      `${id} should have checkClockSkew: true`
    );
  }

  _testOnlyClearRegistry();
});

add_task(async function test_resolve_error_id_nss_fallthrough() {
  const { registerError, isErrorSupported, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);
  const { resolveErrorID } = ChromeUtils.importESModule(LOOKUP_URL);

  _testOnlyClearRegistry();

  registerError({
    id: "nssFailure2",
    errorCode: "nssFailure2",
    category: "cert",
    pageTitleL10nId: "test-title",
    bodyTitleL10nId: "test-body",
    buttons: {},
  });

  Assert.ok(
    isErrorSupported("nssFailure2"),
    "nssFailure2 should be registered"
  );

  Assert.strictEqual(
    resolveErrorID({
      errorCodeString: "SSL_ERROR_RX_CERTIFICATE_REQUIRED_ALERT",
      gErrorCode: "nssFailure2",
      noConnectivity: false,
      vpnActive: false,
    }),
    "nssFailure2",
    "Unrecognized errorCodeString with gErrorCode=nssFailure2 should fall through to nssFailure2"
  );

  Assert.strictEqual(
    resolveErrorID({
      errorCodeString: "SSL_ERROR_RX_CERTIFICATE_REQUIRED_ALERT",
      gErrorCode: "dnsNotFound",
      noConnectivity: false,
      vpnActive: false,
    }),
    null,
    "Unrecognized errorCodeString with gErrorCode other than nssFailure2 should return null"
  );

  _testOnlyClearRegistry();
});

add_task(async function test_sec_error_ca_cert_invalid_registered() {
  const { initializeRegistry, getErrorConfig, _testOnlyClearRegistry } =
    ChromeUtils.importESModule(REGISTRY_URL);

  _testOnlyClearRegistry();
  initializeRegistry();

  const config = getErrorConfig("SEC_ERROR_CA_CERT_INVALID");
  Assert.ok(config, "SEC_ERROR_CA_CERT_INVALID should be registered");
  Assert.equal(config.category, "cert", "Should be in the cert category");
  Assert.ok(
    config.buttons.showAddException,
    "Should show the exception button"
  );
  Assert.ok(config.buttons.showAdvanced, "Should show the advanced section");
  Assert.equal(config.hasNoUserFix, true, "Should have no user fix");
  Assert.equal(
    config.advanced.whyDangerous.dataL10nId,
    "fp-certerror-invalid-cert-why-dangerous",
    "Should use the invalid cert why-dangerous l10n id"
  );
  Assert.equal(
    config.advanced.whatCanYouDo.dataL10nId,
    "fp-certerror-untrusted-issuer-what-can-you-do-body",
    "Should reuse the untrusted issuer what-can-you-do l10n id"
  );

  _testOnlyClearRegistry();
});
