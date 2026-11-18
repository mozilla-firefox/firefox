# WebRender Tests

The WebRender class of tests are used to test the WebRender module
(lives in gfx/wr) in a standalone way, without being pulled into Gecko.
WebRender is written entirely in Rust code, and has its own test suites.

If you are having trouble with these test suites, please contact the
Graphics team (#gfx on Matrix/Element or Slack) and they will be able to
point you in the right direction. Bugs against these test suites should
be filed in the [Core :: Graphics: WebRender](https://bugzilla.mozilla.org/enter_bug.cgi?product=Core&component=Graphics%3A%20WebRender) component.

## WebRender

The WebRender suite has a `WR(wrench)` test job per platform. Generally
these test jobs are only run if code inside the `gfx/wr` subtree are
touched, although they may also run if upstream files they depend on
(e.g. docker images) are modified.

### WR(wrench)

The exact commands run by this test job vary per-platform. Generally,
the commands do some subset of these things:

- build the different webrender crates with different features
  enabled/disabled to make sure they build without errors
- run `cargo test` to run the built-in rust tests
- run the reftests to ensure that the rendering produced by WebRender
  matches the expectations
- run the rawtests (scenarios hand-written in Rust code) to ensure the
  behaviour exhibited by WebRender is correct

#### Running locally (Desktop platforms)

The test scripts can be found in the `gfx/wr/ci-scripts/` folder and
can be run directly from the `gfx/wr` folder if you have the
prerequisite tools (compilers, libraries, etc.) installed. If you build
Firefox you should already have these tools. On MacOS you may
need to do a `brew install cmake pkg-config` in order to get
additional dependencies needed for building osmesa-src.

```shell
cd gfx/wr
ci-scripts/linux-debug-tests.sh # use the script for your platform as needed
```

Note that when running these tests locally, you might get small
antialiasing differences in the reftests, depending on your local
freetype library. This may cause a few tests from the `reftests/text`
folder to fail. Usually as long as they fail the same before/after your
patch it shouldn't be a problem, but doing a try push will confirm that.

#### Running locally (Android emulator/device)

To run the wrench reftests locally on an Android platform, you have to
first build the wrench tool for Android, and then run the mozharness
script that will control the emulator/device, install the APK, and run
the reftests. Steps for doing this are documented in more detail in the
`gfx/wr/wrench/android.txt` file.

#### Running on tryserver

To run on tryserver, use `./mach try fuzzy` and select the appropriate
`webrender-<platform>-(release|debug)` job.
