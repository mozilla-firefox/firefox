/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_cache_FileUtilsImpl_h
#define mozilla_dom_cache_FileUtilsImpl_h

#include "mozilla/dom/FlippedOnce.h"
#include "mozilla/dom/cache/FileUtils.h"
#include "mozilla/dom/quota/ResultExtensions.h"

namespace mozilla::dom::cache {

template <typename Func>
nsresult BodyTraverseFilesForCleanup(
    const Maybe<CacheDirectoryMetadata>& aDirectoryMetadata, nsIFile& aBodyDir,
    const Func& aHandleFileFunc) {
  MOZ_DIAGNOSTIC_ASSERT(aDirectoryMetadata.isSome());

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  {
    nsCOMPtr<nsIFile> parentFile;
    nsresult rv = aBodyDir.GetParent(getter_AddRefs(parentFile));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    MOZ_DIAGNOSTIC_ASSERT(parentFile);

    nsAutoString leafName;
    rv = parentFile->GetLeafName(leafName);
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));

    MOZ_DIAGNOSTIC_ASSERT(StringEndsWith(leafName, kMorgueDirectory));
  }
#endif

  FlippedOnce<true> isEmpty;
  QM_TRY(quota::CollectEachFile(
      aBodyDir,
      [&isEmpty, &aDirectoryMetadata, &aHandleFileFunc](
          const nsCOMPtr<nsIFile>& file) -> Result<Ok, nsresult> {
        QM_TRY_INSPECT(const auto& dirEntryKind, quota::GetDirEntryKind(*file));

        switch (dirEntryKind) {
          case quota::nsIFileKind::ExistsAsDirectory: {
            // If it's a directory somehow, try to remove it and move on
            DebugOnly<nsresult> result = RemoveNsIFileRecursively(
                aDirectoryMetadata, *file, /* aTrackQuota */ false);
            MOZ_ASSERT(NS_SUCCEEDED(result));
            break;
          }

          case quota::nsIFileKind::ExistsAsFile: {
            nsAutoCString leafName;
            QM_TRY(MOZ_TO_RESULT(file->GetNativeLeafName(leafName)));

            // Delete all tmp files regardless of known bodies. These are all
            // considered orphans.
            if (StringEndsWith(leafName, ".tmp"_ns)) {
              DebugOnly<nsresult> result = RemoveNsIFile(
                  aDirectoryMetadata, *file, /* aTrackQuota */ false);
              MOZ_ASSERT(NS_SUCCEEDED(result));
              return Ok{};
            }
            // Otherwise, it must be a .final file.
            QM_WARNONLY_TRY_UNWRAP(const auto maybeEndingOk,
                                   OkIf(StringEndsWith(leafName, ".final"_ns)));

            // If its not, try to remove it and move on.
            if (!maybeEndingOk) {
              DebugOnly<nsresult> result = RemoveNsIFile(
                  aDirectoryMetadata, *file, /* aTrackQuota */ false);
              MOZ_ASSERT(NS_SUCCEEDED(result));
              return Ok{};
            }

            QM_TRY_INSPECT(const bool& fileDeleted,
                           aHandleFileFunc(*file, leafName));
            if (fileDeleted) {
              return Ok{};
            }

            isEmpty.EnsureFlipped();
            break;
          }

          case quota::nsIFileKind::DoesNotExist:
            // Ignore files that got removed externally while iterating.
            break;
        }

        return Ok{};
      }));

  if (isEmpty) {
    DebugOnly<nsresult> result = RemoveNsIFileRecursively(
        aDirectoryMetadata, aBodyDir, /* aTrackQuota */ false);
    MOZ_ASSERT(NS_SUCCEEDED(result));
  }

  return NS_OK;
}

}  // namespace mozilla::dom::cache

#endif  // mozilla_dom_cache_FileUtilsImpl_h
