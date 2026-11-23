/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/
*/

"use strict";

const { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);
const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);

const defaultTopWindowURI = NetUtil.newURI("http://www.example.com/");

var httpServer;
var trackingOrigin;

// ShouldClassify algorithm uses the following parameters:
// 1. Ci.nsIChannel.LOAD_ BYPASS_URL_CLASSIFIER loadflags
// 2. Content type
// 3. triggering principal
// 4. be Conservative
// We test are the combinations here to make sure the algorithm is correct

// const PARAM_LOAD_BYPASS_URL_CLASSIFIER = 1 << 0;
const PARAM_CONTENT_POLICY_TYPE_DOCUMENT = 1 << 1;
const PARAM_TRIGGERING_PRINCIPAL_SYSTEM = 1 << 2;
const PARAM_CAP_BE_CONSERVATIVE = 1 << 3;
const PARAM_MAX = 1 << 4;

function getParameters(bitFlags) {
  var params = {
    loadFlags: Ci.nsIRequest.LOAD_NORMAL,
    contentType: Ci.nsIContentPolicy.TYPE_OTHER,
    system: false,
    beConservative: false,
  };

  if (bitFlags & PARAM_TRIGGERING_PRINCIPAL_SYSTEM) {
    params.loadFlags = Ci.nsIChannel.LOAD_BYPASS_URL_CLASSIFIER;
  }

  if (bitFlags & PARAM_CONTENT_POLICY_TYPE_DOCUMENT) {
    params.contentType = Ci.nsIContentPolicy.TYPE_DOCUMENT;
  }

  if (bitFlags & PARAM_TRIGGERING_PRINCIPAL_SYSTEM) {
    params.system = true;
  }

  if (bitFlags & PARAM_CAP_BE_CONSERVATIVE) {
    params.beConservative = true;
  }

  return params;
}

function getExpectedResult(params) {
  if (params.loadFlags & Ci.nsIChannel.LOAD_BYPASS_URL_CLASSIFIER) {
    return false;
  }
  if (params.beConservative) {
    return false;
  }
  if (
    params.system &&
    params.contentType != Ci.nsIContentPolicy.TYPE_DOCUMENT
  ) {
    return false;
  }

  return true;
}

function setupHttpServer() {
  httpServer = new HttpServer();
  httpServer.start(-1);
  httpServer.identity.setPrimary(
    "http",
    "tracking.example.org",
    httpServer.identity.primaryPort
  );
  httpServer.identity.add(
    "http",
    "example.org",
    httpServer.identity.primaryPort
  );
  trackingOrigin =
    "http://tracking.example.org:" + httpServer.identity.primaryPort;
}

function setupChannel(params) {
  var channel;

  if (params.system) {
    channel = NetUtil.newChannel({
      uri: trackingOrigin + "/evil.js",
      loadUsingSystemPrincipal: true,
      contentPolicyType: params.contentType,
    });
  } else {
    let principal = Services.scriptSecurityManager.createContentPrincipal(
      NetUtil.newURI(trackingOrigin),
      {}
    );
    channel = NetUtil.newChannel({
      uri: trackingOrigin + "/evil.js",
      loadingPrincipal: principal,
      securityFlags: Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      contentPolicyType: params.contentType,
    });
  }

  channel.QueryInterface(Ci.nsIHttpChannel);
  channel.requestMethod = "GET";
  channel.loadFlags |= params.loadFlags;
  channel
    .QueryInterface(Ci.nsIHttpChannelInternal)
    .setTopWindowURIIfUnknown(defaultTopWindowURI);
  channel.QueryInterface(Ci.nsIHttpChannelInternal).beConservative =
    params.beConservative;

  return channel;
}

add_task(async function testShouldClassify() {
  Services.prefs.setBoolPref("dom.security.https_first", false);
  Services.prefs.setBoolPref(
    "privacy.trackingprotection.annotate_channels",
    true
  );
  Services.prefs.setBoolPref("network.dns.native-is-localhost", true);

  setupHttpServer();

  await UrlClassifierTestUtils.addTestTrackers();

  for (let i = 0; i < PARAM_MAX; i++) {
    let params = getParameters(i);
    let channel = setupChannel(params);

    await new Promise(resolve => {
      channel.asyncOpen({
        onStartRequest: request => {
          Assert.equal(
            !!(
              request.QueryInterface(Ci.nsIClassifiedChannel)
                .classificationFlags &
              Ci.nsIClassifiedChannel.CLASSIFIED_ANY_BASIC_TRACKING
            ),
            getExpectedResult(params)
          );
          request.cancel(Cr.NS_ERROR_ABORT);
          resolve();
        },

        onDataAvailable: () => {},
        onStopRequest: () => {},
      });
    });
  }

  await UrlClassifierTestUtils.cleanupTestTrackers();

  httpServer.stop(do_test_finished);
});
