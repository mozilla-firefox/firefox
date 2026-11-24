/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_MultipartImage_h
#define mozilla_image_MultipartImage_h

#include "IProgressObserver.h"
#include "ImageWrapper.h"
#include "ProgressTracker.h"

namespace mozilla {
namespace image {

class NextPartObserver;

/**
 * An Image wrapper that implements support for multipart/x-mixed-replace
 * images.
 */
class MultipartImage : public ImageWrapper, public IProgressObserver {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(MultipartImage)
  // We need to always declare refcounting here, because
  // IProgressObserver has pure-virtual refcounting.
  NS_DECL_ISUPPORTS_INHERITED

  void BeginTransitionToPart(Image* aNextPart);

  // Overridden ImageWrapper methods:
  virtual already_AddRefed<imgIContainer> Unwrap() override;
  virtual already_AddRefed<ProgressTracker> GetProgressTracker() override;
  virtual void SetProgressTracker(ProgressTracker* aTracker) override;
  virtual nsresult OnImageDataAvailable(nsIRequest* aRequest,
                                        nsIInputStream* aInStr,
                                        uint64_t aSourceOffset,
                                        uint32_t aCount) override;
  virtual nsresult OnImageDataComplete(nsIRequest* aRequest, nsresult aStatus,
                                       bool aLastPart) override;

  // We don't support locking or track animation consumers for individual parts,
  // so we override these methods to do nothing.
  NS_IMETHOD LockImage() override { return NS_OK; }
  NS_IMETHOD UnlockImage() override { return NS_OK; }
  virtual void IncrementAnimationConsumers() override {}
  virtual void DecrementAnimationConsumers() override {}
#ifdef DEBUG
  virtual uint32_t GetAnimationConsumers() override { return 1; }
#endif

  // Overridden IProgressObserver methods:
  virtual void Notify(int32_t aType, const nsIntRect* aRect = nullptr) override;
  virtual void OnLoadComplete(bool aLastPart) override;
  virtual void SetHasImage() override;
  virtual bool NotificationsDeferred() const override;
  virtual void MarkPendingNotify() override;
  virtual void ClearPendingNotify() override;

 protected:
  virtual ~MultipartImage();

 private:
  friend class ImageFactory;
  friend class NextPartObserver;

  explicit MultipartImage(Image* aFirstPart);
  void Init();

  void FinishTransition();
  void DropNextPart();

  RefPtr<ProgressTracker> mTracker;
  RefPtr<NextPartObserver> mNextPartObserver;
  RefPtr<Image> mNextPart;
  bool mPendingNotify : 1;
};

}  // namespace image
}  // namespace mozilla

#endif  // mozilla_image_MultipartImage_h
