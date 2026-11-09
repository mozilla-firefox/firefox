// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/sdk/trace/span_limits.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{
namespace span_limits_env
{

SpanLimits GetSpanLimitsFromEnv(const SpanLimits &defaults)
{
  return defaults;
}

}  // namespace span_limits_env
}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
