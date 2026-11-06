# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Guideline to update the version

Increment the:

* MAJOR version when you make incompatible API/ABI changes,
* MINOR version when you add functionality in a backwards compatible manner, and
* PATCH version when you make backwards compatible bug fixes.

## [Unreleased]

## [1.28.0] 2026-07-16

* [RELEASE] Bump main branch to 1.28.0-dev
  [#4081](https://github.com/open-telemetry/opentelemetry-cpp/pull/4081)

* Bump step-security/harden-runner from 2.19.1 to 2.19.2
  [#4082](https://github.com/open-telemetry/opentelemetry-cpp/pull/4082)

* Bump step-security/harden-runner from 2.19.2 to 2.19.3
  [#4084](https://github.com/open-telemetry/opentelemetry-cpp/pull/4084)

* Fix IWYU Clang22 warnings
  [#4083](https://github.com/open-telemetry/opentelemetry-cpp/pull/4083)

* Bump github/codeql-action from 4.35.4 to 4.35.5
  [#4087](https://github.com/open-telemetry/opentelemetry-cpp/pull/4087)

* [CODE HEALTH] Remove unused alias declarations
  [#4091](https://github.com/open-telemetry/opentelemetry-cpp/pull/4091)

* Bump codecov/codecov-action from 6.0.0 to 6.0.1
  [#4092](https://github.com/open-telemetry/opentelemetry-cpp/pull/4092)

* [SDK] MeterProvider: do not warn in destructor after explicit Shutdown
  [#4085](https://github.com/open-telemetry/opentelemetry-cpp/pull/4085)

* Fix: preserve delta start timestamp in fast path
  [#4069](https://github.com/open-telemetry/opentelemetry-cpp/pull/4069)

* Bump step-security/harden-runner from 2.19.3 to 2.19.4
  [#4098](https://github.com/open-telemetry/opentelemetry-cpp/pull/4098)

* Bump docker/build-push-action from 7.1.0 to 7.2.0
  [#4097](https://github.com/open-telemetry/opentelemetry-cpp/pull/4097)

* Bump actions/stale from 10.2.0 to 10.3.0
  [#4096](https://github.com/open-telemetry/opentelemetry-cpp/pull/4096)

* docs: update custom log handler example
  [#4089](https://github.com/open-telemetry/opentelemetry-cpp/pull/4089)

* [BUILD] Suppress deprecated warnings in generated semconv metric headers
  [#4088](https://github.com/open-telemetry/opentelemetry-cpp/pull/4088)

* Bump docker/setup-buildx-action from 4.0.0 to 4.1.0
  [#4103](https://github.com/open-telemetry/opentelemetry-cpp/pull/4103)

* Bump github/codeql-action from 4.35.5 to 4.36.0
  [#4102](https://github.com/open-telemetry/opentelemetry-cpp/pull/4102)

* Bump docker/setup-qemu-action from 4.0.0 to 4.1.0
  [#4107](https://github.com/open-telemetry/opentelemetry-cpp/pull/4107)

* [CI] add cmake gcc maintainer abiv2 job
  [#4105](https://github.com/open-telemetry/opentelemetry-cpp/pull/4105)

* [DOC] Fix typos in SDK header comments
  [#4111](https://github.com/open-telemetry/opentelemetry-cpp/pull/4111)

* [CI] update codeql to cover all options with abiv2
  [#4106](https://github.com/open-telemetry/opentelemetry-cpp/pull/4106)

* [EXPORTER] Convert uint64_t attribute values exceeding
  INT64_MAX to string per OTel spec
  [#4090](https://github.com/open-telemetry/opentelemetry-cpp/pull/4090)

* [CODE HEALTH] Remove last unused nostd namespace alias
  in otlp_populate_attribute_utils
  [#4114](https://github.com/open-telemetry/opentelemetry-cpp/pull/4114)

* [CODE HEALTH] Move curl_http_test classes into anonymous namespace
  [#4115](https://github.com/open-telemetry/opentelemetry-cpp/pull/4115)

* [CODE HEALTH] Move simple_log_record_processor_test into anonymous namespace
  [#4116](https://github.com/open-telemetry/opentelemetry-cpp/pull/4116)

* Bump actions/checkout from 6.0.2 to 6.0.3
  [#4118](https://github.com/open-telemetry/opentelemetry-cpp/pull/4118)

* Bump github/codeql-action from 4.36.0 to 4.36.1
  [#4117](https://github.com/open-telemetry/opentelemetry-cpp/pull/4117)

* [CODE HEALTH] Move registry.cc propagator builders into anonymous namespace
  [#4121](https://github.com/open-telemetry/opentelemetry-cpp/pull/4121)

* [CODE HEALTH] Move sdk_builder.cc builders into anonymous namespace
  [#4122](https://github.com/open-telemetry/opentelemetry-cpp/pull/4122)

* Bump github/codeql-action from 4.36.1 to 4.36.2
  [#4123](https://github.com/open-telemetry/opentelemetry-cpp/pull/4123)

* [CODE HEALTH] Fix clang-tidy bugprone-exception-escape in sum_aggregation.cc
  [#4109](https://github.com/open-telemetry/opentelemetry-cpp/pull/4109)

* [CODE HEALTH] Move logger_sdk_test classes into anonymous namespace
  [#4124](https://github.com/open-telemetry/opentelemetry-cpp/pull/4124)

* [CODE HEALTH] Move func_http_main classes into anonymous namespace
  [#4128](https://github.com/open-telemetry/opentelemetry-cpp/pull/4128)

* [CODE HEALTH] Move func_grpc_main classes into anonymous namespace
  [#4129](https://github.com/open-telemetry/opentelemetry-cpp/pull/4129)

* [CONFIGURATION] Implementing minimum_severity and trace_based
  attributes for LoggerConfig
  [#4131](https://github.com/open-telemetry/opentelemetry-cpp/pull/4131)

* [SDK] Support TracerConfigurator updates
  [#4065](https://github.com/open-telemetry/opentelemetry-cpp/pull/4065)

* Bump codecov/codecov-action from 6.0.1 to 7.0.0
  [#4143](https://github.com/open-telemetry/opentelemetry-cpp/pull/4143)

* Bump bazel-contrib/publish-to-bcr/.github/workflows/publish.yaml
  [#4142](https://github.com/open-telemetry/opentelemetry-cpp/pull/4142)

* [CODE HEALTH] Fix clang-tidy bugprone-exception-escape warnings in API
  [#3964](https://github.com/open-telemetry/opentelemetry-cpp/pull/3964)

* [API+SDK] apply filtering EmitLogRecord
  [#4079](https://github.com/open-telemetry/opentelemetry-cpp/pull/4079)

* [CI] iwyu clang tidy install thirdparty script to unify dev container
  [#4136](https://github.com/open-telemetry/opentelemetry-cpp/pull/4136)

* [CONTEXT] Fix env carrier spec compliance
  [#4141](https://github.com/open-telemetry/opentelemetry-cpp/pull/4141)

* [SDK] Delta temporality: use per-instrument creation time for first interval start_ts
  [#4144](https://github.com/open-telemetry/opentelemetry-cpp/pull/4144)

* Bump bazel-contrib/publish-to-bcr/.github/workflows/publish.yaml
  [#4147](https://github.com/open-telemetry/opentelemetry-cpp/pull/4147)

* [EXPORTER] Add custom HTTP client
  [#4071](https://github.com/open-telemetry/opentelemetry-cpp/pull/4071)

* docs: add CNCF Slack join instructions
  [#4153](https://github.com/open-telemetry/opentelemetry-cpp/pull/4153)

* [SDK] Add ComposableSampler interface and CompositeSampler
  [#4133](https://github.com/open-telemetry/opentelemetry-cpp/pull/4133)

* Bump bazel-contrib/publish-to-bcr/.github/workflows/publish.yaml
  [#4150](https://github.com/open-telemetry/opentelemetry-cpp/pull/4150)

* [CONFIGURATION] Declarative resource detection types
  [#4148](https://github.com/open-telemetry/opentelemetry-cpp/pull/4148)

* [BUILD] Fix protobuf build failure
  [#4154](https://github.com/open-telemetry/opentelemetry-cpp/pull/4154)

* [SEMANTIC CONVENTIONS] Upgrade to semconv 1.42.0
  [#4156](https://github.com/open-telemetry/opentelemetry-cpp/pull/4156)

* Bump rules_cc from 0.2.18 to 0.2.19
  [#4120](https://github.com/open-telemetry/opentelemetry-cpp/pull/4120)

* [CMAKE] upgrade grpc and protobuf
  [#4167](https://github.com/open-telemetry/opentelemetry-cpp/pull/4167)

* Bump actions/checkout from 6.0.3 to 7.0.0
  [#4169](https://github.com/open-telemetry/opentelemetry-cpp/pull/4169)

* [BENCHMARK] add span otlp recordable benchmark
  [#4165](https://github.com/open-telemetry/opentelemetry-cpp/pull/4165)

* [METRICS] Enable synchronous gauge delta temporality
  and add tests for multi reader path
  [#4159](https://github.com/open-telemetry/opentelemetry-cpp/pull/4159)

* Add preview support for bound instruments
  [#4042](https://github.com/open-telemetry/opentelemetry-cpp/pull/4042)

* [API/SDK] add `WITH_LOG_FILTERING_PREVIEW` option
  [#4160](https://github.com/open-telemetry/opentelemetry-cpp/pull/4160)

* [SEMANTIC CONVENTIONS] semconv generate events
  [#4171](https://github.com/open-telemetry/opentelemetry-cpp/pull/4171)

* [EXPORTER] Handle OTLP partial success response
  [#4104](https://github.com/open-telemetry/openelemetry-cpp/pull/4104)

* Bump rules_cc from 0.2.19 to 0.2.20
  [#4173](https://github.com/open-telemetry/opentelemetry-cpp/pull/4173)

* [CI] pin ci workflow Bazel on MacOS runner to macos-15
  [#4176](https://github.com/open-telemetry/opentelemetry-cpp/pull/4176)

* [ADMIN] Revert log filtering preview option
  [#4174](https://github.com/open-telemetry/opentelemetry-cpp/pull/4174)

* [TEST] fix metrics simple example
  [#4175](https://github.com/open-telemetry/opentelemetry-cpp/pull/4175)

* [EXPORTER] Replace new operator with std::make_unique
  [#4181](https://github.com/open-telemetry/opentelemetry-cpp/pull/4181)

* [DOC] update ABI policy docs to clarify breaking changes
  [#4180](https://github.com/open-telemetry/opentelemetry-cpp/pull/4180)

* [CODE HEALTH] Fix clang-tidy bugprone-exception-escape in ostream exporters
  [#4137](https://github.com/open-telemetry/opentelemetry-cpp/pull/4137)

* [CMAKE] Upgrade ryml to 0.15.2
  [#4185](https://github.com/open-telemetry/opentelemetry-cpp/pull/4185)

* [CONFIGURATION] Apply default sampler when none is specified
  [#4170](https://github.com/open-telemetry/opentelemetry-cpp/pull/4170)

* [CMAKE] Upgrade curl to 8.21.0
  [#4187](https://github.com/open-telemetry/opentelemetry-cpp/pull/4187)

* Bump actions/cache from 5.0.5 to 6.0.0
  [#4177](https://github.com/open-telemetry/opentelemetry-cpp/pull/4177)

* Bump actions/cache from 6.0.0 to 6.1.0
  [#4189](https://github.com/open-telemetry/opentelemetry-cpp/pull/4189)

* [API] fix the trace state regex to comply with the w3c trace context level 2
  [#4194](https://github.com/open-telemetry/opentelemetry-cpp/pull/4194)

* [CODE HEALTH] Move trace and baggage propagation test classes into anonymous namespace
  [#4199](https://github.com/open-telemetry/opentelemetry-cpp/pull/4199)

* [CMAKE] Add pkg-config support for OpenTelemetry proto installation
  [#4192](https://github.com/open-telemetry/opentelemetry-cpp/pull/4192)

* [CMAKE] Fix CMake WITH_API_ONLY option
  [#4201](https://github.com/open-telemetry/opentelemetry-cpp/pull/4201)

* [CODE HEALTH] Move context propagation test classes into anonymous namespace
  [#4200](https://github.com/open-telemetry/opentelemetry-cpp/pull/4200)

* [CODE HEALTH] Fix clang-tidy bugprone-unused-local-non-trivial-variable warnings
  [#4202](https://github.com/open-telemetry/opentelemetry-cpp/pull/4202)

* [SDK] LogRecord attribute limits enforcement
  [#4157](https://github.com/open-telemetry/opentelemetry-cpp/pull/4157)

* [SDK] Rename ComposableTraceIdRatioBasedSampler to ComposableProbabilitySampler
  [#4210](https://github.com/open-telemetry/opentelemetry-cpp/pull/4210)

* Bump docker/build-push-action from 7.2.0 to 7.3.0
  [#4204](https://github.com/open-telemetry/opentelemetry-cpp/pull/4204)

* Bump docker/setup-qemu-action from 4.1.0 to 4.2.0
  [#4205](https://github.com/open-telemetry/opentelemetry-cpp/pull/4205)

* Bump docker/setup-buildx-action from 4.1.0 to 4.2.0
  [#4207](https://github.com/open-telemetry/opentelemetry-cpp/pull/4207)

* Bump rules_cc from 0.2.20 to 0.2.21
  [#4214](https://github.com/open-telemetry/opentelemetry-cpp/pull/4214)

* Bump github/codeql-action/upload-sarif from 4.36.2 to 4.36.3
  [#4213](https://github.com/open-telemetry/opentelemetry-cpp/pull/4213)

* Bump fossas/fossa-action from 1.9.0 to 2.0.0
  [#4212](https://github.com/open-telemetry/opentelemetry-cpp/pull/4212)

* Bump github/codeql-action/init from 4.36.2 to 4.36.3
  [#4209](https://github.com/open-telemetry/opentelemetry-cpp/pull/4209)

* [CODE HEALTH] Move api/test and sdk/test classes into anonymous namespace
  [#4217](https://github.com/open-telemetry/opentelemetry-cpp/pull/4217)

* [CODE HEALTH] Fix clang-tidy bugprone-unchecked-string-to-number-conversion warnings
  [#4216](https://github.com/open-telemetry/opentelemetry-cpp/pull/4216)

* [CODE HEALTH] Fix clang-tidy misc-override-with-different-visibility warnings
  [#4215](https://github.com/open-telemetry/opentelemetry-cpp/pull/4215)

* [BUILD] add missing link dependency for bazel
  [#4220](https://github.com/open-telemetry/opentelemetry-cpp/pull/4220)

* [CODE HEALTH] Fix clang-tidy `bugprone-throwing-static-initialization` warnings
  [#4206](https://github.com/open-telemetry/opentelemetry-cpp/pull/4206)

* Bump rules_cc from 0.2.21 to 0.2.22
  [#4223](https://github.com/open-telemetry/opentelemetry-cpp/pull/4223)

* Bump step-security/harden-runner from 2.19.4 to 2.20.0
  [#4222](https://github.com/open-telemetry/opentelemetry-cpp/pull/4222)

* Bump bazel-contrib/publish-to-bcr/.github/workflows/publish.yaml
  [#4221](https://github.com/open-telemetry/opentelemetry-cpp/pull/4221)

* [CODE HEALTH] Move ext and exporters test classes into anonymous namespace
  [#4225](https://github.com/open-telemetry/opentelemetry-cpp/pull/4225)

* Bump github/codeql-action/upload-sarif from 4.36.3 to 4.37.0
  [#4227](https://github.com/open-telemetry/opentelemetry-cpp/pull/4227)

* Bump github/codeql-action/init from 4.36.3 to 4.37.0
  [#4229](https://github.com/open-telemetry/opentelemetry-cpp/pull/4229)

* [SEMANTIC CONVENTIONS] Upgrade to semconv 1.43.0
  [#4230](https://github.com/open-telemetry/opentelemetry-cpp/pull/4230)

* [CODE HEALTH] Move otlp, zipkin and prometheus test classes into anonymous namespace
  [#4231](https://github.com/open-telemetry/opentelemetry-cpp/pull/4231)

* [BAZEL] Upgrade to rapidyaml 1.15.2
  [#4234](https://github.com/open-telemetry/opentelemetry-cpp/pull/4234)

* [BENCHMARK] add SpanData recordable benchmark and unify common utils
  [#4203](https://github.com/open-telemetry/opentelemetry-cpp/pull/4203)

* [CI] Add `modernize-` and `cppcoreguidelines-pro-` checks to clang-tidy (#4238)
  [#4238](https://github.com/open-telemetry/opentelemetry-cpp/pull/4238)

* [SDK] Apply metric cardinality limits to non-overflow attribute sets and
  reserve the overflow point separately.
  [#4236](https://github.com/open-telemetry/opentelemetry-cpp/pull/4236)

* [CONFIGURATION] fix yaml configuration of the logger configurator
  [#4241](https://github.com/open-telemetry/opentelemetry-cpp/pull/4241)

* [SDK] Support LoggerConfigurator updates and example
  [#4224](https://github.com/open-telemetry/opentelemetry-cpp/pull/4224)

* Bump actions/stale from 10.3.0 to 10.4.0
  [#4242](https://github.com/open-telemetry/opentelemetry-cpp/pull/4242)

* [CMAKE] Remove environment variable support from the add external components script
  [#4211](https://github.com/open-telemetry/opentelemetry-cpp/pull/4211)

* [ETW] Make spans own their names
  [#4247](https://github.com/open-telemetry/opentelemetry-cpp/pull/4247)

* [SDK] Span limits configuration
  [#4232](https://github.com/open-telemetry/opentelemetry-cpp/pull/4232)

* [CONFIGURATION] Define and set spec default values in configuration objects
  [#4244](https://github.com/open-telemetry/opentelemetry-cpp/pull/4244)

* [CMAKE] Upgrade to protobuf 35.1 and grpc 1.82.1
  [#4255](https://github.com/open-telemetry/opentelemetry-cpp/pull/4255)

* [RELEASE] Release opentelemetry-cpp 1.28.0
  [#4233](https://github.com/open-telemetry/opentelemetry-cpp/pull/4233)

Important changes:

* [API] fix the trace state regex to comply with the w3c trace context level 2 (#4194)
  [#4194](https://github.com/open-telemetry/opentelemetry-cpp/pull/4194)

  * [API] Fix `TraceState::IsValidKey()` to comply with the W3C Trace Context
    Level 2, where keys containing `@` and keys with more than 241 characters
    before `@` or more than 14 characters after `@` are now accepted.
    Only the total 256-character key length limit is enforced.
    **Note**: this is a correctness fix to an inline API header; the observable
    behavior of `IsValidKey`, `IsValidKeyRegEx`, and `IsValidKeyNonRegEx`
    changes (see `docs/abi-policy.md`).

* [API+SDK] apply filtering EmitLogRecord (#4079)
  [#4079](https://github.com/open-telemetry/opentelemetry-cpp/pull/4079)

  * [API] (ABI v2) `Logger::EmitLogRecord(...)` templates now apply the
    `Enabled` filter chain when a `Severity` is in args. v1 behavior is
    unchanged.

  * [API/SDK] (ABI v2) Add `Logger::CreateLogRecord` virtual taking
    `const nostd::variant<trace::SpanContext, context::Context> &`
    for explicit-context record creation. `Logger::EmitLogRecord(args...)`
    also detects a `Context`, `SpanContext`
    or `TraceId` + `SpanId` [+ `TraceFlags`] in args and routes filtering.

  * [SDK] Add `LogRecordProcessor::HasEnabledFilter()` so the SDK Logger can
    include processor-level filtering in its extended-enabled cache. Defaults
    to `true`. Built-in `SimpleLogRecordProcessor` and
    `BatchLogRecordProcessor` override to `false` since they use the default
    Enabled.

  * [API/SDK] Replace `Context`-only signatures on
    `LogRecordProcessor::Enabled`,
    `LogRecordProcessor::EnabledImplementation`,
    `Logger::EnabledImplementation` (v2), and `Logger::CreateLogRecord` (v2)
    with `nostd::variant<trace::SpanContext, context::Context>`.

## [1.27.0] 2026-05-13

* [RELEASE] Bump main branch to 1.27.0-dev
  [#3947](https://github.com/open-telemetry/opentelemetry-cpp/pull/3947)

* [CODE HEALTH] Fix clang tidy warnings in API `common` and `context`
  [#3948](https://github.com/open-telemetry/opentelemetry-cpp/pull/3948)

* Bump github/codeql-action from 4.33.0 to 4.34.1
  [#3949](https://github.com/open-telemetry/opentelemetry-cpp/pull/3949)

* [CODE HEALTH] Fix more clang-tidy warnings in sdk
  [#3951](https://github.com/open-telemetry/opentelemetry-cpp/pull/3951)

* [CODE HEALTH] Fix more clang-tidy warnings in api
  [#3950](https://github.com/open-telemetry/opentelemetry-cpp/pull/3950)

* [CODE HEALTH] Fix clang-tidy warnings in ext, exporters, and examples
  [#3952](https://github.com/open-telemetry/opentelemetry-cpp/pull/3952)

* Bump grpc from 1.78.0 to 1.80.0
  [#3955](https://github.com/open-telemetry/opentelemetry-cpp/pull/3955)

* Bump codecov/codecov-action from 5.5.3 to 6.0.0
  [#3954](https://github.com/open-telemetry/opentelemetry-cpp/pull/3954)

* Bump github/codeql-action from 4.34.1 to 4.35.1
  [#3957](https://github.com/open-telemetry/opentelemetry-cpp/pull/3957)

* Bump step-security/harden-runner from 2.16.0 to 2.16.1
  [#3960](https://github.com/open-telemetry/opentelemetry-cpp/pull/3960)

* Bump fossas/fossa-action from 1.8.0 to 1.9.0
  [#3961](https://github.com/open-telemetry/opentelemetry-cpp/pull/3961)

* Move implementation from SDK header files to SDK cc
  [#3887](https://github.com/open-telemetry/opentelemetry-cpp/pull/3887)

* Bump bazel-contrib/publish-to-bcr/.github/workflows/publish.yaml
  [#3962](https://github.com/open-telemetry/opentelemetry-cpp/pull/3962)

* Bump benchmark-action/github-action-benchmark from 1.21.0 to 1.22.0
  [#3963](https://github.com/open-telemetry/opentelemetry-cpp/pull/3963)

* [CODE HEALTH] Fix clang-tidy param-not-moved in otlp grpc exporters
  [#3956](https://github.com/open-telemetry/opentelemetry-cpp/pull/3956)

* Enable WITH_OTLP_RETRY_PREVIEW by default
  [#3953](https://github.com/open-telemetry/opentelemetry-cpp/pull/3953)

* Add Doug Barker as maintainer
  [#3969](https://github.com/open-telemetry/opentelemetry-cpp/pull/3969)

* [CODE HEALTH] Cleanup nostd variant access for noexcept methods in api and sdk
  [#3965](https://github.com/open-telemetry/opentelemetry-cpp/pull/3965)

* chore: update readme
  [#3971](https://github.com/open-telemetry/opentelemetry-cpp/pull/3971)

* Adjust emeritus status
  [#3972](https://github.com/open-telemetry/opentelemetry-cpp/pull/3972)

* Enable WITH_OTLP_GRPC_SSL_MTLS_PREVIEW by default
  [#3970](https://github.com/open-telemetry/opentelemetry-cpp/pull/3970)

* Bump step-security/harden-runner from 2.16.1 to 2.17.0
  [#3976](https://github.com/open-telemetry/opentelemetry-cpp/pull/3976)

* Bump actions/github-script from 8.0.0 to 9.0.0
  [#3975](https://github.com/open-telemetry/opentelemetry-cpp/pull/3975)

* [SDK] Reduce lock contention in SyncMetricStorage for concurrent metric recording
  [#3959](https://github.com/open-telemetry/opentelemetry-cpp/pull/3959)

* [Bazel] Add ENABLE_OTLP_GRPC_SSL_MTLS_PREVIEW define to otlp_grpc_log_record_exporter
  [#3988](https://github.com/open-telemetry/opentelemetry-cpp/pull/3988)

* [TEST] Only run the multi_observer_test when building with ABI v2
  [#3982](https://github.com/open-telemetry/opentelemetry-cpp/pull/3982)

* [CODE HEALTH] Fix gcc warnings in release maintainer build
  [#3984](https://github.com/open-telemetry/opentelemetry-cpp/pull/3984)

* Bump docker/build-push-action from 7.0.0 to 7.1.0
  [#3992](https://github.com/open-telemetry/opentelemetry-cpp/pull/3992)

* [CODE HEALTH] Fix clang-tidy misc-use-internal-linkage warnings
  [#3985](https://github.com/open-telemetry/opentelemetry-cpp/pull/3985)

* [CODE HEALTH] Fix clang-tidy narrowing conversions in baggage
  [#3989](https://github.com/open-telemetry/opentelemetry-cpp/pull/3989)

* [CODE HEALTH] Fix clang-tidy narrowing-conversions warnings in tests
  [#3987](https://github.com/open-telemetry/opentelemetry-cpp/pull/3987)

* Bump actions/cache from 5.0.4 to 5.0.5
  [#3994](https://github.com/open-telemetry/opentelemetry-cpp/pull/3994)

* [CODE HEALTH] Fix misc clang-tidy warnings
  [#3993](https://github.com/open-telemetry/opentelemetry-cpp/pull/3993)

* [TEST] Replace routable IP with RFC 5737 TEST-NET address in curl test
  [#4001](https://github.com/open-telemetry/opentelemetry-cpp/pull/4001)

* [TEST] set a maximum thread count for the metrics stress test
  [#4003](https://github.com/open-telemetry/opentelemetry-cpp/pull/4003)

* Bump github/codeql-action from 4.35.1 to 4.35.2
  [#4005](https://github.com/open-telemetry/opentelemetry-cpp/pull/4005)

* Bump step-security/harden-runner from 2.17.0 to 2.18.0
  [#4004](https://github.com/open-telemetry/opentelemetry-cpp/pull/4004)

* [CODE HEALTH] Fix clang-tidy warnings in base2 exponential histogram aggregation
  [#3997](https://github.com/open-telemetry/opentelemetry-cpp/pull/3997)

* [CI] Build third-party dependencies in release with ninja
  [#3995](https://github.com/open-telemetry/opentelemetry-cpp/pull/3995)

* [CI] Update ci scripts and documentation
  [#4000](https://github.com/open-telemetry/opentelemetry-cpp/pull/4000)

* [SDK] Rename parameters in logger_provider
  [#4006](https://github.com/open-telemetry/opentelemetry-cpp/pull/4006)

* Enable ENABLE_OTLP_RETRY_PREVIEW for bazel
  [#4010](https://github.com/open-telemetry/opentelemetry-cpp/pull/4010)

* [CODE HEALTH] Fix clang-tidy misc-no-recursion warnings
  [#4009](https://github.com/open-telemetry/opentelemetry-cpp/pull/4009)

* upgrade bazelisk
  [#4015](https://github.com/open-telemetry/opentelemetry-cpp/pull/4015)

* [SDK] Add destructor to PeriodicExportingMetricReader to fix shutdown race
  [#4008](https://github.com/open-telemetry/opentelemetry-cpp/pull/4008)

* [CODE HEALTH] Fix clang-tidy narrowing-conversions in sync_instruments
  [#4013](https://github.com/open-telemetry/opentelemetry-cpp/pull/4013)

* [CI] Enable Bazel asan and ubsan jobs
  [#4014](https://github.com/open-telemetry/opentelemetry-cpp/pull/4014)

* [CI] update code.coverage job to report on all components and preview features
  [#4002](https://github.com/open-telemetry/opentelemetry-cpp/pull/4002)

* [OTLP GRPC] Allow user use custom grpc::ChannelArguments
  [#3990](https://github.com/open-telemetry/opentelemetry-cpp/pull/3990)

* Bump step-security/harden-runner from 2.18.0 to 2.19.0
  [#4016](https://github.com/open-telemetry/opentelemetry-cpp/pull/4016)

* Bump rules_cc from 0.2.17 to 0.2.18
  [#4022](https://github.com/open-telemetry/opentelemetry-cpp/pull/4022)

* upgrade opentelemetry-proto to 1.10.0
  [#4025](https://github.com/open-telemetry/opentelemetry-cpp/pull/4025)

* [CI] upgrade grpc to 1.80.0 for CMake
  [#4026](https://github.com/open-telemetry/opentelemetry-cpp/pull/4026)

* [CONFIGURATION] File configuration - composable samplers
  [#3966](https://github.com/open-telemetry/opentelemetry-cpp/pull/3966)

* [CHORE] Fix warnings in publish-to-bcr workflow
  [#4032](https://github.com/open-telemetry/opentelemetry-cpp/pull/4032)

* [API] Deprecate opentelemetry::plugin
  [#4021](https://github.com/open-telemetry/opentelemetry-cpp/pull/4021)

* [CI] remove old protobuf vcpkg port files
  [#4029](https://github.com/open-telemetry/opentelemetry-cpp/pull/4029)

* [ADMIN] remove .vscode from the repo
  [#4030](https://github.com/open-telemetry/opentelemetry-cpp/pull/4030)

* [SDK] env var durations non conforming to spec
  [#4020](https://github.com/open-telemetry/opentelemetry-cpp/pull/4020)

* Bump curl from 8.12.0 to 8.12.0.bcr.1
  [#4036](https://github.com/open-telemetry/opentelemetry-cpp/pull/4036)

* fix examples/configuration
  [#4041](https://github.com/open-telemetry/opentelemetry-cpp/pull/4041)

* fix: noexcept for Observer
  [#4039](https://github.com/open-telemetry/opentelemetry-cpp/pull/4039)

* Bump platforms from 1.0.0 to 1.1.0
  [#4050](https://github.com/open-telemetry/opentelemetry-cpp/pull/4050)

* Bump github/codeql-action from 4.35.2 to 4.35.3
  [#4048](https://github.com/open-telemetry/opentelemetry-cpp/pull/4048)

* Bump step-security/harden-runner from 2.19.0 to 2.19.1
  [#4049](https://github.com/open-telemetry/opentelemetry-cpp/pull/4049)

* [DOC] Document adding attributes on span creation
  [#4053](https://github.com/open-telemetry/opentelemetry-cpp/pull/4053)

* [SDK] Enable W3C Trace Context v2
  [#4012](https://github.com/open-telemetry/opentelemetry-cpp/pull/4012)

* Bump step-security/harden-runner from 2.18.0 to 2.19.1
  [#4056](https://github.com/open-telemetry/opentelemetry-cpp/pull/4056)

* [CODE HEALTH] fix nondeterministic pointer iteration order warnings
  [#4035](https://github.com/open-telemetry/opentelemetry-cpp/pull/4035)

* Bump benchmark-action/github-action-benchmark from 1.22.0 to 1.22.1
  [#4059](https://github.com/open-telemetry/opentelemetry-cpp/pull/4059)

* [EXPORTER] Fix PrometheusExporter to propagate new options to collector
  [#4055](https://github.com/open-telemetry/opentelemetry-cpp/pull/4055)

* [SDK] Fix overflow attribute name to match the spec (otel.metric.overflow)
  [#4060](https://github.com/open-telemetry/opentelemetry-cpp/pull/4060)

* [EXPORTER] Implement non-utf8 string to bytes in OTLP
  [#3991](https://github.com/open-telemetry/opentelemetry-cpp/pull/3991)

* [Docs] Fix Read the Docs configuration
  [#4058](https://github.com/open-telemetry/opentelemetry-cpp/pull/4058)

* [API] Fix `Logger.Enabled()`
  [#4011](https://github.com/open-telemetry/opentelemetry-cpp/pull/4011)

* [CI] Use fixed versions in iwyu CI & dev container
  [#4051](https://github.com/open-telemetry/opentelemetry-cpp/pull/4051)

* docs: Expand SIG meeting welcoming language
  [#4064)](https://github.com/open-telemetry/opentelemetry-cpp/pull/4064)

* [CI] update clang-tidy config
  [#4066](https://github.com/open-telemetry/opentelemetry-cpp/pull/4066)

* [TEST] Assert overflow attribute value is boolean true
  [#4063](https://github.com/open-telemetry/opentelemetry-cpp/pull/4063)

* Bump github/codeql-action from 4.35.3 to 4.35.4
  [#4067](https://github.com/open-telemetry/opentelemetry-cpp/pull/4067)

* chore: Move inactive members to emeritus
  [#4040](https://github.com/open-telemetry/opentelemetry-cpp/pull/4040)

* [EXAMPLE] Add manual asynchronous context propagation example
  [#3935](https://github.com/open-telemetry/opentelemetry-cpp/pull/3935)

* [SEMANTIC CONVENTIONS] Upgrade to semantic conventions 1.41.1
  [#4075](https://github.com/open-telemetry/opentelemetry-cpp/pull/4075)

* [BUILD] Upgrade to rapidyaml 0.12.1
  [#4076](https://github.com/open-telemetry/opentelemetry-cpp/pull/4076)

* [EXPORTER] OTLP HTTP exporters read unbounded HTTP response
  [#4078](https://github.com/open-telemetry/opentelemetry-cpp/pull/4078)

* [SDK] Fix Base2ExponentialHistogramAggregation count dropped
  [#4072](https://github.com/open-telemetry/opentelemetry-cpp/pull/4072)

* [RELEASE] Release opentelemetry-cpp 1.27.0
  [#4080](https://github.com/open-telemetry/opentelemetry-cpp/pull/4080)

Security fix:

* [EXPORTER] OTLP HTTP exporters read unbounded HTTP response
  [#4078](https://github.com/open-telemetry/opentelemetry-cpp/pull/4078)

  * When exporting OTLP HTTP data to a misconfigured or malicious endpoint,
    the exporter could allocate an arbitrary amount of memory when getting
    the endpoint HTTP response back.
  * The size of HTTP responses is now limited to 4MiB by default,
    following the opentelemetry-proto recommendations.
  * See CVE-2026-44967

Important changes:

* [SDK] Move inline implementation from SDK headers to .cc files.
  [#3887](https://github.com/open-telemetry/opentelemetry-cpp/pull/3887)

  * Note: `GetEmptyAttributes()` now requires linking `opentelemetry_common`.

* Enable WITH_OTLP_RETRY_PREVIEW by default
  [#3953](https://github.com/open-telemetry/opentelemetry-cpp/pull/3953)

  * CMake flag WITH_OTLP_RETRY_PREVIEW is now enabled by default.
  * This flag is deprecated, planned for removal.

* Enable WITH_OTLP_GRPC_SSL_MTLS_PREVIEW by default
  [#3970](https://github.com/open-telemetry/opentelemetry-cpp/pull/3970)

  * CMake flag WITH_OTLP_GRPC_SSL_MTLS_PREVIEW is now enabled by default.
  * This flag is deprecated, planned for removal.
  * Bazel now always builds with ENABLE_OTLP_GRPC_SSL_MTLS_PREVIEW.
  * grpc properties for ssl KEY and CERT are always available,
    adjust the application code to initialize all members in grpc options.

* [EXPORTER] implement non-utf8 string to bytes in OTLP exporters
  [#3991](https://github.com/open-telemetry/opentelemetry-cpp/pull/3991)

  * Add WITH_OTLP_UTF8_VALIDITY for CMake
    and enable ENABLE_OTLP_UTF8_VALIDITY for Bazel
    to export non-UTF-8 strings as bytes in OTLP.

* Enable ENABLE_OTLP_RETRY_PREVIEW for bazel
  [#4010](https://github.com/open-telemetry/opentelemetry-cpp/pull/4010)

  * Bazel now always builds with ENABLE_OTLP_RETRY_PREVIEW.

* [API] Deprecate opentelemetry::plugin
  [#4021](https://github.com/open-telemetry/opentelemetry-cpp/pull/4021)

  * namespace opentelemetry::plugin is deprecated
  * See file DEPRECATED.md for details.

* [SDK] Fix cardinality-limit overflow attribute name to match the
  specification
  [#4060](https://github.com/open-telemetry/opentelemetry-cpp/pull/4060)

  * The synthetic overflow data point attribute is now exported as
    `otel.metric.overflow` (singular) per the
    [Metrics SDK specification](https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/metrics/sdk.md#cardinality-limits).
  * Previously the SDK exported `otel.metrics.overflow` (plural), which
    diverged from the spec and from every other language SDK
    (Go, Java, JS, .NET).
  * Downstream consumers (dashboards, alerts, queries) that filtered on
    the old name must be updated to the spec-correct name.

Breaking changes:

* [SDK] env var durations non conforming to spec
  [#4020](https://github.com/open-telemetry/opentelemetry-cpp/pull/4020)

  * Environment variables for durations can accept two formats, with units
    (`15s`, `15000ms`) or without units (`15000`).
  * When parsing environment variables that represent durations,
    and only in the case no unit is provided,
    for example `OTEL_METRIC_EXPORT_INTERVAL=15000`,
    the code interpreted the value, `15000`,
    in seconds instead of milliseconds per the specifications.
  * Parsing duration without units has been fixed to use milliseconds.
  * As a consequence, every duration environment variable set
    without a unit is now interpreted differently, which is a breaking change.
  * For example, `OTEL_EXPORTER_OTLP_TRACES_TIMEOUT=30` meant 30 seconds
    before, and now means 30 milliseconds.
    To preserve the same behavior, existing values must be adjusted,
    to either `30000` or `30000ms` or `30s` in this case.
  * The following variables are affected:
    * OTEL_EXPORTER_OTLP_TRACES_TIMEOUT
    * OTEL_EXPORTER_OTLP_METRICS_TIMEOUT
    * OTEL_EXPORTER_OTLP_LOGS_TIMEOUT
    * OTEL_EXPORTER_OTLP_TIMEOUT
    * OTEL_BLRP_SCHEDULE_DELAY
    * OTEL_BLRP_EXPORT_TIMEOUT
    * OTEL_METRIC_EXPORT_INTERVAL
    * OTEL_METRIC_EXPORT_TIMEOUT
    * OTEL_BSP_SCHEDULE_DELAY
    * OTEL_BSP_EXPORT_TIMEOUT

## [1.26.0] 2026-03-19

* [RELEASE] Bump main branch to 1.26.0-dev
  [#3853](https://github.com/open-telemetry/opentelemetry-cpp/pull/3853)

* [BUILD] Configure ryml to use exceptions
  [#3852](https://github.com/open-telemetry/opentelemetry-cpp/pull/3852)

* Bump step-security/harden-runner from 2.14.1 to 2.14.2
  [#3854](https://github.com/open-telemetry/opentelemetry-cpp/pull/3854)

* [CONFIGURATION] File configuration - handle invalid YAML
  [#3858](https://github.com/open-telemetry/opentelemetry-cpp/pull/3858)

* Bump docker/build-push-action from 6.18.0 to 6.19.1
  [#3860](https://github.com/open-telemetry/opentelemetry-cpp/pull/3860)

* [BUILD] Avoid break caused by max() macro on windows
  [#3863](https://github.com/open-telemetry/opentelemetry-cpp/pull/3863)

* Bump docker/build-push-action from 6.19.1 to 6.19.2
  [#3864](https://github.com/open-telemetry/opentelemetry-cpp/pull/3864)

* Bump github/codeql-action from 4.32.2 to 4.32.3
  [#3868](https://github.com/open-telemetry/opentelemetry-cpp/pull/3868)

* Bump actions/stale from 10.1.1 to 10.2.0
  [#3871](https://github.com/open-telemetry/opentelemetry-cpp/pull/3871)

* [CI] Fix publish-to-bcr workflow
  [#3869](https://github.com/open-telemetry/opentelemetry-cpp/pull/3869)

* [CONFIGURATION] Provide bazel updates with dependabot
  [#3870](https://github.com/open-telemetry/opentelemetry-cpp/pull/3870)

* Bump prometheus-cpp from 1.3.0 to 1.3.0.bcr.2
  [#3873](https://github.com/open-telemetry/opentelemetry-cpp/pull/3873)

* Bump bazel_skylib from 1.7.1 to 1.9.0
  [#3874](https://github.com/open-telemetry/opentelemetry-cpp/pull/3874)

* Bump rules_cc from 0.2.9 to 0.2.17
  [#3875](https://github.com/open-telemetry/opentelemetry-cpp/pull/3875)

* Bump zlib from 1.3.1.bcr.5 to 1.3.2
  [#3880](https://github.com/open-telemetry/opentelemetry-cpp/pull/3880)

* Bump curl from 8.8.0 to 8.11.0.bcr.4
  [#3879](https://github.com/open-telemetry/opentelemetry-cpp/pull/3879)

* [CI] Add Doxygen validation workflow
  [#3882](https://github.com/open-telemetry/opentelemetry-cpp/pull/3882)

* [SEMANTIC CONVENTIONS] Upgrade to semantic conventions 1.40.0
  [#3881](https://github.com/open-telemetry/opentelemetry-cpp/pull/3881)

* Bump github/codeql-action from 4.32.3 to 4.32.4
  [#3884](https://github.com/open-telemetry/opentelemetry-cpp/pull/3884)

* [API] Environment Variables as Context Propagation Carriers
  [#3834](https://github.com/open-telemetry/opentelemetry-cpp/pull/3834)

* [SDK] Trace StartSpan - do not call GetCurrentSpan() unless needed
  [#3888](https://github.com/open-telemetry/opentelemetry-cpp/pull/3888)

* Bump step-security/harden-runner from 2.14.2 to 2.15.0
  [#3889](https://github.com/open-telemetry/opentelemetry-cpp/pull/3889)

* Bump google_benchmark from 1.8.4 to 1.9.5
  [#3885](https://github.com/open-telemetry/opentelemetry-cpp/pull/3885)

* Bump actions/download-artifact from 7.0.0 to 8.0.0
  [#3892](https://github.com/open-telemetry/opentelemetry-cpp/pull/3892)

* [BUILD] Revisit EventLogger deprecation
  [#3855](https://github.com/open-telemetry/opentelemetry-cpp/pull/3855)

* Bump platforms from 0.0.11 to 1.0.0
  [#3893](https://github.com/open-telemetry/opentelemetry-cpp/pull/3893)

* Bump benchmark-action/github-action-benchmark from 1.20.7 to 1.21.0
  [#3896](https://github.com/open-telemetry/opentelemetry-cpp/pull/3896)

* Bump github/codeql-action from 4.32.4 to 4.32.5
  [#3897](https://github.com/open-telemetry/opentelemetry-cpp/pull/3897)

* [TEST] Add multi-threaded metrics benchmarks: shared vs per-thread counter
  [#3898](https://github.com/open-telemetry/opentelemetry-cpp/pull/3898)

* [BUILD] Upgrade bazel dependencies
  [#3891](https://github.com/open-telemetry/opentelemetry-cpp/pull/3891)

* [CODE HEALTH] Fix clang-tidy warnings, part 2
  [#3872](https://github.com/open-telemetry/opentelemetry-cpp/pull/3872)

* [CONFIGURATION] File configuration - spec stability
  [#3862](https://github.com/open-telemetry/opentelemetry-cpp/pull/3862)

* [CI] replace cfssl with openssl for certificate generation
  [#3902](https://github.com/open-telemetry/opentelemetry-cpp/pull/3902)

* Bump abseil-cpp from 20240116.1 to 20260107.1
  [#3877](https://github.com/open-telemetry/opentelemetry-cpp/pull/3877)

* Bump googletest from 1.14.0.bcr.1 to 1.17.0.bcr.2
  [#3876](https://github.com/open-telemetry/opentelemetry-cpp/pull/3876)

* Bump curl from 8.11.0.bcr.4 to 8.12.0
  [#3904](https://github.com/open-telemetry/opentelemetry-cpp/pull/3904)

* [BUILD] Upgrade to rapidyaml 0.10.0
  [#3906](https://github.com/open-telemetry/opentelemetry-cpp/pull/3906)

* Bump github/codeql-action from 4.32.5 to 4.32.6
  [#3910](https://github.com/open-telemetry/opentelemetry-cpp/pull/3910)

* Bump nlohmann_json from 3.12.0 to 3.12.0.bcr.1
  [#3912](https://github.com/open-telemetry/opentelemetry-cpp/pull/3912)

* Bump docker/setup-qemu-action from 3.7.0 to 4.0.0
  [#3903](https://github.com/open-telemetry/opentelemetry-cpp/pull/3903)

* Bump docker/setup-buildx-action from 3.12.0 to 4.0.0
  [#3907](https://github.com/open-telemetry/opentelemetry-cpp/pull/3907)

* [EXAMPLES] Deprecated semantic conventions used
  [#3905](https://github.com/open-telemetry/opentelemetry-cpp/pull/3905)

* Bump docker/build-push-action from 6.19.2 to 7.0.0
  [#3909](https://github.com/open-telemetry/opentelemetry-cpp/pull/3909)

* [CODE HEALTH] Fix clang-tidy warnings: cppcoreguidelines-init-variables
  [#3919](https://github.com/open-telemetry/opentelemetry-cpp/pull/3919)

* Bump step-security/harden-runner from 2.15.0 to 2.15.1
  [#3908](https://github.com/open-telemetry/opentelemetry-cpp/pull/3908)

* [CODE HEALTH] Fix clang-tidy misc-use-internal-linkage warnings
  [#3918](https://github.com/open-telemetry/opentelemetry-cpp/pull/3918)

* [CODE HEALTH] Fix clang-tidy macro to enum warnings
  [#3922](https://github.com/open-telemetry/opentelemetry-cpp/pull/3922)

* [BUILD] Fix benchmark genrule capturing stderr into JSON output
  [#3925](https://github.com/open-telemetry/opentelemetry-cpp/pull/3925)

* [CODE HEALTH] Fix clang-tidy performance enum size warnings
  [#3923](https://github.com/open-telemetry/opentelemetry-cpp/pull/3923)

* Bump actions/download-artifact from 8.0.0 to 8.0.1
  [#3928](https://github.com/open-telemetry/opentelemetry-cpp/pull/3928)

* [CODE HEALTH] Fix clang-tidy warnings in nostd files
  [#3924](https://github.com/open-telemetry/opentelemetry-cpp/pull/3924)

* [EXPORTER] Allow custom HttpClient in OTLP HTTP
  [#3930](https://github.com/open-telemetry/opentelemetry-cpp/pull/3930)

* [CODE HEALTH] Fix clang-tidy performance-for-range-copy warnings
  [#3932](https://github.com/open-telemetry/opentelemetry-cpp/pull/3932)

* [CODE HEALTH] Fix clang-tidy performance-unnecessary-value-param warnings
  [#3931](https://github.com/open-telemetry/opentelemetry-cpp/pull/3931)

* [CODE HEALTH] Fix clang-tidy bugprone warnings
  [#3933](https://github.com/open-telemetry/opentelemetry-cpp/pull/3933)

* [CODE HEALTH] Fix clang-tidy special-member-functions warnings in trace API
  [#3934](https://github.com/open-telemetry/opentelemetry-cpp/pull/3934)

* Bump github/codeql-action from 4.32.6 to 4.33.0
  [#3936](https://github.com/open-telemetry/opentelemetry-cpp/pull/3936)

* Bump step-security/harden-runner from 2.15.1 to 2.16.0
  [#3937](https://github.com/open-telemetry/opentelemetry-cpp/pull/3937)

* [TEST] CMake component install test for `resource_detectors`
  [#3940](https://github.com/open-telemetry/opentelemetry-cpp/pull/3940)

* [CODE HEALTH] Fix clang-tidy performance warnings
  [#3941](https://github.com/open-telemetry/opentelemetry-cpp/pull/3941)

* [EXPORTERS] Remove prometheus timestamps
  [#3895](https://github.com/open-telemetry/opentelemetry-cpp/pull/3895)

* [SDK] Swap Tracer/Meter/LoggerConfig disabled for enabled
  [#3942](https://github.com/open-telemetry/opentelemetry-cpp/pull/3942)

* [CMAKE] Update third party dependencies for cmake builds
  [#3943](https://github.com/open-telemetry/opentelemetry-cpp/pull/3943)

* Bump codecov/codecov-action from 5.5.2 to 5.5.3
  [#3945](https://github.com/open-telemetry/opentelemetry-cpp/pull/3945)

* Bump actions/cache from 5.0.3 to 5.0.4
  [#3946](https://github.com/open-telemetry/opentelemetry-cpp/pull/3946)

* [CONFIGURATION] File configuration - Tracer/Meter/Logger config
  [#3920](https://github.com/open-telemetry/opentelemetry-cpp/pull/3920)

Important changes:

* [SDK] Swap Tracer/Meter/LoggerConfig disabled for enabled
  [#3942](https://github.com/open-telemetry/opentelemetry-cpp/pull/3942)

  * In TracerConfig(bool), MeterConfig(bool), and LoggerConfig(bool),
    the boolean parameter now represents `enabled` instead of `disabled`.
  * User code calling these constructors explicitly must be adjusted
    to pass `false` to disable.

* [BUILD] Revisit EventLogger deprecation
  [#3855](https://github.com/open-telemetry/opentelemetry-cpp/pull/3855)

  * File `DEPRECATED.md` contains information about current deprecation
    plans.
  * Please adjust your application accordingly, to avoid disruption.

* [CONFIGURATION] File configuration - spec stability
  [#3862](https://github.com/open-telemetry/opentelemetry-cpp/pull/3862)

  * The specification for declarative configuration is now stable.
  * As a result, environment variable `OTEL_EXPERIMENTAL_CONFIG_FILE`
    is now renamed to `OTEL_CONFIG_FILE`.

## [1.25.0] 2026-02-07

* [RELEASE] Bump main branch to 1.25.0-dev (#3759)
  [#3759](https://github.com/open-telemetry/opentelemetry-cpp/pull/3759)

* [API] fix sign-conversion warnings in span & trace id
  [#3761](https://github.com/open-telemetry/opentelemetry-cpp/pull/3761)

* [TEST] Fix ZipkinExporterTestPeer.ExportJsonIntegrationTest stability
  [#3760](https://github.com/open-telemetry/opentelemetry-cpp/pull/3760)

* Bump github/codeql-action from 4.31.4 to 4.31.5
  [#3764](https://github.com/open-telemetry/opentelemetry-cpp/pull/3764)

* [CI] Update markdownlint-cli version to 0.46.0
  [#3769](https://github.com/open-telemetry/opentelemetry-cpp/pull/3769)

* [chore] Add clo monitor exemption for artifact hub
  [#3772](https://github.com/open-telemetry/opentelemetry-cpp/pull/3772)

* [API] fix shared_ptr self assignments
  [#3768](https://github.com/open-telemetry/opentelemetry-cpp/pull/3768)

* chore: Add badges to readme
  [#3773](https://github.com/open-telemetry/opentelemetry-cpp/pull/3773)

* Bump github/codeql-action from 4.31.5 to 4.31.6
  [#3774](https://github.com/open-telemetry/opentelemetry-cpp/pull/3774)

* Bump step-security/harden-runner from 2.13.2 to 2.13.3
  [#3776](https://github.com/open-telemetry/opentelemetry-cpp/pull/3776)

* Bump actions/checkout from 6.0.0 to 6.0.1
  [#3775](https://github.com/open-telemetry/opentelemetry-cpp/pull/3775)

* [TEST] Multiple exports in OTLP HTTP exporters tests
  [#3771](https://github.com/open-telemetry/opentelemetry-cpp/pull/3771)

* Bump actions/stale from 10.1.0 to 10.1.1
  [#3777](https://github.com/open-telemetry/opentelemetry-cpp/pull/3777)

* Bump github/codeql-action from 4.31.6 to 4.31.7
  [#3783](https://github.com/open-telemetry/opentelemetry-cpp/pull/3783)

* [SDK] Fix BatchSpanProcessorOptions and BatchLogRecordProcessorOptions
  to restore C++20 aggregate initialization support
  [#3780](https://github.com/open-telemetry/opentelemetry-cpp/pull/3780)

* [CI] Add BCR GitHub workflow and configuration
  [#3782](https://github.com/open-telemetry/opentelemetry-cpp/pull/3782)

* Bump codecov/codecov-action from 5.5.1 to 5.5.2
  [#3785](https://github.com/open-telemetry/opentelemetry-cpp/pull/3785)

* Bump step-security/harden-runner from 2.13.3 to 2.14.0
  [#3784](https://github.com/open-telemetry/opentelemetry-cpp/pull/3784)

* Bump actions/cache from 4.3.0 to 5.0.0
  [#3786](https://github.com/open-telemetry/opentelemetry-cpp/pull/3786)

* Bump actions/download-artifact from 6.0.0 to 7.0.0
  [#3791](https://github.com/open-telemetry/opentelemetry-cpp/pull/3791)

* Bump github/codeql-action from 4.31.7 to 4.31.8
  [#3790](https://github.com/open-telemetry/opentelemetry-cpp/pull/3790)

* Bump actions/cache from 5.0.0 to 5.0.1
  [#3789](https://github.com/open-telemetry/opentelemetry-cpp/pull/3789)

* Bump bazel-contrib/publish-to-bcr/.github/workflows/publish.yaml
  from 1.0.0 to 1.1.0
  [#3788](https://github.com/open-telemetry/opentelemetry-cpp/pull/3788)

* [CI] upgrade to clang-tidy 20
  [#3762](https://github.com/open-telemetry/opentelemetry-cpp/pull/3762)

* [SDK] Fix off-by-one error in
  Base2ExponentialHistogramAggregation::Merge() downscaling
  [#3793](https://github.com/open-telemetry/opentelemetry-cpp/pull/3793)

* Bump github/codeql-action from 4.31.8 to 4.31.9
  [#3792](https://github.com/open-telemetry/opentelemetry-cpp/pull/3792)

* Bump docker/setup-buildx-action from 3.11.1 to 3.12.0
  [#3797](https://github.com/open-telemetry/opentelemetry-cpp/pull/3797)

* [SDK] Implement EnvEntityDetector for OTEL_ENTITIES parsing (#3652)
  [#3795](https://github.com/open-telemetry/opentelemetry-cpp/pull/3795)

* [ADMIN] Update link to SIG meeting notes
  [#3803](https://github.com/open-telemetry/opentelemetry-cpp/pull/3803)

* [CONFIGURATION] File configuration - remove zipkin
  [#3804](https://github.com/open-telemetry/opentelemetry-cpp/pull/3804)

* [CONFIGURATION] File configuration - rename tls properties
  [#3805](https://github.com/open-telemetry/opentelemetry-cpp/pull/3805)

* [EXPORTER] add OTLP HTTP metric exporter benchmark
  [#3801](https://github.com/open-telemetry/opentelemetry-cpp/pull/3801)

* [API] Parse baggage value as spec compliant
  [#3758](https://github.com/open-telemetry/opentelemetry-cpp/pull/3758)

* Bump github/codeql-action from 4.31.9 to 4.31.10
  [#3807](https://github.com/open-telemetry/opentelemetry-cpp/pull/3807)

* [TEST] add test for invalid OTEL_BLRP_SCHEDULE_DELAY env value
  [#3811](https://github.com/open-telemetry/opentelemetry-cpp/pull/3811)

* [CONFIGURATION] File configuration - implement log_level
  [#3810](https://github.com/open-telemetry/opentelemetry-cpp/pull/3810)

* [SEMANTIC CONVENTIONS] Upgrade to semantic conventions 1.39.0
  [#3813](https://github.com/open-telemetry/opentelemetry-cpp/pull/3813)

* Bump actions/cache from 5.0.1 to 5.0.2
  [#3814](https://github.com/open-telemetry/opentelemetry-cpp/pull/3814)

* [CONFIGURATION] File configuration - prometheus without_target_info
  [#3818](https://github.com/open-telemetry/opentelemetry-cpp/pull/3818)

* [BAZEL] Fix CI after bazel 9.0.0 release
  [#3823](https://github.com/open-telemetry/opentelemetry-cpp/pull/3823)

* [TEST] Extend logs invalid schedule delay tests
  [#3812](https://github.com/open-telemetry/opentelemetry-cpp/pull/3812)

* [BUILD] Update vendored TraceLoggingDynamic.h to compile
  with msvc's conformant preprocessor
  [#3822](https://github.com/open-telemetry/opentelemetry-cpp/pull/3822)

* [CONFIGURATION] File configuration - implement cardinality_limits
  [#3820](https://github.com/open-telemetry/opentelemetry-cpp/pull/3820)

* Bump actions/checkout from 6.0.1 to 6.0.2
  [#3826](https://github.com/open-telemetry/opentelemetry-cpp/pull/3826)

* [ETW] Fix infinite loop in ETWProvider::close
  [#3827](https://github.com/open-telemetry/opentelemetry-cpp/pull/3827)

* [ETW] Fix tracer initialization order
  [#3824](https://github.com/open-telemetry/opentelemetry-cpp/pull/3824)

* Bump github/codeql-action from 4.31.10 to 4.31.11
  [#3828](https://github.com/open-telemetry/opentelemetry-cpp/pull/3828)

* Bump step-security/harden-runner from 2.14.0 to 2.14.1
  [#3832](https://github.com/open-telemetry/opentelemetry-cpp/pull/3832)

* Bump github/codeql-action from 4.31.11 to 4.32.0
  [#3831](https://github.com/open-telemetry/opentelemetry-cpp/pull/3831)

* [ETW] Fix ETW Log Exporter: incorrect timestamp, traceId, and spanId
  [#3836](https://github.com/open-telemetry/opentelemetry-cpp/pull/3836)

* [CONFIGURATION] File configuration - implement distribution
  [#3833](https://github.com/open-telemetry/opentelemetry-cpp/pull/3833)

* Bump actions/cache from 5.0.2 to 5.0.3
  [#3838](https://github.com/open-telemetry/opentelemetry-cpp/pull/3838)

* [CONFIGURATION] File configuration - exemplar filter
  [#3837](https://github.com/open-telemetry/opentelemetry-cpp/pull/3837)

* [EXPORTER] Add support for otlp exporter collection limits
  [#3816](https://github.com/open-telemetry/opentelemetry-cpp/pull/3816)

* [BAZEL] Add linkstatic = True to http_client_curl
  [#3825](https://github.com/open-telemetry/opentelemetry-cpp/pull/3825)

* Bump github/codeql-action from 4.32.0 to 4.32.1
  [#3839](https://github.com/open-telemetry/opentelemetry-cpp/pull/3839)

* [CODE HEALTH] Fix clang-tidy warnings, part 1
  [#3841](https://github.com/open-telemetry/opentelemetry-cpp/pull/3841)

* [SDK] Invalid thread instrumentation in PeriodicExportingMetricReader
  [#3842](https://github.com/open-telemetry/opentelemetry-cpp/pull/3842)

* [BUILD] Support build proto as shared lib on Windows
  [#3714](https://github.com/open-telemetry/opentelemetry-cpp/pull/3714)

* [INSTALL] Add pkg-config support for exporters
  [#3829](https://github.com/open-telemetry/opentelemetry-cpp/pull/3829)

* Bump github/codeql-action from 4.32.1 to 4.32.2
  [#3847](https://github.com/open-telemetry/opentelemetry-cpp/pull/3847)

* Bump fossas/fossa-action from 1.7.0 to 1.8.0
  [#3848](https://github.com/open-telemetry/opentelemetry-cpp/pull/3848)

* [RELEASE] Release opentelemetry-cpp 1.25.0
  [#3850](https://github.com/open-telemetry/opentelemetry-cpp/pull/3850)

* [BUILD] Ignore deprecated warning
  [#3845](https://github.com/open-telemetry/opentelemetry-cpp/pull/3845)

Breaking changes:

* [CONFIGURATION] File configuration - remove zipkin
  [#3804](https://github.com/open-telemetry/opentelemetry-cpp/pull/3804)
  * The zipkin trace exporter is no longer supported
    by declarative configuration, due to an upstream schema change.

* [CONFIGURATION] File configuration - rename tls properties
  [#3805](https://github.com/open-telemetry/opentelemetry-cpp/pull/3805)
  * The Tls properties for Grpc and Http are renamed,
    due to an upstream schema change.

* [SDK] Invalid thread instrumentation in PeriodicExportingMetricReader
  [#3842](https://github.com/open-telemetry/opentelemetry-cpp/pull/3842)
  * The collect thread in the periodic exporting metric reader no longer
    exists.
  * As a result, member `collect_thread_instrumentation` in class
    `PeriodicExportingMetricReaderRuntimeOptions` is removed.

## [1.24.0] 2025-11-20

* [RELEASE] Bump main branch to 1.24-dev
  [#3662](https://github.com/open-telemetry/opentelemetry-cpp/pull/3662)

* [TEST] Remove workaround for metrics cardinality limit test
  [#3663](https://github.com/open-telemetry/opentelemetry-cpp/pull/3663)

* Bump github/codeql-action from 3.30.3 to 3.30.4
  [#3664](https://github.com/open-telemetry/opentelemetry-cpp/pull/3664)

* [CONFIGURATION] File configuration - semantic version
  [#3629](https://github.com/open-telemetry/opentelemetry-cpp/pull/3629)

* [BUILD] Fix MinGW GCC cross compilation build on Linux for Windows
  [#3666](https://github.com/open-telemetry/opentelemetry-cpp/pull/3666)

* [CONFIGURATION] File configuration - cmake build
  [#3655](https://github.com/open-telemetry/opentelemetry-cpp/pull/3655)

* [CONFIGURATION] File configuration - bazel build
  [#3654](https://github.com/open-telemetry/opentelemetry-cpp/pull/3654)

* Bump step-security/harden-runner from 2.12.2 to 2.13.1
  [#3671](https://github.com/open-telemetry/opentelemetry-cpp/pull/3671)

* Bump github/codeql-action from 3.30.4 to 3.30.5
  [#3670](https://github.com/open-telemetry/opentelemetry-cpp/pull/3670)

* Bump actions/checkout from 4.2.2 to 5.0.0
  [#3669](https://github.com/open-telemetry/opentelemetry-cpp/pull/3669)

* [CMAKE] Only build and run the exemplar tests if the preview flag is set
  [#3675](https://github.com/open-telemetry/opentelemetry-cpp/pull/3675)

* [TEST] Update latest versions of protobuf, grpc, and curl for testing
  [#3678](https://github.com/open-telemetry/opentelemetry-cpp/pull/3678)

* [SDK] Fix typo in hashmap method GetEnteries
  [#3680](https://github.com/open-telemetry/opentelemetry-cpp/pull/3680)

* [TEST] add ryml to the conan files for install tests
  [#3679](https://github.com/open-telemetry/opentelemetry-cpp/pull/3679)

* [TRACE SDK] Batch span processor options now using env variables
  [#3661](https://github.com/open-telemetry/opentelemetry-cpp/pull/3661)

* Bump ossf/scorecard-action from 2.4.2 to 2.4.3
  [#3677](https://github.com/open-telemetry/opentelemetry-cpp/pull/3677)

* [CONFIGURATION] File configuration - cmake install
  [#3668](https://github.com/open-telemetry/opentelemetry-cpp/pull/3668)

* Bump github/codeql-action from 3.30.5 to 3.30.6
  [#3683](https://github.com/open-telemetry/opentelemetry-cpp/pull/3683)

* Bump peter-evans/create-or-update-comment from 4.0.0 to 5.0.0
  [#3682](https://github.com/open-telemetry/opentelemetry-cpp/pull/3682)

* Bump actions/stale from 10.0.0 to 10.1.0
  [#3686](https://github.com/open-telemetry/opentelemetry-cpp/pull/3686)

* [SDK] BatchLogRecordProcessorOptions from env variables
  [#3687](https://github.com/open-telemetry/opentelemetry-cpp/pull/3687)

* [BUILD] Upgrade third party versions to the latest
  [#3685](https://github.com/open-telemetry/opentelemetry-cpp/pull/3685)

* Bump github/codeql-action from 3.30.6 to 4.30.7
  [#3689](https://github.com/open-telemetry/opentelemetry-cpp/pull/3689)

* Bump github/codeql-action from 4.30.7 to 4.30.8
  [#3692](https://github.com/open-telemetry/opentelemetry-cpp/pull/3692)

* [Metrics] Allow registering one callback for multiple instruments
  [#3667](https://github.com/open-telemetry/opentelemetry-cpp/pull/3667)

* [CI] Upgrade tools/vcpkg to 2025.09.17
  [#3701](https://github.com/open-telemetry/opentelemetry-cpp/pull/3701)

* [CI] Upgrade to conan 2.21.0 for conanfile_latest
  [#3703](https://github.com/open-telemetry/opentelemetry-cpp/pull/3703)

* [bazel] Drop WORKSPACE support
  [#3699](https://github.com/open-telemetry/opentelemetry-cpp/pull/3699)

* Bump github/codeql-action from 4.30.8 to 4.30.9
  [#3707](https://github.com/open-telemetry/opentelemetry-cpp/pull/3707)

* [BUILD] Add rules_cc load statements
  [#3697](https://github.com/open-telemetry/opentelemetry-cpp/pull/3697)

* [CONFIGURATION] File configuration - parser location
  [#3705](https://github.com/open-telemetry/opentelemetry-cpp/pull/3705)

* [API] Fix -Werror=alloc-size-larger-than= warning in runtime_context.h
  [#3709](https://github.com/open-telemetry/opentelemetry-cpp/pull/3709)

* Bump github/codeql-action from 4.30.9 to 4.31.0
  [#3720](https://github.com/open-telemetry/opentelemetry-cpp/pull/3720)

* Bump actions/download-artifact from 5.0.0 to 6.0.0
  [#3719](https://github.com/open-telemetry/opentelemetry-cpp/pull/3719)

* [CONFIGURATION] File configuration - prometheus translation
  [#3715](https://github.com/open-telemetry/opentelemetry-cpp/pull/3715)

* [SDK] Misc cleanup in attribute_utils.h
  [#3716](https://github.com/open-telemetry/opentelemetry-cpp/pull/3716)

* [TEST] Disable test BasicCurlHttpTests.SendGetRequestAsync
  [#3722](https://github.com/open-telemetry/opentelemetry-cpp/pull/3722)

* [SDK] Add cardinality_limit to all derived classes of AggregationConfig
  [#3728](https://github.com/open-telemetry/opentelemetry-cpp/pull/3728)

* Bump github/codeql-action from 4.31.0 to 4.31.2
  [#3733](https://github.com/open-telemetry/opentelemetry-cpp/pull/3733)

* [BUILD] Upgrade to opentelemetry-proto 1.8.0
  [#3730](https://github.com/open-telemetry/opentelemetry-cpp/pull/3730)

* [SEMANTIC CONVENTIONS] Upgrade to semantic conventions 1.38.0
  [#3729](https://github.com/open-telemetry/opentelemetry-cpp/pull/3729)

* [CONFIGURATION] Implement declarative configuration (config.yaml)
  [#2518](https://github.com/open-telemetry/opentelemetry-cpp/pull/2518)

* [CONFIGURATION] File configuration - tls
  [#3726](https://github.com/open-telemetry/opentelemetry-cpp/pull/3726)

* [CONFIGURATION] File configuration - console metric exporter
  [#3734](https://github.com/open-telemetry/opentelemetry-cpp/pull/3734)

* [DOCS] Remove out-of-date vcpkg docs
  [#3735](https://github.com/open-telemetry/opentelemetry-cpp/pull/3735)

* [DOCKER] Update docker files to build all third party dependencies
  from the tags file
  [#3718](https://github.com/open-telemetry/opentelemetry-cpp/pull/3718)

* Bump docker/setup-qemu-action from 3.6.0 to 3.7.0
  [#3739](https://github.com/open-telemetry/opentelemetry-cpp/pull/3739)

* Bump step-security/harden-runner from 2.13.1 to 2.13.2
  [#3738](https://github.com/open-telemetry/opentelemetry-cpp/pull/3738)

* [API] Make Request Context Token constructor public
  [#3708](https://github.com/open-telemetry/opentelemetry-cpp/pull/3708)

* [BUILD] Remove CMAKE POLICY CMP0092
  [#3741](https://github.com/open-telemetry/opentelemetry-cpp/pull/3741)

* [DOCS] Fix code sample for setting CustomLogHandler
  [#3736](https://github.com/open-telemetry/opentelemetry-cpp/pull/3736)

* Bump github/codeql-action from 4.31.2 to 4.31.3
  [#3744](https://github.com/open-telemetry/opentelemetry-cpp/pull/3744)

* [METRICS] Add tag to AggregationConfig for aggregation type validation
  [#3732](https://github.com/open-telemetry/opentelemetry-cpp/pull/3732)

* [BUILD] CMake install path with double slash
  [#3747](https://github.com/open-telemetry/opentelemetry-cpp/pull/3747)

* [CI] Free disk space
  [#3749](https://github.com/open-telemetry/opentelemetry-cpp/pull/3749)

* [SDK] Reset TraceFlags::IsSampled bit on sampler Decision::DROP
  [#3745](https://github.com/open-telemetry/opentelemetry-cpp/pull/3745)

* [CODE HEALTH] turn on the cppcoreguidelines-init-variables check and fix warnings
  [#3751](https://github.com/open-telemetry/opentelemetry-cpp/pull/3751)

* Bump actions/checkout from 5.0.0 to 5.0.1
  [#3753](https://github.com/open-telemetry/opentelemetry-cpp/pull/3753)

* Bump github/codeql-action from 4.31.3 to 4.31.4
  [#3755](https://github.com/open-telemetry/opentelemetry-cpp/pull/3755)

* Bump actions/checkout from 5.0.1 to 6.0.0
  [#3757](https://github.com/open-telemetry/opentelemetry-cpp/pull/3757)

New Features:

* [CONFIGURATION] Implement declarative configuration (config.yaml)
  [#2518](https://github.com/open-telemetry/opentelemetry-cpp/pull/2518)

  * Configuration for opentelemetry-cpp can now be done using a config.yaml
    file, instead of using environment variables.
  * See [opentelemetry-configuration](https://github.com/open-telemetry/opentelemetry-configuration)

## [1.23.0] 2025-09-25

* [CodeHealth] Fix clang-tidy warnings part 6
  [#3507](https://github.com/open-telemetry/opentelemetry-cpp/pull/3507)

* [CMAKE] Add CMake scripts to find or fetch curl and find zlib
  [#3526](https://github.com/open-telemetry/opentelemetry-cpp/pull/3526)

* [REMOVAL] remove unused ci bash scripts
  [#3541](https://github.com/open-telemetry/opentelemetry-cpp/pull/3541)

* Bump step-security/harden-runner from 2.12.2 to 2.13.0
  [#3542](https://github.com/open-telemetry/opentelemetry-cpp/pull/3542)

* [SDK] Ensure TraceId is portable on big-endian architectures
  [#3543](https://github.com/open-telemetry/opentelemetry-cpp/pull/3543)

* [TEST] Shared otel-cpp libs linked to latest static protobuf and grpc
  [#3544](https://github.com/open-telemetry/opentelemetry-cpp/pull/3544)

* [CONFIGURATION] File configuration - component registry
  [#3537](https://github.com/open-telemetry/opentelemetry-cpp/pull/3537)

* [SDK] Implement env var configuration for PeriodicExportingMetricReader
  [#3549](https://github.com/open-telemetry/opentelemetry-cpp/pull/3549)

* [SDK] Update default exemplar reservoir size for exponential histograms
  [#3551](https://github.com/open-telemetry/opentelemetry-cpp/pull/3551)

* [SDK] Implements options for the ParentBasedSampler with default values
  [#3553](https://github.com/open-telemetry/opentelemetry-cpp/pull/3553)

* Bump github/codeql-action from 3.29.2 to 3.29.3
  [#3556](https://github.com/open-telemetry/opentelemetry-cpp/pull/3556)

* [CMAKE] Add CMake scripts to find or fetch protobuf and grpc
  [#3533](https://github.com/open-telemetry/opentelemetry-cpp/pull/3533)

* [CONFIGURATION] File configuration - sdk builder
  [#3550](https://github.com/open-telemetry/opentelemetry-cpp/pull/3550)

* Bump github/codeql-action from 3.29.3 to 3.29.4
  [#3558](https://github.com/open-telemetry/opentelemetry-cpp/pull/3558)

* [CONFIGURATION] File configuration - spec version 1.0.0-rc1
  [#3557](https://github.com/open-telemetry/opentelemetry-cpp/pull/3557)

* [CONFIGURATION] File configuration - sdk resource
  [#3567](https://github.com/open-telemetry/opentelemetry-cpp/pull/3567)

* [CONFIGURATION] File configuration - ostream exporter builders
  [#3563](https://github.com/open-telemetry/opentelemetry-cpp/pull/3563)

* [CONFIGURATION] File configuration - prometheus exporter builder
  [#3564](https://github.com/open-telemetry/opentelemetry-cpp/pull/3564)

* [CONFIGURATION] File configuration - zipkin exporter builder
  [#3565](https://github.com/open-telemetry/opentelemetry-cpp/pull/3565)

* [CONFIGURATION] File configuration - otlp exporter builders
  [#3566](https://github.com/open-telemetry/opentelemetry-cpp/pull/3566)

* [CMAKE] Don't set iwyu and clang-tidy properties on nlohmann_json with cmake <3.19
  [#3568](https://github.com/open-telemetry/opentelemetry-cpp/pull/3568)

* [SDK] View should not have a unit
  [#3552](https://github.com/open-telemetry/opentelemetry-cpp/pull/3552)

* [EXPORTER] Fixes tsan warnings
  [#3531](https://github.com/open-telemetry/opentelemetry-cpp/pull/3531)

* [DOC] Document minimum required versions
  [#3562](https://github.com/open-telemetry/opentelemetry-cpp/pull/3562)

* Bump github/codeql-action from 3.29.4 to 3.29.5
  [#3574](https://github.com/open-telemetry/opentelemetry-cpp/pull/3574)

* Add subscript to issue templates
  [#3576](https://github.com/open-telemetry/opentelemetry-cpp/pull/3576)

* [CONFIGURATION] File configuration - configuration example
  [#3573](https://github.com/open-telemetry/opentelemetry-cpp/pull/3573)

* Bump actions/download-artifact from 4.3.0 to 5.0.0
  [#3578](https://github.com/open-telemetry/opentelemetry-cpp/pull/3578)

* Bump actions/cache from 4.2.3 to 4.2.4
  [#3580](https://github.com/open-telemetry/opentelemetry-cpp/pull/3580)

* [SDK] Implementation of container resource as per semconv
  [#3572](https://github.com/open-telemetry/opentelemetry-cpp/pull/3572)

* Bump github/codeql-action from 3.29.7 to 3.29.8
  [#3584](https://github.com/open-telemetry/opentelemetry-cpp/pull/3584)

* [CI] update do_ci scripts to use common cmake cache scripts
  [#3582](https://github.com/open-telemetry/opentelemetry-cpp/pull/3582)

* Bump actions/checkout from 4.2.2 to 5.0.0
  [#3587](https://github.com/open-telemetry/opentelemetry-cpp/pull/3587)

* Bump github/codeql-action from 3.29.8 to 3.29.9
  [#3589](https://github.com/open-telemetry/opentelemetry-cpp/pull/3589)

* [BUILD] Remove duplicated deprecated warnings in logs headers
  [#3592](https://github.com/open-telemetry/opentelemetry-cpp/pull/3592)

* Bump github/codeql-action from 3.29.9 to 3.29.10
  [#3595](https://github.com/open-telemetry/opentelemetry-cpp/pull/3595)

* [resource_detectors] implementation of process resource detector as per semconv
  [#3591](https://github.com/open-telemetry/opentelemetry-cpp/pull/3591)

* Bump codecov/codecov-action from 5.4.3 to 5.5.0
  [#3597](https://github.com/open-telemetry/opentelemetry-cpp/pull/3597)

* Bump github/codeql-action from 3.29.10 to 3.29.11
  [#3602](https://github.com/open-telemetry/opentelemetry-cpp/pull/3602)

* [BAZEL] Drop rules_foreign_cc as a dependency
  [#3601](https://github.com/open-telemetry/opentelemetry-cpp/pull/3601)

* [BUILD] Remove cmake support for thrift as Jaeger was removed
  [#3604](https://github.com/open-telemetry/opentelemetry-cpp/pull/3604)

* [CMAKE] upgrade cmake minimum version to 3.16
  [#3599](https://github.com/open-telemetry/opentelemetry-cpp/pull/3599)

* [CMAKE] Set the project version through the cmake project macro
  [#3605](https://github.com/open-telemetry/opentelemetry-cpp/pull/3605)

* [CONFIGURATION] File configuration - functional tests
  [#3606](https://github.com/open-telemetry/opentelemetry-cpp/pull/3606)

* [SEMANTIC CONVENTIONS] Upgrade to semantic conventions 1.37.0
  [#3615](https://github.com/open-telemetry/opentelemetry-cpp/pull/3615)

* [BUILD] Use -dev versions in main branch
  [#3609](https://github.com/open-telemetry/opentelemetry-cpp/pull/3609)

* [BAZEL] Fix version warnings in MODULE.bazel
  [#3617](https://github.com/open-telemetry/opentelemetry-cpp/pull/3617)

* [Copilot] Add instructions for OpenTelemetry C++
  [#3614](https://github.com/open-telemetry/opentelemetry-cpp/pull/3614)

* [MAINTAINER] Maintain version numbers with tbump
  [#3616](https://github.com/open-telemetry/opentelemetry-cpp/pull/3616)

* Bump github/codeql-action from 3.29.11 to 3.30.0
  [#3621](https://github.com/open-telemetry/opentelemetry-cpp/pull/3621)

* Bump benchmark-action/github-action-benchmark from 1.20.4 to 1.20.5
  [#3623](https://github.com/open-telemetry/opentelemetry-cpp/pull/3623)

* [BUILD] Cleanup cppcheck warnings
  [#3619](https://github.com/open-telemetry/opentelemetry-cpp/pull/3619)

* Bump actions/stale from 9.1.0 to 10.0.0
  [#3626](https://github.com/open-telemetry/opentelemetry-cpp/pull/3626)

* Bump actions/github-script from 7.0.1 to 8.0.0
  [#3627](https://github.com/open-telemetry/opentelemetry-cpp/pull/3627)

* Bump codecov/codecov-action from 5.5.0 to 5.5.1
  [#3625](https://github.com/open-telemetry/opentelemetry-cpp/pull/3625)

* [resource_detectors] implementation of remaining process attributes
  [#3603](https://github.com/open-telemetry/opentelemetry-cpp/pull/3603)

* Bump benchmark-action/github-action-benchmark from 1.20.5 to 1.20.7
  [#3631](https://github.com/open-telemetry/opentelemetry-cpp/pull/3631)

* Bump github/codeql-action from 3.30.0 to 3.30.1
  [#3630](https://github.com/open-telemetry/opentelemetry-cpp/pull/3630)

* Bump github/codeql-action from 3.30.1 to 3.30.2
  [#3637](https://github.com/open-telemetry/opentelemetry-cpp/pull/3637)

* Bump step-security/harden-runner from 2.13.0 to 2.13.1
  [#3636](https://github.com/open-telemetry/opentelemetry-cpp/pull/3636)

* Bump github/codeql-action from 3.30.2 to 3.30.3
  [#3639](https://github.com/open-telemetry/opentelemetry-cpp/pull/3639)

* [Metrics] New Attribute Processor for Exclude list
  [#3633](https://github.com/open-telemetry/opentelemetry-cpp/pull/3633)

* Implementing configurable aggregation cardinality limit
  [#3624](https://github.com/open-telemetry/opentelemetry-cpp/pull/3624)

* [CMAKE] Fix CMake 4.x build error on MacOS runner and when building opentracing
  [#3649](https://github.com/open-telemetry/opentelemetry-cpp/pull/3649)

* [SDK] custom hash and equality for attribute processor
  [#3643](https://github.com/open-telemetry/opentelemetry-cpp/pull/3643)

* Bump actions/cache from 4.2.4 to 4.3.0
  [#3658](https://github.com/open-telemetry/opentelemetry-cpp/pull/3658)

* [SDK] Fix copying overflow attributes in metric AttributesHashMap
  [#3651](https://github.com/open-telemetry/opentelemetry-cpp/pull/3651)

Important changes:

* [CMAKE] Upgrade CMake minimum version to 3.16
  [#3599](https://github.com/open-telemetry/opentelemetry-cpp/pull/3599)

* [BUILD] Use -dev versions in main branch
  [#3609](https://github.com/open-telemetry/opentelemetry-cpp/pull/3609)
  * The version number in the main branch has changed,
    to better differentiate with the latest release.
  * For example:
    * With a latest release 1.22.0, the main branch is 1.23.0-dev
    * Upon release of 1.23.0, the main branch becomes 1.24.0-dev

Breaking changes:

* [SDK] View should not have a unit
  [#3552](https://github.com/open-telemetry/opentelemetry-cpp/pull/3552)
  * The `unit` parameter has been removed from the `View` constructor
    and `ViewFactory::Create` methods.
  * Please adjust SDK configuration code accordingly.

## [1.22.0] 2025-07-11

* [DOC] Udpate link to membership document
  [#3452](https://github.com/open-telemetry/opentelemetry-cpp/pull/3452)

* [CI] build examples with IWYU
  [#3450](https://github.com/open-telemetry/opentelemetry-cpp/pull/3450)

* Bump ossf/scorecard-action from 2.4.1 to 2.4.2
  [#3455](https://github.com/open-telemetry/opentelemetry-cpp/pull/3455)

* [SDK] Use shared_ptr internally for AttributesProcessor to prevent use-after-free
  [#3457](https://github.com/open-telemetry/opentelemetry-cpp/pull/3457)

* [CI] build iwyu
  [#3459](https://github.com/open-telemetry/opentelemetry-cpp/pull/3459)

* Bump github/codeql-action from 3.28.18 to 3.28.19
  [#3462](https://github.com/open-telemetry/opentelemetry-cpp/pull/3462)

* [DOC] Update doc comments to pass -WDocumention check
  [#3375](https://github.com/open-telemetry/opentelemetry-cpp/pull/3375)

* [TEST] test examples in CI with CMake Part 1
  [#3449](https://github.com/open-telemetry/opentelemetry-cpp/pull/3449)

* [BUILD] Allow compilation with CXX26
  [#3464](https://github.com/open-telemetry/opentelemetry-cpp/pull/3464)

* [SDK] Add credentials option to OTLP gRPC client
  [#3403](https://github.com/open-telemetry/opentelemetry-cpp/pull/3403)

* [CI] Remove windows 2019
  [#3466](https://github.com/open-telemetry/opentelemetry-cpp/pull/3466)

* [CodeHealth] fix include-what-you-use, part 8
  [#3465](https://github.com/open-telemetry/opentelemetry-cpp/pull/3465)

* [BUILD] Upgrade to opentelemetry-proto 1.7.0
  [#3443](https://github.com/open-telemetry/opentelemetry-cpp/pull/3443)

* Bump github/codeql-action from 3.28.19 to 3.29.0
  [#3472](https://github.com/open-telemetry/opentelemetry-cpp/pull/3472)

* Bump step-security/harden-runner from 2.12.0 to 2.12.1
  [#3471](https://github.com/open-telemetry/opentelemetry-cpp/pull/3471)

* [SDK] BatchLogRecordProcessor::ForceFlush is not waking up bg thread
  [#3448](https://github.com/open-telemetry/opentelemetry-cpp/pull/3448)

* [CI] rely on github installed cmake for ci runners
  [#3482](https://github.com/open-telemetry/opentelemetry-cpp/pull/3482)

* [CI] Increase code coverage of iwyu and clang-tidy ci jobs
  [#3469](https://github.com/open-telemetry/opentelemetry-cpp/pull/3469)

* [REMOVAL] Remove CMake option WITH_REMOVE_METER_PREVIEW
  [#3476](https://github.com/open-telemetry/opentelemetry-cpp/pull/3476)

* [REMOVAL] Removed deprecated semantic convention header files
  [#3475](https://github.com/open-telemetry/opentelemetry-cpp/pull/3475)

* Bump docker/setup-buildx-action from 3.10.0 to 3.11.0
  [#3483](https://github.com/open-telemetry/opentelemetry-cpp/pull/3483)

* Bump docker/setup-buildx-action from 3.11.0 to 3.11.1
  [#3488](https://github.com/open-telemetry/opentelemetry-cpp/pull/3488)

* [Code Health] include-what-you-use cleanup, part 9
  [#3492](https://github.com/open-telemetry/opentelemetry-cpp/pull/3492)

* [CodeHealth] Fix clang-tidy warnings part 1
  [#3493](https://github.com/open-telemetry/opentelemetry-cpp/pull/3493)

* [CMAKE] Add thirdparty install cmake project and install bash script
  [#3486](https://github.com/open-telemetry/opentelemetry-cpp/pull/3486)

* [DOC] Update community member listings
  [#3499](https://github.com/open-telemetry/opentelemetry-cpp/pull/3499)

* [CodeHealth] Fix clang-tidy warnings part 2
  [#3496](https://github.com/open-telemetry/opentelemetry-cpp/pull/3496)

* [CodeHealth] Fix clang-tidy warnings part 3
  [#3498](https://github.com/open-telemetry/opentelemetry-cpp/pull/3498)

* [DOC] Fix outdated community membership link
  [#3500](https://github.com/open-telemetry/opentelemetry-cpp/pull/3500)

* [CONFIGURATION] File configuration - trace model
  [#3467](https://github.com/open-telemetry/opentelemetry-cpp/pull/3467)

* [CONFIGURATION] File configuration - sampler model
  [#3468](https://github.com/open-telemetry/opentelemetry-cpp/pull/3468)

* [BUILD] Fixes grpc linking for OTLP exporter's tests
  [#3435](https://github.com/open-telemetry/opentelemetry-cpp/pull/3435)

* [CONFIGURATION] File configuration - log model
  [#3473](https://github.com/open-telemetry/opentelemetry-cpp/pull/3473)

* [CONFIGURATION] File configuration - metric model
  [#3474](https://github.com/open-telemetry/opentelemetry-cpp/pull/3474)

* Bump github/codeql-action from 3.29.0 to 3.29.1
  [#3505](https://github.com/open-telemetry/opentelemetry-cpp/pull/3505)

* [EXPORTER] Add bytes support for OTLP recordables
  [#3495](https://github.com/open-telemetry/opentelemetry-cpp/pull/3495)

* [CodeHealth] Fix clang tidy warnings part 4
  [#3501](https://github.com/open-telemetry/opentelemetry-cpp/pull/3501)

* [CodeHealth] Fix clang-tidy warnings part 5
  [#3506](https://github.com/open-telemetry/opentelemetry-cpp/pull/3506)

* [CI] Add minimum token permissions for all github workflow files
  [#3508](https://github.com/open-telemetry/opentelemetry-cpp/pull/3508)

* Bump step-security/harden-runner from 2.12.1 to 2.12.2
  [#3509](https://github.com/open-telemetry/opentelemetry-cpp/pull/3509)

* Bump github/codeql-action from 3.29.1 to 3.29.2
  [#3510](https://github.com/open-telemetry/opentelemetry-cpp/pull/3510)

* [BUILD] Fixes compiling problems in NDK r27
  [#3517](https://github.com/open-telemetry/opentelemetry-cpp/pull/3517)

* [CMAKE] clean up googletest and benchmark dependency management
  [#3485](https://github.com/open-telemetry/opentelemetry-cpp/pull/3485)

* [CONFIGURATION] File configuration - extension model
  [#3503](https://github.com/open-telemetry/opentelemetry-cpp/pull/3503)

* [CONFIGURATION] File configuration - misc model
  [#3504](https://github.com/open-telemetry/opentelemetry-cpp/pull/3504)

* [CONFIGURATION] File configuration - metric aggregation model
  [#3502](https://github.com/open-telemetry/opentelemetry-cpp/pull/3502)

* [CMAKE] find or fetch nlohmann-json
  [#3523](https://github.com/open-telemetry/opentelemetry-cpp/pull/3523)

* [CMAKE] Address the vcpkg opentelemetry-cpp port CMake patches
  [#3518](https://github.com/open-telemetry/opentelemetry-cpp/pull/3518)

* [CMAKE] Add CMake script to find or fetch prometheus-cpp
  [#3522](https://github.com/open-telemetry/opentelemetry-cpp/pull/3522)

* [CMAKE] Switch opentelemetry-proto to use FetchContent
  [#3524](https://github.com/open-telemetry/opentelemetry-cpp/pull/3524)

* [CMAKE] Add CMake script to find or fetch Microsoft.GSL
  [#3521](https://github.com/open-telemetry/opentelemetry-cpp/pull/3521)

* [SEMANTIC CONVENTIONS] Upgrade to semantic conventions 1.36.0
  [#3527](https://github.com/open-telemetry/opentelemetry-cpp/pull/3527)

* [SDK] Fixes duration overflow
  [#3529](https://github.com/open-telemetry/opentelemetry-cpp/pull/3529)

* [CONFIGURATION] File configuration - yaml parser
  [#3519](https://github.com/open-telemetry/opentelemetry-cpp/pull/3519)

* [CONFIGURATION] File configuration - configuration parser
  [#3520](https://github.com/open-telemetry/opentelemetry-cpp/pull/3520)

* [ADMIN] Remove file .github/repository-settings.md
  [#3534](https://github.com/open-telemetry/opentelemetry-cpp/pull/3534)

Important changes:

* [REMOVAL] Removed deprecated semantic convention header files
  [#3475](https://github.com/open-telemetry/opentelemetry-cpp/pull/3475)

  * Old semantic conventions header files have been removed,
    per announcement from Nov 9, 2024, see
   [#3105](https://github.com/open-telemetry/opentelemetry-cpp/pull/3105)

  * Mitigation steps are repeated below, for convenience.

  * Two things have changed:

    * the header file to use
    * the symbol name to use.

    Before, the semantic convention for `url.full` was:

    * declared in file `semantic_conventions.h`
    * declared as symbol `SemanticConventions::kUrlFull`

    Now, the `url.full` convention, which is part or the `url` group, is:

    * declared in file `semconv/url_attributes.h`
    * declared as symbol `semconv::url::kUrlFull`

    Application code that uses semantic conventions must be adjusted
    accordingly.

    In addition, semantic conventions that are not marked as stable
    are generated in a different header file, placed under directory
    `incubating`, to better separate stable and non stable code.

    For example, file `semconv/incubating/url_attributes.h`
    defines `semconv::url::kUrlDomain`,
    which is not marked as stable in semconv v1.27.0

## [1.21.0] 2025-05-28

* [BUILD] Remove WITH_ABSEIL
  [#3318](https://github.com/open-telemetry/opentelemetry-cpp/pull/3318)

* [INSTALL] Add CMake components to the opentelemetry-cpp package
  [#3320](https://github.com/open-telemetry/opentelemetry-cpp/pull/3220)

* [CI] Harden GitHub Actions
  [#3338](https://github.com/open-telemetry/opentelemetry-cpp/pull/3338)

* [StepSecurity] Harden GibHub Actions, part 2
  [#3340](https://github.com/open-telemetry/opentelemetry-cpp/pull/3340)

* Bump github/codeql-action from 3.28.12 to 3.28.13
  [#3341](https://github.com/open-telemetry/opentelemetry-cpp/pull/3341)

* [DEVCONTAINER] expose cmake version setting as docker arg and environment variable
  [#3347](https://github.com/open-telemetry/opentelemetry-cpp/pull/3347)

* [CI] disable bzip2 in conan builds
  [#3352](https://github.com/open-telemetry/opentelemetry-cpp/pull/3352)

* [SEMANTIC CONVENTIONS] Upgrade semantic conventions to 1.32.0
  [#3351](https://github.com/open-telemetry/opentelemetry-cpp/pull/3351)

* Bump github/codeql-action from 3.28.13 to 3.28.15
  [#3353](https://github.com/open-telemetry/opentelemetry-cpp/pull/3353)

* [CMAKE] bump cmake minimum required version to 3.14
  [#3349](https://github.com/open-telemetry/opentelemetry-cpp/pull/3349)

* Bump codecov/codecov-action from 5.4.0 to 5.4.2
  [#3362](https://github.com/open-telemetry/opentelemetry-cpp/pull/3362)

* [DOC] Fix documentation tags in logger API
  [#3371](https://github.com/open-telemetry/opentelemetry-cpp/pull/3371)

* [CI] fix artifacts download/upload
  [#3369](https://github.com/open-telemetry/opentelemetry-cpp/pull/3369)

* [API] Add Enabled method to Tracer
  [#3357](https://github.com/open-telemetry/opentelemetry-cpp/pull/3357)

* [BUILD] Fixes warnings of ciso646 in C++17
  [#3360](https://github.com/open-telemetry/opentelemetry-cpp/pull/3360)

* Bump github/codeql-action from 3.28.15 to 3.28.16
  [#3377](https://github.com/open-telemetry/opentelemetry-cpp/pull/3377)

* Bump step-security/harden-runner from 2.11.1 to 2.12.0
  [#3373](https://github.com/open-telemetry/opentelemetry-cpp/pull/3373)

* Bump docker/build-push-action from 6.15.0 to 6.16.0
  [#3382](https://github.com/open-telemetry/opentelemetry-cpp/pull/3382)

* Bump actions/download-artifact from 4.2.1 to 4.3.0
  [#3381](https://github.com/open-telemetry/opentelemetry-cpp/pull/3381)

* [CI] Harden Github actions - pinned-dependencies (part -1)
  [#3380](https://github.com/open-telemetry/opentelemetry-cpp/pull/3380)

* [StepSecurity] ci: Harden GitHub Actions
  [#3378](https://github.com/open-telemetry/opentelemetry-cpp/pull/3378)

* [SDK] Base2 exponential histogram aggregation
  [#3346](https://github.com/open-telemetry/opentelemetry-cpp/pull/3346)

* [StepSecurity] ci: Harden GitHub Actions
  [#3379](https://github.com/open-telemetry/opentelemetry-cpp/pull/3379)

* [BUILD] Fixes glibc++ 5 checking
  [#3355](https://github.com/open-telemetry/opentelemetry-cpp/pull/3355)

* [TEST] Add stress test for histogram metric for multiple threads validation
  [#3388](https://github.com/open-telemetry/opentelemetry-cpp/pull/3388)

* Bump github/codeql-action from 3.28.16 to 3.28.17
  [#3389](https://github.com/open-telemetry/opentelemetry-cpp/pull/3389)

* [SDK] Optimize PeriodicExportingMetricReader Thread Usage
  [#3383](https://github.com/open-telemetry/opentelemetry-cpp/pull/3383)

* [Metrics SDK] Use nostd::function_ref in AttributesHashMap
  [#3393](https://github.com/open-telemetry/opentelemetry-cpp/pull/3393)

* [SDK] support aggregation of identical instruments
  [#3358](https://github.com/open-telemetry/opentelemetry-cpp/pull/3358)

* [BUILD] Fixes unused var
  [#3397](https://github.com/open-telemetry/opentelemetry-cpp/pull/3397)

* [INSTALL] Unify cmake install functions and dynamically set component dependencies
  [#3368](https://github.com/open-telemetry/opentelemetry-cpp/pull/3368)

* [BUILD] Upgrade nlohmann_json to 3.12.0
  [#3406](https://github.com/open-telemetry/opentelemetry-cpp/pull/3406)

* [BUILD] Upgrade opentelemetry-proto to 1.6.0
  [#3407](https://github.com/open-telemetry/opentelemetry-cpp/pull/3407)

* [CMAKE] add generated protobuf headers to the opentelemetry_proto target
  [#3400](https://github.com/open-telemetry/opentelemetry-cpp/pull/3400)

* [MERGE] Fix accidental rollback of nlohmann-json submodule
  [#3415](https://github.com/open-telemetry/opentelemetry-cpp/pull/3415)

* Bump fossas/fossa-action from 1.6.0 to 1.7.0
  [#3414](https://github.com/open-telemetry/opentelemetry-cpp/pull/3414)

* Bump docker/build-push-action from 6.16.0 to 6.17.0
  [#3420](https://github.com/open-telemetry/opentelemetry-cpp/pull/3420)

* Bump codecov/codecov-action from 5.4.2 to 5.4.3
  [#3419](https://github.com/open-telemetry/opentelemetry-cpp/pull/3419)

* [SEMANTIC CONVENTIONS] Upgrade semantic conventions to 1.33
  [#3416](https://github.com/open-telemetry/opentelemetry-cpp/pull/3416)

* [DOCS] update the INSTALL guide on cmake components
  [#3422](https://github.com/open-telemetry/opentelemetry-cpp/pull/3422)

* Bump github/codeql-action from 3.28.17 to 3.28.18
  [#3423](https://github.com/open-telemetry/opentelemetry-cpp/pull/3423)

* [CMAKE] update cmake files in examples directory
  [#3421](https://github.com/open-telemetry/opentelemetry-cpp/pull/3421)

* [SDK] Fix Base2ExponentialHistogramAggregation Merge with empty buckets
  [#3425](https://github.com/open-telemetry/opentelemetry-cpp/pull/3425)

* [SDK] Fix MetricProducer interface
  [#3413](https://github.com/open-telemetry/opentelemetry-cpp/pull/3413)

* [CMAKE] remove global include_directories usage and rely on target properties
  [#3426](https://github.com/open-telemetry/opentelemetry-cpp/pull/3426)

* [BUILD] remove unused WITH_CURL build flag
  [#3429](https://github.com/open-telemetry/opentelemetry-cpp/pull/3429)

* [SEMANTIC CONVENTIONS] Upgrade to semantic conventions 1.34.0
  [#3428](https://github.com/open-telemetry/opentelemetry-cpp/pull/3428)

* [EXPORTER] ostream log exporter, fix memory ownership issues
  [#3417](https://github.com/open-telemetry/opentelemetry-cpp/pull/3417)

* [TEST] add all components to the cmake fetch content test
  [#3433](https://github.com/open-telemetry/opentelemetry-cpp/pull/3433)

* [BUILD] Error out when building DLL without MSVC
  [#3438](https://github.com/open-telemetry/opentelemetry-cpp/pull/3438)

* [BUILD] Add missing CMake keyword for target_link_libraries
  [#3442](https://github.com/open-telemetry/opentelemetry-cpp/pull/3442)

* [CMAKE] Remove third-party version mismatch warning
  [#3432](https://github.com/open-telemetry/opentelemetry-cpp/pull/3432)

* Bump docker/build-push-action from 6.17.0 to 6.18.0
  [#3446](https://github.com/open-telemetry/opentelemetry-cpp/pull/3446)

* [SEMANTIC CONVENTIONS] Fix comment style to preserve markup.
  [#3444](https://github.com/open-telemetry/opentelemetry-cpp/pull/3444)

* [EXPORTER] support unix sockets in grpc client
  [#3410](https://github.com/open-telemetry/opentelemetry-cpp/pull/3410)

* [BUILD] Propagate INTERFACE_COMPILE_DEFINITIONS from API through common_foo_library
  [#3440](https://github.com/open-telemetry/opentelemetry-cpp/pull/3440)

New Features:

* [SDK] Base2 exponential histogram aggregation
  [#3346](https://github.com/open-telemetry/opentelemetry-cpp/pull/3346)

  * Add base2 exponential histogram aggregation. Includes a new aggregation type,
    ostream exporter, and otlp/grpc exporter. Updated histogram aggregation and
    benchmark tests.

Important changes:

* [EXPORTER] ostream log exporter, fixed memory ownership issues
  [#3417](https://github.com/open-telemetry/opentelemetry-cpp/pull/3417)

  * In the SDK, the following classes implementation has changed:

    * opentelemetry::sdk::logs::ReadableLogRecord
    * opentelemetry::sdk::logs::ReadWriteLogRecord

  * An application implementing a custom log record exporter,
    that reuses these classes from the opentelemetry-cpp SDK,
    will need code adjustments, in particular for methods:

    * GetBody()
    * GetAttributes()

  * Applications not using these SDK classes directly are not affected.

* [BUILD] Remove WITH_ABSEIL
  [#3318](https://github.com/open-telemetry/opentelemetry-cpp/pull/3318)

  * The build option `WITH_ABSEIL` is no longer used, and opentelemetry-cpp
    will no longer use any release of abseil provided externally,
    for its own use.

  * Instead, opentelemetry-cpp will only use an internal abseil version.

  * This change resolves long standing binary integrity issues,
    that occurred in the past when mixing several versions of abseil
    in the build.

## [1.20.0] 2025-04-01

* [BUILD] Update opentelemetry-proto version
  [#3254](https://github.com/open-telemetry/opentelemetry-cpp/pull/3254)

* [BUILD] Build break with CURL 7.29.0
  [#3255](https://github.com/open-telemetry/opentelemetry-cpp/pull/3255)

* [SEMANTIC CONVENTIONS] Upgrade to semantic conventions 1.30.0
  [#3258](https://github.com/open-telemetry/opentelemetry-cpp/pull/3258)

* [SDK] Add tracer scope configurator
  [#3137](https://github.com/open-telemetry/opentelemetry-cpp/pull/3137)

* [DOC] Add document and example for sharing gRPC Client
  [#3260](https://github.com/open-telemetry/opentelemetry-cpp/pull/3260)

* [SDK] Fix BatchLogRecordProcessor to instrument shutdown
  [#3262](https://github.com/open-telemetry/opentelemetry-cpp/pull/3262)

* [SDK] Support OTEL_SDK_DISABLED environment variable
  [#3245](https://github.com/open-telemetry/opentelemetry-cpp/pull/3245)

* [CI] OTLP in Windows builds
  [#3263](https://github.com/open-telemetry/opentelemetry-cpp/pull/3263)

* [BUILD] Fixes compatibility of type_traits
  [#3274](https://github.com/open-telemetry/opentelemetry-cpp/pull/3274)

* [BUILD] Fix compilation with Regex being disabled
  [#3276](https://github.com/open-telemetry/opentelemetry-cpp/pull/3276)

* [EXPORTER] Support exporting event_name using OTLP Exporter
  [#3277](https://github.com/open-telemetry/opentelemetry-cpp/pull/3277)

* [CI] Add FOSSA scanning workflow
  [#3279](https://github.com/open-telemetry/opentelemetry-cpp/pull/3279)

* [BUILD] Adding typecast without whom c++latest build fails
  [#3281](https://github.com/open-telemetry/opentelemetry-cpp/pull/3281)

* [ADMIN] Add FOSSA badges
  [#3280](https://github.com/open-telemetry/opentelemetry-cpp/pull/3280)

* [BUILD] Fix compiling problems with abiv2 and MSVC
  [#3284](https://github.com/open-telemetry/opentelemetry-cpp/pull/3284)

* [BUILD] Enable old behavior of CMP0092
  [#3269](https://github.com/open-telemetry/opentelemetry-cpp/pull/3269)

* [SDK] Add meter scope configurator
  [#3268](https://github.com/open-telemetry/opentelemetry-cpp/pull/3268)

* [DEVCONTAINER] Support customization and run as non-root user
  [#3270](https://github.com/open-telemetry/opentelemetry-cpp/pull/3270)

* [ETW] Add configuration to export 64-bit integer as timestamp
  [#3286](https://github.com/open-telemetry/opentelemetry-cpp/pull/3286)

* [API] Deprecate event logger
  [#3285](https://github.com/open-telemetry/opentelemetry-cpp/pull/3285)

* [BUILD] Add link directory to support curl 8.12
  [#3272](https://github.com/open-telemetry/opentelemetry-cpp/pull/3272)

* [API] Change the param-pack unpacking order to start from left to right
  [#3296](https://github.com/open-telemetry/opentelemetry-cpp/pull/3296)

* [SDK] Implement spec: MetricFilter
  [#3235](https://github.com/open-telemetry/opentelemetry-cpp/pull/3235)

* [SEMANTIC CONVENTIONS] Upgrade semantic conventions to 1.31.0
  [#3297](https://github.com/open-telemetry/opentelemetry-cpp/pull/3297)

* [SDK] Add logger scope configurator
  [#3282](https://github.com/open-telemetry/opentelemetry-cpp/pull/3282)

* [EXAMPLE] fix buffer overrun in the gRPC sample project
  [#3304](https://github.com/open-telemetry/opentelemetry-cpp/pull/3304)

* [CI] Bump fossas/fossa-action from 1.5.0 to 1.6.0
  [#3305](https://github.com/open-telemetry/opentelemetry-cpp/pull/3305)

* [TEST] fix segfault in singleton test with cmake on macos-latest
  [#3316](https://github.com/open-telemetry/opentelemetry-cpp/pull/3316)

* [TEST] fix test failure with elasticsearch exporter on cxx20
  [#3308](https://github.com/open-telemetry/opentelemetry-cpp/pull/3308)

* [TEST] otlp grpc exporter retry test fix
  [#3311](https://github.com/open-telemetry/opentelemetry-cpp/pull/3311)

* [SDK] Use OPENTELEMETRY_EXPORT and static local variables
  [#3314](https://github.com/open-telemetry/opentelemetry-cpp/pull/3314)

* [BUILD] Fix elasticsearch exporter json compatibility
  [#3313](https://github.com/open-telemetry/opentelemetry-cpp/pull/3313)

* [BUILD] Fix missing exported definition for OTLP file exporter and forceflush
  [#3319](https://github.com/open-telemetry/opentelemetry-cpp/pull/3319)

* [BUILD] Remove gRPC header including in OtlpGrpcClientFactory
  [#3321](https://github.com/open-telemetry/opentelemetry-cpp/pull/3321)

* [ADMIN] Add Pranav Sharma in cpp-approvers
  [#3323](https://github.com/open-telemetry/opentelemetry-cpp/pull/3323)

* [DEVCONTAINER] fix grpc install
  [#3325](https://github.com/open-telemetry/opentelemetry-cpp/pull/3325)

* [ADMIN] Add dbarker to approvers
  [#3331](https://github.com/open-telemetry/opentelemetry-cpp/pull/3331)

* [CI] Upgrade CI to ubuntu 22.04
  [#3330](https://github.com/open-telemetry/opentelemetry-cpp/pull/3330)

* [CI] Add ossf-scorecard scanning workflow
  [#3332](https://github.com/open-telemetry/opentelemetry-cpp/pull/3332)

* [CI] pin cmake in ci and devcontainer
  [#3336](https://github.com/open-telemetry/opentelemetry-cpp/pull/3336)

* [METRICS SDK] Fix hash collision in MetricAttributes
  [#3322](https://github.com/open-telemetry/opentelemetry-cpp/pull/3322)

Important changes:

* [SDK] Support OTEL_SDK_DISABLED environment variable
  [#3245](https://github.com/open-telemetry/opentelemetry-cpp/pull/3245)

  * The SDK now exposes the following new methods:

    * opentelemetry::sdk::trace::Provider::SetTracerProvider()
    * opentelemetry::sdk::metrics::Provider::SetMeterProvider()
    * opentelemetry::sdk::logs::Provider::SetLoggerProvider()

  * These methods do support the `OTEL_SDK_DISABLED` environment variable,
    unlike the corresponding existing API Provider classes.

  * Applications are encouraged to migrate from the API to the SDK
    `Provider` classes, to benefit from this feature.

  * All the example code has been updated to reflect the new usage.

## [1.19.0] 2025-01-22

* [PROMETHEUS_EXPORTER] Fix default for emitting otel_scope attributes
  [#3171](https://github.com/open-telemetry/opentelemetry-cpp/pull/3171)

* [Code health] Include what you use cleanup, part 5
  [#3140](https://github.com/open-telemetry/opentelemetry-cpp/pull/3140)

* [BUILD] Upgrade cmake
  [#3167](https://github.com/open-telemetry/opentelemetry-cpp/pull/3167)

* [SHIM] Fix string_view mappings between OT and OTel
  [#3181](https://github.com/open-telemetry/opentelemetry-cpp/pull/3181)

* [EXPORTER] Refactor ElasticSearchRecordable
  [#3164](https://github.com/open-telemetry/opentelemetry-cpp/pull/3164)

* [SEMANTIC CONVENTIONS] Upgrade to semantic conventions 1.29.0
  [#3182](https://github.com/open-telemetry/opentelemetry-cpp/pull/3182)

* [BUILD] Fix cross-compilation with protoc
  [#3186](https://github.com/open-telemetry/opentelemetry-cpp/pull/3186)

* [Code health] Perform cppcheck cleanup
  [#3150](https://github.com/open-telemetry/opentelemetry-cpp/pull/3150)

* [EXPORTER] add instrumentation scope attributes
  to otlp proto messages for traces and metrics
  [#3185](https://github.com/open-telemetry/opentelemetry-cpp/pull/3185)

* [SDK] Tracer provider shutdown blocks in-definitively
  [#3191](https://github.com/open-telemetry/opentelemetry-cpp/pull/3191)

* [SEMANTIC CONVENTIONS] Upgrade to weaver 0.11.0
  [#3194](https://github.com/open-telemetry/opentelemetry-cpp/pull/3194)

* [DOC] Update existing maintaining dependencies doc
  [#3195](https://github.com/open-telemetry/opentelemetry-cpp/pull/3195)

* [TEST] Change is_called_ and got_response_ to use atomic
  [#3204](https://github.com/open-telemetry/opentelemetry-cpp/pull/3204)

* [SEMANTIC CONVENTIONS] update links to openmetrics to reference the v1.0.0 release
  [#3205](https://github.com/open-telemetry/opentelemetry-cpp/pull/3205)

* [CI] Fix CI on ubuntu-latest
  [#3207](https://github.com/open-telemetry/opentelemetry-cpp/pull/3207)

* [BUILD] Build break using protoc 3.14
  [#3211](https://github.com/open-telemetry/opentelemetry-cpp/pull/3211)

* [TEST] Build the singleton test on windows
  [#3183](https://github.com/open-telemetry/opentelemetry-cpp/pull/3183)

* [BUILD] Add cxx feature detections
  [#3203](https://github.com/open-telemetry/opentelemetry-cpp/pull/3203)

* [SDK] Do not frequently create and destroy http client threads
  [#3198](https://github.com/open-telemetry/opentelemetry-cpp/pull/3198)

* [EXPORTER] Optimize OTLP HTTP compression
  [#3178](https://github.com/open-telemetry/opentelemetry-cpp/pull/3178)

* [SDK] Fix include instrumentation scope attributes in equal method
  [#3214](https://github.com/open-telemetry/opentelemetry-cpp/pull/3214)

* Upgrade to opentelemetry-proto 1.5.0
  [#3210](https://github.com/open-telemetry/opentelemetry-cpp/pull/3210)

* [TEST] Added support for SELINUX in functional tests
  [#3212](https://github.com/open-telemetry/opentelemetry-cpp/pull/3212)

* [EDITORIAL] fix changelog entry for PR 3185
  [#3217](https://github.com/open-telemetry/opentelemetry-cpp/pull/3217)

* [TEST] Functional tests for OTLP/gRPC with mutual TLS
  [#3227](https://github.com/open-telemetry/opentelemetry-cpp/pull/3227)

* [SEMCONV] Metrics are incorrectly prefixed with 'metric'
  [#3228](https://github.com/open-telemetry/opentelemetry-cpp/pull/3228)

* [BUILD] Add OTLP/file exporter for dll and examples
  [#3231](https://github.com/open-telemetry/opentelemetry-cpp/pull/3231)

* [Code Health] Include what you use, part 6
  [#3216](https://github.com/open-telemetry/opentelemetry-cpp/pull/3216)

* [CI] Spurious test failures
  [#3233](https://github.com/open-telemetry/opentelemetry-cpp/pull/3233)

* [BUILD] Fix error ‘uint8_t’ does not name a type with gcc-15
  [#3240](https://github.com/open-telemetry/opentelemetry-cpp/pull/3240)

* [EXPORTER] fix throw in OtlpGrpcMetricExporter with shared grpc client
  [#3243](https://github.com/open-telemetry/opentelemetry-cpp/pull/3243)

* [SDK] Better control of threads executed by opentelemetry-cpp
  [#3175](https://github.com/open-telemetry/opentelemetry-cpp/pull/3175)

* [Code Health] Include what you use, part 7
  [#3238](https://github.com/open-telemetry/opentelemetry-cpp/pull/3238)

* [SDK] Fix lifetime of GlobalLogHandler
  [#3221](https://github.com/open-telemetry/opentelemetry-cpp/pull/3221)

* [MAINTAINER] Add devcontainer
  [#3123](https://github.com/open-telemetry/opentelemetry-cpp/pull/3123)

* [SDK] enable deriving from ResourceDetector to create a Resource
  [#3247](https://github.com/open-telemetry/opentelemetry-cpp/pull/3247)

* [EXPORTER] Support handling retry-able errors for OTLP/HTTP
  [#3223](https://github.com/open-telemetry/opentelemetry-cpp/pull/3223)

* [CI] Add GRPC in maintainer CI
  [#3248](https://github.com/open-telemetry/opentelemetry-cpp/pull/3248)

* [EXPORTER] Support handling retry-able errors for OTLP/gRPC
  [#3219](https://github.com/open-telemetry/opentelemetry-cpp/pull/3219)

* [SDK] Optimize Metric Processing for Single Collector with Delta Temporality
  [#3236](https://github.com/open-telemetry/opentelemetry-cpp/pull/3236)

New features:

* [SDK] Better control of threads executed by opentelemetry-cpp
  [#3175](https://github.com/open-telemetry/opentelemetry-cpp/pull/3175)

  * This feature provides a way for applications,
    when configuring the SDK and exporters,
    to participate in the execution path
    of internal opentelemetry-cpp threads.

  * The opentelemetry-cpp library provides the following:

    * a new ThreadInstrumentation interface,
    * new runtime options structures, to optionally configure the SDK:
      * BatchSpanProcessorRuntimeOptions
      * PeriodicExportingMetricReaderRuntimeOptions
      * BatchLogRecordProcessorRuntimeOptions
    * new runtime options structures,
      to optionally configure the OTLP HTTP exporters:
      * OtlpHttpExporterRuntimeOptions
      * OtlpHttpMetricExporterRuntimeOptions
      * OtlpHttpLogRecordExporterRuntimeOptions
    * new ThreadInstrumentation parameters,
      to optionally configure the CURL HttpClient
    * new runtime options structures,
      to optionally configure the OTLP FILE exporters:
      * OtlpFileExporterRuntimeOptions
      * OtlpFileMetricExporterRuntimeOptions
      * OtlpFileLogRecordExporterRuntimeOptions
    * new runtime options structure,
      to optionally configure the OTLP FILE client:
      * OtlpFileClientRuntimeOptions

  * Using the optional runtime options structures,
    an application can subclass the ThreadInstrumentation interface,
    and be notified of specific events of interest during the execution
    of an internal opentelemetry-cpp thread.

  * This allows an application to call, for example:

    * pthread_setaffinity_np(), for better performances,
    * setns(), to control the network namespace used by HTTP CURL connections
    * pthread_setname_np(), for better observability from the operating system
    * many more specific apis, as needed

  * See the documentation for ThreadInstrumentation for details.

  * A new example program, example_otlp_instrumented_http,
    shows how to use the feature,
    and add application logic in the thread execution code path.

  * Note that this feature is experimental,
    protected by a WITH_THREAD_INSTRUMENTATION_PREVIEW
    flag in CMake. Various runtime options structures,
    as well as the thread instrumentation interface,
    may change without notice before this feature is declared stable.

* [EXPORTER] Support handling retry-able errors for OTLP/HTTP
  [#3223](https://github.com/open-telemetry/opentelemetry-cpp/pull/3223)

  * This feature is experimental,
    protected by a WITH_OTLP_RETRY_PREVIEW
    flag in CMake.

* [EXPORTER] Support handling retry-able errors for OTLP/gRPC
  [#3219](https://github.com/open-telemetry/opentelemetry-cpp/pull/3219)

  * This feature is experimental,
    protected by a WITH_OTLP_RETRY_PREVIEW
    flag in CMake.

## [1.18.0] 2024-11-25

* [EXPORTER] Fix crash in ElasticsearchLogRecordExporter
  [#3082](https://github.com/open-telemetry/opentelemetry-cpp/pull/3082)

* [BUILD] Avoid buggy warning with gcc <= 8
  [#3087](https://github.com/open-telemetry/opentelemetry-cpp/pull/3087)

* [API] Jaeger Propagator should not be deprecated
  [#3086](https://github.com/open-telemetry/opentelemetry-cpp/pull/3086)

* Update bzlmod version
  [#3093](https://github.com/open-telemetry/opentelemetry-cpp/pull/3093)

* [BUILD] Remove std::make_unique
  [#3098](https://github.com/open-telemetry/opentelemetry-cpp/pull/3098)

* [BUILD] Fix compiling problems for gcc 4.8
  [#3100](https://github.com/open-telemetry/opentelemetry-cpp/pull/3100)

* [TEST] Fix linking order and gmock linking
  [#3106](https://github.com/open-telemetry/opentelemetry-cpp/pull/3106)

* [EXPORTER] Add config options to prometheus exporter
  [#3104](https://github.com/open-telemetry/opentelemetry-cpp/pull/3104)

* [BUILD] Add a CMake option to disable shared libs
  [#3095](https://github.com/open-telemetry/opentelemetry-cpp/pull/3095)

* [EXPORTER] Remove out of date ETW exporter doc
  [#3103](https://github.com/open-telemetry/opentelemetry-cpp/pull/3103)

* [EXPORTER] Add logging for async gRPC errors
  [#3108](https://github.com/open-telemetry/opentelemetry-cpp/pull/3108)

* [BUILD] Remove aligned_storage from nostd
  [#3112](https://github.com/open-telemetry/opentelemetry-cpp/pull/3112)

* [EXPORTER] Elastic Search exporter follow ECS guidelines
  [#3107](https://github.com/open-telemetry/opentelemetry-cpp/pull/3107)

* [INSTALL] Resolve dependencies in opentelemetry-cpp-config.cmake
  [#3094](https://github.com/open-telemetry/opentelemetry-cpp/pull/3094)

* [API] Add synchronous gauge
  [#3029](https://github.com/open-telemetry/opentelemetry-cpp/pull/3029)

* [BUILD] allow building with -DWITH_OTLP_HTTP_COMPRESSION=OFF without zlib
  [#3120](https://github.com/open-telemetry/opentelemetry-cpp/pull/3120)

* [CI] Comment the arm64 CI
  [#3125](https://github.com/open-telemetry/opentelemetry-cpp/pull/3125)

* [API] Comply with W3C Trace Context
  [#3115](https://github.com/open-telemetry/opentelemetry-cpp/pull/3115)

* [EXPORTER] bump prometheus to v1.3.0
  [#3122](https://github.com/open-telemetry/opentelemetry-cpp/pull/3122)

* [EXPORTER] Log SSL Connection Information
  [#3113](https://github.com/open-telemetry/opentelemetry-cpp/pull/3113)

* [BUILD] Improve how to handle yield() in ARM
  [#3129](https://github.com/open-telemetry/opentelemetry-cpp/pull/3129)

* [BUILD] Fix -Wmissing-template-arg-list-after-template-kw warning
  [#3133](https://github.com/open-telemetry/opentelemetry-cpp/pull/3133)

* [EXPORTER]: Elasticsearch exporter put log resource in root instead of under 'resources'
  [#3131](https://github.com/open-telemetry/opentelemetry-cpp/pull/3131)

* [TEST] Rename w3c_tracecontext_test to w3c_tracecontext_http_test_server
  [#3132](https://github.com/open-telemetry/opentelemetry-cpp/pull/3132)

* [BUILD] Patches for building on AIX
  [#3127](https://github.com/open-telemetry/opentelemetry-cpp/pull/3127)

* [SEMANTIC CONVENTIONS] Migration to weaver
  [#3105](https://github.com/open-telemetry/opentelemetry-cpp/pull/3105)

* [SEMANTIC CONVENTIONS] Upgrade to semantic conventions 1.28.0
  [#3139](https://github.com/open-telemetry/opentelemetry-cpp/pull/3139)

* [EXPORTER] handling of invalid ports in UrlParser
  [#3142](https://github.com/open-telemetry/opentelemetry-cpp/pull/3142)

* [CI] speed up clang-tidy workflow
  [#3148](https://github.com/open-telemetry/opentelemetry-cpp/pull/3148)

* [EXPORTER] Allow to share gRPC clients between OTLP exporters
  [#3041](https://github.com/open-telemetry/opentelemetry-cpp/pull/3041)

* Bump codecov/codecov-action from 4 to 5
  [#3143](https://github.com/open-telemetry/opentelemetry-cpp/pull/3143)

* [CI] Add cppcheck in the build
  [#3151](https://github.com/open-telemetry/opentelemetry-cpp/pull/3151)

* [BUILD] Fix error message
  [#3152](https://github.com/open-telemetry/opentelemetry-cpp/pull/3152)

* [EXPORTER] fix clang-tidy warnings in UrlParser
  [#3146](https://github.com/open-telemetry/opentelemetry-cpp/pull/3146)

* [EXPORTER] Upgrade to opentelemetry-proto 1.4.0
  [#3157](https://github.com/open-telemetry/opentelemetry-cpp/pull/3157)

* [TEST] refactor UrlParser tests to use value-paramterized tests
  [#3153](https://github.com/open-telemetry/opentelemetry-cpp/pull/3153)

* [TEST] add a test for ElasticSearchRecordable
  [#3154](https://github.com/open-telemetry/opentelemetry-cpp/pull/3154)

* [BUILD] Fix missing dependency on protoc compiler
  [#3159](https://github.com/open-telemetry/opentelemetry-cpp/pull/3159)

* [bazel] Update prometheus-cpp in MODULE.bazel
  [#3162](https://github.com/open-telemetry/opentelemetry-cpp/pull/3162)

* [bazel] Enable --incompatible_disallow_empty_glob
  [#2642](https://github.com/open-telemetry/opentelemetry-cpp/pull/2642)

* [INSTALL] Fix cmake/opentelemetry-cpp-config.cmake.in
  [#3165](https://github.com/open-telemetry/opentelemetry-cpp/pull/3165)

* [BUILD] Do not set OTELCPP_PROTO_PATH in the CMake cache
  [#3160](https://github.com/open-telemetry/opentelemetry-cpp/pull/3160)

* [BUILD] Fix build for esp32
  [#3155](https://github.com/open-telemetry/opentelemetry-cpp/pull/3155)

* [bazel] Update opentelemetry-proto in MODULE.bazel
  [#3163](https://github.com/open-telemetry/opentelemetry-cpp/pull/3163)

Important changes:

* [API] Jaeger Propagator should not be deprecated
  [#3086](https://github.com/open-telemetry/opentelemetry-cpp/pull/3086)

  * Deprecation of the Jaeger propagator, as announced on 2023-01-31
    in version 1.8.2, is now reverted.
  * This deprecation turned out to be not justified,
    as the Jaeger propagator can be used without the (now removed)
    Jaeger exporter.

* [EXPORTER] Change log resources location for ElasticsearchLogRecordExporter
  [#3119](https://github.com/open-telemetry/opentelemetry-cpp/pull/3131)

  * Moved from `root/resources` to `root`

* [SEMANTIC CONVENTIONS] Migration to weaver
  [#3105](https://github.com/open-telemetry/opentelemetry-cpp/pull/3105)

  * `semantic_convention.h` header files are deprecated,
    replaced by `semconv/xxx_attributes.h` header files,
    for each `xxx` semantic attribute group.
  * See file DEPRECATED.md for details.

Deprecations:

* This release contains deprecations, see file DEPRECATED.md for details.

## [1.17.0] 2024-10-07

* [CI] Add a clang-tidy build
  [#3001](https://github.com/open-telemetry/opentelemetry-cpp/pull/3001)

* [BUILD] Upgrade to opentelemetry-proto 1.3.2
  [#2991](https://github.com/open-telemetry/opentelemetry-cpp/pull/2991)

* [REMOVAL] Remove build option `WITH_DEPRECATED_SDK_FACTORY`
  [#2717](https://github.com/open-telemetry/opentelemetry-cpp/pull/2717)

* [EXPORTER] ForceFlush before canceling the running requests on shutdown
  [#2727](https://github.com/open-telemetry/opentelemetry-cpp/pull/2727)

* [SDK] Fix crash in PeriodicExportingMetricReader
  [#2983](https://github.com/open-telemetry/opentelemetry-cpp/pull/2983)

* [SDK] Fix memory leak in TlsRandomNumberGenerator() constructor
  [#2661](https://github.com/open-telemetry/opentelemetry-cpp/pull/2661)

* [EXPORTER] Ignore exception when create thread in OTLP file exporter
  [#3012](https://github.com/open-telemetry/opentelemetry-cpp/pull/3012)

* [BUILD] Update the version in MODULE.bazel
  [#3015](https://github.com/open-telemetry/opentelemetry-cpp/pull/3015)

* [BUILD] Fix build without vcpkg on Windows when gRPC is disabled
  [#3016](https://github.com/open-telemetry/opentelemetry-cpp/pull/3016)

* [BUILD] Add abi_version_no bazel flag
  [#3020](https://github.com/open-telemetry/opentelemetry-cpp/pull/3020)

* [Code health] Expand iwyu coverage to include unit tests
  [#3022](https://github.com/open-telemetry/opentelemetry-cpp/pull/3022)

* [BUILD] Version opentelemetry_proto/proto_grpc shared libraries
  [#2992](https://github.com/open-telemetry/opentelemetry-cpp/pull/2992)

* [SEMANTIC CONVENTIONS] Upgrade semantic conventions to 1.27.0
  [#3023](https://github.com/open-telemetry/opentelemetry-cpp/pull/3023)

* [SDK] Support empty histogram buckets
  [#3027](https://github.com/open-telemetry/opentelemetry-cpp/pull/3027)

* [TEST] Fix sync problems in OTLP File exporter tests
  [#3031](https://github.com/open-telemetry/opentelemetry-cpp/pull/3031)

* [SDK] PeriodicExportingMetricReader: future is never set, blocks until timeout
  [#3030](https://github.com/open-telemetry/opentelemetry-cpp/pull/3030)

* [Code Health] Clang Tidy cleanup, Part 2
  [#3038](https://github.com/open-telemetry/opentelemetry-cpp/pull/3038)

* [Code Health] include-what-you-use cleanup, part 3
  [#3004](https://github.com/open-telemetry/opentelemetry-cpp/pull/3004)

* [SDK] Fix overflow in timeout logic
  [#3046](https://github.com/open-telemetry/opentelemetry-cpp/pull/3046)

* [TEST] Add missing tests to Bazel build
  [#3045](https://github.com/open-telemetry/opentelemetry-cpp/pull/3045)

* [TEST] update collector tests with debug exporter
  [#3050](https://github.com/open-telemetry/opentelemetry-cpp/pull/3050)

* [EXAMPLE] update collector example with debug exporter
  [#3049](https://github.com/open-telemetry/opentelemetry-cpp/pull/3049)

* [TEST] update references to logging exporter
  [#3053](https://github.com/open-telemetry/opentelemetry-cpp/pull/3053)

* [EXAMPLE] Clean the tracer initialization in OStream example
  [#3051](https://github.com/open-telemetry/opentelemetry-cpp/pull/3051)

* [EXPORTER] Fix the format of SpanLink for ETW
  [#3054](https://github.com/open-telemetry/opentelemetry-cpp/pull/3054)

* [EXPORTER] Add in-memory metric exporter
  [#3043](https://github.com/open-telemetry/opentelemetry-cpp/pull/3043)

* [Code Health] include-what-you-use cleanup, part 4
  [#3040](https://github.com/open-telemetry/opentelemetry-cpp/pull/3040)

* [BUILD] add loongarch info
  [#3052](https://github.com/open-telemetry/opentelemetry-cpp/pull/3052)

* [CI] Update otel-collector version
  [#3067](https://github.com/open-telemetry/opentelemetry-cpp/pull/3067)

* [SDK] Update MetricProducer interface to match spec
  [#3044](https://github.com/open-telemetry/opentelemetry-cpp/pull/3044)

* [EXPORTER] Fix URL in ES exporter, fix ipv6 supporting for http client
  [#3081](https://github.com/open-telemetry/opentelemetry-cpp/pull/3081)

* [EXPORTER] Add HttpHeaders in ElasticsearchLogRecordExporter
  [#3083](https://github.com/open-telemetry/opentelemetry-cpp/pull/3083)

Breaking changes:

* [REMOVAL] Remove build option `WITH_DEPRECATED_SDK_FACTORY`
  [#2717](https://github.com/open-telemetry/opentelemetry-cpp/pull/2717)

  * As announced in opentelemetry-cpp previous release 1.16.0,
    CMake option `WITH_DEPRECATED_SDK_FACTORY` was temporary,
    and to be removed by the next release.
  * This option is now removed.
  * Code configuring the SDK must be adjusted, as previously described:

    * [API/SDK] Provider cleanup
      [#2664](https://github.com/open-telemetry/opentelemetry-cpp/pull/2664)

    * Before this fix:
      * SDK factory methods such as:
        * opentelemetry::sdk::trace::TracerProviderFactory::Create()
        * opentelemetry::sdk::metrics::MeterProviderFactory::Create()
        * opentelemetry::sdk::logs::LoggerProviderFactory::Create()
        * opentelemetry::sdk::logs::EventLoggerProviderFactory::Create()

        returned an API object (opentelemetry::trace::TracerProvider)
          to the caller.

    * After this fix, these methods return an SDK level object
      (opentelemetry::sdk::trace::TracerProvider) to the caller.
    * Returning an SDK object is necessary for the application to
      cleanup and invoke SDK level methods, such as ForceFlush(),
      on a provider.
    * The application code that configures the SDK, by calling
      the various provider factories, may need adjustment.
    * All the examples have been updated, and in particular no
      longer perform static_cast do convert an API object to an SDK object.
      Please refer to examples for guidance on how to adjust.

## [1.16.1] 2024-07-17

* [BUILD] Add bazel missing BUILD file
  [#2720](https://github.com/open-telemetry/opentelemetry-cpp/pull/2720)

* [SDK] Added reserve for spans array in BatchSpanProcessor.
  [#2724](https://github.com/open-telemetry/opentelemetry-cpp/pull/2724)

* [DOC] Update "Using triplets" section in building-with-vcpkg documentation.
  [#2726](https://github.com/open-telemetry/opentelemetry-cpp/pull/2726)

* [DOC] Remove comment for unused LoggerProvider initialization params
  [#2972](https://github.com/open-telemetry/opentelemetry-cpp/pull/2972)

* [SECURITY] Remove OTLP HTTP support for TLS 1.0 and TLS 1.1,
  require TLS 1.2 or better
  [#2722](https://github.com/open-telemetry/opentelemetry-cpp/pull/2722)

* [TEST] Fix opentelemetry-collector bind address
  [#2989](https://github.com/open-telemetry/opentelemetry-cpp/pull/2989)

* [EXPORTER] Fix references in AttributeValueVisitor
  [#2985](https://github.com/open-telemetry/opentelemetry-cpp/pull/2985)

* [Code health] include-what-you-use cleanup, part 2
  [#2704](https://github.com/open-telemetry/opentelemetry-cpp/pull/2704)

* [Code Health] clang-tidy cleanup, part 1
  [#2990](https://github.com/open-telemetry/opentelemetry-cpp/pull/2990)

* [CI] Build failures with ABSEIL 20240116 and CMAKE 3.30
  [#3002](https://github.com/open-telemetry/opentelemetry-cpp/pull/3002)

* [CI] Enable bzlmod
  [#2995](https://github.com/open-telemetry/opentelemetry-cpp/pull/2995)

* [Metrics SDK] Fix hash calculation for nostd::string
  [#2999](https://github.com/open-telemetry/opentelemetry-cpp/pull/2999)

Breaking changes:

* [SECURITY] Remove OTLP HTTP support for TLS 1.0 and TLS 1.1,
  require TLS 1.2 or better
  [#2722](https://github.com/open-telemetry/opentelemetry-cpp/pull/2722)
  * The OTLP HTTP exporter no longer accept options like:
    * min_TLS = 1.0
    * min_TLS = 1.1
    * max_TLS = 1.0
    * max_TLS = 1.1
  * When connecting to an OTLP HTTP endpoint, using `https`,
    the connection will require TLS 1.2 by default,
    unless min_TLS is set to 1.3
  * Plain `http` connections (insecure) are not affected.

## [1.16.0] 2024-06-21

* [BUILD] Upgrade bazel abseil from 20220623.1 to 20230802.2
  [#2650](https://github.com/open-telemetry/opentelemetry-cpp/pull/2650)
* [BUILD] Use nostd::enable_if_t instead of std::enable_if_t
  [#2648](https://github.com/open-telemetry/opentelemetry-cpp/pull/2648)
* [EXEMPLAR] Update ExemplarFilter and ExemplarReservoir for spec
  [#2372](https://github.com/open-telemetry/opentelemetry-cpp/pull/2372)
* [BUILD] Link CoreFoundation on apple systems
  [#2655](https://github.com/open-telemetry/opentelemetry-cpp/pull/2655)
* [SDK] Avoid missing conditional variable update and simplify atomic bool
  [#2553](https://github.com/open-telemetry/opentelemetry-cpp/pull/2553)
* [BUILD] Build break in OLTP_FILE tests
  [#2659](https://github.com/open-telemetry/opentelemetry-cpp/pull/2659)
* [EXPORTER] General cleanup for is_shutdown flags in exporters.
  [#2663](https://github.com/open-telemetry/opentelemetry-cpp/pull/2663)
* [CI] Upgrade Maintainers CI to ubuntu-24.04
  [#2670](https://github.com/open-telemetry/opentelemetry-cpp/pull/2670)
* [BUILD] Upgrade to opentelemetry-proto 1.3.1
  [#2669](https://github.com/open-telemetry/opentelemetry-cpp/pull/2669)
* [API] Return NoopLogRecord from NoopLogger
  [#2668](https://github.com/open-telemetry/opentelemetry-cpp/pull/2668)
* [BUILD] Remove the hard-coded separator in tracestate
  [#2672](https://github.com/open-telemetry/opentelemetry-cpp/pull/2672)
* [SDK] Fix forceflush may wait for ever
  [#2584](https://github.com/open-telemetry/opentelemetry-cpp/pull/2584)
* [API] DO not allow unsafe Logger::EmitLogRecord
  [#2673](https://github.com/open-telemetry/opentelemetry-cpp/pull/2673)
* [BUILD] Read default proto version from third_party_release
  [#2677](https://github.com/open-telemetry/opentelemetry-cpp/pull/2677)
* [CI] include-what-you-use
  [#2629](https://github.com/open-telemetry/opentelemetry-cpp/pull/2629)
* [CI] Upgrade to clang-format 18
  [#2684](https://github.com/open-telemetry/opentelemetry-cpp/pull/2684)
* [CI] Fix CI failures on Ubuntu 24.04
  [#2686](https://github.com/open-telemetry/opentelemetry-cpp/pull/2686)
* [SEMANTIC CONVENTIONS] Upgrade to version 1.26.0
  [#2687](https://github.com/open-telemetry/opentelemetry-cpp/pull/2687)
* [API/SDK] Provider cleanup
  [#2664](https://github.com/open-telemetry/opentelemetry-cpp/pull/2664)
* [ETW] Add table name mapping for Logs other than the default Log table
  [#2691](https://github.com/open-telemetry/opentelemetry-cpp/pull/2691)
* [CI] Remove benchmark overlay for vcpkg
  [#2695](https://github.com/open-telemetry/opentelemetry-cpp/pull/2695)
* [BUILD] Remove the incorrect set of CMAKE_MSVC_RUNTIME_LIBRARY for vcpkg
  [#2696](https://github.com/open-telemetry/opentelemetry-cpp/pull/2696)
* [BUILD] CMakeLists.txt: Enable CMAKE_MSVC_RUNTIME_LIBRARY support
  [#2652](https://github.com/open-telemetry/opentelemetry-cpp/pull/2652)
* [EXPORTER] OTLP file: use thread-safe file/io
  [#2675](https://github.com/open-telemetry/opentelemetry-cpp/pull/2675)
* [bazel] Bump version and deps
  [#2679](https://github.com/open-telemetry/opentelemetry-cpp/pull/2679)
* [BUILD] Add support for bzlmod
  [#2608](https://github.com/open-telemetry/opentelemetry-cpp/pull/2608)
* [BUILD] Fix Import Abseil-cpp
  [#2701](https://github.com/open-telemetry/opentelemetry-cpp/pull/2701)
* [Code health] include-what-you-use cleanup
  [#2692](https://github.com/open-telemetry/opentelemetry-cpp/pull/2692)
* [BUILD] Restore Bazel flag removed from public API
  [#2702](https://github.com/open-telemetry/opentelemetry-cpp/pull/2702)
* [DOC] Fix typo tace_id -> trace_id in logger.h
  [#2703](https://github.com/open-telemetry/opentelemetry-cpp/pull/2703)
* Bump docker/build-push-action from 5 to 6
  [#2705](https://github.com/open-telemetry/opentelemetry-cpp/pull/2705)
* [CI] Enable ARM64 build in CI
  [#2699](https://github.com/open-telemetry/opentelemetry-cpp/pull/2699)
* [Code health] Remove Unicode Text from Source files
  [#2707](https://github.com/open-telemetry/opentelemetry-cpp/pull/2707)
* [BUILD] Add option WITH_OTLP_GRPC_SSL_MTLS_PREVIEW
  [#2714](https://github.com/open-telemetry/opentelemetry-cpp/pull/2714)
* [EXPORTER] All 2xx return codes should be considered successful.
  [#2712](https://github.com/open-telemetry/opentelemetry-cpp/pull/2712)

Important changes:

* [API/SDK] Provider cleanup
  [#2664](https://github.com/open-telemetry/opentelemetry-cpp/pull/2664)
  * Before this fix:
    * The API class `opentelemetry::trace::Tracer` exposed methods such
      as `ForceFlush()`, `ForceFlushWithMicroseconds()`, `Close()`
      and `CloseWithMicroseconds()`.
    * These methods are meant to be used when configuring the SDK,
      and should not be part of the API. Exposing them was an oversight.
    * Two of these methods are virtual, and therefore part of the ABI.
  * After this fix:
    * In `OPENTELEMETRY_ABI_VERSION_NO 1`, nothing is changed,
      because removing this code would break the ABI.
    * In `OPENTELEMETRY_ABI_VERSION_NO 2`, these methods are moved
      from the API to the SDK. This is a breaking change for ABI version 2,
      which is still experimental.
  * In all cases, instrumenting an application should not
    invoke flush or close on a tracer, do not use these methods.

Breaking changes:

* [API/SDK] Provider cleanup
  [#2664](https://github.com/open-telemetry/opentelemetry-cpp/pull/2664)
  * Before this fix:
    * SDK factory methods such as:
      * opentelemetry::sdk::trace::TracerProviderFactory::Create()
      * opentelemetry::sdk::metrics::MeterProviderFactory::Create()
      * opentelemetry::sdk::logs::LoggerProviderFactory::Create()
      * opentelemetry::sdk::logs::EventLoggerProviderFactory::Create()
      returned an API object (opentelemetry::trace::TracerProvider)
      to the caller.
  * After this fix, these methods return an SDK level object
    (opentelemetry::sdk::trace::TracerProvider) to the caller.
  * Returning an SDK object is necessary for the application to
    cleanup and invoke SDK level methods, such as ForceFlush(),
    on a provider.
  * The application code that configures the SDK, by calling
    the various provider factories, may need adjustment.
  * All the examples have been updated, and in particular no
    longer perform static_cast do convert an API object to an SDK object.
    Please refer to examples for guidance on how to adjust.
  * If adjusting application code is impractical,
    an alternate and temporary solution is to build with option
    WITH_DEPRECATED_SDK_FACTORY=ON in CMake.
  * Option WITH_DEPRECATED_SDK_FACTORY=ON will allow to build code
    without application changes, posponing changes for later.
  * WITH_DEPRECATED_SDK_FACTORY=ON is temporary, only to provide
    an easier migration path. Expect this flag to be removed,
    as early as by the next release.

Notes on experimental features:

* [#2372](https://github.com/open-telemetry/opentelemetry-cpp/issues/2372)
  introduced `MeterProvider::SetExemplar()` which accepts en
  `ExemplarFilterType` enumeration with `kAlwaysOff`, `kAlwaysOn` and
  `kTraceBased`.

## [1.15.0] 2024-04-21

* [EXPORTER] Change OTLP HTTP content_type default to binary
  [#2564](https://github.com/open-telemetry/opentelemetry-cpp/pull/2564)
* [DOC] Fix OTLP documentation: Default endpoint is wrong for OTLP/HTTP
  [#2560](https://github.com/open-telemetry/opentelemetry-cpp/pull/2560)
* [BUILD] Fix old style cast warning
  [#2567](https://github.com/open-telemetry/opentelemetry-cpp/pull/2567)
* [EXPORTER] Gzip compression support for OTLP/HTTP and OTLP/gRPC exporter
  [#2530](https://github.com/open-telemetry/opentelemetry-cpp/pull/2530)
* [BUILD] update vcpkg submodule to 2024.02.14
  [#2575](https://github.com/open-telemetry/opentelemetry-cpp/pull/2575)
* [SDK] Support for OTEL_SERVICE_NAME
  [#2577](https://github.com/open-telemetry/opentelemetry-cpp/pull/2577)
* [EXPORTER] Support URL-encoded values for `OTEL_EXPORTER_OTLP_HEADERS`
  [#2579](https://github.com/open-telemetry/opentelemetry-cpp/pull/2579)
* [BUILD] CMake cleanup for message()
  [#2582](https://github.com/open-telemetry/opentelemetry-cpp/pull/2582)
* [BUILD] Bump CMake minimum required version to 3.9
  [#2581](https://github.com/open-telemetry/opentelemetry-cpp/pull/2581)
* [BUILD] Provide LIKELY / UNLIKELY macros
  [#2580](https://github.com/open-telemetry/opentelemetry-cpp/pull/2580)
* [EXPORTER] OTLP: Fix missing ResourceMetrics SchemaURL
  [#2587](https://github.com/open-telemetry/opentelemetry-cpp/pull/2587)
* [ETW] cleanup include path
  [#2594](https://github.com/open-telemetry/opentelemetry-cpp/pull/2594)
* Upgrade to googletest 1.14.0
  [#2596](https://github.com/open-telemetry/opentelemetry-cpp/pull/2596)
* Upgrade to nlohmann_json 3.11.3
  [#2595](https://github.com/open-telemetry/opentelemetry-cpp/pull/2595)
* [BAZEL] Move -std=c++14 to .bazelrc
  [#2600](https://github.com/open-telemetry/opentelemetry-cpp/pull/2600)
* [BAZEL] Fix -std=c++14 warning on Windows
  [#2601](https://github.com/open-telemetry/opentelemetry-cpp/pull/2601)
* Upgrade to benchmark 1.8.3
  [#2597](https://github.com/open-telemetry/opentelemetry-cpp/pull/2597)
* Upgrade to prometheus 1.2.4
  [#2598](https://github.com/open-telemetry/opentelemetry-cpp/pull/2598)
* [DOC] Fix typo: Asynchronouse -> Asynchronous in meter.h
  [#2604](https://github.com/open-telemetry/opentelemetry-cpp/pull/2604)
* [BUILD] Do not link prometheus-cpp::util when it doesn't exist
  [#2606](https://github.com/open-telemetry/opentelemetry-cpp/pull/2606)
* [SDK] Remove unused variable
  [#2609](https://github.com/open-telemetry/opentelemetry-cpp/pull/2609)
* [METRICS SDK] Remove extra OfferMeasurement call
  in SyncMetricsStorage::OfferMeasurement
  [#2610](https://github.com/open-telemetry/opentelemetry-cpp/pull/2610)
* [MISC] Use set -e on all shell scripts and pass shellcheck --severity=error
  [#2616](https://github.com/open-telemetry/opentelemetry-cpp/pull/2616)
* [CI] Add shellcheck --severity=error as a CI step
  [#2618](https://github.com/open-telemetry/opentelemetry-cpp/pull/2618)
* [CI] Upgrade to abseil 20240116.1 (CMake only)
  [#2599](https://github.com/open-telemetry/opentelemetry-cpp/pull/2599)
* [CI] Benchmark, provide units with --benchmark_min_time
  [#2621](https://github.com/open-telemetry/opentelemetry-cpp/pull/2621)
* [EXPORTER] OTLP file exporter
  [#2540](https://github.com/open-telemetry/opentelemetry-cpp/pull/2540)
* [CI] Use platform CMake
  [#2627](https://github.com/open-telemetry/opentelemetry-cpp/pull/2627)
* [PROTO] Upgrade to opentelemetry-proto 1.2.0
  [#2631](https://github.com/open-telemetry/opentelemetry-cpp/pull/2631)
* [SDK] DefaultLogHandler to print errors to std::cerr, add LogLevel::None
  [#2622](https://github.com/open-telemetry/opentelemetry-cpp/pull/2622)
* [SEMANTIC CONVENTIONS] Upgrade to semantic convention 1.25.0
  [#2633](https://github.com/open-telemetry/opentelemetry-cpp/pull/2633)
* [DOC] Add readme and examples for OTLP FILE exporters.
  [#2638](https://github.com/open-telemetry/opentelemetry-cpp/pull/2638)
* [SEMANTIC CONVENTIONS] Rework on semantic conventions 1.25.0
  [#2640](https://github.com/open-telemetry/opentelemetry-cpp/pull/2640)
* [DOC] Update INSTALL.md
  [#2592](https://github.com/open-telemetry/opentelemetry-cpp/pull/2592)

Important changes:

* [EXPORTER] Gzip compression support for OTLP/HTTP and OTLP/gRPC exporter
  [#2530](https://github.com/open-telemetry/opentelemetry-cpp/pull/2530)
  * In the `OtlpHttpExporterOptions` and `OtlpGrpcExporterOptions`, a new
    field called compression has been introduced. This field can be set
    to "gzip” to enable gzip compression.
  * The CMake option `WITH_OTLP_HTTP_COMPRESSION` is introduced to enable
    gzip compression support for the OTLP HTTP Exporter and includes a
    dependency on zlib.
* [SDK] Change OTLP HTTP content_type default to binary
  [#2558](https://github.com/open-telemetry/opentelemetry-cpp/pull/2558)
* [CI] Use platform CMake
  [#2627](https://github.com/open-telemetry/opentelemetry-cpp/pull/2627)
  * The `CI` in github no longer install a different version of `cmake`.
  * It now always use the `cmake` provided by the platform.
  * As part of this change, the script `ci/setup_cmake.sh` was renamed
    to `ci/setup_googletest.sh`, for clarity, now that this script
    only installs googletest.
* [SDK] DefaultLogHandler to print to std::cerr, add LogLevel::None
  [#2622](https://github.com/open-telemetry/opentelemetry-cpp/pull/2622)
  * Change DefaultLogHandler output
    * Before, the default internal logger, DefaultLogHandler,
      used to print to std::cout.
    * Now, DefaultLogHandler prints errors and warnings to std::cerr,
      as expected, while printing info and debug messages to std::cout.
    * Applications that expected to find the opentelemetry-cpp internal
      error log in std::cout may need adjustments, either by looking
      at std::cerr instead, or by using a custom log handler.
  * Additional LogLevel::None
    * LogLevel::None is a new supported log level, which does not print
      any message.
    * Custom log handlers may need to implement a new case, to avoid
      compiler warnings.
    * Numbering of log levels like OTEL_INTERNAL_LOG_LEVEL_ERROR
      has changed, which requires to rebuild, as the SDK ABI differs.

## [1.14.2] 2024-02-27

* [SDK] Fix observable attributes drop
   [#2557](https://github.com/open-telemetry/opentelemetry-cpp/pull/2557)

## [1.14.1] 2024-02-23

* [SDK] Restore Recordable API compatibility with versions < 1.14.0
  [#2547](https://github.com/open-telemetry/opentelemetry-cpp/pull/2547)
* [DOC] Add missing CHANGELOG.
  [#2549](https://github.com/open-telemetry/opentelemetry-cpp/pull/2549)
* [EXPORTER] Error when grpc endpoint is empty
  [#2507](https://github.com/open-telemetry/opentelemetry-cpp/pull/2507)
* [DOC] Fix typo in benchmarks.rst
  [#2542](https://github.com/open-telemetry/opentelemetry-cpp/pull/2542)

Important changes:

* [SDK] Restore Recordable API compatibility with versions < 1.14.0
  [#2547](https://github.com/open-telemetry/opentelemetry-cpp/pull/2547)
  * For third party _extending_ the SDK, release 1.14.0 introduced
    an API breaking change compared to 1.13.0
  * This fix restores API (but not ABI) compatibility of
    release 1.14.1 with release 1.13.0.
  * This allows to build a third party exporter with no source code changes,
    for both releases 1.14.1 and 1.13.0.

## [1.14.0] 2024-02-16

* [BUILD] Add DLL build CI pipeline with CXX20
  [#2465](https://github.com/open-telemetry/opentelemetry-cpp/pull/2465)
* [EXPORTER] Set `is_monotonic` flag for Observable Counters
  [#2478](https://github.com/open-telemetry/opentelemetry-cpp/pull/2478)
* [PROTO] Upgrade to opentelemetry-proto v1.1.0
  [#2488](https://github.com/open-telemetry/opentelemetry-cpp/pull/2488)
* [BUILD] Introduce CXX 20 CI pipeline for MSVC/Windows
  [#2450](https://github.com/open-telemetry/opentelemetry-cpp/pull/2450)
* [API] Propagation: fix for hex conversion to binary for odd hex strings
  [#2533](https://github.com/open-telemetry/opentelemetry-cpp/pull/2533)
* [DOC] Fix calendar link
  [#2532](https://github.com/open-telemetry/opentelemetry-cpp/pull/2532)
* [ETW EXPORTER] Remove namespace using in ETW exporter which affects global
  namespace
  [#2531](https://github.com/open-telemetry/opentelemetry-cpp/pull/2531)
* [BUILD] Don't invoke vcpkg from this repo with CMAKE_TOOLCHAIN_FILE set
  [#2527](https://github.com/open-telemetry/opentelemetry-cpp/pull/2527)
* [EXPORTER] Async exporting for otlp grpc
  [#2407](https://github.com/open-telemetry/opentelemetry-cpp/pull/2407)
* [METRICS SDK] Fix attribute filtering for synchronous instruments.
  [#2472](https://github.com/open-telemetry/opentelemetry-cpp/pull/2472)
* [BUILD] Better handling of OPENTELEMETRY_STL_VERSION under Bazel.
  [#2503](https://github.com/open-telemetry/opentelemetry-cpp/pull/2503)
* [DOC] Fixes CI markdown error MD055 - Table pipe style
  [#2517](https://github.com/open-telemetry/opentelemetry-cpp/pull/2517)
* [API] Propagators: do not overwrite the active span with a default invalid
  span [#2511](https://github.com/open-telemetry/opentelemetry-cpp/pull/2511)
* [BUILD] Updated the recorded vcpkg submodule version
  [#2513](https://github.com/open-telemetry/opentelemetry-cpp/pull/2513)
* [BUILD] Remove unnecessary usage/includes of nostd/type_traits
  [#2509](https://github.com/open-telemetry/opentelemetry-cpp/pull/2509)
* [SEMANTIC CONVENTIONS] Upgrade to version 1.24.0
  [#2461](https://github.com/open-telemetry/opentelemetry-cpp/pull/2461)
* [EXAMPLES] Improve readme of Metrics example
  [#2510](https://github.com/open-telemetry/opentelemetry-cpp/pull/2510)
* [BUILD] Clang-15 warning about `__has_trivial_destructor`
  [#2502](https://github.com/open-telemetry/opentelemetry-cpp/pull/2502)
* [BUILD] Fix multiple assignment operators for SpinLockMutex
  [#2501](https://github.com/open-telemetry/opentelemetry-cpp/pull/2501)
* [BUILD] Alternative way of exporting symbols (generating .def file)
  [#2476](https://github.com/open-telemetry/opentelemetry-cpp/pull/2476)
* [CI] Make cmake.c++20*.test actually use C++20 and add cmake.c++23*.test
  [#2496](https://github.com/open-telemetry/opentelemetry-cpp/pull/2496)
* [DOCUMENTATION] Add api reference documentation for logs
  [#2497](https://github.com/open-telemetry/opentelemetry-cpp/pull/2497)
* [BUILD] Fix variable shadow
  [#2498](https://github.com/open-telemetry/opentelemetry-cpp/pull/2498)
* [BUILD] Fix checks on __cplusplus under MSVC, do not assume /Zc
  [#2493](https://github.com/open-telemetry/opentelemetry-cpp/pull/2493)
* [EXAMPLES] Use logs API instead of logs bridge API in the example
  [#2494](https://github.com/open-telemetry/opentelemetry-cpp/pull/2494)
* [EXPORTER] Fix forward protocol encoding for ETW exporter
  [#2473](https://github.com/open-telemetry/opentelemetry-cpp/pull/2473)
* [BUILD] Skip patch alias target
  [#2457](https://github.com/open-telemetry/opentelemetry-cpp/pull/2457)
* [EXPORTER] Rename populate_otel_scope to without_otel_scope
  [#2479](https://github.com/open-telemetry/opentelemetry-cpp/pull/2479)
* [EXPORTER SDK] Additional fixes after NOMINMAX removal on Windows
  [#2475](https://github.com/open-telemetry/opentelemetry-cpp/pull/2475)
* [EXPORTER] Do not use regex in `CleanUpString` because some implementations of
  STL may crash.
  [#2464](https://github.com/open-telemetry/opentelemetry-cpp/pull/2464)
* [EXPORTER] Fix Aggregation type detection in OTLP Exporter
  [#2467](https://github.com/open-telemetry/opentelemetry-cpp/pull/2467)
* [EXPORTER] Add option to disable Prometheus otel_scope_name and
  otel_scope_version attributes
  [#2451](https://github.com/open-telemetry/opentelemetry-cpp/pull/2451)
* [SEMANTIC CONVENTIONS] Code generation script fails on SELINUX
  [#2455](https://github.com/open-telemetry/opentelemetry-cpp/pull/2455)
* [BUILD] Fix removing of NOMINMAX on Windows
  [#2449](https://github.com/open-telemetry/opentelemetry-cpp/pull/2449)
* [BUILD] Accept path list in OPENTELEMETRY_EXTERNAL_COMPONENT_PATH
  [#2439](https://github.com/open-telemetry/opentelemetry-cpp/pull/2439)
* [BUILD] Remove gmock from GTEST_BOTH_LIBRARIES
  [#2437](https://github.com/open-telemetry/opentelemetry-cpp/pull/2437)
* [REMOVAL] Remove option WITH_OTLP_HTTP_SSL_PREVIEW
  [#2435](https://github.com/open-telemetry/opentelemetry-cpp/pull/2435)

Important changes:

Breaking changes:

* [REMOVAL] Remove option WITH_OTLP_HTTP_SSL_PREVIEW
  [#2435](https://github.com/open-telemetry/opentelemetry-cpp/pull/2435)
  * CMake options `WITH_OTLP_HTTP_SSL_PREVIEW` and
    `WITH_OTLP_HTTP_SSL_TLS_PREVIEW` are removed. Building opentelemetry-cpp
    without SSL support is no longer possible.

* [PROTO] Upgrade to opentelemetry-proto v1.1.0
  [#2488](https://github.com/open-telemetry/opentelemetry-cpp/pull/2488)
  * Class `opentelemetry::sdk::trace::Recordable` has a new virtual method,
    `SetTraceFlags()`.
  * This is an incompatible change for the SDK Recordable API and ABI.
  * Applications _configuring_ the SDK are not affected.
  * Third parties providing SDK _extensions_ are affected,
    and must provide a `SetTraceFlags()` implementation,
    starting with opentelemetry-cpp 1.14.0.

## [1.13.0] 2023-12-06

* [BUILD] Remove WITH_REMOVE_METER_PREVIEW, use WITH_ABI_VERSION_2 instead
  [#2370](https://github.com/open-telemetry/opentelemetry-cpp/pull/2370)
* [SDK] Metrics ObservableRegistry Cleanup
  [#2376](https://github.com/open-telemetry/opentelemetry-cpp/pull/2376)
* [BUILD] Make WITH_OTLP_HTTP_SSL_PREVIEW mainstream
  [#2378](https://github.com/open-telemetry/opentelemetry-cpp/pull/2378)
* [SDK] Creating DoubleUpDownCounter with no matching view
  [#2379](https://github.com/open-telemetry/opentelemetry-cpp/pull/2379)
* [API] Add InstrumentationScope attributes in TracerProvider::GetTracer()
  [#2371](https://github.com/open-telemetry/opentelemetry-cpp/pull/2371)
* [BUILD] DLL export interface for Metrics
  [#2344](https://github.com/open-telemetry/opentelemetry-cpp/pull/2344)
* [BUILD] enum CanonicalCode names too generic... conflict with old C defines
  [#2385](https://github.com/open-telemetry/opentelemetry-cpp/pull/2385)
* [BUILD] Fix cpack broken package version
  [#2386](https://github.com/open-telemetry/opentelemetry-cpp/pull/2386)
* [API] Add a new AddLink() operation to Span
  [#2380](https://github.com/open-telemetry/opentelemetry-cpp/pull/2380)
* [opentracing-shim] Add check for sampled context
  [#2390](https://github.com/open-telemetry/opentelemetry-cpp/pull/2390)
* [BUILD] Fix exported definitions when building DLL with STL
  [#2387](https://github.com/open-telemetry/opentelemetry-cpp/pull/2387)
* [BUILD] Add missing includes to runtime_context_test
  [#2395](https://github.com/open-telemetry/opentelemetry-cpp/pull/2395)
* [ADMIN] Add file .github/repository-settings.md
  [#2392](https://github.com/open-telemetry/opentelemetry-cpp/pull/2392)
* [SDK] Fix GetLogger with empty library name
  [#2398](https://github.com/open-telemetry/opentelemetry-cpp/pull/2398)
* [TEST] Fix compiling problem and removed -DENABLE_TEST
  [#2401](https://github.com/open-telemetry/opentelemetry-cpp/pull/2401)
* [BUILD] Check windows options are not passed to non-Windows build
  [#2399](https://github.com/open-telemetry/opentelemetry-cpp/pull/2399)
* [EXPORTER] Rework OTLP/HTTP and OTLP/GRPC exporter options
  [#2388](https://github.com/open-telemetry/opentelemetry-cpp/pull/2388)
* [Build] Update vcpkg to latest release
  [#2412](https://github.com/open-telemetry/opentelemetry-cpp/pull/2412)
* [SDK] Cardinality limits for metrics streams
  (Sync Instruments + Delta Temporality)
  [#2255](https://github.com/open-telemetry/opentelemetry-cpp/pull/2255)
* [EXPORTER] Prometheus: Add unit to names, convert to word
  [#2213](https://github.com/open-telemetry/opentelemetry-cpp/pull/2213)
* [Metrics] Make context optional for histogram instruments in Metrics SDK
  [#2416](https://github.com/open-telemetry/opentelemetry-cpp/pull/2416)
* [BUILD] Fix references to trace namespace to be fully qualified
  [#2422](https://github.com/open-telemetry/opentelemetry-cpp/pull/2422)
* [BUILD] Bump third_party/googletest to same version as bazel
  [#2421](https://github.com/open-telemetry/opentelemetry-cpp/pull/2421)
* [BUILD] Remove defining NOMINMAX from api
  [#2420](https://github.com/open-telemetry/opentelemetry-cpp/pull/2420)
* [BUILD] 'uint8_t' not declared in this scope with gcc 13.2.1
  [#2423](https://github.com/open-telemetry/opentelemetry-cpp/pull/2423)
* [BUILD] Improve the handling of OPENTELEMETRY_HAVE_WORKING_REGEX
  [#2430](https://github.com/open-telemetry/opentelemetry-cpp/pull/2430)
* [SEMANTIC CONVENTION] Upgrade to semconv 1.23.1
  [#2428](https://github.com/open-telemetry/opentelemetry-cpp/pull/2428)
* [BUILD] Use fully qualified references to trace/common namespace
  [#2424](https://github.com/open-telemetry/opentelemetry-cpp/pull/2424)
* [API] Create root span with active span
  [#2427](https://github.com/open-telemetry/opentelemetry-cpp/pull/2427)
* [REMOVAL] Remove ZPAGES
  [#2433](https://github.com/open-telemetry/opentelemetry-cpp/pull/2433)

Important changes:

* [API] Add InstrumentationScope attributes in TracerProvider::GetTracer()
  [#2371](https://github.com/open-telemetry/opentelemetry-cpp/pull/2371)
  * TracerProvider::GetTracer() now accepts InstrumentationScope attributes.
  * Because this is an `ABI` breaking change, the fix is only available
    with the `CMake` option `WITH_ABI_VERSION_2=ON`.
  * When building with `CMake` option `WITH_ABI_VERSION_1=ON` (by default)
    the `ABI` is unchanged, and the fix is not available.

* [API] Add a new AddLink() operation to Span
  [#2380](https://github.com/open-telemetry/opentelemetry-cpp/pull/2380)
  * New `API` Span::AddLink() adds a single link to a span.
  * New `API` Span::AddLinks() adds multiple links to a span.
  * Because this is an `ABI` breaking change, the fix is only available
    with the `CMake` option `WITH_ABI_VERSION_2=ON`.
  * When building with `CMake` option `WITH_ABI_VERSION_1=ON` (by default)
    the `ABI` is unchanged, and the fix is not available.

* [BUILD] Make WITH_OTLP_HTTP_SSL_PREVIEW mainstream
  [#2378](https://github.com/open-telemetry/opentelemetry-cpp/pull/2378)
  * The experimental `CMake` option `WITH_OTLP_HTTP_SSL_PREVIEW`
    is now promoted to stable. The default is changed to `ON`.
  * The experimental `CMake` option `WITH_OTLP_HTTP_SSL_TLS_PREVIEW`
    is now promoted to stable. The default is changed to `ON`.
  * These build options are scheduled to be removed by the next release,
    building without SSL/TLS will no longer be possible.

* [EXPORTER] Rework OTLP/HTTP and OTLP/GRPC exporter options
  [#2388](https://github.com/open-telemetry/opentelemetry-cpp/pull/2388)
  * `OtlpGrpcMetricExporterOptions` used to honor `_TRACES_`
    environment variables, instead of `_METRICS_` environment variables.
  * The implementation of `OtlpGrpcMetricExporterOptions` is now fixed.
  * Please check configuration variables,
    to make sure `_METRICS_` variables are set as expected.

Breaking changes:

* [BUILD] Remove WITH_REMOVE_METER_PREVIEW, use WITH_ABI_VERSION_2 instead
  [#2370](https://github.com/open-telemetry/opentelemetry-cpp/pull/2370)
  * The experimental `CMake` option `WITH_REMOVE_METER_PREVIEW` is removed,
    use option `WITH_ABI_VERSION_2` instead.

* [BUILD] enum CanonicalCode names too generic... conflict with old C defines
  [#2385](https://github.com/open-telemetry/opentelemetry-cpp/pull/2385)
  * Header file `opentelemetry/trace/canonical_code.h` is unused,
    and is now removed.
  * This header should not be included directly in an application.
    If this is the case, please remove any remaining include directives.

* [BUILD] Fix exported definitions when building DLL with STL
  [#2387](https://github.com/open-telemetry/opentelemetry-cpp/pull/2387)
  * The MeterSelector, MeterSelectorFactory, InstrumentSelector,
    and InstrumentSelectorFactory APIs now use const std::string&
    instead of nostd::string_view for name, version and schema to
    maintain a single export definition for DLL.

* [EXPORTER] Rework OTLP/HTTP and OTLP/GRPC exporter options
  [#2388](https://github.com/open-telemetry/opentelemetry-cpp/pull/2388)
  * `OtlpGrpcLogRecordExporter` incorrectly used `OtlpGrpcExporterOptions`,
    which are options for traces and not logs.
  * This created a bug: the `OtlpGrpcLogRecordExporter` honors `_TRACES_`
    environment variables, instead of `_LOGS_` environment variables.
  * `OtlpGrpcLogRecordExporter` is changed to use
    `OtlpGrpcLogRecordExporterOptions` instead, fixing the bug.
  * User code that initializes the SDK with a GRPC Log exporter,
    and uses exporter options, should adjust to replace
    `OtlpGrpcExporterOptions` with `OtlpGrpcLogRecordExporterOptions`.
  * Please check configuration variables,
    to make sure `_LOGS_` variables are set as expected.

* [REMOVAL] Remove ZPAGES
  [#2433](https://github.com/open-telemetry/opentelemetry-cpp/pull/2433)
  * As announced in release 1.12.0,
    the deprecated ZPAGES exporter is now removed.

## [1.12.0] 2023-10-16

* [BUILD] Support `pkg-config`
  [#2269](https://github.com/open-telemetry/opentelemetry-cpp/pull/2269)
* [CI] Do not automatically close stale issues
  [#2277](https://github.com/open-telemetry/opentelemetry-cpp/pull/2277)
* [CI] Benchmark workflow fails, C++14 required to build grpc
  [#2278](https://github.com/open-telemetry/opentelemetry-cpp/pull/2278)
* [SDK] Increase metric name maximum length from 63 to 255 characters
  [#2284](https://github.com/open-telemetry/opentelemetry-cpp/pull/2284)
* [SEMANTIC CONVENTION] Deprecated semconv (in the spec)
  not deprecated (in C++)
  [#2285](https://github.com/open-telemetry/opentelemetry-cpp/pull/2285)
* [SDK] Remove unused member variables from SyncMetricStorage
  [#2294](https://github.com/open-telemetry/opentelemetry-cpp/pull/2294)
* [DEPRECATION] Deprecate ZPAGES
  [#2291](https://github.com/open-telemetry/opentelemetry-cpp/pull/2291)
* [API] Deliver ABI breaking changes
  [#2222](https://github.com/open-telemetry/opentelemetry-cpp/pull/2222)
* [SDK] Allow metric instrument names to contain / characters
  [#2310](https://github.com/open-telemetry/opentelemetry-cpp/pull/2310)
* [SDK] Fix Observable Counters/UpDownCounters
  [#2298](https://github.com/open-telemetry/opentelemetry-cpp/pull/2298)
* [SDK] Add exemplar reservoir to async metric storage
  [#2319](https://github.com/open-telemetry/opentelemetry-cpp/pull/2319)
* [TEST] Fix lifetime issues in prometheus test utils
  [#2322](https://github.com/open-telemetry/opentelemetry-cpp/pull/2322)
* [EXPORTER] Prometheus: Remove explicit timestamps from metric points
  [#2324](https://github.com/open-telemetry/opentelemetry-cpp/pull/2324)
* [EXPORTER] Prometheus: Handle attribute key collisions from sanitation
  [#2326](https://github.com/open-telemetry/opentelemetry-cpp/pull/2326)
* [EXPORTER] Prometheus cleanup, test with TranslateToPrometheus
  [#2329](https://github.com/open-telemetry/opentelemetry-cpp/pull/2329)
* [SDK] Fix log message in Meter::RegisterSyncMetricStorage
  [#2325](https://github.com/open-telemetry/opentelemetry-cpp/pull/2325)
* [DOC] Simplify the project status section
  [#2332](https://github.com/open-telemetry/opentelemetry-cpp/pull/2332)
* [EXPORTER] Prometheus: Sanitize labels according to spec
  [#2330](https://github.com/open-telemetry/opentelemetry-cpp/pull/2330)
* [SDK] Fix deadlock when shuting down http client
  [#2337](https://github.com/open-telemetry/opentelemetry-cpp/pull/2337)
* [Exporter] Group spans by resource and instrumentation scope
  in OTLP export requests
  [#2335](https://github.com/open-telemetry/opentelemetry-cpp/pull/2335)
* [BUILD] Need fine-grained HAVE_CPP_STDLIB
  [#2304](https://github.com/open-telemetry/opentelemetry-cpp/pull/2304)
* [API] Add InstrumentationScope attributes in MeterProvider::GetMeter()
  [#2224](https://github.com/open-telemetry/opentelemetry-cpp/pull/2224)
* [REMOVAL] Drop C++11 support
  [#2342](https://github.com/open-telemetry/opentelemetry-cpp/pull/2342)
* [EXPORTER] prometheus: add otel_scope_name and otel_scope_version labels
  [#2293](https://github.com/open-telemetry/opentelemetry-cpp/pull/2293)
* [EXPORTER] Export resource for prometheus
  [#2301](https://github.com/open-telemetry/opentelemetry-cpp/pull/2301)
* [BUILD] error: read-only reference ‘value’ used as ‘asm’ output
  [#2354](https://github.com/open-telemetry/opentelemetry-cpp/pull/2354)
* [BUILD] Build break with external CMake nlohman_json package
  [#2353](https://github.com/open-telemetry/opentelemetry-cpp/pull/2353)
* [BUILD] Upgrade libcurl to version 8.4.0
  [#2358](https://github.com/open-telemetry/opentelemetry-cpp/pull/2358)
* [BUILD] Fix opentracing-shim when added in super project
  [#2356](https://github.com/open-telemetry/opentelemetry-cpp/pull/2356)
* [BUILD] Fix protoc searching with non-imported protobuf::protoc target
  [#2362](https://github.com/open-telemetry/opentelemetry-cpp/pull/2362)
* [BUILD] Support to use different cmake package CONFIG of dependencies
  [#2263](https://github.com/open-telemetry/opentelemetry-cpp/pull/2263)
* [SEMANTIC CONVENTION] Upgrade to semconv 1.22.0
  [#2368](https://github.com/open-telemetry/opentelemetry-cpp/pull/2368)

Important changes:

* [API] Add InstrumentationScope attributes in MeterProvider::GetMeter()
  [#2224](https://github.com/open-telemetry/opentelemetry-cpp/pull/2224)
  * MeterProvider::GetMeter() now accepts InstrumentationScope attributes.
  * Because this is an `ABI` breaking change, the fix is only available
    with the `CMake` option `WITH_ABI_VERSION_2=ON`.
  * When building with `CMake` option `WITH_ABI_VERSION_1=ON` (by default)
    the `ABI` is unchanged, and the fix is not available.

Breaking changes:

* [BUILD] Need fine-grained HAVE_CPP_STDLIB
  [#2304](https://github.com/open-telemetry/opentelemetry-cpp/pull/2304)
  * In `CMAKE`, the boolean option `WITH_STL` as changed to an option
    that accepts the values `OFF`, `ON`, `CXX11`, `CXX14`, `CXX17`,
    `CXX20` and `CXX23`.
  * Applications makefiles that did not set WITH_STL need to use
    `WITH_STL=OFF` instead (this is the default).
  * Applications makefiles that did set WITH_STL need to use
    `WITH_STL=ON` instead, or may choose to pick a specific value.
  * In the `API` header files, the preprocessor symbol `HAVE_CPP_STDLIB`
    is no longer used.
  * Applications that did set `HAVE_CPP_STDLIB` before, need to set
    `OPENTELEMETRY_STL_VERSION=<version>` instead, to build with a
    specific STL version (2011, 2014, 2017, 2020, 2023).
  * The opentelemetry-cpp makefile no longer sets
    CMAKE_CXX_STANDARD by itself.
    Instead, the CMAKE_CXX_STANDARD and/or compiler options -stdc++ used
    by the caller are honored.
  * Applications that set neither CMAKE_CXX_STANDARD nor -stdc++
    options may need to provide a C++ standard in their makefiles.

* [REMOVAL] Drop C++11 support
  [#2342](https://github.com/open-telemetry/opentelemetry-cpp/pull/2342)
  * Building with C++11 is no longer supported.

Deprecations:

* [DEPRECATION] Deprecate ZPAGES
  [#2291](https://github.com/open-telemetry/opentelemetry-cpp/pull/2291)

## [1.11.0] 2023-08-21

* [BUILD] Fix more cases for symbol name for 32-bit win32 DLL build
  [#2264](https://github.com/open-telemetry/opentelemetry-cpp/pull/2264)
* [BUILD] added public link of `opentelemetry_proto_grpc` against gRPC lib (only
  if gRPC library is shared)
  [#2268](https://github.com/open-telemetry/opentelemetry-cpp/pull/2268)
* [CI] use ubuntu-latest for tsan CI
  [#2267](https://github.com/open-telemetry/opentelemetry-cpp/pull/2267)
* [SDK] Fixing an apparent logging macro bug
  [#2265](https://github.com/open-telemetry/opentelemetry-cpp/pull/2265)
* [BUILD] Support protobuf 3.22 or upper
  [#2163](https://github.com/open-telemetry/opentelemetry-cpp/pull/2163)
* [BUILD] Remove extra includes
  [#2252](https://github.com/open-telemetry/opentelemetry-cpp/pull/2252)
* [LOGS API SDK] Mark logs signal as stable API/SDK
  [#2229](https://github.com/open-telemetry/opentelemetry-cpp/pull/2229)
* [SEMANTIC CONVENTIONS] Upgrade to 1.21.0
  [#2248](https://github.com/open-telemetry/opentelemetry-cpp/pull/2248)
* [SDK] Valgrind errors on std::atomic variables
  [#2244](https://github.com/open-telemetry/opentelemetry-cpp/pull/2244)
* [BUILD] Fix compile with clang 16 and libc++
  [#2242](https://github.com/open-telemetry/opentelemetry-cpp/pull/2242)
* [Metrics SDK] Add unit to Instrument selection criteria
  [#2236](https://github.com/open-telemetry/opentelemetry-cpp/pull/2236)
* [SDK] Add OStreamLogRecordExporterFactory
  [#2240](https://github.com/open-telemetry/opentelemetry-cpp/pull/2240)
* [SDK] Add support for LowMemory metrics temporality
  [#2234](https://github.com/open-telemetry/opentelemetry-cpp/pull/2234)
* [CI] Misc build scripts cleanup
  [#2232](https://github.com/open-telemetry/opentelemetry-cpp/pull/2232)
* [CI] Upgrade GoogleTest version from 1.12.1 to 1.13.0
  [#2114](https://github.com/open-telemetry/opentelemetry-cpp/pull/2114)
* [BUILD] include cstdint
  [#2230](https://github.com/open-telemetry/opentelemetry-cpp/pull/2230)
* [EXPORTER] Support protobuf 3.22 or upper
  [#2163](https://github.com/open-telemetry/opentelemetry-cpp/pull/2163)
* [SDK] Mark logs signal as stable API/SDK
  [#2229](https://github.com/open-telemetry/opentelemetry-cpp/pull/2229)

Breaking changes:

* [SDK] Add unit to Instrument selection criteria
  [#2236](https://github.com/open-telemetry/opentelemetry-cpp/pull/2236)
  * The `View` constructor and `ViewFactory::Create` method now takes a
   `unit` criteria as optional third argument.
  * Please adjust SDK configuration code accordingly.

## [1.10.0] 2023-07-11

* [REMOVAL] Remove the jaeger exporter
  [#2031](https://github.com/open-telemetry/opentelemetry-cpp/pull/2031)

* [CI] Add a C++11 build
  [#2152](https://github.com/open-telemetry/opentelemetry-cpp/pull/2152)

* [CI] Add Include what you use
  [#2214](https://github.com/open-telemetry/opentelemetry-cpp/pull/2214)

* [CI] opentelemetry-cpp project CI
  [#2071](https://github.com/open-telemetry/opentelemetry-cpp/pull/2071)

* [CI] Do not tag pull_request with the "need-triage" label
  [#2228](https://github.com/open-telemetry/opentelemetry-cpp/pull/2228)

* [BUILD] Fixing CMake to build GTest on Windows
  [#1887](https://github.com/open-telemetry/opentelemetry-cpp/pull/1887)

* [BUILD] Remove option WITH_OTLP
  [#2161](https://github.com/open-telemetry/opentelemetry-cpp/pull/2161)

* [BUILD] Link to opentelemetry_logs even without OTLP
  [#2177](https://github.com/open-telemetry/opentelemetry-cpp/pull/2177)

* [BUILD] Avoid dependency on protobuf from the OTLP HTTP metrics exporter header
  [#2179](https://github.com/open-telemetry/opentelemetry-cpp/pull/2179)

* [BUILD] Add ctime header to metrics_exporter.cc
  [#2187](https://github.com/open-telemetry/opentelemetry-cpp/pull/2187)

* [BUILD] Fix the exported symbol name for 32-bit win32 DLL
  [#2190](https://github.com/open-telemetry/opentelemetry-cpp/pull/2190)

* [BUILD] Upgrade to opentelemetry-proto 0.20.0
  [#2195](https://github.com/open-telemetry/opentelemetry-cpp/pull/2195)

* [BUILD] SDK Header files cleanup, use forward declarations
  [#2182](https://github.com/open-telemetry/opentelemetry-cpp/pull/2182)

* [BUILD] Enable building otel-cpp extensions from main repo
  [#1937](https://github.com/open-telemetry/opentelemetry-cpp/pull/1937)

* [BUILD] Fix if check on environment variable and add CMake variable
  [#2207](https://github.com/open-telemetry/opentelemetry-cpp/pull/2207)

* [BUILD] Add `OPENTELEMETRY_CPP_FOUND` into cmake CONFIG file
  [#2215](https://github.com/open-telemetry/opentelemetry-cpp/pull/2215)

* [BUILD] Upgrade opentelemetry-proto to 1.0.0
  [#2216](https://github.com/open-telemetry/opentelemetry-cpp/pull/2216)

* [BUILD] Include nostd/string_view which is used in severity.h
  [#2219](https://github.com/open-telemetry/opentelemetry-cpp/pull/2219)

* [TEST] Expand api singleton test to cover explicit dlopen()
  [#2164](https://github.com/open-telemetry/opentelemetry-cpp/pull/2164)

* [API] Remove include_trace_context
  [#2194](https://github.com/open-telemetry/opentelemetry-cpp/pull/2194)

* [API] Remove Meters
  [#2205](https://github.com/open-telemetry/opentelemetry-cpp/pull/2205)

* [SDK] Add AdaptingCircularBufferCounter for exponential histograms
  [#2158](https://github.com/open-telemetry/opentelemetry-cpp/pull/2158)

* [SDK] Add base2 exponential histogram indexer
  [#2173](https://github.com/open-telemetry/opentelemetry-cpp/pull/2173)

* [SDK] Simplify SDK version
  [#2180](https://github.com/open-telemetry/opentelemetry-cpp/pull/2180)

* [SDK] Add benchmark for base2 exponential histogram indexer
  [#2181](https://github.com/open-telemetry/opentelemetry-cpp/pull/2181)

* [SDK] Provide builders to avoid exposing Metrics SDK internals
  [#2189](https://github.com/open-telemetry/opentelemetry-cpp/pull/2189)

* [SDK] MeterProvider should own MeterContext, not share it
  [#2218](https://github.com/open-telemetry/opentelemetry-cpp/pull/2218)

* [SDK] TracerProvider should own TracerContext, not share it
  [#2221](https://github.com/open-telemetry/opentelemetry-cpp/pull/2221)

* [EXPORTER] Change OTLP Json field name to camelCase
  [#2162](https://github.com/open-telemetry/opentelemetry-cpp/pull/2162)

* [EXPORTER] Support empty arrays in `OtlpRecordable` attributes
  [#2166](https://github.com/open-telemetry/opentelemetry-cpp/pull/2166)

* [EXPORTER] set is_monotonic only for instrument type kCounter
  [#2171](https://github.com/open-telemetry/opentelemetry-cpp/pull/2171)

* [EXPORTER] Fixed HTTP CURL for 32bits platforms
  [#2178](https://github.com/open-telemetry/opentelemetry-cpp/pull/2178)

* [EXPORTER] Fix OTLP HTTP exporting in sync mode
  [#2193](https://github.com/open-telemetry/opentelemetry-cpp/pull/2193)

* [EXPORTER] Prometheus exporter sanitizes invalid characters
  [#1934](https://github.com/open-telemetry/opentelemetry-cpp/pull/1934)

* [EXPORTER] Prometheus: Error on ingesting samples
  with different value but same timestamp
  [#2200](https://github.com/open-telemetry/opentelemetry-cpp/pull/2200)

* [EXPORTER] OTLP GRPC mTLS support
  [#2120](https://github.com/open-telemetry/opentelemetry-cpp/pull/2120)

* [DOC] Small fix for Histogram documentation
  [#2156](https://github.com/open-telemetry/opentelemetry-cpp/pull/2156)

* [DOC] Move Reiley Yang to emeritus
  [#2198](https://github.com/open-telemetry/opentelemetry-cpp/pull/2198)

Important changes:

* [API] Remove Meters
  [#2205](https://github.com/open-telemetry/opentelemetry-cpp/pull/2205)
  * The CMake option `WITH_REMOVE_METER_PREVIEW` was added.
  * This option is experimental, and may change in the future.
  * Enabling it is an ABI breaking change.

Breaking changes:

* [REMOVAL] Remove the jaeger exporter
  [#2031](https://github.com/open-telemetry/opentelemetry-cpp/pull/2031)
  * The CMake `WITH_JAEGER` option has been removed
  * Please remove usage of `WITH_JAEGER` from user scripts and makefiles.

* [SDK] MeterProvider should own MeterContext, not share it
  [#2218](https://github.com/open-telemetry/opentelemetry-cpp/pull/2218)
  * The `MeterProvider` constructor now takes a `unique_ptr` on
    `MeterContext`, instead of a `shared_ptr`.
  * Please adjust SDK configuration code accordingly.

* [SDK] TracerProvider should own TracerContext, not share it
  [#2221](https://github.com/open-telemetry/opentelemetry-cpp/pull/2221)
  * The `TracerProvider` constructor now takes a `unique_ptr` on
    `TracerContext`, instead of a `shared_ptr`.
  * The `LoggerProvider` constructor now takes a `unique_ptr` on
    `LoggerContext`, instead of a `shared_ptr`.
  * Please adjust SDK configuration code accordingly.

## [1.9.1] 2023-05-26

* [DEPRECATION] Drop C++11 support
  [#2146](https://github.com/open-telemetry/opentelemetry-cpp/pull/2146)

* [CI] Upgrade Bazel and Bazelisk version
  [#2118](https://github.com/open-telemetry/opentelemetry-cpp/pull/2118)
* [CI] Upgrade Google Benchmark version from 1.6.0 to 1.7.1
  [#2116](https://github.com/open-telemetry/opentelemetry-cpp/pull/2116)
* [CI] Upgrade Nlohmann JSON library version from 3.10.5 to 3.11.2
  [#2115](https://github.com/open-telemetry/opentelemetry-cpp/pull/2115)

* [BUILD] Missed include
  [#2143](https://github.com/open-telemetry/opentelemetry-cpp/pull/2143)
* [BUILD] Add opentelemetry_proto_grpc and allow build shared
  opentelemetry_proto and opentelemetry_proto_grpc on non-Windows platform.
  [#2097](https://github.com/open-telemetry/opentelemetry-cpp/pull/2097)
* [BUILD] Warning cleanup, single character wrapped by std::string
  [#2137](https://github.com/open-telemetry/opentelemetry-cpp/pull/2137)
* [BUILD] Add missing target dependencies
  [#2128](https://github.com/open-telemetry/opentelemetry-cpp/pull/2128)
* [BUILD] Fix if JSON library already added another CMake target
  [#2126](https://github.com/open-telemetry/opentelemetry-cpp/pull/2126)
* [BUILD] shared libraries with version suffix, along with the symbolic link
  [#2109](https://github.com/open-telemetry/opentelemetry-cpp/pull/2109)
* [BUILD] Show warning message if WITH_OTLP is enabled
  [#2112](https://github.com/open-telemetry/opentelemetry-cpp/pull/2112)
* [BUILD] Add missing STL header.
  [#2107](https://github.com/open-telemetry/opentelemetry-cpp/pull/2107)
* [BUILD] Build break with old curl, macro CURL_VERSION_BITS unknown
  [#2102](https://github.com/open-telemetry/opentelemetry-cpp/pull/2102)
* [BUILD] Transitive dependency issue with the otlp http exporter
  [#2154](https://github.com/open-telemetry/opentelemetry-cpp/pull/2154)

* [TEST] Add unit test for log body implicit conversions.
  [#2136](https://github.com/open-telemetry/opentelemetry-cpp/pull/2136)
* [TEST] Add event id to logger benchmark method
  [#2133](https://github.com/open-telemetry/opentelemetry-cpp/pull/2133)

* [API] Fix inclusion header files and use forward declaration
  [#2124](https://github.com/open-telemetry/opentelemetry-cpp/pull/2124)
* [API] Add user facing Logging API and Benchmarks
  [#2094](https://github.com/open-telemetry/opentelemetry-cpp/pull/2094)

* [SDK] SDK support for the new OTel log
  [#2123](https://github.com/open-telemetry/opentelemetry-cpp/pull/2123)

* [EXPORTER] Fixed HTTP session cleanup on shutdown
  [#2111](https://github.com/open-telemetry/opentelemetry-cpp/pull/2111)
* [EXPORTER] Delegate all API calls of gRPC into
  opentelemetry_exporter_otlp_grpc_client,
  and make it contains all symbols needed.
  [#2005](https://github.com/open-telemetry/opentelemetry-cpp/pull/2005)

* [DOC] Add Marc as maintainer.
  [#2027](https://github.com/open-telemetry/opentelemetry-cpp/pull/2027)

Breaking changes:

* Add opentelemetry_proto_grpc and move gRPC sources into it.
  [#2097](https://github.com/open-telemetry/opentelemetry-cpp/pull/2097)
  * There will be no breaking changes for users who only use OTLP exporters and
    do not directly use opentelemetry-cpp::proto. However, it is important to
    note that `opentelemetry-cpp::proto` no longer contains generated gRPC codes
    , and all components that depend on these gRPC codes should also link to
    `opentelemetry-cpp::proto_grpc`.

Deprecations:

* The Jaeger Exporter is deprecated, see [DEPRECATED](./DEPRECATED.md) for details.
* C++11 support is to end, C++14 will be supported instead,
  see [DEPRECATED](./DEPRECATED.md) for details.

## [1.9.0] 2023-04-12

* [CI] Make build environment parallel (Windows)
  [#2080](https://github.com/open-telemetry/opentelemetry-cpp/pull/2080)
* [CI] Make build environment parallel (Linux)
  [#2076](https://github.com/open-telemetry/opentelemetry-cpp/pull/2076)
* [CI] Remove separate run of metrics ostream example
  [#2030](https://github.com/open-telemetry/opentelemetry-cpp/pull/2030)

* [BUILD] Include directory path added for Zipkin exporter example
  [#2069](https://github.com/open-telemetry/opentelemetry-cpp/pull/2069)
* [BUILD] Ignore more warning in generated protobuf files
  [#2067](https://github.com/open-telemetry/opentelemetry-cpp/pull/2067)
* [BUILD] Clean warnings in ETW exporters
  [#2063](https://github.com/open-telemetry/opentelemetry-cpp/pull/2063)
* [BUILD] Fix default value of OPENTELEMETRY_INSTALL_default
  [#2062](https://github.com/open-telemetry/opentelemetry-cpp/pull/2062)

* [SEMANTIC CONVENTIONS] Upgrade to version 1.20.0
  [#2088](https://github.com/open-telemetry/opentelemetry-cpp/pull/2088)
* [SEMANTIC CONVENTIONS] Upgrade to version 1.19.0
  [#2017](https://github.com/open-telemetry/opentelemetry-cpp/pull/2017)

* [API] Checking indices before dereference in string utils
  [#2040](https://github.com/open-telemetry/opentelemetry-cpp/pull/2040)
* [API] Export factory class of log provider
  [#2041](https://github.com/open-telemetry/opentelemetry-cpp/pull/2041)

* [SDK] Implement Forceflush for Periodic Metric Reader
  [#2064](https://github.com/open-telemetry/opentelemetry-cpp/pull/2064)
* [SDK] Add `ForceFlush` for all `LogRecordExporter` and `SpanExporter`
  [#2000](https://github.com/open-telemetry/opentelemetry-cpp/pull/2000)
* [SDK] Fix schema URL precedence bug in `Resource::Merge`
  [#2036](https://github.com/open-telemetry/opentelemetry-cpp/pull/2036)
* [SDK] Use sdk_start_ts for MetricData start_ts for instruments having
  cumulative aggregation temporality.
  [#2086](https://github.com/open-telemetry/opentelemetry-cpp/pull/2086)

* [EXPORTER] Add OTLP HTTP SSL support
  [#1793](https://github.com/open-telemetry/opentelemetry-cpp/pull/1793)
* [EXPORTER] GRPC endpoint scheme should take precedence over OTEL_EXPORTER_OTLP_TRACES_INSECURE
  [#2060](https://github.com/open-telemetry/opentelemetry-cpp/pull/2060)

* [EXAMPLES] Remove unused 'alerting' section from prometheus.yml in examples
  [#2055](https://github.com/open-telemetry/opentelemetry-cpp/pull/2055)
* [EXAMPLES] Fix view names in Prometheus example
  [#2034](https://github.com/open-telemetry/opentelemetry-cpp/pull/2034)

* [DOC] Fix some docs typo
  [#2057](https://github.com/open-telemetry/opentelemetry-cpp/pull/2057)
* [DOC] Update OpenTracing shim README.md
  [#2028](https://github.com/open-telemetry/opentelemetry-cpp/pull/2028)
* [DOC] INSTALL doc clarifications
  [#2078](https://github.com/open-telemetry/opentelemetry-cpp/pull/2078)

Important changes:

* [EXPORTER] GRPC endpoint scheme should take precedence over OTEL_EXPORTER_OTLP_TRACES_INSECURE
  [#2060](https://github.com/open-telemetry/opentelemetry-cpp/pull/2060)
  * The logic to decide whether or not an OTLP GRPC exporter uses SSL has
    changed to comply with the specification:
    * Before this change, the following settings were evaluated, in order:
      * OTEL_EXPORTER_OTLP_TRACES_INSECURE (starting with 1.8.3)
      * OTEL_EXPORTER_OTLP_INSECURE (starting with 1.8.3)
      * OTEL_EXPORTER_OTLP_TRACES_SSL_ENABLE
      * OTEL_EXPORTER_OTLP_SSL_ENABLE
    * With this change, the following settings are evaluated, in order:
      * The GRPC endpoint scheme, if provided:
        * "https" imply with SSL,
        * "http" imply without ssl.
      * OTEL_EXPORTER_OTLP_TRACES_INSECURE
      * OTEL_EXPORTER_OTLP_INSECURE
      * OTEL_EXPORTER_OTLP_TRACES_SSL_ENABLE
      * OTEL_EXPORTER_OTLP_SSL_ENABLE
    * As a result, a behavior change for GRPC SSL is possible,
      because the endpoint scheme now takes precedence.
      Please verify configuration settings for the GRPC endpoint.
* [SDK] Add `ForceFlush` for all `LogRecordExporter` and `SpanExporter`
  [#2000](https://github.com/open-telemetry/opentelemetry-cpp/pull/2000)
  * `LogRecordExporter` and `SpanExporter` add a new virtual function
    `ForceFlush`, and if users implement any customized `LogRecordExporter` and
    `SpanExporter`, they should also implement this function.
    There should be no impact if users only use factory to create exporters.

Deprecations:

* The Jaeger Exporter is deprecated, see [DEPRECATED](./DEPRECATED.md) for details.

## [1.8.3] 2023-03-06

* Provide version major/minor/patch macros
  [#2014](https://github.com/open-telemetry/opentelemetry-cpp/pull/2014)
* [BUILD] Add `OPENTELEMETRY_INSTALL` to allow user to skip install targets.
  [#2022](https://github.com/open-telemetry/opentelemetry-cpp/pull/2022)
* [SDK] Rename the global SDK version variables to avoid naming clash
  [#2011](https://github.com/open-telemetry/opentelemetry-cpp/pull/2011)
* [BUILD] Fix typo in CMakeLists.txt
  [#2010](https://github.com/open-telemetry/opentelemetry-cpp/pull/2010)
* [EXPORTER] fix Prometheus test iterator iterator increment
  [#2006](https://github.com/open-telemetry/opentelemetry-cpp/pull/2006)
* [SDK]Add attributes for InstrumentationScope
  [#2004](https://github.com/open-telemetry/opentelemetry-cpp/pull/2004)
* [METRICS SDK] Performance improvement in measurement processing
  [#1993](https://github.com/open-telemetry/opentelemetry-cpp/pull/1993)
* [EXAMPLE] Add example for logs ostream exporter
  [#1992](https://github.com/open-telemetry/opentelemetry-cpp/pull/1992)
* [ETW Exporter] Support serialize span/log attributes into JSON
  [#1991](https://github.com/open-telemetry/opentelemetry-cpp/pull/1991)
* [ETW Exporter]Do not overwrite ParentId when setting attribute on Span
  [#1989](https://github.com/open-telemetry/opentelemetry-cpp/pull/1989)
* Upgrade prometheus-cpp to v1.1.0
  [#1954](https://github.com/open-telemetry/opentelemetry-cpp/pull/1954)
* Convert Prometheus Exporter to Pull MetricReader
  [#1953](https://github.com/open-telemetry/opentelemetry-cpp/pull/1953)
* [DOCS] Add alpine packages to INSTALL.md
  [#1957](https://github.com/open-telemetry/opentelemetry-cpp/pull/1957)
* [METRICS SDK] Add benchmark tests for Sum Aggregation.
  [#1948](https://github.com/open-telemetry/opentelemetry-cpp/pull/1948)
* [BUILD] Build OpenTelemetry SDK and exporters into DLL
  [#1932](https://github.com/open-telemetry/opentelemetry-cpp/pull/1932)
* [CI] Enforce copyright check in CI
  [#1965](https://github.com/open-telemetry/opentelemetry-cpp/pull/1965)
* [BUILD] Fix typo GENENV -> GETENV
  [#1972](https://github.com/open-telemetry/opentelemetry-cpp/pull/1972)
* [SEMANTIC CONVENTIONS] Upgrade to version 1.18.0
  [#1974](https://github.com/open-telemetry/opentelemetry-cpp/pull/1974)
* [EXT] Fix thread-safety when shutdown.
  [#1977](https://github.com/open-telemetry/opentelemetry-cpp/pull/1977)
* [SDK] Fix missing ObservedTimestamp.
  [#1985](https://github.com/open-telemetry/opentelemetry-cpp/pull/1985)
* [METRICS SDK] fix variable names
  [#1987](https://github.com/open-telemetry/opentelemetry-cpp/pull/1987)
* [EXPORTER] Fix Prometheus server crash on listening to already used port
  [#1986](https://github.com/open-telemetry/opentelemetry-cpp/pull/1986)
* [EXPORTER] Boolean environment variables not parsed per the spec
  [#1982](https://github.com/open-telemetry/opentelemetry-cpp/pull/1982)
* [EXPORTER] Opentracing shim
  [#1909](https://github.com/open-telemetry/opentelemetry-cpp/pull/1909)

## [1.8.2] 2023-01-31

* Remove redundant macro check in nostd::shared_ptr [#1939](https://github.com/open-telemetry/opentelemetry-cpp/pull/1939)
* Fix typo in packages.cmake causing incorrect nuget package versions [#1936](https://github.com/open-telemetry/opentelemetry-cpp/pull/1936)
* [METRICS] Custom Aggregation support [#1899](https://github.com/open-telemetry/opentelemetry-cpp/pull/1899)
* Small fix in INSTALL.md for enabling building package. [#1930](https://github.com/open-telemetry/opentelemetry-cpp/pull/1930)
* [METRICS] Fix warning for misconfiguration of PeriodicExportingMetricReader [#1929](https://github.com/open-telemetry/opentelemetry-cpp/pull/1929)
* Make macros.h available for all source files via version.h [#1918](https://github.com/open-telemetry/opentelemetry-cpp/pull/1918)
* [METRICS] Histogram Aggregation: Fix bucket detection logic,
 performance improvements, and benchmark tests [#1869](https://github.com/open-telemetry/opentelemetry-cpp/pull/1869)
* Remove unused namespace alias for nostd [#1914](https://github.com/open-telemetry/opentelemetry-cpp/pull/1914)
* [METRICS] Update meter.h [#1907](https://github.com/open-telemetry/opentelemetry-cpp/pull/1907)
* sdk::resource::Resource::Merge should be const [#1905](https://github.com/open-telemetry/opentelemetry-cpp/pull/1905)
* [METRICS] Collect and Export metric data before
 PeriodicMetricReader shutdown. [#1860](https://github.com/open-telemetry/opentelemetry-cpp/pull/1860)
* [ETW EXPORTER] Add Virtual destructor for TailSampler, Update Maintainer
 mode warnings for MSVC [#1897](https://github.com/open-telemetry/opentelemetry-cpp/pull/1897)
* Fix #1867 Orderly shutdown in examples [#1868](https://github.com/open-telemetry/opentelemetry-cpp/pull/1868)
* [METRICS] minor metrics handling optimizations [#1890](https://github.com/open-telemetry/opentelemetry-cpp/pull/1890)
* fix SpinLockMutex for Intel Compiler [#1885](https://github.com/open-telemetry/opentelemetry-cpp/pull/1885)
* [LOGS] Change BatchLogRecordProcessorFactory::Create to static method [#1876](https://github.com/open-telemetry/opentelemetry-cpp/pull/1876)
* Enable generating deb, rpm, NuGet, tgz, zip package through cmake build [#1662](https://github.com/open-telemetry/opentelemetry-cpp/pull/1662)
* Updated clone command in INSTALL.md [#1818](https://github.com/open-telemetry/opentelemetry-cpp/pull/1818)
* Small cleanup to remove old metrics design docs [#1855](https://github.com/open-telemetry/opentelemetry-cpp/pull/1855)
* [BUILD] Fix build error with older version of VS2017 compiler. [1857](https://github.com/open-telemetry/opentelemetry-cpp/pull/1857)
* [EXPORTERS] Enable setting Span endtime for ETW exporter [#1846](https://github.com/open-telemetry/opentelemetry-cpp/pull/1846)
* [REMOVAL] Remove deprecated experimental semantic conventions [#1743](https://github.com/open-telemetry/opentelemetry-cpp/pull/1743)
* [EXPORTERS] Fix console debug logs for otlp exporters. [#1848](https://github.com/open-telemetry/opentelemetry-cpp/pull/1848)
* [LOGS] Add `include_trace_context` and `EventLogger` [#1884](https://github.com/open-telemetry/opentelemetry-cpp/pull/1884)
* [METRICS] Change BatchLogRecordProcessorFactory::Create to static method
* [BUILD] Fix OTELCPP_MAINTAINER_MODE [#1844](https://github.com/open-telemetry/opentelemetry-cpp/pull/1844)
* [BUILD] Fix compatibility when using clang and libc++, upgrade GTest and
  cmake when using C++20 [#1852](https://github.com/open-telemetry/opentelemetry-cpp/pull/1852)
* [SEMANTIC CONVENTIONS] Upgrade to version 1.16.0
  [#1854](https://github.com/open-telemetry/opentelemetry-cpp/pull/1854)
* [SDK] BatchSpanProcessor now logs a warning when dropping a span because the
  queue is full
  [1871](https://github.com/open-telemetry/opentelemetry-cpp/pull/1871)
* [BUILD] Migrate from @bazel_tools//platforms to [Bazel Platforms](https://github.com/bazelbuild/platforms)
  to enable Bazel 6.0.0 compatibility [#1873](https://github.com/open-telemetry/opentelemetry-cpp/pull/1873)
* [BUILD] Cleanup CMake makefiles for nlohmann_json
  [#1912](https://github.com/open-telemetry/opentelemetry-cpp/pull/1912)
* [BUILD] Cleanup CMake makefiles for CURL usage
  [#1916](https://github.com/open-telemetry/opentelemetry-cpp/pull/1916)
* [SEMANTIC CONVENTIONS] Upgrade to version 1.17.0
  [#1927](https://github.com/open-telemetry/opentelemetry-cpp/pull/1927)
* [MAINTAINER DOC] Define and document a deprecation process,
  [DEPRECATION] Deprecate the Jaeger exporter,
  implemented by [#1923](https://github.com/open-telemetry/opentelemetry-cpp/pull/1923)
* [BUILD] OTLP HTTP Exporter has build warnings in maintainer mode
  [#1943](https://github.com/open-telemetry/opentelemetry-cpp/pull/1943)

Deprecations:

* [MAINTAINER DOC] Define and document a deprecation process,
  [#1923](https://github.com/open-telemetry/opentelemetry-cpp/pull/1923)
  * A new file, [DEPRECATED](./DEPRECATED.md) list all the code currently
    deprecated.
  * A new [deprecation process](./docs/deprecation-process.md) details the plan to
    deprecate and later remove code.
* [DEPRECATION] Deprecate the Jaeger exporter
  [#1923](https://github.com/open-telemetry/opentelemetry-cpp/pull/1923)
  * The Jaeger Exporter is deprecated, see [DEPRECATED](./DEPRECATED.md) for details.

Important changes:

* [BUILD] Cleanup CMake makefiles for CURL usage
  [#1916](https://github.com/open-telemetry/opentelemetry-cpp/pull/1916)
  * CMake option `WITH_OTLP_HTTP`
    * Before this change, the CMake option `WITH_OTLP_HTTP` was unpredictable,
      sometime set to ON and sometime set to OFF by default,
      depending on whether a CURL package was found or not.
      The option `WITH_OTLP_HTTP` was sometime not displayed in the ccmake
      UI, making it impossible to even discover there is an option of that name.
    * With this change, CMake option `WITH_OTLP_HTTP` is always OFF by
      default. WITH_OTLP_HTTP MUST be set to ON explicitly to build the
      OTLP HTTP exporter. The option is always visible in the ccmake UI.
  * CMake option `BUILD_W3CTRACECONTEXT_TEST`
    * Before this change, the W3C trace context tests were built, or
      not, in an unpredictable way, depending on the presence, or not, of a
      CURL package. In particular, the build could ignore the W3C trace
      context tests even when BUILD_W3CTRACECONTEXT_TEST=ON.
    * With this change, option BUILD_W3CTRACECONTEXT_TEST is honored.
  * HTTP client/server examples
    * Before this change, the HTTP client/server examples were built, or
      not, in an unpredictable way, depending on the presence, or not, of a
      CURL package.
    * With this change, a new option `WITH_EXAMPLES_HTTP` is used to
      build the HTTP client/server examples.

## [1.8.1] 2022-12-04

* [ETW Exporter] Tail based sampling support [#1780](https://github.com/open-telemetry/opentelemetry-cpp/pull/1780)
* [EXPORTERS] fix typo [affecting otlp exported histogram metrics max uint] [#1827](https://github.com/open-telemetry/opentelemetry-cpp/pull/1827)
* [EXPORTERS] fix enum-compare-switch warning [#1833](https://github.com/open-telemetry/opentelemetry-cpp/pull/1833)
* [METRICS] Change default temporality as "Cumulative" for OTLP metrics
exporters [#1828](https://github.com/open-telemetry/opentelemetry-cpp/pull/1828)
* [BUILD] Moved otlp_grpc_utils.cc to opentelemetry_exporter_otlp_grpc_client.
[#1829](https://github.com/open-telemetry/opentelemetry-cpp/pull/1829)
* Fix type mismatch when move nostd::shared_ptr [#1815](https://github.com/open-telemetry/opentelemetry-cpp/pull/1815)
* [BUILD] Fix Prometheus target name [#1820](https://github.com/open-telemetry/opentelemetry-cpp/pull/1820)
* Clean unused docker files [#1817](https://github.com/open-telemetry/opentelemetry-cpp/pull/1817)
* [BUILD] Fix default bazel build [#1816](https://github.com/open-telemetry/opentelemetry-cpp/pull/1816)
* [BUILD] move client::nosend under test_common [#1811](https://github.com/open-telemetry/opentelemetry-cpp/pull/1811)
* [BUILD] Fix opentelemetry-proto file exists check [#1824](https://github.com/open-telemetry/opentelemetry-cpp/pull/1824)

## [1.8.0] 2022-11-27

* [DOC] Update Metrics status in README.md [#1722](https://github.com/open-telemetry/opentelemetry-cpp/pull/1722)
* [DOC] Remove misleading comments about ABI compatibility for nostd::span [#1731](https://github.com/open-telemetry/opentelemetry-cpp/pull/1731)
* [BUILD] Bump abseil-cpp for cmake CI [#1807](https://github.com/open-telemetry/opentelemetry-cpp/pull/1807)
* [Exporter] Add status code to OTLP grpc trace log [#1792](https://github.com/open-telemetry/opentelemetry-cpp/pull/1792)
* [Exporter] add fix for prometheus exporter build [#1795](https://github.com/open-telemetry/opentelemetry-cpp/pull/1795)
* [BUILD] Add option WITH_BENCHMARK to disable building benchmarks [#1794](https://github.com/open-telemetry/opentelemetry-cpp/pull/1794)
* [BUILD] Fix CI benchmark [#1799](https://github.com/open-telemetry/opentelemetry-cpp/pull/1799)
* [BUILD] bump to gRPC v1.48.1 for bazel CIs [#1786](https://github.com/open-telemetry/opentelemetry-cpp/pull/1786)
* [BUILD] Fix CI build [#1798](https://github.com/open-telemetry/opentelemetry-cpp/pull/1798)
* [BUILD] Fix clang-format in CI [#1796](https://github.com/open-telemetry/opentelemetry-cpp/pull/1796)
* Fix session lock of OtlpHttpClient [#1760](https://github.com/open-telemetry/opentelemetry-cpp/pull/1760)
* [Metrics SDK] Add MeterContext::ForEachMeter() method to process callbacks on
 Meter in thread-safe manner [#1783](https://github.com/open-telemetry/opentelemetry-cpp/pull/1783)
* [DOC] Document that clang-format version 10.0 is used. [#1782](https://github.com/open-telemetry/opentelemetry-cpp/pull/1782)
* [BUILD] Upgrade bazel build to use abseil-cpp-20220623.1 [#1779](https://github.com/open-telemetry/opentelemetry-cpp/pull/1779)
* Fix GlobalLogHandler singleton creation order [#1767](https://github.com/open-telemetry/opentelemetry-cpp/pull/1767)
* [Metrics SDK] Change Prometheus CMake target name [#1765](https://github.com/open-telemetry/opentelemetry-cpp/pull/1765)
* [DOC] Cleanup INSTALL.md [#1757](https://github.com/open-telemetry/opentelemetry-cpp/pull/1757)
* [DOC] Format config options in OTLP exporter readme [#1748](https://github.com/open-telemetry/opentelemetry-cpp/pull/1748)
* [DOC] Cleanup ENABLE_METRICS_PREVIEW [#1745](https://github.com/open-telemetry/opentelemetry-cpp/pull/1745)
* [Build] Multiple CURL packages leads to invalid build (#1738) [#1739](https://github.com/open-telemetry/opentelemetry-cpp/pull/1739)
* [Metrics SDK] Cleanup ENABLE_METRICS_PREVIEW [#1735](https://github.com/open-telemetry/opentelemetry-cpp/pull/1735)
* [Logs SDK] LogProcessor, LogExporter class name [#1736](https://github.com/open-telemetry/opentelemetry-cpp/pull/1736)
* [Metrics SDK] Cleanup of old _metric api/sdk [#1734](https://github.com/open-telemetry/opentelemetry-cpp/pull/1734)
* [ETW Exporter] Fix span timestamp(s) precision to nanoseconds [#1726](https://github.com/open-telemetry/opentelemetry-cpp/pull/1726)
* [LOGS SDK] Rename LogProcessor and LogExporter to LogRecordProcessor and LogRecordExporter
  [#1727](https://github.com/open-telemetry/opentelemetry-cpp/pull/1727)
* [METRICS SDK] - Remove old metrics from Github CI
  [#1733](https://github.com/open-telemetry/opentelemetry-cpp/pull/1733)
* [BUILD] Add CMake OTELCPP_PROTO_PATH [#1730](https://github.com/open-telemetry/opentelemetry-cpp/pull/1730)
* [SEMANTIC CONVENTIONS] Upgrade to version 1.15.0
  [#1761](https://github.com/open-telemetry/opentelemetry-cpp/pull/1761)
* [LOGS SDK] New LogRecord and logs::Recordable implementations.
  [#1766](https://github.com/open-telemetry/opentelemetry-cpp/pull/1766)

Deprecation notes:

* [Deprecation] Deprecate experimental semantic conventions
  [#1744](https://github.com/open-telemetry/opentelemetry-cpp/pull/1744)
  * The file
    `api/include/opentelemetry/trace/experimental_semantic_conventions.h`
    is deprecated, and will be removed in a future release.
    Use file
    `api/include/opentelemetry/trace/semantic_conventions.h`
    instead.

  * The file
    `sdk/include/opentelemetry/sdk/resource/experimental_semantic_conventions.h`
    is deprecated, and will be removed in a future release.
    Use file
    `sdk/include/opentelemetry/sdk/resource/semantic_conventions.h`
    instead.

  * The function, declared in the global namespace
    `uint32_t hashCode(const char *str, uint32_t h = 0)`
    is deprecated, and will be removed in a future release.
    No replacement will be provided.
    Note that function `opentelemetry::utils::hashCode`,
    declared in the ETW exporter, is not affected by this deprecation.

Breaking changes:

* [SEMANTIC CONVENTIONS] Upgrade to version 1.15.0
  [#1761](https://github.com/open-telemetry/opentelemetry-cpp/pull/1761)
  * Naming of semantic conventions has changed from uppercase constants,
    like `SemanticConventions::SERVICE_NAME`,
    to camel case, like `SemanticConventions::kServiceName`.
    This is necessary to avoid collisions with macros in general,
    which breaks the build on some platforms.
  * Semantic conventions are flagged as experimental,
    which is why this change is done in this release.

## [1.7.0] 2022-10-28

* [METRICS SDK] Validate Instrument meta data (name, unit, description) [#1713](https://github.com/open-telemetry/opentelemetry-cpp/pull/1713)
* [DOCS] Document libthrift 0.12.0 doesn't work with Jaeger exporter [#1714](https://github.com/open-telemetry/opentelemetry-cpp/pull/1714)
* [Metrics SDK] Add Monotonic Property to Sum Aggregation, and
unit tests for Up Down Counter [#1675](https://github.com/open-telemetry/opentelemetry-cpp/pull/1675)
* [Metrics SDK] Move Metrics Exemplar processing behind feature flag [#1710](https://github.com/open-telemetry/opentelemetry-cpp/pull/1710)
* [Metrics API/SDK] Change Meter API/SDK to return nostd::unique_ptr
 for Sync Instruments [#1707](https://github.com/open-telemetry/opentelemetry-cpp/pull/1707)
which includes breaking change in the Metrics api and sdk.
* [BUILD] Add e2e test to asan & tsan CI [#1670](https://github.com/open-telemetry/opentelemetry-cpp/pull/1670)
* [BUILD] Add otlp-grpc example bazel [#1708](https://github.com/open-telemetry/opentelemetry-cpp/pull/1708)
* [TRACE SDK] Fix debug log of OTLP HTTP exporter and ES log exporter [#1703](https://github.com/open-telemetry/opentelemetry-cpp/pull/1703)
* [METRICS SDK] Fix a potential precision loss on integer in
ReservoirCellIndexFor [#1696](https://github.com/open-telemetry/opentelemetry-cpp/pull/1696)
* [METRICS SDK] Fix Histogram crash [#1685](https://github.com/open-telemetry/opentelemetry-cpp/pull/1685)
* [METRICS SDK] Fix:1676 Segfault when short export period is used for metrics [#1682](https://github.com/open-telemetry/opentelemetry-cpp/pull/1682)
* [METRICS SDK] Add timeout support to MeterContext::ForceFlush [#1673](https://github.com/open-telemetry/opentelemetry-cpp/pull/1673)
* [DOCS] - Minor updates to OStream Metrics exporter documentation [#1679](https://github.com/open-telemetry/opentelemetry-cpp/pull/1679)
* [DOCS] Fix:#1575 API Documentation for Metrics SDK and API [#1678](https://github.com/open-telemetry/opentelemetry-cpp/pull/1678)
* [BUILD] Fixed compiler warnings [#1677](https://github.com/open-telemetry/opentelemetry-cpp/pull/1677)
* [METRICS SDK] Fix threading issue between Meter::RegisterSyncMetricStorage
 and Meter::Collect [#1666](https://github.com/open-telemetry/opentelemetry-cpp/pull/1666)
* [METRICS SDK] Fix data race on MeterContext::meters_ [#1668](https://github.com/open-telemetry/opentelemetry-cpp/pull/1668)
* [METRICS SDK] Fix observable Gauge metrics generation [#1651](https://github.com/open-telemetry/opentelemetry-cpp/pull/1651)
* [BUILD] Detect ARCH=sparc in CMake [#1660](https://github.com/open-telemetry/opentelemetry-cpp/pull/1660)
* [SDK] Add user agent for OTLP http/grpc client [#1657](https://github.com/open-telemetry/opentelemetry-cpp/pull/1657)
* [BUILD] Fix clang and gcc warnings [#1658](https://github.com/open-telemetry/opentelemetry-cpp/pull/1658)
* [Metrics SDK] Add Metrics ExemplarFilter and ExemplarReservoir [#1584](https://github.com/open-telemetry/opentelemetry-cpp/pull/1584)
* [LOGS SDK] Rename OnReceive to OnEmit [#1652](https://github.com/open-telemetry/opentelemetry-cpp/pull/1652)
* [METRICS SDK] Fix Observable Gauge does not reflect updated values,
and send the old value always [#1641](https://github.com/open-telemetry/opentelemetry-cpp/pull/1641)
* [Metrics SDK] Change boundary type to double for Explicit Bucket Histogram Aggregation,
and change default bucket range [#1626](https://github.com/open-telemetry/opentelemetry-cpp/pull/1626)
* [METRICS SDK] Fix occasional Segfault with LongCounter instrument [#1638](https://github.com/open-telemetry/opentelemetry-cpp/pull/1638)
* [BUILD] Bump vcpk to 2022.08.15 [#1633](https://github.com/open-telemetry/opentelemetry-cpp/pull/1633)
* [BUILD] Bump gRPC to v1.48.1 for CMake Linux CI [#1608](https://github.com/open-telemetry/opentelemetry-cpp/pull/1608)
* [Metrics] Switch to explicit 64 bit integers [#1686](https://github.com/open-telemetry/opentelemetry-cpp/pull/1686)
  which includes breaking change in the Metrics api and sdk.
* [Metrics SDK] Add support for Pull Metric Exporter [#1701](https://github.com/open-telemetry/opentelemetry-cpp/pull/1701)
  which includes breaking change in the Metrics api.
* [BUILD] Add CMake OTELCPP_MAINTAINER_MODE [#1650](https://github.com/open-telemetry/opentelemetry-cpp/pull/1650)
* [SEMANTIC CONVENTIONS] Upgrade to version 1.14.0 [#1697](https://github.com/open-telemetry/opentelemetry-cpp/pull/1697)

Notes:

Metrics API/SDK GA release includes PRs [#1686](https://github.com/open-telemetry/opentelemetry-cpp/pull/1686),
[#1701](https://github.com/open-telemetry/opentelemetry-cpp/pull/1701), and
[#1707](https://github.com/open-telemetry/opentelemetry-cpp/pull/1707)
with breaking changes in the Metrics API and SDK.

## [1.6.1] 2022-09-22

* [BUILD] Upgrade opentelemetry-proto to v0.19.0 [#1579](https://github.com/open-telemetry/opentelemetry-cpp/pull/1579)
* [METRICS EXPORTER] Add `OtlpGrpcMetricExporterFactory` and `OtlpHttpMetricExporterFactory`.
  [#1606](https://github.com/open-telemetry/opentelemetry-cpp/pull/1606)
* [SEMANTIC CONVENTIONS] Upgrade to version 1.13.0 [#1624](https://github.com/open-telemetry/opentelemetry-cpp/pull/1624)
* [BUILD] Fixes span creation benchmark issue. [#1622](https://github.com/open-telemetry/opentelemetry-cpp/pull/1622)
* [BUILD] Fix more build warnings (#1616) [#1620](https://github.com/open-telemetry/opentelemetry-cpp/pull/1620)
* [SDK gRPC]: Fix out-of-bounds access of string_view in GrpcClientCarrier in
      the example
      [#1619](https://github.com/open-telemetry/opentelemetry-cpp/pull/1619)
* [EXPORTER ETW] Add Trace flags in SpanContext [#1618](https://github.com/open-telemetry/opentelemetry-cpp/pull/1618)
* [SDK] resource sdk: Update Resource::Merge function docs [#1615](https://github.com/open-telemetry/opentelemetry-cpp/pull/1615)
* [BUILD] Fix build warnings [#1613](https://github.com/open-telemetry/opentelemetry-cpp/pull/1613)
* [API BUILD] Fix header only api singletons (#1520) [#1604](https://github.com/open-telemetry/opentelemetry-cpp/pull/1604)
* [METRICS SDK] Fix default value of
  `OtlpHttpMetricExporterOptions::aggregation_temporality`.
  [#1601](https://github.com/open-telemetry/opentelemetry-cpp/pull/1601)
* [METRICS EXAMPLE] Example for OTLP gRPC exporter for Metrics. [#1598](https://github.com/open-telemetry/opentelemetry-cpp/pull/1598)
* [SDK] Fix `LoggerContext::Shutdown` and tsan of `OtlpHttpClient` [#1592](https://github.com/open-telemetry/opentelemetry-cpp/pull/1592)
* [METRICS SDK] Fix 1585 - Multiple cumulative metric collections without
  measurement recording.
  [#1586](https://github.com/open-telemetry/opentelemetry-cpp/pull/1586)
* [BUILD] metrics warnings [#1583](https://github.com/open-telemetry/opentelemetry-cpp/pull/1583)
* [METRICS SDK] Fix ObservableInstrument::RemoveCallback [#1582](https://github.com/open-telemetry/opentelemetry-cpp/pull/1582)
* [SDK] Add error log when getting a http error code [#1581](https://github.com/open-telemetry/opentelemetry-cpp/pull/1581)
* [EXPORTER] ETW Exporter - Add support for Sampler and ID Generator [#1547](https://github.com/open-telemetry/opentelemetry-cpp/pull/1547)

Notes:

While [OpenTelemetry semantic
convention](https://github.com/open-telemetry/opentelemetry-specification/tree/main/semantic_conventions)
is still in experimental state, PR
[#1624](https://github.com/open-telemetry/opentelemetry-cpp/pull/1624) upgraded
it from 1.12.0 to 1.13.0 which **MAY** break the instrumentation library. Please
update the semantic convention in instrumentation library is needed.

## [1.6.0] 2022-08-15

* [METRICS SDK] Calling Observable Instruments callback during metrics
  collection
  [#1554](https://github.com/open-telemetry/opentelemetry-cpp/pull/1554)
* [METRICS CI] Add CI jobs for new and deprecated metrics [#1531](https://github.com/open-telemetry/opentelemetry-cpp/pull/1531)
* [METRICS BUILD] Fix metrics asan and tsan CI [#1562](https://github.com/open-telemetry/opentelemetry-cpp/pull/1562)
* [METRICS SDK] remove throw check from metrics  with noexcept [#1560](https://github.com/open-telemetry/opentelemetry-cpp/pull/1560)
* [METRICS SDK] fix metrics race condition [#1552](https://github.com/open-telemetry/opentelemetry-cpp/pull/1552)
* [METRICS SDK] Fix metrics context circular reference [#1535](https://github.com/open-telemetry/opentelemetry-cpp/pull/1535)
* [METRICS EXPORTER] Improve scope/instrument names in metrics ostream exporter [#1544](https://github.com/open-telemetry/opentelemetry-cpp/pull/1544)
* [METRICS BUILD] fix IWYU error in instruments.h [#1555](https://github.com/open-telemetry/opentelemetry-cpp/pull/1555)
* [EXPORTER] Prometheus exporter support Gauge Type [#1553](https://github.com/open-telemetry/opentelemetry-cpp/pull/1553)
* [METRICS SDK] Fix default Metric view name [#1515](https://github.com/open-telemetry/opentelemetry-cpp/pull/1515)
* [SDK] Fix infinitely waiting when shutdown with more than one running http
  sessions.
  [#1549](https://github.com/open-telemetry/opentelemetry-cpp/pull/1549)
* [METRICS SDK] Fix OTLP gRPC Metrics env variables [#1543](https://github.com/open-telemetry/opentelemetry-cpp/pull/1543)
* [METRICS SDK] Metric aggregation temporality controls  [#1541](https://github.com/open-telemetry/opentelemetry-cpp/pull/1541)
* [METRICS SDK] Histogram min/max support [#1540](https://github.com/open-telemetry/opentelemetry-cpp/pull/1540)
* [METRICS EXPORTER] ostream exporter should print out resource attributes [#1523](https://github.com/open-telemetry/opentelemetry-cpp/pull/1523)
* [METRICS SDK] Support multiple async callbacks [#1495](https://github.com/open-telemetry/opentelemetry-cpp/pull/1495)
* [BUILD] Fix code scanning alert [#1530](https://github.com/open-telemetry/opentelemetry-cpp/pull/1530)
* [BUILD] Fix several compiling/linking errors [#1539](https://github.com/open-telemetry/opentelemetry-cpp/pull/1539)
* [TRACE SDK] Add SpanData getter for Span Recordable [#1508](https://github.com/open-telemetry/opentelemetry-cpp/pull/1508)
* [LOG SDK] Fix log sdk builder (#1486) [#1524](https://github.com/open-telemetry/opentelemetry-cpp/pull/1524)
* [METRICS SDK] Add configuration options for Aggregation creation [#1513](https://github.com/open-telemetry/opentelemetry-cpp/pull/1513)
* [METRICS TEST] Fix metrics unit test memory leack [#1533](https://github.com/open-telemetry/opentelemetry-cpp/pull/1533)
* [LOG SDK] Add log sdk builders (#1486) [#1524](https://github.com/open-telemetry/opentelemetry-cpp/pull/1524)

## [1.5.0] 2022-07-29

* [EXPORTER BUILD] Add resources to dep list of prometheus exporter test [#1527](https://github.com/open-telemetry/opentelemetry-cpp/pull/1527)
* [BUILD] Don't require applications using jaeger exporter to know about libcurl
  [#1518](https://github.com/open-telemetry/opentelemetry-cpp/pull/1518)
* [EXPORTER] Inline print_value() in ostream exporter [#1512](https://github.com/open-telemetry/opentelemetry-cpp/pull/1512)
* [SDK] fix: urlPaser will incorrect parsing url like `http://abc.com/xxx@xxx/a/b`
  [#1511](https://github.com/open-telemetry/opentelemetry-cpp/pull/1511)
* [SDK] Rename `InstrumentationLibrary` to `InstrumentationScope` [#1507](https://github.com/open-telemetry/opentelemetry-cpp/pull/1507)
* [BUILD] Try to build nlohmann-json only it's depended. [#1505](https://github.com/open-telemetry/opentelemetry-cpp/pull/1505)
* [EXPORTER BUILD] Link opentelemetry_api to ETW exporter test [#1503](https://github.com/open-telemetry/opentelemetry-cpp/pull/1503)
* [SDK] Add automatically semantic conventions from the spec (#873) [#1497](https://github.com/open-telemetry/opentelemetry-cpp/pull/1497)
* [SDK] Use template class for in-memory data. [#1496](https://github.com/open-telemetry/opentelemetry-cpp/pull/1496)
* [SDK] fix compiler warnings [#1499](https://github.com/open-telemetry/opentelemetry-cpp/pull/1499)
* [TRACE SDK] Add trace sdk builders (#1393) [#1471](https://github.com/open-telemetry/opentelemetry-cpp/pull/1471)
* [METRICS BUILD] Enable bazel build for metrics proto files [#1489](https://github.com/open-telemetry/opentelemetry-cpp/pull/1489)
* [METRICS EXPORTER] Add metrics OTLP/HTTP exporter [#1487](https://github.com/open-telemetry/opentelemetry-cpp/pull/1487)
* [EXPORTER] fix otlp grpc exporter naming [#1488](https://github.com/open-telemetry/opentelemetry-cpp/pull/1488)
* [BUILD] Remove `--config Debug` when installing. [#1480](https://github.com/open-telemetry/opentelemetry-cpp/pull/1480)
* [EXPORTER] Fix endpoint in otlp grpc exporter [#1474](https://github.com/open-telemetry/opentelemetry-cpp/pull/1474)
* [EXAMPLE] Fix memory ownership of InMemorySpanExporter (#1473) [#1471](https://github.com/open-telemetry/opentelemetry-cpp/pull/1471)
* [EXPORTER TESTS] Prometheus unit test [#1461](https://github.com/open-telemetry/opentelemetry-cpp/pull/1461)
* [DOCS] Update docs to use relative code links. [#1447](https://github.com/open-telemetry/opentelemetry-cpp/pull/1447)
* [SDK] Remove reference to deprecated InstrumentationLibrary in OTLP [#1469](https://github.com/open-telemetry/opentelemetry-cpp/pull/1469)
* [SDK] Fix trace kIsSampled flag set incorrectly [#1465](https://github.com/open-telemetry/opentelemetry-cpp/pull/1465)
* [METRICS EXPORTER] OTLP gRPC Metrics Exporter [#1454](https://github.com/open-telemetry/opentelemetry-cpp/pull/1454)
* [EXPORTER] fix prometheus exporter failure type [#1460](https://github.com/open-telemetry/opentelemetry-cpp/pull/1460)
* [SDK] Fix build warnings about missing aggregates. [#1368](https://github.com/open-telemetry/opentelemetry-cpp/pull/1368)
* [EXT] `curl::HttpClient` use `curl_multi_handle` instead of creating a thread
  for every request and it's able to reuse connections now. ([#1317](https://github.com/open-telemetry/opentelemetry-cpp/pull/1317))
* [SEMANTIC CONVENTIONS] Upgrade to version 1.12.0 [#873](https://github.com/open-telemetry/opentelemetry-cpp/pull/873)

## [1.4.1] 2022-06-19

* [METRICS SDK] Fix variables inizialization [#1430](https://github.com/open-telemetry/opentelemetry-cpp/pull/1430)
* [DOCS] Fixed broken link to OpenTelemetry.io (#1445) [#1446](https://github.com/open-telemetry/opentelemetry-cpp/pull/1446)
* [BUILD] Upgrade nlohmann_json to 3.10.5 (#1438) [#1441](https://github.com/open-telemetry/opentelemetry-cpp/pull/1441)
* [METRICS SDK] fix histogram [#1440](https://github.com/open-telemetry/opentelemetry-cpp/pull/1440)
* [DOCS] Fix GettingStarted documentation for Jaeger HTTP exporter (#1347) [#1439](https://github.com/open-telemetry/opentelemetry-cpp/pull/1439)
* [BUILD] install sdk-config.h [#1419](https://github.com/open-telemetry/opentelemetry-cpp/pull/1419)
* [EXAMPLE] Log current timestamp instead of epoch time [#1434](https://github.com/open-telemetry/opentelemetry-cpp/pull/1434)
* [METRICS SDK] Add attributes/dimensions to metrics ostream exporter [#1400](https://github.com/open-telemetry/opentelemetry-cpp/pull/1400)
* [SDK] Fix global log handle symbols when using dlopen [#1420](https://github.com/open-telemetry/opentelemetry-cpp/pull/1420)
* [METRICS] Only record non-negative / finite / Non-NAN histogram values([#1427](https://github.com/open-telemetry/opentelemetry-cpp/pull/1427))
* [ETW EXPORTER] Fix ETW log exporter header inclusion [#1426](https://github.com/open-telemetry/opentelemetry-cpp/pull/1426)
* [ETW EXPORTER] Copy string_view passed to ETW exporter in PropertyVariant [#1425](https://github.com/open-telemetry/opentelemetry-cpp/pull/1425)
* [METRICS API/SDK] Pass state to async callback function. [#1408](https://github.com/open-telemetry/opentelemetry-cpp/pull/1408)
* [BUILD] fix nlohmann_json's (third party) include dir [#1415](https://github.com/open-telemetry/opentelemetry-cpp/pull/1415)
* [SDK] fix: WaitOnSocket select error when sockfd above FD_SETSIZE [#1410](https://github.com/open-telemetry/opentelemetry-cpp/pull/1410)
* [SDK] fix OTEL_INTERNAL_LOG_INFO [#1407](https://github.com/open-telemetry/opentelemetry-cpp/pull/1407)
* [DOCS] Document Getting Started with Prometheus and Grafana [#1396](https://github.com/open-telemetry/opentelemetry-cpp/pull/1396)

## [1.4.0] 2022-05-17

* [API SDK] Upgrade proto to v0.17.0, update log data model ([#1383](https://github.com/open-telemetry/opentelemetry-cpp/pull/1383))
* [BUILD] Alpine image ([#1382](https://github.com/open-telemetry/opentelemetry-cpp/pull/1382))
* [LOGS SDK] Get span_id from context when Logger::Log received invalid span_id
  ([#1398](https://github.com/open-telemetry/opentelemetry-cpp/pull/1398))
* [METRICS SDK] Connect async storage with async instruments ([#1388](https://github.com/open-telemetry/opentelemetry-cpp/pull/1388))
* [DOCS] Getting started document using ostream exporter ([#1394](https://github.com/open-telemetry/opentelemetry-cpp/pull/1394))
* [BUILD] Fix missing link to nlohmann_json ([#1390](https://github.com/open-telemetry/opentelemetry-cpp/pull/1390))
* [SDK] Fix sharing resource in batched exported spans ([#1386](https://github.com/open-telemetry/opentelemetry-cpp/pull/1386))
* [PROTOCOL \& LOGS] Upgrade proto to v0.17.0, update log data model ([#1383](https://github.com/open-telemetry/opentelemetry-cpp/pull/1383))
* [METRICS SDK] Remove un-necessary files. ([#1379](https://github.com/open-telemetry/opentelemetry-cpp/pull/1379))
* [EXPORTER] Prometheus exporter meters and instrument name ([#1378](https://github.com/open-telemetry/opentelemetry-cpp/pull/1378))
* [API] Add noexcept/const qualifier at missing places for Trace API. ([#1374](https://github.com/open-telemetry/opentelemetry-cpp/pull/1374))
* [SDK] Fix empty tracestate header propagation ([#1373](https://github.com/open-telemetry/opentelemetry-cpp/pull/1373))
* [METRICS SDK] Reuse temporal metric storage for sync storage ([#1369](https://github.com/open-telemetry/opentelemetry-cpp/pull/1369))
* [SDK] Fix baggage propagation for empty/invalid baggage context ([#1367](https://github.com/open-telemetry/opentelemetry-cpp/pull/1367))
* [BUILD] Export opentelemetry_otlp_recordable ([#1365](https://github.com/open-telemetry/opentelemetry-cpp/pull/1365))
* [TESTS] Disable test on prometheus-cpp which not need ([#1363](https://github.com/open-telemetry/opentelemetry-cpp/pull/1363))
* [METRICS] Fix class member initialization order ([#1360](https://github.com/open-telemetry/opentelemetry-cpp/pull/1360))
* [METRICS SDK] Simplify SDK Configuration: Use View with default aggregation if
  no matching View is configured
  ([#1358](https://github.com/open-telemetry/opentelemetry-cpp/pull/1358))
* [BUILD] Add missing include guard ([#1357](https://github.com/open-telemetry/opentelemetry-cpp/pull/1357))
* [ETW EXPORTER] Fix scalar delete against array ([#1356](https://github.com/open-telemetry/opentelemetry-cpp/pull/1356))
* [ETW EXPORTER] Conditional include for codecvt header ([#1355](https://github.com/open-telemetry/opentelemetry-cpp/pull/1355))
* [BUILD] Use latest TraceLoggingDynamic.h ([#1354](https://github.com/open-telemetry/opentelemetry-cpp/pull/1354))
* [SDK] Add explicit type cast in baggage UrlDecode ([#1353](https://github.com/open-telemetry/opentelemetry-cpp/pull/1353))
* [METRICS SDK] Remove exporter registration to meter provider ([#1350](https://github.com/open-telemetry/opentelemetry-cpp/pull/1350))
* [METRICS SDK] Fix output time in metrics OStream exporter ([#1346](https://github.com/open-telemetry/opentelemetry-cpp/pull/1346))
* [BUILD] ostream metrics cmake ([#1344](https://github.com/open-telemetry/opentelemetry-cpp/pull/1344))
* [BUILD] Link `opentelemetry_ext` with `opentelemetry_api` ([#1336](https://github.com/open-telemetry/opentelemetry-cpp/pull/1336))
* [METRICS SDK] Enable metric collection for Async Instruments - Delta and
  Cumulative
  ([#1334](https://github.com/open-telemetry/opentelemetry-cpp/pull/1334))
* [BUILD] Dependencies image as artifact ([#1333](https://github.com/open-telemetry/opentelemetry-cpp/pull/1333))
* [EXAMPLE] Prometheus example ([#1332](https://github.com/open-telemetry/opentelemetry-cpp/pull/1332))
* [METRICS EXPORTER] Prometheus exporter ([#1331](https://github.com/open-telemetry/opentelemetry-cpp/pull/1331))
* [METRICS] Metrics histogram example ([#1330](https://github.com/open-telemetry/opentelemetry-cpp/pull/1330))
* [TESTS] Replace deprecated googletest API ([#1327](https://github.com/open-telemetry/opentelemetry-cpp/pull/1327))
* [BUILD] Fix Ninja path ([#1326](https://github.com/open-telemetry/opentelemetry-cpp/pull/1326))
* [API] Update yield logic for ARM processor ([#1325](https://github.com/open-telemetry/opentelemetry-cpp/pull/1325))
* [BUILD] Fix metrics compiler warnings ([#1328](https://github.com/open-telemetry/opentelemetry-cpp/pull/1328))
* [METRICS SDK] Implement Merge and Diff operation for Histogram Aggregation ([#1303](https://github.com/open-telemetry/opentelemetry-cpp/pull/1303))

Notes:

While opentelemetry-cpp Logs are still in experimental stage,
[#1383](https://github.com/open-telemetry/opentelemetry-cpp/pull/1383) updated
opentelemetry-proto to 0.17.0, which includes some breaking change in the
protocol, like
[this](https://github.com/open-telemetry/opentelemetry-proto/pull/373). This
makes `name` parameter for our log API unnecessary. However, this parameter is
marked deprecated instead of being removed in this release, and it will be
removed in future release.

## [1.3.0] 2022-04-11

* [ETW EXPORTER] ETW provider handle cleanup ([#1322](https://github.com/open-telemetry/opentelemetry-cpp/pull/1322))
* [BUILD] Move public definitions into `opentelemetry_api`. ([#1314](https://github.com/open-telemetry/opentelemetry-cpp/pull/1314))
* [METRICS] OStream example ([#1312](https://github.com/open-telemetry/opentelemetry-cpp/pull/1312))
* [BUILD] Rename `http_client_curl` to `opentelemetry_http_client_curl` ([#1301](https://github.com/open-telemetry/opentelemetry-cpp/pull/1301))
* [METRICS SDK] Add InstrumentationInfo and Resource to the metrics data to be
  exported.
  ([#1299](https://github.com/open-telemetry/opentelemetry-cpp/pull/1299))
* [TESTS] Add building test without RTTI ([#1294](https://github.com/open-telemetry/opentelemetry-cpp/pull/1294))
* [METRICS SDK] Implement periodic exporting metric reader ([#1286](https://github.com/open-telemetry/opentelemetry-cpp/pull/1286))
* [SDK] Bugfix: span SetAttribute crash ([#1283](https://github.com/open-telemetry/opentelemetry-cpp/pull/1283))
* [BUG] Remove implicitly deleted default constructor ([#1267](https://github.com/open-telemetry/opentelemetry-cpp/pull/1267))
* [METRICS SDK] Synchronous Metric collection (Delta , Cumulative) ([#1265](https://github.com/open-telemetry/opentelemetry-cpp/pull/1265))
* [METRICS SDK] Metrics exemplar round 1 ([#1264](https://github.com/open-telemetry/opentelemetry-cpp/pull/1264))
* [EXPORTER] Fix: use CURLOPT_TIMEOUT_MS to config OtlpHttpExporter's timeout
  instead of CURLOPT_TIMEOUT
  ([#1261](https://github.com/open-telemetry/opentelemetry-cpp/pull/1261))
* [EXPORTER] Jaeger Exporter - Populate Span Links ([#1251](https://github.com/open-telemetry/opentelemetry-cpp/pull/1251))
* [SDK] Reorder the destructor of members in LoggerProvider and TracerProvider ([#1245](https://github.com/open-telemetry/opentelemetry-cpp/pull/1245))
* [METRICS SDK] Enable metric collection from MetricReader ([#1241](https://github.com/open-telemetry/opentelemetry-cpp/pull/1241))
* [METRICS SDK] Asynchronous Aggregation storage ([#1232](https://github.com/open-telemetry/opentelemetry-cpp/pull/1232))
* [METRICS SDK] Synchronous Instruments - Aggregation Storage(s) creation for
  configured views
  ([#1219](https://github.com/open-telemetry/opentelemetry-cpp/pull/1219))
* [BUILD] Added s390x arch into CMake build. ([#1216](https://github.com/open-telemetry/opentelemetry-cpp/pull/1216))
* [API] Allow extension of the lifetime of ContextStorage. ([#1214](https://github.com/open-telemetry/opentelemetry-cpp/pull/1214))
* [METRICS SDK] Add Aggregation storage ([#1213](https://github.com/open-telemetry/opentelemetry-cpp/pull/1213))
* [TESTS] Fix ostream_log_test Mac ([#1208](https://github.com/open-telemetry/opentelemetry-cpp/pull/1208))
* [BUILD] Update grpc to v1.43.2 to support VS2022/MSVC 19.30 and bazel 5.0 ([#1207](https://github.com/open-telemetry/opentelemetry-cpp/pull/1207))
* [DOCS] Benchmark documentation ([#1205](https://github.com/open-telemetry/opentelemetry-cpp/pull/1205))
* [DOCS] Fix errors in SDK documentation ([#1201](https://github.com/open-telemetry/opentelemetry-cpp/pull/1201))
* [METRICS EXPORTER] Ostream metric exporter ([#1196](https://github.com/open-telemetry/opentelemetry-cpp/pull/1196))
* [Metrics SDK] Filtering metrics attributes ([#1191](https://github.com/open-telemetry/opentelemetry-cpp/pull/1191))
* [Metrics SDK] Sync and Async Instruments SDK ([#1184](https://github.com/open-telemetry/opentelemetry-cpp/pull/1184))
* [Metrics SDK] Add Aggregation as part of metrics SDK. ([#1178](https://github.com/open-telemetry/opentelemetry-cpp/pull/1178))
* [BUILD] Cmake: thrift requires boost headers, include them as
  Boost_INCLUDE_DIRS
  ([#1100](https://github.com/open-telemetry/opentelemetry-cpp/pull/1100))

Notes:

[#1301](https://github.com/open-telemetry/opentelemetry-cpp/pull/1301) added
`opentelemetry_` as prefix to http_client_curl library for resolving potential
naming conflict, this could break existing cmake build if http_client_curl is
listed as explicit dependency in user's cmake file.

## [1.2.0] 2022-01-31

* [CI] Continuous benchmark tests as part of the CI ([#1174](https://github.com/open-telemetry/opentelemetry-cpp/pull/1174))
* [API] Allow to use external abseil for bazel targets ([#1172](https://github.com/open-telemetry/opentelemetry-cpp/pull/1172))
* [EXPORTER] Importing gsl::span if std::span is not available ([#1167](https://github.com/open-telemetry/opentelemetry-cpp/pull/1167))
* [EXPORTER] Synchronized calls to Exporter::Export & Shutdown ([#1164](https://github.com/open-telemetry/opentelemetry-cpp/pull/1164))
* [EXPORTER] OTLP http exporter block thread  ([#1163](https://github.com/open-telemetry/opentelemetry-cpp/pull/1163))
* [TESTS] Jaeger: ThriftSender unit test ([#1162](https://github.com/open-telemetry/opentelemetry-cpp/pull/1162))
* [EXPORTER] InMemorySpanExporter shutdown fix ([#1161](https://github.com/open-telemetry/opentelemetry-cpp/pull/1161))
* [EXPORTER] Fix leak in Jaeger exporter ([#1160](https://github.com/open-telemetry/opentelemetry-cpp/pull/1160))
* [TESTS] ZipkinExporter unit-tests ([#1155](https://github.com/open-telemetry/opentelemetry-cpp/pull/1155))
* [SDK] Logger: propagating resources through LoggerProvider ([#1154](https://github.com/open-telemetry/opentelemetry-cpp/pull/1154))
* [SDK] Logger: support for instrumentation library ([#1149](https://github.com/open-telemetry/opentelemetry-cpp/pull/1149))
* [SDK] Add log level for internal log of sdk ([#1147](https://github.com/open-telemetry/opentelemetry-cpp/pull/1147))
* [METRICS] Metrics SDK: View API ([#1110](https://github.com/open-telemetry/opentelemetry-cpp/pull/1110))

Notes on experimental features:

[#1149](https://github.com/open-telemetry/opentelemetry-cpp/pull/1149) and
[#1154](https://github.com/open-telemetry/opentelemetry-cpp/pull/1154) from
above CHANGELOG introduced API changes which are not backward compatible with
previous logs, please update API package to this release if
`ENABLE_LOGS_PREVIEW` is turned on (it is turned off by default).

## [1.1.1] 2021-12-20

* [SDK] Rename OTEL_CPP_GET_ATTR macro, and define it using fully qualified attr
  function
  ([#1140](https://github.com/open-telemetry/opentelemetry-cpp/pull/1140))
* [SDK] Default resource attributes and attributes in OTEL_RESOURCE_ATTRIBUTES
  are missing when using Otlp*LogExporter
  ([#1082](https://github.com/open-telemetry/opentelemetry-cpp/pull/1082))
* [METRICS] Add Meter and MeterProvider in the SDK
  ([#1078](https://github.com/open-telemetry/opentelemetry-cpp/pull/1078))
* [EXPORTER] ZipkinExporter shutdown
  ([#1153](https://github.com/open-telemetry/opentelemetry-cpp/pull/1153))
* [EXPORTER] Jaeger exporter shutdown
  ([#1150](https://github.com/open-telemetry/opentelemetry-cpp/pull/1150))
* [EXPORTER] Bugfix: `jaeger::TUDPTransport::write` crash when `getaddrinfo`
  returns error
  ([#1116](https://github.com/open-telemetry/opentelemetry-cpp/pull/1116))
* [EXPORTER] Bugfix: Jaeger exporter: extend supported attributes types
  ([#1106](https://github.com/open-telemetry/opentelemetry-cpp/pull/1106))
* [EXPORTER] Fix otlp generates null span ids
  ([#1113](https://github.com/open-telemetry/opentelemetry-cpp/pull/1113))
* [EXPORTER] Jaeger bazel (Linux only)
  ([#1077](https://github.com/open-telemetry/opentelemetry-cpp/pull/1077))
* [DOCS] Add note on DLL support
  ([#1137](https://github.com/open-telemetry/opentelemetry-cpp/pull/1137))
* [DOCS] Improve the instructions for Bazel build
  ([#1136](https://github.com/open-telemetry/opentelemetry-cpp/pull/1136))
* [DOCS] Document dependencies
  ([#1119](https://github.com/open-telemetry/opentelemetry-cpp/pull/1119))
* [DOCS] Dockerfile for quick demo/troubleshooting purpose
  ([#905](https://github.com/open-telemetry/opentelemetry-cpp/pull/905))
* [TESTS] Fix data race in BM_ThreadYieldSpinLockThrashing
  ([#1099](https://github.com/open-telemetry/opentelemetry-cpp/pull/1099))
* [EXAMPLE] Otlp gRPC log example
  ([#1083](https://github.com/open-telemetry/opentelemetry-cpp/pull/1083))
* [BUILD] C++20 not Building with VS2019
  ([#1144](https://github.com/open-telemetry/opentelemetry-cpp/pull/1144))
* [BUILD] Mark tags to bazel
  targets([#1075](https://github.com/open-telemetry/opentelemetry-cpp/pull/1075))

## [1.1.0] 2021-11-19

* [BUILD] build release tarball when nlohmann-json not installed
  ([#1074](https://github.com/open-telemetry/opentelemetry-cpp/pull/1074))
* [SDK] Bugfix: regex is neither working on GCC 4.9.x
  ([#1069](https://github.com/open-telemetry/opentelemetry-cpp/pull/1069))
* [SDK] Improvement: span_id should not break strict aliasing.
  ([#1068](https://github.com/open-telemetry/opentelemetry-cpp/pull/1068))
* [EXAMPLE] OTLP HTTP log example
  ([#1062](https://github.com/open-telemetry/opentelemetry-cpp/pull/1062))
* [SDK] OTLP gRPC log export should fail after shutdown
  ([#1064](https://github.com/open-telemetry/opentelemetry-cpp/pull/1064))
* [BUILD] Building otlp exporter from the release tarball
  ([#1056](https://github.com/open-telemetry/opentelemetry-cpp/pull/1056))
* [METRICS] Move old metrics implementation to different directory, and rename
  targets to \_deprecated
  ([#1053](https://github.com/open-telemetry/opentelemetry-cpp/pull/1053))
* [EXPORTER] Add OTLP/gRPC Log Exporter
  ([#1048](https://github.com/open-telemetry/opentelemetry-cpp/pull/1048))
* [EXPORTER] Prometheus Exporter
  ([#1031](https://github.com/open-telemetry/opentelemetry-cpp/pull/1031))
* [EXPORTER] Add OTLP/HTTP Log Exporter
  ([#1030](https://github.com/open-telemetry/opentelemetry-cpp/pull/1030))
* [SDK] fix: issue 368- consistent namespace scope resolution
  ([#1008](https://github.com/open-telemetry/opentelemetry-cpp/pull/1008))

## [1.0.1] 2021-10-21

* [EXPORTER] Exports span attributes to ETW
  ([#1021](https://github.com/open-telemetry/opentelemetry-cpp/pull/1021))
* [BUILD] cmake: add FindThrift.cmake find module method for thrift
  ([#1020](https://github.com/open-telemetry/opentelemetry-cpp/pull/1020))
* [BUILD] Fix nlohmann_json package dependency
  ([#1017](https://github.com/open-telemetry/opentelemetry-cpp/pull/1017))
* [EXPORTER] Change OTLP/HTTP default port from 4317 to 4318
  ([#1018](https://github.com/open-telemetry/opentelemetry-cpp/pull/1018))
* [EXPORTER] ETW Log Exporter
  ([#1006](https://github.com/open-telemetry/opentelemetry-cpp/pull/1006))
* [API] Adding new Logger:log() method
  ([#1005](https://github.com/open-telemetry/opentelemetry-cpp/pull/1005))
* [EXPORTER] Remove scheme from OTLP endpoint before passing to gRPC
  ([#988](https://github.com/open-telemetry/opentelemetry-cpp/pull/988))
* [BUILD] Update opentelemetry-proto for bazel build to 0.9.0
  ([#984](https://github.com/open-telemetry/opentelemetry-cpp/pull/984))
* [BUILD] Cross compling grpc_cpp_plugin not found bug
  ([#982](https://github.com/open-telemetry/opentelemetry-cpp/pull/982))
* [EXPORTER] Support environment variables for both `OtlpGrpcExporter` and
  `OtlpHttpExporter`
  ([#983](https://github.com/open-telemetry/opentelemetry-cpp/pull/983))
* [API/SDK] Add schema_url support to both Resource and InstrumentationLibrary
  ([#979](https://github.com/open-telemetry/opentelemetry-cpp/pull/979))
* [BUILD] Fix build issue where _memcpy_ was not declared in scope
  ([#985](https://github.com/open-telemetry/opentelemetry-cpp/issues/985))

## [1.0.0] 2021-09-16

### API

* Document DefaultSpan, remove DefaultTracer
  ([#959](https://github.com/open-telemetry/opentelemetry-cpp/pull/959))
* Separate baggage<->Context api from Baggage Propagator
  ([#963](https://github.com/open-telemetry/opentelemetry-cpp/pull/963))
* Remove unused public API to_span_ptr
  ([#964](https://github.com/open-telemetry/opentelemetry-cpp/pull/964))
* :collision: Make span context management public
  ([#967](https://github.com/open-telemetry/opentelemetry-cpp/pull/967))
* Support determining parent span from Context while creating new Span
  ([#969](https://github.com/open-telemetry/opentelemetry-cpp/pull/969))
* :collision: Traces: Add note on experimental semantic convention
  implementation, prefix semantics headers with experimental tag
  ([#970](https://github.com/open-telemetry/opentelemetry-cpp/pull/970))
* Increment OPENTELEMETRY_ABI_VERSION_NO to 1
  ([#980](https://github.com/open-telemetry/opentelemetry-cpp/pull/980))

### SDK

* Clean up `GetEnvironmentVariable` and remove unused variable under `NO_GETENV`
  ([#976](https://github.com/open-telemetry/opentelemetry-cpp/pull/976))
* :collision: Resources: Add note on experimental semantic convention
  implementation, prefix semantics headers with experimental tag
  ([#970](https://github.com/open-telemetry/opentelemetry-cpp/pull/970))

### OTLP Exporter

* :bug: Ignore status description if status code is not Error
  ([#962](https://github.com/open-telemetry/opentelemetry-cpp/pull/962))
* :collision: Make Otlp exporter configuration environment variables
  specs-compliant
  ([#974](https://github.com/open-telemetry/opentelemetry-cpp/pull/974))

### Zipkin Exporter

* :bug: Don't set parentId in case parentId is empty
  ([#943](https://github.com/open-telemetry/opentelemetry-cpp/pull/943))
* :rocket: Extend zipkin exporter with ability to provide headers
  ([#951](https://github.com/open-telemetry/opentelemetry-cpp/pull/951))

### DOCS

* :book: Add getting-started documentation for SDK:
  ([#942](https://github.com/open-telemetry/opentelemetry-cpp/pull/942))
* :book: Remove unnecessary spaces and spelling of gRPC in README
  ([#965](https://github.com/open-telemetry/opentelemetry-cpp/pull/965))

### BUILD

* Disable bazel build for gcc 4.8, upgrade versions for grpc(v1.39.1) and
  bazel(4.2.0), document bazel support
  ([#953](https://github.com/open-telemetry/opentelemetry-cpp/pull/953))
* Move CMake config template to cmake folder
  ([#958](https://github.com/open-telemetry/opentelemetry-cpp/pull/958))
* Enable CMake to search the new package variable `<PackageName>_ROOT`
  ([#975](https://github.com/open-telemetry/opentelemetry-cpp/pull/975))
* :bug: Do not override CMAKE_CXX_STANDARD
  ([#977](https://github.com/open-telemetry/opentelemetry-cpp/pull/977))

### :construction: Experiemental Features (Will change in future)

* Semantic Conventions for traces - As the specs is still experimental, the
  implementation will change in future.
* Semantic Convention for resource - As the specs is still experimental, the
  implementation will change in future.
* Logs and Metrics API & SDK - These are not compliant, and are behind feature
  flag. Not recommended to use for now.

## [1.0.0-rc4] 2021-08-04

* [EXPORTER] `BREAKING CHANGE` Fix default HTTP port for OTLP HTTP Exporter
  ([#939](https://github.com/open-telemetry/opentelemetry-cpp/pull/939))
* [API] Fix timeout calculation for Tracer::Flush() and Tracer::Close()
  ([#937](https://github.com/open-telemetry/opentelemetry-cpp/pull/937))
* [API] Fix for Stack::Resize that new_capacity is not assigned to capacity_
  ([#934](https://github.com/open-telemetry/opentelemetry-cpp/pull/934))
* [SDK] Fix race condition in circular buffer simulation test
  ([#931](https://github.com/open-telemetry/opentelemetry-cpp/pull/931))
* [EXPORTER] Fix error logging in Jaeger Exporter
  ([#930](https://github.com/open-telemetry/opentelemetry-cpp/pull/930))
* [BUILD] Use latest grpc version (v1.39.0) for cmake build of otlp exporter
  ([#927](https://github.com/open-telemetry/opentelemetry-cpp/pull/927))
* [EXPORTER] Add Jaeger Thrift HTTP exporter
  ([#926](https://github.com/open-telemetry/opentelemetry-cpp/pull/926))
* [SDK] Move env-var read logic to common dir, and optional reading of env-var
  ([#924](https://github.com/open-telemetry/opentelemetry-cpp/pull/924))
* [EXPORTER] Remove recordable header from CMake install rules for exporters
  ([#923](https://github.com/open-telemetry/opentelemetry-cpp/pull/923))
* [EXPORTER] `BREAKING CHANGE` Rename Recordable to JaegerRecordable in Jaeger
  exporter
  ([#919](https://github.com/open-telemetry/opentelemetry-cpp/pull/919))
* [EXPORTER] `BREAKING CHANGE` Rename Jaeger exporter target
  ([#918](https://github.com/open-telemetry/opentelemetry-cpp/pull/918))
* [EXPORTER] Add Zipkin exporter example
  ([#917](https://github.com/open-telemetry/opentelemetry-cpp/pull/917))
* [EXPORTER] Bazel build for Zipkin exorter
  ([#916](https://github.com/open-telemetry/opentelemetry-cpp/pull/916))
* [BUILD] Allow to use local GSL
  ([#915](https://github.com/open-telemetry/opentelemetry-cpp/pull/915))
* [DOCS] Document vcpkg toolchain configuration
  ([#913](https://github.com/open-telemetry/opentelemetry-cpp/pull/913))
* [SDK] Fix for resource deletion after tracer provider shutdown
  ([#911](https://github.com/open-telemetry/opentelemetry-cpp/pull/911))
* [BUILD] Add bazel build for grpc example
  ([#910](https://github.com/open-telemetry/opentelemetry-cpp/pull/910))
* [EXPORTER] Add resource and instrumentation library support for
  OStreamSpanExporter
  ([#906](https://github.com/open-telemetry/opentelemetry-cpp/pull/906))
* [API] Adding semantic-convention attributes for trace
  ([#868](https://github.com/open-telemetry/opentelemetry-cpp/pull/868))

## [1.0.0-rc3] 2021-07-12

* [DOCS] Add doxygen reference docs for SDK
  ([#902](https://github.com/open-telemetry/opentelemetry-cpp/pull/902))
* [EXPORTER] Jaeger Exporter - Populate resource attributes into process tags in
  Jaeger ([#897](https://github.com/open-telemetry/opentelemetry-cpp/pull/897))
* [EXPORTER] Zipkin Exporter - Report Event timestamp into microseconds
  ([#896](https://github.com/open-telemetry/opentelemetry-cpp/pull/896))
* [EXPORTER] Jaeger Exporter - Handle span events
  ([#895](https://github.com/open-telemetry/opentelemetry-cpp/pull/895))
* [DOCS] Fix API docs for Scope object
  ([#894](https://github.com/open-telemetry/opentelemetry-cpp/pull/894))
* [BUILD] Fix Linking error for libcurl on some macOS environments
  ([#893](https://github.com/open-telemetry/opentelemetry-cpp/pull/893))
* [API] Context cleanup from Runtime Storage
  ([#885](https://github.com/open-telemetry/opentelemetry-cpp/pull/885))
* [DOCS] Document strategy to avoid conflict between two different versions of
  Abseil ([#883](https://github.com/open-telemetry/opentelemetry-cpp/pull/883/))
* [EXPORTER] ETW Exporter - Document example for ETW Exporter
  ([#882](https://github.com/open-telemetry/opentelemetry-cpp/pull/882))
* [SDK] Create Span with Valid spanId and traceId irrespective of Sampling
  decision
  ([#879](https://github.com/open-telemetry/opentelemetry-cpp/pull/879))
* [EXPORTER] Jaeger Exporter - Rename bswap macros to avoid clash on some
  systems ([#876](https://github.com/open-telemetry/opentelemetry-cpp/pull/876))
* [API] Add Semantics Conventions attributes for Resources
  ([#872](https://github.com/open-telemetry/opentelemetry-cpp/pull/872))
* [BUILD] Use nlohmann-json from submodules if not already installed
  ([#870](https://github.com/open-telemetry/opentelemetry-cpp/pull/870))

## [1.0.0-rc2] 2021-06-18

* [EXPORTER] Jaeger Exporter - Support for Instrumentation Library
  ([#864](https://github.com/open-telemetry/opentelemetry-cpp/pull/864))
* [TESTS] Adding benchmark tests for baggage api
  ([#861](https://github.com/open-telemetry/opentelemetry-cpp/pull/861))
* [BUILD] Fix for GCC9/C++20 Support for using STL for modern C++ features
  ([#860](https://github.com/open-telemetry/opentelemetry-cpp/pull/860))
* [TESTS] Adding benchmark tests for span create api
  ([#856](https://github.com/open-telemetry/opentelemetry-cpp/pull/856))
* [BUILD] Fix for using Abseil library for modern C++ features
  ([#850](https://github.com/open-telemetry/opentelemetry-cpp/pull/850))
* [BUILD] Fix issues with win32/x86 compilation
  ([#847](https://github.com/open-telemetry/opentelemetry-cpp/pull/847))
* [DOCS] Document OSS dependencies and their licenses
  ([#844](https://github.com/open-telemetry/opentelemetry-cpp/pull/844))
* [BUILD] Various fixes to build with Visual Studio 2015
  ([#840](https://github.com/open-telemetry/opentelemetry-cpp/pull/840))
* [INSTRUMENTATION] HTTPClient: Change support for full URL argument
  ([#833](https://github.com/open-telemetry/opentelemetry-cpp/pull/833))
* [EXPORTER] Jaeger Exporter - fix endianness of Jaeger IDs for transmission
  ([#832](https://github.com/open-telemetry/opentelemetry-cpp/pull/832))
* [INSTRUMENTATION] fix protobuf compilation warning in gRPC example
  ([#830](https://github.com/open-telemetry/opentelemetry-cpp/pull/830))
* [EXPORTER] `BREAKING CHANGE` - Add OTLP/HTTP+JSON Protocol exporter; Rename
  `OtlpExporter` to `OtlpGrpcExporter`
  ([#810](https://github.com/open-telemetry/opentelemetry-cpp/pull/810))

## [1.0.0-rc1] 2021-06-04

* [BUILD] Enable Jaeger exporter build in Windows
  ([#815](https://github.com/open-telemetry/opentelemetry-cpp/pull/815))
* [DOCS] Versioning doc update to clarify release and versioning policy
  ([#811](https://github.com/open-telemetry/opentelemetry-cpp/pull/811))
* [LOGS] Move Logging implementation under feature-flag
  ([#807](https://github.com/open-telemetry/opentelemetry-cpp/pull/807))
* [BUILD] Filter metric headers files from `opentelemetry-api` and
  `opentelemetry-sdk` targets if metrics feature-flag is disabled
  ([#806](https://github.com/open-telemetry/opentelemetry-cpp/pull/806))
* [BUILD] Fix install rule for ostream exporter, Jaeger, ETW, ElasticSearch
  ([#805](Fix install rule for header files of ostream exporter))
* [API/SDK] Switch from mpark::variant to absl::variant as default
  ([#771](https://github.com/open-telemetry/opentelemetry-cpp/pull/771))
* [API/SDK] Support `const char *` as acceptable data type for attributes and
  resources
  ([#771](https://github.com/open-telemetry/opentelemetry-cpp/pull/771))
* [EXAMPLE] gRPC instrumentation example with context propagation
  ([#729](https://github.com/open-telemetry/opentelemetry-cpp/pull/729))

## [0.7.0] 2021-05-26

* [METRICS] Move metrics api/sdk under preview feature flag
  ([#745](https://github.com/open-telemetry/opentelemetry-cpp/pull/745))
* [DOCS] Add instructions to build using Bazel
  ([#747](https://github.com/open-telemetry/opentelemetry-cpp/pull/747))
* [DOCS] Update copyright headers
  ([#754](https://github.com/open-telemetry/opentelemetry-cpp/pull/754))
* [EXPORTER] Populate resource to OTLP proto data
  ([#758](https://github.com/open-telemetry/opentelemetry-cpp/pull/758))
* [CI] Add CodeQL security scan CI workflow
  ([#770](https://github.com/open-telemetry/opentelemetry-cpp/pull/770))
* [BUILD] Enable building API only CMake Project
  ([#778](https://github.com/open-telemetry/opentelemetry-cpp/pull/778))
* [SDK] Fix for sampling of root span
  ([#784](https://github.com/open-telemetry/opentelemetry-cpp/pull/784))
* [CI] Add Jaeger exporter to CMake CI build
  ([#786](https://github.com/open-telemetry/opentelemetry-cpp/pull/786))
* [API] `BREAKING CHANGE` - Tracer::WithActiveSpan() to return Scope object
  intead of unique_ptr
  ([#788](https://github.com/open-telemetry/opentelemetry-cpp/pull/788))
* [DOCS] Add docs for nested spans and context propagation in readthedocs
  ([#792](https://github.com/open-telemetry/opentelemetry-cpp/pull/792))
* [CI] Output verbose error for failed unit-test in CI
  ([#796](https://github.com/open-telemetry/opentelemetry-cpp/pull/796))

## [0.6.0] 2021-05-11

* [EXPORTER] Add Jaeger exporter
  ([#534](https://github.com/open-telemetry/opentelemetry-cpp/pull/534))
* [SDK] Support multiple processors
  ([#692](https://github.com/open-telemetry/opentelemetry-cpp/pull/692))
* [SDK] Add instrumentation library and multiple tracer support
  ([#693](https://github.com/open-telemetry/opentelemetry-cpp/pull/693))
* [SDK] Resource integration with Exporters
  ([#706](https://github.com/open-telemetry/opentelemetry-cpp/pull/706))
* [EXAMPLE] Enhance existing http example with propagation
  ([#727](https://github.com/open-telemetry/opentelemetry-cpp/pull/727))

## [0.5.0] 2021-04-26

* [SDK] Support custom span-id and trace-id generator
  ([#681](https://github.com/open-telemetry/opentelemetry-cpp/pull/681))
* [SDK] Add SpanContext (and TraceState) to Recordable
  ([#667](https://github.com/open-telemetry/opentelemetry-cpp/pull/667))
* [SDK] Global Propagator
  ([#668](https://github.com/open-telemetry/opentelemetry-cpp/pull/668))
* [SDK] Create SharedContext for updating span pipeline
  ([#650](https://github.com/open-telemetry/opentelemetry-cpp/pull/650))
* [API] Baggage implementation
  ([#676](https://github.com/open-telemetry/opentelemetry-cpp/pull/676))
* [API] Move class from opentelemetry::core namespace to opentelemetry::common
  namespace
  ([#686](https://github.com/open-telemetry/opentelemetry-cpp/pull/686))

## [0.4.0] 2021-04-12

* [EXPORTER] ETW Exporter enhancements
  ([#519](https://github.com/open-telemetry/opentelemetry-cpp/pull/519))
* [EXPORTER] Read Zipkin endpoint from environment variable.
  ([#624](https://github.com/open-telemetry/opentelemetry-cpp/pull/624))
* [EXPORTER] Split Zpages webserver hosting from Exporter
  ([#626](https://github.com/open-telemetry/opentelemetry-cpp/pull/626))
* [EXPORTER] ETW Exporter Usage Instructions
  ([#628](https://github.com/open-telemetry/opentelemetry-cpp/pull/628))
* [INSTRUMENTATION] HTTP Client/Server Instrumentation example
  ([#632](https://github.com/open-telemetry/opentelemetry-cpp/pull/632))
* [EXPORTER] Enable tls authentication for otlp grpc exporter ([#635](Enable tls
  authentication for otlp grpc exporter))
* [API] Refactoring trace_state to reuse common functionality in baggage
  ([#638](https://github.com/open-telemetry/opentelemetry-cpp/pull/638/files))

## [0.3.0] 2021-03-19

* [EXPORTER] Added Zipkin Exporter.
  ([#471](https://github.com/open-telemetry/opentelemetry-cpp/pull/471))
* [API] Added Jaeger propagator.
  ([#599](https://github.com/open-telemetry/opentelemetry-cpp/pull/599))
* [PROPAGATOR] Added Composite Propagator
  ([#597](https://github.com/open-telemetry/opentelemetry-cpp/pull/597))
* [API] Propagate traceflag from parent
  ([#603](https://github.com/open-telemetry/opentelemetry-cpp/pull/603))
* [DOCS] Add sphinx support for api doc generation
  ([595](https://github.com/open-telemetry/opentelemetry-cpp/pull/595))
* [SDK] Add service.name if missing in Resource
  ([#616](https://github.com/open-telemetry/opentelemetry-cpp/pull/616))

## [0.2.0] 2021-03-02

* [SDK] Added `ForceFlush` to `TracerProvider`.
  ([#588](https://github.com/open-telemetry/opentelemetry-cpp/pull/588)).
* [SDK] Added Resource API.
  ([#502](https://github.com/open-telemetry/opentelemetry-cpp/pull/502))
* [API] Modified TraceState support for w3c trace context as per specs.
* [SDK] TraceState implementation as per spec
  ([#551](https://github.com/open-telemetry/opentelemetry-cpp/pull/551))
* [API] Added B3 Propagator.
  ([#523](https://github.com/open-telemetry/opentelemetry-cpp/pull/523))
* [Exporter] Added ETW Exporter.
  ([#376](https://github.com/open-telemetry/opentelemetry-cpp/pull/376))
* [CI] Enable cache for Bazel for faster builds.
  ([#505](https://github.com/open-telemetry/opentelemetry-cpp/pull/505))

## [0.0.1] 2020-12-16

### Added

* Trace API and SDK experimental
* OTLP Exporter

### Changed

### Removed
