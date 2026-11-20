/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
var httpServer = null;

let handlerCallbacks = {};

function listenHandler(metadata) {
  info(metadata.path);
  handlerCallbacks[metadata.path] = (handlerCallbacks[metadata.path] || 0) + 1;
}

function handlerCount(path) {
  return handlerCallbacks[path] || 0;
}

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

// Bug 1805371: Tests that require FaultyServer can't currently be built
// with system NSS.
add_setup(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS,
  },
  async () => {
    httpServer = new HttpServer();
    httpServer.registerPrefixHandler("/callback/", listenHandler);
    httpServer.start(-1);
    registerCleanupFunction(async () => {
      await httpServer.stop();
    });
    await asyncSetupFaultyServer(httpServer);
  }
);

add_task(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS,
  },
  async function testRetry0Rtt() {
    var retryDomains = [
      "0rtt-alert-bad-mac.example.com",
      "0rtt-alert-protocol-version.example.com",
      "0rtt-alert-unexpected.example.com",
    ];

    Services.prefs.setCharPref("network.dns.localDomains", retryDomains);

    for (var i = 0; i < retryDomains.length; i++) {
      {
        let countOfEarlyData = handlerCount("/callback/1");
        let chan = makeChan(`https://${retryDomains[i]}:8443`);
        let [, buf] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
        ok(buf);
        equal(
          handlerCount("/callback/1"),
          countOfEarlyData,
          "no early data sent"
        );
      }

      // The server has an anti-replay mechanism that prohibits it from
      // accepting 0-RTT connections immediately at startup.
      await sleep(1);

      {
        let countOfEarlyData = handlerCount("/callback/1");
        let chan = makeChan(`https://${retryDomains[i]}:8443`);
        let [, buf] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
        ok(buf);
        equal(
          handlerCount("/callback/1"),
          countOfEarlyData + 1,
          "got early data"
        );
      }
    }
  }
);
