/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Buffer.h"

#include "Device.h"
#include "PromiseHelpers.h"
#include "ipc/WebGPUChild.h"
#include "js/ArrayBuffer.h"
#include "js/RootingAPI.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/ipc/Shmem.h"
#include "mozilla/webgpu/ffi/wgpu.h"
#include "nsContentUtils.h"
#include "nsWrapperCache.h"

namespace mozilla::webgpu {

GPU_IMPL_JS_WRAP(Buffer)

// We can't use `NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_WITH_JS_MEMBERS` since
// we need to trace all nested `ArrayBuffer`s. We also need access to the
// parent in the `Cleanup` step before we unlink it.
NS_IMPL_CYCLE_COLLECTION_CLASS(Buffer)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(Buffer)
  tmp->Cleanup();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mMapRequest)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(Buffer)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMapRequest)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(Buffer)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
  if (tmp->mMapped) {
    for (uint32_t i = 0; i < tmp->mMapped->mViews.Length(); ++i) {
      NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(
          mMapped->mViews[i].mArrayBuffer)
    }
  }
NS_IMPL_CYCLE_COLLECTION_TRACE_END

Buffer::Buffer(Device* const aParent, RawId aId, BufferAddress aSize,
               uint32_t aUsage, ipc::SharedMemoryMapping&& aShmem)
    : ObjectBase(aParent->GetChild(), aId, ffi::wgpu_client_drop_buffer),
      ChildOf(aParent),
      mSize(aSize),
      mUsage(aUsage) {
  mozilla::HoldJSObjects(this);
  mShmem = std::make_shared<ipc::SharedMemoryMapping>(std::move(aShmem));
  MOZ_ASSERT(mParent);
}

Buffer::~Buffer() {
  Cleanup();
  mozilla::DropJSObjects(this);
}

already_AddRefed<Buffer> Buffer::Create(Device* aDevice, RawId aDeviceId,
                                        const dom::GPUBufferDescriptor& aDesc,
                                        ErrorResult& aRv) {
  RefPtr<WebGPUChild> child = aDevice->GetChild();

  ipc::MutableSharedMemoryHandle handle;
  ipc::SharedMemoryMapping mapping;

  bool hasMapFlags = aDesc.mUsage & (dom::GPUBufferUsage_Binding::MAP_WRITE |
                                     dom::GPUBufferUsage_Binding::MAP_READ);

  bool allocSucceeded = false;
  if (hasMapFlags || aDesc.mMappedAtCreation) {
    // If shmem allocation fails, we continue and provide the parent side with
    // an empty shmem which it will interpret as an OOM situtation.
    const auto checked = CheckedInt<size_t>(aDesc.mSize);
    const size_t maxSize = WGPUMAX_BUFFER_SIZE;
    if (checked.isValid()) {
      size_t size = checked.value();

      if (size > 0 && size < maxSize) {
        handle = ipc::shared_memory::Create(size);
        mapping = handle.Map();
        if (handle && mapping) {
          allocSucceeded = true;

          MOZ_RELEASE_ASSERT(mapping.Size() >= size);

          // zero out memory
          memset(mapping.Address(), 0, size);
        } else {
          handle = nullptr;
          mapping = nullptr;
        }
      }

      if (size == 0) {
        // Zero-sized buffers is a special case. We don't create a shmem since
        // allocating the memory would not make sense, however mappable null
        // buffers are allowed by the spec so we just pass the null handle which
        // in practice deserializes into a null handle on the parent side and
        // behaves like a zero-sized allocation.
        allocSucceeded = true;
      }
    }
  }

  // If mapped at creation and the shmem allocation failed, immediately throw
  // a range error and don't attempt to create the buffer.
  if (aDesc.mMappedAtCreation && !allocSucceeded) {
    aRv.ThrowRangeError("Allocation failed");
    return nullptr;
  }

  ffi::WGPUFfiBufferDescriptor desc = {};
  webgpu::StringHelper label(aDesc.mLabel);
  desc.label = label.Get();
  desc.size = aDesc.mSize;
  desc.usage = aDesc.mUsage;
  desc.mapped_at_creation = aDesc.mMappedAtCreation;

  auto shmem_handle_index = child->QueueShmemHandle(std::move(handle));
  RawId bufferId = ffi::wgpu_client_create_buffer(child->GetClient(), aDeviceId,
                                                  &desc, shmem_handle_index);

  RefPtr<Buffer> buffer = new Buffer(aDevice, bufferId, aDesc.mSize,
                                     aDesc.mUsage, std::move(mapping));
  buffer->SetLabel(aDesc.mLabel);

  if (aDesc.mMappedAtCreation) {
    // Mapped at creation's raison d'être is write access, since the buffer is
    // being created and there isn't anything interesting to read in it yet.
    bool writable = true;
    buffer->SetMapped(0, aDesc.mSize, writable);
  }

  aDevice->TrackBuffer(buffer.get());

  return buffer.forget();
}

void Buffer::Cleanup() {
  if (!mValid) {
    return;
  }
  mValid = false;

  // `ArrayBuffer`s from `GetMappedRange` may outlive us, but each holds its own
  // reference to `mShmem` (see `ExternalBufferFreeCallback`), so we can leave
  // them attached.
  mMapped.reset();

  mParent->UntrackBuffer(this);
}

void Buffer::SetMapped(BufferAddress aOffset, BufferAddress aSize,
                       bool aWritable) {
  MOZ_ASSERT(!mMapped);
  MOZ_RELEASE_ASSERT(aOffset <= mSize);
  MOZ_RELEASE_ASSERT(aSize <= mSize - aOffset);

  mMapped.emplace();
  mMapped->mWritable = aWritable;
  mMapped->mOffset = aOffset;
  mMapped->mSize = aSize;
}

already_AddRefed<dom::Promise> Buffer::MapAsync(
    uint32_t aMode, uint64_t aOffset, const dom::Optional<uint64_t>& aSize,
    ErrorResult& aRv) {
  RefPtr<dom::Promise> promise = dom::Promise::Create(GetParentObject(), aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  if (mMapped) {
    auto message = "Buffer is already mapped"_ns;
    ffi::wgpu_report_validation_error(GetClient(), mParent->GetId(),
                                      message.get());
    promise->MaybeRejectWithOperationError(message);
    return promise.forget();
  }

  if (mMapRequest) {
    auto message = "Buffer mapping is already pending"_ns;
    ffi::wgpu_report_validation_error(GetClient(), mParent->GetId(),
                                      message.get());
    promise->MaybeRejectWithOperationError(message);
    return promise.forget();
  }

  BufferAddress size = 0;
  if (aSize.WasPassed()) {
    size = aSize.Value();
  } else if (aOffset <= mSize) {
    // Default to passing the reminder of the buffer after the provided offset.
    size = mSize - aOffset;
  } else {
    // The provided offset is larger than the buffer size.
    // The parent side will handle the error, we can let the requested size be
    // zero.
  }

  ffi::wgpu_client_buffer_map(GetClient(), mParent->GetId(), GetId(), aMode,
                              aOffset, size);

  mMapRequest = promise;

  GetChild()->EnqueueBufferMapPromise(GetId(), PendingBufferMapPromise{
                                                   promise,
                                                   this,
                                               });

  return promise.forget();
}

static void ExternalBufferFreeCallback(void* aContents, void* aUserData) {
  (void)aContents;
  auto shm = static_cast<std::shared_ptr<ipc::SharedMemoryMapping>*>(aUserData);
  delete shm;
}

void Buffer::GetMappedRange(JSContext* aCx, uint64_t aOffset,
                            const dom::Optional<uint64_t>& aSize,
                            JS::Rooted<JSObject*>* aObject, ErrorResult& aRv) {
  // The WebGPU spec spells out the validation we must perform, but
  // use `CheckedInt<uint64_t>` anyway to catch our mistakes. Except
  // where we explicitly say otherwise, invalid `CheckedInt` values
  // should only arise when we have a bug, so just calling
  // `CheckedInt::value` where needed should be fine (it checks with
  // `MOZ_DIAGNOSTIC_ASSERT`).

  // https://gpuweb.github.io/gpuweb/#dom-gpubuffer-getmappedrange
  //
  // Content timeline steps:
  //
  // 1. If `size` is missing:
  //    1. Let `rangeSize` be `max(0, this.size - offset)`.
  //    Otherwise, let `rangeSize` be `size`.
  const auto offset = CheckedInt<uint64_t>(aOffset);
  CheckedInt<uint64_t> rangeSize;
  if (aSize.WasPassed()) {
    rangeSize = aSize.Value();
  } else {
    const auto bufferSize = CheckedInt<uint64_t>(mSize);
    // Use `CheckInt`'s underflow detection for `max(0, ...)`.
    rangeSize = bufferSize - offset;
    if (!rangeSize.isValid()) {
      rangeSize = 0;
    }
  }

  // 2. If any of the following conditions are unsatisfied, throw an
  //    `OperationError` and stop.
  //
  //     - `this.[[mapping]]` is not `null`.
  if (!mMapped) {
    aRv.ThrowOperationError("Buffer is not mapped");
    return;
  }

  //     - `offset` is a multiple of 8.
  //
  // (`operator!=` is not available on `CheckedInt`.)
  if (offset.value() % 8 != 0) {
    aRv.ThrowOperationError("GetMappedRange offset is not a multiple of 8");
    return;
  }

  //     - `rangeSize` is a multiple of `4`.
  if (rangeSize.value() % 4 != 0) {
    aRv.ThrowOperationError("GetMappedRange size is not a multiple of 4");
    return;
  }

  //     - `offset ≥ this.[[mapping]].range[0]`.
  if (offset.value() < mMapped->mOffset) {
    aRv.ThrowOperationError(
        "GetMappedRange offset starts before buffer's mapped range");
    return;
  }

  //     - `offset + rangeSize ≤ this.[[mapping]].range[1]`.
  //
  // Perform the addition in `CheckedInt`, treating overflow as a validation
  // error.
  const auto rangeEndChecked = offset + rangeSize;
  if (!rangeEndChecked.isValid() ||
      rangeEndChecked.value() > mMapped->mOffset + mMapped->mSize) {
    aRv.ThrowOperationError(
        "GetMappedRange range extends beyond buffer's mapped range");
    return;
  }

  //     - `[offset, offset + rangeSize)` does not overlap another range
  //       in `this.[[mapping]].views`.
  const uint64_t rangeEnd = rangeEndChecked.value();
  for (const auto& view : mMapped->mViews) {
    if (view.mOffset < rangeEnd && offset.value() < view.mRangeEnd) {
      aRv.ThrowOperationError(
          "GetMappedRange range overlaps with existing buffer view");
      return;
    }
  }

  // 3. Let `data` be `this.[[mapping]].data`.
  //
  // The creation of a *pointer to* a `shared_ptr` here seems redundant but is
  // unfortunately necessary: `JS::BufferContentsDeleter` requires that its
  // `userData` be a `void*`, and while `shared_ptr` can't be inter-converted
  // with `void*` (it's actually two pointers), `shared_ptr*` obviously can.
  std::shared_ptr<ipc::SharedMemoryMapping>* data =
      new std::shared_ptr<ipc::SharedMemoryMapping>(mShmem);

  // 4. Let `view` be (potentially fallible operation follows) create an
  //    `ArrayBuffer` of size `rangeSize`, but with its pointer mutably
  //    referencing the content of `data` at offset `(offset -
  //    [[mapping]].range[0])`.
  //
  // Since `size_t` may not be the same as `uint64_t`, check, convert, and check
  // again. `CheckedInt<size_t>(x)` produces an invalid value if `x` is not in
  // range for `size_t` before any conversion is performed.
  const auto checkedSize = CheckedInt<size_t>(rangeSize.value()).value();
  const auto checkedOffset = CheckedInt<size_t>(offset.value()).value();
  const auto span =
      (*data)->DataAsSpan<uint8_t>().Subspan(checkedOffset, checkedSize);
  UniquePtr<void, JS::BufferContentsDeleter> contents{
      span.data(), {&ExternalBufferFreeCallback, data}};
  JS::Rooted<JSObject*> view(
      aCx, JS::NewExternalArrayBuffer(aCx, checkedSize, std::move(contents)));
  if (!view) {
    aRv.NoteJSContextException(aCx);
    return;
  }

  aObject->set(view);
  mMapped->mViews.AppendElement(
      MappedView({checkedOffset, rangeEnd, *aObject}));
}

void Buffer::UnmapArrayBuffers(JSContext* aCx, ErrorResult& aRv) {
  MOZ_ASSERT(mMapped);

  bool detachedArrayBuffers = true;
  for (const auto& view : mMapped->mViews) {
    JS::Rooted<JSObject*> rooted(aCx, view.mArrayBuffer);
    if (!JS::DetachArrayBuffer(aCx, rooted)) {
      detachedArrayBuffers = false;
    }
  };

  mMapped->mViews.Clear();

  if (NS_WARN_IF(!detachedArrayBuffers)) {
    aRv.NoteJSContextException(aCx);
    return;
  }
}

void Buffer::ResolveMapRequest(dom::Promise* aPromise, BufferAddress aOffset,
                               BufferAddress aSize, bool aWritable) {
  if (mMapRequest != aPromise) {
    // The map request has been cancelled by unmap().
    return;
  }
  SetMapped(aOffset, aSize, aWritable);
  promise::MaybeResolveWithUndefined(std::move(mMapRequest));
}

void Buffer::RejectMapRequest(dom::Promise* aPromise,
                              const nsACString& message) {
  if (mMapRequest != aPromise) {
    // The map request has been cancelled by unmap().
    return;
  }
  promise::MaybeRejectWithOperationError(std::move(mMapRequest),
                                         nsCString(message));
}

void Buffer::RejectMapRequestWithAbortError(dom::Promise* aPromise) {
  if (mMapRequest != aPromise) {
    // The map request has been cancelled by unmap().
    return;
  }
  AbortMapRequest();
}

void Buffer::AbortMapRequest() {
  if (mMapRequest) {
    promise::MaybeRejectWithAbortError(std::move(mMapRequest),
                                       nsCString("Buffer unmapped"));
  }
}

void Buffer::Unmap(JSContext* aCx, ErrorResult& aRv) {
  AbortMapRequest();

  if (!mMapped) {
    ffi::wgpu_client_buffer_unmap(GetClient(), mParent->GetId(), GetId(),
                                  false);
    return;
  }

  UnmapArrayBuffers(aCx, aRv);

  bool hasMapFlags = mUsage & (dom::GPUBufferUsage_Binding::MAP_WRITE |
                               dom::GPUBufferUsage_Binding::MAP_READ);

  if (!hasMapFlags) {
    // We get here if the buffer was mapped at creation without map flags.
    // It won't be possible to map the buffer again so we can get rid of
    // our shmem on this side.
    mShmem = std::make_shared<ipc::SharedMemoryMapping>();
  }

  ffi::wgpu_client_buffer_unmap(GetClient(), mParent->GetId(), GetId(),
                                mMapped->mWritable);

  mMapped.reset();
}

void Buffer::Destroy(JSContext* aCx, ErrorResult& aRv) {
  Unmap(aCx, aRv);

  ffi::wgpu_client_destroy_buffer(GetClient(), GetId());
}

dom::GPUBufferMapState Buffer::MapState() const {
  // Implementation reference:
  // <https://gpuweb.github.io/gpuweb/#dom-gpubuffer-mapstate>.

  if (mMapped) {
    return dom::GPUBufferMapState::Mapped;
  }
  if (mMapRequest) {
    return dom::GPUBufferMapState::Pending;
  }
  return dom::GPUBufferMapState::Unmapped;
}

}  // namespace mozilla::webgpu
