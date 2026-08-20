# Firefox for Android

(firefox-for-android-1)=

Firefox for Android consists of three layers:

- GeckoView: This is a library that brings the Gecko API to Android.
- Android Components: This is a library of reusable browser building blocks.
- Frontend (Fenix): This refers to the frontend interface.

All three components can be found in `mozilla-central`.
To contribute to any of the three, you will need to get set up as a contributor to Firefox.

## Mozilla Central Setup

(mozilla-central-setup-1)=

To set up Mozilla Central, you can follow the general instructions provided in the Mozilla Source Docs:

- {ref}`Getting Set Up To Work On The Firefox Codebase <Getting Set Up To Work On The Firefox Codebase>`

Additionally, for writing, submitting and updating a patch with git, you can refer to the following guide:

- {ref}`Firefox Contributors' Quick Reference <firefox-contributors-quick-reference>`

## Bootstrap

(bootstrap-setup)=

Bootstrap configures everything for GeckoView and Fenix (Firefox for Android) development.

- Ensure you have `mozilla-central` checked out. If this is the first
  time you are doing this, it may take some time.

```bash
git checkout central/default
```

If you are on a mac, you will need to have the Xcode build tools
installed. You can do this by either [installing
Xcode](https://developer.apple.com/xcode/) or installing only the
tools from the command line by running `xcode-select --install` and
following the on screen instructions.

If you are on a newer mac with an Apple Silicon M2 or M3 processor,
you also need to install rosetta for backwards compatilibilty:

```bash
softwareupdate --install-rosetta
```

You will need to `bootstrap` for GeckoView/Firefox for Android. The easiest way is to run the following command:

```bash
./mach --no-interactive bootstrap --application-choice="GeckoView/Firefox for Android"
```

:::{note}

- The `--no-interactive` argument will make `bootstrap` run start to finish without requiring any input from you. It will automatically accept any license agreements.
- The `--application-choice="GeckoView/Firefox for Android"` argument is needed when using `--no-interactive` so that "bootstrapping" is done for the correct application (instead of the default).

If you want to make all the selections yourself and/or read through the license agreements, you can simply run:

```bash
./mach bootstrap
```

:::

You should then choose one the following options:

### A- You will not change or debug any C++ code

Choose: `3. GeckoView/Firefox for Android Artifact Mode`

Artifact mode downloads pre-built C++ components rather than building them locally, trading bandwidth for time.
(more on Artifact mode)

### B- You intend to change or debug C++ code

Choose: `4. GeckoView/Firefox for Android`

This will build GeckoView from scratch, and take more time than the option above.

Once `./mach bootstrap` is complete, it will automatically write the configuration into a new `mozconfig` file.
If you already have a `mozconfig`, mach will instead output a new configuration that you should append to your existing file.

## Build from the command line

(build-from-cmd-line)=

In order to pick up the configuration changes we just made we need to
build from the command line. This will update generated sources, compile
native code, and produce GeckoView AARs and example and test APKs.

```bash
./mach build
```

## Build Using Android Studio

(build-with-android-studio)=

- Install [Android
  Studio](https://developer.android.com/studio/install).

- If on Windows, create a JAVA_HOME environment variable
  and set it to the path of the Android Studio jbr. With the default installation locations, it is
  `C:\Program Files\Android\Android Studio\jbr`.

- Choose File->Open from the toolbar

- Navigate to the root of your `mozilla-central` source directory and
  click “Open”

- Click yes if it asks if you want to use the gradle wrapper.

  - If the gradle sync does not automatically start, select File >
    Sync Project with Gradle Files.

- Wait for the project to index and gradle to sync. Once synced, the
  workspace will reconfigure to display the different projects.

  - annotations contains custom Java annotations used inside GeckoView
  - app contains geckoview build settings and omnijar. omnijar contains
    the parts of Gecko and GeckoView that are not written in Java or Kotlin
  - geckoview is the GeckoView project. Here is all the Java files
    related to GeckoView
  - geckoview_example is an example browser built using GeckoView.

Now you’re set up and ready to go.

**Important: at this time, building from Android Studio or directly from
Gradle does not (re-)compile native code, including C++ and Rust.** This
means you will need to run `mach build` yourself to pick up changes to
native code. [Bug
1509539](https://bugzilla.mozilla.org/show_bug.cgi?id=1509539) tracks
making Android Studio and Gradle do this automatically.

If you want set up code formatting for Kotlin, please reference
[IntelliJ IDEA configuration](https://github.com/Kotlin/ktfmt#intellij-android-studio-and-other-jetbrains-ides).

## Mobile Devices and Emulators

The default mozconfig file, located in the root directory of your `mozilla-central` contains
configurations for running mobile test devices in x86, arm64, and x86_64 architectures.

With the default mozconfig configurations, your machine's architecture should match the test device's
architecture. On newer Macs built on Apple Silicon, the mozconfig will likely not need to be changed
since it runs on arm64 and most mobile devices run on arm64.

If your machine has a different architecture compared to your physical test device, you should
uncomment the option matching your test device. Usually, this means uncommenting the arm64 option:

```bash
ac_add_options --target=aarch64
```

**Note:** When using an emulator, the mozconfig target configuration will most likely need to match
the architecture of your machine.

### Custom AVD

There are several reasons creating a custom AVD can be required, like e.g. the
default emulator setup might be too old for some tasks, and it might be
required to run some newer versions of the APIs or others.

Assuming the following environment variables are already set (versions/OS may change):

```bash
JAVA_HOME=$HOME/.mozbuild/jdk/jdk-17.0.12+7
ANDROID_HOME=$HOME/.mozbuild/android-sdk-linux
ANDROID_AVD_HOME=$HOME/.mozbuild/android-device/avd
PATH=$ANDROID_HOME/cmdline-tools/12.0/bin/:$PATH
```

You can identify usable packages already installed on your system via

```bash
sdkmanager --list
```

It will output list of available and installed packages. Packages of interest
are `system-images` and follow the rule `system-images;android-API;pkg;arch` where

- `API` is the [Android API level](https://developer.android.com/tools/releases/platforms)
- `pkg` is the set of package installed, e.g., `default`, `google_apis`, `google_apis_playstore`
- the last one being the emulator architecture and usually stick to `x86_64`.

The system image package you will use needs to be installed, so if it is not
already in the list above, please use (in this example it installs the package
for Android 14 (API level 34), `default` package and `x86_64` arch).

```bash
sdkmanager "system-images;android-34;default;x86_64"
```

Then the AVD can be created with:

```bash
avdmanager create avd --name android14-x86_64 --package "system-images;android-34;default;x86_64"
```

The name passed in argument can be whatever you want and the package is one of
the installed list. It is then required to modify entries within
{searchfox}`android_device.py <mozilla-central/rev/d56687458d4e6e8882c4b740e78413a0f0a69d59:testing/mozbase/mozrunner/mozrunner/devices/android_device.py#101-175>` to be able to make use of `mach android-emulator`,
changing the definition to match the name of the AVD created above. Example below:

```diff
diff --git a/testing/mozbase/mozrunner/mozrunner/devices/android_device.py b/testing/mozbase/mozrunner/mozrunner/devices/android_device.py
index 4f883261d45c1..07f91c1ab800e 100644
--- a/testing/mozbase/mozrunner/mozrunner/devices/android_device.py
+++ b/testing/mozbase/mozrunner/mozrunner/devices/android_device.py
@@ -151,7 +151,7 @@ AVD_DICT = {
     ),
     "x86_64": AvdInfo(
         "Android x86_64",
-        "mozemulator-x86_64",
+        "android14-x86_64",
         [
             "-skip-adb-auth",
             "-verbose",
```

Once the avd is created, it can be customized by changing the `config.ini`
file (located under `$ANDROID_AVD_HOME/<avd-name>.avd/`, e.g.,
`$HOME/.mozbuild/android-device/avd/android14-x86_64/config.ini` in the above
example). It is recommended to enable physical keyboard by changing the value
`hw.keyboard` to `yes` (otherwise only virtual keyboard input will work).

## Custom mozconfig with Android Studio

Out of the box, Android Studio will use the default mozconfig file, normally
located at `mozconfig` in the root directory of your `mozilla-central`
checkout.

To make Android Studio use a mozconfig in a custom location, you can add the
following to your `local.properties`:

```
mozilla-central.mozconfig=relative/path/to/mozconfig
```

Note that, when running mach from the command line, this value will be ignored,
and the mozconfig from the mach environment will be used instead.

To override the mozconfig used by mach, you can use the `MOZCONFIG` environment
variable, for example:

```
MOZCONFIG=debug.mozconfig ./mach build
```

## Project-Specific Instructions

Now that you're prepared and set up, you can access specific project instructions below:

- GeckoView: {ref}`Contributing to GeckoView <Contributing to GeckoView>`
- Android Components: [Mozilla Android Components](https://mozac.org/)
- Frontend: {ref}`Building Firefox for Android <Building Firefox for Android>`

## Android Request For Comment (RFC) Process

See {doc}`Android RFC Process <rfcs/0015-android-rfc-process>`.

```{toctree}
:hidden:
:caption: Firefox for Android

fenix
```

```{toctree}
:hidden:
:caption: Android Development Guides

shared/android/android_guide
shared/android/kotlin_guide
shared/android/accessibility_guide
shared/android/configure_java
shared/android/docker_guide
shared/android/CONTRIBUTING
shared/android/CONTRIBUTING_code
shared/android/sprint_process
shared/git_guide
shared/sentry_guide
shared/uplift_guide
shared/versioning
```

```{toctree}
:hidden:
:caption: Testing and Automation

shared/android/testing
shared/android/ui-testing
shared/android/device_testing
shared/android/automation
shared/android/taskcluster_guide
shared/android/writing_lint_rules
shared/android/fretboard
shared/android/workmanager_faq
```

```{toctree}
:hidden:
:caption: Decision Records

adrs/0000-template
adrs/0001-kotlin-test
rfcs/0000-template
rfcs/0015-android-rfc-process
shared/rfc/wallpapers_v2_network_improvements
```
