/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "IPCTestUtils.h"

#include "chrome/common/ipc_message.h"
#include "chrome/common/ipc_message_utils.h"
#include "gtest/gtest.h"

#include "mozilla/ipc/SharedMemoryCursor.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/ipc/SharedMemoryMapping.h"

#include <bit>

#ifdef XP_LINUX
#  include <errno.h>
#  include <linux/magic.h>
#  include <stdio.h>
#  include <string.h>
#  include <sys/statfs.h>
#  include <sys/utsname.h>
#endif

#ifdef XP_WIN
#  include <windows.h>
#endif

namespace mozilla::ipc {

struct SharedMemoryInternalTest : public testing::Test {
  // Changes our idea of the size without changing the size of the OS object;
  // mappings could fail, or could succeed but crash on access.
  void UnsafeSetSize(shared_memory::HandleBase& aHandle, uint64_t aSize) {
    aHandle.SetSize(aSize);
  }

  template <shared_memory::Type T>
  shared_memory::Handle<T> UnsafeConvertTo(
      shared_memory::HandleBase&& aHandle) {
    return std::move(aHandle).ConvertTo<T>();
  }

  shared_memory::MutableHandle UnsafeThaw(
      shared_memory::ReadOnlyHandle&& aHandle) {
    return UnsafeConvertTo<shared_memory::Type::Mutable>(std::move(aHandle));
  }
};

#define ASSERT_SHMEM(handle, size)            \
  do {                                        \
    ASSERT_EQ((handle).Size(), size_t(size)); \
    if (size_t(size) == 0) {                  \
      ASSERT_FALSE((handle).IsValid());       \
      ASSERT_FALSE(handle);                   \
    } else {                                  \
      ASSERT_TRUE((handle).IsValid());        \
      ASSERT_TRUE(handle);                    \
    }                                         \
  } while (0)

template <typename T>
struct IPCSharedMemoryFixture : public testing::Test {};

using HandleAndMappingTypes =
    testing::Types<MutableSharedMemoryHandle, ReadOnlySharedMemoryHandle,
                   FreezableSharedMemoryHandle, SharedMemoryMapping,
                   ReadOnlySharedMemoryMapping, FreezableSharedMemoryMapping,
                   MutableOrReadOnlySharedMemoryMapping>;
TYPED_TEST_SUITE(IPCSharedMemoryFixture, HandleAndMappingTypes);

TYPED_TEST(IPCSharedMemoryFixture, Null) {
  TypeParam t;
  ASSERT_SHMEM(t, 0);

  if constexpr (std::is_same_v<TypeParam, MutableSharedMemoryHandle> ||
                std::is_same_v<TypeParam, ReadOnlySharedMemoryHandle>) {
    auto cloned = t.Clone();
    ASSERT_SHMEM(cloned, 0);
    ASSERT_SHMEM(t, 0);
  }
}

TEST(IPCSharedMemoryHandle, Create)
{
  auto handle = shared_memory::Create(1);
  ASSERT_SHMEM(handle, 1);
}

TEST(IPCSharedMemoryHandle, Move)
{
  auto handle = shared_memory::Create(1);

  MutableSharedMemoryHandle newHandle(std::move(handle));
  ASSERT_SHMEM(handle, 0);
  ASSERT_SHMEM(newHandle, 1);

  MutableSharedMemoryHandle assignedHandle;
  assignedHandle = std::move(newHandle);
  ASSERT_SHMEM(newHandle, 0);
  ASSERT_SHMEM(assignedHandle, 1);
}

TEST(IPCSharedMemoryHandle, ToReadOnly)
{
  auto handle = shared_memory::Create(1);
  auto roHandle = std::move(handle).ToReadOnly();
  ASSERT_SHMEM(handle, 0);
  ASSERT_SHMEM(roHandle, 1);
}

TEST(IPCSharedMemoryHandle, Clone)
{
  auto handle = shared_memory::Create(1);
  auto clonedHandle = handle.Clone();
  ASSERT_SHMEM(handle, 1);
  ASSERT_SHMEM(clonedHandle, 1);
}

TEST(IPCSharedMemoryHandle, ROClone)
{
  auto handle = shared_memory::Create(1).ToReadOnly();
  auto clonedHandle = handle.Clone();
  ASSERT_SHMEM(handle, 1);
  ASSERT_SHMEM(clonedHandle, 1);
}

TEST(IPCSharedMemoryHandle, CreateFreezable)
{
  auto handle = shared_memory::CreateFreezable(1);
  ASSERT_SHMEM(handle, 1);
}

TEST(IPCSharedMemoryHandle, WontFreeze)
{
  auto handle = shared_memory::CreateFreezable(1);
  ASSERT_SHMEM(handle, 1);

  auto mHandle = std::move(handle).WontFreeze();
  ASSERT_SHMEM(handle, 0);
  ASSERT_SHMEM(mHandle, 1);
}

TEST(IPCSharedMemoryHandle, Freeze)
{
  auto handle = shared_memory::CreateFreezable(1);
  ASSERT_SHMEM(handle, 1);

  auto roHandle = std::move(handle).Freeze();
  ASSERT_SHMEM(handle, 0);
  ASSERT_SHMEM(roHandle, 1);
}

TEST(IPCSharedMemory, Map)
{
  auto handle = shared_memory::Create(1);

  auto mapping = handle.Map();
  ASSERT_SHMEM(handle, 1);
  ASSERT_SHMEM(mapping, 1);
}

TEST(IPCSharedMemory, ROMap)
{
  auto handle = shared_memory::Create(1).ToReadOnly();

  auto mapping = handle.Map();
  ASSERT_SHMEM(handle, 1);
  ASSERT_SHMEM(mapping, 1);
}

TEST(IPCSharedMemory, FreezeMap)
{
  auto handle = shared_memory::CreateFreezable(1);

  auto mapping = std::move(handle).Map();
  ASSERT_SHMEM(handle, 0);
  ASSERT_SHMEM(mapping, 1);
}

TEST(IPCSharedMemoryMapping, Move)
{
  auto handle = shared_memory::Create(1);

  auto mapping = handle.Map();

  SharedMemoryMapping moved(std::move(mapping));
  ASSERT_SHMEM(mapping, 0);
  ASSERT_SHMEM(moved, 1);

  SharedMemoryMapping moveAssigned;
  moveAssigned = std::move(moved);
  ASSERT_SHMEM(moved, 0);
  ASSERT_SHMEM(moveAssigned, 1);
}

TEST(IPCSharedMemoryMapping, ROMove)
{
  auto handle = shared_memory::Create(1).ToReadOnly();

  auto mapping = handle.Map();

  ReadOnlySharedMemoryMapping moved(std::move(mapping));
  ASSERT_SHMEM(mapping, 0);
  ASSERT_SHMEM(moved, 1);

  ReadOnlySharedMemoryMapping moveAssigned;
  moveAssigned = std::move(moved);
  ASSERT_SHMEM(moved, 0);
  ASSERT_SHMEM(moveAssigned, 1);
}

TEST(IPCSharedMemoryMapping, FreezeMove)
{
  auto handle = shared_memory::CreateFreezable(1);

  auto mapping = std::move(handle).Map();

  FreezableSharedMemoryMapping moved(std::move(mapping));
  ASSERT_SHMEM(mapping, 0);
  ASSERT_SHMEM(moved, 1);

  FreezableSharedMemoryMapping moveAssigned;
  moveAssigned = std::move(moved);
  ASSERT_SHMEM(moved, 0);
  ASSERT_SHMEM(moveAssigned, 1);
}

TEST(IPCSharedMemoryMapping, MutableOrReadOnly)
{
  auto handle = shared_memory::Create(1);
  auto roHandle = handle.Clone().ToReadOnly();

  MutableOrReadOnlySharedMemoryMapping mapping;
  mapping = handle.Map();
  ASSERT_SHMEM(mapping, 1);
  ASSERT_FALSE(mapping.IsReadOnly());

  mapping = roHandle.Map();
  ASSERT_SHMEM(mapping, 1);
  ASSERT_TRUE(mapping.IsReadOnly());
}

TEST(IPCSharedMemoryMapping, FreezableFreeze)
{
  auto handle = shared_memory::CreateFreezable(1);

  auto mapping = std::move(handle).Map();
  auto roHandle = std::move(mapping).Freeze();
  ASSERT_SHMEM(mapping, 0);
  ASSERT_SHMEM(roHandle, 1);
}

TEST(IPCSharedMemoryMapping, FreezableFreezeWithMutableMapping)
{
  auto handle = shared_memory::CreateFreezable(1);

  auto mapping = std::move(handle).Map();
  auto [roHandle, m] = std::move(mapping).FreezeWithMutableMapping();
  ASSERT_SHMEM(mapping, 0);
  ASSERT_SHMEM(roHandle, 1);
  ASSERT_SHMEM(m, 1);
}

TEST(IPCSharedMemoryMapping, FreezableUnmap)
{
  auto handle = shared_memory::CreateFreezable(1);

  auto mapping = std::move(handle).Map();
  handle = std::move(mapping).Unmap();
  ASSERT_SHMEM(handle, 1);
  ASSERT_SHMEM(mapping, 0);
}

// Try to map a frozen shm for writing.  Threat model: the process is
// compromised and then receives a frozen handle.
TEST_F(SharedMemoryInternalTest, FreezeAndMapRW) {
  // Create
  auto handle = shared_memory::CreateFreezable(1);
  ASSERT_TRUE(handle);

  // Initialize
  auto mapping = std::move(handle).Map();
  ASSERT_TRUE(mapping);
  auto* mem = mapping.DataAs<char>();
  ASSERT_TRUE(mem);
  *mem = 'A';

  // Freeze
  auto roHandle = std::move(mapping).Freeze();
  ASSERT_TRUE(roHandle);

  // Bypass the type system
  auto rwHandle = UnsafeThaw(std::move(roHandle));
  ASSERT_TRUE(rwHandle);

  // Fail to write
  auto rwMapping = rwHandle.Map();
  EXPECT_FALSE(rwMapping);

  // But it's still the same handle, for whatever that's worth
  roHandle = std::move(rwHandle).ToReadOnly();
  auto roMapping = roHandle.Map();
  ASSERT_TRUE(roMapping);
  const auto* roMem = roMapping.DataAs<char>();
  ASSERT_TRUE(roMem);
  ASSERT_EQ(*roMem, 'A');
}

// Try to restore write permissions to a frozen mapping.  Threat
// model: the process has mapped frozen shm normally and then is
// compromised, or as for FreezeAndMapRW (see also the
// proof-of-concept at https://crbug.com/project-zero/1671 ).
TEST(IPCSharedMemory, FreezeAndReprotect)
{
  // Create
  auto handle = ipc::shared_memory::CreateFreezable(1);
  ASSERT_TRUE(handle);

  // Initialize
  auto mapping = std::move(handle).Map();
  ASSERT_TRUE(mapping);
  auto* mem = mapping.DataAs<char>();
  ASSERT_TRUE(mem);
  *mem = 'A';

  // Freeze
  auto roHandle = std::move(mapping).Freeze();
  ASSERT_TRUE(roHandle);

  auto roMapping = roHandle.Map();
  ASSERT_TRUE(roMapping);

  const auto* roMem = roMapping.DataAs<char>();
  ASSERT_EQ(*roMem, 'A');

  // Try to alter protection; should fail
  EXPECT_FALSE(ipc::shared_memory::LocalProtect(
      (char*)roMem, 1, ipc::shared_memory::AccessReadWrite));
}

#if !defined(XP_WIN) && !defined(XP_DARWIN)
// This essentially tests whether FreezeAndReprotect would have failed
// without the freeze.
//
// It doesn't work on Windows: VirtualProtect can't exceed the permissions set
// in MapViewOfFile regardless of the security status of the original handle.
//
// It doesn't work on MacOS: we passed the same value as cur_prot and max_prot
// when calling mach_vm_map, so the permissions can't be increased.
TEST(IPCSharedMemory, Reprotect)
{
  // Create
  auto handle = ipc::shared_memory::CreateFreezable(1);
  ASSERT_TRUE(handle);

  // Initialize
  auto mapping = std::move(handle).Map();
  ASSERT_TRUE(mapping);
  auto* mem = mapping.DataAs<char>();
  ASSERT_TRUE(mem);
  *mem = 'A';

  // Unmap without freezing.
  auto rwHandle = std::move(mapping).Unmap().WontFreeze();
  ASSERT_TRUE(rwHandle);
  auto roHandle = std::move(rwHandle).ToReadOnly();
  ASSERT_TRUE(roHandle);

  // Re-map
  auto roMapping = roHandle.Map();
  ASSERT_TRUE(roMapping);
  const auto* cmem = roMapping.DataAs<char>();
  ASSERT_EQ(*cmem, 'A');

  // Try to alter protection; should succeed, because not frozen
  EXPECT_TRUE(ipc::shared_memory::LocalProtect(
      (char*)cmem, 1, ipc::shared_memory::AccessReadWrite));
}
#endif

#ifdef XP_WIN
// Try to regain write permissions on a read-only handle using
// DuplicateHandle; this will succeed if the object has no DACL.
// See also https://crbug.com/338538
TEST(IPCSharedMemory, WinUnfreeze)
{
  // Create
  auto handle = ipc::shared_memory::CreateFreezable(1);
  ASSERT_TRUE(handle);

  // Initialize
  auto mapping = std::move(handle).Map();
  ASSERT_TRUE(mapping);
  auto* mem = mapping.DataAs<char>();
  ASSERT_TRUE(mem);
  *mem = 'A';

  // Freeze
  auto roHandle = std::move(mapping).Freeze();
  ASSERT_TRUE(roHandle);

  // Extract handle.
  auto platformHandle = std::move(roHandle).TakePlatformHandle();

  // Unfreeze.
  HANDLE newHandle = INVALID_HANDLE_VALUE;
  bool unfroze = ::DuplicateHandle(
      GetCurrentProcess(), platformHandle.release(), GetCurrentProcess(),
      &newHandle, FILE_MAP_ALL_ACCESS, false, DUPLICATE_CLOSE_SOURCE);
  ASSERT_FALSE(unfroze);
}
#endif

// Test that a read-only copy sees changes made to the writeable
// mapping in the case that the page wasn't accessed before the copy.
TEST(IPCSharedMemory, ROCopyAndWrite)
{
  auto handle = ipc::shared_memory::CreateFreezable(1);
  ASSERT_TRUE(handle);

  auto [roHandle, rwMapping] =
      std::move(handle).Map().FreezeWithMutableMapping();
  ASSERT_TRUE(rwMapping);
  ASSERT_TRUE(roHandle);

  auto roMapping = roHandle.Map();

  auto* memRW = rwMapping.DataAs<char>();
  ASSERT_TRUE(memRW);
  const auto* memRO = roMapping.DataAs<char>();
  ASSERT_TRUE(memRO);

  ASSERT_NE(memRW, memRO);

  *memRW = 'A';
  EXPECT_EQ(*memRO, 'A');
}

// Test that a read-only copy sees changes made to the writeable
// mapping in the case that the page was accessed before the copy
// (and, before that, sees the state as of when the copy was made).
TEST(IPCSharedMemory, ROCopyAndRewrite)
{
  auto handle = ipc::shared_memory::CreateFreezable(1);
  ASSERT_TRUE(handle);

  auto [roHandle, rwMapping] =
      std::move(handle).Map().FreezeWithMutableMapping();
  ASSERT_TRUE(rwMapping);
  ASSERT_TRUE(roHandle);

  auto roMapping = roHandle.Map();

  auto* memRW = rwMapping.DataAs<char>();
  ASSERT_TRUE(memRW);
  *memRW = 'A';

  const auto* memRO = roMapping.DataAs<char>();
  ASSERT_TRUE(memRO);

  ASSERT_NE(memRW, memRO);

  ASSERT_EQ(*memRW, 'A');
  EXPECT_EQ(*memRO, 'A');
  *memRW = 'X';
  EXPECT_EQ(*memRO, 'X');
}

#ifndef FUZZING
TEST(IPCSharedMemory, BasicIsZero)
{
  static constexpr size_t kSize = 65536;
  auto shm = ipc::shared_memory::Create(kSize).Map();

  auto* mem = shm.DataAs<char>();
  for (size_t i = 0; i < kSize; ++i) {
    ASSERT_EQ(mem[i], 0) << "offset " << i;
  }
}
#endif

#if defined(XP_LINUX) && !defined(ANDROID)
class IPCSharedMemoryLinuxTest : public ::testing::Test {
  int mMajor = 0;
  int mMinor = 0;

 protected:
  void SetUp() override {
    if (mMajor != 0) {
      return;
    }
    struct utsname uts{};
    ASSERT_EQ(uname(&uts), 0) << strerror(errno);
    ASSERT_STREQ(uts.sysname, "Linux");
    ASSERT_EQ(sscanf(uts.release, "%d.%d", &mMajor, &mMinor), 2);
  }

  bool HaveKernelVersion(int aMajor, int aMinor) {
    return mMajor > aMajor || (mMajor == aMajor && mMinor >= aMinor);
  }

  bool ShouldHaveMemfd() { return HaveKernelVersion(3, 17); }

  bool ShouldHaveMemfdNoExec() { return HaveKernelVersion(6, 3); }
};

// Test that memfd_create is used where expected.
//
// More precisely: if memfd_create support is expected, verify that
// shared memory isn't subject to a filesystem size limit.
TEST_F(IPCSharedMemoryLinuxTest, IsMemfd) {
  auto handle = ipc::shared_memory::Create(1);
  UniqueFileHandle fd = std::move(handle).TakePlatformHandle();
  ASSERT_TRUE(fd);

  struct statfs fs{};
  ASSERT_EQ(fstatfs(fd.get(), &fs), 0) << strerror(errno);
  EXPECT_EQ(fs.f_type, TMPFS_MAGIC);
  static constexpr decltype(fs.f_blocks) kNoLimit = 0;
  if (ShouldHaveMemfd()) {
    EXPECT_EQ(fs.f_blocks, kNoLimit);
  } else {
    // On older kernels, we expect the memfd / no-limit test to fail.
    // (In theory it could succeed if backported memfd support exists;
    // if that ever happens, this check can be removed.)
    EXPECT_NE(fs.f_blocks, kNoLimit);
  }
}

TEST_F(IPCSharedMemoryLinuxTest, MemfdNoExec) {
  const bool expectExec = ShouldHaveMemfd() && !ShouldHaveMemfdNoExec();

  auto handle = ipc::shared_memory::Create(1);
  UniqueFileHandle fd = std::move(handle).TakePlatformHandle();
  ASSERT_TRUE(fd);

  struct stat sb{};
  ASSERT_EQ(fstat(fd.get(), &sb), 0) << strerror(errno);
  // Check that mode is reasonable.
  EXPECT_EQ(sb.st_mode & (S_IRUSR | S_IWUSR), mode_t(S_IRUSR | S_IWUSR));
  // Chech the exec bit
  EXPECT_EQ(sb.st_mode & S_IXUSR, mode_t(expectExec ? S_IXUSR : 0));
}
#endif

TEST(IPCSharedMemory, CursorWriteRead)
{
  // Select a chunk size which is at least as big as the allocation granularity,
  // as smaller sizes will not be able to map.
  const size_t chunkSize = ipc::shared_memory::SystemAllocationGranularity();
  ASSERT_TRUE(std::has_single_bit(chunkSize));

  const uint64_t fullSize = chunkSize * 20;
  auto handle = ipc::shared_memory::Create(fullSize);
  ASSERT_TRUE(handle.IsValid());
  ASSERT_EQ(handle.Size(), fullSize);

  // Map the entire region.
  auto mapping = handle.Map();
  ASSERT_TRUE(mapping.IsValid());
  ASSERT_EQ(mapping.Size(), fullSize);

  // Use a cursor to write some data.
  ipc::shared_memory::Cursor cursor(std::move(handle));
  ASSERT_EQ(cursor.Offset(), 0u);
  ASSERT_EQ(cursor.Size(), fullSize);

  // Set the chunk size to ensure we use multiple mappings for this data region.
  cursor.SetChunkSize(chunkSize);

  // Two basic blocks of data which are used for writeReadTest.
  const char data[] = "Hello, World!";
  const char data2[] = "AnotherString";
  auto writeReadTest = [&]() {
    uint64_t initialOffset = cursor.Offset();

    // Clear out the buffer to a known state so that any checks will fail if
    // they're depending on previous writes.
    memset(mapping.Address(), 0xe5, mapping.Size());

    // Write "Hello, World" at the offset, and ensure it is reflected in the
    // full mapping.
    ASSERT_TRUE(cursor.Write(data, std::size(data)));
    ASSERT_EQ(cursor.Offset(), initialOffset + std::size(data));
    ASSERT_STREQ(mapping.DataAs<char>() + initialOffset, data);

    // Write some data in the full mapping at the same offset, and enure it can
    // be read.
    memcpy(mapping.DataAs<char>() + initialOffset, data2, std::size(data2));
    cursor.Seek(initialOffset);
    ASSERT_EQ(cursor.Offset(), initialOffset);
    char buffer[std::size(data2)];
    ASSERT_TRUE(cursor.Read(buffer, std::size(buffer)));
    ASSERT_EQ(cursor.Offset(), initialOffset + std::size(buffer));
    ASSERT_STREQ(buffer, data2);
  };

  writeReadTest();

  // Run the writeReadTest at various offsets within the buffer, including at
  // every chunk boundary, and in the middle of each chunk.
  for (size_t offset = chunkSize - 3; offset < fullSize - 3;
       offset += chunkSize / 2) {
    cursor.Seek(offset);
    writeReadTest();
  }

  // Do a writeReadTest at the very end of the allocated region to ensure that
  // edge case is handled.
  cursor.Seek(mapping.Size() - std::max(std::size(data), std::size(data2)));
  writeReadTest();

  // Ensure that writes past the end fail safely.
  cursor.Seek(mapping.Size() - 3);
  ASSERT_FALSE(cursor.Write(data, std::size(data)));
}

// A baseline for the SizeChangeMsg test below
TEST_F(SharedMemoryInternalTest, PlainRoundTrip) {
  auto handle = shared_memory::Create(65537);
  ASSERT_TRUE(SerializeAndDeserialize(std::move(handle), &handle));
  ASSERT_SHMEM(handle, 65537);
  auto mapping = handle.Map();
  EXPECT_TRUE(mapping.IsValid());
}

// Check that a handle where the stated size is higher than the OS object size
// results in an error at or before mapping time.
TEST_F(SharedMemoryInternalTest, SizeChangeMsg) {
  // Windows and Mach only care about the number of pages, so the change has to
  // roll over a page boundary.  This should be enough.
  static constexpr size_t kRealSize = 65536;
  static constexpr size_t kFakeSize = 65537;

  auto handle = shared_memory::Create(kRealSize);

  // We already know that our Handle class accepts and preserves unaligned
  // sizes, so this is valid.
  UnsafeSetSize(handle, kFakeSize);
  ASSERT_SHMEM(handle, kFakeSize);

#if defined(XP_UNIX) && !defined(XP_DARWIN)
  EXPECT_FALSE(SerializeAndDeserialize(std::move(handle), &handle));
#else  // Win/Mac
  ASSERT_TRUE(SerializeAndDeserialize(std::move(handle), &handle));
  auto mapping = handle.Map();
  EXPECT_FALSE(mapping.IsValid());
#endif
}

// If the claimed size is lower than the real size, that's fine; it doesn't
// break anything, and some OSes round up the size anyway.  It also can't be
// done accidentally though our SharedMemory API, since SetSize is private.
TEST_F(SharedMemoryInternalTest, SizeChangeMsgDown) {
  static constexpr size_t kRealSize = 65536;
  static constexpr size_t kFakeSize = 12288;

  auto handle = shared_memory::Create(kRealSize);
  UnsafeSetSize(handle, kFakeSize);
  ASSERT_SHMEM(handle, kFakeSize);

  ASSERT_TRUE(SerializeAndDeserialize(std::move(handle), &handle));
  auto mapping = handle.Map();
  ASSERT_TRUE(mapping.IsValid());
  EXPECT_EQ(kFakeSize, mapping.Size());
}

}  // namespace mozilla::ipc
