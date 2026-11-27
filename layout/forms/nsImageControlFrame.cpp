/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsImageFrame.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsStyleConsts.h"

using namespace mozilla;

class nsImageControlFrame final : public nsImageFrame {
 public:
  explicit nsImageControlFrame(ComputedStyle* aStyle,
                               nsPresContext* aPresContext);
  ~nsImageControlFrame() final;

  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) final;

  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS(nsImageControlFrame)

  void Reflow(nsPresContext*, ReflowOutput&, const ReflowInput&,
              nsReflowStatus&) final;

  nsresult HandleEvent(nsPresContext*, WidgetGUIEvent*, nsEventStatus*) final;

#ifdef ACCESSIBILITY
  mozilla::a11y::AccType AccessibleType() final;
#endif

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const final {
    return MakeFrameName(u"ImageControl"_ns, aResult);
  }
#endif

  Cursor GetCursor(const nsPoint&) final;
};

nsImageControlFrame::nsImageControlFrame(ComputedStyle* aStyle,
                                         nsPresContext* aPresContext)
    : nsImageFrame(aStyle, aPresContext, kClassID) {}

nsImageControlFrame::~nsImageControlFrame() = default;

nsIFrame* NS_NewImageControlFrame(PresShell* aPresShell,
                                  ComputedStyle* aStyle) {
  return new nsImageControlFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsImageControlFrame)

void nsImageControlFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                               nsIFrame* aPrevInFlow) {
  nsImageFrame::Init(aContent, aParent, aPrevInFlow);

  if (aPrevInFlow) {
    return;
  }

  mContent->SetProperty(nsGkAtoms::imageClickedPoint, new CSSIntPoint(0, 0),
                        nsINode::DeleteProperty<CSSIntPoint>);
}

NS_QUERYFRAME_HEAD(nsImageControlFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsImageFrame)

#ifdef ACCESSIBILITY
a11y::AccType nsImageControlFrame::AccessibleType() {
  if (mContent->IsAnyOfHTMLElements(nsGkAtoms::button, nsGkAtoms::input)) {
    return a11y::eHTMLButtonType;
  }

  return a11y::eNoType;
}
#endif

void nsImageControlFrame::Reflow(nsPresContext* aPresContext,
                                 ReflowOutput& aDesiredSize,
                                 const ReflowInput& aReflowInput,
                                 nsReflowStatus& aStatus) {
  DO_GLOBAL_REFLOW_COUNT("nsImageControlFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  return nsImageFrame::Reflow(aPresContext, aDesiredSize, aReflowInput,
                              aStatus);
}

nsresult nsImageControlFrame::HandleEvent(nsPresContext* aPresContext,
                                          WidgetGUIEvent* aEvent,
                                          nsEventStatus* aEventStatus) {
  NS_ENSURE_ARG_POINTER(aEventStatus);

  // Don't do anything if the event has already been handled by someone
  if (nsEventStatus_eConsumeNoDefault == *aEventStatus) {
    return NS_OK;
  }

  if (IsContentDisabled()) {
    return nsIFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
  }

  *aEventStatus = nsEventStatus_eIgnore;

  if (aEvent->mMessage == eMouseUp &&
      aEvent->AsMouseEvent()->mButton == MouseButton::ePrimary) {
    // Store click point for HTMLInputElement::SubmitNamesValues
    // Do this on MouseUp because the specs don't say and that's what IE does
    auto* lastClickedPoint = static_cast<CSSIntPoint*>(
        mContent->GetProperty(nsGkAtoms::imageClickedPoint));
    if (lastClickedPoint) {
      // normally lastClickedPoint is not null, as it's allocated in Init()
      nsPoint pt = nsLayoutUtils::GetEventCoordinatesRelativeTo(
          aEvent, RelativeTo{this});
      *lastClickedPoint = TranslateEventCoords(pt);
    }
  }
  return nsImageFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
}

nsIFrame::Cursor nsImageControlFrame::GetCursor(const nsPoint&) {
  StyleCursorKind kind = StyleUI()->Cursor().keyword;
  if (kind == StyleCursorKind::Auto) {
    kind = StyleCursorKind::Pointer;
  }
  return Cursor{kind, AllowCustomCursorImage::Yes};
}
