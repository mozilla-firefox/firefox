# Development with Custom GeckoView

If you are an engineer working on Gecko(View) then you might be interested in building Focus/Klar with your own build of GeckoView.

For this you will need to:

* Check out the `main` branch of the Firefox repository (or another branch)
* Setup your system to build Firefox for Android
* Package a GeckoView [AAR](https://developer.android.com/studio/projects/android-library.html)
* Modify your Focus gradle configuration to use your custom GeckoView AAR.

## Setup build system

Follow the [Build instructions](https://developer.mozilla.org/en-US/docs/Mozilla/Developer_guide/Build_Instructions/Simple_Firefox_for_Android_build) to setup a Firefox for Android build (ARM or x86).

A minimal `mozconfig` for GeckoView development might look like this (ARM):

```
ac_add_options --enable-application=mobile/android
ac_add_options --target=arm-linux-androideabi
# For x86 use: ac_add_options --target=i386-linux-android

ac_add_options --with-android-sdk="<path-to-android-SDK>"
ac_add_options --with-android-ndk="<path-to-android-NDK>"
```

## Package GeckoView AAR

After setting up your build system you should be able to build and package Firefox for Android:

```bash
./mach build
./mach package
```

Now you can create the GeckoView AAR from the compiled code:

```bash
./mach android archive-geckoview
```

This should create a file named `geckoview-*.aar` in your build output folder (MOZ_OBJDIR):

```bash
$ ls <your-output-directory>/gradle/build/mobile/android/geckoview/outputs/aar
geckoview-official-withGeckoBinaries-noMinApi-release.aar
```

## Point your Focus/Klar build to your AAR

In your Focus/Klar checkout open `app/build.gradle` (__not__ the build.gradle file in the root directory!) and locate the `repositories` block. This block defines where gradle will look for dependencies. Add the absolute path to your AAR as follows:

```groovy
repositories {
    // ...

    flatDir(
        name: 'localBuild',
        dirs: '<absolute path to AAR>'
    )
}
```

Now locate the ```dependencies``` block. This block defines which dependencies are needed to compile the application. Locate the already existing `armImplementation` and `x86Implementation` statements. Those are currently pointing to AARs that are pulled from our build servers. Replace the correct one (x86 / ARM) to use the name of your local AAR:

```groovy
dependencies {
    // ...

    // armImplementation "org.mozilla:geckoview-nightly-armeabi-v7a:60.0a1"
    armImplementation (
            name: 'geckoview-official-withGeckoBinaries-noMinApi-release',
            ext: 'aar'
    )
    x86Implementation "org.mozilla:geckoview-nightly-x86:60.0a1"

    // ...
}
```

Now either build the `klarArmDebug` or `klarX86Debug` build variant from Android Studio (Running "Sync Project with Gradle files" once might be needed) or build and install from the command line:

```bash
./gradlew installKlarArmDebug
./gradlew installKlarX86Debug
```

Finally, the default renderer might be set as Webview. You can check for the presence or absence of the Gecko logo at `focus:about` to verify. You may change rendering engine settings at `focus:test` and then press back for the app to restart. This should store your engine preference until you uninstall or clear data.

You can also change the default engine at `focus-android/app/src/debug/java/org/mozilla/focus/web/Config.kt`. Setting `DEFAULT_NEW_RENDERER` to `true` will use GeckoView.
