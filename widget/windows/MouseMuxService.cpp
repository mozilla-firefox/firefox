/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MouseMuxService.h"
#include "MouseMuxDebugDialog.h"
#include "nsWindow.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>

#pragma comment(lib, "ws2_32.lib")

// Marker in wParam high bit to identify MouseMux-injected messages
#define MOUSEMUX_MARKER 0x80000000

namespace mozilla {
namespace widget {

MouseMuxService* MouseMuxService::sInstance = nullptr;

static bool ExtractString(const std::string& json, const char* key,
                          std::string& value) {
  std::string searchKey = std::string("\"") + key + "\":";
  size_t pos = json.find(searchKey);
  if (pos == std::string::npos) return false;
  pos += searchKey.length();
  while (pos < json.length() && (json[pos] == ' ' || json[pos] == '"')) pos++;
  if (json[pos - 1] != '"') return false;
  size_t end = json.find('"', pos);
  if (end == std::string::npos) return false;
  value = json.substr(pos, end - pos);
  return true;
}

static bool ExtractInt(const std::string& json, const char* key, int& value) {
  std::string searchKey = std::string("\"") + key + "\":";
  size_t pos = json.find(searchKey);
  if (pos == std::string::npos) return false;
  pos += searchKey.length();
  while (pos < json.length() && json[pos] == ' ') pos++;
  value = std::atoi(json.c_str() + pos);
  return true;
}

static bool ExtractUint(const std::string& json, const char* key, uint32_t& value) {
  int intVal;
  if (!ExtractInt(json, key, intVal)) return false;
  value = static_cast<uint32_t>(intVal);
  return true;
}

MouseMuxService* MouseMuxService::GetInstance() {
  if (!sInstance) {
    sInstance = new MouseMuxService();
  }
  return sInstance;
}

void MouseMuxService::Shutdown() {
  if (sInstance) {
    sInstance->Disconnect();
    delete sInstance;
    sInstance = nullptr;
  }
}

static DWORD WINAPI DialogThreadProc(LPVOID) {
  MouseMuxDebugDialog::GetInstance()->Show();
  MSG msg;
  while (::GetMessage(&msg, nullptr, 0, 0)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }
  return 0;
}

MouseMuxService::MouseMuxService() {
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
  ::CreateThread(nullptr, 0, DialogThreadProc, nullptr, 0, nullptr);
}

MouseMuxService::~MouseMuxService() {
  Disconnect();
  WSACleanup();
}

bool MouseMuxService::Connect(const wchar_t* aUrl) {
  if (mConnectionState == ConnectionState::Connected ||
      mConnectionState == ConnectionState::Connecting) {
    return false;
  }

  mServerUrl = aUrl;
  mShouldStop = false;
  mConnectionState = ConnectionState::Connecting;

  Log("Connecting...");

  mWorkerThread = std::thread(&MouseMuxService::WebSocketThread, this);
  return true;
}

void MouseMuxService::Disconnect() {
  if (mConnectionState == ConnectionState::Disconnected) return;

  Log("Disconnecting...");
  mShouldStop = true;

  if (mSocket != INVALID_SOCKET) {
    closesocket(mSocket);
    mSocket = INVALID_SOCKET;
  }

  if (mWorkerThread.joinable()) {
    mWorkerThread.join();
  }

  mConnectionState = ConnectionState::Disconnected;
}

void MouseMuxService::WebSocketThread() {
  Log("WS thread start");

  mSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (mSocket == INVALID_SOCKET) {
    Log("Socket create failed");
    mConnectionState = ConnectionState::Disconnected;
    return;
  }

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(41001);
  inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

  if (connect(mSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
    Log("TCP connect failed");
    closesocket(mSocket);
    mSocket = INVALID_SOCKET;
    mConnectionState = ConnectionState::Disconnected;
    return;
  }

  Log("TCP connected");

  std::string handshake =
      "GET / HTTP/1.1\r\n"
      "Host: localhost:41001\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n";

  send(mSocket, handshake.c_str(), (int)handshake.length(), 0);

  char buffer[4096];
  int bytesRead = recv(mSocket, buffer, sizeof(buffer) - 1, 0);
  if (bytesRead <= 0) {
    Log("Handshake recv failed");
    closesocket(mSocket);
    mSocket = INVALID_SOCKET;
    mConnectionState = ConnectionState::Disconnected;
    return;
  }
  buffer[bytesRead] = '\0';

  if (strstr(buffer, "101") == nullptr) {
    Log("WS handshake rejected");
    closesocket(mSocket);
    mSocket = INVALID_SOCKET;
    mConnectionState = ConnectionState::Disconnected;
    return;
  }

  Log("WS connected!");
  mConnectionState = ConnectionState::Connected;

  std::string messageBuffer;

  while (!mShouldStop) {
    bytesRead = recv(mSocket, buffer, sizeof(buffer), 0);
    if (bytesRead <= 0) {
      if (!mShouldStop) Log("Connection lost");
      break;
    }

    size_t pos = 0;
    while (pos < (size_t)bytesRead) {
      if (pos + 2 > (size_t)bytesRead) break;

      uint8_t byte0 = buffer[pos];
      uint8_t byte1 = buffer[pos + 1];
      bool fin = (byte0 & 0x80) != 0;
      int opcode = byte0 & 0x0F;
      bool masked = (byte1 & 0x80) != 0;
      uint64_t payloadLen = byte1 & 0x7F;
      pos += 2;

      if (payloadLen == 126) {
        if (pos + 2 > (size_t)bytesRead) break;
        payloadLen = (uint8_t)buffer[pos] << 8 | (uint8_t)buffer[pos + 1];
        pos += 2;
      } else if (payloadLen == 127) {
        if (pos + 8 > (size_t)bytesRead) break;
        payloadLen = 0;
        for (int i = 0; i < 8; i++) {
          payloadLen = (payloadLen << 8) | (uint8_t)buffer[pos + i];
        }
        pos += 8;
      }

      uint8_t maskKey[4] = {0};
      if (masked) {
        if (pos + 4 > (size_t)bytesRead) break;
        memcpy(maskKey, buffer + pos, 4);
        pos += 4;
      }

      if (pos + payloadLen > (size_t)bytesRead) break;

      std::string payload(buffer + pos, payloadLen);
      if (masked) {
        for (size_t i = 0; i < payloadLen; i++) {
          payload[i] ^= maskKey[i % 4];
        }
      }
      pos += payloadLen;

      if (opcode == 0x01) {
        messageBuffer += payload;
        if (fin) {
          HandleMessage(messageBuffer);
          messageBuffer.clear();
        }
      } else if (opcode == 0x08) {
        Log("Server close");
        mShouldStop = true;
        break;
      } else if (opcode == 0x09) {
        uint8_t pong[2] = {0x8A, 0x00};
        send(mSocket, (char*)pong, 2, 0);
      }
    }
  }

  Log("WS thread end");
  if (mSocket != INVALID_SOCKET) {
    closesocket(mSocket);
    mSocket = INVALID_SOCKET;
  }
  mConnectionState = ConnectionState::Disconnected;
}

void MouseMuxService::HandleMessage(const std::string& aMessage) {
  std::string type;
  if (!ExtractString(aMessage, "type", type)) return;

  uint32_t hwid;
  int x, y;
  uint32_t data;

  if (type == "pointer.motion.notify.M2A") {
    if (!ExtractUint(aMessage, "hwid", hwid)) return;
    if (!ExtractInt(aMessage, "x", x)) return;
    if (!ExtractInt(aMessage, "y", y)) return;

    mLastMousePos[hwid] = {x, y};
    HandlePointerMotion(hwid, x, y);

  } else if (type == "pointer.button.notify.M2A") {
    if (!ExtractUint(aMessage, "hwid", hwid)) return;
    if (!ExtractInt(aMessage, "x", x)) return;
    if (!ExtractInt(aMessage, "y", y)) return;
    if (!ExtractUint(aMessage, "data", data)) return;

    mLastMousePos[hwid] = {x, y};
    HandlePointerButton(hwid, x, y, data);

  } else if (type == "pointer.wheel.v.notify.M2A") {
    if (!ExtractUint(aMessage, "hwid", hwid)) return;
    int delta;
    if (!ExtractInt(aMessage, "delta", delta)) return;
    auto& pos = mLastMousePos[hwid];
    HandlePointerWheel(hwid, pos.screenX, pos.screenY, delta, false);

  } else if (type == "pointer.wheel.h.notify.M2A") {
    if (!ExtractUint(aMessage, "hwid", hwid)) return;
    int delta;
    if (!ExtractInt(aMessage, "delta", delta)) return;
    auto& pos = mLastMousePos[hwid];
    HandlePointerWheel(hwid, pos.screenX, pos.screenY, delta, true);

  } else if (type == "keyboard.key.notify.M2A") {
    if (!ExtractUint(aMessage, "hwid", hwid)) return;
    uint32_t vkey, msg, scancode, flags;
    if (!ExtractUint(aMessage, "vkey", vkey)) return;
    if (!ExtractUint(aMessage, "message", msg)) return;
    if (!ExtractUint(aMessage, "scan", scancode)) return;
    if (!ExtractUint(aMessage, "flags", flags)) return;
    HandleKeyboard(hwid, vkey, msg, scancode, flags);

  } else if (type == "user.list.notify.M2A") {
    HandleUserList(aMessage);
  }
}

void MouseMuxService::HandlePointerMotion(uint32_t aHwid, int aScreenX, int aScreenY) {
  nsWindow* window = nullptr;

  // If this hwid is the owner, always send to owned window
  if (aHwid == mOwnerHwid && mOwnedWindow) {
    window = mOwnedWindow;
  } else {
    window = FindWindowAtPoint(aScreenX, aScreenY);
  }

  if (!window) return;

  HWND hwnd = window->GetWindowHandle();
  if (!hwnd) return;

  // Convert screen coords to client coords
  POINT pt = {aScreenX, aScreenY};
  ::ScreenToClient(hwnd, &pt);

  // Build wParam from tracked button state (from MouseMux events only)
  WPARAM wParam = BuildMouseWParam(aHwid);

  // Add marker to identify this as MouseMux message
  wParam |= MOUSEMUX_MARKER;

  LPARAM lParam = MAKELPARAM(pt.x, pt.y);

  ::PostMessage(hwnd, WM_MOUSEMOVE, wParam, lParam);
}

void MouseMuxService::HandlePointerButton(uint32_t aHwid, int aScreenX,
                                          int aScreenY, uint32_t aEventFlags) {
  // SDK v2.2.32 event flags:
  // 0x01=LeftDown, 0x02=LeftUp, 0x04=RightDown, 0x08=RightUp,
  // 0x10=MiddleDown, 0x20=MiddleUp

  nsWindow* window = nullptr;
  bool isButtonDown = (aEventFlags & 0x01) || (aEventFlags & 0x04) || (aEventFlags & 0x10);

  // If this hwid is the owner, always send to owned window
  if (aHwid == mOwnerHwid && mOwnedWindow) {
    window = mOwnedWindow;
  } else {
    window = FindWindowAtPoint(aScreenX, aScreenY);
  }

  if (!window) return;

  // On button down inside a window, this hwid becomes the owner
  if (isButtonDown && window) {
    if (aHwid != mOwnerHwid) {
      mOwnerHwid = aHwid;
      mOwnedWindow = window;
      Log("New owner: hwid=0x%x window=%p", aHwid, window);
    }
  }

  HWND hwnd = window->GetWindowHandle();
  if (!hwnd) return;

  // Convert screen coords to client coords
  POINT pt = {aScreenX, aScreenY};
  ::ScreenToClient(hwnd, &pt);

  LPARAM lParam = MAKELPARAM(pt.x, pt.y);

  // Update tracked button state and post messages
  if (aEventFlags & 0x01) {  // Left down
    mButtonState[aHwid] |= MK_LBUTTON;
    WPARAM wParam = BuildMouseWParam(aHwid) | MOUSEMUX_MARKER;
    ::PostMessage(hwnd, WM_LBUTTONDOWN, wParam, lParam);
  }
  if (aEventFlags & 0x02) {  // Left up
    mButtonState[aHwid] &= ~MK_LBUTTON;
    WPARAM wParam = BuildMouseWParam(aHwid) | MOUSEMUX_MARKER;
    ::PostMessage(hwnd, WM_LBUTTONUP, wParam, lParam);
  }
  if (aEventFlags & 0x04) {  // Right down
    mButtonState[aHwid] |= MK_RBUTTON;
    WPARAM wParam = BuildMouseWParam(aHwid) | MOUSEMUX_MARKER;
    ::PostMessage(hwnd, WM_RBUTTONDOWN, wParam, lParam);
  }
  if (aEventFlags & 0x08) {  // Right up
    mButtonState[aHwid] &= ~MK_RBUTTON;
    WPARAM wParam = BuildMouseWParam(aHwid) | MOUSEMUX_MARKER;
    ::PostMessage(hwnd, WM_RBUTTONUP, wParam, lParam);
  }
  if (aEventFlags & 0x10) {  // Middle down
    mButtonState[aHwid] |= MK_MBUTTON;
    WPARAM wParam = BuildMouseWParam(aHwid) | MOUSEMUX_MARKER;
    ::PostMessage(hwnd, WM_MBUTTONDOWN, wParam, lParam);
  }
  if (aEventFlags & 0x20) {  // Middle up
    mButtonState[aHwid] &= ~MK_MBUTTON;
    WPARAM wParam = BuildMouseWParam(aHwid) | MOUSEMUX_MARKER;
    ::PostMessage(hwnd, WM_MBUTTONUP, wParam, lParam);
  }
}

void MouseMuxService::HandlePointerWheel(uint32_t aHwid, int aScreenX,
                                         int aScreenY, int aDelta, bool aIsHorizontal) {
  nsWindow* window = nullptr;

  // If this hwid is the owner, always send to owned window
  if (aHwid == mOwnerHwid && mOwnedWindow) {
    window = mOwnedWindow;
  } else {
    window = FindWindowAtPoint(aScreenX, aScreenY);
  }

  if (!window) return;

  HWND hwnd = window->GetWindowHandle();
  if (!hwnd) return;

  // Wheel messages use screen coordinates in lParam
  LPARAM lParam = MAKELPARAM(aScreenX, aScreenY);

  // wParam: HIWORD = wheel delta, LOWORD = key state
  WORD keys = (WORD)BuildMouseWParam(aHwid);
  WPARAM wParam = MAKEWPARAM(keys, (short)aDelta);

  // Add marker in high bit of low word (keys area has unused bits)
  wParam |= MOUSEMUX_MARKER;

  ::PostMessage(hwnd, aIsHorizontal ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL, wParam, lParam);
}

void MouseMuxService::HandleKeyboard(uint32_t aHwid, uint32_t aVkey,
                                     uint32_t aMessage, uint32_t aScanCode,
                                     uint32_t aFlags) {
  // For now, send to focused window
  // TODO: Track focus per MouseMux user
  nsWindow* window = nullptr;
  {
    std::lock_guard<std::mutex> lock(mWindowsMutex);
    if (!mWindows.empty()) {
      window = mWindows[0];  // Use first registered window for now
    }
  }
  if (!window) return;

  HWND hwnd = window->GetWindowHandle();
  if (!hwnd) return;

  // Build lParam for key message
  LPARAM lParam = 1;  // repeat count
  lParam |= (aScanCode & 0xFF) << 16;
  lParam |= (aFlags & 0x01) << 24;  // extended key

  bool isUp = (aMessage == WM_KEYUP || aMessage == WM_SYSKEYUP);
  if (isUp) {
    lParam |= (1 << 30);  // previous key state
    lParam |= (1 << 31);  // transition state
  }

  // Add marker to wParam so nsWindow knows this is from MouseMux
  WPARAM wParam = aVkey | MOUSEMUX_MARKER;
  ::PostMessage(hwnd, aMessage, wParam, lParam);
}

void MouseMuxService::HandleUserList(const std::string& aMessage) {
  Log("User list received");
}

WPARAM MouseMuxService::BuildMouseWParam(uint32_t aHwid) {
  // Build wParam from our tracked button state only
  // No GetKeyState or other Win32 calls
  WPARAM wParam = 0;
  auto it = mButtonState.find(aHwid);
  if (it != mButtonState.end()) {
    wParam = it->second;
  }
  return wParam;
}

nsWindow* MouseMuxService::FindWindowAtPoint(int aScreenX, int aScreenY) {
  std::lock_guard<std::mutex> lock(mWindowsMutex);
  for (nsWindow* window : mWindows) {
    if (!window) continue;
    HWND hwnd = window->GetWindowHandle();
    if (!hwnd || !::IsWindowVisible(hwnd)) continue;
    RECT rect;
    if (::GetWindowRect(hwnd, &rect)) {
      if (aScreenX >= rect.left && aScreenX < rect.right &&
          aScreenY >= rect.top && aScreenY < rect.bottom) {
        return window;
      }
    }
  }
  return nullptr;
}

void MouseMuxService::RegisterWindow(nsWindow* aWindow) {
  std::lock_guard<std::mutex> lock(mWindowsMutex);
  mWindows.push_back(aWindow);
  Log("RegisterWindow %p", aWindow);
}

void MouseMuxService::UnregisterWindow(nsWindow* aWindow) {
  {
    std::lock_guard<std::mutex> lock(mWindowsMutex);
    mWindows.erase(std::remove(mWindows.begin(), mWindows.end(), aWindow), mWindows.end());
  }
  {
    std::lock_guard<std::mutex> lock(mActiveUsersMutex);
    mActiveUsers.erase(aWindow);
  }
  Log("UnregisterWindow %p", aWindow);
}

void MouseMuxService::SetActiveHwid(nsWindow* aWindow, uint32_t aMouseHwid, uint32_t aKeyboardHwid) {
  std::lock_guard<std::mutex> lock(mActiveUsersMutex);
  mActiveUsers[aWindow] = {aMouseHwid, aKeyboardHwid};
}

uint32_t MouseMuxService::GetActiveMouseHwid(nsWindow* aWindow) const {
  std::lock_guard<std::mutex> lock(mActiveUsersMutex);
  auto it = mActiveUsers.find(aWindow);
  return (it != mActiveUsers.end()) ? it->second.mouseHwid : 0;
}

void MouseMuxService::ClearActiveHwid(nsWindow* aWindow) {
  std::lock_guard<std::mutex> lock(mActiveUsersMutex);
  mActiveUsers.erase(aWindow);
}

void MouseMuxService::SetUserMapping(uint32_t aMouseHwid, uint32_t aKeyboardHwid) {
  std::lock_guard<std::mutex> lock(mUserMappingMutex);
  mMouseToKeyboard[aMouseHwid] = aKeyboardHwid;
  mKeyboardToMouse[aKeyboardHwid] = aMouseHwid;
}

uint32_t MouseMuxService::GetKeyboardHwidForMouse(uint32_t aMouseHwid) const {
  std::lock_guard<std::mutex> lock(mUserMappingMutex);
  auto it = mMouseToKeyboard.find(aMouseHwid);
  return (it != mMouseToKeyboard.end()) ? it->second : 0;
}

void MouseMuxService::SetLogCallback(LogCallback aCallback) {
  std::lock_guard<std::mutex> lock(mLogMutex);
  mLogCallback = aCallback;
}

// Version for tracking builds
#define MOUSEMUX_VERSION "3.0"

void MouseMuxService::Log(const char* aFormat, ...) {
  char buf[512];
  va_list args;
  va_start(args, aFormat);
  vsnprintf(buf, sizeof(buf), aFormat, args);
  va_end(args);

  // Get current time
  SYSTEMTIME st;
  GetLocalTime(&st);
  char timeBuf[64];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d.%03d",
           st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

  FILE* f = fopen("D:\\scratch\\firefox\\mousemux_debug.log", "a");
  if (f) {
    fprintf(f, "[MM v%s %s] %s\n", MOUSEMUX_VERSION, timeBuf, buf);
    fclose(f);
  }

  std::lock_guard<std::mutex> lock(mLogMutex);
  if (mLogCallback) mLogCallback(buf);
}

}  // namespace widget
}  // namespace mozilla
