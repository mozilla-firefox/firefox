/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsLocalFileWIN_H_
#define _nsLocalFileWIN_H_

#include <shlobj.h>
#include <sys/stat.h>
#include <windows.h>

#include "nsCRT.h"
#include "nsError.h"
#include "nsIClassInfoImpl.h"
#include "nsIFile.h"
#include "nsILocalFileWin.h"
#include "nsString.h"
#include "nsWindowsHelpers.h"
#include "nscore.h"
#include "prio.h"

class nsLocalFile final : public nsILocalFileWin {
 public:
  NS_DEFINE_STATIC_CID_ACCESSOR(NS_LOCAL_FILE_CID)

  nsLocalFile();

  static nsresult nsLocalFileConstructor(const nsIID& aIID,
                                         void** aInstancePtr);

  // nsISupports interface
  NS_DECL_THREADSAFE_ISUPPORTS

  // nsIFile interface
  NS_DECL_NSIFILE

  // nsILocalFileWin interface
  NS_DECL_NSILOCALFILEWIN

 public:
  // Removes registry command handler parameters, quotes, and expands
  // environment strings.
  static bool CleanupCmdHandlerPath(nsAString& aCommandHandler);
  // Called off the main thread to open the window revealing the file
  static nsresult RevealFile(const nsString& aResolvedPath);

  // Checks if the filename is one of the windows reserved filenames
  // (com1, com2, etc...) and returns true if so.
  static bool CheckForReservedFileName(const nsAString& aFileName);

  /**
   * Checks whether the inherited ACEs in aChildDacl only come from the parent,
   * by comparing them, in order, with the ACEs that would be inherited from it.
   *
   * @param aChildDacl the DACL for the child file object
   * @param aIsChildDir true if child is a directory
   * @param aChildSecDesc the SECURITY_DESCRIPTOR for the child, this will
   *                      correspond to aChildDacl
   * @param aParentDir the parent nsIFile
   * @return true if inherited ACEs exactly match those from aParentDir
   *         false if child has other inherited ACEs or a failure occurs
   */
  static bool ChildAclMatchesAclInheritedFromParent(
      const mozilla::NotNull<ACL*> aChildDacl, bool aIsChildDir,
      const AutoFreeSecurityDescriptor& aChildSecDesc, nsIFile* aParentDir);

  // PRFileInfo64 does not hvae an accessTime field;
  struct FileInfo {
    PRFileType type;
    PROffset64 size;
    PRTime creationTime;
    PRTime accessTime;
    PRTime modifyTime;
  };

 private:
  // CopyMove and CopySingleFile constants for |options| parameter:
  enum CopyFileOption {
    FollowSymlinks = 1u << 0,
    Move = 1u << 1,
    SkipNtfsAclReset = 1u << 2,
    Rename = 1u << 3
  };

  nsLocalFile(const nsLocalFile& aOther);
  ~nsLocalFile() = default;

  bool mDirty;  // cached information can only be used when this is false
  bool mResolveDirty;

  bool mUseDOSDevicePathSyntax;

  // this string will always be in native format!
  nsString mWorkingPath;

  // this will be the resolved path of shortcuts, it will *NEVER*
  // be returned to the user
  nsString mResolvedPath;

  // this string, if not empty, is the *short* pathname that represents
  // mWorkingPath
  nsString mShortWorkingPath;

  FileInfo mFileInfo;

  void MakeDirty() {
    mDirty = true;
    mResolveDirty = true;
    mShortWorkingPath.Truncate();
  }

  nsresult LookupExtensionIn(const char* const* aExtensionsArray,
                             size_t aArrayLength, bool* aResult);

  nsresult ResolveAndStat();
  nsresult Resolve();
  nsresult ResolveSymlink();

  void EnsureShortPath();

  nsresult CopyMove(nsIFile* aNewParentDir, const nsAString& aNewName,
                    uint32_t aOptions);
  /**
   * Moves or copies this file or directory as a single entity.
   * @param aDest the destination directory
   * @param aNewName optional new name
   * @param aOptions CopyFileOption flags
   */
  nsresult MoveOrCopyAsSingleFileOrDir(nsIFile* aDest,
                                       const nsAString& aNewName,
                                       uint32_t aOptions);

  enum class TimeField { AccessedTime, ModifiedTime };

  nsresult SetDateImpl(int64_t aTime, TimeField aTimeField);
  nsresult GetDateImpl(PRTime* aTime, TimeField aTimeField, bool aFollowLinks);
  nsresult HasFileAttribute(DWORD aFileAttrib, bool* aResult);
  nsresult AppendInternal(const nsString& aNode, bool aMultipleComponents);

  nsresult OpenNSPRFileDescMaybeShareDelete(int32_t aFlags, int32_t aMode,
                                            bool aShareDelete,
                                            PRFileDesc** aResult);
};

#endif
