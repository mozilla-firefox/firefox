/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScrollableLayerGuid.h"

#include <ostream>
#include "mozilla/HashFunctions.h"  // for HashGeneric
#include "mozilla/IntegerPrintfMacros.h"
#include "nsPrintfCString.h"  // for nsPrintfCString

namespace mozilla {
namespace layers {

ScrollableLayerGuid::ScrollableLayerGuid(LayersId aLayersId,
                                         uint32_t aPresShellId,
                                         ViewID aScrollId)
    : mLayersId(aLayersId), mPresShellId(aPresShellId), mScrollId(aScrollId) {}

bool ScrollableLayerGuid::operator==(const ScrollableLayerGuid& other) const {
  return mLayersId == other.mLayersId && mPresShellId == other.mPresShellId &&
         mScrollId == other.mScrollId;
}

bool ScrollableLayerGuid::operator!=(const ScrollableLayerGuid& other) const {
  return !(*this == other);
}

bool ScrollableLayerGuid::operator<(const ScrollableLayerGuid& other) const {
  if (mLayersId < other.mLayersId) {
    return true;
  }
  if (mLayersId == other.mLayersId) {
    if (mPresShellId < other.mPresShellId) {
      return true;
    }
    if (mPresShellId == other.mPresShellId) {
      return mScrollId < other.mScrollId;
    }
  }
  return false;
}

std::ostream& operator<<(std::ostream& aOut, const ScrollableLayerGuid& aGuid) {
  return aOut << nsPrintfCString("{ l=0x%" PRIx64 ", p=%u, v=%" PRIu64 " }",
                                 uint64_t(aGuid.mLayersId), aGuid.mPresShellId,
                                 aGuid.mScrollId)
                     .get();
}

bool ScrollableLayerGuid::EqualsIgnoringPresShell(
    const ScrollableLayerGuid& aA, const ScrollableLayerGuid& aB) {
  return aA.mLayersId == aB.mLayersId && aA.mScrollId == aB.mScrollId;
}

std::size_t ScrollableLayerGuid::HashFn::operator()(
    const ScrollableLayerGuid& aGuid) const {
  return HashGeneric(uint64_t(aGuid.mLayersId), aGuid.mPresShellId,
                     aGuid.mScrollId);
}

std::size_t ScrollableLayerGuid::HashIgnoringPresShellFn::operator()(
    const ScrollableLayerGuid& aGuid) const {
  return HashGeneric(uint64_t(aGuid.mLayersId), aGuid.mScrollId);
}

bool ScrollableLayerGuid::EqualIgnoringPresShellFn::operator()(
    const ScrollableLayerGuid& lhs, const ScrollableLayerGuid& rhs) const {
  return EqualsIgnoringPresShell(lhs, rhs);
}

}  // namespace layers
}  // namespace mozilla
