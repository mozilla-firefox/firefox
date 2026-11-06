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
