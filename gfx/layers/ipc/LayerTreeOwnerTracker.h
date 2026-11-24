/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_LayerTreeOwnerTracker_h
#define mozilla_layers_LayerTreeOwnerTracker_h

#include <functional>
#include <map>

#include "LayersTypes.h"          // for LayersId
#include "mozilla/Mutex.h"        // for mozilla::Mutex
#include "mozilla/ProcessType.h"  // for GeckoChildID

namespace mozilla {

namespace dom {
class ContentParent;
}

namespace layers {

/**
 * A utility class for tracking which content processes should be allowed
 * to access which layer trees.
 *
 * GeckoChildID's are used to track which content process can access the layer
 * tree, and in the case of nested browser's we use the top level content
 * processes' GeckoChildID.
 *
 * This class is only available in the main process and gpu process. Mappings
 * are synced from main process to the gpu process. The actual syncing happens
 * in GPUProcessManager, and so this class should not be used directly.
 */
class LayerTreeOwnerTracker final {
 public:
  static void Initialize();
  static void Shutdown();
  static LayerTreeOwnerTracker* Get();

  /**
   * Map aLayersId and aChildID together so that that process
   * can access that layer tree.
   */
  void Map(LayersId aLayersId, GeckoChildID aChildID);

  /**
   * Remove an existing mapping.
   */
  void Unmap(LayersId aLayersId, GeckoChildID aChildID);

  /**
   * Checks whether it is okay for aChildID to access aLayersId.
   */
  bool IsMapped(LayersId aLayersId, GeckoChildID aChildID);

  void Iterate(const std::function<void(LayersId aLayersId,
                                        GeckoChildID aChildID)>& aCallback);

 private:
  LayerTreeOwnerTracker();

  mozilla::Mutex mLayerIdsLock MOZ_UNANNOTATED;
  std::map<LayersId, GeckoChildID> mLayerIds;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_LayerTreeOwnerTracker_h
