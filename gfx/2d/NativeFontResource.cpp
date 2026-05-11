/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "2D.h"
#include "nsIMemoryReporter.h"

namespace mozilla {
namespace gfx {

static Atomic<size_t> gTotalNativeFontResourceData;

NativeFontResource::NativeFontResource(size_t aDataLength)
    : mDataLength(aDataLength) {
  gTotalNativeFontResourceData += mDataLength;
}

NativeFontResource::~NativeFontResource() {
  gTotalNativeFontResourceData -= mDataLength;
}

// Memory reporter that estimates the amount of memory that is currently being
// allocated internally by various native font APIs for native font resources.
// The sanest way to do this, given that NativeFontResources can be created and
// used in many different threads or processes and given that such memory is
// implicitly allocated by the native APIs, is just to maintain a global atomic
// counter and report this value as such.
class NativeFontResourceDataMemoryReporter final : public nsIMemoryReporter {
  ~NativeFontResourceDataMemoryReporter() = default;

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    MOZ_COLLECT_REPORT("explicit/gfx/native-font-resource-data", KIND_HEAP,
                       UNITS_BYTES, gTotalNativeFontResourceData,
                       "Total memory used by native font API resource data.");
    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(NativeFontResourceDataMemoryReporter, nsIMemoryReporter)

void NativeFontResource::RegisterMemoryReporter() {
  RegisterStrongMemoryReporter(
      MakeAndAddRef<NativeFontResourceDataMemoryReporter>());
}

}  // namespace gfx
}  // namespace mozilla
