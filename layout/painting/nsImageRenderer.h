/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsImageRenderer_h_
#define nsImageRenderer_h_

#include "Units.h"
#include "mozilla/AspectRatio.h"
#include "mozilla/SurfaceFromElementResult.h"
#include "nsStyleStruct.h"

class gfxDrawable;

namespace mozilla {
class nsDisplayItem;

namespace layers {
class StackingContextHelper;
class WebRenderParentCommand;
class RenderRootStateManager;
}  // namespace layers

namespace wr {
class DisplayListBuilder;
class IpcResourceUpdateQueue;
}  // namespace wr

// A CSSSizeOrRatio represents a (possibly partially specified) size for use
// in computing image sizes. Either or both of the width and height might be
// given. A ratio of width to height may also be given. If we at least two
// of these then we can compute a concrete size, that is a width and height.
struct CSSSizeOrRatio {
  CSSSizeOrRatio()
      : mWidth(0), mHeight(0), mHasWidth(false), mHasHeight(false) {}

  bool CanComputeConcreteSize() const {
    return mHasWidth + mHasHeight + HasRatio() >= 2;
  }
  bool IsConcrete() const { return mHasWidth && mHasHeight; }
  bool HasRatio() const { return !!mRatio; }
  bool IsEmpty() const {
    return (mHasWidth && mWidth <= 0) || (mHasHeight && mHeight <= 0) ||
           !mRatio;
  }

  // CanComputeConcreteSize must return true when ComputeConcreteSize is
  // called.
  nsSize ComputeConcreteSize() const;

  void SetWidth(nscoord aWidth) {
    mWidth = aWidth;
    mHasWidth = true;
    if (mHasHeight) {
      mRatio = AspectRatio::FromSize(mWidth, mHeight);
    }
  }
  void SetHeight(nscoord aHeight) {
    mHeight = aHeight;
    mHasHeight = true;
    if (mHasWidth) {
      mRatio = AspectRatio::FromSize(mWidth, mHeight);
    }
  }
  void SetSize(const nsSize& aSize) {
    mWidth = aSize.width;
    mHeight = aSize.height;
    mHasWidth = true;
    mHasHeight = true;
    mRatio = AspectRatio::FromSize(mWidth, mHeight);
  }
  void SetRatio(const AspectRatio& aRatio) {
    MOZ_ASSERT(
        !mHasWidth || !mHasHeight,
        "Probably shouldn't be setting a ratio if we have a concrete size");
    mRatio = aRatio;
  }

  AspectRatio mRatio;
  nscoord mWidth;
  nscoord mHeight;
  bool mHasWidth;
  bool mHasHeight;
};

/**
 * This is a small wrapper class to encapsulate image drawing that can draw an
 * StyleImage image, which may internally be a real image, a sub image, or a CSS
 * gradient, etc...
 *
 * @note Always call the member functions in the order of PrepareImage(),
 * SetSize(), and Draw*().
 */
class nsImageRenderer {
 public:
  typedef mozilla::image::ImgDrawResult ImgDrawResult;
  typedef mozilla::layers::ImageContainer ImageContainer;

  enum {
    FLAG_SYNC_DECODE_IMAGES = 0x01,
    FLAG_PAINTING_TO_WINDOW = 0x02,
    FLAG_HIGH_QUALITY_SCALING = 0x04,
    FLAG_DRAW_PARTIAL_FRAMES = 0x08
  };
  enum FitType { CONTAIN, COVER };

  nsImageRenderer(nsIFrame* aForFrame, const mozilla::StyleImage* aImage,
                  uint32_t aFlags);
  ~nsImageRenderer() = default;
  /**
   * Populates member variables to get ready for rendering.
   * @return true iff the image is ready, and there is at least a pixel to
   * draw.
   */
  bool PrepareImage();

  /**
   * The three Compute*Size functions correspond to the sizing algorthms and
   * definitions from the CSS Image Values and Replaced Content spec. See
   * http://dev.w3.org/csswg/css-images-3/#sizing .
   */

  /**
   * Compute the intrinsic size of the image as defined in the CSS Image Values
   * spec. The intrinsic size is the unscaled size which the image would ideally
   * like to be in app units.
   */
  mozilla::CSSSizeOrRatio ComputeIntrinsicSize();

  /**
   * Computes the placement for a background image, or for the image data
   * inside of a replaced element.
   *
   * @param aPos The CSS <position> value that specifies the image's position.
   * @param aOriginBounds The box to which the tiling position should be
   *          relative. For background images, this should correspond to
   *          'background-origin' for the frame, except when painting on the
   *          canvas, in which case the origin bounds should be the bounds
   *          of the root element's frame. For a replaced element, this should
   *          be the element's content-box.
   * @param aTopLeft [out] The top-left corner where an image tile should be
   *          drawn.
   * @param aAnchorPoint [out] A point which should be pixel-aligned by
   *          nsLayoutUtils::DrawImage. This is the same as aTopLeft, unless
   *          CSS specifies a percentage (including 'right' or 'bottom'), in
   *          which case it's that percentage within of aOriginBounds. So
   *          'right' would set aAnchorPoint.x to aOriginBounds.XMost().
   *
   * Points are returned relative to aOriginBounds.
   */
  static void ComputeObjectAnchorPoint(const mozilla::Position& aPos,
                                       const nsSize& aOriginBounds,
                                       const nsSize& aImageSize,
                                       nsPoint* aTopLeft,
                                       nsPoint* aAnchorPoint);

  /**
   * Compute the size of the rendered image using either the 'cover' or
   * 'contain' constraints (aFitType).
   */
  static nsSize ComputeConstrainedSize(
      const nsSize& aConstrainingSize,
      const mozilla::AspectRatio& aIntrinsicRatio, FitType aFitType);
  /**
   * Compute the size of the rendered image (the concrete size) where no cover/
   * contain constraints are given. The 'default algorithm' from the CSS Image
   * Values spec.
   */
  static nsSize ComputeConcreteSize(
      const mozilla::CSSSizeOrRatio& aSpecifiedSize,
      const mozilla::CSSSizeOrRatio& aIntrinsicSize,
      const nsSize& aDefaultSize);

  /**
   * Set this image's preferred size. This will be its intrinsic size where
   * specified and the default size where it is not. Used as the unscaled size
   * when rendering the image.
   */
  void SetPreferredSize(const mozilla::CSSSizeOrRatio& aIntrinsicSize,
                        const nsSize& aDefaultSize);

  /**
   * Draws the image to the target rendering context using
   * {background|mask}-specific arguments.
   * @see nsLayoutUtils::DrawImage() for parameters.
   */
  ImgDrawResult DrawLayer(nsPresContext* aPresContext,
                          gfxContext& aRenderingContext, const nsRect& aDest,
                          const nsRect& aFill, const nsPoint& aAnchor,
                          const nsRect& aDirty, const nsSize& aRepeatSize,
                          float aOpacity);

  /**
   * Builds WebRender DisplayItems for an image using
   * {background|mask}-specific arguments.
   * @see nsLayoutUtils::DrawImage() for parameters.
   */
  ImgDrawResult BuildWebRenderDisplayItemsForLayer(
      nsPresContext* aPresContext, mozilla::wr::DisplayListBuilder& aBuilder,
      mozilla::wr::IpcResourceUpdateQueue& aResource,
      const mozilla::layers::StackingContextHelper& aSc,
      mozilla::layers::RenderRootStateManager* aManager, nsDisplayItem* aItem,
      const nsRect& aDest, const nsRect& aFill, const nsPoint& aAnchor,
      const nsRect& aDirty, const nsSize& aRepeatSize, float aOpacity);

  /**
   * Draw the image to a single component of a border-image style rendering.
   * aFill The destination rect to be drawn into
   * aSrc is the part of the image to be rendered into a tile (aUnitSize in
   * aFill), if aSrc and the dest tile are different sizes, the image will be
   * scaled to map aSrc onto the dest tile.
   * aHFill and aVFill are the repeat patterns for the component -
   * NS_STYLE_BORDER_IMAGE_REPEAT_*
   * aUnitSize The scaled size of a single source rect (in destination coords)
   * aIndex identifies the component: 0 1 2
   *                                  3 4 5
   *                                  6 7 8
   * aSVGViewportSize The image size evaluated by default sizing algorithm.
   * Pass Nothing() if we can read a valid viewport size or aspect-ratio from
   * the drawing image directly, otherwise, pass Some() with viewport size
   * evaluated from default sizing algorithm.
   * aHasIntrinsicRatio is used to record if the source image has fixed
   * intrinsic ratio.
   */
  ImgDrawResult DrawBorderImageComponent(
      nsPresContext* aPresContext, gfxContext& aRenderingContext,
      const nsRect& aDirtyRect, const nsRect& aFill,
      const mozilla::CSSIntRect& aSrc,
      mozilla::StyleBorderImageRepeatKeyword aHFill,
      mozilla::StyleBorderImageRepeatKeyword aVFill, const nsSize& aUnitSize,
      uint8_t aIndex, const mozilla::Maybe<CSSSize>& aSVGViewportSize,
      const bool aHasIntrinsicRatio);

  /**
   * Draw the image to aRenderingContext which can be used to define the
   * float area in the presence of "shape-outside: <image>".
   */
  ImgDrawResult DrawShapeImage(nsPresContext* aPresContext,
                               gfxContext& aRenderingContext);

  bool IsRasterImage() const;

  /// Retrieves the image associated with this nsImageRenderer, if there is one.
  already_AddRefed<imgIContainer> GetImage();

  bool IsReady() const { return mPrepareResult == ImgDrawResult::SUCCESS; }
  ImgDrawResult PrepareResult() const { return mPrepareResult; }
  void SetExtendMode(mozilla::gfx::ExtendMode aMode) { mExtendMode = aMode; }
  void SetMaskOp(mozilla::StyleMaskMode aMaskOp) { mMaskOp = aMaskOp; }
  const nsSize& GetSize() const { return mSize; }
  mozilla::StyleImage::Tag GetType() const { return mType; }
  const mozilla::StyleGradient* GetGradientData() const {
    return mGradientData;
  }

 private:
  /**
   * Draws the image to the target rendering context.
   * aSrc is a rect on the source image which will be mapped to aDest; it's
   * currently only used for gradients.
   *
   * @see nsLayoutUtils::DrawImage() for other parameters.
   */
  ImgDrawResult Draw(nsPresContext* aPresContext, gfxContext& aRenderingContext,
                     const nsRect& aDirtyRect, const nsRect& aDest,
                     const nsRect& aFill, const nsPoint& aAnchor,
                     const nsSize& aRepeatSize, const mozilla::CSSIntRect& aSrc,
                     float aOpacity = 1.0);

  /**
   * Builds WebRender DisplayItems for the image.
   * aSrc is a rect on the source image which will be mapped to aDest; it's
   * currently only used for gradients.
   *
   * @see nsLayoutUtils::DrawImage() for other parameters.
   */
  ImgDrawResult BuildWebRenderDisplayItems(
      nsPresContext* aPresContext, mozilla::wr::DisplayListBuilder& aBuilder,
      mozilla::wr::IpcResourceUpdateQueue& aResources,
      const mozilla::layers::StackingContextHelper& aSc,
      mozilla::layers::RenderRootStateManager* aManager, nsDisplayItem* aItem,
      const nsRect& aDirtyRect, const nsRect& aDest, const nsRect& aFill,
      const nsPoint& aAnchor, const nsSize& aRepeatSize,
      const mozilla::CSSIntRect& aSrc, float aOpacity = 1.0);

  /**
   * Helper method for creating a gfxDrawable from mPaintServerFrame or
   * mImageElementSurface.
   * Requires mType to be Element.
   * Returns null if we cannot create the drawable.
   */
  already_AddRefed<gfxDrawable> DrawableForElement(const nsRect& aImageRect,
                                                   gfxContext& aContext);

  nsIFrame* mForFrame;
  const mozilla::StyleImage* mImage;
  ImageResolution mImageResolution;
  mozilla::StyleImage::Tag mType;
  nsCOMPtr<imgIContainer> mImageContainer;
  const mozilla::StyleGradient* mGradientData;
  nsIFrame* mPaintServerFrame;
  SurfaceFromElementResult mImageElementSurface;
  ImgDrawResult mPrepareResult;
  nsSize mSize;  // unscaled size of the image, in app units
  uint32_t mFlags;
  mozilla::gfx::ExtendMode mExtendMode;
  mozilla::StyleMaskMode mMaskOp;
};

}  // namespace mozilla

#endif /* nsImageRenderer_h_ */
