/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScrollGeneration.h"

#include <ostream>

namespace mozilla {

template <typename Tag>
ScrollGeneration<Tag>::ScrollGeneration(uint64_t aValue) : mValue(aValue) {}

template <typename Tag>
std::ostream& operator<<(std::ostream& aStream,
                         const ScrollGeneration<Tag>& aGen) {
  return aStream << aGen.mValue;
}

template struct ScrollGeneration<APZTag>;
template struct ScrollGeneration<MainThreadTag>;

template std::ostream& operator<<(std::ostream&,
                                  const ScrollGeneration<APZTag>&);
template std::ostream& operator<<(std::ostream&,
                                  const ScrollGeneration<MainThreadTag>&);

}  // namespace mozilla
