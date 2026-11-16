# Command Line Parameters

Firefox recognizes many (many!) command line parameters. Overview
documentation of these parameters lives here.

Most parameters can be supplied with either a single dash (`-foo`) or a double
dash (`--foo`); the two forms are equivalent. Many parameters are registered
by individual components, so the exact set available depends on the build and
platform. A current list can always be printed with `firefox --help`.

## Startup and profiles

```{eval-rst}
.. list-table::
   :widths: 30 70
   :header-rows: 1

   * - Parameter
     - Description
   * - ``-h``, ``--help``
     - Print a usage message listing the available parameters and exit.
   * - ``-v``, ``--version``
     - Print the Firefox version and exit.
   * - ``--full-version``
     - Print the Firefox version together with the build and platform build
       ids and exit.
   * - ``-P <profile>``
     - Start with the named ``<profile>``.
   * - ``--profile <path>``
     - Start with the profile located at ``<path>``.
   * - ``--migration``
     - Start with the migration wizard.
   * - ``--ProfileManager``
     - Start with the Profile Manager.
   * - ``--new-instance``
     - Open a new instance rather than a new window in an already running
       instance.
   * - ``--no-remote``
     - Do not accept or send remote commands; implies ``--new-instance``.
   * - ``--safe-mode``
     - Disable extensions and themes for this session.
   * - ``--allow-downgrade``
     - Allow a profile created by a newer version of Firefox to be opened by an
       older version (only available on builds that block profile downgrades).
   * - ``--first-startup``
     - Run post-install actions before opening a new window.
```

## Opening content

```{eval-rst}
.. list-table::
   :widths: 30 70
   :header-rows: 1

   * - Parameter
     - Description
   * - ``--browser``
     - Open a browser window.
   * - ``-url <url>``
     - Open ``<url>``.  This is the parameter implicitly used when a bare URL is
       passed on the command line, and the one used when another application
       delegates a URL to Firefox.
   * - ``--new-window <url>``
     - Open ``<url>`` in a new window.
   * - ``--new-tab <url>``
     - Open ``<url>`` in a new tab.
   * - ``--private-window [<url>]``
     - Open ``<url>`` (or a blank page) in a new private window.
   * - ``--search <term>``
     - Search for ``<term>`` with the default search engine.
   * - ``--preferences``
     - Open the Preferences (Options on Windows) dialog.
   * - ``--setDefaultBrowser``
     - Set Firefox as the default browser.
```

## Display modes

```{eval-rst}
.. list-table::
   :widths: 30 70
   :header-rows: 1

   * - Parameter
     - Description
   * - ``--headless``
     - Run without a GUI.  Available on Windows, Linux (GTK) and macOS.
   * - ``--screenshot [<path>]``
     - Take a screenshot and save it to ``<path>`` (or to the working
       directory).  Implies ``--headless``.
   * - ``--window-size width[,height]``
     - Width and, optionally, height to use for ``--screenshot``.
   * - ``--kiosk``
     - Start the browser in kiosk mode.
   * - ``--kiosk-monitor <num>``
     - Place the kiosk browser window on the given monitor.
   * - ``--start-maximized``
     - Start the browser with the window maximized.
   * - ``--disable-pinch``
     - Disable touch-screen and touch-pad pinch gestures.
```

## Logging and debugging

```{eval-rst}
.. list-table::
   :widths: 30 70
   :header-rows: 1

   * - Parameter
     - Description
   * - ``--MOZ_LOG=<modules>``
     - Treated as the ``MOZ_LOG=<modules>`` environment variable, overriding it.
   * - ``--MOZ_LOG_FILE=<file>``
     - Treated as the ``MOZ_LOG_FILE=<file>`` environment variable, overriding
       it.  If neither this argument nor the environment variable is set,
       logging is written to stdout.
   * - ``--jsconsole``
     - Open the Browser Console.
   * - ``--devtools``
     - Open the DevTools on initial load.
   * - ``--jsdebugger [<path>]``
     - Open the Browser Toolbox.  Defaults to the local build but can be
       overridden by a Firefox path.
   * - ``--wait-for-jsdebugger``
     - Spin the event loop until the JS debugger connects, enabling debugging of
       (some) application startup code paths.  Only has an effect when
       ``--jsdebugger`` is also supplied.
   * - ``--start-debugger-server [ws:][ <port> | <path> ]``
     - Start the DevTools server on a TCP port or Unix domain socket path.
       Defaults to TCP port 6000.  Use the WebSocket protocol if the ``ws:``
       prefix is specified.
   * - ``--marionette``
     - Enable the remote control (Marionette) server.
   * - ``--origin-to-force-quic-on <origin>``
     - Force the use of QUIC for the specified origin.
```

## Platform-specific parameters

```{eval-rst}
.. list-table::
   :widths: 30 70
   :header-rows: 1

   * - Parameter
     - Description
   * - ``--console``
     - On Windows, start Firefox with a debugging console.
   * - ``--dbus-service <launcher>``
     - On Linux builds with DBus support, run as a DBus service for
       ``org.freedesktop.Application`` and set ``<launcher>`` (usually the
       ``/usr/bin/appname`` script) for it.
   * - ``--display=DISPLAY``
     - On X11, the X display to use.
   * - ``--sync``
     - On X11, make X calls synchronous.
   * - ``--g-fatal-warnings``
     - On Unix, make all warnings fatal.
   * - ``-osint``
     - On Windows, ``-osint`` serves two purposes.  Most importantly, it signals
       that the command line is untrusted and must be sanitized.  Command lines
       with ``-osint`` are rejected unless they have a very specific structure,
       usually ``firefox.exe -osint -url URL`` or ``firefox.exe -osint
       -private-window URL``: refer to :searchfox:`the EnsureCommandLineSafe function <mozilla-central/rev/ead7da2d9c5400bc7034ff3f06a030531bd7e5b9:toolkit/xre/CmdLineAndEnvUtils.h#196>`.
       These command lines are produced by apps delegating to Firefox, and the
       resulting URL may not be correctly quoted.  The sanitization process
       ensures that maliciously chosen URLs do not add additional parameters to
       Firefox.  Secondarily, the ``-osint`` parameter signals that Firefox is
       being invoked by Windows to handle a URL: generally a registered file
       type, e.g., ``.html``, or protocol, e.g., ``https``.
```
