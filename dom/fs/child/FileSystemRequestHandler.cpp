/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "fs/FileSystemRequestHandler.h"

#include "FileSystemEntryMetadataArray.h"
#include "fs/FileSystemConstants.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/FileSystemAccessHandleChild.h"
#include "mozilla/dom/FileSystemDirectoryHandle.h"
#include "mozilla/dom/FileSystemFileHandle.h"
#include "mozilla/dom/FileSystemHandle.h"
#include "mozilla/dom/FileSystemHelpers.h"
#include "mozilla/dom/FileSystemLog.h"
#include "mozilla/dom/FileSystemManager.h"
#include "mozilla/dom/FileSystemManagerChild.h"
#include "mozilla/dom/FileSystemSyncAccessHandle.h"
#include "mozilla/dom/FileSystemWritableFileStream.h"
#include "mozilla/dom/FileSystemWritableFileStreamChild.h"
#include "mozilla/dom/IPCBlobUtils.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/fs/IPCRejectReporter.h"
#include "mozilla/dom/quota/QuotaCommon.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "mozilla/ipc/RandomAccessStreamUtils.h"

namespace mozilla::dom::fs {

using mozilla::ipc::RejectCallback;

namespace {

void HandleFailedStatus(nsresult aError, const RefPtr<Promise>& aPromise) {
  switch (aError) {
    case NS_ERROR_FILE_ACCESS_DENIED:
      aPromise->MaybeRejectWithNotAllowedError("Permission denied");
      break;
    case NS_ERROR_FILE_NOT_FOUND:
      [[fallthrough]];
    case NS_ERROR_DOM_NOT_FOUND_ERR:
      aPromise->MaybeRejectWithNotFoundError("Entry not found");
      break;
    case NS_ERROR_DOM_FILESYSTEM_NO_MODIFICATION_ALLOWED_ERR:
      aPromise->MaybeRejectWithInvalidModificationError("Disallowed by system");
      break;
    case NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR:
      aPromise->MaybeRejectWithNoModificationAllowedError(
          "No modification allowed");
      break;
    case NS_ERROR_DOM_TYPE_MISMATCH_ERR:
      aPromise->MaybeRejectWithTypeMismatchError("Wrong type");
      break;
    case NS_ERROR_DOM_INVALID_MODIFICATION_ERR:
      aPromise->MaybeRejectWithInvalidModificationError("Invalid modification");
      break;
    default:
      if (NS_FAILED(aError)) {
        aPromise->MaybeRejectWithUnknownError("Unknown failure");
      } else {
        aPromise->MaybeResolveWithUndefined();
      }
      break;
  }
}

bool MakeResolution(nsIGlobalObject* aGlobal,
                    FileSystemGetEntriesResponse&& aResponse,
                    const bool& /* aResult */,
                    RefPtr<FileSystemEntryMetadataArray>& aSink) {
  // TODO: Add page size to FileSystemConstants, preallocate and handle overflow
  const auto& listing = aResponse.get_FileSystemDirectoryListing();

  for (const auto& it : listing.files()) {
    aSink->AppendElement(it);
  }

  for (const auto& it : listing.directories()) {
    aSink->AppendElement(it);
  }

  return true;
}

RefPtr<FileSystemDirectoryHandle> MakeResolution(
    nsIGlobalObject* aGlobal, FileSystemGetHandleResponse&& aResponse,
    const RefPtr<FileSystemDirectoryHandle>& /* aResult */,
    RefPtr<FileSystemManager>& aManager) {
  RefPtr<FileSystemDirectoryHandle> result = new FileSystemDirectoryHandle(
      aGlobal, aManager,
      FileSystemEntryMetadata(aResponse.get_EntryId(), kRootName,
                              /* directory */ true));
  return result;
}

RefPtr<FileSystemDirectoryHandle> MakeResolution(
    nsIGlobalObject* aGlobal, FileSystemGetHandleResponse&& aResponse,
    const RefPtr<FileSystemDirectoryHandle>& /* aResult */, const Name& aName,
    RefPtr<FileSystemManager>& aManager) {
  RefPtr<FileSystemDirectoryHandle> result = new FileSystemDirectoryHandle(
      aGlobal, aManager,
      FileSystemEntryMetadata(aResponse.get_EntryId(), aName,
                              /* directory */ true));

  return result;
}

RefPtr<FileSystemFileHandle> MakeResolution(
    nsIGlobalObject* aGlobal, FileSystemGetHandleResponse&& aResponse,
    const RefPtr<FileSystemFileHandle>& /* aResult */, const Name& aName,
    RefPtr<FileSystemManager>& aManager) {
  RefPtr<FileSystemFileHandle> result = new FileSystemFileHandle(
      aGlobal, aManager,
      FileSystemEntryMetadata(aResponse.get_EntryId(), aName,
                              /* directory */ false));
  return result;
}

RefPtr<FileSystemSyncAccessHandle> MakeResolution(
    nsIGlobalObject* aGlobal, FileSystemGetAccessHandleResponse&& aResponse,
    const RefPtr<FileSystemSyncAccessHandle>& /* aReturns */,
    const FileSystemEntryMetadata& aMetadata,
    RefPtr<FileSystemManager>& aManager) {
  auto& properties = aResponse.get_FileSystemAccessHandleProperties();

  QM_TRY_UNWRAP(
      RefPtr<FileSystemSyncAccessHandle> result,
      FileSystemSyncAccessHandle::Create(
          aGlobal, aManager, std::move(properties.streamParams()),
          std::move(properties.accessHandleChildEndpoint()),
          std::move(properties.accessHandleControlChildEndpoint()), aMetadata),
      nullptr);

  return result;
}

RefPtr<FileSystemWritableFileStream> MakeResolution(
    nsIGlobalObject* aGlobal,
    FileSystemGetWritableFileStreamResponse&& aResponse,
    const RefPtr<FileSystemWritableFileStream>& /* aReturns */,
    const FileSystemEntryMetadata& aMetadata,
    RefPtr<FileSystemManager>& aManager) {
  auto& properties = aResponse.get_FileSystemWritableFileStreamProperties();

  auto* const actor = static_cast<FileSystemWritableFileStreamChild*>(
      properties.writableFileStream().AsChild().get());

  QM_TRY_UNWRAP(RefPtr<FileSystemWritableFileStream> result,
                FileSystemWritableFileStream::Create(
                    aGlobal, aManager, std::move(properties.streamParams()),
                    actor, aMetadata),
                nullptr);

  return result;
}

RefPtr<File> MakeResolution(nsIGlobalObject* aGlobal,
                            FileSystemGetFileResponse&& aResponse,
                            const RefPtr<File>& /* aResult */,
                            const Name& aName,
                            RefPtr<FileSystemManager>& aManager) {
  auto& fileProperties = aResponse.get_FileSystemFileProperties();

  RefPtr<BlobImpl> blobImpl = IPCBlobUtils::Deserialize(fileProperties.file());
  MOZ_ASSERT(blobImpl);
  RefPtr<File> result = File::Create(aGlobal, blobImpl);
  return result;
}

template <class TResponse, class... Args>
void ResolveCallback(
    TResponse&& aResponse,
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    Args&&... args) {
  MOZ_ASSERT(aPromise);
  QM_TRY(OkIf(Promise::PromiseState::Pending == aPromise->State()), QM_VOID);

  if (TResponse::Tnsresult == aResponse.type()) {
    HandleFailedStatus(aResponse.get_nsresult(), aPromise);
    return;
  }

  auto resolution = MakeResolution(aPromise->GetParentObject(),
                                   std::forward<TResponse>(aResponse),
                                   std::forward<Args>(args)...);
  if (!resolution) {
    aPromise->MaybeRejectWithUnknownError("Could not complete request");
    return;
  }

  aPromise->MaybeResolve(resolution);
}

template <>
void ResolveCallback(
    FileSystemRemoveEntryResponse&& aResponse,
    RefPtr<Promise> aPromise) {  // NOLINT(performance-unnecessary-value-param)
  MOZ_ASSERT(aPromise);
  QM_TRY(OkIf(Promise::PromiseState::Pending == aPromise->State()), QM_VOID);

  if (FileSystemRemoveEntryResponse::Tvoid_t == aResponse.type()) {
    aPromise->MaybeResolveWithUndefined();
    return;
  }

  MOZ_ASSERT(FileSystemRemoveEntryResponse::Tnsresult == aResponse.type());
  HandleFailedStatus(aResponse.get_nsresult(), aPromise);
}

void ResolveCallback(
    FileSystemMoveEntryResponse&& aResponse,
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    const RefPtr<FileSystemHandle>& aHandle, const Name& aName) {
  MOZ_ASSERT(aPromise);
  MOZ_ASSERT(aHandle);
  QM_TRY(OkIf(Promise::PromiseState::Pending == aPromise->State()), QM_VOID);

  if (FileSystemMoveEntryResponse::TEntryId == aResponse.type()) {
    if (aHandle) {
      aHandle->UpdateMetadata(std::move(aResponse.get_EntryId()), aName);
    }

    aPromise->MaybeResolveWithUndefined();
    return;
  }
  MOZ_ASSERT(FileSystemMoveEntryResponse::Tnsresult == aResponse.type());
  const auto& status = aResponse.get_nsresult();
  MOZ_ASSERT(NS_FAILED(status));
  HandleFailedStatus(status, aPromise);
}

template <>
void ResolveCallback(FileSystemResolveResponse&& aResponse,
                     // NOLINTNEXTLINE(performance-unnecessary-value-param)
                     RefPtr<Promise> aPromise) {
  MOZ_ASSERT(aPromise);
  QM_TRY(OkIf(Promise::PromiseState::Pending == aPromise->State()), QM_VOID);

  if (FileSystemResolveResponse::Tnsresult == aResponse.type()) {
    HandleFailedStatus(aResponse.get_nsresult(), aPromise);
    return;
  }

  auto& maybePath = aResponse.get_MaybeFileSystemPath();
  if (maybePath.isSome()) {
    aPromise->MaybeResolve(maybePath.value().path());
    return;
  }

  // Spec says if there is no parent/child relationship, return null
  aPromise->MaybeResolve(JS::NullHandleValue);
}

template <class TResponse, class TReturns, class... Args,
          std::enable_if_t<std::is_same_v<TReturns, void>, bool> = true>
mozilla::ipc::ResolveCallback<TResponse> SelectResolveCallback(
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    Args&&... args) {
  using TOverload = void (*)(TResponse&&, RefPtr<Promise>, Args...);
  return static_cast<std::function<void(TResponse&&)>>(
      // NOLINTNEXTLINE(modernize-avoid-bind)
      std::bind(static_cast<TOverload>(ResolveCallback), std::placeholders::_1,
                aPromise, std::forward<Args>(args)...));
}

template <class TResponse, class TReturns, class... Args,
          std::enable_if_t<!std::is_same_v<TReturns, void>, bool> = true>
mozilla::ipc::ResolveCallback<TResponse> SelectResolveCallback(
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    Args&&... args) {
  using TOverload =
      void (*)(TResponse&&, RefPtr<Promise>, const TReturns&, Args...);
  return static_cast<std::function<void(TResponse&&)>>(
      // NOLINTNEXTLINE(modernize-avoid-bind)
      std::bind(static_cast<TOverload>(ResolveCallback), std::placeholders::_1,
                aPromise, TReturns(), std::forward<Args>(args)...));
}

void RejectCallback(
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    mozilla::ipc::ResponseRejectReason aReason) {
  IPCRejectReporter(aReason);
  QM_TRY(OkIf(Promise::PromiseState::Pending == aPromise->State()), QM_VOID);
  aPromise->MaybeRejectWithUndefined();
}

mozilla::ipc::RejectCallback GetRejectCallback(
    RefPtr<Promise> aPromise) {  // NOLINT(performance-unnecessary-value-param)
  return static_cast<mozilla::ipc::RejectCallback>(
      // NOLINTNEXTLINE(modernize-avoid-bind)
      std::bind(RejectCallback, aPromise, std::placeholders::_1));
}

struct BeginRequestFailureCallback {
  explicit BeginRequestFailureCallback(RefPtr<Promise> aPromise)
      : mPromise(std::move(aPromise)) {}

  void operator()(nsresult aRv) const {
    if (aRv == NS_ERROR_ABORT) {
      mPromise->MaybeRejectWithAbortError(
          "Abort error when calling GetDirectory");
      return;
    }
    if (aRv == NS_ERROR_DOM_SECURITY_ERR) {
      mPromise->MaybeRejectWithSecurityError(
          "Security error when calling GetDirectory");
      return;
    }
    mPromise->MaybeRejectWithUnknownError("Could not create actor");
  }

  RefPtr<Promise> mPromise;
};

}  // namespace

void FileSystemRequestHandler::GetRootHandle(
    RefPtr<FileSystemManager>
        aManager,              // NOLINT(performance-unnecessary-value-param)
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    ErrorResult& aError) {
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(aPromise);
  LOG(("GetRootHandle"));

  if (aManager->IsShutdown()) {
    aError.Throw(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    return;
  }

  aManager->BeginRequest(
      [onResolve = SelectResolveCallback<FileSystemGetHandleResponse,
                                         RefPtr<FileSystemDirectoryHandle>>(
           aPromise, aManager),
       onReject = GetRejectCallback(aPromise)](const auto& actor) mutable {
        actor->SendGetRootHandle(std::move(onResolve), std::move(onReject));
      },
      BeginRequestFailureCallback(aPromise));
}

void FileSystemRequestHandler::GetDirectoryHandle(
    RefPtr<FileSystemManager>& aManager,
    const FileSystemChildMetadata& aDirectory, bool aCreate,
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    ErrorResult& aError) {
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(!aDirectory.parentId().IsEmpty());
  MOZ_ASSERT(aPromise);
  LOG(("GetDirectoryHandle"));

  if (aManager->IsShutdown()) {
    aError.Throw(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    return;
  }

  if (!IsValidName(aDirectory.childName())) {
    aPromise->MaybeRejectWithTypeError("Invalid directory name");
    return;
  }

  aManager->BeginRequest(
      [request = FileSystemGetHandleRequest(aDirectory, aCreate),
       onResolve = SelectResolveCallback<FileSystemGetHandleResponse,
                                         RefPtr<FileSystemDirectoryHandle>>(
           aPromise, aDirectory.childName(), aManager),
       onReject = GetRejectCallback(aPromise)](const auto& actor) mutable {
        actor->SendGetDirectoryHandle(request, std::move(onResolve),
                                      std::move(onReject));
      },
      BeginRequestFailureCallback(aPromise));
}

void FileSystemRequestHandler::GetFileHandle(
    RefPtr<FileSystemManager>& aManager, const FileSystemChildMetadata& aFile,
    bool aCreate,
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    ErrorResult& aError) {
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(!aFile.parentId().IsEmpty());
  MOZ_ASSERT(aPromise);
  LOG(("GetFileHandle"));

  if (aManager->IsShutdown()) {
    aError.Throw(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    return;
  }

  if (!IsValidName(aFile.childName())) {
    aPromise->MaybeRejectWithTypeError("Invalid filename");
    return;
  }

  aManager->BeginRequest(
      [request = FileSystemGetHandleRequest(aFile, aCreate),
       onResolve = SelectResolveCallback<FileSystemGetHandleResponse,
                                         RefPtr<FileSystemFileHandle>>(
           aPromise, aFile.childName(), aManager),
       onReject = GetRejectCallback(aPromise)](const auto& actor) mutable {
        actor->SendGetFileHandle(request, std::move(onResolve),
                                 std::move(onReject));
      },
      BeginRequestFailureCallback(aPromise));
}

void FileSystemRequestHandler::GetAccessHandle(
    RefPtr<FileSystemManager>& aManager, const FileSystemEntryMetadata& aFile,
    const RefPtr<Promise>& aPromise, ErrorResult& aError) {
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(aPromise);
  LOG(("GetAccessHandle %s", NS_ConvertUTF16toUTF8(aFile.entryName()).get()));

  if (aManager->IsShutdown()) {
    aError.Throw(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    return;
  }

  aManager->BeginRequest(
      [request = FileSystemGetAccessHandleRequest(aFile.entryId()),
       onResolve = SelectResolveCallback<FileSystemGetAccessHandleResponse,
                                         RefPtr<FileSystemSyncAccessHandle>>(
           aPromise, aFile, aManager),
       onReject = GetRejectCallback(aPromise)](const auto& actor) mutable {
        actor->SendGetAccessHandle(request, std::move(onResolve),
                                   std::move(onReject));
      },
      BeginRequestFailureCallback(aPromise));
}

void FileSystemRequestHandler::GetWritable(RefPtr<FileSystemManager>& aManager,
                                           const FileSystemEntryMetadata& aFile,
                                           bool aKeepData,
                                           const RefPtr<Promise>& aPromise,
                                           ErrorResult& aError) {
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(aPromise);
  LOG(("GetWritable %s keep %d", NS_ConvertUTF16toUTF8(aFile.entryName()).get(),
       aKeepData));

  if (aManager->IsShutdown()) {
    aError.Throw(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    return;
  }

  aManager->BeginRequest(
      [request = FileSystemGetWritableRequest(aFile.entryId(), aKeepData),
       onResolve =
           SelectResolveCallback<FileSystemGetWritableFileStreamResponse,
                                 RefPtr<FileSystemWritableFileStream>>(
               aPromise, aFile, aManager),
       onReject = GetRejectCallback(aPromise)](const auto& actor) mutable {
        actor->SendGetWritable(request, std::move(onResolve),
                               std::move(onReject));
      },
      [promise = aPromise](const auto&) {
        promise->MaybeRejectWithUnknownError("Could not create actor");
      });
}

void FileSystemRequestHandler::GetFile(
    RefPtr<FileSystemManager>& aManager, const FileSystemEntryMetadata& aFile,
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    ErrorResult& aError) {
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(!aFile.entryId().IsEmpty());
  MOZ_ASSERT(aPromise);
  LOG(("GetFile %s", NS_ConvertUTF16toUTF8(aFile.entryName()).get()));

  if (aManager->IsShutdown()) {
    aError.Throw(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    return;
  }

  aManager->BeginRequest(
      [request = FileSystemGetFileRequest(aFile.entryId()),
       onResolve =
           SelectResolveCallback<FileSystemGetFileResponse, RefPtr<File>>(
               aPromise, aFile.entryName(), aManager),
       onReject = GetRejectCallback(aPromise)](const auto& actor) mutable {
        actor->SendGetFile(request, std::move(onResolve), std::move(onReject));
      },
      BeginRequestFailureCallback(aPromise));
}

void FileSystemRequestHandler::GetEntries(
    RefPtr<FileSystemManager>& aManager, const EntryId& aDirectory,
    PageNumber aPage,
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    RefPtr<FileSystemEntryMetadataArray>& aSink, ErrorResult& aError) {
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(!aDirectory.IsEmpty());
  MOZ_ASSERT(aPromise);
  LOG(("GetEntries, page %u", aPage));

  if (aManager->IsShutdown()) {
    aError.Throw(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    return;
  }

  aManager->BeginRequest(
      [request = FileSystemGetEntriesRequest(aDirectory, aPage),
       onResolve = SelectResolveCallback<FileSystemGetEntriesResponse, bool>(
           aPromise, aSink),
       onReject = GetRejectCallback(aPromise)](const auto& actor) mutable {
        actor->SendGetEntries(request, std::move(onResolve),
                              std::move(onReject));
      },
      BeginRequestFailureCallback(aPromise));
}

void FileSystemRequestHandler::RemoveEntry(
    RefPtr<FileSystemManager>& aManager, const FileSystemChildMetadata& aEntry,
    bool aRecursive,
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    ErrorResult& aError) {
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(!aEntry.parentId().IsEmpty());
  MOZ_ASSERT(aPromise);
  LOG(("RemoveEntry"));

  if (aManager->IsShutdown()) {
    aError.Throw(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    return;
  }

  if (!IsValidName(aEntry.childName())) {
    aPromise->MaybeRejectWithTypeError("Invalid name");
    return;
  }

  aManager->BeginRequest(
      [request = FileSystemRemoveEntryRequest(aEntry, aRecursive),
       onResolve =
           SelectResolveCallback<FileSystemRemoveEntryResponse, void>(aPromise),
       onReject = GetRejectCallback(aPromise)](const auto& actor) mutable {
        actor->SendRemoveEntry(request, std::move(onResolve),
                               std::move(onReject));
      },
      BeginRequestFailureCallback(aPromise));
}

void FileSystemRequestHandler::MoveEntry(
    RefPtr<FileSystemManager>& aManager, RefPtr<FileSystemHandle> aHandle,
    FileSystemEntryMetadata* const aEntry,
    const FileSystemChildMetadata& aNewEntry,
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    ErrorResult& aError) {
  MOZ_ASSERT(aEntry);
  MOZ_ASSERT(!aEntry->entryId().IsEmpty());
  MOZ_ASSERT(aPromise);
  LOG(("MoveEntry"));

  if (aManager->IsShutdown()) {
    aError.Throw(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    return;
  }

  // reject invalid names: empty, path separators, current & parent directories
  if (!IsValidName(aNewEntry.childName())) {
    aPromise->MaybeRejectWithTypeError("Invalid name");
    return;
  }

  const RefPtr<FileSystemHandle> handle(aHandle);
  aManager->BeginRequest(
      [request = FileSystemMoveEntryRequest(*aEntry, aNewEntry),
       onResolve = SelectResolveCallback<FileSystemMoveEntryResponse, void>(
           aPromise, handle, aNewEntry.childName()),
       onReject = GetRejectCallback(aPromise)](const auto& actor) mutable {
        actor->SendMoveEntry(request, std::move(onResolve),
                             std::move(onReject));
      },
      BeginRequestFailureCallback(aPromise));
}

void FileSystemRequestHandler::RenameEntry(
    RefPtr<FileSystemManager>& aManager, RefPtr<FileSystemHandle> aHandle,
    FileSystemEntryMetadata* const aEntry, const Name& aName,
    RefPtr<Promise> aPromise,  // NOLINT(performance-unnecessary-value-param)
    ErrorResult& aError) {
  MOZ_ASSERT(aEntry);
  MOZ_ASSERT(!aEntry->entryId().IsEmpty());
  MOZ_ASSERT(aPromise);
  LOG(("RenameEntry"));

  if (aManager->IsShutdown()) {
    aError.Throw(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    return;
  }

  // reject invalid names: empty, path separators, current & parent directories
  if (!IsValidName(aName)) {
    aPromise->MaybeRejectWithTypeError("Invalid name");
    return;
  }

  const RefPtr<FileSystemHandle> handle(aHandle);
  aManager->BeginRequest(
      [request = FileSystemRenameEntryRequest(*aEntry, aName),
       onResolve = SelectResolveCallback<FileSystemMoveEntryResponse, void>(
           aPromise, handle, aName),
       onReject = GetRejectCallback(aPromise)](const auto& actor) mutable {
        actor->SendRenameEntry(request, std::move(onResolve),
                               std::move(onReject));
      },
      BeginRequestFailureCallback(aPromise));
}

void FileSystemRequestHandler::Resolve(
    RefPtr<FileSystemManager>& aManager,
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    const FileSystemEntryPair& aEndpoints, RefPtr<Promise> aPromise,
    ErrorResult& aError) {
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(!aEndpoints.parentId().IsEmpty());
  MOZ_ASSERT(!aEndpoints.childId().IsEmpty());
  MOZ_ASSERT(aPromise);
  LOG(("Resolve"));

  if (aManager->IsShutdown()) {
    aError.Throw(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    return;
  }

  aManager->BeginRequest(
      [request = FileSystemResolveRequest(aEndpoints),
       onResolve =
           SelectResolveCallback<FileSystemResolveResponse, void>(aPromise),
       onReject = GetRejectCallback(aPromise)](const auto& actor) mutable {
        actor->SendResolve(request, std::move(onResolve), std::move(onReject));
      },
      BeginRequestFailureCallback(aPromise));
}

}  // namespace mozilla::dom::fs
