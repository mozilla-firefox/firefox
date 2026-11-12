/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "common/RandomHex.h"

#include <iomanip>
#include <sstream>

#include "mozilla/Assertions.h"
#include "pk11pub.h"

namespace mozilla {

std::string GetRandomHex(size_t aWords) {
  std::ostringstream os;

  for (size_t i = 0; i < aWords; ++i) {
    uint32_t rand;
    SECStatus rv = PK11_GenerateRandom(reinterpret_cast<unsigned char*>(&rand),
                                       sizeof(rand));
    if (rv != SECSuccess) {
      MOZ_CRASH();
      return "";
    }

    os << std::hex << std::setfill('0') << std::setw(8) << rand;
  }
  return os.str();
}

}  // namespace mozilla
