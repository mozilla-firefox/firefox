/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "prio.h"
#include "prsystem.h"

#include "nsIFile.h"
#include "nsComponentManagerUtils.h"
#include "nsString.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsPrintfCString.h"

#ifdef XP_WIN
#  include <aclapi.h>
#  include "mozilla/RandomNum.h"
#  include "nsILocalFileWin.h"
#  include "nsLocalFile.h"
#  include "nsWindowsHelpers.h"
#else
#  include <limits.h>
#endif

#include <algorithm>

#include "gtest/gtest.h"
#include "mozilla/gtest/MozAssertions.h"

#ifdef XP_WIN
using namespace mozilla;
bool gTestWithPrefix_Win = false;
#endif

static bool VerifyResult(nsresult aRV, const char* aMsg) {
  bool failed = NS_FAILED(aRV);
  EXPECT_FALSE(failed) << aMsg << " rv=" << std::hex << (unsigned int)aRV;
  return !failed;
}

#ifdef XP_WIN
static void SetUseDOSDevicePathSyntax(nsIFile* aFile) {
  if (gTestWithPrefix_Win) {
    nsresult rv;
    nsCOMPtr<nsILocalFileWin> winFile = do_QueryInterface(aFile, &rv);
    VerifyResult(rv, "Querying nsILocalFileWin");

    MOZ_RELEASE_ASSERT(winFile);
    winFile->SetUseDOSDevicePathSyntax(true);
  }
}

static auto GetSecurityInfoStructured(nsIFile* aFile) {
  nsAutoString pathStr;
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(aFile->GetTarget(pathStr)));

  PACL pDacl = nullptr;
  AutoFreeSecurityDescriptor secDesc;
  DWORD errCode = ::GetNamedSecurityInfoW(
      pathStr.getW(), SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION |
          GROUP_SECURITY_INFORMATION,
      nullptr, nullptr, &pDacl, nullptr, getter_Transfers(secDesc));
  MOZ_RELEASE_ASSERT(errCode == ERROR_SUCCESS && pDacl);

  return std::make_tuple(std::move(pathStr), WrapNotNull(pDacl),
                         std::move(secDesc));
}

static bool ChildAclMatchesParent(nsIFile* aChild, nsIFile* aParentDir) {
  auto [childPath, childDacl, childSecDesc] = GetSecurityInfoStructured(aChild);
  bool isDir = false;
  EXPECT_NS_SUCCEEDED(aChild->IsDirectory(&isDir));
  return nsLocalFile::ChildAclMatchesAclInheritedFromParent(
      childDacl, isDir, childSecDesc, aParentDir);
}

// SECURITY_SID_SIZE() is not defined in mingw headers
#  if !defined(__MINGW32__)
static void AddAcesForRandomSidToDir(nsIFile* aDir) {
  auto [dirPath, pDirDacl, secDesc] = GetSecurityInfoStructured(aDir);

  constexpr BYTE kSubAuthorityCount = 4;
  BYTE randomSidBuffer[SECURITY_SID_SIZE(4)];
  ASSERT_TRUE(
      GenerateRandomBytesFromOS(randomSidBuffer, sizeof(randomSidBuffer)));
  auto* randomSid = reinterpret_cast<SID*>(randomSidBuffer);
  randomSid->Revision = SID_REVISION;
  randomSid->SubAuthorityCount = kSubAuthorityCount;
  randomSid->IdentifierAuthority = SECURITY_NULL_SID_AUTHORITY;
  ASSERT_TRUE(::IsValidSid(randomSid));

  EXPLICIT_ACCESS_W newAccess[2];
  newAccess[0].grfAccessMode = GRANT_ACCESS;
  newAccess[0].grfAccessPermissions = GENERIC_READ;
  newAccess[0].grfInheritance = SUB_OBJECTS_ONLY_INHERIT;
  ::BuildTrusteeWithSidW(&newAccess[0].Trustee, randomSid);
  newAccess[1].grfAccessMode = DENY_ACCESS;
  newAccess[1].grfAccessPermissions = GENERIC_WRITE;
  newAccess[1].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
  ::BuildTrusteeWithSidW(&newAccess[1].Trustee, randomSid);
  UniquePtr<ACL, LocalFreeDeleter> newDacl;
  ASSERT_EQ(::SetEntriesInAclW(std::size(newAccess), newAccess, pDirDacl,
                               getter_Transfers(newDacl)),
            (ULONG)ERROR_SUCCESS);

  ASSERT_EQ(::SetNamedSecurityInfoW(dirPath.get(), SE_FILE_OBJECT,
                                    DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                    newDacl.get(), nullptr),
            (ULONG)ERROR_SUCCESS);
}

static void ExpectSameDacl(nsIFile* aFile, nsIFile* aExpectedFile) {
  auto [path, dacl, secDesc] = GetSecurityInfoStructured(aFile);
  auto [expectedPath, expectedDacl, expectedSecDesc] =
      GetSecurityInfoStructured(aExpectedFile);

  ACL_SIZE_INFORMATION sizeInfo;
  ASSERT_TRUE(::GetAclInformation(dacl.get(), &sizeInfo, sizeof(sizeInfo),
                                  AclSizeInformation));
  ACL_SIZE_INFORMATION expectedSizeInfo;
  ASSERT_TRUE(::GetAclInformation(expectedDacl.get(), &expectedSizeInfo,
                                  sizeof(expectedSizeInfo),
                                  AclSizeInformation));
  ASSERT_EQ(sizeInfo.AceCount, expectedSizeInfo.AceCount)
      << NS_ConvertUTF16toUTF8(path).get() << " has a different ACE count to "
      << NS_ConvertUTF16toUTF8(expectedPath).get();
  ASSERT_EQ(sizeInfo.AclBytesInUse, expectedSizeInfo.AclBytesInUse);
  EXPECT_EQ(memcmp(dacl.get() + 1, expectedDacl.get() + 1,
                   sizeInfo.AclBytesInUse - sizeof(ACL)),
            0)
      << NS_ConvertUTF16toUTF8(path).get() << " has different ACEs to "
      << NS_ConvertUTF16toUTF8(expectedPath).get();
}
#  endif
#endif

static already_AddRefed<nsIFile> NewFile(nsIFile* aBase) {
  nsresult rv;
  nsCOMPtr<nsIFile> file;
  rv = NS_NewLocalFileWithFile(aBase, getter_AddRefs(file));
  VerifyResult(rv, "NS_NewLocalFileWithFile");

#ifdef XP_WIN
  SetUseDOSDevicePathSyntax(file);
#endif

  return file.forget();
}

template <typename char_type>
static nsTString<char_type> FixName(const char_type* aName) {
  nsTString<char_type> name;
  for (uint32_t i = 0; aName[i]; ++i) {
    char_type ch = aName[i];
    // PR_GetPathSeparator returns the wrong value on Mac so don't use it
#if defined(XP_WIN)
    if (ch == '/') {
      ch = '\\';
    }
#endif
    name.Append(ch);
  }
  return name;
}

// Test nsIFile::AppendNative, verifying that aName is not a valid file name
static bool TestInvalidFileName(nsIFile* aBase, const char* aName) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (NS_SUCCEEDED(rv)) {
    EXPECT_NS_FAILED(rv) << "AppendNative with invalid filename " << name.get();
    return false;
  }

  return true;
}

// Test nsIFile::Create, verifying that the file exists and did not exist
// before, and leaving it there for future tests
static bool TestCreate(nsIFile* aBase, const char* aName, int32_t aType,
                       int32_t aPerm, nsIFile** aNewFile = nullptr) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (!VerifyResult(rv, "AppendNative")) return false;

  bool exists;
  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (before)")) return false;
  EXPECT_FALSE(exists) << "File " << name.get() << " already exists";
  if (exists) {
    return false;
  }

  rv = file->Create(aType, aPerm);
  if (!VerifyResult(rv, "Create")) return false;

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (after)")) return false;
  EXPECT_TRUE(exists) << "File " << name.get() << " was not created";
  if (!exists) {
    return false;
  }

  if (aNewFile) {
    file.forget(aNewFile);
  }

  return true;
}

// Test nsIFile::CreateUnique, verifying that the new file exists and if it
// existed before, the new file has a different name. The new file is left in
// place.
static bool TestCreateUnique(nsIFile* aBase, const char* aName, int32_t aType,
                             int32_t aPerm) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (!VerifyResult(rv, "AppendNative")) return false;

  bool existsBefore;
  rv = file->Exists(&existsBefore);
  if (!VerifyResult(rv, "Exists (before)")) return false;

  rv = file->CreateUnique(aType, aPerm);
  if (!VerifyResult(rv, "Create")) return false;

  bool existsAfter;
  rv = file->Exists(&existsAfter);
  if (!VerifyResult(rv, "Exists (after)")) return false;
  EXPECT_TRUE(existsAfter) << "File " << name.get() << " was not created";
  if (!existsAfter) {
    return false;
  }

  if (existsBefore) {
    nsAutoCString leafName;
    rv = file->GetNativeLeafName(leafName);
    if (!VerifyResult(rv, "GetNativeLeafName")) return false;
    EXPECT_FALSE(leafName.Equals(name))
        << "File " << name.get() << " was not given a new name by CreateUnique";
    if (leafName.Equals(name)) {
      return false;
    }
  }

  return true;
}

// Test nsIFile::OpenNSPRFileDesc with DELETE_ON_CLOSE, verifying that the file
// exists and did not exist before, and leaving it there for future tests
static bool TestDeleteOnClose(nsIFile* aBase, const char* aName, int32_t aFlags,
                              int32_t aPerm) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (!VerifyResult(rv, "AppendNative")) return false;

  bool exists;
  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (before)")) return false;
  EXPECT_FALSE(exists) << "File " << name.get() << " already exists";
  if (exists) {
    return false;
  }

  PRFileDesc* fileDesc;
  rv = file->OpenNSPRFileDesc(aFlags | nsIFile::DELETE_ON_CLOSE, aPerm,
                              &fileDesc);
  if (!VerifyResult(rv, "OpenNSPRFileDesc")) return false;
  PRStatus status = PR_Close(fileDesc);
  EXPECT_EQ(status, PR_SUCCESS)
      << "File " << name.get() << " could not be closed";
  if (status != PR_SUCCESS) {
    return false;
  }

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (after)")) return false;
  EXPECT_FALSE(exists) << "File " << name.get() << " was not removed on close";
  if (exists) {
    return false;
  }

  return true;
}

// Test nsIFile::Remove, verifying that the file does not exist and did before
static bool TestRemove(nsIFile* aBase, const char* aName, bool aRecursive,
                       uint32_t aExpectedRemoveCount = 1) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (!VerifyResult(rv, "AppendNative")) return false;

  bool exists;
  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (before)")) return false;
  EXPECT_TRUE(exists);
  if (!exists) {
    return false;
  }

  uint32_t removeCount = 0;
  rv = file->Remove(aRecursive, &removeCount);
  if (!VerifyResult(rv, "Remove")) return false;
  EXPECT_EQ(removeCount, aExpectedRemoveCount) << "Removal count was wrong";

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (after)")) return false;
  EXPECT_FALSE(exists) << "File " << name.get() << " was not removed";
  if (exists) {
    return false;
  }

  return true;
}

// Test nsIFile::MoveToNative, verifying that the file did not exist at the new
// location before and does afterward, and that it does not exist at the old
// location anymore
static bool TestMove(nsIFile* aBase, nsIFile* aDestDir, const char* aName,
                     const char* aNewName) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (!VerifyResult(rv, "AppendNative")) return false;

  bool exists;
  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (before)")) return false;
  EXPECT_TRUE(exists);
  if (!exists) {
    return false;
  }

  nsCOMPtr<nsIFile> newFile = NewFile(file);
  nsCString newName = FixName(aNewName);
  rv = newFile->MoveToNative(aDestDir, newName);
  if (!VerifyResult(rv, "MoveToNative")) return false;

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (after)")) return false;
  EXPECT_FALSE(exists) << "File " << name.get() << " was not moved";
  if (exists) {
    return false;
  }

  file = NewFile(aDestDir);
  if (!file) return false;
  rv = file->AppendNative(newName);
  if (!VerifyResult(rv, "AppendNative")) return false;
  bool equal;
  rv = file->Equals(newFile, &equal);
  if (!VerifyResult(rv, "Equals")) return false;
  EXPECT_TRUE(equal) << "File object was not updated to destination";
  if (!equal) {
    return false;
  }

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (new after)")) return false;
  EXPECT_TRUE(exists) << "Destination file " << newName.get()
                      << " was not created";
  if (!exists) {
    return false;
  }

#ifdef XP_WIN
  // Ensure ACE inheritance is correct.
  auto [childPathStr, childDacl, childSecDesc] =
      GetSecurityInfoStructured(file);
  EXPECT_FALSE(childSecDesc->Control & SE_DACL_PROTECTED)
      << "SE_DACL_PROTECTED bit should not be set on the security descriptor";
  EXPECT_TRUE(ChildAclMatchesParent(file, aDestDir))
      << newName.get() << " ACL, does not match destination dir";
#endif

  return true;
}

// Test nsIFile::CopyToNative, verifying that the file did not exist at the new
// location before and does afterward, and that it does exist at the old
// location too
static bool TestCopy(nsIFile* aBase, nsIFile* aDestDir, const char* aName,
                     const char* aNewName) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  nsCString name = FixName(aName);
  nsresult rv = file->AppendNative(name);
  if (!VerifyResult(rv, "AppendNative")) return false;

  bool exists;
  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (before)")) return false;
  EXPECT_TRUE(exists);
  if (!exists) {
    return false;
  }

  nsCOMPtr<nsIFile> newFile = NewFile(file);
  nsCString newName = FixName(aNewName);
  rv = newFile->CopyToNative(aDestDir, newName);
  if (!VerifyResult(rv, "MoveToNative")) return false;
  bool equal;
  rv = file->Equals(newFile, &equal);
  if (!VerifyResult(rv, "Equals")) return false;
  EXPECT_TRUE(equal) << "File object updated unexpectedly";
  if (!equal) {
    return false;
  }

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (after)")) return false;
  EXPECT_TRUE(exists) << "File " << name.get() << " was removed";
  if (!exists) {
    return false;
  }

  file = NewFile(aDestDir);
  if (!file) return false;
  rv = file->AppendNative(newName);
  if (!VerifyResult(rv, "AppendNative")) return false;

  rv = file->Exists(&exists);
  if (!VerifyResult(rv, "Exists (new after)")) return false;
  EXPECT_TRUE(exists) << "Destination file " << newName.get()
                      << " was not created";
  if (!exists) {
    return false;
  }

  return true;
}

// Test nsIFile::GetParent
static bool TestParent(nsIFile* aBase, nsIFile* aStart) {
  nsCOMPtr<nsIFile> file = NewFile(aStart);
  if (!file) return false;

  nsCOMPtr<nsIFile> parent;
  nsresult rv = file->GetParent(getter_AddRefs(parent));
  VerifyResult(rv, "GetParent");

  bool equal;
  rv = parent->Equals(aBase, &equal);
  VerifyResult(rv, "Equals");
  EXPECT_TRUE(equal) << "Incorrect parent";
  if (!equal) {
    return false;
  }

  return true;
}

// Test nsIFile::Contains
static bool TestContains(nsIFile* aBase) {
  nsCOMPtr<nsIFile> sub = NewFile(aBase);
  if (!sub) return false;
  nsresult rv = sub->AppendNative(nsDependentCString("sub"));
  if (!VerifyResult(rv, "AppendNative sub")) return false;

  nsCOMPtr<nsIFile> subSlashSub = NewFile(sub);
  if (!subSlashSub) return false;
  rv = subSlashSub->AppendNative(nsDependentCString("sub"));
  if (!VerifyResult(rv, "AppendNative sub/sub")) return false;

  nsCOMPtr<nsIFile> subsub = NewFile(aBase);
  if (!subsub) return false;
  rv = subsub->AppendNative(nsDependentCString("subsub"));
  if (!VerifyResult(rv, "AppendNative subsub")) return false;

  bool contains;
  rv = sub->Contains(subsub, &contains);
  VerifyResult(rv, "sub contains subsub");
  EXPECT_FALSE(contains) << "sub contains subsub is false";

  rv = sub->Contains(sub, &contains);
  VerifyResult(rv, "sub contains sub (itself)");
  EXPECT_FALSE(contains) << "sub contains sub (itself) is false";

  rv = sub->Contains(subSlashSub, &contains);
  VerifyResult(rv, "sub contains sub/sub");
  EXPECT_TRUE(contains) << "sub contains sub/sub is true";

  // Regression test for bug 1975674: Contains null should not crash.
  rv = sub->Contains(nullptr, &contains);
  EXPECT_NS_FAILED(rv) << "Contains null fails";
  EXPECT_EQ(rv, NS_ERROR_INVALID_ARG) << "rv for contains(null)";

  return true;
}

// Test nsIFile::Normalize and native path setting/getting
static bool TestNormalizeNativePath(nsIFile* aBase, nsIFile* aStart) {
  nsCOMPtr<nsIFile> file = NewFile(aStart);
  if (!file) return false;

  auto path = file->NativePath();
#ifdef XP_WIN
  path.Append(FixName(u"/./.."));
  nsresult rv = file->InitWithPath(path);
  VerifyResult(rv, "InitWithPath");
#else
  path.Append(FixName("/./.."));
  nsresult rv = file->InitWithNativePath(path);
  VerifyResult(rv, "InitWithNativePath");
#endif
  rv = file->Normalize();
  VerifyResult(rv, "Normalize");
  path = file->NativePath();

  auto basePath = aBase->NativePath();
  VerifyResult(rv, "GetNativePath (base)");

  EXPECT_TRUE(path.Equals(basePath))
      << "Incorrect normalization: " << file->HumanReadablePath().get() << " - "
      << aBase->HumanReadablePath().get();
  if (!path.Equals(basePath)) {
    return false;
  }

  return true;
}

// Test nsIFile::GetDiskSpaceAvailable
static bool TestDiskSpaceAvailable(nsIFile* aBase) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  int64_t diskSpaceAvailable = 0;
  nsresult rv = file->GetDiskSpaceAvailable(&diskSpaceAvailable);
  VerifyResult(rv, "GetDiskSpaceAvailable");

  EXPECT_GE(diskSpaceAvailable, 0);

  return true;
}

// Test nsIFile::GetDiskCapacity
static bool TestDiskCapacity(nsIFile* aBase) {
  nsCOMPtr<nsIFile> file = NewFile(aBase);
  if (!file) return false;

  int64_t diskCapacity = 0;
  nsresult rv = file->GetDiskCapacity(&diskCapacity);
  VerifyResult(rv, "GetDiskCapacity");

  EXPECT_GE(diskCapacity, 0);

  return true;
}

static void SetupAndTestFunctions(const nsAString& aDirName,
                                  bool aTestCreateUnique, bool aTestNormalize) {
  nsCOMPtr<nsIFile> base;
  nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(base));
  ASSERT_TRUE(VerifyResult(rv, "Getting temp directory"));

#ifdef XP_WIN
  SetUseDOSDevicePathSyntax(base);
#endif

  rv = base->Append(aDirName);
  ASSERT_TRUE(
      VerifyResult(rv, nsPrintfCString("Appending %s to temp directory name",
                                       NS_ConvertUTF16toUTF8(aDirName).get())
                           .get()));

  // Remove the directory in case tests failed and left it behind.
  // don't check result since it might not be there
  base->Remove(true);

  // Now create the working directory we're going to use
  rv = base->Create(nsIFile::DIRECTORY_TYPE, 0700);
  ASSERT_TRUE(VerifyResult(rv, "Creating temp directory"));

  // Now we can safely normalize the path
  if (aTestNormalize) {
    rv = base->Normalize();
    ASSERT_TRUE(VerifyResult(rv, "Normalizing temp directory name"));
  }

  // Initialize subdir object for later use
  nsCOMPtr<nsIFile> subdir = NewFile(base);
  ASSERT_TRUE(subdir);

  rv = subdir->AppendNative(nsDependentCString("subdir"));
  ASSERT_TRUE(VerifyResult(rv, "Appending 'subdir' to test dir name"));

  // ---------------
  // End setup code.
  // ---------------

  // Test leafName
  nsString leafName;
  rv = base->GetLeafName(leafName);
  ASSERT_TRUE(VerifyResult(rv, "Getting leafName"));
  ASSERT_TRUE(leafName.Equals(aDirName));

  // Test path parsing
  ASSERT_TRUE(TestInvalidFileName(base, "a/b"));
  ASSERT_TRUE(TestParent(base, subdir));

  // Test nsIFile::Contains
  ASSERT_TRUE(TestContains(base));

  // Test file creation
  ASSERT_TRUE(TestCreate(base, "file.txt", nsIFile::NORMAL_FILE_TYPE, 0600));
  ASSERT_TRUE(TestRemove(base, "file.txt", false));

  // Test directory creation
  ASSERT_TRUE(TestCreate(base, "subdir", nsIFile::DIRECTORY_TYPE, 0700));

  // Test move and copy in the base directory
  ASSERT_TRUE(TestCreate(base, "file.txt", nsIFile::NORMAL_FILE_TYPE, 0600));
  ASSERT_TRUE(TestMove(base, base, "file.txt", "file2.txt"));
  ASSERT_TRUE(TestCopy(base, base, "file2.txt", "file3.txt"));

  // Test moving across directories
  ASSERT_TRUE(TestMove(base, subdir, "file2.txt", "file2.txt"));

  // Test moving across directories and renaming at the same time
  ASSERT_TRUE(TestMove(subdir, base, "file2.txt", "file4.txt"));

  // SECURITY_SID_SIZE() is not defined in mingw headers
#if defined(XP_WIN) && !defined(__MINGW32__)
  // On Windows if we move a file or directory to a directory on the same volume
  // where the inherited ACLs differ between the source and target dirs, then we
  // retain the ACEs from the original dir. Add inherited ACEs for random SIDs
  // to subdir to ensure we reset the inheritance to just the target directory.
  AddAcesForRandomSidToDir(subdir);
  ASSERT_TRUE(TestCreate(subdir, "file8.txt", nsIFile::NORMAL_FILE_TYPE, 0600));
  ASSERT_TRUE(TestMove(subdir, base, "file8.txt", "file8.txt"));

  // Test that dir moves also get the ACL reset when required.
  ASSERT_TRUE(TestCreate(subdir, "subdir2", nsIFile::DIRECTORY_TYPE, 0700));
  ASSERT_TRUE(TestMove(subdir, base, "subdir2", "subdir2"));

  // If we set the SE_DACL_PROTECTED bit on the file's security descriptor then
  // no ACEs will be inherited in the moved file or the new ACL we create to
  // compare it with. So we need an extra test to catch this case.
  nsCOMPtr<nsIFile> file9;
  ASSERT_TRUE(TestCreate(subdir, "file9.txt", nsIFile::NORMAL_FILE_TYPE, 0600,
                         getter_AddRefs(file9)));
  auto [file9Path, pFile9Dacl, f9sd] = GetSecurityInfoStructured(file9);
  ASSERT_EQ(::SetNamedSecurityInfoW(
                file9Path.get(), SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                nullptr, nullptr, pFile9Dacl.get(), nullptr),
            (ULONG)ERROR_SUCCESS);
  auto [f9p, f9d, file9SecDescAfter] = GetSecurityInfoStructured(file9);
  ASSERT_TRUE(file9SecDescAfter->Control & SE_DACL_PROTECTED);
  ASSERT_TRUE(TestMove(subdir, base, "file9.txt", "file9.txt"));

  // If the source and target dirs pass on the same number of inherited ACEs,
  // but they are different, then we still need to reset the inheritance.
  nsCOMPtr<nsIFile> subdir3;
  ASSERT_TRUE(TestCreate(base, "subdir3", nsIFile::DIRECTORY_TYPE, 0700,
                         getter_AddRefs(subdir3)));
  AddAcesForRandomSidToDir(subdir3);
  nsCOMPtr<nsIFile> subdir4;
  ASSERT_TRUE(TestCreate(base, "subdir4", nsIFile::DIRECTORY_TYPE, 0700,
                         getter_AddRefs(subdir4)));
  AddAcesForRandomSidToDir(subdir4);
  ASSERT_TRUE(
      TestCreate(subdir3, "file10.txt", nsIFile::NORMAL_FILE_TYPE, 0600));
  ASSERT_TRUE(TestMove(subdir3, subdir4, "file10.txt", "file10.txt"));
  nsCOMPtr<nsIFile> file10 = NewFile(subdir4);
  ASSERT_TRUE(file10);
  ASSERT_NS_SUCCEEDED(file10->AppendNative("file10.txt"_ns));
  nsCOMPtr<nsIFile> file11;
  ASSERT_TRUE(TestCreate(subdir4, "file11.txt", nsIFile::NORMAL_FILE_TYPE, 0600,
                         getter_AddRefs(file11)));
  ExpectSameDacl(file10, file11);

  // Children that inherit normally from a parent should match it, so that we
  // don't make unnecessary calls to reset their inheritance.
  EXPECT_TRUE(ChildAclMatchesParent(file11, subdir4));
  nsCOMPtr<nsIFile> subdir5;
  ASSERT_TRUE(TestCreate(subdir4, "subdir5", nsIFile::DIRECTORY_TYPE, 0700,
                         getter_AddRefs(subdir5)));
  EXPECT_TRUE(ChildAclMatchesParent(subdir5, subdir4));

  // This should also be the case for children of different dirs that inherit
  // the same ACEs.
  nsCOMPtr<nsIFile> subdir6;
  ASSERT_TRUE(TestCreate(base, "subdir6", nsIFile::DIRECTORY_TYPE, 0700,
                         getter_AddRefs(subdir6)));
  nsCOMPtr<nsIFile> subdir7;
  ASSERT_TRUE(TestCreate(base, "subdir7", nsIFile::DIRECTORY_TYPE, 0700,
                         getter_AddRefs(subdir7)));
  nsCOMPtr<nsIFile> file12;
  ASSERT_TRUE(TestCreate(subdir6, "file12.txt", nsIFile::NORMAL_FILE_TYPE, 0600,
                         getter_AddRefs(file12)));
  EXPECT_TRUE(ChildAclMatchesParent(file12, subdir7));
  nsCOMPtr<nsIFile> subdir8;
  ASSERT_TRUE(TestCreate(subdir6, "subdir8", nsIFile::DIRECTORY_TYPE, 0700,
                         getter_AddRefs(subdir8)));
  EXPECT_TRUE(ChildAclMatchesParent(subdir8, subdir7));
#endif

  // Test copying across directories
  ASSERT_TRUE(TestCopy(base, subdir, "file4.txt", "file5.txt"));

  if (aTestNormalize) {
    // Run normalization tests while the directory exists
    ASSERT_TRUE(TestNormalizeNativePath(base, subdir));
  }

  // Test recursive directory removal
  ASSERT_TRUE(TestRemove(base, "subdir", true, 2));

  if (aTestCreateUnique) {
    ASSERT_TRUE(TestCreateUnique(base, "foo", nsIFile::NORMAL_FILE_TYPE, 0600));
    ASSERT_TRUE(TestCreateUnique(base, "foo", nsIFile::NORMAL_FILE_TYPE, 0600));
    ASSERT_TRUE(
        TestCreateUnique(base, "bar.xx", nsIFile::DIRECTORY_TYPE, 0700));
    ASSERT_TRUE(
        TestCreateUnique(base, "bar.xx", nsIFile::DIRECTORY_TYPE, 0700));
  }

  ASSERT_TRUE(
      TestDeleteOnClose(base, "file7.txt", PR_RDWR | PR_CREATE_FILE, 0600));

  ASSERT_TRUE(TestDiskSpaceAvailable(base));
  ASSERT_TRUE(TestDiskCapacity(base));

  // Clean up temporary stuff
  rv = base->Remove(true);
  VerifyResult(rv, "Cleaning up temp directory");
}

TEST(TestFile, Unprefixed)
{
#ifdef XP_WIN
  gTestWithPrefix_Win = false;
#endif

  SetupAndTestFunctions(u"mozfiletests"_ns,
                        /* aTestCreateUnique */ true,
                        /* aTestNormalize */ true);

#ifdef XP_WIN
  gTestWithPrefix_Win = true;
#endif
}

// This simulates what QM_NewLocalFile does (NS_NewLocalFiles and then
// SetUseDOSDevicePathSyntax if it's on Windows for NewFile)
TEST(TestFile, PrefixedOnWin)
{
  SetupAndTestFunctions(u"mozfiletests"_ns,
                        /* aTestCreateUnique */ true,
                        /* aTestNormalize */ true);
}

TEST(TestFile, PrefixedOnWin_PathExceedsMaxPath)
{
  // We want to verify if the prefix would allow as to create a file with over
  // 260 char for its path. However, on Windows, the maximum length of filename
  // is 255. Given the base file path and we are going append some other file
  // to the current base file, let's assume the file path will exceed 260 so
  // that we are able to verify if the prefix works or not.
  nsString dirName;
  dirName.AssignLiteral("mozfiletests");
  for (uint32_t i = 255 - dirName.Length(); i > 0; --i) {
    dirName.AppendLiteral("a");
  }

  // Bypass the test for CreateUnique because there is a check for the max
  // length of the root on all platforms.
  SetupAndTestFunctions(dirName, /* aTestCreateUnique */ false,
                        /* aTestNormalize */ true);
}

TEST(TestFile, PrefixedOnWin_ComponentEndsWithPeriod)
{
  // Bypass the normalization for this because it would strip the trailing
  // period.
  SetupAndTestFunctions(u"mozfiletests."_ns,
                        /* aTestCreateUnique */ true,
                        /* aTestNormalize */ false);
}

TEST(TestFile, GetRelativePath)
{
  nsCOMPtr<nsIFile> base;
  ASSERT_NS_SUCCEEDED(
      NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(base)));

  nsCOMPtr<nsIFile> child;
  ASSERT_NS_SUCCEEDED(base->Clone(getter_AddRefs(child)));
  ASSERT_NS_SUCCEEDED(child->AppendNative("places.sqlite"_ns));

  nsAutoCString result;

  // Path without \\?\ prefix: child relative to base is just the filename.
  ASSERT_NS_SUCCEEDED(child->GetRelativePath(base, result));
  EXPECT_STREQ(result.get(), "places.sqlite");

#ifdef XP_WIN
  // GetRelativePath strips the \\?\ prefix before comparing, so paths with
  // and without the prefix must yield the same result.
  nsCOMPtr<nsIFile> prefixedBase;
  ASSERT_NS_SUCCEEDED(base->Clone(getter_AddRefs(prefixedBase)));
  nsCOMPtr<nsILocalFileWin> winPrefixedBase = do_QueryInterface(prefixedBase);
  ASSERT_TRUE(winPrefixedBase);
  winPrefixedBase->SetUseDOSDevicePathSyntax(true);

  nsCOMPtr<nsIFile> prefixedChild;
  ASSERT_NS_SUCCEEDED(child->Clone(getter_AddRefs(prefixedChild)));
  nsCOMPtr<nsILocalFileWin> winPrefixedChild = do_QueryInterface(prefixedChild);
  ASSERT_TRUE(winPrefixedChild);
  winPrefixedChild->SetUseDOSDevicePathSyntax(true);

  // Both paths with \\?\ prefix.
  ASSERT_NS_SUCCEEDED(prefixedChild->GetRelativePath(prefixedBase, result));
  EXPECT_STREQ(result.get(), "places.sqlite");

  // Prefixed child, unprefixed base.
  ASSERT_NS_SUCCEEDED(prefixedChild->GetRelativePath(base, result));
  EXPECT_STREQ(result.get(), "places.sqlite");

  // Unprefixed child, prefixed base.
  ASSERT_NS_SUCCEEDED(child->GetRelativePath(prefixedBase, result));
  EXPECT_STREQ(result.get(), "places.sqlite");
#endif
}

// These match the limits in nsLocalFileCommon.cpp's CreateUnique.
static constexpr size_t kMaxUniqueFilenameLength = 250;
#ifdef XP_WIN
static constexpr int32_t kMaxUniquePathLength = MAX_PATH - 6;
#else
static constexpr int32_t kMaxUniquePathLength = PATH_MAX - 6;
#endif

TEST(TestFile, CreateUnique_TruncatesLongFilename)
{
  nsCOMPtr<nsIFile> base;
  nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(base));
  ASSERT_NS_SUCCEEDED(rv);

  rv = base->AppendNative("cu_trunc_leaf_test"_ns);
  ASSERT_NS_SUCCEEDED(rv);
  base->Remove(true);
  rv = base->Create(nsIFile::DIRECTORY_TYPE, 0700);
  ASSERT_NS_SUCCEEDED(rv);

  // 260-char root + ".txt" = 264-char leaf, exceeds kMaxUniqueFilenameLength.
  // CreateUnique should truncate the root so the leaf fits.
  {
    nsCOMPtr<nsIFile> file = NewFile(base);
    ASSERT_TRUE(file);

    nsAutoCString name;
    for (int i = 0; i < 260; ++i) {
      name.Append('a');
    }
    name.AppendLiteral(".txt");

    rv = file->AppendNative(name);
    ASSERT_NS_SUCCEEDED(rv);

    rv = file->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
    ASSERT_NS_SUCCEEDED(rv);

    bool exists;
    rv = file->Exists(&exists);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_TRUE(exists);

    nsAutoCString leafName;
    rv = file->GetNativeLeafName(leafName);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_LE(leafName.Length(), kMaxUniqueFilenameLength);
    EXPECT_TRUE(StringEndsWith(leafName, ".txt"_ns));
  }

  // 260-char leaf with no extension.
  {
    nsCOMPtr<nsIFile> file = NewFile(base);
    ASSERT_TRUE(file);

    nsAutoCString name;
    for (int i = 0; i < 260; ++i) {
      name.Append('b');
    }

    rv = file->AppendNative(name);
    ASSERT_NS_SUCCEEDED(rv);

    rv = file->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
    ASSERT_NS_SUCCEEDED(rv);

    bool exists;
    rv = file->Exists(&exists);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_TRUE(exists);

    nsAutoCString leafName;
    rv = file->GetNativeLeafName(leafName);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_LE(leafName.Length(), kMaxUniqueFilenameLength);
  }

  base->Remove(true);
}

TEST(TestFile, CreateUnique_TruncatesLongPath)
{
  nsCOMPtr<nsIFile> base;
  nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(base));
  ASSERT_NS_SUCCEEDED(rv);

  rv = base->AppendNative("cu_trunc_path_test"_ns);
  ASSERT_NS_SUCCEEDED(rv);
  base->Remove(true);
  rv = base->Create(nsIFile::DIRECTORY_TYPE, 0700);
  ASSERT_NS_SUCCEEDED(rv);
  rv = base->Normalize();
  ASSERT_NS_SUCCEEDED(rv);

  // Build nested directories until the path is long enough that appending a
  // moderate leaf name (well under kMaxUniqueFilenameLength) pushes the total
  // past kMaxUniquePathLength.
  const int32_t kLeafLen = 104;  // 100-char root + ".dat"
  const int32_t kOvershoot = 50;

  nsCOMPtr<nsIFile> deepDir = NewFile(base);
  ASSERT_TRUE(deepDir);

#ifdef XP_WIN
  nsAutoString dirPath;
  rv = deepDir->GetPath(dirPath);
#else
  nsAutoCString dirPath;
  rv = deepDir->GetNativePath(dirPath);
#endif
  ASSERT_NS_SUCCEEDED(rv);

  int32_t targetDirLen = kMaxUniquePathLength - kLeafLen - 1 + kOvershoot;

  while (static_cast<int32_t>(dirPath.Length()) < targetDirLen) {
    int32_t remaining = targetDirLen - dirPath.Length() - 1;
    int32_t nameLen = std::min(remaining, 200);
    if (nameLen < 1) break;

    nsAutoCString dirName;
    for (int32_t i = 0; i < nameLen; ++i) {
      dirName.Append('d');
    }

    rv = deepDir->AppendNative(dirName);
    ASSERT_NS_SUCCEEDED(rv);
    rv = deepDir->Create(nsIFile::DIRECTORY_TYPE, 0700);
    ASSERT_NS_SUCCEEDED(rv);

#ifdef XP_WIN
    rv = deepDir->GetPath(dirPath);
#else
    rv = deepDir->GetNativePath(dirPath);
#endif
    ASSERT_NS_SUCCEEDED(rv);
  }

  // The leaf name is short enough for kMaxUniqueFilenameLength, but the total
  // path would exceed kMaxUniquePathLength.
  {
    nsCOMPtr<nsIFile> file = NewFile(deepDir);
    ASSERT_TRUE(file);

    nsAutoCString name;
    for (int i = 0; i < 100; ++i) {
      name.Append('f');
    }
    name.AppendLiteral(".dat");

    rv = file->AppendNative(name);
    ASSERT_NS_SUCCEEDED(rv);

#ifdef XP_WIN
    nsAutoString pathBefore;
    rv = file->GetPath(pathBefore);
#else
    nsAutoCString pathBefore;
    rv = file->GetNativePath(pathBefore);
#endif
    ASSERT_NS_SUCCEEDED(rv);
    ASSERT_GT(static_cast<int32_t>(pathBefore.Length()), kMaxUniquePathLength)
        << "Test setup: path should exceed the limit before CreateUnique";

    rv = file->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
    ASSERT_NS_SUCCEEDED(rv);

    bool exists;
    rv = file->Exists(&exists);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_TRUE(exists);

#ifdef XP_WIN
    nsAutoString pathAfter;
    rv = file->GetPath(pathAfter);
#else
    nsAutoCString pathAfter;
    rv = file->GetNativePath(pathAfter);
#endif
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_LE(static_cast<int32_t>(pathAfter.Length()), kMaxUniquePathLength);

    nsAutoCString leafName;
    rv = file->GetNativeLeafName(leafName);
    ASSERT_NS_SUCCEEDED(rv);
    EXPECT_TRUE(StringEndsWith(leafName, ".dat"_ns));
    EXPECT_LT(leafName.Length(), static_cast<size_t>(kLeafLen));
  }

  base->Remove(true);
}
