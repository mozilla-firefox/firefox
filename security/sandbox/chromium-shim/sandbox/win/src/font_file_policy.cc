/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "sandbox/win/src/font_file_policy.h"

#include "sandbox/win/src/filesystem_policy.h"
#include "sandbox/win/src/ipc_tags.h"
#include "sandbox/win/src/policy_engine_opcodes.h"
#include "sandbox/win/src/policy_params.h"
#include "sandbox/win/src/sandbox_policy_base.h"

namespace sandbox {

namespace {

// Quick filter to prevent brokering for incompatible paths. The real check is
// done by the Gecko font file validator in the broker.
constexpr wchar_t kFontFilePattern[] = L"\\??\\*:\\*";

}  // namespace

bool PolicyRule::AddFontFileMatch(uint8_t parameter) {
  if (done_) {
    // Do not allow to add more rules after generating the action opcode.
    return false;
  }

  if (!opcode_factory_->MakeOpFontFileMatch(parameter, kPolNone)) {
    return false;
  }
  ++buffer_->opcode_count;
  return true;
}

ResultCode ConfigBase::AllowFontFileAccess() {
  if (!FontFilePolicy::GenerateRules(PolicyMaker())) {
    return SBOX_ERROR_BAD_PARAMS;
  }
  return SBOX_ALL_OK;
}

bool FontFilePolicy::GenerateRules(LowLevelPolicy* policy) {
  PolicyRule create(ASK_BROKER);
  PolicyRule open(ASK_BROKER);
  PolicyRule query(ASK_BROKER);
  PolicyRule query_full(ASK_BROKER);

  for (PolicyRule* rule : {&create, &open}) {
    if (!rule->AddNumberMatch(IF_NOT, OpenFile::ACCESS,
                              FileSystemPolicy::kReadonlyRestrictedFlags,
                              AND) ||
        !rule->AddNumberMatch(IF, OpenFile::OPENONLY, true, EQUAL)) {
      return false;
    }
  }

  for (PolicyRule* rule : {&create, &open, &query, &query_full}) {
    if (!rule->AddStringMatch(IF, OpenFile::NAME, kFontFilePattern) ||
        !rule->AddFontFileMatch(OpenFile::NAME)) {
      return false;
    }
  }

  return policy->AddRule(IpcTag::NTCREATEFILE, &create) &&
         policy->AddRule(IpcTag::NTOPENFILE, &open) &&
         policy->AddRule(IpcTag::NTQUERYATTRIBUTESFILE, &query) &&
         policy->AddRule(IpcTag::NTQUERYFULLATTRIBUTESFILE, &query_full);
}

}  // namespace sandbox
