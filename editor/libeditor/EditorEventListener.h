/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef EditorEventListener_h
#define EditorEventListener_h

#include "EditorForwards.h"

#include "mozilla/Attributes.h"
#include "mozilla/EventForwards.h"
#include "nsCOMPtr.h"
#include "nsError.h"
#include "nsIDOMEventListener.h"
#include "nsISupportsImpl.h"
#include "nscore.h"

class nsCaret;
class nsIContent;
class nsPresContext;

// X.h defines KeyPress
#ifdef KeyPress
#  undef KeyPress
#endif

#ifdef XP_WIN
// On Windows, we support switching the text direction by pressing Ctrl+Shift
#  define HANDLE_NATIVE_TEXT_DIRECTION_SWITCH
#endif

namespace mozilla {
class PresShell;
namespace dom {
class DragEvent;
class MouseEvent;
}  // namespace dom

class EditorEventListener : public nsIDOMEventListener {
 public:
  EditorEventListener();

  virtual nsresult Connect(EditorBase* aEditorBase);

  virtual void Disconnect();

  /**
   * DetachedFromEditor() returns true if editor was detached.
   * Otherwise, false.
   */
  [[nodiscard]] bool DetachedFromEditor() const;

  NS_DECL_ISUPPORTS

  // nsIDOMEventListener
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD HandleEvent(dom::Event* aEvent) override;

  /**
   * Called before starting to handle eMouseDown or eMouseUp in PresShell.
   *
   * @return true if IME consumed aMouseEvent.
   */
  MOZ_CAN_RUN_SCRIPT bool WillHandleMouseButtonEvent(
      WidgetMouseEvent& aMouseEvent);

 protected:
  virtual ~EditorEventListener();

  nsresult InstallToEditor();
  void UninstallFromEditor();

#ifdef HANDLE_NATIVE_TEXT_DIRECTION_SWITCH
  nsresult KeyDown(const WidgetKeyboardEvent* aKeyboardEvent);
  MOZ_CAN_RUN_SCRIPT nsresult KeyUp(const WidgetKeyboardEvent* aKeyboardEvent);
#endif
  MOZ_CAN_RUN_SCRIPT nsresult KeyPress(WidgetKeyboardEvent* aKeyboardEvent);
  MOZ_CAN_RUN_SCRIPT nsresult
  HandleChangeComposition(WidgetCompositionEvent* aCompositionEvent);
  nsresult HandleStartComposition(WidgetCompositionEvent* aCompositionEvent);
  MOZ_CAN_RUN_SCRIPT void HandleEndComposition(
      WidgetCompositionEvent* aCompositionEvent);
  MOZ_CAN_RUN_SCRIPT virtual nsresult MouseDown(dom::MouseEvent* aMouseEvent);
  MOZ_CAN_RUN_SCRIPT virtual nsresult MouseUp(dom::MouseEvent* aMouseEvent) {
    return NS_OK;
  }
  MOZ_CAN_RUN_SCRIPT virtual nsresult PointerClick(
      WidgetMouseEvent* aPointerClick);
  MOZ_CAN_RUN_SCRIPT nsresult Focus(const InternalFocusEvent& aFocusEvent);
  MOZ_CAN_RUN_SCRIPT nsresult Blur(const InternalFocusEvent& aBlurEvent);
  MOZ_CAN_RUN_SCRIPT nsresult DragOverOrDrop(dom::DragEvent* aDragEvent);
  nsresult DragLeave(dom::DragEvent* aDragEvent);

  void RefuseToDropAndHideCaret(dom::DragEvent* aDragEvent);
  bool DragEventHasSupportingData(dom::DragEvent* aDragEvent) const;
  MOZ_CAN_RUN_SCRIPT bool CanInsertAtDropPosition(dom::DragEvent* aDragEvent);
  void InitializeDragDropCaret();
  void CleanupDragDropCaret();
  PresShell* GetPresShell() const;
  nsPresContext* GetPresContext() const;

  /**
   * @return true if IME consumed aMouseEvent.
   */
  MOZ_CAN_RUN_SCRIPT bool NotifyIMEOfMouseButtonEvent(
      WidgetMouseEvent& aMouseEvent);

  bool EditorHasFocus();
  bool IsFileControlTextBox();
  bool ShouldHandleNativeKeyBindings(WidgetKeyboardEvent* aKeyboardEvent);

  /**
   * DetachedFromEditorOrDefaultPrevented() returns true if editor was detached
   * and/or the event was consumed.  Otherwise, i.e., attached editor can
   * handle the event, returns true.
   */
  bool DetachedFromEditorOrDefaultPrevented(WidgetEvent* aEvent) const;

  /**
   * EnsureCommitComposition() requests to commit composition if there is.
   * Returns false if the editor is detached from the listener, i.e.,
   * impossible to continue to handle the event.  Otherwise, true.
   */
  [[nodiscard]] bool EnsureCommitComposition();

  EditorBase* mEditorBase;  // weak
  RefPtr<nsCaret> mCaret;
  bool mCommitText;
  bool mInTransaction;
  bool mMouseDownOrUpConsumedByIME;
#ifdef HANDLE_NATIVE_TEXT_DIRECTION_SWITCH
  bool mHaveBidiKeyboards;
  bool mShouldSwitchTextDirection;
  bool mSwitchToRTL;
#endif
};

}  // namespace mozilla

#endif  // #ifndef EditorEventListener_h
