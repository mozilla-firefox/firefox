/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for the HTML <canvas> element */

#include "nsHTMLCanvasFrame.h"

#include "ActiveLayerTracker.h"
#include "mozilla/Assertions.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/layers/WebRenderCanvasRenderer.h"
#include "mozilla/webgpu/CanvasContext.h"
#include "nsDisplayList.h"
#include "nsLayoutUtils.h"
#include "nsStyleUtil.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::layers;
using namespace mozilla::gfx;

/* Helper for our nsIFrame::GetIntrinsicSize() impl. Takes the result of
 * "GetCanvasSize()" as a parameter, which may help avoid redundant
 * indirect calls to GetCanvasSize().
 *
 * @param aCanvasSizeInPx The canvas's size in CSS pixels, as returned
 *                        by GetCanvasSize().
 * @return The canvas's intrinsic size, as an IntrinsicSize object.
 */
static IntrinsicSize IntrinsicSizeFromCanvasSize(
    const CSSIntSize& aCanvasSizeInPx) {
  return IntrinsicSize(CSSIntSize::ToAppUnits(aCanvasSizeInPx));
}

/* Helper for our nsIFrame::GetIntrinsicRatio() impl. Takes the result of
 * "GetCanvasSize()" as a parameter, which may help avoid redundant
 * indirect calls to GetCanvasSize().
 *
 * @return The canvas's intrinsic ratio.
 */
static AspectRatio IntrinsicRatioFromCanvasSize(
    const CSSIntSize& aCanvasSizeInPx) {
  return AspectRatio::FromSize(aCanvasSizeInPx);
}

class nsDisplayCanvas final : public nsPaintedDisplayItem {
 public:
  nsDisplayCanvas(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame)
      : nsPaintedDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayCanvas);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplayCanvas)

  NS_DISPLAY_DECL_NAME("nsDisplayCanvas", TYPE_CANVAS)

  nsRegion GetOpaqueRegion(nsDisplayListBuilder* aBuilder,
                           bool* aSnap) const override {
    *aSnap = false;
    auto* f = static_cast<nsHTMLCanvasFrame*>(Frame());
    auto* canvas = HTMLCanvasElement::FromNode(f->GetContent());
    if (!canvas->GetIsOpaque()) {
      return {};
    }
    // OK, the entire region painted by the canvas is opaque. But what is
    // that region? It's the canvas's "dest rect" (controlled by the
    // object-fit/object-position CSS properties), clipped to the container's
    // ink overflow box (which is what GetBounds() returns). So, we grab those
    // rects and intersect them.
    const nsRect constraintRect = GetBounds(aBuilder, aSnap);
    const nsRect destRect =
        f->GetDestRect(f->GetContentRectRelativeToSelf() + ToReferenceFrame());
    return nsRegion(destRect.Intersect(constraintRect));
  }

  nsRect GetBounds(nsDisplayListBuilder* aBuilder, bool* aSnap) const override {
    *aSnap = true;
    return Frame()->InkOverflowRectRelativeToSelf() + ToReferenceFrame();
  }

  bool CreateWebRenderCommands(
      mozilla::wr::DisplayListBuilder& aBuilder,
      wr::IpcResourceUpdateQueue& aResources, const StackingContextHelper& aSc,
      mozilla::layers::RenderRootStateManager* aManager,
      nsDisplayListBuilder* aDisplayListBuilder) override {
    HTMLCanvasElement* element =
        static_cast<HTMLCanvasElement*>(mFrame->GetContent());
    element->HandlePrintCallback(mFrame->PresContext());

    if (element->IsOffscreen()) {
      // If we are offscreen, then we either display via an ImageContainer
      // which is updated asynchronously, likely from a worker thread, or a
      // CompositableHandle managed inside the compositor process. There is
      // nothing to paint until the owner attaches it.

      element->FlushOffscreenCanvas();

      auto* canvasFrame = static_cast<nsHTMLCanvasFrame*>(mFrame);
      nsRect dest = canvasFrame->GetDestRect(
          mFrame->GetContentRectRelativeToSelf() + ToReferenceFrame());
      LayoutDeviceRect bounds = LayoutDeviceRect::FromAppUnits(
          dest, mFrame->PresContext()->AppUnitsPerDevPixel());

      RefPtr<ImageContainer> container = element->GetImageContainer();
      if (container) {
        MOZ_ASSERT(container->IsAsync());
        aManager->CommandBuilder().PushImage(this, container, aBuilder,
                                             aResources, aSc, bounds, bounds);
        return true;
      }

      return true;
    }

    switch (element->GetCurrentContextType()) {
      case CanvasContextType::Canvas2D:
      case CanvasContextType::WebGL1:
      case CanvasContextType::WebGL2:
      case CanvasContextType::WebGPU: {
        bool isRecycled;
        RefPtr<WebRenderCanvasData> canvasData =
            aManager->CommandBuilder()
                .CreateOrRecycleWebRenderUserData<WebRenderCanvasData>(
                    this, &isRecycled);
        auto* canvasFrame = static_cast<nsHTMLCanvasFrame*>(mFrame);
        if (!canvasFrame->UpdateWebRenderCanvasData(aDisplayListBuilder,
                                                    canvasData)) {
          return true;
        }
        WebRenderCanvasRendererAsync* data = canvasData->GetCanvasRenderer();
        MOZ_ASSERT(data);
        data->UpdateCompositableClient();

        // Push IFrame for async image pipeline.
        // XXX Remove this once partial display list update is supported.
        nsRect dest = canvasFrame->GetDestRect(
            mFrame->GetContentRectRelativeToSelf() + ToReferenceFrame());
        LayoutDeviceRect bounds = LayoutDeviceRect::FromAppUnits(
            dest, mFrame->PresContext()->AppUnitsPerDevPixel());

        // We don't push a stacking context for this async image pipeline here.
        // Instead, we do it inside the iframe that hosts the image. As a
        // result, a bunch of the calculations normally done as part of that
        // stacking context need to be done manually and pushed over to the
        // parent side, where it will be done when we build the display list for
        // the iframe. That happens in WebRenderCompositableHolder.s2);
        aBuilder.PushIFrame(bounds, !BackfaceIsHidden(),
                            data->GetPipelineId().ref(),
                            /*ignoreMissingPipelines*/ true);

        LayoutDeviceRect scBounds(LayoutDevicePoint(0, 0), bounds.Size());
        auto filter = wr::ToImageRendering(mFrame->UsedImageRendering());
        auto mixBlendMode = wr::MixBlendMode::Normal;
        aManager->WrBridge()->AddWebRenderParentCommand(
            OpUpdateAsyncImagePipeline(data->GetPipelineId().value(), scBounds,
                                       wr::WrRotation::Degree0, filter,
                                       mixBlendMode));
        break;
      }
      case CanvasContextType::ImageBitmap: {
        nsHTMLCanvasFrame* canvasFrame =
            static_cast<nsHTMLCanvasFrame*>(mFrame);
        CSSIntSize canvasSizeInPx = canvasFrame->GetCanvasSize();
        if (canvasSizeInPx.width <= 0 || canvasSizeInPx.height <= 0) {
          return true;
        }
        bool isRecycled;
        RefPtr<WebRenderCanvasData> canvasData =
            aManager->CommandBuilder()
                .CreateOrRecycleWebRenderUserData<WebRenderCanvasData>(
                    this, &isRecycled);
        if (!canvasFrame->UpdateWebRenderCanvasData(aDisplayListBuilder,
                                                    canvasData)) {
          canvasData->ClearImageContainer();
          return true;
        }

        nsRect dest = canvasFrame->GetDestRect(
            mFrame->GetContentRectRelativeToSelf() + ToReferenceFrame());

        LayoutDeviceRect bounds = LayoutDeviceRect::FromAppUnits(
            dest, mFrame->PresContext()->AppUnitsPerDevPixel());

        aManager->CommandBuilder().PushImage(
            this, canvasData->GetImageContainer(), aBuilder, aResources, aSc,
            bounds, bounds);
        break;
      }
      case CanvasContextType::NoContext:
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("unknown canvas context type");
    }
    return true;
  }

  // FirstContentfulPaint is supposed to ignore "white" canvases.  We use
  // MaybeModified (if GetContext() was called on the canvas) as a standin for
  // "white"
  bool IsContentful() const override {
    nsHTMLCanvasFrame* f = static_cast<nsHTMLCanvasFrame*>(Frame());
    HTMLCanvasElement* canvas = HTMLCanvasElement::FromNode(f->GetContent());
    return canvas->MaybeModified();
  }

  void Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) override {
    auto* f = static_cast<nsHTMLCanvasFrame*>(Frame());
    auto* canvas = HTMLCanvasElement::FromNode(f->GetContent());

    CSSIntSize canvasSizeInPx = f->GetCanvasSize();
    nsPresContext* presContext = f->PresContext();
    canvas->HandlePrintCallback(presContext);

    if (canvasSizeInPx.width <= 0 || canvasSizeInPx.height <= 0) {
      return;
    }

    nsRect dest = f->GetDestRect(mFrame->GetContentRectRelativeToSelf() +
                                 ToReferenceFrame());
    if (dest.IsEmpty()) {
      return;
    }

    gfxContextMatrixAutoSaveRestore saveMatrix(aCtx);

    if (RefPtr<layers::Image> image = canvas->GetAsImage()) {
      gfxRect destGFXRect = presContext->AppUnitsToGfxUnits(dest);

      // Transform the canvas into the right place
      gfxPoint p = destGFXRect.TopLeft();
      Matrix transform = Matrix::Translation(p.x, p.y);
      transform.PreScale(destGFXRect.Width() / canvasSizeInPx.width,
                         destGFXRect.Height() / canvasSizeInPx.height);

      aCtx->SetMatrix(
          gfxUtils::SnapTransformTranslation(aCtx->CurrentMatrix(), nullptr));

      RefPtr<gfx::SourceSurface> surface = image->GetAsSourceSurface();
      if (!surface || !surface->IsValid()) {
        return;
      }

      transform = gfxUtils::SnapTransform(
          transform, gfxRect(0, 0, canvasSizeInPx.width, canvasSizeInPx.height),
          nullptr);
      aCtx->Multiply(transform);

      aCtx->GetDrawTarget()->FillRect(
          Rect(0, 0, canvasSizeInPx.width, canvasSizeInPx.height),
          SurfacePattern(surface, ExtendMode::CLAMP, Matrix(),
                         nsLayoutUtils::GetSamplingFilterForFrame(f)));
      return;
    }

    if (canvas->IsOffscreen()) {
      return;
    }

    auto renderer = MakeRefPtr<CanvasRenderer>();
    if (!canvas->InitializeCanvasRenderer(aBuilder, renderer)) {
      return;
    }
    renderer->FirePreTransactionCallback();
    const auto snapshot = renderer->BorrowSnapshot();
    if (!snapshot) {
      return;
    }
    const auto& surface = snapshot->mSurf;
    DrawTarget& dt = *aCtx->GetDrawTarget();
    gfx::Rect destRect =
        NSRectToSnappedRect(dest, presContext->AppUnitsPerDevPixel(), dt);

    if (!renderer->YIsDown()) {
      // Calculate y-coord that is as far below the bottom of destGFXRect as
      // the origin was above the top, then reflect about that.
      float y = destRect.Y() + destRect.YMost();
      Matrix transform = Matrix::Translation(0.0f, y).PreScale(1.0f, -1.0f);
      aCtx->Multiply(transform);
    }

    const auto& srcRect = surface->GetRect();
    dt.DrawSurface(
        surface, destRect,
        Rect(float(srcRect.X()), float(srcRect.Y()), float(srcRect.Width()),
             float(srcRect.Height())),
        DrawSurfaceOptions(nsLayoutUtils::GetSamplingFilterForFrame(f)));

    renderer->FireDidTransactionCallback();
    renderer->ResetDirty();
  }
};

nsIFrame* NS_NewHTMLCanvasFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsHTMLCanvasFrame(aStyle, aPresShell->GetPresContext());
}

NS_QUERYFRAME_HEAD(nsHTMLCanvasFrame)
  NS_QUERYFRAME_ENTRY(nsHTMLCanvasFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

NS_IMPL_FRAMEARENA_HELPERS(nsHTMLCanvasFrame)

void nsHTMLCanvasFrame::Destroy(DestroyContext& aContext) {
  if (IsPrimaryFrame()) {
    HTMLCanvasElement::FromNode(*mContent)->ResetPrintCallback();
  }
  nsContainerFrame::Destroy(aContext);
}

nsHTMLCanvasFrame::~nsHTMLCanvasFrame() = default;

CSSIntSize nsHTMLCanvasFrame::GetCanvasSize() const {
  CSSIntSize size;
  if (auto* canvas = HTMLCanvasElement::FromNodeOrNull(GetContent())) {
    size = canvas->GetSize();
    MOZ_ASSERT(size.width >= 0 && size.height >= 0,
               "we should've required <canvas> width/height attrs to be "
               "unsigned (non-negative) values");
  } else {
    MOZ_ASSERT_UNREACHABLE("couldn't get canvas size");
  }
  return size;
}

nscoord nsHTMLCanvasFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                          IntrinsicISizeType aType) {
  if (Maybe<nscoord> containISize = ContainIntrinsicISize()) {
    return *containISize;
  }
  bool vertical = GetWritingMode().IsVertical();
  return nsPresContext::CSSPixelsToAppUnits(vertical ? GetCanvasSize().height
                                                     : GetCanvasSize().width);
}

/* virtual */
IntrinsicSize nsHTMLCanvasFrame::GetIntrinsicSize() {
  const auto containAxes = GetContainSizeAxes();
  IntrinsicSize size = containAxes.IsBoth()
                           ? IntrinsicSize(0, 0)
                           : IntrinsicSizeFromCanvasSize(GetCanvasSize());
  return FinishIntrinsicSize(containAxes, size);
}

/* virtual */
AspectRatio nsHTMLCanvasFrame::GetIntrinsicRatio() const {
  if (GetContainSizeAxes().IsAny()) {
    return AspectRatio();
  }

  return IntrinsicRatioFromCanvasSize(GetCanvasSize());
}

/* virtual */
nsIFrame::SizeComputationResult nsHTMLCanvasFrame::ComputeSize(
    const SizeComputationInput& aSizingInput, WritingMode aWM,
    const LogicalSize& aCBSize, nscoord aAvailableISize,
    const LogicalSize& aMargin, const LogicalSize& aBorderPadding,
    const StyleSizeOverrides& aSizeOverrides, ComputeSizeFlags aFlags) {
  return {ComputeSizeWithIntrinsicDimensions(
              aSizingInput.mRenderingContext, aWM, GetIntrinsicSize(),
              GetAspectRatio(), aCBSize, aMargin, aBorderPadding,
              aSizeOverrides, aFlags),
          AspectRatioUsage::None};
}

nsRect nsHTMLCanvasFrame::GetDestRect(const nsRect& aFrameContentBox) const {
  const CSSIntSize canvasSizeInPx = GetCanvasSize();
  IntrinsicSize intrinsicSize = IntrinsicSizeFromCanvasSize(canvasSizeInPx);
  AspectRatio intrinsicRatio = IntrinsicRatioFromCanvasSize(canvasSizeInPx);
  return nsLayoutUtils::ComputeObjectDestRect(aFrameContentBox, intrinsicSize,
                                              intrinsicRatio, StylePosition());
}

void nsHTMLCanvasFrame::Reflow(nsPresContext* aPresContext,
                               ReflowOutput& aMetrics,
                               const ReflowInput& aReflowInput,
                               nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsHTMLCanvasFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_FRAME_TRACE(
      NS_FRAME_TRACE_CALLS,
      ("enter nsHTMLCanvasFrame::Reflow: availSize=%d,%d",
       aReflowInput.AvailableWidth(), aReflowInput.AvailableHeight()));

  MOZ_ASSERT(HasAnyStateBits(NS_FRAME_IN_REFLOW), "frame is not in reflow");

  WritingMode wm = aReflowInput.GetWritingMode();
  const LogicalSize finalSize = aReflowInput.ComputedSizeWithBorderPadding(wm);

  aMetrics.SetSize(wm, finalSize);
  aMetrics.SetOverflowAreasToDesiredBounds();
  aMetrics.mOverflowAreas.UnionAllWith(
      GetDestRect(aReflowInput.ComputedPhysicalContentBoxRelativeToSelf()));
  FinishAndStoreOverflow(&aMetrics);

  // Reflow the single anon block child.
  nsReflowStatus childStatus;
  nsIFrame* childFrame = mFrames.FirstChild();
  WritingMode childWM = childFrame->GetWritingMode();
  LogicalSize availSize = aReflowInput.ComputedSize(childWM);
  availSize.BSize(childWM) = NS_UNCONSTRAINEDSIZE;
  NS_ASSERTION(!childFrame->GetNextSibling(), "HTML canvas should have 1 kid");
  ReflowOutput childDesiredSize(aReflowInput.GetWritingMode());
  ReflowInput childReflowInput(aPresContext, aReflowInput, childFrame,
                               availSize);
  ReflowChild(childFrame, aPresContext, childDesiredSize, childReflowInput, 0,
              0, ReflowChildFlags::Default, childStatus, nullptr);
  FinishReflowChild(childFrame, aPresContext, childDesiredSize,
                    &childReflowInput, 0, 0, ReflowChildFlags::Default);

  NS_FRAME_TRACE(NS_FRAME_TRACE_CALLS,
                 ("exit nsHTMLCanvasFrame::Reflow: size=%d,%d",
                  aMetrics.ISize(wm), aMetrics.BSize(wm)));
}

bool nsHTMLCanvasFrame::UpdateWebRenderCanvasData(
    nsDisplayListBuilder* aBuilder, WebRenderCanvasData* aCanvasData) {
  HTMLCanvasElement* element = static_cast<HTMLCanvasElement*>(GetContent());
  return element->UpdateWebRenderCanvasData(aBuilder, aCanvasData);
}

void nsHTMLCanvasFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                         const nsDisplayListSet& aLists) {
  if (!IsVisibleForPainting()) {
    return;
  }

  DisplayBorderBackgroundOutline(aBuilder, aLists);

  if (HidesContent()) {
    return;
  }

  DisplayListClipState::AutoSaveRestore clipState(aBuilder);
  auto clipAxes = ShouldApplyOverflowClipping(StyleDisplay());
  if (!clipAxes.isEmpty()) {
    nsRect clipRect;
    nsRectCornerRadii radii;
    bool haveRadii =
        ComputeOverflowClipRectRelativeToSelf(clipAxes, clipRect, radii);
    if (haveRadii ||
        nsStyleUtil::ObjectPropsMightCauseOverflow(StylePosition())) {
      clipState.ClipContainingBlockDescendants(
          clipRect + aBuilder->ToReferenceFrame(this),
          haveRadii ? &radii : nullptr);
    }
  }

  aLists.Content()->AppendNewToTop<nsDisplayCanvas>(aBuilder, this);
}

void nsHTMLCanvasFrame::AppendDirectlyOwnedAnonBoxes(
    nsTArray<OwnedAnonBox>& aResult) {
  MOZ_ASSERT(mFrames.FirstChild(), "Must have our canvas content anon box");
  MOZ_ASSERT(!mFrames.FirstChild()->GetNextSibling(),
             "Must only have our canvas content anon box");
  aResult.AppendElement(OwnedAnonBox(mFrames.FirstChild()));
}

void nsHTMLCanvasFrame::UnionChildOverflow(OverflowAreas& aOverflowAreas,
                                           bool) {
  // Our one child (the canvas content anon box) is unpainted and isn't relevant
  // for child-overflow purposes. So we need to provide our own trivial impl to
  // avoid receiving the child-considering impl that we would otherwise inherit.
}

#ifdef ACCESSIBILITY
a11y::AccType nsHTMLCanvasFrame::AccessibleType() {
  return a11y::eHTMLCanvasType;
}
#endif

#ifdef DEBUG_FRAME_DUMP
nsresult nsHTMLCanvasFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"HTMLCanvas"_ns, aResult);
}
#endif
