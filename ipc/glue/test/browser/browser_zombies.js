/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// This test is a simple check that terminated processes are cleaned
// up.  It's currently Linux only, and only launches one content
// process (no other types, no use of multiple processes to try to
// provoke race conditions, etc.), then causes it to exit and waits
// until it no longer exists.
//
// If the process continues to exist, including as a zombie, the test
// will time out.  The test repeatedly polls the process status and
// logs it, so it will be obvious if it was a regular hang or a
// process management failure.

const { ctypes } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs"
);

const libc = ctypes.open("libc.so.6");
const { O_PATH, O_CLOEXEC } = ChromeUtils.getLibcConstants();
const c_open = libc.declare(
  "open",
  ctypes.default_abi,
  ctypes.int,
  ctypes.char.ptr,
  ctypes.int
);
const c_close = libc.declare(
  "close",
  ctypes.default_abi,
  ctypes.int,
  ctypes.int
);

async function millisleep(ms) {
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, ms));
}

// pidfd is a fd for the /proc/{pid} subdir, to avoid the small
// possibility of spurious test failures if the pid is reused.
async function getProcStatus(pidfd) {
  let bytes;
  try {
    bytes = await IOUtils.read(`/proc/self/fd/${pidfd}/stat`, {
      maxBytes: 4096,
    });
  } catch (ex) {
    if (!DOMException.isInstance(ex)) {
      throw ex;
    }
    // Hopefully this is ENOENT
    return "dead";
  }
  let rparen = bytes.lastIndexOf(41 /* ')' */);
  if (rparen < 0 || rparen >= bytes.length - 2) {
    throw new Error(`bad proc_pid_stat line`);
  }
  let state = String.fromCodePoint(bytes[rparen + 2]);
  if (state == "Z") {
    return "zombie";
  }
  return "running";
}

add_task(async function () {
  let pidfd = -1;
  let pid = -1;
  try {
    // 1. Launch content process
    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: "https://example.com/",
        forceNewProcess: true,
      },
      async function (browser) {
        pid = browser.frameLoader.remoteTab.osPid;
        if (pid <= 0) {
          throw new Error(`bad pid: ${pid}`);
        }
        ok(true, `got content process pid ${pid}`);
        // 2. Open procfs dir
        pidfd = c_open(`/proc/${pid}`, O_PATH | O_CLOEXEC);
        if (pidfd < 0) {
          throw new Error(`failed to open pidfd: errno ${ctypes.errno}`);
        }
        ok(true, `opened pidfd ${pidfd}`);
        Assert.equal(await getProcStatus(pidfd), "running");
        // 3. Shut down content process
      }
    );
    // 4. Check the process status until it's gone
    while (true) {
      let state = await getProcStatus(pidfd);
      dump(`process status: ${state}\n`);
      if (state == "dead") {
        break;
      }
      await millisleep(333); // 3 Hz
    }
    ok(true, "process eventually exited");
  } finally {
    if (pidfd >= 0) {
      let rv = c_close(pidfd);
      Assert.greaterOrEqual(rv, 0, "closed pidfd");
    }
  }
});
