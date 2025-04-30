/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/MathAlgorithms.h"

#include <cstdlib>

#include "jsmath.h"

#include "gc/Allocator.h"
#include "gc/Memory.h"
#include "gc/Nursery.h"
#include "gc/Zone.h"
#include "jsapi-tests/tests.h"
#include "vm/PlainObject.h"

#if defined(XP_WIN)
#  include "util/WindowsWrapper.h"
#  include <psapi.h>
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

class AutoAddGCRootsTracer {
  JSContext* cx_;
  JSTraceDataOp traceOp_;
  void* data_;

 public:
  AutoAddGCRootsTracer(JSContext* cx, JSTraceDataOp traceOp, void* data)
      : cx_(cx), traceOp_(traceOp), data_(data) {
    JS_AddExtraGCRootsTracer(cx, traceOp, data);
  }
  ~AutoAddGCRootsTracer() { JS_RemoveExtraGCRootsTracer(cx_, traceOp_, data_); }
};

static size_t SomeAllocSizes[] = {16,
                                  17,
                                  31,
                                  32,
                                  100,
                                  200,
                                  240,
                                  256,
                                  1000,
                                  4096,
                                  5000,
                                  16 * 1024,
                                  100 * 1024,
                                  255 * 1024,
                                  256 * 1024,
                                  600 * 1024,
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

  static BufferHolderObject* create(JSContext* cx);

  void setBuffer(void* buffer);

 private:
  static const JSClassOps classOps_;

  static void trace(JSTracer* trc, JSObject* obj);
};

const JSClass BufferHolderObject::class_ = {"BufferHolderObject",
                                            JSCLASS_HAS_RESERVED_SLOTS(1),
                                            &BufferHolderObject::classOps_};

const JSClassOps BufferHolderObject::classOps_ = {
    nullptr,                    // addProperty
    nullptr,                    // delProperty
    nullptr,                    // enumerate
    nullptr,                    // newEnumerate
    nullptr,                    // resolve
    nullptr,                    // mayResolve
    nullptr,                    // finalize
    nullptr,                    // call
    nullptr,                    // construct
    BufferHolderObject::trace,  // trace
};

/* static */
BufferHolderObject* BufferHolderObject::create(JSContext* cx) {
  NativeObject* obj = NewObjectWithGivenProto(cx, &class_, nullptr);
  if (!obj) {
    return nullptr;
  }

  BufferHolderObject* holder = &obj->as<BufferHolderObject>();
  holder->setBuffer(nullptr);
  return holder;
}

void BufferHolderObject::setBuffer(void* buffer) {
  setFixedSlot(0, JS::PrivateValue(buffer));
}

/* static */
void BufferHolderObject::trace(JSTracer* trc, JSObject* obj) {
  NativeObject* holder = &obj->as<NativeObject>();
  void* buffer = holder->getFixedSlot(0).toPrivate();
  if (buffer) {
    TraceBufferEdge(trc, obj, &buffer, "BufferHolderObject buffer");
    if (buffer != holder->getFixedSlot(0).toPrivate()) {
      holder->setFixedSlot(0, JS::PrivateValue(buffer));
    }
  }
}

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
      void* alloc = AllocBuffer(zone, requestSize, nurseryOwned);
      CHECK(alloc);

      CHECK(IsBufferAlloc(alloc));
      size_t actualSize = GetAllocSize(zone, alloc);
      CHECK(actualSize == GetGoodAllocSize(requestSize));

      CHECK(IsNurseryOwned(zone, alloc) == nurseryOwned);

      WriteAllocData(alloc, actualSize);
      CHECK(CheckAllocData(alloc, actualSize));

      CHECK(!IsBufferAllocMarkedBlack(zone, alloc));

      CHECK(cx->runtime()->gc.isPointerWithinBufferAlloc(alloc));

      holder->setBuffer(alloc);
      if (nurseryOwned) {
        // Hack to force minor GC. We've marked our alloc 'nursery owned' even
        // though that isn't true.
        NewPlainObject(cx);
        // Hack to force marking our holder.
        cx->runtime()->gc.storeBuffer().putWholeCell(holder);
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
      CHECK(IsBufferAlloc(alloc));
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
  CHECK(!IsBufferAlloc(buffer));

  RootedObject obj(cx, NewPlainObject(cx));
  CHECK(obj);
  CHECK(IsInsideNursery(obj));
  CHECK(!IsBufferAlloc(obj));

  JS_GC(cx);
  CHECK(!IsInsideNursery(obj));
  CHECK(!IsBufferAlloc(obj));

  return true;
}
END_TEST(testBufferAllocator_predicatesOnOtherAllocs)

BEGIN_TEST(testBufferAllocator_stress) {
  AutoLeaveZeal leaveZeal(cx);

  unsigned seed = unsigned(GenerateRandomSeed());
  fprintf(stderr, "Random seed: 0x%x\n", seed);
  std::srand(seed);

  Rooted<PlainObject*> holder(
      cx, NewPlainObjectWithAllocKind(cx, gc::AllocKind::OBJECT2));
  CHECK(holder);

  JS::NonIncrementalGC(cx, JS::GCOptions::Shrink, JS::GCReason::API);
  Zone* zone = cx->zone();

  size_t initialGCHeapSize = zone->gcHeapSize.bytes();
  size_t initialMallocHeapSize = zone->mallocHeapSize.bytes();

  void* liveAllocs[MaxLiveAllocs];
  mozilla::PodZero(&liveAllocs);

  AutoGCParameter setMaxHeap(cx, JSGC_MAX_BYTES, uint32_t(-1));
  AutoGCParameter param1(cx, JSGC_INCREMENTAL_GC_ENABLED, true);
  AutoGCParameter param2(cx, JSGC_PER_ZONE_GC_ENABLED, true);

#ifdef JS_GC_ZEAL
  JS::SetGCZeal(cx, 10, 50);
#endif

  holder->initFixedSlot(0, JS::PrivateValue(&liveAllocs));
  AutoAddGCRootsTracer addTracer(cx, traceAllocs, &holder);

  for (size_t i = 0; i < Iterations; i++) {
    size_t index = std::rand() % MaxLiveAllocs;
    size_t bytes = randomSize();

    if (!liveAllocs[index]) {
      liveAllocs[index] = AllocBuffer(zone, bytes, false);
    } else {
      void* ptr = ReallocBuffer(zone, liveAllocs[index], bytes, false);
      if (ptr) {
        liveAllocs[index] = ptr;
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

  mozilla::PodArrayZero(liveAllocs);

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
      TraceBufferEdge(trc, holder, bufferp, "test buffer");
    }
  }
}
END_TEST(testBufferAllocator_stress)
