/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * WinPointerEvents - Helper functions to retrieve PointerEvent's attributes
 */

#include "WinPointerEvents.h"

#include "WinUtils.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "nsWindowDefs.h"
#include "nscore.h"

using namespace mozilla;
using namespace mozilla::widget;

const wchar_t WinPointerEvents::kPointerLibraryName[] = L"user32.dll";
HMODULE WinPointerEvents::sLibraryHandle = nullptr;
WinPointerEvents::GetPointerTypePtr WinPointerEvents::getPointerType = nullptr;
WinPointerEvents::GetPointerInfoPtr WinPointerEvents::getPointerInfo = nullptr;
WinPointerEvents::GetPointerPenInfoPtr WinPointerEvents::getPointerPenInfo =
    nullptr;

WinPointerEvents::WinPointerEvents() { InitLibrary(); }

/* Load and shutdown */
void WinPointerEvents::InitLibrary() {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (getPointerType) {
    // Return if we already initialized the PointerEvent related interfaces
    return;
  }
  sLibraryHandle = ::LoadLibraryW(kPointerLibraryName);
  MOZ_ASSERT(sLibraryHandle, "cannot load pointer library");
  if (sLibraryHandle) {
    getPointerType =
        (GetPointerTypePtr)GetProcAddress(sLibraryHandle, "GetPointerType");
    getPointerInfo =
        (GetPointerInfoPtr)GetProcAddress(sLibraryHandle, "GetPointerInfo");
    getPointerPenInfo = (GetPointerPenInfoPtr)GetProcAddress(
        sLibraryHandle, "GetPointerPenInfo");
  }

  if (!getPointerType || !getPointerInfo || !getPointerPenInfo) {
    MOZ_ASSERT(false, "get PointerEvent interfaces failed");
    getPointerType = nullptr;
    getPointerInfo = nullptr;
    getPointerPenInfo = nullptr;
    return;
  }
}

bool WinPointerEvents::ShouldHandleWinPointerMessages(UINT aMsg,
                                                      WPARAM aWParam) {
  MOZ_ASSERT(aMsg == WM_POINTERDOWN || aMsg == WM_POINTERUP ||
             aMsg == WM_POINTERUPDATE || aMsg == WM_POINTERLEAVE);
  if (!sLibraryHandle) {
    return false;
  }

  // We only handle WM_POINTER* when the input source is pen. This is because
  // we need some information (e.g. tiltX, tiltY) which can't be retrieved by
  // WM_*BUTTONDOWN.
  uint32_t pointerId = GetPointerId(aWParam);
  POINTER_INPUT_TYPE pointerType = PT_POINTER;
  if (!GetPointerType(pointerId, &pointerType)) {
    MOZ_ASSERT(false, "cannot find PointerType");
    return false;
  }
  return (pointerType == PT_PEN);
}

bool WinPointerEvents::GetPointerType(uint32_t aPointerId,
                                      POINTER_INPUT_TYPE* aPointerType) {
  if (!getPointerType) {
    return false;
  }
  return getPointerType(aPointerId, aPointerType);
}

POINTER_INPUT_TYPE
WinPointerEvents::GetPointerType(uint32_t aPointerId) {
  POINTER_INPUT_TYPE pointerType = PT_POINTER;
  (void)GetPointerType(aPointerId, &pointerType);
  return pointerType;
}

bool WinPointerEvents::GetPointerInfo(uint32_t aPointerId,
                                      POINTER_INFO* aPointerInfo) {
  if (!getPointerInfo) {
    return false;
  }
  return getPointerInfo(aPointerId, aPointerInfo);
}

bool WinPointerEvents::GetPointerPenInfo(uint32_t aPointerId,
                                         POINTER_PEN_INFO* aPenInfo) {
  if (!getPointerPenInfo) {
    return false;
  }
  return getPointerPenInfo(aPointerId, aPenInfo);
}

void WinPointerEvents::GetPointerFrameTouchInfo(
    uint32_t pointerId, nsTArray<POINTER_TOUCH_INFO>& aTouchInfoArray) {
  uint32_t pointerCount = 0;
  if (!::GetPointerFrameTouchInfo(pointerId, &pointerCount, nullptr)) {
    return;
  }
  if (!pointerCount) {
    return;
  }

  aTouchInfoArray.SetLength(pointerCount);
  ::GetPointerFrameTouchInfo(pointerId, &pointerCount,
                             aTouchInfoArray.Elements());
}

bool WinPointerEvents::ShouldRollupOnPointerEvent(UINT aMsg, WPARAM aWParam) {
  MOZ_ASSERT(aMsg == WM_POINTERDOWN);
  // Only roll up popups when we handling WM_POINTER* to fire Gecko
  // WidgetMouseEvent and suppress Windows WM_*BUTTONDOWN.
  return ShouldHandleWinPointerMessages(aMsg, aWParam) &&
         ShouldFirePointerEventByWinPointerMessages();
}

bool WinPointerEvents::ShouldFirePointerEventByWinPointerMessages() {
  MOZ_ASSERT(sLibraryHandle);
  return StaticPrefs::dom_w3c_pointer_events_dispatch_by_pointer_messages();
}

WinPointerInfo* WinPointerEvents::GetCachedPointerInfo(UINT aMsg,
                                                       WPARAM aWParam) {
  if (!sLibraryHandle ||
      MOUSE_INPUT_SOURCE() != dom::MouseEvent_Binding::MOZ_SOURCE_PEN ||
      ShouldFirePointerEventByWinPointerMessages()) {
    return nullptr;
  }
  switch (aMsg) {
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_XBUTTONDOWN:
      return &mPenPointerDownInfo;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    case WM_XBUTTONUP:
      return &mPenPointerUpInfo;
    case WM_MOUSEMOVE:
      return &mPenPointerUpdateInfo;
    default:
      MOZ_ASSERT(false);
  }
  return nullptr;
}

void WinPointerEvents::ConvertAndCachePointerInfo(UINT aMsg, WPARAM aWParam) {
  MOZ_ASSERT(
      !StaticPrefs::dom_w3c_pointer_events_dispatch_by_pointer_messages());
  // Windows doesn't support chorded buttons for pen, so we can simply keep the
  // latest information from pen generated pointer messages and use them when
  // handling mouse messages. Used different pointer info for pointerdown,
  // pointerupdate, and pointerup because Windows doesn't always interleave
  // pointer messages and mouse messages.
  switch (aMsg) {
    case WM_POINTERDOWN:
      ConvertAndCachePointerInfo(aWParam, &mPenPointerDownInfo);
      break;
    case WM_POINTERUP:
      ConvertAndCachePointerInfo(aWParam, &mPenPointerUpInfo);
      break;
    case WM_POINTERUPDATE:
      ConvertAndCachePointerInfo(aWParam, &mPenPointerUpdateInfo);
      break;
    default:
      break;
  }
}

void WinPointerEvents::ConvertAndCachePointerInfo(WPARAM aWParam,
                                                  WinPointerInfo* aInfo) {
  MOZ_ASSERT(
      !StaticPrefs::dom_w3c_pointer_events_dispatch_by_pointer_messages());
  aInfo->pointerId = GetPointerId(aWParam);
  MOZ_ASSERT(GetPointerType(aInfo->pointerId) == PT_PEN);
  POINTER_PEN_INFO penInfo;
  GetPointerPenInfo(aInfo->pointerId, &penInfo);
  aInfo->mTilt = Some(WinPointerInfo::Tilt{penInfo.tiltX, penInfo.tiltY});
  // Windows defines the pen pressure is normalized to a range between 0 and
  // 1024. Convert it to float.
  aInfo->mPressure = penInfo.pressure ? (float)penInfo.pressure / 1024 : 0;
}
