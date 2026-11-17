/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SANDBOX_WIN_SRC_FONT_FILE_POLICY_OPCODE_H_
#define SANDBOX_WIN_SRC_FONT_FILE_POLICY_OPCODE_H_

#include "sandbox/win/src/policy_engine_opcodes.h"

namespace sandbox {

template <int>
EvalResult OpcodeEval(PolicyOpcode* opcode,
                      const ParameterSet* pp,
                      MatchContext* match);

// In the target, this always matches, so that the request is sent to the
// broker. In the broker, this matches if the Gecko font file validator accepts
// the path.
template <>
EvalResult OpcodeEval<OP_FONT_FILE_MATCH>(PolicyOpcode* opcode,
                                          const ParameterSet* param,
                                          MatchContext* context);

}  // namespace sandbox

#endif  // SANDBOX_WIN_SRC_FONT_FILE_POLICY_OPCODE_H_
