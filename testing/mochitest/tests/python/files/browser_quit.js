function test() {
  waitForExplicitFinish();
  ok(true, "Quitting the browser in the middle of the test");
  Services.startup.quit(Ci.nsIAppStartup.eForceQuit);
}
