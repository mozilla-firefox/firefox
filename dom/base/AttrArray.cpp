/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Storage of the children and attributes of a DOM node; storage for
 * the two is unified to minimize footprint.
 */

#include "AttrArray.h"

#include "mozilla/AttributeStyles.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/ServoBindings.h"
#include "mozmemory.h"
#include "nsContentUtils.h"  // nsAutoScriptBlocker
#include "nsNodeInfoManager.h"
#include "nsString.h"
#include "nsThreadUtils.h"
#include "nsUnicharUtils.h"

using mozilla::CheckedUint32;

// Attribute arrays belong to nodes, which only live on the main thread.
static void* AllocateImpl(size_t aSize) {
  MOZ_ASSERT(NS_IsMainThread());
  static const arena_id_t sArena = [] {
    arena_params_t params;
    params.mFlags = ARENA_FLAG_THREAD_MAIN_THREAD_ONLY;
    params.mLabel = "AttrArray";
    return moz_create_arena_with_params(&params);
  }();
  return moz_arena_malloc(sArena, aSize);
}

AttrArray::Impl::~Impl() {
  for (InternalAttr& attr : Attrs()) {
    attr.~InternalAttr();
  }
  if (auto* decl = GetMappedDeclarationBlock()) {
    Servo_DeclarationBlock_Release(decl);
    mMappedAttributeBits = 0;
  }
}

void AttrArray::SetMappedDeclarationBlock(
    already_AddRefed<mozilla::StyleLockedDeclarationBlock> aBlock) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(HasImpl());
  MOZ_ASSERT(IsPendingMappedAttributeEvaluation());
  if (auto* decl = GetMappedDeclarationBlock()) {
    Servo_DeclarationBlock_Release(decl);
  }
  GetImpl()->mMappedAttributeBits = reinterpret_cast<uintptr_t>(aBlock.take());
  MOZ_ASSERT(!IsPendingMappedAttributeEvaluation());
}

const nsAttrValue* AttrArray::GetAttr(const nsAtom* aLocalName) const {
  NS_ASSERTION(aLocalName, "Must have attr name");
  for (const InternalAttr& attr : Attrs()) {
    if (attr.mName.Equals(aLocalName)) {
      return &attr.mValue;
    }
  }
  return nullptr;
}

const nsAttrValue* AttrArray::GetAttr(const nsAtom* aLocalName,
                                      int32_t aNamespaceID) const {
  NS_ASSERTION(aLocalName, "Must have attr name");
  NS_ASSERTION(aNamespaceID != kNameSpaceID_Unknown, "Must have namespace");
  if (aNamespaceID == kNameSpaceID_None) {
    // This should be the common case so lets use the optimized loop
    return GetAttr(aLocalName);
  }
  for (const InternalAttr& attr : Attrs()) {
    if (attr.mName.Equals(aLocalName, aNamespaceID)) {
      return &attr.mValue;
    }
  }
  return nullptr;
}

const nsAttrValue* AttrArray::GetAttr(const nsAString& aLocalName) const {
  for (const InternalAttr& attr : Attrs()) {
    if (attr.mName.Equals(aLocalName)) {
      return &attr.mValue;
    }
  }
  return nullptr;
}

const nsAttrValue* AttrArray::GetAttr(const nsAString& aName,
                                      nsCaseTreatment aCaseSensitive) const {
  // Check whether someone is being silly and passing non-lowercase
  // attr names.
  if (aCaseSensitive == eIgnoreCase &&
      nsContentUtils::StringContainsASCIIUpper(aName)) {
    // Try again with a lowercased name, but make sure we can't reenter this
    // block by passing eCaseSensitive for aCaseSensitive.
    nsAutoString lowercase;
    nsContentUtils::ASCIIToLower(aName, lowercase);
    return GetAttr(lowercase, eCaseMatters);
  }

  for (const InternalAttr& attr : Attrs()) {
    if (attr.mName.QualifiedNameEquals(aName)) {
      return &attr.mValue;
    }
  }

  return nullptr;
}

const nsAttrValue* AttrArray::AttrAt(uint32_t aPos) const {
  NS_ASSERTION(aPos < AttrCount(), "out-of-bounds access in AttrArray");
  return &GetImpl()->Attrs()[aPos].mValue;
}

template <typename Name>
inline nsresult AttrArray::AddNewAttribute(Name* aName, nsAttrValue& aValue) {
  MOZ_ASSERT(!HasImpl() || GetImpl()->mCapacity >= GetImpl()->mAttrCount);
  if (!HasImpl() || GetImpl()->mCapacity == GetImpl()->mAttrCount) {
    if (!GrowBy(1)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  InternalAttr& attr = mImpl->mBuffer[mImpl->mAttrCount++];
  new (&attr.mName) nsAttrName(aName);
  new (&attr.mValue) nsAttrValue();
  attr.mValue.SwapValueWith(aValue);
  return NS_OK;
}

const nsAttrValue* AttrArray::AddNewAttributeAssumeAvailableSlot(
    RefPtr<nsAtom>& aName, nsAttrValue& aValue) {
  MOZ_ASSERT(HasImpl());
  MOZ_ASSERT(GetImpl()->mAttrCount < GetImpl()->mCapacity);
  InternalAttr& attr = mImpl->mBuffer[mImpl->mAttrCount++];
  new (&attr.mName) nsAttrName(aName.forget());
  new (&attr.mValue) nsAttrValue();
  attr.mValue.SwapValueWith(aValue);
  return &attr.mValue;
}

nsresult AttrArray::SetAndSwapAttr(nsAtom* aLocalName, nsAttrValue& aValue,
                                   bool* aHadValue,
                                   mozilla::dom::IsKnownNewAttr aIsKnownNew) {
  *aHadValue = false;

  if (aIsKnownNew == mozilla::dom::IsKnownNewAttr::No) {
    for (InternalAttr& attr : Attrs()) {
      if (attr.mName.Equals(aLocalName)) {
        attr.mValue.SwapValueWith(aValue);
        *aHadValue = true;
        return NS_OK;
      }
    }
  } else {
    MOZ_ASSERT(IndexOfAttr(aLocalName) == -1,
               "Caller asserted attribute is new but it already exists");
  }

  return AddNewAttribute(aLocalName, aValue);
}

nsresult AttrArray::SetAndSwapAttr(mozilla::dom::NodeInfo* aName,
                                   nsAttrValue& aValue, bool* aHadValue,
                                   mozilla::dom::IsKnownNewAttr aIsKnownNew) {
  int32_t namespaceID = aName->NamespaceID();
  nsAtom* localName = aName->NameAtom();
  if (namespaceID == kNameSpaceID_None) {
    return SetAndSwapAttr(localName, aValue, aHadValue, aIsKnownNew);
  }

  *aHadValue = false;
  if (aIsKnownNew == mozilla::dom::IsKnownNewAttr::No) {
    for (InternalAttr& attr : Attrs()) {
      if (attr.mName.Equals(localName, namespaceID)) {
        attr.mName.SetTo(aName);
        attr.mValue.SwapValueWith(aValue);
        *aHadValue = true;
        return NS_OK;
      }
    }
  } else {
    MOZ_ASSERT(IndexOfAttr(localName, namespaceID) == -1,
               "Caller asserted attribute is new but it already exists");
  }

  return AddNewAttribute(aName, aValue);
}

nsresult AttrArray::RemoveAttrAt(uint32_t aPos, nsAttrValue& aValue) {
  NS_ASSERTION(aPos < AttrCount(), "out-of-bounds");

  Impl* impl = GetImpl();
  impl->mBuffer[aPos].mValue.SwapValueWith(aValue);
  impl->mBuffer[aPos].~InternalAttr();

  // InternalAttr are not trivially copyable *but* we manually called the
  // destructor so the memmove should be ok.
  memmove((void*)(impl->mBuffer + aPos), impl->mBuffer + aPos + 1,
          (impl->mAttrCount - aPos - 1) * sizeof(InternalAttr));

  --impl->mAttrCount;
  return NS_OK;
}

mozilla::dom::BorrowedAttrInfo AttrArray::AttrInfoAt(uint32_t aPos) const {
  NS_ASSERTION(aPos < AttrCount(), "out-of-bounds access in AttrArray");
  const Impl* impl = GetImpl();
  return BorrowedAttrInfo(&impl->mBuffer[aPos].mName,
                          &impl->mBuffer[aPos].mValue);
}

const nsAttrName* AttrArray::AttrNameAt(uint32_t aPos) const {
  NS_ASSERTION(aPos < AttrCount(), "out-of-bounds access in AttrArray");
  return &GetImpl()->mBuffer[aPos].mName;
}

[[nodiscard]] bool AttrArray::GetSafeAttrNameAt(
    uint32_t aPos, const nsAttrName** aResult) const {
  if (aPos >= AttrCount()) {
    return false;
  }
  *aResult = &GetImpl()->mBuffer[aPos].mName;
  return true;
}

const nsAttrName* AttrArray::GetSafeAttrNameAt(uint32_t aPos) const {
  const nsAttrName* name;
  if (!GetSafeAttrNameAt(aPos, &name)) {
    MOZ_CRASH("aPos out of bounds");
  }
  return name;
}

const nsAttrName* AttrArray::GetExistingAttrNameFromQName(
    const nsAString& aName, RefPtr<nsAtom>* aOutAtom) const {
  if (aOutAtom) {
    *aOutAtom = nullptr;
  }
  if (!HasAttrs()) {
    return nullptr;
  }
  RefPtr<nsAtom> nameAtom = NS_AtomizeMainThread(aName);
  for (const InternalAttr& attr : Attrs()) {
    // Equals(nsAtom*) compares mBits; for NodeInfo-typed entries mBits has the
    // tag bit set and can never equal a clean atom pointer, so this is safe
    // for both kinds without branching on IsAtom().
    if (attr.mName.Equals(nameAtom.get())) {
      return &attr.mName;
    }
    if (!attr.mName.IsAtom() && attr.mName.QualifiedNameEquals(aName)) {
      return &attr.mName;
    }
  }
  if (aOutAtom) {
    *aOutAtom = nameAtom.forget();
  }
  return nullptr;
}

int32_t AttrArray::IndexOfAttr(const nsAtom* aLocalName) const {
  int32_t i = 0;
  for (const InternalAttr& attr : Attrs()) {
    if (attr.mName.Equals(aLocalName)) {
      return i;
    }
    ++i;
  }
  return -1;
}

int32_t AttrArray::IndexOfAttr(const nsAtom* aLocalName,
                               int32_t aNamespaceID) const {
  if (aNamespaceID == kNameSpaceID_None) {
    // This should be the common case so lets use the optimized loop
    return IndexOfAttr(aLocalName);
  }
  int32_t i = 0;
  for (const InternalAttr& attr : Attrs()) {
    if (attr.mName.Equals(aLocalName, aNamespaceID)) {
      return i;
    }
    ++i;
  }
  return -1;
}

void AttrArray::NodeInfoChanged(nsNodeInfoManager* aManager) {
  for (InternalAttr& attr : Attrs()) {
    if (attr.mName.IsAtom()) {
      continue;
    }
    mozilla::dom::NodeInfo* oldNi = attr.mName.NodeInfo();
    if (oldNi->NodeInfoManager() == aManager) {
      continue;
    }
    RefPtr<mozilla::dom::NodeInfo> ni =
        aManager->GetNodeInfo(oldNi->NameAtom(), oldNi->GetPrefixAtom(),
                              oldNi->NamespaceID(), oldNi->NodeType());
    attr.mName.SetTo(ni);
  }
}

nsresult AttrArray::EnsureCapacityToClone(const AttrArray& aOther) {
  MOZ_ASSERT(!HasImpl(),
             "AttrArray::EnsureCapacityToClone requires the array be empty "
             "when called");

  uint32_t attrCount = aOther.AttrCount();
  if (!attrCount) {
    return NS_OK;
  }

  // No need to use a CheckedUint32 because we are cloning. We know that we
  // have already allocated an AttrArray of this size.
  Impl* impl = static_cast<Impl*>(
      AllocateImpl(Impl::AllocationSizeForAttributes(attrCount)));
  NS_ENSURE_TRUE(impl, NS_ERROR_OUT_OF_MEMORY);

  impl->mMappedAttributeBits = 0;
  impl->mCapacity = attrCount;
  impl->mAttrCount = 0;
  impl->mSubtreeBloomFilter = aOther.GetSubtreeBloomFilter();
  SetImpl(impl);

  return NS_OK;
}

bool AttrArray::GrowBy(uint32_t aGrowSize) {
  const uint32_t kLinearThreshold = 16;
  const uint32_t kLinearGrowSize = 4;

  CheckedUint32 capacity = HasImpl() ? GetImpl()->mCapacity : 0;
  CheckedUint32 minCapacity = capacity;
  minCapacity += aGrowSize;
  if (!minCapacity.isValid()) {
    return false;
  }

  if (capacity.value() <= kLinearThreshold) {
    do {
      capacity += kLinearGrowSize;
      if (!capacity.isValid()) {
        return false;
      }
    } while (capacity.value() < minCapacity.value());
  } else {
    uint32_t shift = mozilla::CeilingLog2(minCapacity.value());
    if (shift >= 32) {
      return false;
    }
    capacity = 1u << shift;
  }

  return GrowTo(capacity.value());
}

bool AttrArray::GrowTo(uint32_t aCapacity) {
  uint32_t oldCapacity = HasImpl() ? GetImpl()->mCapacity : 0;
  if (aCapacity <= oldCapacity) {
    return true;
  }

  CheckedUint32 sizeInBytes = aCapacity;
  sizeInBytes *= sizeof(InternalAttr);
  if (!sizeInBytes.isValid()) {
    return false;
  }

  sizeInBytes += sizeof(Impl);
  if (!sizeInBytes.isValid()) {
    return false;
  }

  MOZ_ASSERT(sizeInBytes.value() ==
             Impl::AllocationSizeForAttributes(aCapacity));

  const bool needToInitialize = !HasImpl();
  uint64_t oldBloom = 0xFFFFFFFFFFFFFFFFULL;
  Impl* oldImpl = nullptr;

  if (HasImpl()) {
    // We have a real Impl pointer, extract it for realloc
    oldImpl = mImpl.release();
  } else if (HasTaggedBloom()) {
    // Preserve bloom filter from the tagged value
    oldBloom = GetTaggedBloom();
  }

  Impl* newImpl =
      static_cast<Impl*>(oldImpl ? realloc(oldImpl, sizeInBytes.value())
                                 : AllocateImpl(sizeInBytes.value()));
  if (!newImpl) {
    if (oldImpl) {
      SetImpl(oldImpl);
    } else if (HasTaggedBloom()) {
      SetTaggedBloom(oldBloom);
    }
    return false;
  }

  // Set initial counts if we didn't have a buffer before
  if (needToInitialize) {
    newImpl->mMappedAttributeBits = 0;
    newImpl->mAttrCount = 0;
    newImpl->mSubtreeBloomFilter = oldBloom;
  }

  newImpl->mCapacity = aCapacity;
  SetImpl(newImpl);
  return true;
}

size_t AttrArray::SizeOfExcludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  if (!HasImpl()) {
    return 0;
  }
  size_t n = aMallocSizeOf(GetImpl());
  for (const InternalAttr& attr : Attrs()) {
    n += attr.mValue.SizeOfExcludingThis(aMallocSizeOf);
  }
  return n;
}

int32_t AttrArray::FindAttrValueIn(int32_t aNameSpaceID, const nsAtom* aName,
                                   AttrValuesArray* aValues,
                                   nsCaseTreatment aCaseSensitive) const {
  NS_ASSERTION(aName, "Must have attr name");
  NS_ASSERTION(aNameSpaceID != kNameSpaceID_Unknown, "Must have namespace");
  NS_ASSERTION(aValues, "Null value array");

  const nsAttrValue* val = GetAttr(aName, aNameSpaceID);
  if (val) {
    for (int32_t i = 0; aValues[i]; ++i) {
      if (val->Equals(aValues[i], aCaseSensitive)) {
        return i;
      }
    }
    return ATTR_VALUE_NO_MATCH;
  }
  return ATTR_MISSING;
}
