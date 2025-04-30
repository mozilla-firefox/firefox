/* -*- Mode: c++; c-basic-offset: 2; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SessionAccessibility.h"
#include "LocalAccessible-inl.h"
#include "AndroidUiThread.h"
#include "AndroidBridge.h"
#include "DocAccessibleParent.h"
#include "IDSet.h"
#include "nsThreadUtils.h"
#include "AccAttributes.h"
#include "AccessibilityEvent.h"
#include "DocAccessibleWrap.h"
#include "JavaBuiltins.h"
#include "nsAccessibilityService.h"
#include "nsAccUtils.h"
#include "nsViewManager.h"

#include "mozilla/PresShell.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/a11y/Accessible.h"
#include "mozilla/a11y/DocAccessibleParent.h"
#include "mozilla/a11y/DocManager.h"
#include "mozilla/a11y/HyperTextAccessibleBase.h"
#include "mozilla/jni/GeckoBundleUtils.h"
#include "mozilla/jni/NativesInlines.h"
#include "mozilla/widget/GeckoViewSupport.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/dom/MouseEventBinding.h"

#ifdef DEBUG
#  include <android/log.h>
#  define AALOG(args...) \
    __android_log_print(ANDROID_LOG_INFO, "GeckoAccessibilityNative", ##args)
#else
#  define AALOG(args...) \
    do {                 \
    } while (0)
#endif

using namespace mozilla::a11y;

// IDs should be a positive 32bit integer.
IDSet sIDSet(31UL);

class Settings final
    : public mozilla::java::SessionAccessibility::Settings::Natives<Settings> {
 public:
  static void ToggleNativeAccessibility(bool aEnable) {
    if (aEnable) {
      GetOrCreateAccService();
    } else {
      MaybeShutdownAccService(nsAccessibilityService::ePlatformAPI);
    }
  }
};

SessionAccessibility::SessionAccessibility(
    jni::NativeWeakPtr<widget::GeckoViewSupport> aWindow,
    java::SessionAccessibility::NativeProvider::Param aSessionAccessibility)
    : mWindow(aWindow), mSessionAccessibility(aSessionAccessibility) {}

void SessionAccessibility::SetAttached(bool aAttached,
                                       already_AddRefed<Runnable> aRunnable) {
  if (RefPtr<nsThread> uiThread = GetAndroidUiThread()) {
    uiThread->Dispatch(NS_NewRunnableFunction(
        "SessionAccessibility::Attach",
        [aAttached,
         sa = java::SessionAccessibility::NativeProvider::GlobalRef(
             mSessionAccessibility),
         runnable = RefPtr<Runnable>(aRunnable)] {
          sa->SetAttached(aAttached);
          if (runnable) {
            runnable->Run();
          }
        }));
  }
}

void SessionAccessibility::Init() {
  java::SessionAccessibility::NativeProvider::Natives<
      SessionAccessibility>::Init();
  Settings::Init();
}

void SessionAccessibility::GetNodeInfo(int32_t aID,
                                       mozilla::jni::Object::Param aNodeInfo) {
  MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
  ReleasableMonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
  java::GeckoBundle::GlobalRef ret = nullptr;
  RefPtr<SessionAccessibility> self(this);
  if (Accessible* acc = GetAccessibleByID(aID)) {
    if (acc->IsLocal()) {
      mal.Unlock();
      nsAppShell::SyncRunEvent(
          [this, self, aID, aNodeInfo = jni::Object::GlobalRef(aNodeInfo)] {
            if (Accessible* acc = GetAccessibleByID(aID)) {
              PopulateNodeInfo(acc, aNodeInfo);
            } else {
              AALOG("oops, nothing for %d", aID);
            }
          });
    } else {
      PopulateNodeInfo(acc, aNodeInfo);
    }
  } else {
    AALOG("oops, nothing for %d", aID);
  }
}

int SessionAccessibility::GetNodeClassName(int32_t aID) {
  MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
  ReleasableMonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
  int32_t classNameEnum = java::SessionAccessibility::CLASSNAME_VIEW;
  RefPtr<SessionAccessibility> self(this);
  if (Accessible* acc = GetAccessibleByID(aID)) {
    if (acc->IsLocal()) {
      mal.Unlock();
      nsAppShell::SyncRunEvent([this, self, aID, &classNameEnum] {
        if (Accessible* acc = GetAccessibleByID(aID)) {
          classNameEnum = AccessibleWrap::AndroidClass(acc);
        }
      });
    } else {
      classNameEnum = AccessibleWrap::AndroidClass(acc);
    }
  }

  return classNameEnum;
}

void SessionAccessibility::SetText(int32_t aID, jni::String::Param aText) {
  if (Accessible* acc = GetAccessibleByID(aID)) {
    if (acc->IsRemote()) {
      acc->AsRemote()->ReplaceText(PromiseFlatString(aText->ToString()));
    } else if (acc->AsLocal()->IsHyperText()) {
      acc->AsLocal()->AsHyperText()->ReplaceText(aText->ToString());
    }
  }
}

void SessionAccessibility::Click(int32_t aID) {
  MOZ_ASSERT(NS_IsMainThread());
  MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
  if (Accessible* acc = GetAccessibleByID(aID)) {
    acc->DoAction(0);
  }
}

bool SessionAccessibility::Pivot(int32_t aID, int32_t aGranularity,
                                 bool aForward, bool aInclusive) {
  MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
  MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
  RefPtr<SessionAccessibility> self(this);
  if (Accessible* acc = GetAccessibleByID(aID)) {
    if (acc->IsLocal()) {
      nsAppShell::PostEvent(
          [this, self, aID, aGranularity, aForward, aInclusive] {
            MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
            if (Accessible* _acc = GetAccessibleByID(aID)) {
              MOZ_ASSERT(_acc->IsLocal());
              if (Accessible* result = AccessibleWrap::DoPivot(
                      _acc, aGranularity, aForward, aInclusive)) {
                SendAccessibilityFocusedEvent(result, true);
              }
            }
          });
      return true;
    }
    Accessible* result =
        AccessibleWrap::DoPivot(acc, aGranularity, aForward, aInclusive);
    if (result) {
      int32_t virtualViewID = AccessibleWrap::GetVirtualViewID(result);
      nsAppShell::PostEvent([this, self, virtualViewID] {
        MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
        if (Accessible* acc = GetAccessibleByID(virtualViewID)) {
          SendAccessibilityFocusedEvent(acc, true);
        }
      });
      return true;
    }
  }

  return false;
}

void SessionAccessibility::ExploreByTouch(int32_t aID, float aX, float aY) {
  MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
  MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
  RefPtr<SessionAccessibility> self(this);
  if (Accessible* origin = GetAccessibleByID(aID)) {
    if (origin->IsLocal()) {
      nsAppShell::PostEvent([this, self, aID, aX, aY] {
        MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
        if (Accessible* origin = GetAccessibleByID(aID)) {
          if (Accessible* result =
                  AccessibleWrap::ExploreByTouch(origin, aX, aY)) {
            SendHoverEnterEvent(result);
          }
        }
      });
    } else {
      if (Accessible* result = AccessibleWrap::ExploreByTouch(origin, aX, aY)) {
        int32_t resultID = AccessibleWrap::GetVirtualViewID(result);
        nsAppShell::PostEvent([this, self, resultID] {
          MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
          if (Accessible* result = GetAccessibleByID(resultID)) {
            SendHoverEnterEvent(result);
          }
        });
      }
    }
  }
}

static void GetSelectionOrCaret(HyperTextAccessibleBase* aHyperTextAcc,
                                int32_t* aStartOffset, int32_t* aEndOffset) {
  if (!aHyperTextAcc->SelectionBoundsAt(0, aStartOffset, aEndOffset)) {
    *aStartOffset = *aEndOffset = aHyperTextAcc->CaretOffset();
  }
}

static void AdjustCaretToTextNavigation(Accessible* aAccessible,
                                        int32_t aStartOffset,
                                        int32_t aEndOffset, bool aForward,
                                        bool aSelect) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!(aAccessible->State() & states::EDITABLE)) {
    return;
  }

  HyperTextAccessibleBase* editable = aAccessible->AsHyperTextBase();
  MOZ_ASSERT(editable);
  if (!editable) {
    return;
  }

  int32_t newOffset = aForward ? aEndOffset : aStartOffset;
  if (aSelect) {
    int32_t anchor = editable->CaretOffset();
    if (editable->SelectionCount()) {
      int32_t startSel, endSel;
      GetSelectionOrCaret(editable, &startSel, &endSel);
      anchor = startSel == anchor ? endSel : startSel;
    }
    editable->SetSelectionBoundsAt(0, anchor, newOffset);
  } else {
    editable->SetCaretOffset(newOffset);
  }
}

bool SessionAccessibility::NavigateText(int32_t aID, int32_t aGranularity,
                                        int32_t aStartOffset,
                                        int32_t aEndOffset, bool aForward,
                                        bool aSelect) {
  MOZ_ASSERT(AndroidBridge::IsJavaUiThread());
  MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
  RefPtr<SessionAccessibility> self(this);
  if (Accessible* acc = GetAccessibleByID(aID)) {
    if (acc->IsLocal()) {
      nsAppShell::PostEvent([this, self, aID, aGranularity, aStartOffset,
                             aEndOffset, aForward, aSelect] {
        MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
        if (Accessible* _acc = GetAccessibleByID(aID)) {
          auto result = AccessibleWrap::NavigateText(
              _acc, aGranularity, aStartOffset, aEndOffset, aForward, aSelect);

          if (result) {
            SendTextTraversedEvent(_acc, result->first, result->second);
            AdjustCaretToTextNavigation(_acc, result->first, result->second,
                                        aForward, aSelect);
          }
        }
      });
      return true;
    } else {
      auto result = AccessibleWrap::NavigateText(
          acc, aGranularity, aStartOffset, aEndOffset, aForward, aSelect);
      if (result) {
        nsAppShell::PostEvent([this, self, aID, result, aForward, aSelect] {
          MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
          if (Accessible* _acc = GetAccessibleByID(aID)) {
            SendTextTraversedEvent(_acc, result->first, result->second);
            AdjustCaretToTextNavigation(_acc, result->first, result->second,
                                        aForward, aSelect);
          }
        });
      }

      return !!result;
    }
  }

  return false;
}

void SessionAccessibility::SetSelection(int32_t aID, int32_t aStart,
                                        int32_t aEnd) {
  if (Accessible* acc = GetAccessibleByID(aID)) {
    if (auto* textAcc = acc->AsHyperTextBase()) {
      if (aStart == aEnd) {
        textAcc->SetCaretOffset(aStart);
      } else {
        textAcc->SetSelectionBoundsAt(0, aStart, aEnd);
      }
    }
  }
}

void SessionAccessibility::Cut(int32_t aID) {
  if (Accessible* acc = GetAccessibleByID(aID)) {
    if (auto* textAcc = acc->AsHyperTextBase()) {
      int32_t startSel, endSel;
      if (textAcc->SelectionBoundsAt(0, &startSel, &endSel)) {
        textAcc->CutText(startSel, endSel);
      }
    }
  }
}

void SessionAccessibility::Copy(int32_t aID) {
  if (Accessible* acc = GetAccessibleByID(aID)) {
    if (auto* textAcc = acc->AsHyperTextBase()) {
      int32_t startSel, endSel;
      GetSelectionOrCaret(textAcc, &startSel, &endSel);
      textAcc->CopyText(startSel, endSel);
    }
  }
}

void SessionAccessibility::Paste(int32_t aID) {
  if (Accessible* acc = GetAccessibleByID(aID)) {
    if (auto* textAcc = acc->AsHyperTextBase()) {
      textAcc->PasteText(nsIAccessibleText::TEXT_OFFSET_CARET);
    }
  }
}

RefPtr<SessionAccessibility> SessionAccessibility::GetInstanceFor(
    Accessible* aAccessible) {
  MOZ_ASSERT(NS_IsMainThread());
  if (LocalAccessible* localAcc = aAccessible->AsLocal()) {
    DocAccessible* docAcc = localAcc->Document();
    // If the accessible is being shutdown from the doc's shutdown
    // the doc accessible won't have a ref to a presshell anymore,
    // but we should have a ref to the DOM document node, and the DOM doc
    // has a ref to the presshell.
    dom::Document* doc = docAcc ? docAcc->DocumentNode() : nullptr;
    if (doc && doc->IsContentDocument()) {
      // Only content accessibles should have an associated SessionAccessible.
      return GetInstanceFor(doc->GetPresShell());
    }
  } else {
    dom::CanonicalBrowsingContext* cbc =
        static_cast<dom::BrowserParent*>(
            aAccessible->AsRemote()->Document()->Manager())
            ->GetBrowsingContext()
            ->Top();
    dom::BrowserParent* bp = cbc->GetBrowserParent();
    if (!bp) {
      bp = static_cast<dom::BrowserParent*>(
          aAccessible->AsRemote()->Document()->Manager());
    }
    if (auto element = bp->GetOwnerElement()) {
      if (auto doc = element->OwnerDoc()) {
        if (nsPresContext* presContext = doc->GetPresContext()) {
          return GetInstanceFor(presContext->PresShell());
        }
      } else {
        MOZ_ASSERT_UNREACHABLE(
            "Browser parent's element does not have owner doc.");
      }
    }
  }

  return nullptr;
}

RefPtr<SessionAccessibility> SessionAccessibility::GetInstanceFor(
    PresShell* aPresShell) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!aPresShell) {
    return nullptr;
  }

  nsViewManager* vm = aPresShell->GetViewManager();
  if (!vm) {
    return nullptr;
  }

  nsCOMPtr<nsIWidget> rootWidget = vm->GetRootWidget();
  // `rootWidget` can be one of several types. Here we make sure it is an
  // android nsWindow.
  if (RefPtr<nsWindow> window = nsWindow::From(rootWidget)) {
    return window->GetSessionAccessibility();
  }

  return nullptr;
}

void SessionAccessibility::SendAccessibilityFocusedEvent(
    Accessible* aAccessible, bool aScrollIntoView) {
  MOZ_ASSERT(NS_IsMainThread());
  mSessionAccessibility->SendEvent(
      java::sdk::AccessibilityEvent::TYPE_VIEW_ACCESSIBILITY_FOCUSED,
      AccessibleWrap::GetVirtualViewID(aAccessible),
      AccessibleWrap::AndroidClass(aAccessible), nullptr);
  if (aScrollIntoView) {
    aAccessible->ScrollTo(nsIAccessibleScrollType::SCROLL_TYPE_ANYWHERE);
  }
}

void SessionAccessibility::SendHoverEnterEvent(Accessible* aAccessible) {
  MOZ_ASSERT(NS_IsMainThread());
  mSessionAccessibility->SendEvent(
      java::sdk::AccessibilityEvent::TYPE_VIEW_HOVER_ENTER,
      AccessibleWrap::GetVirtualViewID(aAccessible),
      AccessibleWrap::AndroidClass(aAccessible), nullptr);
}

void SessionAccessibility::SendFocusEvent(Accessible* aAccessible) {
  MOZ_ASSERT(NS_IsMainThread());
  // Suppress focus events from about:blank pages.
  // This is important for tests.
  if (aAccessible->IsDoc() && aAccessible->ChildCount() == 0) {
    return;
  }

  mSessionAccessibility->SendEvent(
      java::sdk::AccessibilityEvent::TYPE_VIEW_FOCUSED,
      AccessibleWrap::GetVirtualViewID(aAccessible),
      AccessibleWrap::AndroidClass(aAccessible), nullptr);
}

void SessionAccessibility::SendScrollingEvent(Accessible* aAccessible,
                                              int32_t aScrollX,
                                              int32_t aScrollY,
                                              int32_t aMaxScrollX,
                                              int32_t aMaxScrollY) {
  MOZ_ASSERT(NS_IsMainThread());
  int32_t virtualViewId = AccessibleWrap::GetVirtualViewID(aAccessible);

  if (virtualViewId != kNoID) {
    // XXX: Support scrolling in subframes
    return;
  }

  GECKOBUNDLE_START(eventInfo);
  GECKOBUNDLE_PUT(eventInfo, "scrollX", java::sdk::Integer::ValueOf(aScrollX));
  GECKOBUNDLE_PUT(eventInfo, "scrollY", java::sdk::Integer::ValueOf(aScrollY));
  GECKOBUNDLE_PUT(eventInfo, "maxScrollX",
                  java::sdk::Integer::ValueOf(aMaxScrollX));
  GECKOBUNDLE_PUT(eventInfo, "maxScrollY",
                  java::sdk::Integer::ValueOf(aMaxScrollY));
  GECKOBUNDLE_FINISH(eventInfo);

  mSessionAccessibility->SendEvent(
      java::sdk::AccessibilityEvent::TYPE_VIEW_SCROLLED, virtualViewId,
      AccessibleWrap::AndroidClass(aAccessible), eventInfo);
  SendWindowContentChangedEvent();
}

void SessionAccessibility::SendWindowContentChangedEvent() {
  mSessionAccessibility->SendEvent(
      java::sdk::AccessibilityEvent::TYPE_WINDOW_CONTENT_CHANGED, kNoID,
      java::SessionAccessibility::CLASSNAME_WEBVIEW, nullptr);
}

void SessionAccessibility::SendWindowStateChangedEvent(
    Accessible* aAccessible) {
  MOZ_ASSERT(NS_IsMainThread());
  // Suppress window state changed events from about:blank pages.
  // This is important for tests.
  if (aAccessible->IsDoc() && aAccessible->ChildCount() == 0) {
    return;
  }

  mSessionAccessibility->SendEvent(
      java::sdk::AccessibilityEvent::TYPE_WINDOW_STATE_CHANGED,
      AccessibleWrap::GetVirtualViewID(aAccessible),
      AccessibleWrap::AndroidClass(aAccessible), nullptr);

  SendWindowContentChangedEvent();
}

void SessionAccessibility::SendTextSelectionChangedEvent(
    Accessible* aAccessible, int32_t aCaretOffset) {
  MOZ_ASSERT(NS_IsMainThread());
  int32_t fromIndex = aCaretOffset;
  int32_t startSel = -1;
  int32_t endSel = -1;
  bool hasSelection =
      aAccessible->AsHyperTextBase()->SelectionBoundsAt(0, &startSel, &endSel);

  if (hasSelection) {
    fromIndex = startSel == aCaretOffset ? endSel : startSel;
  }

  nsAutoString text;
  if (aAccessible->IsHyperText()) {
    aAccessible->AsHyperTextBase()->TextSubstring(0, -1, text);
  } else if (aAccessible->IsText()) {
    aAccessible->AppendTextTo(text, 0, -1);
  }

  GECKOBUNDLE_START(eventInfo);
  GECKOBUNDLE_PUT(eventInfo, "text", jni::StringParam(text));
  GECKOBUNDLE_PUT(eventInfo, "fromIndex",
                  java::sdk::Integer::ValueOf(fromIndex));
  GECKOBUNDLE_PUT(eventInfo, "toIndex",
                  java::sdk::Integer::ValueOf(aCaretOffset));
  GECKOBUNDLE_FINISH(eventInfo);

  mSessionAccessibility->SendEvent(
      java::sdk::AccessibilityEvent::TYPE_VIEW_TEXT_SELECTION_CHANGED,
      AccessibleWrap::GetVirtualViewID(aAccessible),
      AccessibleWrap::AndroidClass(aAccessible), eventInfo);
}

void SessionAccessibility::SendTextChangedEvent(Accessible* aAccessible,
                                                const nsAString& aStr,
                                                int32_t aStart, uint32_t aLen,
                                                bool aIsInsert,
                                                bool aFromUser) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!aFromUser) {
    // Only dispatch text change events from users, for now.
    return;
  }

  nsAutoString text;
  if (aAccessible->IsHyperText()) {
    aAccessible->AsHyperTextBase()->TextSubstring(0, -1, text);
  } else if (aAccessible->IsText()) {
    aAccessible->AppendTextTo(text, 0, -1);
  }
  nsAutoString beforeText(text);
  if (aIsInsert) {
    beforeText.Cut(aStart, aLen);
  } else {
    beforeText.Insert(aStr, aStart);
  }

  GECKOBUNDLE_START(eventInfo);
  GECKOBUNDLE_PUT(eventInfo, "text", jni::StringParam(text));
  GECKOBUNDLE_PUT(eventInfo, "beforeText", jni::StringParam(beforeText));
  GECKOBUNDLE_PUT(eventInfo, "fromIndex", java::sdk::Integer::ValueOf(aStart));
  GECKOBUNDLE_PUT(eventInfo, "addedCount",
                  java::sdk::Integer::ValueOf(aIsInsert ? aLen : 0));
  GECKOBUNDLE_PUT(eventInfo, "removedCount",
                  java::sdk::Integer::ValueOf(aIsInsert ? 0 : aLen));
  GECKOBUNDLE_FINISH(eventInfo);

  mSessionAccessibility->SendEvent(
      java::sdk::AccessibilityEvent::TYPE_VIEW_TEXT_CHANGED,
      AccessibleWrap::GetVirtualViewID(aAccessible),
      AccessibleWrap::AndroidClass(aAccessible), eventInfo);
}

void SessionAccessibility::SendTextTraversedEvent(Accessible* aAccessible,
                                                  int32_t aStartOffset,
                                                  int32_t aEndOffset) {
  MOZ_ASSERT(NS_IsMainThread());
  nsAutoString text;
  if (aAccessible->IsHyperText()) {
    aAccessible->AsHyperTextBase()->TextSubstring(0, -1, text);
  } else if (aAccessible->IsText()) {
    aAccessible->AppendTextTo(text, 0, -1);
  }

  GECKOBUNDLE_START(eventInfo);
  GECKOBUNDLE_PUT(eventInfo, "text", jni::StringParam(text));
  GECKOBUNDLE_PUT(eventInfo, "fromIndex",
                  java::sdk::Integer::ValueOf(aStartOffset));
  GECKOBUNDLE_PUT(eventInfo, "toIndex",
                  java::sdk::Integer::ValueOf(aEndOffset));
  GECKOBUNDLE_FINISH(eventInfo);

  mSessionAccessibility->SendEvent(
      java::sdk::AccessibilityEvent::
          TYPE_VIEW_TEXT_TRAVERSED_AT_MOVEMENT_GRANULARITY,
      AccessibleWrap::GetVirtualViewID(aAccessible),
      AccessibleWrap::AndroidClass(aAccessible), eventInfo);
}

void SessionAccessibility::SendClickedEvent(Accessible* aAccessible,
                                            uint32_t aFlags) {
  GECKOBUNDLE_START(eventInfo);
  GECKOBUNDLE_PUT(eventInfo, "flags", java::sdk::Integer::ValueOf(aFlags));
  GECKOBUNDLE_FINISH(eventInfo);

  mSessionAccessibility->SendEvent(
      java::sdk::AccessibilityEvent::TYPE_VIEW_CLICKED,
      AccessibleWrap::GetVirtualViewID(aAccessible),
      AccessibleWrap::AndroidClass(aAccessible), eventInfo);
}

void SessionAccessibility::SendSelectedEvent(Accessible* aAccessible,
                                             bool aSelected) {
  MOZ_ASSERT(NS_IsMainThread());
  GECKOBUNDLE_START(eventInfo);
  // Boolean::FALSE/TRUE gets clobbered by a macro, so ugh.
  GECKOBUNDLE_PUT(eventInfo, "selected",
                  java::sdk::Integer::ValueOf(aSelected ? 1 : 0));
  GECKOBUNDLE_FINISH(eventInfo);

  mSessionAccessibility->SendEvent(
      java::sdk::AccessibilityEvent::TYPE_VIEW_SELECTED,
      AccessibleWrap::GetVirtualViewID(aAccessible),
      AccessibleWrap::AndroidClass(aAccessible), eventInfo);
}

void SessionAccessibility::SendAnnouncementEvent(Accessible* aAccessible,
                                                 const nsAString& aAnnouncement,
                                                 uint16_t aPriority) {
  MOZ_ASSERT(NS_IsMainThread());
  GECKOBUNDLE_START(eventInfo);
  GECKOBUNDLE_PUT(eventInfo, "text", jni::StringParam(aAnnouncement));
  GECKOBUNDLE_FINISH(eventInfo);

  // Announcements should have the root as their source, so we ignore the
  // accessible of the event.
  mSessionAccessibility->SendEvent(
      java::sdk::AccessibilityEvent::TYPE_ANNOUNCEMENT, kNoID,
      java::SessionAccessibility::CLASSNAME_WEBVIEW, eventInfo);
}

void SessionAccessibility::PopulateNodeInfo(
    Accessible* aAccessible, mozilla::jni::Object::Param aNodeInfo) {
  nsAutoString name;
  aAccessible->Name(name);
  nsAutoString textValue;
  aAccessible->Value(textValue);
  nsAutoString nodeID;
  aAccessible->DOMNodeID(nodeID);
  nsAutoString accDesc;
  aAccessible->Description(accDesc);
  uint64_t state = aAccessible->State();
  LayoutDeviceIntRect bounds = aAccessible->Bounds();
  uint8_t actionCount = aAccessible->ActionCount();
  int32_t virtualViewID = AccessibleWrap::GetVirtualViewID(aAccessible);
  Accessible* parent = virtualViewID != kNoID ? aAccessible->Parent() : nullptr;
  int32_t parentID = parent ? AccessibleWrap::GetVirtualViewID(parent) : 0;
  role role = aAccessible->Role();
  if (role == roles::LINK && !(state & states::LINKED)) {
    // A link without the linked state (<a> with no href) shouldn't be presented
    // as a link.
    role = roles::TEXT;
  }

  uint32_t flags = AccessibleWrap::GetFlags(role, state, actionCount);
  int32_t className = AccessibleWrap::AndroidClass(aAccessible);

  nsAutoString hint;
  nsAutoString text;
  nsAutoString description;
  if (state & states::EDITABLE) {
    // An editable field's name is populated in the hint.
    hint.Assign(name);
    text.Assign(textValue);
  } else {
    if (role == roles::LINK || role == roles::HEADING) {
      description.Assign(name);
    } else {
      text.Assign(name);
    }
  }

  if (!accDesc.IsEmpty()) {
    if (!hint.IsEmpty()) {
      // If this is an editable, the description is concatenated with a
      // whitespace directly after the name.
      hint.AppendLiteral(" ");
    }
    hint.Append(accDesc);
  }

  if ((state & states::REQUIRED) != 0) {
    nsAutoString requiredString;
    if (LocalizeString(u"stateRequired"_ns, requiredString)) {
      if (!hint.IsEmpty()) {
        // If the hint is non-empty, concatenate with a comma for a brief pause.
        hint.AppendLiteral(", ");
      }
      hint.Append(requiredString);
    }
  }

  RefPtr<AccAttributes> attributes = aAccessible->Attributes();
  nsAccUtils::SetAccGroupAttrs(attributes, aAccessible);

  nsAutoString geckoRole;
  nsAutoString roleDescription;
  if (virtualViewID != kNoID) {
    AccessibleWrap::GetRoleDescription(role, attributes, geckoRole,
                                       roleDescription);
  }

  int32_t inputType = 0;
  if (attributes) {
    nsString inputTypeAttr;
    attributes->GetAttribute(nsGkAtoms::textInputType, inputTypeAttr);
    inputType = AccessibleWrap::GetInputType(inputTypeAttr);
  }

  auto childCount = aAccessible->ChildCount();
  nsTArray<int32_t> children(childCount);
  if (!nsAccUtils::MustPrune(aAccessible)) {
    for (uint32_t i = 0; i < childCount; i++) {
      auto child = aAccessible->ChildAt(i);
      children.AppendElement(AccessibleWrap::GetVirtualViewID(child));
    }
  }

  const int32_t boundsArray[4] = {bounds.x, bounds.y, bounds.x + bounds.width,
                                  bounds.y + bounds.height};

  mSessionAccessibility->PopulateNodeInfo(
      aNodeInfo, virtualViewID, parentID, jni::IntArray::From(children), flags,
      className, jni::IntArray::New(boundsArray, 4), jni::StringParam(text),
      jni::StringParam(description), jni::StringParam(hint),
      jni::StringParam(geckoRole), jni::StringParam(roleDescription),
      jni::StringParam(nodeID), inputType);

  if (aAccessible->HasNumericValue()) {
    double curValue = aAccessible->CurValue();
    double minValue = aAccessible->MinValue();
    double maxValue = aAccessible->MaxValue();
    double step = aAccessible->Step();

    int32_t rangeType = 0;  // integer
    if (maxValue == 1 && minValue == 0) {
      rangeType = 2;  // percent
    } else if (std::round(step) != step) {
      rangeType = 1;  // float;
    }

    mSessionAccessibility->PopulateNodeRangeInfo(
        aNodeInfo, rangeType, static_cast<float>(minValue),
        static_cast<float>(maxValue), static_cast<float>(curValue));
  }

  if (attributes) {
    Maybe<int32_t> rowIndex =
        attributes->GetAttribute<int32_t>(nsGkAtoms::posinset);
    if (rowIndex) {
      mSessionAccessibility->PopulateNodeCollectionItemInfo(
          aNodeInfo, *rowIndex - 1, 1, 0, 1);
    }

    Maybe<int32_t> rowCount =
        attributes->GetAttribute<int32_t>(nsGkAtoms::child_item_count);
    if (rowCount) {
      int32_t selectionMode = 0;
      if (aAccessible->IsSelect()) {
        selectionMode = (state & states::MULTISELECTABLE) ? 2 : 1;
      }
      mSessionAccessibility->PopulateNodeCollectionInfo(
          aNodeInfo, *rowCount, 1, selectionMode,
          attributes->HasAttribute(nsGkAtoms::tree));
    }
  }
}

Accessible* SessionAccessibility::GetAccessibleByID(int32_t aID) const {
  return mIDToAccessibleMap.Get(aID);
}

#ifdef DEBUG
static bool IsDetachedDoc(Accessible* aAccessible) {
  if (!aAccessible->IsRemote() || !aAccessible->AsRemote()->IsDoc()) {
    return false;
  }

  return !aAccessible->Parent() ||
         aAccessible->Parent()->FirstChild() != aAccessible;
}
#endif

SessionAccessibility::IDMappingEntry::IDMappingEntry(Accessible* aAccessible)
    : mInternalID(0) {
  *this = aAccessible;
}

SessionAccessibility::IDMappingEntry&
SessionAccessibility::IDMappingEntry::operator=(Accessible* aAccessible) {
  mInternalID = aAccessible->ID();
  MOZ_ASSERT(!(mInternalID & IS_REMOTE), "First bit is used in accessible ID!");
  if (aAccessible->IsRemote()) {
    mInternalID |= IS_REMOTE;
  }

  Accessible* docAcc = nsAccUtils::DocumentFor(aAccessible);
  MOZ_ASSERT(docAcc);
  if (docAcc) {
    MOZ_ASSERT(docAcc->IsRemote() == aAccessible->IsRemote());
    if (docAcc->IsRemote()) {
      mDoc = docAcc->AsRemote()->AsDoc();
    } else {
      mDoc = docAcc->AsLocal();
    }
  }

  return *this;
}

SessionAccessibility::IDMappingEntry::operator Accessible*() const {
  if (mInternalID == 0) {
    return static_cast<LocalAccessible*>(mDoc.get());
  }

  if (mInternalID == IS_REMOTE) {
    return static_cast<DocAccessibleParent*>(mDoc.get());
  }

  if (mInternalID & IS_REMOTE) {
    return static_cast<DocAccessibleParent*>(mDoc.get())
        ->GetAccessible(mInternalID & ~IS_REMOTE);
  }

  Accessible* accessible =
      static_cast<LocalAccessible*>(mDoc.get())
          ->AsDoc()
          ->GetAccessibleByUniqueID(reinterpret_cast<void*>(mInternalID));
  // If the accessible is retrievable from the DocAccessible, it can't be
  // defunct.
  MOZ_ASSERT(!accessible->AsLocal()->IsDefunct());

  return accessible;
}

void SessionAccessibility::RegisterAccessible(Accessible* aAccessible) {
  if (IPCAccessibilityActive()) {
    // Don't register accessible in content process.
    return;
  }

  nsAccessibilityService::GetAndroidMonitor().AssertCurrentThreadOwns();
  RefPtr<SessionAccessibility> sessionAcc = GetInstanceFor(aAccessible);
  if (!sessionAcc) {
    return;
  }

  bool isTopLevel = false;
  if (aAccessible->IsLocal() && aAccessible->IsDoc()) {
    DocAccessibleWrap* doc =
        static_cast<DocAccessibleWrap*>(aAccessible->AsLocal()->AsDoc());
    isTopLevel = doc->IsTopLevelContentDoc();
  } else if (aAccessible->IsRemote() && aAccessible->IsDoc()) {
    isTopLevel = aAccessible->AsRemote()->AsDoc()->IsTopLevel();
  }

  int32_t virtualViewID = kNoID;
  if (!isTopLevel) {
    if (sessionAcc->mIDToAccessibleMap.IsEmpty()) {
      // We expect there to already be at least one accessible
      // registered (the top-level one). If it isn't we are
      // probably in a shutdown process where it was already
      // unregistered. So we don't register this accessible.
      return;
    }
    // Don't use the special "unset" value (0).
    while ((virtualViewID = sIDSet.GetID()) == kUnsetID) {
    }
  }
  AccessibleWrap::SetVirtualViewID(aAccessible, virtualViewID);

  Accessible* oldAcc = sessionAcc->mIDToAccessibleMap.Get(virtualViewID);
  if (oldAcc) {
    // About to overwrite mapping of registered accessible. This should
    // only happen when the registered accessible is a detached document.
    MOZ_ASSERT(IsDetachedDoc(oldAcc),
               "ID already registered to non-detached document");
    AccessibleWrap::SetVirtualViewID(oldAcc, kUnsetID);
  }

  sessionAcc->mIDToAccessibleMap.InsertOrUpdate(virtualViewID, aAccessible);
}

void SessionAccessibility::UnregisterAccessible(Accessible* aAccessible) {
  if (IPCAccessibilityActive()) {
    // Don't unregister accessible in content process.
    return;
  }

  nsAccessibilityService::GetAndroidMonitor().AssertCurrentThreadOwns();
  int32_t virtualViewID = AccessibleWrap::GetVirtualViewID(aAccessible);
  if (virtualViewID == kUnsetID) {
    return;
  }

  RefPtr<SessionAccessibility> sessionAcc = GetInstanceFor(aAccessible);
  if (sessionAcc) {
    Accessible* registeredAcc =
        sessionAcc->mIDToAccessibleMap.Get(virtualViewID);
    if (registeredAcc != aAccessible) {
      // Attempting to unregister an accessible that is not mapped to
      // its virtual view ID. This probably means it is a detached document
      // and a more recent document overwrote its '-1' mapping.
      // We set its own virtual view ID to `kUnsetID` and return early.
      MOZ_ASSERT(!registeredAcc || IsDetachedDoc(aAccessible),
                 "Accessible is detached document");
      AccessibleWrap::SetVirtualViewID(aAccessible, kUnsetID);
      return;
    }

    MOZ_ASSERT(registeredAcc, "Unregistering unregistered accessible");
    MOZ_ASSERT(registeredAcc == aAccessible, "Unregistering wrong accessible");
    sessionAcc->mIDToAccessibleMap.Remove(virtualViewID);
  }

  if (virtualViewID > kNoID) {
    sIDSet.ReleaseID(virtualViewID);
  }

  AccessibleWrap::SetVirtualViewID(aAccessible, kUnsetID);
}

void SessionAccessibility::UnregisterAll(PresShell* aPresShell) {
  if (IPCAccessibilityActive()) {
    // Don't unregister accessible in content process.
    return;
  }

  nsAccessibilityService::GetAndroidMonitor().AssertCurrentThreadOwns();
  RefPtr<SessionAccessibility> sessionAcc = GetInstanceFor(aPresShell);
  if (sessionAcc) {
    sessionAcc->mIDToAccessibleMap.Clear();
  }
}
