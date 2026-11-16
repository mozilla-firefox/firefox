/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <functional>
#include <memory>

#include "base/process_util.h"
#include "gfxPlatform.h"
#include "gtest/gtest.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/RemoteTextureHostWrapper.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/webrender/RenderThread.h"

using namespace mozilla;
using namespace mozilla::layers;

namespace {

constexpr RemoteTextureTxnType kTestTxnType = 0xFFFFFFFE;

// Long enough to not be flaky, but well short of the 10 second owner wait.
constexpr double kPromptlyMs = 2000;

}  // namespace

class RemoteTextureMapTest : public ::testing::Test {
 protected:
  void SetUp() override {
    gfxPlatform::GetPlatform();
    if (!CompositorThread()) {
      GTEST_SKIP() << "No compositor thread";
    }
    // Pushing textures needs the render thread, which is in the GPU process
    // when there is one.
    if (!wr::RenderThread::Get()) {
      GTEST_SKIP() << "No render thread";
    }
    RunOnCompositorThread([&] {
      ASSERT_TRUE(RemoteTextureMap::Get());
      mClient = MakeRefPtr<RemoteTextureOwnerClient>(Pid());
    });
  }

  void TearDown() override {
    if (!mClient) {
      return;
    }
    RunOnCompositorThread([&] {
      for (const auto& [textureId, ownerId] : mWrappers) {
        Map()->UnregisterRemoteTextureHostWrapper(textureId, ownerId, Pid());
      }
      mWrappers.clear();
      mClient->UnregisterAllTextureOwners();
      mClient = nullptr;
    });
    // Let any deferred work from the tests run before the next test.
    RunOnCompositorThread([] {});
  }

  static void RunOnCompositorThread(std::function<void()>&& aTest) {
    RefPtr<Runnable> runnable = NS_NewRunnableFunction(
        "RemoteTextureMapTest", [test = std::move(aTest)]() { test(); });
    SyncRunnable::DispatchToThread(CompositorThread(), runnable);
  }

  static base::ProcessId Pid() { return base::GetCurrentProcId(); }
  static RemoteTextureMap* Map() { return RemoteTextureMap::Get(); }

  RefPtr<TextureHost> CreateWrapper(RemoteTextureId aTextureId,
                                    RemoteTextureOwnerId aOwnerId) {
    mWrappers.emplace_back(aTextureId, aOwnerId);
    return Map()->GetOrCreateRemoteTextureHostWrapper(aTextureId, aOwnerId,
                                                      Pid(), gfx::IntSize(1, 1),
                                                      TextureFlags::NO_FLAGS);
  }

  void ReleaseWrapper(RemoteTextureId aTextureId,
                      RemoteTextureOwnerId aOwnerId) {
    std::erase(mWrappers, std::pair(aTextureId, aOwnerId));
    Map()->UnregisterRemoteTextureHostWrapper(aTextureId, aOwnerId, Pid());
  }

  static RemoteTextureInfo WaitingInfo(RemoteTextureId aTextureId,
                                       RemoteTextureOwnerId aOwnerId) {
    return RemoteTextureInfo(aTextureId, aOwnerId, Pid(),
                             /* aWaitForRemoteTextureOwner */ true);
  }

  // Callbacks may outlive a failed test, so count on the heap.
  static std::function<void(const RemoteTextureInfo&)> Counter(
      const std::shared_ptr<int>& aCount) {
    return [aCount](const RemoteTextureInfo&) { ++*aCount; };
  }

  RefPtr<RemoteTextureOwnerClient> mClient;
  std::vector<std::pair<RemoteTextureId, RemoteTextureOwnerId>> mWrappers;
};

TEST_F(RemoteTextureMapTest, UnregisteredOwnerIsNotWaitedFor) {
  RunOnCompositorThread([&] {
    const auto ownerId = RemoteTextureOwnerId::GetNext();
    const auto textureId = RemoteTextureId::GetNext();

    mClient->RegisterTextureOwner(ownerId);
    RefPtr<TextureHost> wrapper = CreateWrapper(textureId, ownerId);
    mClient->UnregisterTextureOwner(ownerId);

    const auto start = TimeStamp::Now();
    EXPECT_FALSE(Map()->WaitForRemoteTextureOwner(
        wrapper->AsRemoteTextureHostWrapper()));
    EXPECT_FALSE(
        Map()->WaitRemoteTextureReady(WaitingInfo(textureId, ownerId)));
    EXPECT_LT((TimeStamp::Now() - start).ToMilliseconds(), kPromptlyMs);

    auto called = std::make_shared<int>(0);
    EXPECT_TRUE(Map()->CheckRemoteTextureReady(WaitingInfo(textureId, ownerId),
                                               this, Counter(called)));
    EXPECT_EQ(*called, 0);
  });
}

TEST_F(RemoteTextureMapTest, NewWrapperForUnregisteredOwnerIsNotWaitedFor) {
  RunOnCompositorThread([&] {
    const auto ownerId = RemoteTextureOwnerId::GetNext();
    const auto firstTextureId = RemoteTextureId::GetNext();
    const auto lateTextureId = RemoteTextureId::GetNext();

    mClient->RegisterTextureOwner(ownerId);
    CreateWrapper(firstTextureId, ownerId);
    mClient->UnregisterTextureOwner(ownerId);

    // A texture for the owner arrives after it went away, while an earlier
    // texture of it is still in use.
    RefPtr<TextureHost> late = CreateWrapper(lateTextureId, ownerId);
    const auto start = TimeStamp::Now();
    EXPECT_FALSE(
        Map()->WaitForRemoteTextureOwner(late->AsRemoteTextureHostWrapper()));
    EXPECT_LT((TimeStamp::Now() - start).ToMilliseconds(), kPromptlyMs);

    auto called = std::make_shared<int>(0);
    EXPECT_TRUE(Map()->CheckRemoteTextureReady(
        WaitingInfo(lateTextureId, ownerId), this, Counter(called)));
  });
}

TEST_F(RemoteTextureMapTest, NotYetRegisteredOwnerIsWaitedFor) {
  RunOnCompositorThread([&] {
    const auto ownerId = RemoteTextureOwnerId::GetNext();
    const auto textureId = RemoteTextureId::GetNext();

    CreateWrapper(textureId, ownerId);
    auto called = std::make_shared<int>(0);
    EXPECT_FALSE(Map()->CheckRemoteTextureReady(WaitingInfo(textureId, ownerId),
                                                this, Counter(called)));

    mClient->RegisterTextureOwner(ownerId);
    EXPECT_EQ(*called, 0);
    mClient->PushDummyTexture(textureId, ownerId);
    EXPECT_EQ(*called, 1);
  });
}

TEST_F(RemoteTextureMapTest, ReleasedWrapperIsReady) {
  RunOnCompositorThread([&] {
    const auto ownerId = RemoteTextureOwnerId::GetNext();
    const auto textureId = RemoteTextureId::GetNext();

    CreateWrapper(textureId, ownerId);
    ReleaseWrapper(textureId, ownerId);

    auto called = std::make_shared<int>(0);
    EXPECT_TRUE(Map()->CheckRemoteTextureReady(WaitingInfo(textureId, ownerId),
                                               this, Counter(called)));
    EXPECT_EQ(*called, 0);
  });
}

TEST_F(RemoteTextureMapTest, UnregisteredOwnerIsForgottenWithItsWrappers) {
  RunOnCompositorThread([&] {
    const auto ownerId = RemoteTextureOwnerId::GetNext();
    const auto firstTextureId = RemoteTextureId::GetNext();
    const auto laterTextureId = RemoteTextureId::GetNext();

    mClient->RegisterTextureOwner(ownerId);
    CreateWrapper(firstTextureId, ownerId);
    mClient->UnregisterTextureOwner(ownerId);
    ReleaseWrapper(firstTextureId, ownerId);

    // Once nothing referred to the owner, nothing is kept about it, so a later
    // texture for it waits as if the owner was not registered yet.
    CreateWrapper(laterTextureId, ownerId);
    auto called = std::make_shared<int>(0);
    EXPECT_FALSE(Map()->CheckRemoteTextureReady(
        WaitingInfo(laterTextureId, ownerId), this, Counter(called)));
    Map()->CancelRemoteTextureReadyCheck(WaitingInfo(laterTextureId, ownerId),
                                         this);
  });
}

TEST_F(RemoteTextureMapTest, CancelOnlyCancelsTheRequestersChecks) {
  RunOnCompositorThread([&] {
    const auto ownerId = RemoteTextureOwnerId::GetNext();
    const auto textureId = RemoteTextureId::GetNext();
    auto requesterA = std::make_shared<int>(0);
    auto requesterB = std::make_shared<int>(0);

    CreateWrapper(textureId, ownerId);
    EXPECT_FALSE(Map()->CheckRemoteTextureReady(WaitingInfo(textureId, ownerId),
                                                requesterA.get(),
                                                Counter(requesterA)));
    EXPECT_FALSE(Map()->CheckRemoteTextureReady(WaitingInfo(textureId, ownerId),
                                                requesterB.get(),
                                                Counter(requesterB)));

    Map()->CancelRemoteTextureReadyCheck(WaitingInfo(textureId, ownerId),
                                         requesterA.get());

    mClient->RegisterTextureOwner(ownerId);
    mClient->PushDummyTexture(textureId, ownerId);
    EXPECT_EQ(*requesterA, 0);
    EXPECT_EQ(*requesterB, 1);
  });
}

TEST_F(RemoteTextureMapTest, ContextLossWakesReadyChecks) {
  RunOnCompositorThread([&] {
    const auto ownerId = RemoteTextureOwnerId::GetNext();
    const auto textureId = RemoteTextureId::GetNext();

    mClient->RegisterTextureOwner(ownerId);
    CreateWrapper(textureId, ownerId);
    auto called = std::make_shared<int>(0);
    EXPECT_FALSE(Map()->CheckRemoteTextureReady(WaitingInfo(textureId, ownerId),
                                                this, Counter(called)));

    mClient->NotifyContextLost();
    EXPECT_EQ(*called, 1);
    EXPECT_TRUE(Map()->CheckRemoteTextureReady(WaitingInfo(textureId, ownerId),
                                               this, Counter(called)));
  });
}

TEST_F(RemoteTextureMapTest, DeferredUnregisterWakesReadyChecks) {
  RefPtr<RemoteTextureTxnScheduler> scheduler;
  RunOnCompositorThread([&] {
    const auto ownerId = RemoteTextureOwnerId::GetNext();
    const auto textureId = RemoteTextureId::GetNext();
    scheduler = Map()->RegisterTxnScheduler(Pid(), kTestTxnType);

    mClient->RegisterTextureOwner(ownerId);
    CreateWrapper(textureId, ownerId);
    auto called = std::make_shared<int>(0);
    EXPECT_FALSE(Map()->CheckRemoteTextureReady(WaitingInfo(textureId, ownerId),
                                                this, Counter(called)));

    EXPECT_TRUE(mClient->WaitForTxn(ownerId, kTestTxnType, 1));
    mClient->UnregisterTextureOwner(ownerId);
    EXPECT_EQ(*called, 1);
    EXPECT_TRUE(Map()->CheckRemoteTextureReady(WaitingInfo(textureId, ownerId),
                                               this, Counter(called)));

    scheduler->NotifyTxn(1);
  });
  RunOnCompositorThread([&] { scheduler = nullptr; });
}
