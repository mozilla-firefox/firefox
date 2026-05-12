/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/JSActorService.h"

#include "mozilla/ArrayAlgorithm.h"
#include "mozilla/Logging.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/ChromeUtilsBinding.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/EventListenerBinding.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/dom/EventTargetBinding.h"
#include "mozilla/dom/InProcessChild.h"
#include "mozilla/dom/InProcessParent.h"
#include "mozilla/dom/JSActorManager.h"
#include "mozilla/dom/JSProcessActorBinding.h"
#include "mozilla/dom/JSProcessActorChild.h"
#include "mozilla/dom/JSProcessActorProtocol.h"
#include "mozilla/dom/JSWindowActorBinding.h"
#include "mozilla/dom/JSWindowActorChild.h"
#include "mozilla/dom/JSWindowActorProtocol.h"
#include "mozilla/dom/MessageManagerBinding.h"
#include "mozilla/dom/PContent.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "nsIObserverService.h"

namespace mozilla::dom {
namespace {
StaticRefPtr<JSActorService> gJSActorService;
}

JSActorService::JSActorService() { MOZ_ASSERT(NS_IsMainThread()); }

JSActorService::~JSActorService() { MOZ_ASSERT(NS_IsMainThread()); }

/* static */
already_AddRefed<JSActorService> JSActorService::GetSingleton() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!gJSActorService) {
    gJSActorService = new JSActorService();
    ClearOnShutdown(&gJSActorService);
  }

  RefPtr<JSActorService> service = gJSActorService.get();
  return service.forget();
}

void JSActorService::RegisterWindowActor(const nsACString& aName,
                                         const WindowActorOptions& aOptions,
                                         ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());

  if (mProcessActorDescriptors.Contains(aName)) {
    aRv.ThrowNotSupportedError(
        nsPrintfCString("'%s' actor is already registered as a process actor.",
                        PromiseFlatCString(aName).get()));
    return;
  }

  const auto proto = mWindowActorDescriptors.WithEntryHandle(
      aName, [&](auto&& entry) -> RefPtr<JSWindowActorProtocol> {
        if (entry) {
          aRv.ThrowNotSupportedError(
              nsPrintfCString("'%s' actor is already registered.",
                              PromiseFlatCString(aName).get()));
          return nullptr;
        }

        // Insert a new entry for the protocol.
        RefPtr<JSWindowActorProtocol> protocol =
            JSWindowActorProtocol::FromWebIDLOptions(aName, aOptions, aRv);
        if (NS_WARN_IF(aRv.Failed())) {
          return nullptr;
        }

        entry.Insert(protocol);

        return protocol;
      });

  if (!proto) {
    MOZ_ASSERT(aRv.Failed());
    return;
  }

  // Send information about the newly added entry to every existing content
  // process.
  AutoTArray<JSWindowActorInfo, 1> windowInfos{proto->ToIPC()};
  nsTArray<JSProcessActorInfo> contentInfos{};
  for (auto* cp : ContentParent::AllProcesses(ContentParent::eLive)) {
    (void)cp->SendInitJSActorInfos(contentInfos, windowInfos);
  }

  // Register event listeners for any existing chrome targets.
  for (EventTarget* target : mChromeEventTargets) {
    proto->RegisterListenersFor(target);
  }

  // Add observers to the protocol.
  proto->AddObservers();
}

void JSActorService::UnregisterWindowActor(const nsACString& aName) {
  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());
  CrashReporter::AutoRecordAnnotation autoActorName(
      CrashReporter::Annotation::JSActorName, aName);
  CrashReporter::AutoRecordAnnotation autoMessageName(
      CrashReporter::Annotation::JSActorMessage, "<Unregister>"_ns);

  nsAutoCString name(aName);
  RefPtr<JSWindowActorProtocol> proto;
  if (mWindowActorDescriptors.Remove(name, getter_AddRefs(proto))) {
    // Remove listeners for this actor from each of our chrome targets.
    for (EventTarget* target : mChromeEventTargets) {
      proto->UnregisterListenersFor(target);
    }

    // Remove observers for this actor from observer service.
    proto->RemoveObservers();

    // Tell every content process to also unregister, and accumulate the set of
    // potential managers, to have the actor disabled.
    nsTArray<RefPtr<JSActorManager>> managers;
    if (XRE_IsParentProcess()) {
      for (auto* cp : ContentParent::AllProcesses(ContentParent::eAll)) {
        if (cp->CanSend()) {
          (void)cp->SendUnregisterJSWindowActor(name);
        }
        for (const auto& bp : cp->ManagedPBrowserParent()) {
          for (const auto& wgp : bp->ManagedPWindowGlobalParent()) {
            managers.AppendElement(static_cast<WindowGlobalParent*>(wgp));
          }
        }
      }

      for (const auto& wgp :
           InProcessParent::Singleton()->ManagedPWindowGlobalParent()) {
        managers.AppendElement(static_cast<WindowGlobalParent*>(wgp));
      }
      for (const auto& wgc :
           InProcessChild::Singleton()->ManagedPWindowGlobalChild()) {
        managers.AppendElement(static_cast<WindowGlobalChild*>(wgc));
      }
    } else {
      for (const auto& bc :
           ContentChild::GetSingleton()->ManagedPBrowserChild()) {
        for (const auto& wgc : bc->ManagedPWindowGlobalChild()) {
          managers.AppendElement(static_cast<WindowGlobalChild*>(wgc));
        }
      }
    }

    for (auto& mgr : managers) {
      mgr->JSActorUnregister(name);
    }
  }
}

void JSActorService::LoadJSActorInfos(nsTArray<JSProcessActorInfo>& aProcess,
                                      nsTArray<JSWindowActorInfo>& aWindow) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsContentProcess());

  for (auto& info : aProcess) {
    // Create our JSProcessActorProtocol, register it in
    // mProcessActorDescriptors.
    auto name = info.name();
    RefPtr<JSProcessActorProtocol> proto =
        JSProcessActorProtocol::FromIPC(std::move(info));
    mProcessActorDescriptors.InsertOrUpdate(std::move(name), RefPtr{proto});

    // Add observers for each actor.
    proto->AddObservers();
  }

  for (auto& info : aWindow) {
    auto name = info.name();
    RefPtr<JSWindowActorProtocol> proto =
        JSWindowActorProtocol::FromIPC(std::move(info));
    mWindowActorDescriptors.InsertOrUpdate(std::move(name), RefPtr{proto});

    // Register listeners for each chrome target.
    for (EventTarget* target : mChromeEventTargets) {
      proto->RegisterListenersFor(target);
    }

    // Add observers for each actor.
    proto->AddObservers();
  }
}

void JSActorService::GetJSWindowActorInfos(
    nsTArray<JSWindowActorInfo>& aInfos) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());

  for (const auto& data : mWindowActorDescriptors.Values()) {
    aInfos.AppendElement(data->ToIPC());
  }
}

void JSActorService::RegisterChromeEventTarget(EventTarget* aTarget) {
  MOZ_ASSERT(!mChromeEventTargets.Contains(aTarget));
  mChromeEventTargets.AppendElement(aTarget);

  // Register event listeners on the newly added Window Root.
  for (const auto& data : mWindowActorDescriptors.Values()) {
    data->RegisterListenersFor(aTarget);
  }

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  obs->NotifyObservers(aTarget, "chrome-event-target-created", nullptr);
}

/* static */
void JSActorService::UnregisterChromeEventTarget(EventTarget* aTarget) {
  if (gJSActorService) {
    // NOTE: No need to unregister listeners here, as the target is going away.
    gJSActorService->mChromeEventTargets.RemoveElement(aTarget);
  }
}

void JSActorService::RegisterProcessActor(const nsACString& aName,
                                          const ProcessActorOptions& aOptions,
                                          ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());

  if (mWindowActorDescriptors.Contains(aName)) {
    aRv.ThrowNotSupportedError(
        nsPrintfCString("'%s' actor is already registered as a window actor.",
                        PromiseFlatCString(aName).get()));
    return;
  }

  const auto proto = mProcessActorDescriptors.WithEntryHandle(
      aName, [&](auto&& entry) -> RefPtr<JSProcessActorProtocol> {
        if (entry) {
          aRv.ThrowNotSupportedError(
              nsPrintfCString("'%s' actor is already registered.",
                              PromiseFlatCString(aName).get()));
          return nullptr;
        }

        // Insert a new entry for the protocol.
        RefPtr<JSProcessActorProtocol> protocol =
            JSProcessActorProtocol::FromWebIDLOptions(aName, aOptions, aRv);
        if (NS_WARN_IF(aRv.Failed())) {
          return nullptr;
        }

        entry.Insert(protocol);

        return protocol;
      });

  if (!proto) {
    MOZ_ASSERT(aRv.Failed());
    return;
  }

  // Send information about the newly added entry to every existing content
  // process.
  AutoTArray<JSProcessActorInfo, 1> contentInfos{proto->ToIPC()};
  nsTArray<JSWindowActorInfo> windowInfos{};
  for (auto* cp : ContentParent::AllProcesses(ContentParent::eLive)) {
    (void)cp->SendInitJSActorInfos(contentInfos, windowInfos);
  }

  // Add observers to the protocol.
  proto->AddObservers();
}

void JSActorService::UnregisterProcessActor(const nsACString& aName) {
  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());
  CrashReporter::AutoRecordAnnotation autoActorName(
      CrashReporter::Annotation::JSActorName, aName);
  CrashReporter::AutoRecordAnnotation autoMessageName(
      CrashReporter::Annotation::JSActorMessage, "<Unregister>"_ns);

  nsAutoCString name(aName);
  RefPtr<JSProcessActorProtocol> proto;
  if (mProcessActorDescriptors.Remove(name, getter_AddRefs(proto))) {
    // Remove observers for this actor from observer service.
    proto->RemoveObservers();

    // Tell every content process to also unregister, and accumulate the set of
    // potential managers, to have the actor disabled.
    nsTArray<RefPtr<JSActorManager>> managers;
    if (XRE_IsParentProcess()) {
      for (auto* cp : ContentParent::AllProcesses(ContentParent::eAll)) {
        if (cp->CanSend()) {
          (void)cp->SendUnregisterJSProcessActor(name);
        }
        managers.AppendElement(cp);
      }
      managers.AppendElement(InProcessChild::Singleton());
      managers.AppendElement(InProcessParent::Singleton());
    } else {
      managers.AppendElement(ContentChild::GetSingleton());
    }

    for (auto& mgr : managers) {
      mgr->JSActorUnregister(name);
    }
  }
}

void JSActorService::GetJSProcessActorInfos(
    nsTArray<JSProcessActorInfo>& aInfos) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());

  for (const auto& data : mProcessActorDescriptors.Values()) {
    aInfos.AppendElement(data->ToIPC());
  }
}

already_AddRefed<JSProcessActorProtocol>
JSActorService::GetJSProcessActorProtocol(const nsACString& aName) {
  return mProcessActorDescriptors.Get(aName);
}

already_AddRefed<JSWindowActorProtocol>
JSActorService::GetJSWindowActorProtocol(const nsACString& aName) {
  return mWindowActorDescriptors.Get(aName);
}

bool JSActorProtocol::RemoteTypePrefixMatches(const nsACString& aRemoteType) {
  if (mRemoteTypes.IsEmpty()) {
    return true;
  }

  nsDependentCSubstring remoteTypePrefix(RemoteTypePrefix(aRemoteType));
  for (auto& remoteType : mRemoteTypes) {
    // TODO: Maybe this should use glob-style matching instead. See bug 2006165.
    if (StringBeginsWith(remoteTypePrefix, remoteType)) {
      return true;
    }
  }
  return false;
}

}  // namespace mozilla::dom
