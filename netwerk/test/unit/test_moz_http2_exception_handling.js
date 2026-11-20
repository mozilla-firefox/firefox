"use strict";

const { NodeLegacyHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

function make_channel(url) {
  return NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
}

async function get_response(channel, flags = 0) {
  return new Promise(resolve => {
    channel.asyncOpen(
      new ChannelListener(
        (request, buffer) => {
          request.QueryInterface(Ci.nsIHttpChannel);
          resolve({ status: request.responseStatus, data: buffer });
        },
        null,
        flags
      )
    );
  });
}

// Hit a handler that throws. The server should catch it and return 500, and
// keep serving the requests that follow.
add_task(async function test_exception_does_not_kill_server() {
  do_get_profile();
  let server = new NodeLegacyHTTP2Server();
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });
  await server.registerPathHandler("/exception-test", () => {
    throw new Error("Intentional test exception from /exception-test");
  });
  await server.registerPathHandler("/header", (req, res) => {
    res.writeHead(200);
    res.end("ok");
  });
  const port = server.port();

  const origin = `https://localhost:${port}`;

  let resp = await get_response(
    make_channel(`${origin}/exception-test`),
    CL_ALLOW_UNKNOWN_CL
  );
  Assert.equal(resp.status, 500, "exception-test should return 500");

  resp = await get_response(
    make_channel(`${origin}/header`),
    CL_ALLOW_UNKNOWN_CL
  );
  Assert.equal(
    resp.status,
    200,
    "server should still be alive after exception"
  );
});
