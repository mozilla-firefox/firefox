# HTTP Server for Unit Tests

This page describes the JavaScript implementation of an
HTTP server located in `netwerk/test/httpserver/`.

## Server Functionality

Here are some of the things you can do with the server:

- map a directory of files onto an HTTP path on the server, for an
  arbitrary number of such directories (including nested directories)
- define custom error handlers for HTTP error codes
- serve a given file for requests for a specific path, optionally with
  custom headers and status
- define custom "CGI" handlers for specific paths using a
  JavaScript-based API to create the response (headers and actual
  content)
- run multiple servers at once on different ports (8080, 8081, 8082,
  and so on.)

This functionality should be more than enough for you to use it with any
test which requires HTTP-provided behavior.

## Where You Can Use It

The server is written primarily for use from `xpcshell`-based
tests, and it can be used as an inline script or as an XPCOM component. The
Mochitest framework also uses it to serve its tests, and
{searchfox}`reftests <layout/tools/reftest/README.txt>`
can optionally use it when their behavior is dependent upon specific
HTTP header values.

## Ways You Might Use It

- application update testing
- cross-"server" security tests
- cross-domain security tests, in combination with the right proxy
  settings (for example, using [Proxy
  AutoConfig](https://en.wikipedia.org/wiki/Proxy_auto-config))
- tests where the behavior is dependent on the values of HTTP headers
  (for example, Content-Type)
- anything which requires use of files not stored locally
- open-id : the users could provide their own open id server (they only
  need it when they're using their browser)
- micro-blogging : users could host their own micro blog based on
  standards like RSS/Atom
- rest APIs : web application could interact with REST or SOAP APIs for
  many purposes like : file/data storage, social sharing and so on
- download testing

## Using the Server

The best and first place you should look for documentation is
`netwerk/test/httpserver/nsIHttpServer.idl`. It's extremely
comprehensive and detailed, and it should be enough to figure out how to
make the server do what you want. I also suggest taking a look at the
less-comprehensive server
{searchfox}`README <netwerk/test/httpserver/README>`,
although the IDL should usually be sufficient.

### Running the Server

From test suites, the server should be importable as a testing-only JS
module:

```JavaScript
ChromeUtils.importESModule("resource://testing-common/httpd.sys.mjs");
```

Once you've done that, you can create a new server as follows:

```JavaScript
let server = new HttpServer(); // Or nsHttpServer() if you don't use ChromeUtils.importESModule.

server.registerDirectory("/", nsILocalFileForBasePath);

server.start(-1); // uses a random available port, allows us to run tests concurrently
const SERVER_PORT = server.identity.primaryPort; // you can use this further on

// and when the tests are done, most likely from a callback...
server.stop(function() { /* continue execution here */ });
```

You can also pass in a numeric port argument to the `start()` method,
but we strongly suggest you don't do it. Using a dynamic port allow us
to run your test in parallel with other tests which reduces wait times
and makes everybody happy. If you really have to use a hardcoded port,
you will have to annotate your test in the xpcshell manifest file with
`run-sequentially = REASON`.
However, this should only be used as the last possible option.

:::{note}
You **must** make sure to stop the server (the last line above)
before your test completes. Failure to do so will result in the
"XPConnect is being called on a scope without a Components property"
assertion, which will cause your test to fail in debug builds, and
you'll make people running tests grumbly because you've broken the
tests.
:::

### Debugging Errors

The server's default error pages don't give much information, partly
because the error-dispatch mechanism doesn't currently accommodate doing
so and partly because exposing errors in a real server could make it
easier to exploit them. If you don't know why the server is acting a
particular way, edit
{searchfox}`httpd.sys.mjs <netwerk/test/httpserver/httpd.sys.mjs>`
and change the value of `DEBUG` to `true`. This will cause the
server to print information about the processing of requests (and errors
encountered doing so) to the console, and it's usually not difficult to
determine why problems exist from that output. `DEBUG` is `false` by
default because the information printed with it set to `true`
unnecessarily obscures tinderbox output.

### Header Modification for Files

The server supports modifying the headers of the files (not request
handlers) it serves. To modify the headers for a file, create a sibling
file with the first file's name followed by `^headers^`. Here's an
example of how such a file might look:

```text
HTTP 404 I want a cool HTTP description!
Content-Type: text/plain
```

The status line is optional; all other lines specify HTTP headers in the
standard HTTP format. Any line ending style is accepted, and the file
may optionally end with a single newline character, to play nice with
Unix text tools like `diff` and `git`.

### Hidden Files

Any file which ends with a single `^` is inaccessible when querying
the web server; if you try to access such a file you'll get a
`404 File Not Found` page instead. If for some reason you need to
serve a file ending with a `^`, just tack another `^` onto the end
of the file name and the file will then become available at the
single-`^` location.

At the moment this feature is basically a way to smuggle header
modification for files into the file system without making those files
accessible to clients; it remains to be seen whether and how hidden-file
capabilities will otherwise be used.

### SJS: Server-Side Scripts

Support for server-side scripts is provided through the SJS mechanism.
Essentially an SJS is a file with a particular extension, chosen by the
creator of the server, which contains a function with the name
`handleRequest` which is called to determine the response the server
will generate. That function acts exactly like the `handle` function
on the `nsIHttpRequestHandler` interface. First, tell the server what
extension you're using:

```JavaScript
const SJS_EXTENSION = "cgi";
server.registerContentType(SJS_EXTENSION, "sjs");
```

Now just create an SJS with the extension `cgi` and write whatever you
want. For example:

```JavaScript
function handleRequest(request, response)
{
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.write("Hello world!  This request was dynamically " +
                 "generated at " + new Date().toUTCString());
}
```

Further examples may be found [in the Mozilla source
tree](https://searchfox.org/mozilla-central/search?q=&path=.sjs)
in existing tests. The request object is an instance of
`nsIHttpRequest` and the response is a `nsIHttpResponse`.
Please refer to the `IDL
documentation <https://searchfox.org/firefox-main/source/netwerk/test/httpserver/nsIHttpServer.idl>`
for more details.

### Storing Information Across Requests

HTTP is basically a stateless protocol, and the httpd.js server API is
for the most part similarly stateless. If you're using the server
through the XPCOM interface you can simply store whatever state you want
in enclosing environments or global variables. However, if you're using
it through an SJS your request is processed in a near-empty environment
every time processing occurs. To support stateful SJS behavior, the
following functions have been added to the global scope in which a SJS
handler executes, providing a simple key-value state storage mechanism:

```JavaScript
/*
 * v : T means v is of type T
 * function A() : T means A() has type T
 */

function getState(key : string) : string
function setState(key : string, value : string)
function getSharedState(key : string) : string
function setSharedState(key : string, value : string)
function getObjectState(key : string, callback : function(value : object) : void) // SJS API, XPCOM differs, see below
function setObjectState(key : string, value : object)
```

A key is a string with arbitrary contents. The corresponding value is
also a string, for the non-object-saving functions. For the
object-saving functions, it is (wait for it) an object, or also
`null`. Initially all keys are associated with the empty string or
with `null`, depending on whether the function accesses string- or
object-valued storage. A stored value persists across requests and
across server shutdowns and restarts. The state methods are available
both in SJS and, for convenience when working with the server both via
XPCOM and via SJS, XPCOM through the `nsIHttpServer` interface. The
variants are designed to support different needs.

:::{warning}
**Warning:** Be careful using state: you, the user, are responsible
for synchronizing all uses of state through any of the available
methods. (This includes the methods that act only on per-path state:
you might still run into trouble there if your request handler
generates responses asynchronously. Further, any code with access to
the server XPCOM component could modify it between requests even if
you only ever used or modified that state while generating
synchronous responses.) JavaScript's run-to-completion behavior will
save you in simple cases, but with anything moderately complex you
are playing with fire, and if you do it wrong you will get burned.
:::

#### `getState` and `setState`

`getState` and `setState` are designed for the case where a single
request handler needs to store information from a first request of it
for use in processing a second request of it — say, for example, if you
wanted to implement a request handler implementing a counter:

```JavaScript
/**
 * Generates a response whose body is "0", "1", "2", and so on. each time a
 * request is made.  (Note that browser caching might make it appear
 * to not quite have that behavior; a Cache-Control header would fix
 * that issue if desired.)
 */
function handleRequest(request, response)
{
  var counter = +getState("counter"); // convert to number; +"" === 0
  response.write("" + counter);
  setState("counter", "" + ++counter);
}
```

The useful feature of these two methods is that this state doesn't bleed
outside the single path at which it resides. For example, if the above
SJS were at `/counter`, the value returned by `getState("counter")`
at some other path would be completely distinct from the counter
implemented above. This makes it much simpler to write stateful handlers
without state accidentally bleeding between unrelated handlers.

:::{note}
State saved by this method is specific to the HTTP path,
excluding query string and hash reference. `/counter`,
`/counter?foo`, and `/counter?bar#baz` all share the same state
for the purposes of these methods. (Indeed, non-shared state would be
significantly less useful if it changed when the query string
changed!)
:::

:::{note}
The predefined `__LOCATION__` state
contains the native path of the SJS file itself. You can pass the
result directly to the `nsILocalFile.initWithPath()`. Example:
`thisSJSfile.initWithPath(getState('__LOCATION__'));`
:::

#### `getSharedState` and `setSharedState`

`getSharedState` and `setSharedState` make up the functionality
intentionally not supported by `getState` and set`State`: state
that exists between different paths. If you used the above handler at
the paths `/sharedCounters/1` and `/sharedCounters/2` (changing the
state-calls to use shared state, of course), the first load of either
handler would return "0", a second load of either handler would return
"1", a third load either handler would return "2", and so on. This more
powerful functionality allows you to write cooperative handlers that
expose and manipulate a piece of shared state. Be careful! One test can
screw up another test pretty easily if it's not careful what it does
with this functionality.

#### `getObjectState` and `setObjectState`

`getObjectState` and `setObjectState` support the remaining
functionality not provided by the above methods: storing non-string
values (object values or `null`). These two methods are the same as
`getSharedState` and `setSharedState`in that state is visible
across paths; `setObjectState` in one handler will expose that value
in another handler that uses `getObjectState` with the same key. (This
choice was intentional, because object values already expose mutable
state that you have to be careful about using.) This functionality is
particularly useful for cooperative request handlers where one request
*suspends* another, and that second request must then be *resumed* at a
later time by a third request. Without object-valued storage you'd need
to resort to polling on a string value using either of the previous
state APIs; with this, however, you can make precise callbacks exactly
when a particular event occurs.

`getObjectState` in an SJS differs in one important way from
`getObjectState` accessed via XPCOM. In XPCOM the method takes a
single string argument and returns the object or `null` directly. In
SJS, however, the process to return the value is slightly different:

```JavaScript
function handleRequest(request, response)
{
  var key = request.hasHeader("key")
          ? request.getHeader("key")
          : "unspecified";
  var obj = null;
  getObjectState(key, function(objval)
  {
    // This function is called synchronously with the object value
    // associated with key.
    obj = objval;
  });
  response.write("Keyed object " +
                 (obj && Object.prototype.hasOwnProperty.call(obj, "doStuff")
                 ? "has "
                 : "does not have ") +
                 "a doStuff method.");
}
```

This idiosyncratic API is a restriction imposed by how sandboxes
currently work: external functions added to the sandbox can't return
object values when called within the sandbox. However, such functions
can accept and call callback functions, so we simply use a callback
function here to return the object value associated with the key.

### Advanced Dynamic Response Creation

The default behavior of request handlers is to fully construct the
response, return, and only then send the generated data. For certain use
cases, however, this is infeasible. For example, a handler which wanted
to return an extremely large amount of data (say, over 4GB on a 32-bit
system) might run out of memory doing so. Alternatively, precise control
over the timing of data transmission might be required so that, for
example, one request is received, "paused" while another request is
received and completes, and then finished. httpd.js solves this problem
by defining a `processAsync()` method which indicates to the server
that the response will be written and finished by the handler. Here's an
example of an SJS file which writes some data, waits five seconds, and
then writes some more data and finishes the response:

```JavaScript
var timer = null;

function handleRequest(request, response)
{
  response.processAsync();
  response.setHeader("Content-Type", "text/plain", false);
  response.write("hello...");

  timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  timer.initWithCallback(function()
  {
    response.write("world!");
    response.finish();
  }, 5 * 1000 /* milliseconds */, Ci.nsITimer.TYPE_ONE_SHOT);
}
```

The basic flow is simple: call `processAsync` to mark the response as
being sent asynchronously, write data to the response body as desired,
and when complete call `finish()`. At the moment if you drop such a
response on the floor, nothing will ever terminate the connection, and
the server cannot be stopped (the stop API is asynchronous and
callback-based); in the future a default connection timeout will likely
apply, but for now, "don't do that".

Full documentation for `processAsync()` and its interactions with
other methods may, as always, be found in
`netwerk/test/httpserver/nsIHttpServer.idl`.

### Manual, Arbitrary Response Creation

The standard mode of response creation is fully synchronous and is
guaranteed to produce syntactically correct responses (excluding
headers, which for the most part may be set to arbitrary values).
Asynchronous processing enables the introduction of response handling
coordinated with external events, but again, for the most part only
syntactically correct responses may be generated. The third method of
processing removes the correct-syntax property by allowing a response to
contain completely arbitrary data through the `seizePower()` method.
After this method is called, any data subsequently written to the
response is written directly to the network as the response, skipping
headers and making no attempt whatsoever to ensure any formatting of the
transmitted data. As with asynchronous processing, the response is
generated asynchronously and must be finished manually for the
connection to be closed. (Again, nothing will terminate the connection
for a response dropped on the floor, so again, "don't do that".) This
mode of processing is useful for testing particular data formats that
are either not HTTP or which do not match the precise, canonical
representation that httpd.js generates. Here's an example of an SJS file
which writes an apparent HTTP response whose status text contains a null
byte (not allowed by HTTP/1.1, and attempting to set such status text
through httpd.js would throw an exception) and which has a header that
spans multiple lines (httpd.js responses otherwise generate only
single-line headers):

```JavaScript
function handleRequest(request, response)
{
  response.seizePower();
  response.write("HTTP/1.1 200 OK Null byte \u0000 makes this response malformed\r\n" +
                 "X-Underpants-Gnomes-Strategy:\r\n" +
                 " Phase 1: Collect underpants.\r\n" +
                 " Phase 2: ...\r\n" +
                 " Phase 3: Profit!\r\n" +
                 "\r\n" +
                 "FAIL");
  response.finish();
}
```

While the asynchronous mode is capable of producing certain forms of
invalid responses (through setting a bogus Content-Length header prior
to the start of body transmission, among others), it must not be used in
this manner. No effort will be made to preserve such implementation
quirks (indeed, some are even likely to be removed over time): if you
want to send malformed data, use `seizePower()` instead.

Full documentation for `seizePower()` and its interactions with other
methods may, as always, be found in
`netwerk/test/httpserver/nsIHttpServer.idl`.

## Example Uses of the Server

Shorter examples (for tests which only do one test):

- `netwerk/test/unit/test_bug331825.js`
- `netwerk/test/unit/test_httpcancel.js`
- `netwerk/test/unit/test_cookie_header.js`

Longer tests (where you'd need to do multiple async server requests):

- `netwerk/test/httpserver/test/test_setstatusline.js`
- `netwerk/test/unit/test_content_sniffer.js`
- `netwerk/test/unit/test_authentication.js`
- `netwerk/test/unit/test_event_sink.js`
- `netwerk/test/httpserver/test/`

Examples of modifying HTTP headers in files may be found at
`netwerk/test/httpserver/test/data/cern_meta/`.

## Future Directions

The server, while very functional, is not yet complete. There are a
number of things to fix and features to add, among them support for
pipelining, support for incrementally-received requests (rather than
buffering the entire body before invoking a request handler), and better
conformance to the MUSTs and SHOULDs of HTTP/1.1. If you have
suggestions for functionality or find bugs, file them in
[Testing-httpd.js](https://bugzilla.mozilla.org/enter_bug.cgi?product=Testing&component=General)
.
