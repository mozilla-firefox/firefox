/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MonitorUIUtils } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/MonitorUIUtils.sys.mjs"
);

const { MonitorAgent } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs"
);

const { PlacesUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/PlacesUtils.sys.mjs"
);

const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);

const originalDelete = MonitorAgent.deleteMonitor;

/**
 * Test that deleteMonitorWithConfirmation returns correct values when confirmation is skipped
 */
add_task(async function test_deleteMonitorWithConfirmation_skip() {
  // Mock MonitorAgent.deleteMonitor to return true
  MonitorAgent.deleteMonitor = async () => true;

  try {
    // Test with skipConfirmation = true
    const result = await MonitorUIUtils.deleteMonitorWithConfirmation(
      null, // browsing context not needed when skipping
      "test-monitor-id",
      true // skipConfirmation
    );

    Assert.ok(result.success, "Should return success true");
    Assert.ok(result.deleted, "Should return deleted true");
    Assert.ok(!result.cancelled, "Should return cancelled false");
  } finally {
    // Restore original function
    MonitorAgent.deleteMonitor = originalDelete;
  }
});

/**
 * Test that deleteMonitorWithConfirmation handles deletion failure
 */
add_task(async function test_deleteMonitorWithConfirmation_delete_fails() {
  // Mock MonitorAgent.deleteMonitor to return false
  MonitorAgent.deleteMonitor = async () => false;

  try {
    // Test with skipConfirmation = true to avoid prompt
    const result = await MonitorUIUtils.deleteMonitorWithConfirmation(
      null,
      "test-monitor-id",
      true
    );

    Assert.ok(result.success, "Should return success true");
    Assert.ok(
      !result.deleted,
      "Should return deleted false when deletion fails"
    );
    Assert.ok(!result.cancelled, "Should return cancelled false");
  } finally {
    MonitorAgent.deleteMonitor = originalDelete;
  }
});

/**
 * Test that deleteMonitorWithConfirmation handles errors properly
 */
add_task(async function test_deleteMonitorWithConfirmation_error() {
  // Mock MonitorAgent.deleteMonitor to throw an error
  MonitorAgent.deleteMonitor = async () => {
    throw new Error("Test error");
  };

  try {
    const result = await MonitorUIUtils.deleteMonitorWithConfirmation(
      null,
      "test-monitor-id",
      true
    );

    Assert.ok(!result.success, "Should return success false on error");
    Assert.ok(!result.deleted, "Should return deleted false on error");
    Assert.ok(!result.cancelled, "Should return cancelled false on error");
    Assert.equal(result.error, "Test error", "Should include error message");
  } finally {
    MonitorAgent.deleteMonitor = originalDelete;
  }
});

/**
 * Test that all return values have consistent shape
 */
add_task(async function test_deleteMonitorWithConfirmation_return_shape() {
  // Test successful deletion
  MonitorAgent.deleteMonitor = async () => true;
  let result = await MonitorUIUtils.deleteMonitorWithConfirmation(
    null,
    "test-id",
    true
  );

  Assert.ok("success" in result, "Result should have success property");
  Assert.ok("deleted" in result, "Result should have deleted property");
  Assert.ok("cancelled" in result, "Result should have cancelled property");

  // Test deletion failure
  MonitorAgent.deleteMonitor = async () => false;
  result = await MonitorUIUtils.deleteMonitorWithConfirmation(
    null,
    "test-id",
    true
  );

  Assert.ok("success" in result, "Failed result should have success property");
  Assert.ok("deleted" in result, "Failed result should have deleted property");
  Assert.ok(
    "cancelled" in result,
    "Failed result should have cancelled property"
  );

  // Test error case
  MonitorAgent.deleteMonitor = async () => {
    throw new Error("Test");
  };
  result = await MonitorUIUtils.deleteMonitorWithConfirmation(
    null,
    "test-id",
    true
  );

  Assert.ok("success" in result, "Error result should have success property");
  Assert.ok("deleted" in result, "Error result should have deleted property");
  Assert.ok(
    "cancelled" in result,
    "Error result should have cancelled property"
  );
  Assert.ok("error" in result, "Error result should have error property");

  MonitorAgent.deleteMonitor = originalDelete;
});

/**
 * resolveWatchUrlTitles returns page titles from Places.
 */
add_task(async function test_resolveWatchUrlTitles() {
  do_get_profile();

  const withTitle = "https://example.com/product";
  const noTitle = "https://example.org/untitled";
  await PlacesTestUtils.addVisits([{ uri: withTitle, title: "Great Product" }]);

  try {
    const titles = await MonitorUIUtils.resolveWatchUrlTitles([
      withTitle,
      noTitle,
      "not a url",
    ]);

    Assert.equal(
      titles[withTitle],
      "Great Product",
      "Resolves a page title from Places"
    );
    Assert.ok(!(noTitle in titles), "Omits URLs Places has no title for");
    Assert.ok(!("not a url" in titles), "Ignores malformed URLs");
  } finally {
    await PlacesUtils.history.clear();
  }
});

/**
 * resolveWatchUrlTitles tolerates missing input.
 */
add_task(async function test_resolveWatchUrlTitles_empty() {
  Assert.deepEqual(
    await MonitorUIUtils.resolveWatchUrlTitles(),
    {},
    "Returns an empty map when given no URLs"
  );
});

add_task(function test_formatMonitorForModel() {
  const monitor = {
    title: "Watches",
    monitorPrompt: "new posts",
    watchUrls: ["https://example.com/watches"],
    enabled: true,
    schedule: { type: "weekly", weekday: 1, hour: 8, minute: 30 },
  };
  Assert.deepEqual(
    MonitorUIUtils.formatMonitorForModel(monitor),
    {
      status: "watching",
      name: "Watches",
      watchUrls: ["https://example.com/watches"],
      condition: "new posts",
      schedule: "weekly on Monday at 08:30",
    },
    "A watching weekly monitor is summarized in English"
  );

  const schedules = [
    [
      { type: "weekly", weekday: 0, hour: 23, minute: 5 },
      "weekly on Sunday at 23:05",
    ],
    [
      { type: "weekly", weekday: 6, hour: 0, minute: 0 },
      "weekly on Saturday at 00:00",
    ],
    [{ type: "daily", hour: 9, minute: 0 }, "daily at 09:00"],
    [{ type: "interval", hours: 1 }, "every hour"],
    [{ type: "interval", hours: 6 }, "every 6 hours"],
  ];
  for (const [schedule, expected] of schedules) {
    Assert.equal(
      MonitorUIUtils.formatMonitorForModel({ ...monitor, schedule }).schedule,
      expected,
      `The ${schedule.type} schedule is described as "${expected}"`
    );
  }

  Assert.equal(
    MonitorUIUtils.formatMonitorForModel({ ...monitor, enabled: false }).status,
    "paused",
    "A disabled monitor is paused"
  );
  Assert.equal(
    MonitorUIUtils.formatMonitorForModel({
      ...monitor,
      enabled: false,
      expiry: { expiredAt: "2026-10-01T00:00:00.000Z", reason: "inactive" },
    }).status,
    "expired",
    "An expired monitor is reported as expired rather than paused"
  );
});

add_task(function test_getScheduleL10n() {
  Assert.equal(
    MonitorUIUtils.getScheduleL10n({
      frequency: "weekly",
      time: "08:30",
      weekday: "1",
    }).id,
    "ai-tasks-alert-schedule-weekly-monday",
    "A weekly schedule uses its weekday's string"
  );
  Assert.equal(
    MonitorUIUtils.getScheduleL10n({ frequency: "daily", time: "08:30" }).id,
    "ai-tasks-alert-schedule-daily-at",
    "A daily schedule uses the daily string"
  );
});
