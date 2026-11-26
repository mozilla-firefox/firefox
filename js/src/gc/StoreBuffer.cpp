/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"

#include "gc/GCRuntime.h"
#include "gc/Statistics.h"
#include "vm/MutexIDs.h"
#include "vm/Runtime.h"

#include "gc/StoreBuffer-inl.h"

using namespace js;
using namespace js::gc;

ArenaCellSet ArenaCellSet::Empty;

ArenaCellSet::ArenaCellSet(Arena* arena)
    : arena(arena)
#ifdef DEBUG
      ,
      minorGCNumberAtCreation(
          arena->zone()->runtimeFromMainThread()->gc.minorGCCount())
#endif
{
  MOZ_ASSERT(arena);
  MOZ_ASSERT(bits.isAllClear());
}

template <typename T>
StoreBuffer::MonoTypeBuffer<T>::MonoTypeBuffer(MonoTypeBuffer&& other)
    : stores_(std::move(other.stores_)),
      maxEntries_(other.maxEntries_),
      last_(std::move(other.last_)) {
  other.clear();
}
template <typename T>
StoreBuffer::MonoTypeBuffer<T>& StoreBuffer::MonoTypeBuffer<T>::operator=(
    MonoTypeBuffer&& other) {
  if (&other != this) {
    this->~MonoTypeBuffer();
    new (this) MonoTypeBuffer(std::move(other));
  }
  return *this;
}

template <typename T>
void StoreBuffer::MonoTypeBuffer<T>::setSize(size_t entryCount) {
  MOZ_ASSERT(entryCount != 0);
  maxEntries_ = entryCount;
}

template <typename T>
bool StoreBuffer::MonoTypeBuffer<T>::isEmpty() const {
  return last_ == T() && stores_.empty();
}

template <typename T>
void StoreBuffer::MonoTypeBuffer<T>::clear() {
  last_ = T();
  stores_.clear();
}

template <typename T>
size_t StoreBuffer::MonoTypeBuffer<T>::sizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) {
  return stores_.shallowSizeOfExcludingThis(mallocSizeOf);
}

StoreBuffer::WholeCellBuffer::WholeCellBuffer(WholeCellBuffer&& other)
    : storage_(std::move(other.storage_)),
      maxSize_(other.maxSize_),
      last_(other.last_) {
  other.last_ = nullptr;
}
StoreBuffer::WholeCellBuffer& StoreBuffer::WholeCellBuffer::operator=(
    WholeCellBuffer&& other) {
  if (&other != this) {
    this->~WholeCellBuffer();
    new (this) WholeCellBuffer(std::move(other));
  }
  return *this;
}

bool StoreBuffer::WholeCellBuffer::init() {
  if (!storage_) {
    storage_ = MakeUnique<LifoAlloc>(LifoAllocBlockSize, js::MallocArena);
    if (!storage_) {
      return false;
    }
  }

  // This prevents LifoAlloc::Enum from crashing with a release
  // assertion if we ever allocate one entry larger than
  // LifoAllocBlockSize.
  storage_->disableOversize();

  clear();
  return true;
}

void StoreBuffer::WholeCellBuffer::setSize(size_t entryCount) {
  MOZ_ASSERT(entryCount);
  maxSize_ = entryCount * sizeof(ArenaCellSet);
}

bool StoreBuffer::WholeCellBuffer::isEmpty() const {
  return !storage_ || storage_->isEmpty();
}

void StoreBuffer::WholeCellBuffer::clear() {
  if (storage_) {
    for (LifoAlloc::Enum e(*storage_); !e.empty();) {
      ArenaCellSet* cellSet = e.read<ArenaCellSet>();
      cellSet->arena->bufferedCells() = &ArenaCellSet::Empty;
    }

    storage_->used() ? storage_->releaseAll() : storage_->freeAll();
  }

  last_ = nullptr;
}

ArenaCellSet* StoreBuffer::WholeCellBuffer::allocateCellSet(Arena* arena) {
  MOZ_ASSERT(arena->bufferedCells() == &ArenaCellSet::Empty);

  Zone* zone = arena->zone();
  JSRuntime* rt = zone->runtimeFromMainThread();
  if (!rt->gc.nursery().isEnabled()) {
    return nullptr;
  }

  AutoEnterOOMUnsafeRegion oomUnsafe;
  auto* cells = storage_->new_<ArenaCellSet>(arena);
  if (!cells) {
    oomUnsafe.crash("Failed to allocate ArenaCellSet");
  }

  arena->bufferedCells() = cells;

  if (isAboutToOverflow()) {
    rt->gc.storeBuffer().setAboutToOverflow(
        JS::GCReason::FULL_WHOLE_CELL_BUFFER);
  }

  return cells;
}

size_t StoreBuffer::WholeCellBuffer::sizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) {
  return storage_ ? storage_->sizeOfIncludingThis(mallocSizeOf) : 0;
}

StoreBuffer::GenericBuffer::GenericBuffer(GenericBuffer&& other)
    : storage_(std::move(other.storage_)), maxSize_(other.maxSize_) {}
StoreBuffer::GenericBuffer& StoreBuffer::GenericBuffer::operator=(
    GenericBuffer&& other) {
  if (&other != this) {
    this->~GenericBuffer();
    new (this) GenericBuffer(std::move(other));
  }
  return *this;
}

bool StoreBuffer::GenericBuffer::isEmpty() const {
  return !storage_ || storage_->isEmpty();
}

void StoreBuffer::GenericBuffer::clear() {
  if (storage_) {
    storage_->used() ? storage_->releaseAll() : storage_->freeAll();
  }
}

size_t StoreBuffer::GenericBuffer::sizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) {
  return storage_ ? storage_->sizeOfIncludingThis(mallocSizeOf) : 0;
}

bool StoreBuffer::GenericBuffer::init() {
  if (!storage_) {
    storage_ = MakeUnique<LifoAlloc>(LifoAllocBlockSize, js::MallocArena);
    if (!storage_) {
      return false;
    }
  }

  clear();
  return true;
}

void StoreBuffer::GenericBuffer::setSize(size_t entryCount) {
  MOZ_ASSERT(entryCount != 0);
  maxSize_ = entryCount * (sizeof(BufferableRef) + sizeof(void*));
}

void StoreBuffer::GenericBuffer::trace(JSTracer* trc, StoreBuffer* owner) {
  mozilla::ReentrancyGuard g(*owner);
  MOZ_ASSERT(owner->isEnabled());
  if (!storage_) {
    return;
  }

  for (LifoAlloc::Enum e(*storage_); !e.empty();) {
    unsigned size = *e.read<unsigned>();
    BufferableRef* edge = e.read<BufferableRef>(size);
    edge->trace(trc);
  }
}

StoreBuffer::StoreBuffer(GCRuntime* gc)
    : gc_(gc),
      nursery_(gc->nursery()),
      entryCount_(gc->tunables.storeBufferEntries()),
      entryScaling_(gc->tunables.storeBufferScaling()),
      aboutToOverflow_(false),
      enabled_(false),
      mayHavePointersToDeadCells_(false)
#ifdef DEBUG
      ,
      mEntered(false)
#endif
{
  MOZ_ASSERT(entryCount_ != 0);
}

StoreBuffer::StoreBuffer(StoreBuffer&& other)
    : bufferVal(std::move(other.bufferVal)),
      bufStrCell(std::move(other.bufStrCell)),
      bufBigIntCell(std::move(other.bufBigIntCell)),
      bufGetterSetterCell(std::move(other.bufGetterSetterCell)),
      bufObjCell(std::move(other.bufObjCell)),
      bufferSlot(std::move(other.bufferSlot)),
      bufferWasmAnyRef(std::move(other.bufferWasmAnyRef)),
      bufferWholeCell(std::move(other.bufferWholeCell)),
      bufferGeneric(std::move(other.bufferGeneric)),
      gc_(other.gc_),
      nursery_(other.nursery_),
      entryCount_(other.entryCount_),
      entryScaling_(other.entryScaling_),
      aboutToOverflow_(other.aboutToOverflow_),
      enabled_(other.enabled_),
      mayHavePointersToDeadCells_(other.mayHavePointersToDeadCells_)
#ifdef DEBUG
      ,
      mEntered(other.mEntered)
#endif
{
  MOZ_ASSERT(entryCount_ != 0);
  MOZ_ASSERT(enabled_);
  MOZ_ASSERT(!mEntered);
  other.disable();
}

StoreBuffer& StoreBuffer::operator=(StoreBuffer&& other) {
  if (&other != this) {
    this->~StoreBuffer();
    new (this) StoreBuffer(std::move(other));
  }
  return *this;
}

#ifdef DEBUG
void StoreBuffer::checkAccess() const {
  // The GC runs tasks that may access the storebuffer in parallel and so must
  // take a lock. The mutator may only access the storebuffer from the main
  // thread.
  if (gc_->heapState() != JS::HeapState::Idle &&
      gc_->heapState() != JS::HeapState::MinorCollecting) {
    MOZ_ASSERT(!CurrentThreadIsGCMarking());
    gc_->assertCurrentThreadHasLockedSweepingLock();
  } else {
    MOZ_ASSERT(CurrentThreadCanAccessRuntime(gc_->rt));
  }
}
#endif

void StoreBuffer::checkEmpty() const { MOZ_ASSERT(isEmpty()); }

bool StoreBuffer::isEmpty() const {
  return bufferVal.isEmpty() && bufStrCell.isEmpty() &&
         bufBigIntCell.isEmpty() && bufGetterSetterCell.isEmpty() &&
         bufObjCell.isEmpty() && bufferSlot.isEmpty() &&
         bufferWasmAnyRef.isEmpty() && bufferWholeCell.isEmpty() &&
         bufferGeneric.isEmpty();
}

bool StoreBuffer::enable() {
  if (enabled_) {
    return true;
  }

  checkEmpty();
  if (!bufferWholeCell.init() || !bufferGeneric.init()) {
    return false;
  }

  updateSize();

  enabled_ = true;
  return true;
}

void StoreBuffer::updateSize() {
  // The entry counts for the individual buffers are scaled based on the initial
  // entryCount parameter passed to the constructor.
  MOZ_ASSERT(entryCount_ != 0);
  MOZ_ASSERT(entryScaling_ >= 0.0 && entryScaling_ <= 1.0);

  // Scale the entry count linearly based on the size of the nursery. The entry
  // count parameter specifies the result at a nursery size of 16MB.
  const double nurseryBaseSize = 16 * 1024 * 1024;
  double nurserySizeRatio = double(nursery_.capacity()) / nurseryBaseSize;
  double count =
      ((nurserySizeRatio - 1.0) * entryScaling_ + 1.0) * double(entryCount_);
  MOZ_ASSERT(count > 0.0);
  size_t defaultEntryCount = size_t(std::max(count, 1.0));

  size_t slotsEntryCount = std::max(defaultEntryCount / 2, size_t(1));
  size_t wholeCellEntryCount = std::max(defaultEntryCount / 10, size_t(1));
  size_t genericEntryCount = std::max(defaultEntryCount / 4, size_t(1));

  bufferVal.setSize(defaultEntryCount);
  bufStrCell.setSize(defaultEntryCount);
  bufBigIntCell.setSize(defaultEntryCount);
  bufGetterSetterCell.setSize(defaultEntryCount);
  bufObjCell.setSize(defaultEntryCount);
  bufferSlot.setSize(slotsEntryCount);
  bufferWasmAnyRef.setSize(defaultEntryCount);
  bufferWholeCell.setSize(wholeCellEntryCount);
  bufferGeneric.setSize(genericEntryCount);
}

void StoreBuffer::disable() {
  checkEmpty();

  if (!enabled_) {
    return;
  }

  aboutToOverflow_ = false;

  enabled_ = false;
}

void StoreBuffer::clear() {
  if (!enabled_) {
    return;
  }

  aboutToOverflow_ = false;
  mayHavePointersToDeadCells_ = false;

  bufferVal.clear();
  bufStrCell.clear();
  bufBigIntCell.clear();
  bufGetterSetterCell.clear();
  bufObjCell.clear();
  bufferSlot.clear();
  bufferWasmAnyRef.clear();
  bufferWholeCell.clear();
  bufferGeneric.clear();
}

void StoreBuffer::setAboutToOverflow(JS::GCReason reason) {
  if (!aboutToOverflow_) {
    aboutToOverflow_ = true;
    gc_->stats().count(gcstats::COUNT_STOREBUFFER_OVERFLOW);
  }
  nursery_.requestMinorGC(reason);
}

void StoreBuffer::traceValues(TenuringTracer& mover) {
  bufferVal.trace(mover, this);
}
void StoreBuffer::traceCells(TenuringTracer& mover) {
  bufStrCell.trace(mover, this);
  bufBigIntCell.trace(mover, this);
  bufGetterSetterCell.trace(mover, this);
  bufObjCell.trace(mover, this);
}
void StoreBuffer::traceSlots(TenuringTracer& mover) {
  bufferSlot.trace(mover, this);
}
void StoreBuffer::traceWasmAnyRefs(TenuringTracer& mover) {
  bufferWasmAnyRef.trace(mover, this);
}
void StoreBuffer::traceWholeCells(TenuringTracer& mover) {
  bufferWholeCell.trace(mover, this);
}
void StoreBuffer::traceGenericEntries(JSTracer* trc) {
  bufferGeneric.trace(trc, this);
}

void StoreBuffer::addSizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf,
                                         JS::GCSizes* sizes) {
  sizes->storeBufferVals += bufferVal.sizeOfExcludingThis(mallocSizeOf);
  sizes->storeBufferCells +=
      bufStrCell.sizeOfExcludingThis(mallocSizeOf) +
      bufBigIntCell.sizeOfExcludingThis(mallocSizeOf) +
      bufGetterSetterCell.sizeOfExcludingThis(mallocSizeOf) +
      bufObjCell.sizeOfExcludingThis(mallocSizeOf);
  sizes->storeBufferSlots += bufferSlot.sizeOfExcludingThis(mallocSizeOf);
  sizes->storeBufferWasmAnyRefs +=
      bufferWasmAnyRef.sizeOfExcludingThis(mallocSizeOf);
  sizes->storeBufferWholeCells +=
      bufferWholeCell.sizeOfExcludingThis(mallocSizeOf);
  sizes->storeBufferGenerics += bufferGeneric.sizeOfExcludingThis(mallocSizeOf);
}

void gc::CellHeaderPostWriteBarrier(JSObject** ptr, JSObject* prev,
                                    JSObject* next) {
  InternalBarrierMethods<JSObject*>::postBarrier(ptr, prev, next);
}

template struct StoreBuffer::MonoTypeBuffer<StoreBuffer::ValueEdge>;
template struct StoreBuffer::MonoTypeBuffer<StoreBuffer::SlotsEdge>;
template struct StoreBuffer::MonoTypeBuffer<StoreBuffer::WasmAnyRefEdge>;

void js::gc::PostWriteBarrierCell(Cell* cell, Cell* prev, Cell* next) {
  if (!next || !cell->isTenured()) {
    return;
  }

  StoreBuffer* buffer = next->storeBuffer();
  if (!buffer || (prev && prev->storeBuffer())) {
    return;
  }

  buffer->putWholeCell(cell);
}
