/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_MediaCapabilities_h_
#define mozilla_dom_MediaCapabilities_h_

#include "DDLoggedTypeTraits.h"
#include "MediaResult.h"
#include "js/TypeDecls.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/MediaCapabilitiesBinding.h"
#include "mozilla/dom/MediaKeySystemAccessManager.h"
#include "mozilla/dom/NonRefcountedDOMObject.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsISupports.h"
#include "nsStringFwd.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;

namespace mozilla {
class ErrorResult;
class MediaContainerType;
class MediaExtendedMIMEType;
class TaskQueue;
class TrackInfo;

namespace layers {
class KnowsCompositor;
}
namespace mediacaps {
// Pref-driven behaviour flags resolved once per MediaCapabilities request.
struct BehaviorConfig {
  bool mLegacy = false;
  bool mWebRTCEnabled = true;
};
}  // namespace mediacaps
namespace dom {
class MediaCapabilities;
}  // namespace dom
DDLoggedTypeName(dom::MediaCapabilities);

namespace dom {

struct MediaDecodingConfiguration;
struct MediaEncodingConfiguration;
struct AudioConfiguration;
struct VideoConfiguration;
class Promise;

class MediaCapabilities final : public nsISupports, public nsWrapperCache {
 public:
  // Ref counting and cycle collection
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(MediaCapabilities)

  using CapabilitiesPromise =
      MozPromise<MediaCapabilitiesInfo, MediaResult, /* IsExclusive = */ true>;

  // WebIDL Methods
  already_AddRefed<Promise> DecodingInfo(
      const MediaDecodingConfiguration& aConfiguration, ErrorResult& aRv);
  already_AddRefed<Promise> EncodingInfo(
      const MediaEncodingConfiguration& aConfiguration, ErrorResult& aRv);
  // End WebIDL Methods

  explicit MediaCapabilities(nsIGlobalObject* aParent);

  // Asynchronously queries the platform decoder to determine video decoding
  // capabilities (supported, smooth, powerEfficient). Used by the DRM path for
  // software-encrypted content to determine the powerEfficient field, and also
  // used by the non-DRM video path to query the platform decoder.
  static RefPtr<CapabilitiesPromise> CheckVideoDecodingInfo(
      RefPtr<TaskQueue> aTaskQueue, RefPtr<layers::KnowsCompositor> aCompositor,
      float aFrameRate, bool aShouldResistFingerprinting,
      UniquePtr<TrackInfo> aConfig);

  nsIGlobalObject* GetParentObject() const { return mParent; }
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;
  bool CheckTypeForMediaSource(const MediaExtendedMIMEType& aType) const;
  bool CheckTypeForFile(const MediaExtendedMIMEType& aType) const;
  bool CheckTypeForEncoder(const MediaExtendedMIMEType& aType) const;

 private:
  virtual ~MediaCapabilities() = default;
  already_AddRefed<layers::KnowsCompositor> GetCompositor();
  void CreateMediaCapabilitiesDecodingInfo(
      const MediaDecodingConfiguration& aConfiguration, ErrorResult& aRv,
      Promise* aPromise, const mediacaps::BehaviorConfig& aBehavior);

  void CreateWebRTCDecodingInfo(
      const MediaDecodingConfiguration& aConfiguration, Promise* aPromise,
      Maybe<MediaContainerType> aVideoContainer,
      Maybe<MediaContainerType> aAudioContainer);
  void CreateNonWebRTCDecodingInfo(
      const MediaDecodingConfiguration& aConfiguration, Promise* aPromise,
      Maybe<MediaContainerType> aVideoContainer,
      Maybe<MediaContainerType> aAudioContainer);

  RefPtr<MediaKeySystemAccessManager::MediaKeySystemAccessPromise>
  CheckEncryptedDecodingSupport(
      const MediaDecodingConfiguration& aConfiguration);

  nsCOMPtr<nsIGlobalObject> mParent;
};

}  // namespace dom

}  // namespace mozilla

#endif /* mozilla_dom_MediaCapabilities_h_ */
