/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_InitData_h_
#define mozilla_widget_InitData_h_

#include <cstdint>

#include "X11UndefineNone.h"
#include "mozilla/TypedEnumBits.h"

namespace mozilla::widget {

// Window types
enum class WindowType : uint8_t {
  TopLevel,   // default top level window
  Dialog,     // top level window but usually handled differently by the OS
  Popup,      // used for combo boxes, etc
  Invisible,  // a special hidden window (not to be created by arbitrary code)
};

// Popup types for WindowType::Popup
enum class PopupType : uint8_t {
  Panel,
  Menu,
  Tooltip,
  Any,  // used only to pass to nsXULPopupManager::GetTopPopup
};

// Popup levels specify the window ordering behaviour.
enum class PopupLevel : uint8_t {
  // The popup appears just above its parent and maintains its position
  // relative to the parent.
  Parent,
  // The popup appears on top of other windows, including those of other
  // applications.
  Top,
};

// Border styles
enum class BorderStyle : int16_t {
  None = 0,           // no border, titlebar, etc.. opposite of all
  All = 1 << 0,       // all window decorations
  Border = 1 << 1,    // enables the border on the window. these
                      // are only for decoration and are not
                      // resize handles
  ResizeH = 1 << 2,   // enables the resize handles for the
                      // window. if this is set, border is
                      // implied to also be set
  Title = 1 << 3,     // enables the titlebar for the window
  Menu = 1 << 4,      // enables the window menu button on the
                      // title bar. this being on should force
                      // the title bar to display
  Minimize = 1 << 5,  // enables the minimize button so the user
                      // can minimize the window. turned off for
                      // tranient windows since they can not be
                      // minimized separate from their parent
  Maximize = 1 << 6,  // enables the maxmize button so the user
                      // can maximize the window
  Default = -1        // whatever the OS wants... i.e. don't do anything
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(BorderStyle)

enum class TransparencyMode : uint8_t {
  Opaque = 0,   // Fully opaque
  Transparent,  // Parts of the window may be transparent
  // If you add to the end here, you must update the serialization code in
  // WidgetMessageUtils.h
};

// There are different types of Picture-in-Picture windows on the web
enum class PiPType : uint8_t {
  NoPiP,
  // https://w3c.github.io/picture-in-picture
  MediaPiP,
  // https://wicg.github.io/document-picture-in-picture
  DocumentPiP
};

// Basic struct for widget initialization data.
// @see Create member function of nsIWidget
struct InitData {
  WindowType mWindowType = WindowType::TopLevel;
  BorderStyle mBorderStyle = BorderStyle::Default;
  PopupType mPopupHint = PopupType::Panel;
  PopupLevel mPopupLevel = PopupLevel::Top;
  TransparencyMode mTransparencyMode = TransparencyMode::Opaque;
  // when painting exclude area occupied by child windows and sibling windows
  bool mClipChildren = false;
  bool mClipSiblings = false;
  bool mRTL = false;
  bool mIsDragPopup = false;  // true for drag feedback panels
  // true if window creation animation is suppressed, e.g. for session restore
  bool mIsAnimationSuppressed = false;
  // true if the window should not auto-enter native fullscreen on its initial
  // show, e.g. a window created by detaching a tab from a fullscreen window
  bool mIsInitialFullscreenSuppressed = false;
  // true if the window should support an alpha channel, if available.
  bool mHasRemoteContent = false;
  bool mAlwaysOnTop = false;
  PiPType mPiPType = PiPType::NoPiP;
  // True if the window is user-resizable.
  bool mResizable = false;
  bool mIsPrivate = false;
  // True if the window is an alert / notification.
  bool mIsAlert = false;
};

}  // namespace mozilla::widget

#endif  // mozilla_widget_InitData
