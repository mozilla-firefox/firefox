/*
 * Copyright The OpenTelemetry Authors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * DO NOT EDIT, this is an Auto-generated file from:
 * buildscripts/semantic-convention/templates/registry/semantic_attributes-h.j2
 */

#pragma once

#include "opentelemetry/common/macros.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace semconv
{
namespace telemetry
{

/**
  The name of the auto instrumentation agent or distribution, if used.
  <p>
  Official auto instrumentation agents and distributions SHOULD set the @code telemetry.distro.name
  @endcode attribute to a string starting with @code opentelemetry- @endcode, e.g. @code
  opentelemetry-java-instrumentation @endcode.
 */
static constexpr const char *kTelemetryDistroName = "telemetry.distro.name";

/**
  The version string of the auto instrumentation agent or distribution, if used.
 */
static constexpr const char *kTelemetryDistroVersion = "telemetry.distro.version";

/**
  The language of the telemetry SDK.
 */
static constexpr const char *kTelemetrySdkLanguage = "telemetry.sdk.language";

/**
  The name of the telemetry SDK as defined above.
  <p>
  The OpenTelemetry SDK MUST set the @code telemetry.sdk.name @endcode attribute to @code
  opentelemetry @endcode. If another SDK, like a fork or a vendor-provided implementation, is used,
  this SDK MUST set the
  @code telemetry.sdk.name @endcode attribute to the fully-qualified class or module name of this
  SDK's main entry point or another suitable identifier depending on the language. The identifier
  @code opentelemetry @endcode is reserved and MUST NOT be used in this case. All custom identifiers
  SHOULD be stable across different versions of an implementation.
 */
static constexpr const char *kTelemetrySdkName = "telemetry.sdk.name";

/**
  The version string of the telemetry SDK.
 */
static constexpr const char *kTelemetrySdkVersion = "telemetry.sdk.version";

namespace TelemetrySdkLanguageValues
{
/**
  <a href="https://opentelemetry.io/docs/languages/cpp/">C++</a>
 */
static constexpr const char *kCpp = "cpp";

/**
  <a href="https://opentelemetry.io/docs/languages/dotnet/">.NET</a>
 */
static constexpr const char *kDotnet = "dotnet";

/**
  <a href="https://opentelemetry.io/docs/languages/erlang/">Erlang/Elixir</a>
 */
static constexpr const char *kErlang = "erlang";

/**
  <a href="https://opentelemetry.io/docs/languages/go/">Go</a>
 */
static constexpr const char *kGo = "go";

/**
  <a href="https://opentelemetry.io/docs/languages/java/">Java</a>
 */
static constexpr const char *kJava = "java";

/**
  <a href="https://opentelemetry.io/docs/languages/kotlin/">Kotlin</a>
 */
static constexpr const char *kKotlin = "kotlin";

/**
  <a href="https://opentelemetry.io/docs/languages/js/">Node.js</a>
 */
static constexpr const char *kNodejs = "nodejs";

/**
  <a href="https://opentelemetry.io/docs/languages/php/">PHP</a>
 */
static constexpr const char *kPhp = "php";

/**
  <a href="https://opentelemetry.io/docs/languages/python/">Python</a>
 */
static constexpr const char *kPython = "python";

/**
  <a href="https://opentelemetry.io/docs/languages/ruby/">Ruby</a>
 */
static constexpr const char *kRuby = "ruby";

/**
  <a href="https://opentelemetry.io/docs/languages/rust/">Rust</a>
 */
static constexpr const char *kRust = "rust";

/**
  <a href="https://opentelemetry.io/docs/languages/swift/">Swift</a>
 */
static constexpr const char *kSwift = "swift";

/**
  <a href="https://opentelemetry.io/docs/languages/js/">Browser</a>
 */
static constexpr const char *kWebjs = "webjs";

}  // namespace TelemetrySdkLanguageValues

}  // namespace telemetry
}  // namespace semconv
OPENTELEMETRY_END_NAMESPACE
