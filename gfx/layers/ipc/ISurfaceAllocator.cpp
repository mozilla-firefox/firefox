/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ISurfaceAllocator.h"

#include "mozilla/layers/CompositableForwarder.h"
#include "mozilla/layers/ImageBridgeParent.h"  // for ImageBridgeParent
#include "mozilla/layers/TextureForwarder.h"
#include "mozilla/layers/TextureHost.h"  // for TextureHost

namespace mozilla {
namespace layers {

NS_IMPL_ISUPPORTS(GfxMemoryImageReporter, nsIMemoryReporter)

mozilla::Atomic<ptrdiff_t> GfxMemoryImageReporter::sAmount(0);

RefPtr<TextureForwarder> ISurfaceAllocator::GetTextureForwarder() {
  return nullptr;
}

void HostIPCAllocator::SendPendingAsyncMessages() {
  if (mPendingAsyncMessage.IsEmpty()) {
    return;
  }

  // Some type of AsyncParentMessageData message could have
  // one file descriptor (e.g. OpDeliverFence).
  // A number of file descriptors per gecko ipc message have a limitation
  // on XP_UNIX (MACOSX or LINUX).
  static const uint32_t kMaxMessageNumber =
      IPC::Message::MAX_DESCRIPTORS_PER_MESSAGE;

  nsTArray<AsyncParentMessageData> messages = std::move(mPendingAsyncMessage);
  // Limit maximum number of messages.
  Span<const AsyncParentMessageData> span(messages);
  while (!span.IsEmpty()) {
    auto [chunk, rest] =
        span.SplitAt(std::min<size_t>(kMaxMessageNumber, span.Length()));
    SendAsyncMessage(chunk);
    span = rest;
  }
}

// XXX - We should actually figure out the minimum shmem allocation size on
// a certain platform and use that.
const uint32_t sShmemPageSize = 4096;

const uint32_t sSupportedBlockSize = 4;

FixedSizeSmallShmemSectionAllocator::FixedSizeSmallShmemSectionAllocator(
    LayersIPCChannel* aShmProvider)
    : mShmProvider(aShmProvider) {
  MOZ_RELEASE_ASSERT(mShmProvider);
}

FixedSizeSmallShmemSectionAllocator::~FixedSizeSmallShmemSectionAllocator() {
  ShrinkShmemSectionHeap();
}

bool FixedSizeSmallShmemSectionAllocator::IPCOpen() const {
  return mShmProvider->IPCOpen();
}

bool FixedSizeSmallShmemSectionAllocator::AllocShmemSection(
    uint32_t aSize, ShmemSection* aShmemSection) {
  // For now we only support sizes of 4. If we want to support different sizes
  // some more complicated bookkeeping should be added.
  NS_ASSERT_OWNINGTHREAD(FixedSizeSmallShmemSectionAllocator);
  MOZ_RELEASE_ASSERT(aSize == sSupportedBlockSize);
  MOZ_RELEASE_ASSERT(aShmemSection);

  if (!IPCOpen()) {
    gfxCriticalError() << "Attempt to allocate a ShmemSection after shutdown.";
    return false;
  }

  uint32_t allocationSize = (aSize + sizeof(ShmemSectionHeapAllocation));
  // The heap header lives in shared memory and can be modified by the other
  // process, so its values must be bounded before being used as offsets.
  const uint32_t maxBlocks =
      (sShmemPageSize - sizeof(ShmemSectionHeapHeader) - 1) / allocationSize;

  ipc::Shmem shmem;

  for (size_t i = 0; i < mUsedShmems.size(); i++) {
    ShmemSectionHeapHeader* header =
        mUsedShmems[i].get<ShmemSectionHeapHeader>();
    if (header->mAllocatedBlocks < maxBlocks) {
      shmem = mUsedShmems[i];
      MOZ_RELEASE_ASSERT(mUsedShmems[i].IsWritable());
      break;
    }
  }

  if (!shmem.IsWritable()) {
    ipc::Shmem tmp;
    if (!mShmProvider->AllocUnsafeShmem(sShmemPageSize, &tmp)) {
      return false;
    }

    ShmemSectionHeapHeader* header = tmp.get<ShmemSectionHeapHeader>();
    header->mTotalBlocks = 0;
    header->mAllocatedBlocks = 0;

    mUsedShmems.push_back(tmp);
    shmem = tmp;
  }

  MOZ_RELEASE_ASSERT(shmem.IsWritable());

  ShmemSectionHeapHeader* header = shmem.get<ShmemSectionHeapHeader>();
  uint8_t* heap = shmem.get<uint8_t>() + sizeof(ShmemSectionHeapHeader);

  const uint32_t totalBlocks = header->mTotalBlocks;
  if (totalBlocks > maxBlocks) {
    gfxCriticalNote << "Invalid ShmemSection heap header";
    return false;
  }

  ShmemSectionHeapAllocation* allocHeader = nullptr;

  if (totalBlocks > header->mAllocatedBlocks) {
    // Search for the first available block.
    for (size_t i = 0; i < totalBlocks; i++) {
      auto* block = reinterpret_cast<ShmemSectionHeapAllocation*>(heap);
      if (block->mStatus == STATUS_FREED) {
        allocHeader = block;
        break;
      }
      heap += allocationSize;
    }
    if (!allocHeader) {
      gfxCriticalNote << "Invalid ShmemSection heap header";
      return false;
    }
    MOZ_RELEASE_ASSERT(allocHeader->mSize == sSupportedBlockSize);
  } else {
    if (totalBlocks == maxBlocks) {
      gfxCriticalNote << "Invalid ShmemSection heap header";
      return false;
    }
    heap += totalBlocks * allocationSize;

    header->mTotalBlocks = totalBlocks + 1;
    allocHeader = reinterpret_cast<ShmemSectionHeapAllocation*>(heap);
    allocHeader->mSize = aSize;
  }

  MOZ_RELEASE_ASSERT(allocHeader);
  header->mAllocatedBlocks++;
  allocHeader->mStatus = STATUS_ALLOCATED;

  size_t offset =
      (heap + sizeof(ShmemSectionHeapAllocation)) - shmem.get<uint8_t>();
  if (offset > (size_t)std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  if (!aShmemSection->Init(shmem, offset, aSize)) {
    return false;
  }

  ShrinkShmemSectionHeap();
  return true;
}

void FixedSizeSmallShmemSectionAllocator::FreeShmemSection(
    mozilla::layers::ShmemSection& aShmemSection) {
  MOZ_RELEASE_ASSERT(aShmemSection.size() == sSupportedBlockSize);
  MOZ_RELEASE_ASSERT(aShmemSection.offset() <
                     sShmemPageSize - sSupportedBlockSize);

  if (!aShmemSection.shmem().IsWritable()) {
    return;
  }

  size_t headerSize = sizeof(ShmemSectionHeapAllocation);
  MOZ_RELEASE_ASSERT(aShmemSection.offset() >= headerSize);

  ShmemSectionHeapAllocation* allocHeader =
      reinterpret_cast<ShmemSectionHeapAllocation*>(
          aShmemSection.shmem().get<char>() + aShmemSection.offset() -
          headerSize);

  MOZ_RELEASE_ASSERT(allocHeader->mSize == aShmemSection.size());

  bool success =
      allocHeader->mStatus.compareExchange(STATUS_ALLOCATED, STATUS_FREED);
  // If this fails something really weird is going on.
  MOZ_RELEASE_ASSERT(success);

  ShmemSectionHeapHeader* header =
      aShmemSection.shmem().get<ShmemSectionHeapHeader>();
  header->mAllocatedBlocks--;
}

void FixedSizeSmallShmemSectionAllocator::DeallocShmemSection(
    mozilla::layers::ShmemSection& aShmemSection) {
  NS_ASSERT_OWNINGTHREAD(FixedSizeSmallShmemSectionAllocator);

  if (!IPCOpen()) {
    gfxCriticalNote << "Attempt to dealloc a ShmemSections after shutdown.";
    return;
  }

  FreeShmemSection(aShmemSection);
  ShrinkShmemSectionHeap();
}

void FixedSizeSmallShmemSectionAllocator::ShrinkShmemSectionHeap() {
  NS_ASSERT_OWNINGTHREAD(FixedSizeSmallShmemSectionAllocator);

  if (!IPCOpen()) {
    mUsedShmems.clear();
    return;
  }

  // The loop will terminate as we either increase i, or decrease size
  // every time through.
  size_t i = 0;
  while (i < mUsedShmems.size()) {
    ShmemSectionHeapHeader* header =
        mUsedShmems[i].get<ShmemSectionHeapHeader>();
    if (header->mAllocatedBlocks == 0) {
      mShmProvider->DeallocShmem(mUsedShmems[i]);
      // We don't particularly care about order, move the last one in the array
      // to this position.
      if (i < mUsedShmems.size() - 1) {
        mUsedShmems[i] = mUsedShmems[mUsedShmems.size() - 1];
      }
      mUsedShmems.pop_back();
    } else {
      i++;
    }
  }
}

Maybe<ShmemSection> ShmemSection::FromUntrusted(
    const UntrustedShmemSection& aUntrusted, size_t aMinimumSize) {
  if (aUntrusted.size() < aMinimumSize) {
    return Nothing();
  }

  ShmemSection section;
  if (!section.Init(aUntrusted.shmem(), aUntrusted.offset(),
                    aUntrusted.size())) {
    return Nothing();
  }

  return Some(section);
}

bool ShmemSection::Init(const mozilla::ipc::Shmem& aShmem, uint32_t aOffset,
                        uint32_t aSize) {
  if (!aShmem.IsReadable()) {
    return false;
  }

  size_t shmSize = aShmem.Size<uint8_t>();
  CheckedInt<size_t> end = CheckedInt<size_t>(aOffset) + aSize;

  if (!end.isValid() || end.value() > shmSize) {
    return false;
  }

  mShmem = aShmem;
  mOffset = aOffset;
  mSize = aSize;

  return true;
}

}  // namespace layers
}  // namespace mozilla
