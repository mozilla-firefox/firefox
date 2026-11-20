/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { HistoryDateRegistry } = ChromeUtils.importESModule(
  "moz-src:///browser/components/places/history/HistoryDateRegistry.sys.mjs"
);
registerCleanupFunction(() => PlacesUtils.history.clear());

/*
 * Tests that empty history does not create any containers.
 */
add_task(async function test_empty_history() {
  await PlacesUtils.history.clear();
  const history = await HistoryDateRegistry.create();
  Assert.deepEqual(history.containers, [], "Empty history has no date groups");
});

/*
 * Tests that containers have no children until loaded, and unloading frees
 * children from memory.
 */
add_task(async function test_load_and_unload() {
  await PlacesUtils.history.clear();
  await PlacesUtils.history.insert({
    url: "https://example.com/",
    visits: [{ date: new Date(2024, 0, 1, 12) }],
  });
  const history = await HistoryDateRegistry.create({
    now: new Date(2024, 0, 1, 12),
  });
  const [container] = history.containers;
  Assert.deepEqual(container.children, [], "A new container has no visits");
  Assert.deepEqual(
    container.startDate,
    new Date(2024, 0, 1),
    "The start date can be inspected before loading"
  );
  container.startDate.setDate(2);
  container.datePeriod.startDate.setDate(2);
  container.datePeriod.endDate.setDate(3);
  Assert.deepEqual(
    container.datePeriod,
    { startDate: new Date(2024, 0, 1), endDate: new Date(2024, 0, 2) },
    "Mutating exposed dates does not change the container period"
  );

  await container.load();
  Assert.equal(
    container.children[0].url,
    "https://example.com/",
    "Loading fetches the period's visit"
  );
  Assert.deepEqual(
    container.children[0].date,
    new Date(2024, 0, 1, 12),
    "Loading preserves the visit date"
  );
  Assert.ok(container.isLoaded, "The container is marked loaded");

  container.unload();
  Assert.deepEqual(container.children, [], "Unloading clears the visits");
  Assert.ok(!container.isLoaded, "The container is marked unloaded");

  await container.load();
  Assert.equal(
    container.children[0].url,
    "https://example.com/",
    "An unloaded container can load again"
  );
});

/*
 * Tests that results from `load()` requests started before an `unload()` are
 * not persisted to the container.
 */
add_task(async function test_unload_while_loads_are_pending() {
  await PlacesUtils.history.clear();
  await PlacesUtils.history.insert({
    url: "https://example.com/",
    visits: [{ date: new Date(2024, 0, 1, 12) }],
  });
  const history = await HistoryDateRegistry.create({
    now: new Date(2024, 0, 1, 12),
  });
  const [container] = history.containers;
  const loads = [container.load(), container.load()];

  history.unloadAll();
  await Promise.all(loads);
  Assert.deepEqual(
    container.children,
    [],
    "A late result does not restore unloaded visits"
  );
  Assert.ok(!container.isLoaded, "A late result leaves the container unloaded");
});

/*
 * Tests that we can list out the date periods in all of history, without
 * actually fetching their visits.
 */
add_task(async function test_discover_days_and_months() {
  await PlacesUtils.history.clear();
  await PlacesUtils.history.insert({
    url: "https://example.com/",
    visits: [
      { date: new Date(2024, 0, 3, 12) },
      { date: new Date(2023, 11, 15, 12) },
    ],
  });
  const history = await HistoryDateRegistry.create({
    now: new Date(2024, 0, 5, 12),
  });
  const expectedDatePeriods = [
    // Visit on the same month goes into a day bucket.
    {
      startDate: new Date(2024, 0, 3),
      endDate: new Date(2024, 0, 4),
    },
    // Visit on a previous month goes into a month bucket.
    {
      startDate: new Date(2023, 11, 1),
      endDate: new Date(2024, 0, 1),
    },
  ];
  Assert.deepEqual(
    history.containers.map(({ datePeriod }) => datePeriod),
    expectedDatePeriods,
    "Only populated days and months are discovered, newest first"
  );
  for (const { children, isLoaded } of history.containers) {
    Assert.ok(
      !children.length && !isLoaded,
      "Discovery leaves visits unloaded"
    );
  }
});

/*
 * Date containers are arranged as:
 *
 * (1) Today
 * (2) Yesterday
 * (...) (One day for each of the previous days this month)
 * (...) (One month for each of the previous months)
 *
 * Account for the special case where Yesterday takes place on the previous
 * month. In this case, its visits were already displayed in bucket (2), and
 * should therefore not be reiterated in its respective month bucket.
 */
add_task(async function test_yesterday_in_previous_month() {
  await PlacesUtils.history.clear();
  await PlacesUtils.history.insert({
    url: "https://example.com/",
    visits: [
      { date: new Date(2024, 0, 31, 12) },
      { date: new Date(2024, 0, 30, 12) },
    ],
  });
  const history = await HistoryDateRegistry.create({
    now: new Date(2024, 1, 1, 12),
  });
  Assert.deepEqual(
    history.containers.map(({ datePeriod }) => datePeriod),
    [
      { startDate: new Date(2024, 0, 31), endDate: new Date(2024, 1, 1) },
      { startDate: new Date(2024, 0, 1), endDate: new Date(2024, 0, 31) },
    ],
    "Yesterday and the rest of the January have separate periods."
  );
  const [yesterday, january] = history.containers;
  await january.load();
  Assert.deepEqual(
    january.children.map(({ date }) => date),
    [new Date(2024, 0, 30, 12)],
    "The bucket for January excludes January 31st, which was yesterday."
  );
  await yesterday.load();
  Assert.deepEqual(
    yesterday.children.map(({ date }) => date),
    [new Date(2024, 0, 31, 12)],
    "Yesterday's bucket contains one visit from January 31st."
  );
  history.unloadAll();
});
