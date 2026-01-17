/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef widget_windows_InputFilter_h
#define widget_windows_InputFilter_h

#include <windows.h>
#include <map>
#include <mutex>

namespace mozilla {
namespace widget {

// Per-window input filtering for MouseMux multi-user support.
// When enabled for a window:
// - Native Windows mouse/keyboard input is blocked
// - Only MouseMux-injected input is processed
// - Each window tracks its own cursor position and keyboard state
class InputFilter {
 public:
  // Enable/disable native input blocking per window
  static void EnableForWindow(HWND hwnd);
  static void DisableForWindow(HWND hwnd);
  static bool IsEnabledForWindow(HWND hwnd);
  static void RemoveWindow(HWND hwnd);

  // Message type detection - used to identify what to block
  static bool IsKeyboardMessage(UINT msg);
  static bool IsMouseMessage(UINT msg);
  static bool IsInputMessage(UINT msg);

  // Check if a native input message should be blocked
  // Returns true if the message should NOT be processed (blocked)
  static bool ShouldBlockNativeInput(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  // Per-window cursor position tracking (MouseMux cursor, not system cursor)
  static void SetCursorPosForWindow(HWND hwnd, int screenX, int screenY);
  static bool GetCursorPosForWindow(HWND hwnd, POINT* outPos);

  // Per-window keyboard state from MouseMux
  // This replaces native GetKeyboardState() calls when filtering is enabled
  static void SetKeyStateForWindow(HWND hwnd, BYTE* keyState);
  static bool GetKeyStateForWindow(HWND hwnd, BYTE* outKeyState);
  static void SetSingleKeyState(HWND hwnd, int vkey, bool down, bool toggled = false);

  // Per-window mouse button state from MouseMux
  static void SetMouseButtonState(HWND hwnd, bool left, bool right, bool middle);
  static WORD GetMouseButtonState(HWND hwnd);

  // Current window being processed (for use by KeyboardLayout)
  // Set this before calling ModifierKeyState functions
  static void SetCurrentWindow(HWND hwnd);
  static HWND GetCurrentWindow();
  static void ClearCurrentWindow();

  // Get mouse button state for current window (called by KeyboardLayout)
  // Returns flags compatible with MouseButtonsFlag enum
  static bool GetCurrentMouseButtons(uint16_t* outButtons);

 private:
  static std::map<HWND, bool> sEnabledWindows;
  static std::mutex sMutex;

  // Per-window cursor position storage
  struct CursorPos {
    int screenX = 0;
    int screenY = 0;
    bool valid = false;
  };
  static std::map<HWND, CursorPos> sCursorPositions;
  static std::mutex sCursorMutex;

  // Per-window keyboard state (256 bytes, same as Windows keyboard state)
  struct KeyboardState {
    BYTE keys[256] = {0};
    bool valid = false;
  };
  static std::map<HWND, KeyboardState> sKeyboardStates;
  static std::mutex sKeyboardMutex;

  // Per-window mouse button state
  struct MouseButtonState {
    bool left = false;
    bool right = false;
    bool middle = false;
  };
  static std::map<HWND, MouseButtonState> sMouseButtonStates;
  static std::mutex sMouseButtonMutex;

  static HWND GetTopLevelWindow(HWND hwnd);
};

}  // namespace widget
}  // namespace mozilla

#endif  // widget_windows_InputFilter_h
