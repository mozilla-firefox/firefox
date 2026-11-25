/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebRenderDrawEventRecorder.h"
#include "mozilla/layers/SharedSurfacesChild.h"
#include "mozilla/layers/SharedSurfacesParent.h"

namespace mozilla {
using namespace gfx;

namespace layers {

void WebRenderDrawEventRecorder::StoreSourceSurfaceRecording(
    SourceSurface* aSurface, const char* aReason) {
  wr::ExternalImageId extId;
  nsresult rv = layers::SharedSurfacesChild::Share(aSurface, extId);
  if (NS_FAILED(rv)) {
    DrawEventRecorderMemory::StoreSourceSurfaceRecording(aSurface, aReason);
    return;
  }

  StoreExternalSurfaceRecording(aSurface, wr::AsUint64(extId));
}

already_AddRefed<SourceSurface> WebRenderTranslator::LookupExternalSurface(
    uint64_t aKey) {
  return SharedSurfacesParent::GetForIdNamespace(mIdNamespace,
                                                 wr::ToExternalImageId(aKey));
}

}  // namespace layers
}  // namespace mozilla
