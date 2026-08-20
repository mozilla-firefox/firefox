/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLVideoElement_h
#define mozilla_dom_HTMLVideoElement_h

#include "ImageTypes.h"
#include "Units.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/HTMLMediaElement.h"
#include "mozilla/dom/VideoFrameProvider.h"

namespace mozilla {

class FrameStatistics;

namespace gfx {
class DataSourceSurface;
}

namespace dom {

class WakeLock;
class VideoPlaybackQuality;
class EventHandlerNonNull;
class PictureInPictureWindow;

class HTMLVideoElement final : public HTMLMediaElement {
  class SecondaryVideoOutput;

 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(HTMLVideoElement, HTMLMediaElement)

  typedef mozilla::dom::NodeInfo NodeInfo;

  explicit HTMLVideoElement(already_AddRefed<NodeInfo> aNodeInfo);

  NS_IMPL_FROMNODE_HTML_WITH_TAG(HTMLVideoElement, video)

  using HTMLMediaElement::GetPaused;

  HTMLVideoElement* AsHTMLVideoElement() override { return this; };

  void Invalidate(ImageSizeChanged aImageSizeChanged,
                  const Maybe<nsIntSize>& aNewIntrinsicSize,
                  ForceInvalidate aForceInvalidate) override;

  bool IsVideo() const override { return true; }

  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;
  NS_IMETHOD_(bool) IsAttributeMapped(const nsAtom* aAttribute) const override;

  void AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                    const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                    nsIPrincipal* aMaybeScriptedPrincipal,
                    bool aNotify) override;

  nsMapRuleToAttributesFunc GetAttributeMappingFunction() const override;

  nsresult Clone(NodeInfo*, nsINode** aResult) const override;

  nsresult CopyInnerTo(Element* aDest);

  void UnbindFromTree(UnbindContext&) override;

  mozilla::Maybe<mozilla::CSSIntSize> GetVideoSize() const;

  void UpdateMediaSize(const nsIntSize& aSize) override;

  nsresult SetAcceptHeader(nsIHttpChannel* aChannel) override;

  // Element
  bool IsInteractiveHTMLContent() const override;

  // WebIDL

  uint32_t Width() const {
    return GetDimensionAttrAsUnsignedInt(nsGkAtoms::width, 0);
  }

  void SetWidth(uint32_t aValue, ErrorResult& aRv) {
    SetUnsignedIntAttr(nsGkAtoms::width, aValue, 0, aRv);
  }

  uint32_t Height() const {
    return GetDimensionAttrAsUnsignedInt(nsGkAtoms::height, 0);
  }

  void SetHeight(uint32_t aValue, ErrorResult& aRv) {
    SetUnsignedIntAttr(nsGkAtoms::height, aValue, 0, aRv);
  }

  uint32_t VideoWidth() const;

  uint32_t VideoHeight() const;

  VideoRotation RotationDegrees() const { return mMediaInfo.mVideo.mRotation; }

  bool HasAlpha() const { return mMediaInfo.mVideo.HasAlpha(); }

  void GetPoster(nsAString& aValue) {
    GetURIAttr(nsGkAtoms::poster, nullptr, aValue);
  }
  void SetPoster(const nsAString& aValue, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::poster, aValue, aRv);
  }

  uint32_t MozParsedFrames() const;

  uint32_t MozDecodedFrames() const;

  uint32_t MozPresentedFrames();

  uint32_t MozPaintedFrames();

  double MozFrameDelay();

  bool MozHasAudio() const;

  already_AddRefed<VideoPlaybackQuality> GetVideoPlaybackQuality();

  already_AddRefed<Promise> CloneElementVisually(HTMLVideoElement& aTarget,
                                                 ErrorResult& rv);

  void StopCloningElementVisually();

  bool IsCloningElementVisually() const { return !!mVisualCloneTarget; }

  void OnSecondaryVideoContainerInstalled(
      const RefPtr<VideoFrameContainer>& aSecondaryContainer) override;

  void OnSecondaryVideoOutputFirstFrameRendered();

  void OnVisibilityChange(Visibility aNewVisibility) override;

  void ClosePictureInPictureWindowAndFireEvent();

  already_AddRefed<Promise> RequestPictureInPicture(ErrorResult& aRv);

  // Picture-in-Picture event handlers
  EventHandlerNonNull* GetOnenterpictureinpicture();
  void SetOnenterpictureinpicture(EventHandlerNonNull* aCallback);
  EventHandlerNonNull* GetOnleavepictureinpicture();
  void SetOnleavepictureinpicture(EventHandlerNonNull* aCallback);

  bool DisablePictureInPicture() const {
    return GetBoolAttr(nsGkAtoms::disablepictureinpicture);
  }

  void SetDisablePictureInPicture(bool aValue, ErrorResult& aError) {
    SetHTMLBoolAttr(nsGkAtoms::disablepictureinpicture, aValue, aError);
  }

  void SetAssociatedPictureInPictureWindow(PictureInPictureWindow* aWindow);
  PictureInPictureWindow* GetAssociatedPictureInPictureWindow() const;

 protected:
  virtual ~HTMLVideoElement();

  virtual JSObject* WrapNode(JSContext* aCx,
                             JS::Handle<JSObject*> aGivenProto) override;

  /**
   * We create video wakelock when the video is playing and release it when
   * video pauses. Note, the actual platform wakelock will automatically be
   * released when the page is in the background, so we don't need to check the
   * video's visibility by ourselves.
   */
  void WakeLockRelease() override;
  void UpdateWakeLock() override;

  bool ShouldCreateVideoWakeLock() const;
  void CreateVideoWakeLockIfNeeded();
  void ReleaseVideoWakeLockIfExists();

  gfx::IntSize GetVideoIntrinsicDimensions() const;

  RefPtr<WakeLock> mScreenWakeLock;

  WatchManager<HTMLVideoElement> mVideoWatchManager;

 private:
  bool SetVisualCloneTarget(
      RefPtr<HTMLVideoElement> aVisualCloneTarget,
      RefPtr<Promise> aVisualCloneTargetPromise = nullptr);
  bool SetVisualCloneSource(RefPtr<HTMLVideoElement> aVisualCloneSource);

  // Creates a new DataSourceSurface with data copied from aImage.
  // This is used when creating a static clone of this element, to ensure we
  // do not hold onto an output buffer from a decoder.
  static already_AddRefed<gfx::DataSourceSurface> CopyImage(
      layers::Image* aImage);

  // For video elements, we can clone the frames being played to
  // a secondary video element. If we're doing that, we hold a
  // reference to the video element we're cloning to in
  // mVisualCloneSource.
  //
  // Please don't set this to non-nullptr values directly - use
  // SetVisualCloneTarget() instead.
  RefPtr<HTMLVideoElement> mVisualCloneTarget;
  // Set when mVisualCloneTarget is set, and resolved (and unset) when the
  // secondary container has been applied to the underlying resource.
  RefPtr<Promise> mVisualCloneTargetPromise;
  // Set when beginning to clone visually and we are playing a MediaStream.
  // This is the output wrapping the VideoFrameContainer of mVisualCloneTarget,
  // so we can render its first frame, and resolve mVisualCloneTargetPromise as
  // we do.
  RefPtr<FirstFrameVideoOutput> mSecondaryVideoOutput;
  // If this video is the clone target of another video element,
  // then mVisualCloneSource points to that originating video
  // element.
  //
  // Please don't set this to non-nullptr values directly - use
  // SetVisualCloneTarget() instead.
  RefPtr<HTMLVideoElement> mVisualCloneSource;

  // Reference to the current PictureInPictureWindow for this video element
  RefPtr<PictureInPictureWindow> mPictureInPictureWindow;

 private:
  void ResetState() override;

  bool HasPendingCallbacks() const final {
    return !mVideoFrameRequestManager.IsEmpty();
  }

  VideoFrameRequestManager mVideoFrameRequestManager;
  layers::ContainerFrameID mLastPresentedFrameID =
      layers::kContainerFrameID_Invalid;
  uint32_t mPresentedFrames = 0;

 public:
  uint32_t RequestVideoFrameCallback(VideoFrameRequestCallback& aCallback,
                                     ErrorResult& aRv);
  void CancelVideoFrameCallback(uint32_t aHandle);
  // Returns false if the video element is not ready and callbacks shouldn't
  // be fired (yet).
  [[nodiscard]] bool WillFireVideoFrameCallbacks(
      const TimeStamp& aNowTime, const Maybe<TimeStamp>& aNextTickTime,
      VideoFrameCallbackMetadata& aMd);
  VideoFrameRequestManager& FrameRequestManager() {
    return mVideoFrameRequestManager;
  }
  void FinishedVideoFrameRequestCallbacks();

 private:
  static void MapAttributesIntoRule(MappedDeclarationsBuilder&);

  static bool IsVideoStatsEnabled();
  double TotalPlayTime() const;

  virtual void MaybeBeginCloningVisually() override;
  void EndCloningVisually();
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_HTMLVideoElement_h
