/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { Subprocess } = ChromeUtils.importESModule(
  "resource://gre/modules/Subprocess.sys.mjs"
);

// Running xpcshell without a script argument and with a non-tty stdin makes it
// read the script from stdin without a file name.
add_task(async function test_script_from_piped_stdin() {
  const xpcshell = Services.dirsvc.get("XREExeF", Ci.nsIFile);
  const greDir = Services.dirsvc.get("GreD", Ci.nsIFile);

  const proc = await Subprocess.call({
    command: xpcshell.path,
    arguments: ["-g", greDir.path],
    stderr: "stdout",
  });
  await proc.stdin.write("print('piped-stdin-ok');\n");
  await proc.stdin.close();

  let output = "";
  let chunk;
  while ((chunk = await proc.stdout.readString())) {
    output += chunk;
  }
  const { exitCode } = await proc.wait();

  Assert.equal(exitCode, 0, `xpcshell exited cleanly, output: ${output}`);
  Assert.stringContains(output, "piped-stdin-ok");
});
