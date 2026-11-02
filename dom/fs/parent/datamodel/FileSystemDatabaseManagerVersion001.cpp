/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FileSystemDatabaseManagerVersion001.h"

#include "ErrorList.h"
#include "FileSystemContentTypeGuess.h"
#include "FileSystemDataManager.h"
#include "FileSystemFileManager.h"
#include "FileSystemParentTypes.h"
#include "ResultStatement.h"
#include "StartedTransaction.h"
#include "mozStorageHelper.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/dom/FileSystemDataManager.h"
#include "mozilla/dom/FileSystemHandle.h"
#include "mozilla/dom/FileSystemLog.h"
#include "mozilla/dom/FileSystemTypes.h"
#include "mozilla/dom/PFileSystemManager.h"
#include "mozilla/dom/quota/Client.h"
#include "mozilla/dom/quota/QuotaCommon.h"
#include "mozilla/dom/quota/QuotaManager.h"
#include "mozilla/dom/quota/QuotaObject.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "nsString.h"

namespace mozilla::dom {

using FileSystemEntries = nsTArray<fs::FileSystemEntryMetadata>;

namespace fs::data {

namespace {

constexpr const nsLiteralCString gDescendantsQuery =
    "WITH RECURSIVE traceChildren(handle, parent) AS ( "
    "SELECT handle, parent "
    "FROM Entries "
    "WHERE handle=:handle "
    "UNION "
    "SELECT Entries.handle, Entries.parent FROM traceChildren, Entries "
    "WHERE traceChildren.handle=Entries.parent ) "
    "SELECT handle, Usages.usage "
    "FROM traceChildren INNER JOIN Files USING (handle) "
    "INNER JOIN Usages USING (handle) "
    ";"_ns;

Result<bool, QMResult> DoesFileExist(const FileSystemConnection& aConnection,
                                     const EntryId& aEntryId) {
  MOZ_ASSERT(!aEntryId.IsEmpty());

  const nsCString existsQuery =
      "SELECT EXISTS "
      "(SELECT 1 FROM Files WHERE handle = :handle ) "
      ";"_ns;

  QM_TRY_RETURN(ApplyEntryExistsQuery(aConnection, existsQuery, aEntryId));
}

Result<bool, QMResult> IsDirectoryEmpty(const FileSystemConnection& mConnection,
                                        const EntryId& aEntryId) {
  const nsLiteralCString isDirEmptyQuery =
      "SELECT EXISTS ("
      "SELECT 1 FROM Entries WHERE parent = :parent "
      ");"_ns;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(mConnection, isDirEmptyQuery));
  QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("parent"_ns, aEntryId)));
  QM_TRY_UNWRAP(bool childrenExist, stmt.YesOrNoQuery());

  return !childrenExist;
}

Result<bool, QMResult> DoesDirectoryExist(
    const FileSystemConnection& mConnection,
    const FileSystemChildMetadata& aHandle) {
  MOZ_ASSERT(!aHandle.parentId().IsEmpty());

  const nsCString existsQuery =
      "SELECT EXISTS "
      "(SELECT 1 FROM Directories INNER JOIN Entries USING (handle) "
      "WHERE Directories.name = :name AND Entries.parent = :parent ) "
      ";"_ns;

  QM_TRY_RETURN(ApplyEntryExistsQuery(mConnection, existsQuery, aHandle));
}

Result<bool, QMResult> DoesDirectoryExist(
    const FileSystemConnection& mConnection, const EntryId& aEntry) {
  MOZ_ASSERT(!aEntry.IsEmpty());

  const nsCString existsQuery =
      "SELECT EXISTS "
      "(SELECT 1 FROM Directories WHERE handle = :handle ) "
      ";"_ns;

  QM_TRY_RETURN(ApplyEntryExistsQuery(mConnection, existsQuery, aEntry));
}

Result<bool, QMResult> IsAncestor(const FileSystemConnection& aConnection,
                                  const FileSystemEntryPair& aEndpoints) {
  const nsCString pathQuery =
      "WITH RECURSIVE followPath(handle, parent) AS ( "
      "SELECT handle, parent "
      "FROM Entries "
      "WHERE handle=:entryId "
      "UNION "
      "SELECT Entries.handle, Entries.parent FROM followPath, Entries "
      "WHERE followPath.parent=Entries.handle ) "
      "SELECT EXISTS "
      "(SELECT 1 FROM followPath "
      "WHERE handle=:possibleAncestor ) "
      ";"_ns;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, pathQuery));
  QM_TRY(
      QM_TO_RESULT(stmt.BindEntryIdByName("entryId"_ns, aEndpoints.childId())));
  QM_TRY(QM_TO_RESULT(
      stmt.BindEntryIdByName("possibleAncestor"_ns, aEndpoints.parentId())));

  return stmt.YesOrNoQuery();
}

Result<bool, QMResult> DoesFileExist(const FileSystemConnection& aConnection,
                                     const FileSystemChildMetadata& aHandle) {
  MOZ_ASSERT(!aHandle.parentId().IsEmpty());

  const nsCString existsQuery =
      "SELECT EXISTS "
      "(SELECT 1 FROM Files INNER JOIN Entries USING (handle) "
      "WHERE Files.name = :name AND Entries.parent = :parent ) "
      ";"_ns;

  QM_TRY_RETURN(ApplyEntryExistsQuery(aConnection, existsQuery, aHandle));
}

nsresult GetEntries(const FileSystemConnection& aConnection,
                    const nsACString& aUnboundStmt, const EntryId& aParent,
                    PageNumber aPage, bool aDirectory,
                    FileSystemEntries& aEntries) {
  // The entries inside a directory are sent to the child process in batches
  // of pageSize items. Large value ensures that iteration is less often delayed
  // by IPC messaging and querying the database.
  // TODO: The current value 1024 is not optimized.
  // TODO: Value "pageSize" is shared with the iterator implementation and
  // should be defined in a common place.
  const int32_t pageSize = 1024;

  QM_TRY_UNWRAP(bool exists, DoesDirectoryExist(aConnection, aParent));
  if (!exists) {
    return NS_ERROR_DOM_NOT_FOUND_ERR;
  }

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, aUnboundStmt));
  QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("parent"_ns, aParent)));
  QM_TRY(QM_TO_RESULT(stmt.BindPageNumberByName("pageSize"_ns, pageSize)));
  QM_TRY(QM_TO_RESULT(
      stmt.BindPageNumberByName("pageOffset"_ns, aPage * pageSize)));

  QM_TRY_UNWRAP(bool moreResults, stmt.ExecuteStep());

  while (moreResults) {
    QM_TRY_UNWRAP(EntryId entryId, stmt.GetEntryIdByColumn(/* Column */ 0u));
    QM_TRY_UNWRAP(Name entryName, stmt.GetNameByColumn(/* Column */ 1u));

    FileSystemEntryMetadata metadata(entryId, entryName, aDirectory);
    aEntries.AppendElement(metadata);

    QM_TRY_UNWRAP(moreResults, stmt.ExecuteStep());
  }

  return NS_OK;
}

Result<EntryId, QMResult> GetUniqueEntryId(
    const FileSystemConnection& aConnection,
    const FileSystemChildMetadata& aHandle) {
  const nsCString existsQuery =
      "SELECT EXISTS "
      "(SELECT 1 FROM Entries "
      "WHERE handle = :handle )"
      ";"_ns;

  FileSystemChildMetadata generatorInput = aHandle;

  const size_t maxRounds = 1024u;

  for (size_t hangGuard = 0u; hangGuard < maxRounds; ++hangGuard) {
    QM_TRY_UNWRAP(EntryId entryId, fs::data::GetEntryHandle(generatorInput));
    QM_TRY_UNWRAP(ResultStatement stmt,
                  ResultStatement::Create(aConnection, existsQuery));
    QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("handle"_ns, entryId)));

    QM_TRY_UNWRAP(bool alreadyInUse, stmt.YesOrNoQuery());

    if (!alreadyInUse) {
      return entryId;
    }

    generatorInput.parentId() = std::move(entryId);
  }

  return Err(QMResult(NS_ERROR_UNEXPECTED));
}

nsresult PerformRename(const FileSystemConnection& aConnection,
                       const FileSystemEntryMetadata& aHandle,
                       const Name& aNewName, const ContentType& aNewType,
                       const nsLiteralCString& aNameUpdateQuery) {
  MOZ_ASSERT(!aHandle.entryId().IsEmpty());
  MOZ_ASSERT(IsValidName(aHandle.entryName()));

  // same-name is checked in RenameEntry()
  if (!IsValidName(aNewName)) {
    return NS_ERROR_DOM_TYPE_MISMATCH_ERR;
  }

  // TODO: This should fail when handle doesn't exist - the
  // explicit file or directory existence queries are redundant
  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, aNameUpdateQuery)
                    .mapErr(toNSResult));
  if (!aNewType.IsVoid()) {
    QM_TRY(MOZ_TO_RESULT(stmt.BindContentTypeByName("type"_ns, aNewType)));
  }
  QM_TRY(MOZ_TO_RESULT(stmt.BindNameByName("name"_ns, aNewName)));
  QM_TRY(MOZ_TO_RESULT(stmt.BindEntryIdByName("handle"_ns, aHandle.entryId())));
  QM_TRY(MOZ_TO_RESULT(stmt.Execute()));

  return NS_OK;
}

nsresult PerformRenameDirectory(const FileSystemConnection& aConnection,
                                const FileSystemEntryMetadata& aHandle,
                                const Name& aNewName) {
  const nsLiteralCString updateDirectoryNameQuery =
      "UPDATE Directories "
      "SET name = :name "
      "WHERE handle = :handle "
      ";"_ns;

  return PerformRename(aConnection, aHandle, aNewName, VoidCString(),
                       updateDirectoryNameQuery);
}

nsresult PerformRenameFile(const FileSystemConnection& aConnection,
                           const FileSystemEntryMetadata& aHandle,
                           const Name& aNewName, const ContentType& aNewType) {
  const nsLiteralCString updateFileTypeAndNameQuery =
      "UPDATE Files SET type = :type, name = :name "
      "WHERE handle = :handle ;"_ns;

  const nsLiteralCString updateFileNameQuery =
      "UPDATE Files SET name = :name WHERE handle = :handle ;"_ns;

  if (aNewType.IsVoid()) {
    return PerformRename(aConnection, aHandle, aNewName, aNewType,
                         updateFileNameQuery);
  }

  return PerformRename(aConnection, aHandle, aNewName, aNewType,
                       updateFileTypeAndNameQuery);
}

template <class HandlerType>
nsresult SetUsageTrackingImpl(const FileSystemConnection& aConnection,
                              const FileId& aFileId, bool aTracked,
                              HandlerType&& aOnMissingFile) {
  const nsLiteralCString setTrackedQuery =
      "INSERT INTO Usages "
      "( handle, tracked ) "
      "VALUES "
      "( :handle, :tracked ) "
      "ON CONFLICT(handle) DO "
      "UPDATE SET tracked = excluded.tracked "
      ";"_ns;

  const nsresult customReturnValue =
      aTracked ? NS_ERROR_DOM_NOT_FOUND_ERR : NS_OK;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, setTrackedQuery));
  QM_TRY(MOZ_TO_RESULT(stmt.BindFileIdByName("handle"_ns, aFileId)));
  QM_TRY(MOZ_TO_RESULT(stmt.BindBooleanByName("tracked"_ns, aTracked)));
  QM_TRY(MOZ_TO_RESULT(stmt.Execute()), customReturnValue,
         std::forward<HandlerType>(aOnMissingFile));

  return NS_OK;
}

Result<nsTArray<FileId>, QMResult> GetTrackedFiles(
    const FileSystemConnection& aConnection) {
  // The same query works for both 001 and 002 schemas because handle is
  // an entry id and later on a file id, respectively.
  static const nsLiteralCString getTrackedFilesQuery =
      "SELECT handle FROM Usages WHERE tracked = TRUE;"_ns;
  nsTArray<FileId> trackedFiles;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, getTrackedFilesQuery));
  QM_TRY_UNWRAP(bool moreResults, stmt.ExecuteStep());

  while (moreResults) {
    QM_TRY_UNWRAP(FileId fileId, stmt.GetFileIdByColumn(/* Column */ 0u));

    trackedFiles.AppendElement(fileId);  // TODO: fallible?

    QM_TRY_UNWRAP(moreResults, stmt.ExecuteStep());
  }

  return trackedFiles;
}

/** This handles the file not found error by assigning 0 usage to the dangling
 * handle and puts the handle to a non-tracked state. Otherwise, when the
 * file or database cannot be reached, the file remains in the tracked state.
 */
template <class QuotaCacheUpdate>
nsresult UpdateUsageForFileEntry(const FileSystemConnection& aConnection,
                                 const FileSystemFileManager& aFileManager,
                                 const FileId& aFileId,
                                 const nsLiteralCString& aUpdateQuery,
                                 QuotaCacheUpdate&& aUpdateCache) {
  QM_TRY_INSPECT(const auto& fileHandle, aFileManager.GetFile(aFileId));

  // A file could have changed in a way which doesn't allow to read its size.
  QM_TRY_UNWRAP(
      const Usage fileSize,
      QM_OR_ELSE_WARN_IF(
          // Expression.
          MOZ_TO_RESULT_INVOKE_MEMBER(fileHandle, GetFileSize),
          // Predicate.
          ([](const nsresult rv) { return rv == NS_ERROR_FILE_NOT_FOUND; }),
          // Fallback. If the file does no longer exist, treat it as 0-sized.
          ErrToDefaultOk<Usage>));

  QM_TRY(MOZ_TO_RESULT(aUpdateCache(fileSize)));

  // No transaction as one statement succeeds or fails atomically
  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, aUpdateQuery));

  QM_TRY(MOZ_TO_RESULT(stmt.BindFileIdByName("handle"_ns, aFileId)));

  QM_TRY(MOZ_TO_RESULT(stmt.BindUsageByName("usage"_ns, fileSize)));

  QM_TRY(MOZ_TO_RESULT(stmt.Execute()));

  return NS_OK;
}

nsresult UpdateUsageUnsetTracked(const FileSystemConnection& aConnection,
                                 const FileSystemFileManager& aFileManager,
                                 const FileId& aFileId) {
  static const nsLiteralCString updateUsagesUnsetTrackedQuery =
      "UPDATE Usages SET usage = :usage, tracked = FALSE "
      "WHERE handle = :handle;"_ns;

  auto noCacheUpdateNeeded = [](auto) { return NS_OK; };

  return UpdateUsageForFileEntry(aConnection, aFileManager, aFileId,
                                 updateUsagesUnsetTrackedQuery,
                                 std::move(noCacheUpdateNeeded));
}

/**
 * @brief Get the recorded usage only if the file is in tracked state.
 * During origin initialization, if the usage on disk is unreadable, the latest
 * recorded usage is reported to the quota manager for the tracked files.
 * To allow writing, we attempt to update the real usage with one database and
 * one file size query.
 */
Result<Maybe<Usage>, QMResult> GetMaybeTrackedUsage(
    const FileSystemConnection& aConnection, const FileId& aFileId) {
  const nsLiteralCString trackedUsageQuery =
      "SELECT usage FROM Usages WHERE tracked = TRUE AND handle = :handle "
      ");"_ns;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, trackedUsageQuery));
  QM_TRY(QM_TO_RESULT(stmt.BindFileIdByName("handle"_ns, aFileId)));

  QM_TRY_UNWRAP(const bool moreResults, stmt.ExecuteStep());
  if (!moreResults) {
    return Maybe<Usage>(Nothing());
  }

  QM_TRY_UNWRAP(Usage trackedUsage, stmt.GetUsageByColumn(/* Column */ 0u));

  return Some(trackedUsage);
}

Result<bool, nsresult> ScanTrackedFiles(
    const FileSystemConnection& aConnection,
    const FileSystemFileManager& aFileManager) {
  QM_TRY_INSPECT(const nsTArray<FileId>& trackedFiles,
                 GetTrackedFiles(aConnection).mapErr(toNSResult));

  bool ok = true;
  for (const auto& fileId : trackedFiles) {
    // On success, tracked is set to false, otherwise its value is kept (= true)
    QM_WARNONLY_TRY(MOZ_TO_RESULT(UpdateUsageUnsetTracked(
                        aConnection, aFileManager, fileId)),
                    [&ok](const auto& /*aRv*/) { ok = false; });
  }

  return ok;
}

Result<Ok, QMResult> DeleteEntry(const FileSystemConnection& aConnection,
                                 const EntryId& aEntryId) {
  // If it's a directory, deleting the handle will cascade
  const nsLiteralCString deleteEntryQuery =
      "DELETE FROM Entries "
      "WHERE handle = :handle "
      ";"_ns;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, deleteEntryQuery));

  QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("handle"_ns, aEntryId)));

  QM_TRY(QM_TO_RESULT(stmt.Execute()));

  return Ok{};
}

Result<int32_t, QMResult> GetTrackedFilesCount(
    const FileSystemConnection& aConnection) {
  // TODO: We could query the count directly
  QM_TRY_INSPECT(const auto& trackedFiles, GetTrackedFiles(aConnection));

  CheckedInt32 checkedFileCount = trackedFiles.Length();
  QM_TRY(OkIf(checkedFileCount.isValid()),
         Err(QMResult(NS_ERROR_ILLEGAL_VALUE)));

  return checkedFileCount.value();
}

void LogWithFilename(const FileSystemFileManager& aFileManager,
                     const char* aFormat, const FileId& aFileId) {
  if (!LOG_ENABLED()) {
    return;
  }

  QM_TRY_INSPECT(const auto& localFile, aFileManager.GetFile(aFileId), QM_VOID);

  nsAutoString localPath;
  QM_TRY(MOZ_TO_RESULT(localFile->GetPath(localPath)), QM_VOID);
  LOG((aFormat, NS_ConvertUTF16toUTF8(localPath).get()));
}

Result<bool, QMResult> IsAnyDescendantLocked(
    const FileSystemConnection& aConnection,
    const FileSystemDataManager& aDataManager, const EntryId& aEntryId) {
  constexpr const nsLiteralCString descendantsQuery =
      "WITH RECURSIVE traceChildren(handle, parent) AS ( "
      "SELECT handle, parent "
      "FROM Entries "
      "WHERE handle=:handle "
      "UNION "
      "SELECT Entries.handle, Entries.parent FROM traceChildren, Entries "
      "WHERE traceChildren.handle=Entries.parent ) "
      "SELECT handle, Files.name "
      "FROM traceChildren INNER JOIN Files USING (handle) "
      ";"_ns;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, descendantsQuery));
  QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("handle"_ns, aEntryId)));
  QM_TRY_UNWRAP(bool moreResults, stmt.ExecuteStep());

  while (moreResults) {
    // Works only for version 001
    QM_TRY_INSPECT(const EntryId& entryId,
                   stmt.GetEntryIdByColumn(/* Column */ 0u));

    QM_TRY_UNWRAP(const bool isLocked, aDataManager.IsLocked(entryId), true);
    if (isLocked) {
      return true;
    }

    QM_TRY_UNWRAP(moreResults, stmt.ExecuteStep());
  }

  return false;
}

}  // namespace

FileSystemDatabaseManagerVersion001::FileSystemDatabaseManagerVersion001(
    FileSystemDataManager* aDataManager, FileSystemConnection&& aConnection,
    UniquePtr<FileSystemFileManager>&& aFileManager, const EntryId& aRootEntry)
    : mDataManager(aDataManager),
      mConnection(aConnection),
      mFileManager(std::move(aFileManager)),
      mRootEntry(aRootEntry),
      mClientMetadata(aDataManager->OriginMetadataRef(),
                      quota::Client::FILESYSTEM),
      mFilesOfUnknownUsage(-1) {}

FileSystemDatabaseManagerVersion001::~FileSystemDatabaseManagerVersion001() =
    default;

/* static */
nsresult FileSystemDatabaseManagerVersion001::RescanTrackedUsages(
    const FileSystemConnection& aConnection,
    const quota::OriginMetadata& aOriginMetadata) {
  QM_TRY_UNWRAP(UniquePtr<FileSystemFileManager> fileManager,
                data::FileSystemFileManager::CreateFileSystemFileManager(
                    aOriginMetadata));

  QM_TRY_UNWRAP(bool ok, ScanTrackedFiles(aConnection, *fileManager));
  if (ok) {
    return NS_OK;
  }

  // Retry once without explicit delay
  QM_TRY_UNWRAP(ok, ScanTrackedFiles(aConnection, *fileManager));
  if (!ok) {
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

/* static */
Result<Usage, QMResult> FileSystemDatabaseManagerVersion001::GetFileUsage(
    const FileSystemConnection& aConnection) {
  const nsLiteralCString sumUsagesQuery = "SELECT sum(usage) FROM Usages;"_ns;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, sumUsagesQuery));

  QM_TRY_UNWRAP(const bool moreResults, stmt.ExecuteStep());
  if (!moreResults) {
    return Err(QMResult(NS_ERROR_DOM_FILE_NOT_READABLE_ERR));
  }

  QM_TRY_UNWRAP(Usage totalFiles, stmt.GetUsageByColumn(/* Column */ 0u));

  return totalFiles;
}

Result<quota::UsageInfo, QMResult>
FileSystemDatabaseManagerVersion001::GetUsage() const {
  return FileSystemDatabaseManager::GetUsage(mConnection, mClientMetadata);
}

nsresult FileSystemDatabaseManagerVersion001::UpdateUsage(
    const FileId& aFileId) {
  // We don't track directories or non-existent files.
  QM_TRY_UNWRAP(bool fileExists, DoesFileIdExist(aFileId).mapErr(toNSResult));
  if (!fileExists) {
    return NS_OK;  // May be deleted before update, no assert
  }

  QM_TRY_UNWRAP(nsCOMPtr<nsIFile> file, mFileManager->GetFile(aFileId));
  MOZ_ASSERT(file);

  Usage fileSize = 0;
  bool exists = false;
  QM_TRY(MOZ_TO_RESULT(file->Exists(&exists)));
  if (exists) {
    QM_TRY(MOZ_TO_RESULT(file->GetFileSize(&fileSize)));
  }

  QM_TRY(MOZ_TO_RESULT(UpdateUsageInDatabase(aFileId, fileSize)));

  return NS_OK;
}

Result<EntryId, QMResult>
FileSystemDatabaseManagerVersion001::GetOrCreateDirectory(
    const FileSystemChildMetadata& aHandle, bool aCreate) {
  MOZ_ASSERT(!aHandle.parentId().IsEmpty());

  const auto& name = aHandle.childName();
  // Belt and suspenders: check here as well as in child.
  if (!IsValidName(name)) {
    return Err(QMResult(NS_ERROR_DOM_TYPE_MISMATCH_ERR));
  }
  MOZ_ASSERT(!(name.IsVoid() || name.IsEmpty()));

  bool exists = true;
  QM_TRY_UNWRAP(exists, data::DoesFileExist(mConnection, aHandle));

  // By spec, we don't allow a file and a directory
  // to have the same name and parent
  if (exists) {
    return Err(QMResult(NS_ERROR_DOM_TYPE_MISMATCH_ERR));
  }

  QM_TRY_UNWRAP(exists, DoesDirectoryExist(mConnection, aHandle));

  // exists as directory
  if (exists) {
    return FindEntryId(mConnection, aHandle, false);
  }

  if (!aCreate) {
    return Err(QMResult(NS_ERROR_DOM_NOT_FOUND_ERR));
  }

  const nsLiteralCString insertEntryQuery =
      "INSERT OR IGNORE INTO Entries "
      "( handle, parent ) "
      "VALUES "
      "( :handle, :parent ) "
      ";"_ns;

  const nsLiteralCString insertDirectoryQuery =
      "INSERT OR IGNORE INTO Directories "
      "( handle, name ) "
      "VALUES "
      "( :handle, :name ) "
      ";"_ns;

  QM_TRY_INSPECT(const EntryId& entryId, GetEntryId(aHandle));
  MOZ_ASSERT(!entryId.IsEmpty());

  QM_TRY_UNWRAP(auto transaction, StartedTransaction::Create(mConnection));

  {
    QM_TRY_UNWRAP(ResultStatement stmt,
                  ResultStatement::Create(mConnection, insertEntryQuery));
    QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("handle"_ns, entryId)));
    QM_TRY(
        QM_TO_RESULT(stmt.BindEntryIdByName("parent"_ns, aHandle.parentId())));
    QM_TRY(QM_TO_RESULT(stmt.Execute()));
  }

  {
    QM_TRY_UNWRAP(ResultStatement stmt,
                  ResultStatement::Create(mConnection, insertDirectoryQuery));
    QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("handle"_ns, entryId)));
    QM_TRY(QM_TO_RESULT(stmt.BindNameByName("name"_ns, name)));
    QM_TRY(QM_TO_RESULT(stmt.Execute()));
  }

  QM_TRY(QM_TO_RESULT(transaction.Commit()));

  QM_TRY_UNWRAP(DebugOnly<bool> doesItExistNow,
                DoesDirectoryExist(mConnection, aHandle));
  MOZ_ASSERT(doesItExistNow);

  return entryId;
}

Result<EntryId, QMResult> FileSystemDatabaseManagerVersion001::GetOrCreateFile(
    const FileSystemChildMetadata& aHandle, bool aCreate) {
  MOZ_ASSERT(!aHandle.parentId().IsEmpty());

  const auto& name = aHandle.childName();
  // Belt and suspenders: check here as well as in child.
  if (!IsValidName(name)) {
    return Err(QMResult(NS_ERROR_DOM_TYPE_MISMATCH_ERR));
  }
  MOZ_ASSERT(!(name.IsVoid() || name.IsEmpty()));

  QM_TRY_UNWRAP(bool exists, DoesDirectoryExist(mConnection, aHandle));

  // By spec, we don't allow a file and a directory
  // to have the same name and parent
  QM_TRY(OkIf(!exists), Err(QMResult(NS_ERROR_DOM_TYPE_MISMATCH_ERR)));

  QM_TRY_UNWRAP(exists, data::DoesFileExist(mConnection, aHandle));

  if (exists) {
    QM_TRY_RETURN(FindEntryId(mConnection, aHandle, /* aIsFile */ true));
  }

  if (!aCreate) {
    return Err(QMResult(NS_ERROR_DOM_NOT_FOUND_ERR));
  }

  const nsLiteralCString insertEntryQuery =
      "INSERT INTO Entries "
      "( handle, parent ) "
      "VALUES "
      "( :handle, :parent ) "
      ";"_ns;

  const nsLiteralCString insertFileQuery =
      "INSERT INTO Files "
      "( handle, type, name ) "
      "VALUES "
      "( :handle, :type, :name ) "
      ";"_ns;

  QM_TRY_INSPECT(const EntryId& entryId, GetEntryId(aHandle));
  MOZ_ASSERT(!entryId.IsEmpty());

  const ContentType type = DetermineContentType(name);

  QM_TRY_UNWRAP(auto transaction, StartedTransaction::Create(mConnection));

  {
    QM_TRY_UNWRAP(ResultStatement stmt,
                  ResultStatement::Create(mConnection, insertEntryQuery));
    QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("handle"_ns, entryId)));
    QM_TRY(
        QM_TO_RESULT(stmt.BindEntryIdByName("parent"_ns, aHandle.parentId())));
    QM_TRY(QM_TO_RESULT(stmt.Execute()), QM_PROPAGATE,
           ([this, &aHandle](const auto& aRv) {
             QM_TRY_UNWRAP(bool parentExists,
                           DoesDirectoryExist(mConnection, aHandle.parentId()),
                           QM_VOID);
             QM_TRY(OkIf(parentExists), QM_VOID);
           }));
  }

  {
    QM_TRY_UNWRAP(ResultStatement stmt,
                  ResultStatement::Create(mConnection, insertFileQuery));
    QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("handle"_ns, entryId)));
    QM_TRY(QM_TO_RESULT(stmt.BindContentTypeByName("type"_ns, type)));
    QM_TRY(QM_TO_RESULT(stmt.BindNameByName("name"_ns, name)));
    QM_TRY(QM_TO_RESULT(stmt.Execute()));
  }

  QM_TRY(QM_TO_RESULT(transaction.Commit()));

  return entryId;
}

nsresult FileSystemDatabaseManagerVersion001::GetFile(
    const EntryId& aEntryId, const FileId& aFileId, const FileMode& aMode,
    ContentType& aType, TimeStamp& lastModifiedMilliSeconds,
    nsTArray<Name>& aPath, nsCOMPtr<nsIFile>& aFile) const {
  MOZ_ASSERT(!aFileId.IsEmpty());
  MOZ_ASSERT(aMode == FileMode::EXCLUSIVE);

  const FileSystemEntryPair endPoints(mRootEntry, aEntryId);
  QM_TRY_UNWRAP(aPath, ResolveReversedPath(mConnection, endPoints));
  if (aPath.IsEmpty()) {
    return NS_ERROR_DOM_NOT_FOUND_ERR;
  }

  QM_TRY(MOZ_TO_RESULT(GetFileAttributes(mConnection, aEntryId, aType)));
  QM_TRY_UNWRAP(aFile, mFileManager->GetOrCreateFile(aFileId));

  PRTime lastModTime = 0;
  QM_TRY(MOZ_TO_RESULT(aFile->GetLastModifiedTime(&lastModTime)));
  lastModifiedMilliSeconds = static_cast<TimeStamp>(lastModTime);

  aPath.Reverse();

  return NS_OK;
}

Result<FileSystemDirectoryListing, QMResult>
FileSystemDatabaseManagerVersion001::GetDirectoryEntries(
    const EntryId& aParent, PageNumber aPage) const {
  // TODO: Offset is reported to have bad performance - see Bug 1780386.
  const nsCString directoriesQuery =
      "SELECT Dirs.handle, Dirs.name "
      "FROM Directories AS Dirs "
      "INNER JOIN ( "
      "SELECT handle "
      "FROM Entries "
      "WHERE parent = :parent "
      "LIMIT :pageSize "
      "OFFSET :pageOffset ) "
      "AS Ents "
      "ON Dirs.handle = Ents.handle "
      ";"_ns;
  const nsCString filesQuery =
      "SELECT Files.handle, Files.name "
      "FROM Files "
      "INNER JOIN ( "
      "SELECT handle "
      "FROM Entries "
      "WHERE parent = :parent "
      "LIMIT :pageSize "
      "OFFSET :pageOffset ) "
      "AS Ents "
      "ON Files.handle = Ents.handle "
      ";"_ns;

  FileSystemDirectoryListing entries;
  QM_TRY(
      QM_TO_RESULT(GetEntries(mConnection, directoriesQuery, aParent, aPage,
                              /* aDirectory */ true, entries.directories())));

  QM_TRY(QM_TO_RESULT(GetEntries(mConnection, filesQuery, aParent, aPage,
                                 /* aDirectory */ false, entries.files())));

  return entries;
}

Result<bool, QMResult> FileSystemDatabaseManagerVersion001::RemoveDirectoryImpl(
    const EntryId& entryId) {
  using FileIdArrayAndUsage = std::pair<nsTArray<FileId>, Usage>;

  // Caller has checked that there are no exclusively locked descendants.
  QM_TRY_INSPECT(const FileIdArrayAndUsage& unlockedFileInfo,
                 FindFilesWithoutDeprecatedLocksUnderEntry(entryId));

  const nsTArray<FileId>& descendants = unlockedFileInfo.first;
  // We remove the files which have no open WritableFileStream child actors.
  // The files left behind get removed at the end of the child actor lifecycle.
  nsTArray<FileId> failedRemovals;
  QM_TRY_UNWRAP(DebugOnly<Usage> removedUsage,
                mFileManager->RemoveFiles(descendants, failedRemovals));

  const Usage usage = unlockedFileInfo.second;
  // Usage is for the current main file but we remove temporary files too.
  MOZ_ASSERT_IF(failedRemovals.IsEmpty() && (0 == mFilesOfUnknownUsage),
                usage <= removedUsage);

  TryRemoveDuringIdleMaintenance(failedRemovals);

  for (const auto& fileId : descendants) {
    QM_WARNONLY_TRY(QM_TO_RESULT(RemoveFileId(fileId)));
  }

  if (usage > 0) {  // Performance!
    DecreaseCachedQuotaUsage(usage);
  }

  QM_TRY(DeleteEntry(mConnection, entryId));

  return true;
}

Result<bool, QMResult> FileSystemDatabaseManagerVersion001::RemoveDirectory(
    const FileSystemChildMetadata& aHandle, bool aRecursive) {
  MOZ_ASSERT(!aHandle.parentId().IsEmpty());

  if (aHandle.childName().IsEmpty()) {
    return false;
  }

  DebugOnly<Name> name = aHandle.childName();
  MOZ_ASSERT(!name.inspect().IsVoid());

  QM_TRY_UNWRAP(bool exists, DoesDirectoryExist(mConnection, aHandle));

  if (!exists) {
    return false;
  }

  // At this point, entry exists and is a directory.
  QM_TRY_UNWRAP(EntryId entryId, FindEntryId(mConnection, aHandle, false));
  MOZ_ASSERT(!entryId.IsEmpty());

  QM_TRY_UNWRAP(bool isEmpty, IsDirectoryEmpty(mConnection, entryId));

  MOZ_ASSERT(mDataManager);
  QM_TRY_UNWRAP(const bool isLocked,
                IsAnyDescendantLocked(mConnection, *mDataManager, entryId));

  // XXX If there are any locked file entries, we exit here.
  // There has been talk that the spec might allow unconditional
  // overwrites in the future.
  QM_TRY(OkIf(!isLocked),
         Err(QMResult(NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR)));

  if (!aRecursive && !isEmpty) {
    return Err(QMResult(NS_ERROR_DOM_INVALID_MODIFICATION_ERR));
  }

  QM_TRY_RETURN(RemoveDirectoryImpl(entryId));
}

Result<bool, QMResult> FileSystemDatabaseManagerVersion001::RemoveFile(
    const FileSystemChildMetadata& aHandle) {
  MOZ_ASSERT(!aHandle.parentId().IsEmpty());

  if (aHandle.childName().IsEmpty()) {
    return false;
  }

  DebugOnly<Name> name = aHandle.childName();
  MOZ_ASSERT(!name.inspect().IsVoid());

  // Make it more evident that we won't remove directories
  QM_TRY_UNWRAP(bool exists, data::DoesFileExist(mConnection, aHandle));

  if (!exists) {
    return false;
  }

  // At this point, entry exists and is a file
  QM_TRY_UNWRAP(EntryId entryId, FindEntryId(mConnection, aHandle, true));
  MOZ_ASSERT(!entryId.IsEmpty());

  // XXX This code assumes the spec question is resolved to state
  // removing an in-use file should fail.  If it shouldn't fail, we need to
  // do something to neuter all the extant FileAccessHandles/WritableFileStreams
  // that reference it
  QM_TRY_UNWRAP(const bool isLocked, mDataManager->IsLocked(entryId));
  if (isLocked) {
    LOG(("Trying to remove in-use file"));
    return Err(QMResult(NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR));
  }

  using FileIdArrayAndUsage = std::pair<nsTArray<FileId>, Usage>;
  QM_TRY_INSPECT(const FileIdArrayAndUsage& unlockedFileInfo,
                 FindFilesWithoutDeprecatedLocksUnderEntry(entryId));

  const nsTArray<FileId>& diskItems = unlockedFileInfo.first;
  nsTArray<FileId> failedRemovals;
  QM_TRY_UNWRAP(DebugOnly<Usage> removedUsage,
                mFileManager->RemoveFiles(diskItems, failedRemovals));

  const Usage usage = unlockedFileInfo.second;
  // We only check the most common case. This can fail spuriously if an external
  // application writes to the file, or OS reports zero size due to corruption.
  MOZ_ASSERT_IF(failedRemovals.IsEmpty() && (0 == mFilesOfUnknownUsage),
                usage == removedUsage);

  TryRemoveDuringIdleMaintenance(failedRemovals);

  for (const auto& fileId : diskItems) {
    QM_WARNONLY_TRY(QM_TO_RESULT(RemoveFileId(fileId)));
  }

  if (usage > 0) {  // Performance!
    DecreaseCachedQuotaUsage(usage);
  }

  QM_TRY(DeleteEntry(mConnection, entryId));

  return true;
}

Result<EntryId, QMResult> FileSystemDatabaseManagerVersion001::RenameEntry(
    const FileSystemEntryMetadata& aHandle, const Name& aNewName) {
  const auto& entryId = aHandle.entryId();

  // Can't rename root
  if (mRootEntry == entryId) {
    return Err(QMResult(NS_ERROR_DOM_NOT_FOUND_ERR));
  }

  // Verify the source exists
  QM_TRY_UNWRAP(bool isFile, IsFile(mConnection, entryId),
                Err(QMResult(NS_ERROR_DOM_NOT_FOUND_ERR)));

  // Are we actually renaming?
  if (aHandle.entryName() == aNewName) {
    return entryId;
  }

  QM_TRY(QM_TO_RESULT(PrepareRenameEntry(mConnection, mDataManager, aHandle,
                                         aNewName, isFile)));

  QM_TRY_UNWRAP(auto transaction, StartedTransaction::Create(mConnection));

  if (isFile) {
    const ContentType type = DetermineContentType(aNewName);
    QM_TRY(
        QM_TO_RESULT(PerformRenameFile(mConnection, aHandle, aNewName, type)));
  } else {
    QM_TRY(
        QM_TO_RESULT(PerformRenameDirectory(mConnection, aHandle, aNewName)));
  }

  QM_TRY(QM_TO_RESULT(transaction.Commit()));

  return entryId;
}

Result<EntryId, QMResult> FileSystemDatabaseManagerVersion001::MoveEntry(
    const FileSystemEntryMetadata& aHandle,
    const FileSystemChildMetadata& aNewDesignation) {
  const auto& entryId = aHandle.entryId();
  MOZ_ASSERT(!entryId.IsEmpty());

  if (mRootEntry == entryId) {
    return Err(QMResult(NS_ERROR_DOM_NOT_FOUND_ERR));
  }

  // Verify the source exists
  QM_TRY_UNWRAP(bool isFile, IsFile(mConnection, entryId),
                Err(QMResult(NS_ERROR_DOM_NOT_FOUND_ERR)));

  // If the rename doesn't change the name or directory, just return success.
  // XXX Needs to be added to the spec
  QM_WARNONLY_TRY_UNWRAP(Maybe<bool> maybeSame,
                         IsSame(mConnection, aHandle, aNewDesignation, isFile));
  if (maybeSame && maybeSame.value()) {
    return entryId;
  }

  QM_TRY(QM_TO_RESULT(PrepareMoveEntry(mConnection, mDataManager, aHandle,
                                       aNewDesignation, isFile)));

  const nsLiteralCString updateEntryParentQuery =
      "UPDATE Entries "
      "SET parent = :parent "
      "WHERE handle = :handle "
      ";"_ns;

  QM_TRY_UNWRAP(auto transaction, StartedTransaction::Create(mConnection));

  {
    // We always change the parent because it's simpler than checking if the
    // parent needs to be changed
    QM_TRY_UNWRAP(ResultStatement stmt,
                  ResultStatement::Create(mConnection, updateEntryParentQuery));
    QM_TRY(QM_TO_RESULT(
        stmt.BindEntryIdByName("parent"_ns, aNewDesignation.parentId())));
    QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("handle"_ns, entryId)));
    QM_TRY(QM_TO_RESULT(stmt.Execute()));
  }

  const Name& newName = aNewDesignation.childName();

  // Are we actually renaming?
  if (aHandle.entryName() == newName) {
    QM_TRY(QM_TO_RESULT(transaction.Commit()));

    return entryId;
  }

  if (isFile) {
    const ContentType type = DetermineContentType(newName);
    QM_TRY(
        QM_TO_RESULT(PerformRenameFile(mConnection, aHandle, newName, type)));
  } else {
    QM_TRY(QM_TO_RESULT(PerformRenameDirectory(mConnection, aHandle, newName)));
  }

  QM_TRY(QM_TO_RESULT(transaction.Commit()));

  return entryId;
}

Result<Path, QMResult> FileSystemDatabaseManagerVersion001::Resolve(
    const FileSystemEntryPair& aEndpoints) const {
  QM_TRY_UNWRAP(Path path, ResolveReversedPath(mConnection, aEndpoints));
  // Note: if not an ancestor, returns null

  path.Reverse();
  return path;
}

Result<bool, QMResult> FileSystemDatabaseManagerVersion001::DoesFileExist(
    const EntryId& aEntryId) const {
  MOZ_ASSERT(!aEntryId.IsEmpty());

  QM_TRY_RETURN(data::DoesFileExist(mConnection, aEntryId));
}

Result<EntryId, QMResult> FileSystemDatabaseManagerVersion001::GetEntryId(
    const FileSystemChildMetadata& aHandle) const {
  return GetUniqueEntryId(mConnection, aHandle);
}

Result<EntryId, QMResult> FileSystemDatabaseManagerVersion001::GetEntryId(
    const FileId& aFileId) const {
  return aFileId.Value();
}

Result<FileId, QMResult> FileSystemDatabaseManagerVersion001::EnsureFileId(
    const EntryId& aEntryId) {
  return FileId(aEntryId);
}

Result<FileId, QMResult>
FileSystemDatabaseManagerVersion001::EnsureTemporaryFileId(
    const EntryId& aEntryId) {
  return FileId(aEntryId);
}

Result<FileId, QMResult> FileSystemDatabaseManagerVersion001::GetFileId(
    const EntryId& aEntryId) const {
  return FileId(aEntryId);
}

nsresult FileSystemDatabaseManagerVersion001::MergeFileId(
    const EntryId& /* aEntryId */, const FileId& /* aFileId */,
    bool /* aAbort */) {
  // Version 001 should always use exclusive mode and not get here.
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult FileSystemDatabaseManagerVersion001::RemoveFileIdsWithoutMainFile() {
  // Version 001 has exactly one file per entry which is always the main file.
  return NS_OK;
}

void FileSystemDatabaseManagerVersion001::Close() { mConnection->Close(); }

nsresult FileSystemDatabaseManagerVersion001::BeginUsageTracking(
    const FileId& aFileId) {
  MOZ_ASSERT(!aFileId.IsEmpty());

  // If file is already tracked but we cannot read its size, error.
  // If file does not exist, this will succeed because usage is zero.
  QM_TRY(EnsureUsageIsKnown(aFileId));

  // If file does not exist, set usage tracking to true fails with
  // file not found error.
  QM_TRY(MOZ_TO_RESULT(SetUsageTracking(aFileId, true)));

  return NS_OK;
}

nsresult FileSystemDatabaseManagerVersion001::EndUsageTracking(
    const FileId& aFileId) {
  // This is expected to fail only if database is unreachable.
  QM_TRY(MOZ_TO_RESULT(SetUsageTracking(aFileId, false)));

  return NS_OK;
}

Result<bool, QMResult> FileSystemDatabaseManagerVersion001::DoesFileIdExist(
    const FileId& aFileId) const {
  MOZ_ASSERT(!aFileId.IsEmpty());

  QM_TRY_RETURN(DoesFileExist(aFileId.Value()));
}

nsresult FileSystemDatabaseManagerVersion001::RemoveFileId(
    const FileId& /* aFileId */) {
  return NS_OK;
}

/**
 * @brief Get the sum of usages for all file descendants of a directory entry.
 * We obtain the values with one query, which is presumably better than having a
 * separate query for each individual descendant.
 * TODO: Check if this is true
 *
 * Please see GetFileUsage documentation for why we use the latest recorded
 * value from the database instead of the file size property from the disk.
 */
Result<std::pair<nsTArray<FileId>, Usage>, QMResult>
FileSystemDatabaseManagerVersion001::FindFilesWithoutDeprecatedLocksUnderEntry(
    const EntryId& aEntryId) const {
  nsTArray<FileId> descendants;
  Usage usage{0};
  {
    QM_TRY_UNWRAP(ResultStatement stmt,
                  ResultStatement::Create(mConnection, gDescendantsQuery));
    QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("handle"_ns, aEntryId)));
    QM_TRY_UNWRAP(bool moreResults, stmt.ExecuteStep());

    while (true) {
      if (!moreResults) {
        break;
      }

      // Works only for version 001
      QM_TRY_INSPECT(const FileId& fileId,
                     stmt.GetFileIdByColumn(/* Column */ 0u));

      // FileId equals EntryId for version 001
      if (!mDataManager->IsLockedWithDeprecatedSharedLock(fileId.Value(),
                                                          fileId)) {
        QM_TRY_UNWRAP(DebugOnly<bool> isLocked,
                      mDataManager->IsLocked(fileId.Value()));
        MOZ_ASSERT(!isLocked);
        descendants.AppendElement(fileId);

        QM_TRY_INSPECT(const Usage& fileUsage,
                       stmt.GetUsageByColumn(/* Column */ 1u));
        usage += fileUsage;
      }

      QM_TRY_UNWRAP(moreResults, stmt.ExecuteStep());
    }
  }

  return std::make_pair(std::move(descendants), usage);
}

Result<nsTArray<std::pair<EntryId, FileId>>, QMResult>
FileSystemDatabaseManagerVersion001::FindFileEntriesUnderDirectory(
    const EntryId& aEntryId) const {
  return Err(QMResult(NS_ERROR_NOT_IMPLEMENTED));
}

nsresult FileSystemDatabaseManagerVersion001::SetUsageTracking(
    const FileId& aFileId, bool aTracked) {
  auto onMissingFile = [this, &aFileId](const auto& aRv) {
    // Usages constrains entryId to be present in Files
    MOZ_ASSERT(NS_ERROR_STORAGE_CONSTRAINT == ToNSResult(aRv));

    // The query *should* fail if and only if file does not exist
    QM_TRY_UNWRAP(DebugOnly<bool> fileExists, DoesFileIdExist(aFileId),
                  QM_VOID);
    MOZ_ASSERT(!fileExists);
  };

  return SetUsageTrackingImpl(mConnection, aFileId, aTracked, onMissingFile);
}

nsresult FileSystemDatabaseManagerVersion001::UpdateUsageInDatabase(
    const FileId& aFileId, Usage aNewDiskUsage) {
  const nsLiteralCString updateUsageQuery =
      "INSERT INTO Usages "
      "( handle, usage ) "
      "VALUES "
      "( :handle, :usage ) "
      "ON CONFLICT(handle) DO "
      "UPDATE SET usage = excluded.usage "
      ";"_ns;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(mConnection, updateUsageQuery));
  QM_TRY(MOZ_TO_RESULT(stmt.BindUsageByName("usage"_ns, aNewDiskUsage)));
  QM_TRY(MOZ_TO_RESULT(stmt.BindFileIdByName("handle"_ns, aFileId)));
  QM_TRY(MOZ_TO_RESULT(stmt.Execute()));

  return NS_OK;
}

Result<Ok, QMResult> FileSystemDatabaseManagerVersion001::EnsureUsageIsKnown(
    const FileId& aFileId) {
  if (mFilesOfUnknownUsage < 0) {  // Lazy initialization
    QM_TRY_UNWRAP(mFilesOfUnknownUsage, GetTrackedFilesCount(mConnection));
  }

  if (mFilesOfUnknownUsage == 0) {
    return Ok{};
  }

  QM_TRY_UNWRAP(Maybe<Usage> oldUsage,
                GetMaybeTrackedUsage(mConnection, aFileId));
  if (oldUsage.isNothing()) {
    return Ok{};  // Usage is 0 or it was successfully recorded at unlocking.
  }

  auto quotaCacheUpdate = [this, &aFileId,
                           oldSize = oldUsage.value()](Usage aNewSize) {
    return UpdateCachedQuotaUsage(aFileId, oldSize, aNewSize);
  };

  static const nsLiteralCString updateUsagesKeepTrackedQuery =
      "UPDATE Usages SET usage = :usage WHERE handle = :handle;"_ns;

  // If usage update fails, we log an error and keep things the way they were.
  QM_TRY(QM_TO_RESULT(UpdateUsageForFileEntry(
             mConnection, *mFileManager, aFileId, updateUsagesKeepTrackedQuery,
             std::move(quotaCacheUpdate))),
         Err(QMResult(NS_ERROR_DOM_FILE_NOT_READABLE_ERR)),
         ([this, &aFileId](const auto& /*aRv*/) {
           LogWithFilename(*mFileManager, "Could not read the size of file %s",
                           aFileId);
         }));

  // We read and updated the quota usage successfully.
  --mFilesOfUnknownUsage;
  MOZ_ASSERT(mFilesOfUnknownUsage >= 0);

  return Ok{};
}

void FileSystemDatabaseManagerVersion001::DecreaseCachedQuotaUsage(
    int64_t aDelta) {
  quota::QuotaManager* quotaManager = quota::QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  quotaManager->DecreaseUsageForClient(mClientMetadata, aDelta);
}

nsresult FileSystemDatabaseManagerVersion001::UpdateCachedQuotaUsage(
    const FileId& aFileId, Usage aOldUsage, Usage aNewUsage) const {
  quota::QuotaManager* quotaManager = quota::QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  QM_TRY_UNWRAP(nsCOMPtr<nsIFile> fileObj,
                mFileManager->GetFile(aFileId).mapErr(toNSResult));

  RefPtr<quota::QuotaObject> quotaObject = quotaManager->GetQuotaObject(
      mClientMetadata.mPersistenceType, mClientMetadata,
      quota::Client::FILESYSTEM, fileObj, aOldUsage);
  MOZ_ASSERT(quotaObject);

  QM_TRY(OkIf(quotaObject->MaybeUpdateSize(aNewUsage, /* aTruncate */ true)),
         NS_ERROR_FILE_NO_DEVICE_SPACE);

  return NS_OK;
}

nsresult FileSystemDatabaseManagerVersion001::ClearDestinationIfNotLocked(
    const FileSystemConnection& aConnection,
    const FileSystemDataManager* const aDataManager,
    const FileSystemEntryMetadata& aHandle,
    const FileSystemChildMetadata& aNewDesignation) {
  // If the destination file exists, fail explicitly.  Spec author plans to
  // revise the spec
  QM_TRY_UNWRAP(bool exists, data::DoesFileExist(aConnection, aNewDesignation));
  if (exists) {
    QM_TRY_INSPECT(const EntryId& destId,
                   FindEntryId(aConnection, aNewDesignation, true));
    QM_TRY_UNWRAP(const bool isLocked, aDataManager->IsLocked(destId));
    if (isLocked) {
      LOG(("Trying to overwrite in-use file"));
      return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
    }

    QM_TRY_UNWRAP(DebugOnly<bool> isRemoved, RemoveFile(aNewDesignation));
    MOZ_ASSERT(isRemoved);
  } else {
    QM_TRY_UNWRAP(exists, DoesDirectoryExist(aConnection, aNewDesignation));
    if (exists) {
      QM_TRY_INSPECT(const EntryId& destId,
                     FindEntryId(aConnection, aNewDesignation, false));

      MOZ_ASSERT(aDataManager);
      QM_TRY_UNWRAP(const bool isLocked,
                    IsAnyDescendantLocked(mConnection, *aDataManager, destId));

      // XXX If there are any locked file entries, we exit here.
      // There has been talk that the spec might allow unconditional
      // overwrites in the future.
      QM_TRY(OkIf(!isLocked),
             Err(QMResult(NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR)));

      QM_TRY_UNWRAP(DebugOnly<bool> isRemoved,
                    MOZ_TO_RESULT(RemoveDirectoryImpl(destId)));

      MOZ_ASSERT(isRemoved);
    }
  }

  return NS_OK;
}

nsresult FileSystemDatabaseManagerVersion001::PrepareRenameEntry(
    const FileSystemConnection& aConnection,
    const FileSystemDataManager* const aDataManager,
    const FileSystemEntryMetadata& aHandle, const Name& aNewName,
    bool aIsFile) {
  const EntryId& entryId = aHandle.entryId();

  // At this point, entry exists
  if (aIsFile) {
    QM_TRY_UNWRAP(const bool isLocked, aDataManager->IsLocked(entryId));
    if (isLocked) {
      LOG(("Trying to move in-use file"));
      return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
    }
  }

  // If the destination file exists, fail explicitly.
  FileSystemChildMetadata destination;
  QM_TRY_UNWRAP(EntryId parent, FindParent(mConnection, entryId));
  destination.parentId() = parent;
  destination.childName() = aNewName;

  QM_TRY(MOZ_TO_RESULT(ClearDestinationIfNotLocked(mConnection, mDataManager,
                                                   aHandle, destination)));

  return NS_OK;
}

nsresult FileSystemDatabaseManagerVersion001::PrepareMoveEntry(
    const FileSystemConnection& aConnection,
    const FileSystemDataManager* const aDataManager,
    const FileSystemEntryMetadata& aHandle,
    const FileSystemChildMetadata& aNewDesignation, bool aIsFile) {
  const EntryId& entryId = aHandle.entryId();

  // At this point, entry exists
  if (aIsFile) {
    QM_TRY_UNWRAP(const bool isLocked, aDataManager->IsLocked(entryId));
    if (isLocked) {
      LOG(("Trying to move in-use file"));
      return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
    }
  }

  QM_TRY(QM_TO_RESULT(ClearDestinationIfNotLocked(aConnection, aDataManager,
                                                  aHandle, aNewDesignation)));

  // XXX: This should be before clearing the target

  // To prevent cyclic paths, we check that there is no path from
  // the item to be moved to the destination folder.
  QM_TRY_UNWRAP(const bool isDestinationUnderSelf,
                IsAncestor(aConnection, {entryId, aNewDesignation.parentId()}));
  if (isDestinationUnderSelf) {
    return NS_ERROR_DOM_INVALID_MODIFICATION_ERR;
  }

  return NS_OK;
}

/**
 * Free functions
 */

Result<bool, QMResult> ApplyEntryExistsQuery(
    const FileSystemConnection& aConnection, const nsACString& aQuery,
    const FileSystemChildMetadata& aHandle) {
  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, aQuery));
  QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("parent"_ns, aHandle.parentId())));
  QM_TRY(QM_TO_RESULT(stmt.BindNameByName("name"_ns, aHandle.childName())));

  return stmt.YesOrNoQuery();
}

Result<bool, QMResult> ApplyEntryExistsQuery(
    const FileSystemConnection& aConnection, const nsACString& aQuery,
    const EntryId& aEntry) {
  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, aQuery));
  QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("handle"_ns, aEntry)));

  return stmt.YesOrNoQuery();
}

Result<bool, QMResult> IsFile(const FileSystemConnection& aConnection,
                              const EntryId& aEntryId) {
  QM_TRY_UNWRAP(bool exists, DoesFileExist(aConnection, aEntryId));
  if (exists) {
    return true;
  }

  QM_TRY_UNWRAP(exists, DoesDirectoryExist(aConnection, aEntryId));
  if (exists) {
    return false;
  }

  // Doesn't exist
  return Err(QMResult(NS_ERROR_DOM_NOT_FOUND_ERR));
}

Result<EntryId, QMResult> FindEntryId(const FileSystemConnection& aConnection,
                                      const FileSystemChildMetadata& aHandle,
                                      bool aIsFile) {
  const nsCString aDirectoryQuery =
      "SELECT Entries.handle FROM Directories "
      "INNER JOIN Entries USING (handle) "
      "WHERE Directories.name = :name AND Entries.parent = :parent "
      ";"_ns;

  const nsCString aFileQuery =
      "SELECT Entries.handle FROM Files INNER JOIN Entries USING (handle) "
      "WHERE Files.name = :name AND Entries.parent = :parent "
      ";"_ns;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(
                    aConnection, aIsFile ? aFileQuery : aDirectoryQuery));
  QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("parent"_ns, aHandle.parentId())));
  QM_TRY(QM_TO_RESULT(stmt.BindNameByName("name"_ns, aHandle.childName())));
  QM_TRY_UNWRAP(bool moreResults, stmt.ExecuteStep());

  if (!moreResults) {
    return Err(QMResult(NS_ERROR_DOM_NOT_FOUND_ERR));
  }

  QM_TRY_UNWRAP(EntryId entryId, stmt.GetEntryIdByColumn(/* Column */ 0u));

  return entryId;
}

Result<EntryId, QMResult> FindParent(const FileSystemConnection& aConnection,
                                     const EntryId& aEntryId) {
  const nsCString aParentQuery =
      "SELECT handle FROM Entries "
      "WHERE handle IN ( "
      "SELECT parent FROM Entries WHERE "
      "handle = :entryId ) "
      ";"_ns;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, aParentQuery));
  QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("entryId"_ns, aEntryId)));
  QM_TRY_UNWRAP(bool moreResults, stmt.ExecuteStep());

  if (!moreResults) {
    return Err(QMResult(NS_ERROR_DOM_NOT_FOUND_ERR));
  }

  QM_TRY_UNWRAP(EntryId parentId, stmt.GetEntryIdByColumn(/* Column */ 0u));
  return parentId;
}

Result<bool, QMResult> IsSame(const FileSystemConnection& aConnection,
                              const FileSystemEntryMetadata& aHandle,
                              const FileSystemChildMetadata& aNewHandle,
                              bool aIsFile) {
  MOZ_ASSERT(!aNewHandle.parentId().IsEmpty());

  // Typically aNewHandle does not exist which is not an error
  QM_TRY_RETURN(QM_OR_ELSE_LOG_VERBOSE_IF(
      // Expression.
      FindEntryId(aConnection, aNewHandle, aIsFile)
          .map([&aHandle](const EntryId& entryId) {
            return entryId == aHandle.entryId();
          }),
      // Predicate.
      IsSpecificError<NS_ERROR_DOM_NOT_FOUND_ERR>,
      // Fallback.
      ErrToOkFromQMResult<false>));
}

Result<Path, QMResult> ResolveReversedPath(
    const FileSystemConnection& aConnection,
    const FileSystemEntryPair& aEndpoints) {
  const nsLiteralCString pathQuery =
      "WITH RECURSIVE followPath(handle, parent) AS ( "
      "SELECT handle, parent "
      "FROM Entries "
      "WHERE handle=:entryId "
      "UNION "
      "SELECT Entries.handle, Entries.parent FROM followPath, Entries "
      "WHERE followPath.parent=Entries.handle ) "
      "SELECT COALESCE(Directories.name, Files.name), handle "
      "FROM followPath "
      "LEFT JOIN Directories USING(handle) "
      "LEFT JOIN Files USING(handle);"_ns;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, pathQuery));
  QM_TRY(
      QM_TO_RESULT(stmt.BindEntryIdByName("entryId"_ns, aEndpoints.childId())));
  QM_TRY_UNWRAP(bool moreResults, stmt.ExecuteStep());

  Path pathResult;
  while (moreResults) {
    QM_TRY_UNWRAP(Name entryName, stmt.GetNameByColumn(/* Column */ 0u));
    QM_TRY_UNWRAP(EntryId entryId, stmt.GetEntryIdByColumn(/* Column */ 1u));

    if (aEndpoints.parentId() == entryId) {
      return pathResult;
    }
    pathResult.AppendElement(entryName);

    QM_TRY_UNWRAP(moreResults, stmt.ExecuteStep());
  }

  // Spec wants us to return 'null' for not-an-ancestor case
  pathResult.Clear();
  return pathResult;
}

nsresult GetFileAttributes(const FileSystemConnection& aConnection,
                           const EntryId& aEntryId, ContentType& aType) {
  const nsLiteralCString getFileLocation =
      "SELECT type FROM Files INNER JOIN Entries USING(handle) "
      "WHERE handle = :entryId "
      ";"_ns;

  QM_TRY_UNWRAP(ResultStatement stmt,
                ResultStatement::Create(aConnection, getFileLocation));
  QM_TRY(QM_TO_RESULT(stmt.BindEntryIdByName("entryId"_ns, aEntryId)));
  QM_TRY_UNWRAP(bool hasEntries, stmt.ExecuteStep());

  // Type is an optional attribute
  if (!hasEntries || stmt.IsNullByColumn(/* Column */ 0u)) {
    return NS_OK;
  }

  QM_TRY_UNWRAP(aType, stmt.GetContentTypeByColumn(/* Column */ 0u));

  return NS_OK;
}

// TODO: Implement idle maintenance
void TryRemoveDuringIdleMaintenance(
    const nsTArray<FileId>& /* aItemToRemove */) {
  // Not implemented
}

ContentType DetermineContentType(const Name& aName) {
  QM_TRY_UNWRAP(
      auto typeResult,
      QM_OR_ELSE_LOG_VERBOSE(
          FileSystemContentTypeGuess::FromPath(aName),
          ([](const auto& aRv) -> Result<ContentType, QMResult> {
            const nsresult rv = ToNSResult(aRv);
            switch (rv) {
              case NS_ERROR_FAILURE: /* There is an unknown new extension. */
                return ContentType(""_ns); /* We clear the old extension. */

              case NS_ERROR_INVALID_ARG: /* The name is garbled. */
                [[fallthrough]];
              case NS_ERROR_NOT_AVAILABLE: /* There is no extension. */
                return VoidCString();      /* We keep the old extension. */

              default:
                MOZ_ASSERT_UNREACHABLE("Should never get here!");
                return Err(aRv);
            }
          })),
      ContentType(""_ns));

  return typeResult;
}

}  // namespace fs::data

}  // namespace mozilla::dom
