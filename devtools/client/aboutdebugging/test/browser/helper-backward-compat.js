/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head.js */

// Helpers for the remote debugging backward compatibility tests, which connect
// this Firefox to a second Firefox provisioned by ./mach devtools-compat-test.
// Documentation in /documentation/TESTS_BACKWARD_COMPAT.md

const COMPAT_CONFIG_ENV_VAR = "DEVTOOLS_COMPAT_CONFIG";

// The fixture pages are served by the regular mochitest HTTP server, but the
// server Firefox runs with its own profile and has no proxy configured, so it
// cannot use the example.com aliases the other URL_ROOT constants rely on.
// 127.0.0.1 is served directly (it is registered in build/pgo/server-locations.txt)
// and, unlike mochi.test, it is a secure context, which the service worker
// fixture needs. The port matches the one declared in server-locations.txt.
const FIXTURE_ROOT =
  CHROME_URL_ROOT.replace(
    "chrome://mochitests/content/",
    "http://127.0.0.1:8888/"
  ) + "resources/backward-compat/";

/**
 * Read the configuration written by ./mach devtools-compat-test.
 *
 * @returns {object|null}
 *          The parsed configuration, or null when no server was provisioned,
 *          which is the case on regular pushes and for a plain `mach mochitest`
 *          run. The configuration holds the name of the `server` recipe, the
 *          `host` of the server as a "host:port" string, the `control` channel
 *          base URL, and a `runtime` object describing the server application
 *          with its brandName, version and channel.
 */
async function getCompatConfig() {
  const path = Services.env.get(COMPAT_CONFIG_ENV_VAR);
  if (!path) {
    return null;
  }

  info(`Load backward compatibility configuration from ${path}`);
  const buffer = await IOUtils.read(path);
  return JSON.parse(new TextDecoder().decode(buffer));
}
/* exported getCompatConfig */

function getCurrentVersion() {
  const { AppConstants } = ChromeUtils.importESModule(
    "resource://gre/modules/AppConstants.sys.mjs"
  );
  return AppConstants.MOZ_APP_VERSION;
}

// When testing against the release channel close to a new release, Nightly
// might already have been bumped while release is still lagging behind and
// now has a gap of 3 versions against Nightly.
function isReleaseVersionTooOld(compatConfig) {
  if (compatConfig.runtime.channel !== "release") {
    // Only relevant when testing release channel.
    return false;
  }

  const getMajor = version => Number.parseInt(version.match(/\d+/)[0], 10);
  return (
    getMajor(getCurrentVersion()) === getMajor(compatConfig.runtime.version) + 3
  );
}

/**
 * Register a test which only runs when a server has been provisioned. Without
 * one the task reports a single passing assertion and returns immediately.
 *
 * @param {Function} taskFn
 *        Async test function, called with the configuration returned by
 *        getCompatConfig as its only argument.
 */
function addCompatTask(taskFn) {
  add_task(async function () {
    const config = await getCompatConfig();
    if (!config) {
      ok(
        true,
        `No ${COMPAT_CONFIG_ENV_VAR} in the environment, skipping. ` +
          "Run these tests with `./mach devtools-compat-test`"
      );
      return;
    }

    const { brandName, version, channel } = config.runtime;
    info(
      `Testing against ${brandName} ${version} (${channel}) on ${config.host}`
    );

    /**
     * @backward-compat { version 154 }
     * This was added during early 156 cycle. With Bug 2064221 we are extending
     * the backward compatibility window to allow for a gap of 3 versions.
     * Since the backward compatibility code for v154 was already removed, we
     * should wait until the beginning of cycle 157 to remove this.
     *
     * When Firefox 154 is no longer on release, remove the if block below as
     * well as the `isReleaseVersionTooOld` helper.
     */
    if (isReleaseVersionTooOld(config)) {
      ok(
        true,
        `The release version (${version}) is too old to be tested against the current version (${getCurrentVersion()}). ` +
          "This should only happen for a few days around release time"
      );
      return;
    }

    await taskFn(config);
  });
}
/* exported addCompatTask */

/**
 * Register the provisioned server as an about:debugging network location and
 * open about:debugging.
 *
 * @param {object} config
 *        The configuration returned by getCompatConfig.
 * @param {object} options
 *        Options forwarded to openAboutDebugging.
 * @returns {object} An object with the properties { tab, document, window }
 *          describing the create about:debugging tab.
 */
async function openAboutDebuggingWithCompatServer(config, options = {}) {
  await pushPref(
    "devtools.aboutdebugging.network-locations",
    JSON.stringify([config.host])
  );

  return openAboutDebugging(options);
}
/* exported openAboutDebuggingWithCompatServer */

/**
 * Run a command on the server Firefox, through the harness control channel.
 *
 * @param {object} config
 *        The configuration returned by getCompatConfig.
 * @param {string} name
 *        Name of the command, see backward_compat_test_server/control.py.
 * @param {object} args
 *        Arguments for the command.
 * @returns {object} The result of the command.
 */
async function runDevToolsServerCommand(config, name, args = {}) {
  info(`Run the '${name}' command on the server`);
  const response = await fetch(`${config.control}/command`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ name, args }),
  });

  const body = await response.json();
  if (!response.ok) {
    throw new Error(`Command '${name}' failed: ${body.error}`);
  }
  return body.result;
}
/* exported runDevToolsServerCommand */

/**
 * Open a tab on the server Firefox for one of the fixture pages, and return
 * its window handle.
 */
async function openDevToolsServerTab(config, fixture) {
  const { handle } = await runDevToolsServerCommand(config, "open-tab", {
    url: getCompatFixtureUrl(fixture),
  });
  return handle;
}
/* exported openDevToolsServerTab */

/**
 * Close a tab previously opened with openDevToolsServerTab.
 */
async function closeDevToolsServerTab(config, handle) {
  await runDevToolsServerCommand(config, "close-tab", { handle });
}
/* exported closeDevToolsServerTab */

/**
 * URL of a fixture page, as the server Firefox will load it.
 */
function getCompatFixtureUrl(fixture) {
  return FIXTURE_ROOT + fixture;
}
/* exported getCompatFixtureUrl */

/**
 * Open a fixture page on the server, then open a toolbox on it from the runtime
 * page of the server. This is the setup shared by all the tests inspecting a
 * remote target.
 *
 * @param {object} config
 *        The configuration returned by getCompatConfig.
 * @param {string} fixture
 *        Name of the fixture page to open on the server.
 * @returns {object}
 *        The about:debugging document, tab and window, the toolbox and its tab,
 *        and the window handle of the page opened on the server.
 */
async function openCompatToolbox(config, fixture) {
  const serverHandle = await openDevToolsServerTab(config, fixture);

  const { document, tab, window } =
    await openAboutDebuggingWithCompatServer(config);
  await connectToRuntime(config.host, document);
  await waitForRuntimePage(config.runtime.brandName, document);

  const { devtoolsTab, devtoolsWindow } = await openAboutDevtoolsToolbox(
    document,
    tab,
    window,
    getCompatFixtureUrl(fixture)
  );

  return {
    document,
    tab,
    window,
    devtoolsTab,
    devtoolsWindow,
    toolbox: getToolbox(devtoolsWindow),
    serverHandle,
  };
}
/* exported openCompatToolbox */

/**
 * Tear down what openCompatToolbox created.
 */
async function closeCompatToolbox(config, setup) {
  const { document, tab, window, devtoolsTab, serverHandle } = setup;
  await closeAboutDevtoolsToolbox(document, devtoolsTab, window);
  await closeDevToolsServerTab(config, serverHandle);
  await removeTab(tab);
}
/* exported closeCompatToolbox */
