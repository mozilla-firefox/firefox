# MouseMux Architecture (v5.0)

## Overview
MouseMux provides multi-mouse/keyboard support for Firefox on Windows. It connects
to a MouseMux server (ws://localhost:41001) and receives input events for multiple
input devices, injecting them into Firefox windows via PostMessage.

## Key Principle
**NEVER use Windows input APIs**: No SendInput, GetKeyState, GetCursorPos, etc.
All input comes from MouseMux server and is injected via PostMessage to target HWNDs.

## Components

### 1. InputFilter (InputFilter.h/cpp)
Simple global flag to block native input.
- `InputFilter::Enable()` - Block native mouse/keyboard to Firefox windows
- `InputFilter::Disable()` - Allow native input
- `InputFilter::IsEnabled()` - Check if blocking is active

Used in nsWindow::ProcessMessage to skip native input when enabled.

### 2. MouseMuxClient (MouseMuxClient.h/cpp) - **PRIMARY**
Per-window client that handles MouseMux connection and input injection.
Each nsWindow creates one MouseMuxClient instance.

Key features:
- WebSocket connection to MouseMux server
- Receives pointer.motion, pointer.button, keyboard.key events
- Filters events to only process those within window bounds
- Tracks ownership (which hwid clicked on this window)
- Built-in debug dialog (F11 to toggle)
- Version: defined as MOUSEMUX_CLIENT_VERSION

Methods:
- Connect/Disconnect to server
- ShowDebugDialog/HideDebugDialog
- Log() for debug output

### 3. MouseMuxService (MouseMuxService.h/cpp) - **UNUSED/LEGACY**
Singleton service - appears to be a separate implementation not currently used.
MouseMuxDebugDialog uses this, but the actual nsWindow uses MouseMuxClient.

### 4. MouseMuxDebugDialog (MouseMuxDebugDialog.h/cpp) - **LEGACY**
Singleton debug dialog that uses MouseMuxService.
Not used by current implementation (nsWindow uses MouseMuxClient's built-in dialog).

## nsWindow Integration

### Initialization (nsWindow::Create)
```cpp
InitMouseMux();  // Creates MouseMuxClient for the window
```

### Input Blocking (nsWindow::ProcessMessage)
At the start of ProcessMessage, checks InputFilter:
```cpp
if (InputFilter::IsEnabled()) {
  switch (msg) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    // ... etc
    if (!(wParam & MOUSEMUX_MARKER)) {
      return true;  // Block native input
    }
    wParam &= ~MOUSEMUX_MARKER;  // Strip marker from MouseMux events
  }
}
```

### Keyboard Forwarding (nsWindow::ProcessMessage WM_KEYDOWN/WM_KEYUP)
When connected to MouseMux, keyboard events arriving at top-level window
are forwarded to the focused child window:
```cpp
if (mMouseMuxClient && mMouseMuxClient->IsConnected()) {
  nsWindow* focusedWnd = IMEHandler::GetFocusedWindow();
  if (focusedWnd && focusedWnd != this) {
    ::PostMessage(focusedWnd->mWnd, msg, wParam, lParam);
  }
}
```

### Hotkeys
- F11: Toggle debug dialog
- F12: Emergency exit (disable blocking, disconnect)

## Message Injection

MouseMux events are injected via PostMessage with MOUSEMUX_MARKER:
```cpp
::PostMessage(hwnd, WM_MOUSEMOVE, wParam | MOUSEMUX_MARKER, MAKELPARAM(x, y));
```

The marker (0x80000000 in wParam high bit) identifies MouseMux-injected messages
so they pass through InputFilter when blocking is enabled.

## Current Issues

### Keyboard Not Working
- Keyboard events from MouseMux are received and logged
- PostMessage is called to inject WM_KEYDOWN/WM_KEYUP
- But keys don't appear in content areas
- Need investigation (focus handling, lParam construction, etc.)

## File List
- InputFilter.h/cpp - Simple blocking flag
- MouseMuxClient.h/cpp - Per-window client (USED)
- MouseMuxService.h/cpp - Singleton service (UNUSED)
- MouseMuxDebugDialog.h/cpp - Singleton dialog (UNUSED)
- nsWindow.cpp - Integration point

## Version History
- v4.3: Per-window MouseMuxClient
- v4.5: Fix keyboard handling, add MouseMux rules
- v4.6: Reduce logging (skip motion events)
- v4.7: Forward keyboard to focused window
- v5.0: Restore independent Block Input toggle
