const { Subprocess } = ChromeUtils.importESModule(
  "resource://gre/modules/Subprocess.sys.mjs"
);

let gAppDataDir = null;
let gTempCrashReport = null;
let gPrefsFile = null;
let gCrashReporterPath = null;

// Make a copy of the crash report we generated to a temporary folder
// so we use it in the test.
async function make_temp_crash_report(mdump, extra) {
  const tempReports = do_get_tempdir().clone();
  tempReports.append("reports");

  const tempDump = tempReports.clone();
  tempDump.append(mdump.leafName);
  await IOUtils.copy(mdump.path, tempDump.path);
  const tempExtra = tempReports.clone();
  tempExtra.append(extra.leafName);
  await IOUtils.copy(extra.path, tempExtra.path);

  return {
    mdump: tempDump,
    extra: tempExtra,
  };
}

// Make a copy of the crash report we generated for this test into the
// minidump directory.
async function put_crash_report_in_minidump_dir() {
  const minidumpDir = do_get_minidumpdir();
  const mdump = minidumpDir.clone();
  mdump.append(gTempCrashReport.mdump.leafName);
  await IOUtils.copy(gTempCrashReport.mdump.path, mdump.path);
  const extra = minidumpDir.clone();
  extra.append(gTempCrashReport.extra.leafName);
  await IOUtils.copy(gTempCrashReport.extra.path, extra.path);

  return {
    mdump,
    extra,
  };
}

// Enable/disable the memory tester via profile-specific prefs and optionally
// enable a subset of the tests via the `tests` argument. `tests` should
// contain an array of strings with the test names.
async function set_memtest_prefs(enabled, tests) {
  let prefs = "";

  if (!enabled) {
    // Memory tests are enabled by default, so add this only if we want to
    // explicitly disable them.
    prefs += 'user_pref("browser.crashReporter.memtest", false);\n';
  }

  if (tests) {
    const tests_str = tests.reduce((acc, curr) => acc + " " + curr, "");
    prefs +=
      'user_pref("browser.crashReporter.memtestKinds", "' + tests_str + '");\n';
  }

  await IOUtils.writeUTF8(gPrefsFile.path, prefs);
}

// Run the crash reporter client on the specified crash report, return the
// contents of the .extra file after it has been updated by the client.
async function run_crash_reporter_client(report) {
  const proc = await Subprocess.call({
    command: gCrashReporterPath.path,
    arguments: [report.mdump.path],
    environment: {
      // Set auto submit so the crash reporter will exit on its own.
      MOZ_CRASHREPORTER_AUTO_SUBMIT: "1",
      MOZ_CRASHREPORTER_DATA_DIRECTORY: gAppDataDir,
      MOZ_CRASHREPORTER_NO_DELETE_DUMP: "1",
    },
    environmentAppend: true,
  });

  await proc.wait();
  const movedExtra = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  movedExtra.initWithPath(gAppDataDir);
  movedExtra.append("pending");
  movedExtra.append(report.extra.leafName);

  const extra = await IOUtils.readJSON(movedExtra.path);

  Assert.ok(
    "StackTraces" in extra,
    "The crash report was updated by the client"
  );

  return extra;
}

add_setup(async function prepare_test_environment() {
  gAppDataDir = Services.env.get("MOZ_APP_DATA");
  gPrefsFile = do_get_profile().clone();
  gPrefsFile.append("prefs.js");
  gCrashReporterPath = getCrashReporterPath();

  await do_crash(
    function () {
      crashType = CrashTestUtils.CRASH_INVALID_POINTER_DEREF;
    },
    async function (mdump, extra, extraFile) {
      gTempCrashReport = await make_temp_crash_report(mdump, extraFile);
    },
    /* returnZero */ true
  );

  // Update the temporary extra file with the profile directory and
  // dummy server URL.
  const extra = await IOUtils.readJSON(gTempCrashReport.extra.path);
  extra.ServerURL = "https://foo";
  extra.ProfileDirectory = do_get_profile().path;
  await IOUtils.writeJSON(gTempCrashReport.extra.path, extra);
});

add_task(async function test_disable_memtest() {
  const report = await put_crash_report_in_minidump_dir();
  await set_memtest_prefs(/* enabled */ false);
  const extra = await run_crash_reporter_client(report);

  Assert.ok(
    !("MemtestOutput" in extra),
    "The `MemtestOutput` annotation is not present"
  );
});

add_task(async function test_enable_memtest() {
  const report = await put_crash_report_in_minidump_dir();
  await set_memtest_prefs(/* enabled */ true);
  const extra = await run_crash_reporter_client(report);

  Assert.ok(
    "MemtestOutput" in extra,
    "The `MemtestOutput` annotation is present"
  );
});

add_task(async function test_random_val() {
  const memory_tests = ["RandomVal", "MovInvRandom", "Modulo20"];
  const report = await put_crash_report_in_minidump_dir();
  await set_memtest_prefs(/* enabled */ true, memory_tests);
  const extra = await run_crash_reporter_client(report);

  Assert.ok(
    "MemtestOutput" in extra,
    "The `MemtestOutput` annotation is present"
  );

  const memtest_output = JSON.parse(extra.MemtestOutput);

  for (let i = 0; i < memory_tests.length; i++) {
    Assert.equal(
      memtest_output.reports[i].test_kind,
      memory_tests[i],
      "Memory test no. " + i + " is " + memory_tests[i]
    );
  }
});
