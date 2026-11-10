/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

let trrServer;
let h3Port;
let host;

add_setup(async function setup() {
  trr_test_setup();

  h3Port = Services.env.get("MOZHTTP3_PORT_ECH");
  Assert.notEqual(h3Port, null);
  Assert.notEqual(h3Port, "");

  host = `https://alt1.example.com:${h3Port}/`;

  Services.prefs.setIntPref("network.trr.mode", Ci.nsIDNSService.MODE_TRRFIRST);
});

registerCleanupFunction(async () => {
  trr_clear_prefs();
  if (trrServer) {
    await trrServer.stop();
  }
});

function makeChan(url) {
  let chan = NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
  }).QueryInterface(Ci.nsIHttpChannel);
  return chan;
}

function channelOpenPromise(chan, flags) {
  return new Promise(resolve => {
    function finish(req, buffer) {
      resolve([req, buffer]);
    }
    chan.asyncOpen(new ChannelListener(finish, null, flags));
  });
}

function ActivityObserver() {
  this.activites = [];
}

ActivityObserver.prototype = {
  activites: [],
  observeActivity(
    aHttpChannel,
    aActivityType,
    aActivitySubtype,
    aTimestamp,
    aExtraSizeData,
    aExtraStringData
  ) {
    try {
      aHttpChannel.QueryInterface(Ci.nsINullChannel);
      aHttpChannel.QueryInterface(Ci.nsIChannel);
      if (aHttpChannel.URI.spec === host) {
        dump(
          "*** HTTP Activity 0x" +
            aActivityType.toString(16) +
            " 0x" +
            aActivitySubtype.toString(16) +
            " " +
            aExtraStringData +
            "\n"
        );
        this.activites.push({ host, subType: aActivitySubtype });
      }
    } catch (e) {}
  },
};

function checkHttpActivities(activites, expected) {
  let foundTransClosed = false;
  for (let activity of activites) {
    switch (activity.subType) {
      case Ci.nsIHttpActivityObserver.ACTIVITY_SUBTYPE_TRANSACTION_CLOSE:
        foundTransClosed = true;
        break;
      default:
        break;
    }
  }

  Assert.equal(
    foundTransClosed,
    expected,
    "The activity of speculative transaction should match"
  );
}
// Test steps:
// 1. Create a TRR server that serves the HTTPS record for the H3 server.
// 2. Create a channel and connect to the H3 server.
// 3. Use nsIHttpActivityObserver to observe if we receive the HTTP activity
//    that is sent from the speculative transaction.
async function doTestAltSVCWithHTTPSRR(foundActivity) {
  trrServer = new TRRServer();
  await trrServer.start();

  let observerService = Cc[
    "@mozilla.org/network/http-activity-distributor;1"
  ].getService(Ci.nsIHttpActivityDistributor);

  Services.prefs.setIntPref("network.trr.mode", Ci.nsIDNSService.MODE_TRRONLY);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  let portPrefixedName = `_${h3Port}._https.alt1.example.com`;
  let vals = [
    { key: "alpn", value: "h3" },
    { key: "port", value: h3Port },
  ];

  await trrServer.registerDoHAnswers(portPrefixedName, "HTTPS", {
    answers: [
      {
        name: portPrefixedName,
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: ".",
          values: vals,
        },
      },
    ],
  });

  await trrServer.registerDoHAnswers("alt1.example.com", "A", {
    answers: [
      {
        name: "alt1.example.com",
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  let chan = makeChan(`${host}alt_svc_header`);
  let h3AltSvc = ":" + h3Port;
  chan.setRequestHeader("x-altsvc", h3AltSvc, false);
  let observer = new ActivityObserver();

  let responseObserver = {
    QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
    observe(aSubject, aTopic) {
      let channel = aSubject.QueryInterface(Ci.nsIChannel);
      if (
        aTopic == "http-on-examine-response" &&
        channel.URI.spec === `${host}alt_svc_header`
      ) {
        Services.obs.removeObserver(
          responseObserver,
          "http-on-examine-response"
        );

        observerService.addObserver(observer);
        channel.suspend();
        // We need to close all connections here, otherwise we are not allowed
        // to create a specul connection to validate the Alt-svc header.
        Services.obs.notifyObservers(null, "net:cancel-all-connections");
        // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
        setTimeout(function () {
          channel.resume();
        }, 3000);
      }
    },
  };
  Services.obs.addObserver(responseObserver, "http-on-examine-response");

  await channelOpenPromise(chan);

  // Some dekay here to collect HTTP activites.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 3000));

  checkHttpActivities(observer.activites, foundActivity);

  await trrServer.stop();
  observerService.removeObserver(observer);
}

add_task(async function testAltSVCWithHTTPSRR() {
  Services.prefs.setBoolPref(
    "network.http.skip_alt_svc_validation_on_https_rr",
    false
  );

  await doTestAltSVCWithHTTPSRR(true);

  // Clear the alt-svc mapping.
  Services.obs.notifyObservers(null, "browser:purge-session-history");
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 3000));

  Services.prefs.setBoolPref(
    "network.http.skip_alt_svc_validation_on_https_rr",
    true
  );

  await doTestAltSVCWithHTTPSRR(false);
});
