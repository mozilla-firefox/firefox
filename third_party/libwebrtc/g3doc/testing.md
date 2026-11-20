<!-- go/cmark -->

<!--* freshness: {owner: 'eshr' reviewed: '2026-04-28'} *-->

# Testing in WebRTC

This document collects advice and best practices for writing tests in WebRTC,
covering GN macros, GoogleTest/GMock usage, and available utilities.

## Configure GN Macros

### Use `rtc_cc_test` and `rtc_test_suite` to define tests

Define unit tests with `rtc_cc_test`. It defines a library with the test sources
and a standalone binary (prefixed with the folder name and suffixed with `_bin`)
to run them locally: `autoninja -C out/Default pc:proxy_unittest_bin` will
generate "out/Default/pc_proxy_unittest_bin" as your binary.

It should normally have a single source file, named after the target.

- **Example**:

  ```gn
  import("//webrtc.gni")

  rtc_cc_test("my_unittest") {
    sources = [ "my_unittest.cc" ]
    deps = [
      ":my_library",
      "//test:test_support",
    ]
  }
  ```

Use `rtc_test_suite` to aggregate `rtc_cc_test` targets into larger
"mega-targets" for CI/CQ (e.g., `peerconnection_unittests`). Each `rtc_cc_test`
target must be in exactly one `rtc_test_suite` target.

- **Example**:

  ```gn
  rtc_test_suite("my_tests") {
    tests = [ ":my_unittest" ]
  }
  ```

The binaries of both templates depend on `//test:test_main`, which provides the
standard `main()` function for running GoogleTest. iOS tests written with the
XCTest framework use `rtc_xctest` instead.

Refer to [add-new-test-binary.md](add-new-test-binary.md) for details on how to
add the binary for a new test suite to the infrastructure.

### Prefer `rtc_library` for test support

`rtc_library` is preferred for most targets (including test support code). It
automatically switches between `source_set` and `static_library` based on
configuration and the `testonly` flag.

## Follow GoogleTest and GMock Best Practices

These are documented in the
[Google testing blog](https://testing.googleblog.com/).

Refer to the
[GMock Matchers Reference](https://google.github.io/googletest/reference/matchers.html)
for a full list of available matchers.

### In WebRTC, include wrappers instead of raw headers

Always include `test/gtest.h` and `test/gmock.h` instead of the raw
GoogleTest/GMock headers. These wrappers handle warning suppressions and exports
correctly for WebRTC.

### Use `EXPECT_TRUE` and `EXPECT_FALSE` only for booleans

Use these macros for pure boolean comparisons only. Do not rely on implicit
conversions (e.g., checking if a pointer is non-null).

```cpp {.bad}
EXPECT_TRUE(pointer); // BAD: implicit conversion to bool
EXPECT_TRUE(integer_value); // BAD: implicit conversion
```

```cpp {.good}
using ::testing::NotNull;

EXPECT_NE(pointer, nullptr);
EXPECT_THAT(pointer, NotNull());
EXPECT_NE(integer_value, 0);
```

See [the relevant Abseil tips](https://abseil.io/tips/141) for details.

Note that `using` at the beginning of the file is
[recommended in gmock](https://google.github.io/googletest/gmock_cook_book.html).

### Prefer `EXPECT_THAT` for complex evaluations

`EXPECT_EQ`, `EXPECT_LE`, `EXPECT_LT`, `EXPECT_GE`, `EXPECT_GT` are acceptable
for simple cases. Prefer `EXPECT_THAT` and matchers for anything with complex
evaluations or when better error messages are needed.

```cpp {.bad}
EXPECT_TRUE(vec.size() == 1 && vec[0] == "val"); // poor failure message
```

```cpp {.good}
using ::testing::ElementsAre;

EXPECT_THAT(vec, ElementsAre("val"));
EXPECT_LE(value, 10); // okay for simple cases
```

### Avoid Yoda matching

Always put the **tested value first** and the **constant/expected value last**
in all expectations.

```cpp {.bad}
EXPECT_EQ(0, value);
```

```cpp {.good}
EXPECT_EQ(value, 0);
```

### Use `ASSERT_` for fatal conditions

Prefer `EXPECT_` for the normal case. This allows you to report several errors
from one single test run.

Use `ASSERT_` if failing the condition would cause a crash or undefined behavior
in subsequent code, or if failing the check would make later tests irrelevant
(e.g., checking if a pointer is null before dereferencing it).

This is also recommended by the
[GoogleTest primer](https://google.github.io/googletest/primer.html#assertions).

### Avoid expectations in helper functions

Helper functions in tests that contain expectations (`EXPECT_`/`ASSERT_`) are
discouraged. Instead, use matchers to make tests more readable and provide
better failure messages. You can create custom matchers in three ways:

1. **Returning a Matcher Combination**: Combine existing matchers.
   ```cpp
   auto FirstElementIs(int x) {
     return ::testing::AllOf(
         ::testing::Not(::testing::IsEmpty()),
         ::testing::ResultOf([](const auto& v) { return v.front(); }, ::testing::Eq(x)));
   }
   ```
2. **Using `MATCHER_P` Macros**: For simple parameterized matchers.
   - Example in [test/fake_encoded_frame.h](../test/fake_encoded_frame.h):
     ```cpp
     MATCHER_P(WithId, id, "") {
       return arg.Id() == id;
     }
     ```
3. **Implementing a Matcher Class**: For complex matchers that need to provide
   detailed explanations.
   - Example in [test/near_matcher.h](../test/near_matcher.h) which implements a
     `NearMatcher` for time types.
   - **Usage Example**:
     ```cpp
     #include "test/near_matcher.h"
     // ...
     EXPECT_THAT(actual_timestamp, webrtc::Near(expected_timestamp, TimeDelta::Millis(5)));
     ```

More information about writing matchers is found in the
[GMock cookbook](https://google.github.io/googletest/gmock_cook_book.html#NewMatchers).

## Use WebRTC Testing Utilities

### Use `GlobalSimulatedTimeController` for simulated time

Use `GlobalSimulatedTimeController` to run tests that depend on time (e.g.,
pacing, timeouts) without waiting for real time to pass. It provides a `Clock`
and `TaskQueueFactory`. Prefer modern simulated time infrastructure over legacy
`ScopedFakeClock`.

### Create an `Environment` using `CreateTestEnvironment`

The `Environment` is the modern way to propagate global utilities (like `Clock`,
`TaskQueueFactory`, `FieldTrials`) through the codebase. Tests that exercise any
component using an `Environment` **must always create** an `Environment` using
`webrtc::CreateTestEnvironment` (defined in
[test/create_test_environment.h](../test/create_test_environment.h)) and pass it
to the components being tested.

Using `CreateEnvironment` will cause the test to ignore the command line
`force-fieldtrials` flag, so this should not be used in tests unless there are
specific reasons for using it.

### Avoid polling, use `WaitUntil` when needed

`webrtc::WaitUntil` (located in [test/wait_until.h](../test/wait_until.h)) is a
modern utility for waiting for a condition to become true. It is best to avoid
polling where possible, but when needed, use `WaitUntil` instead of manual
polling or sleeping. It polls a predicate using the `TimeController`.

### Use `RunLoop` for single-threaded async simulation

`webrtc::test::RunLoop` (located in `test/run_loop.h`) is a helper class for
tests that need to process tasks posted to a task queue but still want to run
everything on a single thread. It is useful for simulating async operations
simply.

### Select a Scenario Framework

Some of the available frameworks:

- **`Scenario`** (`test/scenario`): Best for **network and media quality**
  evaluation (e.g., bandwidth estimation, congestion control).
- **`PeerScenario`** (`test/peer_scenario`): Best for **signaling and
  PeerConnection API** level tests that require multiple threads and a simulated
  network but want to stay lightweight. **`PCLF** (`api/test/pclf`) is a
  framework for full integration tests.
- \*\*`IntegrationTestHelpers` (`pc/test/integration_test_helpers`) is an older
  set of tools that is heavily used for PeerConnection-related unit testing.

Other frameworks for more specialized purposes also exist. <--! Question: Should
we recommend one over the others? -->

### Mocking

A lot of classes have existing mocks. Mocks for classes used in `api` live in
`api/test`; there are some in `test`, but in general, mocks for internal classes
should live close to the class they mock.

Mocks should be "pure mocks" using `gmock`. If the test double needs to have
code in it, it should be called a fake. See
[this testing blog](https://testing.googleblog.com/2013/07/testing-on-toilet-know-your-test-doubles.html)
for terminology.

General advice on mocking is in the
[GMock cookbook](https://google.github.io/googletest/gmock_cook_book.html).

### Leverage Other Utilities

- **`FrameGenerator`**: Located in `test/frame_generator.h`; it is useful for
  generating video frames for testing video pipelines.

## Run Tests

### Use `gtest-parallel` for faster local execution

For faster local execution, use
`third_party/gtest-parallel/gtest-parallel <binary>`.

Note that some tests are timing dependent and may be flaky when run under
`gtest-parallel`.
