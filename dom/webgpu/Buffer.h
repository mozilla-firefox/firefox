/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GPU_BUFFER_H_
#define GPU_BUFFER_H_

#include <memory>

#include "ObjectModel.h"
#include "js/RootingAPI.h"
#include "mozilla/dom/Nullable.h"
#include "mozilla/ipc/SharedMemoryMapping.h"
#include "mozilla/webgpu/WebGPUTypes.h"
#include "nsTArray.h"

namespace mozilla {
namespace webgpu {
struct MappedView;
}  // namespace webgpu
}  // namespace mozilla

// Give `nsTArray` some advice on how to handle `MappedInfo::mViews`.
//
// In the `mozilla::webgpu` namespace, `MappedInfo::mViews` is an
// `nsTArray<MappedView>`, and `MappedView::mArrayBuffer` is a `JS::Heap`
// pointer. This arrangement requires special handling.
//
// Normally, `nsTArray` wants its element type to be movable with simple byte
// copies, so that an `nsTArray` can efficiently resize its element buffer.
// However, `JS::Heap` is marked `MOZ_NON_MEMMOVABLE`, meaning that it cannot be
// safely moved by a simple byte-by-byte copy. Normally, this would cause
// `nsTArray` to reject `JS::Heap` as an element type, but `nsTArray.h`
// specializes `nsTArray_RelocationStrategy` to indicate that `JS::Heap` can be
// moved safely using its move constructor. This causes `nsTArray<JS::Heap<T>>`
// to perform element buffer moves using element-by-element move constructor
// application: slower, but safe for `JS::Heap`.
//
// However, while `MappedView` is automatically marked `MOZ_NON_MEMMOVABLE`
// because of its `mArrayBuffer` member, the `nsTArray_RelocationStrategy`
// specialization is not somehow similarly magically carried over from
// `JS::Heap` to `MappedView`. To use `MappedView` in `nsTArray`, we must spell
// out a relocation strategy for it.
template <>
struct nsTArray_RelocationStrategy<mozilla::webgpu::MappedView> {
  // The default move constructors are fine for MappedView.
  using Type =
      nsTArray_RelocateUsingMoveConstructor<mozilla::webgpu::MappedView>;
};

namespace mozilla {
class ErrorResult;

namespace dom {
struct GPUBufferDescriptor;
template <typename T>
class Optional;
enum class GPUBufferMapState : uint8_t;
}  // namespace dom

namespace webgpu {

class Device;

// A portion of the current mapped buffer range that is currently
// visible to JS as an ArrayBuffer.
struct MappedView {
  BufferAddress mOffset;
  BufferAddress mRangeEnd;
  JS::Heap<JSObject*> mArrayBuffer;

  MappedView(BufferAddress aOffset, BufferAddress aRangeEnd,
             JSObject* aArrayBuffer)
      : mOffset(aOffset), mRangeEnd(aRangeEnd), mArrayBuffer(aArrayBuffer) {}
};

struct MappedInfo {
  // True if mapping is requested for writing.
  bool mWritable = false;
  // Populated by `GetMappedRange`.
  nsTArray<MappedView> mViews;
  BufferAddress mOffset;
  BufferAddress mSize;
  MappedInfo() = default;
  MappedInfo(const MappedInfo&) = delete;
};

class Buffer final : public nsWrapperCache,
                     public ObjectBase,
                     public ChildOf<Device> {
 public:
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(Buffer)
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(Buffer)
  GPU_DECL_JS_WRAP(Buffer)

  static already_AddRefed<Buffer> Create(Device* aDevice, RawId aDeviceId,
                                         const dom::GPUBufferDescriptor& aDesc,
                                         ErrorResult& aRv);

  already_AddRefed<dom::Promise> MapAsync(uint32_t aMode, uint64_t aOffset,
                                          const dom::Optional<uint64_t>& aSize,
                                          ErrorResult& aRv);
  void GetMappedRange(JSContext* aCx, uint64_t aOffset,
                      const dom::Optional<uint64_t>& aSize,
                      JS::Rooted<JSObject*>* aObject, ErrorResult& aRv);
  void Unmap(JSContext* aCx, ErrorResult& aRv);
  void Destroy(JSContext* aCx, ErrorResult& aRv);

  uint64_t Size() const { return mSize; }
  uint32_t Usage() const { return mUsage; }
  dom::GPUBufferMapState MapState() const;

  void ResolveMapRequest(dom::Promise* aPromise, BufferAddress aOffset,
                         BufferAddress aSize, bool aWritable);
  void RejectMapRequest(dom::Promise* aPromise, const nsACString& message);
  void RejectMapRequestWithAbortError(dom::Promise* aPromise);

 private:
  Buffer(Device* const aParent, RawId aId, BufferAddress aSize, uint32_t aUsage,
         mozilla::ipc::SharedMemoryMapping&& aShmem);
  virtual ~Buffer();
  void Cleanup();
  void UnmapArrayBuffers(JSContext* aCx, ErrorResult& aRv);
  void AbortMapRequest();
  void SetMapped(BufferAddress aOffset, BufferAddress aSize, bool aWritable);

  bool mValid = true;
  // Note: we can't map a buffer with the size that don't fit into `size_t`
  // (which may be smaller than `BufferAddress`), but general not all buffers
  // are mapped.
  const BufferAddress mSize;
  const uint32_t mUsage;
  nsString mLabel;
  // Information about the currently active mapping.
  Maybe<MappedInfo> mMapped;
  RefPtr<dom::Promise> mMapRequest;

  // A shared memory mapping for the entire buffer, or a zero-length
  // mapping.
  //
  // If `mUsage` contains `MAP_READ` or `MAP_WRITE`, this mapping is
  // created at `Buffer` construction, and destroyed at `Buffer`
  // destruction.
  //
  // If `mUsage` contains neither of those flags, but `this` is mapped
  // at creation, this mapping is created at `Buffer` construction,
  // and destroyed when we first unmap the buffer, by clearing this
  // `shared_ptr`.
  //
  // Otherwise, this points to `SharedMemoryMapping()` (the default
  // constructor), a zero-length mapping that doesn't point to any shared
  // memory.
  std::shared_ptr<mozilla::ipc::SharedMemoryMapping> mShmem;
};

}  // namespace webgpu
}  // namespace mozilla

#endif  // GPU_BUFFER_H_
