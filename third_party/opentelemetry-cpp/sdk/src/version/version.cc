// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

// Do not edit versions directly.
// See file tbump.toml

#include "opentelemetry/sdk/version/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace version
{
const int major_version    = 1;
const int minor_version    = 28;
const int patch_version    = 0;
const char *pre_release    = "";
const char *build_metadata = "none";
const char *short_version  = "1.28.0";
const char *full_version   = "1.28.0";
/**
 * Release date.
 * For published releases: YYYY-MM-DD
 * For -dev releases: empty string
 */
const char *build_date = "2026-07-16";
}  // namespace version
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
