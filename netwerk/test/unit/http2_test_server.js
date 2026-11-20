/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Only runs in node, which provides these.
/* globals require, __dirname, Buffer, setTimeout */
/* eslint-disable mozilla/no-arbitrary-setTimeout */

// Request handling for the HTTP/2 tests in http2_test_common.js. Runs in the
// node process of the server start_http2_test_server() starts.
class Http2TestServerCode {
  // Hooks into node-http2 so handlers can inspect and break the protocol.
  static install() {
    const root = `${__dirname}/../node-http2`;

    // Records the decompressed name-value pairs of each header block.
    const { HeaderSetDecompressor } = require(
      `${root}/lib/protocol/compressor`
    );
    const originalRead = HeaderSetDecompressor.prototype.read;
    let lastDecompressor;
    HeaderSetDecompressor.prototype.read = function () {
      if (this != lastDecompressor) {
        lastDecompressor = this;
        Http2TestServerCode.decompressedPairs = [];
      }
      let pair = originalRead.apply(this, arguments);
      if (pair) {
        Http2TestServerCode.decompressedPairs.push(pair);
      }
      return pair;
    };

    // Lets close() pick the last stream id announced in the GOAWAY.
    const { Connection } = require(`${root}/lib/protocol/connection`);
    const originalClose = Connection.prototype.close;
    Connection.prototype.close = function (error, lastId) {
      if (lastId !== undefined) {
        this._lastIncomingStream = lastId;
      }
      originalClose.apply(this, arguments);
    };

    const { Serializer } = require(`${root}/lib/protocol/framer`);
    Http2TestServerCode.Serializer = Serializer;
    Http2TestServerCode.originalTransform = Serializer.prototype._transform;

    Http2TestServerCode.multiplexResponses = {
      multiplex1: null,
      multiplex2: null,
    };
    Http2TestServerCode.h11requiredConn = null;
    Http2TestServerCode.h11requiredHeader = "yes";
    Http2TestServerCode.didRst = false;
    Http2TestServerCode.rstConnection = null;
    Http2TestServerCode.didUnknownRst = false;
    Http2TestServerCode.illegalheaderConn = null;
  }

  // Serializer transform that sends an empty DATA frame before the first one.
  static emptyDataTransform(frame) {
    const { Serializer, originalTransform } = Http2TestServerCode;
    if (frame.type == "DATA") {
      const emptyFrame = {
        type: "DATA",
        data: Buffer.alloc(0),
        flags: [],
        stream: frame.stream,
      };
      let buffers = [];
      Serializer.DATA(emptyFrame, buffers);
      Serializer.commonHeader(emptyFrame, buffers);
      for (let buffer of buffers) {
        this.push(buffer);
      }
      Serializer.prototype._transform = originalTransform;
    }
    originalTransform.apply(this, arguments);
  }

  // Serializer transform that sends a CONTINUATION frame on stream 0 before
  // the first HEADERS frame. With no HEADERS or PUSH_PROMISE pending, the
  // client skips its pre-dispatch checks and handles it for stream 0.
  static continuationStreamZeroTransform(frame) {
    const { Serializer, originalTransform } = Http2TestServerCode;
    if (frame.type == "HEADERS") {
      // Zero length, type CONTINUATION, flags END_HEADERS, stream id 0.
      this.push(Buffer.from([0, 0, 0, 0x09, 0x04, 0, 0, 0, 0]));
      Serializer.prototype._transform = originalTransform;
    }
    originalTransform.apply(this, arguments);
  }

  static content(pathName) {
    // "You Win!" is used by tests to check that they reached this server.
    return (
      "<!doctype html><html><head><title>HOORAY!</title></head>" +
      `<body>You Win! (by requesting${pathName})</body></html>`
    );
  }

  // Sends the bodies of /multiplex1 and /multiplex2 interleaved, once both
  // requests arrived.
  static multiplex(name, res) {
    const self = Http2TestServerCode;
    self.multiplexResponses[name] = res;
    const { multiplex1, multiplex2 } = self.multiplexResponses;
    if (!multiplex1 || !multiplex2) {
      return;
    }
    self.multiplexResponses = { multiplex1: null, multiplex2: null };
    const buf = "0".repeat(30 * 1024);
    let send = (r, start) => {
      let end = Math.min(start + 1024, buf.length);
      r.write(buf.substring(start, end));
      if (end < buf.length) {
        setTimeout(() => send(r, end), 10);
      } else {
        r.end();
      }
    };
    send(multiplex1, 0);
    setTimeout(() => send(multiplex2, 0), 5);
  }

  // eslint-disable-next-line complexity
  static handleRequest(req, res) {
    const crypto = require("crypto");
    const u = require("url").parse(req.url, true);
    const self = Http2TestServerCode;
    let content = self.content(u.pathname);

    if (req.httpVersionMajor === 2) {
      res.setHeader("X-Connection-Http2", "yes");
      res.setHeader("X-Http2-StreamId", "" + req.stream.id);
    } else {
      res.setHeader("X-Connection-Http2", "no");
    }

    if (u.pathname === "/750ms") {
      setTimeout(() => {
        res.writeHead(200);
        res.end("It's all good 750ms.");
      }, 750);
      return;
    } else if (
      (u.pathname === "/multiplex1" || u.pathname === "/multiplex2") &&
      req.httpVersionMajor === 2
    ) {
      res.setHeader("Content-Type", "text/plain");
      res.writeHead(200);
      self.multiplex(u.pathname.slice(1), res);
      return;
    } else if (u.pathname === "/header") {
      let val = req.headers["x-test-header"];
      if (val) {
        res.setHeader("X-Received-Test-Header", val);
      }
    } else if (u.pathname === "/doubleheader") {
      res.setHeader("Content-Type", "text/html");
      res.writeHead(200);
      res.write(content);
      res.writeHead(200);
      res.end();
      return;
    } else if (u.pathname === "/cookie_crumbling") {
      res.setHeader(
        "X-Received-Header-Pairs",
        JSON.stringify(self.decompressedPairs)
      );
    } else if (u.pathname === "/big") {
      content = "0".repeat(128 * 1024);
      let hash = crypto.createHash("md5");
      hash.update(content);
      res.setHeader("X-Expected-MD5", hash.digest("hex"));
    } else if (u.pathname === "/huge") {
      res.setHeader("Content-Type", "text/plain");
      res.writeHead(200);
      // 1MB of data, in 1KB chunks.
      const chunk = "0".repeat(1024);
      for (let i = 0; i < 1024; i++) {
        res.write(chunk);
      }
      res.end();
      return;
    } else if (u.pathname === "/post" || u.pathname === "/patch") {
      if (req.method != "POST" && req.method != "PATCH") {
        res.writeHead(405);
        res.end("Unexpected method: " + req.method);
        return;
      }
      let postHash = crypto.createHash("md5");
      let receivedData = false;
      req.on("data", chunk => {
        receivedData = true;
        postHash.update(chunk.toString());
      });
      req.on("end", () => {
        res.setHeader(
          "X-Calculated-MD5",
          receivedData ? postHash.digest("hex") : "0"
        );
        res.writeHead(200);
        res.end(content);
      });
      return;
    } else if (u.pathname === "/750msPost") {
      if (req.method != "POST") {
        res.writeHead(405);
        res.end("Unexpected method: " + req.method);
        return;
      }
      let accum = 0;
      req.on("data", chunk => {
        accum += chunk.length;
      });
      req.on("end", () => {
        res.setHeader("X-Recvd", accum);
        setTimeout(() => {
          res.writeHead(200);
          res.end("It's all good 750ms.");
        }, 750);
      });
      return;
    } else if (u.pathname === "/h11required_stream") {
      if (req.httpVersionMajor === 2) {
        self.h11requiredConn = req.stream.connection;
        res.stream.reset("HTTP_1_1_REQUIRED");
        return;
      }
    } else if (u.pathname === "/bigdownload") {
      res.setHeader("Content-Type", "text/html");
      res.writeHead(200);
      // 3MB of data, in 1MB chunks.
      let iter = 3;
      let sendChunk = () => {
        res.write("0".repeat(1024 * 1024));
        if (--iter) {
          setTimeout(sendChunk, 1);
        } else {
          res.end();
        }
      };
      setTimeout(sendChunk, 1);
      return;
    } else if (u.pathname === "/h11required_session") {
      if (req.httpVersionMajor === 2) {
        if (self.h11requiredConn !== req.stream.connection) {
          self.h11requiredHeader = "no";
        }
        res.stream.connection.close("HTTP_1_1_REQUIRED", res.stream.id - 2);
        return;
      }
      res.setHeader("X-H11Required-Stream-Ok", self.h11requiredHeader);
    } else if (u.pathname === "/rstonce") {
      if (!self.didRst && req.httpVersionMajor === 2) {
        self.didRst = true;
        self.rstConnection = req.stream.connection;
        req.stream.reset("REFUSED_STREAM");
        return;
      }

      if (req.httpVersionMajor != 2) {
        res.setHeader("Connection", "close");
      }
      if (
        self.rstConnection === null ||
        self.rstConnection !== req.stream.connection
      ) {
        res.writeHead(400);
        res.end("WRONG CONNECTION, HOMIE!");
        return;
      }

      self.didRst = false;
      self.rstConnection = null;
      res.writeHead(200);
      res.end("It's all good.");
      return;
    } else if (u.pathname === "/unknown_rst_once") {
      // The first HTTP/2 request is reset with an unknown error code, and the
      // retry succeeds.
      if (!self.didUnknownRst && req.httpVersionMajor === 2) {
        self.didUnknownRst = true;
        req.stream.reset(0xfe);
        return;
      }
      self.didUnknownRst = false;
      res.setHeader("Content-Type", "text/html");
      res.writeHead(200);
      res.end("It's all good.");
      return;
    } else if (u.pathname === "/hugecontinuedheaders") {
      for (let i = 0; i < u.query.size; i++) {
        res.setHeader(
          "X-Test-Header-" + i,
          "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789".repeat(1024)
        );
      }
      res.writeHead(200);
      res.end(content);
      return;
    } else if (u.pathname === "/illegalhpacksoft") {
      // node-http2 encodes this header illegally, failing only the stream.
      self.illegalheaderConn = req.stream.connection;
      res.setHeader("Content-Type", "text/html");
      res.setHeader("x-softillegalhpack", "true");
      res.writeHead(200);
      res.end(content);
      return;
    } else if (u.pathname === "/illegalhpackhard") {
      // node-http2 encodes this header illegally, failing the session.
      res.setHeader("Content-Type", "text/html");
      res.setHeader("x-hardillegalhpack", "true");
      res.writeHead(200);
      res.end(content);
      return;
    } else if (u.pathname === "/illegalhpack_validate") {
      res.setHeader(
        "X-Did-Goaway",
        req.stream.connection === self.illegalheaderConn ? "no" : "yes"
      );
    } else if (u.pathname === "/foldedheader") {
      res.setHeader("X-Folded-Header", "this is\n folded");
    } else if (u.pathname === "/emptydata") {
      self.Serializer.prototype._transform = self.emptyDataTransform;
    } else if (u.pathname === "/continuation_stream_zero") {
      self.Serializer.prototype._transform =
        self.continuationStreamZeroTransform;
    } else if (u.pathname === "/statusphrase") {
      // node-http2 lets a status phrase through, so that the client can be
      // tested to reject it.
      res.writeHead("200 OK");
      res.end(content);
      return;
    } else if (u.pathname === "/diskcache") {
      content = "this was pulled via h2";
    } else if (u.pathname.startsWith("/invalid_response_header/")) {
      // Header names and values with characters RFC 7540 section 10.3 forbids.
      let kind = u.pathname.slice("/invalid_response_header/".length);
      if (kind === "name_spaces") {
        res.setHeader("With Spaces", "Hello");
      } else if (kind === "value_line_feed") {
        res.setHeader("invalid-header", "line\nfeed");
      } else if (kind === "value_carriage_return") {
        res.setHeader("invalid-header", "carriage\rreturn");
      } else if (kind === "value_null") {
        res.setHeader("invalid-header", "null\0");
      }
      res.writeHead(200);
      res.end("");
      return;
    }

    res.setHeader("Content-Type", "text/html");
    if (req.httpVersionMajor != 2) {
      res.setHeader("Connection", "close");
    }
    res.writeHead(200);
    res.end(content);
  }
}
