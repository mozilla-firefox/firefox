// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <stdint.h>
#include <chrono>
#include <map>
#include <mutex>
#include <new>
#include <utility>

#include "opentelemetry/context/context.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/instrumentationscope/scope_configurator.h"
#include "opentelemetry/sdk/trace/id_generator.h"
#include "opentelemetry/sdk/trace/sampler.h"
#include "opentelemetry/sdk/trace/tracer.h"
#include "opentelemetry/sdk/trace/tracer_config.h"
#include "opentelemetry/sdk/trace/tracer_context.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/noop.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/span_startoptions.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/trace/trace_state.h"
#include "opentelemetry/version.h"

#include "src/trace/span.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{
Tracer::Tracer(std::shared_ptr<TracerContext> context,
               std::unique_ptr<InstrumentationScope> instrumentation_scope) noexcept
    : instrumentation_scope_{std::move(instrumentation_scope)},
      context_{std::move(context)},
      tracer_config_(context_->GetTracerConfigurator().ComputeConfig(*instrumentation_scope_))
{
  UpdateEnabled(tracer_config_.IsEnabled());
}

nostd::shared_ptr<opentelemetry::trace::Span> Tracer::StartSpan(
    nostd::string_view name,
    const opentelemetry::common::KeyValueIterable &attributes,
    const opentelemetry::trace::SpanContextKeyValueIterable &links,
    const opentelemetry::trace::StartSpanOptions &options) noexcept
{
  // Check if the tracer is enabled using the API Tracer::Enabled() accessor if available.
  if (!Enabled())
  {
    static const std::shared_ptr<opentelemetry::trace::NoopTracer> kNoopTracer =
        std::make_shared<opentelemetry::trace::NoopTracer>();
    return kNoopTracer->StartSpan(name, attributes, links, options);
  }

  // make sure to always overwrite this parent_context
  bool get_current_context = true;
  opentelemetry::trace::SpanContext parent_context(false, false);
  if (const opentelemetry::trace::SpanContext *span_context =
          nostd::get_if<opentelemetry::trace::SpanContext>(&options.parent))
  {
    if (span_context->IsValid())
    {
      parent_context      = *span_context;
      get_current_context = false;
    }
  }
  else if (const context::Context *context = nostd::get_if<context::Context>(&options.parent))
  {
    // fetch span context from parent span stored in the context
    auto parent_span_context = opentelemetry::trace::GetSpan(*context)->GetContext();
    if (parent_span_context.IsValid())
    {
      parent_context      = parent_span_context;
      get_current_context = false;
    }
    else
    {
      if (opentelemetry::trace::IsRootSpan(*context))
      {
        get_current_context = false;
      }
    }
  }

  if (get_current_context)
  {
    parent_context = GetCurrentSpan()->GetContext();
  }

  IdGenerator &generator = GetIdGenerator();
  opentelemetry::trace::TraceId trace_id;
  opentelemetry::trace::SpanId span_id = generator.GenerateSpanId();
  bool is_parent_span_valid            = false;
  uint8_t flags                        = 0;

  if (parent_context.IsValid())
  {
    trace_id             = parent_context.trace_id();
    flags                = parent_context.trace_flags().flags();
    is_parent_span_valid = true;
  }
  else
  {
    trace_id = generator.GenerateTraceId();
    if (generator.IsRandom())
    {
      flags = opentelemetry::trace::TraceFlags::kIsRandom;
    }
  }

  auto sampling_result = context_->GetSampler().ShouldSample(parent_context, trace_id, name,
                                                             options.kind, attributes, links);
  if (sampling_result.IsSampled())
  {
    flags |= opentelemetry::trace::TraceFlags::kIsSampled;
  }
  else
  {
    flags &= ~opentelemetry::trace::TraceFlags::kIsSampled;
  }

#if 0
  /* https://github.com/open-telemetry/opentelemetry-specification as of v1.29.0 */
  /* Support W3C Trace Context version 1. */
  flags &= opentelemetry::trace::TraceFlags::kAllW3CTraceContext1Flags;
#endif

#if 1
  /* Waiting for https://github.com/open-telemetry/opentelemetry-specification/issues/3411 */
  /* Support W3C Trace Context version 2. */
  flags &= opentelemetry::trace::TraceFlags::kAllW3CTraceContext2Flags;
#endif

  opentelemetry::trace::TraceFlags trace_flags(flags);

  auto span_context =
      std::unique_ptr<opentelemetry::trace::SpanContext>(new opentelemetry::trace::SpanContext(
          trace_id, span_id, trace_flags, false,
          sampling_result.trace_state ? sampling_result.trace_state
          : is_parent_span_valid      ? parent_context.trace_state()
                                      : opentelemetry::trace::TraceState::GetDefault()));

  if (!sampling_result.IsRecording())
  {
    // create no-op span with valid span-context.

    auto noop_span = nostd::shared_ptr<opentelemetry::trace::Span>{
        new (std::nothrow)
            opentelemetry::trace::NoopSpan(this->shared_from_this(), std::move(span_context))};
    return noop_span;
  }
  else
  {

    auto span = nostd::shared_ptr<opentelemetry::trace::Span>{
        new (std::nothrow) Span{this->shared_from_this(), name, attributes, links, options,
                                parent_context, std::move(span_context)}};

    // if the attributes is not nullptr, add attributes to the span.
    if (sampling_result.attributes)
    {
      for (auto &kv : *sampling_result.attributes)
      {
        span->SetAttribute(kv.first, kv.second);
      }
    }

    return span;
  }
}

void Tracer::ForceFlushWithMicroseconds(uint64_t timeout) noexcept
{
  if (context_)
  {
    context_->ForceFlush(
        std::chrono::microseconds{static_cast<std::chrono::microseconds::rep>(timeout)});
  }
}

void Tracer::CloseWithMicroseconds(uint64_t timeout) noexcept
{
  // Trace context is shared by many tracers.So we just call ForceFlush to flush all pending spans
  // and do not shutdown it.
  if (context_)
  {
    context_->ForceFlush(
        std::chrono::microseconds{static_cast<std::chrono::microseconds::rep>(timeout)});
  }
}

void Tracer::UpdateTracerConfig(TracerConfig config) noexcept
{
  const bool enabled = config.IsEnabled();
  {
    std::lock_guard<std::mutex> lock(tracer_config_mutex_);
    tracer_config_ = config;
  }
  UpdateEnabled(enabled);
}

}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
