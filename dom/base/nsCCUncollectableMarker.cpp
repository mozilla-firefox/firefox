/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCCUncollectableMarker.h"
#include "nsIObserverService.h"
#include "nsIDocShell.h"
#include "nsServiceManagerUtils.h"
#include "nsIDocumentViewer.h"
#include "mozilla/dom/Document.h"
#include "InProcessBrowserChildMessageManager.h"
#include "nsIWindowMediator.h"
#include "nsPIDOMWindow.h"
#include "nsIWebNavigation.h"
#include "nsISHistory.h"
#include "nsISHEntry.h"
#include "nsIWindowWatcher.h"
#include "mozilla/Services.h"
#include "nsIAppWindow.h"
#include "nsIAppShellService.h"
#include "nsAppShellCID.h"
#include "nsContentUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsGlobalWindowOuter.h"
#include "nsJSEnvironment.h"
#include "nsFrameLoader.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/ChromeMessageBroadcaster.h"
#include "mozilla/dom/ContentFrameMessageManager.h"
#include "mozilla/dom/ContentProcessMessageManager.h"
#include "mozilla/dom/CustomElementRegistry.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ParentProcessMessageManager.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/TimeoutManager.h"
#include "xpcpublic.h"
#include "nsObserverService.h"
#include "nsFocusManager.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIXULRuntime.h"

using namespace mozilla;
using namespace mozilla::dom;

static StaticRefPtr<nsCCUncollectableMarker> sInstance;

// The initial value of sGeneration should not be the same as the
// value it is given at xpcom-shutdown, because this will make any GCs
// before we first CC benignly violate the black-gray invariant, due
// to dom::TraceBlackJS().
uint32_t nsCCUncollectableMarker::sGeneration = 1;
#include "nsXULPrototypeCache.h"

NS_IMPL_ISUPPORTS(nsCCUncollectableMarker, nsIObserver)

/* static */
nsresult nsCCUncollectableMarker::Init() {
  if (sInstance) {
    return NS_OK;
  }

  RefPtr<nsCCUncollectableMarker> marker = new nsCCUncollectableMarker();

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (!obs) return NS_ERROR_FAILURE;

  nsresult rv;

  // This makes the observer service hold an owning reference to the marker
  rv = obs->AddObserver(marker, "xpcom-shutdown", false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = obs->AddObserver(marker, "cycle-collector-begin", false);
  NS_ENSURE_SUCCESS(rv, rv);

  sInstance = marker;

  return NS_OK;
}

static void MarkChildMessageManagers(MessageBroadcaster* aMM) {
  aMM->MarkForCC();

  uint32_t browserChildCount = aMM->ChildCount();
  for (uint32_t j = 0; j < browserChildCount; ++j) {
    RefPtr<MessageListenerManager> childMM = aMM->GetChildAt(j);
    if (!childMM) {
      continue;
    }

    RefPtr<MessageBroadcaster> strongNonLeafMM =
        MessageBroadcaster::From(childMM);
    MessageBroadcaster* nonLeafMM = strongNonLeafMM;

    MessageListenerManager* tabMM = childMM;

    strongNonLeafMM = nullptr;
    childMM = nullptr;

    if (nonLeafMM) {
      MarkChildMessageManagers(nonLeafMM);
      continue;
    }

    tabMM->MarkForCC();

    // XXX hack warning, but works, since we know that
    //    callback is frameloader.
    mozilla::dom::ipc::MessageManagerCallback* cb = tabMM->GetCallback();
    if (cb) {
      nsFrameLoader* fl = static_cast<nsFrameLoader*>(cb);
      InProcessBrowserChildMessageManager* et =
          fl->GetBrowserChildMessageManager();
      if (!et) {
        continue;
      }
      et->MarkForCC();
      EventListenerManager* elm = et->GetExistingListenerManager();
      if (elm) {
        elm->MarkForCC();
      }
    }
  }
}

static void MarkMessageManagers() {
  if (nsFrameMessageManager::GetChildProcessManager()) {
    // ContentProcessMessageManager's MarkForCC also marks ChildProcessManager.
    ContentProcessMessageManager* pg = ContentProcessMessageManager::Get();
    if (pg) {
      pg->MarkForCC();
    }
  }

  // The global message manager only exists in the root process.
  if (!XRE_IsParentProcess()) {
    return;
  }
  RefPtr<ChromeMessageBroadcaster> strongGlobalMM =
      nsFrameMessageManager::GetGlobalMessageManager();
  if (!strongGlobalMM) {
    return;
  }
  ChromeMessageBroadcaster* globalMM = strongGlobalMM;
  strongGlobalMM = nullptr;
  MarkChildMessageManagers(globalMM);

  if (nsFrameMessageManager::sParentProcessManager) {
    nsFrameMessageManager::sParentProcessManager->MarkForCC();
    uint32_t childCount =
        nsFrameMessageManager::sParentProcessManager->ChildCount();
    for (uint32_t i = 0; i < childCount; ++i) {
      RefPtr<MessageListenerManager> childMM =
          nsFrameMessageManager::sParentProcessManager->GetChildAt(i);
      if (!childMM) {
        continue;
      }
      MessageListenerManager* child = childMM;
      childMM = nullptr;
      child->MarkForCC();
    }
  }
  if (nsFrameMessageManager::sSameProcessParentManager) {
    nsFrameMessageManager::sSameProcessParentManager->MarkForCC();
  }
}

void MarkDocumentViewer(nsIDocumentViewer* aViewer, bool aCleanupJS) {
  if (!aViewer) {
    return;
  }

  Document* doc = aViewer->GetDocument();
  if (doc &&
      doc->GetMarkedCCGeneration() != nsCCUncollectableMarker::sGeneration) {
    doc->MarkUncollectableForCCGeneration(nsCCUncollectableMarker::sGeneration);
    if (aCleanupJS) {
      EventListenerManager* elm = doc->GetExistingListenerManager();
      if (elm) {
        elm->MarkForCC();
      }
      RefPtr<nsGlobalWindowInner> win =
          nsGlobalWindowInner::Cast(doc->GetInnerWindow());
      if (win) {
        elm = win->GetExistingListenerManager();
        if (elm) {
          elm->MarkForCC();
        }
        win->GetTimeoutManager()->UnmarkGrayTimers();
      }
    }
  }
  if (doc) {
    if (nsPIDOMWindowInner* inner = doc->GetInnerWindow()) {
      inner->MarkUncollectableForCCGeneration(
          nsCCUncollectableMarker::sGeneration);
    }
    if (nsPIDOMWindowOuter* outer = doc->GetWindow()) {
      outer->MarkUncollectableForCCGeneration(
          nsCCUncollectableMarker::sGeneration);
    }
  }
}

void MarkDocShell(nsIDocShellTreeItem* aNode, bool aCleanupJS);

void MarkSHEntry(nsISHEntry* aSHEntry, bool aCleanupJS) {
  if (!aSHEntry) {
    return;
  }

  nsCOMPtr<nsIDocumentViewer> viewer;
  aSHEntry->GetDocumentViewer(getter_AddRefs(viewer));
  MarkDocumentViewer(viewer, aCleanupJS);

  nsCOMPtr<nsIDocShellTreeItem> child;
  int32_t i = 0;
  while (NS_SUCCEEDED(aSHEntry->ChildShellAt(i++, getter_AddRefs(child))) &&
         child) {
    MarkDocShell(child, aCleanupJS);
  }

  int32_t count;
  aSHEntry->GetChildCount(&count);
  for (i = 0; i < count; ++i) {
    nsCOMPtr<nsISHEntry> childEntry;
    aSHEntry->GetChildAt(i, getter_AddRefs(childEntry));
    MarkSHEntry(childEntry, aCleanupJS);
  }
}

void MarkDocShell(nsIDocShellTreeItem* aNode, bool aCleanupJS) {
  nsCOMPtr<nsIDocShell> shell = do_QueryInterface(aNode);
  if (!shell) {
    return;
  }

  nsCOMPtr<nsIDocumentViewer> viewer;
  shell->GetDocViewer(getter_AddRefs(viewer));
  MarkDocumentViewer(viewer, aCleanupJS);

  nsCOMPtr<nsIWebNavigation> webNav = do_QueryInterface(shell);
  RefPtr<ChildSHistory> history = webNav->GetSessionHistory();
  IgnoredErrorResult ignore;
  nsISHistory* legacyHistory =
      history ? history->GetLegacySHistory(ignore) : nullptr;
  if (legacyHistory) {
    MOZ_DIAGNOSTIC_ASSERT(!mozilla::SessionHistoryInParent());
    int32_t historyCount = history->Count();
    for (int32_t i = 0; i < historyCount; ++i) {
      nsCOMPtr<nsISHEntry> shEntry;
      legacyHistory->GetEntryAtIndex(i, getter_AddRefs(shEntry));

      MarkSHEntry(shEntry, aCleanupJS);
    }
  }

  int32_t i, childCount;
  aNode->GetInProcessChildCount(&childCount);
  for (i = 0; i < childCount; ++i) {
    nsCOMPtr<nsIDocShellTreeItem> child;
    aNode->GetInProcessChildAt(i, getter_AddRefs(child));
    MarkDocShell(child, aCleanupJS);
  }
}

void MarkWindowList(nsISimpleEnumerator* aWindowList, bool aCleanupJS) {
  nsCOMPtr<nsISupports> iter;
  while (NS_SUCCEEDED(aWindowList->GetNext(getter_AddRefs(iter))) && iter) {
    if (nsCOMPtr<nsPIDOMWindowOuter> window = do_QueryInterface(iter)) {
      nsCOMPtr<nsIDocShell> rootDocShell = window->GetDocShell();

      MarkDocShell(rootDocShell, aCleanupJS);

      RefPtr<BrowserChild> browserChild = BrowserChild::GetFrom(rootDocShell);
      if (browserChild) {
        RefPtr<BrowserChildMessageManager> mm =
            browserChild->GetMessageManager();
        if (mm) {
          // MarkForCC ends up calling UnmarkGray on message listeners, which
          // TraceBlackJS can't do yet.
          mm->MarkForCC();
        }
      }
    }
  }
}

nsresult nsCCUncollectableMarker::Observe(nsISupports* aSubject,
                                          const char* aTopic,
                                          const char16_t* aData) {
  if (!strcmp(aTopic, "xpcom-shutdown")) {
    Element::ClearContentUnbinder();

    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (!obs) return NS_ERROR_FAILURE;

    // No need for kungFuDeathGrip here, yay observerservice!
    obs->RemoveObserver(this, "xpcom-shutdown");
    obs->RemoveObserver(this, "cycle-collector-begin");

    sInstance = nullptr;
    sGeneration = 0;

    return NS_OK;
  }

  MOZ_ASSERT(!strcmp(aTopic, "cycle-collector-begin"), "wrong topic");

  Element::ClearContentUnbinder();
  return Cleanup(/* aPrepareForCC = */ true);
}

// Don't call this with aPrepareForCC = false from an observer, as apparently
// calling UnmarkGrayStrongObservers() from inside an observer can cause
// problems. See bug 1958292.
nsresult nsCCUncollectableMarker::Cleanup(bool aPrepareForCC) {
  // JS cleanup can be slow. Do it only if this is the first forget-skippable
  // after a GC.
  const bool cleanupJS =
      nsJSContext::HasHadCleanupSinceLastGC() && !aPrepareForCC;

  // Increase generation to effectively unmark all current objects
  if (!++sGeneration) {
    ++sGeneration;
  }

  nsFocusManager::MarkUncollectableForCCGeneration(sGeneration);

  nsresult rv;

  // Iterate all toplevel windows
  nsCOMPtr<nsISimpleEnumerator> windowList;
  nsCOMPtr<nsIWindowMediator> med = do_GetService(NS_WINDOWMEDIATOR_CONTRACTID);
  if (med) {
    rv = med->GetEnumerator(nullptr, getter_AddRefs(windowList));
    NS_ENSURE_SUCCESS(rv, rv);

    MarkWindowList(windowList, cleanupJS);
  }

  nsCOMPtr<nsIWindowWatcher> ww = do_GetService(NS_WINDOWWATCHER_CONTRACTID);
  if (ww) {
    rv = ww->GetWindowEnumerator(getter_AddRefs(windowList));
    NS_ENSURE_SUCCESS(rv, rv);

    MarkWindowList(windowList, cleanupJS);
  }

  nsCOMPtr<nsIAppShellService> appShell =
      do_GetService(NS_APPSHELLSERVICE_CONTRACTID);
  if (appShell) {
    bool hasHiddenWindow = false;
    appShell->GetHasHiddenWindow(&hasHiddenWindow);
    if (hasHiddenWindow) {
      nsCOMPtr<nsIAppWindow> hw;
      appShell->GetHiddenWindow(getter_AddRefs(hw));
      nsCOMPtr<nsIDocShell> shell;
      hw->GetDocShell(getter_AddRefs(shell));
      MarkDocShell(shell, cleanupJS);
    }
  }

  nsXULPrototypeCache* xulCache = nsXULPrototypeCache::GetInstance();
  if (xulCache) {
    xulCache->MarkInCCGeneration(sGeneration);
  }

  enum ForgetSkippableCleanupState {
    eInitial = 0,
    eUnmarkJSEventListeners = 1,
    eUnmarkMessageManagers = 2,
    eUnmarkStrongObservers = 3,
    eUnmarkJSHolders = 4,
    eDone = 5
  };

  static_assert(eDone == kMajorForgetSkippableCalls,
                "There must be one forgetSkippable call per cleanup state.");

  static uint32_t sFSState = eDone;
  if (aPrepareForCC) {
    sFSState = eDone;
    return NS_OK;
  }

  if (cleanupJS) {
    // After a GC we start clean up phases from the beginning,
    // but we don't want to do the additional clean up phases here
    // since we have done already plenty of gray unmarking while going through
    // frame message managers and docshells.
    sFSState = eInitial;
    return NS_OK;
  }
  ++sFSState;

  switch (sFSState) {
    case eUnmarkJSEventListeners: {
      nsContentUtils::UnmarkGrayJSListenersInCCGenerationDocuments();
      break;
    }
    case eUnmarkMessageManagers: {
      MarkMessageManagers();
      break;
    }
    case eUnmarkStrongObservers: {
      nsCOMPtr<nsIObserverService> obs =
          mozilla::services::GetObserverService();
      static_cast<nsObserverService*>(obs.get())->UnmarkGrayStrongObservers();
      break;
    }
    case eUnmarkJSHolders: {
      xpc_UnmarkSkippableJSHolders();
      break;
    }
    default: {
      break;
    }
  }

  return NS_OK;
}

void nsCCUncollectableMarker::CleanupForForgetSkippable() {
  if (sInstance) {
    (void)sInstance->Cleanup(/* aPrepareForCC = */ false);
  }
}

void mozilla::dom::TraceBlackJS(JSTracer* aTrc) {
  if (!nsCCUncollectableMarker::sGeneration) {
    return;
  }

  if (ContentProcessMessageManager::WasCreated() &&
      nsFrameMessageManager::GetChildProcessManager()) {
    auto* pg = ContentProcessMessageManager::Get();
    if (pg) {
      mozilla::TraceScriptHolder(ToSupports(pg), aTrc);
    }
  }

  // Mark globals of active windows black.
  nsGlobalWindowOuter::OuterWindowByIdTable* windowsById =
      nsGlobalWindowOuter::GetWindowsTable();
  if (windowsById) {
    for (nsGlobalWindowOuter* window : windowsById->Values()) {
      if (!window->IsCleanedUp()) {
        nsGlobalWindowInner* inner = nullptr;
        for (PRCList* win = PR_LIST_HEAD(window); win != window;
             win = PR_NEXT_LINK(inner)) {
          inner = static_cast<nsGlobalWindowInner*>(win);
          if (inner->IsCurrentInnerWindow() ||
              (inner->GetExtantDoc() &&
               inner->GetExtantDoc()->GetBFCacheEntry())) {
            inner->TraceGlobalJSObject(aTrc);
            EventListenerManager* elm = inner->GetExistingListenerManager();
            if (elm) {
              elm->TraceListeners(aTrc);
            }
            CustomElementRegistry* cer = inner->GetExistingCustomElements();
            if (cer) {
              cer->TraceDefinitions(aTrc);
            }
          }
        }

        if (window->IsRootOuterWindow()) {
          // In child process trace all the BrowserChildMessageManagers.
          // Since there is one root outer window per
          // BrowserChildMessageManager, we need to look for only those windows,
          // not all.
          nsIDocShell* ds = window->GetDocShell();
          if (ds) {
            nsCOMPtr<nsIBrowserChild> browserChild = ds->GetBrowserChild();
            if (browserChild) {
              RefPtr<ContentFrameMessageManager> mm;
              browserChild->GetMessageManager(getter_AddRefs(mm));
              if (mm) {
                nsCOMPtr<nsISupports> browserChildAsSupports =
                    do_QueryInterface(browserChild);
                mozilla::TraceScriptHolder(browserChildAsSupports, aTrc);
                EventListenerManager* elm = mm->GetExistingListenerManager();
                if (elm) {
                  elm->TraceListeners(aTrc);
                }
                // As of now there isn't an easy way to trace message listeners.
              }
            }
          }
        }
      }
    }
  }
}
