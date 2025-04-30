/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for replaced elements with image data */

#include "nsImageFrame.h"

#include "TextDrawTarget.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxUtils.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Encoding.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/dom/FetchPriority.h"
#include "mozilla/dom/ImageTracker.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Helpers.h"
#include "mozilla/gfx/PathHelpers.h"
#include "mozilla/dom/GeneratedImageContent.h"
#include "mozilla/dom/HTMLAreaElement.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/ResponsiveImageSelector.h"
#include "mozilla/dom/ViewTransition.h"
#include "mozilla/dom/LargestContentfulPaint.h"
#include "mozilla/image/WebRenderImageProvider.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/SVGImageContext.h"
#include "mozilla/Unused.h"

#include "nsCOMPtr.h"
#include "nsFontMetrics.h"
#include "nsIFrameInlines.h"
#include "nsIImageLoadingContent.h"
#include "nsImageLoadingContent.h"
#include "nsImageRenderer.h"
#include "nsObjectLoadingContent.h"
#include "nsString.h"
#include "nsPrintfCString.h"
#include "nsPresContext.h"
#include "nsGkAtoms.h"
#include "mozilla/dom/Document.h"
#include "nsContentUtils.h"
#include "nsCSSAnonBoxes.h"
#include "nsStyleConsts.h"
#include "nsStyleUtil.h"
#include "nsTransform2D.h"
#include "nsImageMap.h"
#include "nsILoadGroup.h"
#include "nsNetUtil.h"
#include "nsNetCID.h"
#include "nsCSSRendering.h"
#include "nsNameSpaceManager.h"
#include <algorithm>
#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif
#include "nsLayoutUtils.h"
#include "nsDisplayList.h"
#include "nsIContent.h"
#include "mozilla/dom/Selection.h"
#include "nsIURIMutator.h"

#include "imgIContainer.h"
#include "imgLoader.h"
#include "imgRequestProxy.h"

#include "nsCSSFrameConstructor.h"
#include "nsRange.h"

#include "nsError.h"
#include "nsBidiUtils.h"
#include "nsBidiPresUtils.h"

#include "gfxRect.h"
#include "ImageRegion.h"
#include "ImageContainer.h"
#include "mozilla/ServoStyleSet.h"
#include "nsBlockFrame.h"
#include "nsStyleStructInlines.h"

#include "mozilla/Preferences.h"

#include "mozilla/dom/Link.h"
#include "mozilla/dom/HTMLAnchorElement.h"
#include "mozilla/dom/BrowserChild.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::image;
using namespace mozilla::layers;

using mozilla::layout::TextDrawTarget;

static constexpr wr::ImageKey kNoKey{{0}, 0};

class nsDisplayGradient final : public nsPaintedDisplayItem {
 public:
  nsDisplayGradient(nsDisplayListBuilder* aBuilder, nsImageFrame* aFrame)
      : nsPaintedDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayGradient);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplayGradient)

  nsRect GetBounds(bool* aSnap) const {
    *aSnap = true;
    return Frame()->GetContentRectRelativeToSelf() + ToReferenceFrame();
  }

  nsRect GetBounds(nsDisplayListBuilder*, bool* aSnap) const final {
    return GetBounds(aSnap);
  }

  void Paint(nsDisplayListBuilder*, gfxContext* aCtx) final;

  bool CreateWebRenderCommands(wr::DisplayListBuilder&,
                               wr::IpcResourceUpdateQueue&,
                               const StackingContextHelper&,
                               layers::RenderRootStateManager*,
                               nsDisplayListBuilder*) final;

  NS_DISPLAY_DECL_NAME("Gradient", TYPE_GRADIENT)
};

void nsDisplayGradient::Paint(nsDisplayListBuilder* aBuilder,
                              gfxContext* aCtx) {
  auto* frame = static_cast<nsImageFrame*>(Frame());
  nsImageRenderer imageRenderer(frame, frame->GetImageFromStyle(),
                                aBuilder->GetImageRendererFlags());
  nsSize size = frame->GetSize();
  imageRenderer.SetPreferredSize({}, size);

  ImgDrawResult result;
  if (!imageRenderer.PrepareImage()) {
    result = imageRenderer.PrepareResult();
  } else {
    nsRect dest(ToReferenceFrame(), size);
    result = imageRenderer.DrawLayer(
        frame->PresContext(), *aCtx, dest, dest, dest.TopLeft(),
        GetPaintRect(aBuilder, aCtx), dest.Size(), /* aOpacity = */ 1.0f);
  }
  Unused << result;
}

bool nsDisplayGradient::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  auto* frame = static_cast<nsImageFrame*>(Frame());
  nsImageRenderer imageRenderer(frame, frame->GetImageFromStyle(),
                                aDisplayListBuilder->GetImageRendererFlags());
  nsSize size = frame->GetSize();
  imageRenderer.SetPreferredSize({}, size);

  ImgDrawResult result;
  if (!imageRenderer.PrepareImage()) {
    result = imageRenderer.PrepareResult();
  } else {
    nsRect dest(ToReferenceFrame(), size);
    result = imageRenderer.BuildWebRenderDisplayItemsForLayer(
        frame->PresContext(), aBuilder, aResources, aSc, aManager, this, dest,
        dest, dest.TopLeft(), dest, dest.Size(),
        /* aOpacity = */ 1.0f);
    if (result == ImgDrawResult::NOT_SUPPORTED) {
      return false;
    }
  }
  return true;
}

// sizes (pixels) for image icon, padding and border frame
#define ICON_SIZE (16)
#define ICON_PADDING (3)
#define ALT_BORDER_WIDTH (1)

// Default alignment value (so we can tell an unset value from a set value)
#define ALIGN_UNSET uint8_t(-1)

class BrokenImageIcon final : public imgINotificationObserver {
  // private class that wraps the data and logic needed for
  // broken image and loading image icons
 public:
  explicit BrokenImageIcon(const nsImageFrame& aFrame);
  static void Shutdown();

  NS_DECL_ISUPPORTS
  NS_DECL_IMGINOTIFICATIONOBSERVER

  static imgRequestProxy* GetImage(nsImageFrame* aFrame) {
    return Get(*aFrame).mImage.get();
  }

  static void AddObserver(nsImageFrame* aFrame) {
    auto& instance = Get(*aFrame);
    MOZ_ASSERT(!instance.mObservers.Contains(aFrame),
               "Observer shouldn't aleady be in array");
    instance.mObservers.AppendElement(aFrame);
  }

  static void RemoveObserver(nsImageFrame* aFrame) {
    auto& instance = Get(*aFrame);
    DebugOnly<bool> didRemove = instance.mObservers.RemoveElement(aFrame);
    MOZ_ASSERT(didRemove, "Observer not in array");
  }

 private:
  static BrokenImageIcon& Get(const nsImageFrame& aFrame) {
    if (!gSingleton) {
      gSingleton = new BrokenImageIcon(aFrame);
    }
    return *gSingleton;
  }

  ~BrokenImageIcon() = default;

  nsTObserverArray<nsImageFrame*> mObservers;
  RefPtr<imgRequestProxy> mImage;

  static StaticRefPtr<BrokenImageIcon> gSingleton;
};

StaticRefPtr<BrokenImageIcon> BrokenImageIcon::gSingleton;

NS_IMPL_ISUPPORTS(BrokenImageIcon, imgINotificationObserver)

BrokenImageIcon::BrokenImageIcon(const nsImageFrame& aFrame) {
  constexpr auto brokenSrc = u"resource://gre-resources/broken-image.png"_ns;
  nsCOMPtr<nsIURI> realURI;
  NS_NewURI(getter_AddRefs(realURI), brokenSrc);

  MOZ_ASSERT(realURI, "how?");
  if (NS_WARN_IF(!realURI)) {
    return;
  }

  nsPresContext* pc = aFrame.PresContext();
  Document* doc = pc->Document();
  RefPtr<imgLoader> il = nsContentUtils::GetImgLoaderForDocument(doc);

  nsCOMPtr<nsILoadGroup> loadGroup = doc->GetDocumentLoadGroup();

  // For icon loads, we don't need to merge with the loadgroup flags
  const nsLoadFlags loadFlags = nsIRequest::LOAD_NORMAL;
  const nsContentPolicyType contentPolicyType =
      nsIContentPolicy::TYPE_INTERNAL_IMAGE;

  nsresult rv =
      il->LoadImage(realURI, /* icon URI */
                    nullptr, /* initial document URI; this is only
                               relevant for cookies, so does not
                               apply to icons. */
                    nullptr, /* referrer (not relevant for icons) */
                    nullptr, /* principal (not relevant for icons) */
                    0, loadGroup, this, nullptr, /* No context */
                    nullptr, /* Not associated with any particular document */
                    loadFlags, nullptr, contentPolicyType, u""_ns,
                    false, /* aUseUrgentStartForChannel */
                    false, /* aLinkPreload */
                    0, FetchPriority::Auto, getter_AddRefs(mImage));
  Unused << NS_WARN_IF(NS_FAILED(rv));
}

void BrokenImageIcon::Shutdown() {
  if (!gSingleton) {
    return;
  }
  if (gSingleton->mImage) {
    gSingleton->mImage->CancelAndForgetObserver(NS_ERROR_FAILURE);
    gSingleton->mImage = nullptr;
  }
  gSingleton = nullptr;
}

void BrokenImageIcon::Notify(imgIRequest* aRequest, int32_t aType,
                             const nsIntRect* aData) {
  MOZ_ASSERT(aRequest);

  if (aType != imgINotificationObserver::LOAD_COMPLETE &&
      aType != imgINotificationObserver::FRAME_UPDATE) {
    return;
  }

  if (aType == imgINotificationObserver::LOAD_COMPLETE) {
    nsCOMPtr<imgIContainer> image;
    aRequest->GetImage(getter_AddRefs(image));
    if (!image) {
      return;
    }

    // Retrieve the image's intrinsic size.
    int32_t width = 0;
    int32_t height = 0;
    image->GetWidth(&width);
    image->GetHeight(&height);

    // Request a decode at that size.
    image->RequestDecodeForSize(IntSize(width, height),
                                imgIContainer::DECODE_FLAGS_DEFAULT |
                                    imgIContainer::FLAG_HIGH_QUALITY_SCALING);
  }

  for (nsImageFrame* frame : mObservers.ForwardRange()) {
    frame->InvalidateFrame();
  }
}

// test if the width and height are fixed, looking at the style data
// This is used by nsImageFrame::ImageFrameTypeFor and should not be used for
// layout decisions.
static bool HaveSpecifiedSize(const nsStylePosition* aStylePosition,
                              StylePositionProperty aProp) {
  // check the width and height values in the reflow input's style struct
  // - if width and height are specified as either coord or percentage, then
  //   the size of the image frame is constrained
  return aStylePosition->GetWidth(aProp)->IsLengthPercentage() &&
         aStylePosition->GetHeight(aProp)->IsLengthPercentage();
}

template <typename SizeOrMaxSize>
static bool DependsOnIntrinsicSize(const SizeOrMaxSize& aMinOrMaxSize) {
  auto length = nsIFrame::ToExtremumLength(aMinOrMaxSize);
  if (!length) {
    return false;
  }
  switch (*length) {
    case nsIFrame::ExtremumLength::MinContent:
    case nsIFrame::ExtremumLength::MaxContent:
    case nsIFrame::ExtremumLength::FitContent:
    case nsIFrame::ExtremumLength::FitContentFunction:
      return true;
    case nsIFrame::ExtremumLength::MozAvailable:
    case nsIFrame::ExtremumLength::Stretch:
      return false;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown sizing keyword?");
  return false;
}

// Decide whether we can optimize away reflows that result from the
// image's intrinsic size changing.
static bool SizeDependsOnIntrinsicSize(const ReflowInput& aReflowInput) {
  const auto& position = *aReflowInput.mStylePosition;
  const auto positionProperty = aReflowInput.mStyleDisplay->mPosition;
  WritingMode wm = aReflowInput.GetWritingMode();
  // Don't try to make this optimization when an image has percentages
  // in its 'width' or 'height'.  The percentages might be treated like
  // auto (especially for intrinsic width calculations and for heights).
  //
  // min-width: min-content and such can also affect our intrinsic size.
  // but note that those keywords on the block axis behave like auto, so we
  // don't need to check them.
  //
  // Flex item's min-[width|height]:auto resolution depends on intrinsic size.
  return !position.GetHeight(positionProperty)->ConvertsToLength() ||
         !position.GetWidth(positionProperty)->ConvertsToLength() ||
         DependsOnIntrinsicSize(*position.MinISize(wm, positionProperty)) ||
         DependsOnIntrinsicSize(*position.MaxISize(wm, positionProperty)) ||
         aReflowInput.mFrame->IsFlexItem();
}

nsIFrame* NS_NewImageFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new (aPresShell) nsImageFrame(aStyle, aPresShell->GetPresContext(),
                                       nsImageFrame::Kind::ImageLoadingContent);
}

nsIFrame* NS_NewXULImageFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new (aPresShell) nsImageFrame(aStyle, aPresShell->GetPresContext(),
                                       nsImageFrame::Kind::XULImage);
}

nsIFrame* NS_NewImageFrameForContentProperty(PresShell* aPresShell,
                                             ComputedStyle* aStyle) {
  return new (aPresShell) nsImageFrame(aStyle, aPresShell->GetPresContext(),
                                       nsImageFrame::Kind::ContentProperty);
}

nsIFrame* NS_NewImageFrameForGeneratedContentIndex(PresShell* aPresShell,
                                                   ComputedStyle* aStyle) {
  return new (aPresShell)
      nsImageFrame(aStyle, aPresShell->GetPresContext(),
                   nsImageFrame::Kind::ContentPropertyAtIndex);
}

nsIFrame* NS_NewImageFrameForListStyleImage(PresShell* aPresShell,
                                            ComputedStyle* aStyle) {
  return new (aPresShell) nsImageFrame(aStyle, aPresShell->GetPresContext(),
                                       nsImageFrame::Kind::ListStyleImage);
}

nsIFrame* NS_NewImageFrameForViewTransition(PresShell* aPresShell,
                                            ComputedStyle* aStyle) {
  return new (aPresShell) nsImageFrame(aStyle, aPresShell->GetPresContext(),
                                       nsImageFrame::Kind::ViewTransition);
}

bool nsImageFrame::ShouldShowBrokenImageIcon() const {
  // NOTE(emilio, https://github.com/w3c/csswg-drafts/issues/2832): WebKit and
  // Blink behave differently here for content: url(..), for now adapt to
  // Blink's behavior.
  if (mKind != Kind::ImageLoadingContent) {
    return false;
  }

  if (!StaticPrefs::browser_display_show_image_placeholders()) {
    return false;
  }

  // <img alt=""> is special, and it shouldn't draw the broken image icon,
  // unlike the no-alt attribute or non-empty-alt-attribute case.
  if (auto* image = HTMLImageElement::FromNode(mContent)) {
    const nsAttrValue* alt = image->GetParsedAttr(nsGkAtoms::alt);
    if (alt && alt->IsEmptyString()) {
      return false;
    }
  }

  // check for broken images. valid null images (eg. img src="") are
  // not considered broken because they have no image requests
  if (nsCOMPtr<imgIRequest> currentRequest = GetCurrentRequest()) {
    uint32_t imageStatus;
    return NS_SUCCEEDED(currentRequest->GetImageStatus(&imageStatus)) &&
           (imageStatus & imgIRequest::STATUS_ERROR);
  }

  nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(mContent);
  MOZ_ASSERT(imageLoader);
  // Show the broken image icon only if we've tried to perform a load at all
  // (that is, if we have a current uri).
  nsCOMPtr<nsIURI> currentURI = imageLoader->GetCurrentURI();
  return !!currentURI;
}

nsImageFrame* nsImageFrame::CreateContinuingFrame(
    mozilla::PresShell* aPresShell, ComputedStyle* aStyle) const {
  return new (aPresShell)
      nsImageFrame(aStyle, aPresShell->GetPresContext(), mKind);
}

NS_IMPL_FRAMEARENA_HELPERS(nsImageFrame)

nsImageFrame::nsImageFrame(ComputedStyle* aStyle, nsPresContext* aPresContext,
                           ClassID aID, Kind aKind)
    : nsAtomicContainerFrame(aStyle, aPresContext, aID),
      mIntrinsicSize(0, 0),
      mKind(aKind) {
  EnableVisibilityTracking();
}

nsImageFrame::~nsImageFrame() = default;

NS_QUERYFRAME_HEAD(nsImageFrame)
  NS_QUERYFRAME_ENTRY(nsImageFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsAtomicContainerFrame)

#ifdef ACCESSIBILITY
a11y::AccType nsImageFrame::AccessibleType() {
  if (mKind == Kind::ListStyleImage) {
    // This is an HTMLListBulletAccessible.
    return a11y::eNoType;
  }

  if (mKind == Kind::ViewTransition) {
    // View transitions don't show up in the a11y tree.
    return a11y::eNoType;
  }

  // Don't use GetImageMap() to avoid reentrancy into accessibility.
  if (HasImageMap()) {
    return a11y::eHTMLImageMapType;
  }

  return a11y::eImageType;
}
#endif

void nsImageFrame::DisconnectMap() {
  if (!mImageMap) {
    return;
  }

  mImageMap->Destroy();
  mImageMap = nullptr;

#ifdef ACCESSIBILITY
  if (nsAccessibilityService* accService = GetAccService()) {
    accService->RecreateAccessible(PresShell(), mContent);
  }
#endif
}

void nsImageFrame::Destroy(DestroyContext& aContext) {
  MaybeSendIntrinsicSizeAndRatioToEmbedder(Nothing(), Nothing());

  if (mReflowCallbackPosted) {
    PresShell()->CancelReflowCallback(this);
    mReflowCallbackPosted = false;
  }

  // Tell our image map, if there is one, to clean up
  // This causes the nsImageMap to unregister itself as
  // a DOM listener.
  DisconnectMap();

  MOZ_ASSERT(mListener);

  if (mKind == Kind::ImageLoadingContent) {
    MOZ_ASSERT(!mOwnedRequest);
    MOZ_ASSERT(!mOwnedRequestRegistered);
    nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(mContent);
    MOZ_ASSERT(imageLoader);

    // Notify our image loading content that we are going away so it can
    // deregister with our refresh driver.
    imageLoader->FrameDestroyed(this);
    imageLoader->RemoveNativeObserver(mListener);
  } else {
    DeinitOwnedRequest();
  }

  // set the frame to null so we don't send messages to a dead object.
  mListener->SetFrame(nullptr);
  mListener = nullptr;

  // If we were displaying an icon, take ourselves off the list
  if (mDisplayingIcon) {
    BrokenImageIcon::RemoveObserver(this);
  }

  nsAtomicContainerFrame::Destroy(aContext);
}

void nsImageFrame::DeinitOwnedRequest() {
  MOZ_ASSERT(mKind != Kind::ImageLoadingContent);
  if (!mOwnedRequest) {
    return;
  }
  PresContext()->Document()->ImageTracker()->Remove(mOwnedRequest);
  nsLayoutUtils::DeregisterImageRequest(PresContext(), mOwnedRequest,
                                        &mOwnedRequestRegistered);
  mOwnedRequest->CancelAndForgetObserver(NS_BINDING_ABORTED);
  mOwnedRequest = nullptr;
}

void nsImageFrame::DidSetComputedStyle(ComputedStyle* aOldStyle) {
  nsAtomicContainerFrame::DidSetComputedStyle(aOldStyle);

  // A list-style-image ::marker default size is calculated from the font's
  // em-size, which might have changed here.
  if (mKind == Kind::ListStyleImage) {
    UpdateIntrinsicSize();
  }

  // Normal "owned" images reframe when `content` or `list-style-image` change,
  // but XUL images don't (and we don't really need to). So deal with the
  // dynamic list-style-image change in that case.
  //
  // TODO(emilio): We might want to do the same for regular list-style-image or
  // even simple content: url() changes.
  if (mKind == Kind::XULImage && aOldStyle) {
    if (!mContent->AsElement()->HasNonEmptyAttr(nsGkAtoms::src) &&
        aOldStyle->StyleList()->mListStyleImage !=
            StyleList()->mListStyleImage) {
      UpdateXULImage();
    }
    // If we have no image our intrinsic size might be themed. We need to
    // update the size even if the effective appearance hasn't changed to
    // deal correctly with theme changes.
    if (!mOwnedRequest) {
      UpdateIntrinsicSize();
    }
  }

  // We need to update our orientation either if we had no ComputedStyle before
  // because this is the first time it's been set, or if the image-orientation
  // property changed from its previous value.
  bool shouldUpdateOrientation = false;
  nsCOMPtr<imgIRequest> currentRequest = GetCurrentRequest();
  const auto newOrientation =
      StyleVisibility()->UsedImageOrientation(currentRequest);
  if (mImage) {
    if (aOldStyle) {
      auto oldOrientation =
          aOldStyle->StyleVisibility()->UsedImageOrientation(currentRequest);
      shouldUpdateOrientation = oldOrientation != newOrientation;
    } else {
      shouldUpdateOrientation = true;
    }
  }

  if (shouldUpdateOrientation) {
    nsCOMPtr<imgIContainer> image(mImage->Unwrap());
    mImage = nsLayoutUtils::OrientImage(image, newOrientation);

    UpdateIntrinsicSize();
    UpdateIntrinsicRatio();
  } else if (!aOldStyle || aOldStyle->StylePosition()->mAspectRatio !=
                               StylePosition()->mAspectRatio) {
    UpdateIntrinsicRatio();
  }
}

static bool SizeIsAvailable(imgIRequest* aRequest) {
  if (!aRequest) {
    return false;
  }

  uint32_t imageStatus = 0;
  nsresult rv = aRequest->GetImageStatus(&imageStatus);
  return NS_SUCCEEDED(rv) && (imageStatus & imgIRequest::STATUS_SIZE_AVAILABLE);
}

const StyleImage* nsImageFrame::GetImageFromStyle() const {
  switch (mKind) {
    case Kind::ViewTransition:
      break;
    case Kind::ImageLoadingContent:
      break;
    case Kind::ListStyleImage:
      MOZ_ASSERT(
          GetParent()->GetContent()->IsGeneratedContentContainerForMarker());
      MOZ_ASSERT(mContent->IsHTMLElement(nsGkAtoms::mozgeneratedcontentimage));
      return &StyleList()->mListStyleImage;
    case Kind::XULImage:
      MOZ_ASSERT(!mContent->AsElement()->HasNonEmptyAttr(nsGkAtoms::src));
      return &StyleList()->mListStyleImage;
    case Kind::ContentProperty:
    case Kind::ContentPropertyAtIndex: {
      uint32_t contentIndex = 0;
      const nsStyleContent* styleContent = StyleContent();
      if (mKind == Kind::ContentPropertyAtIndex) {
        MOZ_RELEASE_ASSERT(
            mContent->IsHTMLElement(nsGkAtoms::mozgeneratedcontentimage));
        contentIndex =
            static_cast<GeneratedImageContent*>(mContent.get())->Index();

        // TODO(emilio): Consider inheriting the `content` property instead of
        // doing this parent traversal?
        nsIFrame* parent = GetParent();
        MOZ_DIAGNOSTIC_ASSERT(
            parent->GetContent()->IsGeneratedContentContainerForMarker() ||
            parent->GetContent()->IsGeneratedContentContainerForAfter() ||
            parent->GetContent()->IsGeneratedContentContainerForBefore());
        nsIFrame* nonAnonymousParent = parent;
        while (nonAnonymousParent->Style()->IsAnonBox()) {
          nonAnonymousParent = nonAnonymousParent->GetParent();
        }
        MOZ_DIAGNOSTIC_ASSERT(parent->GetContent() ==
                              nonAnonymousParent->GetContent());
        styleContent = nonAnonymousParent->StyleContent();
      }
      auto items = styleContent->NonAltContentItems();
      MOZ_RELEASE_ASSERT(contentIndex < items.Length());
      const auto& contentItem = items[contentIndex];
      MOZ_RELEASE_ASSERT(contentItem.IsImage());
      return &contentItem.AsImage();
    }
  }
  MOZ_ASSERT_UNREACHABLE("Don't call me");
  return nullptr;
}

void nsImageFrame::UpdateXULImage() {
  MOZ_ASSERT(mKind == Kind::XULImage);
  DeinitOwnedRequest();

  nsAutoString src;
  nsPresContext* pc = PresContext();
  if (mContent->AsElement()->GetAttr(nsGkAtoms::src, src) && !src.IsEmpty()) {
    nsContentPolicyType contentPolicyType;
    nsCOMPtr<nsIPrincipal> triggeringPrincipal;
    uint64_t requestContextID = 0;
    nsContentUtils::GetContentPolicyTypeForUIImageLoading(
        mContent, getter_AddRefs(triggeringPrincipal), contentPolicyType,
        &requestContextID);
    nsCOMPtr<nsIURI> uri;
    nsContentUtils::NewURIWithDocumentCharset(
        getter_AddRefs(uri), src, pc->Document(), mContent->GetBaseURI());
    if (uri) {
      auto referrerInfo = MakeRefPtr<ReferrerInfo>(*mContent->AsElement());
      nsContentUtils::LoadImage(
          uri, mContent, pc->Document(), triggeringPrincipal, requestContextID,
          referrerInfo, mListener, nsIRequest::LOAD_NORMAL, u""_ns,
          getter_AddRefs(mOwnedRequest), contentPolicyType);
      SetupOwnedRequest();
    }
  } else {
    const auto* image = GetImageFromStyle();
    if (image->IsImageRequestType()) {
      if (imgRequestProxy* proxy = image->GetImageRequest()) {
        proxy->Clone(mListener, pc->Document(), getter_AddRefs(mOwnedRequest));
        SetupOwnedRequest();
      }
    }
  }

  if (!mOwnedRequest) {
    UpdateImage(nullptr, nullptr);
  }
}

void nsImageFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                        nsIFrame* aPrevInFlow) {
  MOZ_ASSERT_IF(aPrevInFlow,
                aPrevInFlow->Type() == Type() &&
                    static_cast<nsImageFrame*>(aPrevInFlow)->mKind == mKind);

  nsAtomicContainerFrame::Init(aContent, aParent, aPrevInFlow);

  mListener = new nsImageListener(this);

  GetImageMap();  // Ensure to init the image map asap. This is important to
                  // make <area> elements focusable.

  if (StaticPrefs::layout_image_eager_broken_image_icon()) {
    Unused << BrokenImageIcon::GetImage(this);
  }

  nsPresContext* pc = PresContext();
  if (mKind == Kind::ImageLoadingContent) {
    nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(aContent);
    MOZ_ASSERT(imageLoader);
    imageLoader->AddNativeObserver(mListener);
    // We have a PresContext now, so we need to notify the image content node
    // that it can register images.
    imageLoader->FrameCreated(this);
    AssertSyncDecodingHintIsInSync();
    if (nsIDocShell* docShell = pc->GetDocShell()) {
      RefPtr<BrowsingContext> bc = docShell->GetBrowsingContext();
      mIsInObjectOrEmbed = bc->IsEmbedderTypeObjectOrEmbed() &&
                           pc->Document()->IsImageDocument();
    }
  } else if (mKind == Kind::XULImage) {
    UpdateXULImage();
  } else if (mKind == Kind::ViewTransition) {
    // View transitions have a surface directly.
  } else {
    const StyleImage* image = GetImageFromStyle();
    if (image->IsImageRequestType()) {
      if (imgRequestProxy* proxy = image->GetImageRequest()) {
        proxy->Clone(mListener, pc->Document(), getter_AddRefs(mOwnedRequest));
        SetupOwnedRequest();
      }
    }
  }

  // Give image loads associated with an image frame a small priority boost.
  if (nsCOMPtr<imgIRequest> currentRequest = GetCurrentRequest()) {
    uint32_t categoryToBoostPriority = imgIRequest::CATEGORY_FRAME_INIT;

    // Increase load priority further if intrinsic size might be important for
    // layout.
    if (!HaveSpecifiedSize(StylePosition(), StyleDisplay()->mPosition)) {
      categoryToBoostPriority |= imgIRequest::CATEGORY_SIZE_QUERY;
    }

    currentRequest->BoostPriority(categoryToBoostPriority);
  }

  MaybeSendIntrinsicSizeAndRatioToEmbedder();
}

void nsImageFrame::SetupOwnedRequest() {
  MOZ_ASSERT(mKind != Kind::ImageLoadingContent);
  if (!mOwnedRequest) {
    return;
  }

  // We're not using AssociateRequestToFrame for the content property, so we
  // need to add it to the image tracker manually.
  PresContext()->Document()->ImageTracker()->Add(mOwnedRequest);

  uint32_t status = 0;
  nsresult rv = mOwnedRequest->GetImageStatus(&status);
  if (NS_FAILED(rv)) {
    return;
  }

  if (status & imgIRequest::STATUS_SIZE_AVAILABLE) {
    nsCOMPtr<imgIContainer> image;
    mOwnedRequest->GetImage(getter_AddRefs(image));
    OnSizeAvailable(mOwnedRequest, image);
  }

  if (status & imgIRequest::STATUS_FRAME_COMPLETE) {
    mFirstFrameComplete = true;
  }

  if (status & imgIRequest::STATUS_IS_ANIMATED) {
    nsLayoutUtils::RegisterImageRequest(PresContext(), mOwnedRequest,
                                        &mOwnedRequestRegistered);
  }
}

static void ScaleIntrinsicSizeForDensity(IntrinsicSize& aSize,
                                         const ImageResolution& aResolution) {
  if (aSize.width) {
    aResolution.ApplyXTo(aSize.width.ref());
  }
  if (aSize.height) {
    aResolution.ApplyYTo(aSize.height.ref());
  }
}

static void ScaleIntrinsicSizeForDensity(imgIContainer* aImage,
                                         nsIContent& aContent,
                                         IntrinsicSize& aSize) {
  ImageResolution resolution = aImage->GetResolution();
  if (auto* image = HTMLImageElement::FromNode(aContent)) {
    if (auto* selector = image->GetResponsiveImageSelector()) {
      resolution.ScaleBy(selector->GetSelectedImageDensity());
    }
  }
  ScaleIntrinsicSizeForDensity(aSize, resolution);
}

static nscoord ListImageDefaultLength(const nsImageFrame& aFrame) {
  // https://drafts.csswg.org/css-lists-3/#image-markers
  // The spec says we should use 1em x 1em, but that seems too large.
  // See disussion in https://github.com/w3c/csswg-drafts/issues/4207
  auto* pc = aFrame.PresContext();
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForComputedStyle(aFrame.Style(), pc);
  RefPtr<gfxFont> font = fm->GetThebesFontGroup()->GetFirstValidFont();
  auto emAU =
      font->GetMetrics(fm->Orientation()).emHeight * pc->AppUnitsPerDevPixel();
  return std::max(NSToCoordRound(0.4f * emAU),
                  nsPresContext::CSSPixelsToAppUnits(1));
}

IntrinsicSize nsImageFrame::ComputeIntrinsicSize(
    bool aIgnoreContainment) const {
  const auto containAxes =
      aIgnoreContainment ? ContainSizeAxes(false, false) : GetContainSizeAxes();
  if (containAxes.IsBoth()) {
    return FinishIntrinsicSize(containAxes, IntrinsicSize(0, 0));
  }

  nsSize size;
  if (mImage && NS_SUCCEEDED(mImage->GetIntrinsicSize(&size))) {
    IntrinsicSize intrinsicSize;
    intrinsicSize.width = size.width == -1 ? Nothing() : Some(size.width);
    intrinsicSize.height = size.height == -1 ? Nothing() : Some(size.height);
    if (mKind == nsImageFrame::Kind::ListStyleImage) {
      if (intrinsicSize.width.isNothing() || intrinsicSize.height.isNothing()) {
        nscoord defaultLength = ListImageDefaultLength(*this);
        if (intrinsicSize.width.isNothing()) {
          intrinsicSize.width = Some(defaultLength);
        }
        if (intrinsicSize.height.isNothing()) {
          intrinsicSize.height = Some(defaultLength);
        }
      }
    }
    if (mKind == nsImageFrame::Kind::ImageLoadingContent ||
        (mKind == nsImageFrame::Kind::XULImage &&
         GetContent()->AsElement()->HasNonEmptyAttr(nsGkAtoms::src))) {
      ScaleIntrinsicSizeForDensity(mImage, *GetContent(), intrinsicSize);
    } else {
      ScaleIntrinsicSizeForDensity(
          intrinsicSize, GetImageFromStyle()->GetResolution(*Style()));
    }
    return FinishIntrinsicSize(containAxes, intrinsicSize);
  }

  if (auto size = GetViewTransitionSnapshotSize()) {
    IntrinsicSize intrinsicSize;
    intrinsicSize.width.emplace(size->width);
    intrinsicSize.height.emplace(size->height);
    return FinishIntrinsicSize(containAxes, intrinsicSize);
  }

  if (mKind == nsImageFrame::Kind::ListStyleImage) {
    // Note: images are handled above, this handles gradients etc.
    const nscoord defaultLength = ListImageDefaultLength(*this);
    return FinishIntrinsicSize(containAxes,
                               IntrinsicSize(defaultLength, defaultLength));
  }

  if (mKind == nsImageFrame::Kind::XULImage && IsThemed()) {
    nsPresContext* pc = PresContext();
    // FIXME: const_cast here is a bit evil but IsThemed and so does the same.
    const auto widgetSize = pc->Theme()->GetMinimumWidgetSize(
        pc, const_cast<nsImageFrame*>(this),
        StyleDisplay()->EffectiveAppearance());
    const IntrinsicSize intrinsicSize(
        LayoutDeviceIntSize::ToAppUnits(widgetSize, pc->AppUnitsPerDevPixel()));
    return FinishIntrinsicSize(containAxes, intrinsicSize);
  }

  if (ShouldShowBrokenImageIcon()) {
    nscoord edgeLengthToUse = nsPresContext::CSSPixelsToAppUnits(
        ICON_SIZE + (2 * (ICON_PADDING + ALT_BORDER_WIDTH)));
    return FinishIntrinsicSize(containAxes,
                               IntrinsicSize(edgeLengthToUse, edgeLengthToUse));
  }

  if (ShouldUseMappedAspectRatio() &&
      StylePosition()->mAspectRatio.HasRatio()) {
    return IntrinsicSize();
  }

  // XXX: No FinishIntrinsicSize?
  return IntrinsicSize(0, 0);
}

// For compat reasons, see bug 1602047, we don't use the intrinsic ratio from
// width="" and height="" for images with no src attribute (no request).
//
// But we shouldn't get fooled by <img loading=lazy>. We do want to apply the
// ratio then...
bool nsImageFrame::ShouldUseMappedAspectRatio() const {
  if (mKind != Kind::ImageLoadingContent) {
    return true;
  }
  nsCOMPtr<imgIRequest> currentRequest = GetCurrentRequest();
  if (currentRequest) {
    return true;
  }
  // TODO(emilio): Investigate the compat situation of the above check, maybe we
  // can just check for empty src attribute or something...
  auto* image = HTMLImageElement::FromNode(mContent);
  return image && image->IsAwaitingLoadOrLazyLoading();
}

bool nsImageFrame::UpdateIntrinsicSize() {
  IntrinsicSize oldIntrinsicSize = mIntrinsicSize;
  mIntrinsicSize = ComputeIntrinsicSize();
  return mIntrinsicSize != oldIntrinsicSize;
}

nsAtom* nsImageFrame::GetViewTransitionName() const {
  if (mKind != Kind::ViewTransition) {
    return nullptr;
  }
  MOZ_ASSERT(GetContent()->AsElement()->HasName());
  return GetContent()
      ->AsElement()
      ->GetParsedAttr(nsGkAtoms::name)
      ->GetAtomValue();
}

Maybe<nsSize> nsImageFrame::GetViewTransitionSnapshotSize() const {
  auto* name = GetViewTransitionName();
  if (!name) {
    return {};
  }
  auto* vt = PresContext()->Document()->GetActiveViewTransition();
  if (NS_WARN_IF(!vt)) {
    return {};
  }
  return Style()->GetPseudoType() == PseudoStyleType::viewTransitionOld
             ? vt->GetOldSize(name)
             : vt->GetNewSize(name);
}

wr::ImageKey nsImageFrame::GetViewTransitionImageKey(
    layers::RenderRootStateManager* aManager,
    wr::IpcResourceUpdateQueue& aResources) const {
  auto* name = GetViewTransitionName();
  if (!name) {
    return kNoKey;
  }
  auto* vt = PresContext()->Document()->GetActiveViewTransition();
  if (NS_WARN_IF(!vt)) {
    return kNoKey;
  }
  const auto* key =
      Style()->GetPseudoType() == PseudoStyleType::viewTransitionOld
          ? vt->GetOldImageKey(name, aManager, aResources)
          : vt->GetNewImageKey(name);
  return key ? *key : kNoKey;
}

AspectRatio nsImageFrame::ComputeIntrinsicRatioForImage(
    imgIContainer* aImage, bool aIgnoreContainment) const {
  if (!aIgnoreContainment && GetContainSizeAxes().IsAny()) {
    return AspectRatio();
  }

  if (aImage) {
    if (AspectRatio fromImage = aImage->GetIntrinsicRatio()) {
      return fromImage;
    }
  }

  if (auto size = GetViewTransitionSnapshotSize()) {
    return AspectRatio::FromSize(*size);
  }

  if (ShouldUseMappedAspectRatio()) {
    const StyleAspectRatio& ratio = StylePosition()->mAspectRatio;
    if (ratio.auto_ && ratio.HasRatio()) {
      // Return the mapped intrinsic aspect ratio stored in
      // nsStylePosition::mAspectRatio.
      return ratio.ratio.AsRatio().ToLayoutRatio(UseBoxSizing::Yes);
    }
  }
  if (ShouldShowBrokenImageIcon()) {
    return AspectRatio(1.0f);
  }
  return AspectRatio();
}

bool nsImageFrame::UpdateIntrinsicRatio() {
  AspectRatio oldIntrinsicRatio = mIntrinsicRatio;
  mIntrinsicRatio = ComputeIntrinsicRatioForImage(mImage);
  return mIntrinsicRatio != oldIntrinsicRatio;
}

bool nsImageFrame::GetSourceToDestTransform(nsTransform2D& aTransform) {
  nsRect destRect = GetDestRect(GetContentRectRelativeToSelf());
  // Set the translation components, based on destRect
  // XXXbz does this introduce rounding errors because of the cast to
  // float?  Should we just manually add that stuff in every time
  // instead?
  aTransform.SetToTranslate(float(destRect.x), float(destRect.y));

  // NOTE(emilio): This intrinsicSize is not the same as the layout intrinsic
  // size (mIntrinsicSize), which can be scaled due to ResponsiveImageSelector,
  // see ScaleIntrinsicSizeForDensity.
  nsSize intrinsicSize;
  if (!mImage || !NS_SUCCEEDED(mImage->GetIntrinsicSize(&intrinsicSize)) ||
      intrinsicSize.IsEmpty()) {
    return false;
  }

  aTransform.SetScale(float(destRect.width) / float(intrinsicSize.width),
                      float(destRect.height) / float(intrinsicSize.height));
  return true;
}

// This function checks whether the given request is the current request for our
// mContent.
bool nsImageFrame::IsPendingLoad(imgIRequest* aRequest) const {
  // Default to pending load in case of errors
  if (mKind != Kind::ImageLoadingContent) {
    MOZ_ASSERT(aRequest == mOwnedRequest);
    return false;
  }

  nsCOMPtr<nsIImageLoadingContent> imageLoader(do_QueryInterface(mContent));
  MOZ_ASSERT(imageLoader);

  int32_t requestType = nsIImageLoadingContent::UNKNOWN_REQUEST;
  imageLoader->GetRequestType(aRequest, &requestType);

  return requestType != nsIImageLoadingContent::CURRENT_REQUEST;
}

nsRect nsImageFrame::SourceRectToDest(const nsIntRect& aRect) {
  // When scaling the image, row N of the source image may (depending on
  // the scaling function) be used to draw any row in the destination image
  // between floor(F * (N-1)) and ceil(F * (N+1)), where F is the
  // floating-point scaling factor.  The same holds true for columns.
  // So, we start by computing that bound without the floor and ceiling.

  nsRect r(nsPresContext::CSSPixelsToAppUnits(aRect.x - 1),
           nsPresContext::CSSPixelsToAppUnits(aRect.y - 1),
           nsPresContext::CSSPixelsToAppUnits(aRect.width + 2),
           nsPresContext::CSSPixelsToAppUnits(aRect.height + 2));

  nsTransform2D sourceToDest;
  if (!GetSourceToDestTransform(sourceToDest)) {
    // Failed to generate transform matrix. Return our whole content area,
    // to be on the safe side (since this method is used for generating
    // invalidation rects).
    return GetContentRectRelativeToSelf();
  }

  sourceToDest.TransformCoord(&r.x, &r.y, &r.width, &r.height);

  // Now, round the edges out to the pixel boundary.
  nscoord scale = nsPresContext::CSSPixelsToAppUnits(1);
  nscoord right = r.x + r.width;
  nscoord bottom = r.y + r.height;

  r.x -= (scale + (r.x % scale)) % scale;
  r.y -= (scale + (r.y % scale)) % scale;
  r.width = right + ((scale - (right % scale)) % scale) - r.x;
  r.height = bottom + ((scale - (bottom % scale)) % scale) - r.y;

  return r;
}

static bool ImageOk(ElementState aState) {
  return !aState.HasState(ElementState::BROKEN);
}

static bool HasAltText(const Element& aElement) {
  // We always return some alternate text for <input>, see
  // nsCSSFrameConstructor::GetAlternateTextFor.
  if (aElement.IsHTMLElement(nsGkAtoms::input)) {
    return true;
  }

  MOZ_ASSERT(aElement.IsHTMLElement(nsGkAtoms::img));
  return aElement.HasNonEmptyAttr(nsGkAtoms::alt);
}

bool nsImageFrame::ShouldCreateImageFrameForContentProperty(
    const Element& aElement, const ComputedStyle& aStyle) {
  if (aElement.IsRootOfNativeAnonymousSubtree()) {
    return false;
  }
  Span<const StyleContentItem> items =
      aStyle.StyleContent()->NonAltContentItems();
  return items.Length() == 1 && items[0].IsImage();
}

// Check if we want to use an image frame or just let the frame constructor make
// us into an inline, and if so, which kind of image frame should we create.
/* static */
auto nsImageFrame::ImageFrameTypeFor(const Element& aElement,
                                     const ComputedStyle& aStyle)
    -> ImageFrameType {
  if (ShouldCreateImageFrameForContentProperty(aElement, aStyle)) {
    // Prefer the content property, for compat reasons, see bug 1484928.
    return ImageFrameType::ForContentProperty;
  }

  if (ImageOk(aElement.State())) {
    // Image is fine or loading; do the image frame thing
    return ImageFrameType::ForElementRequest;
  }

  if (aStyle.StyleUIReset()->mMozForceBrokenImageIcon) {
    return ImageFrameType::ForElementRequest;
  }

  if (!HasAltText(aElement)) {
    return ImageFrameType::ForElementRequest;
  }

  // FIXME(emilio, bug 1788767): We definitely don't reframe when
  // HaveSpecifiedSize changes...
  if (aElement.OwnerDoc()->GetCompatibilityMode() == eCompatibility_NavQuirks &&
      HaveSpecifiedSize(aStyle.StylePosition(),
                        aStyle.StyleDisplay()->mPosition)) {
    return ImageFrameType::ForElementRequest;
  }

  return ImageFrameType::None;
}

void nsImageFrame::Notify(imgIRequest* aRequest, int32_t aType,
                          const nsIntRect* aRect) {
  if (aType == imgINotificationObserver::SIZE_AVAILABLE) {
    nsCOMPtr<imgIContainer> image;
    aRequest->GetImage(getter_AddRefs(image));
    return OnSizeAvailable(aRequest, image);
  }

  if (aType == imgINotificationObserver::FRAME_UPDATE) {
    return OnFrameUpdate(aRequest, aRect);
  }

  if (aType == imgINotificationObserver::FRAME_COMPLETE) {
    mFirstFrameComplete = true;
  }

  if (aType == imgINotificationObserver::IS_ANIMATED &&
      mKind != Kind::ImageLoadingContent) {
    nsLayoutUtils::RegisterImageRequest(PresContext(), mOwnedRequest,
                                        &mOwnedRequestRegistered);
  }

  if (aType == imgINotificationObserver::LOAD_COMPLETE) {
    LargestContentfulPaint::MaybeProcessImageForElementTiming(
        static_cast<imgRequestProxy*>(aRequest), GetContent()->AsElement());
    return OnLoadComplete(aRequest);
  }
}

void nsImageFrame::OnSizeAvailable(imgIRequest* aRequest,
                                   imgIContainer* aImage) {
  if (!aImage) {
    return;
  }

  /* Get requested animation policy from the pres context:
   *   normal = 0
   *   one frame = 1
   *   one loop = 2
   */
  aImage->SetAnimationMode(PresContext()->ImageAnimationMode());

  if (IsPendingLoad(aRequest)) {
    // We don't care
    return;
  }

  UpdateImage(aRequest, aImage);
}

void nsImageFrame::UpdateImage(imgIRequest* aRequest, imgIContainer* aImage) {
  if (SizeIsAvailable(aRequest)) {
    StyleImageOrientation orientation =
        StyleVisibility()->UsedImageOrientation(aRequest);
    // This is valid and for the current request, so update our stored image
    // container, orienting according to our style.
    mImage = nsLayoutUtils::OrientImage(aImage, orientation);
    MOZ_ASSERT(mImage);
  } else {
    // We no longer have a valid image, so release our stored image container.
    mImage = mPrevImage = nullptr;
    if (mKind == Kind::ListStyleImage) {
      auto* genContent = static_cast<GeneratedImageContent*>(GetContent());
      genContent->NotifyLoadFailed();
      // No need to continue below since the above state change will destroy
      // this frame.
      return;
    }
  }

  UpdateIntrinsicSizeAndRatio();

  if (!GotInitialReflow()) {
    return;
  }

  // We're going to need to repaint now either way.
  InvalidateFrame();
}

void nsImageFrame::OnFrameUpdate(imgIRequest* aRequest,
                                 const nsIntRect* aRect) {
  if (NS_WARN_IF(!aRect)) {
    return;
  }

  if (!GotInitialReflow()) {
    // Don't bother to do anything; we have a reflow coming up!
    return;
  }

  if (mFirstFrameComplete && !StyleVisibility()->IsVisible()) {
    return;
  }

  if (IsPendingLoad(aRequest)) {
    // We don't care
    return;
  }

  nsIntRect layerInvalidRect =
      mImage ? mImage->GetImageSpaceInvalidationRect(*aRect) : *aRect;

  if (layerInvalidRect.IsEqualInterior(GetMaxSizedIntRect())) {
    // Invalidate our entire area.
    InvalidateSelf(nullptr, nullptr);
    return;
  }

  nsRect frameInvalidRect = SourceRectToDest(layerInvalidRect);
  InvalidateSelf(&layerInvalidRect, &frameInvalidRect);
}

void nsImageFrame::InvalidateSelf(const nsIntRect* aLayerInvalidRect,
                                  const nsRect* aFrameInvalidRect) {
  // Check if WebRender has interacted with this frame. If it has
  // we need to let it know that things have changed.
  const auto type = DisplayItemType::TYPE_IMAGE;
  const auto providerId = mImage ? mImage->GetProviderId() : 0;
  if (WebRenderUserData::ProcessInvalidateForImage(this, type, providerId)) {
    return;
  }

  InvalidateLayer(type, aLayerInvalidRect, aFrameInvalidRect);

  if (!mFirstFrameComplete) {
    InvalidateLayer(DisplayItemType::TYPE_ALT_FEEDBACK, aLayerInvalidRect,
                    aFrameInvalidRect);
  }
}

void nsImageFrame::MaybeSendIntrinsicSizeAndRatioToEmbedder() {
  MaybeSendIntrinsicSizeAndRatioToEmbedder(Some(GetIntrinsicSize()),
                                           Some(GetAspectRatio()));
}

void nsImageFrame::MaybeSendIntrinsicSizeAndRatioToEmbedder(
    Maybe<IntrinsicSize> aIntrinsicSize, Maybe<AspectRatio> aIntrinsicRatio) {
  if (!mIsInObjectOrEmbed || !mImage) {
    return;
  }

  nsCOMPtr<nsIDocShell> docShell = PresContext()->GetDocShell();
  if (!docShell) {
    return;
  }

  BrowsingContext* bc = docShell->GetBrowsingContext();
  if (!bc) {
    return;
  }
  MOZ_ASSERT(bc->IsContentSubframe());

  if (bc->GetParent()->IsInProcess()) {
    if (Element* embedder = bc->GetEmbedderElement()) {
      if (nsCOMPtr<nsIObjectLoadingContent> olc = do_QueryInterface(embedder)) {
        static_cast<nsObjectLoadingContent*>(olc.get())
            ->SubdocumentIntrinsicSizeOrRatioChanged(aIntrinsicSize,
                                                     aIntrinsicRatio);
      } else {
        MOZ_ASSERT_UNREACHABLE("Got out of sync?");
      }
      return;
    }
  }

  if (BrowserChild* browserChild = BrowserChild::GetFrom(docShell)) {
    Unused << browserChild->SendIntrinsicSizeOrRatioChanged(aIntrinsicSize,
                                                            aIntrinsicRatio);
  }
}

void nsImageFrame::OnLoadComplete(imgIRequest* aRequest) {
  NotifyNewCurrentRequest(aRequest);
}

void nsImageFrame::ElementStateChanged(ElementState aStates) {
  if (!(aStates & ElementState::BROKEN)) {
    return;
  }
  if (mKind != Kind::ImageLoadingContent) {
    return;
  }
  if (!ImageOk(mContent->AsElement()->State())) {
    UpdateImage(nullptr, nullptr);
  }
}

void nsImageFrame::ResponsiveContentDensityChanged() {
  UpdateIntrinsicSizeAndRatio();
}

void nsImageFrame::UpdateIntrinsicSizeAndRatio() {
  bool intrinsicSizeOrRatioChanged = [&] {
    // NOTE(emilio): We intentionally want to call both functions and avoid
    // short-circuiting.
    bool intrinsicSizeChanged = UpdateIntrinsicSize();
    bool intrinsicRatioChanged = UpdateIntrinsicRatio();
    return intrinsicSizeChanged || intrinsicRatioChanged;
  }();

  if (!intrinsicSizeOrRatioChanged) {
    return;
  }

  // Our aspect-ratio property value changed, and an embedding <object> or
  // <embed> might care about that.
  MaybeSendIntrinsicSizeAndRatioToEmbedder();

  if (!GotInitialReflow()) {
    return;
  }

  // Now we need to reflow if we have an unconstrained size and have
  // already gotten the initial reflow.
  if (!HasAnyStateBits(IMAGE_SIZECONSTRAINED)) {
    PresShell()->FrameNeedsReflow(
        this, IntrinsicDirty::FrameAncestorsAndDescendants, NS_FRAME_IS_DIRTY);
  } else if (PresShell()->IsActive()) {
    // We've already gotten the initial reflow, and our size hasn't changed,
    // so we're ready to request a decode.
    MaybeDecodeForPredictedSize();
  }
}

void nsImageFrame::NotifyNewCurrentRequest(imgIRequest* aRequest) {
  nsCOMPtr<imgIContainer> image;
  aRequest->GetImage(getter_AddRefs(image));
#ifdef DEBUG
  uint32_t imgStatus;
  aRequest->GetImageStatus(&imgStatus);
  NS_ASSERTION(image || (imgStatus & imgIRequest::STATUS_ERROR),
               "Successful load with no container?");
#endif
  UpdateImage(aRequest, image);
}

void nsImageFrame::MaybeDecodeForPredictedSize() {
  // Check that we're ready to decode.
  if (!mImage) {
    return;  // Nothing to do yet.
  }

  if (mComputedSize.IsEmpty()) {
    return;  // We won't draw anything, so no point in decoding.
  }

  if (GetVisibility() != Visibility::ApproximatelyVisible) {
    return;  // We're not visible, so don't decode.
  }

  // OK, we're ready to decode. Compute the scale to the screen...
  mozilla::PresShell* presShell = PresShell();
  MatrixScales scale =
      ScaleFactor<UnknownUnits, UnknownUnits>(
          presShell->GetCumulativeResolution()) *
      nsLayoutUtils::GetTransformToAncestorScaleExcludingAnimated(this);
  auto resolutionToScreen = ViewAs<LayoutDeviceToScreenScale2D>(scale);

  // If we are in a remote browser, then apply scaling from ancestor browsers
  if (BrowserChild* browserChild = BrowserChild::GetFrom(presShell)) {
    resolutionToScreen =
        resolutionToScreen * ViewAs<ScreenToScreenScale2D>(
                                 browserChild->GetEffectsInfo().mRasterScale);
  }

  // ...and this frame's content box...
  const nsPoint offset =
      GetOffsetToCrossDoc(nsLayoutUtils::GetReferenceFrame(this));
  const nsRect frameContentBox = GetContentRectRelativeToSelf() + offset;

  // ...and our predicted dest rect...
  const int32_t factor = PresContext()->AppUnitsPerDevPixel();
  const LayoutDeviceRect destRect =
      LayoutDeviceRect::FromAppUnits(GetDestRect(frameContentBox), factor);

  // ...and use them to compute our predicted size in screen pixels.
  const ScreenSize predictedScreenSize = destRect.Size() * resolutionToScreen;
  const ScreenIntSize predictedScreenIntSize =
      RoundedToInt(predictedScreenSize);
  if (predictedScreenIntSize.IsEmpty()) {
    return;
  }

  // Determine the optimal image size to use.
  uint32_t flags = imgIContainer::FLAG_HIGH_QUALITY_SCALING |
                   imgIContainer::FLAG_ASYNC_NOTIFY;
  SamplingFilter samplingFilter =
      nsLayoutUtils::GetSamplingFilterForFrame(this);
  gfxSize gfxPredictedScreenSize =
      gfxSize(predictedScreenIntSize.width, predictedScreenIntSize.height);
  nsIntSize predictedImageSize = mImage->OptimalImageSizeForDest(
      gfxPredictedScreenSize, imgIContainer::FRAME_CURRENT, samplingFilter,
      flags);

  // Request a decode.
  mImage->RequestDecodeForSize(predictedImageSize, flags);
}

nsRect nsImageFrame::GetDestRect(const nsRect& aFrameContentBox,
                                 nsPoint* aAnchorPoint) {
  // Note: To get the "dest rect", we have to provide the "constraint rect"
  // (which is the content-box, with the effects of fragmentation undone).
  nsRect constraintRect(aFrameContentBox.TopLeft(), mComputedSize);
  constraintRect.y -= GetContinuationOffset();

  auto intrinsicSize = mIntrinsicSize;
  auto intrinsicRatio = mIntrinsicRatio;
  if (GetContainSizeAxes().IsAny()) {
    // Ignore containment for object-fit computations.
    const bool ignoreContainment = true;
    intrinsicSize = ComputeIntrinsicSize(ignoreContainment);
    intrinsicRatio = ComputeIntrinsicRatioForImage(mImage, ignoreContainment);
  }
  return nsLayoutUtils::ComputeObjectDestRect(constraintRect, intrinsicSize,
                                              intrinsicRatio, StylePosition(),
                                              aAnchorPoint);
}

void nsImageFrame::EnsureIntrinsicSizeAndRatio(bool aConsiderIntrinsicsDirty) {
  const auto containAxes = GetContainSizeAxes();
  if (containAxes.IsBoth()) {
    // If we have 'contain:size', then we have no intrinsic aspect ratio,
    // and the intrinsic size is determined by contain-intrinsic-size,
    // regardless of what our underlying image may think.
    mIntrinsicSize = FinishIntrinsicSize(containAxes, IntrinsicSize(0, 0));
    mIntrinsicRatio = AspectRatio();
    return;
  }

  // If mIntrinsicSize is set (i.e. anything besides (0,0)), then we assume that
  // our intrinsic size/ratio have been already computed and don't need
  // recomputing, *unless* the aConsiderIntrinsicsDirty param is set to true
  // (in which case our intrinsic size/ratio might be invalid).
  //
  // The fallback list-style-image marker size might have been set in
  // DidSetComputedStyle, and it might have changed since then.
  // TODO(emilio): We should remove that special case and add missing
  // invalidation if/where needed.
  if (!aConsiderIntrinsicsDirty && mIntrinsicSize != IntrinsicSize(0, 0) &&
      mKind != Kind::ListStyleImage) {
    return;
  }

  bool intrinsicSizeOrRatioChanged = UpdateIntrinsicSize();
  intrinsicSizeOrRatioChanged =
      UpdateIntrinsicRatio() || intrinsicSizeOrRatioChanged;

  if (intrinsicSizeOrRatioChanged) {
    // Our aspect-ratio property value changed, and an embedding <object> or
    // <embed> might care about that.
    MaybeSendIntrinsicSizeAndRatioToEmbedder();
  }
}

nsIFrame::SizeComputationResult nsImageFrame::ComputeSize(
    gfxContext* aRenderingContext, WritingMode aWM, const LogicalSize& aCBSize,
    nscoord aAvailableISize, const LogicalSize& aMargin,
    const LogicalSize& aBorderPadding, const StyleSizeOverrides& aSizeOverrides,
    ComputeSizeFlags aFlags) {
  EnsureIntrinsicSizeAndRatio();
  return {ComputeSizeWithIntrinsicDimensions(
              aRenderingContext, aWM, mIntrinsicSize, GetAspectRatio(), aCBSize,
              aMargin, aBorderPadding, aSizeOverrides, aFlags),
          AspectRatioUsage::None};
}

Element* nsImageFrame::GetMapElement() const {
  return IsForImageLoadingContent()
             ? nsImageLoadingContent::FindImageMap(mContent->AsElement())
             : nullptr;
}

// get the offset into the content area of the image where aImg starts if it is
// a continuation.
nscoord nsImageFrame::GetContinuationOffset() const {
  nscoord offset = 0;
  for (nsIFrame* f = GetPrevInFlow(); f; f = f->GetPrevInFlow()) {
    offset += f->GetContentRect().height;
  }
  NS_ASSERTION(offset >= 0, "bogus GetContentRect");
  return offset;
}

nscoord nsImageFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                     IntrinsicISizeType aType) {
  EnsureIntrinsicSizeAndRatio();
  return mIntrinsicSize.ISize(GetWritingMode()).valueOr(0);
}

void nsImageFrame::ReflowChildren(nsPresContext* aPresContext,
                                  const ReflowInput& aReflowInput,
                                  const LogicalSize& aImageSize) {
  for (nsIFrame* child : mFrames) {
    ReflowOutput childDesiredSize(aReflowInput);
    WritingMode wm = GetWritingMode();
    // Shouldn't be hard to support if we want, but why bother.
    MOZ_ASSERT(
        wm == child->GetWritingMode(),
        "We don't expect mismatched writing-modes in content we control");
    nsReflowStatus childStatus;

    LogicalPoint childOffset(wm);
    ReflowInput childReflowInput(aPresContext, aReflowInput, child, aImageSize);
    const nsSize containerSize = aImageSize.GetPhysicalSize(wm);
    ReflowChild(child, aPresContext, childDesiredSize, childReflowInput, wm,
                childOffset, containerSize, ReflowChildFlags::Default,
                childStatus);

    FinishReflowChild(child, aPresContext, childDesiredSize, &childReflowInput,
                      wm, childOffset, containerSize,
                      ReflowChildFlags::Default);
  }
}

void nsImageFrame::Reflow(nsPresContext* aPresContext, ReflowOutput& aMetrics,
                          const ReflowInput& aReflowInput,
                          nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsImageFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_FRAME_TRACE(
      NS_FRAME_TRACE_CALLS,
      ("enter nsImageFrame::Reflow: availSize=%d,%d",
       aReflowInput.AvailableWidth(), aReflowInput.AvailableHeight()));

  MOZ_ASSERT(HasAnyStateBits(NS_FRAME_IN_REFLOW), "frame is not in reflow");

  // see if we have a frozen size (i.e. a fixed width and height)
  if (!SizeDependsOnIntrinsicSize(aReflowInput)) {
    AddStateBits(IMAGE_SIZECONSTRAINED);
  } else {
    RemoveStateBits(IMAGE_SIZECONSTRAINED);
  }

  mComputedSize = aReflowInput.ComputedPhysicalSize();

  const auto wm = GetWritingMode();
  aMetrics.SetSize(wm, aReflowInput.ComputedSizeWithBorderPadding(wm));

  if (GetPrevInFlow()) {
    aMetrics.Width() = GetPrevInFlow()->GetSize().width;
    nscoord y = GetContinuationOffset();
    aMetrics.Height() -= y + aReflowInput.ComputedPhysicalBorderPadding().top;
    aMetrics.Height() = std::max(0, aMetrics.Height());
  }

  // we have to split images if we are:
  //  in Paginated mode, we need to have a constrained height, and have a height
  //  larger than our available height
  uint32_t loadStatus = imgIRequest::STATUS_NONE;
  if (nsCOMPtr<imgIRequest> currentRequest = GetCurrentRequest()) {
    currentRequest->GetImageStatus(&loadStatus);
  }

  if (aPresContext->IsPaginated() &&
      ((loadStatus & imgIRequest::STATUS_SIZE_AVAILABLE) ||
       HasAnyStateBits(IMAGE_SIZECONSTRAINED)) &&
      NS_UNCONSTRAINEDSIZE != aReflowInput.AvailableHeight() &&
      aMetrics.Height() > aReflowInput.AvailableHeight()) {
    // our desired height was greater than 0, so to avoid infinite
    // splitting, use 1 pixel as the min
    aMetrics.Height() = std::max(nsPresContext::CSSPixelsToAppUnits(1),
                                 aReflowInput.AvailableHeight());
    aStatus.SetIncomplete();
  }

  aMetrics.SetOverflowAreasToDesiredBounds();
  bool imageOK = mKind != Kind::ImageLoadingContent ||
                 ImageOk(mContent->AsElement()->State());

  // Determine if the size is available
  bool haveSize = false;
  if (loadStatus & imgIRequest::STATUS_SIZE_AVAILABLE) {
    haveSize = true;
  }

  if (!imageOK || !haveSize) {
    nsRect altFeedbackSize(
        0, 0,
        nsPresContext::CSSPixelsToAppUnits(
            ICON_SIZE + 2 * (ICON_PADDING + ALT_BORDER_WIDTH)),
        nsPresContext::CSSPixelsToAppUnits(
            ICON_SIZE + 2 * (ICON_PADDING + ALT_BORDER_WIDTH)));
    // We include the altFeedbackSize in our ink overflow, but not in our
    // scrollable overflow, since it doesn't really need to be scrolled to
    // outside the image.
    nsRect& inkOverflow = aMetrics.InkOverflow();
    inkOverflow.UnionRect(inkOverflow, altFeedbackSize);
  } else if (PresShell()->IsActive()) {
    // We've just reflowed and we should have an accurate size, so we're ready
    // to request a decode.
    MaybeDecodeForPredictedSize();
  }
  FinishAndStoreOverflow(&aMetrics, aReflowInput.mStyleDisplay);

  // Reflow the child frames. Our children can't affect our size in any way.
  ReflowChildren(aPresContext, aReflowInput, aMetrics.Size(GetWritingMode()));

  if (HasAnyStateBits(NS_FRAME_FIRST_REFLOW) && !mReflowCallbackPosted) {
    mReflowCallbackPosted = true;
    PresShell()->PostReflowCallback(this);
  }

  NS_FRAME_TRACE(NS_FRAME_TRACE_CALLS, ("exit nsImageFrame::Reflow: size=%d,%d",
                                        aMetrics.Width(), aMetrics.Height()));
}

bool nsImageFrame::ReflowFinished() {
  mReflowCallbackPosted = false;

  // XXX(seth): We don't need this. The purpose of updating visibility
  // synchronously is to ensure that animated images start animating
  // immediately. In the short term, however,
  // nsImageLoadingContent::OnUnlockedDraw() is enough to ensure that
  // animations start as soon as the image is painted for the first time, and in
  // the long term we want to update visibility information from the display
  // list whenever we paint, so we don't actually need to do this. However, to
  // avoid behavior changes during the transition from the old image visibility
  // code, we'll leave it in for now.
  UpdateVisibilitySynchronously();

  return false;
}

void nsImageFrame::ReflowCallbackCanceled() { mReflowCallbackPosted = false; }

// Computes the width of the specified string. aMaxWidth specifies the maximum
// width available. Once this limit is reached no more characters are measured.
// The number of characters that fit within the maximum width are returned in
// aMaxFit. NOTE: it is assumed that the fontmetrics have already been selected
// into the rendering context before this is called (for performance). MMP
nscoord nsImageFrame::MeasureString(const char16_t* aString, int32_t aLength,
                                    nscoord aMaxWidth, uint32_t& aMaxFit,
                                    gfxContext& aContext,
                                    nsFontMetrics& aFontMetrics) {
  nscoord totalWidth = 0;
  aFontMetrics.SetTextRunRTL(false);
  nscoord spaceWidth = aFontMetrics.SpaceWidth();

  aMaxFit = 0;
  while (aLength > 0) {
    // Find the next place we can line break
    uint32_t len = aLength;
    bool trailingSpace = false;
    for (int32_t i = 0; i < aLength; i++) {
      if (dom::IsSpaceCharacter(aString[i]) && (i > 0)) {
        len = i;  // don't include the space when measuring
        trailingSpace = true;
        break;
      }
    }

    // Measure this chunk of text, and see if it fits
    nscoord width = nsLayoutUtils::AppUnitWidthOfStringBidi(
        aString, len, this, aFontMetrics, aContext);
    bool fits = (totalWidth + width) <= aMaxWidth;

    // If it fits on the line, or it's the first word we've processed then
    // include it
    if (fits || (0 == totalWidth)) {
      // New piece fits
      totalWidth += width;

      // If there's a trailing space then see if it fits as well
      if (trailingSpace) {
        if ((totalWidth + spaceWidth) <= aMaxWidth) {
          totalWidth += spaceWidth;
        } else {
          // Space won't fit. Leave it at the end but don't include it in
          // the width
          fits = false;
        }

        len++;
      }

      aMaxFit += len;
      aString += len;
      aLength -= len;
    }

    if (!fits) {
      break;
    }
  }
  return totalWidth;
}

// Formats the alt-text to fit within the specified rectangle. Breaks lines
// between words if a word would extend past the edge of the rectangle
void nsImageFrame::DisplayAltText(nsPresContext* aPresContext,
                                  gfxContext& aRenderingContext,
                                  const nsString& aAltText,
                                  const nsRect& aRect) {
  // Set font and color
  aRenderingContext.SetColor(
      sRGBColor::FromABGR(StyleText()->mColor.ToColor()));
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetInflatedFontMetricsForFrame(this);

  // Format the text to display within the formatting rect

  nscoord maxAscent = fm->MaxAscent();
  nscoord maxDescent = fm->MaxDescent();
  nscoord lineHeight = fm->MaxHeight();  // line-relative, so an x-coordinate
                                         // length if writing mode is vertical

  WritingMode wm = GetWritingMode();
  bool isVertical = wm.IsVertical();

  fm->SetVertical(isVertical);
  fm->SetTextOrientation(StyleVisibility()->mTextOrientation);

  // XXX It would be nice if there was a way to have the font metrics tell
  // use where to break the text given a maximum width. At a minimum we need
  // to be able to get the break character...
  const char16_t* str = aAltText.get();
  int32_t strLen = aAltText.Length();
  nsPoint pt = wm.IsVerticalRL() ? aRect.TopRight() - nsPoint(lineHeight, 0)
                                 : aRect.TopLeft();
  nscoord iSize = isVertical ? aRect.height : aRect.width;

  if (!aPresContext->BidiEnabled() && HasRTLChars(aAltText)) {
    aPresContext->SetBidiEnabled();
  }

  // Always show the first line, even if we have to clip it below
  bool firstLine = true;
  while (strLen > 0) {
    if (!firstLine) {
      // If we've run out of space, break out of the loop
      if ((!isVertical && (pt.y + maxDescent) >= aRect.YMost()) ||
          (wm.IsVerticalRL() && (pt.x + maxDescent < aRect.x)) ||
          (wm.IsVerticalLR() && (pt.x + maxDescent >= aRect.XMost()))) {
        break;
      }
    }

    // Determine how much of the text to display on this line
    uint32_t maxFit;  // number of characters that fit
    nscoord strWidth =
        MeasureString(str, strLen, iSize, maxFit, aRenderingContext, *fm);

    // Display the text
    nsresult rv = NS_ERROR_FAILURE;

    if (aPresContext->BidiEnabled()) {
      mozilla::intl::BidiEmbeddingLevel level;
      nscoord x, y;

      if (isVertical) {
        x = pt.x + maxDescent;
        if (wm.IsBidiLTR()) {
          y = aRect.y;
          level = mozilla::intl::BidiEmbeddingLevel::LTR();
        } else {
          y = aRect.YMost() - strWidth;
          level = mozilla::intl::BidiEmbeddingLevel::RTL();
        }
      } else {
        y = pt.y + maxAscent;
        if (wm.IsBidiLTR()) {
          x = aRect.x;
          level = mozilla::intl::BidiEmbeddingLevel::LTR();
        } else {
          x = aRect.XMost() - strWidth;
          level = mozilla::intl::BidiEmbeddingLevel::RTL();
        }
      }

      rv = nsBidiPresUtils::RenderText(
          str, maxFit, level, aPresContext, aRenderingContext,
          aRenderingContext.GetDrawTarget(), *fm, x, y);
    }
    if (NS_FAILED(rv)) {
      nsLayoutUtils::DrawUniDirString(str, maxFit,
                                      isVertical
                                          ? nsPoint(pt.x + maxDescent, pt.y)
                                          : nsPoint(pt.x, pt.y + maxAscent),
                                      *fm, aRenderingContext);
    }

    // Move to the next line
    str += maxFit;
    strLen -= maxFit;
    if (wm.IsVerticalRL()) {
      pt.x -= lineHeight;
    } else if (wm.IsVerticalLR()) {
      pt.x += lineHeight;
    } else {
      pt.y += lineHeight;
    }

    firstLine = false;
  }
}

struct nsRecessedBorder : public nsStyleBorder {
  explicit nsRecessedBorder(nscoord aBorderWidth) {
    for (const auto side : AllPhysicalSides()) {
      BorderColorFor(side) = StyleColor::Black();
      mBorder.Side(side) = aBorderWidth;
      // Note: use SetBorderStyle here because we want to affect
      // mComputedBorder
      SetBorderStyle(side, StyleBorderStyle::Inset);
    }
  }
};

class nsDisplayAltFeedback final : public nsPaintedDisplayItem {
 public:
  nsDisplayAltFeedback(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame)
      : nsPaintedDisplayItem(aBuilder, aFrame) {}

  nsRect GetBounds(nsDisplayListBuilder* aBuilder, bool* aSnap) const final {
    *aSnap = false;
    return mFrame->InkOverflowRectRelativeToSelf() + ToReferenceFrame();
  }

  void Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) final {
    // Always sync decode, because these icons are UI, and since they're not
    // discardable we'll pay the price of sync decoding at most once.
    uint32_t flags = imgIContainer::FLAG_SYNC_DECODE;

    nsImageFrame* f = static_cast<nsImageFrame*>(mFrame);
    Unused << f->DisplayAltFeedback(*aCtx, GetPaintRect(aBuilder, aCtx),
                                    ToReferenceFrame(), flags);
  }

  bool CreateWebRenderCommands(
      wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
      const StackingContextHelper& aSc,
      layers::RenderRootStateManager* aManager,
      nsDisplayListBuilder* aDisplayListBuilder) final {
    // Always sync decode, because these icons are UI, and since they're not
    // discardable we'll pay the price of sync decoding at most once.
    uint32_t flags =
        imgIContainer::FLAG_SYNC_DECODE | imgIContainer::FLAG_ASYNC_NOTIFY;
    nsImageFrame* f = static_cast<nsImageFrame*>(mFrame);
    ImgDrawResult result = f->DisplayAltFeedbackWithoutLayer(
        this, aBuilder, aResources, aSc, aManager, aDisplayListBuilder,
        ToReferenceFrame(), flags);

    return result == ImgDrawResult::SUCCESS;
  }

  NS_DISPLAY_DECL_NAME("AltFeedback", TYPE_ALT_FEEDBACK)
};

ImgDrawResult nsImageFrame::DisplayAltFeedback(gfxContext& aRenderingContext,
                                               const nsRect& aDirtyRect,
                                               nsPoint aPt, uint32_t aFlags) {
  // Whether we draw the broken or loading icon.
  bool isLoading = mKind != Kind::ImageLoadingContent ||
                   ImageOk(mContent->AsElement()->State());

  // Calculate the content area.
  nsRect inner = GetContentRectRelativeToSelf() + aPt;

  // Display a recessed one pixel border
  nscoord borderEdgeWidth =
      nsPresContext::CSSPixelsToAppUnits(ALT_BORDER_WIDTH);

  // if inner area is empty, then make it big enough for at least the icon
  if (inner.IsEmpty()) {
    inner.SizeTo(2 * (nsPresContext::CSSPixelsToAppUnits(
                         ICON_SIZE + ICON_PADDING + ALT_BORDER_WIDTH)),
                 2 * (nsPresContext::CSSPixelsToAppUnits(
                         ICON_SIZE + ICON_PADDING + ALT_BORDER_WIDTH)));
  }

  // Make sure we have enough room to actually render the border within
  // our frame bounds
  if ((inner.width < 2 * borderEdgeWidth) ||
      (inner.height < 2 * borderEdgeWidth)) {
    return ImgDrawResult::SUCCESS;
  }

  // Paint the border
  if (!isLoading) {
    nsRecessedBorder recessedBorder(borderEdgeWidth);

    // Assert that we're not drawing a border-image here; if we were, we
    // couldn't ignore the ImgDrawResult that PaintBorderWithStyleBorder
    // returns.
    MOZ_ASSERT(recessedBorder.mBorderImageSource.IsNone());

    Unused << nsCSSRendering::PaintBorderWithStyleBorder(
        PresContext(), aRenderingContext, this, inner, inner, recessedBorder,
        mComputedStyle, PaintBorderFlags::SyncDecodeImages);
  }

  // Adjust the inner rect to account for the one pixel recessed border,
  // and a six pixel padding on each edge
  inner.Deflate(
      nsPresContext::CSSPixelsToAppUnits(ICON_PADDING + ALT_BORDER_WIDTH),
      nsPresContext::CSSPixelsToAppUnits(ICON_PADDING + ALT_BORDER_WIDTH));
  if (inner.IsEmpty()) {
    return ImgDrawResult::SUCCESS;
  }

  DrawTarget* drawTarget = aRenderingContext.GetDrawTarget();

  // Clip so we don't render outside the inner rect
  aRenderingContext.Save();
  aRenderingContext.Clip(NSRectToSnappedRect(
      inner, PresContext()->AppUnitsPerDevPixel(), *drawTarget));

  ImgDrawResult result = ImgDrawResult::SUCCESS;

  // Check if we should display image placeholders
  if (ShouldShowBrokenImageIcon()) {
    result = ImgDrawResult::NOT_READY;
    nscoord size = nsPresContext::CSSPixelsToAppUnits(ICON_SIZE);
    imgIRequest* request = BrokenImageIcon::GetImage(this);

    // If we weren't previously displaying an icon, register ourselves
    // as an observer for load and animation updates and flag that we're
    // doing so now.
    if (request && !mDisplayingIcon) {
      BrokenImageIcon::AddObserver(this);
      mDisplayingIcon = true;
    }

    WritingMode wm = GetWritingMode();
    bool flushRight = wm.IsPhysicalRTL();

    // If the icon in question is loaded, draw it.
    uint32_t imageStatus = 0;
    if (request) {
      request->GetImageStatus(&imageStatus);
    }
    if (imageStatus & imgIRequest::STATUS_LOAD_COMPLETE &&
        !(imageStatus & imgIRequest::STATUS_ERROR)) {
      nsCOMPtr<imgIContainer> imgCon;
      request->GetImage(getter_AddRefs(imgCon));
      MOZ_ASSERT(imgCon, "Load complete, but no image container?");
      nsRect dest(flushRight ? inner.XMost() - size : inner.x, inner.y, size,
                  size);
      result = nsLayoutUtils::DrawSingleImage(
          aRenderingContext, PresContext(), imgCon,
          nsLayoutUtils::GetSamplingFilterForFrame(this), dest, aDirtyRect,
          SVGImageContext(), aFlags);
    }

    // If we could not draw the icon, just draw some graffiti in the mean time.
    if (result == ImgDrawResult::NOT_READY) {
      ColorPattern color(ToDeviceColor(sRGBColor(1.f, 0.f, 0.f, 1.f)));

      nscoord iconXPos = flushRight ? inner.XMost() - size : inner.x;

      // stroked rect:
      nsRect rect(iconXPos, inner.y, size, size);
      Rect devPxRect = ToRect(nsLayoutUtils::RectToGfxRect(
          rect, PresContext()->AppUnitsPerDevPixel()));
      drawTarget->StrokeRect(devPxRect, color);

      // filled circle in bottom right quadrant of stroked rect:
      nscoord twoPX = nsPresContext::CSSPixelsToAppUnits(2);
      rect = nsRect(iconXPos + size / 2, inner.y + size / 2, size / 2 - twoPX,
                    size / 2 - twoPX);
      devPxRect = ToRect(nsLayoutUtils::RectToGfxRect(
          rect, PresContext()->AppUnitsPerDevPixel()));
      RefPtr<PathBuilder> builder = drawTarget->CreatePathBuilder();
      AppendEllipseToPath(builder, devPxRect.Center(), devPxRect.Size());
      RefPtr<Path> ellipse = builder->Finish();
      drawTarget->Fill(ellipse, color);
    }

    // Reduce the inner rect by the width of the icon, and leave an
    // additional ICON_PADDING pixels for padding
    int32_t paddedIconSize =
        nsPresContext::CSSPixelsToAppUnits(ICON_SIZE + ICON_PADDING);
    if (wm.IsVertical()) {
      inner.y += paddedIconSize;
      inner.height -= paddedIconSize;
    } else {
      if (!flushRight) {
        inner.x += paddedIconSize;
      }
      inner.width -= paddedIconSize;
    }
  }

  // If there's still room, display the alt-text
  if (!inner.IsEmpty()) {
    nsAutoString altText;
    nsCSSFrameConstructor::GetAlternateTextFor(*mContent->AsElement(), altText);
    DisplayAltText(PresContext(), aRenderingContext, altText, inner);
  }

  aRenderingContext.Restore();

  return result;
}

ImgDrawResult nsImageFrame::DisplayAltFeedbackWithoutLayer(
    nsDisplayItem* aItem, wr::DisplayListBuilder& aBuilder,
    wr::IpcResourceUpdateQueue& aResources, const StackingContextHelper& aSc,
    layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder, nsPoint aPt, uint32_t aFlags) {
  // Whether we draw the broken or loading icon.
  bool isLoading = mKind != Kind::ImageLoadingContent ||
                   ImageOk(mContent->AsElement()->State());

  // Calculate the content area.
  nsRect inner = GetContentRectRelativeToSelf() + aPt;

  // Display a recessed one pixel border
  nscoord borderEdgeWidth =
      nsPresContext::CSSPixelsToAppUnits(ALT_BORDER_WIDTH);

  // if inner area is empty, then make it big enough for at least the icon
  if (inner.IsEmpty()) {
    inner.SizeTo(2 * (nsPresContext::CSSPixelsToAppUnits(
                         ICON_SIZE + ICON_PADDING + ALT_BORDER_WIDTH)),
                 2 * (nsPresContext::CSSPixelsToAppUnits(
                         ICON_SIZE + ICON_PADDING + ALT_BORDER_WIDTH)));
  }

  // Make sure we have enough room to actually render the border within
  // our frame bounds
  if ((inner.width < 2 * borderEdgeWidth) ||
      (inner.height < 2 * borderEdgeWidth)) {
    return ImgDrawResult::SUCCESS;
  }

  // If the TextDrawTarget requires fallback we need to rollback everything we
  // may have added to the display list, but we don't find that out until the
  // end.
  bool textDrawResult = true;
  class AutoSaveRestore {
   public:
    explicit AutoSaveRestore(wr::DisplayListBuilder& aBuilder,
                             bool& aTextDrawResult)
        : mBuilder(aBuilder), mTextDrawResult(aTextDrawResult) {
      mBuilder.Save();
    }
    ~AutoSaveRestore() {
      // If we have to use fallback for the text restore the builder and remove
      // anything else we added to the display list, we need to use fallback.
      if (mTextDrawResult) {
        mBuilder.ClearSave();
      } else {
        mBuilder.Restore();
      }
    }

   private:
    wr::DisplayListBuilder& mBuilder;
    bool& mTextDrawResult;
  };

  AutoSaveRestore autoSaveRestore(aBuilder, textDrawResult);

  // Paint the border
  if (!isLoading) {
    nsRecessedBorder recessedBorder(borderEdgeWidth);
    // Assert that we're not drawing a border-image here; if we were, we
    // couldn't ignore the ImgDrawResult that PaintBorderWithStyleBorder
    // returns.
    MOZ_ASSERT(recessedBorder.mBorderImageSource.IsNone());

    nsRect rect = nsRect(aPt, GetSize());
    Unused << nsCSSRendering::CreateWebRenderCommandsForBorderWithStyleBorder(
        aItem, this, rect, aBuilder, aResources, aSc, aManager,
        aDisplayListBuilder, recessedBorder);
  }

  // Adjust the inner rect to account for the one pixel recessed border,
  // and a six pixel padding on each edge
  inner.Deflate(
      nsPresContext::CSSPixelsToAppUnits(ICON_PADDING + ALT_BORDER_WIDTH),
      nsPresContext::CSSPixelsToAppUnits(ICON_PADDING + ALT_BORDER_WIDTH));
  if (inner.IsEmpty()) {
    return ImgDrawResult::SUCCESS;
  }

  // Clip to this rect so we don't render outside the inner rect
  const auto bounds = LayoutDeviceRect::FromAppUnits(
      inner, PresContext()->AppUnitsPerDevPixel());
  auto wrBounds = wr::ToLayoutRect(bounds);

  // Check if we should display image placeholders
  if (ShouldShowBrokenImageIcon()) {
    ImgDrawResult result = ImgDrawResult::NOT_READY;
    nscoord size = nsPresContext::CSSPixelsToAppUnits(ICON_SIZE);
    imgIRequest* request = BrokenImageIcon::GetImage(this);

    // If we weren't previously displaying an icon, register ourselves
    // as an observer for load and animation updates and flag that we're
    // doing so now.
    if (request && !mDisplayingIcon) {
      BrokenImageIcon::AddObserver(this);
      mDisplayingIcon = true;
    }

    WritingMode wm = GetWritingMode();
    const bool flushRight = wm.IsPhysicalRTL();

    // If the icon in question is loaded, draw it.
    uint32_t imageStatus = 0;
    if (request) {
      request->GetImageStatus(&imageStatus);
    }
    if (imageStatus & imgIRequest::STATUS_LOAD_COMPLETE &&
        !(imageStatus & imgIRequest::STATUS_ERROR)) {
      nsCOMPtr<imgIContainer> imgCon;
      request->GetImage(getter_AddRefs(imgCon));
      MOZ_ASSERT(imgCon, "Load complete, but no image container?");

      nsRect dest(flushRight ? inner.XMost() - size : inner.x, inner.y, size,
                  size);

      const int32_t factor = PresContext()->AppUnitsPerDevPixel();
      const auto destRect = LayoutDeviceRect::FromAppUnits(dest, factor);

      SVGImageContext svgContext;
      Maybe<ImageIntRegion> region;
      IntSize decodeSize =
          nsLayoutUtils::ComputeImageContainerDrawingParameters(
              imgCon, this, destRect, destRect, aSc, aFlags, svgContext,
              region);
      RefPtr<image::WebRenderImageProvider> provider;
      result = imgCon->GetImageProvider(aManager->LayerManager(), decodeSize,
                                        svgContext, region, aFlags,
                                        getter_AddRefs(provider));
      if (provider) {
        bool wrResult = aManager->CommandBuilder().PushImageProvider(
            aItem, provider, result, aBuilder, aResources, destRect, bounds);
        result &= wrResult ? ImgDrawResult::SUCCESS : ImgDrawResult::NOT_READY;
      } else {
        // We don't use &= here because we want the result to be NOT_READY so
        // the next block executes.
        result = ImgDrawResult::NOT_READY;
      }
    }

    // If we could not draw the icon, just draw some graffiti in the mean time.
    if (result == ImgDrawResult::NOT_READY) {
      auto color = wr::ColorF{1.0f, 0.0f, 0.0f, 1.0f};
      bool isBackfaceVisible = !aItem->BackfaceIsHidden();

      nscoord iconXPos = flushRight ? inner.XMost() - size : inner.x;

      // stroked rect:
      nsRect rect(iconXPos, inner.y, size, size);
      auto devPxRect = LayoutDeviceRect::FromAppUnits(
          rect, PresContext()->AppUnitsPerDevPixel());
      auto dest = wr::ToLayoutRect(devPxRect);

      auto borderWidths = wr::ToBorderWidths(1.0, 1.0, 1.0, 1.0);
      wr::BorderSide side = {color, wr::BorderStyle::Solid};
      wr::BorderSide sides[4] = {side, side, side, side};
      Range<const wr::BorderSide> sidesRange(sides, 4);
      aBuilder.PushBorder(dest, wrBounds, isBackfaceVisible, borderWidths,
                          sidesRange, wr::EmptyBorderRadius());

      // filled circle in bottom right quadrant of stroked rect:
      nscoord twoPX = nsPresContext::CSSPixelsToAppUnits(2);
      rect = nsRect(iconXPos + size / 2, inner.y + size / 2, size / 2 - twoPX,
                    size / 2 - twoPX);
      devPxRect = LayoutDeviceRect::FromAppUnits(
          rect, PresContext()->AppUnitsPerDevPixel());
      dest = wr::ToLayoutRect(devPxRect);

      aBuilder.PushRoundedRect(dest, wrBounds, isBackfaceVisible, color);
    }

    // Reduce the inner rect by the width of the icon, and leave an
    // additional ICON_PADDING pixels for padding
    int32_t paddedIconSize =
        nsPresContext::CSSPixelsToAppUnits(ICON_SIZE + ICON_PADDING);
    if (wm.IsVertical()) {
      inner.y += paddedIconSize;
      inner.height -= paddedIconSize;
    } else {
      if (!flushRight) {
        inner.x += paddedIconSize;
      }
      inner.width -= paddedIconSize;
    }
  }

  // Draw text
  if (!inner.IsEmpty()) {
    RefPtr<TextDrawTarget> textDrawer =
        new TextDrawTarget(aBuilder, aResources, aSc, aManager, aItem, inner,
                           /* aCallerDoesSaveRestore = */ true);
    MOZ_ASSERT(textDrawer->IsValid());
    if (textDrawer->IsValid()) {
      gfxContext captureCtx(textDrawer);

      nsAutoString altText;
      nsCSSFrameConstructor::GetAlternateTextFor(*mContent->AsElement(),
                                                 altText);
      DisplayAltText(PresContext(), captureCtx, altText, inner);

      textDrawer->TerminateShadows();
      textDrawResult = !textDrawer->CheckHasUnsupportedFeatures();
    }
  }

  // Purposely ignore local DrawResult because we handled it not being success
  // already.
  return textDrawResult ? ImgDrawResult::SUCCESS : ImgDrawResult::NOT_READY;
}

// We want to sync-decode in this case, as otherwise we either need to flash
// white while waiting to decode the new image, or paint the old image with a
// different aspect-ratio, which would be bad as it'd be stretched.
//
// See bug 1589955.
static bool OldImageHasDifferentRatio(const nsImageFrame& aFrame,
                                      imgIContainer& aImage,
                                      imgIContainer* aPrevImage) {
  if (!aPrevImage || aPrevImage == &aImage) {
    return false;
  }

  // If we don't depend on our intrinsic image size / ratio, we're good.
  //
  // FIXME(emilio): There's the case of the old image being painted
  // intrinsically, and src and styles changing at the same time... Maybe we
  // should keep track of the old GetPaintRect()'s ratio and the image's ratio,
  // instead of checking this bit?
  if (aFrame.HasAnyStateBits(IMAGE_SIZECONSTRAINED)) {
    return false;
  }

  auto currentRatio = aFrame.GetIntrinsicRatio();
  auto oldRatio = aFrame.ComputeIntrinsicRatioForImage(aPrevImage);
  return oldRatio != currentRatio;
}

#ifdef DEBUG
void nsImageFrame::AssertSyncDecodingHintIsInSync() const {
  if (!IsForImageLoadingContent()) {
    return;
  }
  nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(mContent);
  MOZ_ASSERT(imageLoader);

  // The opposite is not true, we might have some other heuristics which force
  // sync-decoding of images.
  MOZ_ASSERT_IF(imageLoader->GetSyncDecodingHint(), mForceSyncDecoding);
}
#endif

void nsDisplayImage::Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) {
  auto* frame = Frame();
  frame->AssertSyncDecodingHintIsInSync();

  auto* image = frame->mImage.get();
  auto* prevImage = frame->mPrevImage.get();
  if (!image) {
    return;
  }

  const bool oldImageIsDifferent =
      OldImageHasDifferentRatio(*frame, *image, prevImage);

  uint32_t flags = aBuilder->GetImageDecodeFlags();
  if (aBuilder->ShouldSyncDecodeImages() || oldImageIsDifferent ||
      frame->mForceSyncDecoding) {
    flags |= imgIContainer::FLAG_SYNC_DECODE;
  }

  ImgDrawResult result = frame->PaintImage(
      *aCtx, ToReferenceFrame(), GetPaintRect(aBuilder, aCtx), image, flags);

  if (result == ImgDrawResult::NOT_READY ||
      result == ImgDrawResult::INCOMPLETE ||
      result == ImgDrawResult::TEMPORARY_ERROR) {
    // If the current image failed to paint because it's still loading or
    // decoding, try painting the previous image.
    if (prevImage) {
      result =
          frame->PaintImage(*aCtx, ToReferenceFrame(),
                            GetPaintRect(aBuilder, aCtx), prevImage, flags);
    }
  }
}

nsRect nsDisplayImage::GetDestRect() const {
  bool snap = true;
  const nsRect frameContentBox = GetBounds(&snap);
  nsImageFrame* imageFrame = static_cast<nsImageFrame*>(mFrame);
  return imageFrame->GetDestRect(frameContentBox);
}

nsRegion nsDisplayImage::GetOpaqueRegion(nsDisplayListBuilder* aBuilder,
                                         bool* aSnap) const {
  *aSnap = false;
  auto* image = Frame()->mImage.get();
  if (image && image->WillDrawOpaqueNow()) {
    const nsRect frameContentBox = GetBounds(aSnap);
    return GetDestRect().Intersect(frameContentBox);
  }
  return nsRegion();
}

void nsDisplayImage::MaybeCreateWebRenderCommandsForViewTransition(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  auto* frame = Frame();
  MOZ_ASSERT(!frame->mImage);
  auto key = frame->GetViewTransitionImageKey(aManager, aResources);
  if (NS_WARN_IF(key == kNoKey)) {
    return;
  }
  VT_LOG_DEBUG("GetViewTransitionImageKey(%s) = %s", frame->ListTag().get(),
               ToString(key).c_str());
  const nsRect destAppUnits = GetDestRect();
  const int32_t factor = mFrame->PresContext()->AppUnitsPerDevPixel();
  const auto destRect =
      wr::ToLayoutRect(LayoutDeviceRect::FromAppUnits(destAppUnits, factor));
  auto rendering = wr::ToImageRendering(frame->UsedImageRendering());
  aBuilder.PushDebug(1);
  aBuilder.PushImage(destRect, destRect, !BackfaceIsHidden(),
                     /* aForceAntiAliasing = */ false, rendering, key);
}

bool nsDisplayImage::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  auto* frame = Frame();
  auto* image = frame->mImage.get();
  if (!image) {
    MaybeCreateWebRenderCommandsForViewTransition(
        aBuilder, aResources, aSc, aManager, aDisplayListBuilder);
    return true;
  }

  if (nsImageMap* map = frame->GetImageMap(); map && map->HasFocus()) {
    // We can't draw some of the focus areas (in particular, PolyArea would be
    // somewhat hard to do).
    return false;
  }

  auto* prevImage = frame->mPrevImage.get();

  frame->AssertSyncDecodingHintIsInSync();
  const bool oldImageIsDifferent =
      OldImageHasDifferentRatio(*frame, *image, prevImage);

  uint32_t flags = aDisplayListBuilder->GetImageDecodeFlags();
  if (aDisplayListBuilder->ShouldSyncDecodeImages() || oldImageIsDifferent ||
      frame->mForceSyncDecoding) {
    flags |= imgIContainer::FLAG_SYNC_DECODE;
  }
  if (StaticPrefs::image_svg_blob_image() &&
      image->GetType() == imgIContainer::TYPE_VECTOR) {
    flags |= imgIContainer::FLAG_RECORD_BLOB;
  }

  const nsRect destAppUnits = GetDestRect();
  const int32_t factor = mFrame->PresContext()->AppUnitsPerDevPixel();
  const auto destRect = LayoutDeviceRect::FromAppUnits(destAppUnits, factor);

  SVGImageContext svgContext;
  Maybe<ImageIntRegion> region;
  IntSize decodeSize = nsLayoutUtils::ComputeImageContainerDrawingParameters(
      image, mFrame, destRect, destRect, aSc, flags, svgContext, region);

  RefPtr<image::WebRenderImageProvider> provider;
  ImgDrawResult drawResult =
      image->GetImageProvider(aManager->LayerManager(), decodeSize, svgContext,
                              region, flags, getter_AddRefs(provider));

  if (nsCOMPtr<imgIRequest> currentRequest = frame->GetCurrentRequest()) {
    LCPHelpers::FinalizeLCPEntryForImage(
        frame->GetContent()->AsElement(),
        static_cast<imgRequestProxy*>(currentRequest.get()),
        destAppUnits - ToReferenceFrame());
  }

  // While we got a container, it may not contain a fully decoded surface. If
  // that is the case, and we have an image we were previously displaying which
  // has a fully decoded surface, then we should prefer the previous image.
  bool updatePrevImage = false;
  switch (drawResult) {
    case ImgDrawResult::NOT_READY:
    case ImgDrawResult::INCOMPLETE:
    case ImgDrawResult::TEMPORARY_ERROR:
      if (prevImage && prevImage != image) {
        // The current image and the previous image might be switching between
        // rasterized surfaces and blob recordings, so we need to update the
        // flags appropriately.
        uint32_t prevFlags = flags;
        if (StaticPrefs::image_svg_blob_image() &&
            prevImage->GetType() == imgIContainer::TYPE_VECTOR) {
          prevFlags |= imgIContainer::FLAG_RECORD_BLOB;
        } else {
          prevFlags &= ~imgIContainer::FLAG_RECORD_BLOB;
        }

        RefPtr<image::WebRenderImageProvider> prevProvider;
        ImgDrawResult prevDrawResult = prevImage->GetImageProvider(
            aManager->LayerManager(), decodeSize, svgContext, region, prevFlags,
            getter_AddRefs(prevProvider));
        if (prevProvider && (prevDrawResult == ImgDrawResult::SUCCESS ||
                             prevDrawResult == ImgDrawResult::WRONG_SIZE)) {
          // We use WRONG_SIZE here to ensure that when the frame next tries to
          // invalidate due to a frame update from the current image, we don't
          // consider the result from the previous image to be a valid result to
          // avoid redrawing.
          drawResult = ImgDrawResult::WRONG_SIZE;
          provider = std::move(prevProvider);
          flags = prevFlags;
          break;
        }

        // Previous image was unusable; we can forget about it.
        updatePrevImage = true;
      }
      break;
    case ImgDrawResult::NOT_SUPPORTED:
      return false;
    default:
      updatePrevImage = prevImage != image;
      break;
  }

  // The previous image was not used, and is different from the current image.
  // We should forget about it. We need to update the frame as well because the
  // display item may get recreated.
  if (updatePrevImage) {
    frame->mPrevImage = frame->mImage;
  }

  // If the image provider is null, we don't want to fallback. Any other
  // failure will be due to resource constraints and fallback is unlikely to
  // help us. Hence we can ignore the return value from PushImage.
  aManager->CommandBuilder().PushImageProvider(
      this, provider, drawResult, aBuilder, aResources, destRect, destRect);
  return true;
}

ImgDrawResult nsImageFrame::PaintImage(gfxContext& aRenderingContext,
                                       nsPoint aPt, const nsRect& aDirtyRect,
                                       imgIContainer* aImage, uint32_t aFlags) {
  DrawTarget* drawTarget = aRenderingContext.GetDrawTarget();

  // Render the image into our content area (the area inside
  // the borders and padding)
  NS_ASSERTION(GetContentRectRelativeToSelf().width == mComputedSize.width,
               "bad width");

  nsPoint anchorPoint;
  nsRect dest = GetDestRect(GetContentRectRelativeToSelf() + aPt, &anchorPoint);

  SVGImageContext svgContext;
  SVGImageContext::MaybeStoreContextPaint(svgContext, this, aImage);

  ImgDrawResult result = nsLayoutUtils::DrawSingleImage(
      aRenderingContext, PresContext(), aImage,
      nsLayoutUtils::GetSamplingFilterForFrame(this), dest, aDirtyRect,
      svgContext, aFlags, &anchorPoint);

  if (nsImageMap* map = GetImageMap(); map && map->HasFocus()) {
    gfxPoint devPixelOffset = nsLayoutUtils::PointToGfxPoint(
        dest.TopLeft(), PresContext()->AppUnitsPerDevPixel());
    AutoRestoreTransform autoRestoreTransform(drawTarget);
    drawTarget->SetTransform(
        drawTarget->GetTransform().PreTranslate(ToPoint(devPixelOffset)));

    // solid white stroke:
    ColorPattern white(ToDeviceColor(sRGBColor::OpaqueWhite()));
    map->DrawFocus(this, *drawTarget, white);

    // then dashed black stroke over the top:
    ColorPattern black(ToDeviceColor(sRGBColor::OpaqueBlack()));
    StrokeOptions strokeOptions;
    nsLayoutUtils::InitDashPattern(strokeOptions, StyleBorderStyle::Dotted);
    map->DrawFocus(this, *drawTarget, black, strokeOptions);
  }

  if (result == ImgDrawResult::SUCCESS) {
    mPrevImage = aImage;
  } else if (result == ImgDrawResult::BAD_IMAGE) {
    mPrevImage = nullptr;
  }

  return result;
}

already_AddRefed<imgIRequest> nsImageFrame::GetCurrentRequest() const {
  if (mKind != Kind::ImageLoadingContent) {
    return do_AddRef(mOwnedRequest);
  }

  MOZ_ASSERT(!mOwnedRequest);

  nsCOMPtr<imgIRequest> request;
  nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(mContent);
  MOZ_ASSERT(imageLoader);
  imageLoader->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                          getter_AddRefs(request));
  return request.forget();
}

void nsImageFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                    const nsDisplayListSet& aLists) {
  if (!IsVisibleForPainting()) {
    return;
  }

  DisplayBorderBackgroundOutline(aBuilder, aLists);

  if (HidesContent()) {
    DisplaySelectionOverlay(aBuilder, aLists.Content(),
                            nsISelectionDisplay::DISPLAY_IMAGES);
    return;
  }

  uint32_t clipFlags =
      nsStyleUtil::ObjectPropsMightCauseOverflow(StylePosition())
          ? 0
          : DisplayListClipState::ASSUME_DRAWING_RESTRICTED_TO_CONTENT_RECT;

  DisplayListClipState::AutoClipContainingBlockDescendantsToContentBox clip(
      aBuilder, this, clipFlags);

  if (!mComputedSize.IsEmpty()) {
    const bool isViewTransition = mKind == Kind::ViewTransition;
    const bool imageOK = mKind != Kind::ImageLoadingContent ||
                         ImageOk(mContent->AsElement()->State());

    nsCOMPtr<imgIRequest> currentRequest = GetCurrentRequest();

    const bool isImageFromStyle = mKind != Kind::ImageLoadingContent &&
                                  mKind != Kind::XULImage && !isViewTransition;
    const bool drawAltFeedback = [&] {
      if (!imageOK) {
        return true;
      }
      if (isImageFromStyle && !GetImageFromStyle()->IsImageRequestType()) {
        // If we're a gradient, we don't need to draw alt feedback.
        return false;
      }
      if (isViewTransition) {
        // Same for view transitions.
        return false;
      }
      // XXX(seth): The SizeIsAvailable check here should not be necessary - the
      // intention is that a non-null mImage means we have a size, but there is
      // currently some code that violates this invariant.
      return !mImage || !SizeIsAvailable(currentRequest);
    }();

    if (drawAltFeedback) {
      // No image yet, or image load failed. Draw the alt-text and an icon
      // indicating the status
      aLists.Content()->AppendNewToTop<nsDisplayAltFeedback>(aBuilder, this);

      // This image is visible (we are being asked to paint it) but it's not
      // decoded yet. And we are not going to ask the image to draw, so this
      // may be the only chance to tell it that it should decode.
      if (currentRequest) {
        uint32_t status = 0;
        currentRequest->GetImageStatus(&status);
        if (!(status & imgIRequest::STATUS_DECODE_COMPLETE)) {
          MaybeDecodeForPredictedSize();
        }
        // Increase loading priority if the image is ready to be displayed.
        if (!(status & imgIRequest::STATUS_LOAD_COMPLETE)) {
          currentRequest->BoostPriority(imgIRequest::CATEGORY_DISPLAY);
        }
      }
    } else {
      if (mImage || isViewTransition) {
        aLists.Content()->AppendNewToTop<nsDisplayImage>(aBuilder, this);
      } else if (isImageFromStyle) {
        aLists.Content()->AppendNewToTop<nsDisplayGradient>(aBuilder, this);
      }

      // If we were previously displaying an icon, we're not anymore
      if (mDisplayingIcon) {
        BrokenImageIcon::RemoveObserver(this);
        mDisplayingIcon = false;
      }
    }
  }

  if (ShouldDisplaySelection()) {
    DisplaySelectionOverlay(aBuilder, aLists.Content(),
                            nsISelectionDisplay::DISPLAY_IMAGES);
  }

  BuildDisplayListForNonBlockChildren(aBuilder, aLists);
}

bool nsImageFrame::ShouldDisplaySelection() {
  int16_t displaySelection = PresShell()->GetSelectionFlags();
  if (!(displaySelection & nsISelectionDisplay::DISPLAY_IMAGES)) {
    // no need to check the blue border, we cannot be drawn selected.
    return false;
  }

  if (displaySelection != nsISelectionDisplay::DISPLAY_ALL) {
    return true;
  }

  // If the image is currently resize target of the editor, don't draw the
  // selection overlay.
  HTMLEditor* htmlEditor = nsContentUtils::GetHTMLEditor(PresContext());
  if (!htmlEditor) {
    return true;
  }

  return htmlEditor->GetResizerTarget() != mContent;
}

nsImageMap* nsImageFrame::GetImageMap() {
  if (!mImageMap) {
    if (nsIContent* map = GetMapElement()) {
      mImageMap = new nsImageMap();
      mImageMap->Init(this, map);
    }
  }

  return mImageMap;
}

bool nsImageFrame::IsServerImageMap() {
  return mContent->AsElement()->HasAttr(nsGkAtoms::ismap);
}

CSSIntPoint nsImageFrame::TranslateEventCoords(const nsPoint& aPoint) const {
  const nsRect contentRect = GetContentRectRelativeToSelf();
  // Subtract out border and padding here so that the coordinates are
  // now relative to the content area of this frame.
  return CSSPixel::FromAppUnitsRounded(aPoint - contentRect.TopLeft());
}

bool nsImageFrame::GetAnchorHREFTargetAndNode(nsIURI** aHref, nsString& aTarget,
                                              nsIContent** aNode) {
  aTarget.Truncate();
  *aHref = nullptr;
  *aNode = nullptr;

  // Walk up the content tree, looking for an nsIDOMAnchorElement
  for (nsIContent* content = mContent->GetParent(); content;
       content = content->GetParent()) {
    nsCOMPtr<dom::Link> link = do_QueryInterface(content);
    if (!link) {
      continue;
    }
    if (nsCOMPtr<nsIURI> href = link->GetURI()) {
      href.forget(aHref);
    }

    if (auto* anchor = HTMLAnchorElement::FromNode(content)) {
      anchor->GetLinkTarget(aTarget);
    }
    NS_ADDREF(*aNode = content);
    return *aHref != nullptr;
  }
  return false;
}

bool nsImageFrame::IsLeafDynamic() const {
  if (mKind != Kind::ImageLoadingContent) {
    // Image frames created for "content: url()" could have an author-controlled
    // shadow root, we want to be a regular leaf for those.
    return true;
  }
  // For elements that create image frames, calling attachShadow() will throw,
  // so the only ShadowRoot we could ever have is a UA widget.
  const auto* shadow = mContent->AsElement()->GetShadowRoot();
  MOZ_ASSERT_IF(shadow, shadow->IsUAWidget());
  return !shadow;
}

nsIContent* nsImageFrame::GetContentForEvent(const WidgetEvent* aEvent) const {
  if (mImageMap) {
    // XXX We need to make this special check for area element's capturing the
    // mouse due to bug 135040. Remove it once that's fixed.
    nsIContent* capturingContent = aEvent->HasMouseEventMessage()
                                       ? PresShell::GetCapturingContent()
                                       : nullptr;
    if (capturingContent && capturingContent->GetPrimaryFrame() == this) {
      return capturingContent;
    }
    const CSSIntPoint p = TranslateEventCoords(
        nsLayoutUtils::GetEventCoordinatesRelativeTo(aEvent, RelativeTo{this}));
    if (auto* area = mImageMap->GetArea(p)) {
      return area;
    }
  }
  return nsIFrame::GetContentForEvent(aEvent);
}

// XXX what should clicks on transparent pixels do?
nsresult nsImageFrame::HandleEvent(nsPresContext* aPresContext,
                                   WidgetGUIEvent* aEvent,
                                   nsEventStatus* aEventStatus) {
  NS_ENSURE_ARG_POINTER(aEventStatus);

  if ((aEvent->mMessage == ePointerClick &&
       aEvent->AsMouseEvent()->mButton == MouseButton::ePrimary) ||
      aEvent->mMessage == eMouseMove) {
    nsImageMap* map = GetImageMap();
    bool isServerMap = IsServerImageMap();
    if (map || isServerMap) {
      CSSIntPoint p =
          TranslateEventCoords(nsLayoutUtils::GetEventCoordinatesRelativeTo(
              aEvent, RelativeTo{this}));

      // Even though client-side image map triggering happens
      // through content, we need to make sure we're not inside
      // (in case we deal with a case of both client-side and
      // sever-side on the same image - it happens!)
      const bool inside = map && map->GetArea(p);

      if (!inside && isServerMap) {
        // Server side image maps use the href in a containing anchor
        // element to provide the basis for the destination url.
        nsCOMPtr<nsIURI> uri;
        nsAutoString target;
        nsCOMPtr<nsIContent> anchorNode;
        if (GetAnchorHREFTargetAndNode(getter_AddRefs(uri), target,
                                       getter_AddRefs(anchorNode))) {
          // XXX if the mouse is over/clicked in the border/padding area
          // we should probably just pretend nothing happened. Nav4
          // keeps the x,y coordinates positive as we do; IE doesn't
          // bother. Both of them send the click through even when the
          // mouse is over the border.
          if (p.x < 0) {
            p.x = 0;
          }
          if (p.y < 0) {
            p.y = 0;
          }

          nsAutoCString spec;
          nsresult rv = uri->GetSpec(spec);
          NS_ENSURE_SUCCESS(rv, rv);

          spec += nsPrintfCString("?%d,%d", p.x.value, p.y.value);
          rv = NS_MutateURI(uri).SetSpec(spec).Finalize(uri);
          NS_ENSURE_SUCCESS(rv, rv);

          bool clicked = false;
          if (aEvent->mMessage == ePointerClick &&
              !aEvent->DefaultPrevented()) {
            *aEventStatus = nsEventStatus_eConsumeDoDefault;
            clicked = true;
          }
          nsContentUtils::TriggerLink(anchorNode, uri, target, clicked);
        }
      }
    }
  }

  return nsAtomicContainerFrame::HandleEvent(aPresContext, aEvent,
                                             aEventStatus);
}

nsIFrame::Cursor nsImageFrame::GetCursor(const nsPoint& aPoint) {
  nsImageMap* map = GetImageMap();
  if (!map) {
    return nsIFrame::GetCursor(aPoint);
  }
  const CSSIntPoint p = TranslateEventCoords(aPoint);
  HTMLAreaElement* area = map->GetArea(p);
  if (!area) {
    return nsIFrame::GetCursor(aPoint);
  }

  // Use the cursor from the style of the *area* element.
  RefPtr<ComputedStyle> areaStyle =
      PresShell()->StyleSet()->ResolveStyleLazily(*area);

  // This is one of the cases, like the <xul:tree> pseudo-style stuff, where we
  // get styles out of the blue and expect to trigger image loads for those.
  areaStyle->StartImageLoads(*PresContext()->Document());

  StyleCursorKind kind = areaStyle->StyleUI()->Cursor().keyword;
  if (kind == StyleCursorKind::Auto) {
    kind = StyleCursorKind::Default;
  }
  return Cursor{kind, AllowCustomCursorImage::Yes, std::move(areaStyle)};
}

nsresult nsImageFrame::AttributeChanged(int32_t aNameSpaceID,
                                        nsAtom* aAttribute, int32_t aModType) {
  nsresult rv = nsAtomicContainerFrame::AttributeChanged(aNameSpaceID,
                                                         aAttribute, aModType);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (nsGkAtoms::alt == aAttribute) {
    PresShell()->FrameNeedsReflow(
        this, IntrinsicDirty::FrameAncestorsAndDescendants, NS_FRAME_IS_DIRTY);
  }
  if (mKind == Kind::XULImage && aAttribute == nsGkAtoms::src &&
      aNameSpaceID == kNameSpaceID_None) {
    UpdateXULImage();
  }
  return NS_OK;
}

void nsImageFrame::OnVisibilityChange(
    Visibility aNewVisibility, const Maybe<OnNonvisible>& aNonvisibleAction) {
  if (mKind == Kind::ImageLoadingContent) {
    nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(mContent);
    imageLoader->OnVisibilityChange(aNewVisibility, aNonvisibleAction);
  }

  if (aNewVisibility == Visibility::ApproximatelyVisible &&
      PresShell()->IsActive()) {
    MaybeDecodeForPredictedSize();
  }

  nsAtomicContainerFrame::OnVisibilityChange(aNewVisibility, aNonvisibleAction);
}

void nsImageFrame::MarkIntrinsicISizesDirty() {
  // Recompute our intrinsic size and ratio. It's important that we pass 'true'
  // here -- that makes us recompute the intrinsic size *and* ratio, regardless
  // of their current value. Without that, EnsureIntrinsicSizeAndRatio assumes
  // the intrinsic ratio can't have changed in some cases; but if we're a
  // "content-visibility:auto" image that's being scrolled back into view, our
  // ratio may have changed from not-existing to existing, per spec text[1]
  // about suppressing the natural aspect ratio when size-contained.
  // [1] https://drafts.csswg.org/css-contain-2/#containment-size
  EnsureIntrinsicSizeAndRatio(true);

  // Call superclass method:
  nsAtomicContainerFrame::MarkIntrinsicISizesDirty();
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsImageFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"ImageFrame"_ns, aResult);
}

void nsImageFrame::List(FILE* out, const char* aPrefix,
                        ListFlags aFlags) const {
  nsCString str;
  ListGeneric(str, aPrefix, aFlags);

  // output the img src url
  if (nsCOMPtr<imgIRequest> currentRequest = GetCurrentRequest()) {
    nsCOMPtr<nsIURI> uri = currentRequest->GetURI();
    nsAutoCString uristr;
    uri->GetAsciiSpec(uristr);
    str += nsPrintfCString(" [src=%s]", uristr.get());
  }
  fprintf_stderr(out, "%s\n", str.get());
}
#endif

LogicalSides nsImageFrame::GetLogicalSkipSides() const {
  LogicalSides skip(mWritingMode);
  if (MOZ_UNLIKELY(StyleBorder()->mBoxDecorationBreak ==
                   StyleBoxDecorationBreak::Clone)) {
    return skip;
  }
  if (GetPrevInFlow()) {
    skip += LogicalSide::BStart;
  }
  if (GetNextInFlow()) {
    skip += LogicalSide::BEnd;
  }
  return skip;
}
NS_IMPL_ISUPPORTS(nsImageListener, imgINotificationObserver)

nsImageListener::nsImageListener(nsImageFrame* aFrame) : mFrame(aFrame) {}

nsImageListener::~nsImageListener() = default;

void nsImageListener::Notify(imgIRequest* aRequest, int32_t aType,
                             const nsIntRect* aData) {
  if (!mFrame) {
    return;
  }

  return mFrame->Notify(aRequest, aType, aData);
}

static bool IsInAutoWidthTableCellForQuirk(nsIFrame* aFrame) {
  if (eCompatibility_NavQuirks != aFrame->PresContext()->CompatibilityMode()) {
    return false;
  }
  // Check if the parent of the closest nsBlockFrame has auto width.
  nsBlockFrame* ancestor = nsLayoutUtils::FindNearestBlockAncestor(aFrame);
  if (ancestor->Style()->GetPseudoType() == PseudoStyleType::cellContent) {
    // Assume direct parent is a table cell frame.
    nsIFrame* grandAncestor = static_cast<nsIFrame*>(ancestor->GetParent());
    return grandAncestor &&
           grandAncestor->StylePosition()
               ->GetWidth(grandAncestor->StyleDisplay()->mPosition)
               ->IsAuto();
  }
  return false;
}

void nsImageFrame::AddInlineMinISize(const IntrinsicSizeInput& aInput,
                                     InlineMinISizeData* aData) {
  // Note: we are one of the children that mPercentageBasisForChildren was
  // prepared for (i.e. our parent frame prepares the percentage basis for us,
  // not for our own children). Hence it's fine that we're resolving our
  // percentages sizes against this basis in IntrinsicForContainer().
  nscoord isize = nsLayoutUtils::IntrinsicForContainer(
      aInput.mContext, this, IntrinsicISizeType::MinISize,
      aInput.mPercentageBasisForChildren);
  bool canBreak = !IsInAutoWidthTableCellForQuirk(this);
  aData->DefaultAddInlineMinISize(this, isize, canBreak);
}

void nsImageFrame::ReleaseGlobals() { BrokenImageIcon::Shutdown(); }
