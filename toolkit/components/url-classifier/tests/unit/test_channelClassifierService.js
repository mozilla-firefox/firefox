/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* Unit tests for the nsIUrlClassifierSkipListService implementation. */

var httpserver = new HttpServer();

const { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);
const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);

const FEATURE_STP_PREF = "privacy.trackingprotection.socialtracking.enabled";
const TOP_LEVEL_DOMAIN = "http://www.example.com/";
const TRACKER_DOMAIN = "http://social-tracking.example.org/";

function setupChannel(uri, topUri = TOP_LEVEL_DOMAIN) {
  httpserver.registerPathHandler("/", null);
  httpserver.start(-1);

  let channel = NetUtil.newChannel({
    uri: uri + ":" + httpserver.identity.primaryPort,
    loadingPrincipal: Services.scriptSecurityManager.createContentPrincipal(
      NetUtil.newURI(topUri),
      {}
    ),
    securityFlags: Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_OTHER,
  });

  channel
    .QueryInterface(Ci.nsIHttpChannelInternal)
    .setTopWindowURIIfUnknown(Services.io.newURI(topUri));

  return channel;
}

function waitForBeforeBlockEvent(expected, callback) {
  return new Promise(function (resolve) {
    let observer = function observe(aSubject, aTopic) {
      switch (aTopic) {
        case "urlclassifier-before-block-channel": {
          let channel = aSubject.QueryInterface(
            Ci.nsIUrlClassifierBlockedChannel
          );
          Assert.equal(
            channel.reason,
            expected.reason,
            "verify blocked reason"
          );
          Assert.equal(
            channel.url,
            expected.url,
            "verify url of blocked channel"
          );

          if (callback) {
            callback(channel);
          }

          service.removeListener(observer);
          resolve(channel);
          break;
        }
      }
    };

    let service = Cc[
      "@mozilla.org/url-classifier/channel-classifier-service;1"
    ].getService(Ci.nsIChannelClassifierService);
    service.addListener(observer);
  });
}

add_task(async function test_block_channel() {
  Services.prefs.setBoolPref(FEATURE_STP_PREF, true);
  await UrlClassifierTestUtils.addTestTrackers();

  let channel = setupChannel(TRACKER_DOMAIN);

  let blockPromise = waitForBeforeBlockEvent(
    {
      reason: Ci.nsIUrlClassifierBlockedChannel.SOCIAL_TRACKING_PROTECTION,
      url: channel.URI.spec,
    },
    null
  );

  let openPromise = new Promise(resolve => {
    channel.asyncOpen({
      onStartRequest: () => {},
      onDataAvailable: () => {},
      onStopRequest: (request, status) => {
        dump("status = " + status + "\n");
        if (status == 200) {
          Assert.ok(false, "Should not successfully open the channel");
        } else {
          Assert.equal(
            status,
            Cr.NS_ERROR_SOCIALTRACKING_URI,
            "Should fail to open the channel"
          );
        }
        resolve();
      },
    });
  });

  // wait for block event from url-classifier
  await blockPromise;

  // wait for onStopRequest callback from AsyncOpen
  await openPromise;

  // clean up
  await UrlClassifierTestUtils.cleanupTestTrackers();
  Services.prefs.clearUserPref(FEATURE_STP_PREF);
  httpserver.stop();
});

add_task(async function test_unblock_channel() {
  Services.prefs.setBoolPref(FEATURE_STP_PREF, true);
  //Services.prefs.setBoolPref("network.dns.native-is-localhost", true);

  await UrlClassifierTestUtils.addTestTrackers();

  let channel = setupChannel(TRACKER_DOMAIN);

  let blockPromise = waitForBeforeBlockEvent(
    {
      reason: Ci.nsIUrlClassifierBlockedChannel.SOCIAL_TRACKING_PROTECTION,
      url: channel.URI.spec,
    },
    ch => {
      ch.replace();
    }
  );

  let openPromise = new Promise(resolve => {
    channel.asyncOpen({
      onStartRequest: () => {},
      onDataAvailable: () => {},
      onStopRequest: (request, status) => {
        if (status == Cr.NS_ERROR_SOCIALTRACKING_URI) {
          Assert.ok(false, "Classifier should not cancel this channel");
        } else {
          // This request is supposed to fail, but we need to ensure it
          // is not canceled by url-classifier
          Assert.equal(
            status,
            Cr.NS_ERROR_UNKNOWN_HOST,
            "Not cancel by classifier"
          );
        }
        resolve();
      },
    });
  });

  // wait for block event from url-classifier
  await blockPromise;

  // wait for onStopRequest callback from AsyncOpen
  await openPromise;

  // clean up
  await UrlClassifierTestUtils.cleanupTestTrackers();
  Services.prefs.clearUserPref(FEATURE_STP_PREF);
  httpserver.stop();
});

add_task(async function test_allow_channel() {
  Services.prefs.setBoolPref(FEATURE_STP_PREF, true);
  //Services.prefs.setBoolPref("network.dns.native-is-localhost", true);

  await UrlClassifierTestUtils.addTestTrackers();

  let channel = setupChannel(TRACKER_DOMAIN);

  let blockPromise = waitForBeforeBlockEvent(
    {
      reason: Ci.nsIUrlClassifierBlockedChannel.SOCIAL_TRACKING_PROTECTION,
      url: channel.URI.spec,
    },
    ch => {
      ch.allow();
    }
  );

  let openPromise = new Promise(resolve => {
    channel.asyncOpen({
      onStartRequest: () => {},
      onDataAvailable: () => {},
      onStopRequest: (request, status) => {
        if (status == Cr.NS_ERROR_SOCIALTRACKING_URI) {
          Assert.ok(false, "Classifier should not cancel this channel");
        } else {
          // This request is supposed to fail, but we need to ensure it
          // is not canceled by url-classifier
          Assert.equal(
            status,
            Cr.NS_ERROR_UNKNOWN_HOST,
            "Not cancel by classifier"
          );
        }
        resolve();
      },
    });
  });

  // wait for block event from url-classifier
  await blockPromise;

  // wait for onStopRequest callback from AsyncOpen
  await openPromise;

  // clean up
  await UrlClassifierTestUtils.cleanupTestTrackers();
  Services.prefs.clearUserPref(FEATURE_STP_PREF);
  httpserver.stop();
});
