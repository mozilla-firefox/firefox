/* -*- mode: js; indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { EventEmitter } from "resource://gre/modules/EventEmitter.sys.mjs";
import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

// Hard limits on maximum message size that can be read/written
// These are defined in the native messaging documentation, note that
// the write limit is imposed by the "wire protocol" in which message
// boundaries are defined by preceding each message with its length as
// 4-byte unsigned integer so this is the largest value that can be
// represented.  Good luck generating a serialized message that large,
// the practical write limit is likely to be dictated by available memory.
const MAX_READ = 1024 * 1024;
const MAX_WRITE = 0xffffffff;

// Preferences that can lower the message size limits above,
// used for testing the limits.
const PREF_MAX_READ = "webextensions.native-messaging.max-input-message-bytes";
const PREF_MAX_WRITE =
  "webextensions.native-messaging.max-output-message-bytes";

const lazy = XPCOMUtils.declareLazy({
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  NativeManifests: "resource://gre/modules/NativeManifests.sys.mjs",
  Subprocess: "resource://gre/modules/Subprocess.sys.mjs",
  maxRead: { pref: PREF_MAX_READ, default: MAX_READ },
  maxWrite: { pref: PREF_MAX_WRITE, default: MAX_WRITE },
  portal: {
    service: "@mozilla.org/extensions/native-messaging-portal;1",
    iid: Ci.nsINativeMessagingPortal,
  },
});

const { ExtensionError, promiseTimeout } = ExtensionUtils;

// For a graceful shutdown (i.e., when the extension is unloaded or when it
// explicitly calls disconnect() on a native port), how long we give the native
// application to exit before we start trying to kill it.  (in milliseconds)
const GRACEFUL_SHUTDOWN_TIME = 3000;

export class NativeApp extends EventEmitter {
  _throwGenericError(application) {
    // Report a generic error to not leak information about whether a native
    // application is installed to addons that do not have the right permission.
    throw new ExtensionError(`No such native application ${application}`);
  }

  /**
   * @param {BaseContext} context The context that initiated the native app.
   * @param {string} application The identifier of the native app.
   */
  constructor(context, application) {
    super();

    this.context = context;
    this.name = application;

    // We want a close() notification when the window is destroyed.
    this.context.callOnClose(this);

    this.proc = null;
    this.readPromise = null;
    this.sendQueue = [];
    this.writePromise = null;
    this.cleanupStarted = false;
    this.portalSessionHandle = null;

    if ("@mozilla.org/extensions/native-messaging-portal;1" in Cc) {
      if (lazy.portal.shouldUse()) {
        this.startupPromise = this._doInitPortal().catch(err => {
          this.startupPromise = null;
          Cu.reportError(err instanceof Error ? err : err.message);
          this._cleanup(err);
        });
        return;
      }
    }

    this.startupPromise = lazy.NativeManifests.lookupManifest(
      "stdio",
      application,
      context
    )
      .then(hostInfo => {
        if (!hostInfo) {
          this._throwGenericError(application);
        }

        let command = hostInfo.manifest.path;
        if (AppConstants.platform == "win") {
          // Normalize in case the extension used / instead of \.
          command = command.replaceAll("/", "\\");

          // Relative paths are only supported on Windows. On Linux and macOS,
          // _tryPath in NativeManifests.sys.mjs enforces that the command path
          // is absolute.
          if (!PathUtils.isAbsolute(command)) {
            // Note: hostInfo.path is an absolute path to the manifest.
            const parentPath = PathUtils.parent(
              hostInfo.path.replaceAll("/", "\\")
            );
            // PathUtils.joinRelative cannot be used because it throws for "..".
            // but command is allowed to contain ".." to traverse the directory.
            command = `${parentPath}\\${command}`;
          }
        }

        let subprocessOpts = {
          command: command,
          arguments: [hostInfo.path, context.extension.id],
          workdir: PathUtils.parent(command),
          stderr: "pipe",
          disclaim: true,
        };

        return lazy.Subprocess.call(subprocessOpts);
      })
      .then(proc => {
        this.startupPromise = null;
        this.proc = proc;
        this._startRead();
        this._startWrite();
        this._startStderrRead();
      })
      .catch(err => {
        this.startupPromise = null;
        Cu.reportError(err instanceof Error ? err : err.message);
        this._cleanup(err);
      });
  }

  async _doInitPortal() {
    let available = await lazy.portal.available;
    if (!available) {
      Cu.reportError("Native messaging portal is not available");
      this._throwGenericError(this.name);
    }

    let handle = await lazy.portal.createSession(this.name);
    this.portalSessionHandle = handle;

    let hostInfo = null;
    let path;
    try {
      let manifest = await lazy.portal.getManifest(
        handle,
        this.name,
        this.context.extension.id
      );
      path = manifest.substring(0, 30) + "...";
      hostInfo = await lazy.NativeManifests.parseManifest(
        "stdio",
        path,
        this.name,
        this.context,
        JSON.parse(manifest)
      );
    } catch (ex) {
      if (ex instanceof SyntaxError && ex.message.startsWith("JSON.parse:")) {
        Cu.reportError(`Error parsing native manifest ${path}: ${ex.message}`);
        this._throwGenericError(this.name);
      }
    }
    if (!hostInfo) {
      this._throwGenericError(this.name);
    }

    let pipes;
    try {
      pipes = await lazy.portal.start(
        handle,
        this.name,
        this.context.extension.id
      );
    } catch (err) {
      if (err.name == "NotFoundError") {
        this._throwGenericError(this.name);
      } else {
        throw err;
      }
    }
    this.proc = await lazy.Subprocess.connectRunning([
      pipes.stdin,
      pipes.stdout,
      pipes.stderr,
    ]);
    this.startupPromise = null;
    this._startRead();
    this._startWrite();
    this._startStderrRead();
  }

  /**
   * Open a connection to a native messaging host.
   *
   * @param {number} portId A unique internal ID that identifies the port.
   * @param {import("ExtensionParent.sys.mjs").NativeMessenger} port Parent NativeMessenger used to send messages.
   * @returns {import("ExtensionParent.sys.mjs").ParentPort}
   */
  onConnect(portId, port) {
    // eslint-disable-next-line
    this.on("message", (_, message) => {
      port.sendPortMessage(
        portId,
        new StructuredCloneHolder(
          `NativeMessaging/onConnect/${this.name}`,
          null,
          message
        )
      );
    });
    this.once("disconnect", (_, error) => {
      port.sendPortDisconnect(portId, error && new ClonedErrorHolder(error));
    });
    return {
      onPortMessage: holder => this.send(holder),
      onPortDisconnect: () => this.close(),
    };
  }

  /**
   * @param {BaseContext} context The scope from where `message` originates.
   * @param {*} message A message from the extension, meant for a native app.
   * @returns {ArrayBufferLike} An ArrayBuffer that can be sent to the native app.
   */
  static encodeMessage(context, message) {
    message = context.jsonStringify(message);
    let buffer = new TextEncoder().encode(message).buffer;
    if (buffer.byteLength > lazy.maxWrite) {
      throw new context.Error("Write too big");
    }
    return buffer;
  }

  // A port is definitely "alive" if this.proc is non-null.  But we have
  // to provide a live port object immediately when connecting so we also
  // need to consider a port alive if proc is null but the startupPromise
  // is still pending.
  get _isDisconnected() {
    return !this.proc && !this.startupPromise;
  }

  _startRead() {
    if (this.readPromise) {
      throw new Error("Entered _startRead() while readPromise is non-null");
    }
    this.readPromise = this.proc.stdout
      .readUint32()
      .then(len => {
        if (len > lazy.maxRead) {
          throw new ExtensionError(
            `Native application tried to send a message of ${len} bytes, which exceeds the limit of ${lazy.maxRead} bytes.`
          );
        }
        return this.proc.stdout.readJSON(len);
      })
      .then(msg => {
        this.emit("message", msg);
        this.readPromise = null;
        this._startRead();
      })
      .catch(err => {
        if (err.errorCode != lazy.Subprocess.ERROR_END_OF_FILE) {
          Cu.reportError(err instanceof Error ? err : err.message);
        }
        this._cleanup(err);
      });
  }

  _startWrite() {
    if (!this.sendQueue.length) {
      return;
    }

    if (this.writePromise) {
      throw new Error("Entered _startWrite() while writePromise is non-null");
    }

    let buffer = this.sendQueue.shift();
    let uintArray = Uint32Array.of(buffer.byteLength);

    this.writePromise = Promise.all([
      this.proc.stdin.write(uintArray.buffer),
      this.proc.stdin.write(buffer),
    ])
      .then(() => {
        this.writePromise = null;
        this._startWrite();
      })
      .catch(err => {
        Cu.reportError(err.message);
        this._cleanup(err);
      });
  }

  _startStderrRead() {
    let proc = this.proc;
    let app = this.name;
    (async function () {
      let partial = "";
      while (true) {
        let data = await proc.stderr.readString();
        if (!data.length) {
          // We have hit EOF, just stop reading
          if (partial) {
            Services.console.logStringMessage(
              `stderr output from native app ${app}: ${partial}`
            );
          }
          break;
        }

        let lines = data.split(/\r?\n/);
        lines[0] = partial + lines[0];
        partial = lines.pop();

        for (let line of lines) {
          Services.console.logStringMessage(
            `stderr output from native app ${app}: ${line}`
          );
        }
      }
    })();
  }

  send(holder) {
    if (this._isDisconnected) {
      throw new ExtensionError("Attempt to postMessage on disconnected port");
    }
    let msg = holder.deserialize(globalThis);
    if (Cu.getClassName(msg, true) != "ArrayBuffer") {
      // This error cannot be triggered by extensions; it indicates an error in
      // our implementation.
      throw new Error(
        "The message to the native messaging host is not an ArrayBuffer"
      );
    }

    let buffer = msg;

    if (buffer.byteLength > lazy.maxWrite) {
      throw new ExtensionError("Write too big");
    }

    this.sendQueue.push(buffer);
    if (!this.startupPromise && !this.writePromise) {
      this._startWrite();
    }
  }

  // Shut down the native application and (by default) signal to the extension
  // that the connect has been disconnected.
  async _cleanup(err, fromExtension = false) {
    if (this.cleanupStarted) {
      return;
    }
    this.cleanupStarted = true;
    this.context.forgetOnClose(this);

    if (!fromExtension) {
      if (err && err.errorCode == lazy.Subprocess.ERROR_END_OF_FILE) {
        err = null;
      }
      this.emit("disconnect", err);
    }

    await this.startupPromise;

    if (this.portalSessionHandle) {
      if (this.writePromise) {
        await this.writePromise.catch(Cu.reportError);
      }
      // When using the WebExtensions portal, we don't control the external
      // process, the portal does. So let the portal handle waiting/killing the
      // external process as it sees fit.
      await lazy.portal
        .closeSession(this.portalSessionHandle)
        .catch(Cu.reportError);
      this.portalSessionHandle = null;
      this.proc?.kill();
      this.proc = null;
      return;
    }

    if (!this.proc) {
      // Failed to initialize proc in the constructor.
      return;
    }

    // To prevent an uncooperative process from blocking shutdown, we take the
    // following actions, and wait for GRACEFUL_SHUTDOWN_TIME in between.
    //
    // 1. Allow exit by closing the stdin pipe.
    // 2. Allow exit by a kill signal.
    // 3. Allow exit by forced kill signal.
    // 4. Give up and unblock shutdown despite the process still being alive.

    // Close the stdin stream and allow the process to exit on its own.
    // proc.wait() below will resolve once the process has exited gracefully.
    this.proc.stdin.close().catch(err => {
      if (err.errorCode != lazy.Subprocess.ERROR_END_OF_FILE) {
        Cu.reportError(err);
      }
    });
    let exitPromise = Promise.race([
      // 1. Allow the process to exit on its own after closing stdin.
      this.proc.wait().then(() => {
        this.proc = null;
      }),
      promiseTimeout(GRACEFUL_SHUTDOWN_TIME).then(() => {
        if (this.proc) {
          // 2. Kill the process gracefully. 3. Force kill after a timeout.
          this.proc.kill(GRACEFUL_SHUTDOWN_TIME);

          // 4. If the process is still alive after a kill + timeout followed
          // by a forced kill + timeout, give up and just resolve exitPromise.
          //
          // Note that waiting for just one interval is not enough, because the
          // `proc.kill()` is asynchronous, so we need to wait a bit after the
          // kill signal has been sent.
          return promiseTimeout(2 * GRACEFUL_SHUTDOWN_TIME);
        }
      }),
    ]);

    lazy.AsyncShutdown.profileBeforeChange.addBlocker(
      `Native Messaging: Wait for application ${this.name} to exit`,
      exitPromise
    );
  }

  // Called when the Context or Port is closed.
  close() {
    this._cleanup(null, true);
  }

  sendMessage(holder) {
    let responsePromise = new Promise((resolve, reject) => {
      this.once("message", (what, msg) => {
        resolve(msg);
      });
      this.once("disconnect", (what, err) => {
        reject(err);
      });
    });

    let result = this.startupPromise.then(() => {
      // Skip .send() if _cleanup() has been called already;
      // otherwise the error passed to _cleanup/"disconnect" would be hidden by the
      // "Attempt to postMessage on disconnected port" error from this.send().
      if (!this.cleanupStarted) {
        this.send(holder);
      }
      return responsePromise;
    });

    result.then(
      () => {
        this._cleanup();
      },
      () => {
        // Prevent the response promise from being reported as an
        // unchecked rejection if the startup promise fails.
        responsePromise.catch(() => {});

        this._cleanup();
      }
    );

    return result;
  }
}
