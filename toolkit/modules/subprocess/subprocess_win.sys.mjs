/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  BaseProcess,
  PromiseWorker,
} from "resource://gre/modules/subprocess/subprocess_common.sys.mjs";

import { ctypes } from "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs";

var obj = { ctypes };
Services.scriptloader.loadSubScript(
  "resource://gre/modules/subprocess/subprocess_shared.js",
  obj
);
Services.scriptloader.loadSubScript(
  "resource://gre/modules/subprocess/subprocess_shared_win.js",
  obj
);

const { SubprocessConstants } = obj;

// libc and win32 are exported for tests.
export const libc = obj.libc;
export const win32 = obj.win32;

class WinPromiseWorker extends PromiseWorker {
  constructor(...args) {
    super(...args);

    // Used by the worker thread to block until any I/O completes. Used on this
    // side to unblock the worker to receive messages from postMessage below.
    const iocp = libc.CreateIoCompletionPort(
      win32.INVALID_HANDLE_VALUE,
      win32.NULL_HANDLE_VALUE,
      0,
      1 // The worker thread is the only consumer of IOCP.
    );
    if (!iocp) {
      throw new Error(`Failed to create IOCP: ${ctypes.winLastError}`);
    }
    // Wrap in Handle to ensure that CloseHandle is called after worker exits.
    this.iocpHandle = win32.Handle(iocp);

    this.call("init", [
      {
        comspec: Services.env.get("COMSPEC"),
        iocpCompletionPort: String(ctypes.cast(iocp, ctypes.uintptr_t).value),
      },
    ]);
  }

  signalWorker() {
    libc.PostQueuedCompletionStatus(
      this.iocpHandle,
      0,
      win32.IOCP_COMPLETION_KEY_WAKE_WORKER,
      win32.OVERLAPPED.ptr(0)
    );
  }

  postMessage(...args) {
    this.signalWorker();
    return super.postMessage(...args);
  }
}

class Process extends BaseProcess {
  static get WORKER_URL() {
    return "resource://gre/modules/subprocess/subprocess_win.worker.js";
  }

  static get WorkerClass() {
    return WinPromiseWorker;
  }
}

var SubprocessWin = {
  Process,

  call(options) {
    return Process.create(options);
  },

  *getEnvironment() {
    let env = libc.GetEnvironmentStringsW();
    try {
      for (let p = env, q = env; ; p = p.increment()) {
        if (p.contents == "\0") {
          if (String(p) == String(q)) {
            break;
          }

          let str = q.readString();
          q = p.increment();

          let idx = str.indexOf("=");
          if (idx == 0) {
            idx = str.indexOf("=", 1);
          }

          if (idx >= 0) {
            yield [str.slice(0, idx), str.slice(idx + 1)];
          }
        }
      }
    } finally {
      libc.FreeEnvironmentStringsW(env);
    }
  },

  async isExecutableFile(path) {
    if (!PathUtils.isAbsolute(path)) {
      return false;
    }

    try {
      let info = await IOUtils.stat(path);
      // On Windows, a FileType of "other" indicates it is a reparse point
      // (i.e., a link).
      return info.type !== "directory" && info.type !== "other";
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
        `File at path "${bin}" does not exist, or is not a normal file`
      );
      error.errorCode = SubprocessConstants.ERROR_BAD_EXECUTABLE;
      throw error;
    }

    let dirs = [];
    let exts = [];
    if (environment.PATH) {
      dirs = environment.PATH.split(";");
    }
    if (environment.PATHEXT) {
      exts = environment.PATHEXT.split(";");
    }

    for (let dir of dirs) {
      let path = PathUtils.join(dir, bin);

      if (await this.isExecutableFile(path)) {
        return path;
      }

      for (let ext of exts) {
        let file = path + ext;

        if (await this.isExecutableFile(file)) {
          return file;
        }
      }
    }
    let error = new Error(`Executable not found: ${bin}`);
    error.errorCode = SubprocessConstants.ERROR_BAD_EXECUTABLE;
    throw error;
  },

  connectRunning(_options) {
    // Not relevant (yet?) on Windows. This is currently used only on Unix
    // for native messaging through the WebExtensions portal.
    throw new Error("Not implemented");
  },
};

export var SubprocessImpl = SubprocessWin;
