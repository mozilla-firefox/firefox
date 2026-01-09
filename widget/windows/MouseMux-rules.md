# MouseMux Integration Rules

## CRITICAL: Input Handling Rules

### PROHIBITED Windows APIs
The following Windows input APIs are **STRICTLY PROHIBITED**:
- `SendInput()` - Sends input to the system, affects all applications
- `keybd_event()` - Legacy keyboard input, affects all applications
- `mouse_event()` - Legacy mouse input, affects all applications
- `GetKeyState()` - Gets global keyboard state
- `GetAsyncKeyState()` - Gets global async keyboard state
- `SetCursorPos()` - Moves the global cursor
- Any API that generates or queries **system-wide** input

### ALLOWED Input Methods
Input from MouseMux MUST be delivered using **window-targeted** methods:
- `PostMessage()` - Posts messages directly to a specific window
- `SendMessage()` - Sends messages directly to a specific window
- Direct window message injection via HWND

### Rationale
MouseMux is designed for multi-user scenarios where each user has their own
mouse/keyboard. Using system-wide input APIs would:
1. Affect ALL applications, not just the target window
2. Interfere with other users' input
3. Defeat the purpose of per-window input isolation

### Implementation Notes
- All mouse events (WM_MOUSEMOVE, WM_LBUTTONDOWN, etc.) → PostMessage to target HWND
- All keyboard events (WM_KEYDOWN, WM_KEYUP, etc.) → PostMessage to target HWND
- Use MOUSEMUX_MARKER in message parameters to identify injected input
- Never block or intercept input from other MouseMux devices

## Version History
- v4.5: Added these rules after incorrectly attempting to use SendInput
