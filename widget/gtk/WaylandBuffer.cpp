/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WaylandBuffer.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "SHMBufSurface.h"
#include "WaylandSurface.h"
#include "WaylandSurfaceLock.h"
#include "gfx2DGlue.h"
#include "mozilla/WidgetUtilsGtk.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Tools.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "nsGtkUtils.h"
#include "nsPrintfCString.h"
#include "prenv.h"  // For PR_GetEnv

#ifdef MOZ_LOGGING
#  include "Units.h"
#  include "mozilla/Logging.h"
extern mozilla::LazyLogModule gWidgetWaylandLog;
#  define LOGWAYLAND(...) \
    MOZ_LOG(gWidgetWaylandLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#else
#  define LOGWAYLAND(...)
#endif /* MOZ_LOGGING */

using namespace mozilla::gl;

namespace mozilla::widget {

#ifdef MOZ_LOGGING
MOZ_RUNINIT int WaylandBuffer::mDumpSerial =
    PR_GetEnv("MOZ_WAYLAND_DUMP_WL_BUFFERS") ? 1 : 0;
MOZ_RUNINIT char* WaylandBuffer::mDumpDir = PR_GetEnv("MOZ_WAYLAND_DUMP_DIR");
#endif

WaylandBuffer::WaylandBuffer(const LayoutDeviceIntSize& aSize) : mSize(aSize) {}
WaylandBuffer::~WaylandBuffer() {
  LOGWAYLAND("WaylandBuffer::~WaylandBuffer [%p] UID %d\n", (void*)this,
             mBufferSurface ? mBufferSurface->GetUID() : -1);
  MOZ_RELEASE_ASSERT(mBufferTransactions.IsEmpty());
}

/* static */
already_AddRefed<WaylandBuffer> WaylandBuffer::Create(
    RefPtr<BufferSurface> aSurface) {
  const auto size =
      LayoutDeviceIntSize(aSurface->GetWidth(), aSurface->GetHeight());
  RefPtr<WaylandBuffer> buffer = new WaylandBuffer(size);

  LOGWAYLAND("WaylandBuffer::Create() BUfferSurface [%p] UID %d [%d x %d]",
             (void*)buffer, aSurface->GetUID(), size.width, size.height);

  buffer->mBufferSurface = aSurface;
  return buffer.forget();
}

bool WaylandBuffer::IsAttached(const WaylandSurfaceLock& aSurfaceLock) const {
  for (const auto& transaction : mBufferTransactions) {
    if (transaction->IsAttached()) {
      return true;
    }
  }
  return false;
}

BufferTransaction* WaylandBuffer::GetTransaction(
    const WaylandSurfaceLock& aSurfaceLock) {
  for (const auto& transaction : mBufferTransactions) {
    if (transaction->CanRecycle(aSurfaceLock.GetWaylandSurface())) {
      LOGWAYLAND("WaylandBuffer::GetTransaction() [%p] reuse transaction [%d]",
                 (void*)this, (int)mBufferTransactions.Length());
      return transaction;
    }
  }

  wl_buffer* buffer = mExternalWlBuffer;
  if (!buffer) {
    buffer = CreateWlBuffer();
  }
  if (!buffer) {
    gfxCriticalError()
        << "WaylandBuffer::GetTransaction() failed to create wl_buffer!";
    return nullptr;
  }

  LOGWAYLAND(
      "WaylandBuffer::GetTransaction() create new [%p] wl_buffer [%p] "
      "transactions [%d] external buffer [%d] WaylandSurface [%p]",
      (void*)this, buffer, (int)mBufferTransactions.Length(),
      !!mExternalWlBuffer, aSurfaceLock.GetWaylandSurface());

  auto* transaction = new BufferTransaction(this, buffer, !!mExternalWlBuffer);
  mBufferTransactions.AppendElement(transaction);
  return transaction;
}

void WaylandBuffer::RemoveTransaction(const WaylandSurfaceLock& aSurfaceLock,
                                      RefPtr<BufferTransaction> aTransaction) {
  LOGWAYLAND("WaylandBuffer::RemoveTransaction() [%p]", (void*)aTransaction);
  [[maybe_unused]] bool removed =
      mBufferTransactions.RemoveElement(aTransaction);
  MOZ_DIAGNOSTIC_ASSERT(removed);
  MOZ_DIAGNOSTIC_ASSERT(!mBufferTransactions.Contains(aTransaction));
}

void WaylandBuffer::SetExternalWLBuffer(wl_buffer* aWLBuffer) {
  LOGWAYLAND("WaylandBuffer::SetExternalWLBuffer() [%p] wl_buffer %p",
             (void*)this, aWLBuffer);
  MOZ_DIAGNOSTIC_ASSERT(!mExternalWlBuffer);
  mExternalWlBuffer = aWLBuffer;
}

wl_buffer* WaylandBuffer::CreateWlBuffer() {
  MOZ_DIAGNOSTIC_ASSERT(!mExternalWlBuffer);
  auto* buffer = mBufferSurface->CreateWlBuffer();
  LOGWAYLAND("WaylandBuffer::CreateWlBuffer() [%p] UID %d wl_buffer [%p]",
             (void*)this, mBufferSurface->GetUID(), buffer);
  return buffer;
}

already_AddRefed<gfx::DrawTarget> WaylandBuffer::Lock() {
  LOGWAYLAND("WaylandBuffer::lock() [%p]\n", (void*)this);
  return mBufferSurface->Lock();
}

void* WaylandBuffer::GetImageData() { return mBufferSurface->GetImageData(); }

#ifdef MOZ_LOGGING
void WaylandBuffer::DumpToFile(const char* aHint) {
  if (!mDumpSerial) {
    NS_WARNING("mDumpSerial is not set!");
    return;
  }

  nsCString filename;
  if (mDumpDir) {
    filename.Append(mDumpDir);
    filename.Append('/');
  }
  filename.Append(
      nsPrintfCString("firefox-wl-buffer-%.5d-%s.png", mDumpSerial++, aHint));
  mBufferSurface->DumpToFile(filename.get());
  LOGWAYLAND("Dumped wl_buffer to %s\n", filename.get());
}
#endif

/* static */
RefPtr<WaylandBuffer> WaylandBuffer::CreateSHM(
    const LayoutDeviceIntSize& aSize, RefPtr<widget::DRMFormat> aFormat) {
  RefPtr<WaylandBuffer> buffer = new WaylandBuffer(aSize);

  LOGWAYLAND("WaylandBuffer::CreateSHM() [%p] [%d x %d]", (void*)buffer,
             aSize.width, aSize.height);

  int32_t FOURCCFormat = aFormat ? aFormat->GetFormat() : GBM_FORMAT_ARGB8888;
  buffer->mBufferSurface =
      SHMBufSurfaceRGBA::Create(aSize.ToUnknownSize(), FOURCCFormat);
  if (!buffer->mBufferSurface) {
    LOGWAYLAND("  failed to create SHMBufSurface");
    return nullptr;
  }

  LOGWAYLAND("  created [%p]\n", buffer.get());
  return buffer;
}

/* static */
already_AddRefed<WaylandBuffer> WaylandBuffer::CreateDMABuf(
    const LayoutDeviceIntSize& aSize, GLContext* aGL,
    RefPtr<DRMFormat> aFormat) {
  RefPtr<WaylandBuffer> buffer = new WaylandBuffer(aSize);

  buffer->mBufferSurface = DMABufSurfaceRGBA::CreateDMABufSurface(
      aGL, aSize.width, aSize.height, DMABUF_SCANOUT | DMABUF_USE_MODIFIERS,
      aFormat);
  if (!buffer->mBufferSurface || !buffer->mBufferSurface->CreateTextures(aGL)) {
    LOGWAYLAND("  failed to create texture");
    return nullptr;
  }

  LOGWAYLAND("WaylandBuffer::CreateDMABuf() [%p] UID %d [%d x %d]",
             (void*)buffer, buffer->mBufferSurface->GetUID(), aSize.width,
             aSize.height);
  return buffer.forget();
}

WaylandBufferHolder::WaylandBufferHolder(BufferSurface* aSurface,
                                         wl_buffer* aWLBuffer,
                                         RefPtr<WaylandBuffer> aWaylandBuffer)
    : mWLBuffer(aWLBuffer), mWaylandBuffer(std::move(aWaylandBuffer)) {
  mUID = aSurface->GetUID();
  mPID = aSurface->GetPID();
  LOGWAYLAND(
      "WaylandBufferHolder::WaylandBufferHolder wl_buffer [%p] UID "
      "%d PID %d",
      mWLBuffer, mUID, mPID);
}

WaylandBufferHolder::~WaylandBufferHolder() {
  LOGWAYLAND(
      "WaylandBufferHolder::~WaylandBufferHolder wl_buffer [%p] "
      "UID %d PID %d",
      mWLBuffer, mUID, mPID);
  MozClearPointer(mWLBuffer, wl_buffer_destroy);
}

bool WaylandBufferHolder::CheckForRecycle(
    const WaylandSurfaceLock& aSurfaceLock) {
  // We can recycle buffer holders without active transaction.
  if (!mTransaction) {
    mWaylandBuffer = nullptr;
    return true;
  }
  // Remove finished transaction and wayland buffer references
  // to release them.
  if (mTransaction->IsDeleted()) {
    mTransaction = nullptr;
    mWaylandBuffer = nullptr;
    return true;
  }
  return false;
}

bool WaylandBufferHolder::CanRecycle(const WaylandSurfaceLock& aSurfaceLock,
                                     BufferSurface* aSurface) {
  return CheckForRecycle(aSurfaceLock) && mUID == aSurface->GetUID() &&
         mPID == aSurface->GetPID();
}

void WaylandBufferHolder::Recycle(const WaylandSurfaceLock& aSurfaceLock,
                                  RefPtr<WaylandBuffer> aWaylandBuffer) {
  MOZ_RELEASE_ASSERT(!mTransaction, "Already used?");
  MOZ_RELEASE_ASSERT(!mWaylandBuffer, "Already used?");
  mWaylandBuffer = std::move(aWaylandBuffer);
}

bool WaylandBufferHolder::Matches(const WaylandSurfaceLock& aSurfaceLock,
                                  WaylandBuffer* aWaylandBuffer) const {
  return mWaylandBuffer == aWaylandBuffer;
}

wl_buffer* BufferTransaction::BufferBorrowLocked(
    const WaylandSurfaceLock& aSurfaceLock) {
  LOGWAYLAND(
      "BufferTransaction::BufferBorrow() [%p] widget [%p] WaylandSurface [%p] "
      "WaylandBuffer [%p]",
      this, aSurfaceLock.GetWaylandSurface()->GetLoggingWidget(),
      aSurfaceLock.GetWaylandSurface(), mBuffer.get());

  MOZ_DIAGNOSTIC_ASSERT(
      !mSurface || mSurface == aSurfaceLock.GetWaylandSurface(),
      "Can't transfer transaction between WaylandSurfaces!");
  MOZ_DIAGNOSTIC_ASSERT(mBufferState == BufferState::Detached);

  mSurface = aSurfaceLock.GetWaylandSurface();

  // We don't take reference to this. Some compositors doesn't send
  // buffer release callback and we may leak BufferTransaction then.
  // Rather we destroy wl_buffer at end which makes sure no release callback
  // comes after BufferTransaction release.
  if (wl_proxy_get_listener((wl_proxy*)mWLBuffer)) {
    wl_proxy_set_user_data((wl_proxy*)mWLBuffer, this);
  } else {
    static const struct wl_buffer_listener listener{
        [](void* aData, wl_buffer* aBuffer) {
          auto transaction = static_cast<BufferTransaction*>(aData);
          if (transaction) {
            // BufferDetachCallback can delete transaction
            RefPtr grip{transaction};
            transaction->BufferDetachCallback();
          }
        }};
    if (wl_buffer_add_listener(mWLBuffer, &listener, this) < 0) {
      gfxCriticalError() << "wl_buffer_add_listener() failed";
    }
  }

  mBufferState = BufferState::WaitingForDetach;
  return mWLBuffer;
}

void BufferTransaction::BufferDetachCallback() {
  WaylandSurfaceLock lock(mSurface);
  LOGWAYLAND(
      "BufferTransaction::BufferDetach() [%p] WaylandBuffer [%p] attached to "
      "WaylandSurface %d",
      this, (void*)mBuffer, mSurface->IsBufferAttached(mBuffer));

  if (mBufferState != BufferState::WaitingForDelete) {
    mBufferState = BufferState::Detached;

    // Delete this transaction if WaylandSurface uses different WaylandBuffer
    // already, we don't need to keep it for recycling.
    if (!mSurface->IsBufferAttached(mBuffer)) {
      DeleteTransactionLocked(lock);
    }
  }
}

void BufferTransaction::BufferDeleteCallback() {
  LOGWAYLAND("BufferTransaction::DeleteCallback() [%p] WaylandBuffer [%p] ",
             this, (void*)mBuffer);
  WaylandSurfaceLock lock(mSurface);
  mBufferState = BufferState::Deleted;
  DeleteLocked(lock);
}

void BufferTransaction::WlBufferDeleteLocked(
    const WaylandSurfaceLock& aSurfaceLock) {
  LOGWAYLAND(
      "BufferTransaction::WlBufferDeleteLocked() [%p] WaylandBuffer [%p] ",
      this, (void*)mBuffer);
  MOZ_DIAGNOSTIC_ASSERT(mWLBuffer);
  if (mIsExternalBuffer) {
    wl_proxy_set_user_data((wl_proxy*)mWLBuffer, nullptr);
    mWLBuffer = nullptr;
  } else {
    MozClearPointer(mWLBuffer, wl_buffer_destroy);
  }
}

void BufferTransaction::DeleteTransactionLocked(
    const WaylandSurfaceLock& aSurfaceLock) {
  // It's possible that the transaction is already deleted. It happens
  // if one WaylandBuffer is attached to WaylandSurface,
  // then detached/deleted from WaylandSurface::UnmapLocked() where all buffers
  // are removed and then attached to another WaylandSurface.
  if (mBufferState == BufferState::WaitingForDelete ||
      mBufferState == BufferState::Deleted) {
    return;
  }

  LOGWAYLAND(
      "BufferTransaction::BufferDelete() [%p] WaylandBuffer [%p] wl_buffer "
      "[%p] "
      "external %d state %d",
      this, (void*)mBuffer, mWLBuffer, mIsExternalBuffer, (int)mBufferState);

  WlBufferDeleteLocked(aSurfaceLock);

  // wl_buffer is detached so we can't get any release event so
  // delete the transaction now.
  if (mBufferState == BufferState::Detached) {
    mBufferState = BufferState::Deleted;
    DeleteLocked(aSurfaceLock);
    return;
  }

  mBufferState = BufferState::WaitingForDelete;

  // There are various Wayland queues processed for every thread.
  // It's possible that wl_buffer release event is pending in any
  // queue while we already asked for wl_buffer delete.
  // We need to finish wl_buffer removal when all events from this
  // point are processed so we use sync callback.
  //
  // When wl_display_sync comes back to us (from main thread)
  // we know all events are processed and there isn't any
  // wl_buffer operation pending so we can safely release WaylandSurface
  // and WaylandBuffer objects.
  // We need to wait with WaylandMonitor
  AddRef();
  static const struct wl_callback_listener listener{
      [](void* aData, struct wl_callback* callback, uint32_t time) {
        RefPtr t = dont_AddRef(static_cast<BufferTransaction*>(aData));
        t->BufferDeleteCallback();
      }};
  wl_callback_add_listener(wl_display_sync(WaylandDisplayGetWLDisplay()),
                           &listener, this);
}

void BufferTransaction::DeleteLocked(const WaylandSurfaceLock& aSurfaceLock) {
  LOGWAYLAND("BufferTransaction::DeleteLocked() [%p] WaylandBuffer [%p]", this,
             (void*)mBuffer);
  MOZ_DIAGNOSTIC_ASSERT(mBufferState == BufferState::Deleted);
  MOZ_DIAGNOSTIC_ASSERT(mSurface);

  // Unlink from Surface
  mSurface->RemoveTransactionLocked(aSurfaceLock, this);
  mSurface = nullptr;

  // This can destroy us
  RefPtr grip{this};
  mBuffer->RemoveTransaction(aSurfaceLock, this);
  mBuffer = nullptr;
}

BufferTransaction::BufferTransaction(WaylandBuffer* aBuffer,
                                     wl_buffer* aWLBuffer,
                                     bool aIsExternalBuffer)
    : mBuffer(aBuffer),
      mWLBuffer(aWLBuffer),
      mIsExternalBuffer(aIsExternalBuffer) {
  MOZ_COUNT_CTOR(BufferTransaction);
  LOGWAYLAND(
      "BufferTransaction::BufferTransaction() [%p] WaylandBuffer [%p] "
      "wl_buffer [%p] external [%d]",
      this, aBuffer, mWLBuffer, mIsExternalBuffer);
}

BufferTransaction::~BufferTransaction() {
  MOZ_COUNT_DTOR(BufferTransaction);
  LOGWAYLAND("BufferTransaction::~BufferTransaction() [%p] ", this);
  MOZ_DIAGNOSTIC_ASSERT(mBufferState == BufferState::Deleted);
  MOZ_DIAGNOSTIC_ASSERT(!mWLBuffer);
}

}  // namespace mozilla::widget
