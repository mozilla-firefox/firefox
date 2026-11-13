# Network Security Services

Network Security Services (NSS) is a set of libraries designed to support
cross-platform development of security-enabled client and server
applications. NSS supports TLS 1.2, TLS 1.3, PKCS #5, PKCS#7,
PKCS #11, PKCS #12, S/MIME, X.509 v3 certificates, and other security
standards.

## Getting started

In order to get started create a new directory on that you will be uses as your
local work area, and check out NSS and NSPR.

    git clone https://github.com/mozilla/nss
    git clone https://github.com/mozilla/nspr

NSS can also be cloned with mercurial

    hg clone https://hg.mozilla.org/projects/nss

## Building NSS

**This build system is under development. It does not yet support all the
features or platforms that NSS supports. To build on anything other than Mac or
Linux please use the legacy build system as described below.**

Build requirements:

* [gyp](https://gyp.gsrc.io/)
* [ninja](https://ninja-build.org/)

After changing into the NSS directory a typical build is done as follows

    ./build.sh

Once the build is done the build output is found in the directory
`../dist/Debug` for debug builds and `../dist/Release` for opt builds.
Exported header files can be found in the `include` directory, library files in
directory `lib`, and tools in directory `bin`. In order to run the tools, set
your system environment to use the libraries of your build from the "lib"
directory, e.g., using the `LD_LIBRARY_PATH` or `DYLD_LIBRARY_PATH`.

See [help.txt](https://hg.mozilla.org/projects/nss/raw-file/tip/help.txt) for
more information on using build.sh.

### Building for Android and iOS

`--target-os` cross-compiles NSPR and NSS with the platform's own toolchain.
Use a separate `--dist` directory for each target:

    # Android: an NDK (r19 or later), on Linux or macOS
    ./build.sh --target-os=android --android-ndk=$ANDROID_NDK_ROOT \
        --android-api=21 -t arm64 --opt --static --dist=../dist-android-arm64

    # iOS: Xcode, on macOS
    ./build.sh --target-os=ios --ios-sdk=iphonesimulator -t arm64 \
        --opt --static --dist=../dist-ios-sim-arm64

Android supports `-t arm`, `arm64`, `x64` and `ia32`; iOS supports `-t arm64`
and `x64` (simulator only). Both default to `arm64`.

## Building NSS (legacy build system)

After changing into the NSS directory a typical build of 32-bit NSS is done as
follows:

    make nss_build_all

The following environment variables might be useful:

* `BUILD_OPT=1` to get an optimised build

* `USE_64=1` to get a 64-bit build (recommended)

The complete list of environment variables can be found
[here](https://developer.mozilla.org/en-US/docs/Mozilla/Projects/NSS/Reference/NSS_environment_variables).

To clean the build directory run:

    make nss_clean_all

## Tests

### Setup

Make sure that the address `$HOST.$DOMSUF` on your computer is available. This
is necessary because NSS tests generate certificates and establish TLS
connections, which requires a fully qualified domain name.
You can test this by
calling `ping $HOST.$DOMSUF`. If this is working, you're all set.  If it's not,
set or export:

    HOST=nss
    DOMSUF=local

Note that you might have to add `nss.local` to `/etc/hosts` if it's not
there. The entry should look something like `127.0.0.1 nss.local nss`.

### Running tests

**Runnning all tests will take a while!**

    cd tests
    ./all.sh

Make sure that all environment variables set for the build are set while running
the tests as well.  Test results are published in the folder
`../../test_results/`.

Individual tests can be run with the `NSS_TESTS` environment variable,
e.g. `NSS_TESTS=ssl_gtests ./all.sh` or by changing into the according directory
and running the bash script there `cd ssl_gtests && ./ssl_gtests.sh`.  The
following tests are available:

    cipher lowhash libpkix cert dbtests tools fips sdr smime ssl ocsp merge pkits chains ec gtests ssl_gtests bogo policy

To make tests run faster it's recommended to set `NSS_CYCLES=standard` to run
only the standard cycle.

## Releases

NSS releases can be found at [Mozilla's download
server](https://ftp.mozilla.org/pub/security/nss/releases/). Because NSS depends
on the base library NSPR you should download the archive that combines both NSS
and NSPR.

## Contributing

[Bugzilla](https://bugzilla.mozilla.org/) is used to track NSS development and
bugs. File new bugs in the NSS product.

A list with good first bugs to start with are [listed
here](https://bugzilla.mozilla.org/buglist.cgi?keywords=good-first-bug%2C%20&keywords_type=allwords&list_id=13238861&resolution=---&query_format=advanced&product=NSS).

### NSS Folder Structure

The nss directory contains the following important subdirectories:

- `coreconf` contains the build logic.

- `lib` contains all library code that is used to create the runtime libraries.

- `cmd` contains a set of various tool programs that are built with NSS. Several
  tools are general purpose and can be used to inspect and manipulate the
  storage files that software using the NSS library creates and modifies. Other
  tools are only used for testing purposes.

- `test` and `gtests` contain the NSS test suite. While `test` contains shell
  scripts to drive test programs in `cmd`, `gtests` holds a set of
  [gtests](https://github.com/google/googletest).

A more comprehensible overview of the NSS folder structure and API guidelines
can be found
[here](https://developer.mozilla.org/en-US/docs/Mozilla/Projects/NSS/NSS_API_Guidelines).

## Build mechanisms related to FIPS compliance

NSS supports build configurations for FIPS-140 compliance, and alternative build
configurations that disable functionality specific to FIPS-140 compliance.

This section documents the environment variables and build parameters that
control these configurations.

### Build FIPS startup tests

The C macro NSS_NO_INIT_SUPPORT controls the FIPS startup self tests.
If NSS_NO_INIT_SUPPORT is defined, the startup tests are disabled.

The legacy build system (make) by default disables these tests.
To enable these tests, set environment variable NSS_FORCE_FIPS=1 at build time.

The gyp build system by default disables these tests.
To enable these tests, pass parameter --enable-fips to build.sh.

### Building either FIPS compliant or alternative compliant code

The C macro NSS_FIPS_DISABLED can be used to disable some FIPS compliant code
and enable alternative implementations.

The legacy build system (make) never defines NSS_FIPS_DISABLED and always uses
the FIPS compliant code.

The gyp build system by default defines NSS_FIPS_DISABLED.
To use the FIPS compliant code, pass parameter --enable-fips to build.sh.

### Test execution

The NSS test suite may contain tests that are included, excluded, or are
different based on the FIPS build configuration. To execute the correct tests,
it's necessary to determine which build configuration was used.

The legacy build system (make) uses environment variables to control all
aspects of the build configuration, including FIPS build configuration.

Because the gyp build system doesn't use environment variables to control the
build configuration, the NSS tests cannot rely on environment variables to
determine the build configuration.

A helper binary named nss-build-flags is produced as part of the NSS build,
which prints the C macro symbols that were defined at build time, and which are
relevant to test execution.
