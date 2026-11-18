# Build GeckoView Reference Browser

## When do you need to build GeckoView Reference Browser

If a remote debugging change impacts the server (file located in `devtools/server` or in `devtools/shared`), you will not be able to test it without building your GeckoView Reference Browser. For the purpose of remote debugging, your local build of Firefox Desktop where you will test about:debugging runs the content of `devtools/client` (including `devtools/client/aboutdebugging`). And `devtools/server` runs on the device. So as soon as you are developing or testing a patch that needs to update the server and is about USB debugging, you need to build GeckoView Reference Browser and deploy it on a test device. To build custom Reference Browser, need two modules of GeckoView and Reference Browser.

## Setup your environment and build

This will be a short documentation focused on the typical patches you may write for about:debugging. For a more complete documentation, you can refer to https://mozilla.github.io/geckoview/ and https://firefox-source-docs.mozilla.org/mobile/android/.

The whole setup needs to download several gigabytes of dependencies so try to have a fast internet connection to follow those steps.

### Make empty directory

It is recommended to create a new directory to build your GeckoView and Reference Browser.

```
  mkdir geckoview-reference-browser
  cd geckoview-reference-browser
```

### Build GeckoView

#### Clone the Firefox repository

It is recommended to create a new clone of the Firefox repository for your GeckoView builds.

```
  git clone https://github.com/mozilla-firefox/firefox.git mozilla-central-gecko-view
  cd mozilla-central-gecko-view
```

#### Run bootstrap

Next simply run `mach bootstrap` and select the third option `3. GeckoView/Firefox for Android Artifact Mode`

```
  > ./mach bootstrap
  Please choose the version of Firefox you want to build:
  1. Firefox for Desktop Artifact Mode
  2. Firefox for Desktop
  3. GeckoView/Firefox for Android Artifact Mode
  4. GeckoView/Firefox for Android
  > 3
```

Follow the instructions, it will take some time as it needs to download a lot of dependencies. At the end it will provide you with a template you should use to create a `.mozconfig` file. You can use the proposed content without changing anything.

### Build

Execute command below to build.

```
  ./mach build
  ./mach package
  ./mach android archive-geckoview
```

If the build has finished successfully, the GeckoView AAR file will be created in your build output directory. You can find this file with following command:

```
  > ls mozilla-central-gecko-view/<your-output-directory>/gradle/build/mobile/android/geckoview/outputs/aar
  geckoview-withGeckoBinaries-debug.aar
```


### Build Reference Browser

#### Clone reference-browser

It is recommended to create a new clone of reference-browser for your Reference Browser builds.

```
  cd ../
  git clone https://github.com/mozilla-mobile/reference-browser
  cd reference-browser
```

#### Create `local.properties` file

`local.properties` file is necessary to specify the location of the Android SDK. Please write the absolute path of Android SDK with `sdk.dir` key. If you did `./mach bootstrap` once, Android SDK should already installed. You can find the directory whose name is like `android-sdk-<os-name>` under `~/.mozconfig/`. Thus, in mac osx case, the content of `local.properties` should be like below:

```
sdk.dir=/Users/xxxxxx/.mozbuild/android-sdk-osx
```

#### Edit `app/build.gradle` to build Reference Browser with above GeckoView

You need to edit two places in `app/build.gradle`.

1. Add `repositories` block with following content to bottom of file. <absolute path to AAR> is the directory which was created by `./mach android archive-geckoview` to build GeckoView. This should be like `/User/xxxxxx/mozilla-central-gecko-view/<your-output-directory>/gradle/build/mobile/android/geckoview/outputs/aar`.

```
repositories {
    flatDir(
        name: "localBuild",
        dirs: "<absolute path to AAR>"
    )
}
```

2. Edit `geckoNightlyArmImplementation`

```
dependencies {
    // ...

    //geckoNightlyArmImplementation Gecko.geckoview_nightly_arm
    geckoNightlyArmImplementation(
        name: 'geckoview-withGeckoBinaries-debug',
        ext: 'aar'
    )

    // ...
}
```

#### Build and deploy to your phone

Connect your phone to your computer with a USB cable. Then run:

```
  ./gradlew build
  ./gradlew installGeckoNightlyArmDebug
```

At this step if you go to the list of applications on your phone, you should be able to spot a "Reference Browser" application.

### Enable USB debugging on your phone

If you already used your device for USB debugging, this should already be enabled, but we will repeat the steps here.

In the Settings menu, choose "About" and scroll down to the Build Number option. There's a hidden option there to activate "developer mode": tap the Build Number option seven times. You’ll see a countdown, and then a "Developer Options" menu will appear in your Settings. Don’t worry — you can turn this off whenever you like. The last step is to enable USB Debugging in the Developer Options menu on Reference Browser on your device.

And, you can test with Reference Browser with custom GeckoView!

## Reflect changes

If you change codes in GeckoView, need to build and install again. Reflect the changes by:

```
  cd mozilla-central-gecko-view
  ./mach build faster
  ./mach package
  ./mach android archive-geckoview
  cd ../
  cd reference-browser
  ./gradlew build
  ./gradlew installGeckoNightlyArmDebug
```

Once you built all, the changes under `devtools/server` and `devtools/shared` can build with `faster` option. This should be faster.
