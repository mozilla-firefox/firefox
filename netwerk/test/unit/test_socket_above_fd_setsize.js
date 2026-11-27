/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// NSPR emulates poll() with select() on macOS, so a socket whose descriptor
// number is at or above the FD_SETSIZE the emulation was built with cannot be
// polled. Occupy every descriptor below HIGH_FD, so that new sockets get
// numbers above it, and check that a request still goes through.

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
const { NetUtil: NetUtilModule } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);
const { ctypes } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs"
);

// Above the 4096 that used to be the ceiling, below the 65536 it is now.
const HIGH_FD = 4500;

add_task(async function test_socket_above_old_fd_setsize() {
  const libc = ctypes.open("libSystem.B.dylib");
  const rlimit = new ctypes.StructType("rlimit", [
    { cur: ctypes.uint64_t },
    { max: ctypes.uint64_t },
  ]);
  // The port sits at offset 2 in both sockaddr_in and sockaddr_in6, in
  // network byte order.
  const sockaddr = new ctypes.StructType("sockaddr_storage", [
    { ss_len: ctypes.uint8_t },
    { ss_family: ctypes.uint8_t },
    { port: ctypes.ArrayType(ctypes.uint8_t, 2) },
    { ss_pad: ctypes.ArrayType(ctypes.uint8_t, 124) },
  ]);
  const getrlimit = libc.declare(
    "getrlimit",
    ctypes.default_abi,
    ctypes.int,
    ctypes.int,
    rlimit.ptr
  );
  const setrlimit = libc.declare(
    "setrlimit",
    ctypes.default_abi,
    ctypes.int,
    ctypes.int,
    rlimit.ptr
  );
  const dup = libc.declare("dup", ctypes.default_abi, ctypes.int, ctypes.int);
  const close = libc.declare(
    "close",
    ctypes.default_abi,
    ctypes.int,
    ctypes.int
  );
  const getsockname = libc.declare(
    "getsockname",
    ctypes.default_abi,
    ctypes.int,
    ctypes.int,
    sockaddr.ptr,
    ctypes.uint32_t.ptr
  );
  const RLIMIT_NOFILE = 8;
  const RLIM_INFINITY = ctypes.UInt64("0x7fffffffffffffff");
  const AF_INET = 2;
  const AF_INET6 = 30;

  let filled = [];
  let server = null;
  try {
    let limit = new rlimit();
    Assert.equal(getrlimit(RLIMIT_NOFILE, limit.address()), 0, "getrlimit");
    const needed = HIGH_FD + 64;
    if (ctypes.UInt64.compare(limit.cur, ctypes.UInt64(needed)) < 0) {
      if (
        ctypes.UInt64.compare(limit.max, RLIM_INFINITY) != 0 &&
        ctypes.UInt64.compare(limit.max, ctypes.UInt64(needed)) < 0
      ) {
        info(`Hard descriptor limit ${limit.max} is below ${needed}, skipping`);
        return;
      }
      limit.cur = ctypes.UInt64(needed);
      Assert.equal(
        setrlimit(RLIMIT_NOFILE, limit.address()),
        0,
        `Raise the soft descriptor limit to ${needed}`
      );
    }

    // dup() returns the lowest free descriptor, so once it hands out HIGH_FD
    // every descriptor below it is in use.
    let fd = dup(0);
    while (fd != -1 && fd < HIGH_FD) {
      filled.push(fd);
      fd = dup(0);
    }
    Assert.greaterOrEqual(
      fd,
      HIGH_FD,
      `Occupied every descriptor below ${HIGH_FD}`
    );
    close(fd);

    server = new HttpServer();
    server.registerPathHandler("/hello", (request, response) => {
      response.setHeader("Content-Type", "text/plain");
      response.write("hello");
    });
    server.start(-1);

    let result = await new Promise(resolve => {
      NetUtilModule.asyncFetch(
        {
          uri: `http://localhost:${server.identity.primaryPort}/hello`,
          loadUsingSystemPrincipal: true,
        },
        (stream, status, request) => {
          let body = "";
          let httpStatus = -1;
          if (Components.isSuccessCode(status)) {
            httpStatus = request.QueryInterface(
              Ci.nsIHttpChannel
            ).responseStatus;
            body = NetUtilModule.readInputStreamToString(
              stream,
              stream.available()
            );
          }
          resolve({ status, httpStatus, body });
        }
      );
    });

    Assert.equal(result.status, Cr.NS_OK, "The request succeeded");
    Assert.equal(result.httpStatus, 200, "The server answered");
    Assert.equal(result.body, "hello", "The body arrived");

    // The listening socket was created after the fill and stays open until
    // server.stop(), so its descriptor shows where new sockets land.
    const port = server.identity.primaryPort;
    let listenFd = -1;
    for (let i = HIGH_FD; i < HIGH_FD + 64 && listenFd == -1; i++) {
      let addr = new sockaddr();
      let len = ctypes.uint32_t(sockaddr.size);
      if (
        getsockname(i, addr.address(), len.address()) == 0 &&
        (addr.ss_family == AF_INET || addr.ss_family == AF_INET6) &&
        addr.port[0] * 256 + addr.port[1] == port
      ) {
        listenFd = i;
      }
    }
    Assert.greaterOrEqual(
      listenFd,
      HIGH_FD,
      `The server listens on a descriptor at or above ${HIGH_FD}`
    );
  } finally {
    if (server) {
      await new Promise(resolve => server.stop(resolve));
    }
    for (let fd of filled) {
      close(fd);
    }
    libc.close();
  }
});
