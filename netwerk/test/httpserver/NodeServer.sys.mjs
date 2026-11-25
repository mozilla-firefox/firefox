/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { NetUtil } from "resource://gre/modules/NetUtil.sys.mjs";

/* globals require, __dirname, global, Buffer, process */

class BaseNodeHTTPServerCode {
  static globalHandler(req, resp) {
    let path = new URL(req.url, "https://example.com").pathname;
    let handler = global.path_handlers[path];
    if (handler) {
      return handler(req, resp);
    }

    // Didn't find a handler for this path.
    let response = `<h1> 404 Path not found: ${path}</h1>`;
    resp.setHeader("Content-Type", "text/html");
    resp.setHeader("Content-Length", response.length);
    resp.writeHead(404);
    resp.end(response);
    return undefined;
  }
}

export class ADB {
  static async stopForwarding(port) {
    return this.forwardPort(port, true);
  }

  static async forwardPort(port, remove = false) {
    if (!process.env.MOZ_ANDROID_DATA_DIR) {
      // Not android, or we don't know how to do the forwarding
      return true;
    }
    // When creating a server on Android we must make sure that the port
    // is forwarded from the host machine to the emulator.
    let adb_path = "adb";
    if (process.env.MOZ_FETCHES_DIR) {
      adb_path = `${process.env.MOZ_FETCHES_DIR}/android-sdk-linux/platform-tools/adb`;
    }

    let command = `${adb_path} reverse tcp:${port} tcp:${port}`;
    if (remove) {
      command = `${adb_path} reverse --remove tcp:${port}`;
      return true;
    }

    try {
      await new Promise((resolve, reject) => {
        const { exec } = require("child_process");
        exec(command, (error, stdout, stderr) => {
          if (error) {
            console.log(`error: ${error.message}`);
            reject(error);
          } else if (stderr) {
            console.log(`stderr: ${stderr}`);
            reject(stderr);
          } else {
            // console.log(`stdout: ${stdout}`);
            resolve();
          }
        });
      });
    } catch (error) {
      console.log(`Command failed: ${error}`);
      return false;
    }

    return true;
  }

  // Binds both loopback addresses; a wildcard can share its port on macOS and Windows.
  static async listenOnLoopback(server, port) {
    const net = require("net");
    const { once } = require("events");
    server.listen(port, "127.0.0.1"); // Not awaitable; once() rejects on `error`.
    await once(server, "listening");
    const ipv6 = net.createServer(socket => server.emit("connection", socket));
    ipv6.listen(server.address().port, "::1");
    try {
      await once(ipv6, "listening");
    } catch (e) {
      if (e.code === "EADDRNOTAVAIL" || e.code === "EAFNOSUPPORT") {
        return; // No IPv6 loopback.
      }
      await new Promise(resolve => server.close(resolve));
      throw e;
    }
    server.once("close", () => ipv6.close());
  }

  static async listenAndForwardPort(server, port) {
    let retryCount = 0;
    const maxRetries = 10;

    while (retryCount < maxRetries) {
      try {
        await ADB.listenOnLoopback(server, port);
      } catch (e) {
        if (port || e.code !== "EADDRINUSE") {
          throw e;
        }
        retryCount++;
        console.log(`Port is taken on ::1 (${e}), retrying`);
        continue;
      }
      let serverPort = server.address().port;
      let res = await ADB.forwardPort(serverPort);

      if (res) {
        return serverPort;
      }

      retryCount++;
      console.log(
        `Port forwarding failed. Retrying (${retryCount}/${maxRetries})...`
      );
      server.close();
      // eslint-disable-next-line no-undef
      await new Promise(resolve => setTimeout(resolve, 500));
    }

    return -1;
  }
}

// Helper that runs in a forked node child. Generates an RSA leaf
// certificate covering the given DNS hostnames, signed by the same key
// the test CA (http2-ca.pem) uses (which is the key in
// testing/xpcshell/moz-http2/http2-cert.key — pykey's shared default).
// Uses only Node builtins: `crypto` and `fs`.
class CertGenCode {
  static derLen(n) {
    if (n < 0x80) {
      return Buffer.from([n]);
    }
    let bytes = [];
    let v = n;
    while (v > 0) {
      bytes.unshift(v & 0xff);
      v >>>= 8;
    }
    return Buffer.from([0x80 | bytes.length, ...bytes]);
  }
  static derTLV(tag, content) {
    return Buffer.concat([
      Buffer.from([tag]),
      CertGenCode.derLen(content.length),
      content,
    ]);
  }
  static derIntFromNumber(n) {
    if (n === 0) {
      return CertGenCode.derTLV(0x02, Buffer.from([0]));
    }
    let bytes = [];
    let v = n;
    while (v > 0) {
      bytes.unshift(v & 0xff);
      v = Math.floor(v / 256);
    }
    if (bytes[0] & 0x80) {
      bytes.unshift(0);
    }
    return CertGenCode.derTLV(0x02, Buffer.from(bytes));
  }
  static derOID(parts) {
    const out = [40 * parts[0] + parts[1]];
    for (let i = 2; i < parts.length; i++) {
      let v = parts[i];
      let stack = [v & 0x7f];
      v >>>= 7;
      while (v > 0) {
        stack.unshift((v & 0x7f) | 0x80);
        v >>>= 7;
      }
      out.push(...stack);
    }
    return CertGenCode.derTLV(0x06, Buffer.from(out));
  }
  static derSeq(...parts) {
    return CertGenCode.derTLV(0x30, Buffer.concat(parts));
  }
  static derSet(...parts) {
    return CertGenCode.derTLV(0x31, Buffer.concat(parts));
  }
  static derNull() {
    return Buffer.from([0x05, 0x00]);
  }
  static derUTF8(s) {
    return CertGenCode.derTLV(0x0c, Buffer.from(s, "utf8"));
  }
  static derUTCTime(d) {
    const pad = n => String(n).padStart(2, "0");
    const s =
      pad(d.getUTCFullYear() % 100) +
      pad(d.getUTCMonth() + 1) +
      pad(d.getUTCDate()) +
      pad(d.getUTCHours()) +
      pad(d.getUTCMinutes()) +
      pad(d.getUTCSeconds()) +
      "Z";
    return CertGenCode.derTLV(0x17, Buffer.from(s, "ascii"));
  }
  static derBitStr(buf) {
    return CertGenCode.derTLV(
      0x03,
      Buffer.concat([Buffer.from([0]), Buffer.from(buf)])
    );
  }
  static derOctet(buf) {
    return CertGenCode.derTLV(0x04, buf);
  }
  static derCtx(tag, content) {
    return CertGenCode.derTLV(0xa0 | tag, content);
  }
  static derBool(b) {
    return CertGenCode.derTLV(0x01, Buffer.from([b ? 0xff : 0]));
  }

  static algSha256RSA() {
    return CertGenCode.derSeq(
      CertGenCode.derOID([1, 2, 840, 113549, 1, 1, 11]),
      CertGenCode.derNull()
    );
  }

  static commonName(cn) {
    return CertGenCode.derSeq(
      CertGenCode.derSet(
        CertGenCode.derSeq(
          CertGenCode.derOID([2, 5, 4, 3]),
          CertGenCode.derUTF8(cn)
        )
      )
    );
  }

  static sanExtension(hostnames) {
    const names = hostnames.map(h =>
      CertGenCode.derTLV(0x82, Buffer.from(h, "ascii"))
    );
    return CertGenCode.derSeq(
      CertGenCode.derOID([2, 5, 29, 17]),
      CertGenCode.derOctet(CertGenCode.derSeq(...names))
    );
  }

  static basicConstraintsExt() {
    return CertGenCode.derSeq(
      CertGenCode.derOID([2, 5, 29, 19]),
      CertGenCode.derBool(true),
      CertGenCode.derOctet(CertGenCode.derSeq())
    );
  }

  static keyUsageExt() {
    // bits: digitalSignature(0), keyEncipherment(2). Unused bits = 5.
    const bits = CertGenCode.derTLV(0x03, Buffer.from([0x05, 0xa0]));
    return CertGenCode.derSeq(
      CertGenCode.derOID([2, 5, 29, 15]),
      CertGenCode.derBool(true),
      CertGenCode.derOctet(bits)
    );
  }

  static ekuExt() {
    return CertGenCode.derSeq(
      CertGenCode.derOID([2, 5, 29, 37]),
      CertGenCode.derOctet(
        CertGenCode.derSeq(CertGenCode.derOID([1, 3, 6, 1, 5, 5, 7, 3, 1]))
      )
    );
  }

  static generate(hostnames) {
    const nodeCrypto = require("crypto");
    const fs = require("fs");

    const caKey = nodeCrypto.createPrivateKey(
      fs.readFileSync(__dirname + "/http2-cert.key")
    );

    const { publicKey, privateKey } = nodeCrypto.generateKeyPairSync("rsa", {
      modulusLength: 2048,
    });
    const spkiDer = publicKey.export({ type: "spki", format: "der" });
    const leafKeyPem = privateKey.export({ type: "pkcs8", format: "pem" });

    const serial = nodeCrypto.randomBytes(16);
    serial[0] &= 0x7f;
    if (serial[0] === 0) {
      serial[0] = 1;
    }

    const now = new Date();
    const notBefore = new Date(now.getTime() - 5 * 60 * 1000);
    const notAfter = new Date(now.getTime() + 365 * 24 * 60 * 60 * 1000);

    const tbs = CertGenCode.derSeq(
      CertGenCode.derCtx(0, CertGenCode.derIntFromNumber(2)),
      CertGenCode.derTLV(0x02, serial),
      CertGenCode.algSha256RSA(),
      // Leading space is intentional: must byte-match the CA's subject DN
      // (pycert/http2-ca.pem encodes the CN as " HTTP2 Test CA").
      CertGenCode.commonName(" HTTP2 Test CA"),
      CertGenCode.derSeq(
        CertGenCode.derUTCTime(notBefore),
        CertGenCode.derUTCTime(notAfter)
      ),
      CertGenCode.commonName(hostnames[0]),
      spkiDer,
      CertGenCode.derCtx(
        3,
        CertGenCode.derSeq(
          CertGenCode.sanExtension(hostnames),
          CertGenCode.basicConstraintsExt(),
          CertGenCode.keyUsageExt(),
          CertGenCode.ekuExt()
        )
      )
    );

    const sig = nodeCrypto.sign("sha256", tbs, caKey);
    const cert = CertGenCode.derSeq(
      tbs,
      CertGenCode.algSha256RSA(),
      CertGenCode.derBitStr(sig)
    );

    const certB64 = cert.toString("base64");
    const certPem =
      "-----BEGIN CERTIFICATE-----\n" +
      certB64.match(/.{1,64}/g).join("\n") +
      "\n-----END CERTIFICATE-----\n";

    return { keyPem: leafKeyPem, certPem, certBase64: certB64 };
  }
}

// UDP socket that never answers, to reserve a port number for UDP as well as TCP.
class NodeNoResponseUdpCode {
  static async bind(port) {
    const dgram = require("dgram");
    const { once } = require("events");
    // Dual-stack, to reserve the port for IPv4 and IPv6 alike.
    global.noResponseUdp = dgram.createSocket({
      type: "udp6",
      ipv6Only: false,
    });
    global.noResponseUdp.bind(port);
    await once(global.noResponseUdp, "listening");
    global.noResponseUdp.on("error", err => console.log(`udp error: ${err}`));
  }
}

/// Runs start(), then claim() on the port start() got; if claim() fails,
/// runs stop() and retries, so both end up on one port free for each.
export async function startOnSharedPort(start, claim, stop, maxRetries = 20) {
  for (let retry = 0; retry < maxRetries; retry++) {
    await start();
    try {
      return await claim();
    } catch (e) {
      console.log(`Port is taken (${e}), retrying`);
      await stop();
    }
  }
  throw new Error("No port was free for both servers");
}

export class BaseNodeServer {
  protocol() {
    return this._protocol;
  }
  version() {
    return this._version;
  }
  origin() {
    return `${this.protocol()}://${this.domain()}:${this.port()}`;
  }
  port() {
    return this._port;
  }
  domain() {
    return this._domain || `localhost`;
  }

  // Static form: install a CA certificate file (default http2-ca.pem) into
  // the NSS db so that test servers using the matching pre-shipped leaf
  // certs are trusted. Kept for backward compatibility with callers that
  // pass a filename string.
  static async installCert(filename = "http2-ca.pem") {
    return BaseNodeServer._installCertFromDisk(filename);
  }

  // Instance form: takes an optional list of hostnames. Always installs
  // http2-ca.pem in NSS. When hostnames are provided, also asks the node
  // child to mint a fresh leaf cert (signed by the same key the test CA
  // uses) covering those hostnames as DNS SANs and stores it on `this`
  // for `start()` to pass to the server.
  async installCert(hostnames) {
    if (Services.appinfo.processType != Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT) {
      return;
    }
    await BaseNodeServer._installCertFromDisk("http2-ca.pem");

    if (!Array.isArray(hostnames) || !hostnames.length) {
      return;
    }

    this.processId = this.processId || (await NodeServer.fork());
    await this.execute(CertGenCode);
    let result = await this.execute(
      `CertGenCode.generate(${JSON.stringify(hostnames)})`
    );
    this._keyPem = result.keyPem;
    this._certPem = result.certPem;
    this._domain = hostnames[0];
  }

  static async _installCertFromDisk(filename) {
    if (Services.appinfo.processType != Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT) {
      // Can't install cert from content process.
      return;
    }
    let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
      Ci.nsIX509CertDB
    );

    function readFile(file) {
      let fstream = Cc[
        "@mozilla.org/network/file-input-stream;1"
      ].createInstance(Ci.nsIFileInputStream);
      fstream.init(file, -1, 0, 0);
      let data = NetUtil.readInputStreamToString(fstream, fstream.available());
      fstream.close();
      return data;
    }

    // Find the root directory that contains netwerk/
    let currentDir = Services.dirsvc.get("CurWorkD", Ci.nsIFile);
    let rootDir = currentDir.clone();

    // XXX(valentin) The certs are stored in netwerk/test/unit
    // Walk up until the dir contains netwerk/
    // This is hacky, but the alternative would also require
    // us to walk up the path to the root dir.
    while (rootDir) {
      let netwerkDir = rootDir.clone();
      netwerkDir.append("netwerk");
      if (netwerkDir.exists() && netwerkDir.isDirectory()) {
        break;
      }
      let parent = rootDir.parent;
      if (!parent || parent.equals(rootDir)) {
        // Reached filesystem root, fallback to current directory
        rootDir = currentDir;
        break;
      }
      rootDir = parent;
    }

    function findCertPath(dir) {
      let candidates = [
        ["netwerk", "test", "unit", filename],
        // This one works for mochitests
        ["_tests", "xpcshell", "netwerk", "test", "unit", filename],
      ];
      for (let candidate of candidates) {
        let certpath = dir.clone().QueryInterface(Ci.nsIFile);
        for (let part of candidate) {
          certpath.append(part);
        }
        if (certpath.exists()) {
          return certpath;
        }
      }
      return null;
    }

    let certFile = findCertPath(rootDir);
    if (!certFile) {
      // mochitest browser-chrome: the cert is shipped via TEST_HARNESS_FILES
      // (see netwerk/test/moz.build) and exposed under chrome://mochitests/.
      try {
        let chromeReg = Cc["@mozilla.org/chrome/chrome-registry;1"].getService(
          Ci.nsIChromeRegistry
        );
        let resolved = chromeReg.convertChromeURL(
          Services.io.newURI(
            `chrome://mochitests/content/tests/netwerk/test/unit/${filename}`
          )
        );
        let resolvedFile = resolved.QueryInterface(Ci.nsIFileURL).file;
        if (resolvedFile.exists()) {
          certFile = resolvedFile;
        }
      } catch (e) {}
    }
    if (!certFile) {
      console.log(`Error installing cert: file not found.}`);
      return;
    }

    try {
      let pem = readFile(certFile)
        .replace(/-----BEGIN CERTIFICATE-----/, "")
        .replace(/-----END CERTIFICATE-----/, "")
        .replace(/[\r\n]/g, "");
      certdb.addCertFromBase64(pem, "CTu,u,u");
    } catch (e) {
      let errStr = e.toString();
      console.log(`Error installing cert ${errStr} path:${certFile.path}`);
      if (errStr.includes("0x805a1fe8")) {
        // Can't install the cert without a profile
        // Let's show an error, otherwise this will be difficult to diagnose.
        console.log(
          `!!! BaseNodeServer.installCert > Make sure your unit test calls do_get_profile()`
        );
      }
    }
  }

  // Pushes the generated TLS material into the node child as globals so
  // that *ServerCode.startServer can pick it up. Server-code helpers fall
  // back to the on-disk pre-shipped cert files when these are unset.
  async _pushGeneratedTLSMaterial() {
    if (!this._keyPem || !this._certPem) {
      return;
    }
    await this.execute(
      `global.tlsKey = ${JSON.stringify(this._keyPem)};
       global.tlsCert = ${JSON.stringify(this._certPem)};`
    );
  }

  /// Starts the server on a port free for TCP and UDP alike, holding the UDP
  /// side bound but never answering, so that nothing else can claim it.
  async startWithNoResponseUdpPort(start = () => this.start()) {
    return startOnSharedPort(
      start,
      async () => {
        await this.execute(NodeNoResponseUdpCode);
        return this.execute(`NodeNoResponseUdpCode.bind(${this.port()})`);
      },
      () => this.stop()
    );
  }

  /// Stops the server
  async stop() {
    if (this.processId) {
      await this.execute(`ADB.stopForwarding(${this.port()})`);
      await NodeServer.kill(this.processId);
      this.processId = undefined;
    }
  }

  /// Executes a command in the context of the node server
  async execute(command) {
    return NodeServer.execute(this.processId, command);
  }

  /// @path : string - the path on the server that we're handling. ex: /path
  /// @handler : function(req, resp, url) - function that processes request and
  ///     emits a response.
  async registerPathHandler(path, handler) {
    return this.execute(
      `global.path_handlers["${path}"] = ${handler.toString()}`
    );
  }
}

// TCP Echo server

class NodeTCPEchoServerCode {
  static async startServer(port) {
    const net = require("net");

    // Simple TCP echo server
    global.server = net.createServer(socket => {
      socket.on("data", data => {
        try {
          console.log("got data:" + data);
          socket.write(data);
        } catch (e) {
          console.log(`echo write error: ${e}`);
        }
      });

      socket.on("error", err => {
        console.log(`socket error: ${err}`);
      });
    });

    const serverPort = await ADB.listenAndForwardPort(global.server, port);
    return serverPort;
  }
}

export class NodeTCPEchoServer extends BaseNodeServer {
  _protocol = "tcp";
  _version = "tcp";

  /// Starts the TCP echo server
  async start(port = 0) {
    this.processId = await NodeServer.fork();

    await this.execute(ADB);
    await this.execute(NodeTCPEchoServerCode);

    this._port = await this.execute(
      `NodeTCPEchoServerCode.startServer(${port})`
    );
  }
}

// Minimal SOCKS5 remote-DNS forwarding proxy for tests.
//
// Completes the SOCKS5 no-auth handshake, then transparently pipes the byte
// stream (TLS etc. flow end-to-end) to 127.0.0.1 at the requested port. Any
// requested hostname is "resolved" to loopback, emulating a remote-DNS SOCKS
// proxy: the client is expected to send the hostname (ATYP=domain), never a
// pre-resolved IP. The hostnames the proxy receives are recorded in
// global.socksConns so a test can confirm remote DNS was actually used and
// that no client-side resolution leaked out of the proxy.
class NodeSocks5ForwardServerCode {
  static async startServer(port) {
    const net = require("net");

    global.socksConns = [];
    global.server = net.createServer(client => {
      client.on("error", () => {});
      // Greeting: VER(0x05) NMETHODS METHODS...
      client.once("data", greeting => {
        if (!greeting.length || greeting[0] !== 0x05) {
          client.end();
          return;
        }
        // Reply: VER=0x05 METHOD=0x00 (no authentication). Firefox only sends
        // the request after receiving this, so greeting and request never
        // coalesce into one chunk.
        client.write(Buffer.from([0x05, 0x00]));
        client.once("data", req => {
          // Request: VER CMD RSV ATYP DST.ADDR DST.PORT ; CMD 0x01 = CONNECT.
          if (req.length < 7 || req[0] !== 0x05 || req[1] !== 0x01) {
            client.end();
            return;
          }
          const atyp = req[3];
          let offset = 4;
          if (atyp === 0x01) {
            offset = 8; // IPv4
          } else if (atyp === 0x03) {
            const len = req[4];
            const name = req.slice(5, 5 + len).toString("ascii");
            global.socksConns.push(name);
            offset = 5 + len;
          } else if (atyp === 0x04) {
            offset = 4 + 16; // IPv6
          } else {
            client.end();
            return;
          }
          const dstPort = req.readUInt16BE(offset);
          const upstream = net.connect(dstPort, "127.0.0.1", () => {
            // Success: VER REP RSV ATYP BND.ADDR(0.0.0.0) BND.PORT(0).
            client.write(
              Buffer.from([0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0])
            );
            client.pipe(upstream);
            upstream.pipe(client);
          });
          upstream.on("error", () => {
            try {
              client.end();
            } catch (e) {}
          });
          client.on("error", () => {
            try {
              upstream.end();
            } catch (e) {}
          });
        });
      });
    });

    const serverPort = await ADB.listenAndForwardPort(global.server, port);
    return serverPort;
  }
}

export class NodeSocks5ForwardServer extends BaseNodeServer {
  _protocol = "socks";
  _version = "socks5";

  /// Starts the SOCKS5 forwarding proxy.
  async start(port = 0) {
    this.processId = await NodeServer.fork();

    await this.execute(ADB);
    await this.execute(NodeSocks5ForwardServerCode);

    this._port = await this.execute(
      `NodeSocks5ForwardServerCode.startServer(${port})`
    );
  }

  /// Hostnames (ATYP=domain) the proxy has been asked to connect to.
  async connectedHostnames() {
    return JSON.parse(
      await this.execute("JSON.stringify(global.socksConns || [])")
    );
  }
}

// TCP Echo server

class NodeTLSEchoServerCode {
  static async startServer(port) {
    const tls = require("tls");
    const fs = require("fs");
    const options = {
      key: global.tlsKey || fs.readFileSync(__dirname + "/http2-cert.key"),
      cert: global.tlsCert || fs.readFileSync(__dirname + "/http2-cert.pem"),
    };
    global.server = tls.createServer(options, socket => {
      socket.on("data", data => {
        try {
          console.log("tls: got data:", data);
          socket.write(data);
        } catch (e) {
          console.log(`tls echo write error: ${e}`);
        }
      });
      socket.on("error", err => {
        console.log(`tls socket error: ${err}`);
      });
    });
    const serverPort = await ADB.listenAndForwardPort(global.server, port);
    return serverPort;
  }
}

export class NodeTLSEchoServer extends BaseNodeServer {
  _protocol = "tls";
  _version = "tls";

  async start(port = 0, hostnames) {
    if (!this._skipCert) {
      await this.installCert(hostnames);
    }
    this.processId = this.processId || (await NodeServer.fork());
    await this.execute(ADB);
    await this.execute(NodeTLSEchoServerCode);
    await this._pushGeneratedTLSMaterial();
    this._port = await this.execute(
      `NodeTLSEchoServerCode.startServer(${port})`
    );
  }
}

// HTTP

class NodeHTTPServerCode extends BaseNodeHTTPServerCode {
  static async startServer(port) {
    const http = require("http");
    global.server = http.createServer(BaseNodeHTTPServerCode.globalHandler);

    let serverPort = await ADB.listenAndForwardPort(global.server, port);
    return serverPort;
  }
}

export class NodeHTTPServer extends BaseNodeServer {
  _protocol = "http";
  _version = "http/1.1";
  /// Starts the server
  /// @port - default 0
  ///    when provided, will attempt to listen on that port.
  async start(port = 0) {
    this.processId = await NodeServer.fork();

    await this.execute(BaseNodeHTTPServerCode);
    await this.execute(NodeHTTPServerCode);
    await this.execute(ADB);
    this._port = await this.execute(`NodeHTTPServerCode.startServer(${port})`);
    await this.execute(`global.path_handlers = {};`);
  }
}

// HTTPS

class NodeHTTPSServerCode extends BaseNodeHTTPServerCode {
  static async startServer(port) {
    const fs = require("fs");
    const options = {
      key: global.tlsKey || fs.readFileSync(__dirname + "/http2-cert.key"),
      cert: global.tlsCert || fs.readFileSync(__dirname + "/http2-cert.pem"),
      // Optionally request a client cert; rejectUnauthorized is off so the
      // handshake completes regardless (tests assert via the dialog mock).
      requestCert: !!global.requestClientCert,
      rejectUnauthorized: false,
      maxHeaderSize: 128 * 1024,
    };
    const https = require("https");
    global.server = https.createServer(
      options,
      BaseNodeHTTPServerCode.globalHandler
    );

    let serverPort = await ADB.listenAndForwardPort(global.server, port);
    return serverPort;
  }
}

export class NodeHTTPSServer extends BaseNodeServer {
  _protocol = "https";
  _version = "http/1.1";
  _requestClientCert = false;

  /// Make the TLS server request a client cert. Call before `start()`.
  setRequestClientCert(value) {
    this._requestClientCert = !!value;
  }

  /// Starts the server
  /// @port - default 0
  ///    when provided, will attempt to listen on that port.
  /// @hostnames - optional array of DNS hostnames. When provided, a
  ///    fresh leaf cert covering those hostnames is generated and used
  ///    by the server; otherwise the pre-shipped http2-cert is used.
  async start(port = 0, hostnames) {
    if (!this._skipCert) {
      await this.installCert(hostnames);
    }
    this.processId = this.processId || (await NodeServer.fork());

    await this.execute(BaseNodeHTTPServerCode);
    await this.execute(NodeHTTPSServerCode);
    await this.execute(ADB);
    await this._pushGeneratedTLSMaterial();
    await this.execute(
      `global.requestClientCert = ${this._requestClientCert};`
    );
    this._port = await this.execute(`NodeHTTPSServerCode.startServer(${port})`);
    await this.execute(`global.path_handlers = {};`);
  }
}

// HTTP2

class NodeHTTP2ServerCode extends BaseNodeHTTPServerCode {
  static async startServer(port, allowHTTP1) {
    const fs = require("fs");
    const options = {
      key: global.tlsKey || fs.readFileSync(__dirname + "/http2-cert.key"),
      cert: global.tlsCert || fs.readFileSync(__dirname + "/http2-cert.pem"),
      allowHTTP1,
    };
    const http2 = require("http2");
    global.server = http2.createSecureServer(
      options,
      BaseNodeHTTPServerCode.globalHandler
    );

    global.sessionCount = 0;
    global.sessions = new Set();
    global.server.on("session", session => {
      global.sessions.add(session);
      session.on("close", () => {
        global.sessions.delete(session);
      });
      global.sessionCount++;
    });

    let serverPort = await ADB.listenAndForwardPort(global.server, port);
    return serverPort;
  }

  static sessionCount() {
    return global.sessionCount;
  }
}

export class NodeHTTP2Server extends BaseNodeServer {
  _protocol = "https";
  _version = "h2";
  // When true, also accepts HTTP/1.1 clients. Must be set before start().
  allowHTTP1 = false;
  /// Starts the server
  /// @port - default 0
  ///    when provided, will attempt to listen on that port.
  async start(port = 0, hostnames) {
    if (!this._skipCert) {
      await this.installCert(hostnames);
    }
    this.processId = this.processId || (await NodeServer.fork());

    await this.execute(BaseNodeHTTPServerCode);
    await this.execute(NodeHTTP2ServerCode);
    await this.execute(ADB);
    await this._pushGeneratedTLSMaterial();
    this._port = await this.execute(
      `NodeHTTP2ServerCode.startServer(${port}, ${this.allowHTTP1})`
    );
    await this.execute(`global.path_handlers = {};`);
  }

  async sessionCount() {
    let count = this.execute(`NodeHTTP2ServerCode.sessionCount()`);
    return count;
  }
}

// HTTP2 server using the vendored node-http2 library instead of Node's http2
// module. The library can be patched to produce protocol errors on demand.

class NodeLegacyHTTP2ServerCode extends BaseNodeHTTPServerCode {
  static async startServer(port) {
    const fs = require("fs");
    global.http2Lib = require(`${__dirname}/../node-http2`);
    const options = {
      key: global.tlsKey || fs.readFileSync(__dirname + "/http2-cert.key"),
      cert: global.tlsCert || fs.readFileSync(__dirname + "/http2-cert.pem"),
    };
    global.server = global.http2Lib.createServer(
      options,
      NodeLegacyHTTP2ServerCode.handleRequest
    );
    // Clients tearing down connections cause socket errors; ignore them.
    global.server.on("connection", socket => socket.on("error", () => {}));
    // CONNECT is only supported over HTTP/2.
    global.server.on("connect", (req, clientSocket) => {
      clientSocket.write("HTTP/1.1 404 Not Found\r\n\r\n");
      clientSocket.destroy();
    });
    // Handlers can throw from timers, which must not take the server down.
    process.on("uncaughtException", e => {
      console.log(`NodeLegacyHTTP2Server uncaught exception: ${e.stack}`);
    });
    // The library wraps an https.Server, which is what listens and accepts.
    return ADB.listenAndForwardPort(global.server._server, port);
  }

  static handleRequest(req, res) {
    try {
      if (req.method == "CONNECT") {
        if (global.connect_handler) {
          return global.connect_handler(req, res);
        }
        res.writeHead(405);
        res.end();
        return undefined;
      }
      if (global.request_handler) {
        return global.request_handler(req, res);
      }
      return BaseNodeHTTPServerCode.globalHandler(req, res);
    } catch (e) {
      console.log(`NodeLegacyHTTP2Server handler exception: ${e.stack}`);
      try {
        res.writeHead(500, { "content-type": "text/plain" });
        res.end(`handler exception: ${e.message}`);
      } catch (_) {
        // The response may already be unusable.
      }
    }
    return undefined;
  }
}

export class NodeLegacyHTTP2Server extends BaseNodeServer {
  _protocol = "https";
  _version = "h2";
  /// Starts the server
  /// @port - default 0
  ///    when provided, will attempt to listen on that port.
  async start(port = 0, hostnames) {
    if (!this._skipCert) {
      await this.installCert(hostnames);
    }
    this.processId = this.processId || (await NodeServer.fork());

    await this.execute(BaseNodeHTTPServerCode);
    await this.execute(NodeLegacyHTTP2ServerCode);
    await this.execute(ADB);
    await this._pushGeneratedTLSMaterial();
    this._port = await this.execute(
      `NodeLegacyHTTP2ServerCode.startServer(${port})`
    );
    await this.execute(`global.path_handlers = {};`);
  }

  /// Handles every request that isn't a CONNECT, instead of the path handlers.
  /// @handler : function(req, resp)
  async setRequestHandler(handler) {
    return this.execute(`global.request_handler = ${handler.toString()}`);
  }

  /// Handles CONNECT requests, making the server act as a proxy.
  /// @handler : function(req, resp)
  async setConnectHandler(handler) {
    return this.execute(`global.connect_handler = ${handler.toString()}`);
  }
}

// Base HTTP proxy

class BaseProxyCode {
  static proxyHandler(req, res) {
    if (req.url.startsWith("/")) {
      res.writeHead(405);
      res.end();
      return;
    }

    let url = new URL(req.url);
    const http = require("http");
    let preq = http
      .request(
        {
          method: req.method,
          path: url.pathname,
          port: url.port,
          host: url.hostname,
          protocol: url.protocol,
        },
        proxyresp => {
          res.writeHead(
            proxyresp.statusCode,
            proxyresp.statusMessage,
            proxyresp.headers
          );
          proxyresp.on("data", chunk => {
            if (!res.writableEnded) {
              res.write(chunk);
            }
          });
          proxyresp.on("end", () => {
            res.end();
          });
        }
      )
      .on("error", e => {
        console.log(`sock err: ${e}`);
      });
    if (req.method != "POST") {
      preq.end();
    } else {
      req.on("data", chunk => {
        if (!preq.writableEnded) {
          preq.write(chunk);
        }
      });
      req.on("end", () => preq.end());
    }
  }

  static onConnect(req, clientSocket, head) {
    if (global.connect_handler) {
      global.connect_handler(req, clientSocket, head);
      return;
    }
    const net = require("net");
    // Connect to an origin server. The test-only hostnames below have no DNS
    // entry: Gecko resolves them via network.dns.native-is-localhost, but this
    // process uses the OS resolver and would fail with ENOTFOUND.
    const { port, hostname } = new URL(`https://${req.url}`);
    const localhostNames = [
      "foo.example.com",
      "alt1.example.com",
      "alt2.example.com",
    ];
    const serverSocket = net
      .connect(
        {
          port: port || 443,
          host: localhostNames.includes(hostname) ? "127.0.0.1" : hostname,
          family: 4, // Specifies to use IPv4
        },
        () => {
          clientSocket.write(
            "HTTP/1.1 200 Connection Established\r\n" +
              "Proxy-agent: Node.js-Proxy\r\n" +
              "\r\n"
          );
          serverSocket.write(head);
          serverSocket.pipe(clientSocket);
          clientSocket.pipe(serverSocket);
        }
      )
      .on("error", e => {
        console.log("error" + e);
        // The socket will error out when we kill the connection
        // just ignore it.
      });

    clientSocket.on("error", e => {
      console.log("client error" + e);
      // Sometimes we got ECONNRESET error on windows platform.
      // Ignore it for now.
    });
  }
}

class BaseHTTPProxy extends BaseNodeServer {
  registerFilter() {
    const pps =
      Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();
    this.filter = new NodeProxyFilter(
      this.protocol(),
      "localhost",
      this.port(),
      0
    );
    pps.registerFilter(this.filter, 10);
  }

  unregisterFilter() {
    const pps =
      Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();
    if (this.filter) {
      pps.unregisterFilter(this.filter);
      this.filter = undefined;
    }
  }

  /// Stops the server
  async stop() {
    this.unregisterFilter();
    await super.stop();
  }

  async registerConnectHandler(handler) {
    return this.execute(`global.connect_handler = ${handler.toString()}`);
  }
}

// HTTP1 Proxy

export class NodeProxyFilter {
  constructor(type, host, port, flags) {
    this._type = type;
    this._host = host;
    this._port = port;
    this._flags = flags;
    this.QueryInterface = ChromeUtils.generateQI(["nsIProtocolProxyFilter"]);
  }
  applyFilter(uri, pi, cb) {
    const pps =
      Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();
    cb.onProxyFilterResult(
      pps.newProxyInfo(
        this._type,
        this._host,
        this._port,
        "",
        "",
        this._flags,
        1000,
        null
      )
    );
  }
}

export class Http3ProxyFilter {
  constructor(host, port, flags, masqueTemplate, auth) {
    this._host = host;
    this._port = port;
    this._flags = flags;
    this._masqueTemplate = masqueTemplate;
    this._auth = auth;
    this.QueryInterface = ChromeUtils.generateQI(["nsIProtocolProxyFilter"]);
  }
  applyFilter(uri, pi, cb) {
    const pps =
      Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();
    cb.onProxyFilterResult(
      pps.newMASQUEProxyInfo(
        this._host,
        this._port,
        this._masqueTemplate,
        this._auth,
        "",
        this._flags,
        1000,
        null
      )
    );
  }
}

class HTTPProxyCode {
  static async startServer(port) {
    const http = require("http");
    global.proxy = http.createServer(BaseProxyCode.proxyHandler);
    global.proxy.on("connect", BaseProxyCode.onConnect);

    let proxyPort = await ADB.listenAndForwardPort(global.proxy, port);
    return proxyPort;
  }
}

export class NodeHTTPProxyServer extends BaseHTTPProxy {
  _protocol = "http";
  /// Starts the server
  /// @port - default 0
  ///    when provided, will attempt to listen on that port.
  async start(port = 0) {
    this.processId = await NodeServer.fork();

    await this.execute(BaseProxyCode);
    await this.execute(HTTPProxyCode);
    await this.execute(ADB);
    await this.execute(`global.connect_handler = null;`);
    this._port = await this.execute(`HTTPProxyCode.startServer(${port})`);

    this.registerFilter();
  }
}

// HTTPS proxy

class HTTPSProxyCode {
  static async startServer(port) {
    const fs = require("fs");
    const options = {
      key: global.tlsKey || fs.readFileSync(__dirname + "/proxy-cert.key"),
      cert: global.tlsCert || fs.readFileSync(__dirname + "/proxy-cert.pem"),
    };
    const https = require("https");
    global.proxy = https.createServer(options, BaseProxyCode.proxyHandler);
    global.proxy.on("connect", BaseProxyCode.onConnect);

    let proxyPort = await ADB.listenAndForwardPort(global.proxy, port);
    return proxyPort;
  }
}

export class NodeHTTPSProxyServer extends BaseHTTPProxy {
  _protocol = "https";
  /// Starts the server
  /// @port - default 0
  ///    when provided, will attempt to listen on that port.
  async start(port = 0, hostnames) {
    if (!this._skipCert) {
      if (Array.isArray(hostnames) && hostnames.length) {
        await this.installCert(hostnames);
      } else {
        await BaseNodeServer.installCert("proxy-ca.pem");
      }
    }
    this.processId = this.processId || (await NodeServer.fork());

    await this.execute(BaseProxyCode);
    await this.execute(HTTPSProxyCode);
    await this.execute(ADB);
    await this.execute(`global.connect_handler = null;`);
    await this._pushGeneratedTLSMaterial();
    this._port = await this.execute(`HTTPSProxyCode.startServer(${port})`);

    this.registerFilter();
  }
}

// HTTP2 proxy

class HTTP2ProxyCode {
  static async startServer(port, auth, maxConcurrentStreams) {
    const fs = require("fs");
    const options = {
      key: global.tlsKey || fs.readFileSync(__dirname + "/proxy-cert.key"),
      cert: global.tlsCert || fs.readFileSync(__dirname + "/proxy-cert.pem"),
      settings: {
        maxConcurrentStreams,
      },
    };
    const http2 = require("http2");
    global.proxy = http2.createSecureServer(options);
    global.socketCounts = {};
    this.setupProxy(auth);

    let proxyPort = await ADB.listenAndForwardPort(global.proxy, port);
    return proxyPort;
  }

  static setupProxy(auth) {
    if (!global.proxy) {
      throw new Error("proxy is null");
    }

    global.proxy.on("stream", (stream, headers) => {
      if (headers[":scheme"] === "http") {
        const http = require("http");
        let url = new URL(
          `${headers[":scheme"]}://${headers[":authority"]}${headers[":path"]}`
        );
        let req = http
          .request(
            {
              method: headers[":method"],
              path: headers[":path"],
              port: url.port,
              host: url.hostname,
              protocol: url.protocol,
            },
            proxyresp => {
              let proxyheaders = Object.assign({}, proxyresp.headers);
              // Filter out some prohibited headers.
              ["connection", "transfer-encoding", "keep-alive"].forEach(
                prop => {
                  delete proxyheaders[prop];
                }
              );
              try {
                stream.respond(
                  Object.assign(
                    { ":status": proxyresp.statusCode },
                    proxyheaders
                  )
                );
              } catch (e) {
                // The channel may have been closed already.
                if (
                  e.code !== "ERR_HTTP2_INVALID_STREAM" &&
                  !e.message.includes("The stream has been destroyed")
                ) {
                  throw e;
                }
              }
              proxyresp.on("data", chunk => {
                if (stream.writable) {
                  stream.write(chunk);
                }
              });
              proxyresp.on("end", () => {
                stream.end();
              });
            }
          )
          .on("error", e => {
            console.log(`sock err: ${e}`);
          });

        if (headers[":method"] != "POST") {
          req.end();
        } else {
          stream.on("data", chunk => {
            if (!req.writableEnded) {
              req.write(chunk);
            }
          });
          stream.on("end", () => req.end());
        }
        return;
      }
      if (headers[":method"] !== "CONNECT") {
        // Only accept CONNECT requests
        try {
          stream.respond({ ":status": 405 });
        } catch (e) {
          if (
            e.code !== "ERR_HTTP2_INVALID_STREAM" &&
            !e.message.includes("The stream has been destroyed")
          ) {
            throw e;
          }
        }
        stream.end();
        return;
      }

      const authorization_token = headers["proxy-authorization"];
      if (auth && !authorization_token) {
        try {
          stream.respond({
            ":status": 407,
            "proxy-authenticate": "Basic realm='foo'",
          });
        } catch (e) {
          if (
            e.code !== "ERR_HTTP2_INVALID_STREAM" &&
            !e.message.includes("The stream has been destroyed")
          ) {
            throw e;
          }
        }
        stream.end();
        return;
      }

      const target = headers[":authority"];
      const { port } = new URL(`https://${target}`);
      const net = require("net");
      const socket = net.connect(port, "127.0.0.1", () => {
        try {
          global.socketCounts[socket.remotePort] =
            (global.socketCounts[socket.remotePort] || 0) + 1;
          try {
            stream.respond({ ":status": 200, "Proxy-agent": "Node.js-Proxy" });
          } catch (e) {
            if (
              e.code !== "ERR_HTTP2_INVALID_STREAM" &&
              !e.message.includes("The stream has been destroyed")
            ) {
              throw e;
            }
          }
          socket.pipe(stream);
          stream.pipe(socket);
        } catch (exception) {
          console.log(exception);
          stream.close();
        }
      });
      const http2 = require("http2");
      socket.on("error", error => {
        const status = error.errno == "ENOTFOUND" ? 404 : 502;
        try {
          // If we already sent headers when the socket connected
          // then sending the status again would throw.
          if (!stream.sentHeaders) {
            try {
              stream.respond({ ":status": status });
            } catch (e) {
              if (
                e.code !== "ERR_HTTP2_INVALID_STREAM" &&
                !e.message.includes("The stream has been destroyed")
              ) {
                throw e;
              }
            }
          }
          stream.end();
        } catch (exception) {
          stream.close(http2.constants.NGHTTP2_CONNECT_ERROR);
        }
      });
      stream.on("close", () => {
        socket.end();
      });
      socket.on("close", () => {
        stream.close();
      });
      stream.on("end", () => {
        socket.end();
      });
      stream.on("aborted", () => {
        socket.end();
      });
      stream.on("error", error => {
        console.log("RESPONSE STREAM ERROR", error);
      });
    });
  }

  static socketCount(port) {
    return global.socketCounts[port];
  }
}

export class NodeHTTP2ProxyServer extends BaseHTTPProxy {
  _protocol = "https";
  /// Starts the server
  /// @port - default 0
  ///    when provided, will attempt to listen on that port.
  async start(port = 0, auth, maxConcurrentStreams = 100, hostnames) {
    await this.startWithoutProxyFilter(
      port,
      auth,
      maxConcurrentStreams,
      hostnames
    );
    this.registerFilter();
  }

  async startWithoutProxyFilter(
    port = 0,
    auth,
    maxConcurrentStreams = 100,
    hostnames
  ) {
    if (!this._skipCert) {
      if (Array.isArray(hostnames) && hostnames.length) {
        await this.installCert(hostnames);
      } else {
        await BaseNodeServer.installCert("proxy-ca.pem");
      }
    }
    this.processId = this.processId || (await NodeServer.fork());

    await this.execute(BaseProxyCode);
    await this.execute(HTTP2ProxyCode);
    await this.execute(ADB);
    await this.execute(`global.connect_handler = null;`);
    await this._pushGeneratedTLSMaterial();
    this._port = await this.execute(
      `HTTP2ProxyCode.startServer(${port}, ${auth}, ${maxConcurrentStreams})`
    );
  }

  async socketCount(port) {
    let count = await this.execute(`HTTP2ProxyCode.socketCount(${port})`);
    return count;
  }
}

// websocket server

class NodeWebSocketServerCode extends BaseNodeHTTPServerCode {
  static messageHandler(data, ws) {
    if (global.wsInputHandler) {
      global.wsInputHandler(data, ws);
      return;
    }

    ws.send("test");
  }

  static async startServer(port) {
    const fs = require("fs");
    const options = {
      key: global.tlsKey || fs.readFileSync(__dirname + "/http2-cert.key"),
      cert: global.tlsCert || fs.readFileSync(__dirname + "/http2-cert.pem"),
    };
    const https = require("https");
    global.server = https.createServer(
      options,
      BaseNodeHTTPServerCode.globalHandler
    );

    let node_ws_root = `${__dirname}/../node-ws`;
    const WS = require(`${node_ws_root}/lib/websocket`);
    WS.Server = require(`${node_ws_root}/lib/websocket-server`);
    global.webSocketServer = new WS.Server({ server: global.server });
    global.webSocketServer.on("connection", function connection(ws) {
      ws.on("message", data =>
        NodeWebSocketServerCode.messageHandler(data, ws)
      );
    });

    let serverPort = await ADB.listenAndForwardPort(global.server, port);
    return serverPort;
  }
}

export class NodeWebSocketServer extends BaseNodeServer {
  _protocol = "wss";
  /// Starts the server
  /// @port - default 0
  ///    when provided, will attempt to listen on that port.
  async start(port = 0, hostnames) {
    if (!this._skipCert) {
      await this.installCert(hostnames);
    }
    this.processId = this.processId || (await NodeServer.fork());

    await this.execute(BaseNodeHTTPServerCode);
    await this.execute(NodeWebSocketServerCode);
    await this.execute(ADB);
    await this._pushGeneratedTLSMaterial();
    this._port = await this.execute(
      `NodeWebSocketServerCode.startServer(${port})`
    );
    await this.execute(`global.path_handlers = {};`);
    await this.execute(`global.wsInputHandler = null;`);
  }

  async registerMessageHandler(handler) {
    return this.execute(`global.wsInputHandler = ${handler.toString()}`);
  }
}

// unencrypted websocket server

class NodeWebSocketPlainServerCode extends BaseNodeHTTPServerCode {
  static async startServer(port) {
    const http = require("http");
    global.server = http.createServer(BaseNodeHTTPServerCode.globalHandler);

    let node_ws_root = `${__dirname}/../node-ws`;
    const WS = require(`${node_ws_root}/lib/websocket`);
    WS.Server = require(`${node_ws_root}/lib/websocket-server`);
    global.webSocketServer = new WS.Server({ server: global.server });
    global.webSocketServer.on("connection", function connection(ws) {
      ws.on("message", data =>
        NodeWebSocketServerCode.messageHandler(data, ws)
      );
    });

    let serverPort = await ADB.listenAndForwardPort(global.server, port);
    return serverPort;
  }
}

export class NodeWebSocketPlainServer extends BaseNodeServer {
  _protocol = "ws";
  /// Starts the server
  /// @port - default 0
  ///    when provided, will attempt to listen on that port.
  async start(port = 0) {
    this.processId = await NodeServer.fork();

    await this.execute(BaseNodeHTTPServerCode);
    await this.execute(NodeWebSocketServerCode);
    await this.execute(NodeWebSocketPlainServerCode);
    await this.execute(ADB);
    this._port = await this.execute(
      `NodeWebSocketPlainServerCode.startServer(${port})`
    );
    await this.execute(`global.path_handlers = {};`);
    await this.execute(`global.wsInputHandler = null;`);
  }

  async registerMessageHandler(handler) {
    return this.execute(`global.wsInputHandler = ${handler.toString()}`);
  }
}

// websocket http2 server
// This code is inspired by
// https://github.com/szmarczak/http2-wrapper/blob/master/examples/ws/server.js
class NodeWebSocketHttp2ServerCode extends BaseNodeHTTPServerCode {
  static async startServer(port, fallbackToH1) {
    const fs = require("fs");
    const options = {
      key: global.tlsKey || fs.readFileSync(__dirname + "/http2-cert.key"),
      cert: global.tlsCert || fs.readFileSync(__dirname + "/http2-cert.pem"),
      settings: {
        enableConnectProtocol: !fallbackToH1,
        allowHTTP1: fallbackToH1,
      },
    };
    const http2 = require("http2");
    global.h2Server = http2.createSecureServer(options);

    let node_ws_root = `${__dirname}/../node-ws`;
    const WS = require(`${node_ws_root}/lib/websocket`);

    global.h2Server.on("stream", (stream, headers) => {
      if (headers[":method"] === "CONNECT") {
        try {
          stream.respond();
        } catch (e) {
          if (
            e.code !== "ERR_HTTP2_INVALID_STREAM" &&
            !e.message.includes("The stream has been destroyed")
          ) {
            throw e;
          }
        }

        const ws = new WS(null);
        stream.setNoDelay = () => {};
        ws.setSocket(stream, Buffer.from(""), 100 * 1024 * 1024);

        ws.on("message", data => {
          if (global.wsInputHandler) {
            global.wsInputHandler(data, ws);
            return;
          }

          ws.send("test");
        });
      } else {
        try {
          stream.respond();
        } catch (e) {
          if (
            e.code !== "ERR_HTTP2_INVALID_STREAM" &&
            !e.message.includes("The stream has been destroyed")
          ) {
            throw e;
          }
        }
        stream.end("ok");
      }
    });

    let serverPort = await ADB.listenAndForwardPort(global.h2Server, port);
    return serverPort;
  }
}

export class NodeWebSocketHttp2Server extends BaseNodeServer {
  _protocol = "wss";
  /// Starts the server
  /// @port - default 0
  ///    when provided, will attempt to listen on that port.
  async start(port = 0, fallbackToH1 = false, hostnames) {
    if (!this._skipCert) {
      await this.installCert(hostnames);
    }
    this.processId = this.processId || (await NodeServer.fork());

    await this.execute(BaseNodeHTTPServerCode);
    await this.execute(NodeWebSocketHttp2ServerCode);
    await this.execute(ADB);
    await this._pushGeneratedTLSMaterial();
    this._port = await this.execute(
      `NodeWebSocketHttp2ServerCode.startServer(${port}, ${fallbackToH1})`
    );
    await this.execute(`global.path_handlers = {};`);
    await this.execute(`global.wsInputHandler = null;`);
  }

  async registerMessageHandler(handler) {
    return this.execute(`global.wsInputHandler = ${handler.toString()}`);
  }
}

// Helper functions

export async function with_node_servers(arrayOfClasses, asyncClosure) {
  for (let s of arrayOfClasses) {
    let server = new s();
    await server.start();
    await asyncClosure(server);
    await server.stop();
  }
}

export class WebSocketConnection {
  constructor() {
    this._openPromise = new Promise(resolve => {
      this._openCallback = resolve;
    });

    this._stopPromise = new Promise(resolve => {
      this._stopCallback = resolve;
    });

    this._msgPromise = new Promise(resolve => {
      this._msgCallback = resolve;
    });

    this._proxyAvailablePromise = new Promise(resolve => {
      this._proxyAvailCallback = resolve;
    });

    this._messages = [];
    this._ws = null;
  }

  get QueryInterface() {
    return ChromeUtils.generateQI([
      "nsIWebSocketListener",
      "nsIProtocolProxyCallback",
    ]);
  }

  onAcknowledge() {}
  onBinaryMessageAvailable(aContext, aMsg) {
    this._messages.push(aMsg);
    this._msgCallback();
  }
  onMessageAvailable() {}
  onServerClose() {}
  onWebSocketListenerStart() {}
  onStart() {
    this._openCallback();
  }
  onStop(aContext, aStatusCode) {
    this._stopCallback({ status: aStatusCode });
    this._ws = null;
  }
  onProxyAvailable(req, chan, proxyInfo) {
    if (proxyInfo) {
      this._proxyAvailCallback({ type: proxyInfo.type });
    } else {
      this._proxyAvailCallback({});
    }
  }

  static makeWebSocketChan(url) {
    let protocol = url.startsWith("wss:") ? "wss" : "ws";
    let chan = Cc[`@mozilla.org/network/protocol;1?name=${protocol}`]
      .getService(Ci.nsIWebSocketProtocolHandler)
      .newWebSocketChannel();
    chan.initLoadInfo(
      null, // aLoadingNode
      Services.scriptSecurityManager.getSystemPrincipal(),
      null, // aTriggeringPrincipal
      Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      Ci.nsIContentPolicy.TYPE_WEBSOCKET
    );
    return chan;
  }
  // Returns a promise that resolves when the websocket channel is opened.
  open(url) {
    this._ws = WebSocketConnection.makeWebSocketChan(url);
    let uri = Services.io.newURI(url);
    this._ws.asyncOpen(uri, url, {}, 0, this, null);
    return this._openPromise;
  }
  // Closes the inner websocket. code and reason arguments are optional.
  close(code, reason) {
    this._ws.close(code || Ci.nsIWebSocketChannel.CLOSE_NORMAL, reason || "");
  }
  // Sends a message to the server.
  send(msg) {
    this._ws.sendMsg(msg);
  }
  // Returns a promise that resolves when the channel's onStop is called.
  // Promise resolves with an `{status}` object, where status is the
  // result passed to onStop.
  finished() {
    return this._stopPromise;
  }
  getProxyInfo() {
    return this._proxyAvailablePromise;
  }

  // Returned promise resolves with an array of received messages
  // If messages have been received in the the past before calling
  // receiveMessages, the promise will immediately resolve. Otherwise
  // it will resolve when the first message is received.
  async receiveMessages() {
    await this._msgPromise;
    this._msgPromise = new Promise(resolve => {
      this._msgCallback = resolve;
    });
    let messages = this._messages;
    this._messages = [];
    return messages;
  }
}

export class HTTP3Server {
  protocol() {
    return "https";
  }
  version() {
    return "h3";
  }
  origin() {
    return `${this.protocol()}://localhost:${this.port()}`;
  }
  port() {
    return this._port;
  }
  masque_proxy_port() {
    return this._masque_proxy_port;
  }
  no_response_port() {
    return this._no_response_port;
  }
  reverse_proxy_port() {
    return this._reverse_proxy_port;
  }
  domain() {
    return `localhost`;
  }

  /// Stops the server
  async stop() {
    if (this.processId) {
      await NodeServer.kill(this.processId);
      this.processId = undefined;
    }
  }

  async start(path, dbPath) {
    let result = await NodeServer.sendCommand(
      "",
      `/forkH3Server?path=${path}&dbPath=${dbPath}`
    );
    this.processId = result.id;

    const lineMatch = result.output.match(
      /HTTP3 server listening on ports ([\d, ]+ and \d+)\./
    );
    if (!lineMatch) {
      throw new Error(
        `HTTP3Server: unexpected server output: ${result.output.slice(0, 500)}`
      );
    }

    // Remove the ports.length guard once esr140 (5-port binary) is EOL.
    const ports = lineMatch[1].match(/\d+/g).map(Number);
    this._port = ports[0];
    if (ports.length >= 6) {
      this._reverse_proxy_port = ports[3];
      this._no_response_port = ports[4];
      this._masque_proxy_port = ports[5];
    }
    return this._port;
  }
}

export class NodeServer {
  // Executes command in the context of a node server.
  // See handler in moz-http2.js
  //
  // Example use:
  // let id = NodeServer.fork(); // id is a random string
  // await NodeServer.execute(id, `"hello"`)
  // > "hello"
  // await NodeServer.execute(id, `(() => "hello")()`)
  // > "hello"
  // await NodeServer.execute(id, `(() => var_defined_on_server)()`)
  // > "0"
  // await NodeServer.execute(id, `var_defined_on_server`)
  // > "0"
  // function f(param) { if (param) return param; return "bla"; }
  // await NodeServer.execute(id, f); // Defines the function on the server
  // await NodeServer.execute(id, `f()`) // executes defined function
  // > "bla"
  // let result = await NodeServer.execute(id, `f("test")`);
  // > "test"
  // await NodeServer.kill(id); // shuts down the server

  // Forks a new node server using moz-http2-child.js as a starting point
  static fork() {
    return this.sendCommand("", "/fork");
  }
  // Executes command in the context of the node server indicated by `id`
  static execute(id, command) {
    return this.sendCommand(command, `/execute/${id}`);
  }
  // Shuts down the server
  static kill(id) {
    return this.sendCommand("", `/kill/${id}`);
  }

  // Issues a request to the node server (handler defined in moz-http2.js)
  // This method should not be called directly.
  static sendCommand(command, path) {
    let h2Port = Services.env.get("MOZNODE_EXEC_PORT");
    if (!h2Port) {
      throw new Error("Could not find MOZNODE_EXEC_PORT");
    }

    let req = new XMLHttpRequest({ mozAnon: true, mozSystem: true });
    const serverIP =
      AppConstants.platform == "android" ? "10.0.2.2" : "127.0.0.1";
    // eslint-disable-next-line sdl/no-insecure-url
    req.open("POST", `http://${serverIP}:${h2Port}${path}`);
    req.channel.QueryInterface(Ci.nsIHttpChannelInternal).bypassProxy = true;
    req.channel.loadFlags |= Ci.nsIChannel.LOAD_BYPASS_URL_CLASSIFIER;
    // Prevent HTTPS-Only Mode from upgrading the request.
    req.channel.loadInfo.httpsOnlyStatus |= Ci.nsILoadInfo.HTTPS_ONLY_EXEMPT;
    // Allow deprecated HTTP request from SystemPrincipal
    req.channel.loadInfo.allowDeprecatedSystemRequests = true;

    // Passing a function to NodeServer.execute will define that function
    // in node. It can be called in a later execute command.
    let isFunction = function (obj) {
      return !!(obj && obj.constructor && obj.call && obj.apply);
    };
    let payload = command;
    if (isFunction(command)) {
      payload = `${command.name} = ${command.toString()};`;
    }

    return new Promise((resolve, reject) => {
      req.onload = () => {
        let x = null;

        if (req.statusText != "OK") {
          reject(`XHR request failed: ${req.statusText}`);
          return;
        }

        try {
          x = JSON.parse(req.responseText);
        } catch (e) {
          reject(`Failed to parse ${req.responseText} - ${e}`);
          return;
        }

        if (x.error) {
          let e = new Error(x.error, "", 0);
          e.stack = x.errorStack;
          reject(e);
          return;
        }
        resolve(x.result);
      };
      req.onerror = e => {
        reject(e);
      };

      req.send(payload.toString());
    });
  }
}
