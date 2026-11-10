/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_LAYERS_ISURFACEDEALLOCATOR
#define GFX_LAYERS_ISURFACEDEALLOCATOR

#include <stddef.h>  // for size_t
#include <stdint.h>  // for uint32_t
#include "gfxTypes.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/gfx/Point.h"         // for IntSize
#include "mozilla/ipc/SharedMemory.h"  // for SharedMemory, etc
#include "mozilla/RefPtr.h"
#include "nsIMemoryReporter.h"              // for nsIMemoryReporter
#include "mozilla/Atomics.h"                // for Atomic
#include "mozilla/layers/LayersMessages.h"  // for ShmemSection

namespace mozilla {
namespace ipc {
class Shmem;
class IShmemAllocator;
}  // namespace ipc
namespace gfx {
class DataSourceSurface;
}  // namespace gfx

namespace layers {

class CompositableForwarder;
class CompositorBridgeParentBase;
class TextureForwarder;

class ShmemSectionAllocator;
class LegacySurfaceDescriptorAllocator;
class ClientIPCAllocator;
class HostIPCAllocator;
class LayersIPCChannel;

enum BufferCapabilities {
  DEFAULT_BUFFER_CAPS = 0,
  /**
   * The allocated buffer must be efficiently mappable as a DataSourceSurface.
   */
  MAP_AS_IMAGE_SURFACE = 1 << 0,
  /**
   * The allocated buffer will be used for GL rendering only
   */
  USING_GL_RENDERING_ONLY = 1 << 1
};

class SurfaceDescriptor;

/**
 * An interface used to create and destroy surfaces that are shared with the
 * Compositor process (using shmem, or other platform specific memory)
 *
 * Most of the methods here correspond to methods that are implemented by IPDL
 * actors without a common polymorphic interface.
 * These methods should be only called in the ipdl implementor's thread, unless
 * specified otherwise in the implementing class.
 */
class ISurfaceAllocator {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(ISurfaceAllocator)
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ISurfaceAllocator)

  ISurfaceAllocator() = default;

  // down-casting

  virtual mozilla::ipc::IShmemAllocator* AsShmemAllocator() { return nullptr; }

  virtual ShmemSectionAllocator* AsShmemSectionAllocator() { return nullptr; }

  virtual CompositableForwarder* AsCompositableForwarder() { return nullptr; }

  virtual TextureForwarder* GetTextureForwarder() { return nullptr; }

  virtual ClientIPCAllocator* AsClientAllocator() { return nullptr; }

  virtual HostIPCAllocator* AsHostIPCAllocator() { return nullptr; }

  virtual LegacySurfaceDescriptorAllocator*
  AsLegacySurfaceDescriptorAllocator() {
    return nullptr;
  }

  virtual CompositorBridgeParentBase* AsCompositorBridgeParentBase() {
    return nullptr;
  }

  // ipc info

  virtual bool IPCOpen() const { return true; }

  virtual bool IsSameProcess() const = 0;

  virtual bool UsesImageBridge() const { return false; }

  virtual bool UsesWebRenderBridge() const { return false; }

  virtual dom::ContentParentId GetContentId() { return dom::ContentParentId(); }

 protected:
  void Finalize() {}

  virtual ~ISurfaceAllocator() = default;
};

/// Methods that are specific to the client/child side.
class ClientIPCAllocator : public ISurfaceAllocator {
 public:
  ClientIPCAllocator() = default;

  ClientIPCAllocator* AsClientAllocator() override { return this; }

  virtual base::ProcessId GetParentPid() const = 0;

  virtual MessageLoop* GetMessageLoop() const = 0;

  virtual void CancelWaitForNotifyNotUsed(uint64_t aTextureId) = 0;
};

/// Methods that are specific to the host/parent side.
class HostIPCAllocator : public ISurfaceAllocator {
 public:
  HostIPCAllocator() = default;

  HostIPCAllocator* AsHostIPCAllocator() override { return this; }

  /**
   * Get child side's process Id.
   */
  virtual base::ProcessId GetChildProcessId() = 0;

  virtual void NotifyNotUsed(PTextureParent* aTexture,
                             uint64_t aTransactionId) = 0;

  virtual void SendAsyncMessage(
      const nsTArray<AsyncParentMessageData>& aMessage) = 0;

  virtual void SendPendingAsyncMessages();

  virtual void SetAboutToSendAsyncMessages() {
    mAboutToSendAsyncMessages = true;
  }

  bool IsAboutToSendAsyncMessages() { return mAboutToSendAsyncMessages; }
  void ClearAboutToSendAsyncMessages() { mAboutToSendAsyncMessages = false; }

 protected:
  std::vector<AsyncParentMessageData> mPendingAsyncMessage;
  bool mAboutToSendAsyncMessages = false;
};

class ShmemSection {
 public:
  static Maybe<ShmemSection> FromUntrusted(
      const UntrustedShmemSection& aUntrusted, size_t aMinSize);
  bool Init(const mozilla::ipc::Shmem& aShm, uint32_t offset, uint32_t size);
  UntrustedShmemSection AsUntrusted();

  uint32_t size() const { return mSize; }
  uint32_t offset() const { return mOffset; }
  const mozilla::ipc::Shmem& shmem() { return mShmem; }

 private:
  mozilla::ipc::Shmem mShmem;
  uint32_t mOffset;
  uint32_t mSize;
};

/// An allocator that can group allocations in bigger chunks of shared memory.
///
/// The allocated shmem sections can only be deallocated by the same allocator
/// instance (and only in the child process).
class ShmemSectionAllocator {
 public:
  virtual bool AllocShmemSection(uint32_t aSize,
                                 ShmemSection* aShmemSection) = 0;

  virtual void DeallocShmemSection(ShmemSection& aShmemSection) = 0;

  virtual void MemoryPressure() {}
};

/// Some old stuff that's still around and used for screenshots.
///
/// New code should not need this (see TextureClient).
class LegacySurfaceDescriptorAllocator {
 public:
  virtual bool AllocSurfaceDescriptor(const gfx::IntSize& aSize,
                                      gfxContentType aContent,
                                      SurfaceDescriptor* aBuffer) = 0;

  virtual bool AllocSurfaceDescriptorWithCaps(const gfx::IntSize& aSize,
                                              gfxContentType aContent,
                                              uint32_t aCaps,
                                              SurfaceDescriptor* aBuffer) = 0;

  virtual void DestroySurfaceDescriptor(SurfaceDescriptor* aSurface) = 0;
};

bool IsSurfaceDescriptorValid(const SurfaceDescriptor& aSurface);

class GfxMemoryImageReporter final : public nsIMemoryReporter {
  ~GfxMemoryImageReporter() = default;

 public:
  NS_DECL_ISUPPORTS

  GfxMemoryImageReporter() {
#ifdef DEBUG
    // There must be only one instance of this class, due to |sAmount|
    // being static.
    static bool hasRun = false;
    MOZ_ASSERT(!hasRun);
    hasRun = true;
#endif
  }

  MOZ_DEFINE_MALLOC_SIZE_OF_ON_ALLOC(MallocSizeOfOnAlloc)
  MOZ_DEFINE_MALLOC_SIZE_OF_ON_FREE(MallocSizeOfOnFree)

  static void DidAlloc(void* aPointer) {
    sAmount += MallocSizeOfOnAlloc(aPointer);
  }

  static void WillFree(void* aPointer) {
    sAmount -= MallocSizeOfOnFree(aPointer);
  }

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    MOZ_COLLECT_REPORT(
        "explicit/gfx/heap-textures", KIND_HEAP, UNITS_BYTES, sAmount,
        "Heap memory shared between threads by texture clients and hosts.");

    return NS_OK;
  }

 private:
  // Typically we use |size_t| in memory reporters, but in the past this
  // variable has sometimes gone negative due to missing DidAlloc() calls.
  // Therefore, we use a signed type so that any such negative values show up
  // as negative in about:memory, rather than as enormous positive numbers.
  static mozilla::Atomic<ptrdiff_t> sAmount;
};

/// A simple shmem section allocator that can only allocate small
/// fixed size elements (only intended to be used to store tile
/// copy-on-write locks for now).
class FixedSizeSmallShmemSectionAllocator final : public ShmemSectionAllocator {
 public:
  enum AllocationStatus { STATUS_ALLOCATED, STATUS_FREED };

  struct ShmemSectionHeapHeader {
    Atomic<uint32_t> mTotalBlocks;
    Atomic<uint32_t> mAllocatedBlocks;
  };

  struct ShmemSectionHeapAllocation {
    Atomic<uint32_t> mStatus;
    uint32_t mSize;
  };

  explicit FixedSizeSmallShmemSectionAllocator(LayersIPCChannel* aShmProvider);

  ~FixedSizeSmallShmemSectionAllocator();

  bool AllocShmemSection(uint32_t aSize, ShmemSection* aShmemSection) override;

  void DeallocShmemSection(ShmemSection& aShmemSection) override;

  void MemoryPressure() override { ShrinkShmemSectionHeap(); }

  // can be called on the compositor process.
  static void FreeShmemSection(ShmemSection& aShmemSection);

  void ShrinkShmemSectionHeap();

  bool IPCOpen() const;

 protected:
  std::vector<mozilla::ipc::Shmem> mUsedShmems;
  LayersIPCChannel* mShmProvider;
};

}  // namespace layers
}  // namespace mozilla

#endif
