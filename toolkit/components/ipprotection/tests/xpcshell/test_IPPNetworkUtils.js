/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);
const { IPPNetworkUtils } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPNetworkUtils.sys.mjs"
);

const mockLinkService = {
  isLinkUp: true,
  linkStatusKnown: true,
  QueryInterface: ChromeUtils.generateQI(["nsINetworkLinkService"]),
};

add_setup(() => {
  const cid = MockRegistrar.register(
    "@mozilla.org/network/network-link-service;1",
    mockLinkService
  );
  registerCleanupFunction(() => MockRegistrar.unregister(cid));
});

add_task(function test_link_down_known_is_offline() {
  mockLinkService.isLinkUp = false;
  mockLinkService.linkStatusKnown = true;
  Assert.ok(IPPNetworkUtils.isOffline, "A known down link is offline");
});

add_task(function test_link_down_unknown_is_not_offline() {
  mockLinkService.isLinkUp = false;
  mockLinkService.linkStatusKnown = false;
  Assert.ok(
    !IPPNetworkUtils.isOffline,
    "An untrusted isLinkUp value must not report offline"
  );
});
