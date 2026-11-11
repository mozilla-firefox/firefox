/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef OscillatorNode_h_
#define OscillatorNode_h_

#include "AudioParam.h"
#include "AudioScheduledSourceNode.h"
#include "PeriodicWave.h"
#include "mozilla/StateMirroring.h"
#include "mozilla/StateWatching.h"
#include "mozilla/dom/OscillatorNodeBinding.h"

namespace mozilla::dom {

class AudioContext;
struct OscillatorOptions;

class OscillatorNode final : public AudioScheduledSourceNode {
 public:
  static already_AddRefed<OscillatorNode> Create(
      AudioContext& aAudioContext, const OscillatorOptions& aOptions,
      ErrorResult& aRv);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(OscillatorNode,
                                           AudioScheduledSourceNode)

  static already_AddRefed<OscillatorNode> Constructor(
      const GlobalObject& aGlobal, AudioContext& aAudioContext,
      const OscillatorOptions& aOptions, ErrorResult& aRv) {
    return Create(aAudioContext, aOptions, aRv);
  }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  void DestroyMediaTrack() override;

  uint16_t NumberOfInputs() const final { return 0; }

  OscillatorType Type() const { return mType; }
  void SetType(OscillatorType aType, ErrorResult& aRv) {
    if (aType == OscillatorType::Custom) {
      // ::Custom can only be set by setPeriodicWave().
      // https://github.com/WebAudio/web-audio-api/issues/105 for exception.
      aRv.ThrowInvalidStateError("Can't set type to \"custom\"");
      return;
    }
    mType = aType;
    SendTypeToTrack();
  }

  AudioParam* Frequency() const { return mFrequency; }
  AudioParam* Detune() const { return mDetune; }

  void Start(double aWhen, ErrorResult& aRv) override;
  void Stop(double aWhen, ErrorResult& aRv) override;

  void SetPeriodicWave(PeriodicWave& aPeriodicWave) {
    mPeriodicWave = &aPeriodicWave;
    // SendTypeToTrack will call SendPeriodicWaveToTrack for us.
    mType = OscillatorType::Custom;
    SendTypeToTrack();
  }

  const char* NodeType() const override { return "OscillatorNode"; }

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const override;
  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const override;

 private:
  explicit OscillatorNode(AudioContext* aContext);
  ~OscillatorNode() = default;

  void SendTypeToTrack();
  void SendPeriodicWaveToTrack();
  void OnTrackEnded();

  OscillatorType mType;
  RefPtr<PeriodicWave> mPeriodicWave;
  RefPtr<AudioParam> mFrequency;
  RefPtr<AudioParam> mDetune;
  bool mStartCalled;
  WatchManager<OscillatorNode> mWatchManager;
  Mirror<bool> mTrackEnded;
};

}  // namespace mozilla::dom

#endif
