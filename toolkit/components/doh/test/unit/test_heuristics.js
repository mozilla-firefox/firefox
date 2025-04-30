/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);

let cid;

async function SetMockParentalControlEnabled(aEnabled) {
  if (cid) {
    MockRegistrar.unregister(cid);
  }

  let parentalControlsService = {
    parentalControlsEnabled: aEnabled,
    QueryInterface: ChromeUtils.generateQI(["nsIParentalControlsService"]),
  };
  cid = MockRegistrar.register(
    "@mozilla.org/parental-controls-service;1",
    parentalControlsService
  );
}

registerCleanupFunction(() => {
  if (cid) {
    MockRegistrar.unregister(cid);
  }
});

add_task(setup);

add_task(async function test_parentalControls() {
  let DoHHeuristics = ChromeUtils.importESModule(
    "resource://gre/modules/DoHHeuristics.sys.mjs"
  );

  let parentalControls = DoHHeuristics.parentalControls;

  Assert.equal(
    await parentalControls(),
    "enable_doh",
    "By default, parental controls should be disabled and doh should be enabled"
  );

  SetMockParentalControlEnabled(false);

  Assert.equal(
    await parentalControls(),
    "enable_doh",
    "Mocked parental controls service is disabled; doh is enabled"
  );

  SetMockParentalControlEnabled(true);

  Assert.equal(
    await parentalControls(),
    "enable_doh",
    "Default value of mocked parental controls service is disabled; doh is enabled"
  );

  SetMockParentalControlEnabled(false);

  Assert.equal(
    await parentalControls(),
    "enable_doh",
    "Mocked parental controls service is disabled; doh is enabled"
  );

  MockRegistrar.unregister(cid);

  Assert.equal(
    await parentalControls(),
    "enable_doh",
    "By default, parental controls should be disabled and doh should be enabled"
  );
});
