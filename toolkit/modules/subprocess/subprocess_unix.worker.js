/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

/* exported Process, ManagedProcess */

/* import-globals-from subprocess_shared.js */
/* import-globals-from subprocess_shared_unix.js */
/* import-globals-from subprocess_worker_common.js */
importScripts(
  "resource://gre/modules/subprocess/subprocess_shared.js",
  "resource://gre/modules/subprocess/subprocess_shared_unix.js",
  "resource://gre/modules/subprocess/subprocess_worker_common.js"
);

const POLL_TIMEOUT = 5000;

let io;

let nextPipeId = 0;

class Pipe extends BasePipe {
  constructor(process, fd) {
    super();

    this.process = process;
    this.fd = fd;
    this.id = nextPipeId++;
  }

  get pollEvents() {
    throw new Error("Not implemented");
  }

  /**
   * Closes the file descriptor.
   *
   * @param {boolean} [force=false]
   *        If true, the file descriptor is closed immediately. If false, the
   *        file descriptor is closed after all current pending IO operations
   *        have completed.
   *
   * @returns {Promise<void>}
   *          Resolves when the file descriptor has been closed.
   */
  close(force = false) {
    if (!force && this.pending.length) {
      this.closing = true;
      return this.closedPromise;
    }

    for (let { reject } of this.pending) {
      let error = new Error("File closed");
      error.errorCode = SubprocessConstants.ERROR_END_OF_FILE;
      reject(error);
    }
    this.pending.length = 0;

    if (!this.closed) {
      this.fd.dispose();

      this.closed = true;
      this.resolveClosed();

      io.pipes.delete(this.id);
      io.updatePollFds();
    }
    return this.closedPromise;
  }

  /**
   * Called when an error occurred while polling our file descriptor.
   */
  onError() {
    this.close(true);
    this.process.wait();
  }
}

class InputPipe extends Pipe {
  /**
   * A bit mask of poll() events which we currently wish to be notified of on
   * this file descriptor.
   */
  get pollEvents() {
    if (this.pending.length) {
      return LIBC.POLLIN;
    }
    return 0;
  }

  /**
   * Asynchronously reads at most `length` bytes of binary data from the file
   * descriptor into an ArrayBuffer of the same size. Returns a promise which
   * resolves when the operation is complete.
   *
   * @param {integer} length
   *        The number of bytes to read.
   *
   * @returns {Promise<ArrayBuffer>}
   */
  read(length) {
    if (this.closing || this.closed) {
      throw new Error("Attempt to read from closed pipe");
    }

    return new Promise((resolve, reject) => {
      this.pending.push({ resolve, reject, length });
      io.updatePollFds();
    });
  }

  /**
   * Synchronously reads at most `count` bytes of binary data into an
   * ArrayBuffer, and returns that buffer. If no data can be read without
   * blocking, returns null instead.
   *
   * @param {integer} count
   *        The number of bytes to read.
   *
   * @returns {ArrayBuffer|null}
   */
  readBuffer(count) {
    let buffer = new ArrayBuffer(count);

    let read = +libc.read(this.fd, buffer, buffer.byteLength);
    if (read < 0 && ctypes.errno != LIBC.EAGAIN) {
      this.onError();
    }

    if (read <= 0) {
      return null;
    }

    if (read < buffer.byteLength) {
      return ArrayBuffer_transfer(buffer, read);
    }

    return buffer;
  }

  /**
   * Called when one of the IO operations matching the `pollEvents` mask may be
   * performed without blocking.
   *
   * @returns {boolean}
   *        True if any data was successfully read.
   */
  onReady() {
    let result = false;
    let reads = this.pending;
    while (reads.length) {
      let { resolve, length } = reads[0];

      let buffer = this.readBuffer(length);
      if (buffer) {
        result = true;
        this.shiftPending();
        resolve(buffer);
      } else {
        break;
      }
    }

    if (!reads.length) {
      io.updatePollFds();
    }
    return result;
  }
}

class OutputPipe extends Pipe {
  /**
   * A bit mask of poll() events which we currently wish to be notified of on
   * this file discriptor.
   */
  get pollEvents() {
    if (this.pending.length) {
      return LIBC.POLLOUT;
    }
    return 0;
  }

  /**
   * Asynchronously writes the given buffer to our file descriptor, and returns
   * a promise which resolves when the operation is complete.
   *
   * @param {ArrayBuffer} buffer
   *        The buffer to write.
   *
   * @returns {Promise<integer>}
   *          Resolves to the number of bytes written when the operation is
   *          complete.
   */
  write(buffer) {
    if (this.closing || this.closed) {
      throw new Error("Attempt to write to closed pipe");
    }

    return new Promise((resolve, reject) => {
      this.pending.push({ resolve, reject, buffer, length: buffer.byteLength });
      io.updatePollFds();
    });
  }

  /**
   * Attempts to synchronously write the given buffer to our file descriptor.
   * Writes only as many bytes as can be written without blocking, and returns
   * the number of byes successfully written.
   *
   * Closes the file descriptor if an IO error occurs.
   *
   * @param {ArrayBuffer} buffer
   *        The buffer to write.
   *
   * @returns {integer}
   *          The number of bytes successfully written.
   */
  writeBuffer(buffer) {
    let bytesWritten = libc.write(this.fd, buffer, buffer.byteLength);

    if (bytesWritten < 0 && ctypes.errno != LIBC.EAGAIN) {
      this.onError();
    }

    return bytesWritten;
  }

  /**
   * Called when one of the IO operations matching the `pollEvents` mask may be
   * performed without blocking.
   */
  onReady() {
    let writes = this.pending;
    while (writes.length) {
      let { buffer, resolve, length } = writes[0];

      let written = this.writeBuffer(buffer);

      if (written == buffer.byteLength) {
        resolve(length);
        this.shiftPending();
      } else if (written > 0) {
        writes[0].buffer = buffer.slice(written);
      } else {
        break;
      }
    }

    if (!writes.length) {
      io.updatePollFds();
    }
  }
}

class Signal {
  constructor(fd) {
    this.fd = fd;
  }

  cleanup() {
    libc.close(this.fd);
    this.fd = null;
  }

  get pollEvents() {
    return LIBC.POLLIN;
  }

  /**
   * Called when an error occurred while polling our file descriptor.
   */
  onError() {
    io.shutdown();
  }

  /**
   * Called when one of the IO operations matching the `pollEvents` mask may be
   * performed without blocking.
   */
  onReady() {
    let buffer = new ArrayBuffer(16);
    let count = +libc.read(this.fd, buffer, buffer.byteLength);
    if (count > 0) {
      io.messageCount += count;
    }
  }
}

class Process extends BaseProcess {
  /**
   * Each Process object opens an additional pipe from the target object, which
   * will be automatically closed when the process exits, but otherwise
   * carries no data.
   *
   * This property contains a bit mask of poll() events which we wish to be
   * notified of on this descriptor. We're not expecting any input from this
   * pipe, but we need to poll for input until the process exits in order to be
   * notified when the pipe closes.
   */
  get pollEvents() {
    if (this.exitCode === null) {
      return LIBC.POLLIN;
    }
    return 0;
  }

  /**
   * Kills the process with the given signal.
   *
   * @param {integer} signal
   */
  kill(signal) {
    libc.kill(this.pid, signal);
    this.wait();
  }

  /**
   * Initializes the IO pipes for use as standard input, output, and error
   * descriptors in the spawned process.
   *
   * @param {object} options
   *        The Subprocess options object for this process.
   * @returns {unix.Fd[]}
   *          The array of file descriptors belonging to the spawned process.
   */
  initPipes(options) {
    let stderr = options.stderr;

    let our_pipes = [];
    let their_pipes = new Map();

    let pipe = input => {
      let fds = ctypes.int.array(2)();

      let res = libc.pipe(fds);
      if (res == -1) {
        throw new Error("Unable to create pipe");
      }

      fds = Array.from(fds, unix.Fd);

      if (input) {
        fds.reverse();
      }

      if (input) {
        our_pipes.push(new InputPipe(this, fds[1]));
      } else {
        our_pipes.push(new OutputPipe(this, fds[1]));
      }

      libc.fcntl(fds[0], LIBC.F_SETFD, ctypes.int(LIBC.FD_CLOEXEC));
      libc.fcntl(fds[1], LIBC.F_SETFD, ctypes.int(LIBC.FD_CLOEXEC));
      libc.fcntl(fds[1], LIBC.F_SETFL, ctypes.int(LIBC.O_NONBLOCK));

      return fds[0];
    };

    their_pipes.set(0, pipe(false));
    their_pipes.set(1, pipe(true));

    if (stderr == "pipe") {
      their_pipes.set(2, pipe(true));
    } else if (stderr == "stdout") {
      their_pipes.set(2, their_pipes.get(1));
    }

    // Create an additional pipe that we can use to monitor for process exit.
    their_pipes.set(3, pipe(true));
    this.fd = our_pipes.pop().fd;

    this.pipes = our_pipes;

    return their_pipes;
  }

  spawn(options) {
    let fds = this.initPipes(options);

    let launchOptions = {
      environment: options.environment,
      disclaim: options.disclaim,
      fdMap: [],
    };

    // Check for truthiness to avoid chdir("null")
    if (options.workdir) {
      launchOptions.workdir = options.workdir;
    }

    for (let [dst, src] of fds.entries()) {
      launchOptions.fdMap.push({ src, dst });
    }

    try {
      this.pid = IOUtils.launchProcess(options.arguments, launchOptions);
    } catch (e) {
      // No child holds the other ends, so close ours right away rather than
      // leaving them to the garbage collector.
      for (let pipe of this.pipes) {
        pipe.close();
      }
      this.fd.dispose();
      // The exception from IOUtils cannot be sent to the main thread, so report
      // a plain error that callers can inspect.
      let error = new Error(`Failed to launch process: ${e.message || e}`);
      error.errorCode = SubprocessConstants.ERROR_BAD_EXECUTABLE;
      throw error;
    } finally {
      for (let fd of new Set(fds.values())) {
        fd.dispose();
      }
    }
  }

  /**
   * Called when input is available on our sentinel file descriptor.
   *
   * @see pollEvents
   */
  onReady() {
    // We're not actually expecting any input on this pipe. If we get any, we
    // can't poll the pipe any further without reading it.
    if (this.wait() == undefined) {
      this.kill(9);
    }
  }

  /**
   * Called when an error occurred while polling our sentinel file descriptor.
   *
   * @see pollEvents
   */
  onError() {
    this.wait();
  }

  /**
   * Attempts to wait for the process's exit status, without blocking. If
   * successful, resolves the `exitPromise` to the process's exit value.
   *
   * @returns {integer|null}
   *          The process's exit status, if it has already exited.
   */
  wait() {
    if (this.exitCode !== null) {
      return this.exitCode;
    }

    let status = ctypes.int();

    let res = libc.waitpid(this.pid, status.address(), LIBC.WNOHANG);
    // If there's a failure here and we get any errno other than EINTR, it
    // means that the process has been reaped by another thread (most likely
    // the nspr process wait thread), and its actual exit status is not
    // available to us. In that case, we have to assume success.
    if (res == 0 || (res == -1 && ctypes.errno == LIBC.EINTR)) {
      return null;
    }

    let sig = unix.WTERMSIG(status.value);
    if (sig) {
      this.exitCode = -sig;
    } else {
      this.exitCode = unix.WEXITSTATUS(status.value);
    }

    this.fd.dispose();
    io.updatePollFds();
    this.resolveExit(this.exitCode);
    return this.exitCode;
  }
}

/*
 * Wrapping file descriptors of already-running process to allow interacting
 * with the process via the Subprocess module.
 *
 * This is used e.g. by NativeMessaging code when interfacing with XDG
 * WebExtensions portal (required when running under Flatpak or Snap). The
 * actual portal binary is executed outside and file descriptors will be shared
 * over DBus so interaction can happen with the portal.
 *
 * The file descriptors are wrapped in a unix.Fd() to ensure proper cleanup as
 * long as the ManagedProcess instance is properly disposed off: kill() should
 * ensure this is the case.
 *
 * All the file descriptors will be monitored by poll() to try and detect
 * process termination.
 * */
class ManagedProcess extends BaseProcess {
  /**
   * Connect to an already running process that was spawned externally,
   * through numeric stdin/stdout/stderr file descriptors.
   *
   * @param {number[]} receivedFDs
   *        An array of file descriptors (stdin, stdout and stderr).
   */
  connectRunning(receivedFDs) {
    const fdCheck = fds => {
      for (let value of io.pipes.values()) {
        const fd = parseInt(value.fd.toString(), 10);
        return fd === fds[0] || fd === fds[1] || fd === fds[2];
      }
    };

    const alreadyUsed = fdCheck(receivedFDs);
    if (alreadyUsed) {
      throw new Error("Attempt to connect FDs already handled by Subprocess");
    }

    this.pipes.push(new OutputPipe(this, unix.Fd(receivedFDs[0])));
    this.pipes.push(new InputPipe(this, unix.Fd(receivedFDs[1])));
    this.pipes.push(new InputPipe(this, unix.Fd(receivedFDs[2])));
  }

  get pollEvents() {
    // No poll fd here: we don't have a handle to the process, only its fd.
    // Each fd of this.pipes is already polled by updatePollFds, and their
    // Pipe's onError() method calls our wait() method when the fd is closed.
    // We assume that the process has exited when all pipes have closed.

    // ManagedProcess does not have onReady() or onError() definitions because
    // updatePollFds() does not call these when pollEvents is 0.
    return 0;
  }

  /*
   * Termination of the ManagedProcess: such a process is started/stopped
   * outside of the browser, the termination here ensures that all its pipes are
   * properly closed and that any code waiting for the process to terminate is
   * unblocked by resolving the Promise.
   * */
  kill() {
    this.pipes.forEach(p => p.close());
    this.resolveExit(this.exitCode);
  }

  /* A ManagedProcess being ran outside of our PID namespace (Snap/Flatpak)
   * there is no realistic way to waitpid() here.
   * As noted in pollEvents, we consider the process closed if all of its fd
   * have closed.
   * */
  wait() {
    if (this.pipes.every(pipe => pipe.closed)) {
      // Actual exitCode is unknown, just return null.
      this.resolveExit(null);
    } else {
      io.updatePollFds();
    }
  }

  /**
   * A ManagedProcess is already running, so here the spawn just performs the
   * connection of the file descriptors received.
   *
   * @param {Array} options
   *        An array of file descriptors from an existing process.
   */
  spawn(options) {
    return this.connectRunning(options);
  }
}

io = {
  pollFds: null,
  pollHandlers: null,

  pipes: new Map(),

  processes: new Map(),

  messageCount: 0,

  running: true,

  polling: false,

  init(details) {
    this.signal = new Signal(details.signalFd);
    this.updatePollFds();

    setTimeout(this.loop.bind(this), 0);
  },

  shutdown() {
    if (this.running) {
      this.running = false;

      this.signal.cleanup();
      this.signal = null;

      self.postMessage({ msg: "close" });
      self.close();
    }
  },

  getPipe(pipeId) {
    let pipe = this.pipes.get(pipeId);

    if (!pipe) {
      let error = new Error("File closed");
      error.errorCode = SubprocessConstants.ERROR_END_OF_FILE;
      throw error;
    }
    return pipe;
  },

  getProcess(processId) {
    let process = this.processes.get(processId);

    if (!process) {
      throw new Error(`Invalid process ID: ${processId}`);
    }
    return process;
  },

  updatePollFds() {
    let handlers = [
      this.signal,
      ...this.pipes.values(),
      ...this.processes.values(),
    ];

    handlers = handlers.filter(handler => handler.pollEvents);

    // Our poll loop is only useful if we've got at least 1 thing to poll other than our own
    // signal.
    if (handlers.length == 1) {
      this.polling = false;
    } else if (!this.polling && this.running) {
      // Restart the poll loop if necessary:
      setTimeout(this.loop.bind(this), 0);
      this.polling = true;
    }

    let pollfds = unix.pollfd.array(handlers.length)();

    for (let [i, handler] of handlers.entries()) {
      let pollfd = pollfds[i];

      pollfd.fd = handler.fd;
      pollfd.events = handler.pollEvents;
      pollfd.revents = 0;
    }

    this.pollFds = pollfds;
    this.pollHandlers = handlers;
  },

  loop() {
    this.poll();
    if (this.running && this.polling) {
      setTimeout(this.loop.bind(this), 0);
    }
  },

  poll() {
    let handlers = this.pollHandlers;
    let pollfds = this.pollFds;

    let timeout = this.messageCount > 0 ? 0 : POLL_TIMEOUT;
    let count = libc.poll(pollfds, pollfds.length, timeout);

    for (let i = 0; count && i < pollfds.length; i++) {
      let pollfd = pollfds[i];
      if (pollfd.revents) {
        count--;

        let handler = handlers[i];
        try {
          let success = false;
          if (pollfd.revents & handler.pollEvents) {
            success = handler.onReady();
          }
          // Only call the error handler in this iteration if we didn't also
          // have a success. This is necessary because Linux systems set POLLHUP
          // on a pipe when it's closed but there's still buffered data to be
          // read, and Darwin sets POLLIN and POLLHUP on a closed pipe, even
          // when there's no data to be read.
          if (
            !success &&
            pollfd.revents & (LIBC.POLLERR | LIBC.POLLHUP | LIBC.POLLNVAL)
          ) {
            handler.onError();
          }
        } catch (e) {
          console.error(e);
          debug(`Worker error: ${e} :: ${e.stack}`);
          handler.onError();
        }

        pollfd.revents = 0;
      }
    }
  },

  addProcess(process) {
    this.processes.set(process.id, process);

    for (let pipe of process.pipes) {
      this.pipes.set(pipe.id, pipe);
    }
  },

  cleanupProcess(process) {
    this.processes.delete(process.id);
  },
};
