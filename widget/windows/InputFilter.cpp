/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InputFilter.h"

namespace mozilla {
namespace widget {

// Static member definitions
std::map<HWND, bool> InputFilter::sEnabledWindows;
std::mutex InputFilter::sMutex;
std::map<HWND, InputFilter::CursorPos> InputFilter::sCursorPositions;
std::mutex InputFilter::sCursorMutex;
std::map<HWND, InputFilter::KeyboardState> InputFilter::sKeyboardStates;
std::mutex InputFilter::sKeyboardMutex;
std::map<HWND, InputFilter::MouseButtonState> InputFilter::sMouseButtonStates;
std::mutex InputFilter::sMouseButtonMutex;

HWND InputFilter::GetTopLevelWindow(HWND hwnd) {
  if (!hwnd) return nullptr;
  HWND parent = hwnd;
  HWND next;
  while ((next = ::GetParent(parent)) != nullptr) {
    parent = next;
  }
  return parent;
}

// Window enable/disable
void InputFilter::EnableForWindow(HWND hwnd) {
  HWND topLevel = GetTopLevelWindow(hwnd);
  if (!topLevel) topLevel = hwnd;
  std::lock_guard<std::mutex> lock(sMutex);
  sEnabledWindows[topLevel] = true;
}

void InputFilter::DisableForWindow(HWND hwnd) {
  HWND topLevel = GetTopLevelWindow(hwnd);
  if (!topLevel) topLevel = hwnd;
  std::lock_guard<std::mutex> lock(sMutex);
  sEnabledWindows[topLevel] = false;
}

bool InputFilter::IsEnabledForWindow(HWND hwnd) {
  HWND topLevel = GetTopLevelWindow(hwnd);
  if (!topLevel) topLevel = hwnd;
  std::lock_guard<std::mutex> lock(sMutex);
  auto it = sEnabledWindows.find(topLevel);
  return (it != sEnabledWindows.end()) ? it->second : false;
}

void InputFilter::RemoveWindow(HWND hwnd) {
  HWND topLevel = GetTopLevelWindow(hwnd);
  if (!topLevel) topLevel = hwnd;
  {
    std::lock_guard<std::mutex> lock(sMutex);
    sEnabledWindows.erase(topLevel);
  }
  {
    std::lock_guard<std::mutex> lock(sCursorMutex);
    sCursorPositions.erase(topLevel);
  }
  {
    std::lock_guard<std::mutex> lock(sKeyboardMutex);
    sKeyboardStates.erase(topLevel);
  }
  {
    std::lock_guard<std::mutex> lock(sMouseButtonMutex);
    sMouseButtonStates.erase(topLevel);
  }
}

// Message type detection
bool InputFilter::IsKeyboardMessage(UINT msg) {
  switch (msg) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
    case WM_UNICHAR:
    case WM_HOTKEY:
    case WM_IME_KEYDOWN:
    case WM_IME_KEYUP:
    case WM_IME_CHAR:
    case WM_IME_COMPOSITION:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION:
      return true;
    default:
      return false;
  }
}

bool InputFilter::IsMouseMessage(UINT msg) {
  switch (msg) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_NCMOUSEMOVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCLBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
    case WM_NCRBUTTONDBLCLK:
    case WM_NCMBUTTONDOWN:
    case WM_NCMBUTTONUP:
    case WM_NCMBUTTONDBLCLK:
      return true;
    default:
      return false;
  }
}

bool InputFilter::IsInputMessage(UINT msg) {
  return IsKeyboardMessage(msg) || IsMouseMessage(msg);
}

bool InputFilter::ShouldBlockNativeInput(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  // If filtering not enabled for this window, don't block anything
  if (!IsEnabledForWindow(hwnd)) {
    return false;
  }

  // Only block input messages (keyboard and mouse)
  if (!IsInputMessage(msg)) {
    return false;
  }

  // Block all native input when filter is enabled
  // MouseMux will inject its own events which bypass this filter
  return true;
}

// Cursor position tracking
void InputFilter::SetCursorPosForWindow(HWND hwnd, int screenX, int screenY) {
  HWND topLevel = GetTopLevelWindow(hwnd);
  if (!topLevel) topLevel = hwnd;
  std::lock_guard<std::mutex> lock(sCursorMutex);
  CursorPos& pos = sCursorPositions[topLevel];
  pos.screenX = screenX;
  pos.screenY = screenY;
  pos.valid = true;
}

bool InputFilter::GetCursorPosForWindow(HWND hwnd, POINT* outPos) {
  HWND topLevel = GetTopLevelWindow(hwnd);
  if (!topLevel) topLevel = hwnd;
  std::lock_guard<std::mutex> lock(sCursorMutex);
  auto it = sCursorPositions.find(topLevel);
  if (it != sCursorPositions.end() && it->second.valid) {
    outPos->x = it->second.screenX;
    outPos->y = it->second.screenY;
    return true;
  }
  return false;
}

// Keyboard state management
void InputFilter::SetKeyStateForWindow(HWND hwnd, BYTE* keyState) {
  HWND topLevel = GetTopLevelWindow(hwnd);
  if (!topLevel) topLevel = hwnd;
  std::lock_guard<std::mutex> lock(sKeyboardMutex);
  KeyboardState& state = sKeyboardStates[topLevel];
  memcpy(state.keys, keyState, 256);
  state.valid = true;
}

bool InputFilter::GetKeyStateForWindow(HWND hwnd, BYTE* outKeyState) {
  HWND topLevel = GetTopLevelWindow(hwnd);
  if (!topLevel) topLevel = hwnd;
  std::lock_guard<std::mutex> lock(sKeyboardMutex);
  auto it = sKeyboardStates.find(topLevel);
  if (it != sKeyboardStates.end() && it->second.valid) {
    memcpy(outKeyState, it->second.keys, 256);
    return true;
  }
  return false;
}

void InputFilter::SetSingleKeyState(HWND hwnd, int vkey, bool down, bool toggled) {
  if (vkey < 0 || vkey > 255) return;

  HWND topLevel = GetTopLevelWindow(hwnd);
  if (!topLevel) topLevel = hwnd;
  std::lock_guard<std::mutex> lock(sKeyboardMutex);
  KeyboardState& state = sKeyboardStates[topLevel];

  // High bit (0x80) = key is down
  // Low bit (0x01) = key is toggled (for toggle keys like CapsLock)
  BYTE val = 0;
  if (down) val |= 0x80;
  if (toggled) val |= 0x01;
  state.keys[vkey] = val;
  state.valid = true;
}

// Mouse button state management
void InputFilter::SetMouseButtonState(HWND hwnd, bool left, bool right, bool middle) {
  HWND topLevel = GetTopLevelWindow(hwnd);
  if (!topLevel) topLevel = hwnd;
  std::lock_guard<std::mutex> lock(sMouseButtonMutex);
  MouseButtonState& state = sMouseButtonStates[topLevel];
  state.left = left;
  state.right = right;
  state.middle = middle;
}

WORD InputFilter::GetMouseButtonState(HWND hwnd) {
  HWND topLevel = GetTopLevelWindow(hwnd);
  if (!topLevel) topLevel = hwnd;
  std::lock_guard<std::mutex> lock(sMouseButtonMutex);
  auto it = sMouseButtonStates.find(topLevel);
  if (it == sMouseButtonStates.end()) {
    return 0;
  }
  WORD flags = 0;
  if (it->second.left) flags |= MK_LBUTTON;
  if (it->second.right) flags |= MK_RBUTTON;
  if (it->second.middle) flags |= MK_MBUTTON;
  return flags;
}

// Thread-local current window for KeyboardLayout to query
static thread_local HWND sCurrentProcessingWindow = nullptr;

void InputFilter::SetCurrentWindow(HWND hwnd) {
  sCurrentProcessingWindow = hwnd;
}

HWND InputFilter::GetCurrentWindow() {
  return sCurrentProcessingWindow;
}

void InputFilter::ClearCurrentWindow() {
  sCurrentProcessingWindow = nullptr;
}

bool InputFilter::GetCurrentMouseButtons(uint16_t* outButtons) {
  HWND hwnd = sCurrentProcessingWindow;
  if (!hwnd || !outButtons) {
    return false;
  }

  if (!IsEnabledForWindow(hwnd)) {
    return false;  // Use native GetKeyState
  }

  // Get button state from our tracked state
  WORD flags = GetMouseButtonState(hwnd);
  *outButtons = 0;

  // Convert MK_* flags to MouseButtonsFlag values
  // MouseButtonsFlag::ePrimaryFlag = 1, eSecondaryFlag = 2, eMiddleFlag = 4
  if (flags & MK_LBUTTON) *outButtons |= 1;   // ePrimaryFlag
  if (flags & MK_RBUTTON) *outButtons |= 2;   // eSecondaryFlag
  if (flags & MK_MBUTTON) *outButtons |= 4;   // eMiddleFlag

  return true;
}

}  // namespace widget
}  // namespace mozilla
