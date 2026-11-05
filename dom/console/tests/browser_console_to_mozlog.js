/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* Use console API to log via MOZ_LOG to stdout/file/profiler */

// Use background task in order to control MOZ_LOG env variable passed to another gecko run
const { BackgroundTasksTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/BackgroundTasksTestUtils.sys.mjs"
);
BackgroundTasksTestUtils.init(this);
const do_backgroundtask = BackgroundTasksTestUtils.do_backgroundtask.bind(
  BackgroundTasksTestUtils
);

add_task(async function test_console_to_mozlog() {
  const lines = [];
  // This will load toolkit/components/backgroundtasks/tests/BackgroundTask_console.sys.mjs into another process
  const promise = do_backgroundtask("console", {
    onStdoutLine: (line, _proc) => {
      lines.push(line);
    },
    extraEnv: {
      MOZ_LOG:
        "console:5,my-prefix:2,PageMessages:5,error-only:1,log-module:5,prefix-only:5",
    },
  });
  const exitCode = await promise;
  is(exitCode, 0);

  const pidLine = lines.find(line => line.includes("CONSOLE-PID"));
  ok(pidLine, "Found the line where the parent process PID is logged");
  const [, pid] = pidLine.split(":");
  ok(pid, "Got the pid out of the PID line");

  // Each MOZ_LOG / console api call starts with a description of the process and thread where it is logged
  const threadPrefix = `[Parent ${pid}: Main Thread]: `;

  const expectedLogs = [
    // MOZ_LOG=console:5
    `I/console log: "foo"`,
    `D/console debug: "bar"`,
    `E/console assert: "assert-failure"`,

    // MOZ_LOG=my-prefix:2
    // The console logger allows "info" level, while we restrict to warning.
    //
    // Bug 1923985: For now, the console API level expose to privileged JS
    // isn't synchronized with MOZ_LOG one.
    // `shouldLogLog` should be false because of my-prefix set to level 2.
    `E/my-prefix error: ({shouldLogError:true, shouldLogLog:true})`,
    `W/my-prefix warn: "warning"`,
    `E/my-prefix assert: "prefixed-assert-failure"`,

    // Logger with "Error level" only
    `E/error-only error: "error-only-error"`,
    `E/error-only assert: "error-only-assert"`,

    // MOZ_LOG=PageMessages:5
    `I/PageMessages String message`,
    `E/PageMessages [JavaScript Error: "Error: Async exception" {file: "resource://testing-common/backgroundtasks/BackgroundTask_console.sys.mjs" line: 50}]`,

    // MOZ_LOG=log-module:5
    // MOZ_LOG already logs the module name, so only the prefix is added.
    `I/log-module log: "log-module-only"`,
    `I/log-module log: Some prefix: "log-module-and-prefix"`,
    `I/log-module log: "log-module-as-prefix"`,

    // MOZ_LOG=prefix-only:5
    // Without a logModule, the prefix names the module, and isn't added.
    `I/prefix-only log: "prefix-only"`,
  ];

  info(lines);

  for (const expected of expectedLogs) {
    ok(
      lines.some(line => line.includes(`${threadPrefix}${expected}`)),
      `Found ${expected}`
    );
  }
  ok(
    !lines.some(line => line.includes("limited-level")),
    "The 'limited-level' logger will be completely disabled as it isn't enabled in MOZ_LOG"
  );

  ok(
    !lines.some(line =>
      line.includes("BackgroundTask_console.sys.mjs 38 runBackgroundTask")
    ),
    "Stack trace for console calls are **not** logged"
  );

  // The console.log call with my-prefix isn't logged because of log level set to "2" for my-prefix
  ok(
    !lines.some(line => line.includes("not-logged")),
    "Logs blocked by too verbose level aren't visible in stdout"
  );
});

add_task(async function test_console_to_mozlog_level_override() {
  const lines = [];
  const promise = do_backgroundtask("console", {
    onStdoutLine: (line, _proc) => {
      lines.push(line);
    },
    extraEnv: {
      MOZ_LOG: "limited-level:5,jsstacks",
    },
  });
  const exitCode = await promise;
  is(exitCode, 0);

  const pidLine = lines.find(line => line.includes("CONSOLE-PID"));
  ok(pidLine, "Found the line where the parent process PID is logged");
  const [, pid] = pidLine.split(":");
  ok(pid, "Got the pid out of the PID line");

  // Each MOZ_LOG / console api call starts with a description of the process and thread where it is logged
  const threadPrefix = `[Parent ${pid}: Main Thread]: `;

  const expectedLogs = [
    // By default, the logger will only log the error
    `E/limited-level error: "log-unless-disabled-by-mozlog"`,
    // But thanks to the MOZ_LOG override, it also log the other levels now
    `I/limited-level log: "may-be-logged-via-mozlog"`,
  ];

  for (const expected of expectedLogs) {
    ok(
      lines.some(line => line.includes(`${threadPrefix}${expected}`)),
      `Found ${expected}`
    );
  }

  ok(
    lines.some(line =>
      line.includes("BackgroundTask_console.sys.mjs 38 runBackgroundTask")
    ),
    "Stack trace for console calls are logged"
  );

  // The console.log call with my-prefix isn't logged because of log level set to "2" for my-prefix
  ok(
    !lines.some(line => line.includes("not-logged")),
    "Logs blocked by too verbose level aren't visible in stdout"
  );
});
