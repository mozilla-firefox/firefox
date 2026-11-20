/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
//  HTTP Server-Timing header test
//

"use strict";

function make_and_open_channel(url, callback) {
  let chan = NetUtil.newChannel({ uri: url, loadUsingSystemPrincipal: true });
  chan.asyncOpen(new ChannelListener(callback, null, CL_ALLOW_UNKNOWN_CL));
}

var responseServerTiming = [
  { metric: "metric", duration: "123.4", description: "description" },
  { metric: "metric2", duration: "456.78", description: "description1" },
];
var trailerServerTiming = [
  { metric: "metric3", duration: "789.11", description: "description2" },
  { metric: "metric4", duration: "1112.13", description: "description3" },
];

function run_test() {
  do_test_pending();

  do_get_profile();

  Services.prefs.setCharPref("network.dns.localDomains", "foo.example.com");
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.dns.localDomains");
  });

  startServer().then(serverPort => {
    make_and_open_channel(
      "https://foo.example.com:" + serverPort + "/server-timing",
      readServerContent
    );
  });
}

async function startServer() {
  let server = await create_h2_server({
    "/server-timing": (req, res) => {
      res.setHeader("Content-Type", "text/plain");
      res.setHeader("Content-Length", "12");
      res.setHeader("Trailer", "Server-Timing");
      res.setHeader(
        "Server-Timing",
        "metric; dur=123.4; desc=description, metric2; dur=456.78; desc=description1"
      );
      res.write("data reached");
      res.addTrailers({
        "Server-Timing":
          "metric3; dur=789.11; desc=description2, metric4; dur=1112.13; desc=description3",
      });
      res.end();
    },
  });
  return server.port();
}

function checkServerTimingContent(headers) {
  var expectedResult = responseServerTiming.concat(trailerServerTiming);
  Assert.equal(headers.length, expectedResult.length);

  for (var i = 0; i < expectedResult.length; i++) {
    let header = headers.queryElementAt(i, Ci.nsIServerTiming);
    Assert.equal(header.name, expectedResult[i].metric);
    Assert.equal(header.description, expectedResult[i].description);
    Assert.equal(header.duration, parseFloat(expectedResult[i].duration));
  }
}

function readServerContent(request) {
  let channel = request.QueryInterface(Ci.nsITimedChannel);
  let headers = channel.serverTiming.QueryInterface(Ci.nsIArray);
  checkServerTimingContent(headers);
  do_test_finished();
}
