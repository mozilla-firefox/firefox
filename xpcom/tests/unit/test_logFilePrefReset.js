/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const kLogFilePref = "logging.config.LOG_FILE";
const kModule = "logFilePrefReset";

add_task(async function test_clearing_log_file_pref_stops_logging_to_files() {
  const cwdLog = Services.dirsvc.get("CurWorkD", Ci.nsIFile);
  cwdLog.append(".moz_log");
  Assert.ok(!cwdLog.exists(), "No .moz_log in the working directory yet");

  const logger = console.createInstance({ logModule: kModule });
  Services.prefs.setStringPref(`logging.${kModule}`, "debug");
  // Flush each line, so the log file size can be checked right after logging.
  Services.prefs.setBoolPref("logging.config.sync", true);

  const profilePath = do_get_profile().path;
  Services.prefs.setStringPref(
    kLogFilePref,
    PathUtils.join(profilePath, "log")
  );
  const logPath = PathUtils.join(
    profilePath,
    `log-main.${Services.appinfo.processID}.moz_log`
  );
  Assert.ok(await IOUtils.exists(logPath), "The log file was created");

  logger.log("Logged to the file");
  Assert.greater(
    (await IOUtils.stat(logPath)).size,
    0,
    "Something was logged to the file"
  );

  Services.prefs.clearUserPref(kLogFilePref);
  Services.tm.spinEventLoopUntilEmpty();
  const { size } = await IOUtils.stat(logPath);
  logger.log("Logged to stderr");
  Assert.equal(
    (await IOUtils.stat(logPath)).size,
    size,
    "Nothing more was logged to the file after clearing the pref"
  );
  Assert.ok(
    !cwdLog.exists(),
    "Clearing the log file pref doesn't create .moz_log in the working directory"
  );
});
