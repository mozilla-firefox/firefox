/*
 * Copyright 2016 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "wasm/WasmRealm.h"

#include "gc/Marking.h"
#include "vm/GlobalObject.h"
#include "vm/Realm.h"
#include "wasm/WasmDebug.h"
#include "wasm/WasmInstance.h"
#include "wasm/WasmProcess.h"

#include "debugger/DebugAPI-inl.h"
#include "wasm/WasmInstance-inl.h"

using namespace js;
using namespace wasm;

wasm::Realm::Realm(JSRuntime* rt) : runtime_(rt) {}

wasm::Realm::~Realm() { MOZ_ASSERT(instances_.empty()); }

bool wasm::Realm::registerInstance(JSContext* cx,
                                   Handle<WasmInstanceObject*> instanceObj) {
  MOZ_ASSERT(runtime_ == cx->runtime());

  Instance& instance = instanceObj->instance();
  MOZ_ASSERT(this == &instance.realm()->wasm);

  instance.ensureProfilingLabels(cx->runtime()->geckoProfiler().enabled());

  if (instance.debugEnabled() &&
      instance.realm()->debuggerObservesAllExecution()) {
    instance.debug().ensureEnterFrameTrapsState(cx, &instance, true);
  }

  {
    if (!instances_.putNew(&instance)) {
      return false;
    }

    auto runtimeInstances = cx->runtime()->wasmInstances.lock();
    if (!runtimeInstances->putNew(&instance)) {
      instances_.remove(&instance);
      return false;
    }
  }

  // Notify the debugger after wasmInstances is unlocked.
  if (!instance.codeMeta().isSelfHostedModule()) {
    DebugAPI::onNewWasmInstance(cx, instanceObj);
  }
  return true;
}

void wasm::Realm::unregisterInstance(Instance& instance) {
  instances_.remove(&instance);

  auto runtimeInstances = runtime_->wasmInstances.lock();
  runtimeInstances->remove(&instance);
}

void wasm::Realm::traceWeakInstances() {
  // Registration/unregistration of instances_ is tied to Instance lifetime, so
  // an instance whose owning object is about to be finalized is still present
  // here until ~Instance runs. Remove such entries now, at the start of zone
  // sweeping, because the instances() read barrier that otherwise protects
  // readers is a no-op once the zone is being swept.
  for (auto iter = instances_.modIter(); !iter.done(); iter.next()) {
    if (js::gc::IsAboutToBeFinalizedUnbarriered(
            iter.get()->objectUnbarriered())) {
      iter.remove();
    }
  }
}

void wasm::Realm::ensureProfilingLabels(bool profilingEnabled) {
  for (auto iter = instances_.iter(); !iter.done(); iter.next()) {
    iter.get()->ensureProfilingLabels(profilingEnabled);
  }
}

void wasm::Realm::resetPretenuredAllocSites() {
  for (auto iter = instances_.iter(); !iter.done(); iter.next()) {
    iter.get()->resetPretenuredAllocSites();
  }
}

void wasm::Realm::addSizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf,
                                         size_t* realmTables) {
  *realmTables += instances_.shallowSizeOfExcludingThis(mallocSizeOf);
}

void wasm::InterruptRunningCode(JSContext* cx) {
  auto runtimeInstances = cx->runtime()->wasmInstances.lock();
  for (auto iter = runtimeInstances->iter(); !iter.done(); iter.next()) {
    iter.get()->setInterrupt();
  }
}

void wasm::ResetInterruptState(JSContext* cx) {
  auto runtimeInstances = cx->runtime()->wasmInstances.lock();
  for (auto iter = runtimeInstances->iter(); !iter.done(); iter.next()) {
    iter.get()->resetInterrupt();
  }
}
