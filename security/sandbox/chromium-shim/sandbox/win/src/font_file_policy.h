/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SANDBOX_WIN_SRC_FONT_FILE_POLICY_H_
#define SANDBOX_WIN_SRC_FONT_FILE_POLICY_H_

#include "sandbox/win/src/policy_low_level.h"

namespace sandbox {

class FontFilePolicy {
 public:
  // Creates the read-only low-level policy rules for font files, which only
  // match if the Gecko font file validator accepts the path.
  static bool GenerateRules(LowLevelPolicy* policy);
};

}  // namespace sandbox

#endif  // SANDBOX_WIN_SRC_FONT_FILE_POLICY_H_
