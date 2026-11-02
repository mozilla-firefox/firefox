/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_FS_PARENT_DATAMODEL_FILESYSTEMDATABASEMANAGERVERSION001_H_
#define DOM_FS_PARENT_DATAMODEL_FILESYSTEMDATABASEMANAGERVERSION001_H_

#include "FileSystemDatabaseManager.h"
#include "mozilla/dom/quota/CommonMetadata.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "nsString.h"

namespace mozilla::dom::fs {

struct FileId;

namespace data {

class FileSystemDataManager;
class FileSystemFileManager;

/**
 * @brief Versioned implementation of database interface enables backwards
 * support after the schema has changed. Version number 0 refers to
 * uninitialized database, and versions after that are sequential upgrades.
 *
 * To change the schema to the next version x,
 * - a new implementation FileSystemDatabaseManagerVersion00x is derived from
 * the previous version and the required methods are overridden
 * - a new apppropriate schema initialization class SchemaVersion00x is created
 * or derived
 * - the factory method of FileSystemDatabaseManager is extended to try to
 * migrate the data from the previous version to version x, and to return
 * FileSystemDatabaseManagerVersion00x implementation if the database version
 * after the migrations is x
 * - note that if the migration fails at some old version, the corresponding
 * old implementation should be returned: this way the users whose migrations
 * fail systematically due to hardware or other issues will not get locked out
 */
class FileSystemDatabaseManagerVersion001 : public FileSystemDatabaseManager {
 public:
  FileSystemDatabaseManagerVersion001(
      FileSystemDataManager* aDataManager, FileSystemConnection&& aConnection,
      UniquePtr<FileSystemFileManager>&& aFileManager,
      const EntryId& aRootEntry);

  /* Static to allow use by quota client without instantiation */
  static nsresult RescanTrackedUsages(
      const FileSystemConnection& aConnection,
      const quota::OriginMetadata& aOriginMetadata);

  /* Static to allow use by quota client without instantiation */
  static Result<Usage, QMResult> GetFileUsage(
      const FileSystemConnection& aConnection);

  Result<quota::UsageInfo, QMResult> GetUsage() const override;

  nsresult UpdateUsage(const FileId& aFileId) override;

  Result<EntryId, QMResult> GetOrCreateDirectory(
      const FileSystemChildMetadata& aHandle, bool aCreate) override;

  Result<EntryId, QMResult> GetOrCreateFile(
      const FileSystemChildMetadata& aHandle, bool aCreate) override;

  nsresult GetFile(const EntryId& aEntryId, const FileId& aFileId,
                   const FileMode& aMode, ContentType& aType,
                   TimeStamp& lastModifiedMilliSeconds, Path& aPath,
                   nsCOMPtr<nsIFile>& aFile) const override;

  Result<FileSystemDirectoryListing, QMResult> GetDirectoryEntries(
      const EntryId& aParent, PageNumber aPage) const override;

  Result<bool, QMResult> RemoveDirectory(const FileSystemChildMetadata& aHandle,
                                         bool aRecursive) override;

  Result<bool, QMResult> RemoveFile(
      const FileSystemChildMetadata& aHandle) override;

  Result<EntryId, QMResult> RenameEntry(const FileSystemEntryMetadata& aHandle,
                                        const Name& aNewName) override;

  Result<EntryId, QMResult> MoveEntry(
      const FileSystemEntryMetadata& aHandle,
      const FileSystemChildMetadata& aNewDesignation) override;

  Result<Path, QMResult> Resolve(
      const FileSystemEntryPair& aEndpoints) const override;

  Result<bool, QMResult> DoesFileExist(const EntryId& aEntryId) const override;

  Result<EntryId, QMResult> GetEntryId(
      const FileSystemChildMetadata& aHandle) const override;

  Result<EntryId, QMResult> GetEntryId(const FileId& aFileId) const override;

  Result<FileId, QMResult> EnsureFileId(const EntryId& aEntryId) override;

  Result<FileId, QMResult> EnsureTemporaryFileId(
      const EntryId& aEntryId) override;

  Result<FileId, QMResult> GetFileId(const EntryId& aEntryId) const override;

  nsresult MergeFileId(const EntryId& aEntryId, const FileId& aFileId,
                       bool aAbort) override;

  nsresult RemoveFileIdsWithoutMainFile() override;

  void Close() override;

  nsresult BeginUsageTracking(const FileId& aFileId) override;

  nsresult EndUsageTracking(const FileId& aFileId) override;

  virtual ~FileSystemDatabaseManagerVersion001();

 protected:
  virtual Result<bool, QMResult> DoesFileIdExist(const FileId& aFileId) const;

  virtual nsresult RemoveFileId(const FileId& aFileId);

  virtual Result<std::pair<nsTArray<FileId>, Usage>, QMResult>
  FindFilesWithoutDeprecatedLocksUnderEntry(const EntryId& aEntryId) const;

  virtual Result<nsTArray<std::pair<EntryId, FileId>>, QMResult>
  FindFileEntriesUnderDirectory(const EntryId& aEntryId) const;

  nsresult SetUsageTracking(const FileId& aFileId, bool aTracked);

  nsresult UpdateUsageInDatabase(const FileId& aFileId, Usage aNewDiskUsage);

  Result<Ok, QMResult> EnsureUsageIsKnown(const FileId& aFileId);

  void DecreaseCachedQuotaUsage(int64_t aDelta);

  nsresult UpdateCachedQuotaUsage(const FileId& aFileId, Usage aOldUsage,
                                  Usage aNewUsage) const;

  Result<bool, QMResult> RemoveDirectoryImpl(const EntryId& aEntryId);

  nsresult ClearDestinationIfNotLocked(
      const FileSystemConnection& aConnection,
      const FileSystemDataManager* const aDataManager,
      const FileSystemEntryMetadata& aHandle,
      const FileSystemChildMetadata& aNewDesignation);

  nsresult PrepareRenameEntry(const FileSystemConnection& aConnection,
                              const FileSystemDataManager* const aDataManager,
                              const FileSystemEntryMetadata& aHandle,
                              const Name& aNewName, bool aIsFile);

  nsresult PrepareMoveEntry(const FileSystemConnection& aConnection,
                            const FileSystemDataManager* const aDataManager,
                            const FileSystemEntryMetadata& aHandle,
                            const FileSystemChildMetadata& aNewDesignation,
                            bool aIsFile);

  // This is a raw pointer since we're owned by the FileSystemDataManager.
  FileSystemDataManager* MOZ_NON_OWNING_REF mDataManager;

  FileSystemConnection mConnection;

  UniquePtr<FileSystemFileManager> mFileManager;

  const EntryId mRootEntry;

  const quota::ClientMetadata mClientMetadata;

  int32_t mFilesOfUnknownUsage;
};

inline auto toNSResult = [](const auto& aRv) { return ToNSResult(aRv); };

Result<bool, QMResult> ApplyEntryExistsQuery(
    const FileSystemConnection& aConnection, const nsACString& aQuery,
    const FileSystemChildMetadata& aHandle);

Result<bool, QMResult> ApplyEntryExistsQuery(
    const FileSystemConnection& aConnection, const nsACString& aQuery,
    const EntryId& aEntry);

Result<bool, QMResult> IsFile(const FileSystemConnection& aConnection,
                              const EntryId& aEntryId);

Result<EntryId, QMResult> FindEntryId(const FileSystemConnection& aConnection,
                                      const FileSystemChildMetadata& aHandle,
                                      bool aIsFile);

Result<EntryId, QMResult> FindParent(const FileSystemConnection& aConnection,
                                     const EntryId& aEntryId);

Result<bool, QMResult> IsSame(const FileSystemConnection& aConnection,
                              const FileSystemEntryMetadata& aHandle,
                              const FileSystemChildMetadata& aNewHandle,
                              bool aIsFile);

Result<Path, QMResult> ResolveReversedPath(
    const FileSystemConnection& aConnection,
    const FileSystemEntryPair& aEndpoints);

nsresult GetFileAttributes(const FileSystemConnection& aConnection,
                           const EntryId& aEntryId, ContentType& aType);

void TryRemoveDuringIdleMaintenance(const nsTArray<FileId>& aItemToRemove);

ContentType DetermineContentType(const Name& aName);

}  // namespace data
}  // namespace mozilla::dom::fs

#endif  // DOM_FS_PARENT_DATAMODEL_FILESYSTEMDATABASEMANAGERVERSION001_H_
