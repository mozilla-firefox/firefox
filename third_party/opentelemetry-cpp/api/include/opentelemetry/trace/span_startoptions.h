// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace trace
{

/**
 * Provides options for creating a Span.
 */
struct StartSpanOptions
{
  /**
   * Optionally sets the system-clock start time of the Span.
   *
   * If a start time is set, both the system-clock and steady-clock timestamps
   * must be provided. The system clock places the Span relative to Spans from
   * other systems, while the steady clock measures its duration accurately.
   */
  common::SystemTimestamp start_system_time;

  /** The steady-clock start time corresponding to start_system_time. */
  common::SteadyTimestamp start_steady_time;

  /**
   * Explicitly sets the parent of the Span.
   *
   * A valid `SpanContext` identifies the parent directly. An invalid
   * `SpanContext` falls back to the currently active Span, if one exists;
   * otherwise, the new Span is created as a root Span.
   *
   * When a `context::Context` is provided, a valid Span in that Context is used
   * as the parent. Otherwise, if the Context contains `is_root_span` set to
   * `true`, the new Span is created without a parent. If neither is present,
   * the currently active Span is used as the parent, if one exists; otherwise,
   * the new Span is created as a root Span.
   *
   * Example:
   * ```cpp
   * opentelemetry::trace::StartSpanOptions options;
   * options.parent = opentelemetry::context::Context{
   *     opentelemetry::trace::kIsRootSpanKey, true};
   * auto root_span = tracer->StartSpan("span root", options);
   * ```
   */
  nostd::variant<SpanContext, context::Context> parent = context::Context{};

  /** The role of the Span in a trace. */
  SpanKind kind = SpanKind::kInternal;
};

}  // namespace trace
OPENTELEMETRY_END_NAMESPACE
