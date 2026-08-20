// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox/win/src/registry_policy.h"

#include <ntstatus.h>
#include <stdint.h>

#include <string>

#include "base/check.h"
#include "sandbox/win/src/ipc_tags.h"
#include "sandbox/win/src/policy_engine_opcodes.h"
#include "sandbox/win/src/policy_params.h"
#include "sandbox/win/src/sandbox_nt_util.h"
#include "sandbox/win/src/win_utils.h"

namespace sandbox {
namespace {

struct ObjectAttribs : public OBJECT_ATTRIBUTES {
  UNICODE_STRING uni_name;
  ObjectAttribs(const std::wstring& name, ULONG attributes, HANDLE root) {
    ::RtlInitUnicodeString(&uni_name, name.c_str());
    InitializeObjectAttributes(this, &uni_name, attributes, root, nullptr);
  }
};

static const uint32_t kAllowedRegFlags =
    KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY | KEY_READ |
    GENERIC_READ | GENERIC_EXECUTE | READ_CONTROL | KEY_WOW64_64KEY |
    KEY_WOW64_32KEY;

// Opens the key referenced by |obj_attributes| with |access| and
// checks what permission was given. Remove the WRITE flags and update
// |access| with the new value.
NTSTATUS TranslateMaximumAllowed(OBJECT_ATTRIBUTES* obj_attributes,
                                 DWORD* access) {
  NtOpenKeyFunction NtOpenKey = nullptr;
  ResolveNTFunctionPtr("NtOpenKey", &NtOpenKey);

  NtCloseFunction NtClose = nullptr;
  ResolveNTFunctionPtr("NtClose", &NtClose);

  NtQueryObjectFunction NtQueryObject = nullptr;
  ResolveNTFunctionPtr("NtQueryObject", &NtQueryObject);

  // Open the key.
  HANDLE handle;
  NTSTATUS status = NtOpenKey(&handle, *access, obj_attributes);
  if (!NT_SUCCESS(status))
    return status;

  OBJECT_BASIC_INFORMATION info = {0};
  status = NtQueryObject(handle, ObjectBasicInformation, &info, sizeof(info),
                         nullptr);
  CHECK(NT_SUCCESS(NtClose(handle)));
  if (!NT_SUCCESS(status))
    return status;

  *access = info.GrantedAccess & kAllowedRegFlags;
  return STATUS_SUCCESS;
}

NTSTATUS NtOpenKeyInTarget(HANDLE* target_key_handle,
                           ACCESS_MASK desired_access,
                           OBJECT_ATTRIBUTES* obj_attributes,
                           HANDLE target_process) {
  *target_key_handle = nullptr;
  NtOpenKeyFunction NtOpenKey = nullptr;
  ResolveNTFunctionPtr("NtOpenKey", &NtOpenKey);

  if (MAXIMUM_ALLOWED & desired_access) {
    NTSTATUS status = TranslateMaximumAllowed(obj_attributes, &desired_access);
    if (!NT_SUCCESS(status)) {
      return STATUS_ACCESS_DENIED;
    }
  }

  HANDLE local_handle = INVALID_HANDLE_VALUE;
  NTSTATUS status = NtOpenKey(&local_handle, desired_access, obj_attributes);

  if (!NT_SUCCESS(status)) {
    return status;
  }

  if (!::DuplicateHandle(::GetCurrentProcess(), local_handle, target_process,
                         target_key_handle, 0, false,
                         DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS)) {
    return STATUS_ACCESS_DENIED;
  }
  return STATUS_SUCCESS;
}

// Holds the information about a known registry key.
struct KnownReservedKey {
  const wchar_t* name;
  HKEY key;
};

// Contains all the known registry key by name and by handle.
const KnownReservedKey kKnownKey[] = {
    {L"HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT},
    {L"HKEY_CURRENT_USER", HKEY_CURRENT_USER},
    {L"HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE},
    {L"HKEY_USERS", HKEY_USERS},
    {L"HKEY_PERFORMANCE_DATA", HKEY_PERFORMANCE_DATA},
    {L"HKEY_PERFORMANCE_TEXT", HKEY_PERFORMANCE_TEXT},
    {L"HKEY_PERFORMANCE_NLSTEXT", HKEY_PERFORMANCE_NLSTEXT},
    {L"HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG},
    {L"HKEY_DYN_DATA", HKEY_DYN_DATA}};

std::optional<std::wstring> ResolveRegistryName(const std::wstring& name) {
  for (auto i : kKnownKey) {
    if (name.find(i.name) == 0) {
      HKEY key;
      DWORD disposition;
      if (ERROR_SUCCESS != ::RegCreateKeyEx(i.key, L"", 0, nullptr, 0,
                                            MAXIMUM_ALLOWED, nullptr, &key,
                                            &disposition)) {
        return std::nullopt;
      }

      auto result = sandbox::GetPathFromHandle(key);
      ::RegCloseKey(key);

      if (!result) return std::nullopt;

      result->append(name.substr(wcslen(i.name)));
      return result;
    }
  }
  return std::nullopt;
}

}  // namespace

bool RegistryPolicy::GenerateRules(const wchar_t* name,
                                   LowLevelPolicy* policy) {
  std::wstring name_str(name);
  if (name_str.empty()) {
    return false;
  }

  auto resolved_name = ResolveRegistryName(name_str);
  if (!resolved_name) {
    return false;
  }

  EvalResult result = ASK_BROKER;

  PolicyRule open(result);
  PolicyRule create(result);

  // We consider all flags that are not known to be readonly as potentially
  // used for write. Here we also support MAXIMUM_ALLOWED, but we are going
  // to expand it to read-only before the call.
  uint32_t restricted_flags = ~(kAllowedRegFlags | MAXIMUM_ALLOWED);
  open.AddNumberMatch(IF_NOT, OpenKey::ACCESS, restricted_flags, AND);
  create.AddNumberMatch(IF_NOT, OpenKey::ACCESS, restricted_flags, AND);

  if (!create.AddStringMatch(IF, OpenKey::NAME, resolved_name->c_str()) ||
      !policy->AddRule(IpcTag::NTCREATEKEY, &create)) {
    return false;
  }

  if (!open.AddStringMatch(IF, OpenKey::NAME, resolved_name->c_str()) ||
      !policy->AddRule(IpcTag::NTOPENKEY, &open)) {
    return false;
  }

  return true;
}

bool RegistryPolicy::OpenKeyAction(EvalResult eval_result,
                                   const ClientInfo& client_info,
                                   const std::wstring& key,
                                   uint32_t attributes,
                                   HANDLE root_directory,
                                   uint32_t desired_access,
                                   HANDLE* handle,
                                   NTSTATUS* nt_status) {
  // The only action supported is ASK_BROKER which means open the requested
  // file as specified.
  if (ASK_BROKER != eval_result) {
    *nt_status = STATUS_ACCESS_DENIED;
    return false;
  }

  ObjectAttribs obj_attributes(key, attributes, root_directory);
  *nt_status = NtOpenKeyInTarget(handle, desired_access, &obj_attributes,
                                 client_info.process);
  return true;
}

}  // namespace sandbox
