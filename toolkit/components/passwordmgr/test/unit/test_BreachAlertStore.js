/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { BreachAlertStorage } = ChromeUtils.importESModule(
  "resource://gre/modules/BreachAlertStore.sys.mjs"
);

let breachAlertStorage;

add_setup(async () => {
  breachAlertStorage = new BreachAlertStorage();
  await breachAlertStorage.initialize();
  registerCleanupFunction(() => breachAlertStorage.finalize());
});

add_task(async function test_set_and_get() {
  const dismissals = [
    { breachName: "breach-1", timeDismissed: 1000 },
    { breachName: "breach-2", timeDismissed: 2000 },
  ];
  await breachAlertStorage.setBreachAlertDismissals(dismissals);

  const results = await breachAlertStorage.getBreachAlertDismissals([
    "breach-1",
    "breach-2",
  ]);
  Assert.equal(results.length, 2);
  Assert.deepEqual(results[0], { breachName: "breach-1", timeDismissed: 1000 });
  Assert.deepEqual(results[1], { breachName: "breach-2", timeDismissed: 2000 });
});

add_task(async function test_get_unknown_id_returns_empty() {
  const results = await breachAlertStorage.getBreachAlertDismissals([
    "unknown-breach",
  ]);
  Assert.equal(results.length, 0);
});

add_task(async function test_clear_specific() {
  await breachAlertStorage.setBreachAlertDismissals([
    { breachName: "breach-a", timeDismissed: 1000 },
    { breachName: "breach-b", timeDismissed: 2000 },
  ]);

  await breachAlertStorage.clearBreachAlertDismissals(["breach-a"]);

  const remaining = await breachAlertStorage.getBreachAlertDismissals([
    "breach-a",
    "breach-b",
  ]);
  Assert.equal(remaining.length, 1);
  Assert.equal(remaining[0].breachName, "breach-b");
});

add_task(async function test_clear_all() {
  await breachAlertStorage.setBreachAlertDismissals([
    { breachName: "breach-x", timeDismissed: 1000 },
    { breachName: "breach-y", timeDismissed: 2000 },
  ]);

  await breachAlertStorage.clearAllBreachAlertDismissals();

  const results = await breachAlertStorage.getBreachAlertDismissals([
    "breach-x",
    "breach-y",
  ]);
  Assert.equal(results.length, 0);
});

add_task(async function test_singleton() {
  const second = new BreachAlertStorage();
  Assert.equal(
    breachAlertStorage,
    second,
    "Constructor should return the same instance."
  );
});
