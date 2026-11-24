# Node HTTP Servers for Testing

This page describes the Node.js-based HTTP server implementation located in `netwerk/test/httpserver/NodeServer.sys.mjs`. This system provides HTTP, HTTPS, HTTP/2, HTTP/3, WebSocket, and proxy servers for use in xpcshell tests.

## Overview

The NodeServer system allows tests to spawn Node.js-based HTTP servers that run in separate processes. Unlike the JavaScript-based httpd.sys.mjs server, these Node servers provide full support for modern protocols like HTTP/2, HTTP/3, WebSockets over HTTP/2, and various proxy configurations.

## Architecture

The NodeServer system consists of three main components:

### 1. NodeServer.sys.mjs (Test Interface)

This is the Firefox/XPCShell side interface that tests use to control Node servers. It provides:

- Server classes for different protocols (HTTP, HTTPS, HTTP/2, WebSocket, Proxies)
- Methods to start/stop servers
- Methods to execute code in the Node.js context
- Methods to register request handlers

### 2. runxpcshelltests.py (Test Harness)

The xpcshell test harness automatically starts the moz-http2.js server when tests run:

- Spawns `node moz-http2.js` as a subprocess
- Sets the `MOZNODE_EXEC_PORT` environment variable with the server's HTTP port
- Handles server lifecycle (startup/shutdown)

### 3. moz-http2.js (Node Server)

This Node.js process hosts the servers that tests start. It doesn't serve test
requests itself: every test starts the servers it needs. It:

- Listens on the port specified in `MOZNODE_EXEC_PORT`
- Provides special endpoints for process management:
  - `/fork` - Spawns a new Node.js child process
  - `/execute/{id}` - Executes code in a forked process
  - `/kill/{id}` - Terminates a forked process
  - `/forkH3Server` - Spawns an HTTP/3 server

## How It Works

### Server Startup Flow

```
runxpcshelltests.py
    |
    v
Spawns node process: node moz-http2/moz-http2.js
    |
    v
Sets MOZNODE_EXEC_PORT environment variable
    |
    v
moz-http2.js server starts listening on random port
    |
    v
Tests can now use NodeServer.sys.mjs to create servers
```

### Process Forking Flow

When a test creates a server (e.g., `new NodeHTTPServer()`):

```
Test calls server.start()
    |
    v
NodeServer.fork() sends POST to http://127.0.0.1:{MOZNODE_EXEC_PORT}/fork
    |
    v
moz-http2.js receives /fork request
    |
    v
Calls fork() to spawn moz-http2-child.js
    |
    v
Returns unique process ID to test
    |
    v
Test uses NodeServer.execute(id, code) to run code in child process
    |
    v
Code is sent via POST to /execute/{id}
    |
    v
moz-http2.js forwards code to child process via IPC
    |
    v
moz-http2-child.js receives message, runs eval(code)
    |
    v
Result is sent back through IPC chain to test
```

### Code Execution in Child Process

The child process (moz-http2-child.js) is extremely simple:

```javascript
process.on("message", msg => {
  const code = msg.code;
  let evalResult = eval(code);  // Execute the code
  process.send({ result: evalResult });  // Send result back
});
```

This allows tests to:

1. Define classes and functions in the Node.js context
2. Start HTTP servers
3. Register request handlers
4. Query server state

## Server Types

### NodeHTTPServer

Basic HTTP/1.1 server.

```javascript
const { NodeHTTPServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let server = new NodeHTTPServer();
await server.start(); // Random port
const port = server.port();
const origin = server.origin(); // http://localhost:{port}

// Register a path handler
await server.registerPathHandler("/test", (req, resp) => {
  resp.writeHead(200);
  resp.end("Hello World");
});

// When done
await server.stop();
```

### NodeHTTPSServer

HTTPS server using HTTP/1.1.

```javascript
const { NodeHTTPSServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let server = new NodeHTTPSServer();
await server.start(8443); // Specific port, or 0 for random
// Uses certificate from netwerk/test/unit/http2-cert.pem
```

### NodeHTTP2Server

HTTP/2 over TLS server.

```javascript
const { NodeHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let server = new NodeHTTP2Server();
await server.start();
// Or with custom hostnames covered by a freshly-generated leaf cert:
// await server.start(0, ["custom_domain.example"]);
// Supports HTTP/2 specific features like server push, multiplexing

// Check session count
let count = await server.sessionCount();
```

To also accept HTTP/1.1 clients, set `allowHTTP1` before starting the server:

```javascript
let server = new NodeHTTP2Server();
server.allowHTTP1 = true;
await server.start();
```

### NodeLegacyHTTP2Server

HTTP/2 over TLS server built on the vendored node-http2 library
(`testing/xpcshell/node-http2`) instead of Node's `http2` module. The library
can be patched to produce protocol errors on demand, such as illegal HPACK
encodings, so use this server only for tests that rely on such quirks.
`NodeHTTP2Server` is the right choice otherwise.

```javascript
const { NodeLegacyHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let server = new NodeLegacyHTTP2Server();
await server.start();

// Path handlers work as for the other servers. Alternatively, handle every
// request that isn't a CONNECT in one function:
await server.setRequestHandler((req, res) => {
  res.writeHead(200);
  res.end("ok");
});

// Handle CONNECT requests, making the server act as an HTTP/2 proxy:
await server.setConnectHandler((req, res) => {
  res.writeHead(200);
  res.end();
});
```

Exceptions thrown by handlers are caught, and the server answers with a 500.

### HTTP/3 Server

HTTP/3 (QUIC) server.

```javascript
const { HTTP3Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let server = new HTTP3Server();
let path = "/path/to/http3/server/binary";
let dbPath = "/path/to/quic/database";
await server.start(path, dbPath);
const port = server.port();
const masquePort = server.masque_proxy_port();
```

### NodeWebSocketServer

WebSocket server over HTTPS.

```javascript
const { NodeWebSocketServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let server = new NodeWebSocketServer();
await server.start();

// Register custom message handler
await server.registerMessageHandler((data, ws) => {
  ws.send("Echo: " + data);
});
```

### NodeWebSocketHttp2Server

WebSocket over HTTP/2 (RFC 8441).

```javascript
const { NodeWebSocketHttp2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let server = new NodeWebSocketHttp2Server();
await server.start(0, false); // port, fallbackToH1
```

### Proxy Servers

```javascript
const { NodeHTTPProxyServer, NodeHTTPSProxyServer, NodeHTTP2ProxyServer } =
  ChromeUtils.importESModule("resource://testing-common/NodeServer.sys.mjs");

// HTTP proxy
let httpProxy = new NodeHTTPProxyServer();
await httpProxy.start();

// HTTPS proxy
let httpsProxy = new NodeHTTPSProxyServer();
await httpsProxy.start();

// HTTP/2 proxy
let http2Proxy = new NodeHTTP2ProxyServer();
await http2Proxy.start(0, true, 100); // port, auth, maxConcurrentStreams
```

## Advanced Usage

### Registering Path Handlers

Path handlers are functions that process requests for specific paths:

```javascript
await server.registerPathHandler("/api/data", (req, resp) => {
  // req is Node's http.IncomingMessage
  // resp is Node's http.ServerResponse

  resp.setHeader("Content-Type", "application/json");
  resp.writeHead(200);
  resp.end(JSON.stringify({ status: "ok" }));
});
```

### Executing Arbitrary Code

You can execute any JavaScript code in the Node.js context:

```javascript
// Define a function
await server.execute(`
  function customHandler(req, resp) {
    resp.writeHead(200);
    resp.end("Custom response");
  }
`);

// Use the function
await server.execute(`global.path_handlers["/custom"] = customHandler`);

// Query state
let result = await server.execute(`Object.keys(global.path_handlers).length`);
```

### Passing Functions

You can pass JavaScript functions directly:

```javascript
function myHandler(req, resp) {
  resp.writeHead(200);
  resp.end("Handler from test");
}

// The function is serialized and defined in the Node context
await server.execute(myHandler);

// Now call it
await server.execute(`myHandler(someReq, someResp)`);
```

### Working with Global State

The Node.js child processes maintain global state:

```javascript
// Set up global variables
await server.execute(`global.requestCount = 0;`);

// Use in handlers
await server.registerPathHandler("/count", (req, resp) => {
  global.requestCount++;
  resp.writeHead(200);
  resp.end(`Request ${global.requestCount}`);
});

// Query state
let count = await server.execute(`global.requestCount`);
```

## Android Support

The system includes ADB port forwarding support for Android testing:

```javascript
// Automatically handled when MOZ_ANDROID_DATA_DIR is set
// The ADB class in NodeServer.sys.mjs forwards ports using:
// adb reverse tcp:{port} tcp:{port}
```

This means xpcshell-tests on Android can pretend to connect to `localhost:${port}` while the node server actually runs on the host.

## Certificate Handling

HTTPS and HTTP/2 servers automatically install test certificates:

- Certificate: `netwerk/test/unit/http2-cert.pem`
- CA: `netwerk/test/unit/http2-ca.pem`
- Key: `netwerk/test/unit/http2-cert.key`

Proxy servers use different certificates:

- Certificate: `netwerk/test/unit/proxy-cert.pem`
- CA: `netwerk/test/unit/proxy-ca.pem`
- Key: `netwerk/test/unit/proxy-cert.key`

To skip automatic certificate installation:

```javascript
let server = new NodeHTTPSServer();
server._skipCert = true;
await server.start();
```

The pre-shipped certificates are valid for the following domains: `localhost`, `foo.example.com`, `alt1.example.com`, `alt2.example.com`. Check `http2-cert.pem.certspec` and `proxy-cert.pem.certspec` for the up-to-date information.

### Custom hostnames

Each TLS-bearing server (`NodeHTTPSServer`, `NodeHTTP2Server`, `NodeTLSEchoServer`, `NodeWebSocketServer`, `NodeWebSocketHttp2Server`, `NodeHTTPSProxyServer`, `NodeHTTP2ProxyServer`) accepts an optional list of hostnames as the last argument of `start()`. When provided, the server mints a fresh leaf certificate covering those hostnames as DNS SANs (signed by the same key the existing test CA uses, so it chains under the already-installed `http2-ca.pem`):

```javascript
const customDomain = "custom_domain.example";

// Make Firefox resolve the custom hostname to 127.0.0.1.
Services.prefs.setCharPref("network.dns.localDomains", customDomain);

let server = new NodeHTTP2Server();
await server.start(0, [customDomain]);

// server.domain() and server.origin() now reflect the custom hostname.
let chan = makeChan(`${server.origin()}/test`); // https://custom_domain.example:<port>/test
```

Generation runs entirely with Node builtins (no extra `node_modules` deps). When `start()` is called without hostnames, the pre-shipped on-disk certificates are used unchanged, so existing tests are unaffected.

If you instead need to bypass cert validation altogether:

```javascript
const certOverrideService = Cc[
  "@mozilla.org/security/certoverride;1"
].getService(Ci.nsICertOverrideService);
certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyData(true);
```

## Best Practices

### Always Stop Servers

Always stop servers in cleanup to avoid resource leaks:

```javascript
registerCleanupFunction(async () => {
  await server.stop();
});
```

### Use Random Ports

Use port 0 (or omit the port parameter) to get a random available port:

```javascript
await server.start(); // Random port
// NOT: await server.start(8080); // Fixed port causes conflicts
```

### Helper Function for Multiple Server Types

Use the `with_node_servers` helper to test multiple server types:

```javascript
const { with_node_servers, NodeHTTPServer, NodeHTTP2Server } =
  ChromeUtils.importESModule("resource://testing-common/NodeServer.sys.mjs");

await with_node_servers(
  [NodeHTTPServer, NodeHTTP2Server],
  async server => {
    // This runs once for each server type
    let response = await fetch(server.origin() + "/test");
    // ... test code ...
  }
);
// Servers are automatically stopped
```

### Error Handling

Wrap server operations that may fail in try-catch blocks:

```javascript
try {
  await server.execute(`
    global.server.listen(port);
  `);
} catch (e) {
  // Handle execution errors
  console.error("Server setup failed:", e);
}
```

### Debugging

To debug issues, you can inspect the Node.js process:

```javascript
// Log in Node context
await server.execute(`console.log("Debug info:", someVariable)`);

// Check the xpcshell test output for Node.js console.log output
```

## Example Tests

### Simple HTTP Server Test

```javascript
add_task(async function test_simple_http_server() {
  let server = new NodeHTTPServer();
  await server.start();

  registerCleanupFunction(async () => {
    await server.stop();
  });

  await server.registerPathHandler("/hello", (req, resp) => {
    resp.writeHead(200, { "Content-Type": "text/plain" });
    resp.end("Hello, World!");
  });

  let response = await fetch(server.origin() + "/hello");
  let text = await response.text();
  Assert.equal(text, "Hello, World!");
});
```

### HTTP/2 Server Test

```javascript
add_task(async function test_http2_multiplexing() {
  let server = new NodeHTTP2Server();
  await server.start();

  registerCleanupFunction(async () => {
    await server.stop();
  });

  await server.registerPathHandler("/data", (req, resp) => {
    resp.writeHead(200);
    resp.end("data");
  });

  // Make multiple requests
  let responses = await Promise.all([
    fetch(server.origin() + "/data"),
    fetch(server.origin() + "/data"),
    fetch(server.origin() + "/data"),
  ]);

  // All requests should use the same HTTP/2 session
  let sessionCount = await server.sessionCount();
  Assert.equal(sessionCount, 1, "Should reuse single HTTP/2 session");
});
```

### WebSocket Test

```javascript
add_task(async function test_websocket() {
  let server = new NodeWebSocketServer();
  await server.start();

  registerCleanupFunction(async () => {
    await server.stop();
  });

  await server.registerMessageHandler((data, ws) => {
    ws.send("Echo: " + data);
  });

  let wsc = new WebSocketConnection();
  await wsc.open(server.origin().replace("https", "wss") + "/");
  wsc.send("test message");

  let messages = await wsc.receiveMessages();
  Assert.equal(messages[0], "Echo: test message");

  wsc.close();
  await wsc.finished();
});
```

### Proxy Test

```javascript
add_task(async function test_http_proxy() {
  let proxy = new NodeHTTPProxyServer();
  await proxy.start();

  registerCleanupFunction(async () => {
    await proxy.stop();
  });

  // Proxy filter is automatically registered
  // All HTTP requests will now go through the proxy

  let response = await fetch("http://example.com/");
  Assert.equal(response.status, 200);
});
```

### Async State Management Test

This test demonstrates concurrent async operations with proper result routing:

```javascript
add_task(async function test_async_state_management() {
  let server = new NodeHTTP2Server();
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });

  // Initialize state in the Node.js context
  await server.execute(`global.asyncResults = [];`);

  // Define an async function that takes time to complete
  await server.execute(`
    global.asyncCounter = 0;
    global.performAsyncOperation = function(delay, value) {
      return new Promise(resolve => {
        setTimeout(() => {
          global.asyncCounter++;
          global.asyncResults.push({ counter: global.asyncCounter, value });
          resolve({ counter: global.asyncCounter, value });
        }, delay);
      });
    };
  `);

  // Launch two concurrent async operations with different delays
  let op1 = server.execute(`performAsyncOperation(100, "first")`);
  let op2 = server.execute(`performAsyncOperation(50, "second")`);

  // Wait for both to complete
  let result1 = await op1;
  let result2 = await op2;

  // op2 completes first (50ms delay) so it gets counter=1
  equal(result2.counter, 1);
  equal(result2.value, "second");

  // op1 completes second (100ms delay) so it gets counter=2
  equal(result1.counter, 2);
  equal(result1.value, "first");

  // Verify the global state was updated correctly
  let results = await server.execute(`global.asyncResults`);
  equal(results.length, 2);
  equal(results[0].value, "second");  // First to complete
  equal(results[1].value, "first");   // Second to complete

  let counter = await server.execute(`global.asyncCounter`);
  equal(counter, 2);

  await server.stop();
});
```

This test demonstrates:
- Multiple concurrent `execute()` calls on the same server
- Each operation receives its correct result despite different completion times
- Global state is properly shared across executions
- The message handler system correctly routes responses to their respective promises

## Common Pitfalls

### Not Awaiting Async Operations

All server operations are asynchronous:

```javascript
// WRONG
server.start();
server.registerPathHandler("/test", handler);

// CORRECT
await server.start();
await server.registerPathHandler("/test", handler);
```

### Forgetting to Stop Servers

Servers must be explicitly stopped:

```javascript
// WRONG
add_task(async function test() {
  let server = new NodeHTTPServer();
  await server.start();
  // ... test code ...
  // Server is never stopped!
});

// CORRECT
add_task(async function test() {
  let server = new NodeHTTPServer();
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });
  // ... test code ...
});
```

### Hardcoded Ports

Avoid hardcoded ports as they can cause conflicts when tests run in parallel:

```javascript
// WRONG
await server.start(8080);

// CORRECT
await server.start(); // or await server.start(0);
let port = server.port();
```

Borrowing one of the HTTP/3 server's ports is no better: those are UDP ports, so
nothing reserves them for TCP. A test that wants UDP on its port left unanswered
rather than served can hold that side itself:

```javascript
await server.startWithNoResponseUdpPort();
let port = server.port(); // TCP served, UDP never answers

// A server that needs a different start step passes it in:
await proxy.startWithNoResponseUdpPort(() => proxy.startWithoutProxyFilter());
```

To put two servers on one port number, use `startOnSharedPort(start, claim, stop)`.
It runs `start()`, then `claim()` on the port that `start()` got, and on failure
runs `stop()` and retries. `create_masque_proxy_server()` in `head_http3.js` uses it
to start an HTTP/2 proxy on the MASQUE proxy's port.

### Scope Issues in Handlers

Remember that handlers run in the Node.js context, not the test context:

```javascript
// WRONG - testVariable is not accessible in Node.js
let testVariable = "value";
await server.registerPathHandler("/test", (req, resp) => {
  resp.end(testVariable); // ERROR: testVariable is undefined
});

// CORRECT - Pass values explicitly
let testVariable = "value";
await server.execute(`global.sharedValue = "${testVariable}"`);
await server.registerPathHandler("/test", (req, resp) => {
  resp.end(global.sharedValue);
});
```

## Implementation Details

### Process IDs

When you call `NodeServer.fork()`, the moz-http2.js server generates a random 6-character process ID. This ID is used to route commands to the correct child process.

### Communication Protocol

Communication uses HTTP POST requests with JSON payloads:

```
POST /execute/{processId}
Body: JavaScript code to execute

Response: { "result": <return value>, "error": "", "errorStack": "", "messageId": <id> }
```

### Message Handler System

The system uses a message handler architecture to support concurrent async operations:

1. Each `/execute/{processId}` request generates a unique 6-character `messageId`
2. A promise handler is stored in `forked.messageHandlers[messageId] = { resolve, reject }`
3. The `messageId` is sent to the child process along with the code
4. The child process returns the result with the same `messageId`
5. The response is routed to the correct promise handler using the `messageId`

This design allows multiple async operations to run concurrently on the same child process without interfering with each other. For example, you can call `server.execute()` multiple times in parallel and each will properly receive its own result.

### Eval-based Execution

Code execution uses `eval()` in the child process:

```javascript
// In moz-http2-child.js
process.on("message", msg => {
  const code = msg.code;
  const messageId = msg.messageId;
  let evalResult = eval(code);
  if (evalResult instanceof Promise) {
    evalResult
      .then(x => process.send({ result: x, messageId }))
      .catch(e => process.send({ error: e.toString(), messageId }));
  } else {
    process.send({ result: evalResult, messageId });
  }
});
```

This allows executing:

- Variable declarations
- Function definitions
- Expressions
- Async operations (Promise returns are handled automatically)
- Concurrent async operations without conflicts

### Function Serialization

When you pass a function to `execute()`, it's serialized:

```javascript
// You pass:
function handler(req, resp) { resp.end("ok"); }

// The system sends:
"handler = function handler(req, resp) { resp.end(\"ok\"); };"
```

## See Also

- `netwerk/test/httpserver/nsIHttpServer.idl` - JavaScript HTTP server
- `testing/xpcshell/moz-http2/moz-http2.js` - Node HTTP/2 server implementation
- `netwerk/test/unit/` - Example tests using NodeServer
- `netwerk/docs/http_server_for_testing.rst` - JavaScript-based httpd.sys.mjs server
