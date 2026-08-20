# Recurring DevTools tasks

There are a few things we should do on each Nightly cycle to keep our code clean and up-to-date.

## Update MDN data for the Compatibility panel

Follow instructions from [devtools/client/inspector/compatibility/README.md](https://searchfox.org/firefox-main/source/devtools/client/inspector/compatibility/README.md).

## Generate webidl-pure-allowlist.js and webidl-deprecated-list.js

The `webidl-pure-allowlist.js` file is used by the console instant evaluation, in order to know
if a given method does not have side effects. The file might be updated if new APIs are added,
or if methods are tagged as pure (or untagged).

The `webidl-deprecated-list.js` file will be used to avoid calling deprecated getters from devtools code.

1. Generating those files requires a non-artifact build. If you're mostly working with artifact builds, you might want to run `./mach bootstrap` in order to have a proper build environment.
2. Once the build is over, you should be able to follow instructions at the top of [GenerateDataFromWebIdls.py](https://searchfox.org/firefox-main/source/devtools/shared/webconsole/GenerateDataFromWebIdls.py), which should be:
   2.1. Run the script with `./mach python devtools/shared/webconsole/GenerateDataFromWebIdls.py`

## Remove backwards compatibility code

In order to accommodate connecting to older server, we sometimes need to introduce specific branches in the code. At the moment, we only support connecting to server 3 versions older than the client (e.g. if the client is 87, we support connecting to 86, 85 and 84).
This means that on each release there's an opportunity to cleanup backward compatibility code that was introduced for server we don't have to support anymore. If I go back to my example with the 87 client, we can remove any backward-compatibility code that was added in 84.

When adding compatibility code, we also add comments that follow a specific pattern: `@backward-compat { version XX }`, where `XX` is the version number the code is supporting.

Back to our example where the current version is 87, we need to list all the comments added for 84. This can be done by doing a search with the following expression: `@backward-compat { version 84 }` (here's the searchfox equivalent: [searchfox query](https://searchfox.org/mozilla-central/search?q=%40backward-compat%5Cs*%7B%5Cs*version+84%5Cs*%7D&path=&case=false&regexp=true)).

## Smoke test remote debugging

### Automated tests

We are currently switching backward compatibility tests to automated CI jobs.
See the [DevTools Backward Compatitility Tests](tests/backward-compat-tests.md)
documentation.

The DevTools backward compatibility job was added in [Bug 2053559](https://bugzilla.mozilla.org/show_bug.cgi?id=2053559).

The automated job currently only covers testing Desktop Firefox. Once we are
confident the job works correctly and [Android coverage has been added](https://bugzilla.mozilla.org/show_bug.cgi?id=2062545),
the manual smoke tests steps below should be removed.

### Setup

We will run the remote debugging smoke tests twice. Once to exercise backward compatibility, and once without backward compatibility (same version). The tests to run are the same in both cases (see Tests section).

You can use either desktop or mobile versions of Firefox as the server. Mobile is preferable as some codepaths are specific to Firefox mobile, but if you don't have access to an Android device, using a Desktop server is a decent alternative.

- [Instructions](https://firefox-source-docs.mozilla.org/devtools-user/about_colon_debugging/index.html#connecting-to-a-remote-device) to setup remote debugging for Firefox mobile.
- [Instructions](https://gist.github.com/juliandescottes/b0d3d83154d9ea8a84db5d32aa35d2c1) to setup remote debugging for Firefox desktop.

#### Backward compatibility test

- Start the current Nightly (release XX) as Client
- Prepare Firefox (release XX -1) as the Server. Either
  [https://play.google.com/store/apps/details?id=org.mozilla.firefox_beta](https://play.google.com/store/apps/details?id=org.mozilla.firefox_beta) (mobile beta) or
  Desktop Beta or DevEdition

#### Same version test

- Start the current Nightly (release XX) as Client
- Prepare Firefox (also for release XX) as the Server. Either
  [https://play.google.com/store/apps/details?id=org.mozilla.fenix](https://play.google.com/store/apps/details?id=org.mozilla.fenix) (mobile nightly)
  or Desktop Nightly

### Tests

#### Basic connection

- On the Client Firefox Nightly, open about:debugging
- Connect to the Server (either via network or USB)
- Open the corresponding Runtime Page

#### Debug targets

- On the Server Firefox, open a tab to [https://mdn.github.io/dom-examples/service-worker/simple-service-worker/](https://mdn.github.io/dom-examples/service-worker/simple-service-worker/)
- On the Client Firefox, check in the Runtime Page for the Server Firefox that you can see the new tab as well as the corresponding service worker
- On the Client Firefox, open the Profiler by clicking the Profile Performance button and record a short profile by clicking the Start, then the Stop button. Verify that the profiler opens a new tab with the recording.
- On the Server Firefox, close the tab you just opened
- On the Client Firefox, check that the corresponding tab is removed
- On the Client Firefox, unregister the service worker, check that the corresponding SW is removed from the list

#### Inspect a remote target

- On the Server Firefox, open a tab to [https://juliandescottes.github.io/webcomponents-playground/debugger-example/](https://juliandescottes.github.io/webcomponents-playground/debugger-example/)
- On the Client Firefox, click on Inspect for this tab. Check that toolbox opens. Now we will verify that the toolbox is working.
- Open Inspector, check that no panel is blank. Check that selecting another element in the markup-view updates the computed view.
- Open Console, check that you see the "script loaded" message. Type "1+1" in the console, check you get "2".
- Open Debugger, check that you can see the script.js source. Open it, put a breakpoint inside the clickMe() method (line 6). On the Server Firefox, click on the button in the page, check that you hit the breakpoint.
- Open the Network tab. If it is empty and tells you to "perform a request…", reload the page on the Server Firefox. Check that requests are displayed.

#### Inspect a remote extension

- On the Server Firefox, install any extension (for instance [https://addons.mozilla.org/en-US/firefox/addon/devtools-highlighter/](https://addons.mozilla.org/en-US/firefox/addon/devtools-highlighter/))
- On the Client Firefox, check the extension is displayed in the Extensions category
- Click on Inspect, check the toolbox opens.
- Check the Inspector, Console, Debugger and Netmonitor UIs for empty panels.

## Remove expired or renew telemetry probes

Bugs are automatically filed for all expired probes. You can find them using [this bugzilla query](https://bugzilla.mozilla.org/buglist.cgi?quicksearch=[probe-expiry-alert]%20devtools).

We should review the list of bugs and make sure each of them block the following [META bug](https://bugzilla.mozilla.org/show_bug.cgi?id=1566383).

Reviewing a probe means either to update the expiration field because we are still monitoring the data, or to remove the probe and all the related code for recording it. This discussion can happen on the bug.

## Check if third-party library should be updated

This is not a mandatory task to do on each cycle, but having up-to-date libraries can help us getting new features and most importantly bug fixes that will improve the user experience of DevTools users.

### WasmDis and WasmParser

These modules are used by the debugger to be able to parse and debug WASM sources.

Follow the [upgrade documentation](https://searchfox.org/firefox-main/source/devtools/client/shared/vendor/WASMPARSER_UPGRADING)

### jsbeautify

This module is used by the inspector and the webconsole to pretty print user input.

Follow the [upgrade documentation](https://searchfox.org/firefox-main/source/devtools/shared/jsbeautify/UPGRADING.md)

### CodeMirror should be updated

CodeMirror is used by our source editor component, which is used all over DevTools.

Follow the [upgrade section in the documentation](https://searchfox.org/firefox-main/source/devtools/client/shared/sourceeditor/README)

### fluent-react

This module is used in several panels to manage localization in React applications.

Follow the [upgrade documentation](https://searchfox.org/firefox-main/source/devtools/client/shared/vendor/FLUENT_REACT_UPGRADING)

### reselect

Follow the [upgrade documentation](https://searchfox.org/firefox-main/source/devtools/client/shared/vendor/RESELECT_UPGRADING)

### pretty-fast

**TODO**

## Check xpcshell debugging

Check that xpcshell tests can be debugged using the `--jsdebugger` option.

1. Run Firefox from `mozilla-central` using `./mach run`
1. Open an `about:debugging` tab
1. If you don't have a `localhost:6000` item on the left sidebar, click on `Setup`
1. In setup, add a new `localhost:6000` item under `Network Location`
1. From your `mozilla-central` folder, run an xpcshell test with the `--jsdebugger` option (e.g. `./mach test devtools/server/tests/xpcshell/test_front_destroy.js --jsdebugger`)
1. In the terminal, you should see the following message: `Waiting for the debugger to connect on port 6000`
1. Go back to the `about:debugging` tab, click on the `Connect` button next to `localhost:6000`, and click again on the `localhost:6000` item.
1. In the new screen, under the `Processes` section, there should be a `Multiprocess Toolbox` item with an `Inspect` button next to it. Click on the button.
1. This should open an `about:devtools-toolbox` tab, showing the test file, paused at the first breakable line.
1. Make sure you can add breakpoints in the test and that you can step/resume
