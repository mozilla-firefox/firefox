/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GLScreenBuffer.h"

#include "CompositorTypes.h"
#include "GLContext.h"
#include "MozFramebuffer.h"
#include "SharedSurface.h"
#include "gfx2DGlue.h"
#include "mozilla/gfx/BuildConstants.h"

namespace mozilla::gl {

// -
// SwapChainPresenter

UniquePtr<SwapChainPresenter> SwapChain::Acquire(
    const gfx::IntSize& size, const gfx::ColorSpace2 colorSpace,
    const gfx::TransferFunction transferFunction) {
  MOZ_ASSERT(mFactory);

  std::shared_ptr<SharedSurface> surf;
  if (!mPool.empty()) {
    // Try reuse
    const auto& existingDesc = mPool.front()->mDesc;
    auto newDesc = existingDesc;
    newDesc.size = size;
    newDesc.colorSpace = mFactory->GetFactoryCspace(colorSpace);
    newDesc.transferFunction = transferFunction;
    if (newDesc != existingDesc || !mPool.front()->IsValid()) {
      mPool = {};
    }
  }

  // When pooling is disabled, recycling of SharedSurfaces is managed by the
  // owner of the SwapChain by calling StoreRecycledSurface().
  if (!mPool.empty() && (!mPoolLimit || mPool.size() >= mPoolLimit)) {
    surf = mPool.front();
    mPool.pop();
  }
  if (!surf) {
    auto uniquePtrSurf = mFactory->CreateShared(size, colorSpace);
    if (!uniquePtrSurf) return nullptr;
    surf.reset(uniquePtrSurf.release());
  }
  if (mPoolLimit > 0) {
    mPool.push(surf);
    while (mPool.size() > mPoolLimit) {
      mPool.pop();
    }
  }

  auto ret = std::make_unique<SwapChainPresenter>(*this);
  const auto old = ret->SwapBackBuffer(std::move(surf));
  MOZ_ALWAYS_TRUE(!old);
  return ret;
}

void SwapChain::ClearPool() {
  mPool = {};
  mPrevFrontBuffer = nullptr;
}

bool SwapChain::StoreRecycledSurface(
    const std::shared_ptr<SharedSurface>& surf) {
  MOZ_ASSERT(mFactory);
  // Don't allow external recycled surfaces if SwapChain is managing own pool.
  MOZ_ASSERT(!mPoolLimit);
  if (mPoolLimit > 0 || !mFactory ||
      NS_WARN_IF(surf->mDesc.gl != mFactory->mDesc.gl)) {
    // Ensure we don't accidentally store an expired shared surface or from a
    // different context.
    return false;
  }
  mPool.push(surf);
  return true;
}

// -

SwapChainPresenter::SwapChainPresenter(SwapChain& swapChain)
    : mSwapChain(&swapChain) {
  MOZ_RELEASE_ASSERT(mSwapChain->mPresenter == nullptr);
  mSwapChain->mPresenter = this;
}

SwapChainPresenter::~SwapChainPresenter() {
  if (!mSwapChain) return;
  MOZ_RELEASE_ASSERT(mSwapChain->mPresenter == this);
  mSwapChain->mPresenter = nullptr;

  auto newFront = SwapBackBuffer(nullptr);
  if (newFront) {
    mSwapChain->mPrevFrontBuffer = mSwapChain->mFrontBuffer;
    mSwapChain->mFrontBuffer = std::move(newFront);
  }
}

std::shared_ptr<SharedSurface> SwapChainPresenter::SwapBackBuffer(
    std::shared_ptr<SharedSurface> back) {
  if (mBackBuffer) {
    mBackBuffer->EndWrite();
  }
  auto old = mBackBuffer;
  mBackBuffer = std::move(back);
  if (mBackBuffer) {
    mBackBuffer->BeginWrite();
  }
  return old;
}

GLuint SwapChainPresenter::Fb() const {
  if (!mBackBuffer) return 0;
  const auto& fb = mBackBuffer->mFb;
  if (!fb) return 0;
  return fb->mFB;
}

// -
// SwapChain

SwapChain::SwapChain()
    :  // We need to apply pooling on Android because of the AndroidSurface slow
       // destructor bugs. They cause a noticeable performance hit. See bug
       // #1646073.
      mPoolLimit(kIsAndroid ? 4 : 0) {}

SwapChain::~SwapChain() {
  if (mPresenter) {
    // Out of order destruction, but ok.
    (void)mPresenter->SwapBackBuffer(nullptr);
    mPresenter->mSwapChain = nullptr;
    mPresenter = nullptr;
  }
  if (mDestroyedCallback) {
    mDestroyedCallback();
  }
}

}  // namespace mozilla::gl
