# JavaScript Logging

This document details logging in JavaScript. For c++ based logging, please see
the [Gecko Logging document](/xpcom/logging).

## Notes About Logging

### Logging Ethos

The general ethos is to keep the browser console "clean" of logging output by
default, except in situations where there are errors or potentially warnings.

There is also [a test](https://searchfox.org/firefox-main/source/browser/components/tests/marionette/test_no_errors_clean_profile.py)
which will fail if there are any unexpected logs on startup.

For situations where it is useful to have debug logging available, these are
generally covered by off-by-default logging which can be enabled by a preference.

Also be aware that console logging can have a performance impact even when the
developer tools are not displayed.

### Obsolete Utilities

In the tree, there are two modules that should be considered obsolete:
[Log.sys.mjs](https://searchfox.org/firefox-main/source/toolkit/modules/Log.sys.mjs)
and [Console.sys.mjs](https://searchfox.org/firefox-main/source/toolkit/modules/Console.sys.mjs). Existing uses should be transitioned to use `console.createInstance` as part of
the [one logger](https://bugzilla.mozilla.org/show_bug.cgi?id=881389) effort.

The `console` object is available in all areas of the Firefox code base and
has better integration into the developer tools than the existing modules.

## Logging using the Console Web API

The [Console Web API](https://developer.mozilla.org/docs/Web/API/console)
is available throughout the Firefox code base. It is the best tool to use, as it
integrates directly with the
[developer tools](/devtools-user/index), which
provide detailed logging facilities.

By default logs will be output to the browser console, according to the processes
and filters selected within the console itself.

Logs may also be output to stdout via the `devtools.console.stdout.chrome` preference.
By default, this is set to true for non-official builds, and false for official builds,
meaning most of the time, developers do not need to change it.

### console.createInstance

In some cases, it is useful to attribute logging to a particular module or have
it controlled by a preference. In this case, `console.createInstance` may be used
to create a specific logging instance.

For example:

```js
const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    maxLogLevelPref: "toolkit.download.loglevel",
    logModule: "Downloads",
  });
});
```

The preference might have a typical default value of `Error`. The available
levels are listed in [Console.webidl](https://searchfox.org/mozilla-central/rev/593c49fa812ceb4be45fcea7c9e90d15f59edb70/dom/webidl/Console.webidl#205-209).

This creates a lazily initialized console instance that can be used as follows:

```js
// Logs by default.
lazy.logConsole.error("Something bad happened");

// Doesn't log unless the preference was changed prior to logging.
lazy.logConsole.debug("foo", 123)
```

**Note:** Workers are not able to access preferences, and therefore must use
`maxLogLevel` rather than `maxLogLevelPref`.

### Other Options to console.createInstance

`console.createInstance` may be passed other options. See
[`ConsoleInstanceOptions` in the Web IDL for more details](https://searchfox.org/mozilla-central/rev/593c49fa812ceb4be45fcea7c9e90d15f59edb70/dom/webidl/Console.webidl#211-235)

The most useful of these is probably `maxLogLevel` which allows manually setting
the log level, which may be useful for transitioning to `console.createInstance`
from other logging systems where preferences already exist.
