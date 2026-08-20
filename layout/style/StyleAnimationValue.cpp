/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Utilities for animation of computed style values */

#include "mozilla/StyleAnimationValue.h"

#include "PseudoStyleType.h"
#include "gfx2DGlue.h"
#include "gfxMatrix.h"
#include "gfxQuaternion.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/ComputedStyleInlines.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/ServoBindings.h"  // StyleLockedDeclarationBlock
#include "mozilla/ServoCSSParser.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/layers/LayersMessages.h"
#include "nsCOMArray.h"
#include "nsComputedDOMStyle.h"
#include "nsIFrame.h"
#include "nsString.h"
#include "nsStyleTransformMatrix.h"

using namespace mozilla;
using namespace mozilla::css;
using namespace mozilla::dom;
using namespace mozilla::gfx;

bool AnimationValue::operator==(const AnimationValue& aOther) const {
  if (mServo && aOther.mServo) {
    return Servo_AnimationValue_DeepEqual(mServo, aOther.mServo);
  }
  if (!mServo && !aOther.mServo) {
    return true;
  }
  return false;
}

float AnimationValue::GetOpacity() const {
  MOZ_ASSERT(mServo);
  return Servo_AnimationValue_GetOpacity(mServo);
}

nscolor AnimationValue::GetColor(nscolor aForegroundColor) const {
  MOZ_ASSERT(mServo);
  return Servo_AnimationValue_GetColor(mServo, aForegroundColor);
}

bool AnimationValue::IsCurrentColor() const {
  MOZ_ASSERT(mServo);
  return Servo_AnimationValue_IsCurrentColor(mServo);
}

const StyleTranslate& AnimationValue::GetTranslateProperty() const {
  MOZ_ASSERT(mServo);
  return *Servo_AnimationValue_GetTranslate(mServo);
}

const StyleRotate& AnimationValue::GetRotateProperty() const {
  MOZ_ASSERT(mServo);
  return *Servo_AnimationValue_GetRotate(mServo);
}

const StyleScale& AnimationValue::GetScaleProperty() const {
  MOZ_ASSERT(mServo);
  return *Servo_AnimationValue_GetScale(mServo);
}

const StyleTransform& AnimationValue::GetTransformProperty() const {
  MOZ_ASSERT(mServo);
  return *Servo_AnimationValue_GetTransform(mServo);
}

void AnimationValue::GetOffsetPathProperty(StyleOffsetPath& aOffsetPath) const {
  MOZ_ASSERT(mServo);
  Servo_AnimationValue_GetOffsetPath(mServo, &aOffsetPath);
}

const mozilla::LengthPercentage& AnimationValue::GetOffsetDistanceProperty()
    const {
  MOZ_ASSERT(mServo);
  return *Servo_AnimationValue_GetOffsetDistance(mServo);
}

const mozilla::StyleOffsetRotate& AnimationValue::GetOffsetRotateProperty()
    const {
  MOZ_ASSERT(mServo);
  return *Servo_AnimationValue_GetOffsetRotate(mServo);
}

const mozilla::StylePositionOrAuto& AnimationValue::GetOffsetAnchorProperty()
    const {
  MOZ_ASSERT(mServo);
  return *Servo_AnimationValue_GetOffsetAnchor(mServo);
}

const mozilla::StyleOffsetPosition& AnimationValue::GetOffsetPositionProperty()
    const {
  MOZ_ASSERT(mServo);
  return *Servo_AnimationValue_GetOffsetPosition(mServo);
}

bool AnimationValue::IsOffsetPathUrl() const {
  return mServo && Servo_AnimationValue_IsOffsetPathUrl(mServo);
}

MatrixScales AnimationValue::GetScaleValue(const nsIFrame* aFrame) const {
  using namespace nsStyleTransformMatrix;

  CSSPropertyId property(eCSSProperty_UNKNOWN);
  Servo_AnimationValue_GetPropertyId(mServo, &property);
  switch (property.mId) {
    case eCSSProperty_scale: {
      const StyleScale& scale = GetScaleProperty();
      return scale.IsNone()
                 ? MatrixScales()
                 : MatrixScales(scale.AsScale()._0, scale.AsScale()._1);
    }
    case eCSSProperty_rotate:
    case eCSSProperty_translate:
      return MatrixScales();
    case eCSSProperty_transform:
      break;
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Should only need to check in transform properties");
      return MatrixScales();
  }

  TransformReferenceBox refBox(aFrame);
  Matrix4x4 t =
      ReadTransforms(StyleTranslate::None(), StyleRotate::None(),
                     StyleScale::None(), nullptr, GetTransformProperty(),
                     refBox, aFrame->PresContext()->AppUnitsPerDevPixel(),
                     aFrame->Style()->EffectiveZoom());
  Matrix transform2d;
  bool canDraw2D = t.CanDraw2D(&transform2d);
  if (!canDraw2D) {
    return MatrixScales(0, 0);
  }
  return transform2d.ScaleFactors();
}

void AnimationValue::SerializeSpecifiedValue(
    const CSSPropertyId& aProperty, const StylePerDocumentStyleData* aRawData,
    nsACString& aString) const {
  MOZ_ASSERT(mServo);
  Servo_AnimationValue_Serialize(mServo, &aProperty, aRawData, &aString);
}

bool AnimationValue::IsInterpolableWith(const CSSPropertyId& aProperty,
                                        const AnimationValue& aToValue) const {
  if (IsNull() || aToValue.IsNull()) {
    return false;
  }

  MOZ_ASSERT(mServo);
  MOZ_ASSERT(aToValue.mServo);
  return Servo_AnimationValues_IsInterpolable(mServo, aToValue.mServo);
}

double AnimationValue::ComputeDistance(const AnimationValue& aOther) const {
  if (IsNull() || aOther.IsNull()) {
    return 0.0;
  }

  MOZ_ASSERT(mServo);
  MOZ_ASSERT(aOther.mServo);

  double distance =
      Servo_AnimationValues_ComputeDistance(mServo, aOther.mServo);
  return distance < 0.0 ? 0.0 : distance;
}

/* static */
AnimationValue AnimationValue::FromString(CSSPropertyId& aProperty,
                                          const nsACString& aValue,
                                          Element* aElement) {
  MOZ_ASSERT(aElement);

  AnimationValue result;

  nsCOMPtr<Document> doc = aElement->GetComposedDoc();
  if (!doc) {
    return result;
  }

  RefPtr<PresShell> presShell = doc->GetPresShell();
  if (!presShell) {
    return result;
  }

  // GetComputedStyle() flushes style, so we shouldn't assume that any
  // non-owning references we have are still valid.
  RefPtr<const ComputedStyle> computedStyle =
      nsComputedDOMStyle::GetComputedStyle(aElement);
  MOZ_ASSERT(computedStyle);

  RefPtr<StyleLockedDeclarationBlock> declarations =
      ServoCSSParser::ParseProperty(aProperty, aValue,
                                    ServoCSSParser::GetParsingEnvironment(doc),
                                    StyleParsingMode::DEFAULT);

  if (!declarations) {
    return result;
  }

  result.mServo = presShell->StyleSet()->ComputeAnimationValue(
      aElement, declarations, computedStyle);
  return result;
}

std::ostream& operator<<(std::ostream& aOut, const AnimationValue& aValue) {
  MOZ_ASSERT(aValue.mServo);
  nsAutoCString s;
  Servo_AnimationValue_Dump(aValue.mServo, &s);
  return aOut << s;
}

/* static */
already_AddRefed<StyleAnimationValue> AnimationValue::FromAnimatable(
    NonCustomCSSPropertyId aProperty, const layers::Animatable& aAnimatable) {
  switch (aAnimatable.type()) {
    case layers::Animatable::Tnull_t:
      break;
    case layers::Animatable::TStyleTransform: {
      const StyleTransform& transform = aAnimatable.get_StyleTransform();
      MOZ_ASSERT(!transform.HasPercent(),
                 "Received transform operations should have been resolved.");
      return Servo_AnimationValue_Transform(&transform).Consume();
    }
    case layers::Animatable::Tfloat:
      return Servo_AnimationValue_Opacity(aAnimatable.get_float()).Consume();
    case layers::Animatable::Tnscolor:
      return Servo_AnimationValue_Color(aProperty, aAnimatable.get_nscolor())
          .Consume();
    case layers::Animatable::TStyleRotate:
      return Servo_AnimationValue_Rotate(&aAnimatable.get_StyleRotate())
          .Consume();
    case layers::Animatable::TStyleScale:
      return Servo_AnimationValue_Scale(&aAnimatable.get_StyleScale())
          .Consume();
    case layers::Animatable::TStyleTranslate:
      MOZ_ASSERT(
          aAnimatable.get_StyleTranslate().IsNone() ||
              (!aAnimatable.get_StyleTranslate()
                    .AsTranslate()
                    ._0.HasPercent() &&
               !aAnimatable.get_StyleTranslate().AsTranslate()._1.HasPercent()),
          "Should have been resolved already");
      return Servo_AnimationValue_Translate(&aAnimatable.get_StyleTranslate())
          .Consume();
    case layers::Animatable::TStyleOffsetPath:
      return Servo_AnimationValue_OffsetPath(&aAnimatable.get_StyleOffsetPath())
          .Consume();
    case layers::Animatable::TLengthPercentage:
      return Servo_AnimationValue_OffsetDistance(
                 &aAnimatable.get_LengthPercentage())
          .Consume();
    case layers::Animatable::TStyleOffsetRotate:
      return Servo_AnimationValue_OffsetRotate(
                 &aAnimatable.get_StyleOffsetRotate())
          .Consume();
    case layers::Animatable::TStylePositionOrAuto:
      return Servo_AnimationValue_OffsetAnchor(
                 &aAnimatable.get_StylePositionOrAuto())
          .Consume();
    case layers::Animatable::TStyleOffsetPosition:
      return Servo_AnimationValue_OffsetPosition(
                 &aAnimatable.get_StyleOffsetPosition())
          .Consume();
    default:
      MOZ_ASSERT_UNREACHABLE("Unsupported type");
  }
  return nullptr;
}
