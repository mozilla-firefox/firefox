/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MouseMuxDebugDialog.h"
#include "MouseMuxService.h"
#include "InputFilter.h"
#include <cstdio>
#include <cstdarg>

#define MOUSEMUX_VERSION "3.0"

namespace mozilla {
namespace widget {

MouseMuxDebugDialog* MouseMuxDebugDialog::sInstance = nullptr;

MouseMuxDebugDialog* MouseMuxDebugDialog::GetInstance() {
  if (!sInstance) {
    sInstance = new MouseMuxDebugDialog();
  }
  return sInstance;
}

void MouseMuxDebugDialog::Shutdown() {
  if (sInstance) {
    sInstance->Hide();
    delete sInstance;
    sInstance = nullptr;
  }
}

MouseMuxDebugDialog::MouseMuxDebugDialog() {}

MouseMuxDebugDialog::~MouseMuxDebugDialog() {
  if (mDialog) {
    ::DestroyWindow(mDialog);
    mDialog = nullptr;
  }
}

void MouseMuxDebugDialog::CreateDialogWindow() {
  if (mDialog) return;

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DialogProc;
  wc.hInstance = ::GetModuleHandle(nullptr);
  wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = L"MouseMuxDebugDialog";
  ::RegisterClassExW(&wc);

  // Create as normal overlapped window with taskbar entry, always on top
  wchar_t title[64];
  swprintf(title, 64, L"MouseMux Debug v%S", MOUSEMUX_VERSION);
  mDialog = ::CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_APPWINDOW, L"MouseMuxDebugDialog", title,
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 400, 350,
      nullptr, nullptr, ::GetModuleHandle(nullptr), this);

  // Log to file for debugging
  FILE* f = fopen("D:/scratch/firefox/mousemux_debug.log", "a");
  if (f) {
    fprintf(f, "[Dialog] CreateWindowExW returned %p, GetLastError=%lu\n", mDialog, ::GetLastError());
    fflush(f);
    fclose(f);
  }

  mStatusLabel = ::CreateWindowW(L"STATIC", L"Status: Disconnected",
                                 WS_CHILD | WS_VISIBLE, 10, 10, 380, 20,
                                 mDialog, (HMENU)ID_STATUS, nullptr, nullptr);

  mConnectBtn = ::CreateWindowW(L"BUTTON", L"Connect",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 40,
                                120, 25, mDialog, (HMENU)ID_CONNECT, nullptr, nullptr);

  mBlockBtn = ::CreateWindowW(L"BUTTON", L"Block Input",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 140, 40,
                              120, 25, mDialog, (HMENU)ID_BLOCK, nullptr, nullptr);

  mLogEdit = ::CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
      10, 75, 370, 230, mDialog, (HMENU)ID_LOG, nullptr, nullptr);

  HFONT hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
  ::SendMessage(mStatusLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
  ::SendMessage(mConnectBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
  ::SendMessage(mBlockBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
  ::SendMessage(mLogEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

  MouseMuxService::GetInstance()->SetLogCallback(
      [this](const char* msg) { this->AppendLog(msg); });

  UpdateStatus();
}

void MouseMuxDebugDialog::Show() {
  FILE* f = fopen("D:/scratch/firefox/mousemux_debug.log", "a");
  if (f) {
    fprintf(f, "[Dialog] Show() called, mDialog=%p\n", mDialog);
    fflush(f);
    fclose(f);
  }

  if (!mDialog) {
    CreateDialogWindow();
  }

  if (mDialog) {
    ::ShowWindow(mDialog, SW_SHOWNORMAL);
    ::SetForegroundWindow(mDialog);
    ::BringWindowToTop(mDialog);
    mVisible = true;
    UpdateStatus();

    f = fopen("D:/scratch/firefox/mousemux_debug.log", "a");
    if (f) {
      fprintf(f, "[Dialog] ShowWindow done, mDialog=%p visible=%d\n", mDialog, ::IsWindowVisible(mDialog));
      fflush(f);
      fclose(f);
    }
  }
}

void MouseMuxDebugDialog::Hide() {
  if (mDialog) {
    ::ShowWindow(mDialog, SW_HIDE);
  }
  mVisible = false;
}

LRESULT CALLBACK MouseMuxDebugDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  MouseMuxDebugDialog* self = nullptr;

  if (msg == WM_CREATE) {
    CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
    self = (MouseMuxDebugDialog*)cs->lpCreateParams;
    ::SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
  } else {
    self = (MouseMuxDebugDialog*)::GetWindowLongPtr(hwnd, GWLP_USERDATA);
  }

  if (self) {
    return self->HandleMessage(msg, wParam, lParam);
  }
  return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT MouseMuxDebugDialog::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case ID_CONNECT: OnToggleConnect(); return 0;
        case ID_BLOCK: OnToggleBlock(); return 0;
      }
      break;
    case WM_CLOSE:
      Hide();
      return 0;
    case WM_DESTROY:
      mDialog = nullptr;
      return 0;
  }
  return ::DefWindowProc(mDialog, msg, wParam, lParam);
}

void MouseMuxDebugDialog::OnToggleConnect() {
  auto* service = MouseMuxService::GetInstance();
  if (service->IsConnected()) {
    Log("Disconnecting...");
    service->Disconnect();
  } else {
    Log("Connecting...");
    service->Connect();
  }
  UpdateStatus();
}

void MouseMuxDebugDialog::OnToggleBlock() {
  if (InputFilter::IsEnabled()) {
    InputFilter::Disable();
    Log("Input filter DISABLED");
  } else {
    InputFilter::Enable();
    Log("Input filter ENABLED");
  }
  UpdateStatus();
}

void MouseMuxDebugDialog::UpdateStatus() {
  if (!mStatusLabel) return;

  auto* service = MouseMuxService::GetInstance();
  const char* statusText = "Unknown";
  bool connected = false;

  switch (service->GetConnectionState()) {
    case MouseMuxService::ConnectionState::Disconnected: statusText = "Disconnected"; break;
    case MouseMuxService::ConnectionState::Connecting: statusText = "Connecting..."; break;
    case MouseMuxService::ConnectionState::Connected: statusText = "Connected"; connected = true; break;
    case MouseMuxService::ConnectionState::Reconnecting: statusText = "Reconnecting..."; break;
  }

  bool blocked = InputFilter::IsEnabled();
  uint32_t ownerHwid = service->GetOwnerHwid();

  wchar_t buf[256];
  if (ownerHwid) {
    swprintf(buf, 256, L"%S | %s | Owner: 0x%X",
             statusText, blocked ? L"BLOCKED" : L"Normal", ownerHwid);
  } else {
    swprintf(buf, 256, L"%S | %s | Owner: None",
             statusText, blocked ? L"BLOCKED" : L"Normal");
  }
  ::SetWindowTextW(mStatusLabel, buf);

  if (mConnectBtn) {
    ::SetWindowTextW(mConnectBtn, connected ? L"Disconnect" : L"Connect");
  }
  if (mBlockBtn) {
    ::SetWindowTextW(mBlockBtn, blocked ? L"Unblock" : L"Block Input");
  }
}

void MouseMuxDebugDialog::Log(const char* aFormat, ...) {
  char buf[512];
  va_list args;
  va_start(args, aFormat);
  vsnprintf(buf, sizeof(buf), aFormat, args);
  va_end(args);
  AppendLog(buf);
}

void MouseMuxDebugDialog::AppendLog(const char* text) {
  if (!mLogEdit) return;

  std::lock_guard<std::mutex> lock(mLogMutex);

  mLogLines.push_back(text);
  while (mLogLines.size() > 100) {
    mLogLines.erase(mLogLines.begin());
  }

  std::string fullText;
  for (const auto& line : mLogLines) {
    fullText += line;
    fullText += "\r\n";
  }

  ::SetWindowTextA(mLogEdit, fullText.c_str());
  int lineCount = (int)::SendMessage(mLogEdit, EM_GETLINECOUNT, 0, 0);
  ::SendMessage(mLogEdit, EM_LINESCROLL, 0, lineCount);
  UpdateStatus();
}

}  // namespace widget
}  // namespace mozilla
