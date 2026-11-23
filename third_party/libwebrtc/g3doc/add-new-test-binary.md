# Add a new test binary

This page lists all the steps needed in order to add a new test binary to
WebRTC's BUILD.gn files and ensure it will run on the presubmit and postsubmit
infrastructure. To add tests to an existing test binary, you only need to add
your `rtc_cc_test` targets to the `tests` of its `rtc_test_suite` target.

1. While working on your CL, define your tests with `rtc_cc_test` targets and
   add an `rtc_test_suite` target listing them in `tests` (see
   [testing.md](testing.md)). The `rtc_test_suite` target is the test binary,
   and it automatically adds `test:test_main` to its dependencies.

2. Add the newly created `rtc_test_suite` target to the `group("test_suites")`
   target in the root
   [BUILD.gn](https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/BUILD.gn).
   The build fails if an `rtc_cc_test` target is not in one of the test suites
   of this group.

3. Add the name of the newly created `rtc_test_suite` into
   [infra/specs/gn_isolate_map.pyl](https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/infra/specs/gn_isolate_map.pyl).

4. Add the name of the newly created `rtc_test_suite` into
   [infra/specs/test_suites.pyl](https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/infra/specs/test_suites.pyl).
   By default, you should add it to the `android_tests`, `desktop_tests` and
   `ios_simulator_tests` sections.

5. Run the script
   [infra/specs/generate_buildbot_json.py](https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/infra/specs/generate_buildbot_json.py)
   (no arguments) to generate the JSON scripts based on the previously modified
   .pyl scripts.

6. Build, test, review and submit!

The bots will execute the new configs as part of the CQ. Inspect some logs to
verify that your test is in fact executed by the bots where you expect them to
be.

The details of the (many) config files are described in
https://chromium.googlesource.com/chromium/src/testing/+/HEAD/buildbot/README.md.
