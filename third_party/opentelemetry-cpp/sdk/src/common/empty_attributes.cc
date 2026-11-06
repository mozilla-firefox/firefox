// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <stdint.h>
#include <array>
#include <string>
#include <utility>

#include "opentelemetry/common/key_value_iterable_view.h"
#include "opentelemetry/nostd/utility.h"
#include "opentelemetry/sdk/common/empty_attributes.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{

const opentelemetry::common::KeyValueIterableView<std::array<std::pair<std::string, int32_t>, 0>> &
GetEmptyAttributes() noexcept
{
  static const std::array<std::pair<std::string, int32_t>, 0> array{};
  static const opentelemetry::common::KeyValueIterableView<
      std::array<std::pair<std::string, int32_t>, 0>>
      kEmptyAttributes(array);

  return kEmptyAttributes;
}

}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
