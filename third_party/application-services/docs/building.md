# Building Application Services

When working on Application Services, it's important to set up your environment for building the Rust code and the Android or iOS code needed by the application.

## First time builds

Building for the first time is more complicated than a typical Rust project.
To build for an end-to-end experience that enables you to test changes in
client applications like **Firefox for Android (Fenix)** and **Firefox iOS**, there are a number of build
systems required for all the dependencies. The initial setup is likely to take
a number of hours to complete.


## Building the Rust Components

*Complete this section before moving to the android/iOS build instructions.*
1. Make sure you cloned the repository:
  ```shell
    $ git clone https://github.com/mozilla/application-services # (or use the ssh link)
    $ cd application-services
    $ git submodule update --init --recursive
  ```
2. Install Rust: install [via rustup](https://www.rust-lang.org/tools/install)
3. Install your system dependencies:

    #### Linux
    1. Install the system dependencies required for building NSS
        1. Install gyp: `apt install gyp` (required for NSS)
        1. Install ninja-build: `apt install ninja-build`
        1. Install python3 (at least 3.6): `apt install python3`
        1. Install zlib: `apt install zlib1g-dev`
        1. Install perl (needed to build openssl): `apt install perl`
        1. Install patch (to build the libs): `apt install patch`

    1. Install the system dependencies required for SQLcipher
        1. Install tcl: `apt install tclsh` (required for SQLcipher)

    1. Install the system dependencies required for bindgen
        1. Install libclang: `apt install libclang-dev`

    #### MacOS
    1. Install Xcode: check the [ci config](https://github.com/mozilla/application-services/blob/main/.circleci/config.yml) for the correct version.
    1. Install Xcode tools: `xcode-select --install`
    1. Install homebrew via its [installation instructions](https://brew.sh/) (it's what we use for ci).
    1. Install the system dependencies required for building NSS:
        1. Install ninja and python: `brew install ninja python`
        1. Make sure `which python3` maps to the freshly installed homebrew python.
            1. If it isn't, add the following to your bash/zsh profile and `source` the profile before continuing:
                ```shell
                alias python3=$(brew --prefix)/bin/python3
                ```
            1. Ensure `python` maps to the same Python version. You may have to
               create a symlink:
               ```shell
               PYPATH=$(which python3); ln -s $PYPATH `dirname $PYPATH`/python
               ```
                1. Recent brew installations of Python may create unversioned symlinks but not add them to your `PATH`. You can instead run `brew info python` to see their installation locations, and add the corresponding location to your `PATH`. For example:
                    ```shell
                        export PATH="/opt/homebrew/opt/python@3.14/libexec/bin:$PATH"
                    ```
               
        1. Install [gyp-next](https://github.com/nodejs/gyp-next) into a virtualenv and put it on your path:
            ```shell
            python3 -m venv ~/.local/venvs/gyp-next
            ~/.local/venvs/gyp-next/bin/pip install gyp-next
            mkdir -p ~/.local/bin
            ln -sf ~/.local/venvs/gyp-next/bin/gyp ~/.local/bin/gyp
            ```
            1. Make sure `~/.local/bin` is on your path (add this to your bash/zsh profile if needed):
               ```shell
               export PATH="$HOME/.local/bin:$PATH"
               ```
            1. The virtualenv path must not contain spaces: gyp writes its Python path unquoted into the
               generated ninja files. This rules out `pipx`'s default location on macOS
               (`~/Library/Application Support/pipx`).
            1. Don't use the old Chromium gyp (`chromium.googlesource.com/external/gyp`). It is unmaintained
               and fails on current Python and Xcode versions.
    1. If linking fails with `tapi error: malformed file ... unknown architecture`, the selected macOS SDK is
       newer than your Command Line Tools linker. Check `xcrun --show-sdk-path`, and either update the
       Command Line Tools or pin a matching SDK in your bash/zsh profile, e.g.:
        ```shell
        export SDKROOT=$(xcrun --sdk macosx26.5 --show-sdk-path)
        ```
    #### Windows
    *Install windows build tools*

    > Why [Windows Subsystem for Linux (WSL)](https://docs.microsoft.com/en-us/windows/wsl/about)?
    >
    > It's currently tricky to get some of these builds working on Windows, primarily due to our use of SQLcipher. By using WSL it is possible to get builds working, but still have them published to your "native" local maven cache so it's available for use by a "native" Android Studio.

    1. Install [WSL](https://docs.microsoft.com/en-us/windows/wsl/about) (recommended over native tooling)
    1. Install unzip: `sudo apt install unzip`
    1. Install python3: `sudo apt install python3` *Note: must be python 3.6 or later*
    1. Install system build tools: `sudo apt install build-essential`
    1. Install zlib: `sudo apt-get install zlib1g-dev`
    1. Install tcl: `sudo apt install tcl-dev`

4. [Build the required NSS libraries](https://github.com/mozilla/application-services/blob/main/libs/README.md).
4. Check dependencies and environment variables by running: `./libs/verify-desktop-environment.sh`
  > Note that this script might instruct you to set some environment variables, set those by adding them to your
  `.zshrc` or `.bashrc` so they are set by default on your terminal. If it does so instruct you, you must
  run the command again after setting them so the libraries are built.
5. Run cargo test: `cargo test`

Once you have successfully run `./libs/verify-desktop-environment.sh` and `cargo test` you can move to the [**Building for Fenix**](building.md#building-for-fenix) and [**Building for iOS**](building.md#building-for-firefox-ios) sections below to setup your local environment for testing with our client applications.

---

## Building for Fenix
The following instructions assume that you are building `application-services` for Fenix, and want to take advantage of the
[Fenix Auto-publication workflow for android-components and application-services](howtos/locally-published-components-in-fenix.md).

1. Install Android SDK, JAVA, NDK and set required env vars
   1. [Bootstrap a Firefox for Android environment](https://firefox-source-docs.mozilla.org/mobile/android/index.html) (**not** inside the Application Service repository).
   1. Download and install [Android Studio](https://developer.android.com/studio/#downloads).
   1. Configure Android Studio to use the bootstrapped SDK under `Settings > Languages & Frameworks > Android SDK`, set it to the SDK from mozbuild. Ex: `~/.mozbuild/android-sdk-linux` or `~/.mozbuild/android-sdk-macos`
   1. Download the required NDK and command-line tools under `Settings > Languages & Frameworks > Android SDK > SDK Tools`. Pick:
      1. NDK (Side by side) version 29.0.14206865 [as configured](https://github.com/mozilla/application-services/blob/bb8cde8a5/taskcluster/docker/linux/Dockerfile#L25)
      1. Android SDK Command-line Tools (latest)
   1. Set environment variables based on the boostrapped code and the downloaded NDK. Add it to your rc file (either `.zshrc` or `.bashrc` depending on your shell) to make it permanent
      1. Set `JAVA_HOME` to point to the bootstraped JDK 25 installation directory. Ex: `export JAVA_HOME=~/.mozbuild/jdk/jdk-25.0.5+8/` (or `export JAVA_HOME=~/.mozbuild/jdk/jdk-25.0.5+8/Contents/Home` on Mac).
      1. Set `ANDROID_HOME` to the bootstraped Android SDK. As an example:
            ```shell
            export ANDROID_HOME=~/.mozbuild/android-sdk-linux
            ```

      1. Set `NSS_STATIC` to 1. Ex: `export NSS_STATIC=1`
      1. Set `NSS_DIR` to your local NSS folder. Ex: `export NSS_DIR=~/Mozilla/application-services/libs/desktop/linux-x86-64/nss`
      1. Set `ANDROID_NDK_ROOT` to the NDK you downloaded via Android Studio, Ex: `export ANDROID_NDK_ROOT=~/.mozbuild/android-sdk-linux/ndk/29.0.14206865`
1. If you are on Windows using WSL - drop to the section below, [Windows setup
for Android (WSL)](building.md#windows-setup-for-android-via-wsl) before proceeding.
1. Check dependencies, environment variables
   1. Run `./libs/verify-android-environment.sh`
   2. Follow instructions and rerun until it is successful.

**Note:** If you opened the application-services or Firefox repos in Android Studio, it likely added a `local.properties` or `gradle-daemon-jvm.properties` file that could override some of the environment variables above. If you experience build errors, correct or delete these values in the `local.properties` files.

### Windows setup for Android (via WSL)

Note: For non-Ubuntu linux versions, it may be necessary to execute `$ANDROID_HOME/tools/bin/sdkmanager "build-tools;26.0.2" "platform-tools" "platforms;android-26" "tools"`. See also [this gist](https://gist.github.com/fdmnio/fd42caec2e5a7e93e12943376373b7d0) for additional information.

#### Configure Maven

Configure maven to use the native windows maven repository - then, when doing `./gradlew install` from WSL, it ends up in the Windows maven repo. This means we can do a number of things with Android Studio in "native" windows and have then work correctly with stuff we built in WSL.

1. Install maven: `sudo apt install maven`
1. Confirm existence of (or create) a `~/.m2` folder
1. In the `~/.m2` create a file called `settings.xml`
1. Add the content below replacing `{username}` with your username:
```xml
    <settings>
      <localRepository>/mnt/c/Users/{username}/.m2/repository</localRepository>
    </settings>
```
---

## Building for Firefox iOS

1. Install [xcpretty](https://github.com/xcpretty/xcpretty#installation): `gem install xcpretty`
1. Run `./libs/verify-ios-environment.sh` to check your setup and environment
variables.
    - Make any corrections recommended by the script and re-run.
1. Run `./automation/build_ios_artifacts.sh` to build all the binaries, UniFFi bindings, Glean metrics generation

    > Note: The artifacts generated in the above steps can then be used in any consuming product



### Locally building Firefox iOS against a local Application Services

Detailed steps to build Firefox iOS against a local application services can be found [this document](./howtos/locally-published-components-in-firefox-ios.md)
