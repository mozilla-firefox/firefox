/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_WinMessages_h_
#define mozilla_widget_WinMessages_h_

/*****************************************************************************
 * MOZ_WM_* messages
 ****************************************************************************/

// From Firefox 5 (2011) to Firefox 137 (2025), these were internal messages for
// WM_MOUSEWHEEL, WM_MOUSEHWHEEL, WM_VSCROLL, and WM_HSCROLL, used to work
// around issues with out-of-process NPAPI plugins.
//
// (There may yet be old third-party apps that unwisely send these messages
// directly to our windows to cause scrolling; we should probably avoid reusing
// them for a while, so as not to have to worry about that.)
#define MOZ_WM_MOUSE_RESERVED_UNUSED_0 (WM_APP + 0x0310)
#define MOZ_WM_MOUSE_RESERVED_UNUSED_1 (WM_APP + 0x0311)
#define MOZ_WM_MOUSE_RESERVED_UNUSED_2 (WM_APP + 0x0312)
#define MOZ_WM_MOUSE_RESERVED_UNUSED_3 (WM_APP + 0x0313)

// If TSFTextStore needs to notify TSF/TIP of layout change later, this
// message is posted.
#define MOZ_WM_NOTIFY_TSF_OF_LAYOUT_CHANGE (WM_APP + 0x0315)
// Internal message used in correcting backwards clock skew
#define MOZ_WM_SKEWFIX (WM_APP + 0x0316)
// Internal message used for rolling up popups for dmanip events
#define MOZ_WM_DMANIP (WM_APP + 0x0317)

// Internal message used to work around race condition in explorer.exe's
// fullscreen window-state update handler in Windows 10+. (See bug 1835851.)
#define MOZ_WM_FULLSCREEN_STATE_UPDATE (WM_APP + 0x0318)

// Internal message used for handling X1/X2 mouse button presses.
#define MOZ_WM_AFTER_XBUTTONUP (WM_APP + 0x0319)

// XXX Should rename them to MOZ_WM_* and use safer values!
// Messages for fullscreen transition window
#define WM_FULLSCREEN_TRANSITION_BEFORE (WM_USER + 0)
#define WM_FULLSCREEN_TRANSITION_AFTER (WM_USER + 1)

#ifndef APPCOMMAND_BROWSER_BACKWARD
#  define APPCOMMAND_BROWSER_BACKWARD 1
#  define APPCOMMAND_BROWSER_FORWARD 2
#  define APPCOMMAND_BROWSER_REFRESH 3
#  define APPCOMMAND_BROWSER_STOP 4
#  define APPCOMMAND_BROWSER_SEARCH 5
#  define APPCOMMAND_BROWSER_FAVORITES 6
#  define APPCOMMAND_BROWSER_HOME 7

#  define APPCOMMAND_MEDIA_NEXTTRACK 11
#  define APPCOMMAND_MEDIA_PREVIOUSTRACK 12
#  define APPCOMMAND_MEDIA_STOP 13
#  define APPCOMMAND_MEDIA_PLAY_PAUSE 14

/*
 * Additional commands currently not in use.
 *
 *#define APPCOMMAND_VOLUME_MUTE            8
 *#define APPCOMMAND_VOLUME_DOWN            9
 *#define APPCOMMAND_VOLUME_UP              10
 *#define APPCOMMAND_LAUNCH_MAIL            15
 *#define APPCOMMAND_LAUNCH_MEDIA_SELECT    16
 *#define APPCOMMAND_LAUNCH_APP1            17
 *#define APPCOMMAND_LAUNCH_APP2            18
 *#define APPCOMMAND_BASS_DOWN              19
 *#define APPCOMMAND_BASS_BOOST             20
 *#define APPCOMMAND_BASS_UP                21
 *#define APPCOMMAND_TREBLE_DOWN            22
 *#define APPCOMMAND_TREBLE_UP              23
 *#define FAPPCOMMAND_MOUSE                 0x8000
 *#define FAPPCOMMAND_KEY                   0
 *#define FAPPCOMMAND_OEM                   0x1000
 */

#  define GET_APPCOMMAND_LPARAM(lParam) \
    ((short)(HIWORD(lParam) & ~FAPPCOMMAND_MASK))

/*
 *#define GET_DEVICE_LPARAM(lParam)         ((WORD)(HIWORD(lParam) &
 *FAPPCOMMAND_MASK)) #define GET_MOUSEORKEY_LPARAM             GET_DEVICE_LPARAM
 *#define GET_FLAGS_LPARAM(lParam)          (LOWORD(lParam))
 *#define GET_KEYSTATE_LPARAM(lParam)       GET_FLAGS_LPARAM(lParam)
 */
#endif  // #ifndef APPCOMMAND_BROWSER_BACKWARD

#endif  // #ifndef mozilla_widget_WinMessages_h_
