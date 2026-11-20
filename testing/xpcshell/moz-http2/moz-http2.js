/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Hosts the node processes that NodeServer.sys.mjs forks for tests, and an
// HTTP/2 server on MOZHTTP2_PORT that answers every request with a 200.

var node_http2_root = "../node-http2";
if (process.env.NODE_HTTP2_ROOT) {
  node_http2_root = process.env.NODE_HTTP2_ROOT;
}
var http2 = require(node_http2_root);
var fs = require("fs");
var url = require("url");
const { fork } = require("child_process");
const { spawn } = require("child_process");
const path = require("path");

function handleRequest(req, res) {
  res.setHeader("Content-Type", "text/html");
  if (req.httpVersionMajor != 2) {
    res.setHeader("Connection", "close");
  }
  res.writeHead(200);
  res.end("<!doctype html><html><body>You Win!</body></html>");
}

// Keeps the process, and the node processes it hosts, alive on exceptions
// thrown from asynchronous callbacks.
process.on("uncaughtException", function (err) {
  console.error("moz-http2: Uncaught exception (server continuing):", err);
});

// Set up the SSL certs for our server - this server has a cert for foo.example.com
// signed by netwerk/tests/unit/http2-ca.pem
var options = {
  key: fs.readFileSync(__dirname + "/http2-cert.key"),
  cert: fs.readFileSync(__dirname + "/http2-cert.pem"),
};

if (process.env.HTTP2_LOG !== undefined) {
  var log_module = node_http2_root + "/test/util";
  options.log = require(log_module).createLogger("server");
}

var server = http2.createServer(options, handleRequest);

server.on("connection", function (socket) {
  socket.on("error", function () {
    // Ignoring SSL socket errors, since they usually represent a connection that was tore down
    // by the browser because of an untrusted certificate. And this happens at least once, when
    // the first test case if done.
  });
});

server.on("connect", function (req, clientSocket) {
  clientSocket.write(
    "HTTP/1.1 404 Not Found\r\nProxy-agent: Node.js-Proxy\r\n\r\n"
  );
  clientSocket.destroy();
});

function makeid(length) {
  var result = "";
  var characters =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  var charactersLength = characters.length;
  for (var i = 0; i < length; i++) {
    result += characters.charAt(Math.floor(Math.random() * charactersLength));
  }
  return result;
}

let globalObjects = {};
var serverPort;

const listen = (serv, envport) => {
  if (!serv) {
    return Promise.resolve(0);
  }

  let portSelection = 0;
  if (envport !== undefined) {
    try {
      portSelection = parseInt(envport, 10);
    } catch (e) {
      portSelection = -1;
    }
  }
  return new Promise(resolve => {
    serv.listen(portSelection, "127.0.0.1", 2000, () => {
      resolve(serv.address().port);
    });
  });
};

const http = require("http");
let httpServer = http.createServer((req, res) => {
  if (req.method != "POST") {
    let u = url.parse(req.url, true);
    if (u.pathname == "/test") {
      // This path is used to test that the server is working properly
      res.writeHead(200);
      res.end("OK");
      return;
    }
    res.writeHead(405);
    res.end("Unexpected method: " + req.method);
    return;
  }

  let code = "";
  req.on("data", function receivePostData(chunk) {
    code += chunk;
  });
  req.on("end", function finishPost() {
    let u = url.parse(req.url, true);
    if (u.pathname == "/fork") {
      let id = forkProcess();
      computeAndSendBackResponse(id);
      return;
    }

    if (u.pathname == "/forkH3Server") {
      forkH3Server(u.query.path, u.query.dbPath)
        .then(result => {
          computeAndSendBackResponse(result);
        })
        .catch(error => {
          computeAndSendBackResponse(error);
        });
      return;
    }

    if (u.pathname.startsWith("/kill/")) {
      let id = u.pathname.slice(6);
      let forked = globalObjects[id];
      if (!forked) {
        computeAndSendBackResponse(undefined, new Error("could not find id"));
        return;
      }

      new Promise((resolve, reject) => {
        forked.resolve = resolve;
        forked.reject = reject;
        forked.kill();
      })
        .then(x =>
          computeAndSendBackResponse(
            undefined,
            new Error(`incorrectly resolved ${x}`)
          )
        )
        .catch(e => {
          // We indicate a proper shutdown by resolving with undefined.
          if (e && e.toString().match(/child process exit closing code/)) {
            e = undefined;
          }
          computeAndSendBackResponse(undefined, e);
        });
      return;
    }

    if (u.pathname.startsWith("/execute/")) {
      let id = u.pathname.slice(9);
      let forked = globalObjects[id];
      if (!forked) {
        computeAndSendBackResponse(undefined, new Error("could not find id"));
        return;
      }

      let messageId = makeid(6);
      new Promise((resolve, reject) => {
        forked.messageHandlers[messageId] = { resolve, reject };
        forked.send({ code, messageId });
      })
        .then(x => sendBackResponse(x))
        .catch(e => computeAndSendBackResponse(undefined, e));
    }

    function computeAndSendBackResponse(evalResult, e) {
      let output = { result: evalResult, error: "", errorStack: "" };
      if (e) {
        output.error = e.toString();
        output.errorStack = e.stack;
      }
      sendBackResponse(output);
    }

    function sendBackResponse(output) {
      output = JSON.stringify(output);

      res.setHeader("Content-Length", output.length);
      res.setHeader("Content-Type", "application/json");
      res.writeHead(200);
      res.write(output);
      res.end("");
    }
  });
});

function forkH3Server(serverPath, dbPath) {
  const args = [dbPath];
  const env = Object.assign({}, process.env, {
    RUST_BACKTRACE: "full",
  });
  let child = spawn(serverPath, args, { env });
  let id = forkProcessInternal(child);
  // Return a promise that resolves when we receive data from stdout
  return new Promise((resolve, _) => {
    child.stdout.on("data", data => {
      console.log(data.toString());
      resolve({ id, output: data.toString().trim() });
    });

    child.stderr.on("data", chunk => {
      const s = chunk.toString();
      console.log(`[child stderr] ${s}`);
    });
  });
}

function forkProcess() {
  let scriptPath = path.resolve(__dirname, "moz-http2-child.js");
  let forked = fork(scriptPath);
  return forkProcessInternal(forked);
}

function forkProcessInternal(forked) {
  let id = makeid(6);
  forked.errors = "";
  forked.messageHandlers = {};
  globalObjects[id] = forked;
  forked.on("message", msg => {
    if (msg.messageId && forked.messageHandlers[msg.messageId]) {
      let handler = forked.messageHandlers[msg.messageId];
      delete forked.messageHandlers[msg.messageId];
      handler.resolve(msg);
    } else {
      console.log(
        `forked process without handler sent: ${JSON.stringify(msg)}`
      );
      forked.errors += `forked process without handler sent: ${JSON.stringify(
        msg
      )}\n`;
    }
  });

  let exitFunction = (code, signal) => {
    if (globalObjects[id]) {
      delete globalObjects[id];
    } else {
      // already called
      return;
    }

    let errorMsg = `child process exit closing code: ${code} signal: ${signal}`;
    if (forked.errors != "") {
      errorMsg = forked.errors;
      forked.errors = "";
    }

    // Handle /kill/ case where forked.reject is set
    if (forked.reject) {
      forked.reject(errorMsg);
      forked.reject = null;
      forked.resolve = null;
    }

    if (Object.keys(forked.messageHandlers).length === 0) {
      return;
    }

    for (let messageId in forked.messageHandlers) {
      forked.messageHandlers[messageId].reject(errorMsg);
    }
    forked.messageHandlers = {};
  };

  forked.on("error", exitFunction);
  forked.on("close", exitFunction);
  forked.on("exit", exitFunction);

  return id;
}

Promise.all([
  listen(server, process.env.MOZHTTP2_PORT).then(port => (serverPort = port)),
  listen(httpServer, process.env.MOZNODE_EXEC_PORT),
]).then(([sPort, nodeExecPort]) => {
  console.log(`HTTP2 server listening on ports ${sPort},${nodeExecPort}`);
});
