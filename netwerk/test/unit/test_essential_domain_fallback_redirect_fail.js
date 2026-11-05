/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// When a system request to an essential domain fails, nsHttpChannel redirects
// it to the fallback domain from within its OnStartRequest, without notifying
// its listener. If that redirect then fails, the listener must still get
// OnStartRequest before OnStopRequest.
//
// The redirect is vetoed asynchronously here; in the wild it failed because the
// new channel could not be opened during shutdown.

"use strict";

const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);

add_setup(async function setup() {
  Services.prefs.setBoolPref("network.essential_domains_fallback", true);
  Services.io.addEssentialDomainMapping("aus5.mozilla.org", "foo.example.com");

  let ncs = Cc[
    "@mozilla.org/network/network-connectivity-service;1"
  ].getService(Ci.nsINetworkConnectivityService);
  ncs.IPv4 = Ci.nsINetworkConnectivityService.OK;

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.essential_domains_fallback");
    override.clearOverrides();
  });
});

// Vetoes every redirect, after returning to the event loop.
class AsyncRedirectVeto {
  QueryInterface = ChromeUtils.generateQI([
    "nsIInterfaceRequestor",
    "nsIChannelEventSink",
  ]);

  getInterface(aIID) {
    return this.QueryInterface(aIID);
  }

  asyncOnChannelRedirect(aOldChannel, aNewChannel, aFlags, aCallback) {
    executeSoon(() => aCallback.onRedirectVerifyCallback(Cr.NS_ERROR_ABORT));
  }
}

add_task(async function test_failed_fallback_redirect_notifies_listener() {
  Services.dns.clearCache(true);
  override.addIPOverride("aus5.mozilla.org", "N/A");
  override.addIPOverride("foo.example.com", "127.0.0.1");

  let chan = NetUtil.newChannel({
    uri: "https://aus5.mozilla.org/stuff",
    loadUsingSystemPrincipal: true,
  });
  chan.notificationCallbacks = new AsyncRedirectVeto();

  let fallback = new Promise(resolve => {
    let observer = () => {
      Services.obs.removeObserver(observer, "httpchannel-fallback");
      resolve();
    };
    Services.obs.addObserver(observer, "httpchannel-fallback");
  });

  let events = [];
  let status = await new Promise(resolve => {
    chan.asyncOpen({
      QueryInterface: ChromeUtils.generateQI([
        "nsIStreamListener",
        "nsIRequestObserver",
      ]),
      onStartRequest() {
        events.push("start");
      },
      onDataAvailable(request, stream, offset, count) {
        read_stream(stream, count);
      },
      onStopRequest(request, aStatus) {
        events.push("stop");
        resolve(aStatus);
      },
    });
  });
  await fallback;

  Assert.deepEqual(
    events,
    ["start", "stop"],
    "the listener gets OnStartRequest before OnStopRequest"
  );
  Assert.ok(!Components.isSuccessCode(status), "the request fails");
});
