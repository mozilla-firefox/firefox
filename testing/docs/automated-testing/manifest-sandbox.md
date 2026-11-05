# Adding Context to `manifestparser` based Manifests

Suites that use `manifestparser`, like Mochitest and XPCShell, have test
manifests that denote whether a given test should be skipped or not based
on a set of context.

Gecko builds generate a `target.mozinfo.json` with metadata about the build,
written by {searchfox}`python/mozbuild/mozbuild/mozinfo.py`. An excerpt of one
might look like this:

```json
{
  "os": "linux",
  "processor": "x86_64",
  "bits": 64,
  "debug": false,
  "asan": false,
  "nightly_build": true,
  "toolkit": "gtk"
}
```

These keys can then be used `skip-if` in test manifests:

```
skip-if = e10s && os == 'win'
```

In this case, `e10s` is a boolean.

The test will download the build's `target.mozinfo.json`, then update the
mozinfo dictionary with additional runtime information based on the task or
runtime environment. This logic lives in [mozinfo](https://hg.mozilla.org/mozilla-central/file/default/testing/mozbase/mozinfo/mozinfo/mozinfo.py).

## How to Add a Keyword

Where to add the new key depends on what type of information it is.

1. If the key is a property of the build, you'll need to patch {searchfox}`this file <python/mozbuild/mozbuild/mozinfo.py>`.
2. If the key is a property of the test environment, you'll need to patch
   {doc}`mozinfo </mozbase/mozinfo>`.
3. If the key is a runtime configuration, for example based on a pref that is
   passed in via mach or the task configuration, then you'll need to update the
   individual test harnesses. For example, {searchfox}`this location <mozilla-central/rev/a7e33b7f61e7729e2b1051d2a7a27799f11a5de6:testing/mochitest/runtests.py#3341>`
   for Mochitest. Currently there is no shared location to set runtime keys
   across test harnesses.

# Adding a Context to Reftest Style Manifests

Reftests and Crashtests use a different kind of manifest, but the general idea
is the same.

As before, Gecko builds generate a `target.mozinfo.json` with metadata about
the build. This is consumed in the Reftest harness and translated
into keywords that can be used like:

```
fuzzyIf(cocoaWidget&&isDebugBuild,1-1,85-88)
```

In this case, `cocoaWidget` and `isDebugbuild` are booleans.

The test will download the build's `target.mozinfo.json`, then in addition to
the mozinfo, will query runtime info from the browser to build a sandbox of
keywords. This logic lives in {searchfox}`manifest.sys.mjs <layout/tools/reftest/manifest.sys.mjs#439>`.

## How to Add a Keyword

Where to add the new key depends on what type of information it is.

1. If the key is a property of the build, you'll need to patch {searchfox}`this file <python/mozbuild/mozbuild/mozinfo.py>`.
2. If the key is a property of the test environment or a runtime configuration,
   then you'll need need to update manifest sandbox.

For example, for Apple Silicon, we can add an `apple_silicon` keyword with a
patch like this:

```diff
--- a/layout/tools/reftest/manifest.sys.mjs
+++ b/layout/tools/reftest/manifest.sys.mjs
@@ -572,16 +572,18 @@ function BuildConditionSandbox(aURL) {

    // Set OSX to be the Mac OS X version, as an integer, or undefined
    // for other platforms.  The integer is formed by 100 times the
    // major version plus the minor version, so 1006 for 10.6, 1010 for
    // 10.10, etc.
    var osxmatch = /Mac OS X (\d+).(\d+)$/.exec(hh.oscpu);
    sandbox.OSX = osxmatch ? parseInt(osxmatch[1]) * 100 + parseInt(osxmatch[2]) : undefined;

+   sandbox.apple_silicon = sandbox.cocoaWidget && sandbox.OSX>=11;
+
    // Plugins are no longer supported.  Don't try to use TestPlugin.
    sandbox.haveTestPlugin = false;

    // Set a flag on sandbox if the windows default theme is active
    sandbox.windowsDefaultTheme = g.containingWindow.matchMedia("(-moz-windows-default-theme)").matches;

    try {
        sandbox.nativeThemePref = !prefs.getBoolPref("widget.disable-native-theme-for-content");
```

Then to use this:

```
fuzzy-if(apple_silicon,1-1,281-281) == frame_above_rules_none.html frame_above_rules_none_ref.html
```
