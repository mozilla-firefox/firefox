/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DimensionRequest.h"

#include "mozilla/Try.h"
#include "nsIAppWindow.h"
#include "nsIBaseWindow.h"

namespace mozilla {

nsresult DimensionRequest::SupplementFrom(nsIBaseWindow* aSource) {
  NS_ENSURE_ARG_POINTER(aSource);
  int32_t x = 0, y = 0, width = 0, height = 0;

  bool needsPosition = mX.isSome() != mY.isSome();
  bool needsSize = mWidth.isSome() != mHeight.isSome();

  if (!needsPosition && !needsSize) {
    return NS_OK;
  }

  MOZ_TRY(aSource->GetDimensions(mDimensionKind, needsPosition ? &x : nullptr,
                                 needsPosition ? &y : nullptr,
                                 needsSize ? &width : nullptr,
                                 needsSize ? &height : nullptr));

  if (needsPosition) {
    if (mX.isNothing()) {
      mX.emplace(x);
    }
    if (mY.isNothing()) {
      mY.emplace(y);
    }
  }
  if (needsSize) {
    if (mWidth.isNothing()) {
      mWidth.emplace(width);
    }
    if (mHeight.isNothing()) {
      mHeight.emplace(height);
    }
  }

  MOZ_ASSERT(mX.isSome() == mY.isSome());
  MOZ_ASSERT(mWidth.isSome() == mHeight.isSome());
  return NS_OK;
}

nsresult DimensionRequest::ApplyOuterTo(nsIBaseWindow* aTarget) {
  NS_ENSURE_ARG_POINTER(aTarget);
  MOZ_ASSERT(mX.isSome() == mY.isSome(),
             "Missing dimensions should have been completed.");
  MOZ_ASSERT(mWidth.isSome() == mHeight.isSome(),
             "Missing dimensions should have been completed.");
  if (mDimensionKind != DimensionKind::Outer) {
    MOZ_ASSERT_UNREACHABLE("Expected outer dimensions.");
    return NS_ERROR_UNEXPECTED;
  }

  bool havePosition = mX.isSome() && mY.isSome();
  bool haveSize = mWidth.isSome() && mHeight.isSome();

  if (!havePosition && !haveSize) {
    return NS_OK;
  }

  if (havePosition && haveSize) {
    return aTarget->SetPositionAndSize(*mX, *mY, *mWidth, *mHeight, true);
  }

  if (havePosition) {
    return aTarget->SetPosition(*mX, *mY);
  }

  if (haveSize) {
    return aTarget->SetSize(*mWidth, *mHeight, true);
  }

  MOZ_ASSERT_UNREACHABLE();
  return NS_ERROR_UNEXPECTED;
}

nsresult DimensionRequest::ApplyInnerTo(nsIAppWindow* aTarget,
                                        bool aAsRootShell) {
  NS_ENSURE_ARG_POINTER(aTarget);
  MOZ_ASSERT(mX.isSome() == mY.isSome(),
             "Missing dimensions should have been completed.");
  MOZ_ASSERT(mWidth.isSome() == mHeight.isSome(),
             "Missing dimensions should have been completed.");
  if (mDimensionKind != DimensionKind::Inner) {
    MOZ_ASSERT_UNREACHABLE("Expected inner dimensions.");
    return NS_ERROR_UNEXPECTED;
  }

  bool havePosition = mX.isSome() && mY.isSome();
  bool haveSize = mWidth.isSome() && mHeight.isSome();

  if (!havePosition && !haveSize) {
    return NS_OK;
  }

  if (havePosition) {
    MOZ_ASSERT_UNREACHABLE("Inner position is not implemented.");
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  if (haveSize) {
    if (aAsRootShell) {
      return aTarget->SetRootShellSize(*mWidth, *mHeight);
    }
    return aTarget->SetPrimaryContentSize(*mWidth, *mHeight);
  }

  MOZ_ASSERT_UNREACHABLE();
  return NS_ERROR_UNEXPECTED;
}

}  // namespace mozilla
