/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMathMLmoFrame.h"

#include <algorithm>

#include "gfxContext.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_mathml.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MathMLElement.h"
#include "nsCSSValue.h"
#include "nsContentUtils.h"
#include "nsFrameSelection.h"
#include "nsGkAtoms.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"

using namespace mozilla;

//
// <mo> -- operator, fence, or separator - implementation
//

nsIFrame* NS_NewMathMLmoFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsMathMLmoFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsMathMLmoFrame)

nsMathMLmoFrame::~nsMathMLmoFrame() = default;

static const char16_t kApplyFunction = char16_t(0x2061);
static const char16_t kInvisibleTimes = char16_t(0x2062);
static const char16_t kInvisibleSeparator = char16_t(0x2063);
static const char16_t kInvisiblePlus = char16_t(0x2064);

MathMLFrameType nsMathMLmoFrame::GetMathMLFrameType() {
  return mFlags.Booleans().contains(OperatorBoolean::Invisible)
             ? MathMLFrameType::OperatorInvisible
             : MathMLFrameType::OperatorOrdinary;
}

// since a mouse click implies selection, we cannot just rely on the
// frame's state bit in our child text frame. So we will first check
// its selected state bit, and use this little helper to double check.
bool nsMathMLmoFrame::IsFrameInSelection(nsIFrame* aFrame) {
  NS_ASSERTION(aFrame, "null arg");
  if (!aFrame || !aFrame->IsSelected()) {
    return false;
  }

  const nsFrameSelection* frameSelection = aFrame->GetConstFrameSelection();
  UniquePtr<SelectionDetails> details = frameSelection->LookUpSelection(
      aFrame->GetContent(), 0, 1,
      aFrame->ShouldPaintNormalSelection()
          ? nsFrameSelection::IgnoreNormalSelection::No
          : nsFrameSelection::IgnoreNormalSelection::Yes);

  return details != nullptr;
}

bool nsMathMLmoFrame::UseMathMLChar() {
  return (mFlags.Form() != OperatorForm::Unknown &&
          mFlags.Booleans().contains(OperatorBoolean::Mutable)) ||
         mFlags.Booleans().contains(OperatorBoolean::ForcesMathMLChar);
}

void nsMathMLmoFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                       const nsDisplayListSet& aLists) {
  bool useMathMLChar = UseMathMLChar();

  if (!useMathMLChar) {
    // let the base class do everything
    nsMathMLTokenFrame::BuildDisplayList(aBuilder, aLists);
  } else {
    DisplayBorderBackgroundOutline(aBuilder, aLists);

    // make our char selected if our inner child text frame is selected
    bool isSelected = false;
    nsRect selectedRect;
    nsIFrame* firstChild = mFrames.FirstChild();
    if (IsFrameInSelection(firstChild)) {
      mMathMLChar.GetRect(selectedRect);
      // add a one pixel border (it renders better for operators like minus)
      selectedRect.Inflate(nsPresContext::CSSPixelsToAppUnits(1));
      isSelected = true;
    }
    mMathMLChar.Display(aBuilder, this, aLists, 0,
                        isSelected ? &selectedRect : nullptr);
  }
}

// get the text that we enclose and setup our nsMathMLChar
void nsMathMLmoFrame::ProcessTextData() {
  mFlags.Clear();

  nsAutoString data;
  nsContentUtils::GetNodeTextContent(mContent, false, data);

  data.CompressWhitespace();
  int32_t length = data.Length();
  char16_t ch = (length == 0) ? char16_t('\0') : data[0];

  if ((length == 1) && (ch == kApplyFunction || ch == kInvisibleSeparator ||
                        ch == kInvisiblePlus || ch == kInvisibleTimes)) {
    mFlags.Booleans() += OperatorBoolean::Invisible;
  }

  // don't bother doing anything special if we don't have a single child
  if (mFrames.GetLength() != 1) {
    data.Truncate();  // empty data to reset the char
    mMathMLChar.SetData(data);
    mMathMLChar.SetComputedStyle(Style());
    return;
  }

  // special... in math mode, the usual minus sign '-' looks too short, so
  // what we do here is to remap <mo>-</mo> to the official Unicode minus
  // sign (U+2212) which looks much better. For background on this, see
  // http://groups.google.com/groups?hl=en&th=66488daf1ade7635&rnum=1
  if (1 == length && ch == '-') {
    ch = 0x2212;
    data = ch;
    mFlags.Booleans() += OperatorBoolean::ForcesMathMLChar;
  }

  // cache the special bits: mutable, accent, movablelimits.
  // we need to do this in anticipation of other requirements, and these
  // bits don't change. Do not reset these bits unless the text gets changed.

  // lookup all the forms under which the operator is listed in the dictionary,
  // and record whether the operator has accent="true" or movablelimits="true"
  OperatorBooleans allFlags;
  for (const auto& form :
       {OperatorForm::Infix, OperatorForm::Postfix, OperatorForm::Prefix}) {
    nsOperatorFlags flags;
    float dummy;
    if (nsMathMLOperators::LookupOperator(data, form, &flags, &dummy, &dummy)) {
      allFlags += flags.Booleans();
    }
  }

  mFlags.Booleans() +=
      allFlags &
      OperatorBooleans({OperatorBoolean::Accent, OperatorBoolean::MovableLimits,
                        OperatorBoolean::LargeOperator});

  // cache the operator
  mMathMLChar.SetData(data);

  // cache the native direction -- beware of bug 133429...
  // mEmbellishData.direction must always retain our native direction, whereas
  // mMathMLChar.GetStretchDirection() may change later, when Stretch() is
  // called
  mEmbellishData.direction = mMathMLChar.GetStretchDirection();

  bool isMutable = allFlags.contains(OperatorBoolean::LargeOperator) ||
                   (mEmbellishData.direction != StretchDirection::Unsupported);
  if (isMutable) {
    mFlags.Booleans() += OperatorBoolean::Mutable;
  }

  mMathMLChar.SetComputedStyle(Style());
}

// get our 'form' and lookup in the Operator Dictionary to fetch
// our default data that may come from there. Then complete our setup
// using attributes that we may have. To stay in sync, this function is
// called very often. We depend on many things that may change around us.
// However, we re-use unchanged values.
void nsMathMLmoFrame::ProcessOperatorData() {
  // if we have been here before, we will just use our cached form
  auto form = mFlags.Form();
  nsAutoString value;
  float fontSizeInflation = nsLayoutUtils::FontSizeInflationFor(this);

  // special bits are always kept in mFlags.
  // remember the mutable bit from ProcessTextData().
  // Some chars are listed under different forms in the dictionary,
  // and there could be a form under which the char is mutable.
  // If the char is the core of an embellished container, we will keep
  // it mutable irrespective of the form of the embellished container.
  // Also remember the other special bits that we want to carry forward.
  mFlags.SetForm(OperatorForm::Unknown);
  mFlags.SetDirection(OperatorDirection::Unknown);
  mFlags.Booleans() &=
      {OperatorBoolean::Mutable,          OperatorBoolean::Accent,
       OperatorBoolean::MovableLimits,    OperatorBoolean::Invisible,
       OperatorBoolean::ForcesMathMLChar, OperatorBoolean::LargeOperator};

  if (!mEmbellishData.coreFrame) {
    // i.e., we haven't been here before, the default form is infix
    form = OperatorForm::Infix;

    // reset everything so that we don't keep outdated values around
    // in case of dynamic changes
    mEmbellishData.flags.clear();
    mEmbellishData.coreFrame = nullptr;
    mEmbellishData.leadingSpace = 0;
    mEmbellishData.trailingSpace = 0;
    if (mMathMLChar.Length() != 1) {
      mEmbellishData.direction = StretchDirection::Unsupported;
    }
    // else... retain the native direction obtained in ProcessTextData()

    if (!mFrames.FirstChild()) {
      return;
    }

    mEmbellishData.flags += MathMLEmbellishFlag::EmbellishedOperator;
    mEmbellishData.coreFrame = this;

    // there are two particular things that we also need to record so that if
    // our parent is <mover>, <munder>, or <munderover>, they will treat us
    // properly: 1) do we have accent="true" 2) do we have movablelimits="true"

    // they need the extra information to decide how to treat their
    // scripts/limits (note: <mover>, <munder>, or <munderover> need not
    // necessarily be our direct parent -- case of embellished operators)

    // default values from the Operator Dictionary were obtained in
    // ProcessTextData() and these special bits are always kept in mFlags
    if (mFlags.Booleans().contains(OperatorBoolean::Accent)) {
      mEmbellishData.flags += MathMLEmbellishFlag::Accent;
    }
    if (mFlags.Booleans().contains(OperatorBoolean::MovableLimits)) {
      mEmbellishData.flags += MathMLEmbellishFlag::MovableLimits;
    }
    if (mFlags.Booleans().contains(OperatorBoolean::LargeOperator)) {
      mEmbellishData.flags += MathMLEmbellishFlag::LargeOp;
    }

    // see if the accent attribute is there
    if (mContent->AsElement()->GetAttr(nsGkAtoms::accent, value)) {
      [&]() {
        AutoTArray<nsString, 2> params;
        auto parentName = GetParent()->GetContent()->NodeInfo()->NameAtom();
        if (parentName == nsGkAtoms::mover) {
          params.AppendElement(u"accent");
          params.AppendElement(u"mover");
        } else if (parentName == nsGkAtoms::munder) {
          params.AppendElement(u"accentunder");
          params.AppendElement(u"munder");
        } else if (parentName == nsGkAtoms::munderover) {
          params.AppendElement(u"accent/accentunder");
          params.AppendElement(u"munderover");
        } else {
          return;
        }
        PresContext()->Document()->WarnOnceAbout(
            dom::DeprecatedOperations::eMathML_DeprecatedMoExplicitAccent,
            false, params);
      }();
      if (value.LowerCaseEqualsLiteral("true")) {
        mEmbellishData.flags += MathMLEmbellishFlag::Accent;
      } else if (value.LowerCaseEqualsLiteral("false")) {
        mEmbellishData.flags -= MathMLEmbellishFlag::Accent;
      }
    }

    // see if the movablelimits attribute is there
    mContent->AsElement()->GetAttr(nsGkAtoms::movablelimits, value);
    if (value.LowerCaseEqualsLiteral("true")) {
      mEmbellishData.flags += MathMLEmbellishFlag::MovableLimits;
    } else if (value.LowerCaseEqualsLiteral("false")) {
      mEmbellishData.flags -= MathMLEmbellishFlag::MovableLimits;
    }

    // ---------------------------------------------------------------------
    // we will be called again to re-sync the rest of our state next time...
    // (nobody needs the other values below at this stage)
    mFlags.SetForm(form);
    return;
  }

  // beware of bug 133814 - there is a two-way dependency in the
  // embellished hierarchy: our embellished ancestors need to set
  // their flags based on some of our state (set above), and here we
  // need to re-sync our 'form' depending on our outermost embellished
  // container. A null form here means that an earlier attempt to stretch
  // our mMathMLChar failed, in which case we don't bother re-stretching again
  if (form != OperatorForm::Unknown) {
    // get our outermost embellished container and its parent.
    // (we ensure that we are the core, not just a sibling of the core)
    nsIFrame* embellishAncestor = this;
    nsEmbellishData embellishData;
    nsIFrame* parentAncestor = this;
    do {
      embellishAncestor = parentAncestor;
      parentAncestor = embellishAncestor->GetParent();
      GetEmbellishDataFrom(parentAncestor, embellishData);
    } while (embellishData.coreFrame == this);

    // flag if we have an embellished ancestor
    if (embellishAncestor != this) {
      mFlags.Booleans() += OperatorBoolean::HasEmbellishAncestor;
    } else {
      mFlags.Booleans() -= OperatorBoolean::HasEmbellishAncestor;
    }

    // find the position of our outermost embellished container w.r.t
    // its siblings.

    nsIFrame* nextSibling = embellishAncestor->GetNextSibling();
    nsIFrame* prevSibling = embellishAncestor->GetPrevSibling();

    // flag to distinguish from a real infix.  Set for (embellished) operators
    // that live in (inferred) mrows.
    nsIMathMLFrame* mathAncestor = do_QueryFrame(parentAncestor);
    bool zeroSpacing = false;
    if (mathAncestor) {
      zeroSpacing = !mathAncestor->IsMrowLike();
    } else {
      nsMathMLmathBlockFrame* blockFrame = do_QueryFrame(parentAncestor);
      if (blockFrame) {
        zeroSpacing = !blockFrame->IsMrowLike();
      }
    }
    if (zeroSpacing) {
      mFlags.Booleans() += OperatorBoolean::EmbellishIsIsolated;
    } else {
      mFlags.Booleans() -= OperatorBoolean::EmbellishIsIsolated;
    }

    // find our form
    form = OperatorForm::Infix;
    mContent->AsElement()->GetAttr(nsGkAtoms::form, value);
    if (!value.IsEmpty()) {
      if (value.EqualsLiteral("prefix")) {
        form = OperatorForm::Prefix;
      } else if (value.EqualsLiteral("postfix")) {
        form = OperatorForm::Postfix;
      }
    } else {
      // set our form flag depending on the position
      if (!prevSibling && nextSibling) {
        form = OperatorForm::Prefix;
      } else if (prevSibling && !nextSibling) {
        form = OperatorForm::Postfix;
      }
    }
    mFlags.SetForm(form);

    // Use the default value suggested by the MathML REC.
    // http://www.w3.org/TR/MathML/chapter3.html#presm.mo.attrs
    // thickmathspace = 5/18em
    float lspace = 5.0f / 18.0f;
    float rspace = 5.0f / 18.0f;
    // lookup the operator dictionary
    nsAutoString data;
    mMathMLChar.GetData(data);
    nsOperatorFlags flags;
    if (nsMathMLOperators::LookupOperatorWithFallback(data, form, &flags,
                                                      &lspace, &rspace)) {
      mFlags.SetForm(flags.Form());
      mFlags.SetDirection(flags.Direction());
      mFlags.Booleans() +=
          flags.Booleans();  // just add bits without overwriting
    }

    // Spacing is zero if our outermost embellished operator is not in an
    // inferred mrow.
    if (!mFlags.Booleans().contains(OperatorBoolean::EmbellishIsIsolated) &&
        (lspace || rspace)) {
      // Cache the default values of lspace and rspace.
      // since these values are relative to the 'em' unit, convert to twips now
      nscoord em;
      RefPtr<nsFontMetrics> fm =
          nsLayoutUtils::GetFontMetricsForFrame(this, fontSizeInflation);
      GetEmHeight(fm, em);

      mEmbellishData.leadingSpace = NSToCoordRound(lspace * em);
      mEmbellishData.trailingSpace = NSToCoordRound(rspace * em);

      // tuning if we don't want too much extra space when we are a script.
      // (with its fonts, TeX sets lspace=0 & rspace=0 as soon as scriptlevel>0.
      // Our fonts can be anything, so...)
      if (!StaticPrefs::
              mathml_adjust_default_lspace_rspace_for_positive_scriptlevel_disabled() &&
          StyleFont()->mMathDepth > 0 &&
          !mFlags.Booleans().contains(OperatorBoolean::HasEmbellishAncestor)) {
        mEmbellishData.leadingSpace /= 2;
        mEmbellishData.trailingSpace /= 2;
      }
    }
  }

  // If we are an accent without explicit lspace="." or rspace=".",
  // we will ignore our default leading/trailing space

  // lspace
  //
  // "Specifies the leading space appearing before the operator"
  //
  // values: length
  // default: set by dictionary (thickmathspace)
  //
  // XXXfredw Support for negative and relative values is not implemented
  // (bug 805926).
  // Relative values will give a multiple of the current leading space,
  // which is not necessarily the default one.
  //
  nscoord leadingSpace = mEmbellishData.leadingSpace;
  mContent->AsElement()->GetAttr(nsGkAtoms::lspace, value);
  if (!value.IsEmpty()) {
    nsCSSValue cssValue;
    if (dom::MathMLElement::ParseNumericValue(value, cssValue,
                                              mContent->OwnerDoc())) {
      if ((eCSSUnit_Number == cssValue.GetUnit()) &&
          !cssValue.GetFloatValue()) {
        leadingSpace = 0;
      } else if (cssValue.IsLengthUnit()) {
        leadingSpace = CalcLength(cssValue, fontSizeInflation, this);
      }
      mFlags.Booleans() += OperatorBoolean::HasLSpaceAttribute;
    }
  }

  // rspace
  //
  // "Specifies the trailing space appearing after the operator"
  //
  // values: length
  // default: set by dictionary (thickmathspace)
  //
  // XXXfredw Support for negative and relative values is not implemented
  // (bug 805926).
  // Relative values will give a multiple of the current leading space,
  // which is not necessarily the default one.
  //
  nscoord trailingSpace = mEmbellishData.trailingSpace;
  mContent->AsElement()->GetAttr(nsGkAtoms::rspace, value);
  if (!value.IsEmpty()) {
    nsCSSValue cssValue;
    if (dom::MathMLElement::ParseNumericValue(value, cssValue,
                                              mContent->OwnerDoc())) {
      if ((eCSSUnit_Number == cssValue.GetUnit()) &&
          !cssValue.GetFloatValue()) {
        trailingSpace = 0;
      } else if (cssValue.IsLengthUnit()) {
        trailingSpace = CalcLength(cssValue, fontSizeInflation, this);
      }
      mFlags.Booleans() += OperatorBoolean::HasRSpaceAttribute;
    }
  }

  // little extra tuning to round lspace & rspace to at least a pixel so that
  // operators don't look as if they are colliding with their operands
  if (leadingSpace || trailingSpace) {
    nscoord onePixel = nsPresContext::CSSPixelsToAppUnits(1);
    if (leadingSpace && leadingSpace < onePixel) {
      leadingSpace = onePixel;
    }
    if (trailingSpace && trailingSpace < onePixel) {
      trailingSpace = onePixel;
    }
  }

  // the values that we get from our attributes override the dictionary
  mEmbellishData.leadingSpace = leadingSpace;
  mEmbellishData.trailingSpace = trailingSpace;

  // Now see if there are user-defined attributes that override the dictionary.
  // XXX Bug 1197771 - forcing an attribute to true when it is false in the
  // dictionary can cause conflicts in the rest of the stretching algorithms
  // (e.g. all largeops are assumed to have a vertical direction)

  // For each attribute overriden by the user, turn off its bit flag.
  // symmetric|movablelimits|separator|largeop|accent|fence|stretchy|form
  // special: accent and movablelimits are handled above,
  // don't process them here

  mContent->AsElement()->GetAttr(nsGkAtoms::stretchy, value);
  if (value.LowerCaseEqualsLiteral("false")) {
    mFlags.Booleans() -= OperatorBoolean::Stretchy;
  } else if (value.LowerCaseEqualsLiteral("true")) {
    mFlags.Booleans() += OperatorBoolean::Stretchy;
  }
  if (mFlags.Booleans().contains(OperatorBoolean::Fence)) {
    mContent->AsElement()->GetAttr(nsGkAtoms::fence, value);
    if (value.LowerCaseEqualsLiteral("false")) {
      mFlags.Booleans() -= OperatorBoolean::Fence;
    } else {
      mEmbellishData.flags += MathMLEmbellishFlag::Fence;
    }
  }
  mContent->AsElement()->GetAttr(nsGkAtoms::largeop, value);
  if (value.LowerCaseEqualsLiteral("false")) {
    mFlags.Booleans() -= OperatorBoolean::LargeOperator;
    mEmbellishData.flags -= MathMLEmbellishFlag::LargeOp;
  } else if (value.LowerCaseEqualsLiteral("true")) {
    mFlags.Booleans() += OperatorBoolean::LargeOperator;
    mEmbellishData.flags += MathMLEmbellishFlag::LargeOp;
  }
  if (mFlags.Booleans().contains(OperatorBoolean::Separator)) {
    mContent->AsElement()->GetAttr(nsGkAtoms::separator, value);
    if (value.LowerCaseEqualsLiteral("false")) {
      mFlags.Booleans() -= OperatorBoolean::Separator;
    } else {
      mEmbellishData.flags += MathMLEmbellishFlag::Separator;
    }
  }
  mContent->AsElement()->GetAttr(nsGkAtoms::symmetric, value);
  if (value.LowerCaseEqualsLiteral("false")) {
    mFlags.Booleans() -= OperatorBoolean::Symmetric;
  } else if (value.LowerCaseEqualsLiteral("true")) {
    mFlags.Booleans() += OperatorBoolean::Symmetric;
  }

  // minsize
  //
  // "Specifies the minimum size of the operator when stretchy"
  //
  // values: length
  // default: set by dictionary (1em)
  //
  // We don't allow negative values.
  // Note: Contrary to other "length" values, unitless and percentage do not
  // give a multiple of the defaut value but a multiple of the operator at
  // normal size.
  //
  mMinSize = 0;
  mContent->AsElement()->GetAttr(nsGkAtoms::minsize, value);
  if (!value.IsEmpty()) {
    nsCSSValue cssValue;
    if (dom::MathMLElement::ParseNumericValue(value, cssValue,
                                              mContent->OwnerDoc())) {
      nsCSSUnit unit = cssValue.GetUnit();
      if (eCSSUnit_Number == unit) {
        mMinSize = cssValue.GetFloatValue();
      } else if (eCSSUnit_Percent == unit) {
        mMinSize = cssValue.GetPercentValue();
      } else if (eCSSUnit_Null != unit) {
        mMinSize = float(CalcLength(cssValue, fontSizeInflation, this));
        mFlags.Booleans() += OperatorBoolean::MinSizeIsAbsolute;
      }
    }
  }

  // maxsize
  //
  // "Specifies the maximum size of the operator when stretchy"
  //
  // values: length | "infinity"
  // default: set by dictionary (infinity)
  //
  // We don't allow negative values.
  // Note: Contrary to other "length" values, unitless and percentage do not
  // give a multiple of the defaut value but a multiple of the operator at
  // normal size.
  //
  mMaxSize = kMathMLOperatorSizeInfinity;
  mContent->AsElement()->GetAttr(nsGkAtoms::maxsize, value);
  if (!value.IsEmpty()) {
    nsCSSValue cssValue;
    if (dom::MathMLElement::ParseNumericValue(value, cssValue,
                                              mContent->OwnerDoc())) {
      nsCSSUnit unit = cssValue.GetUnit();
      if (eCSSUnit_Number == unit) {
        mMaxSize = cssValue.GetFloatValue();
      } else if (eCSSUnit_Percent == unit) {
        mMaxSize = cssValue.GetPercentValue();
      } else if (eCSSUnit_Null != unit) {
        mMaxSize = float(CalcLength(cssValue, fontSizeInflation, this));
        mFlags.Booleans() += OperatorBoolean::MaxSizeIsAbsolute;
      }
    }
  }
}

static MathMLStretchFlags GetStretchFlags(nsOperatorFlags aFlags,
                                          nsPresentationData aPresentationData,
                                          bool aIsVertical,
                                          const nsStyleFont* aStyleFont) {
  MathMLStretchFlags stretchFlags;
  // See if it is okay to stretch,
  // starting from what the Operator Dictionary said
  if (aFlags.Booleans().contains(OperatorBoolean::Mutable)) {
    // set the largeop or largeopOnly flags to suitably cover all the
    // 8 possible cases depending on whether displaystyle, largeop,
    // stretchy are true or false (see bug 69325).
    // . largeopOnly is taken if largeop=true and stretchy=false
    // . largeop is taken if largeop=true and stretchy=true
    if (aStyleFont->mMathStyle == StyleMathStyle::Normal &&
        aFlags.Booleans().contains(OperatorBoolean::LargeOperator)) {
      stretchFlags =
          MathMLStretchFlag::LargeOperator;  // (largeopOnly, not mask!)
      if (aFlags.Booleans().contains(OperatorBoolean::Stretchy)) {
        stretchFlags += MathMLStretchFlag::Nearer;
        stretchFlags += MathMLStretchFlag::Larger;
      }
    } else if (aFlags.Booleans().contains(OperatorBoolean::Stretchy)) {
      if (aIsVertical) {
        // TeX hint. Can impact some sloppy markups missing <mrow></mrow>
        stretchFlags = MathMLStretchFlag::Nearer;
      } else {
        stretchFlags = MathMLStretchFlag::Normal;
      }
    }
    // else if the stretchy and largeop attributes have been disabled,
    // the operator is not mutable
  }
  return stretchFlags;
}

// NOTE: aDesiredStretchSize is an IN/OUT parameter
//       On input  - it contains our current size
//       On output - the same size or the new size that we want
NS_IMETHODIMP
nsMathMLmoFrame::Stretch(DrawTarget* aDrawTarget,
                         StretchDirection aStretchDirection,
                         nsBoundingMetrics& aContainerSize,
                         ReflowOutput& aDesiredStretchSize) {
  if (mPresentationData.flags.contains(MathMLPresentationFlag::StretchDone)) {
    NS_WARNING("it is wrong to fire stretch more than once on a frame");
    return NS_OK;
  }
  mPresentationData.flags += MathMLPresentationFlag::StretchDone;

  nsIFrame* firstChild = mFrames.FirstChild();

  // get the axis height;
  float fontSizeInflation = nsLayoutUtils::FontSizeInflationFor(this);
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForFrame(this, fontSizeInflation);
  nscoord axisHeight, height;
  GetAxisHeight(aDrawTarget, fm, axisHeight);

  // Operators that are stretchy are handled by the MathMLChar
  // ('form' is reset if stretch fails -- i.e., we don't bother to stretch next
  // time)
  bool useMathMLChar = UseMathMLChar();

  nsBoundingMetrics charSize;
  nsBoundingMetrics container = aDesiredStretchSize.mBoundingMetrics;
  bool isVertical = false;

  if (((aStretchDirection == StretchDirection::Vertical) ||
       (aStretchDirection == StretchDirection::Default)) &&
      (mEmbellishData.direction == StretchDirection::Vertical)) {
    isVertical = true;
  }

  auto stretchFlags =
      GetStretchFlags(mFlags, mPresentationData, isVertical, StyleFont());

  if (useMathMLChar) {
    nsBoundingMetrics initialSize = aDesiredStretchSize.mBoundingMetrics;

    if (!stretchFlags.isEmpty()) {
      container = aContainerSize;

      // some adjustments if the operator is symmetric and vertical

      if (isVertical &&
          mFlags.Booleans().contains(OperatorBoolean::Symmetric)) {
        // we need to center about the axis
        nscoord delta = std::max(container.ascent - axisHeight,
                                 container.descent + axisHeight);
        container.ascent = delta + axisHeight;
        container.descent = delta - axisHeight;

        // get ready in case we encounter user-desired min-max size
        delta = std::max(initialSize.ascent - axisHeight,
                         initialSize.descent + axisHeight);
        initialSize.ascent = delta + axisHeight;
        initialSize.descent = delta - axisHeight;
      }

      // check for user-desired min-max size

      if (mMaxSize != kMathMLOperatorSizeInfinity && mMaxSize > 0.0f) {
        // if we are here, there is a user defined maxsize ...
        // XXX Set stretchFlags = MathMLStretchFlag::Normal? to honor the
        // maxsize as close as possible?
        if (mFlags.Booleans().contains(OperatorBoolean::MaxSizeIsAbsolute)) {
          // there is an explicit value like maxsize="20pt"
          // try to maintain the aspect ratio of the char
          float aspect =
              mMaxSize / float(initialSize.ascent + initialSize.descent);
          container.ascent =
              std::min(container.ascent, nscoord(initialSize.ascent * aspect));
          container.descent = std::min(container.descent,
                                       nscoord(initialSize.descent * aspect));
          // below we use a type cast instead of a conversion to avoid a VC++
          // bug see
          // http://support.microsoft.com/support/kb/articles/Q115/7/05.ASP
          container.width = std::min(container.width, (nscoord)mMaxSize);
        } else {  // multiplicative value
          container.ascent = std::min(container.ascent,
                                      nscoord(initialSize.ascent * mMaxSize));
          container.descent = std::min(container.descent,
                                       nscoord(initialSize.descent * mMaxSize));
          container.width =
              std::min(container.width, nscoord(initialSize.width * mMaxSize));
        }

        if (isVertical &&
            !mFlags.Booleans().contains(OperatorBoolean::Symmetric)) {
          // re-adjust to align the char with the bottom of the initial
          // container
          height = container.ascent + container.descent;
          container.descent = aContainerSize.descent;
          container.ascent = height - container.descent;
        }
      }

      if (mMinSize > 0.0f) {
        // if we are here, there is a user defined minsize ...
        // always allow the char to stretch in its natural direction,
        // even if it is different from the caller's direction
        if (aStretchDirection != StretchDirection::Default &&
            aStretchDirection != mEmbellishData.direction) {
          aStretchDirection = StretchDirection::Default;
          // but when we are not honoring the requested direction
          // we should not use the caller's container size either
          container = initialSize;
        }
        if (mFlags.Booleans().contains(OperatorBoolean::MinSizeIsAbsolute)) {
          // there is an explicit value like minsize="20pt"
          // try to maintain the aspect ratio of the char
          float aspect =
              mMinSize / float(initialSize.ascent + initialSize.descent);
          container.ascent =
              std::max(container.ascent, nscoord(initialSize.ascent * aspect));
          container.descent = std::max(container.descent,
                                       nscoord(initialSize.descent * aspect));
          container.width = std::max(container.width, (nscoord)mMinSize);
        } else {  // multiplicative value
          container.ascent = std::max(container.ascent,
                                      nscoord(initialSize.ascent * mMinSize));
          container.descent = std::max(container.descent,
                                       nscoord(initialSize.descent * mMinSize));
          container.width =
              std::max(container.width, nscoord(initialSize.width * mMinSize));
        }

        if (isVertical &&
            !mFlags.Booleans().contains(OperatorBoolean::Symmetric)) {
          // re-adjust to align the char with the bottom of the initial
          // container
          height = container.ascent + container.descent;
          container.descent = aContainerSize.descent;
          container.ascent = height - container.descent;
        }
      }
    }

    // let the MathMLChar stretch itself...
    nsresult res = mMathMLChar.Stretch(
        this, aDrawTarget, fontSizeInflation, aStretchDirection, container,
        charSize, stretchFlags, GetWritingMode().IsBidiRTL());
    if (NS_FAILED(res)) {
      // gracefully handle cases where stretching the char failed (i.e.,
      // GetBoundingMetrics failed) clear our 'form' to behave as if the
      // operator wasn't in the dictionary
      mFlags.SetForm(OperatorForm::Unknown);
      useMathMLChar = false;
    }
  }

  // Place our children using the default method and no border/padding.
  // This will allow our child text frame to get its DidReflow()
  PlaceFlags flags(PlaceFlag::IgnoreBorderPadding,
                   PlaceFlag::DoNotAdjustForWidthAndHeight);
  Place(aDrawTarget, flags, aDesiredStretchSize);

  if (useMathMLChar) {
    // update our bounding metrics... it becomes that of our MathML char
    mBoundingMetrics = charSize;

    // if the returned direction is 'unsupported', the char didn't actually
    // change. So we do the centering only if necessary
    if (mMathMLChar.GetStretchDirection() != StretchDirection::Unsupported) {
      bool largeopOnly =
          stretchFlags.contains(MathMLStretchFlag::LargeOperator) &&
          (stretchFlags & kMathMLStretchVariableSet).isEmpty();

      if (isVertical) {
        // the desired size returned by mMathMLChar maybe different
        // from the size of the container.
        // the mMathMLChar.mRect.y calculation is subtle, watch out!!!

        height = mBoundingMetrics.ascent + mBoundingMetrics.descent;
        if (mFlags.Booleans().contains(OperatorBoolean::Symmetric)) {
          // For symmetric and vertical operators, we want to center about the
          // axis of the container
          mBoundingMetrics.descent = height / 2 - axisHeight;
        } else if (!largeopOnly) {
          // Align the center of the char with the center of the container
          mBoundingMetrics.descent =
              height / 2 + (container.ascent + container.descent) / 2 -
              container.ascent;
        }  // else align the baselines
        mBoundingMetrics.ascent = height - mBoundingMetrics.descent;
      }
    }
  }

  // Fixup for the final height.
  // On one hand, our stretchy height can sometimes be shorter than surrounding
  // ASCII chars, e.g., arrow symbols have |mBoundingMetrics.ascent|
  // that is smaller than the ASCII's ascent, hence when painting the background
  // later, it won't look uniform along the line.
  // On the other hand, sometimes we may leave too much gap when our glyph
  // happens to come from a font with tall glyphs. For example, since CMEX10 has
  // very tall glyphs, its natural font metrics are large, even if we pick a
  // small glyph whose size is comparable to the size of a normal ASCII glyph.
  // So to avoid uneven spacing in either of these two cases, we use the height
  // of the ASCII font as a reference and try to match it if possible.

  // special case for accents... keep them short to improve mouse operations...
  // an accent can only be the non-first child of <mover>, <munder>,
  // <munderover>
  bool isAccent = mEmbellishData.flags.contains(MathMLEmbellishFlag::Accent);
  if (isAccent) {
    nsEmbellishData parentData;
    GetEmbellishDataFrom(GetParent(), parentData);
    isAccent = (parentData.flags.contains(MathMLEmbellishFlag::AccentOver) ||
                parentData.flags.contains(MathMLEmbellishFlag::AccentUnder)) &&
               parentData.coreFrame != this;
  }
  if (isAccent && firstChild) {
    // see bug 188467 for what is going on here
    nscoord dy =
        aDesiredStretchSize.BlockStartAscent() - (mBoundingMetrics.ascent);
    aDesiredStretchSize.SetBlockStartAscent(mBoundingMetrics.ascent);
    aDesiredStretchSize.Height() =
        aDesiredStretchSize.BlockStartAscent() + mBoundingMetrics.descent;

    firstChild->SetPosition(firstChild->GetPosition() - nsPoint(0, dy));
  } else if (useMathMLChar) {
    nscoord ascent = fm->MaxAscent();
    nscoord descent = fm->MaxDescent();
    aDesiredStretchSize.SetBlockStartAscent(
        std::max(mBoundingMetrics.ascent, ascent));
    aDesiredStretchSize.Height() = aDesiredStretchSize.BlockStartAscent() +
                                   std::max(mBoundingMetrics.descent, descent);
  }
  aDesiredStretchSize.Width() = mBoundingMetrics.width;
  aDesiredStretchSize.mBoundingMetrics = mBoundingMetrics;
  mReference.x = 0;
  mReference.y = aDesiredStretchSize.BlockStartAscent();
  // Place our mMathMLChar, its origin is in our coordinate system
  if (useMathMLChar) {
    nscoord dy =
        aDesiredStretchSize.BlockStartAscent() - mBoundingMetrics.ascent;
    mMathMLChar.SetRect(
        nsRect(0, dy, charSize.width, charSize.ascent + charSize.descent));
  }

  // Before we leave... there is a last item in the check-list:
  // If our parent is not embellished, it means we are the outermost embellished
  // container and so we put the spacing, otherwise we don't include the
  // spacing, the outermost embellished container will take care of it.

  nscoord leadingSpace = 0, trailingSpace = 0;
  if (!StaticPrefs::
          mathml_lspace_rspace_for_child_spacing_during_mrow_layout_enabled() &&
      !mFlags.Booleans().contains(OperatorBoolean::HasEmbellishAncestor)) {
    // Account the spacing if we are not an accent with explicit attributes
    if (!isAccent ||
        mFlags.Booleans().contains(OperatorBoolean::HasLSpaceAttribute)) {
      leadingSpace = mEmbellishData.leadingSpace;
    }
    if (!isAccent ||
        mFlags.Booleans().contains(OperatorBoolean::HasRSpaceAttribute)) {
      trailingSpace = mEmbellishData.trailingSpace;
    }
  }

  flags = PlaceFlags();
  auto sizes = GetWidthAndHeightForPlaceAdjustment(flags);
  auto borderPadding = GetBorderPaddingForPlace(flags);
  if (leadingSpace || trailingSpace || !borderPadding.IsAllZero() ||
      sizes.width || sizes.height) {
    mBoundingMetrics.width += leadingSpace + trailingSpace;
    aDesiredStretchSize.Width() = mBoundingMetrics.width;
    aDesiredStretchSize.mBoundingMetrics.width = mBoundingMetrics.width;

    nscoord dx = GetWritingMode().IsBidiRTL() ? trailingSpace : leadingSpace;
    mBoundingMetrics.leftBearing += dx;
    mBoundingMetrics.rightBearing += dx;
    aDesiredStretchSize.mBoundingMetrics.leftBearing += dx;
    aDesiredStretchSize.mBoundingMetrics.rightBearing += dx;

    // Apply inline/block sizes to math content box.
    dx += ApplyAdjustmentForWidthAndHeight(flags, sizes, aDesiredStretchSize,
                                           mBoundingMetrics);

    // Add border/padding.
    InflateReflowAndBoundingMetrics(borderPadding, aDesiredStretchSize,
                                    mBoundingMetrics);
    dx += borderPadding.left;
    nscoord dy = borderPadding.top;

    if (dx || dy) {
      // adjust the offsets

      if (useMathMLChar) {
        nsRect rect;
        mMathMLChar.GetRect(rect);
        mMathMLChar.SetRect(
            nsRect(rect.x + dx, rect.y + dy, rect.width, rect.height));
      } else {
        nsIFrame* childFrame = firstChild;
        while (childFrame) {
          childFrame->SetPosition(childFrame->GetPosition() + nsPoint(dx, dy));
          childFrame = childFrame->GetNextSibling();
        }
      }
    }
  }

  // Finished with these:
  ClearSavedChildMetrics();
  // Set our overflow area
  GatherAndStoreOverflow(&aDesiredStretchSize);

  // There used to be code here to change the height of the child frame to
  // change the caret height, but the text frame that manages the caret is now
  // not a direct child but wrapped in a block frame.  See also bug 412033.

  return NS_OK;
}

NS_IMETHODIMP
nsMathMLmoFrame::InheritAutomaticData(nsIFrame* aParent) {
  // retain our native direction, it only changes if our text content changes
  StretchDirection direction = mEmbellishData.direction;
  nsMathMLTokenFrame::InheritAutomaticData(aParent);
  ProcessTextData();
  mEmbellishData.direction = direction;
  return NS_OK;
}

NS_IMETHODIMP
nsMathMLmoFrame::TransmitAutomaticData() {
  // this will cause us to re-sync our flags from scratch
  // but our returned 'form' is still not final (bug 133429), it will
  // be recomputed to its final value during the next call in Reflow()
  mEmbellishData.coreFrame = nullptr;
  ProcessOperatorData();
  return NS_OK;
}

void nsMathMLmoFrame::SetInitialChildList(ChildListID aListID,
                                          nsFrameList&& aChildList) {
  // First, let the parent class do its work
  nsMathMLTokenFrame::SetInitialChildList(aListID, std::move(aChildList));
  ProcessTextData();
}

void nsMathMLmoFrame::Reflow(nsPresContext* aPresContext,
                             ReflowOutput& aDesiredSize,
                             const ReflowInput& aReflowInput,
                             nsReflowStatus& aStatus) {
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  // certain values use units that depend on our ComputedStyle, so
  // it is safer to just process the whole lot here
  ProcessOperatorData();

  nsMathMLTokenFrame::Reflow(aPresContext, aDesiredSize, aReflowInput, aStatus);
}

void nsMathMLmoFrame::Place(DrawTarget* aDrawTarget, const PlaceFlags& aFlags,
                            ReflowOutput& aDesiredSize) {
  nsMathMLTokenFrame::Place(aDrawTarget, aFlags, aDesiredSize);

  /* Special behaviour for largeops.
     In MathML "stretchy" and displaystyle "largeop" are different notions,
     even if we use the same technique to draw them (picking size variants).
     So largeop display operators should be considered "non-stretchy" and
     thus their sizes should be taken into account for the stretch size of
     other elements.

     This is a preliminary stretch - exact sizing/placement is handled by the
     Stretch() method.
  */

  if (aFlags.contains(PlaceFlag::MeasureOnly) &&
      StyleFont()->mMathStyle == StyleMathStyle::Normal &&
      mFlags.Booleans().contains(OperatorBoolean::LargeOperator) &&
      UseMathMLChar()) {
    nsBoundingMetrics newMetrics;
    nsresult rv = mMathMLChar.Stretch(
        this, aDrawTarget, nsLayoutUtils::FontSizeInflationFor(this),
        StretchDirection::Vertical, aDesiredSize.mBoundingMetrics, newMetrics,
        MathMLStretchFlag::LargeOperator, GetWritingMode().IsBidiRTL());

    if (NS_FAILED(rv)) {
      // Just use the initial size
      return;
    }

    aDesiredSize.mBoundingMetrics = newMetrics;
    /* Treat the ascent/descent values calculated in the TokenFrame place
      calculations as the minimum for aDesiredSize calculations, rather
      than fetching them from font metrics again.
   */
    aDesiredSize.SetBlockStartAscent(
        std::max(mBoundingMetrics.ascent, newMetrics.ascent));
    aDesiredSize.Height() =
        aDesiredSize.BlockStartAscent() +
        std::max(mBoundingMetrics.descent, newMetrics.descent);
    aDesiredSize.Width() = newMetrics.width;
    mBoundingMetrics = newMetrics;
  }
}

/* virtual */
void nsMathMLmoFrame::MarkIntrinsicISizesDirty() {
  // if we get this, it may mean that something changed in the text
  // content. So blow away everything an re-build the automatic data
  // from the parent of our outermost embellished container (we ensure
  // that we are the core, not just a sibling of the core)

  ProcessTextData();

  nsIFrame* target = this;
  nsEmbellishData embellishData;
  do {
    target = target->GetParent();
    GetEmbellishDataFrom(target, embellishData);
  } while (embellishData.coreFrame == this);

  // we have automatic data to update in the children of the target frame
  // XXXldb This should really be marking dirty rather than rebuilding
  // so that we don't rebuild multiple times for the same change.
  RebuildAutomaticDataForChildren(target);

  nsMathMLContainerFrame::MarkIntrinsicISizesDirty();
}

/* virtual */
void nsMathMLmoFrame::GetIntrinsicISizeMetrics(gfxContext* aRenderingContext,
                                               ReflowOutput& aDesiredSize) {
  ProcessOperatorData();
  if (UseMathMLChar()) {
    auto stretchFlags =
        GetStretchFlags(mFlags, mPresentationData, true, StyleFont());
    aDesiredSize.Width() = mMathMLChar.GetMaxWidth(
        this, aRenderingContext->GetDrawTarget(),
        nsLayoutUtils::FontSizeInflationFor(this), stretchFlags);
    aDesiredSize.Width() += IntrinsicISizeOffsets().BorderPadding();
  } else {
    nsMathMLTokenFrame::GetIntrinsicISizeMetrics(aRenderingContext,
                                                 aDesiredSize);
  }

  // leadingSpace and trailingSpace are actually applied to the outermost
  // embellished container but for determining total intrinsic width it should
  // be safe to include it for the core here instead.
  nscoord leadingSpace = 0, trailingSpace = 0;
  if (!StaticPrefs::
          mathml_lspace_rspace_for_child_spacing_during_mrow_layout_enabled()) {
    leadingSpace = mEmbellishData.leadingSpace;
    trailingSpace = mEmbellishData.trailingSpace;
  }
  bool isRTL = GetWritingMode().IsBidiRTL();
  aDesiredSize.Width() += leadingSpace + trailingSpace;
  aDesiredSize.mBoundingMetrics.width = aDesiredSize.Width();
  if (isRTL) {
    aDesiredSize.mBoundingMetrics.leftBearing += trailingSpace;
    aDesiredSize.mBoundingMetrics.rightBearing += trailingSpace;
  } else {
    aDesiredSize.mBoundingMetrics.leftBearing += leadingSpace;
    aDesiredSize.mBoundingMetrics.rightBearing += leadingSpace;
  }
}

nsresult nsMathMLmoFrame::AttributeChanged(int32_t aNameSpaceID,
                                           nsAtom* aAttribute,
                                           AttrModType aModType) {
  // check if this is an attribute that can affect the embellished hierarchy
  // in a significant way and re-layout the entire hierarchy.
  // This is not needed for the fence and separator
  // attributes, since they have no visual effect.
  if (aAttribute == nsGkAtoms::accent || aAttribute == nsGkAtoms::form ||
      aAttribute == nsGkAtoms::largeop || aAttribute == nsGkAtoms::maxsize ||
      aAttribute == nsGkAtoms::minsize ||
      aAttribute == nsGkAtoms::movablelimits ||
      aAttribute == nsGkAtoms::rspace || aAttribute == nsGkAtoms::stretchy ||
      aAttribute == nsGkAtoms::symmetric || aAttribute == nsGkAtoms::lspace) {
    // set the target as the parent of our outermost embellished container
    // (we ensure that we are the core, not just a sibling of the core)
    nsIFrame* target = this;
    nsEmbellishData embellishData;
    do {
      target = target->GetParent();
      GetEmbellishDataFrom(target, embellishData);
    } while (embellishData.coreFrame == this);

    // we have automatic data to update in the children of the target frame
    return ReLayoutChildren(target);
  }

  return nsMathMLTokenFrame::AttributeChanged(aNameSpaceID, aAttribute,
                                              aModType);
}

void nsMathMLmoFrame::DidSetComputedStyle(ComputedStyle* aOldStyle) {
  nsMathMLTokenFrame::DidSetComputedStyle(aOldStyle);
  mMathMLChar.SetComputedStyle(Style());
}

nscoord nsMathMLmoFrame::ItalicCorrection() {
  return UseMathMLChar() ? mMathMLChar.ItalicCorrection() : 0;
}

nscoord nsMathMLmoFrame::FixInterFrameSpacing(ReflowOutput& aDesiredSize) {
  nscoord gap = nsMathMLContainerFrame::FixInterFrameSpacing(aDesiredSize);
  if (!gap) {
    return 0;
  }

  // Move the MathML character.
  nsRect rect;
  mMathMLChar.GetRect(rect);
  rect.MoveBy(gap, 0);
  mMathMLChar.SetRect(rect);

  return gap;
}
