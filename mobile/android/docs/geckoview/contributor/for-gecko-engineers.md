# GeckoView For Gecko Engineers

## Table of contents

```{contents}
:local: true
```

### Introduction

Who this guide is for: As the title suggests, the target audience of
this guide is existing Gecko engineers who need to be able to build and
(locally) test GeckoView. If you aren’t already familiar with building
Firefox on a desktop platform, you’ll likely be better served by reading
[our general introduction](geckoview-quick-start.md). This guide may
also be helpful if you find you’ve written a patch that requires
changing GeckoView’s public API, see [Landing a Patch](#landing-a-patch).

Who this guide is not for: As mentioned above, if you are not already
familiar with building Firefox for desktop, you’d likely be better
served by our general bootstrapping guide. If you are looking to
contribute to front-end development of one of Mozilla’s Android
browsers, you’re likely better off starting with their codebase and
returning here only if actual GeckoView changes are needed. See, for
example, {searchfox}`Fenix on Mozilla Central <mobile/android/fenix>`.

What to do if this guide contains bugs or leads you astray: The quickest
way to get a response is to ask generally on #gv on Mozilla Slack.
If you believe the guide needs updating, it would also be good to file a bug
to request that.

## Configuring the build system

First, a quick note: This guide was written on MacOS 10.14; it should
translate quite closely to other supported versions of MacOS and to
Linux. Building GeckoView on Windows is not officially supported at the
moment. To begin with, re-run `./mach bootstrap`; it will present you
with options for the version of Firefox/GV that you want to build.
Currently, option `3` is
`GeckoView/Firefox for Android Artifact Mode` and `4` is
`GeckoView/Firefox for Android`; if you’re here, you want one of
these. The brief and approximately correct breakdown of `Artifact` vs
regular builds for GeckoView is that `Artifact` builds will not allow
you to work on native code, only on JS or Java. Once you’ve selected
your build type, `bootstrap` should do its usual thing and grab
whatever dependencies are necessary. You may need to agree to some
licenses along the way. Once `bootstrap` has successfully completed,
it will spit out a recommended `mozconfig`.

### Mozconfig and Building

If you’ve followed from the previous section, `./mach bootstrap`
printed out a recommended `mozconfig` that looks something like this:

```
# Build GeckoView/Firefox for Android:
ac_add_options --enable-project=mobile/android

# If --target is not specified it will default to host architecture for fast
# emulation (x86_64 or aarch64). For testing on physical phones you most likely
# want to use an aarch64 (ARM64) target.
# ac_add_options --target=aarch64
```

This will default to either x86_64 or aarch64 depending on your host
platform to allow for fast emulation. For testing builds on real phones,
you will want to uncomment the line specifying a target of aarch64.

Don’t worry about installing an emulator at the moment, that will be covered
shortly. It’s worth noting here that other `mozconfig` options will generally
work as you’d expect. Additionally, if you plan on debugging native code on
Android, you should include the `mozconfig` changes mentioned [in our native
debugging guide](native-debugging.md). Now, using that `mozconfig` with
any modifications you’ve made, simply `./mach build`. If all goes well, you
will have successfully built GeckoView.

### Installing, Running, and Using in Fenix/AC

An (x86-64) emulator is the most common and developer-friendly way of
contributing to GeckoView in most cases. If you’re going to go this
route, simply run `./mach android-emulator` — by default, this will
install and launch an x86-64 Android emulator running the same Android
7.0 image that is used on `try`. If you need a different emulator
image you can run `./mach android-emulator --help` for information on
what Android images are available via `mach`. You can also install an
emulator image via Android Studio. In cases where an emulator may not
suffice (eg graphics or performance testing), or if you’d simply prefer
not to use an emulator, you can opt to use an actual phone instead. To
do so, you’ll need to enable `USB Debugging` on your phone if you
haven’t already. On most modern Android devices, you can do this by
opening `Settings`, going to `About phone`, and tapping
`Build number` seven times. You should get a notification informing
you that you’ve unlocked developer options. Now return to `Settings`,
go to `Developer options`, and enable USB debugging.

#### GeckoView Example App

Now that you’ve connected a phone or setup an emulator, the simplest way
to test GeckoView is to launch the GeckoView Example app by running
`./mach run` (or install it with `./mach install` and run it
yourself). This is a simplistic GV-based browser that lives in the tree;
in many cases, it is sufficient to test and debug Gecko changes, and is
by far the simplest way of doing so. It supports remote debugging by
default — simply open Remote Debugging on your desktop browser and the
connected device/emulator should show up when the example app is open.
You can also use the example app for native debugging, follow the
[native debugging guide](native-debugging.md).

#### GeckoView JUnit Tests

Once you’ve successfully built GV, you can run tests from the GeckoView
JUnit test suite with `./mach geckoview-junit`. For further examples
(eg running individual tests, repeating tests, etc.), consult the [quick
start guide](geckoview-quick-start.md#running-tests-and-linter-locally).

#### Fenix and other GV-based Apps

If you are working on something for which the GeckoView Example app is
not sufficient for some reason, you may need to [use your local build of
GeckoView in one of Mozilla’s GV-based apps like Fenix](geckoview-quick-start.md#include-geckoview-as-a-dependency).

### Debugging

#### Remote Debugging

To recap a bit of the above, in the GeckoView Example app, remote
debugging is enabled by default, and your device should show up in your
desktop browser’s Remote Debugging window with no special effort. For
Fenix, you can enable remote debugging by opening the three-dot menu and
toggling `Remote debugging via USB` under `Developer tools`; other
Mozilla GV-based browsers have similar options.

#### Native Debugging

To perform native debugging on any GV app will require you to install
Android Studio and follow instructions [here](native-debugging.md).

### Landing a Patch

In most cases, there shouldn’t be anything out of the ordinary to deal
with when landing a patch that affects GeckoView; make sure you include
Android in your `try` runs and you should be good. However, if you
need to alter the GeckoView public API in any way — essentially anything
that’s exposed as `public` in GeckoView Java files — then you’ll find
that you need to run the API linter and update the change log. To do
this, first run `./mach lint --linter android-api-lint` — if you have
indeed changed the public API, this will give you a `gradle` command
to run that will give further instructions. GeckoView API changes
require two reviews from GeckoView team members; you can open it up to
the team in general by adding `#geckoview-reviewers` as a reviewer on
Phabricator.

#### Recommended Reading

- [Translations: Toolkit to Fenix](translations-toolkit-to-fenix.md): Creating
  a new Android feature using existing JavaScript toolkit code
