/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <limits>
#include <memory>

#include "mozHunspellRLBoxHost.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Try.h"
#include "nsContentUtils.h"
#include "nsILoadInfo.h"
#include "nsNetUtil.h"
#include "nsUnicharUtils.h"

#include "hunspell_csutil.hxx"

using namespace mozilla;

mozHunspellFileMgrHost::mozHunspellFileMgrHost(const nsCString& aFilename) {
  nsCOMPtr<nsIChannel> channel;
  DebugOnly<Result<Ok, nsresult>> result = Open(aFilename, channel, mStream);
  NS_WARNING_ASSERTION(result.value.isOk(), "Failed to open Hunspell file");
}

/* static */
Result<Ok, nsresult> mozHunspellFileMgrHost::Open(
    const nsCString& aPath, nsCOMPtr<nsIChannel>& aChannel,
    nsCOMPtr<nsIInputStream>& aStream) {
  nsCOMPtr<nsIURI> uri;
  MOZ_TRY(NS_NewURI(getter_AddRefs(uri), aPath));

  MOZ_TRY(NS_NewChannel(
      getter_AddRefs(aChannel), uri, nsContentUtils::GetSystemPrincipal(),
      nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT,
      nsIContentPolicy::TYPE_OTHER));

  MOZ_TRY(aChannel->Open(getter_AddRefs(aStream)));
  return Ok();
}

Result<Ok, nsresult> mozHunspellFileMgrHost::ReadLine(nsACString& aLine) {
  if (!mStream) {
    return Err(NS_ERROR_NOT_INITIALIZED);
  }

  bool ok;
  MOZ_TRY(NS_ReadLine(mStream.get(), &mLineBuffer, aLine, &ok));
  if (!ok) {
    mStream = nullptr;
  }

  mLineNum++;
  return Ok();
}

/* static */
Result<int64_t, nsresult> mozHunspellFileMgrHost::GetSize(
    const nsCString& aFilename) {
  int64_t ret = -1;

  nsCOMPtr<nsIChannel> channel;
  nsCOMPtr<nsIInputStream> stream;
  MOZ_TRY(Open(aFilename, channel, stream));

  channel->GetContentLength(&ret);
  return ret;
}

bool mozHunspellFileMgrHost::GetLine(nsACString& aResult) {
  return !ReadLine(aResult).isErr();
}

/* static */
uint32_t mozHunspellCallbacks::sCurrentFreshId = 0;
/* static */
mozilla::StaticRWLock mozHunspellCallbacks::sFileMgrMapLock;
/* static */
MOZ_RUNINIT std::map<uint32_t, std::unique_ptr<mozHunspellFileMgrHost>>
    mozHunspellCallbacks::sFileMgrMap;
/* static */
MOZ_RUNINIT std::set<nsCString> mozHunspellCallbacks::sFileMgrAllowList;

/* static */
void mozHunspellCallbacks::AllowFile(const nsCString& aFilename) {
  mozilla::StaticAutoWriteLock lock(sFileMgrMapLock);
  sFileMgrAllowList.insert(aFilename);
}

/* static */
void mozHunspellCallbacks::Clear() {
  mozilla::StaticAutoWriteLock lock(sFileMgrMapLock);
  sCurrentFreshId = 0;
  sFileMgrMap.clear();
  sFileMgrAllowList.clear();
}

/* static */
tainted_hunspell<uint32_t> mozHunspellCallbacks::CreateFilemgr(
    rlbox_sandbox_hunspell& /*aSandbox*/,
    tainted_hunspell<const char*> t_aFilename) {
  mozilla::StaticAutoWriteLock lock(sFileMgrMapLock);

  return t_aFilename.copy_and_verify_string(
      [&](std::unique_ptr<char[]> aFilename) {
        nsCString cFilename = nsDependentCString(aFilename.get());

        // Ensure that the filename is in the allowlist
        auto it = sFileMgrAllowList.find(cFilename);
        MOZ_RELEASE_ASSERT(it != sFileMgrAllowList.end());

        // Get new id
        uint32_t freshId = GetFreshId();
        // Save mapping of id to file manager
        sFileMgrMap[freshId] =
            std::make_unique<mozHunspellFileMgrHost>(cFilename);

        return freshId;
      });
}

/* static */
uint32_t mozHunspellCallbacks::GetFreshId() {
  // i is uint64_t to prevent overflow during loop increment which would cause
  // an infinite loop
  for (uint64_t i = sCurrentFreshId; i < std::numeric_limits<uint32_t>::max();
       i++) {
    auto it = sFileMgrMap.find(i);
    if (it == sFileMgrMap.end()) {
      // set sCurrentFreshId to the next (possibly) available id
      sCurrentFreshId = i + 1;
      return static_cast<uint32_t>(i);
    }
  }

  MOZ_CRASH("Ran out of unique file ids for hunspell dictionaries");
}

/* static */
mozHunspellFileMgrHost& mozHunspellCallbacks::GetMozHunspellFileMgrHost(
    tainted_hunspell<uint32_t> t_aFd) {
  mozilla::StaticAutoReadLock lock(sFileMgrMapLock);
  uint32_t aFd = t_aFd.copy_and_verify([](uint32_t aFd) { return aFd; });
  auto iter = sFileMgrMap.find(aFd);
  MOZ_RELEASE_ASSERT(iter != sFileMgrMap.end());
  return *(iter->second.get());
}

/* static */
tainted_hunspell<bool> mozHunspellCallbacks::GetLine(
    rlbox_sandbox_hunspell& aSandbox, tainted_hunspell<uint32_t> t_aFd,
    tainted_hunspell<char**> t_aLinePtr) {
  mozHunspellFileMgrHost& inst =
      mozHunspellCallbacks::GetMozHunspellFileMgrHost(t_aFd);
  nsAutoCString line;
  bool ok = inst.GetLine(line);
  // If the getline fails, return a null which is "graceful" failure
  if (ok) {
    // Copy the line into the sandbox. This memory is eventually freed by
    // hunspell.
    size_t size = line.Length() + 1;
    tainted_hunspell<char*> t_line = aSandbox.malloc_in_sandbox<char>(size);

    if (t_line == nullptr) {
      // If malloc fails, we should go to "graceful" failure path
      ok = false;
    } else {
      rlbox::memcpy(aSandbox, t_line, line.get(), size);
    }
    *t_aLinePtr = t_line;
  } else {
    *t_aLinePtr = nullptr;
  }
  return ok;
}

/* static */
tainted_hunspell<int> mozHunspellCallbacks::GetLineNum(
    rlbox_sandbox_hunspell& /*aSandbox*/, tainted_hunspell<uint32_t> t_aFd) {
  mozHunspellFileMgrHost& inst =
      mozHunspellCallbacks::GetMozHunspellFileMgrHost(t_aFd);
  int num = inst.GetLineNum();
  return num;
}

/* static */
void mozHunspellCallbacks::DestructFilemgr(rlbox_sandbox_hunspell& /*aSandbox*/,
                                           tainted_hunspell<uint32_t> t_aFd) {
  mozilla::StaticAutoWriteLock lock(sFileMgrMapLock);
  uint32_t aFd = t_aFd.copy_and_verify([](uint32_t aFd) { return aFd; });

  auto iter = sFileMgrMap.find(aFd);
  if (iter != sFileMgrMap.end()) {
    sFileMgrMap.erase(iter);
  }
}

// Callbacks for using Firefox's encoding instead of hunspell's

/* static */
tainted_hunspell<uint32_t> mozHunspellCallbacks::ToUpperCase(
    rlbox_sandbox_hunspell& /*aSandbox*/, tainted_hunspell<uint32_t> t_aChar) {
  uint32_t aChar =
      t_aChar.copy_and_verify([](uint32_t aChar) { return aChar; });
  return ::ToUpperCase(aChar);
}

/* static */
tainted_hunspell<uint32_t> mozHunspellCallbacks::ToLowerCase(
    rlbox_sandbox_hunspell& /*aSandbox*/, tainted_hunspell<uint32_t> t_aChar) {
  uint32_t aChar =
      t_aChar.copy_and_verify([](uint32_t aChar) { return aChar; });
  return ::ToLowerCase(aChar);
}

/* static */ tainted_hunspell<struct cs_info*>
mozHunspellCallbacks::GetCurrentCS(rlbox_sandbox_hunspell& aSandbox,
                                   tainted_hunspell<const char*> t_es) {
  tainted_hunspell<struct cs_info*> t_ccs =
      aSandbox.malloc_in_sandbox<struct cs_info>(256);
  MOZ_RELEASE_ASSERT(t_ccs);
  return t_es.copy_and_verify_string([&](std::unique_ptr<char[]> es) {
    struct cs_info* ccs = hunspell_get_current_cs(es.get());
    rlbox::memcpy(aSandbox, t_ccs, ccs, sizeof(struct cs_info) * 256);
    delete[] ccs;
    return t_ccs;
  });
}
