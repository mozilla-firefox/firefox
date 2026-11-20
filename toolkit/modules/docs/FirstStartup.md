# FirstStartup

`FirstStartup` is a module which is invoked on application startup by the Windows Installer,
to initialize services before the first application window appears.

This is useful for:

- one-time performance tuning
- downloading critical data (hotfixes, experiments, etc)

Blocking until the first Application window appears is important because the Installer
will show a progress bar until this happens. This gives a user experience of:

1. User downloads and starts the Windows Stub Installer.
2. Progress bar advances while the application is downloaded and installed.
3. Installer invokes the application with `--first-startup`.
4. Application window appears, and the installer window closes.

Overall, the user experiences a very fast first-startup, with critical tasks that normally
would be deferred until after UI startup already complete.

(firststartup-architecture)=

## FirstStartup: Example use case

An example use of the `FirstStartup` module is to invoke Nimbus to download an experiment
that will be used to customize the first-run page that Firefox shows.

In this example, the first-run page would be loaded experimentally based on an attribution code provided
by the Installer. The flow for this looks like:

1. User clicks on download link containing an attribution (UTM) code(s).
2. The download page serves a custom Windows Stub Installer with the appropriate attribution code embedded.
3. The installer invokes Firefox with the `--first-startup` flag, which blocks the first window.
4. Nimbus is initialized by `FirstStartup` and downloads a list of available experiments, or "recipes".
5. Recipes are evaluated and filtered based on local information, such as the OS platform and the attribution codes.
6. A recipe is found which matches the current attribution code, and appropriate data is made available to the first-run page.
7. `FirstStartup` completes and unblocks, which causes Firefox to show the first window and load the appropriate first-run data.

## List of phases

`FirstStartup.NOT_STARTED`

The `FirstStartup` module has not been initialized (the `init()`
function has not been called). This is the default state.

`FirstStartup.IN_PROGRESS`

`FirstStartup.init()` has been called, and the event loop is
spinning. This state will persist until either all startup tasks
have finished, or time-out has been reached.

The time-out defaults to 30 seconds, but is configurable via the
`first-startup.timeout` pref, which is specified in milliseconds.

`FirstStartup.TIMED_OUT`

The time-out has been reached before startup tasks are complete.

`FirstStartup.SUCCESS`

All startup tasks have completed successfully, and application startup may resume.

`FirstStartup.UNSUPPORTED`

No startup tasks are supported, and `FirstStartup` exited.
