// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/publicdomain/zero/1.0/
"use strict";

// Tests that a change of the "disable all security checks" state of a user
// context stops sessions established before it from being resumed.

do_get_profile();

const HOST = "expired.example.com";
const ORIGIN_ATTRIBUTES = { userContextId: 5 };

const certOverrideService = Cc[
  "@mozilla.org/security/certoverride;1"
].getService(Ci.nsICertOverrideService);

function setDisableAllSecurityChecks(aDisable) {
  certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyData(
    aDisable
  );
}

function setDisableAllSecurityChecksForUserContext(aDisable) {
  certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyDataForUserContext(
    ORIGIN_ATTRIBUTES.userContextId,
    aDisable
  );
}

function resetDisableAllSecurityChecksForUserContext() {
  certOverrideService.resetDisableAllSecurityChecksAndLetAttackersInterceptMyDataForUserContext(
    ORIGIN_ATTRIBUTES.userContextId
  );
}

function resetAllSecurityChecks() {
  setDisableAllSecurityChecks(false);
  resetDisableAllSecurityChecksForUserContext();
}

function connect(aExpectedResult, aResumed) {
  return asyncConnectTo(
    HOST,
    aExpectedResult,
    aResumed === undefined
      ? null
      : securityInfo => equal(securityInfo.resumed, aResumed),
    null,
    ORIGIN_ATTRIBUTES
  );
}

add_setup(async function () {
  await asyncStartTLSTestServer("BadCertAndPinningServer", "bad_certs");
  registerCleanupFunction(resetAllSecurityChecks);
});

add_task(async function test_reset_clears_resumable_sessions() {
  await connect(SEC_ERROR_EXPIRED_CERTIFICATE);
  setDisableAllSecurityChecksForUserContext(true);
  await connect(PRErrorCodeSuccess, false);
  await connect(PRErrorCodeSuccess, true);
  resetDisableAllSecurityChecksForUserContext();
  await connect(SEC_ERROR_EXPIRED_CERTIFICATE);
});

add_task(async function test_disabling_clears_resumable_sessions() {
  setDisableAllSecurityChecksForUserContext(true);
  await connect(PRErrorCodeSuccess, false);
  await connect(PRErrorCodeSuccess, true);
  setDisableAllSecurityChecksForUserContext(false);
  await connect(SEC_ERROR_EXPIRED_CERTIFICATE);
});

add_task(async function test_user_context_overrides_global_state() {
  setDisableAllSecurityChecksForUserContext(false);
  setDisableAllSecurityChecks(true);
  await connect(SEC_ERROR_EXPIRED_CERTIFICATE);
  resetDisableAllSecurityChecksForUserContext();
  await connect(PRErrorCodeSuccess, false);
  await connect(PRErrorCodeSuccess, true);
  setDisableAllSecurityChecksForUserContext(false);
  await connect(SEC_ERROR_EXPIRED_CERTIFICATE);
  resetAllSecurityChecks();
});

add_task(async function test_unchanged_state_keeps_resumable_sessions() {
  setDisableAllSecurityChecksForUserContext(true);
  await connect(PRErrorCodeSuccess, false);
  await connect(PRErrorCodeSuccess, true);
  setDisableAllSecurityChecksForUserContext(true);
  await connect(PRErrorCodeSuccess, true);
  resetDisableAllSecurityChecksForUserContext();
  setDisableAllSecurityChecks(true);
  await connect(PRErrorCodeSuccess, false);
  await connect(PRErrorCodeSuccess, true);
  setDisableAllSecurityChecksForUserContext(true);
  await connect(PRErrorCodeSuccess, true);
  resetDisableAllSecurityChecksForUserContext();
  await connect(PRErrorCodeSuccess, true);
  setDisableAllSecurityChecks(false);
  await connect(SEC_ERROR_EXPIRED_CERTIFICATE);
});
