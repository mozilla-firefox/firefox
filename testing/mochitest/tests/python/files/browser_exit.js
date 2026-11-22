add_task(async function () {
  // Exit with a non-zero status while the test is still running, without
  // crashing, so no minidump is left behind. Never finish the task: the
  // harness must not get to report this test itself.
  Services.startup.quit(Ci.nsIAppStartup.eForceQuit, 3);
  await new Promise(() => {});
});
