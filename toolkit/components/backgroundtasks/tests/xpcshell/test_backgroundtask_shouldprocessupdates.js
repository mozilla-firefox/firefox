/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

add_task(async function test_backgroundtask_shouldprocessupdates() {
  // The task returns 81 if we should process updates, i.e.,
  // !ShouldNotProcessUpdates(), <= 80 otherwise.  xpcshell itself counts as an
  // instance, so the background task will see it and think another instance is
  // running.  N.b.: this isn't as robust as it could be: running Firefox
  // instances and parallel tests interact here (mostly harmlessly).
  //
  // Since the behaviour under test (ShouldNotProcessUpdates()) happens at startup,
  // we can't easily change the lock location of the background task.

  // `shouldprocessupdates` is an updating task, but there is another instance
  // running, so we should not process updates.
  let exitCode = await do_backgroundtask("shouldprocessupdates", {
    extraArgs: ["--test-should-not-process-updates"],
  });
  Assert.equal(80, exitCode);

  // If we change our lock location, the background task won't see our instance
  // running.
  let file = do_get_profile();
  file.append("customExePath");
  let syncManager = Cc["@mozilla.org/updates/update-sync-manager;1"].getService(
    Ci.nsIUpdateSyncManager
  );
  syncManager.resetLock(file);

  // Since we've changed the xpcshell executable name, the background task won't
  // see us and think another instance is running.  This time, there is no
  // reason to not process updates.
  // MOZ_AGENT_ALLOW_UPDATE_PROCESSING keeps this working when the test itself
  // runs under an AI agent.
  exitCode = await do_backgroundtask("shouldprocessupdates", {
    extraArgs: ["--test-should-not-process-updates"],
    extraEnv: { MOZ_AGENT_ALLOW_UPDATE_PROCESSING: "1" },
  });
  Assert.equal(81, exitCode);

  // Same setup, but launched by an AI agent.
  let printedAgentMessage = false;
  exitCode = await do_backgroundtask("shouldprocessupdates", {
    extraArgs: ["--test-should-not-process-updates"],
    extraEnv: {
      CODEX_SANDBOX: "seatbelt",
      MOZ_AGENT_ALLOW_UPDATE_PROCESSING: "",
    },
    onStdoutLine: line => {
      if (line.includes("Pending updates will not be applied")) {
        printedAgentMessage = true;
      }
    },
  });
  Assert.equal(76, exitCode);
  Assert.ok(printedAgentMessage, "Should explain why updates were not applied");

  // MOZ_AGENT_ALLOW_UPDATE_PROCESSING lets an AI agent launch process updates.
  exitCode = await do_backgroundtask("shouldprocessupdates", {
    extraArgs: ["--test-should-not-process-updates"],
    extraEnv: {
      CODEX_SANDBOX: "seatbelt",
      MOZ_AGENT_ALLOW_UPDATE_PROCESSING: "1",
    },
  });
  Assert.equal(81, exitCode);

  // Same setup as above (no other instance detected), but with
  // MOZ_DISABLE_UPDATE_PROCESSING set in the environment, which automation
  // drivers use to prevent a launched instance from finalizing an update
  // that is pending/staged for the shared installation.  This should be
  // the deciding factor, taking priority over there being no other reason
  // to skip processing updates.
  exitCode = await do_backgroundtask("shouldprocessupdates", {
    extraArgs: ["--test-should-not-process-updates"],
    extraEnv: { MOZ_DISABLE_UPDATE_PROCESSING: "1" },
  });
  Assert.equal(77, exitCode);

  // `shouldnotprocessupdates` is not a recognized updating task, so we should
  // not process updates.
  exitCode = await do_backgroundtask("shouldnotprocessupdates", {
    extraArgs: ["--test-should-not-process-updates"],
  });
  Assert.equal(78, exitCode);
});
