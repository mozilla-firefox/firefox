// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once
// Please refer to provider.h for documentation on how to obtain a Tracer object.
//
// This file is part of the internal implementation of OpenTelemetry. Nothing in this file should be
// used directly. Please refer to span.h and tracer.h for documentation on these interfaces.

#include <cstdint>
#include <memory>
#include <utility>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/context/context_value.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/unique_ptr.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/default_span.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_context_kv_iterable.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/span_startoptions.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/trace/tracer_provider.h"
#include "opentelemetry/version.h"

namespace trace_api = opentelemetry::trace;

OPENTELEMETRY_BEGIN_NAMESPACE
namespace trace
{
/**
 * No-op implementation of Span. This class should not be used directly.
 */
class OPENTELEMETRY_EXPORT NoopSpan final : public Span
{
public:
  explicit NoopSpan() : span_context_{new SpanContext(false, false)} {}

  explicit NoopSpan(nostd::unique_ptr<SpanContext> span_context)
      : span_context_{std::move(span_context)}
  {}

  void SetAttribute(nostd::string_view /*key*/,
                    const common::AttributeValue & /*value*/) noexcept override
  {}

  void AddEvent(nostd::string_view /*name*/) noexcept override {}

  void AddEvent(nostd::string_view /*name*/,
                common::SystemTimestamp /*timestamp*/) noexcept override
  {}

  void AddEvent(nostd::string_view /* name */,
                const common::KeyValueIterable & /* attributes */) noexcept override
  {}

  void AddEvent(nostd::string_view /*name*/,
                common::SystemTimestamp /*timestamp*/,
                const common::KeyValueIterable & /*attributes*/) noexcept override
  {}

#if OPENTELEMETRY_ABI_VERSION_NO >= 2
  void AddLink(const SpanContext & /* target */,
               const common::KeyValueIterable & /* attrs */) noexcept override
  {}

  void AddLinks(const SpanContextKeyValueIterable & /* links */) noexcept override {}
#endif

  void SetStatus(StatusCode /*code*/, nostd::string_view /*description*/) noexcept override {}

  void UpdateName(nostd::string_view /*name*/) noexcept override {}

  void End(const EndSpanOptions & /*options*/) noexcept override {}

  bool IsRecording() const noexcept override { return false; }

  SpanContext GetContext() const noexcept override { return *span_context_.get(); }

private:
  nostd::unique_ptr<SpanContext> span_context_;
};

/**
 * No-op implementation of Tracer.
 */
class OPENTELEMETRY_EXPORT NoopTracer final : public Tracer,
                                              public std::enable_shared_from_this<NoopTracer>
{
public:
  // Tracer
  NoopTracer() : noop_span_(new trace::NoopSpan())
  {
#if OPENTELEMETRY_ABI_VERSION_NO >= 2
    UpdateEnabled(false);
#endif
  }

  nostd::shared_ptr<Span> StartSpan(nostd::string_view /*name*/,
                                    const common::KeyValueIterable & /*attributes*/,
                                    const SpanContextKeyValueIterable & /*links*/,
                                    const StartSpanOptions &options) noexcept override
  {
    const auto parent_context = [&options]() noexcept -> SpanContext {
      if (const auto *span_context = nostd::get_if<SpanContext>(&options.parent))
      {
        if (span_context->IsValid())
        {
          return *span_context;
        }
      }
      else if (const auto *context = nostd::get_if<context::Context>(&options.parent))
      {
        auto ctx_span_context = GetSpanContext(*context);
        if (ctx_span_context.IsValid())
        {
          return ctx_span_context;
        }
        if (IsRootSpan(*context))
        {
          return SpanContext::GetInvalid();
        }
      }

      return GetSpanContext(context::RuntimeContext::GetCurrent());
    }();
    if (!parent_context.IsValid())
    {
      return noop_span_;
    }
    return nostd::shared_ptr<Span>{std::make_shared<DefaultSpan>(parent_context)};
  }

#if OPENTELEMETRY_ABI_VERSION_NO == 1

  void ForceFlushWithMicroseconds(uint64_t /*timeout*/) noexcept override {}

  void CloseWithMicroseconds(uint64_t /*timeout*/) noexcept override {}

#endif /* OPENTELEMETRY_ABI_VERSION_NO */

private:
  std::shared_ptr<trace::Span> noop_span_;
};

/**
 * No-op implementation of a TracerProvider.
 */
class OPENTELEMETRY_EXPORT NoopTracerProvider final : public trace::TracerProvider
{
public:
  NoopTracerProvider() : tracer_{nostd::shared_ptr<trace::NoopTracer>(new trace::NoopTracer)} {}

#if OPENTELEMETRY_ABI_VERSION_NO >= 2
  nostd::shared_ptr<trace::Tracer> GetTracer(
      nostd::string_view /* name */,
      nostd::string_view /* version */,
      nostd::string_view /* schema_url */,
      const common::KeyValueIterable * /* attributes */) noexcept override
  {
    return tracer_;
  }
#else
  nostd::shared_ptr<trace::Tracer> GetTracer(nostd::string_view /* name */,
                                             nostd::string_view /* version */,
                                             nostd::string_view /* schema_url */) noexcept override
  {
    return tracer_;
  }
#endif

private:
  nostd::shared_ptr<trace::Tracer> tracer_;
};
}  // namespace trace
OPENTELEMETRY_END_NAMESPACE
