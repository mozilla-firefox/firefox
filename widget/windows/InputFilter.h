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

// Per-window flag to block native mouse input in Firefox
// When enabled for a window, nsWindow skips processing native mouse messages
// for that specific window only. Each window is independent.
class InputFilter {
 public:
  static void EnableForWindow(HWND hwnd);
  static void DisableForWindow(HWND hwnd);
  static bool IsEnabledForWindow(HWND hwnd);
  static void RemoveWindow(HWND hwnd);  // Cleanup when window is destroyed

  // Per-window cursor position tracking for MouseMux
  // Stores the last known MouseMux cursor position per-window
  static void SetCursorPosForWindow(HWND hwnd, int screenX, int screenY);
  static bool GetCursorPosForWindow(HWND hwnd, POINT* outPos);

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

  // Helper to find the top-level window for a child window
  static HWND GetTopLevelWindow(HWND hwnd);
};

}  // namespace widget
}  // namespace mozilla

#endif  // widget_windows_InputFilter_h
