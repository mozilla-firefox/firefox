# Build Firefox for Android

## When do you need to build Firefox for Android

If a remote debugging change impacts the server (file located in `devtools/server` or in `devtools/shared`), you will not be able to test it with a device which runs the release version of Firefox for Android. For the purpose of remote debugging, your local build of Firefox Desktop where you will test about:debugging runs the content of `devtools/client` (including `devtools/client/aboutdebugging`). And `devtools/server` runs on the device. So as soon as you are developing or testing a patch that needs to update the server and is about USB debugging, you need to build Firefox for Android and deploy it on a test device.

## Setup your environment

This will be a short documentation focused on the typical patches you may write for about:debugging. For a more complete documentation, you can refer to https://firefox-source-docs.mozilla.org/mobile/android/.

The whole setup needs to download several gigabytes of dependencies so try to have a fast internet connection to follow those steps.

### Clone the Firefox repository

It is recommended to create a new clone of the Firefox repository for your Firefox for Android builds.

```
  git clone https://github.com/mozilla-firefox/firefox.git mozilla-central-android
  cd mozilla-central-android
```

### Run bootstrap

Next simply run `mach bootstrap` and select the third option `3. Firefox for Android Artifact Mode`

```
  > ./mach bootstrap
  Please choose the version of Firefox you want to build:
  1. Firefox for Desktop Artifact Mode
  2. Firefox for Desktop
  3. Firefox for Android Artifact Mode
  4. Firefox for Android
  > 3
```

Follow the instructions, it will take some time as it needs to download a lot of dependencies. At the end it will provide you with a template you should use to create a `.mozconfig` file. You can use the proposed content without changing anything.

### Enable USB debugging on your phone

If you already used your device for USB debugging, this should already be enabled, but we will repeat the steps here.

In the Settings menu, choose "About" and scroll down to the Build Number option. There's a hidden option there to activate "developer mode": tap the Build Number option seven times. You’ll see a countdown, and then a "Developer Options" menu will appear in your Settings. Don’t worry — you can turn this off whenever you like. The last step is to enable USB Debugging in the Developer Options menu.

## Build and deploy to your phone

Connect your phone to your computer with a USB cable. Then run:

```
  ./mach build
  ./mach package
  ./mach install
```

At this step if you go to the list of applications on your phone, you should be able to spot a "Fennec" application. The fullname will be slightly different, for instance for me it is called "Fennec jdescottes". You can then run the application from your desktop command-line:

```
  ./mach run
```

Sometimes this will fail with `WARNING: unable to launch Firefox for Android`. In that case you can simply start the application on your phone, as you would start any other application.
