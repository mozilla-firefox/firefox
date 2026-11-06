// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/context/context.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/trace/default_span.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace trace
{

// Get Span from explicit context
inline nostd::shared_ptr<Span> GetSpan(const context::Context &context) noexcept
{
  context::ContextValue span_value = context.GetValue(kSpanKey);
  if (const nostd::shared_ptr<Span> *value = nostd::get_if<nostd::shared_ptr<Span>>(&span_value))
  {
    return *value;
  }
  return nostd::shared_ptr<Span>(new DefaultSpan(SpanContext::GetInvalid()));
}

// Get the SpanContext of the active span from explicit context. Falls back to a SpanContext
// stored directly in the context, and returns an invalid SpanContext if neither is present.
inline SpanContext GetSpanContext(const context::Context &context) noexcept
{
  const context::ContextValue context_value = context.GetValue(kSpanKey);

  if (nostd::holds_alternative<nostd::monostate>(context_value))
  {
    // The context does not have a span or span context
    return SpanContext::GetInvalid();
  }

  // Get the span metadata from the active span in the context.
  if (const nostd::shared_ptr<Span> *maybe_span =
          nostd::get_if<nostd::shared_ptr<Span>>(&context_value))
  {
    return *maybe_span ? (*maybe_span)->GetContext() : SpanContext::GetInvalid();
  }
  // Get the span metadata directly from a SpanContext in the context.
  // TODO: This path is unused and may be removed in the future.
  if (const nostd::shared_ptr<SpanContext> *maybe_span_context =
          nostd::get_if<nostd::shared_ptr<SpanContext>>(&context_value))
  {
    return *maybe_span_context ? **maybe_span_context : SpanContext::GetInvalid();
  }
  return SpanContext::GetInvalid();
}

// Check if the context is from a root span
inline bool IsRootSpan(const context::Context &context) noexcept
{
  context::ContextValue is_root_span = context.GetValue(kIsRootSpanKey);
  if (const bool *value = nostd::get_if<bool>(&is_root_span))
  {
    return *value;
  }
  return false;
}

// Set Span into explicit context
inline context::Context SetSpan(context::Context &context,
                                const nostd::shared_ptr<Span> &span) noexcept
{
  return context.SetValue(kSpanKey, span);
}

}  // namespace trace
OPENTELEMETRY_END_NAMESPACE
