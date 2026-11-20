/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Hosts the node processes that NodeServer.sys.mjs forks for tests.

var url = require("url");
const { fork } = require("child_process");
const { spawn } = require("child_process");
const path = require("path");

// Keeps the process, and the node processes it hosts, alive on exceptions
// thrown from asynchronous callbacks.
process.on("uncaughtException", function (err) {
  console.error("moz-http2: Uncaught exception (server continuing):", err);
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

const listen = (serv, envport) => {
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

// The harnesses parse this line. The first port belonged to an HTTP/2 server
// that no longer exists, and is always 0.
listen(httpServer, process.env.MOZNODE_EXEC_PORT).then(nodeExecPort => {
  const sPort = 0;
  console.log(`HTTP2 server listening on ports ${sPort},${nodeExecPort}`);
});
