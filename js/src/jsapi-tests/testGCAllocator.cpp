/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/MathAlgorithms.h"

#include <cstdlib>

#include "gc/Allocator.h"
#include "gc/BufferAllocatorInternals.h"
#include "gc/Memory.h"
#include "gc/Nursery.h"
#include "gc/Zone.h"
#include "js/GCVector.h"
#include "jsapi-tests/tests.h"
#include "util/RandomSeed.h"
#include "vm/PlainObject.h"

#include "gc/BufferAllocator-inl.h"

#if defined(XP_WIN)
// clang-format off
#  include "util/WindowsWrapper.h"
#  include <psapi.h>
// clang-format on
#elif defined(__wasi__)
// Nothing.
#else
#  include <algorithm>
#  include <errno.h>
#  include <sys/mman.h>
#  include <sys/resource.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

#include "gc/BufferAllocator-inl.h"
#include "gc/StoreBuffer-inl.h"
#include "vm/JSContext-inl.h"
#include "vm/JSObject-inl.h"

using namespace js;
using namespace js::gc;

BEGIN_TEST(testGCAllocator) {
#ifdef JS_64BIT
  // If we're using the scattershot allocator, this test does not apply.
  if (js::gc::UsingScattershotAllocator()) {
    return true;
  }
#endif

  size_t PageSize = js::gc::SystemPageSize();

  /* Finish any ongoing background free activity. */
  js::gc::FinishGC(cx);

  bool growUp = false;
  CHECK(addressesGrowUp(&growUp));

  if (growUp) {
    return testGCAllocatorUp(PageSize);
  } else {
    return testGCAllocatorDown(PageSize);
  }
}

static const size_t Chunk = 512 * 1024;
static const size_t Alignment = 2 * Chunk;
static const int MaxTempChunks = 4096;
static const size_t StagingSize = 16 * Chunk;

bool addressesGrowUp(bool* resultOut) {
  /*
   * Try to detect whether the OS allocates memory in increasing or decreasing
   * address order by making several allocations and comparing the addresses.
   */

  static const unsigned ChunksToTest = 20;
  static const int ThresholdCount = 15;

  void* chunks[ChunksToTest];
  for (unsigned i = 0; i < ChunksToTest; i++) {
    chunks[i] = mapMemory(2 * Chunk);
    CHECK(chunks[i]);
  }

  int upCount = 0;
  int downCount = 0;

  for (unsigned i = 0; i < ChunksToTest - 1; i++) {
    if (chunks[i] < chunks[i + 1]) {
      upCount++;
    } else {
      downCount++;
    }
  }

  for (unsigned i = 0; i < ChunksToTest; i++) {
    unmapPages(chunks[i], 2 * Chunk);
  }

  /* Check results were mostly consistent. */
  CHECK(abs(upCount - downCount) >= ThresholdCount);

  *resultOut = upCount > downCount;

  return true;
}

size_t offsetFromAligned(void* p) { return uintptr_t(p) % Alignment; }

enum AllocType { UseNormalAllocator, UseLastDitchAllocator };

bool testGCAllocatorUp(const size_t PageSize) {
  const size_t UnalignedSize = StagingSize + Alignment - PageSize;
  void* chunkPool[MaxTempChunks];
  // Allocate a contiguous chunk that we can partition for testing.
  void* stagingArea = mapMemory(UnalignedSize);
  if (!stagingArea) {
    return false;
  }
  // Ensure that the staging area is aligned.
  unmapPages(stagingArea, UnalignedSize);
  if (offsetFromAligned(stagingArea)) {
    const size_t Offset = offsetFromAligned(stagingArea);
    // Place the area at the lowest aligned address.
    stagingArea = (void*)(uintptr_t(stagingArea) + (Alignment - Offset));
  }
  mapMemoryAt(stagingArea, StagingSize);
  // Make sure there are no available chunks below the staging area.
  int tempChunks;
  if (!fillSpaceBeforeStagingArea(tempChunks, stagingArea, chunkPool, false)) {
    return false;
  }
  // Unmap the staging area so we can set it up for testing.
  unmapPages(stagingArea, StagingSize);
  // Check that the first chunk is used if it is aligned.
  CHECK(positionIsCorrect("xxooxxx---------", stagingArea, chunkPool,
                          tempChunks));
  // Check that the first chunk is used if it can be aligned.
  CHECK(positionIsCorrect("x-ooxxx---------", stagingArea, chunkPool,
                          tempChunks));
  // Check that an aligned chunk after a single unalignable chunk is used.
  CHECK(positionIsCorrect("x--xooxxx-------", stagingArea, chunkPool,
                          tempChunks));
  // Check that we fall back to the slow path after two unalignable chunks.
  CHECK(positionIsCorrect("x--xx--xoo--xxx-", stagingArea, chunkPool,
                          tempChunks));
  // Check that we also fall back after an unalignable and an alignable chunk.
  CHECK(positionIsCorrect("x--xx---x-oo--x-", stagingArea, chunkPool,
                          tempChunks));
  // Check that the last ditch allocator works as expected.
  CHECK(positionIsCorrect("x--xx--xx-oox---", stagingArea, chunkPool,
                          tempChunks, UseLastDitchAllocator));
  // Check that the last ditch allocator can deal with naturally aligned chunks.
  CHECK(positionIsCorrect("x--xx--xoo------", stagingArea, chunkPool,
                          tempChunks, UseLastDitchAllocator));

  // Clean up.
  while (--tempChunks >= 0) {
    unmapPages(chunkPool[tempChunks], 2 * Chunk);
  }
  return true;
}

bool testGCAllocatorDown(const size_t PageSize) {
  const size_t UnalignedSize = StagingSize + Alignment - PageSize;
  void* chunkPool[MaxTempChunks];
  // Allocate a contiguous chunk that we can partition for testing.
  void* stagingArea = mapMemory(UnalignedSize);
  if (!stagingArea) {
    return false;
  }
  // Ensure that the staging area is aligned.
  unmapPages(stagingArea, UnalignedSize);
  if (offsetFromAligned(stagingArea)) {
    void* stagingEnd = (void*)(uintptr_t(stagingArea) + UnalignedSize);
    const size_t Offset = offsetFromAligned(stagingEnd);
    // Place the area at the highest aligned address.
    stagingArea = (void*)(uintptr_t(stagingEnd) - Offset - StagingSize);
  }
  mapMemoryAt(stagingArea, StagingSize);
  // Make sure there are no available chunks above the staging area.
  int tempChunks;
  if (!fillSpaceBeforeStagingArea(tempChunks, stagingArea, chunkPool, true)) {
    return false;
  }
  // Unmap the staging area so we can set it up for testing.
  unmapPages(stagingArea, StagingSize);
  // Check that the first chunk is used if it is aligned.
  CHECK(positionIsCorrect("---------xxxooxx", stagingArea, chunkPool,
                          tempChunks));
  // Check that the first chunk is used if it can be aligned.
  CHECK(positionIsCorrect("---------xxxoo-x", stagingArea, chunkPool,
                          tempChunks));
  // Check that an aligned chunk after a single unalignable chunk is used.
  CHECK(positionIsCorrect("-------xxxoox--x", stagingArea, chunkPool,
                          tempChunks));
  // Check that we fall back to the slow path after two unalignable chunks.
  CHECK(positionIsCorrect("-xxx--oox--xx--x", stagingArea, chunkPool,
                          tempChunks));
  // Check that we also fall back after an unalignable and an alignable chunk.
  CHECK(positionIsCorrect("-x--oo-x---xx--x", stagingArea, chunkPool,
                          tempChunks));
  // Check that the last ditch allocator works as expected.
  CHECK(positionIsCorrect("---xoo-xx--xx--x", stagingArea, chunkPool,
                          tempChunks, UseLastDitchAllocator));
  // Check that the last ditch allocator can deal with naturally aligned chunks.
  CHECK(positionIsCorrect("------oox--xx--x", stagingArea, chunkPool,
                          tempChunks, UseLastDitchAllocator));

  // Clean up.
  while (--tempChunks >= 0) {
    unmapPages(chunkPool[tempChunks], 2 * Chunk);
  }
  return true;
}

bool fillSpaceBeforeStagingArea(int& tempChunks, void* stagingArea,
                                void** chunkPool, bool addressesGrowDown) {
  // Make sure there are no available chunks before the staging area.
  tempChunks = 0;
  chunkPool[tempChunks++] = mapMemory(2 * Chunk);
  while (tempChunks < MaxTempChunks && chunkPool[tempChunks - 1] &&
         (chunkPool[tempChunks - 1] < stagingArea) ^ addressesGrowDown) {
    chunkPool[tempChunks++] = mapMemory(2 * Chunk);
    if (!chunkPool[tempChunks - 1]) {
      break;  // We already have our staging area, so OOM here is okay.
    }
    if ((chunkPool[tempChunks - 1] < chunkPool[tempChunks - 2]) ^
        addressesGrowDown) {
      break;  // The address growth direction is inconsistent!
    }
  }
  // OOM also means success in this case.
  if (!chunkPool[tempChunks - 1]) {
    --tempChunks;
    return true;
  }
  // Bail if we can't guarantee the right address space layout.
  if ((chunkPool[tempChunks - 1] < stagingArea) ^ addressesGrowDown ||
      (tempChunks > 1 &&
       (chunkPool[tempChunks - 1] < chunkPool[tempChunks - 2]) ^
           addressesGrowDown)) {
    while (--tempChunks >= 0) {
      unmapPages(chunkPool[tempChunks], 2 * Chunk);
    }
    unmapPages(stagingArea, StagingSize);
    return false;
  }
  return true;
}

bool positionIsCorrect(const char* str, void* base, void** chunkPool,
                       int tempChunks,
                       AllocType allocator = UseNormalAllocator) {
  // str represents a region of memory, with each character representing a
  // region of Chunk bytes. str should contain only x, o and -, where
  // x = mapped by the test to set up the initial conditions,
  // o = mapped by the GC allocator, and
  // - = unmapped.
  // base should point to a region of contiguous free memory
  // large enough to hold strlen(str) chunks of Chunk bytes.
  int len = strlen(str);
  int i;
  // Find the index of the desired address.
  for (i = 0; i < len && str[i] != 'o'; ++i);
  void* desired = (void*)(uintptr_t(base) + i * Chunk);
  // Map the regions indicated by str.
  for (i = 0; i < len; ++i) {
    if (str[i] == 'x') {
      mapMemoryAt((void*)(uintptr_t(base) + i * Chunk), Chunk);
    }
  }
  // Allocate using the GC's allocator.
  void* result;
  if (allocator == UseNormalAllocator) {
    result = js::gc::MapAlignedPages(2 * Chunk, Alignment);
  } else {
    result = js::gc::TestMapAlignedPagesLastDitch(2 * Chunk, Alignment);
  }
  // Clean up the mapped regions.
  if (result) {
    js::gc::UnmapPages(result, 2 * Chunk);
  }
  for (--i; i >= 0; --i) {
    if (str[i] == 'x') {
      unmapPages((void*)(uintptr_t(base) + i * Chunk), Chunk);
    }
  }
  // CHECK returns, so clean up on failure.
  if (result != desired) {
    while (--tempChunks >= 0) {
      unmapPages(chunkPool[tempChunks], 2 * Chunk);
    }
  }
  return result == desired;
}

#if defined(XP_WIN)

void* mapMemoryAt(void* desired, size_t length) {
  return VirtualAlloc(desired, length, MEM_COMMIT | MEM_RESERVE,
                      PAGE_READWRITE);
}

void* mapMemory(size_t length) {
  return VirtualAlloc(nullptr, length, MEM_COMMIT | MEM_RESERVE,
                      PAGE_READWRITE);
}

void unmapPages(void* p, size_t size) {
  MOZ_ALWAYS_TRUE(VirtualFree(p, 0, MEM_RELEASE));
}

#elif defined(__wasi__)

void* mapMemoryAt(void* desired, size_t length) { return nullptr; }

void* mapMemory(size_t length) {
  void* addr = nullptr;
  if (int err = posix_memalign(&addr, js::gc::SystemPageSize(), length)) {
    MOZ_ASSERT(err == ENOMEM);
  }
  MOZ_ASSERT(addr);
  memset(addr, 0, length);
  return addr;
}

void unmapPages(void* p, size_t size) { free(p); }

#else

void* mapMemoryAt(void* desired, size_t length) {
  void* region = mmap(desired, length, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANON, -1, 0);
  if (region == MAP_FAILED) {
    return nullptr;
  }
  if (region != desired) {
    if (munmap(region, length)) {
      MOZ_RELEASE_ASSERT(errno == ENOMEM);
    }
    return nullptr;
  }
  return region;
}

void* mapMemory(size_t length) {
  int prot = PROT_READ | PROT_WRITE;
  int flags = MAP_PRIVATE | MAP_ANON;
  int fd = -1;
  off_t offset = 0;
  void* region = mmap(nullptr, length, prot, flags, fd, offset);
  if (region == MAP_FAILED) {
    return nullptr;
  }
  return region;
}

void unmapPages(void* p, size_t size) {
  if (munmap(p, size)) {
    MOZ_RELEASE_ASSERT(errno == ENOMEM);
  }
}

#endif

END_TEST(testGCAllocator)

static size_t SomeAllocSizes[] = {16,
                                  17,
                                  31,
                                  32,
                                  100,
                                  200,
                                  240,
                                  256,
                                  1000,
                                  3000,
                                  3968,
                                  4096,
                                  5000,
                                  16 * 1024,
                                  100 * 1024,
                                  255 * 1024,
                                  257 * 1024,
                                  600 * 1024,
                                  MaxMediumAllocSize,
                                  MaxMediumAllocSize + 1,
                                  1020 * 1024,
                                  1 * 1024 * 1024,
                                  3 * 1024 * 1024,
                                  10 * 1024 * 1024};

static void WriteAllocData(void* alloc, size_t bytes) {
  auto* data = reinterpret_cast<uint32_t*>(alloc);
  size_t length = std::min(bytes / sizeof(uint32_t), size_t(4096));
  for (size_t i = 0; i < length; i++) {
    data[i] = i;
  }
}

static bool CheckAllocData(void* alloc, size_t bytes) {
  const auto* data = reinterpret_cast<uint32_t*>(alloc);
  size_t length = std::min(bytes / sizeof(uint32_t), size_t(4096));
  for (size_t i = 0; i < length; i++) {
    if (data[i] != i) {
      return false;
    }
  }
  return true;
}

class BufferHolderObject : public NativeObject {
 public:
  static const JSClass class_;

  static BufferHolderObject* create(JSContext* cx, size_t count = 1);

  void setBuffer(void* buffer, size_t index = 0) { buffers()[index] = buffer; }

  void prepareForPostBarrier(JSContext* cx);
  void postBarrier(JSContext* cx);

  using BufferVector = Vector<void*, 1, SystemAllocPolicy>;
  BufferVector& buffers() {
    return *reinterpret_cast<BufferVector*>(getFixedSlot(0).toPrivate());
  }

 private:
  static const JSClassOps classOps_;

  static void trace(JSTracer* trc, JSObject* obj);
};

const JSClass BufferHolderObject::class_ = {"BufferHolderObject",
                                            JSCLASS_HAS_RESERVED_SLOTS(1),
                                            &BufferHolderObject::classOps_};

const JSClassOps BufferHolderObject::classOps_ = {
    .trace = BufferHolderObject::trace,
};

/* static */
BufferHolderObject* BufferHolderObject::create(JSContext* cx, size_t count) {
  auto buffers = MakeUnique<BufferVector>();
  if (!buffers || !buffers->resize(count)) {
    return nullptr;
  }

  for (auto& buffer : *buffers) {
    buffer = nullptr;
  }

  NativeObject* obj = NewObjectWithGivenProto(cx, &class_, nullptr);
  if (!obj) {
    return nullptr;
  }

  obj->setFixedSlot(0, PrivateValue(buffers.release()));
  return &obj->as<BufferHolderObject>();
}

// Buffer holder post barrier. Used when adding a nursery-owned buffer to the
// holder.
//
// This is a hack to allow us to test allocation of 'nursery-owned' buffers even
// though they are not really owned by a nursery GC thing. This works by adding
// a whole cell store buffer entry to trace all buffers on minor GC. For that to
// happen we also need to ensure the nursery is not empty, and that has to
// happen before allocating any nursery owned buffers.
void BufferHolderObject::prepareForPostBarrier(JSContext* cx) {
  Nursery& nursery = cx->runtime()->gc.nursery();
  if (nursery.minorGCRequested()) {
    cx->minorGC(JS::GCReason::API);
  }
  if (nursery.isEmpty()) {
    MOZ_RELEASE_ASSERT(NewPlainObject(cx));
  }
  MOZ_RELEASE_ASSERT(!nursery.isEmpty());
  MOZ_RELEASE_ASSERT(!nursery.minorGCRequested());
}
void BufferHolderObject::postBarrier(JSContext* cx) {
  GCRuntime* gc = &cx->runtime()->gc;
  MOZ_RELEASE_ASSERT(!gc->nursery().isEmpty());
  if (isTenured()) {
    gc->storeBuffer().putWholeCell(this);
  }
}

/* static */
void BufferHolderObject::trace(JSTracer* trc, JSObject* obj) {
  auto* holder = &obj->as<BufferHolderObject>();
  for (auto& buffer : holder->buffers()) {
    TraceBufferEdge(trc, &buffer, "BufferHolderObject buffer");
  }
}

namespace js::gc {
size_t TestGetAllocSizeKind(void* alloc) {
  if (BufferAllocator::IsLargeAlloc(alloc)) {
    return 2;
  }
  if (BufferAllocator::IsMediumAlloc(alloc)) {
    return 1;
  }
  MOZ_RELEASE_ASSERT(BufferAllocator::IsSmallAlloc(alloc));
  return 0;
}
}  // namespace js::gc

BEGIN_TEST(testBufferAllocator_API) {
  AutoLeaveZeal leaveZeal(cx);

  Rooted<BufferHolderObject*> holder(cx, BufferHolderObject::create(cx));
  CHECK(holder);

  JS::NonIncrementalGC(cx, JS::GCOptions::Shrink, JS::GCReason::API);

  Zone* zone = cx->zone();
  size_t initialGCHeapSize = zone->gcHeapSize.bytes();
  size_t initialMallocHeapSize = zone->mallocHeapSize.bytes();

  for (size_t requestSize : SomeAllocSizes) {
    size_t goodSize = GetGoodAllocSize(requestSize);

    size_t wastage = goodSize - requestSize;
    double fraction = double(wastage) / double(goodSize);
    fprintf(stderr, "%8zu -> %8zu %7zu (%3.1f%%)\n", requestSize, goodSize,
            wastage, fraction * 100.0);

    CHECK(goodSize >= requestSize);
    if (requestSize > 64) {
      CHECK(goodSize < 2 * requestSize);
    }
    CHECK(GetGoodAllocSize(goodSize) == goodSize);

    // Check we don't waste space requesting 1MB aligned sizes.
    if (requestSize >= ChunkSize) {
      CHECK(goodSize == RoundUp(requestSize, ChunkSize));
    }

    for (bool nurseryOwned : {true, false}) {
      if (nurseryOwned) {
        holder->prepareForPostBarrier(cx);
      }
      void* alloc = AllocBuffer(zone, requestSize, nurseryOwned);
      CHECK(alloc);

      CHECK(IsGCPointerBufferAlloc(alloc));
      size_t actualSize = GetAllocSize(zone, alloc);
      CHECK(actualSize == GetGoodAllocSize(requestSize));

      CHECK(IsNurseryOwned(zone, alloc) == nurseryOwned);

      size_t expectedKind;
      if (goodSize >= MinLargeAllocSize) {
        expectedKind = 2;
      } else if (goodSize >= MinMediumAllocSize) {
        expectedKind = 1;
      } else {
        expectedKind = 0;
      }
      CHECK(TestGetAllocSizeKind(alloc) == expectedKind);

      WriteAllocData(alloc, actualSize);
      CHECK(CheckAllocData(alloc, actualSize));

      CHECK(!IsBufferAllocMarkedBlack(zone, alloc));

      gc::WaitForBackgroundTasks(cx);
      CHECK(cx->runtime()->gc.isPointerWithinBufferAlloc(alloc));
      void* ptr = reinterpret_cast<void*>(uintptr_t(alloc) + 8);
      CHECK(cx->runtime()->gc.isPointerWithinBufferAlloc(ptr));

      holder->setBuffer(alloc);
      if (nurseryOwned) {
        holder->postBarrier(cx);
      }
      JS_GC(cx);

      // Post GC marking state depends on whether allocation is small or not.
      // Small allocations will remain marked whereas others will have their
      // mark state cleared.

      CHECK(CheckAllocData(alloc, actualSize));

      holder->setBuffer(nullptr);
      JS_GC(cx);

      CHECK(zone->gcHeapSize.bytes() == initialGCHeapSize);
      CHECK(zone->mallocHeapSize.bytes() == initialMallocHeapSize);
    }
  }

  return true;
}
END_TEST(testBufferAllocator_API)

BEGIN_TEST(testBufferAllocator_largeAllocOverflow) {
  AutoLeaveZeal leaveZeal(cx);

  JS::NonIncrementalGC(cx, JS::GCOptions::Shrink, JS::GCReason::API);

  Zone* zone = cx->zone();
  size_t initialGCHeapSize = zone->gcHeapSize.bytes();
  size_t initialMallocHeapSize = zone->mallocHeapSize.bytes();

  CHECK(AllocBuffer(zone, size_t(-1), false) == nullptr);
  CHECK(zone->gcHeapSize.bytes() == initialGCHeapSize);
  CHECK(zone->mallocHeapSize.bytes() == initialMallocHeapSize);

  return true;
}
END_TEST(testBufferAllocator_largeAllocOverflow)

BEGIN_TEST(testBufferAllocator_realloc) {
  AutoLeaveZeal leaveZeal(cx);

  Rooted<BufferHolderObject*> holder(cx, BufferHolderObject::create(cx));
  CHECK(holder);

  JS::NonIncrementalGC(cx, JS::GCOptions::Shrink, JS::GCReason::API);

  Zone* zone = cx->zone();
  size_t initialGCHeapSize = zone->gcHeapSize.bytes();
  size_t initialMallocHeapSize = zone->mallocHeapSize.bytes();

  for (bool nurseryOwned : {false, true}) {
    for (size_t requestSize : SomeAllocSizes) {
      if (nurseryOwned && requestSize < Nursery::MaxNurseryBufferSize) {
        continue;
      }

      // Realloc nullptr.
      void* alloc = ReallocBuffer(zone, nullptr, requestSize, nurseryOwned);
      CHECK(alloc);
      CHECK(IsGCPointerBufferAlloc(alloc));
      CHECK(IsNurseryOwned(zone, alloc) == nurseryOwned);
      size_t actualSize = GetAllocSize(zone, alloc);
      WriteAllocData(alloc, actualSize);
      holder->setBuffer(alloc);

      // Realloc to same size.
      void* prev = alloc;
      alloc = ReallocBuffer(zone, alloc, requestSize, nurseryOwned);
      CHECK(alloc);
      CHECK(alloc == prev);
      CHECK(actualSize == GetAllocSize(zone, alloc));
      CHECK(IsNurseryOwned(zone, alloc) == nurseryOwned);
      CHECK(CheckAllocData(alloc, actualSize));

      // Grow.
      size_t newSize = requestSize + requestSize / 2;
      alloc = ReallocBuffer(zone, alloc, newSize, nurseryOwned);
      CHECK(alloc);
      CHECK(IsNurseryOwned(zone, alloc) == nurseryOwned);
      CHECK(CheckAllocData(alloc, actualSize));

      // Shrink.
      newSize = newSize / 2;
      alloc = ReallocBuffer(zone, alloc, newSize, nurseryOwned);
      CHECK(alloc);
      CHECK(IsNurseryOwned(zone, alloc) == nurseryOwned);
      actualSize = GetAllocSize(zone, alloc);
      CHECK(CheckAllocData(alloc, actualSize));

      // Free.
      holder->setBuffer(nullptr);
      FreeBuffer(zone, alloc);
    }

    NewPlainObject(cx);  // Force minor GC.
    JS_GC(cx);
  }

  CHECK(zone->gcHeapSize.bytes() == initialGCHeapSize);
  CHECK(zone->mallocHeapSize.bytes() == initialMallocHeapSize);

  return true;
}
END_TEST(testBufferAllocator_realloc)

BEGIN_TEST(testBufferAllocator_reallocInPlace) {
  AutoLeaveZeal leaveZeal(cx);

  Rooted<BufferHolderObject*> holder(cx, BufferHolderObject::create(cx));
  CHECK(holder);

  JS::NonIncrementalGC(cx, JS::GCOptions::Shrink, JS::GCReason::API);

  Zone* zone = cx->zone();
  size_t initialGCHeapSize = zone->gcHeapSize.bytes();
  size_t initialMallocHeapSize = zone->mallocHeapSize.bytes();

  // Check that we resize some buffers in place if the sizes allow.

  // Grow medium -> medium: supported if free space after allocation
  // We should be able to grow in place if it's the last thing allocated.
  // *** If this starts failing we may need to allocate a new zone ***
  size_t bytes = MinMediumAllocSize;
  CHECK(TestRealloc(bytes, bytes * 2, true));

  // Shrink medium -> medium: supported
  CHECK(TestRealloc(bytes * 2, bytes, true));

  // Grow large -> large: not supported
  bytes = MinLargeAllocSize;
  CHECK(TestRealloc(bytes, 2 * bytes, false));

  // Shrink large -> large: supported on non-Windows platforms
#ifdef XP_WIN
  CHECK(TestRealloc(2 * bytes, bytes, false));
#else
  CHECK(TestRealloc(2 * bytes, bytes, true));
#endif

  JS_GC(cx);
  CHECK(zone->gcHeapSize.bytes() == initialGCHeapSize);
  CHECK(zone->mallocHeapSize.bytes() == initialMallocHeapSize);

  return true;
}

bool TestRealloc(size_t fromSize, size_t toSize, bool expectedInPlace) {
  fprintf(stderr, "TestRealloc %zu -> %zu %u\n", fromSize, toSize,
          unsigned(expectedInPlace));

  Zone* zone = cx->zone();
  void* alloc = AllocBuffer(zone, fromSize, false);
  CHECK(alloc);

  void* newAlloc = ReallocBuffer(zone, alloc, toSize, false);
  CHECK(newAlloc);

  if (expectedInPlace) {
    CHECK(newAlloc == alloc);
  } else {
    CHECK(newAlloc != alloc);
  }

  FreeBuffer(zone, newAlloc);
  return true;
}
END_TEST(testBufferAllocator_reallocInPlace)

namespace js::gc {
void* TestAllocAligned(Zone* zone, size_t bytes) {
  return zone->bufferAllocator.allocMediumAligned(bytes, false, false);
}
}  // namespace js::gc

BEGIN_TEST(testBufferAllocator_alignedAlloc) {
  AutoLeaveZeal leaveZeal(cx);

  Rooted<BufferHolderObject*> holder(cx, BufferHolderObject::create(cx));
  CHECK(holder);

  JS::NonIncrementalGC(cx, JS::GCOptions::Shrink, JS::GCReason::API);

  Zone* zone = cx->zone();
  size_t initialGCHeapSize = zone->gcHeapSize.bytes();
  size_t initialMallocHeapSize = zone->mallocHeapSize.bytes();

  for (size_t requestSize = MinMediumAllocSize;
       requestSize <= MaxAlignedAllocSize; requestSize *= 2) {
    void* alloc = TestAllocAligned(zone, requestSize);
    CHECK(alloc);
    CHECK((uintptr_t(alloc) % requestSize) == 0);

    CHECK(IsGCPointerBufferAlloc(alloc));
    size_t actualSize = GetAllocSize(zone, alloc);
    CHECK(actualSize == requestSize);

    CHECK(!IsNurseryOwned(zone, alloc));
    FreeBuffer(zone, alloc);
  }

  JS_GC(cx);
  CHECK(zone->gcHeapSize.bytes() == initialGCHeapSize);
  CHECK(zone->mallocHeapSize.bytes() == initialMallocHeapSize);

  return true;
}
END_TEST(testBufferAllocator_alignedAlloc)

BEGIN_TEST(testBufferAllocator_rooting) {
  // Exercise RootedBuffer API to hold tenured-owned buffers live before
  // attaching them to a GC thing.

  const size_t bytes = 12 * 1024;  // Large enough to affect memory accounting.

  Zone* zone = cx->zone();
  size_t initialMallocHeapSize = zone->mallocHeapSize.bytes();

  auto* buffer = static_cast<uint8_t*>(gc::AllocBuffer(zone, bytes, false));
  CHECK(buffer);

  RootedBuffer<uint8_t> root(cx, buffer);
  buffer = nullptr;
  CHECK(root);
  CHECK(zone->mallocHeapSize.bytes() > initialMallocHeapSize);

  memset(root, 42, bytes);
  JS_GC(cx);
  CHECK(zone->mallocHeapSize.bytes() > initialMallocHeapSize);
  for (size_t i = 0; i < bytes; i++) {
    CHECK(root[i] == 42);
  }

  HandleBuffer<uint8_t> handle(root);
  CHECK(handle[0] == 42);

  MutableHandleBuffer<uint8_t> mutableHandle(&root);
  CHECK(mutableHandle[0] == 42);
  mutableHandle.set(nullptr);
  CHECK(!root);
  CHECK(!handle);

  JS_GC(cx);
  CHECK(zone->mallocHeapSize.bytes() == initialMallocHeapSize);

  return true;
}
END_TEST(testBufferAllocator_rooting)

BEGIN_TEST(testBufferAllocator_predicatesOnOtherAllocs) {
  if (!cx->runtime()->gc.nursery().isEnabled()) {
    fprintf(stderr, "Skipping test as nursery is disabled.\n");
  }

  AutoLeaveZeal leaveZeal(cx);

  JS_GC(cx);
  auto [buffer, isMalloced] = cx->nursery().allocNurseryOrMallocBuffer(
      cx->zone(), 256, js::MallocArena);
  CHECK(buffer);
  CHECK(!isMalloced);
  CHECK(cx->nursery().isInside(buffer));
  CHECK(!IsGCPointerBufferAlloc(buffer));

  RootedObject obj(cx, NewPlainObject(cx));
  CHECK(obj);
  CHECK(IsInsideNursery(obj));
  CHECK(!IsGCPointerBufferAlloc(obj));

  JS_GC(cx);
  CHECK(!IsInsideNursery(obj));
  CHECK(!IsGCPointerBufferAlloc(obj));

  // IsGCPointerBufferAlloc is not guaranteed to be correct for malloced memory.

  return true;
}
END_TEST(testBufferAllocator_predicatesOnOtherAllocs)

BEGIN_TEST(testBufferAllocator_stress) {
  AutoLeaveZeal leaveZeal(cx);

  unsigned seed = unsigned(GenerateRandomSeed());
  fprintf(stderr, "Random seed: 0x%x\n", seed);
  std::srand(seed);

  JS::NonIncrementalGC(cx, JS::GCOptions::Shrink, JS::GCReason::API);
  Zone* zone = cx->zone();

  size_t initialGCHeapSize = zone->gcHeapSize.bytes();
  size_t initialMallocHeapSize = zone->mallocHeapSize.bytes();

  Rooted<BufferHolderObject*> holder(
      cx, BufferHolderObject::create(cx, MaxLiveAllocs));
  CHECK(holder);

  AutoGCParameter setMaxHeap(cx, JSGC_MAX_BYTES, uint32_t(-1));
  AutoGCParameter param1(cx, JSGC_INCREMENTAL_GC_ENABLED, true);
  AutoGCParameter param2(cx, JSGC_PER_ZONE_GC_ENABLED, true);

#ifdef JS_GC_ZEAL
  JS::SetGCZeal(cx, 10, 50);
#endif

  BufferHolderObject::BufferVector& liveAllocs = holder->buffers();

  for (size_t i = 0; i < Iterations; i++) {
    holder->prepareForPostBarrier(cx);

    size_t index = std::rand() % MaxLiveAllocs;
    size_t bytes = randomSize();

    if (!liveAllocs[index]) {
      if ((std::rand() % 4) == 0 && bytes >= MinMediumAllocSize &&
          bytes <= ChunkSize / 4) {
        bytes = mozilla::RoundUpPow2(bytes);
        liveAllocs[index] = TestAllocAligned(zone, bytes);
      } else {
        bool nurseryOwned = (std::rand() % 2) == 0;
        liveAllocs[index] = AllocBuffer(zone, bytes, nurseryOwned);
        if (nurseryOwned) {
          holder->postBarrier(cx);
        }
      }
    } else {
      bool nurseryOwned = IsNurseryOwned(zone, liveAllocs[index]);
      void* ptr = ReallocBuffer(zone, liveAllocs[index], bytes, nurseryOwned);
      if (ptr) {
        liveAllocs[index] = ptr;
        if (nurseryOwned) {
          holder->postBarrier(cx);
        }
      }
    }

    index = std::rand() % MaxLiveAllocs;
    if (liveAllocs[index]) {
      if (std::rand() % 1) {
        FreeBuffer(zone, liveAllocs[index]);
      }
      liveAllocs[index] = nullptr;
    }

    // Trigger zeal GCs.
    NewPlainObject(cx);

    if ((i % 500) == 0) {
      // Trigger extra minor GCs.
      cx->minorGC(JS::GCReason::API);
    }
  }

  holder = nullptr;

#ifdef JS_GC_ZEAL
  JS::SetGCZeal(cx, 0, 100);
#endif

  JS::PrepareForFullGC(cx);
  JS::NonIncrementalGC(cx, JS::GCOptions::Shrink, JS::GCReason::API);

  CHECK(zone->gcHeapSize.bytes() == initialGCHeapSize);
  CHECK(zone->mallocHeapSize.bytes() == initialMallocHeapSize);

  return true;
}

static constexpr size_t Iterations = 50000;
static constexpr size_t MaxLiveAllocs = 500;

static size_t randomSize() {
  constexpr size_t Log2MinSize = 4;
  constexpr size_t Log2MaxSize = 22;  // Up to 4MB.

  double r = double(std::rand()) / double(RAND_MAX);
  double log2size = (Log2MaxSize - Log2MinSize) * r + Log2MinSize;
  MOZ_ASSERT(log2size <= Log2MaxSize);
  return size_t(std::pow(2.0, log2size));
}

static void traceAllocs(JSTracer* trc, void* data) {
  auto& holder = *static_cast<Rooted<PlainObject*>*>(data);
  auto* liveAllocs = static_cast<void**>(holder->getFixedSlot(0).toPrivate());
  for (size_t i = 0; i < MaxLiveAllocs; i++) {
    void** bufferp = &liveAllocs[i];
    if (*bufferp) {
      TraceBufferEdge(trc, bufferp, "test buffer");
    }
  }
}
END_TEST(testBufferAllocator_stress)

// Test using the buffer allocator for container with BufferAllocPolicy.

// A JS object that holds a buffer-allocated vector.
class VectorObject : public NativeObject {
 public:
  using VectorT =
      BufferAllocated<GCVector<HeapPtr<JSObject*>, 0, BufferAllocPolicy>>;

  enum { VectorSlot, SlotCount };

  static VectorObject* create(JSContext* cx, bool nurseryOwned) {
    NewObjectKind kind = nurseryOwned ? GenericObject : TenuredObject;
    auto* obj =
        NewObjectWithClassProto<VectorObject>(cx, nullptr, {.newKind = kind});
    if (!obj) {
      return nullptr;
    }

    VectorT* vector = NewBuffer<VectorT>(obj, BufferAllocPolicy(obj));
    if (!vector) {
      return nullptr;
    }

    InitBufferSlot(obj, VectorSlot, vector);
    return obj;
  }

  VectorT* getVector() {
    return static_cast<VectorT*>(getFixedSlot(VectorSlot).toPrivate());
  }

  size_t length() { return getVector()->length(); }

  JSObject* get(size_t index) { return (*getVector())[index]; }

  void check(bool expectNurseryOwned) {
    MOZ_RELEASE_ASSERT(IsInsideNursery(this) == expectNurseryOwned);

    VectorT* vector = getVector();
    MOZ_RELEASE_ASSERT(IsGCPointerBufferAlloc(vector));
    MOZ_RELEASE_ASSERT(IsNurseryOwned(zone(), vector) == expectNurseryOwned);

    if (!vector->empty()) {
      void* ptr = vector->begin();
      MOZ_RELEASE_ASSERT(IsGCPointerBufferAlloc(ptr));
      MOZ_RELEASE_ASSERT(IsNurseryOwned(zone(), ptr) == expectNurseryOwned);
    }
  }

  static void trace(JSTracer* trc, JSObject* obj) {
    auto* self = &obj->as<VectorObject>();
    TraceBufferSlot(trc, self, VectorSlot, "VectorObject vector");
    if (VectorT* vector = self->getVector()) {
      vector->trace(trc, self);
    }
  }

  static constexpr JSClassOps classOps_ = {
      .trace = trace,
  };

  static constexpr JSClass class_ = {
      "VectorObject", JSCLASS_HAS_RESERVED_SLOTS(SlotCount), &classOps_};
};

BEGIN_TEST(testBufferAllocPolicy_vector) {
  // Exercise using BufferAllocPolicy for a vector of GC things.

  AutoLeaveZeal leaveZeal(cx);

  CHECK(testVector(/* allocInNursery = */ true, /* dieInNursery = */ true));
  CHECK(testVector(/* allocInNursery = */ true, /* dieInNursery = */ false));
  CHECK(testVector(/* allocInNursery = */ false, /* dieInNursery = */ false));
  return true;
}

bool testVector(bool allocInNursery, bool dieInNursery) {
  MOZ_ASSERT_IF(!allocInNursery, !dieInNursery);

  const size_t ElementCount = 1000;

  JS_GC(cx);

  Zone* zone = cx->zone();
  size_t initialMallocHeapSize = zone->mallocHeapSize.bytes();

  bool nurseryOwned = allocInNursery;
  Rooted<VectorObject*> obj(cx, VectorObject::create(cx, nurseryOwned));
  CHECK(obj);
  obj->check(nurseryOwned);

  mozilla::MallocSizeOf mallocSizeOf = nullptr;  // Unused.
  CHECK(obj->getVector()->sizeOfOwnedAllocs(mallocSizeOf) == 0);

  for (size_t i = 0; i < ElementCount; i++) {
    Rooted<PlainObject*> element(cx, NewPlainObject(cx));
    CHECK(element);

    RootedValue value(cx, Int32Value(i));
    CHECK(JS_DefineProperty(cx, element, "i", value, 0));
    CHECK(obj->getVector()->append(element));

    obj->check(nurseryOwned);
  }

  CHECK(obj->getVector()->sizeOfOwnedAllocs(mallocSizeOf) != 0);
  CHECK(zone->mallocHeapSize.bytes() > initialMallocHeapSize);

  if (!dieInNursery) {
    cx->minorGC(JS::GCReason::API);
    nurseryOwned = false;
    obj->check(nurseryOwned);
  }

  {
    auto& vector = *obj->getVector();
    vector.shrinkTo(ElementCount / 2);
    vector.shrinkStorageToFit();
    CHECK(vector.length() == ElementCount / 2);
  }

  for (size_t i = 0; i < obj->length() / 2; i++) {
    Rooted<PlainObject*> element(cx, &obj->get(i)->as<PlainObject>());
    RootedValue value(cx);
    CHECK(JS_GetProperty(cx, element, "i", &value));
    CHECK(value.toInt32() == int32_t(i));
  }

  obj->check(nurseryOwned);

  // Note internal pointers so we can check whether they get freed.
  void* oldVector = obj->getVector();
  void* oldBuffer = obj->getVector()->begin();
  gc::WaitForBackgroundTasks(cx);
  CHECK(zone->bufferAllocator.isPointerWithinBuffer(oldVector));
  CHECK(zone->bufferAllocator.isPointerWithinBuffer(oldBuffer));

  obj = nullptr;
  if (nurseryOwned) {
    cx->minorGC(JS::GCReason::API);
  } else {
    JS_GC(cx);
  }

  gc::WaitForBackgroundTasks(cx);
  CHECK(!zone->bufferAllocator.isPointerWithinBuffer(oldVector));
  CHECK(!zone->bufferAllocator.isPointerWithinBuffer(oldBuffer));

  if (nurseryOwned) {
    JS_GC(cx);
  }
  MOZ_ASSERT(zone->mallocHeapSize.bytes() == initialMallocHeapSize);
  CHECK(zone->mallocHeapSize.bytes() == initialMallocHeapSize);

  return true;
}
END_TEST(testBufferAllocPolicy_vector)

// JS_EXPECT_HAZARDS will cause the hazard analysis to fail here if a hazard is
// *not* found when holding a gc::Buffer subclass live across a GC.
BEGIN_TEST_WITH_ATTRIBUTES(testBufferAllocator_unrootedBuffer,
                           JS_EXPECT_HAZARDS) {
  AutoLeaveZeal leaveZeal(cx);

  // The owner is tenured so the buffer will not move and this will not crash.
  Rooted<VectorObject*> obj(cx, VectorObject::create(cx, false));
  CHECK(obj);

  VectorObject::VectorT* vector = obj->getVector();
  JS_GC(cx);
  CHECK(vector->empty());

  return true;
}
END_TEST(testBufferAllocator_unrootedBuffer)

BEGIN_TEST(testBufferAllocator_rootedNoHazard) {
  AutoLeaveZeal leaveZeal(cx);

  Rooted<VectorObject*> obj(cx, VectorObject::create(cx, false));
  CHECK(obj);

  RootedBuffer<VectorObject::VectorT> vector(cx, obj->getVector());
  JS_GC(cx);
  CHECK(vector->empty());

  return true;
}
END_TEST(testBufferAllocator_rootedNoHazard)

// A JS object that holds a buffer-allocated hash set.
class HashSetObject : public NativeObject {
 public:
  using HashSetT = BufferAllocated<
      GCHashSet<HeapPtr<JSObject*>, StableCellHasher<HeapPtr<JSObject*>>,
                BufferAllocPolicy>>;

  enum { HashSetSlot, SlotCount };

  static HashSetObject* create(JSContext* cx, bool nurseryOwned) {
    NewObjectKind kind = nurseryOwned ? GenericObject : TenuredObject;
    auto* obj =
        NewObjectWithClassProto<HashSetObject>(cx, nullptr, {.newKind = kind});
    if (!obj) {
      return nullptr;
    }

    HashSetT* set = NewBuffer<HashSetT>(obj, BufferAllocPolicy(obj));
    if (!set) {
      return nullptr;
    }

    InitBufferSlot(obj, HashSetSlot, set);
    return obj;
  }

  HashSetT* getSet() {
    return static_cast<HashSetT*>(getFixedSlot(HashSetSlot).toPrivate());
  }

  void check(bool expectNurseryOwned) {
    MOZ_RELEASE_ASSERT(IsInsideNursery(this) == expectNurseryOwned);

    HashSetT* set = getSet();
    MOZ_RELEASE_ASSERT(IsGCPointerBufferAlloc(set));
    MOZ_RELEASE_ASSERT(IsNurseryOwned(zone(), set) == expectNurseryOwned);
  }

  static void trace(JSTracer* trc, JSObject* obj) {
    auto* self = &obj->as<HashSetObject>();
    TraceBufferSlot(trc, self, HashSetSlot, "HashSetObject set");
    if (HashSetT* set = self->getSet()) {
      set->trace(trc, self);
    }
  }

  static constexpr JSClassOps classOps_ = {
      .trace = trace,
  };

  static constexpr JSClass class_ = {
      "HashSetObject", JSCLASS_HAS_RESERVED_SLOTS(SlotCount), &classOps_};
};

BEGIN_TEST(testBufferAllocPolicy_hashSet) {
  // Exercise using BufferAllocPolicy for a hash set of objects.

  AutoLeaveZeal leaveZeal(cx);

  CHECK(testSet(/* allocInNursery = */ true, /* dieInNursery = */ true));
  CHECK(testSet(/* allocInNursery = */ true, /* dieInNursery = */ false));
  CHECK(testSet(/* allocInNursery = */ false, /* dieInNursery = */ false));
  return true;
}

bool testSet(bool allocInNursery, bool dieInNursery) {
  MOZ_ASSERT_IF(!allocInNursery, !dieInNursery);

  const size_t ElementCount = 1000;

  JS_GC(cx);

  Zone* zone = cx->zone();
  size_t initialMallocHeapSize = zone->mallocHeapSize.bytes();

  bool nurseryOwned = allocInNursery;
  Rooted<HashSetObject*> obj(cx, HashSetObject::create(cx, nurseryOwned));
  CHECK(obj);
  obj->check(nurseryOwned);

  mozilla::MallocSizeOf mallocSizeOf = nullptr;  // Unused.
  CHECK(obj->getSet()->sizeOfOwnedAllocs(mallocSizeOf) == 0);

  for (size_t i = 0; i < ElementCount; i++) {
    Rooted<PlainObject*> element(cx, NewPlainObject(cx));
    CHECK(element);

    RootedValue value(cx, Int32Value(i));
    CHECK(JS_DefineProperty(cx, element, "i", value, 0));
    CHECK(obj->getSet()->put(element));

    obj->check(nurseryOwned);
  }

  CHECK(obj->getSet()->sizeOfOwnedAllocs(mallocSizeOf) != 0);
  CHECK(zone->mallocHeapSize.bytes() > initialMallocHeapSize);

  if (!dieInNursery) {
    cx->minorGC(JS::GCReason::API);
    nurseryOwned = false;
    obj->check(nurseryOwned);
  }

  auto& set = *obj->getSet();
  size_t i = 0;
  for (auto iter = set.modIter(); !iter.done(); iter.next()) {
    i++;
    if (i % 2 == 0) {
      iter.remove();
    }
  }
  set.compact();
  CHECK(set.count() == ElementCount / 2);

  for (auto iter = set.iter(); !iter.done(); iter.next()) {
    Rooted<PlainObject*> element(cx, &iter.get()->as<PlainObject>());
    RootedValue value(cx);
    CHECK(JS_GetProperty(cx, element, "i", &value));
    CHECK(value.toInt32() >= 0);
    CHECK(value.toInt32() < int32_t(ElementCount));
  }

  obj->check(nurseryOwned);

  // Note set pointer so we can check whether it gets freed.
  void* oldSet = obj->getSet();
  gc::WaitForBackgroundTasks(cx);
  CHECK(zone->bufferAllocator.isPointerWithinBuffer(oldSet));

  obj = nullptr;
  if (nurseryOwned) {
    cx->minorGC(JS::GCReason::API);
  } else {
    JS_GC(cx);
  }

  gc::WaitForBackgroundTasks(cx);
  CHECK(!zone->bufferAllocator.isPointerWithinBuffer(oldSet));

  if (nurseryOwned) {
    JS_GC(cx);
  }
  MOZ_ASSERT(zone->mallocHeapSize.bytes() == initialMallocHeapSize);
  CHECK(zone->mallocHeapSize.bytes() == initialMallocHeapSize);

  return true;
}
END_TEST(testBufferAllocPolicy_hashSet)

namespace js::gc {

bool TestGetAllocTenuredInMixedChunks(Zone* zone) {
  return zone->bufferAllocator.allocTenuredInMixedChunks;
}

bool TestChunkHasNurseryOwnedAllocs(void* alloc) {
  return BufferChunk::from(alloc)->hasNurseryOwnedAllocs;
}

}  // namespace js::gc

BEGIN_TEST(testBufferAllocator_chunkKindSmallHeapSharing) {
  // For small heaps tenured allocations will share space with nursery
  // allocations when allocating into available space. Once the heap grows past
  // a threshold we try to use separate chunks for nursery and tenured
  // allocations.

  AutoLeaveZeal leaveZeal(cx);
  JS::NonIncrementalGC(cx, JS::GCOptions::Shrink, JS::GCReason::API);

  Zone* zone = cx->zone();
  size_t initialGCHeapSize = zone->gcHeapSize.bytes();
  size_t initialMallocHeapSize = zone->mallocHeapSize.bytes();

  // Initially we expect to share chunks.
  CHECK(TestGetAllocTenuredInMixedChunks(zone));

  const size_t GrowthChunkCount = 6;
  const size_t BufferCount = GrowthChunkCount + 5;
  Rooted<BufferHolderObject*> holder(
      cx, BufferHolderObject::create(cx, BufferCount));
  CHECK(holder);
  size_t index = 0;

  // Allocate a nursery owned buffer that leaves plenty of free space behind
  // in its (mixed) chunk.
  holder->prepareForPostBarrier(cx);
  void* nurseryAlloc = AllocBuffer(zone, ChunkSize / 2, true);
  CHECK(nurseryAlloc);
  holder->setBuffer(nurseryAlloc, index++);
  holder->postBarrier(cx);
  CHECK(TestChunkHasNurseryOwnedAllocs(nurseryAlloc));

  // A small tenured allocation should be satisfied from the space left over
  // in the mixed chunk, since sharing is allowed on a small heap.
  void* tenuredAlloc = AllocBuffer(zone, MinMediumAllocSize, false);
  CHECK(tenuredAlloc);
  holder->setBuffer(tenuredAlloc, index++);
  CHECK(BufferChunk::from(nurseryAlloc) == BufferChunk::from(tenuredAlloc));
  CHECK(TestChunkHasNurseryOwnedAllocs(tenuredAlloc));

  // Grow the heap past the small-heap threshold with dedicated tenured
  // chunks, then run a major GC so the allocator recomputes
  // |allocTenuredInMixedChunks|.
  for (size_t i = 0; i < GrowthChunkCount; i++) {
    void* alloc = AllocBuffer(zone, MaxMediumAllocSize, false);
    CHECK(alloc);
    holder->setBuffer(alloc, index++);
    CHECK(!TestChunkHasNurseryOwnedAllocs(alloc));
  }
  JS::NonIncrementalGC(cx, JS::GCOptions::Shrink, JS::GCReason::API);
  CHECK(!TestGetAllocTenuredInMixedChunks(zone));

  // Now that sharing is disabled, a small tenured allocation must not be
  // satisfied from the free space left in a mixed chunk, even though such
  // space is available.
  tenuredAlloc = AllocBuffer(zone, MinMediumAllocSize, false);
  CHECK(tenuredAlloc);
  CHECK(!TestChunkHasNurseryOwnedAllocs(tenuredAlloc));

  nurseryAlloc = AllocBuffer(zone, MinMediumAllocSize, true);
  CHECK(nurseryAlloc);
  CHECK(BufferChunk::from(nurseryAlloc) != BufferChunk::from(tenuredAlloc));
  CHECK(TestChunkHasNurseryOwnedAllocs(nurseryAlloc));

  holder = nullptr;
  NewPlainObject(cx);  // Force minor GC.
  JS_GC(cx);

  CHECK(zone->gcHeapSize.bytes() == initialGCHeapSize);
  CHECK(zone->mallocHeapSize.bytes() == initialMallocHeapSize);

  // Everything is dead so the heap shrinks back to its original size. The flag
  // should be reset.
  CHECK(TestGetAllocTenuredInMixedChunks(zone));

  return true;
}
END_TEST(testBufferAllocator_chunkKindSmallHeapSharing)
