# WebRTC iOS development

## Development Environment

In case you need to build the framework manually or you want to try out the
demo application AppRTCMobile, follow the instructions illustrated bellow.

A macOS machine is required for iOS development. While it's possible to
develop purely from the command line with text editors, it's easiest to use
Xcode. Both methods will be illustrated here.

_NOTICE:_ You will need to install [Chromium depot_tools][webrtc-prerequisite-sw].

## Getting the Code

Create a working directory, enter it, and run:

```
$ fetch --nohooks webrtc_ios
$ gclient sync
```

This will fetch a regular WebRTC checkout with the iOS-specific parts
added. Notice the size is quite large: about 6GB. The same checkout can be used
for both Mac and iOS development, since GN allows you to generate your
[Ninja][ninja] project files in different directories for each build config.

You may want to disable Spotlight indexing for the checkout to speed up
file operations.

Note that the git repository root is in `src`.

From here you can check out a new local branch with:

```
$ git new-branch <branch name>
```

See [Development][webrtc-development] for generic instructions on how
to update the code in your checkout.


## Generating project files

[GN][gn] is used to generate [Ninja][ninja] project files. In order to configure
[GN][gn] to generate build files for iOS certain variables need to be set.
Those variables can be edited for the various build configurations as needed.

The variables you should care about are the following:

* `target_os`:
  - To build for iOS this should be set as `target_os="ios"` in your `gn args`.
  The default is whatever OS you are running the script on, so this can be
  omitted when generating build files for macOS.
* `target_cpu`:
  - For builds targeting iOS devices, this should be set to either `"arm"` or
  `"arm64"`, depending on the architecture of the device. For builds to run in
  the simulator, this should be set to `"x64"`.
* `is_debug`:
  - Debug builds are the default. When building for release, specify `false`.

The component build is the default for Debug builds, which are also enabled by
default unless `is_debug=false` is specified.

The [GN][gn] command for generating build files is `gn gen <output folder>`.

After you've generated your build files once, subsequent invocations of `gn gen`
with the same output folder will use the same arguments as first supplied.
To edit these at any time use `gn args <output folder>`. This will open up
a file in `$EDITOR` where you can edit the arguments. When you've made
changes and save the file, `gn` will regenerate your project files for you
with the new arguments.

### Examples

```
$ # debug build for 64-bit iOS
$ gn gen out/ios_64 --args='target_os="ios" target_cpu="arm64"'

$ # debug build for simulator
$ gn gen out/ios_sim --args='target_os="ios" target_cpu="x64" enable_run_ios_unittests_with_xctest=true'
```

## Compiling with ninja

To compile, just run ninja on the appropriate target. For example:

```
$ ninja -C out/ios_64 AppRTCMobile
```

Replace `AppRTCMobile` in the command above with the target you
are interested in.

To see a list of available targets, run `gn ls out/<output folder>`.

## Using Xcode

Xcode is the default and preferred IDE to develop for the iOS platform.

*Generating an Xcode project*

To have GN generate Xcode project files, pass the argument `--ide=xcode`
when running `gn gen`. This will result in a file named `all.xcodeproj`
placed in your specified output directory.

Example:

```
$ gn gen out/ios --args='target_os="ios" target_cpu="arm64"' --ide=xcode
$ open -a Xcode.app out/ios/all.xcodeproj
```

*Compile and run with Xcode*

Compiling with Xcode is not supported! What we do instead is compile using a
script that runs ninja from Xcode. This is done with a custom _run script_
action in the build phases of the generated project. This script will simply
call ninja as you would when building from the command line.

This gives us access to the usual deployment/debugging workflow iOS developers
are used to in Xcode, without sacrificing the build speed of Ninja.

## Running the tests

There are several test targets in WebRTC. To run the tests, you must deploy the
`.app` bundle to a device (see next section) and run them from there.
To run a specific test or collection of tests, normally with gtest one would pass
the `--gtest_filter` argument to the test binary when running. To do this when
running the tests from Xcode, from the targets menu, select the test bundle
and press _edit scheme..._ at the bottom of the target dropdown menu. From there
click _Run_ in the sidebar and add `--gtest_filter` to the _Arguments passed on
Launch_ list.

If deploying to a device via the command line using [`ios-deploy`][ios-deploy],
use the `-a` flag to pass arguments to the executable on launch.

## Deploying to Device

It's easiest to deploy to a device using Xcode. Other command line tools exist
as well, e.g. [`ios-deploy`][ios-deploy].

**NOTICE:** To deploy to an iOS device you must have a valid signing identity
set up. You can verify this by running:

```
$ xcrun security find-identity -v -p codesigning
```

If you don't have a valid signing identity, you can still build for ARM,
but you won't be able to deploy your code to an iOS device. To do this,
add the flag `ios_enable_code_signing=false` to the `gn gen` args when you
generate the build files.

## Using WebRTC in your app

To build WebRTC for use in a native iOS app, it's easiest to build
`WebRTC.framework`. This can be done with ninja as follows, replacing `ios`
with the actual location of your generated build files.

```
ninja -C out/ios framework_objc
```

This should result in a `.framework` bundle being generated in `out/ios`.
This bundle can now be directly included in another app.

If you need a FAT `.framework`, that is, a binary that contains code for
multiple architectures, and will work both on device and in the simulator,
a script is available [here][framework-script]

The resulting framework can be found in out_ios_libs/.

Please note that you can not ship the FAT framework binary with your app
if you intend to distribute it through the app store.
To solve this either remove "x86-64" from the list of architectures in
the [build script][framework-script] or split the binary and recreate it without x86-64.
For instructions on how to do this see [here][strip-arch].


[cocoapods]: https://cocoapods.org/pods/GoogleWebRTC
[webrtc-prerequisite-sw]: https://webrtc.googlesource.com/src/+/main/docs/native-code/development/prerequisite-sw/
[webrtc-development]: https://webrtc.googlesource.com/src/+/main/docs/native-code/development/
[framework-script]: https://webrtc.googlesource.com/src/+/main/tools_webrtc/ios/build_ios_libs.py
[ninja]: https://ninja-build.org/
[gn]: https://gn.googlesource.com/gn/+/main/README.md
[ios-deploy]: https://github.com/phonegap/ios-deploy
[strip-arch]: http://ikennd.ac/blog/2015/02/stripping-unwanted-architectures-from-dynamic-libraries-in-xcode/
