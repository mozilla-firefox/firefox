/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* This source code was derived from Chromium code, and as such is also subject
 * to the [Chromium license](ipc/chromium/src/LICENSE). */

#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/ipc/SharedMemoryMapping.h"
#include "SharedMemoryPlatform.h"

#include "chrome/common/ipc_message_utils.h"
#include "mozilla/Atomics.h"
#include "nsDebug.h"
#include "nsIMemoryReporter.h"

namespace mozilla::ipc::shared_memory {

// Implementation of the shared memory logger in SharedMemoryPlatform.h.
LazyLogModule gSharedMemoryLog{"SharedMemory"};

class AllocationReporter final : public nsIMemoryReporter {
  ~AllocationReporter() = default;

 public:
  static Atomic<uint64_t> allocated;

  NS_DECL_THREADSAFE_ISUPPORTS

  NS_IMETHOD
  CollectReports(nsIHandleReportCallback* aHandleReport, nsISupports* aData,
                 bool aAnonymize) override {
    MOZ_COLLECT_REPORT(
        "shmem-allocated", KIND_OTHER, UNITS_BYTES, allocated,
        "Memory shared with other processes that is accessible (but not "
        "necessarily mapped).");

    return NS_OK;
  }
};

Atomic<uint64_t> AllocationReporter::allocated;

NS_IMPL_ISUPPORTS(AllocationReporter, nsIMemoryReporter)

static void RegisterAllocationMemoryReporter() {
  static Atomic<bool> registered;
  if (registered.compareExchange(false, true)) {
    RegisterStrongMemoryReporter(MakeAndAddRef<AllocationReporter>());
  }
}

HandleBase::HandleBase() = default;

HandleBase::~HandleBase() {
  if (mSize > 0) {
    MOZ_ASSERT(AllocationReporter::allocated >= mSize,
               "Can't destroy more than allocated");
    SetSize(0);
  }
  mHandle = nullptr;
}

HandleBase& HandleBase::operator=(HandleBase&& aOther) {
  mHandle = std::move(aOther.mHandle);
  SetSize(std::exchange(aOther.mSize, 0));
  return *this;
}

HandleBase HandleBase::Clone() const {
  HandleBase hb;
  hb.mHandle = Platform::CloneHandle(mHandle);
  if (hb.mHandle) {
    // TODO more intelligently handle clones to not count as additional
    // allocations?
    hb.SetSize(mSize);
  }
  return hb;
}

void HandleBase::ToMessageWriter(IPC::MessageWriter* aWriter) && {
  WriteParam(aWriter, std::move(mHandle));
  WriteParam(aWriter, mSize);
  SetSize(0);
}

bool HandleBase::FromMessageReader(IPC::MessageReader* aReader) {
  mozilla::ipc::shared_memory::PlatformHandle handle;
  if (!ReadParam(aReader, &handle)) {
    aReader->FatalError("Failed to read shared memory PlatformHandle");
    return false;
  }
  if (handle && !Platform::IsSafeToMap(handle)) {
    aReader->FatalError("Shared memory PlatformHandle is not safe to map");
    return false;
  }
  uint64_t size = 0;
  if (!ReadParam(aReader, &size)) {
    aReader->FatalError("Failed to read shared memory handle size");
    return false;
  }
  if (handle && !size) {
    aReader->FatalError(
        "Unexpected PlatformHandle for zero-sized shared memory handle");
    return false;
  }
  mHandle = std::move(handle);
  SetSize(size);
  return true;
}

void HandleBase::SetSize(uint64_t aSize) {
  RegisterAllocationMemoryReporter();
  mozilla::ipc::shared_memory::AllocationReporter::allocated -= mSize;
  mSize = aSize;
  mozilla::ipc::shared_memory::AllocationReporter::allocated += mSize;
}

MutableMapping MutableHandle::Map(void* aFixedAddress) const {
  return MutableMapping(*this, aFixedAddress);
}

MutableMapping MutableHandle::MapSubregion(uint64_t aOffset, size_t aSize,
                                           void* aFixedAddress) const {
  return MutableMapping(*this, aOffset, aSize, aFixedAddress);
}

MutableMappingWithHandle MutableHandle::MapWithHandle(void* aFixedAddress) && {
  return MutableMappingWithHandle(std::move(*this), aFixedAddress);
}

ReadOnlyHandle MutableHandle::ToReadOnly() && {
  return std::move(*this).ConvertTo<Type::ReadOnly>();
}

const ReadOnlyHandle& MutableHandle::AsReadOnly() const {
  static_assert(sizeof(ReadOnlyHandle) == sizeof(MutableHandle));
  return reinterpret_cast<const ReadOnlyHandle&>(*this);
}

ReadOnlyMapping ReadOnlyHandle::Map(void* aFixedAddress) const {
  return ReadOnlyMapping(*this, aFixedAddress);
}

ReadOnlyMapping ReadOnlyHandle::MapSubregion(uint64_t aOffset, size_t aSize,
                                             void* aFixedAddress) const {
  return ReadOnlyMapping(*this, aOffset, aSize, aFixedAddress);
}

ReadOnlyMappingWithHandle ReadOnlyHandle::MapWithHandle(
    void* aFixedAddress) && {
  return ReadOnlyMappingWithHandle(std::move(*this), aFixedAddress);
}

FreezableHandle::~Handle() {
  NS_WARNING_ASSERTION(!IsValid(), "freezable shared memory was never frozen");
}

MutableHandle FreezableHandle::WontFreeze() && {
  return std::move(*this).ConvertTo<Type::Mutable>();
}

ReadOnlyHandle FreezableHandle::Freeze() && {
  DebugOnly<const uint64_t> previous_size = Size();
  if (Platform::Freeze(*this)) {
    MOZ_ASSERT(Size() == previous_size);
    return std::move(*this).ConvertTo<Type::ReadOnly>();
  }
  return nullptr;
}

FreezableMapping FreezableHandle::Map(void* aFixedAddress) && {
  return FreezableMapping(std::move(*this), aFixedAddress);
}

FreezableMapping FreezableHandle::MapSubregion(uint64_t aOffset, size_t aSize,
                                               void* aFixedAddress) && {
  return FreezableMapping(std::move(*this), aOffset, aSize, aFixedAddress);
}

MutableHandle Create(uint64_t aSize) {
  MutableHandle h;
  const auto success = Platform::Create(h, aSize);
  MOZ_ASSERT(success == h.IsValid());
  if (success) {
    MOZ_ASSERT(aSize == h.Size());
  }
  return h;
}

FreezableHandle CreateFreezable(uint64_t aSize) {
  FreezableHandle h;
  const auto success = Platform::CreateFreezable(h, aSize);
  MOZ_ASSERT(success == h.IsValid());
  if (success) {
    MOZ_ASSERT(aSize == h.Size());
  }
  return h;
}

}  // namespace mozilla::ipc::shared_memory

namespace IPC {

void ParamTraits<mozilla::ipc::shared_memory::MutableHandle>::Write(
    MessageWriter* aWriter,
    mozilla::ipc::shared_memory::MutableHandle&& aParam) {
  std::move(aParam).ToMessageWriter(aWriter);
}

bool ParamTraits<mozilla::ipc::shared_memory::MutableHandle>::Read(
    MessageReader* aReader,
    mozilla::ipc::shared_memory::MutableHandle* aResult) {
  return aResult->FromMessageReader(aReader);
}

void ParamTraits<mozilla::ipc::shared_memory::ReadOnlyHandle>::Write(
    MessageWriter* aWriter,
    mozilla::ipc::shared_memory::ReadOnlyHandle&& aParam) {
  std::move(aParam).ToMessageWriter(aWriter);
}

bool ParamTraits<mozilla::ipc::shared_memory::ReadOnlyHandle>::Read(
    MessageReader* aReader,
    mozilla::ipc::shared_memory::ReadOnlyHandle* aResult) {
  return aResult->FromMessageReader(aReader);
}

}  // namespace IPC
