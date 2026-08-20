/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLVideoElement.h"

#include "mozilla/AppShutdown.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/HTMLVideoElementBinding.h"
#ifdef MOZ_WEBRTC
#  include "mozilla/dom/RTCStatsReport.h"
#endif
#include <algorithm>
#include <limits>

#include "FrameStatistics.h"
#include "ImageContainer.h"
#include "MediaDecoder.h"
#include "MediaDecoderStateMachine.h"
#include "MediaError.h"
#include "VideoFrameContainer.h"
#include "VideoOutput.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/FeaturePolicyUtils.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/PictureInPictureEvent.h"
#include "mozilla/dom/PictureInPictureEventBinding.h"
#include "mozilla/dom/PictureInPictureService.h"
#include "mozilla/dom/PictureInPictureWindow.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/TimeRanges.h"
#include "mozilla/dom/VideoPlaybackQuality.h"
#include "mozilla/dom/VideoStreamTrack.h"
#include "mozilla/dom/WakeLock.h"
#include "mozilla/dom/power/PowerManagerService.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "nsError.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsIHttpChannel.h"
#include "nsNodeInfoManager.h"
#include "nsRFPService.h"
#include "nsSize.h"
#include "nsThreadUtils.h"
#include "plbase64.h"
#include "prlock.h"

extern mozilla::LazyLogModule gMediaElementLog;
#define LOG(msg, ...)                                                         \
  MOZ_LOG_FMT(gMediaElementLog, LogLevel::Debug, "HTMLVideoElement={}, " msg, \
              fmt::ptr(this), ##__VA_ARGS__)

nsGenericHTMLElement* NS_NewHTMLVideoElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo,
    mozilla::dom::FromParser aFromParser) {
  RefPtr<mozilla::dom::NodeInfo> nodeInfo(aNodeInfo);
  auto* nim = nodeInfo->NodeInfoManager();
  mozilla::dom::HTMLVideoElement* element =
      new (nim) mozilla::dom::HTMLVideoElement(nodeInfo.forget());
  element->Init();
  return element;
}

namespace mozilla::dom {

nsresult HTMLVideoElement::Clone(mozilla::dom::NodeInfo* aNodeInfo,
                                 nsINode** aResult) const {
  *aResult = nullptr;
  RefPtr<mozilla::dom::NodeInfo> ni(aNodeInfo);
  auto* nim = ni->NodeInfoManager();
  HTMLVideoElement* it = new (nim) HTMLVideoElement(ni.forget());
  it->Init();
  nsCOMPtr<nsINode> kungFuDeathGrip = it;
  nsresult rv = const_cast<HTMLVideoElement*>(this)->CopyInnerTo(it);
  if (NS_SUCCEEDED(rv)) {
    kungFuDeathGrip.swap(*aResult);
  }
  return rv;
}

nsresult HTMLVideoElement::CopyInnerTo(Element* aDest) {
  nsresult rv = HTMLMediaElement::CopyInnerTo(aDest);
  NS_ENSURE_SUCCESS(rv, rv);
  HTMLVideoElement* dest = static_cast<HTMLVideoElement*>(aDest);

  // Cloning into a static document indicates we are creating a clone for
  // printing purposes only.
  //
  // If we are making a clone for printing, also clone a frame of video.
  // Avoid using GetVideoFrameContainer on this object, because that may
  // create a new video container for no reason.
  if (aDest->OwnerDoc()->IsStaticDocument() && mVideoFrameContainer) {
    // We can expect the source video to have frames, unless the decoder
    // has been suspended. When that happens, all frames are cleared.
    // Otherwise, even if the media is not seekable, once a frame is
    // decoded there should always be images available.
    //
    // When the decoder is suspended, we will only get fake frames.
    // This situation is similar to when JS needs a frame to use in,
    // eg., nsLayoutUtils::SurfaceFromElement() via drawImage().
    //
    // TODO: As an alternative, we could asynchronously resume decoding
    // and dispatch an event to copy an image when that succeeds.
    //
    // See bug 1295921 for synchronous decoding support:
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1295921#c208
    AutoTArray<ImageContainer::OwningImage, 10> images;
    mVideoFrameContainer->GetImageContainer()->GetCurrentImages(&images);
    if (images.IsEmpty()) {
      LOG("no video images, printing with a suspended video decoder?");
      return rv;
    }

    // Ask for the video frame container only after we know we have a current
    // image from the source.
    // GetVideoFrameContainer might create a new video frame container,
    // which would be pointless if we won't be able to create any video data.
    VideoFrameContainer* const dstVideo = dest->GetVideoFrameContainer();
    NS_ENSURE_TRUE(dstVideo, rv);

    // Make a copy of the first image. This ensures we don't hold onto any
    // output buffers from a decoder while print preview is open.
    RefPtr<gfx::DataSourceSurface> dstSurface(CopyImage(images[0].mImage));
    if (!dstSurface) {
      MOZ_LOG_FMT(gMediaElementLog, LogLevel::Error,
                  "failed to copy video image");
      return rv;
    }
    RefPtr<layers::SourceSurfaceImage> dstImage =
        MakeAndAddRef<layers::SourceSurfaceImage>(dstSurface.get());

    dstVideo->SetCurrentFrame(dstImage->GetSize(), dstImage, TimeStamp(),
                              media::TimeUnit::Invalid(),
                              media::TimeUnit::Invalid());
  }
  return rv;
}

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(HTMLVideoElement,
                                               HTMLMediaElement)

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLVideoElement)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(HTMLVideoElement)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mVideoFrameRequestManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mVisualCloneTarget)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mVisualCloneTargetPromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mVisualCloneSource)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPictureInPictureWindow)
  tmp->mSecondaryVideoOutput = nullptr;
NS_IMPL_CYCLE_COLLECTION_UNLINK_END_INHERITED(HTMLMediaElement)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(HTMLVideoElement,
                                                  HTMLMediaElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mVideoFrameRequestManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mVisualCloneTarget)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mVisualCloneTargetPromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mVisualCloneSource)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPictureInPictureWindow)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

HTMLVideoElement::HTMLVideoElement(already_AddRefed<NodeInfo> aNodeInfo)
    : HTMLMediaElement(std::move(aNodeInfo)),
      mVideoWatchManager(this, AbstractThread::MainThread()) {
  DecoderDoctorLogger::LogConstruction(this);
}

HTMLVideoElement::~HTMLVideoElement() {
  mVideoWatchManager.Shutdown();
  DecoderDoctorLogger::LogDestruction(this);
}

void HTMLVideoElement::UpdateMediaSize(const nsIntSize& aSize) {
  HTMLMediaElement::UpdateMediaSize(aSize);
  // If we have a clone target, we should update its size as well.
  if (mVisualCloneTarget) {
    Maybe<nsIntSize> newSize = Some(aSize);
    mVisualCloneTarget->Invalidate(ImageSizeChanged::Yes, newSize,
                                   ForceInvalidate::Yes);
  }
}

Maybe<CSSIntSize> HTMLVideoElement::GetVideoSize() const {
  if (!mMediaInfo.HasVideo()) {
    return Nothing();
  }

  if (mDisableVideo) {
    return Nothing();
  }

  CSSIntSize size;
  switch (mMediaInfo.mVideo.mRotation) {
    case VideoRotation::kDegree_90:
    case VideoRotation::kDegree_270: {
      size.width = mMediaInfo.mVideo.mDisplay.height;
      size.height = mMediaInfo.mVideo.mDisplay.width;
      break;
    }
    case VideoRotation::kDegree_0:
    case VideoRotation::kDegree_180:
    default: {
      size.height = mMediaInfo.mVideo.mDisplay.height;
      size.width = mMediaInfo.mVideo.mDisplay.width;
      break;
    }
  }
  return Some(size);
}

void HTMLVideoElement::Invalidate(ImageSizeChanged aImageSizeChanged,
                                  const Maybe<nsIntSize>& aNewIntrinsicSize,
                                  ForceInvalidate aForceInvalidate) {
  HTMLMediaElement::Invalidate(aImageSizeChanged, aNewIntrinsicSize,
                               aForceInvalidate);
  if (mVisualCloneTarget) {
    VideoFrameContainer* container =
        mVisualCloneTarget->GetVideoFrameContainer();
    if (container) {
      container->Invalidate();
    }
  }

  if (mVideoFrameRequestManager.IsEmpty()) {
    return;
  }

  if (RefPtr<ImageContainer> imageContainer = GetImageContainer()) {
    if (imageContainer->HasCurrentImage()) {
      OwnerDoc()->ScheduleVideoFrameCallbacks(this);
    }
  }
}

bool HTMLVideoElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                      const nsAString& aValue,
                                      nsIPrincipal* aMaybeScriptedPrincipal,
                                      nsAttrValue& aResult) {
  if (aAttribute == nsGkAtoms::width || aAttribute == nsGkAtoms::height) {
    return aResult.ParseHTMLDimension(aValue);
  }

  return HTMLMediaElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                          aMaybeScriptedPrincipal, aResult);
}

void HTMLVideoElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                    const nsAttrValue* aValue,
                                    const nsAttrValue* aOldValue,
                                    nsIPrincipal* aMaybeScriptedPrincipal,
                                    bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None &&
      aName == nsGkAtoms::disablepictureinpicture && aValue) {
    if (OwnerDoc()->GetPictureInPictureElementInternal() == this) {
      PictureInPictureService::DispatchExitPictureInPictureRunnable(
          /* aPromise */ nullptr, this);
    }
  }

  HTMLMediaElement::AfterSetAttr(aNameSpaceID, aName, aValue, aOldValue,
                                 aMaybeScriptedPrincipal, aNotify);
}

void HTMLVideoElement::MapAttributesIntoRule(
    MappedDeclarationsBuilder& aBuilder) {
  MapImageSizeAttributesInto(aBuilder, MapAspectRatio::Yes);
  MapCommonAttributesInto(aBuilder);
}

NS_IMETHODIMP_(bool)
HTMLVideoElement::IsAttributeMapped(const nsAtom* aAttribute) const {
  static const MappedAttributeEntry attributes[] = {
      {nsGkAtoms::width}, {nsGkAtoms::height}, {nullptr}};

  static const MappedAttributeEntry* const map[] = {attributes,
                                                    sCommonAttributeMap};

  return FindAttributeDependence(aAttribute, map);
}

nsMapRuleToAttributesFunc HTMLVideoElement::GetAttributeMappingFunction()
    const {
  return &MapAttributesIntoRule;
}

void HTMLVideoElement::UnbindFromTree(UnbindContext& aContext) {
  if (mVisualCloneSource) {
    mVisualCloneSource->EndCloningVisually();
  } else if (mVisualCloneTarget) {
    AsyncEventDispatcher::RunDOMEventWhenSafe(
        *this, u"MozStopPictureInPicture"_ns, CanBubble::eNo,
        ChromeOnlyDispatch::eYes);
    EndCloningVisually();
  }

  HTMLMediaElement::UnbindFromTree(aContext);
}

nsresult HTMLVideoElement::SetAcceptHeader(nsIHttpChannel* aChannel) {
  nsAutoCString value(
      "video/webm,"
      "video/ogg,"
      "video/*;q=0.9,"
      "application/ogg;q=0.7,"
      "audio/*;q=0.6,*/*;q=0.5");

  return aChannel->SetRequestHeader("Accept"_ns, value, false);
}

bool HTMLVideoElement::IsInteractiveHTMLContent() const {
  return HasAttr(nsGkAtoms::controls) ||
         HTMLMediaElement::IsInteractiveHTMLContent();
}

gfx::IntSize HTMLVideoElement::GetVideoIntrinsicDimensions() const {
  const auto& sz = mMediaInfo.mVideo.mDisplay;

  // Prefer the size of the container as it's more up to date.
  return ToMaybeRef(mVideoFrameContainer.get())
      .map([&](auto& aVFC) { return aVFC.CurrentIntrinsicSize().valueOr(sz); })
      .valueOr(sz);
}

uint32_t HTMLVideoElement::VideoWidth() const {
  if (!HasVideo()) {
    return 0;
  }
  gfx::IntSize size = GetVideoIntrinsicDimensions();
  if (mMediaInfo.mVideo.mRotation == VideoRotation::kDegree_90 ||
      mMediaInfo.mVideo.mRotation == VideoRotation::kDegree_270) {
    return size.height;
  }
  return size.width;
}

uint32_t HTMLVideoElement::VideoHeight() const {
  if (!HasVideo()) {
    return 0;
  }
  gfx::IntSize size = GetVideoIntrinsicDimensions();
  if (mMediaInfo.mVideo.mRotation == VideoRotation::kDegree_90 ||
      mMediaInfo.mVideo.mRotation == VideoRotation::kDegree_270) {
    return size.width;
  }
  return size.height;
}

uint32_t HTMLVideoElement::MozParsedFrames() const {
  MOZ_ASSERT(NS_IsMainThread(), "Should be on main thread.");
  if (!IsVideoStatsEnabled()) {
    return 0;
  }

  if (OwnerDoc()->ShouldResistFingerprinting(
          RFPTarget::VideoElementMozFrames)) {
    return nsRFPService::GetSpoofedTotalFrames(TotalPlayTime());
  }

  return mDecoder ? mDecoder->GetFrameStatistics().GetParsedFrames() : 0;
}

uint32_t HTMLVideoElement::MozDecodedFrames() const {
  MOZ_ASSERT(NS_IsMainThread(), "Should be on main thread.");
  if (!IsVideoStatsEnabled()) {
    return 0;
  }

  if (OwnerDoc()->ShouldResistFingerprinting(
          RFPTarget::VideoElementMozFrames)) {
    return nsRFPService::GetSpoofedTotalFrames(TotalPlayTime());
  }

  return mDecoder ? mDecoder->GetFrameStatistics().GetDecodedFrames() : 0;
}

uint32_t HTMLVideoElement::MozPresentedFrames() {
  MOZ_ASSERT(NS_IsMainThread(), "Should be on main thread.");
  if (!IsVideoStatsEnabled()) {
    return 0;
  }

  if (OwnerDoc()->ShouldResistFingerprinting(
          RFPTarget::VideoElementMozFrames)) {
    return nsRFPService::GetSpoofedPresentedFrames(TotalPlayTime(),
                                                   VideoWidth(), VideoHeight());
  }

  return mDecoder ? mDecoder->GetFrameStatistics().GetPresentedFrames() : 0;
}

uint32_t HTMLVideoElement::MozPaintedFrames() {
  MOZ_ASSERT(NS_IsMainThread(), "Should be on main thread.");
  if (!IsVideoStatsEnabled()) {
    return 0;
  }

  if (OwnerDoc()->ShouldResistFingerprinting(
          RFPTarget::VideoElementMozFrames)) {
    return nsRFPService::GetSpoofedPresentedFrames(TotalPlayTime(),
                                                   VideoWidth(), VideoHeight());
  }

  layers::ImageContainer* container = GetImageContainer();
  return container ? container->GetPaintCount() : 0;
}

double HTMLVideoElement::MozFrameDelay() {
  MOZ_ASSERT(NS_IsMainThread(), "Should be on main thread.");

  if (!IsVideoStatsEnabled() || OwnerDoc()->ShouldResistFingerprinting(
                                    RFPTarget::VideoElementMozFrameDelay)) {
    return 0.0;
  }

  VideoFrameContainer* container = GetVideoFrameContainer();
  // Hide negative delays. Frame timing tweaks in the compositor (e.g.
  // adding a bias value to prevent multiple dropped/duped frames when
  // frame times are aligned with composition times) may produce apparent
  // negative delay, but we shouldn't report that.
  return container ? std::max(0.0, container->GetFrameDelay()) : 0.0;
}

bool HTMLVideoElement::MozHasAudio() const {
  MOZ_ASSERT(NS_IsMainThread(), "Should be on main thread.");
  return HasAudio();
}

JSObject* HTMLVideoElement::WrapNode(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return HTMLVideoElement_Binding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<VideoPlaybackQuality>
HTMLVideoElement::GetVideoPlaybackQuality() {
  DOMHighResTimeStamp creationTime = 0;
  uint32_t totalFrames = 0;
  uint32_t droppedFrames = 0;

  if (IsVideoStatsEnabled()) {
    if (nsPIDOMWindowInner* window = OwnerDoc()->GetInnerWindow()) {
      Performance* perf = window->GetPerformance();
      if (perf) {
        creationTime = perf->Now();
      }
    }

    if (mDecoder) {
      if (OwnerDoc()->ShouldResistFingerprinting(
              RFPTarget::VideoElementPlaybackQuality)) {
        totalFrames = nsRFPService::GetSpoofedTotalFrames(TotalPlayTime());
        droppedFrames = nsRFPService::GetSpoofedDroppedFrames(
            TotalPlayTime(), VideoWidth(), VideoHeight());
      } else {
        FrameStatistics* stats = &mDecoder->GetFrameStatistics();
        if (sizeof(totalFrames) >= sizeof(stats->GetParsedFrames())) {
          totalFrames = stats->GetTotalFrames();
          droppedFrames = stats->GetDroppedFrames();
        } else {
          uint64_t total = stats->GetTotalFrames();
          const auto maxNumber = std::numeric_limits<uint32_t>::max();
          if (total <= maxNumber) {
            totalFrames = uint32_t(total);
            droppedFrames = uint32_t(stats->GetDroppedFrames());
          } else {
            // Too big number(s) -> Resize everything to fit in 32 bits.
            double ratio = double(maxNumber) / double(total);
            totalFrames = maxNumber;  // === total * ratio
            droppedFrames = uint32_t(double(stats->GetDroppedFrames()) * ratio);
          }
        }
      }
      if (!StaticPrefs::media_video_dropped_frame_stats_enabled()) {
        droppedFrames = 0;
      }
    }
  }

  RefPtr<VideoPlaybackQuality> playbackQuality =
      new VideoPlaybackQuality(this, creationTime, totalFrames, droppedFrames);
  return playbackQuality.forget();
}

void HTMLVideoElement::WakeLockRelease() {
  HTMLMediaElement::WakeLockRelease();
  ReleaseVideoWakeLockIfExists();
}

void HTMLVideoElement::UpdateWakeLock() {
  HTMLMediaElement::UpdateWakeLock();
  if (!mPaused) {
    CreateVideoWakeLockIfNeeded();
  } else {
    ReleaseVideoWakeLockIfExists();
  }
}

bool HTMLVideoElement::ShouldCreateVideoWakeLock() const {
  if (!StaticPrefs::media_video_wakelock()) {
    return false;
  }
  // Only request wake lock for video with audio or video from media
  // stream, because non-stream video without audio is often used as a
  // background image.
  //
  // Some web conferencing sites route audio outside the video element,
  // and would not be detected unless we check for media stream, so do
  // that below.
  //
  // Media streams generally aren't used as background images, though if
  // they were we'd get false positives. If this is an issue, we could
  // check for media stream AND document has audio playing (but that was
  // tricky to do).
  return HasVideo() && (mSrcStream || HasAudio());
}

void HTMLVideoElement::CreateVideoWakeLockIfNeeded() {
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return;
  }
  if (!mScreenWakeLock && ShouldCreateVideoWakeLock()) {
    RefPtr<power::PowerManagerService> pmService =
        power::PowerManagerService::GetInstance();
    NS_ENSURE_TRUE_VOID(pmService);

    ErrorResult rv;
    mScreenWakeLock = pmService->NewWakeLock(u"video-playing"_ns,
                                             OwnerDoc()->GetInnerWindow(), rv);
  }
}

void HTMLVideoElement::ReleaseVideoWakeLockIfExists() {
  if (mScreenWakeLock) {
    ErrorResult rv;
    mScreenWakeLock->Unlock(rv);
    rv.SuppressException();
    mScreenWakeLock = nullptr;
    return;
  }
}

bool HTMLVideoElement::SetVisualCloneTarget(
    RefPtr<HTMLVideoElement> aVisualCloneTarget,
    RefPtr<Promise> aVisualCloneTargetPromise) {
  MOZ_DIAGNOSTIC_ASSERT(!mVisualCloneSource,
                        "Can't clone a video element that is already a clone.");
  if (!aVisualCloneTarget || !mVisualCloneSource) {
    mVisualCloneTarget = std::move(aVisualCloneTarget);
    mVisualCloneTargetPromise = std::move(aVisualCloneTargetPromise);
    return true;
  }
  return false;
}

bool HTMLVideoElement::SetVisualCloneSource(
    RefPtr<HTMLVideoElement> aVisualCloneSource) {
  MOZ_DIAGNOSTIC_ASSERT(!mVisualCloneTarget,
                        "Can't clone a video element that is already a "
                        "clone.");
  if (!aVisualCloneSource || !mVisualCloneTarget) {
    mVisualCloneSource = std::move(aVisualCloneSource);
    return true;
  }
  return false;
}

/* static */
already_AddRefed<gfx::DataSourceSurface> HTMLVideoElement::CopyImage(
    layers::Image* aImage) {
  RefPtr<gfx::SourceSurface> surface = aImage->GetAsSourceSurface();
  if (!surface) {
    return nullptr;
  }

  RefPtr<gfx::DataSourceSurface> data = surface->GetDataSurface();
  if (!data) {
    return nullptr;
  }

  gfx::DataSourceSurface::ScopedMap read(data, gfx::DataSourceSurface::READ);
  if (!read.IsMapped()) {
    return nullptr;
  }

  RefPtr<gfx::DataSourceSurface> copy = gfx::CreateDataSourceSurfaceFromData(
      data->GetSize(), data->GetFormat(), read.GetData(), read.GetStride());

  MOZ_ASSERT_IF(copy, data->GetSize() == copy->GetSize());
  MOZ_ASSERT_IF(copy, data->GetFormat() == copy->GetFormat());
  return copy.forget();
}

/* static */
bool HTMLVideoElement::IsVideoStatsEnabled() {
  return StaticPrefs::media_video_stats_enabled();
}

double HTMLVideoElement::TotalPlayTime() const {
  double total = 0.0;

  if (mPlayed) {
    uint32_t timeRangeCount = mPlayed->Length();

    for (uint32_t i = 0; i < timeRangeCount; i++) {
      double begin = mPlayed->Start(i);
      double end = mPlayed->End(i);
      total += end - begin;
    }

    if (mCurrentPlayRangeStart != -1.0) {
      double now = CurrentTime();
      if (mCurrentPlayRangeStart != now) {
        total += now - mCurrentPlayRangeStart;
      }
    }
  }

  return total;
}

already_AddRefed<Promise> HTMLVideoElement::CloneElementVisually(
    HTMLVideoElement& aTargetVideo, ErrorResult& aRv) {
  MOZ_ASSERT(aTargetVideo.IsInComposedDoc(),
             "Can't clone to a video that's not bound to a DOM tree.");
  if (!aTargetVideo.IsInComposedDoc()) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow();
  if (!win) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(win->AsGlobal(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Do we already have a visual clone target? If so, shut it down.
  if (mVisualCloneTarget) {
    EndCloningVisually();
  }

  // If there's a poster set on the target video, clear it, otherwise
  // it'll display over top of the cloned frames.
  aTargetVideo.UnsetHTMLAttr(nsGkAtoms::poster, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  if (!SetVisualCloneTarget(&aTargetVideo, promise)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  if (!aTargetVideo.SetVisualCloneSource(this)) {
    mVisualCloneTarget = nullptr;
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  aTargetVideo.SetMediaInfo(mMediaInfo);

  if (IsInComposedDoc() && !StaticPrefs::media_cloneElementVisually_testing()) {
    NotifyUAWidgetSetupOrChange();
  }

  MaybeBeginCloningVisually();

  return promise.forget();
}

void HTMLVideoElement::StopCloningElementVisually() {
  if (mVisualCloneTarget) {
    EndCloningVisually();
  }
}

void HTMLVideoElement::MaybeBeginCloningVisually() {
  if (!mVisualCloneTarget) {
    return;
  }

  if (mDecoder) {
    mDecoder->SetSecondaryVideoContainer(
        mVisualCloneTarget->GetVideoFrameContainer());
    NotifyDecoderActivityChanges();
    UpdateMediaControlAfterPictureInPictureModeChanged();
  } else if (mSrcStream) {
    VideoFrameContainer* container =
        mVisualCloneTarget->GetVideoFrameContainer();
    if (container) {
      mSecondaryVideoOutput = MakeRefPtr<FirstFrameVideoOutput>(
          container, AbstractThread::MainThread());
      mVideoWatchManager.Watch(
          mSecondaryVideoOutput->mFirstFrameRendered,
          &HTMLVideoElement::OnSecondaryVideoOutputFirstFrameRendered);
      SetSecondaryMediaStreamRenderer(container, mSecondaryVideoOutput);
    }
    UpdateMediaControlAfterPictureInPictureModeChanged();
  }
}

void HTMLVideoElement::EndCloningVisually() {
  MOZ_ASSERT(mVisualCloneTarget);

  if (mDecoder) {
    mDecoder->SetSecondaryVideoContainer(nullptr);
    NotifyDecoderActivityChanges();
  } else if (mSrcStream) {
    if (mSecondaryVideoOutput) {
      mVideoWatchManager.Unwatch(
          mSecondaryVideoOutput->mFirstFrameRendered,
          &HTMLVideoElement::OnSecondaryVideoOutputFirstFrameRendered);
      mSecondaryVideoOutput = nullptr;
    }
    SetSecondaryMediaStreamRenderer(nullptr);
  }

  (void)mVisualCloneTarget->SetVisualCloneSource(nullptr);
  (void)SetVisualCloneTarget(nullptr);

  UpdateMediaControlAfterPictureInPictureModeChanged();

  if (IsInComposedDoc() && OwnerDoc()->IsCurrentActiveDocument() &&
      !StaticPrefs::media_cloneElementVisually_testing()) {
    NotifyUAWidgetSetupOrChange();
  }

  ClosePictureInPictureWindowAndFireEvent();
}

void HTMLVideoElement::OnSecondaryVideoContainerInstalled(
    const RefPtr<VideoFrameContainer>& aSecondaryContainer) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT_IF(mVisualCloneTargetPromise, mVisualCloneTarget);
  if (!mVisualCloneTargetPromise) {
    // Clone target was unset.
    return;
  }

  VideoFrameContainer* container = mVisualCloneTarget->GetVideoFrameContainer();
  if (NS_WARN_IF(container != aSecondaryContainer)) {
    // Not the right container.
    return;
  }

  NS_DispatchToCurrentThread(NewRunnableMethod(
      "Promise::MaybeResolveWithUndefined", mVisualCloneTargetPromise,
      &Promise::MaybeResolveWithUndefined));
  mVisualCloneTargetPromise = nullptr;
}

void HTMLVideoElement::OnSecondaryVideoOutputFirstFrameRendered() {
  OnSecondaryVideoContainerInstalled(
      mVisualCloneTarget->GetVideoFrameContainer());
}

void HTMLVideoElement::OnVisibilityChange(Visibility aNewVisibility) {
  HTMLMediaElement::OnVisibilityChange(aNewVisibility);

  // See the alternative part after step 4, but we only pause/resume invisible
  // autoplay for non-audible video, which is different from the spec. This
  // behavior seems aiming to reduce the power consumption without interering
  // users, and Chrome and Safari also chose to do that only for non-audible
  // video, so we want to match them in order to reduce webcompat issue.
  // https://html.spec.whatwg.org/multipage/media.html#ready-states:eligible-for-autoplay-2
  if (!HasAttr(nsGkAtoms::autoplay) || IsAudible()) {
    return;
  }

  if (aNewVisibility == Visibility::ApproximatelyVisible && mPaused &&
      IsEligibleForAutoplay() && AllowedToPlay()) {
    LOG("resume invisible paused autoplay video");
    RunAutoplay();
  }

  // We need to consider the Pip window as well, which won't reflect in the
  // visibility event.
  if ((aNewVisibility == Visibility::ApproximatelyNonVisible &&
       !IsCloningElementVisually()) &&
      mCanAutoplayFlag) {
    // Defer pausing the element to avoid changing the element state during the
    // style refresh.
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        __func__, [self = RefPtr<HTMLMediaElement>(this), this] {
          // https://html.spec.whatwg.org/multipage/media.html#ready-states:intersect-the-viewport-3
          if (mVisibilityState != Visibility::ApproximatelyNonVisible ||
              !mCanAutoplayFlag) {
            return;
          }
          LOG("pause non-audible autoplay video when it's invisible");
          PauseInternal();
          mCanAutoplayFlag = true;
        }));
    return;
  }
}

void HTMLVideoElement::ResetState() {
  HTMLMediaElement::ResetState();
  mLastPresentedFrameID = layers::kContainerFrameID_Invalid;
}

bool HTMLVideoElement::WillFireVideoFrameCallbacks(
    const TimeStamp& aNowTime, const Maybe<TimeStamp>& aNextTickTime,
    VideoFrameCallbackMetadata& aMd) {
  // Attempt to find the next image to be presented on this tick. Note that
  // composited will be accurate only if the element is visible.
  AutoTArray<ImageContainer::OwningImage, 4> images;
  if (RefPtr<layers::ImageContainer> container = GetImageContainer()) {
    container->GetCurrentImages(&images);
  }

  // If we did not find any current images, we must have fired too early, or we
  // are in the process of shutting down. Wait for the next invalidation.
  if (images.IsEmpty()) {
    return false;
  }

  // We are guaranteed that the images are in timestamp order. It is possible we
  // are already behind if the compositor notifications have not been processed
  // yet, so as per the standard, this is a best effort attempt at synchronizing
  // with the state of the GPU process.
  const ImageContainer::OwningImage* selected = nullptr;
  bool composited = false;
  for (const auto& image : images) {
    if (image.mTimeStamp <= aNowTime) {
      // Image should already have been composited. Because we might not be in
      // the display list, we cannot rely upon its mComposited status, and
      // should just assume it has indeed been composited.
      selected = &image;
      composited = true;
    } else if (!aNextTickTime || image.mTimeStamp <= aNextTickTime.ref()) {
      // Image should be the next to be composited. mComposited will be false
      // if the compositor hasn't rendered the frame yet or notified us of the
      // render yet, but it is in progress. If it is true, then we know the
      // next vsync will display the frame.
      selected = &image;
      composited = false;
    } else {
      // Image is for a future composition.
      break;
    }
  }

  // If all of the available images are for future compositions, we must have
  // fired too early. Wait for the next invalidation.
  if (!selected || selected->mFrameID == layers::kContainerFrameID_Invalid ||
      selected->mFrameID == mLastPresentedFrameID) {
    return false;
  }

  // If we have got a dummy frame, then we must have suspended decoding and have
  // no actual frame to present. This should only happen if we raced on
  // requesting a callback, and the media state machine advancing.
  gfx::IntSize frameSize = selected->mImage->GetSize();
  if (NS_WARN_IF(frameSize.IsEmpty())) {
    return false;
  }

  // If we have already displayed the expected frame, we need to make the
  // display time match the presentation time to indicate it is already
  // complete.
  if (composited) {
    aMd.mExpectedDisplayTime = aMd.mPresentationTime;
  }

  MOZ_ASSERT(!frameSize.IsEmpty());

  aMd.mWidth = frameSize.width;
  aMd.mHeight = frameSize.height;

  // If we were not provided a valid media time, then we need to estimate based
  // on the CurrentTime from the element.
  aMd.mMediaTime = selected->mMediaTime.IsValid()
                       ? selected->mMediaTime.ToSeconds()
                       : CurrentTime();

  // If we have a processing duration, we need to round it.
  //
  // https://wicg.github.io/video-rvfc/#security-and-privacy
  //
  // 5. Security and Privacy Considerations.
  // ... processingDuration exposes some under-the-hood performance information
  // about the video pipeline ... We therefore propose a resolution of 100μs,
  // which is still useful for automated quality analysis, but doesn’t offer any
  // new sources of high resolution information.
  if (selected->mProcessingDuration.IsValid()) {
    aMd.mProcessingDuration.Construct(
        selected->mProcessingDuration.ToBase(10000).ToSeconds());
  }

#ifdef MOZ_WEBRTC
  // If given, this is the RTP timestamp from the last packet for the frame.
  if (selected->mRtpTimestamp) {
    aMd.mRtpTimestamp.Construct(*selected->mRtpTimestamp);
  }

  // For remote sources, the capture and receive time are represented as WebRTC
  // timestamps relative to an origin that is specific to the WebRTC session.
  const bool hasCaptureTimeNtp = selected->mWebrtcCaptureTime.is<int64_t>();
  const bool hasReceiveTimeReal = selected->mWebrtcReceiveTime.isSome();
  const auto* tsMaker = mSelectedVideoStreamTrack
                            ? mSelectedVideoStreamTrack->GetTimestampMaker()
                            : nullptr;
  auto* win = OwnerDoc()->GetInnerWindow();
  auto* perf = win ? win->GetPerformance() : nullptr;

  // WebRTC timestamps are converted to document-local high resolution
  // timestamps to have a shared time base (see bug 1937776)
  if (tsMaker && perf) {
    if (hasCaptureTimeNtp) {
      const int64_t capt64 = selected->mWebrtcCaptureTime.as<int64_t>();
      webrtc::Timestamp captTs = webrtc::Timestamp::Millis(capt64);
      aMd.mCaptureTime.Construct(
          RTCStatsTimestamp::FromNtp(*tsMaker, captTs).ToDom() -
          perf->TimeOrigin());
    }

    if (hasReceiveTimeReal) {
      const int64_t recvUs = selected->mWebrtcReceiveTime.value();
      webrtc::Timestamp recvTs = webrtc::Timestamp::Micros(recvUs);
      aMd.mReceiveTime.Construct(
          RTCStatsTimestamp::FromRealtime(*tsMaker, recvTs).ToDom() -
          perf->TimeOrigin());
    }
  }

  // The capture time may already be a high resolution timestamp from the
  // camera pipeline indicating when the sample was captured.
  if (perf && selected->mWebrtcCaptureTime.is<TimeStamp>()) {
    aMd.mCaptureTime.Construct(perf->TimeStampToDOMHighResForRendering(
        selected->mWebrtcCaptureTime.as<TimeStamp>()));
  }
#endif

  // Note that if we seek, or restart a video, we may present an earlier frame
  // that we already presented with the same ID. This would cause presented
  // frames to go backwards when it must be monotonically increasing. Presented
  // frames cannot simply increment by 1 each request callback because it is
  // also used by the caller to determine if frames were missed. As such, we
  // will typically use the difference between the current frame and the last
  // presented via the callback, but otherwise assume a single frame due to the
  // seek.
  mPresentedFrames +=
      selected->mFrameID > 1 && selected->mFrameID > mLastPresentedFrameID
          ? selected->mFrameID - mLastPresentedFrameID
          : 1;
  mLastPresentedFrameID = selected->mFrameID;

  // Presented frames is a bit of a misnomer from a rendering perspective,
  // because we still need to advance regardless of composition. Video elements
  // that are outside of the DOM, or are not visible, still advance the video in
  // the background, and presumably the caller still needs some way to know how
  // many frames we have advanced.
  aMd.mPresentedFrames = mPresentedFrames;

  NS_DispatchToMainThread(NewRunnableMethod(
      "HTMLVideoElement::FinishedVideoFrameRequestCallbacks", this,
      &HTMLVideoElement::FinishedVideoFrameRequestCallbacks));

  return true;
}

void HTMLVideoElement::FinishedVideoFrameRequestCallbacks() {
  // After we have executed the rVFC and rAF callbacks, we need to check whether
  // or not we have scheduled more. If we did not, then we need to notify the
  // decoder, because it may be the only thing keeping the decoder fully active.
  if (!HasPendingCallbacks()) {
    NotifyDecoderActivityChanges();
  }
}

uint32_t HTMLVideoElement::RequestVideoFrameCallback(
    VideoFrameRequestCallback& aCallback, ErrorResult& aRv) {
  bool hasPending = HasPendingCallbacks();
  uint32_t handle = 0;
  aRv = mVideoFrameRequestManager.Schedule(aCallback, &handle);
  if (!hasPending && HasPendingCallbacks()) {
    NotifyDecoderActivityChanges();
  }
  return handle;
}

void HTMLVideoElement::CancelVideoFrameCallback(uint32_t aHandle) {
  if (mVideoFrameRequestManager.Cancel(aHandle) && !HasPendingCallbacks()) {
    NotifyDecoderActivityChanges();
  }
}

void HTMLVideoElement::ClosePictureInPictureWindowAndFireEvent() {
  // If this is not GetPictureInPictureElementInternal, then
  // we did cloned this video using our native feature.
  if (OwnerDoc()->GetPictureInPictureElementInternal() != this) {
    return;
  }

  // TODO: address PIP spec. so that "Unset pictureInPictureElement" is done
  // before firing event as this can trigger script to run.
  // https://w3c.github.io/picture-in-picture/#exit-pip
  // 4. Unset pictureInPictureElement.
  OwnerDoc()->SetPictureInPictureElement(nullptr);
  if (RefPtr<PictureInPictureWindow> pipWindow =
          std::move(mPictureInPictureWindow)) {
    pipWindow->Close();
  }
}

// https://w3c.github.io/picture-in-picture/#dom-htmlvideoelement-requestpictureinpicture
already_AddRefed<Promise> HTMLVideoElement::RequestPictureInPicture(
    ErrorResult& aRv) {
  PictureInPictureService::EnsureInit();
  // 9. Let p be a new promise created in this’s relevant realm.
  RefPtr<Promise> p = Promise::Create(GetRelevantGlobal(), aRv);
  if (!p) {
    return nullptr;
  }

  // 1. If Picture-in-Picture support is false, return a promise rejected with
  // NotSupportedError DOMException.
  if (!PictureInPictureWindow::PictureInPictureEnabled()) {
    p->MaybeRejectWithNotSupportedError("Picture-In-Picture is not enabled");
    return p.forget();
  }

  // 2. If this’s node document is not allowed to use the policy-controlled
  // feature named "picture-in-picture", reject p with a SecurityError
  // exception and return p.
  if (!FeaturePolicyUtils::IsFeatureAllowed(OwnerDoc(),
                                            u"picture-in-picture"_ns)) {
    p->MaybeRejectWithSecurityError(
        "Permissions policy: picture-in-picture not allowed");
    return p.forget();
  }

  // 3. If this’s readyState attribute is HAVE_NOTHING, reject p with an
  // InvalidStateError exception and return p.
  if (ReadyState() == HTMLMediaElement_Binding::HAVE_NOTHING) {
    p->MaybeRejectWithInvalidStateError("Video readyState is HAVE_NOTHING");
    return p.forget();
  }

  // 4. If this has no video track, reject p with an InvalidStateError
  // exception and return p.
  if (!HasVideo()) {
    p->MaybeRejectWithInvalidStateError("Video element has no video track");
    return p.forget();
  }

  // 5. If this’s disablePictureInPicture is true, the user agent MAY reject p
  // with an InvalidStateError exception and return p.
  if (DisablePictureInPicture()) {
    p->MaybeRejectWithInvalidStateError(
        "Picture-in-Picture is disabled on this video");
    return p.forget();
  }

  // 6. If pictureInPictureElement is null:
  // 6.1. If this’s relevant global object does not have a transient
  // activation reject p with a NotAllowedError exception and return p.
  // 6.2. Consume user activation given this’s relevant global object.
  Document* doc = OwnerDoc();
  const Element* pictureInPictureElement =
      doc->GetPictureInPictureElementInternal();
  if (!pictureInPictureElement &&
      !doc->ConsumeTransientUserGestureActivation()) {
    p->MaybeRejectWithNotAllowedError(
        "Picture-in-Picture requires user activation");
    return p.forget();
  }

  // 7. If this is pictureInPictureElement:
  if (this == pictureInPictureElement) {
    // 1. Return a promise resolved with with the Picture-in-Picture window
    // associated with pictureInPictureElement.
    auto pipWindow = mPictureInPictureWindow;
    p->MaybeResolve(pipWindow);
    return p.forget();
  }

  // 8. Let global be this’s relevant global object.
  // 9. Return promise, and run the remaining steps in parallel:
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [promise = RefPtr{p},
                 video = RefPtr{this}]() MOZ_CAN_RUN_SCRIPT_BOUNDARY {
        PictureInPictureService::OpenPictureInPictureWindow(promise, video);
      }));
  return p.forget();
}

// Picture-in-Picture event handlers
EventHandlerNonNull* HTMLVideoElement::GetOnenterpictureinpicture() {
  return EventTarget::GetEventHandler(nsGkAtoms::onenterpictureinpicture);
}

void HTMLVideoElement::SetOnenterpictureinpicture(
    EventHandlerNonNull* aCallback) {
  EventTarget::SetEventHandler(nsGkAtoms::onenterpictureinpicture, aCallback);
}

EventHandlerNonNull* HTMLVideoElement::GetOnleavepictureinpicture() {
  return EventTarget::GetEventHandler(nsGkAtoms::onleavepictureinpicture);
}

void HTMLVideoElement::SetOnleavepictureinpicture(
    EventHandlerNonNull* aCallback) {
  EventTarget::SetEventHandler(nsGkAtoms::onleavepictureinpicture, aCallback);
}

void HTMLVideoElement::SetAssociatedPictureInPictureWindow(
    PictureInPictureWindow* aWindow) {
  mPictureInPictureWindow = aWindow;
}

PictureInPictureWindow* HTMLVideoElement::GetAssociatedPictureInPictureWindow()
    const {
  return mPictureInPictureWindow;
}

}  // namespace mozilla::dom

#undef LOG
