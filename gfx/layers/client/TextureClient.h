/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_TEXTURECLIENT_H
#define MOZILLA_GFX_TEXTURECLIENT_H

#include <stddef.h>  // for size_t
#include <stdint.h>  // for uint32_t, uint8_t, uint64_t

#include "GLTextureImage.h"  // for TextureImage
#include "GfxTexturesReporter.h"
#include "ImageTypes.h"          // for StereoMode
#include "mozilla/Assertions.h"  // for MOZ_ASSERT, etc
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"  // for override
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"  // for RefPtr, RefCounted
#include "mozilla/UniquePtr.h"
#include "mozilla/UniquePtrExtensions.h"  // for UniqueFileHandle
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/gfx/2D.h"  // for DrawTarget
#include "mozilla/gfx/CriticalSection.h"
#include "mozilla/gfx/Point.h"  // for IntSize
#include "mozilla/gfx/Types.h"  // for SurfaceFormat
#include "mozilla/ipc/Shmem.h"  // for Shmem
#include "mozilla/layers/AtomicRefCountedWithFinalize.h"
#include "mozilla/layers/CompositorTypes.h"  // for TextureFlags, etc
#include "mozilla/layers/ISurfaceAllocator.h"
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/PTextureChild.h"  // for PTextureChild
#include "mozilla/layers/SyncObject.h"
#include "mozilla/mozalloc.h"  // for operator delete
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsCOMPtr.h"         // for already_AddRefed
#include "nsISupportsImpl.h"  // for TextureImage::AddRef, etc
#include "nsThreadUtils.h"
#include "pratom.h"

class gfxImageSurface;
struct ID3D11Device;

namespace mozilla {

namespace layers {

class AndroidHardwareBufferTextureData;
class BufferTextureData;
class CompositableForwarder;
class FwdTransactionTracker;
class KnowsCompositor;
class LayersIPCChannel;
class CompositableClient;
struct PlanarYCbCrData;
class Image;
class PTextureChild;
class TextureChild;
class TextureData;
class ShmemHolder;
class GPUVideoTextureData;
class TextureClient;
class ITextureClientRecycleAllocator;
class SharedSurfaceTextureData;
class TextureForwarder;
class ImageBridgeChild;
class RecordedTextureData;
struct RemoteTextureOwnerId;

/**
 * TextureClient is the abstraction that allows us to share data between the
 * content and the compositor side.
 */

enum TextureAllocationFlags {
  ALLOC_DEFAULT = 0,
  ALLOC_CLEAR_BUFFER =
      1 << 1,  // Clear the buffer to whatever is best for the draw target
  ALLOC_CLEAR_BUFFER_WHITE = 1 << 2,  // explicit all white
  ALLOC_CLEAR_BUFFER_BLACK = 1 << 3,  // explicit all black
  ALLOC_DISALLOW_BUFFERTEXTURECLIENT = 1 << 4,

  // Allocate the texture for out-of-band content updates. This is mostly for
  // TextureClientD3D11, which may otherwise choose D3D10 or non-KeyedMutex
  // surfaces when used on the main thread.
  ALLOC_FOR_OUT_OF_BAND_CONTENT = 1 << 5,

  // Disable any cross-device synchronization. This is also for
  // TextureClientD3D11, and creates a texture without KeyedMutex.
  ALLOC_MANUAL_SYNCHRONIZATION = 1 << 6,

  // The texture is going to be updated using UpdateFromSurface and needs to
  // support that call.
  ALLOC_UPDATE_FROM_SURFACE = 1 << 7,

  // Do not use an accelerated texture type.
  ALLOC_DO_NOT_ACCELERATE = 1 << 8,

  // Force allocation of remote/recorded texture, or fail if not possible.
  ALLOC_FORCE_REMOTE = 1 << 9,

  // Prefer to use keyed mutex than D3D11Fence on Windows
  USE_D3D11_KEYED_MUTEX = 1 << 10,
};

enum class BackendSelector { Content, Canvas };

/// Temporary object providing direct access to a Texture's memory.
///
/// see TextureClient::CanExposeMappedData() and
/// TextureClient::BorrowMappedData().
struct MappedTextureData {
  uint8_t* data;
  gfx::IntSize size;
  int32_t stride;
  gfx::SurfaceFormat format;
};

struct MappedYCbCrChannelData {
  uint8_t* data;
  gfx::IntSize size;
  int32_t stride;
  int32_t skip;
  uint32_t bytesPerPixel;

  bool CopyInto(MappedYCbCrChannelData& aDst);
};

struct MappedYCbCrTextureData {
  MappedYCbCrChannelData y;
  MappedYCbCrChannelData cb;
  MappedYCbCrChannelData cr;
  // Sad but because of how SharedPlanarYCbCrData is used we have to expose this
  // for now.
  uint8_t* metadata;
  StereoMode stereoMode;

  bool CopyInto(MappedYCbCrTextureData& aDst) {
    return y.CopyInto(aDst.y) && cb.CopyInto(aDst.cb) && cr.CopyInto(aDst.cr);
  }
};

class ReadLockDescriptor;
class NonBlockingTextureReadLock;

// A class to help implement copy-on-write semantics for shared textures.
//
// A TextureClient/Host pair can opt into using a ReadLock by calling
// TextureClient::EnableReadLock. This will equip the TextureClient with a
// ReadLock object that will be automatically ReadLock()'ed by the texture
// itself when it is written into (see TextureClient::Unlock). A
// TextureReadLock's counter starts at 1 and is expected to be equal to 1 when
// the lock is destroyed. See ShmemTextureReadLock for explanations about why we
// use 1 instead of 0 as the initial state. TextureReadLock is mostly internally
// managed by the TextureClient/Host pair, and the compositable only has to
// forward it during updates. If an update message contains a null_t lock, it
// means that the texture was not written into on the content side, and there is
// no synchronization required on the compositor side (or it means that the
// texture pair did not opt into using ReadLocks). On the compositor side, the
// TextureHost can receive a ReadLock during a transaction, and will both
// ReadUnlock() it and drop it as soon as the shared data is available again for
// writing (the texture upload is done, or the compositor not reading the
// texture anymore). The lock is dropped to make sure it is ReadUnlock()'ed only
// once.
class TextureReadLock {
 protected:
  virtual ~TextureReadLock() = default;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TextureReadLock)

  virtual bool ReadLock() = 0;
  virtual bool TryReadLock(TimeDuration aTimeout) { return ReadLock(); }
  virtual int32_t ReadUnlock() = 0;
  virtual bool IsValid() const = 0;

  static already_AddRefed<TextureReadLock> Deserialize(
      ReadLockDescriptor&& aDescriptor, ISurfaceAllocator* aAllocator);

  virtual bool Serialize(ReadLockDescriptor& aOutput,
                         base::ProcessId aOther) = 0;

  enum LockType {
    TYPE_NONBLOCKING_MEMORY,
    TYPE_NONBLOCKING_SHMEM,
    TYPE_CROSS_PROCESS_SEMAPHORE
  };
  virtual LockType GetType() = 0;

  virtual NonBlockingTextureReadLock* AsNonBlockingLock() { return nullptr; }
};

class NonBlockingTextureReadLock : public TextureReadLock {
 public:
  virtual int32_t GetReadCount() = 0;

  static already_AddRefed<TextureReadLock> Create(LayersIPCChannel* aAllocator);

  NonBlockingTextureReadLock* AsNonBlockingLock() override { return this; }
};

#ifdef XP_WIN
class D3D11TextureData;
class DXGIYCbCrTextureData;
#endif

class TextureData {
 public:
  struct Info {
    gfx::IntSize size;
    gfx::SurfaceFormat format;
    bool hasSynchronization;
    bool supportsMoz2D;
    bool canExposeMappedData;
    bool canConcurrentlyReadLock;

    Info()
        : format(gfx::SurfaceFormat::UNKNOWN),
          hasSynchronization(false),
          supportsMoz2D(false),
          canExposeMappedData(false),
          canConcurrentlyReadLock(true) {}
  };

  static TextureData* Create(
      TextureType aTextureType, gfx::SurfaceFormat aFormat,
      const gfx::IntSize& aSize, TextureAllocationFlags aAllocFlags,
      gfx::BackendType aBackendType = gfx::BackendType::NONE);
  static TextureData* Create(TextureForwarder* aAllocator,
                             gfx::SurfaceFormat aFormat, gfx::IntSize aSize,
                             KnowsCompositor* aKnowsCompositor,
                             BackendSelector aSelector,
                             TextureFlags aTextureFlags,
                             TextureAllocationFlags aAllocFlags);

  static bool IsRemote(KnowsCompositor* aKnowsCompositor,
                       BackendSelector aSelector,
                       gfx::SurfaceFormat aFormat = gfx::SurfaceFormat::UNKNOWN,
                       gfx::IntSize aSize = gfx::IntSize(1, 1));

  MOZ_COUNTED_DTOR_VIRTUAL(TextureData)

  virtual TextureType GetTextureType() const { return TextureType::Last; }

  virtual void FillInfo(TextureData::Info& aInfo) const = 0;

  virtual void InvalidateContents() {}

  virtual bool Lock(OpenMode aMode) = 0;

  virtual void Unlock() = 0;

  virtual already_AddRefed<gfx::DrawTarget> BorrowDrawTarget() {
    return nullptr;
  }

  virtual void ReturnDrawTarget(already_AddRefed<gfx::DrawTarget> aDT);

  /**
   * When the TextureData is not being Unlocked, this can be used to inform a
   * TextureData that drawing has finished until the next BorrowDrawTarget.
   */
  virtual void EndDraw() {}

  virtual already_AddRefed<gfx::SourceSurface> BorrowSnapshot() {
    return nullptr;
  }

  virtual void ReturnSnapshot(already_AddRefed<gfx::SourceSurface> aSnapshot);

  virtual bool BorrowMappedData(MappedTextureData&) { return false; }

  virtual bool BorrowMappedYCbCrData(MappedYCbCrTextureData&) { return false; }

  virtual void Deallocate(LayersIPCChannel* aAllocator) = 0;

  /// Depending on the texture's flags either Deallocate or Forget is called.
  virtual void Forget(LayersIPCChannel* aAllocator) {}

  virtual bool Serialize(SurfaceDescriptor& aDescriptor) = 0;
  virtual RemoteDecoderVideoType GetRemoteDecoderVideoType() {
    return RemoteDecoderVideoType::TypeNone;
  }

  virtual void OnForwardedToHost() {}

  virtual TextureData* CreateSimilar(
      LayersIPCChannel* aAllocator, LayersBackend aLayersBackend,
      TextureFlags aFlags = TextureFlags::DEFAULT,
      TextureAllocationFlags aAllocFlags = ALLOC_DEFAULT) const {
    return nullptr;
  }

  virtual bool UpdateFromSurface(gfx::SourceSurface* aSurface) {
    return false;
  };

  virtual void SyncWithObject(RefPtr<SyncObjectClient> aSyncObject) {};

  virtual TextureFlags GetTextureFlags() const {
    return TextureFlags::NO_FLAGS;
  }

#ifdef XP_WIN
  virtual D3D11TextureData* AsD3D11TextureData() { return nullptr; }
  virtual DXGIYCbCrTextureData* AsDXGIYCbCrTextureData() { return nullptr; }
#endif

  virtual BufferTextureData* AsBufferTextureData() { return nullptr; }

  virtual GPUVideoTextureData* AsGPUVideoTextureData() { return nullptr; }

  virtual AndroidHardwareBufferTextureData*
  AsAndroidHardwareBufferTextureData() {
    return nullptr;
  }

  virtual RecordedTextureData* AsRecordedTextureData() { return nullptr; }

  // It is used by AndroidHardwareBufferTextureData and
  // SharedSurfaceTextureData. Returns buffer id when it owns
  // AndroidHardwareBuffer. It is used only on android.
  virtual Maybe<uint64_t> GetBufferId() const { return Nothing(); }

  // The acquire fence is a fence that is used for waiting until rendering to
  // its AHardwareBuffer is completed.
  // It is used only on android.
  virtual UniqueFileHandle GetAcquireFence() { return UniqueFileHandle(); }

  virtual bool RequiresRefresh() const { return false; }

  virtual already_AddRefed<FwdTransactionTracker> UseCompositableForwarder(
      CompositableForwarder* aForwarder) {
    return nullptr;
  }

 protected:
  MOZ_COUNTED_DEFAULT_CTOR(TextureData)
};

/**
 * TextureClient is a thin abstraction over texture data that need to be shared
 * between the content process and the compositor process. It is the
 * content-side half of a TextureClient/TextureHost pair. A corresponding
 * TextureHost lives on the compositor-side.
 *
 * TextureClient's primary purpose is to present texture data in a way that is
 * understood by the IPC system. There are two ways to use it:
 * - Use it to serialize image data that is not IPC-friendly (most likely
 * involving a copy into shared memory)
 * - preallocate it and paint directly into it, which avoids copy but requires
 * the painting code to be aware of TextureClient (or at least the underlying
 * shared memory).
 *
 * There is always one and only one TextureClient per TextureHost, and the
 * TextureClient/Host pair only owns one buffer of image data through its
 * lifetime. This means that the lifetime of the underlying shared data
 * matches the lifetime of the TextureClient/Host pair. It also means
 * TextureClient/Host do not implement double buffering, which is the
 * responsibility of the compositable (which would use pairs of Textures).
 * In order to send several different buffers to the compositor side, use
 * several TextureClients.
 */
class TextureClient : public AtomicRefCountedWithFinalize<TextureClient> {
 public:
  TextureClient(TextureData* aData, TextureFlags aFlags,
                LayersIPCChannel* aAllocator);

  virtual ~TextureClient();

  static already_AddRefed<TextureClient> CreateWithData(
      TextureData* aData, TextureFlags aFlags, LayersIPCChannel* aAllocator);

  // Creates and allocates a TextureClient usable with Moz2D.
  static already_AddRefed<TextureClient> CreateForDrawing(
      KnowsCompositor* aAllocator, gfx::SurfaceFormat aFormat,
      gfx::IntSize aSize, BackendSelector aSelector, TextureFlags aTextureFlags,
      TextureAllocationFlags flags = ALLOC_DEFAULT);

  static already_AddRefed<TextureClient> CreateFromSurface(
      KnowsCompositor* aAllocator, gfx::SourceSurface* aSurface,
      BackendSelector aSelector, TextureFlags aTextureFlags,
      TextureAllocationFlags aAllocFlags);

  // Creates and allocates a TextureClient supporting the YCbCr format.
  static already_AddRefed<TextureClient> CreateForYCbCr(
      KnowsCompositor* aAllocator, const gfx::IntRect& aDisplay,
      const gfx::IntSize& aYSize, uint32_t aYStride,
      const gfx::IntSize& aCbCrSize, uint32_t aCbCrStride,
      StereoMode aStereoMode, gfx::ColorDepth aColorDepth,
      gfx::YUVColorSpace aYUVColorSpace, gfx::ColorRange aColorRange,
      gfx::TransferFunction aTransferFunction,
      gfx::ChromaSubsampling aSubsampling, TextureFlags aTextureFlags,
      const Maybe<gfx::HDRMetadata>& aHDRMetadata = Nothing());

  // Creates and allocates a TextureClient (can be accessed through raw
  // pointers).
  static already_AddRefed<TextureClient> CreateForRawBufferAccess(
      KnowsCompositor* aAllocator, gfx::SurfaceFormat aFormat,
      gfx::IntSize aSize, gfx::BackendType aMoz2dBackend,
      TextureFlags aTextureFlags, TextureAllocationFlags flags = ALLOC_DEFAULT);

  // Creates and allocates a TextureClient of the same type.
  already_AddRefed<TextureClient> CreateSimilar(
      LayersBackend aLayersBackend = LayersBackend::LAYERS_NONE,
      TextureFlags aFlags = TextureFlags::DEFAULT,
      TextureAllocationFlags aAllocFlags = ALLOC_DEFAULT) const;

  /**
   * Locks the shared data, allowing the caller to get access to it.
   *
   * Please always lock/unlock when accessing the shared data.
   * If Lock() returns false, you should not attempt to access the shared data.
   */
  bool Lock(OpenMode aMode);

  void Unlock();

  bool IsLocked() const { return mIsLocked; }

  gfx::IntSize GetSize() const { return mInfo.size; }

  gfx::SurfaceFormat GetFormat() const { return mInfo.format; }

  /**
   * Returns true if this texture has a synchronization mechanism (mutex, fence,
   * etc.). Textures that do not implement synchronization should be immutable
   * or should use immediate uploads (see TextureFlags in CompositorTypes.h)
   * Even if a texture does not implement synchronization, Lock and Unlock need
   * to be used appropriately since the latter are also there to map/numap data.
   */
  bool HasSynchronization() const { return mInfo.hasSynchronization; }

  bool CanExposeDrawTarget() const { return mInfo.supportsMoz2D; }

  bool CanExposeMappedData() const { return mInfo.canExposeMappedData; }

  /**
   * Returns a DrawTarget to draw into the TextureClient.
   * This function should never be called when not on the main thread!
   *
   * This must never be called on a TextureClient that is not sucessfully
   * locked. When called several times within one Lock/Unlock pair, this method
   * should return the same DrawTarget. The DrawTarget is automatically flushed
   * by the TextureClient when the latter is unlocked, and the DrawTarget that
   * will be returned within the next lock/unlock pair may or may not be the
   * same object. Do not keep references to the DrawTarget outside of the
   * lock/unlock pair.
   *
   * This is typically used as follows:
   *
   * if (!texture->Lock(OpenMode::OPEN_READ_WRITE)) {
   *   return false;
   * }
   * {
   *   // Restrict this code's scope to ensure all references to dt are gone
   *   // when Unlock is called.
   *   DrawTarget* dt = texture->BorrowDrawTarget();
   *   // use the draw target ...
   * }
   * texture->Unlock();
   *
   */
  gfx::DrawTarget* BorrowDrawTarget();

  /**
   * When the TextureClient is not being Unlocked, this can be used to inform it
   * that drawing has finished until the next BorrowDrawTarget.
   */
  void EndDraw();

  already_AddRefed<gfx::SourceSurface> BorrowSnapshot();

  // Snapshot BufferTextureData.
  already_AddRefed<gfx::SourceSurface> GetBufferSnapshot();

  void ReturnSnapshot(already_AddRefed<gfx::SourceSurface> aSnapshot);

  /**
   * Similar to BorrowDrawTarget but provides direct access to the texture's
   * bits instead of a DrawTarget.
   */
  bool BorrowMappedData(MappedTextureData&);
  bool BorrowMappedYCbCrData(MappedYCbCrTextureData&);

  // Also retain the shmem mapping when keeping plane pointers beyond Unlock().
  // aShmemHolder is null for textures which do not use shmem.
  bool BorrowMappedYCbCrData(MappedYCbCrTextureData& aMap,
                             RefPtr<ShmemHolder>& aShmemHolder);

  /**
   * This function can be used to update the contents of the TextureClient
   * off the main thread.
   */
  void UpdateFromSurface(gfx::SourceSurface* aSurface);

  /**
   * This method is strictly for debugging. It causes locking and
   * needless copies.
   */
  already_AddRefed<gfx::DataSourceSurface> GetAsSurface();

  /**
   * Copies a rectangle from this texture client to a position in aTarget.
   * It is assumed that the necessary locks are in place; so this should at
   * least have a read lock and aTarget should at least have a write lock.
   */
  bool CopyToTextureClient(TextureClient* aTarget, const gfx::IntRect* aRect,
                           const gfx::IntPoint* aPoint);

  /**
   * Allocate a TextureChild actor.
   *
   * TextureChild is an implementation detail of TextureClient that is not
   * exposed to the rest of the code base.
   */
  static already_AddRefed<PTextureChild> CreateIPDLActor();

  /**
   * Get the TextureClient corresponding to the actor passed in parameter.
   */
  static already_AddRefed<TextureClient> AsTextureClient(PTextureChild* actor);

  /**
   * TextureFlags contain important information about various aspects
   * of the texture, like how its liferime is managed, and how it
   * should be displayed.
   * See TextureFlags in CompositorTypes.h.
   */
  TextureFlags GetFlags() const { return mFlags; }

  bool HasFlags(TextureFlags aFlags) const {
    return (mFlags & aFlags) == aFlags;
  }

  void AddFlags(TextureFlags aFlags);

  void RemoveFlags(TextureFlags aFlags);

  // Must not be called when TextureClient is in use by CompositableClient.
  void RecycleTexture(TextureFlags aFlags);

  /**
   * After being shared with the compositor side, an immutable texture is never
   * modified, it can only be read. It is safe to not Lock/Unlock immutable
   * textures.
   */
  bool IsImmutable() const { return !!(mFlags & TextureFlags::IMMUTABLE); }

  void MarkImmutable() { AddFlags(TextureFlags::IMMUTABLE); }

  bool IsSharedWithCompositor() const;

  /**
   * If this method returns false users of TextureClient are not allowed
   * to access the shared data.
   */
  bool IsValid() const { return !!mData; }

  /**
   * Called when TextureClient is added to CompositableClient.
   */
  void SetAddedToCompositableClient();

  /**
   * If this method retuns false, TextureClient is already added to
   * CompositableClient, since its creation or recycling.
   */
  bool IsAddedToCompositableClient() const {
    return mAddedToCompositableClient;
  }

  /**
   * Create and init the TextureChild/Parent IPDL actor pair
   * with a CompositableForwarder.
   *
   * Should be called only once per TextureClient.
   * The TextureClient must not be locked when calling this method.
   */
  bool InitIPDLActor(CompositableForwarder* aForwarder);

  /**
   * Create and init the TextureChild/Parent IPDL actor pair
   * with a TextureForwarder.
   *
   * Should be called only once per TextureClient.
   * The TextureClient must not be locked when calling this method.
   */
  bool InitIPDLActor(KnowsCompositor* aKnowsCompositor,
                     const dom::ContentParentId& aContentId);

  /**
   * Return a pointer to the IPDLActor.
   *
   * This is to be used with IPDL messages only. Do not store the returned
   * pointer.
   */
  PTextureChild* GetIPDLActor();

  /**
   * Triggers the destruction of the shared data and the corresponding
   * TextureHost.
   *
   * If the texture flags contain TextureFlags::DEALLOCATE_CLIENT, the
   * destruction will be synchronously coordinated with the compositor side,
   * otherwise it will be done asynchronously.
   */
  void Destroy();

  /**
   * Track how much of this texture is wasted.
   * For example we might allocate a 256x256 tile but only use 10x10.
   */
  void SetWaste(int aWasteArea) {
    mWasteTracker.Update(aWasteArea, BytesPerPixel(GetFormat()));
  }

  void SyncWithObject(RefPtr<SyncObjectClient> aSyncObject) {
    mData->SyncWithObject(aSyncObject);
  }

  LayersIPCChannel* GetAllocator() { return mAllocator; }

  ITextureClientRecycleAllocator* GetRecycleAllocator() {
    return mRecycleAllocator;
  }
  void SetRecycleAllocator(ITextureClientRecycleAllocator* aAllocator);

  /// If you add new code that uses this method, you are probably doing
  /// something wrong.
  TextureData* GetInternalData() { return mData.get(); }
  const TextureData* GetInternalData() const { return mData.get(); }

  /**
   * Serializes the underlying TextureData into aDescriptor. Returns false if
   * the TextureData has already been destroyed. Safe to call from any thread
   * concurrently with Destroy().
   */
  bool ToSurfaceDescriptor(SurfaceDescriptor& aDescriptor);

  uint64_t GetSerial() const { return mSerial; }
  void GetSurfaceDescriptorRemoteDecoder(
      SurfaceDescriptorRemoteDecoder* aOutDesc);

  void CancelWaitForNotifyNotUsed();

  /**
   * Set last transaction id of CompositableForwarder.
   *
   * Called when TextureClient has TextureFlags::RECYCLE flag.
   * When CompositableForwarder forwards the TextureClient with
   * TextureFlags::RECYCLE, it holds TextureClient's ref until host side
   * releases it. The host side sends TextureClient release message.
   * The id is used to check if the message is for the last TextureClient
   * forwarding.
   */
  void SetLastFwdTransactionId(uint64_t aTransactionId) {
    MOZ_ASSERT(mFwdTransactionId <= aTransactionId);
    mFwdTransactionId = aTransactionId;
  }

  uint64_t GetLastFwdTransactionId() { return mFwdTransactionId; }

  bool HasReadLock() const {
    MutexAutoLock lock(mMutex);
    return !!mReadLock;
  }

  int32_t GetNonBlockingReadLockCount() {
    MutexAutoLock lock(mMutex);
    if (NS_WARN_IF(!mReadLock)) {
      MOZ_ASSERT_UNREACHABLE("No read lock created yet?");
      return 0;
    }
    MOZ_ASSERT(mReadLock->AsNonBlockingLock(),
               "Can only check locked for non-blocking locks!");
    return mReadLock->AsNonBlockingLock()->GetReadCount();
  }

  bool IsReadLocked();

  bool ShouldReadLock() const {
    return bool(mFlags & (TextureFlags::NON_BLOCKING_READ_LOCK |
                          TextureFlags::BLOCKING_READ_LOCK));
  }

  bool TryReadLock();
  void ReadUnlock();

  void SetUpdated() { mUpdated = true; }

  void OnPrepareForwardToHost();
  void OnAbandonForwardToHost();
  bool OnForwardedToHost();

  // Mark that the TextureClient will be used by the paint thread, and should
  // not free its underlying texture data. This must only be called from the
  // main thread.
  void AddPaintThreadRef();

  // Mark that the TextureClient is no longer in use by the PaintThread. This
  // must only be called from the PaintThread.
  void DropPaintThreadRef();

  wr::MaybeExternalImageId GetExternalImageKey() { return mExternalImageId; }

 private:
  static void TextureClientRecycleCallback(TextureClient* aClient,
                                           void* aClosure);

  static already_AddRefed<TextureClient> CreateForDrawing(
      TextureForwarder* aAllocator, gfx::SurfaceFormat aFormat,
      gfx::IntSize aSize, KnowsCompositor* aKnowsCompositor,
      BackendSelector aSelector, TextureFlags aTextureFlags,
      TextureAllocationFlags aAllocFlags = ALLOC_DEFAULT);

  static already_AddRefed<TextureClient> CreateForRawBufferAccess(
      LayersIPCChannel* aAllocator, gfx::SurfaceFormat aFormat,
      gfx::IntSize aSize, gfx::BackendType aMoz2dBackend,
      LayersBackend aLayersBackend, TextureFlags aTextureFlags,
      TextureAllocationFlags flags = ALLOC_DEFAULT);

  void EnsureHasReadLock() MOZ_REQUIRES(mMutex);
  void EnableReadLock() MOZ_REQUIRES(mMutex);
  void EnableBlockingReadLock() MOZ_REQUIRES(mMutex);

  /**
   * Called once, during the destruction of the Texture, on the thread in which
   * texture's reference count reaches 0 (could be any thread).
   *
   * Here goes the shut-down code that uses virtual methods.
   * Must only be called by Release().
   */
  void Finalize() {}

  friend class AtomicRefCountedWithFinalize<TextureClient>;

 protected:
  void LockActor() const;
  void UnlockActor() const;

  TextureData::Info mInfo;
  mutable Mutex mMutex;

  RefPtr<LayersIPCChannel> mAllocator;
  RefPtr<TextureChild> mActor;
  // Actor locked by LockActor() for the matching UnlockActor() call on the
  // same thread, including drawing-thread and IPDL-thread callers.
  mutable RefPtr<TextureChild> mActorForUnlock MOZ_GUARDED_BY(mMutex);
  // Set when Destroy() starts; prevents assigning a new actor afterward.
  bool mActorDestroying MOZ_GUARDED_BY(mMutex) = false;
  RefPtr<ITextureClientRecycleAllocator> mRecycleAllocator;
  RefPtr<TextureReadLock> mReadLock MOZ_GUARDED_BY(mMutex);

  UniquePtr<TextureData> mData;
  RefPtr<gfx::DrawTarget> mBorrowedDrawTarget;
  bool mBorrowedSnapshot = false;

  TextureFlags mFlags;

  gl::GfxTextureWasteTracker mWasteTracker;

  OpenMode mOpenMode;
  bool mIsLocked;
  bool mIsReadLocked MOZ_GUARDED_BY(mMutex);
  bool mIsPendingForwardReadLocked MOZ_GUARDED_BY(mMutex) = false;
  // This member tracks that the texture was written into until the update
  // is sent to the compositor. We need this remember to lock mReadLock on
  // behalf of the compositor just before sending the notification.
  bool mUpdated;

  // Used when TextureClient is recycled with TextureFlags::RECYCLE flag.
  bool mAddedToCompositableClient;

  uint64_t mFwdTransactionId;

  // Serial id of TextureClient. It is unique in current process.
  const uint64_t mSerial;

  // When non-zero, texture data must not be freed.
  mozilla::Atomic<uintptr_t> mPaintThreadRefs;

  // External image id. It is unique if it is allocated.
  // The id is allocated in TextureClient::InitIPDLActor().
  // Its allocation is supported by
  // CompositorBridgeChild and ImageBridgeChild for now.
  wr::MaybeExternalImageId mExternalImageId;

  // Used to assign serial ids of TextureClient.
  static mozilla::Atomic<uint64_t> sSerialCounter;

  friend class TextureChild;
  friend void TestTextureClientSurface(TextureClient*, gfxImageSurface*);
  friend void TestTextureClientYCbCr(TextureClient*, PlanarYCbCrData&);
  friend void TestYCbCrDescriptorTransferFunction(gfx::TransferFunction,
                                                  Maybe<gfx::HDRMetadata>,
                                                  RefPtr<ImageBridgeChild>);
  friend already_AddRefed<TextureHost> CreateTextureHostWithBackend(
      TextureClient*, ISurfaceAllocator*, LayersBackend&);
};

/**
 * Task that releases TextureClient pointer on a specified thread.
 */
class TextureClientReleaseTask : public Runnable {
 public:
  explicit TextureClientReleaseTask(TextureClient* aClient)
      : Runnable("layers::TextureClientReleaseTask"), mTextureClient(aClient) {}

  NS_IMETHOD Run() override {
    mTextureClient = nullptr;
    return NS_OK;
  }

 private:
  RefPtr<TextureClient> mTextureClient;
};

// Automatically lock and unlock a texture. Since texture locking is fallible,
// Succeeded() must be checked on the guard object before proceeding.
class MOZ_RAII TextureClientAutoLock {
 public:
  TextureClientAutoLock(TextureClient* aTexture, OpenMode aMode)
      : mTexture(aTexture), mSucceeded(false) {
    mSucceeded = mTexture->Lock(aMode);
#ifdef DEBUG
    mChecked = false;
#endif
  }
  ~TextureClientAutoLock() {
    MOZ_ASSERT(mChecked);
    if (mSucceeded) {
      mTexture->Unlock();
    }
  }

  bool Succeeded() {
#ifdef DEBUG
    mChecked = true;
#endif
    return mSucceeded;
  }

 private:
  TextureClient* mTexture;
#ifdef DEBUG
  bool mChecked;
#endif
  bool mSucceeded;
};

/// Convenience function to set the content of ycbcr texture.
bool UpdateYCbCrTextureClient(TextureClient* aTexture,
                              const PlanarYCbCrData& aData);

TextureType PreferredCanvasTextureType(KnowsCompositor* aKnowsCompositor);

}  // namespace layers
}  // namespace mozilla

#endif
