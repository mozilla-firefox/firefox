/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* utility functions for drawing borders and backgrounds */

#ifndef nsCSSRendering_h_
#define nsCSSRendering_h_

#include "gfxBlur.h"
#include "gfxContext.h"
#include "gfxTextRun.h"
#include "mozilla/TypedEnumBits.h"
#include "mozilla/gfx/PathHelpers.h"
#include "mozilla/gfx/Rect.h"
#include "nsCSSRenderingBorders.h"
#include "nsIFrame.h"
#include "nsImageRenderer.h"
#include "nsStyleStruct.h"

class gfxContext;
class nsPresContext;

namespace mozilla {

class ComputedStyle;

namespace gfx {
struct sRGBColor;
class DrawTarget;
}  // namespace gfx

namespace layers {
class ImageContainer;
class StackingContextHelper;
class WebRenderParentCommand;
class WebRenderLayerManager;
class RenderRootStateManager;
}  // namespace layers

namespace wr {
class DisplayListBuilder;
}  // namespace wr

enum class PaintBorderFlags : uint8_t { SyncDecodeImages = 1 << 0 };
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(PaintBorderFlags)

}  // namespace mozilla

/**
 * A struct representing all the information needed to paint a background
 * image to some target, taking into account all CSS background-* properties.
 * See PrepareImageLayer.
 */
struct nsBackgroundLayerState {
  typedef mozilla::gfx::CompositionOp CompositionOp;
  typedef mozilla::nsImageRenderer nsImageRenderer;

  /**
   * @param aFlags some combination of nsCSSRendering::PAINTBG_* flags
   */
  nsBackgroundLayerState(nsIFrame* aForFrame, const mozilla::StyleImage* aImage,
                         uint32_t aFlags)
      : mImageRenderer(aForFrame, aImage, aFlags) {}

  /**
   * The nsImageRenderer that will be used to draw the background.
   */
  nsImageRenderer mImageRenderer;
  /**
   * A rectangle that one copy of the image tile is mapped onto. Same
   * coordinate system as aBorderArea/aBGClipRect passed into
   * PrepareImageLayer.
   */
  nsRect mDestArea;
  /**
   * The actual rectangle that should be filled with (complete or partial)
   * image tiles. Same coordinate system as aBorderArea/aBGClipRect passed into
   * PrepareImageLayer.
   */
  nsRect mFillArea;
  /**
   * The anchor point that should be snapped to a pixel corner. Same
   * coordinate system as aBorderArea/aBGClipRect passed into
   * PrepareImageLayer.
   */
  nsPoint mAnchor;
  /**
   * The background-repeat property space keyword computes the
   * repeat size which is image size plus spacing.
   */
  nsSize mRepeatSize;
};

struct nsCSSRendering {
  typedef mozilla::gfx::sRGBColor sRGBColor;
  typedef mozilla::gfx::CompositionOp CompositionOp;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::gfx::Float Float;
  typedef mozilla::gfx::Point Point;
  typedef mozilla::gfx::Rect Rect;
  typedef mozilla::gfx::Size Size;
  typedef mozilla::gfx::RectCornerRadii RectCornerRadii;
  typedef mozilla::layers::WebRenderLayerManager WebRenderLayerManager;
  typedef mozilla::image::ImgDrawResult ImgDrawResult;
  typedef nsIFrame::Sides Sides;

  /**
   * Initialize any static variables used by nsCSSRendering.
   */
  static void Init();

  /**
   * Clean up any static variables used by nsCSSRendering.
   */
  static void Shutdown();

  static bool IsBoxDecorationSlice(const nsStyleBorder& aStyleBorder);
  static nsRect BoxDecorationRectForBorder(
      nsIFrame* aFrame, const nsRect& aBorderArea, Sides aSkipSides,
      const nsStyleBorder* aStyleBorder = nullptr);
  static nsRect BoxDecorationRectForBackground(
      nsIFrame* aFrame, const nsRect& aBorderArea, Sides aSkipSides,
      const nsStyleBorder* aStyleBorder = nullptr);

  static bool GetShadowInnerRadii(nsIFrame* aFrame, const nsRect& aFrameArea,
                                  RectCornerRadii& aOutInnerRadii);
  static nsRect GetBoxShadowInnerPaddingRect(nsIFrame* aFrame,
                                             const nsRect& aFrameArea);
  static bool ShouldPaintBoxShadowInner(nsIFrame* aFrame);
  static void PaintBoxShadowInner(nsPresContext* aPresContext,
                                  gfxContext& aRenderingContext,
                                  nsIFrame* aForFrame,
                                  const nsRect& aFrameArea);

  static bool GetBorderRadii(const nsRect& aFrameRect,
                             const nsRect& aBorderRect, nsIFrame* aFrame,
                             RectCornerRadii& aOutRadii);
  static nsRect GetShadowRect(const nsRect& aFrameArea, bool aNativeTheme,
                              nsIFrame* aForFrame);
  static mozilla::gfx::sRGBColor GetShadowColor(
      const mozilla::StyleSimpleShadow&, nsIFrame* aFrame, float aOpacity);
  // Returns if the frame has a themed frame.
  // aMaybeHasBorderRadius will return false if we can early detect
  // that we don't have a border radius.
  static bool HasBoxShadowNativeTheme(nsIFrame* aFrame,
                                      bool& aMaybeHasBorderRadius);
  static void PaintBoxShadowOuter(nsPresContext* aPresContext,
                                  gfxContext& aRenderingContext,
                                  nsIFrame* aForFrame, const nsRect& aFrameArea,
                                  const nsRect& aDirtyRect,
                                  float aOpacity = 1.0);

  static void ComputePixelRadii(const nsRectCornerRadii& aRadii,
                                nscoord aAppUnitsPerPixel,
                                RectCornerRadii* oBorderRadii);

  /**
   * Render the border for an element using css rendering rules
   * for borders. aSkipSides says which sides to skip
   * when rendering, the default is to skip none.
   */
  static ImgDrawResult PaintBorder(
      nsPresContext* aPresContext, gfxContext& aRenderingContext,
      nsIFrame* aForFrame, const nsRect& aDirtyRect, const nsRect& aBorderArea,
      mozilla::ComputedStyle* aStyle, mozilla::PaintBorderFlags aFlags,
      Sides aSkipSides = Sides());

  /**
   * Like PaintBorder, but taking an nsStyleBorder argument instead of
   * getting it from aStyle. aSkipSides says which sides to skip
   * when rendering, the default is to skip none.
   */
  static ImgDrawResult PaintBorderWithStyleBorder(
      nsPresContext* aPresContext, gfxContext& aRenderingContext,
      nsIFrame* aForFrame, const nsRect& aDirtyRect, const nsRect& aBorderArea,
      const nsStyleBorder& aBorderStyle, mozilla::ComputedStyle* aStyle,
      mozilla::PaintBorderFlags aFlags, Sides aSkipSides = Sides());

  static mozilla::Maybe<nsCSSBorderRenderer> CreateBorderRenderer(
      nsPresContext* aPresContext, DrawTarget* aDrawTarget, nsIFrame* aForFrame,
      const nsRect& aDirtyRect, const nsRect& aBorderArea,
      mozilla::ComputedStyle* aStyle, bool* aOutBorderIsEmpty,
      Sides aSkipSides = Sides());

  static mozilla::Maybe<nsCSSBorderRenderer>
  CreateBorderRendererWithStyleBorder(
      nsPresContext* aPresContext, DrawTarget* aDrawTarget, nsIFrame* aForFrame,
      const nsRect& aDirtyRect, const nsRect& aBorderArea,
      const nsStyleBorder& aBorderStyle, mozilla::ComputedStyle* aStyle,
      bool* aOutBorderIsEmpty, Sides aSkipSides = Sides());

  static mozilla::Maybe<nsCSSBorderRenderer>
  CreateNullBorderRendererWithStyleBorder(
      nsPresContext* aPresContext, DrawTarget* aDrawTarget, nsIFrame* aForFrame,
      const nsRect& aDirtyRect, const nsRect& aBorderArea,
      const nsStyleBorder& aBorderStyle, mozilla::ComputedStyle* aStyle,
      bool* aOutBorderIsEmpty, Sides aSkipSides = Sides());

  static mozilla::Maybe<nsCSSBorderRenderer>
  CreateBorderRendererForNonThemedOutline(nsPresContext* aPresContext,
                                          DrawTarget* aDrawTarget,
                                          nsIFrame* aForFrame,
                                          const nsRect& aDirtyRect,
                                          const nsRect& aInnerRect,
                                          mozilla::ComputedStyle* aStyle);

  static ImgDrawResult CreateWebRenderCommandsForBorder(
      mozilla::nsDisplayItem* aItem, nsIFrame* aForFrame,
      const nsRect& aBorderArea, mozilla::wr::DisplayListBuilder& aBuilder,
      mozilla::wr::IpcResourceUpdateQueue& aResources,
      const mozilla::layers::StackingContextHelper& aSc,
      mozilla::layers::RenderRootStateManager* aManager,
      mozilla::nsDisplayListBuilder* aDisplayListBuilder);

  static void CreateWebRenderCommandsForNullBorder(
      mozilla::nsDisplayItem* aItem, nsIFrame* aForFrame,
      const nsRect& aBorderArea, mozilla::wr::DisplayListBuilder& aBuilder,
      mozilla::wr::IpcResourceUpdateQueue& aResources,
      const mozilla::layers::StackingContextHelper& aSc,
      const nsStyleBorder& aStyleBorder);

  static ImgDrawResult CreateWebRenderCommandsForBorderWithStyleBorder(
      mozilla::nsDisplayItem* aItem, nsIFrame* aForFrame,
      const nsRect& aBorderArea, mozilla::wr::DisplayListBuilder& aBuilder,
      mozilla::wr::IpcResourceUpdateQueue& aResources,
      const mozilla::layers::StackingContextHelper& aSc,
      mozilla::layers::RenderRootStateManager* aManager,
      mozilla::nsDisplayListBuilder* aDisplayListBuilder,
      const nsStyleBorder& aStyleBorder);

  /**
   * Render the outline for an element using css rendering rules for borders.
   */
  static void PaintNonThemedOutline(nsPresContext* aPresContext,
                                    gfxContext& aRenderingContext,
                                    nsIFrame* aForFrame,
                                    const nsRect& aDirtyRect,
                                    const nsRect& aInnerRect,
                                    mozilla::ComputedStyle* aStyle);

  /**
   * Render keyboard focus on an element.
   * |aFocusRect| is the outer rectangle of the focused element.
   * Uses a fixed style equivalent to "1px dotted |aColor|".
   * Not used for controls, because the native theme may differ.
   */
  static nsCSSBorderRenderer GetBorderRendererForFocus(nsIFrame*, DrawTarget*,
                                                       const nsRect& aFocusRect,
                                                       nscolor aColor);

  /**
   * Find the frame whose background style should be used to draw the
   * canvas background. aForFrame must be the frame for the root element
   * whose background style should be used. This function will return
   * aForFrame unless the <body> background should be propagated, in
   * which case we return the frame associated with the <body>'s background.
   */
  static nsIFrame* FindBackgroundStyleFrame(nsIFrame* aForFrame);

  /**
   * Returns the ComputedStyle to be used to paint the background for the given
   * frame, if its element has a meaningful background.  This applies the rules
   * for propagating backgrounds between BODY, the root element, and the
   * canvas.
   *
   * @return the ComputedStyle (if any) to be used for painting aForFrame's
   *         background.
   */
  static mozilla::ComputedStyle* FindBackground(const nsIFrame* aForFrame);
  static nsIFrame* FindBackgroundFrame(const nsIFrame* aForFrame);

  /**
   * As FindBackground, but the passed-in frame is known to be a root frame
   * (returned from nsCSSFrameConstructor::GetRootElementStyleFrame())
   * and there is always some meaningful background returned.
   */
  static mozilla::ComputedStyle* FindRootFrameBackground(nsIFrame* aForFrame);

  /**
   * Find a non-transparent background color on an ancestor, for various
   * contrast checks. Note that this only accounts for background-color and
   * might stop at themed frames (depending on the argument), so it might not be
   * what you want. Note that if we stop at themed frames we might, in fact, end
   * up returning a transparent color (but then mIsThemed will be set to true).
   *
   * For semi-transparent colors, right now we blend with the default
   * background-color rather than with all ancestor backgrounds.
   *
   * If aPreferBodyToCanvas is true, we prefer the background color of the
   * <body> frame, even though we found a canvas background, because the body
   * background color is most likely what will be visible as the background
   * color of the page, even if the html element has a different background
   * color which prevents that of the body frame to propagate to the viewport.
   */
  struct EffectiveBackgroundColor {
    nscolor mColor = 0;
    bool mIsThemed = false;
  };
  static EffectiveBackgroundColor FindEffectiveBackgroundColor(
      nsIFrame* aFrame, bool aStopAtThemed = true,
      bool aPreferBodyToCanvas = false);

  /**
   * Determine the background color to draw taking into account print settings.
   */
  static nscolor DetermineBackgroundColor(nsPresContext* aPresContext,
                                          const mozilla::ComputedStyle* aStyle,
                                          nsIFrame* aFrame,
                                          bool& aDrawBackgroundImage,
                                          bool& aDrawBackgroundColor);

  static nsRect ComputeImageLayerPositioningArea(
      nsPresContext* aPresContext, nsIFrame* aForFrame,
      const nsRect& aBorderArea, const nsStyleImageLayers::Layer& aLayer,
      nsIFrame** aAttachedToFrame, bool* aOutTransformedFixed);

  struct BackgroundImageInfo {
    nsSize mSize{};
    nsPoint mAnchor{};
    nsPoint mTopLeft{};

    bool IsEmpty() const { return mSize.IsEmpty(); }
  };
  static BackgroundImageInfo ComputeBackgroundImageInfo(
      const mozilla::CSSSizeOrRatio& aIntrinsicSize,
      const nsSize& aPositionAreaSize, const nsStyleImageLayers::Layer& aLayer);

  // Implementation of the formula for computation of background-repeat round
  // See http://dev.w3.org/csswg/css3-background/#the-background-size
  // This function returns the adjusted size of the background image.
  static nscoord ComputeRoundedSize(nscoord aCurrentSize,
                                    nscoord aPositioningSize);

  /* ComputeBorderSpacedRepeatSize
   * aImageDimension: the image width/height
   * aAvailableSpace: the background positioning area width/height
   * aSpace: the space between each image
   * Returns the image size plus gap size of app units for use as spacing
   */
  static nscoord ComputeBorderSpacedRepeatSize(nscoord aImageDimension,
                                               nscoord aAvailableSpace,
                                               nscoord& aSpace);

  static nsBackgroundLayerState PrepareImageLayer(
      nsPresContext* aPresContext, nsIFrame* aForFrame, uint32_t aFlags,
      const nsRect& aBorderArea, const nsRect& aBGClipRect,
      const nsStyleImageLayers::Layer& aLayer,
      bool* aOutIsTransformedFixed = nullptr);

  struct ImageLayerClipState {
    nsRect mBGClipArea;            // Affected by mClippedRadii
    nsRect mAdditionalBGClipArea;  // Not affected by mClippedRadii
    nsRect mDirtyRectInAppUnits;
    gfxRect mDirtyRectInDevPx;

    nsRectCornerRadii mRadii;
    RectCornerRadii mClippedRadii;
    bool mHasRoundedCorners;
    bool mHasAdditionalBGClipArea;

    // Whether we are being asked to draw with a caller provided background
    // clipping area. If this is true we also disable rounded corners.
    bool mCustomClip;

    ImageLayerClipState()
        : mHasRoundedCorners(false),
          mHasAdditionalBGClipArea(false),
          mCustomClip(false) {}

    bool IsValid() const;
  };

  static void GetImageLayerClip(const nsStyleImageLayers::Layer& aLayer,
                                nsIFrame* aForFrame,
                                const nsStyleBorder& aBorder,
                                const nsRect& aBorderArea,
                                const nsRect& aCallerDirtyRect,
                                bool aWillPaintBorder,
                                nscoord aAppUnitsPerPixel,
                                /* out */ ImageLayerClipState* aClipState);

  /**
   * Render the background for an element using css rendering rules
   * for backgrounds or mask.
   */
  enum {
    /**
     * When this flag is passed, the element's nsDisplayBorder will be
     * painted immediately on top of this background.
     */
    PAINTBG_WILL_PAINT_BORDER = 0x01,
    /**
     * When this flag is passed, images are synchronously decoded.
     */
    PAINTBG_SYNC_DECODE_IMAGES = 0x02,
    /**
     * When this flag is passed, painting will go to the screen so we can
     * take advantage of the fact that it will be clipped to the viewport.
     */
    PAINTBG_TO_WINDOW = 0x04,
    /**
     * When this flag is passed, painting will read properties of mask-image
     * style, instead of background-image.
     */
    PAINTBG_MASK_IMAGE = 0x08,
    /**
     * When this flag is passed, images are downscaled during decode. This
     * is also implied by PAINTBG_TO_WINDOW.
     */
    PAINTBG_HIGH_QUALITY_SCALING = 0x10,
  };

  struct PaintBGParams {
    nsPresContext& presCtx;
    nsRect dirtyRect;
    nsRect borderArea;
    nsIFrame* frame;
    uint32_t paintFlags;
    nsRect* bgClipRect = nullptr;
    int32_t layer;  // -1 means painting all layers; other
                    // value means painting one specific
                    // layer only.
    CompositionOp compositionOp;
    float opacity;

    static PaintBGParams ForAllLayers(nsPresContext& aPresCtx,
                                      const nsRect& aDirtyRect,
                                      const nsRect& aBorderArea,
                                      nsIFrame* aFrame, uint32_t aPaintFlags,
                                      float aOpacity = 1.0);
    static PaintBGParams ForSingleLayer(
        nsPresContext& aPresCtx, const nsRect& aDirtyRect,
        const nsRect& aBorderArea, nsIFrame* aFrame, uint32_t aPaintFlags,
        int32_t aLayer, CompositionOp aCompositionOp = CompositionOp::OP_OVER,
        float aOpacity = 1.0);

   private:
    PaintBGParams(nsPresContext& aPresCtx, const nsRect& aDirtyRect,
                  const nsRect& aBorderArea, nsIFrame* aFrame,
                  uint32_t aPaintFlags, int32_t aLayer,
                  CompositionOp aCompositionOp, float aOpacity)
        : presCtx(aPresCtx),
          dirtyRect(aDirtyRect),
          borderArea(aBorderArea),
          frame(aFrame),
          paintFlags(aPaintFlags),
          layer(aLayer),
          compositionOp(aCompositionOp),
          opacity(aOpacity) {}
  };

  static ImgDrawResult PaintStyleImageLayer(const PaintBGParams& aParams,
                                            gfxContext& aRenderingCtx);

  /**
   * Same as |PaintStyleImageLayer|, except using the provided style structs.
   * This short-circuits the code that ensures that the root element's
   * {background|mask} is drawn on the canvas.
   * The aLayer parameter allows you to paint a single layer of the
   * {background|mask}.
   * The default value for aLayer, -1, means that all layers will be painted.
   * The background color will only be painted if the back-most layer is also
   * being painted and (aParams.paintFlags & PAINTBG_MASK_IMAGE) is false.
   * aCompositionOp is only respected if a single layer is specified (aLayer !=
   * -1). If all layers are painted, the image layer's blend mode (or the mask
   * layer's composition mode) will be used.
   */
  static ImgDrawResult PaintStyleImageLayerWithSC(
      const PaintBGParams& aParams, gfxContext& aRenderingCtx,
      const mozilla::ComputedStyle* aBackgroundSC,
      const nsStyleBorder& aBorder);

  static bool CanBuildWebRenderDisplayItemsForStyleImageLayer(
      WebRenderLayerManager* aManager, nsPresContext& aPresCtx,
      nsIFrame* aFrame, const nsStyleBackground* aBackgroundStyle,
      int32_t aLayer, uint32_t aPaintFlags);
  static ImgDrawResult BuildWebRenderDisplayItemsForStyleImageLayer(
      const PaintBGParams& aParams, mozilla::wr::DisplayListBuilder& aBuilder,
      mozilla::wr::IpcResourceUpdateQueue& aResources,
      const mozilla::layers::StackingContextHelper& aSc,
      mozilla::layers::RenderRootStateManager* aManager,
      mozilla::nsDisplayItem* aItem);

  static ImgDrawResult BuildWebRenderDisplayItemsForStyleImageLayerWithSC(
      const PaintBGParams& aParams, mozilla::wr::DisplayListBuilder& aBuilder,
      mozilla::wr::IpcResourceUpdateQueue& aResources,
      const mozilla::layers::StackingContextHelper& aSc,
      mozilla::layers::RenderRootStateManager* aManager,
      mozilla::nsDisplayItem* aItem, mozilla::ComputedStyle* mBackgroundSC,
      const nsStyleBorder& aBorder);

  /**
   * Returns the rectangle covered by the given background layer image, taking
   * into account background positioning, sizing, and repetition, but not
   * clipping.
   */
  static nsRect GetBackgroundLayerRect(nsPresContext* aPresContext,
                                       nsIFrame* aForFrame,
                                       const nsRect& aBorderArea,
                                       const nsRect& aClipRect,
                                       const nsStyleImageLayers::Layer& aLayer,
                                       uint32_t aFlags);

  /** Called when we switch pres shells during painting. */
  static void PresShellChanged();

  // Draw a border segment in the table collapsing border model with beveling
  // corners.
  static void DrawTableBorderSegment(
      DrawTarget& aDrawTarget, mozilla::StyleBorderStyle aBorderStyle,
      nscolor aBorderColor, const nsRect& aBorderRect,
      int32_t aAppUnitsPerDevPixel, mozilla::Side aStartBevelSide,
      nscoord aStartBevelOffset, mozilla::Side aEndBevelSide,
      nscoord aEndBevelOffset);

  // A single border bevel.
  struct Bevel {
    mozilla::Side mSide;
    nscoord mOffset;
  };

  // A single solid beveled border segment.
  struct SolidBeveledBorderSegment {
    nsRect mRect;
    nscolor mColor;
    Bevel mStartBevel;
    Bevel mEndBevel;
  };

  // Collect the table border segments with beveling. Can't be called with
  // dashed / dotted borders, since we don't support beveling those.
  static void GetTableBorderSolidSegments(
      nsTArray<SolidBeveledBorderSegment>& aSegments,
      mozilla::StyleBorderStyle aBorderStyle, nscolor aBorderColor,
      const nsRect& aBorderRect, int32_t aAppUnitsPerDevPixel,
      mozilla::Side aStartBevelSide, nscoord aStartBevelOffset,
      mozilla::Side aEndBevelSide, nscoord aEndBevelOffset);

  // NOTE: pt, dirtyRect, lineSize, ascent, offset, insetLeft, and insetRight in
  //       the following structs are non-rounded device pixels, not app units.
  struct DecorationRectParams {
    // Checks if either start or end inset value is negative.
    // In that situation, text decoration will extend past the edges of the
    // text.
    bool HasNegativeInset() const {
      return insetLeft < 0.0 || insetRight < 0.0;
    }

    // The width [length] and the height [thickness] of the decoration
    // line. This is a "logical" size in textRun orientation, so that
    // for a vertical textrun, width will actually be a physical height;
    // and conversely, height will be a physical width.
    Size lineSize;
    // The default height [thickness] of the line given by the font metrics.
    // This is used for obtaining the correct offset for the decoration line
    // when CSS specifies a unique thickness for a text-decoration,
    // since the offset given by the font is derived from the font metric's
    // assumed line height
    Float defaultLineThickness = 0.0f;
    // The ascent of the text.
    Float ascent = 0.0f;
    // The offset of the decoration line from the baseline of the text
    // (if the value is positive, the line is lifted up).
    Float offset = 0.0f;
    // If descentLimit is zero or larger and the underline overflows
    // from the descent space, the underline should be lifted up as far
    // as possible.  Note that this does not mean the underline never
    // overflows from this limitation, because if the underline is
    // positioned to the baseline or upper, it causes unreadability.
    // Note that if this is zero or larger, the underline rect may be
    // shrunken if it's possible.  Therefore, this value is used for
    // strikeout line and overline too.
    Float descentLimit = -1.0f;
    // Which line will be painted. The value can be
    // UNDERLINE or OVERLINE or LINE_THROUGH.
    mozilla::StyleTextDecorationLine decoration =
        mozilla::StyleTextDecorationLine::UNDERLINE;
    // The style of the decoration line
    mozilla::StyleTextDecorationStyle style =
        mozilla::StyleTextDecorationStyle::None;
    // Decoration-line start/end inset values for this decoration.
    // The caller must ensure that the start/end values are translated to
    // left/right in the line-relative direction.
    Float insetLeft = 0.0f;
    Float insetRight = 0.0f;
    bool vertical = false;
    bool sidewaysLeft = false;
    gfxTextRun::Range glyphRange;
    gfxTextRun::PropertyProvider* provider = nullptr;
  };

  struct PaintDecorationLineParams : DecorationRectParams {
    // No need to paint outside this rect.
    Rect dirtyRect;
    // The top/left edge of the text.
    Point pt;
    // The color of the decoration line.
    nscolor color = NS_RGBA(0, 0, 0, 0);
    // The distance between the left edge of the given frame and the
    // position of the text as positioned without offset of the shadow.
    Float icoordInFrame = 0.0f;
    // Baseline offset being applied to this text (block-direction adjustment
    // applied to glyph positions when computing skip-ink intercepts).
    Float baselineOffset = 0.0f;
    // Whether text-decoration-skip-ink behavior is to be supported.
    bool allowInkSkipping = true;

    mozilla::StyleTextDecorationSkipInk skipInk =
        mozilla::StyleTextDecorationSkipInk::None;
  };

  /**
   * Function for painting the clipped decoration lines for the text.
   * Takes into account the rect clipping that occurs when
   * text-decoration-skip-ink is being used for underlines or overlines
   *
   *  input:
   *    @param aFrame            the frame which needs the decoration line
   *    @param aDrawTarget       the target/backend being drawn to
   *    @param aParams           the parameters for the decoration line
   *                             being drawn
   *    @param aRect             the rect representing the decoration line
   */
  static void PaintDecorationLineInternal(
      nsIFrame* aFrame, DrawTarget& aDrawTarget,
      const PaintDecorationLineParams& aParams, Rect aRect);

  /**
   * Function for painting the decoration lines for the text.
   *
   *   input:
   *     @param aFrame            the frame which needs the decoration line
   *     @param aDrawTarget       the target/backend being drawn to
   *     @param aParams           the parameters for the decoration line
   *                              being drawn
   */
  static void PaintDecorationLine(nsIFrame* aFrame, DrawTarget& aDrawTarget,
                                  const PaintDecorationLineParams& aParams);

  /**
   * Returns a Rect corresponding to the outline of the decoration line for the
   * given text metrics.  Arguments have the same meaning as for
   * PaintDecorationLine.  Currently this only works for solid
   * decorations; for other decoration styles the returned Rect will be empty.
   */
  static Rect DecorationLineToPath(const PaintDecorationLineParams& aParams);

  /**
   * Function for getting the decoration line rect for the text.
   * NOTE: aLineSize, aAscent and aOffset are non-rounded device pixels,
   *       not app units.
   *   input:
   *     @param aPresContext
   *   output:
   *     @return                  the decoration line rect for the input,
   *                              the each values are app units.
   */
  static nsRect GetTextDecorationRect(nsPresContext* aPresContext,
                                      const DecorationRectParams& aParams);

  static CompositionOp GetGFXBlendMode(mozilla::StyleBlend aBlendMode) {
    switch (aBlendMode) {
      case mozilla::StyleBlend::Normal:
        return CompositionOp::OP_OVER;
      case mozilla::StyleBlend::Multiply:
        return CompositionOp::OP_MULTIPLY;
      case mozilla::StyleBlend::Screen:
        return CompositionOp::OP_SCREEN;
      case mozilla::StyleBlend::Overlay:
        return CompositionOp::OP_OVERLAY;
      case mozilla::StyleBlend::Darken:
        return CompositionOp::OP_DARKEN;
      case mozilla::StyleBlend::Lighten:
        return CompositionOp::OP_LIGHTEN;
      case mozilla::StyleBlend::ColorDodge:
        return CompositionOp::OP_COLOR_DODGE;
      case mozilla::StyleBlend::ColorBurn:
        return CompositionOp::OP_COLOR_BURN;
      case mozilla::StyleBlend::HardLight:
        return CompositionOp::OP_HARD_LIGHT;
      case mozilla::StyleBlend::SoftLight:
        return CompositionOp::OP_SOFT_LIGHT;
      case mozilla::StyleBlend::Difference:
        return CompositionOp::OP_DIFFERENCE;
      case mozilla::StyleBlend::Exclusion:
        return CompositionOp::OP_EXCLUSION;
      case mozilla::StyleBlend::Hue:
        return CompositionOp::OP_HUE;
      case mozilla::StyleBlend::Saturation:
        return CompositionOp::OP_SATURATION;
      case mozilla::StyleBlend::Color:
        return CompositionOp::OP_COLOR;
      case mozilla::StyleBlend::Luminosity:
        return CompositionOp::OP_LUMINOSITY;
      case mozilla::StyleBlend::PlusLighter:
        return CompositionOp::OP_ADD;
      default:
        MOZ_ASSERT(false);
        return CompositionOp::OP_OVER;
    }
  }

  static CompositionOp GetGFXCompositeMode(
      mozilla::StyleMaskComposite aCompositeMode) {
    switch (aCompositeMode) {
      case mozilla::StyleMaskComposite::Add:
        return CompositionOp::OP_OVER;
      case mozilla::StyleMaskComposite::Subtract:
        return CompositionOp::OP_OUT;
      case mozilla::StyleMaskComposite::Intersect:
        return CompositionOp::OP_IN;
      case mozilla::StyleMaskComposite::Exclude:
        return CompositionOp::OP_XOR;
      default:
        MOZ_ASSERT(false);
        return CompositionOp::OP_OVER;
    }
  }

 protected:
  static gfxRect GetTextDecorationRectInternal(
      const Point& aPt, const DecorationRectParams& aParams,
      bool aSnapToDevicePixels);

  /**
   * Returns inflated rect for painting a decoration line.
   * Complex style decoration lines should be painted from leftmost of nearest
   * ancestor block box because that makes better look of connection of lines
   * for different nodes.  ExpandPaintingRectForDecorationLine() returns
   * a rect for actual painting rect for the clipped rect.
   *
   * input:
   *     @param aFrame            the frame which needs the decoration line.
   *     @param aStyle            the style of the complex decoration line
   *     @param aClippedRect      the clipped rect for the decoration line.
   *                              in other words, visible area of the line.
   *     @param aICoordInFrame  the distance between inline-start edge of aFrame
   *                              and aClippedRect.pos.
   *     @param aCycleLength      the width of one cycle of the line style.
   */
  static Rect ExpandPaintingRectForDecorationLine(
      nsIFrame* aFrame, const mozilla::StyleTextDecorationStyle aStyle,
      const Rect& aClippedRect, const Float aICoordInFrame,
      const Float aCycleLength, bool aVertical);
};

/*
 * nsContextBoxBlur
 * Creates an 8-bit alpha channel context for callers to draw in, blurs the
 * contents of that context and applies it as a 1-color mask on a
 * different existing context. Uses gfxGaussianBlur as its back end.
 *
 * You must call Init() first to create a suitable temporary surface to draw
 * on.  You must then draw any desired content onto the given context, then
 * call DoPaint() to apply the blurred content as a single-color mask. You
 * can only call Init() once, so objects cannot be reused.
 *
 * This is very useful for creating drop shadows or silhouettes.
 */
class nsContextBoxBlur {
  typedef mozilla::gfx::sRGBColor sRGBColor;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::gfx::RectCornerRadii RectCornerRadii;

 public:
  enum { FORCE_MASK = 0x01 };
  /**
   * Prepares a gfxContext to draw on. Do not call this twice; if you want
   * to get the gfxContext again use GetContext().
   *
   * @param aRect                The coordinates of the surface to create.
   *                             All coordinates must be in app units.
   *                             This must not include the blur radius, pass
   *                             it as the second parameter and everything
   *                             is taken care of.
   *
   * @param aBlurRadius          The blur radius in app units.
   *
   * @param aAppUnitsPerDevPixel The number of app units in a device pixel,
   *                             for conversion.  Most of the time you'll
   *                             pass this from the current PresContext if
   *                             available.
   *
   * @param aDestinationCtx      The graphics context to apply the blurred
   *                             mask to when you call DoPaint(). Make sure
   *                             it is not destroyed before you call
   *                             DoPaint(). To set the color of the
   *                             resulting blurred graphic mask, you must
   *                             set the color on this context before
   *                             calling Init().
   *
   * @param aDirtyRect           The absolute dirty rect in app units. Used to
   *                             optimize the temporary surface size and speed
   * up blur.
   *
   * @param aSkipRect            An area in device pixels (NOT app units!) to
   * avoid blurring over, to prevent unnecessary work.
   *
   * @param aFlags               FORCE_MASK to ensure that the content drawn to
   * the returned gfxContext is used as a mask, and not drawn directly to
   * aDestinationCtx.
   *
   * @return            A blank 8-bit alpha-channel-only graphics context to
   *                    draw on, or null on error. Must not be freed. The
   *                    context has a device offset applied to it given by
   *                    aRect. This means you can use coordinates as if it
   *                    were at the desired position at aRect and you don't
   *                    need to worry about translating any coordinates to
   *                    draw on this temporary surface.
   *
   * If aBlurRadius is 0, the returned context is aDestinationCtx and
   * DoPaint() does nothing, because no blurring is required. Therefore, you
   * should prepare the destination context as if you were going to draw
   * directly on it instead of any temporary surface created in this class.
   */
  gfxContext* Init(const nsRect& aRect, nscoord aSpreadRadius,
                   nscoord aBlurRadius, int32_t aAppUnitsPerDevPixel,
                   gfxContext* aDestinationCtx, const nsRect& aDirtyRect,
                   const gfxRect* aSkipRect, uint32_t aFlags = 0);

  /**
   * Does the actual blurring and mask applying. Users of this object *must*
   * have called Init() first, then have drawn whatever they want to be
   * blurred onto the internal gfxContext before calling this.
   */
  void DoPaint();

  /**
   * Gets the internal gfxContext at any time. Must not be freed. Avoid
   * calling this before calling Init() since the context would not be
   * constructed at that point.
   */
  gfxContext* GetContext();

  /**
   * Get the margin associated with the given blur radius, i.e., the
   * additional area that might be painted as a result of it.  (The
   * margin for a spread radius is itself, on all sides.)
   */
  static nsMargin GetBlurRadiusMargin(nscoord aBlurRadius,
                                      int32_t aAppUnitsPerDevPixel);

  /**
   * Blurs a coloured rectangle onto aDestinationCtx. This is equivalent
   * to calling Init(), drawing a rectangle onto the returned surface
   * and then calling DoPaint, but may let us optimize better in the
   * backend.
   *
   * @param aDestinationCtx      The destination to blur to.
   * @param aRect                The rectangle to blur in app units.
   * @param aAppUnitsPerDevPixel The number of app units in a device pixel,
   *                             for conversion.  Most of the time you'll
   *                             pass this from the current PresContext if
   *                             available.
   * @param aCornerRadii         Corner radii for aRect, if it is a rounded
   *                             rectangle.
   * @param aBlurRadius          The blur radius in app units.
   * @param aShadowColor         The color to draw the blurred shadow.
   * @param aDirtyRect           The absolute dirty rect in app units. Used to
   *                             optimize the temporary surface size and speed
   * up blur.
   * @param aSkipRect            An area in device pixels (NOT app units!) to
   * avoid blurring over, to prevent unnecessary work.
   */
  static void BlurRectangle(gfxContext* aDestinationCtx, const nsRect& aRect,
                            int32_t aAppUnitsPerDevPixel,
                            RectCornerRadii* aCornerRadii, nscoord aBlurRadius,
                            const sRGBColor& aShadowColor,
                            const nsRect& aDirtyRect, const gfxRect& aSkipRect);

  /**
   * Draws a blurred inset box shadow shape onto the destination surface.
   * Like BlurRectangle, this is equivalent to calling Init(),
   * drawing a rectangle onto the returned surface
   * and then calling DoPaint, but may let us optimize better in the
   * backend.
   *
   * @param aDestinationCtx      The destination to blur to.
   * @param aDestinationRect     The rectangle to blur in app units.
   * @param aShadowClipRect      The inside clip rect that creates the path.
   * @param aShadowColor         The color of the blur
   * @param aBlurRadiusAppUnits  The blur radius in app units
   * @param aSpreadRadiusAppUnits The spread radius in app units.
   * @param aAppUnitsPerDevPixel The number of app units in a device pixel,
   *                             for conversion.  Most of the time you'll
   *                             pass this from the current PresContext if
   *                             available.
   * @param aHasBorderRadius     If this inset box blur has a border radius
   * @param aInnerClipRectRadii  The clip rect radii used for the inside rect's
   * path.
   * @param aSkipRect            An area in device pixels (NOT app units!) to
   * avoid blurring over, to prevent unnecessary work.
   */
  bool InsetBoxBlur(gfxContext* aDestinationCtx,
                    mozilla::gfx::Rect aDestinationRect,
                    mozilla::gfx::Rect aShadowClipRect,
                    mozilla::gfx::sRGBColor& aShadowColor,
                    nscoord aBlurRadiusAppUnits, nscoord aSpreadRadiusAppUnits,
                    int32_t aAppUnitsPerDevPixel, bool aHasBorderRadius,
                    RectCornerRadii& aInnerClipRectRadii,
                    mozilla::gfx::Rect aSkipRect,
                    mozilla::gfx::Point aShadowOffset);

 protected:
  static void GetBlurAndSpreadRadius(DrawTarget* aDestDrawTarget,
                                     int32_t aAppUnitsPerDevPixel,
                                     nscoord aBlurRadius, nscoord aSpreadRadius,
                                     mozilla::gfx::IntSize& aOutBlurRadius,
                                     mozilla::gfx::IntSize& aOutSpreadRadius,
                                     bool aConstrainSpreadRadius = true);

  gfxGaussianBlur mGaussianBlur;
  mozilla::UniquePtr<gfxContext> mOwnedContext;
  gfxContext* mContext;  // may be either mOwnedContext or mDestinationContext
  gfxContext* mDestinationCtx;

  /* This is true if the blur already has it's content transformed
   * by mDestinationCtx's transform */
  bool mPreTransformed;
};

#endif /* nsCSSRendering_h_ */
