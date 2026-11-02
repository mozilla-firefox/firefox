/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const isWin = AppConstants.platform == "win";
const isMac = AppConstants.platform == "macosx";

function getShell() {
  const sandbox = sinon.createSandbox();
  let shell = {
    canPinToTaskbar() {},
    QueryInterface: () => shell,
    get macDockSupport() {
      return this;
    },
    get shellService() {
      return this;
    },

    ensureAppIsPinnedToDock: sandbox.stub(),
    isCurrentAppPinnedToTaskbar: sandbox.stub(),
    pinCurrentAppToTaskbar: sandbox.stub().resolves(undefined),
    isAppInDock: false,
  };

  // Prefer the mocked implementation and fall back to the original version,
  // which can call back into the mocked version (via this.shellService).
  shell = new Proxy(shell, {
    get(target, prop) {
      return (prop in target ? target : ShellService)[prop];
    },
  });

  return { sandbox, shell };
}

add_task(async function test_PIN_FIREFOX_TO_TASKBAR() {
  const { sandbox, shell } = getShell();

  const test = () =>
    SMATestUtils.executeAndValidateAction(
      { type: "PIN_FIREFOX_TO_TASKBAR" },
      {
        documentGlobal: {
          getShellService: () => shell,
        },
      }
    );

  await test();

  function check(count, message) {
    Assert.equal(
      shell.pinCurrentAppToTaskbar.callCount,
      count * isWin,
      `pinCurrentAppToTaskbar was ${message} by the action for windows`
    );
    if (isWin) {
      Assert.ok(
        shell.pinCurrentAppToTaskbar.calledWithExactly(false, false, false),
        "pinCurrentAppToTaskbar called with privateBrowsing=false, fireAndForget=false and promptOnly=false"
      );
    }
    Assert.equal(
      shell.ensureAppIsPinnedToDock.callCount,
      count * isMac,
      `ensureAppIsPinnedToDock was ${message} by the action for not windows`
    );
  }
  check(1, "called");

  // Pretend the app is already pinned.
  shell.isCurrentAppPinnedToTaskbar.resolves(true);
  shell.isAppInDock = true;
  await test();
  check(1, "not called");

  // Pretend the app became unpinned.
  shell.isCurrentAppPinnedToTaskbar.resolves(false);
  shell.isAppInDock = false;
  await test();
  check(2, "called again");

  sandbox.restore();
});

add_task(async function test_FIRE_AND_FORGET() {
  // Test the fireAndForget parameter.
  const { sandbox, shell } = getShell();

  const test = () =>
    SMATestUtils.executeAndValidateAction(
      { type: "PIN_FIREFOX_TO_TASKBAR", data: { fireAndForget: true } },
      {
        documentGlobal: {
          getShellService: () => shell,
        },
      }
    );

  await test();

  function check(count, message) {
    Assert.equal(
      shell.pinCurrentAppToTaskbar.callCount,
      count * isWin,
      `pinCurrentAppToTaskbar was ${message} by the action for windows`
    );
    if (isWin) {
      Assert.ok(
        shell.pinCurrentAppToTaskbar.calledWithExactly(false, true, false),
        "pinCurrentAppToTaskbar called with privateBrowsing=false, fireAndForget=true and promptOnly=false"
      );
    }
    Assert.equal(
      shell.ensureAppIsPinnedToDock.callCount,
      count * isMac,
      `ensureAppIsPinnedToDock was ${message} by the action for not windows`
    );
  }
  check(1, "called");

  sandbox.restore();
});

add_task(async function test_PROMPT_ONLY() {
  for (const type of ["PIN_FIREFOX_TO_TASKBAR", "PIN_AND_DEFAULT"]) {
    const { sandbox, shell } = getShell();
    sandbox.stub(SpecialMessageActions, "setDefaultBrowser");

    await SMATestUtils.executeAndValidateAction(
      { type, data: { promptOnly: true } },
      {
        documentGlobal: {
          getShellService: () => shell,
        },
      }
    );

    Assert.equal(
      shell.pinCurrentAppToTaskbar.callCount,
      isWin ? 1 : 0,
      `pinCurrentAppToTaskbar was called by ${type} for windows`
    );
    if (isWin) {
      Assert.ok(
        shell.pinCurrentAppToTaskbar.calledWithExactly(false, false, true),
        `${type} called pinCurrentAppToTaskbar with promptOnly=true`
      );
    }

    sandbox.restore();
  }
});
