/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * A struct that represents the value (type and actual data) of an
 * attribute.
 */

#include "nsAttrValue.h"

#include <algorithm>

#include "ReferrerInfo.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/AttributeStyles.h"
#include "mozilla/BloomFilter.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/DeclarationBlock.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/SVGAttrValueWrapper.h"
#include "mozilla/ServoBindingTypes.h"
#include "mozilla/ServoUtils.h"
#include "mozilla/ShadowParts.h"
#include "mozilla/URLExtraData.h"
#include "mozilla/dom/Document.h"
#include "nsAttrValueInlines.h"
#include "nsContentUtils.h"
#include "nsIURI.h"
#include "nsReadableUtils.h"
#include "nsStyledElement.h"
#include "nsUnicharUtils.h"

using namespace mozilla;

constexpr uint32_t kMiscContainerCacheSize = 128;
static void* gMiscContainerCache[kMiscContainerCacheSize];
static uint32_t gMiscContainerCount = 0;

/**
 * Global cache for eAtomArray MiscContainer objects, to speed up the parsing
 * of class attributes with multiple class names.
 * This cache doesn't keep anything alive - a MiscContainer removes itself from
 * the cache once its last reference is dropped.
 */
struct AtomArrayCache {
  // We don't keep any strong references, neither to the atom nor to the
  // MiscContainer. The MiscContainer removes itself from the cache when
  // the last reference to it is dropped, and the atom is kept alive by
  // the MiscContainer.
  using MapType = nsTHashMap<nsAtom*, MiscContainer*>;

  static MiscContainer* Lookup(nsAtom* aValue) {
    if (auto* instance = GetInstance()) {
      return instance->LookupImpl(aValue);
    }
    return nullptr;
  }

  static void Insert(nsAtom* aValue, MiscContainer* aCont) {
    if (auto* instance = GetInstance()) {
      instance->InsertImpl(aValue, aCont);
    }
  }

  static void Remove(nsAtom* aValue) {
    if (auto* instance = GetInstance()) {
      instance->RemoveImpl(aValue);
    }
  }

  static AtomArrayCache* GetInstance() {
    static StaticAutoPtr<AtomArrayCache> sInstance;
    if (!sInstance && !PastShutdownPhase(ShutdownPhase::XPCOMShutdownFinal)) {
      sInstance = new AtomArrayCache();
      ClearOnShutdown(&sInstance, ShutdownPhase::XPCOMShutdownFinal);
    }
    return sInstance;
  }

 private:
  MiscContainer* LookupImpl(nsAtom* aValue) {
    auto lookupResult = mMap.Lookup(aValue);
    return lookupResult ? *lookupResult : nullptr;
  }

  void InsertImpl(nsAtom* aValue, MiscContainer* aCont) {
    MOZ_ASSERT(aCont);
    mMap.InsertOrUpdate(aValue, aCont);
  }

  void RemoveImpl(nsAtom* aValue) { mMap.Remove(aValue); }

  MapType mMap;
};

/* static */
MiscContainer* nsAttrValue::AllocMiscContainer() {
  MOZ_ASSERT(NS_IsMainThread());

  static_assert(sizeof(gMiscContainerCache) <= 1024);
  static_assert(sizeof(MiscContainer) <= 32);

  // Allocate MiscContainer objects in batches to improve performance.
  if (gMiscContainerCount == 0) {
    for (; gMiscContainerCount < kMiscContainerCacheSize;
         ++gMiscContainerCount) {
      gMiscContainerCache[gMiscContainerCount] =
          moz_xmalloc(sizeof(MiscContainer));
    }
  }

  return new (gMiscContainerCache[--gMiscContainerCount]) MiscContainer();
}

/* static */
void nsAttrValue::DeallocMiscContainer(MiscContainer* aCont) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!aCont) {
    return;
  }

  aCont->~MiscContainer();

  if (gMiscContainerCount < kMiscContainerCacheSize) {
    gMiscContainerCache[gMiscContainerCount++] = aCont;
    return;
  }

  free(aCont);
}

bool MiscContainer::GetString(nsAString& aString) const {
  bool isString;
  void* ptr = GetStringOrAtomPtr(isString);
  if (!ptr) {
    return false;
  }
  if (isString) {
    auto* buffer = static_cast<mozilla::StringBuffer*>(ptr);
    aString.Assign(buffer, buffer->StorageSize() / sizeof(char16_t) - 1);
  } else {
    static_cast<nsAtom*>(ptr)->ToString(aString);
  }
  return true;
}

void MiscContainer::Cache() {
  switch (mType) {
    case nsAttrValue::eCSSDeclaration: {
      MOZ_ASSERT(IsRefCounted());
      MOZ_ASSERT(mValue.mRefCount > 0);
      MOZ_ASSERT(!mValue.mCached);

      AttributeStyles* attrStyles =
          mValue.mCSSDeclaration->GetAttributeStyles();
      if (!attrStyles) {
        return;
      }

      nsString str;
      bool gotString = GetString(str);
      if (!gotString) {
        return;
      }

      attrStyles->CacheStyleAttr(str, this);
      mValue.mCached = 1;

      // This has to be immutable once it goes into the cache.
      mValue.mCSSDeclaration->SetImmutable();
      break;
    }
    case nsAttrValue::eAtomArray: {
      MOZ_ASSERT(IsRefCounted());
      MOZ_ASSERT(mValue.mRefCount > 0);
      MOZ_ASSERT(!mValue.mCached);

      nsAtom* atom = GetStoredAtom();
      if (!atom) {
        return;
      }

      AtomArrayCache::Insert(atom, this);
      mValue.mCached = 1;
      break;
    }
    default:
      MOZ_ASSERT_UNREACHABLE("unexpected cached nsAttrValue type");
      break;
  }
}

void MiscContainer::Evict() {
  switch (mType) {
    case nsAttrValue::eCSSDeclaration: {
      MOZ_ASSERT(IsRefCounted());
      MOZ_ASSERT(mValue.mRefCount == 0);

      if (!mValue.mCached) {
        return;
      }

      AttributeStyles* attrStyles =
          mValue.mCSSDeclaration->GetAttributeStyles();
      MOZ_ASSERT(attrStyles);

      nsString str;
      DebugOnly<bool> gotString = GetString(str);
      MOZ_ASSERT(gotString);

      attrStyles->EvictStyleAttr(str, this);
      mValue.mCached = 0;
      break;
    }
    case nsAttrValue::eAtomArray: {
      MOZ_ASSERT(IsRefCounted());
      MOZ_ASSERT(mValue.mRefCount == 0);

      if (!mValue.mCached) {
        return;
      }

      nsAtom* atom = GetStoredAtom();
      MOZ_ASSERT(atom);

      AtomArrayCache::Remove(atom);

      mValue.mCached = 0;
      break;
    }
    default:

      MOZ_ASSERT_UNREACHABLE("unexpected cached nsAttrValue type");
      break;
  }
}

nsTArray<nsAttrValue::EnumTableSpan>* nsAttrValue::sEnumTableArray = nullptr;

nsAttrValue::nsAttrValue() : mBits(0) {}

nsAttrValue::nsAttrValue(const nsAttrValue& aOther) : mBits(0) {
  SetTo(aOther);
}

nsAttrValue::nsAttrValue(const nsAString& aValue) : mBits(0) { SetTo(aValue); }

nsAttrValue::nsAttrValue(nsAtom* aValue) : mBits(0) { SetTo(aValue); }

nsAttrValue::nsAttrValue(already_AddRefed<DeclarationBlock> aValue,
                         const nsAString* aSerialized)
    : mBits(0) {
  SetTo(std::move(aValue), aSerialized);
}

nsAttrValue::~nsAttrValue() { ResetIfSet(); }

/* static */
void nsAttrValue::Init() {
  MOZ_ASSERT(!sEnumTableArray, "nsAttrValue already initialized");
  sEnumTableArray = new nsTArray<EnumTableSpan>;
}

/* static */
void nsAttrValue::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  delete sEnumTableArray;
  sEnumTableArray = nullptr;

  for (uint32_t i = 0; i < gMiscContainerCount; ++i) {
    free(gMiscContainerCache[i]);
  }
  gMiscContainerCount = 0;
}

void nsAttrValue::Reset() {
  switch (BaseType()) {
    case eStringBase: {
      if (auto* str = static_cast<mozilla::StringBuffer*>(GetPtr())) {
        str->Release();
      }
      break;
    }
    case eOtherBase: {
      MiscContainer* cont = GetMiscContainer();
      if (cont->IsRefCounted() && cont->mValue.mRefCount > 1) {
        NS_RELEASE(cont);
        break;
      }

      DeallocMiscContainer(ClearMiscContainer());

      break;
    }
    case eAtomBase: {
      nsAtom* atom = GetAtomValue();
      NS_RELEASE(atom);

      break;
    }
    case eIntegerBase: {
      break;
    }
  }

  mBits = 0;
}

void nsAttrValue::SetTo(const nsAttrValue& aOther) {
  if (this == &aOther) {
    return;
  }

  switch (aOther.BaseType()) {
    case eStringBase: {
      ResetIfSet();
      if (auto* str = static_cast<mozilla::StringBuffer*>(aOther.GetPtr())) {
        str->AddRef();
        SetPtrValueAndType(str, eStringBase);
      }
      return;
    }
    case eOtherBase: {
      break;
    }
    case eAtomBase: {
      ResetIfSet();
      nsAtom* atom = aOther.GetAtomValue();
      NS_ADDREF(atom);
      SetPtrValueAndType(atom, eAtomBase);
      return;
    }
    case eIntegerBase: {
      ResetIfSet();
      mBits = aOther.mBits;
      return;
    }
  }

  MiscContainer* otherCont = aOther.GetMiscContainer();
  if (otherCont->IsRefCounted()) {
    DeallocMiscContainer(ClearMiscContainer());
    NS_ADDREF(otherCont);
    SetPtrValueAndType(otherCont, eOtherBase);
    return;
  }

  MiscContainer* cont = EnsureEmptyMiscContainer();
  switch (otherCont->mType) {
    case eInteger: {
      cont->mValue.mInteger = otherCont->mValue.mInteger;
      break;
    }
    case eEnum: {
      cont->mValue.mEnumValue = otherCont->mValue.mEnumValue;
      break;
    }
    case ePercent: {
      cont->mDoubleValue = otherCont->mDoubleValue;
      break;
    }
    case eColor: {
      cont->mValue.mColor = otherCont->mValue.mColor;
      break;
    }
    case eAtomArray:
    case eShadowParts:
    case eCSSDeclaration: {
      MOZ_CRASH("These should be refcounted!");
    }
    case eURL: {
      NS_ADDREF(cont->mValue.mURL = otherCont->mValue.mURL);
      break;
    }
    case eDoubleValue: {
      cont->mDoubleValue = otherCont->mDoubleValue;
      break;
    }
    default: {
      if (IsSVGType(otherCont->mType)) {
        // All SVG types are just pointers to classes and will therefore have
        // the same size so it doesn't really matter which one we assign
        cont->mValue.mSVGLength = otherCont->mValue.mSVGLength;
      } else {
        MOZ_ASSERT_UNREACHABLE("unknown type stored in MiscContainer");
      }
      break;
    }
  }

  bool isString;
  if (void* otherPtr = otherCont->GetStringOrAtomPtr(isString)) {
    if (isString) {
      static_cast<mozilla::StringBuffer*>(otherPtr)->AddRef();
    } else {
      static_cast<nsAtom*>(otherPtr)->AddRef();
    }
    cont->SetStringBitsMainThread(otherCont->mStringBits);
  }
  // Note, set mType after switch-case, otherwise EnsureEmptyAtomArray doesn't
  // work correctly.
  cont->mType = otherCont->mType;
}

void nsAttrValue::SetTo(const nsAString& aValue) {
  ResetIfSet();
  mozilla::StringBuffer* buf = GetStringBuffer(aValue).take();
  if (buf) {
    SetPtrValueAndType(buf, eStringBase);
  }
}

void nsAttrValue::SetToAssumeUnset(
    already_AddRefed<mozilla::StringBuffer> aValue) {
  MOZ_ASSERT(!mBits);
  SetPtrValueAndType(aValue.take(), eStringBase);
}

void nsAttrValue::SetTo(nsAtom* aValue) {
  ResetIfSet();
  if (aValue) {
    NS_ADDREF(aValue);
    SetPtrValueAndType(aValue, eAtomBase);
  }
}

void nsAttrValue::SetToAssumeUnset(already_AddRefed<nsAtom> aValue) {
  MOZ_ASSERT(!mBits);
  SetPtrValueAndType(aValue.take(), eAtomBase);
}

void nsAttrValue::SetTo(int16_t aInt) {
  ResetIfSet();
  SetIntValueAndType(aInt, eInteger, nullptr);
}

void nsAttrValue::SetTo(int32_t aInt, const nsAString* aSerialized) {
  ResetIfSet();
  SetIntValueAndType(aInt, eInteger, aSerialized);
}

void nsAttrValue::SetTo(double aValue, const nsAString* aSerialized) {
  MiscContainer* cont = EnsureEmptyMiscContainer();
  cont->mDoubleValue = aValue;
  cont->mType = eDoubleValue;
  SetMiscAtomOrString(aSerialized);
}

void nsAttrValue::SetTo(already_AddRefed<DeclarationBlock> aValue,
                        const nsAString* aSerialized) {
  MiscContainer* cont = EnsureEmptyMiscContainer();
  MOZ_ASSERT(cont->mValue.mRefCount == 0);
  cont->mValue.mCSSDeclaration = aValue.take();
  cont->mType = eCSSDeclaration;
  NS_ADDREF(cont);
  SetMiscAtomOrString(aSerialized);
  MOZ_ASSERT(cont->mValue.mRefCount == 1);
}

void nsAttrValue::SetTo(nsIURI* aValue, const nsAString* aSerialized) {
  MiscContainer* cont = EnsureEmptyMiscContainer();
  NS_ADDREF(cont->mValue.mURL = aValue);
  cont->mType = eURL;
  SetMiscAtomOrString(aSerialized);
}

void nsAttrValue::SetToSerialized(const nsAttrValue& aOther) {
  if (aOther.Type() != nsAttrValue::eString &&
      aOther.Type() != nsAttrValue::eAtom) {
    nsAutoString val;
    aOther.ToString(val);
    SetTo(val);
  } else {
    SetTo(aOther);
  }
}

void nsAttrValue::SetTo(const SVGAnimatedOrient& aValue,
                        const nsAString* aSerialized) {
  SetSVGType(eSVGOrient, &aValue, aSerialized);
}

void nsAttrValue::SetTo(const SVGAnimatedIntegerPair& aValue,
                        const nsAString* aSerialized) {
  SetSVGType(eSVGIntegerPair, &aValue, aSerialized);
}

void nsAttrValue::SetTo(const SVGAnimatedLength& aValue,
                        const nsAString* aSerialized) {
  SetSVGType(eSVGLength, &aValue, aSerialized);
}

void nsAttrValue::SetTo(const SVGLengthList& aValue,
                        const nsAString* aSerialized) {
  // While an empty string will parse as a length list, there's no need to store
  // it (and SetMiscAtomOrString will assert if we try)
  if (aSerialized && aSerialized->IsEmpty()) {
    aSerialized = nullptr;
  }
  SetSVGType(eSVGLengthList, &aValue, aSerialized);
}

void nsAttrValue::SetTo(const SVGNumberList& aValue,
                        const nsAString* aSerialized) {
  // While an empty string will parse as a number list, there's no need to store
  // it (and SetMiscAtomOrString will assert if we try)
  if (aSerialized && aSerialized->IsEmpty()) {
    aSerialized = nullptr;
  }
  SetSVGType(eSVGNumberList, &aValue, aSerialized);
}

void nsAttrValue::SetTo(const SVGAnimatedNumberPair& aValue,
                        const nsAString* aSerialized) {
  SetSVGType(eSVGNumberPair, &aValue, aSerialized);
}

void nsAttrValue::SetTo(const SVGPathData& aValue,
                        const nsAString* aSerialized) {
  // While an empty string will parse as path data, there's no need to store it
  // (and SetMiscAtomOrString will assert if we try)
  if (aSerialized && aSerialized->IsEmpty()) {
    aSerialized = nullptr;
  }
  SetSVGType(eSVGPathData, &aValue, aSerialized);
}

void nsAttrValue::SetTo(const SVGPointList& aValue,
                        const nsAString* aSerialized) {
  // While an empty string will parse as a point list, there's no need to store
  // it (and SetMiscAtomOrString will assert if we try)
  if (aSerialized && aSerialized->IsEmpty()) {
    aSerialized = nullptr;
  }
  SetSVGType(eSVGPointList, &aValue, aSerialized);
}

void nsAttrValue::SetTo(const SVGAnimatedPreserveAspectRatio& aValue,
                        const nsAString* aSerialized) {
  SetSVGType(eSVGPreserveAspectRatio, &aValue, aSerialized);
}

void nsAttrValue::SetTo(const SVGStringList& aValue,
                        const nsAString* aSerialized) {
  // While an empty string will parse as a string list, there's no need to store
  // it (and SetMiscAtomOrString will assert if we try)
  if (aSerialized && aSerialized->IsEmpty()) {
    aSerialized = nullptr;
  }
  SetSVGType(eSVGStringList, &aValue, aSerialized);
}

void nsAttrValue::SetTo(const SVGTransformList& aValue,
                        const nsAString* aSerialized) {
  // While an empty string will parse as a transform list, there's no need to
  // store it (and SetMiscAtomOrString will assert if we try)
  if (aSerialized && aSerialized->IsEmpty()) {
    aSerialized = nullptr;
  }
  SetSVGType(eSVGTransformList, &aValue, aSerialized);
}

void nsAttrValue::SetTo(const SVGAnimatedViewBox& aValue,
                        const nsAString* aSerialized) {
  SetSVGType(eSVGViewBox, &aValue, aSerialized);
}

void nsAttrValue::SwapValueWith(nsAttrValue& aOther) {
  uintptr_t tmp = aOther.mBits;
  aOther.mBits = mBits;
  mBits = tmp;
}

void nsAttrValue::RemoveDuplicatesFromAtomArray() {
  if (Type() != eAtomArray) {
    return;
  }

  const AttrAtomArray* currentAtomArray = GetMiscContainer()->mValue.mAtomArray;
  UniquePtr<AttrAtomArray> deduplicatedAtomArray =
      currentAtomArray->CreateDeduplicatedCopyIfDifferent();

  if (!deduplicatedAtomArray) {
    // No duplicates found. Leave this value unchanged.
    return;
  }

  // We found duplicates. Wrap the new atom array into a fresh MiscContainer,
  // and copy over the existing container's string or atom.

  MiscContainer* oldCont = GetMiscContainer();
  MOZ_ASSERT(oldCont->IsRefCounted());

  uintptr_t stringBits = 0;
  bool isString = false;
  if (void* otherPtr = oldCont->GetStringOrAtomPtr(isString)) {
    stringBits = oldCont->mStringBits;
    if (isString) {
      static_cast<mozilla::StringBuffer*>(otherPtr)->AddRef();
    } else {
      static_cast<nsAtom*>(otherPtr)->AddRef();
    }
  }

  MiscContainer* cont = EnsureEmptyMiscContainer();
  MOZ_ASSERT(cont->mValue.mRefCount == 0);
  cont->mValue.mAtomArray = deduplicatedAtomArray.release();
  cont->mType = eAtomArray;
  NS_ADDREF(cont);
  MOZ_ASSERT(cont->mValue.mRefCount == 1);
  cont->SetStringBitsMainThread(stringBits);

  // Don't cache the new container. It would stomp over the undeduplicated
  // value in the cache. But we could have a separate cache for deduplicated
  // atom arrays, if repeated deduplication shows up in profiles.
}

void nsAttrValue::ToString(nsAString& aResult) const {
  MiscContainer* cont = nullptr;
  if (BaseType() == eOtherBase) {
    cont = GetMiscContainer();

    if (cont->GetString(aResult)) {
      return;
    }
  }

  switch (Type()) {
    case eString: {
      if (auto* str = static_cast<mozilla::StringBuffer*>(GetPtr())) {
        aResult.Assign(str, str->StorageSize() / sizeof(char16_t) - 1);
      } else {
        aResult.Truncate();
      }
      break;
    }
    case eAtom: {
      auto* atom = static_cast<nsAtom*>(GetPtr());
      atom->ToString(aResult);
      break;
    }
    case eInteger: {
      nsAutoString intStr;
      intStr.AppendInt(GetIntegerValue());
      aResult = std::move(intStr);

      break;
    }
#ifdef DEBUG
    case eColor: {
      MOZ_ASSERT_UNREACHABLE("color attribute without string data");
      aResult.Truncate();
      break;
    }
#endif
    case eEnum: {
      GetEnumString(aResult, false);
      break;
    }
    case ePercent: {
      nsAutoString str;
      if (cont) {
        str.AppendFloat(cont->mDoubleValue);
      } else {
        str.AppendInt(GetIntInternal());
      }
      aResult = str + u"%"_ns;

      break;
    }
    case eCSSDeclaration: {
      aResult.Truncate();
      MiscContainer* container = GetMiscContainer();
      if (DeclarationBlock* decl = container->mValue.mCSSDeclaration) {
        nsAutoCString result;
        decl->ToString(result);
        CopyUTF8toUTF16(result, aResult);
      }

      // This can be reached during parallel selector matching with attribute
      // selectors on the style attribute. SetMiscAtomOrString handles this
      // case, and as of this writing this is the only consumer that needs it.
      const_cast<nsAttrValue*>(this)->SetMiscAtomOrString(&aResult);

      break;
    }
    case eDoubleValue: {
      aResult.Truncate();
      aResult.AppendFloat(GetDoubleValue());
      break;
    }
    case eSVGIntegerPair: {
      SVGAttrValueWrapper::ToString(
          GetMiscContainer()->mValue.mSVGAnimatedIntegerPair, aResult);
      break;
    }
    case eSVGOrient: {
      SVGAttrValueWrapper::ToString(
          GetMiscContainer()->mValue.mSVGAnimatedOrient, aResult);
      break;
    }
    case eSVGLength: {
      SVGAttrValueWrapper::ToString(GetMiscContainer()->mValue.mSVGLength,
                                    aResult);
      break;
    }
    case eSVGLengthList: {
      SVGAttrValueWrapper::ToString(GetMiscContainer()->mValue.mSVGLengthList,
                                    aResult);
      break;
    }
    case eSVGNumberList: {
      SVGAttrValueWrapper::ToString(GetMiscContainer()->mValue.mSVGNumberList,
                                    aResult);
      break;
    }
    case eSVGNumberPair: {
      SVGAttrValueWrapper::ToString(
          GetMiscContainer()->mValue.mSVGAnimatedNumberPair, aResult);
      break;
    }
    case eSVGPathData: {
      SVGAttrValueWrapper::ToString(GetMiscContainer()->mValue.mSVGPathData,
                                    aResult);
      break;
    }
    case eSVGPointList: {
      SVGAttrValueWrapper::ToString(GetMiscContainer()->mValue.mSVGPointList,
                                    aResult);
      break;
    }
    case eSVGPreserveAspectRatio: {
      SVGAttrValueWrapper::ToString(
          GetMiscContainer()->mValue.mSVGAnimatedPreserveAspectRatio, aResult);
      break;
    }
    case eSVGStringList: {
      SVGAttrValueWrapper::ToString(GetMiscContainer()->mValue.mSVGStringList,
                                    aResult);
      break;
    }
    case eSVGTransformList: {
      SVGAttrValueWrapper::ToString(
          GetMiscContainer()->mValue.mSVGTransformList, aResult);
      break;
    }
    case eSVGViewBox: {
      SVGAttrValueWrapper::ToString(
          GetMiscContainer()->mValue.mSVGAnimatedViewBox, aResult);
      break;
    }
    default: {
      aResult.Truncate();
      break;
    }
  }
}

already_AddRefed<nsAtom> nsAttrValue::GetAsAtom() const {
  switch (Type()) {
    case eString:
      return NS_AtomizeMainThread(GetStringValue());

    case eAtom: {
      RefPtr<nsAtom> atom = GetAtomValue();
      return atom.forget();
    }

    default: {
      nsAutoString val;
      ToString(val);
      return NS_AtomizeMainThread(val);
    }
  }
}

const nsCheapString nsAttrValue::GetStringValue() const {
  MOZ_ASSERT(Type() == eString, "wrong type");

  return nsCheapString(static_cast<mozilla::StringBuffer*>(GetPtr()));
}

bool nsAttrValue::GetColorValue(nscolor& aColor) const {
  if (Type() != eColor) {
    // Unparseable value, treat as unset.
    NS_ASSERTION(Type() == eString, "unexpected type for color-valued attr");
    return false;
  }

  aColor = GetMiscContainer()->mValue.mColor;
  return true;
}

void nsAttrValue::GetEnumString(nsAString& aResult, bool aRealTag) const {
  MOZ_ASSERT(Type() == eEnum, "wrong type");

  uint32_t allEnumBits = (BaseType() == eIntegerBase)
                             ? static_cast<uint32_t>(GetIntInternal())
                             : GetMiscContainer()->mValue.mEnumValue;
  int16_t val = allEnumBits >> NS_ATTRVALUE_ENUMTABLEINDEX_BITS;
  EnumTableSpan table = sEnumTableArray->ElementAt(
      allEnumBits & NS_ATTRVALUE_ENUMTABLEINDEX_MASK);
  for (const auto& entry : table) {
    if (entry.value == val) {
      aResult.AssignASCII(entry.tag);
      if (!aRealTag &&
          allEnumBits & NS_ATTRVALUE_ENUMTABLE_VALUE_NEEDS_TO_UPPER) {
        nsContentUtils::ASCIIToUpper(aResult);
      }
      return;
    }
  }

  MOZ_ASSERT_UNREACHABLE("couldn't find value in EnumTable");
}

UniquePtr<AttrAtomArray> AttrAtomArray::CreateDeduplicatedCopyIfDifferentImpl()
    const {
  MOZ_ASSERT(mMayContainDuplicates);

  bool usingHashTable = false;
  BitBloomFilter<8, nsAtom> filter;
  nsTHashSet<nsAtom*> hash;

  auto CheckDuplicate = [&](size_t i) {
    nsAtom* atom = mArray[i];
    if (!usingHashTable) {
      if (!filter.mightContain(atom)) {
        filter.add(atom);
        return false;
      }
      for (size_t j = 0; j < i; ++j) {
        hash.Insert(mArray[j]);
      }
      usingHashTable = true;
    }
    return !hash.EnsureInserted(atom);
  };

  size_t len = mArray.Length();
  UniquePtr<AttrAtomArray> deduplicatedArray;
  for (size_t i = 0; i < len; ++i) {
    if (!CheckDuplicate(i)) {
      if (deduplicatedArray) {
        deduplicatedArray->mArray.AppendElement(mArray[i]);
      }
      continue;
    }
    // We've found a duplicate!
    if (!deduplicatedArray) {
      // Allocate the deduplicated copy and copy the preceding elements into it.
      deduplicatedArray = std::make_unique<AttrAtomArray>();
      deduplicatedArray->mMayContainDuplicates = false;
      deduplicatedArray->mBloomFilter = mBloomFilter;
      deduplicatedArray->mArray.SetCapacity(len - 1);
      for (size_t indexToCopy = 0; indexToCopy < i; indexToCopy++) {
        deduplicatedArray->mArray.AppendElement(mArray[indexToCopy]);
      }
    }
  }

  if (!deduplicatedArray) {
    // This AttrAtomArray doesn't contain any duplicates, cache this information
    // for future invocations.
    mMayContainDuplicates = false;
  }
  return deduplicatedArray;
}

uint32_t nsAttrValue::GetAtomCount() const {
  ValueType type = Type();

  if (type == eAtom) {
    return 1;
  }

  if (type == eAtomArray) {
    return GetAtomArrayValue()->Array().Length();
  }

  return 0;
}

nsAtom* nsAttrValue::AtomAt(int32_t aIndex) const {
  MOZ_ASSERT(aIndex >= 0, "Index must not be negative");
  MOZ_ASSERT(GetAtomCount() > uint32_t(aIndex), "aIndex out of range");

  if (BaseType() == eAtomBase) {
    return GetAtomValue();
  }

  NS_ASSERTION(Type() == eAtomArray, "GetAtomCount must be confused");
  return GetAtomArrayValue()->Array().ElementAt(aIndex);
}

uint32_t nsAttrValue::HashValue() const {
  switch (BaseType()) {
    case eStringBase: {
      if (auto* str = static_cast<mozilla::StringBuffer*>(GetPtr())) {
        uint32_t len = str->StorageSize() / sizeof(char16_t) - 1;
        return HashString(static_cast<char16_t*>(str->Data()), len);
      }

      return 0;
    }
    case eOtherBase: {
      break;
    }
    case eAtomBase:
    case eIntegerBase: {
      // mBits and uint32_t might have different size. This should silence
      // any warnings or compile-errors. This is what the implementation of
      // NS_PTR_TO_INT32 does to take care of the same problem.
      return mBits - 0;
    }
  }

  MiscContainer* cont = GetMiscContainer();
  if (static_cast<ValueBaseType>(cont->mStringBits &
                                 NS_ATTRVALUE_BASETYPE_MASK) == eAtomBase) {
    return cont->mStringBits - 0;
  }

  switch (cont->mType) {
    case eInteger: {
      return cont->mValue.mInteger;
    }
    case eEnum: {
      return cont->mValue.mEnumValue;
    }
    case ePercent: {
      return cont->mDoubleValue;
    }
    case eColor: {
      return cont->mValue.mColor;
    }
    case eCSSDeclaration: {
      return NS_PTR_TO_INT32(cont->mValue.mCSSDeclaration);
    }
    case eURL: {
      nsString str;
      ToString(str);
      return HashString(str);
    }
    case eAtomArray: {
      uint32_t hash = 0;
      for (const auto& atom : cont->mValue.mAtomArray->Array()) {
        hash = AddToHash(hash, atom.get());
      }
      return hash;
    }
    case eDoubleValue: {
      // XXX this is crappy, but oh well
      return cont->mDoubleValue;
    }
    default: {
      if (IsSVGType(cont->mType)) {
        // All SVG types are just pointers to classes so we can treat them alike
        return NS_PTR_TO_INT32(cont->mValue.mSVGLength);
      }
      MOZ_ASSERT_UNREACHABLE("unknown type stored in MiscContainer");
      return 0;
    }
  }
}

bool nsAttrValue::Equals(const nsAttrValue& aOther) const {
  if (BaseType() != aOther.BaseType()) {
    return false;
  }

  switch (BaseType()) {
    case eStringBase: {
      return GetStringValue().Equals(aOther.GetStringValue());
    }
    case eOtherBase: {
      break;
    }
    case eAtomBase:
    case eIntegerBase: {
      return mBits == aOther.mBits;
    }
  }

  MiscContainer* thisCont = GetMiscContainer();
  MiscContainer* otherCont = aOther.GetMiscContainer();
  if (thisCont == otherCont) {
    return true;
  }

  if (thisCont->mType != otherCont->mType) {
    return false;
  }

  bool needsStringComparison = false;

  switch (thisCont->mType) {
    case eInteger: {
      if (thisCont->mValue.mInteger == otherCont->mValue.mInteger) {
        needsStringComparison = true;
      }
      break;
    }
    case eEnum: {
      if (thisCont->mValue.mEnumValue == otherCont->mValue.mEnumValue) {
        needsStringComparison = true;
      }
      break;
    }
    case ePercent: {
      if (thisCont->mDoubleValue == otherCont->mDoubleValue) {
        needsStringComparison = true;
      }
      break;
    }
    case eColor: {
      if (thisCont->mValue.mColor == otherCont->mValue.mColor) {
        needsStringComparison = true;
      }
      break;
    }
    case eCSSDeclaration: {
      return thisCont->mValue.mCSSDeclaration ==
             otherCont->mValue.mCSSDeclaration;
    }
    case eURL: {
      return thisCont->mValue.mURL == otherCont->mValue.mURL;
    }
    case eAtomArray: {
      // For classlists we could be insensitive to order, however
      // classlists are never mapped attributes so they are never compared.

      if (!(*thisCont->mValue.mAtomArray == *otherCont->mValue.mAtomArray)) {
        return false;
      }

      needsStringComparison = true;
      break;
    }
    case eDoubleValue: {
      return thisCont->mDoubleValue == otherCont->mDoubleValue;
    }
    default: {
      if (IsSVGType(thisCont->mType)) {
        // Currently this method is never called for nsAttrValue objects that
        // point to SVG data types.
        // If that changes then we probably want to add methods to the
        // corresponding SVG types to compare their base values.
        // As a shortcut, however, we can begin by comparing the pointers.
        MOZ_ASSERT(false, "Comparing nsAttrValues that point to SVG data");
        return false;
      }
      MOZ_ASSERT_UNREACHABLE("unknown type stored in MiscContainer");
      return false;
    }
  }
  if (needsStringComparison) {
    if (thisCont->mStringBits == otherCont->mStringBits) {
      return true;
    }
    if ((static_cast<ValueBaseType>(thisCont->mStringBits &
                                    NS_ATTRVALUE_BASETYPE_MASK) ==
         eStringBase) &&
        (static_cast<ValueBaseType>(otherCont->mStringBits &
                                    NS_ATTRVALUE_BASETYPE_MASK) ==
         eStringBase)) {
      return nsCheapString(reinterpret_cast<mozilla::StringBuffer*>(
                               thisCont->mStringBits.load()))
          .Equals(nsCheapString(reinterpret_cast<mozilla::StringBuffer*>(
              otherCont->mStringBits.load())));
    }
  }
  return false;
}

bool nsAttrValue::Equals(const nsAString& aValue,
                         nsCaseTreatment aCaseSensitive) const {
  switch (BaseType()) {
    case eStringBase: {
      if (auto* str = static_cast<mozilla::StringBuffer*>(GetPtr())) {
        nsDependentString dep(static_cast<char16_t*>(str->Data()),
                              str->StorageSize() / sizeof(char16_t) - 1);
        return aCaseSensitive == eCaseMatters
                   ? aValue.Equals(dep)
                   : nsContentUtils::EqualsIgnoreASCIICase(aValue, dep);
      }
      return aValue.IsEmpty();
    }
    case eAtomBase: {
      auto* atom = static_cast<nsAtom*>(GetPtr());
      if (aCaseSensitive == eCaseMatters) {
        return atom->Equals(aValue);
      }
      return nsContentUtils::EqualsIgnoreASCIICase(nsDependentAtomString(atom),
                                                   aValue);
    }
    default:
      break;
  }

  nsAutoString val;
  ToString(val);
  return aCaseSensitive == eCaseMatters
             ? val.Equals(aValue)
             : nsContentUtils::EqualsIgnoreASCIICase(val, aValue);
}

bool nsAttrValue::Equals(const nsAtom* aValue,
                         nsCaseTreatment aCaseSensitive) const {
  switch (BaseType()) {
    case eAtomBase: {
      auto* atom = static_cast<nsAtom*>(GetPtr());
      if (atom == aValue) {
        return true;
      }
      if (aCaseSensitive == eCaseMatters) {
        return false;
      }
      if (atom->IsAsciiLowercase() && aValue->IsAsciiLowercase()) {
        return false;
      }
      return nsContentUtils::EqualsIgnoreASCIICase(
          nsDependentAtomString(atom), nsDependentAtomString(aValue));
    }
    case eStringBase: {
      if (auto* str = static_cast<mozilla::StringBuffer*>(GetPtr())) {
        size_t strLen = str->StorageSize() / sizeof(char16_t) - 1;
        if (aValue->GetLength() != strLen) {
          return false;
        }
        const char16_t* strData = static_cast<char16_t*>(str->Data());
        const char16_t* valData = aValue->GetUTF16String();
        if (aCaseSensitive == eCaseMatters) {
          // Avoid string construction / destruction for the easy case.
          return ArrayEqual(strData, valData, strLen);
        }
        nsDependentSubstring depStr(strData, strLen);
        nsDependentSubstring depVal(valData, strLen);
        return nsContentUtils::EqualsIgnoreASCIICase(depStr, depVal);
      }
      return aValue->IsEmpty();
    }
    default:
      break;
  }

  nsAutoString val;
  ToString(val);
  nsDependentAtomString dep(aValue);
  return aCaseSensitive == eCaseMatters
             ? val.Equals(dep)
             : nsContentUtils::EqualsIgnoreASCIICase(val, dep);
}

struct HasPrefixFn {
  static bool Check(const char16_t* aAttrValue, size_t aAttrLen,
                    const nsAString& aSearchValue,
                    nsCaseTreatment aCaseSensitive) {
    if (aCaseSensitive == eCaseMatters) {
      if (aSearchValue.Length() > aAttrLen) {
        return false;
      }
      return !memcmp(aAttrValue, aSearchValue.BeginReading(),
                     aSearchValue.Length() * sizeof(char16_t));
    }
    return StringBeginsWith(nsDependentString(aAttrValue, aAttrLen),
                            aSearchValue,
                            nsASCIICaseInsensitiveStringComparator);
  }
};

struct HasSuffixFn {
  static bool Check(const char16_t* aAttrValue, size_t aAttrLen,
                    const nsAString& aSearchValue,
                    nsCaseTreatment aCaseSensitive) {
    if (aCaseSensitive == eCaseMatters) {
      if (aSearchValue.Length() > aAttrLen) {
        return false;
      }
      return !memcmp(aAttrValue + aAttrLen - aSearchValue.Length(),
                     aSearchValue.BeginReading(),
                     aSearchValue.Length() * sizeof(char16_t));
    }
    return StringEndsWith(nsDependentString(aAttrValue, aAttrLen), aSearchValue,
                          nsASCIICaseInsensitiveStringComparator);
  }
};

struct HasSubstringFn {
  static bool Check(const char16_t* aAttrValue, size_t aAttrLen,
                    const nsAString& aSearchValue,
                    nsCaseTreatment aCaseSensitive) {
    if (aCaseSensitive == eCaseMatters) {
      if (aSearchValue.IsEmpty()) {
        return true;
      }
      const char16_t* end = aAttrValue + aAttrLen;
      return std::search(aAttrValue, end, aSearchValue.BeginReading(),
                         aSearchValue.EndReading()) != end;
    }
    return AsciiCaseInsensitiveFindInReadable(
        aSearchValue, nsDependentString(aAttrValue, aAttrLen));
  }
};

template <typename F>
bool nsAttrValue::SubstringCheck(const nsAString& aValue,
                                 nsCaseTreatment aCaseSensitive) const {
  switch (BaseType()) {
    case eStringBase: {
      if (auto* str = static_cast<mozilla::StringBuffer*>(GetPtr())) {
        return F::Check(static_cast<char16_t*>(str->Data()),
                        str->StorageSize() / sizeof(char16_t) - 1, aValue,
                        aCaseSensitive);
      }
      return aValue.IsEmpty();
    }
    case eAtomBase: {
      auto* atom = static_cast<nsAtom*>(GetPtr());
      return F::Check(atom->GetUTF16String(), atom->GetLength(), aValue,
                      aCaseSensitive);
    }
    default:
      break;
  }

  nsAutoString val;
  ToString(val);
  return F::Check(val.BeginReading(), val.Length(), aValue, aCaseSensitive);
}

bool nsAttrValue::HasPrefix(const nsAString& aValue,
                            nsCaseTreatment aCaseSensitive) const {
  return SubstringCheck<HasPrefixFn>(aValue, aCaseSensitive);
}

bool nsAttrValue::HasSuffix(const nsAString& aValue,
                            nsCaseTreatment aCaseSensitive) const {
  return SubstringCheck<HasSuffixFn>(aValue, aCaseSensitive);
}

bool nsAttrValue::HasSubstring(const nsAString& aValue,
                               nsCaseTreatment aCaseSensitive) const {
  return SubstringCheck<HasSubstringFn>(aValue, aCaseSensitive);
}

bool nsAttrValue::EqualsAsStrings(const nsAttrValue& aOther) const {
  if (Type() == aOther.Type()) {
    return Equals(aOther);
  }

  // We need to serialize at least one nsAttrValue before passing to
  // Equals(const nsAString&), but we can avoid unnecessarily serializing both
  // by checking if one is already of a string type.
  bool thisIsString = (BaseType() == eStringBase || BaseType() == eAtomBase);
  const nsAttrValue& lhs = thisIsString ? *this : aOther;
  const nsAttrValue& rhs = thisIsString ? aOther : *this;

  switch (rhs.BaseType()) {
    case eAtomBase:
      return lhs.Equals(rhs.GetAtomValue(), eCaseMatters);

    case eStringBase:
      return lhs.Equals(rhs.GetStringValue(), eCaseMatters);

    default: {
      nsAutoString val;
      rhs.ToString(val);
      return lhs.Equals(val, eCaseMatters);
    }
  }
}

bool nsAttrValue::Contains(nsAtom* aValue,
                           nsCaseTreatment aCaseSensitive) const {
  switch (BaseType()) {
    case eAtomBase: {
      nsAtom* atom = GetAtomValue();
      if (aCaseSensitive == eCaseMatters) {
        return aValue == atom;
      }

      // For performance reasons, don't do a full on unicode case insensitive
      // string comparison. This is only used for quirks mode anyway.
      return nsContentUtils::EqualsIgnoreASCIICase(aValue, atom);
    }
    default: {
      if (Type() == eAtomArray) {
        const AttrAtomArray* array = GetAtomArrayValue();
        if (aCaseSensitive == eCaseMatters) {
          return array->MayContain(aValue) && array->Array().Contains(aValue);
        }

        for (const RefPtr<nsAtom>& cur : array->Array()) {
          // For performance reasons, don't do a full on unicode case
          // insensitive string comparison. This is only used for quirks mode
          // anyway.
          if (nsContentUtils::EqualsIgnoreASCIICase(aValue, cur)) {
            return true;
          }
        }
      }
    }
  }

  return false;
}

struct AtomArrayStringComparator {
  bool Equals(nsAtom* atom, const nsAString& string) const {
    return atom->Equals(string);
  }
};

bool nsAttrValue::Contains(const nsAString& aValue) const {
  switch (BaseType()) {
    case eAtomBase: {
      nsAtom* atom = GetAtomValue();
      return atom->Equals(aValue);
    }
    default: {
      if (Type() == eAtomArray) {
        const AttrAtomArray* array = GetAtomArrayValue();
        return array->Array().Contains(aValue, AtomArrayStringComparator());
      }
    }
  }

  return false;
}

void nsAttrValue::ParseAtom(const nsAString& aValue) {
  ResetIfSet();

  RefPtr<nsAtom> atom = NS_Atomize(aValue);
  if (atom) {
    SetPtrValueAndType(atom.forget().take(), eAtomBase);
  }
}

void nsAttrValue::ParseAtomArray(nsAtom* aValue) {
  if (MiscContainer* cont = AtomArrayCache::Lookup(aValue)) {
    // Set our MiscContainer to the cached one.
    // AddRef must happen before ResetIfSet: the cache does not hold a strong
    // reference, and ResetIfSet could release the last reference to cont if
    // this nsAttrValue is already holding it.
    NS_ADDREF(cont);
    ResetIfSet();
    SetPtrValueAndType(cont, eOtherBase);
    return;
  }

  const char16_t* iter = aValue->GetUTF16String();
  const char16_t* end = iter + aValue->GetLength();
  bool hasSpace = false;

  // skip initial whitespace
  while (iter != end && nsContentUtils::IsHTMLWhitespace(*iter)) {
    hasSpace = true;
    ++iter;
  }

  if (iter == end) {
    // The value is empty or only contains whitespace.
    // Set this attribute to the string value.
    // We don't call the SetTo(nsAtom*) overload because doing so would
    // leave us with a classList of length 1.
    SetTo(nsDependentAtomString(aValue));
    return;
  }

  const char16_t* start = iter;

  // get first - and often only - atom
  do {
    ++iter;
  } while (iter != end && !nsContentUtils::IsHTMLWhitespace(*iter));

  RefPtr<nsAtom> classAtom = iter == end && !hasSpace
                                 ? RefPtr<nsAtom>(aValue).forget()
                                 : NS_AtomizeMainThread(Substring(start, iter));
  if (!classAtom) {
    ResetIfSet();
    return;
  }

  // skip whitespace
  while (iter != end && nsContentUtils::IsHTMLWhitespace(*iter)) {
    hasSpace = true;
    ++iter;
  }

  if (iter == end && !hasSpace) {
    // we only found one classname and there was no whitespace so
    // don't bother storing a list
    ResetIfSet();
    nsAtom* atom = nullptr;
    classAtom.swap(atom);
    SetPtrValueAndType(atom, eAtomBase);
    return;
  }

  // We have at least one class atom. Create a new AttrAtomArray.
  auto array = std::make_unique<AttrAtomArray>();
  array->Append(std::move(classAtom));

  // parse the rest of the classnames
  while (iter != end) {
    start = iter;

    do {
      ++iter;
    } while (iter != end && !nsContentUtils::IsHTMLWhitespace(*iter));

    classAtom = NS_AtomizeMainThread(Substring(start, iter));

    array->Append(std::move(classAtom));

    // skip whitespace
    while (iter != end && nsContentUtils::IsHTMLWhitespace(*iter)) {
      ++iter;
    }
  }

  // Wrap the AtomArray into a fresh MiscContainer.
  MiscContainer* cont = EnsureEmptyMiscContainer();
  MOZ_ASSERT(cont->mValue.mRefCount == 0);
  cont->mValue.mAtomArray = array.release();
  cont->mType = eAtomArray;
  NS_ADDREF(cont);
  MOZ_ASSERT(cont->mValue.mRefCount == 1);

  // Assign the atom to the container's string bits (like SetMiscAtomOrString
  // would do).
  MOZ_ASSERT(!IsInServoTraversal());
  aValue->AddRef();
  uintptr_t bits = reinterpret_cast<uintptr_t>(aValue) | eAtomBase;
  cont->SetStringBitsMainThread(bits);

  // Put the container in the cache.
  cont->Cache();
}

void nsAttrValue::ParseAtomArray(const nsAString& aValue) {
  if (aValue.IsVoid()) {
    ResetIfSet();
  } else {
    RefPtr<nsAtom> atom = NS_AtomizeMainThread(aValue);
    ParseAtomArray(atom);
  }
}

void nsAttrValue::ParseStringOrAtom(const nsAString& aValue) {
  uint32_t len = aValue.Length();
  // Don't bother with atoms if it's an empty string since
  // we can store those efficiently anyway.
  if (len && len <= NS_ATTRVALUE_MAX_STRINGLENGTH_ATOM) {
    ParseAtom(aValue);
  } else {
    SetTo(aValue);
  }
}

void nsAttrValue::ParsePartMapping(const nsAString& aValue) {
  ResetIfSet();
  MiscContainer* cont = EnsureEmptyMiscContainer();

  cont->mType = eShadowParts;
  cont->mValue.mShadowParts = new ShadowParts(ShadowParts::Parse(aValue));
  NS_ADDREF(cont);
  SetMiscAtomOrString(&aValue);
  MOZ_ASSERT(cont->mValue.mRefCount == 1);
}

void nsAttrValue::SetIntValueAndType(int32_t aValue, ValueType aType,
                                     const nsAString* aStringValue) {
  if (aStringValue || aValue > NS_ATTRVALUE_INTEGERTYPE_MAXVALUE ||
      aValue < NS_ATTRVALUE_INTEGERTYPE_MINVALUE) {
    MiscContainer* cont = EnsureEmptyMiscContainer();
    switch (aType) {
      case eInteger: {
        cont->mValue.mInteger = aValue;
        break;
      }
      case ePercent: {
        cont->mDoubleValue = aValue;
        break;
      }
      case eEnum: {
        cont->mValue.mEnumValue = aValue;
        break;
      }
      default: {
        MOZ_ASSERT_UNREACHABLE("unknown integer type");
        break;
      }
    }
    cont->mType = aType;
    SetMiscAtomOrString(aStringValue);
  } else {
    NS_ASSERTION(!mBits, "Reset before calling SetIntValueAndType!");
    mBits = (aValue * NS_ATTRVALUE_INTEGERTYPE_MULTIPLIER) | aType;
  }
}

void nsAttrValue::SetDoubleValueAndType(double aValue, ValueType aType,
                                        const nsAString* aStringValue) {
  MOZ_ASSERT(aType == eDoubleValue || aType == ePercent, "Unexpected type");
  MiscContainer* cont = EnsureEmptyMiscContainer();
  cont->mDoubleValue = aValue;
  cont->mType = aType;
  SetMiscAtomOrString(aStringValue);
}

nsAtom* nsAttrValue::GetStoredAtom() const {
  if (BaseType() == eAtomBase) {
    return static_cast<nsAtom*>(GetPtr());
  }
  if (BaseType() == eOtherBase) {
    return GetMiscContainer()->GetStoredAtom();
  }
  return nullptr;
}

mozilla::StringBuffer* nsAttrValue::GetStoredStringBuffer() const {
  if (BaseType() == eStringBase) {
    return static_cast<mozilla::StringBuffer*>(GetPtr());
  }
  if (BaseType() == eOtherBase) {
    return GetMiscContainer()->GetStoredStringBuffer();
  }
  return nullptr;
}

/**
 * Compares two `EnumTableItem` spans by comparing their data pointer.
 */
struct EnumTablesHaveEqualContent {
  bool Equals(const nsAttrValue::EnumTableSpan& aSpan1,
              const nsAttrValue::EnumTableSpan& aSpan2) const {
    return aSpan1.Elements() == aSpan2.Elements();
  }
};

int16_t nsAttrValue::GetEnumTableIndex(EnumTableSpan aTable) {
  int16_t index =
      sEnumTableArray->IndexOf(aTable, 0, EnumTablesHaveEqualContent());
  if (index < 0) {
    index = sEnumTableArray->Length();
    NS_ASSERTION(index <= NS_ATTRVALUE_ENUMTABLEINDEX_MAXVALUE,
                 "too many enum tables");
    sEnumTableArray->AppendElement(aTable);
  }

  return index;
}

int32_t nsAttrValue::EnumTableEntryToValue(EnumTableSpan aEnumTable,
                                           const EnumTableEntry& aTableEntry) {
  int16_t index = GetEnumTableIndex(aEnumTable);
  int32_t value =
      (aTableEntry.value << NS_ATTRVALUE_ENUMTABLEINDEX_BITS) + index;
  return value;
}

bool nsAttrValue::ParseEnumValue(const nsAString& aValue, EnumTableSpan aTable,
                                 bool aCaseSensitive,
                                 const EnumTableEntry* aDefaultValue) {
  ResetIfSet();
  for (const auto& tableEntry : aTable) {
    if (aCaseSensitive ? aValue.EqualsASCII(tableEntry.tag)
                       : aValue.LowerCaseEqualsASCII(tableEntry.tag)) {
      int32_t value = EnumTableEntryToValue(aTable, tableEntry);

      bool equals = aCaseSensitive || aValue.EqualsASCII(tableEntry.tag);
      if (!equals) {
        nsAutoString tag;
        tag.AssignASCII(tableEntry.tag);
        nsContentUtils::ASCIIToUpper(tag);
        if ((equals = tag.Equals(aValue))) {
          value |= NS_ATTRVALUE_ENUMTABLE_VALUE_NEEDS_TO_UPPER;
        }
      }
      SetIntValueAndType(value, eEnum, equals ? nullptr : &aValue);
      NS_ASSERTION(GetEnumValue() == tableEntry.value,
                   "failed to store enum properly");

      return true;
    }
  }

  if (aDefaultValue) {
    SetIntValueAndType(EnumTableEntryToValue(aTable, *aDefaultValue), eEnum,
                       &aValue);
    return true;
  }

  return false;
}

bool nsAttrValue::DoParseHTMLDimension(const nsAString& aInput,
                                       bool aEnsureNonzero) {
  ResetIfSet();

  // We don't use nsContentUtils::ParseHTMLInteger here because we
  // need a bunch of behavioral differences from it.  We _could_ try to
  // use it, but it would not be a great fit.

  // https://html.spec.whatwg.org/multipage/#rules-for-parsing-dimension-values

  // Steps 1 and 2.
  const char16_t* position = aInput.BeginReading();
  const char16_t* end = aInput.EndReading();

  // We will need to keep track of whether this was a canonical representation
  // or not.  It's non-canonical if it has leading whitespace, leading '+',
  // leading '0' characters, or trailing garbage.
  bool canonical = true;

  // Step 3.
  while (position != end && nsContentUtils::IsHTMLWhitespace(*position)) {
    canonical = false;  // Leading whitespace
    ++position;
  }

  // Step 4.
  if (position == end || *position < char16_t('0') ||
      *position > char16_t('9')) {
    return false;
  }

  // Step 5.
  CheckedInt32 value = 0;

  // Collect up leading '0' first to avoid extra branching in the main
  // loop to set 'canonical' properly.
  while (position != end && *position == char16_t('0')) {
    canonical = false;  // Leading '0'
    ++position;
  }

  // Now collect up other digits.
  while (position != end && *position >= char16_t('0') &&
         *position <= char16_t('9')) {
    value = value * 10 + (*position - char16_t('0'));
    if (!value.isValid()) {
      // The spec assumes we can deal with arbitrary-size integers here, but we
      // really can't.  If someone sets something too big, just bail out and
      // ignore it.
      return false;
    }
    ++position;
  }

  // Step 6 is implemented implicitly via the various "position != end" guards
  // from this point on.

  Maybe<double> doubleValue;
  // Step 7.  The return in step 7.2 is handled by just falling through to the
  // code below this block when we reach end of input or a non-digit, because
  // the while loop will terminate at that point.
  if (position != end && *position == char16_t('.')) {
    canonical = false;  // Let's not rely on double serialization reproducing
                        // the string we started with.
    // Step 7.1.
    ++position;
    // If we have a '.' _not_ followed by digits, this is not as efficient as it
    // could be, because we will store as a double while we could have stored as
    // an int.  But that seems like a pretty rare case.
    doubleValue.emplace(value.value());
    // Step 7.3.
    double divisor = 1.0f;
    // Step 7.4.
    while (position != end && *position >= char16_t('0') &&
           *position <= char16_t('9')) {
      // Step 7.4.1.
      divisor = divisor * 10.0f;
      // Step 7.4.2.
      doubleValue.ref() += (*position - char16_t('0')) / divisor;
      // Step 7.4.3.
      ++position;
      // Step 7.4.4 and 7.4.5 are captured in the while loop condition and the
      // "position != end" checks below.
    }
  }

  if (aEnsureNonzero && value.value() == 0 &&
      (!doubleValue || *doubleValue == 0.0f)) {
    // Not valid.  Just drop it.
    return false;
  }

  // Step 8 and the spec's early return from step 7.2.
  ValueType type;
  if (position != end && *position == char16_t('%')) {
    type = ePercent;
    ++position;
  } else if (doubleValue) {
    type = eDoubleValue;
  } else {
    type = eInteger;
  }

  if (position != end) {
    canonical = false;
  }

  if (doubleValue) {
    MOZ_ASSERT(!canonical, "We set it false above!");
    SetDoubleValueAndType(*doubleValue, type, &aInput);
  } else {
    SetIntValueAndType(value.value(), type, canonical ? nullptr : &aInput);
  }

#ifdef DEBUG
  nsAutoString str;
  ToString(str);
  MOZ_ASSERT(str == aInput, "We messed up our 'canonical' boolean!");
#endif

  return true;
}

bool nsAttrValue::ParseIntWithBounds(const nsAString& aString, int32_t aMin,
                                     int32_t aMax) {
  MOZ_ASSERT(aMin < aMax, "bad boundaries");

  ResetIfSet();

  nsContentUtils::ParseHTMLIntegerResultFlags result;
  int32_t originalVal = nsContentUtils::ParseHTMLInteger(aString, &result);
  if (result & nsContentUtils::eParseHTMLInteger_Error) {
    return false;
  }

  int32_t val = std::max(originalVal, aMin);
  val = std::min(val, aMax);
  bool nonStrict =
      (val != originalVal) ||
      (result & nsContentUtils::eParseHTMLInteger_NonStandard) ||
      (result & nsContentUtils::eParseHTMLInteger_DidNotConsumeAllInput);

  SetIntValueAndType(val, eInteger, nonStrict ? &aString : nullptr);

  return true;
}

void nsAttrValue::ParseIntWithFallback(const nsAString& aString,
                                       int32_t aDefault, int32_t aMax) {
  ResetIfSet();

  nsContentUtils::ParseHTMLIntegerResultFlags result;
  int32_t val = nsContentUtils::ParseHTMLInteger(aString, &result);
  bool nonStrict = false;
  if ((result & nsContentUtils::eParseHTMLInteger_Error) || val < 1) {
    val = aDefault;
    nonStrict = true;
  }

  if (val > aMax) {
    val = aMax;
    nonStrict = true;
  }

  if ((result & nsContentUtils::eParseHTMLInteger_NonStandard) ||
      (result & nsContentUtils::eParseHTMLInteger_DidNotConsumeAllInput)) {
    nonStrict = true;
  }

  SetIntValueAndType(val, eInteger, nonStrict ? &aString : nullptr);
}

void nsAttrValue::ParseClampedNonNegativeInt(const nsAString& aString,
                                             int32_t aDefault, int32_t aMin,
                                             int32_t aMax) {
  ResetIfSet();

  nsContentUtils::ParseHTMLIntegerResultFlags result;
  int32_t val = nsContentUtils::ParseHTMLInteger(aString, &result);
  bool nonStrict =
      (result & nsContentUtils::eParseHTMLInteger_NonStandard) ||
      (result & nsContentUtils::eParseHTMLInteger_DidNotConsumeAllInput);

  if (result & nsContentUtils::eParseHTMLInteger_ErrorOverflow) {
    if (result & nsContentUtils::eParseHTMLInteger_Negative) {
      val = aDefault;
    } else {
      val = aMax;
    }
    nonStrict = true;
  } else if ((result & nsContentUtils::eParseHTMLInteger_Error) || val < 0) {
    val = aDefault;
    nonStrict = true;
  } else if (val < aMin) {
    val = aMin;
    nonStrict = true;
  } else if (val > aMax) {
    val = aMax;
    nonStrict = true;
  }

  SetIntValueAndType(val, eInteger, nonStrict ? &aString : nullptr);
}

bool nsAttrValue::ParseNonNegativeIntValue(const nsAString& aString) {
  ResetIfSet();

  nsContentUtils::ParseHTMLIntegerResultFlags result;
  int32_t originalVal = nsContentUtils::ParseHTMLInteger(aString, &result);
  if ((result & nsContentUtils::eParseHTMLInteger_Error) || originalVal < 0) {
    return false;
  }

  bool nonStrict =
      (result & nsContentUtils::eParseHTMLInteger_NonStandard) ||
      (result & nsContentUtils::eParseHTMLInteger_DidNotConsumeAllInput);

  SetIntValueAndType(originalVal, eInteger, nonStrict ? &aString : nullptr);

  return true;
}

bool nsAttrValue::ParsePositiveIntValue(const nsAString& aString) {
  ResetIfSet();

  nsContentUtils::ParseHTMLIntegerResultFlags result;
  int32_t originalVal = nsContentUtils::ParseHTMLInteger(aString, &result);
  if ((result & nsContentUtils::eParseHTMLInteger_Error) || originalVal <= 0) {
    return false;
  }

  bool nonStrict =
      (result & nsContentUtils::eParseHTMLInteger_NonStandard) ||
      (result & nsContentUtils::eParseHTMLInteger_DidNotConsumeAllInput);

  SetIntValueAndType(originalVal, eInteger, nonStrict ? &aString : nullptr);

  return true;
}

bool nsAttrValue::SetColorValue(nscolor aColor, const nsAString& aString) {
  mozilla::StringBuffer* buf = GetStringBuffer(aString).take();
  if (!buf) {
    return false;
  }

  MiscContainer* cont = EnsureEmptyMiscContainer();
  cont->mValue.mColor = aColor;
  cont->mType = eColor;

  // Save the literal string we were passed for round-tripping.
  cont->SetStringBitsMainThread(reinterpret_cast<uintptr_t>(buf) | eStringBase);
  return true;
}

bool nsAttrValue::ParseColor(const nsAString& aString) {
  ResetIfSet();

  // FIXME (partially, at least): HTML5's algorithm says we shouldn't do
  // the whitespace compression, trimming, or the test for emptiness.
  // (I'm a little skeptical that we shouldn't do the whitespace
  // trimming; WebKit also does it.)
  nsAutoString colorStr(aString);
  colorStr.CompressWhitespace(true, true);
  if (colorStr.IsEmpty()) {
    return false;
  }

  nscolor color;
  // No color names begin with a '#'; in standards mode, all acceptable
  // numeric colors do.
  if (colorStr.First() == '#') {
    nsDependentString withoutHash(colorStr.get() + 1, colorStr.Length() - 1);
    if (NS_HexToRGBA(withoutHash, nsHexColorType::NoAlpha, &color)) {
      return SetColorValue(color, aString);
    }
  } else if (colorStr.LowerCaseEqualsLiteral("transparent")) {
    return SetColorValue(NS_RGBA(0, 0, 0, 0), aString);
  } else {
    const NS_ConvertUTF16toUTF8 colorNameU8(colorStr);
    StyleAbsoluteColor result;
    if (Servo_ColorNameToRgb(&colorNameU8, &result)) {
      return SetColorValue(result.ToColor(), aString);
    }
  }

  // FIXME (maybe): HTML5 says we should handle system colors.  This
  // means we probably need another storage type, since we'd need to
  // handle dynamic changes.  However, I think this is a bad idea:
  // http://lists.whatwg.org/pipermail/whatwg-whatwg.org/2010-May/026449.html

  // Use NS_LooseHexToRGB as a fallback if nothing above worked.
  if (NS_LooseHexToRGB(colorStr, &color)) {
    return SetColorValue(color, aString);
  }

  return false;
}

bool nsAttrValue::ParseDoubleValue(const nsAString& aString) {
  ResetIfSet();

  nsresult ec;
  double val = PromiseFlatString(aString).ToDouble(&ec);
  if (NS_FAILED(ec)) {
    return false;
  }

  MiscContainer* cont = EnsureEmptyMiscContainer();
  cont->mDoubleValue = val;
  cont->mType = eDoubleValue;
  nsAutoString serializedFloat;
  serializedFloat.AppendFloat(val);
  SetMiscAtomOrString(serializedFloat.Equals(aString) ? nullptr : &aString);
  return true;
}

bool nsAttrValue::ParseStyleAttribute(const nsAString& aString,
                                      nsIPrincipal* aMaybeScriptedPrincipal,
                                      nsStyledElement* aElement) {
  dom::Document* doc = aElement->OwnerDoc();
  AttributeStyles* attrStyles = doc->GetAttributeStyles();
  NS_ASSERTION(aElement->NodePrincipal() == doc->NodePrincipal(),
               "This is unexpected");

  nsIPrincipal* principal = aMaybeScriptedPrincipal ? aMaybeScriptedPrincipal
                                                    : aElement->NodePrincipal();
  RefPtr<URLExtraData> data = aElement->GetURLDataForStyleAttr(principal);

  // If the (immutable) document URI does not match the element's base URI
  // (the common case is that they do match) do not cache the rule.  This is
  // because the results of the CSS parser are dependent on these URIs, and we
  // do not want to have to account for the URIs in the hash lookup.
  // Similarly, if the triggering principal does not match the node principal,
  // do not cache the rule, since the principal will be encoded in any parsed
  // URLs in the rule.
  const bool cachingAllowed = attrStyles &&
                              doc->GetDocumentURI() == data->BaseURI() &&
                              principal == aElement->NodePrincipal();
  if (cachingAllowed) {
    if (MiscContainer* cont = attrStyles->LookupStyleAttr(aString)) {
      // Set our MiscContainer to the cached one.
      // AddRef must happen before ResetIfSet: the cache does not hold a strong
      // reference, and ResetIfSet could release the last reference to cont if
      // this nsAttrValue is already holding it.
      NS_ADDREF(cont);
      ResetIfSet();
      SetPtrValueAndType(cont, eOtherBase);
      return true;
    }
  }

  RefPtr<DeclarationBlock> decl = DeclarationBlock::FromCssText(
      aString, data, doc->GetCompatibilityMode(), doc->GetExistingCSSLoader(),
      StyleCssRuleType::Style);
  if (!decl) {
    return false;
  }
  decl->SetAttributeStyles(attrStyles);
  SetTo(decl.forget(), &aString);

  if (cachingAllowed) {
    MiscContainer* cont = GetMiscContainer();
    cont->Cache();
  }

  return true;
}

void nsAttrValue::SetMiscAtomOrString(const nsAString* aValue) {
  NS_ASSERTION(GetMiscContainer(), "Must have MiscContainer!");
  NS_ASSERTION(!GetMiscContainer()->mStringBits || IsInServoTraversal(),
               "Trying to re-set atom or string!");
  if (aValue) {
    uint32_t len = aValue->Length();
    // * We're allowing eCSSDeclaration attributes to store empty
    //   strings as it can be beneficial to store an empty style
    //   attribute as a parsed rule.
    // * We're allowing enumerated values because sometimes the empty
    //   string corresponds to a particular enumerated value, especially
    //   for enumerated values that are not limited enumerated.
    // Add other types as needed.
    NS_ASSERTION(len || Type() == eCSSDeclaration || Type() == eEnum,
                 "Empty string?");
    MiscContainer* cont = GetMiscContainer();

    if (len <= NS_ATTRVALUE_MAX_STRINGLENGTH_ATOM) {
      nsAtom* atom = MOZ_LIKELY(!IsInServoTraversal())
                         ? NS_AtomizeMainThread(*aValue).take()
                         : NS_Atomize(*aValue).take();
      NS_ENSURE_TRUE_VOID(atom);
      uintptr_t bits = reinterpret_cast<uintptr_t>(atom) | eAtomBase;

      // In the common case we're not in the servo traversal, and we can just
      // set the bits normally. The parallel case requires more care.
      if (MOZ_LIKELY(!IsInServoTraversal())) {
        cont->SetStringBitsMainThread(bits);
      } else if (!cont->mStringBits.compareExchange(0, bits)) {
        // We raced with somebody else setting the bits. Release our copy.
        atom->Release();
      }
    } else {
      mozilla::StringBuffer* buffer = GetStringBuffer(*aValue).take();
      NS_ENSURE_TRUE_VOID(buffer);
      uintptr_t bits = reinterpret_cast<uintptr_t>(buffer) | eStringBase;

      // In the common case we're not in the servo traversal, and we can just
      // set the bits normally. The parallel case requires more care.
      if (MOZ_LIKELY(!IsInServoTraversal())) {
        cont->SetStringBitsMainThread(bits);
      } else if (!cont->mStringBits.compareExchange(0, bits)) {
        // We raced with somebody else setting the bits. Release our copy.
        buffer->Release();
      }
    }
  }
}

void nsAttrValue::ResetMiscAtomOrString() {
  MiscContainer* cont = GetMiscContainer();
  bool isString;
  if (void* ptr = cont->GetStringOrAtomPtr(isString)) {
    if (isString) {
      static_cast<mozilla::StringBuffer*>(ptr)->Release();
    } else {
      static_cast<nsAtom*>(ptr)->Release();
    }
    cont->SetStringBitsMainThread(0);
  }
}

void nsAttrValue::SetSVGType(ValueType aType, const void* aValue,
                             const nsAString* aSerialized) {
  MOZ_ASSERT(IsSVGType(aType), "Not an SVG type");

  MiscContainer* cont = EnsureEmptyMiscContainer();
  // All SVG types are just pointers to classes so just setting any of them
  // will do. We'll lose type-safety but the signature of the calling
  // function should ensure we don't get anything unexpected, and once we
  // stick aValue in a union we lose type information anyway.
  cont->mValue.mSVGLength = static_cast<const SVGAnimatedLength*>(aValue);
  cont->mType = aType;
  SetMiscAtomOrString(aSerialized);
}

MiscContainer* nsAttrValue::ClearMiscContainer() {
  MiscContainer* cont = nullptr;
  if (BaseType() == eOtherBase) {
    cont = GetMiscContainer();
    if (cont->IsRefCounted() && cont->mValue.mRefCount > 1) {
      // This MiscContainer is shared, we need a new one.
      NS_RELEASE(cont);

      cont = AllocMiscContainer();
      SetPtrValueAndType(cont, eOtherBase);
    } else {
      switch (cont->mType) {
        case eCSSDeclaration: {
          MOZ_ASSERT(cont->mValue.mRefCount == 1);
          cont->Release();
          cont->Evict();
          NS_RELEASE(cont->mValue.mCSSDeclaration);
          break;
        }
        case eShadowParts: {
          MOZ_ASSERT(cont->mValue.mRefCount == 1);
          cont->Release();
          delete cont->mValue.mShadowParts;
          break;
        }
        case eURL: {
          NS_RELEASE(cont->mValue.mURL);
          break;
        }
        case eAtomArray: {
          MOZ_ASSERT(cont->mValue.mRefCount == 1);
          cont->Release();
          cont->Evict();
          delete cont->mValue.mAtomArray;
          break;
        }
        default: {
          break;
        }
      }
    }
    ResetMiscAtomOrString();
  } else {
    ResetIfSet();
  }

  return cont;
}

MiscContainer* nsAttrValue::EnsureEmptyMiscContainer() {
  MiscContainer* cont = ClearMiscContainer();
  if (cont) {
    MOZ_ASSERT(BaseType() == eOtherBase);
    ResetMiscAtomOrString();
    cont = GetMiscContainer();
  } else {
    cont = AllocMiscContainer();
    SetPtrValueAndType(cont, eOtherBase);
  }

  return cont;
}

already_AddRefed<mozilla::StringBuffer> nsAttrValue::GetStringBuffer(
    const nsAString& aValue) const {
  uint32_t len = aValue.Length();
  if (!len) {
    return nullptr;
  }
  if (mozilla::StringBuffer* buf = aValue.GetStringBuffer();
      buf && (buf->StorageSize() / sizeof(char16_t) - 1) == len) {
    // We can only reuse the buffer if it's exactly sized, since we rely on
    // StorageSize() to get the string length in ToString().
    return do_AddRef(buf);
  }
  return mozilla::StringBuffer::Create(aValue.Data(), aValue.Length());
}

size_t nsAttrValue::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t n = 0;

  switch (BaseType()) {
    case eStringBase: {
      mozilla::StringBuffer* str =
          static_cast<mozilla::StringBuffer*>(GetPtr());
      n += str ? str->SizeOfIncludingThisIfUnshared(aMallocSizeOf) : 0;
      break;
    }
    case eOtherBase: {
      MiscContainer* container = GetMiscContainer();
      if (!container) {
        break;
      }
      if (container->IsRefCounted() && container->mValue.mRefCount > 1) {
        // We don't report this MiscContainer at all in order to avoid
        // twice-reporting it.
        // TODO DMD, bug 1027551 - figure out how to report this ref-counted
        // object just once.
        break;
      }
      n += aMallocSizeOf(container);

      // We only count the size of the object pointed by otherPtr if it's a
      // string. When it's an atom, it's counted separately.
      if (mozilla::StringBuffer* buf = container->GetStoredStringBuffer()) {
        n += buf->SizeOfIncludingThisIfUnshared(aMallocSizeOf);
      }

      if (Type() == eCSSDeclaration && container->mValue.mCSSDeclaration) {
        // TODO: mCSSDeclaration might be owned by another object which
        //       would make us count them twice, bug 677493.
        // Bug 1281964: For DeclarationBlock if we do measure we'll
        // need a way to call the Servo heap_size_of function.
        // n += container->mCSSDeclaration->SizeOfIncludingThis(aMallocSizeOf);
      } else if (Type() == eAtomArray && container->mValue.mAtomArray) {
        // Don't measure each nsAtom, because they are measured separately.
        n += container->mValue.mAtomArray->ShallowSizeOfIncludingThis(
            aMallocSizeOf);
      }
      break;
    }
    case eAtomBase:     // Atoms are counted separately.
    case eIntegerBase:  // The value is in mBits, nothing to do.
      break;
  }

  return n;
}
