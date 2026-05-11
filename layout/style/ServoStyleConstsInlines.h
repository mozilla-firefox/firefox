/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Some inline functions declared in cbindgen.toml */

#ifndef mozilla_ServoStyleConstsInlines_h
#define mozilla_ServoStyleConstsInlines_h

#include <new>
#include <type_traits>

#include "MainThreadUtils.h"
#include "mozilla/AspectRatio.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/SVGContentUtils.h"
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/URLExtraData.h"
#include "mozilla/dom/WorkerCommon.h"
#include "nsGkAtoms.h"
#include "nsNetUtil.h"

// TODO(emilio): there are quite a few other implementations scattered around
// that should move here.

namespace mozilla {

// We need to explicitly instantiate these so that the clang plugin can see that
// they're trivially copyable...
//
// https://github.com/eqrion/cbindgen/issues/402 tracks doing something like
// this automatically from cbindgen.
template struct StyleStrong<ComputedStyle>;
template struct StyleStrong<StyleLockedCssRules>;
template struct StyleStrong<StyleAnimationValue>;
template struct StyleStrong<StyleLockedDeclarationBlock>;
template struct StyleStrong<StyleStylesheetContents>;
template struct StyleStrong<StyleLockedKeyframe>;
template struct StyleStrong<StyleLayerBlockRule>;
template struct StyleStrong<StyleLayerStatementRule>;
template struct StyleStrong<StyleLockedMediaList>;
template struct StyleStrong<StyleLockedStyleRule>;
template struct StyleStrong<StyleLockedImportRule>;
template struct StyleStrong<StyleLockedKeyframesRule>;
template struct StyleStrong<StyleMediaRule>;
template struct StyleStrong<StyleCustomMediaRule>;
template struct StyleStrong<StyleDocumentRule>;
template struct StyleStrong<StyleNamespaceRule>;
template struct StyleStrong<StyleMarginRule>;
template struct StyleStrong<StyleLockedPageRule>;
template struct StyleStrong<StylePropertyRule>;
template struct StyleStrong<StyleSupportsRule>;
template struct StyleStrong<StyleFontFeatureValuesRule>;
template struct StyleStrong<StyleFontPaletteValuesRule>;
template struct StyleStrong<StyleLockedFontFaceRule>;
template struct StyleStrong<StyleLockedCounterStyleRule>;
template struct StyleStrong<StyleContainerRule>;
template struct StyleStrong<StyleScopeRule>;
template struct StyleStrong<StyleStartingStyleRule>;
template struct StyleStrong<StyleAppearanceBaseRule>;
template struct StyleStrong<StyleLockedPositionTryRule>;
template struct StyleStrong<StyleLockedNestedDeclarationsRule>;
template struct StyleStrong<StyleViewTransitionRule>;

template <typename T>
inline void StyleOwnedSlice<T>::Clear() {
  if (!len) {
    return;
  }
  for (size_t i : IntegerRange(len)) {
    ptr[i].~T();
  }
  free(ptr);
  ptr = (T*)alignof(T);
  len = 0;
}

template <typename T>
inline void StyleOwnedSlice<T>::CopyFrom(const StyleOwnedSlice& aOther) {
  Clear();
  len = aOther.len;
  if (!len) {
    ptr = (T*)alignof(T);
  } else {
    ptr = (T*)malloc(len * sizeof(T));
    size_t i = 0;
    for (const T& elem : aOther.AsSpan()) {
      new (ptr + i++) T(elem);
    }
  }
}

template <typename T>
inline void StyleOwnedSlice<T>::SwapElements(StyleOwnedSlice& aOther) {
  std::swap(ptr, aOther.ptr);
  std::swap(len, aOther.len);
}

template <typename T>
inline StyleOwnedSlice<T>::StyleOwnedSlice(const StyleOwnedSlice& aOther)
    : StyleOwnedSlice() {
  CopyFrom(aOther);
}

template <typename T>
inline StyleOwnedSlice<T>::StyleOwnedSlice(StyleOwnedSlice&& aOther)
    : StyleOwnedSlice() {
  SwapElements(aOther);
}

template <typename T>
inline StyleOwnedSlice<T>::StyleOwnedSlice(Vector<T>&& aVector)
    : StyleOwnedSlice() {
  if (!aVector.length()) {
    return;
  }

  // We could handle this if Vector provided the relevant APIs, see bug 1610702.
  MOZ_DIAGNOSTIC_ASSERT(aVector.length() == aVector.capacity(),
                        "Shouldn't over-allocate");
  len = aVector.length();
  ptr = aVector.extractRawBuffer();
  MOZ_ASSERT(ptr,
             "How did extractRawBuffer return null if we're not using inline "
             "capacity?");
}

template <typename T>
inline StyleOwnedSlice<T>& StyleOwnedSlice<T>::operator=(
    const StyleOwnedSlice& aOther) {
  CopyFrom(aOther);
  return *this;
}

template <typename T>
inline StyleOwnedSlice<T>& StyleOwnedSlice<T>::operator=(
    StyleOwnedSlice&& aOther) {
  Clear();
  SwapElements(aOther);
  return *this;
}

template <typename T>
inline StyleOwnedSlice<T>::~StyleOwnedSlice() {
  Clear();
}

// This code is basically a C++ port of the Arc::clone() implementation in
// servo/components/servo_arc/lib.rs.
static constexpr const size_t kStaticRefcount =
    std::numeric_limits<size_t>::max();
static constexpr const size_t kMaxRefcount =
    std::numeric_limits<intptr_t>::max();

template <typename T>
inline void StyleArcInner<T>::IncrementRef() {
  if (count.load(std::memory_order_relaxed) != kStaticRefcount) {
    auto old_size = count.fetch_add(1, std::memory_order_relaxed);
    if (MOZ_UNLIKELY(old_size > kMaxRefcount)) {
      ::abort();
    }
  }
}

// This is a C++ port-ish of Arc::drop().
template <typename T>
inline bool StyleArcInner<T>::DecrementRef() {
  if (count.load(std::memory_order_relaxed) == kStaticRefcount) {
    return false;
  }
  if (count.fetch_sub(1, std::memory_order_release) != 1) {
    return false;
  }
#ifdef MOZ_TSAN
  // TSan doesn't understand std::atomic_thread_fence, so in order
  // to avoid a false positive for every time a refcounted object
  // is deleted, we replace the fence with an atomic operation.
  count.load(std::memory_order_acquire);
#else
  std::atomic_thread_fence(std::memory_order_acquire);
#endif
  MOZ_LOG_DTOR(this, "ServoArc", 8);
  return true;
}

template <typename H, typename T>
inline bool StyleHeaderSlice<H, T>::operator==(
    const StyleHeaderSlice& aOther) const {
  return header == aOther.header && AsSpan() == aOther.AsSpan();
}

template <typename H, typename T>
inline bool StyleHeaderSlice<H, T>::operator!=(
    const StyleHeaderSlice& aOther) const {
  return !(*this == aOther);
}

template <typename H, typename T>
inline StyleHeaderSlice<H, T>::~StyleHeaderSlice() {
  for (T& elem : Span(data, len)) {
    elem.~T();
  }
}

template <typename H, typename T>
inline Span<const T> StyleHeaderSlice<H, T>::AsSpan() const {
  // Explicitly specify template argument here to avoid instantiating Span<T>
  // first and then implicitly converting to Span<const T>
  return Span<const T>{data, len};
}

static constexpr const uint64_t kArcSliceCanary = 0xf3f3f3f3f3f3f3f3;

#define ASSERT_CANARY \
  MOZ_DIAGNOSTIC_ASSERT(_0.p->data.header == kArcSliceCanary, "Uh?");

template <typename T>
inline StyleArcSlice<T>::StyleArcSlice()
    : _0(reinterpret_cast<decltype(_0.p)>(Servo_StyleArcSlice_EmptyPtr())) {
  ASSERT_CANARY
}

template <typename T>
inline StyleArcSlice<T>::StyleArcSlice(
    const StyleForgottenArcSlicePtr<T>& aPtr) {
  // See the forget() implementation to see why reinterpret_cast() is ok.
  _0.p = reinterpret_cast<decltype(_0.p)>(aPtr._0);
  ASSERT_CANARY
}

template <typename T>
inline size_t StyleArcSlice<T>::Length() const {
  ASSERT_CANARY
  return _0->Length();
}

template <typename T>
inline bool StyleArcSlice<T>::IsEmpty() const {
  ASSERT_CANARY
  return _0->IsEmpty();
}

template <typename T>
inline Span<const T> StyleArcSlice<T>::AsSpan() const {
  ASSERT_CANARY
  return _0->AsSpan();
}

#undef ASSERT_CANARY

template <typename T>
inline StyleArc<T>::StyleArc(const StyleArc& aOther) : p(aOther.p) {
  p->IncrementRef();
}

template <typename T>
inline void StyleArc<T>::Release() {
  if (MOZ_LIKELY(!p->DecrementRef())) {
    return;
  }
  p->data.~T();
  free(p);
}

template <typename T>
inline StyleArc<T>& StyleArc<T>::operator=(const StyleArc& aOther) {
  if (p != aOther.p) {
    Release();
    p = aOther.p;
    p->IncrementRef();
  }
  return *this;
}

template <typename T>
inline StyleArc<T>& StyleArc<T>::operator=(StyleArc&& aOther) {
  std::swap(p, aOther.p);
  return *this;
}

template <typename T>
inline StyleArc<T>::~StyleArc() {
  Release();
}

inline bool StyleAtom::IsStatic() const { return !!(_0 & 1); }

inline nsAtom* StyleAtom::AsAtom() const {
  if (IsStatic()) {
    return const_cast<nsStaticAtom*>(&detail::gGkAtoms.mAtoms[_0 >> 1]);
  }
  return reinterpret_cast<nsAtom*>(_0);
}

inline void StyleAtom::AddRef() {
  if (!IsStatic()) {
    AsAtom()->AddRef();
  }
}

inline void StyleAtom::Release() {
  if (!IsStatic()) {
    AsAtom()->Release();
  }
}

inline StyleAtom::StyleAtom(already_AddRefed<nsAtom> aAtom) {
  nsAtom* atom = aAtom.take();
  if (atom->IsStatic()) {
    size_t index = atom->AsStatic() - &detail::gGkAtoms.mAtoms[0];
    _0 = (index << 1) | 1;
  } else {
    _0 = reinterpret_cast<uintptr_t>(atom);
  }
  MOZ_ASSERT(IsStatic() == atom->IsStatic());
  MOZ_ASSERT(AsAtom() == atom);
}

inline StyleAtom::StyleAtom(nsStaticAtom* aAtom)
    : StyleAtom(do_AddRef(static_cast<nsAtom*>(aAtom))) {}

inline StyleAtom::StyleAtom(const StyleAtom& aOther) : _0(aOther._0) {
  AddRef();
}

inline StyleAtom& StyleAtom::operator=(const StyleAtom& aOther) {
  if (MOZ_LIKELY(this != &aOther)) {
    Release();
    _0 = aOther._0;
    AddRef();
  }
  return *this;
}

inline StyleAtom::~StyleAtom() { Release(); }

inline nsAtom* StyleCustomIdent::AsAtom() const { return _0.AsAtom(); }

inline nsDependentCSubstring StyleOwnedStr::AsString() const {
  Span<const uint8_t> s = _0.AsSpan();
  return nsDependentCSubstring(reinterpret_cast<const char*>(s.Elements()),
                               s.Length());
}

template <typename T>
inline Span<const T> StyleGenericTransform<T>::Operations() const {
  return _0.AsSpan();
}

template <typename T>
inline bool StyleGenericTransform<T>::IsNone() const {
  return Operations().IsEmpty();
}

inline StyleAngle StyleAngle::Zero() { return {0.0f}; }

inline float StyleAngle::ToDegrees() const { return _0; }

inline double StyleAngle::ToRadians() const {
  return double(ToDegrees()) * kRadPerDegree;
}

inline bool StyleUrlExtraData::IsShared() const { return !!(_0 & 1); }

inline StyleUrlExtraData::~StyleUrlExtraData() {
  if (!IsShared()) {
    reinterpret_cast<URLExtraData*>(_0)->Release();
  }
}

inline const URLExtraData& StyleUrlExtraData::get() const {
  if (IsShared()) {
    return *URLExtraData::sShared[_0 >> 1];
  }
  return *reinterpret_cast<const URLExtraData*>(_0);
}

inline nsDependentCSubstring StyleCssUrl::SpecifiedSerialization() const {
  return _0->serialization.AsString();
}

inline const URLExtraData& StyleCssUrl::ExtraData() const {
  return _0->extra_data.get();
}

inline const StyleLoadData& StyleCssUrl::LoadData() const {
  if (MOZ_LIKELY(_0->load_data.tag == StyleLoadDataSource::Tag::Owned)) {
    return _0->load_data.owned._0;
  }
  return *Servo_LoadData_GetLazy(&_0->load_data);
}

inline StyleLoadData& StyleCssUrl::MutLoadData() const {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread() ||
                        dom::IsCurrentThreadRunningWorker());
  return const_cast<StyleLoadData&>(LoadData());
}

inline nsIURI* StyleCssUrl::GetURI() const {
  auto& loadData = const_cast<StyleLoadData&>(LoadData());
  // Try to read the flags first. If it's set we can avoid entering the CAS
  // loop.
  auto flags = __atomic_load_n(&loadData.flags.bits, __ATOMIC_RELAXED);
  if (!(flags & StyleLoadDataFlags::TRIED_TO_RESOLVE_URI.bits)) {
    nsDependentCSubstring serialization = SpecifiedSerialization();
    // https://drafts.csswg.org/css-values-4/#url-empty:
    //
    //     If the value of the url() is the empty string (like url("") or
    //     url()), the url must resolve to an invalid resource (similar to what
    //     the url about:invalid does).
    //
    nsIURI* resolved = nullptr;
    if (!serialization.IsEmpty()) {
      nsIURI* old_resolved = nullptr;
      // NOTE: This addrefs `resolved`, and `resolved` might still be null for
      // invalid URIs.
      NS_NewURI(&resolved, serialization, nullptr, ExtraData().BaseURI());
      if (!__atomic_compare_exchange_n(&loadData.resolved_uri, &old_resolved,
                                       resolved, /* weak = */ false,
                                       __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
        // In the unlikely case two threads raced to write the url, avoid
        // leaking resolved. The actual value is in `old_resolved`.
        NS_IF_RELEASE(resolved);
        resolved = old_resolved;
      }
    }
    // The flag is effectively just an optimization so we can use relaxed
    // ordering.
    __atomic_fetch_or(&loadData.flags.bits,
                      StyleLoadDataFlags::TRIED_TO_RESOLVE_URI.bits,
                      __ATOMIC_RELAXED);
    return resolved;
  }
  return __atomic_load_n(&loadData.resolved_uri, __ATOMIC_ACQUIRE);
}

inline nsDependentCSubstring StyleComputedUrl::SpecifiedSerialization() const {
  return _0.SpecifiedSerialization();
}
inline const URLExtraData& StyleComputedUrl::ExtraData() const {
  return _0.ExtraData();
}
inline const StyleLoadData& StyleComputedUrl::LoadData() const {
  return _0.LoadData();
}
inline StyleLoadData& StyleComputedUrl::MutLoadData() const {
  return _0.MutLoadData();
}
inline StyleCorsMode StyleComputedUrl::CorsMode() const {
  return _0._0->cors_mode;
}
inline nsIURI* StyleComputedUrl::GetURI() const { return _0.GetURI(); }

inline bool StyleComputedUrl::IsLocalRef() const {
  return Servo_CssUrl_IsLocalRef(&_0);
}

inline bool StyleComputedUrl::HasRef() const {
  if (IsLocalRef()) {
    return true;
  }
  if (nsIURI* uri = GetURI()) {
    bool hasRef = false;
    return NS_SUCCEEDED(uri->GetHasRef(&hasRef)) && hasRef;
  }
  return false;
}

inline bool StyleComputedUrl::IsImageResolved() const {
  return bool(LoadData().flags & StyleLoadDataFlags::TRIED_TO_RESOLVE_IMAGE);
}

inline imgRequestProxy* StyleComputedUrl::GetImage() const {
  MOZ_ASSERT(IsImageResolved());
  return LoadData().resolved_image;
}

template <>
inline bool StyleGradient::Repeating() const {
  if (IsLinear()) {
    return bool(AsLinear().flags & StyleGradientFlags::REPEATING);
  }
  if (IsRadial()) {
    return bool(AsRadial().flags & StyleGradientFlags::REPEATING);
  }
  return bool(AsConic().flags & StyleGradientFlags::REPEATING);
}

template <>
bool StyleGradient::IsOpaque() const;

template <>
inline const StyleColorInterpolationMethod&
StyleGradient::ColorInterpolationMethod() const {
  if (IsLinear()) {
    return AsLinear().color_interpolation_method;
  }
  if (IsRadial()) {
    return AsRadial().color_interpolation_method;
  }
  return AsConic().color_interpolation_method;
}

inline StyleAnimationName::StyleAnimationName() : _0(nsGkAtoms::_empty) {};

inline StyleTimelineIdent::StyleTimelineIdent() : _0(nsGkAtoms::_empty) {};

template <typename Integer>
inline StyleGenericGridLine<Integer>::StyleGenericGridLine()
    : ident{StyleAtom(nsGkAtoms::_empty)}, line_num(0), is_span(false) {}

template <>
inline nsAtom* StyleGridLine::LineName() const {
  return ident.AsAtom();
}

template <>
inline bool StyleGridLine::IsAuto() const {
  return LineName()->IsEmpty() && line_num == 0 && !is_span;
}

using LengthPercentage = StyleLengthPercentage;
using LengthPercentageOrAuto = StyleLengthPercentageOrAuto;
using NonNegativeLengthPercentage = StyleNonNegativeLengthPercentage;
using NonNegativeLengthPercentageOrAuto =
    StyleNonNegativeLengthPercentageOrAuto;
using NonNegativeLengthPercentageOrNormal =
    StyleNonNegativeLengthPercentageOrNormal;
using Length = StyleLength;
using LengthOrAuto = StyleLengthOrAuto;
using NonNegativeLength = StyleNonNegativeLength;
using NonNegativeLengthOrAuto = StyleNonNegativeLengthOrAuto;
using BorderRadius = StyleBorderRadius;

bool StyleCSSPixelLength::IsZero() const { return _0 == 0.0f; }

void StyleCSSPixelLength::ScaleBy(float aScale) { _0 *= aScale; }

StyleCSSPixelLength StyleCSSPixelLength::ScaledBy(float aScale) const {
  return FromPixels(ToCSSPixels() * aScale);
}

namespace detail {
static inline nscoord DefaultPercentLengthToAppUnits(float aPixelLength) {
  return NSToCoordTruncClamped(aPixelLength);
}

static inline nscoord DefaultLengthToAppUnits(float aPixelLength) {
  // We want to round lengths rounding 0.5 away from zero, instead of the
  // default behavior of NSToCoordRound{,WithClamp} which do floor(x + 0.5).
  float length = aPixelLength * float(mozilla::AppUnitsPerCSSPixel());
  if (length >= float(nscoord_MAX)) {
    return nscoord_MAX;
  }
  if (length <= float(nscoord_MIN)) {
    return nscoord_MIN;
  }
  return NSToIntRound(length);
}
}  // namespace detail

nscoord StyleCSSPixelLength::ToAppUnits() const {
  if (IsZero()) {
    // Avoid the expensive FP math below.
    return 0;
  }
  return detail::DefaultLengthToAppUnits(_0);
}

bool LengthPercentage::IsLength() const { return Tag() == TAG_LENGTH; }

StyleLengthPercentageUnion::StyleLengthPercentageUnion() {
  length = {TAG_LENGTH, {0.0f}};
  MOZ_ASSERT(IsLength());
}

static_assert(sizeof(LengthPercentage) == sizeof(uint64_t), "");

Length& LengthPercentage::AsLength() {
  MOZ_ASSERT(IsLength());
  return length.length;
}

const Length& LengthPercentage::AsLength() const {
  return const_cast<LengthPercentage*>(this)->AsLength();
}

bool LengthPercentage::IsPercentage() const { return Tag() == TAG_PERCENTAGE; }

StylePercentage& LengthPercentage::AsPercentage() {
  MOZ_ASSERT(IsPercentage());
  return percentage.percentage;
}

const StylePercentage& LengthPercentage::AsPercentage() const {
  return const_cast<LengthPercentage*>(this)->AsPercentage();
}

bool LengthPercentage::IsCalc() const { return Tag() == TAG_CALC; }

StyleCalcLengthPercentage& LengthPercentage::AsCalc() {
  MOZ_ASSERT(IsCalc());
  // NOTE: in 32-bits, the pointer is not swapped, and goes along with the tag.
#ifdef SERVO_32_BITS
  return *reinterpret_cast<StyleCalcLengthPercentage*>(calc.ptr);
#else
  return *reinterpret_cast<StyleCalcLengthPercentage*>(
      NativeEndian::swapFromLittleEndian(calc.ptr));
#endif
}

const StyleCalcLengthPercentage& LengthPercentage::AsCalc() const {
  return const_cast<LengthPercentage*>(this)->AsCalc();
}

StyleLengthPercentageUnion::StyleLengthPercentageUnion(const Self& aOther) {
  if (aOther.IsLength()) {
    length = {TAG_LENGTH, aOther.AsLength()};
  } else if (aOther.IsPercentage()) {
    percentage = {TAG_PERCENTAGE, aOther.AsPercentage()};
  } else {
    MOZ_ASSERT(aOther.IsCalc());
    auto* ptr = new StyleCalcLengthPercentage(aOther.AsCalc());
    // NOTE: in 32-bits, the pointer is not swapped, and goes along with the
    // tag.
    calc = {
#ifdef SERVO_32_BITS
        TAG_CALC,
        ptr,
#else
        NativeEndian::swapToLittleEndian(reinterpret_cast<uintptr_t>(ptr)),
#endif
    };
  }
  MOZ_ASSERT(Tag() == aOther.Tag());
}

StyleLengthPercentageUnion::~StyleLengthPercentageUnion() {
  if (IsCalc()) {
    delete &AsCalc();
  }
}

LengthPercentage& LengthPercentage::operator=(const LengthPercentage& aOther) {
  if (this != &aOther) {
    this->~LengthPercentage();
    new (this) LengthPercentage(aOther);
  }
  return *this;
}

bool LengthPercentage::operator==(const LengthPercentage& aOther) const {
  if (Tag() != aOther.Tag()) {
    return false;
  }
  if (IsLength()) {
    return AsLength() == aOther.AsLength();
  }
  if (IsPercentage()) {
    return AsPercentage() == aOther.AsPercentage();
  }
  return AsCalc() == aOther.AsCalc();
}

bool LengthPercentage::operator!=(const LengthPercentage& aOther) const {
  return !(*this == aOther);
}

LengthPercentage LengthPercentage::Zero() { return {}; }

LengthPercentage LengthPercentage::FromPixels(CSSCoord aCoord) {
  LengthPercentage l;
  MOZ_ASSERT(l.IsLength());
  l.length.length = {aCoord};
  return l;
}

LengthPercentage LengthPercentage::FromAppUnits(nscoord aCoord) {
  return FromPixels(CSSPixel::FromAppUnits(aCoord));
}

LengthPercentage LengthPercentage::FromPercentage(float aPercentage) {
  LengthPercentage l;
  l.percentage = {TAG_PERCENTAGE, {aPercentage}};
  return l;
}

bool LengthPercentage::HasPercent() const { return IsPercentage() || IsCalc(); }

bool LengthPercentage::ConvertsToLength() const { return IsLength(); }

nscoord LengthPercentage::ToLength() const {
  MOZ_ASSERT(ConvertsToLength());
  return AsLength().ToAppUnits();
}

CSSCoord LengthPercentage::ToLengthInCSSPixels() const {
  MOZ_ASSERT(ConvertsToLength());
  return AsLength().ToCSSPixels();
}

bool LengthPercentage::ConvertsToPercentage() const { return IsPercentage(); }

float LengthPercentage::ToPercentage() const {
  MOZ_ASSERT(ConvertsToPercentage());
  return AsPercentage()._0;
}

bool LengthPercentage::HasLengthAndPercentage() const {
  if (!IsCalc()) {
    return false;
  }
  MOZ_ASSERT(!ConvertsToLength() && !ConvertsToPercentage(),
             "Should've been simplified earlier");
  return true;
}

bool LengthPercentage::IsDefinitelyZero() const {
  if (IsLength()) {
    return AsLength().IsZero();
  }
  if (IsPercentage()) {
    return AsPercentage()._0 == 0.0f;
  }
  // calc() should've been simplified to a percentage.
  return false;
}

CSSCoord StyleCalcLengthPercentage::ResolveToCSSPixels(CSSCoord aBasis) const {
  return Servo_ResolveCalcLengthPercentage(this, aBasis);
}

template <typename Rounder>
nscoord StyleCalcLengthPercentage::Resolve(nscoord aBasis,
                                           Rounder aRounder) const {
  static_assert(std::is_same_v<decltype(aRounder(1.0f)), nscoord>);
  CSSCoord result = ResolveToCSSPixels(CSSPixel::FromAppUnits(aBasis));
  return aRounder(result * AppUnitsPerCSSPixel());
}

template <>
void StyleCalcNode::ScaleLengthsBy(float);

CSSCoord LengthPercentage::ResolveToCSSPixels(CSSCoord aPercentageBasis) const {
  if (IsLength()) {
    return AsLength().ToCSSPixels();
  }
  if (IsPercentage()) {
    return AsPercentage()._0 * aPercentageBasis;
  }
  return AsCalc().ResolveToCSSPixels(aPercentageBasis);
}

template <typename T>
CSSCoord LengthPercentage::ResolveToCSSPixelsWith(T aPercentageGetter) const {
  static_assert(std::is_same_v<decltype(aPercentageGetter()), CSSCoord>);
  if (ConvertsToLength()) {
    return ToLengthInCSSPixels();
  }
  return ResolveToCSSPixels(aPercentageGetter());
}

template <typename T, typename Rounder>
nscoord LengthPercentage::Resolve(T aPercentageGetter, Rounder aRounder) const {
  static_assert(std::is_same_v<decltype(aPercentageGetter()), nscoord>);
  static_assert(std::is_same_v<decltype(aRounder(1.0f)), nscoord>);
  if (ConvertsToLength()) {
    return ToLength();
  }
  if (IsPercentage() && AsPercentage()._0 == 0.0f) {
    return 0;
  }
  nscoord basis = aPercentageGetter();
  if (IsPercentage()) {
    return aRounder(basis * AsPercentage()._0);
  }
  return AsCalc().Resolve(basis, aRounder);
}

nscoord LengthPercentage::Resolve(nscoord aPercentageBasis) const {
  return Resolve([=] { return aPercentageBasis; },
                 detail::DefaultPercentLengthToAppUnits);
}

template <typename T>
nscoord LengthPercentage::Resolve(T aPercentageGetter) const {
  return Resolve(aPercentageGetter, detail::DefaultPercentLengthToAppUnits);
}

template <typename Rounder>
nscoord LengthPercentage::Resolve(nscoord aPercentageBasis,
                                  Rounder aRounder) const {
  return Resolve([aPercentageBasis] { return aPercentageBasis; }, aRounder);
}

void LengthPercentage::ScaleLengthsBy(float aScale) {
  if (IsLength()) {
    AsLength().ScaleBy(aScale);
  }
  if (IsCalc()) {
    AsCalc().node.ScaleLengthsBy(aScale);
  }
}

#define IMPL_LENGTHPERCENTAGE_FORWARDS(ty_)                                 \
  template <>                                                               \
  inline bool ty_::HasPercent() const {                                     \
    return IsLengthPercentage() && AsLengthPercentage().HasPercent();       \
  }                                                                         \
  template <>                                                               \
  inline bool ty_::ConvertsToLength() const {                               \
    return IsLengthPercentage() && AsLengthPercentage().ConvertsToLength(); \
  }                                                                         \
  template <>                                                               \
  inline bool ty_::HasLengthAndPercentage() const {                         \
    return IsLengthPercentage() &&                                          \
           AsLengthPercentage().HasLengthAndPercentage();                   \
  }                                                                         \
  template <>                                                               \
  inline nscoord ty_::ToLength() const {                                    \
    MOZ_ASSERT(ConvertsToLength());                                         \
    return AsLengthPercentage().ToLength();                                 \
  }                                                                         \
  template <>                                                               \
  inline bool ty_::ConvertsToPercentage() const {                           \
    return IsLengthPercentage() &&                                          \
           AsLengthPercentage().ConvertsToPercentage();                     \
  }                                                                         \
  template <>                                                               \
  inline float ty_::ToPercentage() const {                                  \
    MOZ_ASSERT(ConvertsToPercentage());                                     \
    return AsLengthPercentage().ToPercentage();                             \
  }

IMPL_LENGTHPERCENTAGE_FORWARDS(LengthPercentageOrAuto)
IMPL_LENGTHPERCENTAGE_FORWARDS(StyleSize)
IMPL_LENGTHPERCENTAGE_FORWARDS(StyleMaxSize)
IMPL_LENGTHPERCENTAGE_FORWARDS(StyleInset)
IMPL_LENGTHPERCENTAGE_FORWARDS(StyleMargin)

template <>
inline bool StyleInset::HasAnchorPositioningFunction() const {
  return IsAnchorFunction() || IsAnchorSizeFunction() ||
         IsAnchorContainingCalcFunction();
}

template <>
inline bool StyleMargin::HasAnchorPositioningFunction() const {
  return IsAnchorSizeFunction() || IsAnchorContainingCalcFunction();
}

template <>
inline bool StyleSize::HasAnchorPositioningFunction() const {
  return IsAnchorSizeFunction() || IsAnchorContainingCalcFunction();
}

template <>
inline StyleSize StyleSize::FromAppUnits(nscoord aAppUnits) {
  return StyleSize::LengthPercentage(LengthPercentage::FromAppUnits(aAppUnits));
}

template <>
inline bool StyleMaxSize::HasAnchorPositioningFunction() const {
  return IsAnchorSizeFunction() || IsAnchorContainingCalcFunction();
}

#undef IMPL_LENGTHPERCENTAGE_FORWARDS

template <>
inline bool LengthOrAuto::IsLength() const {
  return IsLengthPercentage();
}

template <>
inline const Length& LengthOrAuto::AsLength() const {
  return AsLengthPercentage();
}

template <>
inline nscoord LengthOrAuto::ToLength() const {
  return AsLength().ToAppUnits();
}

template <>
inline bool StyleFlexBasis::IsAuto() const {
  return IsSize() && AsSize().IsAuto();
}

#define IMPL_BEHAVES_LIKE_SIZE_METHODS(ty_, isInitialValMethod_)        \
  template <>                                                           \
  inline bool ty_::BehavesLikeStretchOnInlineAxis() const {             \
    return IsStretch() || IsMozAvailable() || IsWebkitFillAvailable();  \
  }                                                                     \
  template <>                                                           \
  inline bool ty_::BehavesLikeStretchOnBlockAxis() const {              \
    /* TODO(dholbert): Add "|| IsMozAvailable()" in bug 527285. */      \
    return IsStretch() || IsWebkitFillAvailable();                      \
  }                                                                     \
  template <>                                                           \
  inline bool ty_::BehavesLikeInitialValueOnBlockAxis() const {         \
    return isInitialValMethod_() ||                                     \
           (!BehavesLikeStretchOnBlockAxis() && !IsLengthPercentage()); \
  }

IMPL_BEHAVES_LIKE_SIZE_METHODS(StyleSize, IsAuto)
IMPL_BEHAVES_LIKE_SIZE_METHODS(StyleMaxSize, IsNone)

#undef IMPL_BEHAVES_LIKE_SIZE_METHODS

template <>
inline bool StyleBackgroundSize::IsInitialValue() const {
  return IsExplicitSize() && explicit_size.width.IsAuto() &&
         explicit_size.height.IsAuto();
}

template <typename T>
const T& StyleRect<T>::Get(mozilla::Side aSide) const {
  static_assert(sizeof(StyleRect<T>) == sizeof(T) * 4, "");
  static_assert(alignof(StyleRect<T>) == alignof(T), "");
  return reinterpret_cast<const T*>(this)[aSide];
}

template <typename T>
T& StyleRect<T>::Get(mozilla::Side aSide) {
  return const_cast<T&>(static_cast<const StyleRect&>(*this).Get(aSide));
}

template <typename T>
template <typename Predicate>
bool StyleRect<T>::All(Predicate aPredicate) const {
  return aPredicate(_0) && aPredicate(_1) && aPredicate(_2) && aPredicate(_3);
}

template <typename T>
template <typename Predicate>
bool StyleRect<T>::Any(Predicate aPredicate) const {
  return aPredicate(_0) || aPredicate(_1) || aPredicate(_2) || aPredicate(_3);
}

template <>
inline const LengthPercentage& BorderRadius::Get(HalfCorner aCorner) const {
  static_assert(sizeof(BorderRadius) == sizeof(LengthPercentage) * 8, "");
  static_assert(alignof(BorderRadius) == alignof(LengthPercentage), "");
  const auto* self = reinterpret_cast<const LengthPercentage*>(this);
  return self[aCorner];
}

template <>
inline bool StyleTrackBreadth::HasPercent() const {
  return IsBreadth() && AsBreadth().HasPercent();
}

// Implemented in nsStyleStructs.cpp
template <>
bool StyleTransform::HasPercent() const;

template <>
inline bool StyleTransformOrigin::HasPercent() const {
  // NOTE(emilio): `depth` is just a `<length>` so doesn't have a percentage at
  // all.
  return horizontal.HasPercent() || vertical.HasPercent();
}

template <>
inline Maybe<size_t> StyleGridTemplateComponent::RepeatAutoIndex() const {
  if (!IsTrackList()) {
    return Nothing();
  }
  const auto& list = *AsTrackList();
  return list.auto_repeat_index < list.values.Length()
             ? Some(list.auto_repeat_index)
             : Nothing();
}

template <>
inline bool StyleGridTemplateComponent::HasRepeatAuto() const {
  return RepeatAutoIndex().isSome();
}

template <>
inline Span<const StyleGenericTrackListValue<LengthPercentage, StyleInteger>>
StyleGridTemplateComponent::TrackListValues() const {
  if (IsTrackList()) {
    return AsTrackList()->values.AsSpan();
  }
  return {};
}

template <>
inline const StyleGenericTrackRepeat<LengthPercentage, StyleInteger>*
StyleGridTemplateComponent::GetRepeatAutoValue() const {
  auto index = RepeatAutoIndex();
  if (!index) {
    return nullptr;
  }
  return &TrackListValues()[*index].AsTrackRepeat();
}

constexpr const auto kPaintOrderShift = StylePAINT_ORDER_SHIFT;
constexpr const auto kPaintOrderMask = StylePAINT_ORDER_MASK;

template <>
inline nsRect StyleGenericClipRect<LengthOrAuto>::ToLayoutRect(
    nscoord aAutoSize) const {
  nscoord x = left.IsLength() ? left.ToLength() : 0;
  nscoord y = top.IsLength() ? top.ToLength() : 0;
  nscoord width = right.IsLength() ? right.ToLength() - x : aAutoSize;
  nscoord height = bottom.IsLength() ? bottom.ToLength() - y : aAutoSize;
  return nsRect(x, y, width, height);
}

using RestyleHint = StyleRestyleHint;

inline RestyleHint RestyleHint::RestyleSubtree() {
  return RESTYLE_SELF | RESTYLE_DESCENDANTS;
}

inline RestyleHint RestyleHint::RecascadeSubtree() {
  return RECASCADE_SELF | RECASCADE_DESCENDANTS;
}

inline RestyleHint RestyleHint::ForAnimations() {
  return RESTYLE_CSS_TRANSITIONS | RESTYLE_CSS_ANIMATIONS | RESTYLE_SMIL;
}

inline bool RestyleHint::DefinitelyRecascadesAllSubtree() const {
  if (!(*this & (RECASCADE_DESCENDANTS | RESTYLE_DESCENDANTS))) {
    return false;
  }
  return bool(*this & (RESTYLE_SELF | RECASCADE_SELF));
}

template <>
ImageResolution StyleImage::GetResolution(const ComputedStyle*) const;

template <>
inline const StyleImage& StyleImage::FinalImage() const {
  if (!IsImageSet()) {
    return *this;
  }
  const auto& set = *AsImageSet();
  auto items = set.items.AsSpan();
  if (MOZ_LIKELY(set.selected_index < items.Length())) {
    return items[set.selected_index].image.FinalImage();
  }
  static auto sNone = StyleImage::None();
  return sNone;
}

template <>
inline bool StyleImage::IsImageRequestType() const {
  const auto& finalImage = FinalImage();
  return finalImage.IsUrl();
}

template <>
inline const StyleComputedUrl* StyleImage::GetImageRequestURLValue() const {
  const auto& finalImage = FinalImage();
  if (finalImage.IsUrl()) {
    return &finalImage.AsUrl();
  }
  return nullptr;
}

template <>
inline imgRequestProxy* StyleImage::GetImageRequest() const {
  const auto* url = GetImageRequestURLValue();
  return url ? url->GetImage() : nullptr;
}

template <>
inline bool StyleImage::IsResolved() const {
  const auto* url = GetImageRequestURLValue();
  return !url || url->IsImageResolved();
}

template <>
bool StyleImage::IsOpaque() const;
template <>
bool StyleImage::IsSizeAvailable() const;
template <>
bool StyleImage::IsComplete() const;
template <>
void StyleImage::ResolveImage(dom::Document&, const StyleImage*);

template <>
inline AspectRatio StyleRatio<StyleNonNegativeNumber>::ToLayoutRatio(
    UseBoxSizing aUseBoxSizing) const {
  // 0/1, 1/0, and 0/0 are all degenerate ratios (which behave as auto), and we
  // always return 0.0f.
  // https://drafts.csswg.org/css-values-4/#degenerate-ratio
  return AspectRatio::FromSize(_0, _1, aUseBoxSizing);
}

template <>
inline AspectRatio StyleAspectRatio::ToLayoutRatio() const {
  return HasRatio() ? ratio.AsRatio().ToLayoutRatio(auto_ ? UseBoxSizing::No
                                                          : UseBoxSizing::Yes)
                    : AspectRatio();
}

inline void StyleFontWeight::ToString(nsACString& aString) const {
  Servo_FontWeight_ToCss(this, &aString);
}

inline void StyleFontStretch::ToString(nsACString& aString) const {
  Servo_FontStretch_ToCss(this, &aString);
}

inline void StyleFontStyle::ToString(nsACString& aString) const {
  Servo_FontStyle_ToCss(this, &aString);
}

inline bool StyleFontWeight::IsBold() const { return *this >= BOLD_THRESHOLD; }

inline bool StyleFontWeight::PreferBold() const {
  return *this > PREFER_BOLD_THRESHOLD;
}

inline bool StyleFontStyle::IsItalic() const { return *this == ITALIC; }

inline float StyleFontStyle::ObliqueAngle() const {
  MOZ_ASSERT(!IsItalic());
  return ToFloat();
}

inline float StyleFontStyle::SlantAngle() const {
  return IsNormal() ? 0 : IsItalic() ? DEFAULT_OBLIQUE_DEGREES : ObliqueAngle();
}

using FontStretch = StyleFontStretch;
using FontSlantStyle = StyleFontStyle;
using FontWeight = StyleFontWeight;

inline StyleFontPalette StyleFontPalette::Normal() {
  return StyleFontPalette{StyleAtom(nsGkAtoms::normal->ToAddRefed())};
}

template <>
inline double StyleComputedTimingFunction::At(double aPortion,
                                              bool aBeforeFlag) const {
  return Servo_EasingFunctionAt(
      this, aPortion,
      aBeforeFlag ? StyleEasingBeforeFlag::Set : StyleEasingBeforeFlag::Unset);
}

template <>
inline void StyleComputedTimingFunction::AppendToString(
    nsACString& aOut) const {
  return Servo_SerializeEasing(this, &aOut);
}

template <>
inline double StyleComputedTimingFunction::GetPortion(
    const Maybe<StyleComputedTimingFunction>& aFn, double aPortion,
    bool aBeforeFlag) {
  return aFn ? aFn->At(aPortion, aBeforeFlag) : aPortion;
}

/* static */
template <>
inline LengthPercentageOrAuto LengthPercentageOrAuto::Zero() {
  return LengthPercentage(LengthPercentage::Zero());
}

template <>
inline StyleViewTimelineInset::StyleGenericViewTimelineInset()
    : start(LengthPercentageOrAuto::Auto()),
      end(LengthPercentageOrAuto::Auto()) {}

/* static */
template <>
inline StyleAnimationRangeStart
StyleGenericAnimationRangeValue<LengthPercentage>::DefaultStart() {
  return {StyleTimelineRangeName::Normal,
          LengthPercentage::FromPercentage(0.0f)};
}

/* static */
template <>
inline StyleAnimationRangeEnd
StyleGenericAnimationRangeValue<LengthPercentage>::DefaultEnd() {
  return {StyleTimelineRangeName::Normal,
          LengthPercentage::FromPercentage(1.0f)};
}

inline StyleDisplayOutside StyleDisplay::Outside() const {
  return StyleDisplayOutside((_0 & OUTSIDE_MASK) >> OUTSIDE_SHIFT);
}

inline StyleDisplayInside StyleDisplay::Inside() const {
  return StyleDisplayInside(_0 & INSIDE_MASK);
}

inline bool StyleDisplay::IsListItem() const { return _0 & LIST_ITEM_MASK; }

inline bool StyleDisplay::IsInternalTable() const {
  return Outside() == StyleDisplayOutside::InternalTable;
}

inline bool StyleDisplay::IsInternalTableExceptCell() const {
  return IsInternalTable() && *this != TableCell;
}

inline bool StyleDisplay::IsInternalRuby() const {
  return Outside() == StyleDisplayOutside::InternalRuby;
}

inline bool StyleDisplay::IsRuby() const {
  return Inside() == StyleDisplayInside::Ruby || IsInternalRuby();
}

inline bool StyleDisplay::IsInlineFlow() const {
  return Outside() == StyleDisplayOutside::Inline &&
         Inside() == StyleDisplayInside::Flow;
}

inline bool StyleDisplay::IsInlineInside() const {
  return IsInlineFlow() || IsRuby();
}

inline bool StyleDisplay::IsInlineOutside() const {
  return Outside() == StyleDisplayOutside::Inline || IsInternalRuby();
}

inline float StyleZoom::Zoom(float aValue) const {
  if (*this == ONE) {
    return aValue;
  }
  return ToFloat() * aValue;
}

inline float StyleZoom::Unzoom(float aValue) const {
  if (*this == ONE) {
    return aValue;
  }
  return aValue / ToFloat();
}

inline nscoord StyleZoom::ZoomCoord(nscoord aValue) const {
  if (*this == ONE) {
    return aValue;
  }
  return NSToCoordRoundWithClamp(Zoom(float(aValue)));
}

inline nscoord StyleZoom::UnzoomCoord(nscoord aValue) const {
  if (*this == ONE) {
    return aValue;
  }
  return NSToCoordRoundWithClamp(Unzoom(float(aValue)));
}

inline nsSize StyleZoom::Zoom(const nsSize& aValue) const {
  if (*this == ONE) {
    return aValue;
  }
  return nsSize(ZoomCoord(aValue.Width()), ZoomCoord(aValue.Height()));
}

inline nsSize StyleZoom::Unzoom(const nsSize& aValue) const {
  if (*this == ONE) {
    return aValue;
  }
  return nsSize(UnzoomCoord(aValue.Width()), UnzoomCoord(aValue.Height()));
}

inline nsPoint StyleZoom::Zoom(const nsPoint& aValue) const {
  if (*this == ONE) {
    return aValue;
  }
  return nsPoint(ZoomCoord(aValue.X()), ZoomCoord(aValue.Y()));
}

inline nsPoint StyleZoom::Unzoom(const nsPoint& aValue) const {
  if (*this == ONE) {
    return aValue;
  }
  return nsPoint(UnzoomCoord(aValue.X()), UnzoomCoord(aValue.Y()));
}

inline nsRect StyleZoom::Zoom(const nsRect& aValue) const {
  if (*this == ONE) {
    return aValue;
  }
  return nsRect(ZoomCoord(aValue.X()), ZoomCoord(aValue.Y()),
                ZoomCoord(aValue.Width()), ZoomCoord(aValue.Height()));
}

inline nsRect StyleZoom::Unzoom(const nsRect& aValue) const {
  if (*this == ONE) {
    return aValue;
  }
  return nsRect(UnzoomCoord(aValue.X()), UnzoomCoord(aValue.Y()),
                UnzoomCoord(aValue.Width()), UnzoomCoord(aValue.Height()));
}

template <>
inline gfx::Point StyleCoordinatePair<StyleCSSFloat>::ToGfxPoint(
    const CSSSize* aBasis) const {
  return gfx::Point(x, y);
}

template <>
inline gfx::Point StyleCoordinatePair<LengthPercentage>::ToGfxPoint(
    const CSSSize* aBasis) const {
  MOZ_ASSERT(aBasis);
  return gfx::Point(x.ResolveToCSSPixels(aBasis->Width()),
                    y.ResolveToCSSPixels(aBasis->Height()));
}

template <>
inline gfx::Point
StyleGenericPosition<StyleCSSFloat, StyleCSSFloat>::ToGfxPoint(
    const CSSSize* aBasis) const {
  return gfx::Point(horizontal, vertical);
}

template <>
inline gfx::Point StylePosition::ToGfxPoint(const CSSSize* aBasis) const {
  MOZ_ASSERT(aBasis);
  return gfx::Point(horizontal.ResolveToCSSPixels(aBasis->Width()),
                    vertical.ResolveToCSSPixels(aBasis->Height()));
}

template <>
inline gfx::Point
StyleCommandEndPoint<StyleGenericPosition<StyleCSSFloat, StyleCSSFloat>,
                     StyleCSSFloat>::ToGfxPoint(const CSSSize* aBasis) const {
  if (IsToPosition()) {
    auto& pos = AsToPosition();
    return pos.ToGfxPoint();
  } else {
    auto& coord = AsByCoordinate();
    return coord.ToGfxPoint();
  }
}

template <>
inline gfx::Point
StyleCommandEndPoint<StylePosition, LengthPercentage>::ToGfxPoint(
    const CSSSize* aBasis) const {
  MOZ_ASSERT(aBasis);
  if (IsToPosition()) {
    auto& pos = AsToPosition();
    return pos.ToGfxPoint(aBasis);
  } else {
    auto& coord = AsByCoordinate();
    return coord.ToGfxPoint(aBasis);
  }
}

template <>
inline gfx::Coord StyleAxisEndPoint<StyleCSSFloat>::ToGfxCoord(
    const StyleCSSFloat* aBasis) const {
  if (IsToPosition()) {
    const auto pos = AsToPosition();
    MOZ_ASSERT(pos.IsLengthPercent());
    return gfx::Coord(pos.AsLengthPercent());
  }
  return gfx::Coord(AsByCoordinate());
}

template <>
inline gfx::Coord StyleAxisEndPoint<LengthPercentage>::ToGfxCoord(
    const StyleCSSFloat* aBasis) const {
  MOZ_ASSERT(aBasis);
  if (IsToPosition()) {
    const auto pos = AsToPosition();
    MOZ_ASSERT(pos.IsLengthPercent());
    return gfx::Coord(pos.AsLengthPercent().ResolveToCSSPixels(*aBasis));
  }
  return gfx::Coord(AsByCoordinate().ResolveToCSSPixels(*aBasis));
}

template <>
inline gfx::Point
StyleControlPoint<StyleGenericPosition<StyleCSSFloat, StyleCSSFloat>,
                  StyleCSSFloat>::ToGfxPoint(const gfx::Point aStatePos,
                                             const gfx::Point aEndPoint,
                                             const CSSSize* aBasis) const {
  if (IsAbsolute()) {
    auto& pos = AsAbsolute();
    return pos.ToGfxPoint();
  }

  // Else
  auto& point = AsRelative();
  auto cp = point.coord.ToGfxPoint();
  if (point.reference == StyleControlReference::Start) {
    return cp + aStatePos;
  } else if (point.reference == StyleControlReference::End) {
    return cp + aEndPoint;
  } else {
    return cp;
  }
}

template <>
inline gfx::Point
StyleControlPoint<StylePosition, LengthPercentage>::ToGfxPoint(
    const gfx::Point aStatePos, const gfx::Point aEndPoint,
    const CSSSize* aBasis) const {
  MOZ_ASSERT(aBasis);
  if (IsAbsolute()) {
    auto& pos = AsAbsolute();
    return pos.ToGfxPoint(aBasis);
  }

  // Else
  auto& point = AsRelative();
  auto cp = point.coord.ToGfxPoint(aBasis);
  if (point.reference == StyleControlReference::Start) {
    return cp + aStatePos;
  } else if (point.reference == StyleControlReference::End) {
    return cp + aEndPoint;
  } else {
    return cp;
  }
}

template <>
inline gfx::Point StyleArcRadii<StyleCSSFloat>::ToGfxPoint(
    const CSSSize* aBasis) const {
  return ry.IsSome() ? gfx::Point(rx, ry.AsSome()) : gfx::Point(rx, rx);
}

template <>
inline gfx::Point StyleArcRadii<LengthPercentage>::ToGfxPoint(
    const CSSSize* aBasis) const {
  MOZ_ASSERT(aBasis);
  if (ry.IsSome()) {
    return gfx::Point(rx.ResolveToCSSPixels(aBasis->Width()),
                      ry.AsSome().ResolveToCSSPixels(aBasis->Height()));
  }

  // Else percentages are resolved against the direction-agnostic size
  // of the reference box for both radiuses.
  // https://drafts.csswg.org/css-shapes-1/#typedef-shape-arc-command
  const auto directionAgnostic = SVGContentUtils::ComputeNormalizedHypotenuse(
      aBasis->Width(), aBasis->Height());
  const auto radius = rx.ResolveToCSSPixels(directionAgnostic);
  return gfx::Point(radius, radius);
}

inline StylePhysicalSide ToStylePhysicalSide(mozilla::Side aSide) {
  // TODO(dshin): Should look into merging these two types...
  static_assert(static_cast<uint8_t>(mozilla::Side::eSideLeft) ==
                    static_cast<uint8_t>(StylePhysicalSide::Left),
                "Left side doesn't match");
  static_assert(static_cast<uint8_t>(mozilla::Side::eSideRight) ==
                    static_cast<uint8_t>(StylePhysicalSide::Right),
                "Left side doesn't match");
  static_assert(static_cast<uint8_t>(mozilla::Side::eSideTop) ==
                    static_cast<uint8_t>(StylePhysicalSide::Top),
                "Left side doesn't match");
  static_assert(static_cast<uint8_t>(mozilla::Side::eSideBottom) ==
                    static_cast<uint8_t>(StylePhysicalSide::Bottom),
                "Left side doesn't match");
  return static_cast<StylePhysicalSide>(static_cast<uint8_t>(aSide));
}

inline StylePhysicalAxis ToStylePhysicalAxis(StylePhysicalSide aSide) {
  return aSide == StylePhysicalSide::Top || aSide == StylePhysicalSide::Bottom
             ? StylePhysicalAxis::Vertical
             : StylePhysicalAxis::Horizontal;
}

inline StylePhysicalAxis ToStylePhysicalAxis(mozilla::Side aSide) {
  return ToStylePhysicalAxis(ToStylePhysicalSide(aSide));
}

inline mozilla::Side ToSide(StylePhysicalSide aSide) {
  return static_cast<mozilla::Side>(static_cast<uint8_t>(aSide));
}

#define DEFINE_LENGTH_PERCENTAGE_CTOR(ty_)                               \
  template <>                                                            \
  inline Style##ty_::StyleGeneric##ty_(const StyleLengthPercentage& aLP) \
      : tag{Tag::LengthPercentage} {                                     \
    ::new (&length_percentage._0)(StyleLengthPercentage)(aLP);           \
  }

DEFINE_LENGTH_PERCENTAGE_CTOR(Inset)
DEFINE_LENGTH_PERCENTAGE_CTOR(Margin)
DEFINE_LENGTH_PERCENTAGE_CTOR(Size)
DEFINE_LENGTH_PERCENTAGE_CTOR(MaxSize)

inline bool StylePositionArea::IsNone() const {
  return first == StylePositionAreaKeyword::None;
}

template <>
inline bool StyleTreeScoped<StyleAnchorNameIdent>::IsEmpty() const {
  return value.IsEmpty();
}

template <>
inline Span<const mozilla::StyleAtom>
StyleTreeScoped<StyleAnchorNameIdent>::AsSpan() const {
  return value.AsSpan();
}

}  // namespace mozilla

#endif
