# Remote debugging backward compatibility tests

The DevTools backward compatibility tests are automated smoke tests which
verify that a client Firefox can connect to and debug a server Firefox which has
either the `same` version, or is older (`beta`, `release`, …).

See [bug 2053559](https://bugzilla.mozilla.org/show_bug.cgi?id=2053559).

Check the job results for the last 30 days on [devtools-backward-monitor](https://firefox-devtools.github.io/devtools-backward-monitor/).

## Technical overview

The tests are written as regular aboutdebugging browser-chrome mochitests. They
can be found in `devtools/client/aboutdebugging/test/browser/browser_backward*`.

When run as regular mochitests however, they early return without failing. To
properly run the tests, you need to use `./mach devtools-compat-test`, which
will setup the server Firefox and then run the client test.

The server is setup by the python module found in
`devtools/client/aboutdebugging/test/backward_compat_test_server`. It handles
the logic to download, install and start the server. This server will be driven
by Marionette, and a few command wrappers are exposed through control.py.

`browser_backward_compat_xpcshell.js` is a special case. It does not use the
server Firefox. Instead it starts an xpcshell test with `--jsdebugger`, through
the `start-xpcshell` command of the control channel, and debugs it. The xpcshell
harness comes from the build under test, so this test only runs with the `local`
server, and returns early with the other servers.

## Running the tests

Locally you can run the whole test suite with:

```
./mach devtools-compat-test
```

By default this will create a Firefox server using your current Firefox build.

You can run the suite against our other channels: `release`, `beta`,
`devedition`, `nightly`, `local` (the default).

```
./mach devtools-compat-test --server {channel}
```

In this case, the devtools-compat-test task will download Firefox for the
selected channel from <https://download.mozilla.org/> and will start it as the
server. The downloaded and installed binaries are cached to speedup successive
runs.

Use `--headless` to hide all browser windows.

```
./mach devtools-compat-test --headless
```

Additional parameters are forwarded to the mochitest harness, such as `--repeat`,
or specific test path:

```
./mach devtools-compat-test \
  devtools/client/aboutdebugging/test/browser/browser_backward_compat_connect.js \
  --repeat 5
```

Running the tests with `./mach mochitest` or `./mach test` is not an error, but
as mentioned earlier the test will simply early return. Make sure to explicitly
use `./mach devtools-compat-test` to run the tests locally.

## Daily cron on CI

Three tasks run once a day on mozilla-central, through the
`devtools-backward-compat` cron job:

| Treeherder symbol | Server |
|---|---|
| `DTbc(same)` | the build under test, same version |
| `DTbc(beta)` | latest Firefox Beta |
| `DTbc(release)` | latest Firefox Release |

They will be scheduled in regular mozilla-central jobs. You can look for
previous runs by [filtering on devtools-compat](https://treeherder.mozilla.org/jobs?repo=mozilla-central&searchStr=devtools-compat).

An easier way to find the jobs is to directly check the [monitoring page](https://firefox-devtools.github.io/devtools-backward-monitor/).

## Manual Try push

If you want to push to try manually, you can use `./mach try fuzzy --full` and
select the `devtools-compat-*` jobs, or simply:

```
./mach try fuzzy --full -q devtools-compat
```

## Adding new tests

The tests are very similar to regular DevTools browser chrome mochitests, and
share the same helpers as the other aboutdebugging tests. To add a new test,
simply add it to devtools/client/aboutdebugging/test/browser/browser_backward_compat.toml.

## Adding a server-side action

Server-side actions are commands on the control channel. They allow to drive the
server browser via Marionette commands (examples: `_cmd_navigate`,
`_cmd_install_extension`).

To create a new action, add a `_cmd_<name>` method to `ControlServer` in
`backward_compat_test_server/control.py`, then call it from a test with
`runDevToolsServerCommand(config, "<name>", args)`.

For instance for `"install-extension"`:

```
  const { addonId } = await runDevToolsServerCommand(
    config,
    "install-extension"
  );
```
