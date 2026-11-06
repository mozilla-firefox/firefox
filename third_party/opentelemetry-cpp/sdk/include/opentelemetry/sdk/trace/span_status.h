// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{
namespace detail
{

/**
 * Outcome of applying the span status transition rules to a SetStatus call.
 */
struct StatusTransition
{
  /// Whether the call changes the status. When false, the SetStatus call must be ignored.
  bool accepted{false};

  /// Status code to record.
  opentelemetry::trace::StatusCode code{opentelemetry::trace::StatusCode::kUnset};

  /// Description to record.
  nostd::string_view description{""};
};

/**
 * @brief Applies the span status transition rules from the trace API specification.
 *
 * @param current Status code recorded so far, kUnset if SetStatus was never called.
 * @param code Status code passed to SetStatus.
 * @param description Description passed to SetStatus.
 */
inline StatusTransition ApplyStatusTransition(opentelemetry::trace::StatusCode current,
                                              opentelemetry::trace::StatusCode code,
                                              nostd::string_view description) noexcept
{
  const auto is_already_ok{current == opentelemetry::trace::StatusCode::kOk};
  const auto is_unset{code == opentelemetry::trace::StatusCode::kUnset};

  if (is_already_ok || is_unset)
  {
    return StatusTransition{false, current, nostd::string_view{""}};
  }

  const auto keep_description{(code == opentelemetry::trace::StatusCode::kError) &&
                              !description.empty()};
  const auto effective_description{keep_description ? description : nostd::string_view{""}};

  return StatusTransition{true, code, effective_description};
}

}  // namespace detail
}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
