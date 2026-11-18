/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedMap.h"

#include "MemMapSnapshot.h"
#include "ScriptPreloader-inl.h"
#include "SharedMapChangeEvent.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/IOBuffers.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ScriptPreloader.h"
#include "mozilla/Try.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ContentProcessMessageManager.h"
#include "mozilla/dom/IPCBlobUtils.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ScriptSettings.h"

using namespace mozilla::loader;

namespace mozilla {

using namespace ipc;

namespace dom::ipc {

// Align to size of uintptr_t here, to be safe. It's probably not strictly
// necessary, though.
constexpr size_t kStructuredCloneAlign = sizeof(uintptr_t);

static inline void AlignTo(size_t* aOffset, size_t aAlign) {
  if (auto mod = *aOffset % aAlign) {
    *aOffset += aAlign - mod;
  }
}

SharedMap::SharedMap() = default;

SharedMap::SharedMap(nsIGlobalObject* aGlobal, SharedMemoryHandle&& aMapHandle,
                     nsTArray<NotNull<RefPtr<BlobImpl>>>&& aBlobs)
    : DOMEventTargetHelper(aGlobal),
      mBlobImpls(std::move(aBlobs)),
      mHandle(std::move(aMapHandle)) {}

bool SharedMap::Has(const nsACString& aName) {
  (void)MaybeRebuild();
  return mEntries.Contains(aName);
}

void SharedMap::Get(JSContext* aCx, const nsACString& aName,
                    JS::MutableHandle<JS::Value> aRetVal, ErrorResult& aRv) {
  auto res = MaybeRebuild();
  if (res.isErr()) {
    aRv.Throw(res.unwrapErr());
    return;
  }

  Entry* entry = mEntries.Get(aName);
  if (!entry) {
    aRetVal.setNull();
    return;
  }

  entry->Read(aCx, aRetVal, aRv);
}

void SharedMap::Entry::Read(JSContext* aCx,
                            JS::MutableHandle<JS::Value> aRetVal,
                            ErrorResult& aRv) {
  if (mData.is<RefPtr<StructuredCloneData>>()) {
    // We have a temporary buffer for a key that was changed after the last
    // snapshot. Just decode it directly.
    auto& holder = mData.as<RefPtr<StructuredCloneData>>();
    holder->Read(aCx, aRetVal, aRv);
    return;
  }

  // We have a pointer to a shared memory region containing our structured
  // clone data. Create a temporary buffer to decode that data, and then
  // discard it so that we don't keep a separate process-local copy around any
  // longer than necessary.
  auto holder = MakeRefPtr<StructuredCloneData>(
      JS::StructuredCloneScope::DifferentProcess,
      StructuredCloneHolder::TransferringNotSupported);
  if (!holder->CopyExternalData(Data(), Size())) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
  if (mBlobCount) {
    holder->BlobImpls().AppendElements(Blobs());
  }
  holder->Read(aCx, aRetVal, aRv);
}

void SharedMap::Update(SharedMemoryHandle&& aMapHandle,
                       nsTArray<NotNull<RefPtr<BlobImpl>>>&& aBlobs,
                       nsTArray<nsCString>&& aChangedKeys) {
  MOZ_DIAGNOSTIC_ASSERT(!mWritable);

  mMapping = nullptr;
  mHandle = std::move(aMapHandle);
  mEntries.Clear();
  mEntryArray.reset();

  mBlobImpls = std::move(aBlobs);

  AutoEntryScript aes(GetParentObject(), "SharedMap change event");
  JSContext* cx = aes.cx();

  RootedDictionary<MozSharedMapChangeEventInit> init(cx);
  if (!init.mChangedKeys.SetCapacity(aChangedKeys.Length(), fallible)) {
    NS_WARNING("Failed to dispatch SharedMap change event");
    return;
  }
  for (auto& key : aChangedKeys) {
    (void)init.mChangedKeys.AppendElement(NS_ConvertUTF8toUTF16(key), fallible);
  }

  RefPtr<SharedMapChangeEvent> event =
      SharedMapChangeEvent::Constructor(this, u"change"_ns, init);
  event->SetTrusted(true);

  DispatchEvent(*event);
}

const nsTArray<SharedMap::Entry*>& SharedMap::EntryArray() const {
  if (mEntryArray.isNothing()) {
    MaybeRebuild();

    mEntryArray.emplace(mEntries.Count());
    auto& array = mEntryArray.ref();
    for (auto& entry : mEntries) {
      array.AppendElement(entry.GetRawPtr());
    }
  }

  return mEntryArray.ref();
}

const nsString SharedMap::GetKeyAtIndex(uint32_t aIndex) const {
  return NS_ConvertUTF8toUTF16(EntryArray()[aIndex]->Name());
}

bool SharedMap::GetValueAtIndex(JSContext* aCx, uint32_t aIndex,
                                JS::MutableHandle<JS::Value> aResult) const {
  ErrorResult rv;
  EntryArray()[aIndex]->Read(aCx, aResult, rv);
  if (rv.MaybeSetPendingException(aCx)) {
    return false;
  }
  return true;
}

void SharedMap::Entry::SetData(StructuredCloneData* aHolder) {
  MOZ_ASSERT(!aHolder->SupportsTransferring());

  CheckedInt<uint32_t> size = aHolder->BufferData().Size();
  MOZ_RELEASE_ASSERT(size.isValid(),
                     "SharedMap entry size exceeds max allowed size");

  mData = AsVariant(RefPtr{aHolder});

  mSize = size.value();
  mBlobCount = Holder()->BlobImpls().Length();
}

void SharedMap::Entry::ExtractData(char* aDestPtr, uint32_t aNewOffset,
                                   uint16_t aNewBlobOffset) {
  if (mData.is<RefPtr<StructuredCloneData>>()) {
    char* ptr = aDestPtr;
    Holder()->BufferData().ForEachDataChunk(
        [&](const char* aData, size_t aSize) {
          memcpy(ptr, aData, aSize);
          ptr += aSize;
          return true;
        });
    MOZ_ASSERT(uint32_t(ptr - aDestPtr) == mSize);
  } else {
    memcpy(aDestPtr, Data(), mSize);
  }

  mData = AsVariant(aNewOffset);
  mBlobOffset = aNewBlobOffset;
}

Result<Ok, nsresult> SharedMap::MaybeRebuild() {
  if (mMapping || !mHandle) {
    return Ok();
  }

  MOZ_DIAGNOSTIC_ASSERT(!mWritable);

  // This function maps a shared memory region created by Serialize() and reads
  // its header block to build a new mEntries hashtable of its contents.
  //
  // The entries created by this function contain a pointer to this SharedMap
  // instance, and the offsets and sizes of their structured clone data within
  // its shared memory region. When needed, that structured clone data is
  // retrieved directly as indexes into the SharedMap's shared memory region.

  mMapping = mHandle.Map();
  if (!mMapping) {
    return Err(NS_ERROR_FAILURE);
  }
  mHandle = nullptr;

  mozilla::Range<const uint8_t> inputRange(mMapping.DataAsSpan<uint8_t>());
  InputBuffer buffer(inputRange);

  uint32_t count;
  buffer.codeUint32(count);

  MOZ_ASSERT(mEntries.IsEmpty());
  MOZ_ASSERT(mEntryArray.isNothing());
  for (uint32_t i = 0; i < count; i++) {
    auto entry = std::make_unique<Entry>(*this);
    entry->Code(buffer);

    // This buffer was created at runtime, during this session, so any errors
    // indicate memory corruption, and are fatal.
    MOZ_RELEASE_ASSERT(!buffer.error());

    // Note: While the order of evaluation of the arguments to Put doesn't
    // matter for this (the actual move will only happen within Put), to be
    // clear about this, we call entry->Name() before calling Put.
    const auto& name = entry->Name();
    mEntries.InsertOrUpdate(name, std::move(entry));
  }

  return Ok();
}

void SharedMap::MaybeRebuild() const {
  (void)const_cast<SharedMap*>(this)->MaybeRebuild();
}

WritableSharedMap::WritableSharedMap() {
  mWritable = true;
  // Serialize the initial empty contents of the map immediately so that we
  // always have a file descriptor to send.
  (void)Serialize();
  MOZ_RELEASE_ASSERT(mHandle.IsValid() && mMapping.IsValid());
}

SharedMap* WritableSharedMap::GetReadOnly() {
  if (!mReadOnly) {
    nsTArray<NotNull<RefPtr<BlobImpl>>> blobs(mBlobImpls.Clone());
    mReadOnly =
        new SharedMap(ContentProcessMessageManager::Get()->GetParentObject(),
                      mHandle.Clone(), std::move(blobs));
  }
  return mReadOnly;
}

Result<Ok, nsresult> WritableSharedMap::Serialize() {
  // Serializes a new snapshot of the map, initializes a new read-only shared
  // memory region with its contents, and updates all entries to point to that
  // new snapshot.
  //
  // The layout of the snapshot is as follows:
  //
  // - A header containing a uint32 count field containing the number of
  //   entries in the map, followed by that number of serialized entry headers,
  //   as produced by Entry::Code.
  //
  // - A data block containing structured clone data for each of the entries'
  //   values. This data is referenced by absolute byte offsets from the start
  //   of the shared memory region, encoded in each of the entry header values.
  //   Each entry's data is aligned to kStructuredCloneAlign, and therefore may
  //   have alignment padding before it.
  //
  // This serialization format is decoded by the MaybeRebuild() method of
  // read-only SharedMap() instances, and used to populate their mEntries
  // hashtables.
  //
  // Writable instances never read the header blocks, but instead directly
  // update their Entry instances to point to the appropriate offsets in the
  // shared memory region created by this function.

  uint32_t count = mEntries.Count();

  size_t dataSize = 0;
  size_t headerSize = sizeof(count);
  size_t blobCount = 0;

  for (const auto& entry : mEntries.Values()) {
    headerSize += entry->HeaderSize();
    blobCount += entry->BlobCount();

    dataSize += entry->Size();
    AlignTo(&dataSize, kStructuredCloneAlign);
  }

  size_t offset = headerSize;
  AlignTo(&offset, kStructuredCloneAlign);

  OutputBuffer header;
  header.codeUint32(count);

  MemMapSnapshot mem;
  MOZ_TRY(mem.Init(offset + dataSize));

  auto ptr = mem.Get<char>();

  // We need to build the new array of blobs before we overwrite the existing
  // one, since previously-serialized entries will store their blob references
  // as indexes into our blobs array.
  nsTArray<NotNull<RefPtr<BlobImpl>>> blobImpls(blobCount);

  for (const auto& entry : mEntries.Values()) {
    AlignTo(&offset, kStructuredCloneAlign);

    size_t blobOffset = blobImpls.Length();
    if (entry->BlobCount()) {
      blobImpls.AppendElements(entry->Blobs());
    }

    entry->ExtractData(&ptr[offset], offset, blobOffset);
    entry->Code(header);

    offset += entry->Size();
  }

  mBlobImpls = std::move(blobImpls);

  // FIXME: We should create a separate OutputBuffer class which can encode to
  // a static memory region rather than dynamically allocating and then
  // copying.
  MOZ_ASSERT(header.cursor() == headerSize);
  memcpy(ptr.get(), header.Get(), header.cursor());

  // We've already updated offsets at this point. We need this to succeed.
  auto result = mem.Finalize();
  MOZ_RELEASE_ASSERT(result.isOk());
  mHandle = result.unwrap();
  mMapping = mHandle.Map();
  MOZ_RELEASE_ASSERT(mMapping.IsValid());

  return Ok();
}

void WritableSharedMap::SendTo(ContentParent* aParent) const {
  nsTArray<IPCBlob> blobs(mBlobImpls.Length());

  for (auto& blobImpl : mBlobImpls) {
    nsresult rv = IPCBlobUtils::Serialize(blobImpl, *blobs.AppendElement());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }
  }

  (void)aParent->SendUpdateSharedData(mHandle.Clone(), blobs, mChangedKeys);
}

void WritableSharedMap::BroadcastChanges() {
  if (mChangedKeys.IsEmpty()) {
    return;
  }

  if (!Serialize().isOk()) {
    return;
  }

  nsTArray<ContentParent*> parents;
  ContentParent::GetAll(parents);
  for (auto& parent : parents) {
    SendTo(parent);
  }

  if (mReadOnly) {
    nsTArray<NotNull<RefPtr<BlobImpl>>> blobImpls(mBlobImpls.Clone());
    mReadOnly->Update(mHandle.Clone(), std::move(blobImpls),
                      std::move(mChangedKeys));
  }

  mChangedKeys.Clear();
}

void WritableSharedMap::Delete(const nsACString& aName) {
  if (mEntries.Remove(aName)) {
    KeyChanged(aName);
  }
}

void WritableSharedMap::Set(JSContext* aCx, const nsACString& aName,
                            JS::Handle<JS::Value> aValue, ErrorResult& aRv) {
  auto holder = MakeRefPtr<StructuredCloneData>(
      JS::StructuredCloneScope::DifferentProcess,
      StructuredCloneHolder::TransferringNotSupported);

  holder->Write(aCx, aValue, aRv);
  if (aRv.Failed()) {
    return;
  }

  // Cap the maximum size of a value that can be stored inside SharedMap.
  if (!CheckedInt<uint32_t>(holder->BufferData().Size()).isValid()) {
    aRv.ThrowRangeError("SharedMap value too large");
    return;
  }

  if (!holder->InputStreams().IsEmpty()) {
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  Entry* entry = mEntries.GetOrInsertNew(aName, *this, aName);
  entry->SetData(holder);

  KeyChanged(aName);
}

void WritableSharedMap::Flush() { BroadcastChanges(); }

void WritableSharedMap::IdleFlush() {
  mPendingFlush = false;
  Flush();
}

nsresult WritableSharedMap::KeyChanged(const nsACString& aName) {
  if (!mChangedKeys.ContainsSorted(aName)) {
    mChangedKeys.InsertElementSorted(aName);
  }
  mEntryArray.reset();

  if (!mPendingFlush) {
    MOZ_TRY(NS_DispatchToCurrentThreadQueue(
        NewRunnableMethod("WritableSharedMap::IdleFlush", this,
                          &WritableSharedMap::IdleFlush),
        EventQueuePriority::Idle));
    mPendingFlush = true;
  }
  return NS_OK;
}

JSObject* SharedMap::WrapObject(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  return MozSharedMap_Binding::Wrap(aCx, this, aGivenProto);
}

JSObject* WritableSharedMap::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return MozWritableSharedMap_Binding::Wrap(aCx, this, aGivenProto);
}

/* static */
already_AddRefed<SharedMapChangeEvent> SharedMapChangeEvent::Constructor(
    EventTarget* aEventTarget, const nsAString& aType,
    const MozSharedMapChangeEventInit& aInit) {
  RefPtr<SharedMapChangeEvent> event = new SharedMapChangeEvent(aEventTarget);

  bool trusted = event->Init(aEventTarget);
  event->InitEvent(aType, aInit.mBubbles, aInit.mCancelable);
  event->SetTrusted(trusted);
  event->SetComposed(aInit.mComposed);

  event->mChangedKeys = aInit.mChangedKeys;

  return event.forget();
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(WritableSharedMap, SharedMap, mReadOnly)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WritableSharedMap)
NS_INTERFACE_MAP_END_INHERITING(SharedMap)

NS_IMPL_ADDREF_INHERITED(WritableSharedMap, SharedMap)
NS_IMPL_RELEASE_INHERITED(WritableSharedMap, SharedMap)

}  // namespace dom::ipc
}  // namespace mozilla
