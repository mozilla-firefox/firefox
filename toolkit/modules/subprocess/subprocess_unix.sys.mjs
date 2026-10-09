/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  BaseProcess,
  PromiseWorker,
} from "resource://gre/modules/subprocess/subprocess_common.sys.mjs";

import { ctypes } from "resource://gre/modules/ctypes.sys.mjs";

var obj = { ctypes };
Services.scriptloader.loadSubScript(
  "resource://gre/modules/subprocess/subprocess_shared.js",
  obj
);
Services.scriptloader.loadSubScript(
  "resource://gre/modules/subprocess/subprocess_shared_unix.js",
  obj
);

const { SubprocessConstants, LIBC } = obj;

// libc is exported for tests.
export var libc = obj.libc;

class UnixPromiseWorker extends PromiseWorker {
  constructor(...args) {
    super(...args);

    let fds = ctypes.int.array(2)();
    let res = libc.pipe(fds);
    if (res == -1) {
      throw new Error("Unable to create pipe");
    }

    this.signalFd = fds[1];

    libc.fcntl(fds[0], LIBC.F_SETFL, ctypes.int(LIBC.O_NONBLOCK));
    libc.fcntl(fds[0], LIBC.F_SETFD, ctypes.int(LIBC.FD_CLOEXEC));
    libc.fcntl(fds[1], LIBC.F_SETFD, ctypes.int(LIBC.FD_CLOEXEC));

    this.call("init", [{ signalFd: fds[0] }]);
  }

  closePipe() {
    if (this.signalFd) {
      libc.close(this.signalFd);
      this.signalFd = null;
    }
  }

  onClose() {
    this.closePipe();
    super.onClose();
  }

  signalWorker() {
    libc.write(this.signalFd, new ArrayBuffer(1), 1);
  }

  postMessage(...args) {
    this.signalWorker();
    return super.postMessage(...args);
  }
}

class Process extends BaseProcess {
  static get WORKER_URL() {
    return "resource://gre/modules/subprocess/subprocess_unix.worker.js";
  }

  static get WorkerClass() {
    return UnixPromiseWorker;
  }
}

// Convert a null-terminated char pointer into a sized char array, and then
// convert that into a JS typed array.
// The resulting array will not be null-terminated.
function ptrToUint8Array(input) {
  let { cast, uint8_t } = ctypes;

  let len = 0;
  for (
    let ptr = cast(input, uint8_t.ptr);
    ptr.contents;
    ptr = ptr.increment()
  ) {
    len++;
  }

  let aryPtr = cast(input, uint8_t.array(len).ptr);
  return new Uint8Array(aryPtr.contents);
}

var SubprocessUnix = {
  Process,

  call(options) {
    return Process.create(options);
  },

  *getEnvironment() {
    let environ;
    if (Services.appinfo.OS === "Darwin") {
      environ = libc._NSGetEnviron().contents;
    } else {
      environ = libc.environ;
    }

    const EQUAL = "=".charCodeAt(0);
    let decoder = new TextDecoder("utf-8", { fatal: true });

    function decode(array) {
      try {
        return decoder.decode(array);
      } catch (e) {
        return array;
      }
    }

    for (
      let envp = environ;
      !envp.isNull() && !envp.contents.isNull();
      envp = envp.increment()
    ) {
      let buf = ptrToUint8Array(envp.contents);

      for (let i = 0; i < buf.length; i++) {
        if (buf[i] == EQUAL) {
          yield [decode(buf.subarray(0, i)), decode(buf.subarray(i + 1))];
          break;
        }
      }
    }
  },

  isExecutableFile: async function isExecutable(path) {
    if (!PathUtils.isAbsolute(path)) {
      return false;
    }

    try {
      let info = await IOUtils.stat(path);

      // FIXME: We really want access(path, X_OK) here, but IOUtils does not
      // support it.
      return info.type !== "directory" && info.permissions & 0o111;
    } catch (e) {
      return false;
    }
  },

  /**
   * Searches for the given executable file in the system executable
   * file paths as specified by the PATH environment variable.
   *
   * On Windows, if the unadorned filename cannot be found, the
   * extensions in the semicolon-separated list in the PATHEXT
   * environment variable are successively appended to the original
   * name and searched for in turn.
   *
   * @param {string} bin
   *        The name of the executable to find.
   * @param {object} environment
   *        An object containing a key for each environment variable to be used
   *        in the search.
   * @returns {Promise<string>}
   */
  async pathSearch(bin, environment) {
    if (PathUtils.isAbsolute(bin)) {
      if (await this.isExecutableFile(bin)) {
        return bin;
      }
      let error = new Error(
        `File at path "${bin}" does not exist, or is not executable`
      );
      error.errorCode = SubprocessConstants.ERROR_BAD_EXECUTABLE;
      throw error;
    }

    let dirs = [];
    if (typeof environment.PATH === "string") {
      dirs = environment.PATH.split(":");
    }

    for (let dir of dirs) {
      let path = PathUtils.join(dir, bin);

      if (await this.isExecutableFile(path)) {
        return path;
      }
    }
    let error = new Error(`Executable not found: ${bin}`);
    error.errorCode = SubprocessConstants.ERROR_BAD_EXECUTABLE;
    throw error;
  },

  connectRunning(options) {
    return Process.fromRunning(options);
  },
};

export var SubprocessImpl = SubprocessUnix;
