"use strict";

const DEVICE_ID = "FAKE_DEVICE_ID";
const SOCKET_PATH = "fake/socket/path";

/**
 * Test the prepare-tcp-connection command in isolation.
 */
add_task(async function testParseFileUri() {
  info("Enable devtools.testing for this test to allow mocked modules");
  Services.prefs.setBoolPref("devtools.testing", true);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("devtools.testing");
  });

  // Mocks are not supported for the regular DevTools loader.
  info("Create a BrowserLoader to enable mocks in the test");
  const { BrowserLoader } = ChromeUtils.importESModule(
    "resource://devtools/shared/loader/browser-loader.sys.mjs"
  );
  const mockedRequire = BrowserLoader({
    baseURI: "resource://devtools/client/shared/remote-debugging/adb",
    // Needs console for the adb logger.
    window: { console },
  }).require;

  // Prepare a mocked version of the run-command.js module to test
  // prepare-tcp-connection in isolation.
  info("Create a run-command module mock");
  let createdPort;
  const mock = {
    runCommand: command => {
      // Check that we only receive the expected command and extract the port
      // number.

      // The command should follow the pattern
      // `host-serial:${deviceId}:forward:tcp:${localPort};${devicePort}`
      // with devicePort = `localfilesystem:${socketPath}`
      const socketPathRe = SOCKET_PATH.replace(/\//g, "\\/");
      const regexp = new RegExp(
        `host-serial:${DEVICE_ID}:forward:tcp:(\\d+);localfilesystem:${socketPathRe}`
      );
      const matches = regexp.exec(command);
      equal(matches.length, 2, "The command is the expected");
      createdPort = matches[1];

      // Finally return a Promise-like object.
      return {
        then: () => {},
      };
    },
  };

  info("Enable the mocked run-command module");
  const { setMockedModule, removeMockedModule } = mockedRequire(
    "devtools/shared/loader/browser-loader-mocks"
  );
  setMockedModule(
    mock,
    "devtools/client/shared/remote-debugging/adb/commands/run-command"
  );
  registerCleanupFunction(() => {
    removeMockedModule(
      "devtools/client/shared/remote-debugging/adb/commands/run-command"
    );
  });

  const { prepareTCPConnection } = mockedRequire(
    "devtools/client/shared/remote-debugging/adb/commands/prepare-tcp-connection"
  );

  const port = await prepareTCPConnection(DEVICE_ID, SOCKET_PATH);
  ok(!Number.isNaN(port), "prepareTCPConnection returned a valid port");
  equal(
    port,
    Number(createdPort),
    "prepareTCPConnection returned the port sent to the host-serial command"
  );
});
