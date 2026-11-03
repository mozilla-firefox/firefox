/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FrameUniformityData.h"

#include "mozilla/TimeStamp.h"
#include "mozilla/dom/APZTestDataBinding.h"
#include "mozilla/dom/ToJSValue.h"
#include "nsTArray.h"

namespace mozilla {
namespace layers {

using namespace gfx;

bool FrameUniformityData::ToJS(JS::MutableHandle<JS::Value> aOutValue,
                               JSContext* aContext) {
  nsTArray<dom::FrameUniformity> layers(mUniformities.size());
  for (const auto& [layerAddr, uniformity] : mUniformities) {
    dom::FrameUniformity& entry = *layers.AppendElement();
    entry.mLayerAddress.Construct() = layerAddr;
    entry.mFrameUniformity.Construct() = uniformity;
  }

  dom::FrameUniformityResults results;
  results.mLayerUniformities.Construct(std::move(layers));

  return dom::ToJSValue(aContext, results, aOutValue);
}

}  // namespace layers
}  // namespace mozilla
