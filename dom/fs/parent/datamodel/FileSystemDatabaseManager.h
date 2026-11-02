/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_FS_PARENT_DATAMODEL_FILESYSTEMDATABASEMANAGER_H_
#define DOM_FS_PARENT_DATAMODEL_FILESYSTEMDATABASEMANAGER_H_

#include "ResultConnection.h"
#include "mozilla/dom/FileSystemTypes.h"
#include "mozilla/dom/quota/ForwardDecls.h"
#include "mozilla/dom/quota/UsageInfo.h"
#include "nsStringFwd.h"

template <class T>
class nsCOMPtr;

class nsIFile;

namespace mozilla {

template <typename V, typename E>
class Result;

namespace dom {

namespace quota {

struct OriginMetadata;

}  // namespace quota

namespace fs {

struct FileId;
enum class FileMode;
class FileSystemChildMetadata;
class FileSystemEntryMetadata;
class FileSystemDirectoryListing;
class FileSystemEntryPair;

namespace data {

using FileSystemConnection = fs::ResultConnection;

class FileSystemDatabaseManager {
 public:
  /**
   * @brief Updates stored usage data for all tracked files.
   *
   * @return nsresult error code
   */
  static nsresult RescanUsages(const ResultConnection& aConnection,
                               const quota::OriginMetadata& aOriginMetadata);

  /**
   * @brief Obtains the current total usage for origin and connection.
   *
   * @return Result<quota::UsageInfo, QMResult> On success,
   *  - field UsageInfo::DatabaseUsage contains the sum of current
   *    total database and file usage,
   *  - field UsageInfo::FileUsage is not used and should be equal to Nothing.
   *
   * If the disk is inaccessible, various IO related errors may be returned.
   */
  static Result<quota::UsageInfo, QMResult> GetUsage(
      const ResultConnection& aConnection,
      const quota::OriginMetadata& aOriginMetadata);

  /**
   * @brief Obtains the current total usage.
   *
   * @return Result<quota::UsageInfo, QMResult> On success,
   *  - field UsageInfo::DatabaseUsage contains the sum of current
   *    total database and file usage,
   *  - field UsageInfo::FileUsage is not used and should be equal to Nothing.
   */
  virtual Result<quota::UsageInfo, QMResult> GetUsage() const = 0;

  /**
   * @brief Refreshes the stored file size.
   *
   * @param aEntry EntryId of the file whose size is refreshed.
   */
  virtual nsresult UpdateUsage(const FileId& aFileId) = 0;

  /**
   * @brief Returns directory identifier, optionally creating it if it doesn't
   * exist
   *
   * @param aHandle Current directory and filename
   * @return Result<bool, QMResult> Directory identifier or error
   */
  virtual Result<EntryId, QMResult> GetOrCreateDirectory(
      const FileSystemChildMetadata& aHandle, bool aCreate) = 0;

  /**
   * @brief Returns file identifier, optionally creating it if it doesn't exist
   *
   * @param aHandle Current directory and filename
   * @param aType Content type which is ignored if the file already exists
   * @param aCreate true if file is to be created when it does not already exist
   * @return Result<bool, QMResult> File identifier or error
   */
  virtual Result<EntryId, QMResult> GetOrCreateFile(
      const FileSystemChildMetadata& aHandle, bool aCreate) = 0;

  /**
   * @brief Returns the properties of a file corresponding to a file handle
   */
  virtual nsresult GetFile(const EntryId& aEntryId, const FileId& aFileId,
                           const FileMode& aMode, ContentType& aType,
                           TimeStamp& lastModifiedMilliSeconds, Path& aPath,
                           nsCOMPtr<nsIFile>& aFile) const = 0;

  virtual Result<FileSystemDirectoryListing, QMResult> GetDirectoryEntries(
      const EntryId& aParent, PageNumber aPage) const = 0;

  /**
   * @brief Removes a directory
   *
   * @param aHandle Current directory and filename
   * @return Result<bool, QMResult> False if file did not exist, otherwise true
   * or error
   */
  virtual Result<bool, QMResult> RemoveDirectory(
      const FileSystemChildMetadata& aHandle, bool aRecursive) = 0;

  /**
   * @brief Removes a file
   *
   * @param aHandle Current directory and filename
   * @return Result<bool, QMResult> False if file did not exist, otherwise true
   * or error
   */
  virtual Result<bool, QMResult> RemoveFile(
      const FileSystemChildMetadata& aHandle) = 0;

  /**
   * @brief Rename a file/directory
   *
   * @param aHandle Source directory or file
   * @param aNewName New entry name
   * @return Result<EntryId, QMResult> The relevant entry id or error
   */
  virtual Result<EntryId, QMResult> RenameEntry(
      const FileSystemEntryMetadata& aHandle, const Name& aNewName) = 0;

  /**
   * @brief Move a file/directory
   *
   * @param aHandle Source directory or file
   * @param aNewDesignation Destination directory and entry name
   * @return Result<EntryId, QMResult> The relevant entry id or error
   */
  virtual Result<EntryId, QMResult> MoveEntry(
      const FileSystemEntryMetadata& aHandle,
      const FileSystemChildMetadata& aNewDesignation) = 0;

  /**
   * @brief Tries to connect a parent directory to a file system item with a
   * path, excluding the parent directory
   *
   * @param aHandle Pair of parent directory and child item candidates
   * @return Result<Path, QMResult> Path or error if no it didn't exists
   */
  virtual Result<Path, QMResult> Resolve(
      const FileSystemEntryPair& aEndpoints) const = 0;

  /**
   * @brief Returns true only if a file with a given EntryId exists.
   */
  virtual Result<bool, QMResult> DoesFileExist(
      const EntryId& aEntryId) const = 0;

  /**
   * @brief Generates an EntryId for a given parent EntryId and filename.
   */
  virtual Result<EntryId, QMResult> GetEntryId(
      const FileSystemChildMetadata& aHandle) const = 0;

  /**
   * @brief To check if a file under a directory is locked, we need to map
   * fileId's to entries.
   *
   * @param aFileId a FileId
   * @return Result<EntryId, QMResult> Entry id of a temporary or main file
   */
  virtual Result<EntryId, QMResult> GetEntryId(const FileId& aFileId) const = 0;

  /**
   * @brief Make sure EntryId maps to a FileId. This method should be called
   * before exclusive locking is attempted.
   */
  virtual Result<FileId, QMResult> EnsureFileId(const EntryId& aEntryId) = 0;

  /**
   * @brief Make sure EntryId maps to a temporary FileId. This method should be
   * called before shared locking is attempted.
   */
  virtual Result<FileId, QMResult> EnsureTemporaryFileId(
      const EntryId& aEntryId) = 0;

  /**
   * @brief To support moves in metadata, the actual files on disk are tagged
   * with file id's which are mapped to entry id's which represent paths.
   * This function returns the main file corresponding to an entry.
   *
   * @param aEntryId An id of an entry
   * @return Result<EntryId, QMResult> Main file id, used by exclusive locks
   */
  virtual Result<FileId, QMResult> GetFileId(const EntryId& aEntryId) const = 0;

  /**
   * @brief Flag aFileId as the main file for aEntryId or abort. Removes the
   * file which did not get flagged as the main file.
   */
  virtual nsresult MergeFileId(const EntryId& aEntryId, const FileId& aFileId,
                               bool aAbort) = 0;

  /**
   * @brief Removes files which are not the main file of any entry, along with
   * their file ids and usage. Such files are temporary files left behind by a
   * crash; they can only be removed safely while nothing can reference them,
   * i.e. right after the database has been opened.
   */
  virtual nsresult RemoveFileIdsWithoutMainFile() = 0;

  /**
   * @brief Close database connection.
   */
  virtual void Close() = 0;

  /**
   * @brief Start tracking file's usage.
   */
  virtual nsresult BeginUsageTracking(const FileId& aFileId) = 0;

  /**
   * @brief Stop tracking file's usage.
   */
  virtual nsresult EndUsageTracking(const FileId& aFileId) = 0;

  virtual ~FileSystemDatabaseManager() = default;
};

}  // namespace data
}  // namespace fs
}  // namespace dom
}  // namespace mozilla

#endif  // DOM_FS_PARENT_DATAMODEL_FILESYSTEMDATABASEMANAGER_H_
