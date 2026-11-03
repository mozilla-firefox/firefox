/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SpecialMessageActions } = ChromeUtils.importESModule(
  "resource://messaging-system/lib/SpecialMessageActions.sys.mjs"
);

const { FxAccounts } = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccounts.sys.mjs"
);

const { FxaAuthProviderImpl } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/auth/FxaAuthProviderImpl.sys.mjs"
);

const { GuestAuthProviderImpl } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/auth/GuestAuthProviderImpl.sys.mjs"
);

/**
 * Sets up the sign-in flow so AuthProvider.runSignInFlow can be driven to any
 * of its outcomes. Resets Glean first so each task reads its own events.
 *
 * @param {object} options
 * @param {boolean} [options.canConnect] What canConnectAccount resolves to
 * @param {boolean|Error} [options.flow] What fxaSignInFlow resolves to, or an
 *   Error for it to reject with
 * @returns {object} The sandbox and the fxaSignInFlow stub
 */
function stubSignInFlow({ canConnect = true, flow = true } = {}) {
  Services.fog.testResetFOG();

  const sb = sinon.createSandbox();
  sb.stub(FxAccounts, "canConnectAccount").resolves(canConnect);
  const fxaSignInFlow = sb.stub(SpecialMessageActions, "fxaSignInFlow");
  if (flow instanceof Error) {
    fxaSignInFlow.rejects(flow);
  } else {
    fxaSignInFlow.resolves(flow);
  }

  return { sb, fxaSignInFlow };
}

/**
 * Stubs AuthProvider so maybeSignIn can be driven without running a
 * sign-in flow.
 *
 * @param {object} options
 * @param {boolean} [options.signedIn] What isSignedIn resolves to
 * @param {boolean} [options.didSignIn] What runSignInFlow resolves to
 * @returns {object} The sandbox and the runSignInFlow stub
 */
function stubAuthProvider({ signedIn = false, didSignIn = true } = {}) {
  Services.fog.testResetFOG();

  const sb = sinon.createSandbox();
  sb.stub(AuthProvider, "isSignedIn").resolves(signedIn);
  const runSignInFlow = sb
    .stub(AuthProvider, "runSignInFlow")
    .resolves(didSignIn);

  return { sb, runSignInFlow };
}

function singleEventExtra(metric, name) {
  const events = metric.testGetValue();
  Assert.equal(events?.length, 1, `One ${name} was recorded`);
  return events[0].extra;
}

add_task(async function test_autoClose_follows_firstrun_completion() {
  for (const hasCompleted of [false, true]) {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.hasCompleted", hasCompleted]],
    });
    const { sb, fxaSignInFlow } = stubSignInFlow();

    try {
      await AuthProvider.runSignInFlow(gBrowser.selectedBrowser, {
        reason: "signed_out",
      });

      Assert.ok(
        fxaSignInFlow.calledOnce,
        "fxaSignInFlow should be called once"
      );
      Assert.equal(
        fxaSignInFlow.getCall(0).args[0].autoClose,
        hasCompleted,
        `autoClose is ${hasCompleted} when firstrun hasCompleted is ${hasCompleted}`
      );
    } finally {
      sb.restore();
      await SpecialPowers.popPrefEnv();
    }
  }
});

add_task(async function test_signin_flow_started_reason() {
  const { sb } = stubSignInFlow();

  try {
    await AuthProvider.runSignInFlow(gBrowser.selectedBrowser, {
      reason: "both",
    });

    Assert.equal(
      singleEventExtra(
        Glean.smartWindow.signinFlowStarted,
        "signin_flow_started"
      ).reason,
      "both",
      "reason is the one passed to runSignInFlow"
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_signin_flow_provider() {
  for (const provider of ["fxa", "guest"]) {
    const { sb } = stubSignInFlow();
    if (provider === "guest") {
      sb.stub(GuestAuthProviderImpl.prototype, "signIn").resolves(true);
      AuthProvider.setImpl(new GuestAuthProviderImpl());
    }

    try {
      await AuthProvider.runSignInFlow(gBrowser.selectedBrowser, {
        reason: "signed_out",
      });

      Assert.equal(
        singleEventExtra(
          Glean.smartWindow.signinFlowStarted,
          "signin_flow_started"
        ).auth_provider,
        provider,
        `signin_flow_started provider is ${provider}`
      );
      Assert.equal(
        singleEventExtra(
          Glean.smartWindow.signinFlowCompleted,
          "signin_flow_completed"
        ).auth_provider,
        provider,
        `signin_flow_completed provider is ${provider}`
      );
    } finally {
      sb.restore();
      AuthProvider.setImpl(new FxaAuthProviderImpl());
    }
  }
});

add_task(async function test_signin_flow_completed_outcome() {
  const error = new Error("sign-in failed");
  for (const { canConnect, flow, outcome, expected } of [
    { flow: true, outcome: "completed", expected: true },
    { flow: false, outcome: "abandoned", expected: false },
    { canConnect: false, outcome: "blocked", expected: false },
    { flow: error, outcome: "error" },
  ]) {
    const { sb, fxaSignInFlow } = stubSignInFlow({ canConnect, flow });

    try {
      const promise = AuthProvider.runSignInFlow(gBrowser.selectedBrowser, {
        reason: "signed_out",
      });
      if (outcome === "error") {
        await Assert.rejects(
          promise,
          e => e === error,
          "runSignInFlow rethrows"
        );
      } else {
        Assert.equal(
          await promise,
          expected,
          `runSignInFlow resolves ${expected}`
        );
      }
      if (outcome === "blocked") {
        Assert.ok(fxaSignInFlow.notCalled, "No sign-in page is shown");
      }

      Assert.equal(
        singleEventExtra(
          Glean.smartWindow.signinFlowCompleted,
          "signin_flow_completed"
        ).outcome,
        outcome,
        `outcome is ${outcome}`
      );
    } finally {
      sb.restore();
    }
  }
});

add_task(async function test_maybeSignIn_denial_reason() {
  const cases = [
    { consentTime: 0, signedIn: false, reason: "both" },
    { consentTime: 0, signedIn: true, reason: "no_consent" },
    { consentTime: 1735689600, signedIn: false, reason: "signed_out" },
  ];

  for (const { consentTime, signedIn, reason } of cases) {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.tos.consentTime", consentTime]],
    });
    const { sb, runSignInFlow } = stubAuthProvider({
      signedIn,
      didSignIn: false,
    });

    try {
      await AuthProvider.maybeSignIn(gBrowser.selectedBrowser);

      Assert.equal(
        runSignInFlow.getCall(0).args[1].reason,
        reason,
        `reason is ${reason} with consentTime ${consentTime} and signedIn ${signedIn}`
      );
    } finally {
      sb.restore();
      await SpecialPowers.popPrefEnv();
    }
  }
});

add_task(async function test_maybeSignIn_signs_in_when_denied() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.tos.consentTime", 0]],
  });
  const { sb, runSignInFlow } = stubAuthProvider({ signedIn: true });

  try {
    Assert.ok(
      await AuthProvider.maybeSignIn(gBrowser.selectedBrowser),
      "Access is granted after signing in"
    );
    Assert.ok(runSignInFlow.calledOnce, "runSignInFlow should be called once");
    Assert.equal(
      runSignInFlow.getCall(0).args[1].reason,
      "no_consent",
      "runSignInFlow is given the denial reason"
    );
    Assert.ok(
      AuthProvider.hasToUConsent,
      "ToU consent is recorded after signing in"
    );
  } finally {
    sb.restore();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_maybeSignIn_denied_when_not_signed_in() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.tos.consentTime", 0]],
  });
  const { sb } = stubAuthProvider({ didSignIn: false });

  try {
    Assert.ok(
      !(await AuthProvider.maybeSignIn(gBrowser.selectedBrowser)),
      "Access is denied"
    );
    Assert.ok(!AuthProvider.hasToUConsent, "ToU consent is not recorded");
  } finally {
    sb.restore();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_maybeSignIn_records_nothing_when_allowed() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.tos.consentTime", 1735689600]],
  });
  const { sb, runSignInFlow } = stubAuthProvider({ signedIn: true });

  try {
    Assert.ok(
      await AuthProvider.maybeSignIn(gBrowser.selectedBrowser),
      "Access is allowed"
    );
    Assert.ok(runSignInFlow.notCalled, "No sign-in flow is started");
    Assert.equal(
      Glean.smartWindow.signinFlowStarted.testGetValue(),
      null,
      "No signin_flow_started is recorded"
    );
  } finally {
    sb.restore();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_promptSignIn_runs_flow_when_allowed() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.tos.consentTime", 1735689600]],
  });
  const { sb, runSignInFlow } = stubAuthProvider({ signedIn: true });

  try {
    Assert.ok(
      await AuthProvider.promptSignIn(gBrowser.selectedBrowser),
      "Sign-in succeeds"
    );
    Assert.ok(runSignInFlow.calledOnce, "runSignInFlow should be called once");
    Assert.equal(
      runSignInFlow.getCall(0).args[1].reason,
      "none",
      "reason is none when the user already has access"
    );
  } finally {
    sb.restore();
    await SpecialPowers.popPrefEnv();
  }
});
