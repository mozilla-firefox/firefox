/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "sandbox/win/src/font_file_policy_opcode.h"

#include <string_view>

#include "mozilla/sandboxing/SandboxInitialization.h"
#include "sandbox/win/BrokerGeckoClient.h"

namespace sandbox {

PolicyOpcode* OpcodeFactory::MakeOpFontFileMatch(uint8_t selected_param,
                                                 uint32_t options) {
  return MakeBase(OP_FONT_FILE_MATCH, options, selected_param);
}

template <>
EvalResult OpcodeEval<OP_FONT_FILE_MATCH>(PolicyOpcode* opcode,
                                          const ParameterSet* param,
                                          MatchContext* context) {
  std::wstring_view path;
  if (!param->Get(&path)) {
    return EVAL_ERROR;
  }

  if (IsSandboxedProcess()) {
    return EVAL_TRUE;
  }

  return mozilla::sandboxing::GeckoValidateFontFile(path) ? EVAL_TRUE
                                                          : EVAL_FALSE;
}

}  // namespace sandbox
