/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ConstantSourceNode_h_
#define ConstantSourceNode_h_

#include "AudioParam.h"
#include "AudioScheduledSourceNode.h"
#include "mozilla/StateMirroring.h"
#include "mozilla/StateWatching.h"
#include "mozilla/dom/ConstantSourceNodeBinding.h"

namespace mozilla::dom {

class AudioContext;

class ConstantSourceNode final : public AudioScheduledSourceNode {
 public:
  explicit ConstantSourceNode(AudioContext* aContext);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(ConstantSourceNode,
                                           AudioScheduledSourceNode)

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  static already_AddRefed<ConstantSourceNode> Constructor(
      const GlobalObject& aGlobal, AudioContext& aContext,
      const ConstantSourceOptions& aOptions);

  void DestroyMediaTrack() override;

  uint16_t NumberOfInputs() const final { return 0; }

  AudioParam* Offset() const { return mOffset; }

  void Start(double aWhen, ErrorResult& rv) override;
  void Stop(double aWhen, ErrorResult& rv) override;

  const char* NodeType() const override { return "ConstantSourceNode"; }

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const override;
  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const override;

 protected:
  virtual ~ConstantSourceNode();

 private:
  void OnTrackEnded();

  RefPtr<AudioParam> mOffset;
  bool mStartCalled;
  WatchManager<ConstantSourceNode> mWatchManager;
  Mirror<bool> mTrackEnded;
};

}  // namespace mozilla::dom

#endif
