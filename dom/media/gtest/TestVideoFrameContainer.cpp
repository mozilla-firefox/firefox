/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageContainer.h"
#include "MockMediaDecoderOwner.h"
#include "TimeUnits.h"
#include "VideoFrameContainer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nsContentUtils.h"

using namespace mozilla;
using namespace mozilla::layers;

using testing::InSequence;
using testing::MockFunction;
using testing::StrEq;

TEST(TestVideoFrameContainer, UpdatePrincipalHandleForFrameID)
{
  auto owner = std::make_unique<MockMediaDecoderOwner>();
  PrincipalHandle principal(nsContentUtils::GetSystemPrincipal());
  RefPtr container = new VideoFrameContainer(
      owner.get(),
      MakeAndAddRef<ImageContainer>(ImageUsageType::VideoFrameContainer,
                                    ImageContainer::ASYNCHRONOUS));
  MockFunction<void(const char* name)> checkpoint;
  {
    InSequence s;
    EXPECT_CALL(checkpoint, Call(StrEq("id2 is first"))).Times(1);
    EXPECT_CALL(*owner, PrincipalHandleChangedForVideoFrameContainer(
                            container.get(), principal))
        .Times(1);
  }

  auto image = container->GetImageContainer()->CreatePlanarYCbCrImage();
  AutoTArray<ImageContainer::NonOwningImage, 2> images;
  images.AppendElements(2);
  TimeStamp timeStamp = TimeStamp::Now();
  for (auto& imageRef : images) {
    imageRef.mFrameID = container->NewFrameID();
    imageRef.mImage = image;
    imageRef.mTimeStamp = timeStamp;
    timeStamp += TimeDuration::FromSeconds(0.1);
  }
  gfx::IntSize intrinsicSize;
  container->SetCurrentFrames(intrinsicSize, images);

  ImageContainer::FrameID id2 = container->NewFrameID();
  container->UpdatePrincipalHandleForFrameID(principal, id2);
  images.RemoveElementAt(0);
  auto image2 = images.AppendElement();
  image2->mFrameID = id2;
  image2->mImage = image;
  image2->mTimeStamp = timeStamp;
  container->SetCurrentFrames(intrinsicSize, images);
  // Check no pending NotifyDecoderPrincipalChanged() event.
  NS_ProcessPendingEvents(nullptr);

  images.RemoveElementAt(0);
  container->SetCurrentFrames(intrinsicSize, images);
  checkpoint.Call("id2 is first");
  // Process NotifyDecoderPrincipalChanged() event.
  NS_ProcessPendingEvents(nullptr);
}
