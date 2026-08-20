/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "IMEStateManager.h"

#include "IMEContentObserver.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/EditorBase.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/Logging.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_intl.h"
#include "mozilla/TextComposition.h"
#include "mozilla/TextEvents.h"
#include "mozilla/ToString.h"
#include "mozilla/dom/BrowserBridgeChild.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLFormElement.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLTextAreaElement.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/widget/IMEData.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsFocusManager.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsIFormControl.h"
#include "nsINode.h"
#include "nsISupports.h"
#include "nsIURI.h"
#include "nsIURIMutator.h"
#include "nsPresContext.h"
#include "nsTextControlFrame.h"
#include "nsThreadUtils.h"

namespace mozilla {

using namespace dom;
using namespace widget;

/**
 * When a method is called, log its arguments and/or related static variables
 * with LogLevel::Info.  However, if it puts too many logs like
 * OnDestroyPresContext(), should long only when the method actually does
 * something. In this case, the log should start with "<method name>".
 *
 * When a method quits due to unexpected situation, log the reason with
 * LogLevel::Error.  In this case, the log should start with
 * "<method name>(), FAILED".  The indent makes the log look easier.
 *
 * When a method does something only in some situations and it may be important
 * for debug, log the information with LogLevel::Debug.  In this case, the log
 * should start with "  <method name>(),".
 */
LazyLogModule sISMLog("IMEStateManager");

StaticRefPtr<Element> IMEStateManager::sFocusedElement;
StaticRefPtr<nsPresContext> IMEStateManager::sFocusedPresContext;
nsIWidget* IMEStateManager::sTextInputHandlingWidget = nullptr;
nsIWidget* IMEStateManager::sFocusedIMEWidget = nullptr;
StaticRefPtr<BrowserParent> IMEStateManager::sFocusedIMEBrowserParent;
nsIWidget* IMEStateManager::sActiveInputContextWidget = nullptr;
StaticRefPtr<IMEContentObserver> IMEStateManager::sActiveIMEContentObserver;
TextCompositionArray* IMEStateManager::sTextCompositions = nullptr;
InputContext::Origin IMEStateManager::sOrigin = InputContext::ORIGIN_MAIN;
MOZ_RUNINIT InputContext IMEStateManager::sActiveChildInputContext;
uint32_t IMEStateManager::sFocusGeneration = 0;
bool IMEStateManager::sInstalledMenuKeyboardListener = false;
bool IMEStateManager::sIsGettingNewIMEState = false;
bool IMEStateManager::sCleaningUpForStoppingIMEStateManagement = false;
bool IMEStateManager::sIsActive = false;
constinit Maybe<IMEStateManager::PendingFocusedBrowserSwitchingData>
    IMEStateManager::sPendingFocusedBrowserSwitchingData;

class PseudoFocusChangeRunnable : public Runnable {
 public:
  explicit PseudoFocusChangeRunnable(bool aInstallingMenuKeyboardListener)
      : Runnable("PseudoFocusChangeRunnable"),
        mFocusedPresContext(IMEStateManager::sFocusedPresContext),
        mFocusedElement(IMEStateManager::sFocusedElement),
        mInstallMenuKeyboardListener(aInstallingMenuKeyboardListener) {}

  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override {
    IMEStateManager::SetMenubarPseudoFocus(this, mInstallMenuKeyboardListener,
                                           mFocusedPresContext);
    return NS_OK;
  }

 private:
  const RefPtr<nsPresContext> mFocusedPresContext;
  const RefPtr<Element> mFocusedElement;
  const bool mInstallMenuKeyboardListener;
};

StaticRefPtr<PseudoFocusChangeRunnable>
    IMEStateManager::sPseudoFocusChangeRunnable;

// static
void IMEStateManager::Init() {
  sOrigin = XRE_IsParentProcess() ? InputContext::ORIGIN_MAIN
                                  : InputContext::ORIGIN_CONTENT;
  ResetActiveChildInputContext();
}

// static
void IMEStateManager::Shutdown() {
  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("Shutdown(), sTextCompositions=0x%p, sTextCompositions->Length()=%zu, "
       "sPendingFocusedBrowserSwitchingData.isSome()=%s",
       sTextCompositions, sTextCompositions ? sTextCompositions->Length() : 0,
       TrueOrFalse(sPendingFocusedBrowserSwitchingData.isSome())));
  MOZ_LOG(sISMLog, LogLevel::Debug,
          ("  Shutdown(), sFocusedElement=0x%p, sFocusedPresContext=0x%p, "
           "sTextInputHandlingWidget=0x%p, sFocusedIMEWidget=0x%p, "
           "sFocusedIMEBrowserParent=0x%p, sActiveInputContextWidget=0x%p, "
           "sActiveIMEContentObserver=0x%p",
           sFocusedElement.get(), sFocusedPresContext.get(),
           sTextInputHandlingWidget, sFocusedIMEWidget,
           sFocusedIMEBrowserParent.get(), sActiveInputContextWidget,
           sActiveIMEContentObserver.get()));

  sPendingFocusedBrowserSwitchingData.reset();
  MOZ_ASSERT(!sTextCompositions || !sTextCompositions->Length());
  delete sTextCompositions;
  sTextCompositions = nullptr;
  // All string instances in the global space need to be empty after XPCOM
  // shutdown.
  sActiveChildInputContext.ShutDown();
}

// static
void IMEStateManager::OnFocusMovedBetweenBrowsers(BrowserParent* aBlur,
                                                  BrowserParent* aFocus) {
  RefPtr<BrowserParent> blur(aBlur);
  MOZ_ASSERT(blur != aFocus);
  MOZ_ASSERT(XRE_IsParentProcess());

  if (sPendingFocusedBrowserSwitchingData.isSome()) {
    MOZ_ASSERT(blur ==
               sPendingFocusedBrowserSwitchingData.ref().mBrowserParentFocused);
    // If focus is not changed between browsers actually, we need to do
    // nothing here.  Let's cancel handling what this method does.
    if (sPendingFocusedBrowserSwitchingData.ref().mBrowserParentBlurred ==
        aFocus) {
      sPendingFocusedBrowserSwitchingData.reset();
      MOZ_LOG(sISMLog, LogLevel::Info,
              ("  OnFocusMovedBetweenBrowsers(), canceled all pending focus "
               "moves between browsers"));
      return;
    }
    blur = sPendingFocusedBrowserSwitchingData.ref().mBrowserParentBlurred;
    sPendingFocusedBrowserSwitchingData.ref().mBrowserParentFocused = aFocus;
    MOZ_ASSERT(blur != aFocus);
  }

  // If application was inactive, but is now activated, and the last focused
  // this is called by BrowserParent::UnsetTopLevelWebFocusAll() from
  // nsFocusManager::WindowRaised().  If a content has focus in a remote
  // process and it has composition, it may get focus back later and the
  // composition shouldn't be commited now.  Therefore, we should put off to
  // handle this until getting another call of this method or a call of
  //`OnFocusChangeInternal()`.
  if (blur && !aFocus && !sIsActive && sTextInputHandlingWidget &&
      sTextCompositions &&
      sTextCompositions->GetCompositionFor(sTextInputHandlingWidget)) {
    if (sPendingFocusedBrowserSwitchingData.isNothing()) {
      sPendingFocusedBrowserSwitchingData.emplace(blur, aFocus);
    }
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  OnFocusMovedBetweenBrowsers(), put off to handle it until "
             "next OnFocusChangeInternal() call"));
    return;
  }
  sPendingFocusedBrowserSwitchingData.reset();

  const nsCOMPtr<nsIWidget> oldWidget = sTextInputHandlingWidget;
  // In the chrome-process case, we'll get sTextInputHandlingWidget from a
  // PresShell later.
  sTextInputHandlingWidget =
      aFocus ? nsCOMPtr<nsIWidget>(aFocus->GetTextInputHandlingWidget()).get()
             : nullptr;
  if (oldWidget && sTextCompositions) {
    RefPtr<TextComposition> composition =
        sTextCompositions->GetCompositionFor(oldWidget);
    if (composition) {
      MOZ_LOG(sISMLog, LogLevel::Debug,
              ("  OnFocusMovedBetweenBrowsers(), requesting to commit "
               "composition to "
               "the (previous) focused widget (would request=%s)",
               TrueOrFalse(!oldWidget->IMENotificationRequestsRef().contains(
                   IMENotificationRequest::NotifyDuringInactive))));
      NotifyIME(REQUEST_TO_COMMIT_COMPOSITION, oldWidget,
                composition->GetBrowserParent());
    }
  }

  // The manager check is to avoid telling the content process to stop
  // IME state management after focus has already moved there between
  // two same-process-hosted out-of-process iframes.
  if (blur && (!aFocus || (blur->Manager() != aFocus->Manager()))) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  OnFocusMovedBetweenBrowsers(), notifying previous "
             "focused child process of parent process or another child process "
             "getting focus"));
    blur->StopIMEStateManagement();
  }

  if (sActiveIMEContentObserver) {
    DestroyIMEContentObserver();
  }

  if (sFocusedIMEWidget) {
    // sFocusedIMEBrowserParent can be null, if IME focus hasn't been
    // taken before BrowserParent blur.
    // `blur` can be null when keyboard focus moves not actually
    // between tabs but an open menu is involved.
    MOZ_ASSERT(!sFocusedIMEBrowserParent || !blur ||
               (sFocusedIMEBrowserParent == blur));
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  OnFocusMovedBetweenBrowsers(), notifying IME of blur"));
    NotifyIME(NOTIFY_IME_OF_BLUR, sFocusedIMEWidget, sFocusedIMEBrowserParent);

    MOZ_ASSERT(!sFocusedIMEBrowserParent);
    MOZ_ASSERT(!sFocusedIMEWidget);

  } else {
    MOZ_ASSERT(!sFocusedIMEBrowserParent);
  }

  // We deliberately don't null out sFocusedElement or sFocusedPresContext here.
  // When focus is in remote content, as far as layout in the chrome process is
  // concerned, the corresponding content is the top-level XUL browser. Changes
  // among out-of-process iframes don't change that, so dropping the pointer to
  // the XUL browser upon such a change would break IME handling.
}

// static
void IMEStateManager::WidgetDestroyed(nsIWidget* aWidget) {
  MOZ_LOG(sISMLog, LogLevel::Debug,
          ("WidgetDestroyed(aWidget=0x%p), sFocusedIMEWidget=0x%p, "
           "sActiveInputContextWidget=0x%p, sFocusedIMEBrowserParent=0x%p",
           aWidget, sFocusedIMEWidget, sActiveInputContextWidget,
           sFocusedIMEBrowserParent.get()));
  if (sTextInputHandlingWidget == aWidget) {
    sTextInputHandlingWidget = nullptr;
  }
  if (sFocusedIMEWidget == aWidget) {
    if (sFocusedIMEBrowserParent) {
      OnFocusMovedBetweenBrowsers(sFocusedIMEBrowserParent, nullptr);
      MOZ_ASSERT(!sFocusedIMEBrowserParent);
    }
    sFocusedIMEWidget = nullptr;
  }
  if (sActiveInputContextWidget == aWidget) {
    sActiveInputContextWidget = nullptr;
  }
}

// static
void IMEStateManager::WidgetOnQuit(nsIWidget* aWidget) {
  if (sFocusedIMEWidget == aWidget) {
    MOZ_LOG(
        sISMLog, LogLevel::Debug,
        ("WidgetOnQuit(aWidget=0x%p (available %s)), sFocusedIMEWidget=0x%p",
         aWidget, TrueOrFalse(aWidget && !aWidget->Destroyed()),
         sFocusedIMEWidget));
    // Notify IME of blur (which is done by IMEContentObserver::Destroy
    // automatically) when the widget still has IME focus before forgetting the
    // focused widget because the focused widget is required to clean up native
    // IME handler with sending blur notification.  Fortunately, the widget
    // has not been destroyed yet here since some methods to sending blur
    // notification won't work with destroyed widget.
    IMEStateManager::DestroyIMEContentObserver();
    // Finally, clean up the widget and related objects for avoiding to leak.
    IMEStateManager::WidgetDestroyed(aWidget);
  }
}

// static
void IMEStateManager::StopIMEStateManagement() {
  MOZ_ASSERT(XRE_IsContentProcess());
  MOZ_LOG(sISMLog, LogLevel::Info, ("StopIMEStateManagement()"));

  // NOTE: Don't set input context from here since this has already lost
  //       the rights to change input context.

  // The requestee of this method in the main process must destroy its
  // active IMEContentObserver for making existing composition end and
  // make it be possible to start new composition in new focused process.
  // Therefore, we shouldn't notify the main process of any changes which
  // occurred after here.
  AutoRestore<bool> restoreStoppingIMEStateManagementState(
      sCleaningUpForStoppingIMEStateManagement);
  sCleaningUpForStoppingIMEStateManagement = true;

  if (sTextCompositions && sFocusedPresContext) {
    NotifyIME(REQUEST_TO_COMMIT_COMPOSITION, sFocusedPresContext, nullptr);
  }
  sActiveInputContextWidget = nullptr;
  sFocusedPresContext = nullptr;
  sFocusedElement = nullptr;
  AdvanceFocusGeneration();
  sIsActive = false;
  DestroyIMEContentObserver();
}

// static
void IMEStateManager::MaybeStartOffsetUpdatedInChild(nsIWidget* aWidget,
                                                     uint32_t aStartOffset) {
  if (NS_WARN_IF(!sTextCompositions)) {
    MOZ_LOG(sISMLog, LogLevel::Warning,
            ("MaybeStartOffsetUpdatedInChild(aWidget=0x%p, aStartOffset=%u), "
             "called when there is no composition",
             aWidget, aStartOffset));
    return;
  }

  TextComposition* const composition = GetTextCompositionFor(aWidget);
  if (NS_WARN_IF(!composition)) {
    MOZ_LOG(sISMLog, LogLevel::Warning,
            ("MaybeStartOffsetUpdatedInChild(aWidget=0x%p, aStartOffset=%u), "
             "called when there is no composition",
             aWidget, aStartOffset));
    return;
  }

  if (composition->NativeOffsetOfStartComposition() == aStartOffset) {
    return;
  }

  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("MaybeStartOffsetUpdatedInChild(aWidget=0x%p, aStartOffset=%u), "
       "old offset=%u",
       aWidget, aStartOffset, composition->NativeOffsetOfStartComposition()));
  composition->OnStartOffsetUpdatedInChild(aStartOffset);
}

// static
nsresult IMEStateManager::OnDestroyPresContext(nsPresContext& aPresContext) {
  // First, if there is a composition in the aPresContext, clean up it.
  if (sTextCompositions) {
    TextCompositionArray::index_type i =
        sTextCompositions->IndexOf(&aPresContext);
    if (i != TextCompositionArray::NoIndex) {
      MOZ_LOG(sISMLog, LogLevel::Debug,
              ("  OnDestroyPresContext(), "
               "removing TextComposition instance from the array (index=%zu)",
               i));
      // there should be only one composition per presContext object.
      sTextCompositions->ElementAt(i)->Destroy();
      sTextCompositions->RemoveElementAt(i);
      if (sTextCompositions->IndexOf(&aPresContext) !=
          TextCompositionArray::NoIndex) {
        MOZ_LOG(sISMLog, LogLevel::Error,
                ("  OnDestroyPresContext(), FAILED to remove "
                 "TextComposition instance from the array"));
        MOZ_CRASH("Failed to remove TextComposition instance from the array");
      }
    }
  }

  if (&aPresContext != sFocusedPresContext) {
    return NS_OK;
  }

  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("OnDestroyPresContext(aPresContext=0x%p), "
       "sFocusedPresContext=0x%p, sFocusedElement=0x%p, sTextCompositions=0x%p",
       &aPresContext, sFocusedPresContext.get(), sFocusedElement.get(),
       sTextCompositions));

  DestroyIMEContentObserver();

  if (sTextInputHandlingWidget) {
    IMEState newState = GetNewIMEState(*sFocusedPresContext, nullptr);
    InputContextAction action(InputContextAction::CAUSE_UNKNOWN,
                              InputContextAction::LOST_FOCUS);
    InputContext::Origin origin =
        BrowserParent::GetFocused() ? InputContext::ORIGIN_CONTENT : sOrigin;
    OwningNonNull<nsIWidget> textInputHandlingWidget =
        *sTextInputHandlingWidget;
    SetIMEState(newState, nullptr, nullptr, textInputHandlingWidget, action,
                origin);
  }
  sTextInputHandlingWidget = nullptr;
  sFocusedElement = nullptr;
  sFocusedPresContext = nullptr;
  AdvanceFocusGeneration();
  return NS_OK;
}

// static
nsresult IMEStateManager::OnRemoveContent(nsPresContext& aPresContext,
                                          Element& aElement) {
  // First, if there is a composition in the aElement, clean up it.
  if (sTextCompositions) {
    const RefPtr<TextComposition> compositionInContent =
        sTextCompositions->GetCompositionInContent(&aPresContext, &aElement);

    if (compositionInContent) {
      MOZ_LOG(sISMLog, LogLevel::Debug,
              ("  OnRemoveContent(), composition is in the content"));

      // Try resetting the native IME state.  Be aware, typically, this method
      // is called during the content being removed.  Then, the native
      // composition events which are caused by following APIs are ignored due
      // to unsafe to run script (in PresShell::HandleEvent()).
      nsresult rv =
          compositionInContent->NotifyIME(REQUEST_TO_CANCEL_COMPOSITION);
      if (NS_FAILED(rv)) {
        compositionInContent->NotifyIME(REQUEST_TO_COMMIT_COMPOSITION);
      }
    }
  }

  if (!sFocusedPresContext ||
      // If focused element is a text control or an editing host, we need to
      // emulate "blur" on it when it's removed.
      (sFocusedElement && sFocusedElement != &aElement) ||
      // If it is (or was) in design mode, we need to emulate "blur" on the
      // document when the observing element (typically, <body>) is removed.
      (!sFocusedElement &&
       (!sActiveIMEContentObserver ||
        sActiveIMEContentObserver
                ->GetObservingEditingHostOrTextControlElement() !=
            &aElement))) {
    return NS_OK;
  }
  MOZ_ASSERT(sFocusedPresContext == &aPresContext);

  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("OnRemoveContent(aPresContext=0x%p, aElement=0x%p), "
       "sFocusedPresContext=0x%p, sFocusedElement=0x%p, sTextCompositions=0x%p",
       &aPresContext, &aElement, sFocusedPresContext.get(),
       sFocusedElement.get(), sTextCompositions));

  DestroyIMEContentObserver();

  // FYI: Don't clear sTextInputHandlingWidget and sFocusedPresContext because
  // the window/document keeps having focus.
  sFocusedElement = nullptr;
  AdvanceFocusGeneration();

  // Current IME transaction should commit
  if (!sTextInputHandlingWidget) {
    return NS_OK;
  }

  IMEState newState = GetNewIMEState(*sFocusedPresContext, nullptr);
  InputContextAction action(InputContextAction::CAUSE_UNKNOWN,
                            InputContextAction::LOST_FOCUS);
  InputContext::Origin origin =
      BrowserParent::GetFocused() ? InputContext::ORIGIN_CONTENT : sOrigin;
  OwningNonNull<nsIWidget> textInputHandlingWidget = *sTextInputHandlingWidget;
  SetIMEState(newState, &aPresContext, nullptr, textInputHandlingWidget, action,
              origin);
  if (sFocusedPresContext != &aPresContext || sFocusedElement) {
    return NS_OK;  // Somebody already has focus, don't steal it.
  }

  if (IsIMEObserverNeeded(newState)) {
    // Initializing IMEContentObserver instance requires Selection, but its
    // ranges have not been adjusted for this removal.  Therefore, we need to
    // wait a moment.
    nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
        "IMEStateManager::RecreateIMEContentObserverWhenContentRemoved",
        [presContext = OwningNonNull{aPresContext}]() {
          // If focus has been changed, we need to do nothing.
          if (sFocusedPresContext != presContext || sFocusedElement)
              [[unlikely]] {
            return;
          }
          // Otherwise, we need to recreate IMEContentObserver if there is no
          // proper IMEContentObserver has not been created.
          const RefPtr<HTMLEditor> htmlEditor =
              nsContentUtils::GetHTMLEditor(presContext);
          if (!htmlEditor) [[unlikely]] {
            return;
          }
          if (sActiveIMEContentObserver) {
            // If we've already created IMEContentObserver and it is observing
            // for the HTMLEditor, we need to do nothing.
            if (sActiveIMEContentObserver->IsObserving(presContext, nullptr)) {
              return;
            }
            NS_WARNING(
                "There is unexpected IMEContentObserver, we'll recreate it...");
            // Then, destroy the unnecessary observer.
            DestroyIMEContentObserver();
            // NOTIFY_IME_OF_BLUR **might** cause changing the DOM focus because
            // if we're in the parent process, the widget may call native IME
            // synchronously.
            if (sFocusedPresContext != presContext || sFocusedElement)
                [[unlikely]] {
              MOZ_ASSERT(XRE_IsParentProcess());
              return;
            }
            if (sActiveIMEContentObserver) {
              MOZ_ASSERT(XRE_IsParentProcess());
              MOZ_ASSERT(
                  sActiveIMEContentObserver->IsObserving(presContext, nullptr));
              return;
            }
          }
          CreateIMEContentObserver(*htmlEditor, nullptr);
        }));
  }

  return NS_OK;
}

// static
void IMEStateManager::OnParentChainChangedOfObservingElement(
    IMEContentObserver& aObserver, nsIContent& aContent) {
  if (!sFocusedPresContext || sActiveIMEContentObserver != &aObserver) {
    return;
  }
  if (Element* const textControlElement =
          aObserver.GetObservingTextControlElement()) {
    // If a text control has focus, the parent chain changed is notified when
    // the anonymous <div> is removed since aObserver is observing it. However,
    // we want to be notified only when the focused element, i.e., the text
    // control itself, is removed.  So, when the text control element is not an
    // inclusive descendant of aContent, we don't need to handle this.
    MOZ_ASSERT(textControlElement->IsTextControlElement());
    if (!textControlElement->IsInclusiveDescendantOf(&aContent)) {
      return;
    }
  }
  const RefPtr<nsPresContext> presContext = aObserver.GetPresContext();
  const RefPtr<Element> editingHostOrTextControlElement =
      aObserver.GetObservingEditingHostOrTextControlElement();
  if (NS_WARN_IF(!presContext) ||
      NS_WARN_IF(!editingHostOrTextControlElement)) {
    return;
  }
  MOZ_LOG(sISMLog, LogLevel::Info,
          ("OnParentChainChangedOfObservingElement(aObserver=0x%p), "
           "sFocusedPresContext=0x%p, sFocusedElement=0x%p, "
           "aObserver->GetPresContext()=0x%p, "
           "aObserver->GetObservingEditingHostOrTextControlElement()=0x%p",
           &aObserver, sFocusedPresContext.get(), sFocusedElement.get(),
           presContext.get(), editingHostOrTextControlElement.get()));
  OnRemoveContent(*presContext, *editingHostOrTextControlElement);
}

// static
void IMEStateManager::OnUpdateHTMLEditorRootElement(HTMLEditor& aHTMLEditor,
                                                    Element* aNewRootElement) {
  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("OnUpdateHTMLEditorRootElement(aHTMLEditor=0x%p, aNewRootElement=%s), "
       "sFocusedPresContext=0x%p, sFocusedElement=%s, "
       "sActiveIMEContentObserver=0x%p (GetObservingElement()=%s), "
       "sTextInputHandlingWidget=0x%p, aHTMLEditor.GetPresContext()=0x%p",
       &aHTMLEditor,
       aNewRootElement ? ToString(*aNewRootElement).c_str() : "nullptr",
       sFocusedPresContext.get(),
       ToString(RefPtr<Element>(sFocusedElement)).c_str(),
       sActiveIMEContentObserver.get(),
       sActiveIMEContentObserver
           ? ToString(RefPtr<Element>(
                          sActiveIMEContentObserver->GetObservingElement()))
                 .c_str()
           : "N/A",
       sTextInputHandlingWidget, aHTMLEditor.GetPresContext()));

  if (
      // Nothing to do if nobody has focus.
      !sFocusedPresContext || !sTextInputHandlingWidget ||
      // Nothing to do if the editable document does not have focus.
      sFocusedPresContext != aHTMLEditor.GetPresContext() ||
      // If it's not in the design mode, any mutation should be handled with a
      // focus change.
      !aHTMLEditor.IsInDesignMode() ||
      // Nothing to do if the active IMEContentObserver has already been
      // observing the new root element.
      (aNewRootElement && sActiveIMEContentObserver &&
       sActiveIMEContentObserver->GetObservingElement() == aNewRootElement)) {
    return;
  }

  OwningNonNull<nsPresContext> presContext = *sFocusedPresContext;
  if (sFocusedElement && presContext->Document()->IsInDesignMode()) {
    sFocusedElement = presContext->Document()->GetRootElement();
    AdvanceFocusGeneration();
  }
  RefPtr<Element> focusedElement = sFocusedElement;

  DestroyIMEContentObserver();

  if (!aNewRootElement) {
    // When there is no element in the document, let's disable IME.
    IMEState newState = GetNewIMEState(*presContext, nullptr);
    MOZ_ASSERT(newState.mEnabled == IMEEnabled::Disabled);
    InputContextAction action(InputContextAction::CAUSE_UNKNOWN,
                              InputContextAction::LOST_FOCUS);
    InputContext::Origin origin =
        BrowserParent::GetFocused() ? InputContext::ORIGIN_CONTENT : sOrigin;
    OwningNonNull<nsIWidget> textInputHandlingWidget =
        *sTextInputHandlingWidget;
    SetIMEState(newState, presContext, nullptr, textInputHandlingWidget, action,
                origin);
    return;
  }

  MOZ_ASSERT(aNewRootElement);
  const IMEState newState = GetNewIMEState(*presContext, focusedElement);
  // The caller wants to enable IME if there is at least one element in the most
  // cases.  However, IME may be disabled, e.g., the menubar key listener is now
  // installed, etc.  Therefore, if the new state is not "enabled", we should
  // not update the state in unexpected situations.
  if (MOZ_UNLIKELY(newState.mEnabled != IMEEnabled::Enabled)) {
    MOZ_LOG(sISMLog, LogLevel::Warning,
            ("  OnUpdateHTMLEditorRootElement(): WARNING, Not updating IME "
             "state because of the new IME state is not \"enabled\""));
    return;
  }
  InputContextAction action(InputContextAction::CAUSE_UNKNOWN,
                            InputContextAction::GOT_FOCUS);
  InputContext::Origin origin =
      BrowserParent::GetFocused() ? InputContext::ORIGIN_CONTENT : sOrigin;
  OwningNonNull<nsIWidget> textInputHandlingWidget = *sTextInputHandlingWidget;
  SetIMEState(newState, presContext, focusedElement, textInputHandlingWidget,
              action, origin);
  // Somebody moved focus, don't keep handling this since we lost the rights
  // to touch IME state.
  if (sFocusedPresContext != presContext || sFocusedElement != focusedElement ||
      sActiveIMEContentObserver) {
    MOZ_LOG(sISMLog, LogLevel::Warning,
            ("OnUpdateHTMLEditorRootElement(), WARNING: Somebody update focus "
             "during setting IME state, sFocusedPresContext=0x%p, "
             "sFocusedElement=%s, sActiveIMEContentObserver=0x%p",
             sFocusedPresContext.get(),
             ToString(RefPtr<Element>(sFocusedElement)).c_str(),
             sActiveIMEContentObserver.get()));
    return;
  }

  if (IsIMEObserverNeeded(newState)) {
    MOZ_ASSERT(sFocusedPresContext == presContext);
    MOZ_ASSERT(sFocusedElement == focusedElement);
    CreateIMEContentObserver(aHTMLEditor, focusedElement);
  }
}

// static
bool IMEStateManager::CanHandleWith(const nsPresContext* aPresContext) {
  return aPresContext && aPresContext->GetPresShell() &&
         !aPresContext->PresShell()->IsDestroying();
}

// static
nsresult IMEStateManager::OnChangeFocus(nsPresContext* aPresContext,
                                        Element* aElement,
                                        InputContextAction::Cause aCause) {
  MOZ_LOG(sISMLog, LogLevel::Info,
          ("OnChangeFocus(aPresContext=0x%p, aElement=0x%p, aCause=%s)",
           aPresContext, aElement, ToString(aCause).c_str()));

  InputContextAction action(aCause);
  return OnChangeFocusInternal(aPresContext, aElement, action);
}

// static
nsresult IMEStateManager::OnChangeFocusInternal(nsPresContext* aPresContext,
                                                Element* aElement,
                                                InputContextAction aAction) {
  NS_ASSERTION(!aElement || aElement->GetPresContext(
                                Element::PresContextFor::eForComposedDoc) ==
                                aPresContext,
               "aPresContext does not match with one of aElement");

  bool remoteHasFocus = EventStateManager::IsRemoteTarget(aElement);
  // If we've handled focused content, we were inactive but now active,
  // a remote process has focus, and setting focus to same content in the main
  // process, it means that we're restoring focus without changing DOM focus
  // both in the main process and the remote process.
  const bool restoringContextForRemoteContent =
      XRE_IsParentProcess() && remoteHasFocus && !sIsActive && aPresContext &&
      sFocusedPresContext && sFocusedElement &&
      sFocusedPresContext.get() == aPresContext &&
      sFocusedElement.get() == aElement &&
      aAction.mFocusChange != InputContextAction::MENU_GOT_PSEUDO_FOCUS;

  if (MOZ_LOG_TEST(sISMLog, LogLevel::Info)) {
    MOZ_LOG_FMT(sISMLog, LogLevel::Info, "");
    MOZ_LOG_FMT(sISMLog, LogLevel::Info, "========");
    MOZ_LOG_FMT(
        sISMLog, LogLevel::Info,
        "OnChangeFocusInternal(\n"
        "  aPresContext={} (available: {}),\n"
        "  aElement={} (remote: {}),\n"
        "  aAction={{ mCause={}, mFocusChange={} }}),\n"
        "  sFocusedPresContext={} (available: {}),\n"
        "  sFocusedElement={},\n"
        "  sTextInputHandlingWidget={} (available: {}),\n"
        "  BrowserParent::GetFocused()={},\n"
        "  sActiveIMEContentObserver={},\n"
        "  sInstalledMenuKeyboardListener={},\n"
        "  sIsActive={},\n"
        "  restoringContextForRemoteContent={}",
        static_cast<void*>(aPresContext),
        TrueOrFalse(CanHandleWith(aPresContext)), RefPtr{aElement},
        TrueOrFalse(remoteHasFocus), ToString(aAction.mCause),
        ToString(aAction.mFocusChange), static_cast<void*>(sFocusedPresContext),
        TrueOrFalse(CanHandleWith(sFocusedPresContext)), sFocusedElement,
        static_cast<void*>(sTextInputHandlingWidget),
        TrueOrFalse(sTextInputHandlingWidget &&
                    !sTextInputHandlingWidget->Destroyed()),
        static_cast<void*>(BrowserParent::GetFocused()),
        static_cast<void*>(sActiveIMEContentObserver),
        TrueOrFalse(sInstalledMenuKeyboardListener), TrueOrFalse(sIsActive),
        TrueOrFalse(restoringContextForRemoteContent));
  }
  const auto LogEnd = MakeScopeExit([]() {
    if (MOZ_LOG_TEST(sISMLog, LogLevel::Info)) {
      MOZ_LOG_FMT(sISMLog, LogLevel::Info, "========");
      MOZ_LOG_FMT(sISMLog, LogLevel::Info, "");
    }
  });

  sIsActive = !!aPresContext;
  if (sPendingFocusedBrowserSwitchingData.isSome()) {
    MOZ_ASSERT(XRE_IsParentProcess());
    const uint32_t originalGeneration = sFocusGeneration;

    const RefPtr<Element> focusedElement = sFocusedElement;
    const RefPtr<nsPresContext> focusedPresContext = sFocusedPresContext;
    const RefPtr<BrowserParent> browserParentBlurred =
        sPendingFocusedBrowserSwitchingData.ref().mBrowserParentBlurred;
    const RefPtr<BrowserParent> browserParentFocused =
        sPendingFocusedBrowserSwitchingData.ref().mBrowserParentFocused;
    OnFocusMovedBetweenBrowsers(browserParentBlurred, browserParentFocused);
    // If another call of this method happens during the
    // OnFocusMovedBetweenBrowsers call, we shouldn't take back focus to
    // the old one.
    if (originalGeneration != sFocusGeneration) {
      MOZ_LOG_FMT(
          sISMLog, LogLevel::Debug,
          "  OnChangeFocusInternal() stoped handling it because the focused "
          "content was changed (\n"
          "    sFocusedPresContext=         {}\n"
          "    previous sFocusedPresContext={}\n"
          "    sFocusedElement=         {}\n"
          "    previous sFocusedElement={}\n"
          "  )",
          static_cast<void*>(sFocusedPresContext.get()),
          static_cast<void*>(focusedPresContext.get()), sFocusedElement,
          focusedElement);
      return NS_OK;
    }
  }

  // If new aPresShell has been destroyed, this should handle the focus change
  // as nobody is getting focus.
  MOZ_ASSERT_IF(!aPresContext, !aElement);
  if (NS_WARN_IF(aPresContext && !CanHandleWith(aPresContext))) {
    MOZ_LOG(sISMLog, LogLevel::Warning,
            ("  OnChangeFocusInternal(), called with destroyed PresShell, "
             "handling this call as nobody getting focus"));
    aPresContext = nullptr;
    aElement = nullptr;
  } else if (!aPresContext) {
    aElement = nullptr;
  }

  const nsCOMPtr<nsIWidget> oldWidget = sTextInputHandlingWidget;
  const nsCOMPtr<nsIWidget> newWidget =
      aPresContext ? aPresContext->GetTextInputHandlingWidget() : nullptr;
  const bool focusActuallyChanging =
      (sFocusedElement != aElement || sFocusedPresContext != aPresContext ||
       oldWidget != newWidget ||
       (remoteHasFocus && !restoringContextForRemoteContent &&
        (aAction.mFocusChange != InputContextAction::MENU_GOT_PSEUDO_FOCUS)));

  // If old widget has composition, we may need to commit composition since
  // a native IME context is shared on all editors on some widgets or all
  // widgets (it depends on platforms).
  if (oldWidget && focusActuallyChanging && sTextCompositions) {
    RefPtr<TextComposition> composition =
        sTextCompositions->GetCompositionFor(oldWidget);
    if (composition) {
      // However, don't commit the composition if we're being inactivated
      // but the composition should be kept even during deactive.
      // Note that oldWidget and sFocusedIMEWidget may be different here (in
      // such case, sFocusedIMEWidget is perhaps nullptr).  For example, IME
      // may receive only blur notification but still has composition.
      // We need to clean up only the oldWidget's composition state here.
      if (aPresContext || !oldWidget->IMENotificationRequestsRef().contains(
                              IMENotificationRequest::NotifyDuringInactive)) {
        MOZ_LOG(
            sISMLog, LogLevel::Info,
            ("  OnChangeFocusInternal(), requesting to commit composition to "
             "the (previous) focused widget"));
        const uint32_t originalGeneration = sFocusGeneration;

        const RefPtr<Element> focusedElement = sFocusedElement;
        const RefPtr<nsPresContext> focusedPresContext = sFocusedPresContext;
        NotifyIME(REQUEST_TO_COMMIT_COMPOSITION, oldWidget,
                  composition->GetBrowserParent());
        if (originalGeneration != sFocusGeneration) {
          MOZ_LOG_FMT(sISMLog, LogLevel::Info,
                      "  OnChangeFocusInternal(), abort handling the focus "
                      "change because focus has been moved during requesting "
                      "to commit composition (\n"
                      "    sFocusedPresContext=         {}\n"
                      "    previous sFocusedPresContext={}\n"
                      "    sFocusedElement=         {}\n"
                      "    previous sFocusedElement={}\n"
                      "  )",
                      static_cast<void*>(sFocusedPresContext.get()),
                      static_cast<void*>(focusedPresContext.get()),
                      sFocusedElement, focusedElement);
          return NS_OK;
        }
      }
    }
  }

  if (sActiveIMEContentObserver) {
    MOZ_ASSERT(!remoteHasFocus || XRE_IsContentProcess(),
               "IMEContentObserver should have been destroyed by "
               "OnFocusMovedBetweenBrowsers.");
    if (!aPresContext) {
      if (!sActiveIMEContentObserver->KeepAliveDuringDeactive()) {
        DestroyIMEContentObserver();
      }
    }
    // Otherwise, i.e., new focused content is in this process, let's check
    // whether the editable content for aElement has already been being observed
    // by the active IMEContentObserver.  If not, let's stop observing the
    // editable content which is being blurred.
    else if (!sActiveIMEContentObserver->IsObserving(*aPresContext, aElement)) {
      DestroyIMEContentObserver();
    }
  }

  if (!aPresContext) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  OnChangeFocusInternal(), no nsPresContext is being activated"));
    return NS_OK;
  }

  if (NS_WARN_IF(!newWidget)) {
    MOZ_LOG(sISMLog, LogLevel::Error,
            ("  OnChangeFocusInternal(), FAILED due to no widget to manage its "
             "IME state"));
    return NS_OK;
  }

  // Update the cached widget since root view of the presContext may be
  // changed to different view.
  sTextInputHandlingWidget = newWidget;

  // If a child process has focus, we should disable IME state until the child
  // process actually gets focus because if user types keys before that they
  // are handled by IME.
  IMEState newState = remoteHasFocus ? IMEState(IMEEnabled::Disabled)
                                     : GetNewIMEState(*aPresContext, aElement);
  bool setIMEState = true;

  const auto CanSkipSettingContext = [&](const InputContext& aOldContext) {
    const auto IsChangingBrowsingMode = [&]() {
      const bool willBeInPrivateBrowsingMode =
          aPresContext && aPresContext->Document() &&
          aPresContext->Document()->IsInPrivateBrowsing();
      return willBeInPrivateBrowsingMode != aOldContext.mInPrivateBrowsing;
    };
    const auto IsChangingURI = [&]() {
      const nsCOMPtr<nsIURI> newURI =
          IMEStateManager::GetExposableURL(aPresContext);
      if (!newURI != !aOldContext.mURI) {
        return true;  // One is not exposable URI.
      }
      if (!newURI) {
        MOZ_ASSERT(!aOldContext.mURI);
        return false;  // Moved in non-exposable URIs.
      }
      bool same = false;
      return NS_FAILED(newURI->Equals(aOldContext.mURI, &same)) || !same;
    };
    return !IsChangingBrowsingMode() && !IsChangingURI();
  };

  if (remoteHasFocus && XRE_IsParentProcess()) {
    if (aAction.mFocusChange == InputContextAction::MENU_GOT_PSEUDO_FOCUS) {
      // If menu keyboard listener is installed, we need to disable IME now.
      setIMEState = true;
    } else if (aAction.mFocusChange ==
               InputContextAction::MENU_LOST_PSEUDO_FOCUS) {
      // If menu keyboard listener is uninstalled, we need to restore
      // input context which was set by the remote process.  However, if
      // the remote process hasn't been set input context yet, we need to
      // wait next SetInputContextForChildProcess() call.
      if (HasActiveChildSetInputContext()) {
        setIMEState = true;
        newState = sActiveChildInputContext.mIMEState;
      } else {
        setIMEState = false;
      }
    } else if (focusActuallyChanging) {
      InputContext context = newWidget->GetInputContext();
      if (context.mIMEState.mEnabled == IMEEnabled::Disabled &&
          context.mOrigin == InputContext::ORIGIN_CONTENT &&
          CanSkipSettingContext(context)) {
        setIMEState = false;
        MOZ_LOG(sISMLog, LogLevel::Debug,
                ("  OnChangeFocusInternal(), doesn't set IME state because "
                 "focused element (or document) is in a child process and the "
                 "IME state is already disabled by a remote process"));
      } else {
        // When new remote process gets focus, we should forget input context
        // coming from old focused remote process.
        ResetActiveChildInputContext();
        MOZ_LOG(sISMLog, LogLevel::Debug,
                ("  OnChangeFocusInternal(), will disable IME until new "
                 "focused element (or document) in the child process will get "
                 "focus actually"));
      }
    } else if (newWidget->GetInputContext().mOrigin !=
                   InputContext::ORIGIN_MAIN &&
               CanSkipSettingContext(newWidget->GetInputContext())) {
      // When focus is NOT changed actually, we shouldn't set IME state if
      // current input context was set by a remote process since that means
      // that the window is being activated and the child process may have
      // composition.  Then, we shouldn't commit the composition with making
      // IME state disabled.
      setIMEState = false;
      MOZ_LOG(
          sISMLog, LogLevel::Debug,
          ("  OnChangeFocusInternal(), doesn't set IME state because focused "
           "element (or document) is already in the child process"));
    }
  } else {
    // When this process gets focus, we should forget input context coming
    // from remote process.
    ResetActiveChildInputContext();
  }

  if (setIMEState) {
    if (!focusActuallyChanging) {
      // actual focus isn't changing, but if IME enabled state is changing,
      // we should do it.
      InputContext context = newWidget->GetInputContext();
      if (context.mIMEState.mEnabled == newState.mEnabled &&
          CanSkipSettingContext(context)) {
        MOZ_LOG(sISMLog, LogLevel::Debug,
                ("  OnChangeFocusInternal(), neither focus nor IME state is "
                 "changing"));
        return NS_OK;
      }
      aAction.mFocusChange = InputContextAction::FOCUS_NOT_CHANGED;

      // Even if focus isn't changing actually, we should commit current
      // composition here since the IME state is changing.
      if (sFocusedPresContext && oldWidget) {
        // FIXME: During committing composition, focused element may be changed.
        // In such case, we should not set sFocusedPresContext nor
        // sFocusedElement below.
        NotifyIME(REQUEST_TO_COMMIT_COMPOSITION, oldWidget,
                  sFocusedIMEBrowserParent);
      }
    } else if (aAction.mFocusChange == InputContextAction::FOCUS_NOT_CHANGED) {
      // If aElement isn't null or aElement is null but editable, somebody gets
      // focus.
      bool gotFocus = aElement || (newState.mEnabled == IMEEnabled::Enabled);
      aAction.mFocusChange = gotFocus ? InputContextAction::GOT_FOCUS
                                      : InputContextAction::LOST_FOCUS;
    }

    // FIXME: If we're in the parent process, the following calls may cause
    // calling the OS API. Then, anything could happen. So, the focus may have
    // been changed something other. If so, shouldn't set sFocusedPresContext
    // nor sFocusedElement below anymore.
    if (remoteHasFocus && HasActiveChildSetInputContext() &&
        aAction.mFocusChange == InputContextAction::MENU_LOST_PSEUDO_FOCUS) {
      // Restore the input context in the active remote process when
      // menu keyboard listener is uninstalled and active remote tab has
      // focus.
      SetInputContext(*newWidget, sActiveChildInputContext, aAction);
    } else {
      // Update IME state for new focus widget
      SetIMEState(newState, aPresContext, aElement, *newWidget, aAction,
                  remoteHasFocus ? InputContext::ORIGIN_CONTENT : sOrigin);
    }
  }

  sFocusedPresContext = aPresContext;
  sFocusedElement = aElement;
  AdvanceFocusGeneration();

  // Don't call CreateIMEContentObserver() here, we'll call it in
  // UpdateIMEState() after the focused editor is initialized and the focus is
  // not changed during the initialization.

  MOZ_LOG_FMT(sISMLog, LogLevel::Debug,
              "  OnChangeFocusInternal(), modified IME state for "
              "sFocusedPresContext={}, sFocusedElement={}",
              static_cast<void*>(sFocusedPresContext.get()), sFocusedElement);

  return NS_OK;
}

// static
void IMEStateManager::OnInstalledMenuKeyboardListener(bool aInstalling) {
  MOZ_LOG_FMT(
      sISMLog, LogLevel::Info,
      "OnInstalledMenuKeyboardListener(aInstalling={}), "
      "nsContentUtils::IsSafeToRunScript()={}, "
      "sInstalledMenuKeyboardListener={}, BrowserParent::GetFocused()={}, "
      "sActiveChildInputContext={},\n"
      "sFocusedPresContext={},\n"
      "sFocusedElement={},\n"
      "sPseudoFocusChangeRunnable={}",
      TrueOrFalse(aInstalling),
      TrueOrFalse(nsContentUtils::IsSafeToRunScript()),
      TrueOrFalse(sInstalledMenuKeyboardListener),
      static_cast<void*>(BrowserParent::GetFocused()),
      ToString(sActiveChildInputContext).c_str(),
      static_cast<void*>(sFocusedPresContext), sFocusedElement,
      static_cast<void*>(sPseudoFocusChangeRunnable));

  // Update the state whether the menubar has pseudo focus or not immediately.
  // This will be referred by the runner which is created below.
  sInstalledMenuKeyboardListener = aInstalling;
  // However, this may be called when it's not safe to run script.  Therefore,
  // we need to create a runnable to notify IME of the pseudo focus change.
  if (sPseudoFocusChangeRunnable) {
    return;
  }
  sPseudoFocusChangeRunnable = new PseudoFocusChangeRunnable(aInstalling);
  nsContentUtils::AddScriptRunner(sPseudoFocusChangeRunnable);
}

// static
void IMEStateManager::SetMenubarPseudoFocus(
    PseudoFocusChangeRunnable* aCaller, bool aSetPseudoFocus,
    nsPresContext* aFocusedPresContextAtRequested) {
  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("SetMenubarPseudoFocus(aCaller=0x%p, aSetPseudoFocus=%s, "
       "aFocusedPresContextAtRequested=0x%p), "
       "sInstalledMenuKeyboardListener=%s, sFocusedPresContext=0x%p, "
       "sFocusedElement=0x%p, sPseudoFocusChangeRunnable=0x%p",
       aCaller, TrueOrFalse(aSetPseudoFocus), aFocusedPresContextAtRequested,
       TrueOrFalse(sInstalledMenuKeyboardListener), sFocusedPresContext.get(),
       sFocusedElement.get(), sPseudoFocusChangeRunnable.get()));

  MOZ_ASSERT(sPseudoFocusChangeRunnable.get() == aCaller);

  // Clear the runnable first for nested call of
  // OnInstalledMenuKeyboardListener().
  RefPtr<PseudoFocusChangeRunnable> runningOne =
      sPseudoFocusChangeRunnable.forget();
  MOZ_ASSERT(!sPseudoFocusChangeRunnable);

  // If the requested state is still same, let's make the menubar get
  // pseudo focus with the latest focused PresContext and Element.
  // Note that this is no problem even after the focused element is changed
  // after aCaller is created because only sInstalledMenuKeyboardListener
  // manages the state and current focused PresContext and element should be
  // used only for restoring the focus from the menubar.  So, restoring
  // focus with the lasted one does make sense.
  if (sInstalledMenuKeyboardListener == aSetPseudoFocus) {
    InputContextAction action(InputContextAction::CAUSE_UNKNOWN,
                              aSetPseudoFocus
                                  ? InputContextAction::MENU_GOT_PSEUDO_FOCUS
                                  : InputContextAction::MENU_LOST_PSEUDO_FOCUS);
    RefPtr<nsPresContext> focusedPresContext = sFocusedPresContext;
    RefPtr<Element> focusedElement = sFocusedElement;
    OnChangeFocusInternal(focusedPresContext, focusedElement, action);
    return;
  }

  // If the requested state is different from current state, we don't need to
  // make the menubar get and lose pseudo focus because of redundant. However,
  // if there is a composition on the original focused element, we need to
  // commit it because pseudo focus move should've caused it.
  if (!aFocusedPresContextAtRequested) {
    return;
  }
  RefPtr<TextComposition> composition =
      GetTextCompositionFor(aFocusedPresContextAtRequested);
  if (!composition) {
    return;
  }
  if (nsCOMPtr<nsIWidget> widget =
          aFocusedPresContextAtRequested->GetTextInputHandlingWidget()) {
    composition->RequestToCommit(widget, false);
  }
}

// static
bool IMEStateManager::OnMouseButtonEventInEditor(
    nsPresContext& aPresContext, Element* aElement,
    WidgetMouseEvent& aMouseEvent) {
  MOZ_LOG(sISMLog, LogLevel::Info,
          ("OnMouseButtonEventInEditor(aPresContext=0x%p (available: %s), "
           "aElement=0x%p, aMouseEvent=0x%p), sFocusedPresContext=0x%p, "
           "sFocusedElement=0x%p",
           &aPresContext, TrueOrFalse(CanHandleWith(&aPresContext)), aElement,
           &aMouseEvent, sFocusedPresContext.get(), sFocusedElement.get()));

  if (sFocusedPresContext != &aPresContext || sFocusedElement != aElement) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  OnMouseButtonEventInEditor(), "
             "the mouse event isn't fired on the editor managed by ISM"));
    return false;
  }

  if (!sActiveIMEContentObserver) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  OnMouseButtonEventInEditor(), "
             "there is no active IMEContentObserver"));
    return false;
  }

  if (!sActiveIMEContentObserver->IsObserving(aPresContext, aElement)) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  OnMouseButtonEventInEditor(), "
             "the active IMEContentObserver isn't managing the editor"));
    return false;
  }

  OwningNonNull<IMEContentObserver> observer = *sActiveIMEContentObserver;
  bool consumed = observer->OnMouseButtonEvent(aPresContext, aMouseEvent);
  MOZ_LOG(sISMLog, LogLevel::Info,
          ("  OnMouseButtonEventInEditor(), "
           "mouse event (mMessage=%s, mButton=%d) is %s",
           ToChar(aMouseEvent.mMessage), aMouseEvent.mButton,
           consumed ? "consumed" : "not consumed"));
  return consumed;
}

// static
void IMEStateManager::OnClickInEditor(nsPresContext& aPresContext,
                                      Element* aElement,
                                      const WidgetMouseEvent& aMouseEvent) {
  MOZ_LOG(sISMLog, LogLevel::Info,
          ("OnClickInEditor(aPresContext=0x%p (available: %s), aElement=0x%p, "
           "aMouseEvent=0x%p), sFocusedPresContext=0x%p, sFocusedElement=0x%p, "
           "sTextInputHandlingWidget=0x%p (available: %s)",
           &aPresContext, TrueOrFalse(CanHandleWith(&aPresContext)), aElement,
           &aMouseEvent, sFocusedPresContext.get(), sFocusedElement.get(),
           sTextInputHandlingWidget,
           TrueOrFalse(sTextInputHandlingWidget &&
                       !sTextInputHandlingWidget->Destroyed())));

  if (sFocusedPresContext != &aPresContext || sFocusedElement != aElement ||
      NS_WARN_IF(!sFocusedPresContext) ||
      NS_WARN_IF(!sTextInputHandlingWidget) ||
      NS_WARN_IF(sTextInputHandlingWidget->Destroyed())) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  OnClickInEditor(), "
             "the mouse event isn't fired on the editor managed by ISM"));
    return;
  }

  const OwningNonNull<nsIWidget> textInputHandlingWidget =
      *sTextInputHandlingWidget;
  MOZ_ASSERT_IF(sFocusedPresContext->GetTextInputHandlingWidget(),
                sFocusedPresContext->GetTextInputHandlingWidget() ==
                    textInputHandlingWidget.get());

  if (!aMouseEvent.IsTrusted()) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  OnClickInEditor(), "
             "the mouse event isn't a trusted event"));
    return;  // ignore untrusted event.
  }

  if (aMouseEvent.mButton) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  OnClickInEditor(), "
             "the mouse event isn't a left mouse button event"));
    return;  // not a left click event.
  }

  if (aMouseEvent.mClickCount != 1) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  OnClickInEditor(), "
             "the mouse event isn't a single click event"));
    return;  // should notify only first click event.
  }

  MOZ_ASSERT_IF(aElement, !EventStateManager::IsRemoteTarget(aElement));
  InputContextAction::Cause cause =
      aMouseEvent.mInputSource == MouseEvent_Binding::MOZ_SOURCE_TOUCH
          ? InputContextAction::CAUSE_TOUCH
          : InputContextAction::CAUSE_MOUSE;

  InputContextAction action(cause, InputContextAction::FOCUS_NOT_CHANGED);
  IMEState newState = GetNewIMEState(aPresContext, aElement);
  // If the state is not editable, there should be no active IMEContentObserver.
  // However, if this click sets focus to the editor, IMEContentObserver may
  // have not been created yet.  Instead, if there is active IMEContentObserver,
  // it should be editable.
  MOZ_ASSERT_IF(!newState.IsEditable(), !sActiveIMEContentObserver);
  MOZ_ASSERT_IF(sActiveIMEContentObserver, newState.IsEditable());
  SetIMEState(newState, &aPresContext, aElement, textInputHandlingWidget,
              action, sOrigin);
}

// static
Element* IMEStateManager::GetFocusedElement() { return sFocusedElement; }

// static
bool IMEStateManager::IsFocusedElement(const nsPresContext& aPresContext,
                                       const Element* aFocusedElement) {
  if (!sFocusedPresContext || &aPresContext != sFocusedPresContext) {
    return false;
  }

  if (sFocusedElement == aFocusedElement) {
    return true;
  }

  // If sFocusedElement is not nullptr, but aFocusedElement is nullptr, it does
  // not have focus from point of view of IMEStateManager.
  if (sFocusedElement) {
    return false;
  }

  // If the caller does not think that nobody has focus, but we know there is
  // a focused content, the caller must be called with wrong content.
  if (!aFocusedElement) {
    return false;
  }

  // If the aFocusedElement is in design mode, sFocusedElement may be nullptr.
  if (aFocusedElement->IsInDesignMode()) {
    MOZ_ASSERT(&aPresContext == sFocusedPresContext && !sFocusedElement);
    return true;
  }

  // Otherwise, only when aFocusedElement is the root element, it can have
  // focus, but IMEStateManager::OnChangeFocus is called with nullptr for
  // aFocusedElement if it was not editable.
  // XXX In this case, should the caller update sFocusedElement?
  return aFocusedElement->IsEditable() && sFocusedPresContext->Document() &&
         sFocusedPresContext->Document()->GetRootElement() == aFocusedElement;
}

// static
void IMEStateManager::OnFocusInEditor(nsPresContext& aPresContext,
                                      Element* aElement,
                                      EditorBase& aEditorBase) {
  MOZ_LOG_FMT(sISMLog, LogLevel::Info,
              "OnFocusInEditor(\n"
              "  aPresContext={} (available: {}),\n"
              "  aElement={},\n"
              "  aEditorBase={}),\n"
              "  sFocusedPresContext={},\n"
              "  sFocusedElement={},\n"
              "  sActiveIMEContentObserver={}",
              static_cast<void*>(&aPresContext),
              TrueOrFalse(CanHandleWith(&aPresContext)), RefPtr{aElement},
              static_cast<void*>(&aEditorBase),
              static_cast<void*>(sFocusedPresContext.get()), sFocusedElement,
              static_cast<void*>(sActiveIMEContentObserver.get()));
  if (!IsFocusedElement(aPresContext, aElement)) {
    MOZ_LOG_FMT(sISMLog, LogLevel::Debug,
                "  OnFocusInEditor(), an editor not managed by ISM gets focus");
    return;
  }
  MOZ_ASSERT(sTextInputHandlingWidget);

  // If the IMEContentObserver instance isn't observing the editable content for
  // aElement, we need to recreate the instance because the active observer is
  // observing different content even if aElement keeps being focused.  E.g.,
  // it's an <input> and editing host but was not a text control, but now, it's
  // a text control.
  if (sActiveIMEContentObserver) {
    if (sActiveIMEContentObserver->IsObserving(aPresContext, aElement)) {
      MOZ_LOG_FMT(sISMLog, LogLevel::Debug,
                  "  OnFocusInEditor(), "
                  "the editable content for aEditorBase has already been being "
                  "observed by sActiveIMEContentObserver");
      return;
    }
    // If the IMEContentObserver has not finished initializing itself yet,
    // we don't need to recreate it because the following
    // TryToFlushPendingNotifications call must make it initialized.
    const nsCOMPtr<nsIWidget> textInputHandlingWidget =
        sTextInputHandlingWidget;
    if (!sActiveIMEContentObserver->IsBeingInitializedFor(
            aPresContext, aElement, aEditorBase)) {
      DestroyIMEContentObserver();
    }
    if (NS_WARN_IF(!IsFocusedElement(aPresContext, aElement)) ||
        NS_WARN_IF(!sTextInputHandlingWidget) ||
        NS_WARN_IF(sTextInputHandlingWidget != textInputHandlingWidget)) {
      MOZ_LOG_FMT(sISMLog, LogLevel::Error,
                  "  OnFocusInEditor(), detected unexpected focus change with "
                  "re-initializing active IMEContentObserver");
      return;
    }
  }

  if (!sActiveIMEContentObserver && sTextInputHandlingWidget &&
      IsIMEObserverNeeded(
          sTextInputHandlingWidget->GetInputContext().mIMEState)) {
    CreateIMEContentObserver(aEditorBase, aElement);
    if (sActiveIMEContentObserver) {
      MOZ_LOG_FMT(sISMLog, LogLevel::Debug,
                  "  OnFocusInEditor(), new IMEContentObserver is created ({})",
                  static_cast<void*>(sActiveIMEContentObserver.get()));
    }
  }

  if (sActiveIMEContentObserver) {
    sActiveIMEContentObserver->TryToFlushPendingNotifications(false);
    MOZ_LOG_FMT(sISMLog, LogLevel::Debug,
                "  OnFocusInEditor(), trying to send pending notifications in "
                "the active IMEContentObserver ({})...",
                static_cast<void*>(sActiveIMEContentObserver.get()));
  }
}

// static
void IMEStateManager::OnEditorInitialized(EditorBase& aEditorBase) {
  if (!sActiveIMEContentObserver ||
      !sActiveIMEContentObserver->WasInitializedWith(aEditorBase)) {
    return;
  }

  MOZ_LOG(sISMLog, LogLevel::Info,
          ("OnEditorInitialized(aEditorBase=0x%p)", &aEditorBase));

  sActiveIMEContentObserver->UnsuppressNotifyingIME();
}

// static
void IMEStateManager::OnEditorDestroying(EditorBase& aEditorBase) {
  if (!sActiveIMEContentObserver ||
      !sActiveIMEContentObserver->WasInitializedWith(aEditorBase)) {
    return;
  }

  MOZ_LOG(sISMLog, LogLevel::Info,
          ("OnEditorDestroying(aEditorBase=0x%p)", &aEditorBase));

  // The IMEContentObserver shouldn't notify IME of anything until reframing
  // is finished.
  sActiveIMEContentObserver->SuppressNotifyingIME();
}

void IMEStateManager::OnReFocus(nsPresContext& aPresContext,
                                Element& aElement) {
  MOZ_LOG_FMT(
      sISMLog, LogLevel::Info,
      "OnReFocus(\n"
      "  aPresContext={} (available: {}),\n"
      "  aElement={}),\n"
      "  sActiveIMEContentObserver={},\n"
      "  sFocusedElement={}",
      static_cast<void*>(&aPresContext),
      TrueOrFalse(CanHandleWith(&aPresContext)), OwningNonNull(aElement),
      static_cast<void*>(sActiveIMEContentObserver.get()), sFocusedElement);

  if (NS_WARN_IF(!sTextInputHandlingWidget) ||
      NS_WARN_IF(sTextInputHandlingWidget->Destroyed())) {
    return;
  }

  // Check if IME has focus.  If and only if IME has focus, we may need to
  // update IME state of the widget to re-open VKB.  Otherwise, IME will open
  // VKB at getting focus.
  if (!sActiveIMEContentObserver ||
      !sActiveIMEContentObserver->IsObserving(aPresContext, &aElement)) {
    MOZ_LOG_FMT(sISMLog, LogLevel::Debug,
                "  OnReFocus(), editable content for aElement was not being "
                "observed by the sActiveIMEContentObserver");
    return;
  }

  MOZ_ASSERT(&aElement == sFocusedElement.get());

  if (!UserActivation::IsHandlingUserInput() ||
      UserActivation::IsHandlingKeyboardInput()) {
    return;
  }

  const OwningNonNull<nsIWidget> textInputHandlingWidget =
      *sTextInputHandlingWidget;

  // Don't update IME state during composition.
  if (sTextCompositions) {
    if (const TextComposition* composition =
            sTextCompositions->GetCompositionFor(textInputHandlingWidget)) {
      if (composition->IsComposing()) {
        return;
      }
    }
  }

  const InputContextAction action(InputContextAction::CAUSE_UNKNOWN,
                                  InputContextAction::FOCUS_NOT_CHANGED);
  const IMEState newState = GetNewIMEState(aPresContext, &aElement);
  // If aElement has not had a primary frame for it,
  if (MOZ_UNLIKELY(!newState.IsEditable())) {
    if (sActiveIMEContentObserver->EditorIsTextEditor()) {
      TextControlElement* const textControlElement =
          TextControlElement::FromNode(aElement);
      MOZ_ASSERT(textControlElement);
      if (textControlElement &&
          textControlElement->IsSingleLineTextControlOrTextArea()) {
        MOZ_LOG_FMT(
            sISMLog, LogLevel::Warning,
            "  OnReFocus(), Temporarily disabling IME for the focused element "
            "because probably the TextControlState could not return "
            "TextEditor (textEditor: {})",
            static_cast<void*>(textControlElement->GetTextControlState()
                                   ->GetExtantTextEditor()));
      }
    } else {
      HTMLEditor* const htmlEditor =
          nsContentUtils::GetHTMLEditor(&aPresContext);
#ifdef DEBUG
      MOZ_ASSERT(htmlEditor);
      Result<IMEState, nsresult> stateOrError =
          htmlEditor->GetPreferredIMEState();
      MOZ_ASSERT(stateOrError.isOk());
      MOZ_ASSERT(!stateOrError.inspect().IsEditable());
#endif  // #ifdef DEBUG
      MOZ_LOG_FMT(sISMLog, LogLevel::Warning,
                  "  OnReFocus(), Disabling IME for the focused element, "
                  "HTMLEditor={} {{ IsReadonly()={} }}",
                  static_cast<void*>(htmlEditor),
                  htmlEditor ? TrueOrFalse(htmlEditor->IsReadonly()) : "N/A");
    }
  }
  SetIMEState(newState, &aPresContext, &aElement, textInputHandlingWidget,
              action, sOrigin);
}

// static
void IMEStateManager::MaybeOnEditableStateDisabled(nsPresContext& aPresContext,
                                                   dom::Element* aElement) {
  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("MaybeOnEditableStateDisabled(aPresContext=0x%p, aElement=0x%p), "
       "sFocusedPresContext=0x%p (available: %s), "
       "sFocusedElement=0x%p, sTextInputHandlingWidget=0x%p (available: %s), "
       "sActiveIMEContentObserver=0x%p, sIsGettingNewIMEState=%s",
       &aPresContext, aElement, sFocusedPresContext.get(),
       TrueOrFalse(CanHandleWith(sFocusedPresContext)), sFocusedElement.get(),
       sTextInputHandlingWidget,
       TrueOrFalse(sTextInputHandlingWidget &&
                   !sTextInputHandlingWidget->Destroyed()),
       sActiveIMEContentObserver.get(), TrueOrFalse(sIsGettingNewIMEState)));

  if (sIsGettingNewIMEState) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  MaybeOnEditableStateDisabled(), "
             "does nothing because of called while getting new IME state"));
    return;
  }

  if (&aPresContext != sFocusedPresContext || aElement != sFocusedElement) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  MaybeOnEditableStateDisabled(), "
             "does nothing because of another element already has focus"));
    return;
  }

  if (NS_WARN_IF(!sTextInputHandlingWidget) ||
      NS_WARN_IF(sTextInputHandlingWidget->Destroyed())) {
    MOZ_LOG(sISMLog, LogLevel::Error,
            ("  MaybeOnEditableStateDisabled(), FAILED due to "
             "the widget for the managing the nsPresContext has gone"));
    return;
  }

  const OwningNonNull<nsIWidget> textInputHandlingWidget =
      *sTextInputHandlingWidget;
  MOZ_ASSERT_IF(sFocusedPresContext->GetTextInputHandlingWidget(),
                sFocusedPresContext->GetTextInputHandlingWidget() ==
                    textInputHandlingWidget.get());

  const IMEState newIMEState = GetNewIMEState(aPresContext, aElement);
  // If IME state becomes editable, HTMLEditor should also be initialized with
  // same path as it gets focus.  Therefore, IMEStateManager::UpdateIMEState
  // should be called by HTMLEditor instead.
  if (newIMEState.IsEditable()) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  MaybeOnEditableStateDisabled(), "
             "does nothing because IME state does not become disabled"));
    return;
  }

  // Otherwise, disable IME on the widget and destroy active IMEContentObserver
  // if there is.
  const InputContext inputContext = textInputHandlingWidget->GetInputContext();
  if (inputContext.mIMEState.mEnabled == newIMEState.mEnabled) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  MaybeOnEditableStateDisabled(), "
             "does nothing because IME state is not changed"));
    return;
  }

  if (sActiveIMEContentObserver) {
    DestroyIMEContentObserver();
  }

  InputContextAction action(InputContextAction::CAUSE_UNKNOWN,
                            InputContextAction::FOCUS_NOT_CHANGED);
  SetIMEState(newIMEState, &aPresContext, aElement, textInputHandlingWidget,
              action, sOrigin);
}

// static
void IMEStateManager::UpdateIMEState(const IMEState& aNewIMEState,
                                     Element* aElement, EditorBase& aEditorBase,
                                     const UpdateIMEStateOptions& aOptions) {
  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("UpdateIMEState(aNewIMEState=%s, aElement=0x%p, aEditorBase=0x%p, "
       "aOptions=0x%0x), sFocusedPresContext=0x%p (available: %s), "
       "sFocusedElement=0x%p, sTextInputHandlingWidget=0x%p (available: %s), "
       "sActiveIMEContentObserver=0x%p, sIsGettingNewIMEState=%s",
       ToString(aNewIMEState).c_str(), aElement, &aEditorBase,
       aOptions.serialize(), sFocusedPresContext.get(),
       TrueOrFalse(CanHandleWith(sFocusedPresContext)), sFocusedElement.get(),
       sTextInputHandlingWidget,
       TrueOrFalse(sTextInputHandlingWidget &&
                   !sTextInputHandlingWidget->Destroyed()),
       sActiveIMEContentObserver.get(), TrueOrFalse(sIsGettingNewIMEState)));
  if (aElement) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  aElement:        %s", ToString(*aElement).c_str()));
  }
  if (sFocusedElement) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  sFocusedElement: %s", ToString(*sFocusedElement).c_str()));
  }

  if (sIsGettingNewIMEState) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  UpdateIMEState(), "
             "does nothing because of called while getting new IME state"));
    return;
  }

  RefPtr<PresShell> presShell(aEditorBase.GetPresShell());
  if (NS_WARN_IF(!presShell)) {
    MOZ_LOG(sISMLog, LogLevel::Error,
            ("  UpdateIMEState(), FAILED due to "
             "editor doesn't have PresShell"));
    return;
  }

  const RefPtr<nsPresContext> presContext =
      aElement
          ? aElement->GetPresContext(Element::PresContextFor::eForComposedDoc)
          : aEditorBase.GetPresContext();
  if (NS_WARN_IF(!presContext)) {
    MOZ_LOG(sISMLog, LogLevel::Error,
            ("  UpdateIMEState(), FAILED due to "
             "editor doesn't have PresContext"));
    return;
  }

  // IMEStateManager::UpdateIMEState() should be called after
  // IMEStateManager::OnChangeFocus() is called for setting focus to aElement
  // and aEditorBase.  However, when aEditorBase is an HTMLEditor, this may be
  // called by nsIEditor::PostCreate() before IMEStateManager::OnChangeFocus().
  // Similarly, when aEditorBase is a TextEditor, this may be called by
  // nsIEditor::SetFlags().  In such cases, this method should do nothing
  // because input context should be updated when
  // IMEStateManager::OnChangeFocus() is called later.
  if (sFocusedPresContext != presContext) {
    MOZ_LOG(sISMLog, LogLevel::Warning,
            ("  UpdateIMEState(), does nothing due to "
             "the editor hasn't managed by IMEStateManager yet"));
    return;
  }

  // If IMEStateManager doesn't manage any document, this cannot update IME
  // state of any widget.
  if (NS_WARN_IF(!sFocusedPresContext)) {
    MOZ_LOG(sISMLog, LogLevel::Error,
            ("  UpdateIMEState(), FAILED due to "
             "no managing nsPresContext"));
    return;
  }

  if (NS_WARN_IF(!sTextInputHandlingWidget) ||
      NS_WARN_IF(sTextInputHandlingWidget->Destroyed())) {
    MOZ_LOG(sISMLog, LogLevel::Error,
            ("  UpdateIMEState(), FAILED due to "
             "the widget for the managing nsPresContext has gone"));
    return;
  }

  const OwningNonNull<nsIWidget> textInputHandlingWidget =
      *sTextInputHandlingWidget;
  MOZ_ASSERT_IF(sFocusedPresContext->GetTextInputHandlingWidget(),
                sFocusedPresContext->GetTextInputHandlingWidget() ==
                    textInputHandlingWidget.get());

  // TODO: Investigate if we could put off to initialize IMEContentObserver
  //       later because a lot of callers need to be marked as
  //       MOZ_CAN_RUN_SCRIPT otherwise.

  // If there is an active observer and we need an observer, we want to keep
  // using the observer as far as possible because it's already notified IME of
  // focus.  However, between the call of OnChangeFocus() and UpdateIMEState(),
  // focus and/or IME state may have been changed.  If so, we need to update
  // the observer for current situation.
  const bool hasActiveObserverAndNeedObserver =
      sActiveIMEContentObserver && IsIMEObserverNeeded(aNewIMEState);

  // If the active observer was not initialized with aEditorBase, it means
  // that the old editor lost focus but new editor gets focus **without**
  // focus move in the DOM tree.  This may happen with changing the type of
  // <input> element, etc.  In this case, we need to let IME know a focus move
  // with recreating IMEContentObserver because editable target has been
  // changed.
  const bool needToRecreateObserver =
      hasActiveObserverAndNeedObserver &&
      !sActiveIMEContentObserver->WasInitializedWith(aEditorBase);

  // If the active observer was initialized with same editor, we can/should
  // keep using it for avoiding to notify IME of a focus change.  However, we
  // may need to re-initialize it because it may have temporarily stopped
  // observing the content.
  if (hasActiveObserverAndNeedObserver && !needToRecreateObserver) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  UpdateIMEState(), try to reinitialize the active "
             "IMEContentObserver"));
    if (!sActiveIMEContentObserver->MaybeReinitialize(
            textInputHandlingWidget, *sFocusedPresContext, aElement,
            aEditorBase)) [[unlikely]] {
      MOZ_LOG(sISMLog, LogLevel::Error,
              ("  UpdateIMEState(), failed to reinitialize the active "
               "IMEContentObserver"));
    }
    if (NS_WARN_IF(textInputHandlingWidget->Destroyed())) {
      MOZ_LOG(sISMLog, LogLevel::Error,
              ("  UpdateIMEState(), widget has gone during re-initializing "
               "the active IMEContentObserver"));
      return;
    }
  }

  // If there is no active IMEContentObserver or it isn't observing the
  // editable content for aElement, we should recreate an observer.  E.g.,
  // aElement which is an editing host may have been changed from/to a text
  // control.
  const bool createNewObserver =
      IsIMEObserverNeeded(aNewIMEState) &&
      (!sActiveIMEContentObserver || needToRecreateObserver ||
       !sActiveIMEContentObserver->IsObserving(*sFocusedPresContext, aElement));
  // If we're recreating new IMEContentObserver or new state does not need
  // IMEContentObserver, destroy the active one.
  const bool destroyCurrentObserver =
      sActiveIMEContentObserver &&
      (createNewObserver || !IsIMEObserverNeeded(aNewIMEState));

  // If IME state is changed, e.g., from "enabled" or "password" to "disabled",
  // we cannot keep the composing state because the new "disabled" state does
  // not support composition, and also from "enabled" to "password" and vice
  // versa, IME may use different composing rules.  Therefore, for avoiding
  // IME to be confused, we should fix the composition first.
  const bool updateIMEState =
      aOptions.contains(UpdateIMEStateOption::ForceUpdate) ||
      (textInputHandlingWidget->GetInputContext().mIMEState.mEnabled !=
       aNewIMEState.mEnabled);
  if (NS_WARN_IF(textInputHandlingWidget->Destroyed())) {
    MOZ_LOG(
        sISMLog, LogLevel::Error,
        ("  UpdateIMEState(), widget has gone during getting input context"));
    return;
  }

  if (updateIMEState &&
      !aOptions.contains(UpdateIMEStateOption::DontCommitComposition)) {
    NotifyIME(REQUEST_TO_COMMIT_COMPOSITION, textInputHandlingWidget,
              sFocusedIMEBrowserParent);
    if (NS_WARN_IF(textInputHandlingWidget->Destroyed())) {
      MOZ_LOG(sISMLog, LogLevel::Error,
              ("  UpdateIMEState(), widget has gone during committing "
               "composition"));
      return;
    }
    // FIXME: If committing composition changes IME state recursively, we should
    //        not keep updating IME state here.  However, how can we manage it?
    //        Is a generation of the state is required?
  }

  // Notify IME of blur first.
  if (destroyCurrentObserver) {
    DestroyIMEContentObserver();
    if (NS_WARN_IF(textInputHandlingWidget->Destroyed()) ||
        NS_WARN_IF(sTextInputHandlingWidget != textInputHandlingWidget)) {
      MOZ_LOG(sISMLog, LogLevel::Error,
              ("  UpdateIMEState(), has set input context, but the widget is "
               "not focused"));
      return;
    }
  }

  // Then, notify our IME handler of new IME state.
  if (updateIMEState) {
    InputContextAction action(InputContextAction::CAUSE_UNKNOWN,
                              InputContextAction::FOCUS_NOT_CHANGED);
    RefPtr<nsPresContext> editorPresContext = aEditorBase.GetPresContext();
    if (NS_WARN_IF(!editorPresContext)) {
      MOZ_LOG(sISMLog, LogLevel::Error,
              ("  UpdateIMEState(), nsPresContext for editor has already been "
               "lost"));
      return;
    }
    SetIMEState(aNewIMEState, editorPresContext, aElement,
                textInputHandlingWidget, action, sOrigin);
    if (NS_WARN_IF(textInputHandlingWidget->Destroyed()) ||
        NS_WARN_IF(sTextInputHandlingWidget != textInputHandlingWidget)) {
      MOZ_LOG(sISMLog, LogLevel::Error,
              ("  UpdateIMEState(), has set input context, but the widget is "
               "not focused"));
      return;
    }
    if (NS_WARN_IF(
            sTextInputHandlingWidget->GetInputContext().mIMEState.mEnabled !=
            aNewIMEState.mEnabled)) {
      MOZ_LOG(sISMLog, LogLevel::Error,
              ("  UpdateIMEState(), has set input context, but IME enabled "
               "state was overridden by somebody else"));
      return;
    }
  }

  NS_ASSERTION(IsFocusedElement(*presContext, aElement),
               "aElement does not match with sFocusedElement");

  // Finally, create new observer if required.
  if (createNewObserver) {
    if (!sActiveIMEContentObserver && sFocusedPresContext &&
        sTextInputHandlingWidget) {
      // XXX In this case, it might not be enough safe to notify IME of
      //     anything.  So, don't try to flush pending notifications of
      //     IMEContentObserver here.
      CreateIMEContentObserver(aEditorBase, aElement);
    } else {
      MOZ_LOG(sISMLog, LogLevel::Error,
              ("  UpdateIMEState(), wanted to create IMEContentObserver, but "
               "lost focus"));
    }
  }
}

// static
IMEState IMEStateManager::GetNewIMEState(const nsPresContext& aPresContext,
                                         Element* aElement) {
  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("GetNewIMEState(aPresContext=0x%p, aElement=0x%p), "
       "sInstalledMenuKeyboardListener=%s",
       &aPresContext, aElement, TrueOrFalse(sInstalledMenuKeyboardListener)));

  if (!CanHandleWith(&aPresContext)) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  GetNewIMEState() returns IMEEnabled::Disabled because "
             "the nsPresContext has been destroyed"));
    return IMEState(IMEEnabled::Disabled);
  }

  // On Printing or Print Preview, we don't need IME.
  if (aPresContext.Type() == nsPresContext::eContext_PrintPreview ||
      aPresContext.Type() == nsPresContext::eContext_Print) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  GetNewIMEState() returns IMEEnabled::Disabled because "
             "the nsPresContext is for print or print preview"));
    return IMEState(IMEEnabled::Disabled);
  }

  if (sInstalledMenuKeyboardListener) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  GetNewIMEState() returns IMEEnabled::Disabled because "
             "menu keyboard listener was installed"));
    return IMEState(IMEEnabled::Disabled);
  }

  if (!aElement) {
    // Even if there are no focused content, the focused document might be
    // editable, such case is design mode.
    if (aPresContext.Document() && aPresContext.Document()->IsInDesignMode()) {
      if (aPresContext.Document()->GetRootElement()) {
        MOZ_LOG(sISMLog, LogLevel::Debug,
                ("  GetNewIMEState() returns IMEEnabled::Enabled because "
                 "design mode editor has focus"));
        return IMEState(IMEEnabled::Enabled);
      }
      MOZ_LOG(sISMLog, LogLevel::Debug,
              ("  GetNewIMEState() returns IMEEnabled::Disabled because "
               "document is in the design mode but has no element"));
      return IMEState(IMEEnabled::Disabled);
    }
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  GetNewIMEState() returns IMEEnabled::Disabled because "
             "no content has focus"));
    return IMEState(IMEEnabled::Disabled);
  }

  // If aElement is in designMode, aElement should be the root node of the
  // document.
  if (aElement && aElement->IsInDesignMode()) {
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  GetNewIMEState() returns IMEEnabled::Enabled because "
             "a content node in design mode editor has focus"));
    return IMEState(IMEEnabled::Enabled);
  }

  // nsIContent::GetDesiredIMEState() may cause a call of UpdateIMEState()
  // from EditorBase::PostCreate() because GetDesiredIMEState() needs to
  // retrieve an editor instance for the element if it's editable element.
  // For avoiding such nested IME state updates, we should set
  // sIsGettingNewIMEState here and UpdateIMEState() should check it.
  GettingNewIMEStateBlocker blocker;

  IMEState newIMEState = aElement->GetDesiredIMEState();
  MOZ_LOG(sISMLog, LogLevel::Debug,
          ("  GetNewIMEState() returns %s", ToString(newIMEState).c_str()));
  return newIMEState;
}

// static
void IMEStateManager::ResetActiveChildInputContext() {
  sActiveChildInputContext.mIMEState.mEnabled = IMEEnabled::Unknown;
}

// static
bool IMEStateManager::HasActiveChildSetInputContext() {
  return sActiveChildInputContext.mIMEState.mEnabled != IMEEnabled::Unknown;
}

// static
void IMEStateManager::SetInputContextForChildProcess(
    BrowserParent* aBrowserParent, const InputContext& aInputContext,
    const InputContextAction& aAction) {
  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("SetInputContextForChildProcess(aBrowserParent=0x%p, "
       "aInputContext=%s , aAction={ mCause=%s, mAction=%s }), "
       "sFocusedPresContext=0x%p (available: %s), "
       "sTextInputHandlingWidget=0x%p (available: %s), "
       "BrowserParent::GetFocused()=0x%p, sInstalledMenuKeyboardListener=%s",
       aBrowserParent, ToString(aInputContext).c_str(),
       ToString(aAction.mCause).c_str(), ToString(aAction.mFocusChange).c_str(),
       sFocusedPresContext.get(),
       TrueOrFalse(CanHandleWith(sFocusedPresContext)),
       sTextInputHandlingWidget,
       TrueOrFalse(sTextInputHandlingWidget &&
                   !sTextInputHandlingWidget->Destroyed()),
       BrowserParent::GetFocused(),
       TrueOrFalse(sInstalledMenuKeyboardListener)));

  if (aBrowserParent != BrowserParent::GetFocused()) {
    MOZ_LOG(sISMLog, LogLevel::Error,
            ("  SetInputContextForChildProcess(), FAILED, "
             "because non-focused tab parent tries to set input context"));
    return;
  }

  if (NS_WARN_IF(!CanHandleWith(sFocusedPresContext))) {
    MOZ_LOG(sISMLog, LogLevel::Error,
            ("  SetInputContextForChildProcess(), FAILED, "
             "due to no focused presContext"));
    return;
  }

  if (NS_WARN_IF(!sTextInputHandlingWidget) ||
      NS_WARN_IF(sTextInputHandlingWidget->Destroyed())) {
    MOZ_LOG(sISMLog, LogLevel::Error,
            ("  SetInputContextForChildProcess(), FAILED, "
             "due to the widget for the nsPresContext has gone"));
    return;
  }

  const OwningNonNull<nsIWidget> textInputHandlingWidget =
      *sTextInputHandlingWidget;
  MOZ_ASSERT_IF(sFocusedPresContext->GetTextInputHandlingWidget(),
                sFocusedPresContext->GetTextInputHandlingWidget() ==
                    textInputHandlingWidget.get());
  MOZ_ASSERT(aInputContext.mOrigin == InputContext::ORIGIN_CONTENT);

  sActiveChildInputContext = aInputContext;
  MOZ_ASSERT(HasActiveChildSetInputContext());

  // If input context is changed in remote process while menu keyboard listener
  // is installed, this process shouldn't set input context now.  When it's
  // uninstalled, input context should be restored from
  // sActiveChildInputContext.
  if (sInstalledMenuKeyboardListener) {
    MOZ_LOG(sISMLog, LogLevel::Info,
            ("  SetInputContextForChildProcess(), waiting to set input context "
             "until menu keyboard listener is uninstalled"));
    return;
  }

  SetInputContext(textInputHandlingWidget, aInputContext, aAction);
}

MOZ_CAN_RUN_SCRIPT static bool IsNextFocusableElementTextControl(
    const Element* aInputContent) {
  RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager();
  if (MOZ_UNLIKELY(!fm)) {
    return false;
  }
  nsCOMPtr<nsIContent> nextContent;
  const RefPtr<Element> inputContent = const_cast<Element*>(aInputContent);
  const nsCOMPtr<nsPIDOMWindowOuter> outerWindow =
      aInputContent->OwnerDoc()->GetWindow();
  nsresult rv = fm->DetermineElementToMoveFocus(
      outerWindow, inputContent, nsIFocusManager::MOVEFOCUS_FORWARD, true,
      false, getter_AddRefs(nextContent));
  if (NS_WARN_IF(NS_FAILED(rv)) || !nextContent) {
    return false;
  }
  nextContent = nextContent->FindFirstNonChromeOnlyAccessContent();
  const auto* nextControl = nsIFormControl::FromNode(nextContent);
  if (!nextControl || !nextControl->IsTextControl(false)) {
    return false;
  }

  // XXX We don't consider next element is date/time control yet.
  nsGenericHTMLElement* nextElement =
      nsGenericHTMLElement::FromNodeOrNull(nextContent);
  if (!nextElement) {
    return false;
  }

  // FIXME: Should probably use nsIFrame::IsFocusable if possible, to account
  // for things like visibility: hidden or so.
  if (!nextElement->IsFocusableWithoutStyle()) {
    return false;
  }

  // Check readonly attribute.
  if (nextElement->IsHTMLElement(nsGkAtoms::textarea)) {
    auto* textAreaElement = HTMLTextAreaElement::FromNode(nextElement);
    return !textAreaElement->ReadOnly();
  }

  // If neither textarea nor input, what element type?
  MOZ_DIAGNOSTIC_ASSERT(nextElement->IsHTMLElement(nsGkAtoms::input));

  auto* inputElement = HTMLInputElement::FromNode(nextElement);
  return !inputElement->ReadOnly();
}

static void GetInputType(const IMEState& aState, const nsIContent& aContent,
                         nsAString& aInputType) {
  // NOTE: If you change here, you may need to update
  //       widget::InputContext::GatNativeKeyBindings too.
  if (aContent.IsHTMLElement(nsGkAtoms::input)) {
    const HTMLInputElement* inputElement =
        HTMLInputElement::FromNode(&aContent);
    if (inputElement->HasBeenTypePassword() && aState.IsEditable()) {
      aInputType.AssignLiteral("password");
    } else {
      inputElement->GetType(aInputType);
    }
  } else if (aContent.IsHTMLElement(nsGkAtoms::textarea)) {
    aInputType.Assign(nsGkAtoms::textarea->GetUTF16String());
  }
}

MOZ_CAN_RUN_SCRIPT static void GetActionHint(const IMEState& aState,
                                             const nsIContent& aContent,
                                             nsAString& aActionHint) {
  MOZ_ASSERT(aContent.IsHTMLElement());

  if (aState.IsEditable()) {
    nsGenericHTMLElement::FromNode(&aContent)->GetEnterKeyHint(aActionHint);

    // If enterkeyhint is set, we don't infer action hint.
    if (!aActionHint.IsEmpty()) {
      return;
    }
  }

  if (!aContent.IsHTMLElement(nsGkAtoms::input)) {
    return;
  }

  // Get the input content corresponding to the focused node,
  // which may be an anonymous child of the input content.
  MOZ_ASSERT(&aContent == aContent.FindFirstNonChromeOnlyAccessContent());
  const HTMLInputElement* inputElement = HTMLInputElement::FromNode(aContent);
  if (!inputElement) {
    return;
  }

  // If we don't have an action hint and
  // return won't submit the form, use "maybenext".
  bool willSubmit = false;
  bool isLastElement = false;
  HTMLFormElement* formElement = inputElement->GetFormInternal();
  // is this a form and does it have a default submit element?
  if (formElement) {
    if (formElement->IsLastActiveElement(inputElement)) {
      isLastElement = true;
    }

    if (formElement->GetDefaultSubmitElement()) {
      willSubmit = true;
      // is this an html form...
    } else {
      // ... and does it only have a single text input element ?
      if (!formElement->ImplicitSubmissionIsDisabled() ||
          // ... or is this the last non-disabled element?
          isLastElement) {
        willSubmit = true;
      }
    }
  }

  if (!isLastElement && formElement) {
    // If next tabbable content in form is text control, hint should be "next"
    // even there is submit in form.
    // MOZ_KnownLive(inputElement) because it's an alias of aContent.
    if (IsNextFocusableElementTextControl(MOZ_KnownLive(inputElement))) {
      // This is focusable text control
      // XXX What good hint for read only field?
      aActionHint.AssignLiteral("maybenext");
      return;
    }
  }

  if (!willSubmit) {
    aActionHint.Truncate();
    return;
  }

  if (inputElement->ControlType() == FormControlType::InputSearch) {
    aActionHint.AssignLiteral("search");
    return;
  }

  aActionHint.AssignLiteral("go");
}

static void GetInputMode(const IMEState& aState, const nsIContent& aContent,
                         nsAString& aInputMode) {
  if (aState.IsEditable()) {
    aContent.AsElement()->GetAttr(nsGkAtoms::inputmode, aInputMode);
    if (aContent.IsHTMLElement(nsGkAtoms::input) &&
        aInputMode.EqualsLiteral("mozAwesomebar")) {
      if (!nsContentUtils::IsChromeDoc(aContent.OwnerDoc())) {
        // mozAwesomebar should be allowed only in chrome
        aInputMode.Truncate();
      }
    } else {
      ToLowerCase(aInputMode);
    }
  }
}

static void GetAutocapitalize(const IMEState& aState, const Element& aElement,
                              const InputContext& aInputContext,
                              nsAString& aAutocapitalize) {
  if (aElement.IsHTMLElement() && aState.IsEditable() &&
      aInputContext.IsAutocapitalizeSupported()) {
    nsGenericHTMLElement::FromNode(&aElement)->GetAutocapitalize(
        aAutocapitalize);
  }
}

static bool GetAutocorrect(const IMEState& aState, const Element& aElement,
                           const InputContext& aInputContext) {
  if (!StaticPrefs::dom_forms_autocorrect()) {
#ifdef ANDROID
    // Autocorrect was on-by-default on Android by bug 806349, despite
    // autocorrect preference.
    return true;
#else
    return false;
#endif
  }

  if (aElement.IsHTMLElement() && aState.IsEditable()) {
    if (nsContentUtils::IsChromeDoc(aElement.OwnerDoc()) &&
        !aElement.HasAttr(nsGkAtoms::autocorrect)) {
      // Since Chrome UI may not want to enable autocorrect by default such as
      // bug 1881783.
      return false;
    }

    return nsGenericHTMLElement::FromNode(&aElement)->Autocorrect();
  }
  return true;
}

// static
already_AddRefed<nsIURI> IMEStateManager::GetExposableURL(
    const nsPresContext* aPresContext) {
  if (!aPresContext) {
    return nullptr;
  }
  nsIURI* uri = aPresContext->Document()->GetDocumentURI();
  if (!uri) {
    return nullptr;
  }
  // We don't need to and should not expose special URLs such as:
  // about: Any apps like IME should work normally and constantly in any
  //        default pages such as about:blank, about:home, etc in either
  //        the main process or a content process.
  // blob: This may contain big data.  If we copy it to the main process,
  //       it may make the heap dirty which makes the process slower.
  // chrome: Same as about, any apps should work normally and constantly in
  //         any chrome documents.
  // data: Any native apps in the environment shouldn't change the behavior
  //       with the data URL's content and it may contain too big data.
  // file: The file path may contain private things and we shouldn't let
  //       other apps like IME know which one is touched by the user because
  //       malicious text services may like files which are explicitly used
  //       by the user better.
  if (!net::SchemeIsHttpOrHttps(uri)) {
    return nullptr;
  }
  // Note that we don't need to expose UserPass, Query and Reference to
  // IME since they may contain sensitive data, but non-malicious text
  // services must not require these data.
  nsCOMPtr<nsIURI> exposableURL;
  if (NS_FAILED(NS_MutateURI(uri)
                    .SetQuery(""_ns)
                    .SetRef(""_ns)
                    .SetUserPass(""_ns)
                    .Finalize(exposableURL))) {
    return nullptr;
  }

  return exposableURL.forget();
}

// static
void IMEStateManager::SetIMEState(const IMEState& aState,
                                  const nsPresContext* aPresContext,
                                  Element* aElement, nsIWidget& aWidget,
                                  InputContextAction aAction,
                                  InputContext::Origin aOrigin) {
  MOZ_LOG(sISMLog, LogLevel::Info,
          ("SetIMEState(aState=%s, nsPresContext=0x%p, aElement=0x%p "
           "(BrowserParent=0x%p), aWidget=0x%p, aAction={ mCause=%s, "
           "mFocusChange=%s }, aOrigin=%s)",
           ToString(aState).c_str(), aPresContext, aElement,
           BrowserParent::GetFrom(aElement), &aWidget,
           ToString(aAction.mCause).c_str(),
           ToString(aAction.mFocusChange).c_str(), ToChar(aOrigin)));

  InputContext context;
  context.mIMEState = aState;
  context.mURI = IMEStateManager::GetExposableURL(aPresContext);
  context.mOrigin = aOrigin;

  context.mHasHandledUserInput =
      aPresContext && aPresContext->PresShell()->HasHandledUserInput();

  context.mInPrivateBrowsing = aPresContext && aPresContext->Document() &&
                               aPresContext->Document()->IsInPrivateBrowsing();

  const RefPtr<Element> focusedElement =
      aElement ? Element::FromNodeOrNull(
                     aElement->FindFirstNonChromeOnlyAccessContent())
               : nullptr;

  if (focusedElement && focusedElement->IsHTMLElement()) {
    GetInputType(aState, *focusedElement, context.mHTMLInputType);
    GetActionHint(aState, *focusedElement, context.mActionHint);
    GetInputMode(aState, *focusedElement, context.mHTMLInputMode);
    GetAutocapitalize(aState, *focusedElement, context,
                      context.mAutocapitalize);
    context.mAutocorrect = GetAutocorrect(aState, *focusedElement, context);
  }

  if (aAction.mCause == InputContextAction::CAUSE_UNKNOWN &&
      nsContentUtils::LegacyIsCallerChromeOrNativeCode()) {
    aAction.mCause = InputContextAction::CAUSE_UNKNOWN_CHROME;
  }

  if ((aAction.mCause == InputContextAction::CAUSE_UNKNOWN ||
       aAction.mCause == InputContextAction::CAUSE_UNKNOWN_CHROME) &&
      UserActivation::IsHandlingUserInput()) {
    aAction.mCause =
        UserActivation::IsHandlingKeyboardInput()
            ? InputContextAction::CAUSE_UNKNOWN_DURING_KEYBOARD_INPUT
            : InputContextAction::CAUSE_UNKNOWN_DURING_NON_KEYBOARD_INPUT;
  }

  SetInputContext(aWidget, context, aAction);
}

// static
void IMEStateManager::SetInputContext(nsIWidget& aWidget,
                                      const InputContext& aInputContext,
                                      const InputContextAction& aAction) {
  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("SetInputContext(aWidget=0x%p, aInputContext=%s, "
       "aAction={ mCause=%s, mAction=%s }), BrowserParent::GetFocused()=0x%p",
       &aWidget, ToString(aInputContext).c_str(),
       ToString(aAction.mCause).c_str(), ToString(aAction.mFocusChange).c_str(),
       BrowserParent::GetFocused()));

  OwningNonNull<nsIWidget> widget = aWidget;
  widget->SetInputContext(aInputContext, aAction);
  sActiveInputContextWidget = widget;
}

// static
void IMEStateManager::EnsureTextCompositionArray() {
  if (sTextCompositions) {
    return;
  }
  sTextCompositions = new TextCompositionArray();
}

// static
void IMEStateManager::DispatchCompositionEvent(
    nsINode* aEventTargetNode, nsPresContext* aPresContext,
    BrowserParent* aBrowserParent, WidgetCompositionEvent* aCompositionEvent,
    nsEventStatus* aStatus, EventDispatchingCallback* aCallBack,
    bool aIsSynthesized) {
  if (MOZ_LOG_TEST(sISMLog, LogLevel::Info)) {
    MOZ_LOG_FMT(sISMLog, LogLevel::Info, "");
    MOZ_LOG_FMT(sISMLog, LogLevel::Info, ">>>>>>>>");
    MOZ_LOG_FMT(sISMLog, LogLevel::Info,
                "DispatchCompositionEvent(\n"
                "  aNode={},\n"
                "  aPresContext={},\n"
                "  aCompositionEvent={{\n"
                "    mMessage={},\n"
                "    mNativeIMEContext={}\n"
                "    mWidget({})={{\n"
                "      GetNativeIMEContext()={}\n"
                "      Destroyed()={}\n"
                "    }},\n"
                "    mFlags={{ mIsTrusted={}, mPropagationStopped={} }}\n"
                "  }}\n"
                "  aIsSynthesized={}),\n"
                "  browserParent={}",
                RefPtr{aEventTargetNode}, static_cast<void*>(aPresContext),
                ToChar(aCompositionEvent->mMessage),
                aCompositionEvent->mNativeIMEContext,
                static_cast<void*>(aCompositionEvent->mWidget.get()),
                aCompositionEvent->mWidget->GetNativeIMEContext(),
                TrueOrFalse(aCompositionEvent->mWidget->Destroyed()),
                TrueOrFalse(aCompositionEvent->mFlags.mIsTrusted),
                TrueOrFalse(aCompositionEvent->mFlags.mPropagationStopped),
                TrueOrFalse(aIsSynthesized),
                static_cast<void*>(aBrowserParent));
  }
  const auto LogEnd = MakeScopeExit([]() {
    if (MOZ_LOG_TEST(sISMLog, LogLevel::Info)) {
      MOZ_LOG_FMT(sISMLog, LogLevel::Info, "<<<<<<<<");
      MOZ_LOG_FMT(sISMLog, LogLevel::Info, "");
    }
  });

  if (NS_WARN_IF(!aCompositionEvent->IsTrusted()) ||
      NS_WARN_IF(aCompositionEvent->PropagationStopped())) {
    return;
  }

  MOZ_ASSERT(aCompositionEvent->mMessage != eCompositionUpdate,
             "compositionupdate event shouldn't be dispatched manually");

  EnsureTextCompositionArray();

  RefPtr<TextComposition> composition =
      sTextCompositions->GetCompositionFor(aCompositionEvent);
  if (!composition) {
    // If synthesized event comes after delayed native composition events
    // for request of commit or cancel, we should ignore it.
    if (NS_WARN_IF(aIsSynthesized)) {
      return;
    }
    MOZ_LOG(sISMLog, LogLevel::Debug,
            ("  DispatchCompositionEvent(), "
             "adding new TextComposition to the array"));
    MOZ_ASSERT(aCompositionEvent->mMessage == eCompositionStart);
    composition = new TextComposition(aPresContext, aEventTargetNode,
                                      aBrowserParent, aCompositionEvent);
    sTextCompositions->AppendElement(composition);
  }
#ifdef DEBUG
  else {
    MOZ_ASSERT(aCompositionEvent->mMessage != eCompositionStart);
  }
#endif  // #ifdef DEBUG

  // Dispatch the event on composing target.
  composition->DispatchCompositionEvent(aCompositionEvent, aStatus, aCallBack,
                                        aIsSynthesized);

  // WARNING: the |composition| might have been destroyed already.

  // Remove the ended composition from the array.
  // NOTE: When TextComposition is synthesizing compositionend event for
  //       emulating a commit, the instance shouldn't be removed from the array
  //       because IME may perform it later.  Then, we need to ignore the
  //       following commit events in TextComposition::DispatchEvent().
  //       However, if commit or cancel for a request is performed synchronously
  //       during not safe to dispatch events, PresShell must have discarded
  //       compositionend event.  Then, the synthesized compositionend event is
  //       the last event for the composition.  In this case, we need to
  //       destroy the TextComposition with synthesized compositionend event.
  if ((!aIsSynthesized ||
       composition->WasNativeCompositionEndEventDiscarded()) &&
      aCompositionEvent->CausesDOMCompositionEndEvent()) {
    TextCompositionArray::index_type i =
        sTextCompositions->IndexOf(aCompositionEvent->mWidget);
    if (i != TextCompositionArray::NoIndex) {
      MOZ_LOG(
          sISMLog, LogLevel::Debug,
          ("  DispatchCompositionEvent(), "
           "removing TextComposition from the array since NS_COMPOSTION_END "
           "was dispatched"));
      sTextCompositions->ElementAt(i)->Destroy();
      sTextCompositions->RemoveElementAt(i);
    }
  }
}

// static
IMEContentObserver* IMEStateManager::GetActiveContentObserver() {
  return sActiveIMEContentObserver;
}

// static
nsIContent* IMEStateManager::GetRootContent(nsPresContext* aPresContext) {
  Document* doc = aPresContext->Document();
  if (NS_WARN_IF(!doc)) {
    return nullptr;
  }
  return doc->GetRootElement();
}

// static
void IMEStateManager::HandleSelectionEvent(
    nsPresContext* aPresContext, nsIContent* aEventTargetContent,
    WidgetSelectionEvent* aSelectionEvent) {
  RefPtr<BrowserParent> browserParent = GetActiveBrowserParent();
  if (!browserParent) {
    browserParent = BrowserParent::GetFrom(aEventTargetContent
                                               ? aEventTargetContent
                                               : GetRootContent(aPresContext));
  }

  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("HandleSelectionEvent(aPresContext=0x%p, "
       "aEventTargetContent=0x%p, aSelectionEvent={ mMessage=%s, "
       "mFlags={ mIsTrusted=%s } }), browserParent=%p",
       aPresContext, aEventTargetContent, ToChar(aSelectionEvent->mMessage),
       TrueOrFalse(aSelectionEvent->mFlags.mIsTrusted), browserParent.get()));

  if (!aSelectionEvent->IsTrusted()) {
    return;
  }

  RefPtr<TextComposition> composition =
      sTextCompositions
          ? sTextCompositions->GetCompositionFor(aSelectionEvent->mWidget)
          : nullptr;
  if (composition) {
    // When there is a composition, TextComposition should guarantee that the
    // selection event will be handled in same target as composition events.
    composition->HandleSelectionEvent(aSelectionEvent);
  } else {
    // When there is no composition, the selection event should be handled
    // in the aPresContext or browserParent.
    TextComposition::HandleSelectionEvent(aPresContext, browserParent,
                                          aSelectionEvent);
  }
}

// static
void IMEStateManager::OnCompositionEventDiscarded(
    WidgetCompositionEvent* aCompositionEvent) {
  // Note that this method is never called for synthesized events for emulating
  // commit or cancel composition.

  MOZ_LOG(
      sISMLog, LogLevel::Info,
      ("OnCompositionEventDiscarded(aCompositionEvent={ "
       "mMessage=%s, mNativeIMEContext={ mRawNativeIMEContext=0x%" PRIXPTR ", "
       "mOriginProcessID=0x%" PRIX64 " }, mWidget(0x%p)={ "
       "GetNativeIMEContext()={ mRawNativeIMEContext=0x%" PRIXPTR ", "
       "mOriginProcessID=0x%" PRIX64 " }, Destroyed()=%s }, "
       "mFlags={ mIsTrusted=%s } })",
       ToChar(aCompositionEvent->mMessage),
       aCompositionEvent->mNativeIMEContext.mRawNativeIMEContext,
       aCompositionEvent->mNativeIMEContext.mOriginProcessID,
       aCompositionEvent->mWidget.get(),
       aCompositionEvent->mWidget->GetNativeIMEContext().mRawNativeIMEContext,
       aCompositionEvent->mWidget->GetNativeIMEContext().mOriginProcessID,
       TrueOrFalse(aCompositionEvent->mWidget->Destroyed()),
       TrueOrFalse(aCompositionEvent->mFlags.mIsTrusted)));

  if (!aCompositionEvent->IsTrusted()) {
    return;
  }

  // Ignore compositionstart for now because sTextCompositions may not have
  // been created yet.
  if (aCompositionEvent->mMessage == eCompositionStart) {
    return;
  }

  RefPtr<TextComposition> composition =
      sTextCompositions->GetCompositionFor(aCompositionEvent->mWidget);
  if (!composition) {
    // If the PresShell has been being destroyed during composition,
    // a TextComposition instance for the composition was already removed from
    // the array and destroyed in OnDestroyPresContext().  Therefore, we may
    // fail to retrieve a TextComposition instance here.
    MOZ_LOG(sISMLog, LogLevel::Info,
            ("  OnCompositionEventDiscarded(), "
             "TextComposition instance for the widget has already gone"));
    return;
  }
  composition->OnCompositionEventDiscarded(aCompositionEvent);
}

// static
nsresult IMEStateManager::NotifyIME(IMEMessage aMessage, nsIWidget* aWidget,
                                    BrowserParent* aBrowserParent) {
  return IMEStateManager::NotifyIME(IMENotification(aMessage), aWidget,
                                    aBrowserParent);
}

// static
nsresult IMEStateManager::NotifyIME(const IMENotification& aNotification,
                                    nsIWidget* aWidget,
                                    BrowserParent* aBrowserParent) {
  MOZ_LOG(sISMLog, LogLevel::Info,
          ("NotifyIME(aNotification={ mMessage=%s }, "
           "aWidget=0x%p, aBrowserParent=0x%p), sFocusedIMEWidget=0x%p, "
           "BrowserParent::GetFocused()=0x%p, sFocusedIMEBrowserParent=0x%p, "
           "aBrowserParent == BrowserParent::GetFocused()=%s, "
           "aBrowserParent == sFocusedIMEBrowserParent=%s, "
           "CanSendNotificationToWidget()=%s",
           ToChar(aNotification.mMessage), aWidget, aBrowserParent,
           sFocusedIMEWidget, BrowserParent::GetFocused(),
           sFocusedIMEBrowserParent.get(),
           TrueOrFalse(aBrowserParent == BrowserParent::GetFocused()),
           TrueOrFalse(aBrowserParent == sFocusedIMEBrowserParent),
           TrueOrFalse(CanSendNotificationToWidget())));

  if (NS_WARN_IF(!aWidget)) {
    MOZ_LOG(sISMLog, LogLevel::Error,
            ("  NotifyIME(), FAILED due to no widget"));
    return NS_ERROR_INVALID_ARG;
  }

  switch (aNotification.mMessage) {
    case NOTIFY_IME_OF_FOCUS: {
      MOZ_ASSERT(CanSendNotificationToWidget());

      // If focus notification comes from a remote browser which already lost
      // focus, we shouldn't accept the focus notification.  Then, following
      // notifications from the process will be ignored.
      if (aBrowserParent != BrowserParent::GetFocused()) {
        MOZ_LOG(sISMLog, LogLevel::Warning,
                ("  NotifyIME(), WARNING, the received focus notification is "
                 "ignored, because its associated BrowserParent did not match"
                 "the focused BrowserParent."));
        return NS_OK;
      }
      // If IME focus is already set, first blur the currently-focused
      // IME widget
      if (sFocusedIMEWidget) {
        // XXX Why don't we first request the previously-focused IME
        // widget to commit the composition?
        MOZ_ASSERT(
            sFocusedIMEBrowserParent || aBrowserParent,
            "This case shouldn't be caused by focus move in this process");
        MOZ_LOG(sISMLog, LogLevel::Warning,
                ("  NotifyIME(), WARNING, received focus notification with ")
                 "non-null sFocusedIMEWidget. How come "
                 "OnFocusMovedBetweenBrowsers did not blur it already?");
        nsCOMPtr<nsIWidget> focusedIMEWidget(sFocusedIMEWidget);
        sFocusedIMEWidget = nullptr;
        sFocusedIMEBrowserParent = nullptr;
        focusedIMEWidget->NotifyIME(IMENotification(NOTIFY_IME_OF_BLUR));
      }
#ifdef DEBUG
      if (aBrowserParent) {
        nsCOMPtr<nsIWidget> browserParentWidget =
            aBrowserParent->GetTextInputHandlingWidget();
        MOZ_ASSERT(browserParentWidget == aWidget);
      } else {
        MOZ_ASSERT(sFocusedPresContext);
        MOZ_ASSERT_IF(
            sFocusedPresContext->GetTextInputHandlingWidget(),
            sFocusedPresContext->GetTextInputHandlingWidget() == aWidget);
      }
#endif
      sFocusedIMEBrowserParent = aBrowserParent;
      sFocusedIMEWidget = aWidget;
      nsCOMPtr<nsIWidget> widget(aWidget);
      MOZ_LOG(
          sISMLog, LogLevel::Info,
          ("  NotifyIME(), about to call widget->NotifyIME() for IME focus"));
      return widget->NotifyIME(aNotification);
    }
    case NOTIFY_IME_OF_BLUR: {
      if (aBrowserParent != sFocusedIMEBrowserParent) {
        MOZ_LOG(sISMLog, LogLevel::Warning,
                ("  NotifyIME(), WARNING, the received blur notification is "
                 "ignored "
                 "because it's not from current focused IME browser"));
        return NS_OK;
      }
      if (!sFocusedIMEWidget) {
        MOZ_LOG(
            sISMLog, LogLevel::Error,
            ("  NotifyIME(), WARNING, received blur notification but there is "
             "no focused IME widget"));
        return NS_OK;
      }
      if (NS_WARN_IF(sFocusedIMEWidget != aWidget)) {
        MOZ_LOG(sISMLog, LogLevel::Warning,
                ("  NotifyIME(), WARNING, the received blur notification is "
                 "ignored "
                 "because it's not for current focused IME widget"));
        return NS_OK;
      }
      nsCOMPtr<nsIWidget> focusedIMEWidget(sFocusedIMEWidget);
      sFocusedIMEWidget = nullptr;
      sFocusedIMEBrowserParent = nullptr;
      return CanSendNotificationToWidget()
                 ? focusedIMEWidget->NotifyIME(
                       IMENotification(NOTIFY_IME_OF_BLUR))
                 : NS_OK;
    }
    case NOTIFY_IME_OF_SELECTION_CHANGE:
    case NOTIFY_IME_OF_TEXT_CHANGE:
    case NOTIFY_IME_OF_POSITION_CHANGE:
    case NOTIFY_IME_OF_MOUSE_BUTTON_EVENT:
    case NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED: {
      if (aBrowserParent != sFocusedIMEBrowserParent) {
        MOZ_LOG(
            sISMLog, LogLevel::Warning,
            ("  NotifyIME(), WARNING, the received content change notification "
             "is ignored because it's not from current focused IME browser"));
        return NS_OK;
      }
      if (!sFocusedIMEWidget) {
        MOZ_LOG(
            sISMLog, LogLevel::Warning,
            ("  NotifyIME(), WARNING, the received content change notification "
             "is ignored because there is no focused IME widget"));
        return NS_OK;
      }
      if (NS_WARN_IF(sFocusedIMEWidget != aWidget)) {
        MOZ_LOG(
            sISMLog, LogLevel::Warning,
            ("  NotifyIME(), WARNING, the received content change notification "
             "is ignored because it's not for current focused IME widget"));
        return NS_OK;
      }
      if (!CanSendNotificationToWidget()) {
        return NS_OK;
      }
      nsCOMPtr<nsIWidget> widget(aWidget);
      return widget->NotifyIME(aNotification);
    }
    default:
      // Other notifications should be sent only when there is composition.
      // So, we need to handle the others below.
      break;
  }

  if (!sTextCompositions) {
    MOZ_LOG(sISMLog, LogLevel::Info,
            ("  NotifyIME(), the request to IME is ignored because "
             "there have been no compositions yet"));
    return NS_OK;
  }

  RefPtr<TextComposition> composition =
      sTextCompositions->GetCompositionFor(aWidget);
  if (!composition) {
    MOZ_LOG(sISMLog, LogLevel::Info,
            ("  NotifyIME(), the request to IME is ignored because "
             "there is no active composition"));
    return NS_OK;
  }

  if (aBrowserParent != composition->GetBrowserParent()) {
    MOZ_LOG(
        sISMLog, LogLevel::Warning,
        ("  NotifyIME(), WARNING, the request to IME is ignored because "
         "it does not come from the remote browser which has the composition "
         "on aWidget"));
    return NS_OK;
  }

  switch (aNotification.mMessage) {
    case REQUEST_TO_COMMIT_COMPOSITION:
      return composition->RequestToCommit(aWidget, false);
    case REQUEST_TO_CANCEL_COMPOSITION:
      return composition->RequestToCommit(aWidget, true);
    default:
      MOZ_CRASH("Unsupported notification");
  }
  MOZ_CRASH(
      "Failed to handle the notification for non-synthesized composition");
  return NS_ERROR_FAILURE;
}

// static
nsresult IMEStateManager::NotifyIME(IMEMessage aMessage,
                                    nsPresContext* aPresContext,
                                    BrowserParent* aBrowserParent) {
  MOZ_LOG(sISMLog, LogLevel::Info,
          ("NotifyIME(aMessage=%s, aPresContext=0x%p, aBrowserParent=0x%p)",
           ToChar(aMessage), aPresContext, aBrowserParent));
  // This assertion is just for clarify which message may be set, so feel free
  // to add other messages if you want.
  MOZ_ASSERT(aMessage == REQUEST_TO_CANCEL_COMPOSITION ||
             aMessage == REQUEST_TO_COMMIT_COMPOSITION ||
             aMessage == NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED);
  // However, these messages require additional information.  Therefore, this
  // overload shouldn't be used for them.
  MOZ_ASSERT(aMessage != NOTIFY_IME_OF_FOCUS &&
             aMessage != NOTIFY_IME_OF_BLUR &&
             aMessage != NOTIFY_IME_OF_TEXT_CHANGE &&
             aMessage != NOTIFY_IME_OF_SELECTION_CHANGE &&
             aMessage != NOTIFY_IME_OF_MOUSE_BUTTON_EVENT);

  if (NS_WARN_IF(!CanHandleWith(aPresContext))) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIWidget> widget =
      aPresContext == sFocusedPresContext && sTextInputHandlingWidget
          ? sTextInputHandlingWidget
          : aPresContext->GetTextInputHandlingWidget();
  if (NS_WARN_IF(!widget)) {
    MOZ_LOG(sISMLog, LogLevel::Error,
            ("  NotifyIME(), FAILED due to no widget for the nsPresContext"));
    return NS_ERROR_NOT_AVAILABLE;
  }
  return NotifyIME(aMessage, widget, aBrowserParent);
}

// static
bool IMEStateManager::IsIMEObserverNeeded(const IMEState& aState) {
  return aState.IsEditable();
}

// static
void IMEStateManager::DestroyIMEContentObserver() {
  if (!sActiveIMEContentObserver) {
    MOZ_LOG_FMT(sISMLog, LogLevel::Verbose,
                "DestroyIMEContentObserver() does nothing");
    return;
  }

  MOZ_LOG_FMT(sISMLog, LogLevel::Info,
              "DestroyIMEContentObserver(), destroying "
              "the active IMEContentObserver ({}) for {}...",
              static_cast<void*>(sActiveIMEContentObserver.get()),
              RefPtr{sActiveIMEContentObserver->GetObservingElement()});
  RefPtr<IMEContentObserver> tsm = sActiveIMEContentObserver.get();
  sActiveIMEContentObserver = nullptr;
  tsm->Destroy();
}

// static
void IMEStateManager::CreateIMEContentObserver(EditorBase& aEditorBase,
                                               Element* aFocusedElement) {
  MOZ_ASSERT(!sActiveIMEContentObserver);
  MOZ_ASSERT(sTextInputHandlingWidget);
  MOZ_ASSERT(sFocusedPresContext);
  MOZ_ASSERT(IsIMEObserverNeeded(
      sTextInputHandlingWidget->GetInputContext().mIMEState));

  MOZ_LOG_FMT(sISMLog, LogLevel::Info,
              "CreateIMEContentObserver(aEditorBase={}, aFocusedElement={}), "
              "sFocusedPresContext={}, sFocusedElement={}, "
              "sTextInputHandlingWidget={} (available: {}), "
              "sActiveIMEContentObserver={}, "
              "sActiveIMEContentObserver->IsObserving(sFocusedPresContext, "
              "sFocusedElement)={}",
              static_cast<void*>(&aEditorBase), RefPtr{aFocusedElement},
              static_cast<void*>(sFocusedPresContext.get()), sFocusedElement,
              static_cast<void*>(sTextInputHandlingWidget),
              TrueOrFalse(sTextInputHandlingWidget &&
                          !sTextInputHandlingWidget->Destroyed()),
              static_cast<void*>(sActiveIMEContentObserver.get()),
              TrueOrFalse(sActiveIMEContentObserver && sFocusedPresContext &&
                          sActiveIMEContentObserver->IsObserving(
                              *sFocusedPresContext, sFocusedElement)));

  if (NS_WARN_IF(sTextInputHandlingWidget->Destroyed())) {
    MOZ_LOG_FMT(sISMLog, LogLevel::Error,
                "  CreateIMEContentObserver(), FAILED due to "
                "the widget for the nsPresContext has gone");
    return;
  }

  MOZ_ASSERT_IF(sFocusedPresContext->GetTextInputHandlingWidget(),
                sFocusedPresContext->GetTextInputHandlingWidget() ==
                    sTextInputHandlingWidget);

  MOZ_LOG_FMT(sISMLog, LogLevel::Debug,
              "  CreateIMEContentObserver() is creating an "
              "IMEContentObserver instance...");
  sActiveIMEContentObserver = new IMEContentObserver();
  sActiveIMEContentObserver->Init(*sTextInputHandlingWidget,
                                  *sFocusedPresContext, aFocusedElement,
                                  aEditorBase);
  MOZ_LOG_FMT(sISMLog, LogLevel::Debug,
              "  CreateIMEContentObserver() is created ({})",
              static_cast<void*>(sActiveIMEContentObserver));
}

// static
nsresult IMEStateManager::GetFocusSelectionAndRootElement(
    Selection** aSelection, Element** aRootElement) {
  if (!sActiveIMEContentObserver) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return sActiveIMEContentObserver->GetSelectionAndRoot(aSelection,
                                                        aRootElement);
}

// static
TextComposition* IMEStateManager::GetTextCompositionFor(nsIWidget* aWidget) {
  return sTextCompositions ? sTextCompositions->GetCompositionFor(aWidget)
                           : nullptr;
}

// static
TextComposition* IMEStateManager::GetTextCompositionFor(
    const WidgetCompositionEvent* aCompositionEvent) {
  return sTextCompositions
             ? sTextCompositions->GetCompositionFor(aCompositionEvent)
             : nullptr;
}

// static
TextComposition* IMEStateManager::GetTextCompositionFor(
    nsPresContext* aPresContext) {
  return sTextCompositions ? sTextCompositions->GetCompositionFor(aPresContext)
                           : nullptr;
}

}  // namespace mozilla
