/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * This file contains forward declarations and typedefs for types that cbindgen
 * cannot understand but renames / prefixes, and includes for some of the types
 * it needs.
 */

#ifndef mozilla_ServoStyleConstsForwards_h
#define mozilla_ServoStyleConstsForwards_h

#ifndef mozilla_ServoStyleConsts_h
#  error "This file is only meant to be included from ServoStyleConsts.h"
#endif

#include <atomic>

#include "NonCustomCSSPropertyId.h"
#include "Units.h"
#include "mozilla/AtomArray.h"
#include "mozilla/CORSMode.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/ServoBindingTypes.h"
#include "mozilla/ServoTypes.h"
#include "mozilla/Vector.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/image/Resolution.h"
#include "nsColor.h"
#include "nsCompatibility.h"
#include "nsCoord.h"
#include "nsIURI.h"

struct RawServoAnimationValueTable;

class nsAtom;
class nsIFrame;
class nsINode;
class nsIContent;
class nsCSSPropertyIDSet;
class nsPresContext;
class nsStaticAtom;
class imgRequestProxy;
struct nsCSSValueSharedList;
struct AnchorPosResolutionParams;
struct AnchorPosOffsetResolutionParams;
class gfxFontFeatureValueSet;
struct GeckoFontMetrics;
namespace mozilla {

// Forward declaration for `StyleLengthPercentage::AsCalc`, which
// references the type below in the generated code.
struct StyleCalcLengthPercentage;

// Forward declaration for `StyleVariableReferenceValue`, which references the
// the type below in the generated code.
struct StyleUnparsedSegment;
using StyleUnparsedValue = CopyableTArray<StyleUnparsedSegment>;

// Forward declaration required due to a circular type dependency between
// StyleNumericValue and StyleMathSum.
// cbindgen does not currently emit this forward declaration automatically.
struct StyleNumericValue;

namespace gfx {
struct FontVariation;
struct FontFeature;
class FontPaletteValueSet;
}  // namespace gfx
}  // namespace mozilla
using gfxFontVariation = mozilla::gfx::FontVariation;
using gfxFontFeature = mozilla::gfx::FontFeature;

enum nsCSSUnit : uint32_t;
enum nsChangeHint : uint32_t;

namespace nsStyleTransformMatrix {
enum class MatrixTransformOperator : uint8_t;
}

template <typename T>
class nsMainThreadPtrHolder;

namespace mozilla {

class ComputedStyle;

using Matrix4x4Components = float[16];
using StyleMatrix4x4Components = Matrix4x4Components;

// This is sound because std::num::NonZeroUsize is repr(transparent).
//
// It is just the case that cbindgen doesn't understand it natively.
using StyleNonZeroUsize = uintptr_t;

struct Keyframe;
struct PropertyStyleAnimationValuePair;

using ComputedKeyframeValues = nsTArray<PropertyStyleAnimationValuePair>;

class ComputedStyle;
enum class LogicalAxis : uint8_t;
enum class PhysicalAxis : uint8_t;
class SeenPtrs;
class SharedFontList;
class StyleSheet;
class WritingMode;
class ServoElementSnapshotTable;
class StyleParserState;

template <typename T>
struct StyleForgottenArcSlicePtr;

struct AnimationPropertySegment;
struct AspectRatio;
struct ComputedTiming;
struct CSSPropertyId;
struct URLExtraData;

enum HalfCorner : uint8_t;
enum class LogicalSide : uint8_t;
enum class PseudoStyleType : uint8_t;
enum class OriginFlags : uint8_t;
enum class UseBoxSizing : uint8_t;
struct PseudoStyleRequest;

template <typename L>
union StyleGenericCalcNode;

namespace css {
class Loader;
class LoaderReusableStyleSheets;
class SheetLoadData;
using SheetLoadDataHolder = nsMainThreadPtrHolder<SheetLoadData>;
}  // namespace css

namespace dom {
enum class IterationCompositeOperation : uint8_t;
enum class CallerType : uint32_t;
class SimpleContentList;

class Element;
class Document;

}  // namespace dom

using StyleSimpleContentList = dom::SimpleContentList;

// Replacement for a Rust Box<T> for a non-dynamically-sized-type.
//
// TODO(emilio): If this was some sort of nullable box then this could be made
// to work with moves, and also reduce memory layout size of stuff, potentially.
template <typename T>
struct StyleBox {
  explicit StyleBox(UniquePtr<T> aPtr) : mRaw(aPtr.release()) {
    MOZ_DIAGNOSTIC_ASSERT(mRaw);
  }

  ~StyleBox() {
    MOZ_DIAGNOSTIC_ASSERT(mRaw);
    delete mRaw;
  }

  StyleBox(const StyleBox& aOther) : StyleBox(MakeUnique<T>(*aOther)) {}

  StyleBox& operator=(const StyleBox& aOther) const {
    delete mRaw;
    mRaw = MakeUnique<T>(*aOther).release();
    return *this;
  }

  const T* operator->() const {
    MOZ_DIAGNOSTIC_ASSERT(mRaw);
    return mRaw;
  }

  const T& operator*() const {
    MOZ_DIAGNOSTIC_ASSERT(mRaw);
    return *mRaw;
  }

  T* operator->() {
    MOZ_DIAGNOSTIC_ASSERT(mRaw);
    return mRaw;
  }

  T& operator*() {
    MOZ_DIAGNOSTIC_ASSERT(mRaw);
    return *mRaw;
  }

  bool operator==(const StyleBox& aOther) const { return *(*this) == *aOther; }

 private:
  T* mRaw;
};

// Work-around weird cbindgen renaming / avoiding moving stuff outside its
// namespace.

using StyleLoader = css::Loader;
using StyleLoaderReusableStyleSheets = css::LoaderReusableStyleSheets;
using StyleCallerType = dom::CallerType;
using StyleSheetLoadData = css::SheetLoadData;
using StyleSheetLoadDataHolder = css::SheetLoadDataHolder;
using StyleGeckoMallocSizeOf = MallocSizeOf;
using StyleDomStyleSheet = StyleSheet;

using StyleRawGeckoNode = nsINode;
using StyleRawGeckoElement = dom::Element;
using StyleRawShadowRoot = dom::ShadowRoot;
using StyleDocument = dom::Document;
using StyleComputedValues = ComputedStyle;
using StyleIterationCompositeOperation = dom::IterationCompositeOperation;

using StyleMatrixTransformOperator =
    nsStyleTransformMatrix::MatrixTransformOperator;

#define SERVO_LOCKED_ARC_TYPE(name_) struct StyleLocked##type_;
#include "mozilla/ServoLockedArcTypeList.inc"
#undef SERVO_LOCKED_ARC_TYPE

#define SERVO_BOXED_TYPE(name_, type_) struct Style##type_;
#include "mozilla/ServoBoxedTypeList.inc"
#undef SERVO_BOXED_TYPE

using StyleAtomicUsize = std::atomic<size_t>;

#define SERVO_FIXED_POINT_HELPERS(T, RawT, FractionBits)                      \
  static constexpr RawT kPointFive = 1 << (FractionBits - 1);                 \
  static constexpr uint16_t kScale = 1 << FractionBits;                       \
  static constexpr float kInverseScale = 1.0f / kScale;                       \
  static T FromRaw(RawT aRaw) { return {{aRaw}}; }                            \
  static T FromFloat(float aFloat) { return FromRaw(RawT(aFloat * kScale)); } \
  static T FromInt(RawT aInt) { return FromRaw(RawT(aInt * kScale)); }        \
  RawT Raw() const { return _0.value; }                                       \
  uint16_t UnsignedRaw() const { return uint16_t(Raw()); }                    \
  float ToFloat() const { return Raw() * kInverseScale; }                     \
  RawT ToIntRounded() const { return (Raw() + kPointFive) >> FractionBits; }  \
  inline void ToString(nsACString&) const;

}  // namespace mozilla

#ifndef HAVE_64BIT_BUILD
static_assert(sizeof(void*) == 4, "");
#  define SERVO_32_BITS 1
#endif
#define CBINDGEN_IS_GECKO

#endif
