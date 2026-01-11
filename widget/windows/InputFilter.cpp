/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InputFilter.h"

namespace mozilla {
namespace widget {

std::map<HWND, bool> InputFilter::sEnabledWindows;
std::mutex InputFilter::sMutex;

HWND InputFilter::GetTopLevelWindow(HWND hwnd) {
  if (!hwnd) return nullptr;
  
  // Walk up the parent chain to find the top-level window
  HWND parent = hwnd;
  HWND next;
  while ((next = ::GetParent(parent)) != nullptr) {
    parent = next;
  }
  return parent;
}

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
  if (it != sEnabledWindows.end()) {
    return it->second;
  }
  return false;  // Not in map means not enabled
}

void InputFilter::RemoveWindow(HWND hwnd) {
  HWND topLevel = GetTopLevelWindow(hwnd);
  if (!topLevel) topLevel = hwnd;
  
  std::lock_guard<std::mutex> lock(sMutex);
  sEnabledWindows.erase(topLevel);
}

}  // namespace widget
}  // namespace mozilla
