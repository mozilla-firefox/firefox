// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace resource
{
namespace detail
{

// Decodes valid percent-encoded sequences in value.
// Invalid or incomplete percent-encoded sequences are left unchanged.
std::string PercentDecode(const std::string &value);

}  // namespace detail
}  // namespace resource
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
