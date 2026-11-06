// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/common/macros.h"  // IWYU pragma: export
#include "opentelemetry/detail/preprocessor.h"

#ifndef OPENTELEMETRY_ABI_VERSION_NO
// NOLINTNEXTLINE(cppcoreguidelines-macro-to-enum)
#  define OPENTELEMETRY_ABI_VERSION_NO 1
#endif

// NOLINTBEGIN(cppcoreguidelines-macro-to-enum)
#define OPENTELEMETRY_VERSION "1.28.0"
#define OPENTELEMETRY_VERSION_MAJOR 1
#define OPENTELEMETRY_VERSION_MINOR 28
#define OPENTELEMETRY_VERSION_PATCH 0
// NOLINTEND(cppcoreguidelines-macro-to-enum)

#define OPENTELEMETRY_ABI_VERSION OPENTELEMETRY_STRINGIFY(OPENTELEMETRY_ABI_VERSION_NO)

// clang-format off
#define OPENTELEMETRY_BEGIN_NAMESPACE \
  namespace opentelemetry { inline namespace OPENTELEMETRY_CONCAT(v, OPENTELEMETRY_ABI_VERSION_NO) {

#define OPENTELEMETRY_END_NAMESPACE \
  }}

#define OPENTELEMETRY_NAMESPACE opentelemetry :: OPENTELEMETRY_CONCAT(v, OPENTELEMETRY_ABI_VERSION_NO)

// clang-format on

// Experimental: bound synchronous metric instruments (Counter, Histogram).
// This public API is available only in ABI v2 preview builds. Guard bound
// instrument code with OPENTELEMETRY_HAVE_METRICS_BOUND_INSTRUMENTS_PREVIEW.
#if OPENTELEMETRY_ABI_VERSION_NO >= 2 && defined(ENABLE_METRICS_BOUND_INSTRUMENTS_PREVIEW)
#  define OPENTELEMETRY_HAVE_METRICS_BOUND_INSTRUMENTS_PREVIEW 1
#endif
