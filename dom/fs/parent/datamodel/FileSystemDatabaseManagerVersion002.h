/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_FS_PARENT_DATAMODEL_FILESYSTEMDATABASEMANAGERVERSION002_H_
#define DOM_FS_PARENT_DATAMODEL_FILESYSTEMDATABASEMANAGERVERSION002_H_

#include "FileSystemDatabaseManagerVersion001.h"

namespace mozilla::dom::fs::data {

class FileSystemDatabaseManagerVersion002
    : public FileSystemDatabaseManagerVersion001 {
 public:
  FileSystemDatabaseManagerVersion002(
      FileSystemDataManager* aDataManager, FileSystemConnection&& aConnection,
      UniquePtr<FileSystemFileManager>&& aFileManager,
      const EntryId& aRootEntry)
      : FileSystemDatabaseManagerVersion001(
            aDataManager, std::move(aConnection), std::move(aFileManager),
            aRootEntry) {}

  /* Static to allow use by quota client without instantiation */
  static nsresult RescanTrackedUsages(
      const FileSystemConnection& aConnection,
      const quota::OriginMetadata& aOriginMetadata);

  /* Static to allow use by quota client without instantiation */
  static Result<Usage, QMResult> GetFileUsage(
      const FileSystemConnection& aConnection);

  nsresult GetFile(const EntryId& aEntryId, const FileId& aFileId,
                   const FileMode& aMode, ContentType& aType,
                   TimeStamp& lastModifiedMilliSeconds, Path& aPath,
                   nsCOMPtr<nsIFile>& aFile) const override;

  Result<EntryId, QMResult> RenameEntry(const FileSystemEntryMetadata& aHandle,
                                        const Name& aNewName) override;

  Result<EntryId, QMResult> MoveEntry(
      const FileSystemEntryMetadata& aHandle,
      const FileSystemChildMetadata& aNewDesignation) override;

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

 protected:
  Result<bool, QMResult> DoesFileIdExist(const FileId& aFileId) const override;

  nsresult RemoveFileId(const FileId& aFileId) override;

  Result<std::pair<nsTArray<FileId>, Usage>, QMResult>
  FindFilesWithoutDeprecatedLocksUnderEntry(
      const EntryId& aEntryId) const override;

  Result<nsTArray<std::pair<EntryId, FileId>>, QMResult>
  FindFileEntriesUnderDirectory(const EntryId& aEntryId) const override;

  Result<Ok, QMResult> DeprecateSharedLocksForDirectory(
      const FileSystemChildMetadata& aNewDesignation);
};

}  // namespace mozilla::dom::fs::data

#endif  // DOM_FS_PARENT_DATAMODEL_FILESYSTEMDATABASEMANAGERVERSION002_H_
