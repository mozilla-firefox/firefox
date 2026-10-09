/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// fcntl commands for reading the flags set by the subprocess modules. The
// values are the same on Linux and macOS but are not exposed by
// ChromeUtils.getLibcConstants().
const F_GETFD = 1;
const F_GETFL = 3;

add_task(async function test_subprocess_pipe_flags() {
  const { getSubprocessImplForTest } = ChromeUtils.importESModule(
    "resource://gre/modules/Subprocess.sys.mjs"
  );
  const { libc } = ChromeUtils.importESModule(
    "resource://gre/modules/subprocess/subprocess_unix.sys.mjs"
  );
  const { FD_CLOEXEC, O_NONBLOCK } = ChromeUtils.getLibcConstants();

  const worker = getSubprocessImplForTest().Process.getWorker();
  equal(
    libc.fcntl(worker.signalFd, F_GETFD),
    FD_CLOEXEC,
    "The main-thread signal pipe has exactly FD_CLOEXEC set"
  );

  const proc = await Subprocess.call({
    command: await Subprocess.pathSearch("cat"),
    stderr: "pipe",
  });
  const fds = await worker.call("getFds", [proc.id]);
  ok(
    fds.every(Number.isInteger),
    "The worker owns the stdin, stdout and stderr pipes"
  );
  for (const fd of fds) {
    equal(
      libc.fcntl(fd, F_GETFD),
      FD_CLOEXEC,
      "Worker pipes have exactly FD_CLOEXEC set"
    );
    equal(
      libc.fcntl(fd, F_GETFL) & O_NONBLOCK,
      O_NONBLOCK,
      "Worker pipes are nonblocking"
    );
  }

  const output = proc.stdout.readString(5);
  await proc.stdin.write("hello");
  equal(await output, "hello", "The subprocess pipes transfer data");
  await proc.stdin.close();
  equal((await proc.wait()).exitCode, 0, "The subprocess exits cleanly");
});
