/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_AudioWorkletGlobalScope_h
#define mozilla_dom_AudioWorkletGlobalScope_h

#include "js/ForOfIterator.h"
#include "mozilla/dom/AudioParamDescriptorMap.h"
#include "mozilla/dom/FunctionBinding.h"
#include "mozilla/dom/MessagePort.h"
#include "mozilla/dom/WorkletGlobalScope.h"
#include "mozilla/dom/WorkletPerformance.h"
#include "nsRefPtrHashtable.h"

namespace mozilla {

class AudioWorkletImpl;

namespace dom {

class AudioWorkletProcessorConstructor;
class StructuredCloneHolder;
class UniqueMessagePortId;

class AudioWorkletGlobalScope final : public WorkletGlobalScope {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(AudioWorkletGlobalScope,
                                           WorkletGlobalScope);

  explicit AudioWorkletGlobalScope(AudioWorkletImpl* aImpl);

  bool WrapGlobalObject(JSContext* aCx,
                        JS::MutableHandle<JSObject*> aReflector) override;

  void RegisterProcessor(JSContext* aCx, const nsAString& aName,
                         AudioWorkletProcessorConstructor& aProcessorCtor,
                         ErrorResult& aRv);

  AudioWorkletImpl* Impl() const;

  uint64_t CurrentFrame() const;

  double CurrentTime() const;

  float SampleRate() const;

  MessagePort* Port() const { return mPort; };

  void SetPort(MessagePort* aPort) { mPort = aPort; }

  // If successful, returns true and sets aRetProcessor, which will be in the
  // compartment for the realm of this global.  Returns false on failure.
  MOZ_CAN_RUN_SCRIPT
  bool ConstructProcessor(JSContext* aCx, const nsAString& aName,
                          NotNull<StructuredCloneHolder*> aSerializedOptions,
                          UniqueMessagePortId& aPortIdentifier,
                          JS::MutableHandle<JSObject*> aRetProcessor);

  // Returns null if not called during ConstructProcessor() or if the port has
  // already been taken.
  RefPtr<MessagePort> TakePortForProcessorCtor();

  WorkletPerformance* Performance() const { return mPerformance; }

 private:
  ~AudioWorkletGlobalScope() = default;

  // Returns an AudioParamDescriptorMap filled with AudioParamDescriptor
  // objects, extracted from JS. Returns an empty map in case of error and set
  // aRv accordingly.
  AudioParamDescriptorMap DescriptorsFromJS(JSContext* aCx,
                                            JS::ForOfIterator* aIter,
                                            ErrorResult& aRv);

  typedef nsRefPtrHashtable<nsStringHashKey, AudioWorkletProcessorConstructor>
      NodeNameToProcessorDefinitionMap;
  NodeNameToProcessorDefinitionMap mNameToProcessorMap;
  // https://webaudio.github.io/web-audio-api/#pending-processor-construction-data-transferred-port
  // This does not need to be traversed during cycle-collection because it is
  // only set while this AudioWorkletGlobalScope is on the stack.
  RefPtr<MessagePort> mPortForProcessor;

  RefPtr<MessagePort> mPort;

  RefPtr<WorkletPerformance> mPerformance;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_AudioWorkletGlobalScope_h
