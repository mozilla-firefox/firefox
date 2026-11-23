/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_active_policies() {
  await setupPolicyEngineWithJson({
    policies: {
      PrivateBrowsingModeAvailability: 1,
    },
  });

  let expected = {
    PrivateBrowsingModeAvailability: 1,
  };

  Assert.deepEqual(
    await Services.policies.getActivePolicies(),
    expected,
    "Active policies parsed correctly"
  );
});

add_task(async function test_wrong_policies() {
  await setupPolicyEngineWithJson({
    policies: {
      BlockAboutSupport: [true],
    },
  });

  Assert.deepEqual(
    await Services.policies.getActivePolicies(),
    {},
    "Wrong policies ignored"
  );
});

add_task(async function test_content_process() {
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], function () {
    try {
      Services.policies.getActivePolicies();
    } catch (ex) {
      is(
        ex.result,
        Cr.NS_ERROR_XPC_JSOBJECT_HAS_NO_FUNCTION_NAMED,
        "Function getActivePolicies() doesn't have a valid definition in the content process"
      );
    }
  });
});
