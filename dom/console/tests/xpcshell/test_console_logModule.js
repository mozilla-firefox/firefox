/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

add_setup(function check_environment() {
  Assert.equal(
    Services.env.get("MOZ_LOG"),
    "testWarn:2,testVerbose:5",
    "xpcshell.toml sets testWarn to Warning and testVerbose to Verbose"
  );
});

add_task(function test_module_not_in_MOZ_LOG() {
  let ci = console.createInstance({
    logModule: "testNotInEnv",
    maxLogLevel: "Off",
  });

  Assert.ok(
    !ci.shouldLog("Error"),
    "With maxLogLevel Off, only the MOZ_LOG level of the module applies"
  );
});

add_task(async function test_logModule_names_the_module() {
  let ci = console.createInstance({
    logModule: "testWarn",
    prefix: "testVerbose",
    maxLogLevel: "Off",
  });

  Assert.ok(ci.shouldLog("Warn"), "The MOZ_LOG level of logModule applies");
  Assert.ok(
    !ci.shouldLog("Info"),
    "The MOZ_LOG level of the prefix doesn't apply"
  );
});

add_task(async function test_prefix_alone_names_the_module() {
  let ci = console.createInstance({
    prefix: "testVerbose",
    maxLogLevel: "Off",
  });

  Assert.ok(
    ci.shouldLog("Info"),
    "The MOZ_LOG level of the prefix applies without a logModule"
  );
});

add_task(async function test_printed_prefix() {
  const tests = [
    {
      options: { logModule: "testWarn" },
      label: "testWarn",
    },
    {
      options: { logModule: "testWarn", prefix: "Some prefix" },
      label: "testWarn: Some prefix",
    },
    {
      options: { logModule: "testWarn", prefix: "testWarn" },
      label: "testWarn",
    },
    {
      options: { prefix: "Some prefix" },
      label: "Some prefix",
    },
  ];

  for (const { options, label } of tests) {
    let dumped = [];
    let ci = console.createInstance({
      ...options,
      maxLogLevel: "All",
      dump: message => dumped.push(message),
    });
    let endConsoleListening = TestUtils.listenForConsoleMessages();
    ci.log(42);
    let events = await endConsoleListening();

    Assert.equal(dumped.length, 1, "One message was dumped");
    Assert.equal(
      dumped[0],
      `console.log: ${label}: 42\n`,
      "The dumped message has the label"
    );
    Assert.equal(events.length, 1, "One console event was sent");
    Assert.equal(events[0].prefix, label, "The console event has the label");
  }
});
