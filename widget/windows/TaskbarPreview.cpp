/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TaskbarPreview.h"

#include <nsCOMPtr.h>
#include <nsError.h>
#include <nsITaskbarPreviewController.h>
#include <nsIWidget.h>
#include <nsServiceManagerUtils.h>
#include <windows.h>

#include "GdiBackBuffer.h"
#include "TaskbarPreviewButton.h"
#include "WinUtils.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "nsAppShell.h"
#include "nsComponentManagerUtils.h"
#include "nsWindow.h"

// Defined in dwmapi in a header that needs a higher numbered _WINNT #define
#ifndef DWM_SIT_DISPLAYFRAME
#  define DWM_SIT_DISPLAYFRAME 0x1
#endif

namespace mozilla {
namespace widget {

///////////////////////////////////////////////////////////////////////////////
// TaskbarPreview

TaskbarPreview::TaskbarPreview(ITaskbarList4* aTaskbar,
                               nsITaskbarPreviewController* aController,
                               HWND aHWND, nsIDocShell* aShell)
    : mTaskbar(aTaskbar),
      mController(aController),
      mWnd(aHWND),
      mVisible(false),
      mDocShell(do_GetWeakReference(aShell)) {}

TaskbarPreview::~TaskbarPreview() {
  // Avoid dangling pointer
  if (sActivePreview == this) sActivePreview = nullptr;

  // Our subclass should have invoked DetachFromNSWindow already.
  NS_ASSERTION(
      !mWnd,
      "TaskbarPreview::DetachFromNSWindow was not called before destruction");

  // Make sure to release before potentially uninitializing COM
  mTaskbar = nullptr;

  ::CoUninitialize();
}

nsresult TaskbarPreview::Init() {
  // TaskbarPreview may outlive the WinTaskbar that created it
  if (FAILED(::CoInitialize(nullptr))) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  WindowHook* hook = GetWindowHook();
  if (!hook) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return hook->AddMonitor(WM_DESTROY, MainWindowHook, this);
}

NS_IMETHODIMP
TaskbarPreview::SetController(nsITaskbarPreviewController* aController) {
  NS_ENSURE_ARG(aController);

  mController = aController;
  return NS_OK;
}

NS_IMETHODIMP
TaskbarPreview::GetController(nsITaskbarPreviewController** aController) {
  NS_ADDREF(*aController = mController);
  return NS_OK;
}

NS_IMETHODIMP
TaskbarPreview::GetTooltip(nsAString& aTooltip) {
  aTooltip = mTooltip;
  return NS_OK;
}

NS_IMETHODIMP
TaskbarPreview::SetTooltip(const nsAString& aTooltip) {
  mTooltip = aTooltip;
  return CanMakeTaskbarCalls() ? UpdateTooltip() : NS_OK;
}

NS_IMETHODIMP
TaskbarPreview::SetVisible(bool visible) {
  if (mVisible == visible) return NS_OK;
  mVisible = visible;

  // If the nsWindow has already been destroyed but the caller is still trying
  // to use it then just pretend that everything succeeded.  The caller doesn't
  // actually have a way to detect this since it's the same case as when we
  // CanMakeTaskbarCalls returns false.
  if (!IsWindowAvailable()) return NS_OK;

  return visible ? Enable() : Disable();
}

NS_IMETHODIMP
TaskbarPreview::GetVisible(bool* visible) {
  *visible = mVisible;
  return NS_OK;
}

NS_IMETHODIMP
TaskbarPreview::SetActive(bool active) {
  if (active)
    sActivePreview = this;
  else if (sActivePreview == this)
    sActivePreview = nullptr;

  return CanMakeTaskbarCalls() ? ShowActive(active) : NS_OK;
}

NS_IMETHODIMP
TaskbarPreview::GetActive(bool* active) {
  *active = sActivePreview == this;
  return NS_OK;
}

NS_IMETHODIMP
TaskbarPreview::Invalidate() {
  if (!mVisible) return NS_OK;

  HWND previewWindow = PreviewWindow();
  return FAILED(DwmInvalidateIconicBitmaps(previewWindow)) ? NS_ERROR_FAILURE
                                                           : NS_OK;
}

nsresult TaskbarPreview::UpdateTaskbarProperties() {
  nsresult rv = UpdateTooltip();

  // If we are the active preview and our window is the active window, restore
  // our active state - otherwise some other non-preview window is now active
  // and should be displayed as so.
  if (sActivePreview == this) {
    if (mWnd == ::GetActiveWindow()) {
      nsresult rvActive = ShowActive(true);
      if (NS_FAILED(rvActive)) rv = rvActive;
    } else {
      sActivePreview = nullptr;
    }
  }
  return rv;
}

nsresult TaskbarPreview::Enable() {
  nsresult rv = NS_OK;
  if (CanMakeTaskbarCalls()) {
    rv = UpdateTaskbarProperties();
  } else if (IsWindowAvailable()) {
    WindowHook* hook = GetWindowHook();
    MOZ_ASSERT(hook,
               "IsWindowAvailable() should have eliminated the null case.");
    hook->AddMonitor(nsAppShell::GetTaskbarButtonCreatedMessage(),
                     MainWindowHook, this);
  }
  return rv;
}

nsresult TaskbarPreview::Disable() {
  if (!IsWindowAvailable()) {
    // Window is already destroyed
    return NS_OK;
  }

  WindowHook* hook = GetWindowHook();
  MOZ_ASSERT(hook, "IsWindowAvailable() should have eliminated the null case.");
  (void)hook->RemoveMonitor(nsAppShell::GetTaskbarButtonCreatedMessage(),
                            MainWindowHook, this);

  return NS_OK;
}

bool TaskbarPreview::IsWindowAvailable() const {
  if (mWnd) {
    nsWindow* win = WinUtils::GetNSWindowPtr(mWnd);
    if (win && !win->Destroyed()) {
      return true;
    }
  }
  return false;
}

void TaskbarPreview::DetachFromNSWindow() {
  if (WindowHook* hook = GetWindowHook()) {
    hook->RemoveMonitor(WM_DESTROY, MainWindowHook, this);
  }
  mWnd = nullptr;
}

LRESULT
TaskbarPreview::WndProc(UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case WM_DWMSENDICONICTHUMBNAIL: {
      uint32_t width = HIWORD(lParam);
      uint32_t height = LOWORD(lParam);
      float aspectRatio = width / float(height);

      nsresult rv;
      float preferredAspectRatio;
      rv = mController->GetThumbnailAspectRatio(&preferredAspectRatio);
      if (NS_FAILED(rv)) break;

      uint32_t thumbnailWidth = width;
      uint32_t thumbnailHeight = height;

      if (aspectRatio > preferredAspectRatio) {
        thumbnailWidth = uint32_t(thumbnailHeight * preferredAspectRatio);
      } else {
        thumbnailHeight = uint32_t(thumbnailWidth / preferredAspectRatio);
      }

      DrawBitmap(thumbnailWidth, thumbnailHeight, false);
    } break;
    case WM_DWMSENDICONICLIVEPREVIEWBITMAP: {
      uint32_t width, height;
      nsresult rv;
      rv = mController->GetWidth(&width);
      if (NS_FAILED(rv)) break;
      rv = mController->GetHeight(&height);
      if (NS_FAILED(rv)) break;

      double scale = StaticPrefs::layout_css_devPixelsPerPx();
      if (scale <= 0.0) {
        scale = WinUtils::LogToPhysFactor(PreviewWindow());
      }
      DrawBitmap(NSToIntRound(scale * width), NSToIntRound(scale * height),
                 true);
    } break;
  }
  return ::DefWindowProcW(PreviewWindow(), nMsg, wParam, lParam);
}

bool TaskbarPreview::CanMakeTaskbarCalls() {
  // If the nsWindow has already been destroyed and we know it but our caller
  // clearly doesn't so we can't make any calls.
  if (!mWnd) return false;
  // Certain functions like SetTabOrder seem to require a visible window. During
  // window close, the window seems to be hidden before being destroyed.
  if (!::IsWindowVisible(mWnd)) return false;
  if (mVisible) {
    nsWindow* window = WinUtils::GetNSWindowPtr(mWnd);
    NS_ASSERTION(window, "Could not get nsWindow from HWND");
    return window ? window->HasTaskbarIconBeenCreated() : false;
  }
  return false;
}

WindowHook* TaskbarPreview::GetWindowHook() {
  nsWindow* window = WinUtils::GetNSWindowPtr(mWnd);
  NS_ASSERTION(window, "Cannot use taskbar previews in an embedded context!");

  return window ? &window->GetWindowHook() : nullptr;
}

void TaskbarPreview::EnableCustomDrawing(HWND aHWND, bool aEnable) {
  BOOL enabled = aEnable;
  DwmSetWindowAttribute(aHWND, DWMWA_FORCE_ICONIC_REPRESENTATION, &enabled,
                        sizeof(enabled));

  DwmSetWindowAttribute(aHWND, DWMWA_HAS_ICONIC_BITMAP, &enabled,
                        sizeof(enabled));
}

nsresult TaskbarPreview::UpdateTooltip() {
  NS_ASSERTION(CanMakeTaskbarCalls() && mVisible,
               "UpdateTooltip called on invisible tab preview");

  if (FAILED(mTaskbar->SetThumbnailTooltip(PreviewWindow(), mTooltip.get())))
    return NS_ERROR_FAILURE;
  return NS_OK;
}

void TaskbarPreview::DrawBitmap(uint32_t width, uint32_t height,
                                bool isPreview) {
  nsresult rv;
  nsCOMPtr<nsITaskbarPreviewCallback> callback =
      do_CreateInstance("@mozilla.org/widget/taskbar-preview-callback;1", &rv);
  if (NS_FAILED(rv)) {
    return;
  }

  ((TaskbarPreviewCallback*)callback.get())->SetPreview(this);

  if (isPreview) {
    ((TaskbarPreviewCallback*)callback.get())->SetIsPreview();
    mController->RequestPreview(callback);
  } else {
    mController->RequestThumbnail(callback, width, height);
  }
}

///////////////////////////////////////////////////////////////////////////////
// TaskbarPreviewCallback

NS_IMPL_ISUPPORTS(TaskbarPreviewCallback, nsITaskbarPreviewCallback)

/* void done (in nsISupports aCanvas, in boolean aDrawBorder); */
NS_IMETHODIMP
TaskbarPreviewCallback::Done(nsISupports* aCanvas, bool aDrawBorder) {
  // We create and destroy TaskbarTabPreviews from front end code in response
  // to TabOpen and TabClose events. Each TaskbarTabPreview creates and owns a
  // proxy HWND which it hands to Windows as a tab identifier. When a tab
  // closes, TaskbarTabPreview Disable() method is called by front end, which
  // destroys the proxy window and clears mProxyWindow which is the HWND
  // returned from PreviewWindow(). So, since this is async, we should check to
  // be sure the tab is still alive before doing all this gfx work and making
  // dwm calls. To accomplish this we check the result of PreviewWindow().
  if (!aCanvas || !mPreview || !mPreview->PreviewWindow() ||
      !mPreview->IsWindowAvailable()) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIContent> content(do_QueryInterface(aCanvas));
  auto canvas = dom::HTMLCanvasElement::FromNodeOrNull(content);
  if (!canvas) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<gfx::SourceSurface> source = canvas->GetSurfaceSnapshot();
  if (!source) {
    return NS_ERROR_FAILURE;
  }
  using DataSrcSurf = gfx::DataSourceSurface;
  RefPtr<DataSrcSurf> srcSurface = source->GetDataSurface();
  if (!srcSurface) {
    return NS_ERROR_FAILURE;
  }

  const gfx::IntSize size = srcSurface->GetSize();
  uint8_t* bits = nullptr;
  HBITMAP hBitmap = widget::CreateTopDownDIBSection(nullptr, size, &bits);
  if (!hBitmap) {
    return NS_ERROR_FAILURE;
  }
  auto deleteBitmap = MakeScopeExit([&] { ::DeleteObject(hBitmap); });

  if (DataSrcSurf::ScopedMap const sourceMap(srcSurface, DataSrcSurf::READ);
      sourceMap.IsMapped()) {
    mozilla::gfx::CopySurfaceDataToPackedArray(
        sourceMap.GetData(), bits, size, sourceMap.GetStride(),
        BytesPerPixel(srcSurface->GetFormat()));
  } else if (size.IsEmpty()) {
    // A zero-size source-surface probably shouldn't happen, but is harmless
    // here. Fall through.
  } else {
    return NS_ERROR_FAILURE;
  }

  DWORD flags = aDrawBorder ? DWM_SIT_DISPLAYFRAME : 0;
  HRESULT hr;
  if (!mIsThumbnail) {
    POINT pptClient = {0, 0};
    hr = DwmSetIconicLivePreviewBitmap(mPreview->PreviewWindow(), hBitmap,
                                       &pptClient, flags);
  } else {
    hr = DwmSetIconicThumbnail(mPreview->PreviewWindow(), hBitmap, flags);
  }
  MOZ_ASSERT(SUCCEEDED(hr));
  (void)hr;
  return NS_OK;
}

/* static */
bool TaskbarPreview::MainWindowHook(void* aContext, HWND hWnd, UINT nMsg,
                                    WPARAM wParam, LPARAM lParam,
                                    LRESULT* aResult) {
  NS_ASSERTION(nMsg == nsAppShell::GetTaskbarButtonCreatedMessage() ||
                   nMsg == WM_DESTROY,
               "Window hook proc called with wrong message");
  NS_ASSERTION(aContext, "Null context in MainWindowHook");
  if (!aContext) return false;
  TaskbarPreview* preview = reinterpret_cast<TaskbarPreview*>(aContext);
  if (nMsg == WM_DESTROY) {
    // nsWindow is being destroyed
    // We can't really do anything at this point including removing hooks
    return false;
  } else {
    nsWindow* window = WinUtils::GetNSWindowPtr(preview->mWnd);
    if (window) {
      window->SetHasTaskbarIconBeenCreated();

      if (preview->mVisible) preview->UpdateTaskbarProperties();
    }
  }
  return false;
}

TaskbarPreview* TaskbarPreview::sActivePreview = nullptr;

}  // namespace widget
}  // namespace mozilla
