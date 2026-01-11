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
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

#define MOUSEMUX_CLIENT_VERSION "5.9"
#define MOUSEMUX_BUILD_TIME __DATE__ " " __TIME__

namespace mozilla {
namespace widget {

static bool sWinsockInitialized = false;
static std::mutex sWinsockMutex;

static bool EnsureWinsockInitialized() {
  std::lock_guard<std::mutex> lock(sWinsockMutex);
  if (!sWinsockInitialized) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
      return false;
    }
    sWinsockInitialized = true;
  }
  return true;
}

MouseMuxClient::MouseMuxClient(HWND aOwnerHwnd) : mOwnerHwnd(aOwnerHwnd) {
  Log("MouseMuxClient v%s created for HWND %p", MOUSEMUX_CLIENT_VERSION, aOwnerHwnd);
}

MouseMuxClient::~MouseMuxClient() {
  Log("MouseMuxClient destroying");
  mShouldStop.store(true);

  {
    std::lock_guard<std::mutex> sockLock(mSocketMutex);
    if (mSocket != INVALID_SOCKET) {
      ::shutdown(mSocket, SD_BOTH);
      ::closesocket(mSocket);
      mSocket = INVALID_SOCKET;
    }
  }

  // Wait for thread with timeout
  if (mWorkerThread.joinable()) {
    auto start = std::chrono::steady_clock::now();
    while (mThreadRunning.load()) {
      auto elapsed = std::chrono::steady_clock::now() - start;
      if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > 500) {
        Log("Worker thread timeout, detaching");
        mWorkerThread.detach();
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (mWorkerThread.joinable()) {
      mWorkerThread.join();
    }
  }

  if (mDebugDialog && ::IsWindow(mDebugDialog)) {
    ::DestroyWindow(mDebugDialog);
  }
  mDebugDialog = nullptr;
}

bool MouseMuxClient::Connect(const wchar_t* aUrl) {
  std::lock_guard<std::mutex> lock(mConnectMutex);

  if (mConnected.load()) {
    Log("Already connected");
    return true;
  }

  // Wait for any previous thread
  if (mWorkerThread.joinable()) {
    if (mThreadRunning.load()) {
      Log("Previous thread still running");
      return false;
    }
    mWorkerThread.join();
  }

  if (!EnsureWinsockInitialized()) {
    Log("WSAStartup failed");
    return false;
  }

  mServerUrl = aUrl ? aUrl : L"ws://localhost:41001";
  mShouldStop.store(false);

  mWorkerThread = std::thread(&MouseMuxClient::WebSocketThread, this);
  Log("Worker thread started");
  return true;
}

void MouseMuxClient::Disconnect() {
  mShouldStop.store(true);
  mConnected.store(false);

  {
    std::lock_guard<std::mutex> sockLock(mSocketMutex);
    if (mSocket != INVALID_SOCKET) {
      ::shutdown(mSocket, SD_BOTH);
      ::closesocket(mSocket);
      mSocket = INVALID_SOCKET;
    }
  }

  // Don't join here - would block UI. Thread will exit on its own.
  if (mWorkerThread.joinable()) {
    mWorkerThread.detach();
  }

  UpdateDebugStatusSafe();
}

void MouseMuxClient::WebSocketThread() {
  mThreadRunning.store(true);
  Log("WebSocket thread started");

  SOCKET sock = INVALID_SOCKET;

  auto cleanup = [&]() {
    if (sock != INVALID_SOCKET) {
      ::closesocket(sock);
    }
    {
      std::lock_guard<std::mutex> lock(mSocketMutex);
      mSocket = INVALID_SOCKET;
    }
    mConnected.store(false);
    Log("WebSocket thread exiting");
    mThreadRunning.store(false);
    UpdateDebugStatusSafe();
  };

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

  char hostA[256] = {0};
  char portA[16] = {0};
  wcstombs(hostA, host.c_str(), sizeof(hostA) - 1);
  snprintf(portA, sizeof(portA), "%d", port);

  struct addrinfo hints = {0}, *result = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(hostA, portA, &hints, &result) != 0) {
    Log("getaddrinfo failed for %s:%s", hostA, portA);
    cleanup();
    return;
  }

  sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (sock == INVALID_SOCKET) {
    Log("socket() failed: %d", WSAGetLastError());
    freeaddrinfo(result);
    cleanup();
    return;
  }

  // Set connect timeout
  DWORD connTimeout = 3000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&connTimeout, sizeof(connTimeout));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&connTimeout, sizeof(connTimeout));

  if (mShouldStop.load()) {
    freeaddrinfo(result);
    cleanup();
    return;
  }

  if (connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
    Log("connect() failed: %d", WSAGetLastError());
    freeaddrinfo(result);
    cleanup();
    return;
  }
  freeaddrinfo(result);

  {
    std::lock_guard<std::mutex> lock(mSocketMutex);
    mSocket = sock;
  }

  char request[512];
  snprintf(request, sizeof(request),
          "GET / HTTP/1.1\r\n"
          "Host: %s:%d\r\n"
          "Upgrade: websocket\r\n"
          "Connection: Upgrade\r\n"
          "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
          "Sec-WebSocket-Version: 13\r\n\r\n",
          hostA, port);

  if (send(sock, request, (int)strlen(request), 0) == SOCKET_ERROR) {
    Log("send() handshake failed: %d", WSAGetLastError());
    cleanup();
    return;
  }

  char response[1024];
  int recvLen = recv(sock, response, sizeof(response) - 1, 0);
  if (recvLen <= 0) {
    Log("Handshake failed - no response: %d", WSAGetLastError());
    cleanup();
    return;
  }
  response[recvLen] = '\0';

  if (strstr(response, "101") == nullptr) {
    Log("Handshake failed - expected 101, got: %.100s", response);
    cleanup();
    return;
  }

  mConnected.store(true);
  Log("Connected to MouseMux server at %s:%d", hostA, port);
  UpdateDebugStatusSafe();

  DWORD timeout = 100;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

  std::string messageBuffer;
  while (!mShouldStop.load()) {
    {
      std::lock_guard<std::mutex> lock(mSocketMutex);
      if (mSocket == INVALID_SOCKET) break;
    }

    unsigned char header[2];
    int headerLen = recv(sock, (char*)header, 2, 0);

    if (headerLen <= 0) {
      int err = WSAGetLastError();
      if (err == WSAETIMEDOUT) continue;
      if (err == WSAEINTR) continue;
      Log("recv() header failed: %d", err);
      break;
    }

    if (headerLen < 2) continue;

    bool fin = (header[0] & 0x80) != 0;
    int opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payloadLen = header[1] & 0x7F;

    if (payloadLen == 126) {
      unsigned char ext[2];
      if (recv(sock, (char*)ext, 2, 0) != 2) break;
      payloadLen = (ext[0] << 8) | ext[1];
    } else if (payloadLen == 127) {
      unsigned char ext[8];
      if (recv(sock, (char*)ext, 8, 0) != 8) break;
      payloadLen = 0;
      for (int i = 0; i < 8; i++) {
        payloadLen = (payloadLen << 8) | ext[i];
      }
    }

    unsigned char mask[4] = {0};
    if (masked) {
      if (recv(sock, (char*)mask, 4, 0) != 4) break;
    }

    if (payloadLen > 65536) {
      Log("Payload too large: %llu bytes", (unsigned long long)payloadLen);
      break;
    }

    std::string payload;
    payload.resize((size_t)payloadLen);
    size_t received = 0;
    while (received < payloadLen && !mShouldStop.load()) {
      int chunk = recv(sock, &payload[received], (int)(payloadLen - received), 0);
      if (chunk <= 0) {
        int err = WSAGetLastError();
        if (err == WSAETIMEDOUT) continue;
        break;
      }
      received += chunk;
    }

    if (received < payloadLen) break;

    if (masked) {
      for (size_t i = 0; i < payloadLen; i++) {
        payload[i] ^= mask[i % 4];
      }
    }

    if (opcode == 0x08) {
      Log("Server sent close frame");
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

  cleanup();
}

void MouseMuxClient::UpdateDebugStatusSafe() {
  if (mDebugDialog && ::IsWindow(mDebugDialog)) {
    ::PostMessage(mDebugDialog, WM_MOUSEMUX_UPDATE, 0, 0);
  }
}

void MouseMuxClient::HandleMessage(const std::string& aMessage) {
  auto getString = [&](const char* key) -> std::string {
    std::string search = std::string("\"") + key + "\":\"";
    size_t pos = aMessage.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    size_t end = aMessage.find("\"", pos);
    if (end == std::string::npos) return "";
    return aMessage.substr(pos, end - pos);
  };

  auto getInt = [&](const char* key) -> int {
    std::string search = std::string("\"") + key + "\":";
    size_t pos = aMessage.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.length();
    return atoi(aMessage.c_str() + pos);
  };

  auto getUint = [&](const char* key) -> uint32_t {
    std::string search = std::string("\"") + key + "\":";
    size_t pos = aMessage.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.length();
    return (uint32_t)strtoul(aMessage.c_str() + pos, nullptr, 10);
  };

  std::string type = getString("type");

  if (type == "pointer.motion.notify.M2A") {
    HandlePointerMotion(getUint("hwid"), getInt("x"), getInt("y"));
  } else if (type == "pointer.button.notify.M2A") {
    HandlePointerButton(getUint("hwid"), getInt("x"), getInt("y"), getUint("data"));
  } else if (type == "pointer.scroll.notify.M2A") {
    bool horiz = aMessage.find("\"horizontal\":true") != std::string::npos;
    HandlePointerWheel(getUint("hwid"), getInt("x"), getInt("y"), getInt("delta"), horiz);
  } else if (type == "keyboard.key.notify.M2A") {
    HandleKeyboard(getUint("hwid"), getUint("vkey"), getUint("message"),
                   getUint("scan"), getUint("flags"));
  } else if (type == "user_list") {
    ParseUserList(aMessage);
  }
}

void MouseMuxClient::ParseUserList(const std::string& aMessage) {
  std::lock_guard<std::mutex> lock(mMappingMutex);
  mMouseToKeyboard.clear();

  size_t pos = aMessage.find("\"users\":");
  if (pos == std::string::npos) return;

  size_t arrayStart = aMessage.find("[", pos);
  size_t arrayEnd = aMessage.find("]", arrayStart);
  if (arrayStart == std::string::npos || arrayEnd == std::string::npos) return;

  std::string usersStr = aMessage.substr(arrayStart, arrayEnd - arrayStart + 1);
  size_t userPos = 0;

  while ((userPos = usersStr.find("{", userPos)) != std::string::npos) {
    size_t userEnd = usersStr.find("}", userPos);
    if (userEnd == std::string::npos) break;

    std::string userObj = usersStr.substr(userPos, userEnd - userPos + 1);

    auto getUserInt = [&](const char* key) -> uint32_t {
      std::string search = std::string("\"") + key + "\":";
      size_t p = userObj.find(search);
      if (p == std::string::npos) return 0;
      return (uint32_t)strtoul(userObj.c_str() + p + search.length(), nullptr, 10);
    };

    uint32_t mouseHwid = getUserInt("mouse_hwid");
    uint32_t keyboardHwid = getUserInt("keyboard_hwid");
    if (mouseHwid && keyboardHwid) {
      mMouseToKeyboard[mouseHwid] = keyboardHwid;
    }
    userPos = userEnd;
  }

  Log("User list updated: %zu mappings", mMouseToKeyboard.size());
}

bool MouseMuxClient::IsPointInWindow(int aScreenX, int aScreenY) {
  HWND hwnd = mOwnerHwnd;
  if (!hwnd || !::IsWindow(hwnd)) return false;

  RECT rect;
  if (!::GetWindowRect(hwnd, &rect)) return false;

  return aScreenX >= rect.left && aScreenX < rect.right &&
         aScreenY >= rect.top && aScreenY < rect.bottom;
}

POINT MouseMuxClient::ScreenToClient(int aScreenX, int aScreenY) {
  POINT pt = {aScreenX, aScreenY};
  if (mOwnerHwnd) {
    ::ScreenToClient(mOwnerHwnd, &pt);
  }
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
  {
    std::lock_guard<std::mutex> lock(mMousePosMutex);
    mLastMousePos[aHwid] = {aScreenX, aScreenY};
  }

  uint32_t owner = mOwnerHwid.load();
  bool isOwner = (aHwid == owner);
  bool inWindow = IsPointInWindow(aScreenX, aScreenY);

  // If window has an owner, only process that owner's motion
  // If no owner, only process motion if cursor is in window (hover)
  if (owner != 0) {
    if (!isOwner) return;
  } else {
    if (!inWindow) return;
  }
  if (!mOwnerHwnd) return;

  POINT clientPt = ScreenToClient(aScreenX, aScreenY);
  LPARAM lParam = MAKELPARAM(clientPt.x, clientPt.y);
  WPARAM wParam = BuildMouseWParam(aHwid);

  ::PostMessage(mOwnerHwnd, WM_MOUSEMOVE, wParam, lParam);
}

void MouseMuxClient::HandlePointerButton(uint32_t aHwid, int aScreenX, int aScreenY,
                                         uint32_t aEventFlags) {
  {
    std::lock_guard<std::mutex> lock(mMousePosMutex);
    mLastMousePos[aHwid] = {aScreenX, aScreenY};
  }

  bool leftDown = (aEventFlags & 0x01) != 0;
  bool leftUp = (aEventFlags & 0x02) != 0;
  bool rightDown = (aEventFlags & 0x04) != 0;
  bool rightUp = (aEventFlags & 0x08) != 0;
  bool middleDown = (aEventFlags & 0x10) != 0;
  bool middleUp = (aEventFlags & 0x20) != 0;

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
  uint32_t owner = mOwnerHwid.load();
  bool isOwner = (aHwid == owner);
  bool inWindow = IsPointInWindow(aScreenX, aScreenY);

  // Release ownership if user clicked outside this window
  if (isButtonDown && !inWindow && isOwner) {
    mOwnerHwid.store(0);
    Log("Released owner: hwid=0x%X (clicked outside)", aHwid);
    UpdateDebugStatusSafe();
    return;
  }

  if (isButtonDown && inWindow && aHwid != owner) {
    mOwnerHwid.store(aHwid);
    Log("New owner: hwid=0x%X (was 0x%X)", aHwid, owner);
    UpdateDebugStatusSafe();
    isOwner = true;
  }

  // If window has an owner, only process that owner's motion
  // If no owner, only process motion if cursor is in window (hover)
  if (owner != 0) {
    if (!isOwner) return;
  } else {
    if (!inWindow) return;
  }
  if (!mOwnerHwnd) return;

  POINT clientPt = ScreenToClient(aScreenX, aScreenY);
  LPARAM lParam = MAKELPARAM(clientPt.x, clientPt.y);
  WPARAM wParam = BuildMouseWParam(aHwid);

  if (leftDown) { Log("LBUTTONDOWN hwid=0x%X at (%d,%d)", aHwid, aScreenX, aScreenY); ::PostMessage(mOwnerHwnd, WM_LBUTTONDOWN, wParam, lParam); }
  if (leftUp) ::PostMessage(mOwnerHwnd, WM_LBUTTONUP, wParam, lParam);
  if (rightDown) ::PostMessage(mOwnerHwnd, WM_RBUTTONDOWN, wParam, lParam);
  if (rightUp) ::PostMessage(mOwnerHwnd, WM_RBUTTONUP, wParam, lParam);
  if (middleDown) ::PostMessage(mOwnerHwnd, WM_MBUTTONDOWN, wParam, lParam);
  if (middleUp) ::PostMessage(mOwnerHwnd, WM_MBUTTONUP, wParam, lParam);
}

void MouseMuxClient::HandlePointerWheel(uint32_t aHwid, int aScreenX, int aScreenY,
                                        int aDelta, bool aIsHorizontal) {
  uint32_t owner = mOwnerHwid.load();
  bool isOwner = (aHwid == owner);
  bool inWindow = IsPointInWindow(aScreenX, aScreenY);

  // If window has an owner, only process that owner's motion
  // If no owner, only process motion if cursor is in window (hover)
  if (owner != 0) {
    if (!isOwner) return;
  } else {
    if (!inWindow) return;
  }
  if (!mOwnerHwnd) return;

  POINT clientPt = ScreenToClient(aScreenX, aScreenY);
  LPARAM lParam = MAKELPARAM(clientPt.x, clientPt.y);
  WPARAM wParam = BuildMouseWParam(aHwid);
  wParam |= ((aDelta & 0xFFFF) << 16);

  UINT msg = aIsHorizontal ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL;
  ::PostMessage(mOwnerHwnd, msg, wParam, lParam);
}

void MouseMuxClient::HandleKeyboard(uint32_t aHwid, uint32_t aVkey, uint32_t aMessage,
                                    uint32_t aScanCode, uint32_t aFlags) {
  uint32_t owner = mOwnerHwid.load();
  if (owner == 0) return;

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

  if (mouseHwid != 0 && mouseHwid != owner) return;
  if (!mOwnerHwnd) return;

  LPARAM lParam = 1;
  lParam |= (aScanCode & 0xFF) << 16;
  if (aFlags & 0x01) lParam |= (1 << 24);

  if (aMessage == WM_KEYUP || aMessage == WM_SYSKEYUP) {
    lParam |= (1 << 30);
    lParam |= (1 << 31);
  }

  WPARAM markedVkey = aVkey | MOUSEMUX_MARKER;
  ::PostMessage(mOwnerHwnd, aMessage, markedVkey, lParam);
}

void MouseMuxClient::Log(const char* aFormat, ...) {
  char buf[512];
  va_list args;
  va_start(args, aFormat);
  vsnprintf(buf, sizeof(buf), aFormat, args);
  va_end(args);

  FILE* f = fopen("D:/scratch/firefox/mousemux_client.log", "a");
  if (f) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[v%s %02d:%02d:%02d.%03d HWND=%p] %s\n",
            MOUSEMUX_CLIENT_VERSION,
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            mOwnerHwnd, buf);
    fclose(f);
  }

  AppendLog(buf);
}

void MouseMuxClient::AppendLog(const char* text) {
  {
    std::lock_guard<std::mutex> lock(mLogMutex);
    mLogLines.push_back(text);
    while (mLogLines.size() > 100) {
      mLogLines.erase(mLogLines.begin());
    }
  }
  if (mDebugDialog && ::IsWindow(mDebugDialog)) {
    ::PostMessage(mDebugDialog, WM_MOUSEMUX_LOG, 0, 0);
  }
}

void MouseMuxClient::FlushLogToUI() {
  if (!mLogEdit || !::IsWindow(mLogEdit)) return;

  std::string fullText;
  {
    std::lock_guard<std::mutex> lock(mLogMutex);
    for (const auto& line : mLogLines) {
      fullText += line;
      fullText += "\r\n";
    }
  }
  ::SetWindowTextA(mLogEdit, fullText.c_str());
  int lineCount = (int)::SendMessage(mLogEdit, EM_GETLINECOUNT, 0, 0);
  ::SendMessage(mLogEdit, EM_LINESCROLL, 0, lineCount);
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
  if (mDebugDialog && ::IsWindow(mDebugDialog)) {
    ::ShowWindow(mDebugDialog, SW_HIDE);
  }
  mDebugDialogVisible = false;
}

void MouseMuxClient::CreateDebugDialog() {
  static std::once_flag classRegisterFlag;
  std::call_once(classRegisterFlag, []() {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DebugDialogProc;
    wc.hInstance = ::GetModuleHandle(nullptr);
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MouseMuxClientDebug";
    ::RegisterClassExW(&wc);
  });

  RECT ownerRect = {100, 100, 500, 450};
  if (mOwnerHwnd && ::IsWindow(mOwnerHwnd)) {
    ::GetWindowRect(mOwnerHwnd, &ownerRect);
  }

  wchar_t title[256];
  wchar_t winTitle[128] = L"(unknown)";
  if (mOwnerHwnd && ::IsWindow(mOwnerHwnd)) {
    ::GetWindowTextW(mOwnerHwnd, winTitle, 128);
  }
  swprintf(title, 256, L"MouseMux v%S - %s [%p]", MOUSEMUX_CLIENT_VERSION, winTitle, mOwnerHwnd);

  mDebugDialog = ::CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_APPWINDOW, L"MouseMuxClientDebug", title,
      WS_OVERLAPPEDWINDOW, ownerRect.right + 10, ownerRect.top, 800, 400,
      nullptr, nullptr, ::GetModuleHandle(nullptr), this);

  if (!mDebugDialog) {
    Log("Failed to create debug dialog: %d", GetLastError());
    return;
  }

  mStatusLabel = ::CreateWindowW(L"STATIC", L"Status: Disconnected",
                                 WS_CHILD | WS_VISIBLE, 10, 10, 780, 20,
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
      10, 75, 770, 280, mDebugDialog, (HMENU)ID_LOG, nullptr, nullptr);

  HFONT hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
  if (mStatusLabel) ::SendMessage(mStatusLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
  if (mConnectBtn) ::SendMessage(mConnectBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
  if (mBlockBtn) ::SendMessage(mBlockBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
  if (mLogEdit) ::SendMessage(mLogEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

  Log("Debug dialog created");
}

void MouseMuxClient::UpdateDebugStatus() {
  if (!mStatusLabel || !::IsWindow(mStatusLabel)) return;

  wchar_t buf[512];
  wchar_t winTitle[128] = L"";
  if (mOwnerHwnd && ::IsWindow(mOwnerHwnd)) {
    ::GetWindowTextW(mOwnerHwnd, winTitle, 128);
  }
  uint32_t owner = mOwnerHwid.load();
  bool connected = mConnected.load();
  bool blocked = InputFilter::IsEnabledForWindow(mOwnerHwnd);

  swprintf(buf, 512, L"%s [%p] | %s | %s | Owner: %s",
           winTitle, mOwnerHwnd,
           connected ? L"Connected" : L"Disconnected",
           blocked ? L"BLOCKED" : L"Normal",
           owner ? std::to_wstring(owner).c_str() : L"None");
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
    return self->HandleDebugMessage(hwnd, msg, wParam, lParam);
  }
  return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT MouseMuxClient::HandleDebugMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_MOUSEMUX_UPDATE:
      UpdateDebugStatus();
      return 0;

    case WM_MOUSEMUX_LOG:
      FlushLogToUI();
      return 0;

    case WM_COMMAND:
      if (LOWORD(wParam) == ID_CONNECT) {
        if (mConnected.load()) {
          Disconnect();
        } else {
          Connect();
        }
        return 0;
      }
      if (LOWORD(wParam) == ID_BLOCK) {
        if (InputFilter::IsEnabledForWindow(mOwnerHwnd)) {
          InputFilter::DisableForWindow(mOwnerHwnd);
          Log("Input filter DISABLED");
        } else {
          InputFilter::EnableForWindow(mOwnerHwnd);
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
      mConnectBtn = nullptr;
      mBlockBtn = nullptr;
      mLogEdit = nullptr;
      return 0;
  }
  return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

}  // namespace widget
}  // namespace mozilla
