/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for replaced elements with image data */

#ifndef nsImageFrame_h___
#define nsImageFrame_h___

#include "nsAtomicContainerFrame.h"
#include "nsIObserver.h"

#include "imgINotificationObserver.h"

#include "nsDisplayList.h"
#include "imgIContainer.h"
#include "mozilla/Attributes.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/StaticPtr.h"
#include "nsIReflowCallback.h"
#include "nsTObserverArray.h"

class nsFontMetrics;
class nsImageMap;
class nsIURI;
class nsILoadGroup;
class nsPresContext;
class nsImageFrame;
class nsTransform2D;
class nsImageLoadingContent;

namespace mozilla {
class nsDisplayImage;
class PresShell;
namespace layers {
class ImageContainer;
class LayerManager;
}  // namespace layers
}  // namespace mozilla

class nsImageListener final : public imgINotificationObserver {
 protected:
  virtual ~nsImageListener();

 public:
  explicit nsImageListener(nsImageFrame* aFrame);

  NS_DECL_ISUPPORTS
  NS_DECL_IMGINOTIFICATIONOBSERVER

  void SetFrame(nsImageFrame* frame) { mFrame = frame; }

 private:
  nsImageFrame* mFrame;
};

class nsImageFrame : public nsAtomicContainerFrame, public nsIReflowCallback {
 public:
  template <typename T>
  using Maybe = mozilla::Maybe<T>;
  using Nothing = mozilla::Nothing;
  using Visibility = mozilla::Visibility;

  typedef mozilla::image::ImgDrawResult ImgDrawResult;
  typedef mozilla::layers::ImageContainer ImageContainer;
  typedef mozilla::layers::LayerManager LayerManager;

  NS_DECL_FRAMEARENA_HELPERS(nsImageFrame)
  NS_DECL_QUERYFRAME

  void Destroy(DestroyContext&) override;
  void DidSetComputedStyle(ComputedStyle* aOldStyle) final;

  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;
  void BuildDisplayList(nsDisplayListBuilder*, const nsDisplayListSet&) final;

  nscoord IntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                         mozilla::IntrinsicISizeType aType) final;

  mozilla::IntrinsicSize GetIntrinsicSize() final { return mIntrinsicSize; }
  mozilla::AspectRatio GetIntrinsicRatio() const final {
    return mIntrinsicRatio;
  }
  void Reflow(nsPresContext*, ReflowOutput&, const ReflowInput&,
              nsReflowStatus&) override;
  bool IsLeafDynamic() const override;

  nsIContent* GetContentForEvent(const mozilla::WidgetEvent*) const final;
  nsresult HandleEvent(nsPresContext*, mozilla::WidgetGUIEvent*,
                       nsEventStatus*) override;
  Cursor GetCursor(const nsPoint&) override;
  nsresult AttributeChanged(int32_t aNameSpaceID, nsAtom* aAttribute,
                            int32_t aModType) final;

  void OnVisibilityChange(
      Visibility aNewVisibility,
      const Maybe<OnNonvisible>& aNonvisibleAction = Nothing()) final;

  void MarkIntrinsicISizesDirty() override;

  void ResponsiveContentDensityChanged();
  void ElementStateChanged(mozilla::dom::ElementState) override;
  void SetupOwnedRequest();
  void DeinitOwnedRequest();
  bool ShouldShowBrokenImageIcon() const;

  bool IsForImageLoadingContent() const {
    return mKind == Kind::ImageLoadingContent;
  }

  void UpdateXULImage();
  const mozilla::StyleImage* GetImageFromStyle() const;

#ifdef ACCESSIBILITY
  mozilla::a11y::AccType AccessibleType() override;
#endif

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override;
  void List(FILE* out = stderr, const char* aPrefix = "",
            ListFlags aFlags = ListFlags()) const final;
#endif

  LogicalSides GetLogicalSkipSides() const final;

  static void ReleaseGlobals();

  already_AddRefed<imgIRequest> GetCurrentRequest() const;
  void Notify(imgIRequest*, int32_t aType, const nsIntRect* aData);

  /**
   * Returns whether we should replace an element with an image corresponding to
   * its 'content' CSS property.
   */
  static bool ShouldCreateImageFrameForContentProperty(
      const mozilla::dom::Element&, const ComputedStyle&);

  /**
   * Function to test whether given an element and its style, that element
   * should get an image frame, and if so, which kind of image frame (for
   * `content`, or for the element itself).
   */
  enum class ImageFrameType {
    ForContentProperty,
    ForElementRequest,
    None,
  };
  static ImageFrameType ImageFrameTypeFor(const mozilla::dom::Element&,
                                          const ComputedStyle&);

  ImgDrawResult DisplayAltFeedback(gfxContext& aRenderingContext,
                                   const nsRect& aDirtyRect, nsPoint aPt,
                                   uint32_t aFlags);

  ImgDrawResult DisplayAltFeedbackWithoutLayer(
      nsDisplayItem*, mozilla::wr::DisplayListBuilder&,
      mozilla::wr::IpcResourceUpdateQueue&,
      const mozilla::layers::StackingContextHelper&,
      mozilla::layers::RenderRootStateManager*, nsDisplayListBuilder*,
      nsPoint aPt, uint32_t aFlags);

  /**
   * Return a map element associated with this image.
   */
  mozilla::dom::Element* GetMapElement() const;

  /**
   * Return true if the image has associated image map.
   */
  bool HasImageMap() const { return mImageMap || GetMapElement(); }

  nsImageMap* GetImageMap();
  nsImageMap* GetExistingImageMap() const { return mImageMap; }

  void AddInlineMinISize(const mozilla::IntrinsicSizeInput& aInput,
                         InlineMinISizeData* aData) final;

  void DisconnectMap();

  // nsIReflowCallback
  bool ReflowFinished() final;
  void ReflowCallbackCanceled() final;

  // The kind of image frame we are.
  enum class Kind : uint8_t {
    // For an nsImageLoadingContent.
    ImageLoadingContent,
    // For a <xul:image> element.
    XULImage,
    // For css 'content: url(..)' on non-generated content.
    ContentProperty,
    // For a child of a ::before / ::after pseudo-element that had an url() item
    // for the content property.
    ContentPropertyAtIndex,
    // For a list-style-image ::marker.
    ListStyleImage,
    // For a ::view-transition-old or ::view-transition-new pseudo-element.
    // Which one of the two is determined by the PseudoStyleType applying to us.
    ViewTransition,
  };

  // Creates a suitable continuing frame for this frame.
  nsImageFrame* CreateContinuingFrame(mozilla::PresShell*,
                                      ComputedStyle*) const;

  mozilla::AspectRatio ComputeIntrinsicRatioForImage(
      imgIContainer*, bool aIgnoreContainment = false) const;

 private:
  friend nsIFrame* NS_NewImageFrame(mozilla::PresShell*, ComputedStyle*);
  friend nsIFrame* NS_NewXULImageFrame(mozilla::PresShell*, ComputedStyle*);
  friend nsIFrame* NS_NewImageFrameForContentProperty(mozilla::PresShell*,
                                                      ComputedStyle*);
  friend nsIFrame* NS_NewImageFrameForGeneratedContentIndex(mozilla::PresShell*,
                                                            ComputedStyle*);
  friend nsIFrame* NS_NewImageFrameForListStyleImage(mozilla::PresShell*,
                                                     ComputedStyle*);
  friend nsIFrame* NS_NewImageFrameForViewTransition(mozilla::PresShell*,
                                                     ComputedStyle*);

  nsImageFrame(ComputedStyle* aStyle, nsPresContext* aPresContext, Kind aKind)
      : nsImageFrame(aStyle, aPresContext, kClassID, aKind) {}

  nsImageFrame(ComputedStyle*, nsPresContext* aPresContext, ClassID, Kind);

  void ReflowChildren(nsPresContext*, const ReflowInput&,
                      const mozilla::LogicalSize& aImageSize);

  void UpdateIntrinsicSizeAndRatio();

 protected:
  nsImageFrame(ComputedStyle* aStyle, nsPresContext* aPresContext, ClassID aID)
      : nsImageFrame(aStyle, aPresContext, aID, Kind::ImageLoadingContent) {}

  ~nsImageFrame() override;

  /**
   * Populate/update mIntrinsicSize and mIntrinsicSize if necessary.
   *
   * @param aConsiderIntrinsicsDirty if true, then this function will update
   *   mIntrinsicSize and mIntrinsicRatio *regardless* of what their current
   *   value is. (We'll still reason about whether the value changed or not
   *   when deciding whether additional notifications are needed.)  This param
   *   defaults to false, but it's used in MarkIntrinsicISizesDirty.
   */
  void EnsureIntrinsicSizeAndRatio(bool aConsiderIntrinsicsDirty = false);

  bool GotInitialReflow() const {
    return !HasAnyStateBits(NS_FRAME_FIRST_REFLOW);
  }

  SizeComputationResult ComputeSize(
      gfxContext* aRenderingContext, mozilla::WritingMode aWM,
      const mozilla::LogicalSize& aCBSize, nscoord aAvailableISize,
      const mozilla::LogicalSize& aMargin,
      const mozilla::LogicalSize& aBorderPadding,
      const mozilla::StyleSizeOverrides& aSizeOverrides,
      mozilla::ComputeSizeFlags aFlags) final;

  bool IsServerImageMap();

  // Translate a point that is relative to our frame into a localized CSS pixel
  // coordinate that is relative to the content area of this frame (inside the
  // border+padding).
  mozilla::CSSIntPoint TranslateEventCoords(const nsPoint&) const;

  bool GetAnchorHREFTargetAndNode(nsIURI** aHref, nsString& aTarget,
                                  nsIContent** aNode);
  /**
   * Computes the width of the string that fits into the available space
   *
   * @param in aLength total length of the string in PRUnichars
   * @param in aMaxWidth width not to be exceeded
   * @param out aMaxFit length of the string that fits within aMaxWidth
   *            in PRUnichars
   * @return width of the string that fits within aMaxWidth
   */
  nscoord MeasureString(const char16_t* aString, int32_t aLength,
                        nscoord aMaxWidth, uint32_t& aMaxFit,
                        gfxContext& aContext, nsFontMetrics& aFontMetrics);

  void DisplayAltText(nsPresContext* aPresContext,
                      gfxContext& aRenderingContext, const nsString& aAltText,
                      const nsRect& aRect);

  ImgDrawResult PaintImage(gfxContext& aRenderingContext, nsPoint aPt,
                           const nsRect& aDirtyRect, imgIContainer* aImage,
                           uint32_t aFlags);

  /**
   * If we're ready to decode - that is, if our current request's image is
   * available and our decoding heuristics are satisfied - then trigger a decode
   * for our image at the size we predict it will be drawn next time it's
   * painted.
   */
  void MaybeDecodeForPredictedSize();

 protected:
  friend class nsImageListener;
  friend class nsImageLoadingContent;
  friend class mozilla::PresShell;

  void OnSizeAvailable(imgIRequest* aRequest, imgIContainer* aImage);
  void OnFrameUpdate(imgIRequest* aRequest, const nsIntRect* aRect);
  void OnLoadComplete(imgIRequest* aRequest);
  mozilla::IntrinsicSize ComputeIntrinsicSize(
      bool aIgnoreContainment = false) const;
  // Whether the image frame should use the mapped aspect ratio from width=""
  // and height="".
  bool ShouldUseMappedAspectRatio() const;

  nsAtom* GetViewTransitionName() const;
  Maybe<nsSize> GetViewTransitionSnapshotSize() const;
  mozilla::wr::ImageKey GetViewTransitionImageKey(
      mozilla::layers::RenderRootStateManager*,
      mozilla::wr::IpcResourceUpdateQueue&) const;

  /**
   * Notification that aRequest will now be the current request.
   */
  void NotifyNewCurrentRequest(imgIRequest* aRequest);

  /// Always sync decode our image when painting if @aForce is true.
  void SetForceSyncDecoding(bool aForce) { mForceSyncDecoding = aForce; }

  void AssertSyncDecodingHintIsInSync() const
#ifndef DEBUG
      {}
#else
      ;
#endif

  /**
   * Computes the dest rect that we'll draw into, in app units, based upon the
   * provided frame content box. The result is not necessarily contained in the
   * frame content box.
   */
  nsRect GetDestRect(const nsRect& aFrameContentBox,
                     nsPoint* aAnchorPoint = nullptr);

 private:
  nscoord GetContinuationOffset() const;
  bool ShouldDisplaySelection();

  // Recalculate mIntrinsicSize from the image.
  bool UpdateIntrinsicSize();

  // Recalculate mIntrinsicRatio from the image.
  bool UpdateIntrinsicRatio();

  /**
   * This function calculates the transform for converting between
   * source space & destination space. May fail if our image has a
   * percent-valued or zero-valued height or width.
   *
   * @param aTransform The transform object to populate.
   *
   * @return whether we succeeded in creating the transform.
   */
  bool GetSourceToDestTransform(nsTransform2D& aTransform);

  /**
   * Helper function to check whether the request corresponds to a load we don't
   * care about.  Most of the decoder observer methods will bail early if this
   * returns true.
   */
  bool IsPendingLoad(imgIRequest*) const;

  /**
   * Updates mImage based on the current image request, and the image passed in
   * (both can be null), and invalidate layout and paint as needed.
   */
  void UpdateImage(imgIRequest*, imgIContainer*);

  /**
   * Function to convert a dirty rect in the source image to a dirty
   * rect for the image frame.
   */
  nsRect SourceRectToDest(const nsIntRect& aRect);

  /**
   * Triggers invalidation for both our image display item and, if appropriate,
   * our alt-feedback display item.
   *
   * @param aLayerInvalidRect The area to invalidate in layer space. If null,
   * the entire layer will be invalidated.
   * @param aFrameInvalidRect The area to invalidate in frame space. If null,
   * the entire frame will be invalidated.
   */
  void InvalidateSelf(const nsIntRect* aLayerInvalidRect,
                      const nsRect* aFrameInvalidRect);

  void MaybeSendIntrinsicSizeAndRatioToEmbedder();
  void MaybeSendIntrinsicSizeAndRatioToEmbedder(Maybe<mozilla::IntrinsicSize>,
                                                Maybe<mozilla::AspectRatio>);

  RefPtr<nsImageMap> mImageMap;

  RefPtr<nsImageListener> mListener;

  // An image request created for content: url(..), list-style-image, or
  // <xul:image>.
  RefPtr<imgRequestProxy> mOwnedRequest;

  nsCOMPtr<imgIContainer> mImage;
  nsCOMPtr<imgIContainer> mPrevImage;

  // The content-box size as if we are not fragmented, cached in the most recent
  // reflow.
  nsSize mComputedSize;

  mozilla::IntrinsicSize mIntrinsicSize;

  // Stores mImage's intrinsic ratio, or a default AspectRatio if there's no
  // intrinsic ratio.
  mozilla::AspectRatio mIntrinsicRatio;

  const Kind mKind;
  bool mOwnedRequestRegistered = false;
  bool mDisplayingIcon = false;
  bool mFirstFrameComplete = false;
  bool mReflowCallbackPosted = false;
  bool mForceSyncDecoding = false;
  bool mIsInObjectOrEmbed = false;

 public:
  friend class mozilla::nsDisplayImage;
  friend class nsDisplayGradient;
};

namespace mozilla {
/**
 * Note that nsDisplayImage does not receive events. However, an image element
 * is replaced content so its background will be z-adjacent to the
 * image itself, and hence receive events just as if the image itself
 * received events.
 */
class nsDisplayImage final : public nsPaintedDisplayItem {
 public:
  typedef mozilla::layers::LayerManager LayerManager;

  nsDisplayImage(nsDisplayListBuilder* aBuilder, nsImageFrame* aFrame)
      : nsPaintedDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayImage);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplayImage)

  void Paint(nsDisplayListBuilder*, gfxContext* aCtx) final;

  /**
   * @return The dest rect we'll use when drawing this image, in app units.
   *         Not necessarily contained in this item's bounds.
   */
  nsRect GetDestRect() const;

  nsRect GetBounds(bool* aSnap) const {
    *aSnap = true;
    return Frame()->GetContentRectRelativeToSelf() + ToReferenceFrame();
  }

  nsRect GetBounds(nsDisplayListBuilder*, bool* aSnap) const final {
    return GetBounds(aSnap);
  }

  nsRegion GetOpaqueRegion(nsDisplayListBuilder*, bool* aSnap) const final;

  bool CreateWebRenderCommands(mozilla::wr::DisplayListBuilder&,
                               mozilla::wr::IpcResourceUpdateQueue&,
                               const StackingContextHelper&,
                               mozilla::layers::RenderRootStateManager*,
                               nsDisplayListBuilder*) final;

  void MaybeCreateWebRenderCommandsForViewTransition(
      mozilla::wr::DisplayListBuilder&, mozilla::wr::IpcResourceUpdateQueue&,
      const StackingContextHelper&, mozilla::layers::RenderRootStateManager*,
      nsDisplayListBuilder*);

  nsImageFrame* Frame() const {
    MOZ_ASSERT(mFrame->IsImageFrame() || mFrame->IsImageControlFrame());
    return static_cast<nsImageFrame*>(mFrame);
  }

  NS_DISPLAY_DECL_NAME("Image", TYPE_IMAGE)
};

}  // namespace mozilla

#endif /* nsImageFrame_h___ */
