/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { E10SUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/E10SUtils.sys.mjs"
);

function isJitDisabledForRemoteType(remoteType) {
  return (
    remoteType.endsWith("^disableJit=1") || remoteType.endsWith("&disableJit=1")
  );
}

function assertJitState(url, isAllowed) {
  let uri = Services.io.newURI(url);

  Assert.equal(
    Services.policies.isAllowedForURI("jit", uri),
    isAllowed,
    `Policy service should return the expected state for ${url}`
  );

  let remoteType = E10SUtils.getRemoteTypeForURIObject(uri, {
    remoteSubFrames: true,
    multiProcess: true,
    preferredRemoteType: E10SUtils.DEFAULT_REMOTE_TYPE,
  });

  Assert.equal(
    isJitDisabledForRemoteType(remoteType),
    !isAllowed,
    `Remote type should have the expected JIT state for ${url}`
  );
}

add_task(async function test_isAllowedForSite() {
  // Empty policies don't block anything
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", true);
  assertJitState("http://example.com/", true);
  assertJitState(
    "moz-nullprincipal:{56cac540-864d-47e7-8e25-1614eab5155e}",
    true
  );

  // Simple match case
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", true);
  assertJitState("http://example.com/", false);
  assertJitState("http://www.example.com/", false);
  assertJitState("http://test.example.com/", false);
  assertJitState(
    "moz-nullprincipal:{56cac540-864d-47e7-8e25-1614eab5155e}",
    true
  );

  // Multiple match case
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com", "*.example.org"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", false);
  assertJitState("http://example.com/", false);
  assertJitState(
    "moz-nullprincipal:{56cac540-864d-47e7-8e25-1614eab5155e}",
    true
  );

  // Missing wildcard or being too specific still uses the base domain
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["example.com", "www.example.org"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", false);
  assertJitState("http://www.example.org/", false);
  assertJitState("http://test.example.org/", false);
  assertJitState("http://example.com/", false);
  assertJitState("http://www.example.com/", false);
  assertJitState("http://test.example.com/", false);
  assertJitState(
    "moz-nullprincipal:{56cac540-864d-47e7-8e25-1614eab5155e}",
    true
  );

  // No match implies all sites
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Exceptions: ["*.example.com"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", false);
  assertJitState("http://example.org/", false);
  assertJitState("http://example.com/", true);
  assertJitState(
    "moz-nullprincipal:{56cac540-864d-47e7-8e25-1614eab5155e}",
    false
  );

  // Empty match implies all sites
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: [],
          Exceptions: ["*.example.com"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", false);
  assertJitState("http://example.org/", false);
  assertJitState("http://example.com/", true);
  assertJitState(
    "moz-nullprincipal:{56cac540-864d-47e7-8e25-1614eab5155e}",
    false
  );

  // Wildcard implies all sites
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*"],
          Exceptions: ["*.example.com"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", false);
  assertJitState("http://example.org/", false);
  assertJitState("http://example.com/", true);
  assertJitState(
    "moz-nullprincipal:{56cac540-864d-47e7-8e25-1614eab5155e}",
    false
  );

  // Empty policies do nothing
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {},
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", true);
  assertJitState("http://example.com/", true);
  assertJitState(
    "moz-nullprincipal:{56cac540-864d-47e7-8e25-1614eab5155e}",
    true
  );

  // Earlier policies take precedence over later ones.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {
            DisableJit: false,
          },
        },
        {
          Match: ["*.example.com", "*.example.org"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", false);
  assertJitState("http://example.com/", true);
  assertJitState(
    "moz-nullprincipal:{56cac540-864d-47e7-8e25-1614eab5155e}",
    true
  );

  // Earlier policies only take precedence if they include the relevant policy.
  await setupPolicyEngineWithJson({
    policies: {
      SitePolicies: [
        {
          Match: ["*.example.com"],
          Policies: {},
        },
        {
          Match: ["*.example.com", "*.example.org"],
          Policies: {
            DisableJit: true,
          },
        },
      ],
    },
  });

  assertJitState("http://example.net/", true);
  assertJitState("http://example.org/", false);
  assertJitState("http://example.com/", false);
  assertJitState(
    "moz-nullprincipal:{56cac540-864d-47e7-8e25-1614eab5155e}",
    true
  );
});
