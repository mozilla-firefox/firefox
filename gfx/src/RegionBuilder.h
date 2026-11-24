/* This Source Code Form is subject to the terms of the Mozilla Public
 *  * License, v. 2.0. If a copy of the MPL was not distributed with this
 *  * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef RegionBuilder_h_
#define RegionBuilder_h_

#include "mozilla/ArrayView.h"
#include "nsTArray.h"

template <typename RegionType>
class RegionBuilder {
 public:
  typedef typename RegionType::RectType RectType;

  RegionBuilder() = default;

  void OrWith(const RectType& aRect) { mRects.AppendElement(aRect); }

  RegionType ToRegion() const {
    return RegionType(mozilla::gfx::ArrayView<RectType>(mRects));
  }

 private:
  nsTArray<RectType> mRects;
};

#endif  // RegionBuilder_h_
