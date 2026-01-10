/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MouseMuxClient.h"
#include "InputFilter.h"
#include <ws2tcpip.h>
#include <cstdio>
#include <cstdarg>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

#define MOUSEMUX_CLIENT_VERSION "5.1"
#define MOUSEMUX_BUILD_TIME __DATE__ " " __TIME__

namespace mozilla {
namespace widget {

MouseMuxClient::MouseMuxClient(HWND aOwnerHwnd) : mOwnerHwnd(aOwnerHwnd) {
  Log("MouseMuxClient created for HWND %p (build: %s)", aOwnerHwnd, MOUSEMUX_BUILD_TIME);
}

MouseMuxClient::~MouseMuxClient() {
  Log("MouseMuxClient destroying for HWND %p", mOwnerHwnd);
  Disconnect();
  if (mDebugDialog) {
    ::DestroyWindow(mDebugDialog);
    mDebugDialog = nullptr;
  }
}

bool MouseMuxClient::Connect(const wchar_t* aUrl) {
  if (mConnected) return true;

  mServerUrl = aUrl;
  mShouldStop = false;

  // Initialize Winsock
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    Log("WSAStartup failed");
    return false;
  }

  mWorkerThread = std::thread(&MouseMuxClient::WebSocketThread, this);
  return true;
}

void MouseMuxClient::Disconnect() {
  mShouldStop = true;
  mConnected = false;

  if (mSocket != INVALID_SOCKET) {
    // Shutdown first to interrupt any blocking recv()
    ::shutdown(mSocket, SD_BOTH);
    ::closesocket(mSocket);
    mSocket = INVALID_SOCKET;
  }

  // Don't block UI thread - detach instead of join
  if (mWorkerThread.joinable()) {
    mWorkerThread.detach();
  }

  UpdateDebugStatus();
}

void MouseMuxClient::WebSocketThread() {
  Log("WebSocket thread started");

  // Parse URL (ws://host:port)
  std::wstring url = mServerUrl;
  std::wstring host = L"localhost";
  int port = 41001;

  size_t hostStart = url.find(L"://");
  if (hostStart != std::wstring::npos) {
    hostStart += 3;
    size_t portStart = url.find(L":", hostStart);
    if (portStart != std::wstring::npos) {
      host = url.substr(hostStart, portStart - hostStart);
      port = _wtoi(url.substr(portStart + 1).c_str());
    } else {
      host = url.substr(hostStart);
    }
  }

  // Connect
  struct addrinfo hints = {0}, *result = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  char hostA[256], portA[16];
  wcstombs(hostA, host.c_str(), 256);
  sprintf(portA, "%d", port);

  if (getaddrinfo(hostA, portA, &hints, &result) != 0) {
    Log("getaddrinfo failed");
    return;
  }

  mSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (mSocket == INVALID_SOCKET) {
    Log("socket creation failed");
    freeaddrinfo(result);
    return;
  }

  if (connect(mSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
    Log("connect failed");
    freeaddrinfo(result);
    closesocket(mSocket);
    mSocket = INVALID_SOCKET;
    return;
  }

  freeaddrinfo(result);

  // WebSocket handshake
  char request[512];
  sprintf(request,
          "GET / HTTP/1.1\r\n"
          "Host: %s:%d\r\n"
          "Upgrade: websocket\r\n"
          "Connection: Upgrade\r\n"
          "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
          "Sec-WebSocket-Version: 13\r\n\r\n",
          hostA, port);

  send(mSocket, request, (int)strlen(request), 0);

  char response[1024];
  int recvLen = recv(mSocket, response, sizeof(response) - 1, 0);
  if (recvLen <= 0) {
    Log("Handshake failed - no response");
    closesocket(mSocket);
    mSocket = INVALID_SOCKET;
    return;
  }
  response[recvLen] = '\0';

  if (strstr(response, "101") == nullptr) {
    Log("Handshake failed - not 101");
    closesocket(mSocket);
    mSocket = INVALID_SOCKET;
    return;
  }

  mConnected = true;
  Log("Connected to MouseMux server");
  UpdateDebugStatus();

  // Set socket timeout for recv
  DWORD timeout = 100;
  setsockopt(mSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

  // Message loop
  std::string messageBuffer;
  while (!mShouldStop && mSocket != INVALID_SOCKET) {
    unsigned char header[2];
    int headerLen = recv(mSocket, (char*)header, 2, 0);

    if (headerLen <= 0) {
      if (WSAGetLastError() == WSAETIMEDOUT) continue;
      break;
    }

    if (headerLen < 2) continue;

    bool fin = (header[0] & 0x80) != 0;
    int opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payloadLen = header[1] & 0x7F;

    if (payloadLen == 126) {
      unsigned char ext[2];
      if (recv(mSocket, (char*)ext, 2, 0) != 2) break;
      payloadLen = (ext[0] << 8) | ext[1];
    } else if (payloadLen == 127) {
      unsigned char ext[8];
      if (recv(mSocket, (char*)ext, 8, 0) != 8) break;
      payloadLen = 0;
      for (int i = 0; i < 8; i++) {
        payloadLen = (payloadLen << 8) | ext[i];
      }
    }

    unsigned char mask[4] = {0};
    if (masked) {
      if (recv(mSocket, (char*)mask, 4, 0) != 4) break;
    }

    if (payloadLen > 65536) {
      Log("Payload too large: %llu", payloadLen);
      break;
    }

    std::string payload;
    payload.resize((size_t)payloadLen);
    size_t received = 0;
    while (received < payloadLen) {
      int chunk = recv(mSocket, &payload[received], (int)(payloadLen - received), 0);
      if (chunk <= 0) break;
      received += chunk;
    }

    if (received < payloadLen) break;

    if (masked) {
      for (size_t i = 0; i < payloadLen; i++) {
        payload[i] ^= mask[i % 4];
      }
    }

    if (opcode == 0x08) {
      Log("Server closed connection");
      break;
    }

    if (opcode == 0x01 || opcode == 0x02) {
      messageBuffer += payload;
      if (fin) {
        HandleMessage(messageBuffer);
        messageBuffer.clear();
      }
    }
  }

  mConnected = false;
  Log("WebSocket thread exiting");
  UpdateDebugStatus();
}

void MouseMuxClient::HandleMessage(const std::string& aMessage) {
  // Log raw message (truncated) - skip motion to reduce noise
  if (aMessage.find("motion") == std::string::npos) {
    if (aMessage.length() < 200) {
      Log("MSG: %s", aMessage.c_str());
    } else {
      Log("MSG: %.200s...", aMessage.c_str());
    }
  }

  // Parse JSON manually (simple parser for our specific format)
  auto getString = [&](const char* key) -> std::string {
    std::string search = "\"" + std::string(key) + "\":\"";
    size_t pos = aMessage.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    size_t end = aMessage.find("\"", pos);
    if (end == std::string::npos) return "";
    return aMessage.substr(pos, end - pos);
  };

  auto getInt = [&](const char* key) -> int {
    std::string search = "\"" + std::string(key) + "\":";
    size_t pos = aMessage.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.length();
    return atoi(aMessage.c_str() + pos);
  };

  auto getUint = [&](const char* key) -> uint32_t {
    std::string search = "\"" + std::string(key) + "\":";
    size_t pos = aMessage.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.length();
    return (uint32_t)strtoul(aMessage.c_str() + pos, nullptr, 10);
  };

  std::string type = getString("type");

  // SDK v2.2.33 message types
  if (type == "pointer.motion.notify.M2A") {
    HandlePointerMotion(getUint("hwid"), getInt("x"), getInt("y"));
  } else if (type == "pointer.button.notify.M2A") {
    HandlePointerButton(getUint("hwid"), getInt("x"), getInt("y"),
                        getUint("data"));
  } else if (type == "pointer.scroll.notify.M2A") {
    HandlePointerWheel(getUint("hwid"), getInt("x"), getInt("y"),
                       getInt("delta"), aMessage.find("\"horizontal\":true") != std::string::npos);
  } else if (type == "keyboard.key.notify.M2A") {
    HandleKeyboard(getUint("hwid"), getUint("vkey"), getUint("message"),
                   getUint("scan"), getUint("flags"));
  } else if (type == "user_list") {
    // Parse user mappings
    std::lock_guard<std::mutex> lock(mMappingMutex);
    mMouseToKeyboard.clear();
    // Simple parse for users array
    size_t pos = aMessage.find("\"users\":");
    if (pos != std::string::npos) {
      size_t arrayStart = aMessage.find("[", pos);
      size_t arrayEnd = aMessage.find("]", arrayStart);
      if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
        std::string usersStr = aMessage.substr(arrayStart, arrayEnd - arrayStart + 1);
        size_t userPos = 0;
        while ((userPos = usersStr.find("{", userPos)) != std::string::npos) {
          size_t userEnd = usersStr.find("}", userPos);
          if (userEnd == std::string::npos) break;
          std::string userObj = usersStr.substr(userPos, userEnd - userPos + 1);

          auto getUserInt = [&](const char* key) -> uint32_t {
            std::string search = "\"" + std::string(key) + "\":";
            size_t p = userObj.find(search);
            if (p == std::string::npos) return 0;
            p += search.length();
            return (uint32_t)strtoul(userObj.c_str() + p, nullptr, 10);
          };

          uint32_t mouseHwid = getUserInt("mouse_hwid");
          uint32_t keyboardHwid = getUserInt("keyboard_hwid");
          if (mouseHwid && keyboardHwid) {
            mMouseToKeyboard[mouseHwid] = keyboardHwid;
          }
          userPos = userEnd;
        }
      }
    }
    Log("User list updated: %zu mappings", mMouseToKeyboard.size());
  }
}

bool MouseMuxClient::IsPointInWindow(int aScreenX, int aScreenY) {
  if (!mOwnerHwnd || !::IsWindow(mOwnerHwnd)) return false;

  RECT rect;
  if (!::GetWindowRect(mOwnerHwnd, &rect)) return false;

  return aScreenX >= rect.left && aScreenX < rect.right &&
         aScreenY >= rect.top && aScreenY < rect.bottom;
}

POINT MouseMuxClient::ScreenToClient(int aScreenX, int aScreenY) {
  POINT pt = {aScreenX, aScreenY};
  ::ScreenToClient(mOwnerHwnd, &pt);
  return pt;
}

WPARAM MouseMuxClient::BuildMouseWParam(uint32_t aHwid) {
  WPARAM wParam = MOUSEMUX_MARKER;

  std::lock_guard<std::mutex> lock(mButtonStateMutex);
  auto it = mButtonState.find(aHwid);
  if (it != mButtonState.end()) {
    uint32_t state = it->second;
    if (state & 0x01) wParam |= MK_LBUTTON;
    if (state & 0x04) wParam |= MK_RBUTTON;
    if (state & 0x10) wParam |= MK_MBUTTON;
  }

  return wParam;
}

void MouseMuxClient::HandlePointerMotion(uint32_t aHwid, int aScreenX, int aScreenY) {
  // Update last known position
  {
    std::lock_guard<std::mutex> lock(mMousePosMutex);
    mLastMousePos[aHwid] = {aScreenX, aScreenY};
  }

  // If this hwid owns this window, always handle
  // Otherwise, only handle if point is in our window
  bool isOwner = (aHwid == mOwnerHwid);
  bool inWindow = IsPointInWindow(aScreenX, aScreenY);

  if (!isOwner && !inWindow) return;

  POINT clientPt = ScreenToClient(aScreenX, aScreenY);
  LPARAM lParam = MAKELPARAM(clientPt.x, clientPt.y);
  WPARAM wParam = BuildMouseWParam(aHwid);

  ::PostMessage(mOwnerHwnd, WM_MOUSEMOVE, wParam, lParam);
}

void MouseMuxClient::HandlePointerButton(uint32_t aHwid, int aScreenX, int aScreenY,
                                         uint32_t aEventFlags) {
  // Update last known position
  {
    std::lock_guard<std::mutex> lock(mMousePosMutex);
    mLastMousePos[aHwid] = {aScreenX, aScreenY};
  }

  // Decode button events (SDK v2.2.32 format)
  bool leftDown = (aEventFlags & 0x01) != 0;
  bool leftUp = (aEventFlags & 0x02) != 0;
  bool rightDown = (aEventFlags & 0x04) != 0;
  bool rightUp = (aEventFlags & 0x08) != 0;
  bool middleDown = (aEventFlags & 0x10) != 0;
  bool middleUp = (aEventFlags & 0x20) != 0;

  // Update button state
  {
    std::lock_guard<std::mutex> lock(mButtonStateMutex);
    uint32_t& state = mButtonState[aHwid];
    if (leftDown) state |= 0x01;
    if (leftUp) state &= ~0x01;
    if (rightDown) state |= 0x04;
    if (rightUp) state &= ~0x04;
    if (middleDown) state |= 0x10;
    if (middleUp) state &= ~0x10;
  }

  bool isButtonDown = leftDown || rightDown || middleDown;
  bool isOwner = (aHwid == mOwnerHwid);
  bool inWindow = IsPointInWindow(aScreenX, aScreenY);

  // Debug logging
  if (isButtonDown) {
    RECT rect = {0};
    if (mOwnerHwnd) ::GetWindowRect(mOwnerHwnd, &rect);
    Log("Button hwid=0x%X pos=(%d,%d) flags=0x%X inWnd=%d wndRect=(%d,%d,%d,%d)",
        aHwid, aScreenX, aScreenY, aEventFlags, inWindow,
        rect.left, rect.top, rect.right, rect.bottom);
  }

  // On button down in our window, claim ownership
  if (isButtonDown && inWindow) {
    if (aHwid != mOwnerHwid) {
      mOwnerHwid = aHwid;
      Log("New owner: hwid=0x%X", aHwid);
      UpdateDebugStatus();
    }
    isOwner = true;
  }

  // Only handle if owner or in window
  if (!isOwner && !inWindow) return;

  POINT clientPt = ScreenToClient(aScreenX, aScreenY);
  LPARAM lParam = MAKELPARAM(clientPt.x, clientPt.y);
  WPARAM wParam = BuildMouseWParam(aHwid);

  if (leftDown) ::PostMessage(mOwnerHwnd, WM_LBUTTONDOWN, wParam, lParam);
  if (leftUp) ::PostMessage(mOwnerHwnd, WM_LBUTTONUP, wParam, lParam);
  if (rightDown) ::PostMessage(mOwnerHwnd, WM_RBUTTONDOWN, wParam, lParam);
  if (rightUp) ::PostMessage(mOwnerHwnd, WM_RBUTTONUP, wParam, lParam);
  if (middleDown) ::PostMessage(mOwnerHwnd, WM_MBUTTONDOWN, wParam, lParam);
  if (middleUp) ::PostMessage(mOwnerHwnd, WM_MBUTTONUP, wParam, lParam);
}

void MouseMuxClient::HandlePointerWheel(uint32_t aHwid, int aScreenX, int aScreenY,
                                        int aDelta, bool aIsHorizontal) {
  bool isOwner = (aHwid == mOwnerHwid);
  bool inWindow = IsPointInWindow(aScreenX, aScreenY);

  if (!isOwner && !inWindow) return;

  POINT clientPt = ScreenToClient(aScreenX, aScreenY);
  LPARAM lParam = MAKELPARAM(clientPt.x, clientPt.y);
  WPARAM wParam = BuildMouseWParam(aHwid);
  wParam |= ((aDelta & 0xFFFF) << 16);

  UINT msg = aIsHorizontal ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL;
  ::PostMessage(mOwnerHwnd, msg, wParam, lParam);
}

void MouseMuxClient::HandleKeyboard(uint32_t aHwid, uint32_t aVkey, uint32_t aMessage,
                                    uint32_t aScanCode, uint32_t aFlags) {
  // Only process if window has an owner
  if (mOwnerHwid == 0) return;

  // Try to find the mouse hwid associated with this keyboard
  uint32_t mouseHwid = 0;
  {
    std::lock_guard<std::mutex> lock(mMappingMutex);
    for (const auto& pair : mMouseToKeyboard) {
      if (pair.second == aHwid) {
        mouseHwid = pair.first;
        break;
      }
    }
  }

  // If we have a mapping, check if it matches the owner
  // If no mapping exists, accept keyboard events for any owned window
  if (mouseHwid != 0 && mouseHwid != mOwnerHwid) return;

  Log("Key hwid=0x%X vkey=%u msg=%u scan=%u owner=0x%X",
      aHwid, aVkey, aMessage, aScanCode, mOwnerHwid.load());

  // Build lParam for keyboard message (per MouseMux rules: use PostMessage only)
  LPARAM lParam = 1;  // repeat count = 1
  lParam |= (aScanCode & 0xFF) << 16;  // scan code
  if (aFlags & 0x01) lParam |= (1 << 24);  // extended key flag

  if (aMessage == WM_KEYUP || aMessage == WM_SYSKEYUP) {
    lParam |= (1 << 30);  // previous key state (was down)
    lParam |= (1 << 31);  // transition state (being released)
  }

  // Post to the owner window - Firefox will route to focused child
  // Add MOUSEMUX_MARKER so it passes through InputFilter when blocking is enabled
  WPARAM markedVkey = aVkey | MOUSEMUX_MARKER;
  BOOL result = ::PostMessage(mOwnerHwnd, aMessage, markedVkey, lParam);
  Log("PostMessage(hwnd=%p, msg=%u, vk=%u|MARKER) result=%d", mOwnerHwnd, aMessage, aVkey, result);
}

void MouseMuxClient::Log(const char* aFormat, ...) {
  char buf[512];
  va_list args;
  va_start(args, aFormat);
  vsnprintf(buf, sizeof(buf), aFormat, args);
  va_end(args);

  // Log to file
  FILE* f = fopen("D:/scratch/firefox/mousemux_client.log", "a");
  if (f) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[v%s %02d:%02d:%02d.%03d HWND=%p] %s\n",
            MOUSEMUX_CLIENT_VERSION,
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            mOwnerHwnd, buf);
    fflush(f);
    fclose(f);
  }

  // Append to debug dialog
  AppendLog(buf);
}

void MouseMuxClient::AppendLog(const char* text) {
  std::lock_guard<std::mutex> lock(mLogMutex);
  mLogLines.push_back(text);
  while (mLogLines.size() > 100) {
    mLogLines.erase(mLogLines.begin());
  }

  if (mLogEdit && ::IsWindow(mLogEdit)) {
    std::string fullText;
    for (const auto& line : mLogLines) {
      fullText += line;
      fullText += "\r\n";
    }
    ::SetWindowTextA(mLogEdit, fullText.c_str());
    int lineCount = (int)::SendMessage(mLogEdit, EM_GETLINECOUNT, 0, 0);
    ::SendMessage(mLogEdit, EM_LINESCROLL, 0, lineCount);
  }
}

void MouseMuxClient::ShowDebugDialog() {
  if (!mDebugDialog) {
    CreateDebugDialog();
  }
  if (mDebugDialog) {
    ::ShowWindow(mDebugDialog, SW_SHOWNORMAL);
    ::SetForegroundWindow(mDebugDialog);
    mDebugDialogVisible = true;
    UpdateDebugStatus();
  }
}

void MouseMuxClient::HideDebugDialog() {
  if (mDebugDialog) {
    ::ShowWindow(mDebugDialog, SW_HIDE);
  }
  mDebugDialogVisible = false;
}

void MouseMuxClient::CreateDebugDialog() {
  static bool classRegistered = false;
  if (!classRegistered) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DebugDialogProc;
    wc.hInstance = ::GetModuleHandle(nullptr);
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MouseMuxClientDebug";
    ::RegisterClassExW(&wc);
    classRegistered = true;
  }

  // Position dialog near owner window
  RECT ownerRect = {100, 100, 500, 450};
  if (mOwnerHwnd) {
    ::GetWindowRect(mOwnerHwnd, &ownerRect);
  }

  wchar_t title[128];
  swprintf(title, 128, L"MouseMux Client v%S - HWND %p",
           MOUSEMUX_CLIENT_VERSION, mOwnerHwnd);

  mDebugDialog = ::CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_APPWINDOW, L"MouseMuxClientDebug", title,
      WS_OVERLAPPEDWINDOW, ownerRect.right + 10, ownerRect.top, 400, 350,
      nullptr, nullptr, ::GetModuleHandle(nullptr), this);

  mStatusLabel = ::CreateWindowW(L"STATIC", L"Status: Disconnected",
                                 WS_CHILD | WS_VISIBLE, 10, 10, 380, 20,
                                 mDebugDialog, (HMENU)ID_STATUS, nullptr, nullptr);

  mConnectBtn = ::CreateWindowW(L"BUTTON", L"Connect",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 40,
                                120, 25, mDebugDialog, (HMENU)ID_CONNECT, nullptr, nullptr);

  mBlockBtn = ::CreateWindowW(L"BUTTON", L"Block Input",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 140, 40,
                              120, 25, mDebugDialog, (HMENU)ID_BLOCK, nullptr, nullptr);

  mLogEdit = ::CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
      10, 75, 370, 230, mDebugDialog, (HMENU)ID_LOG, nullptr, nullptr);

  HFONT hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
  ::SendMessage(mStatusLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
  ::SendMessage(mConnectBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
  ::SendMessage(mBlockBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
  ::SendMessage(mLogEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

  Log("Debug dialog created");
}

void MouseMuxClient::UpdateDebugStatus() {
  if (!mStatusLabel || !::IsWindow(mStatusLabel)) return;

  wchar_t buf[256];
  uint32_t owner = mOwnerHwid;
  bool connected = mConnected;
  bool blocked = InputFilter::IsEnabled();

  if (owner) {
    swprintf(buf, 256, L"%s | %s | Owner: 0x%X",
             connected ? L"Connected" : L"Disconnected",
             blocked ? L"BLOCKED" : L"Normal",
             owner);
  } else {
    swprintf(buf, 256, L"%s | %s | Owner: None",
             connected ? L"Connected" : L"Disconnected",
             blocked ? L"BLOCKED" : L"Normal");
  }
  ::SetWindowTextW(mStatusLabel, buf);

  if (mConnectBtn && ::IsWindow(mConnectBtn)) {
    ::SetWindowTextW(mConnectBtn, connected ? L"Disconnect" : L"Connect");
  }
  if (mBlockBtn && ::IsWindow(mBlockBtn)) {
    ::SetWindowTextW(mBlockBtn, blocked ? L"Unblock" : L"Block Input");
  }
}

LRESULT CALLBACK MouseMuxClient::DebugDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  MouseMuxClient* self = nullptr;

  if (msg == WM_CREATE) {
    CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
    self = (MouseMuxClient*)cs->lpCreateParams;
    ::SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
  } else {
    self = (MouseMuxClient*)::GetWindowLongPtr(hwnd, GWLP_USERDATA);
  }

  if (self) {
    return self->HandleDebugMessage(msg, wParam, lParam);
  }
  return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT MouseMuxClient::HandleDebugMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_COMMAND:
      if (LOWORD(wParam) == ID_CONNECT) {
        if (mConnected) {
          Disconnect();
        } else {
          Connect();
        }
        return 0;
      }
      if (LOWORD(wParam) == ID_BLOCK) {
        if (InputFilter::IsEnabled()) {
          InputFilter::Disable();
          Log("Input filter DISABLED");
        } else {
          InputFilter::Enable();
          Log("Input filter ENABLED");
        }
        UpdateDebugStatus();
        return 0;
      }
      break;
    case WM_CLOSE:
      HideDebugDialog();
      return 0;
    case WM_DESTROY:
      mDebugDialog = nullptr;
      mStatusLabel = nullptr;
      mBlockBtn = nullptr;
      mLogEdit = nullptr;
      return 0;
  }
  return ::DefWindowProc(mDebugDialog, msg, wParam, lParam);
}

}  // namespace widget
}  // namespace mozilla
