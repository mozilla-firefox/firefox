/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Checks nsINetworkLinkService.matchNetworkID against the platform's own
// network ID. The comparison itself is covered by the TestLinkServiceCommon
// gtest.

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

add_task(async function test_match_current_network_id() {
  let linkService = Cc[
    "@mozilla.org/network/network-link-service;1"
  ].getService(Ci.nsINetworkLinkService);
  let seed = linkService.networkIDSeed;
  Assert.ok(seed, "the process has a network ID seed");

  // The ID is determined asynchronously after startup, and a machine with no
  // active network has none.
  await TestUtils.waitForCondition(
    () => linkService.networkID,
    "waiting for the network ID"
  ).catch(() => {});

  let networkID = linkService.networkID;
  let match = {};
  if (!networkID) {
    Assert.ok(
      !linkService.matchNetworkID("anything", seed, match),
      "there is no current network to match"
    );
    Assert.equal(match.value, "", "nothing matches without a network");
    return;
  }

  Assert.ok(linkService.matchNetworkID(networkID, seed, match));
  Assert.equal(match.value, networkID, "the current ID matches its own seed");

  Assert.ok(
    linkService.matchNetworkID(
      networkID,
      "{00000000-0000-0000-0000-000000000000}",
      match
    )
  );
  Assert.equal(match.value, "", "another seed does not match");

  Assert.ok(linkService.matchNetworkID(networkID, "not a seed", match));
  Assert.equal(match.value, "", "an unparseable seed does not match");
});
