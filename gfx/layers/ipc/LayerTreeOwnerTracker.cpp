/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LayerTreeOwnerTracker.h"

#include <functional>

#include "mozilla/StaticPtr.h"              // for StaticAutoPtr
#include "mozilla/gfx/GPUChild.h"           // for GPUChild
#include "mozilla/gfx/GPUProcessManager.h"  // for GPUProcessManager
#include "mozilla/layers/LayerTreeLog.h"

namespace mozilla {
namespace layers {

LazyLogModule gLayerTreeLog("LayerTree");

static StaticAutoPtr<LayerTreeOwnerTracker> sSingleton;

LayerTreeOwnerTracker::LayerTreeOwnerTracker()
    : mLayerIdsLock("LayerTreeOwnerTrackerLock") {}

void LayerTreeOwnerTracker::Initialize() {
  MOZ_RELEASE_ASSERT(!sSingleton);
  sSingleton = new LayerTreeOwnerTracker();
}

void LayerTreeOwnerTracker::Shutdown() { sSingleton = nullptr; }

LayerTreeOwnerTracker* LayerTreeOwnerTracker::Get() { return sSingleton; }

void LayerTreeOwnerTracker::Map(LayersId aLayersId, GeckoChildID aChildID) {
  MutexAutoLock lock(mLayerIdsLock);

  LAYERTREE_LOG("Map layer tree {:x} to child {}", aLayersId, aChildID);

  // Add the mapping to the list
  const auto i = mLayerIds.insert({aLayersId, aChildID});
  MOZ_RELEASE_ASSERT(i.second || i.first->second == aChildID,
                     "Mapping layers ID already owned by another process!");
}

void LayerTreeOwnerTracker::Unmap(LayersId aLayersId, GeckoChildID aChildID) {
  MutexAutoLock lock(mLayerIdsLock);

  auto i = mLayerIds.find(aLayersId);
  if (i == mLayerIds.end()) {
    return;
  }

  if (NS_WARN_IF(i->second != aChildID)) {
    MOZ_ASSERT_UNREACHABLE("Unmapping layers ID for another process!");
    return;
  }

  LAYERTREE_LOG("Unmap layer tree {:x} from child {}", aLayersId, aChildID);
  mLayerIds.erase(i);
}

bool LayerTreeOwnerTracker::IsMapped(LayersId aLayersId,
                                     GeckoChildID aChildID) {
  MutexAutoLock lock(mLayerIdsLock);

  auto iter = mLayerIds.find(aLayersId);
  return iter != mLayerIds.end() && iter->second == aChildID;
}

void LayerTreeOwnerTracker::Iterate(
    const std::function<void(LayersId aLayersId, GeckoChildID aChildID)>&
        aCallback) {
  MutexAutoLock lock(mLayerIdsLock);

  for (const auto& iter : mLayerIds) {
    aCallback(iter.first, iter.second);
  }
}

}  // namespace layers
}  // namespace mozilla
