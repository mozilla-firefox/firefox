/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioCaptureTrack.h"
#include "AudioDeviceInfo.h"
#include "AudioNodeExternalInputTrack.h"
#include "AudioNodeTrack.h"
#include "AudioSegment.h"
#include "CrossGraphPort.h"
#include "CubebDeviceEnumerator.h"
#include "ForwardedInputTrack.h"
#include "ImageContainer.h"
#include "MediaTrackGraphImpl.h"
#include "VideoSegment.h"
#include "mozilla/Attributes.h"
#include "mozilla/Logging.h"
#include "nsContentUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "prerror.h"
#if defined(MOZ_WEBRTC)
#  include "MediaEngineWebRTCAudio.h"
#endif  // MOZ_WEBRTC
#include <algorithm>

#include "GeckoProfiler.h"
#include "GraphRunner.h"
#include "MediaTrackListener.h"
#include "Tracing.h"
#include "UnderrunHandler.h"
#include "VideoFrameContainer.h"
#include "VideoUtils.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/BaseAudioContextBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/WorkletThread.h"
#include "mozilla/media/MediaUtils.h"
#include "transport/runnable_utils.h"
#include "webaudio/blink/DenormalDisabler.h"
#include "webaudio/blink/HRTFDatabaseLoader.h"

using namespace mozilla::layers;
using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::media;

namespace mozilla {

using AudioDeviceID = CubebUtils::AudioDeviceID;
using IsInShutdown = MediaTrack::IsInShutdown;

LazyLogModule gMediaTrackGraphLog("MediaTrackGraph");
#define LOG(type, ...) \
  MOZ_LOG_FMT(gMediaTrackGraphLog, type, MOZ_LOG_EXPAND_ARGS __VA_ARGS__)

NativeInputTrack* DeviceInputTrackManager::GetNativeInputTrack() {
  return mNativeInputTrack.get();
}

DeviceInputTrack* DeviceInputTrackManager::GetDeviceInputTrack(
    CubebUtils::AudioDeviceID aID) {
  if (mNativeInputTrack && mNativeInputTrack->mDeviceId == aID) {
    return mNativeInputTrack.get();
  }
  for (const RefPtr<NonNativeInputTrack>& t : mNonNativeInputTracks) {
    if (t->mDeviceId == aID) {
      return t.get();
    }
  }
  return nullptr;
}

NonNativeInputTrack* DeviceInputTrackManager::GetFirstNonNativeInputTrack() {
  if (mNonNativeInputTracks.IsEmpty()) {
    return nullptr;
  }
  return mNonNativeInputTracks[0].get();
}

void DeviceInputTrackManager::Add(DeviceInputTrack* aTrack) {
  if (NativeInputTrack* native = aTrack->AsNativeInputTrack()) {
    MOZ_ASSERT(!mNativeInputTrack);
    mNativeInputTrack = native;
  } else {
    NonNativeInputTrack* nonNative = aTrack->AsNonNativeInputTrack();
    MOZ_ASSERT(nonNative);
    struct DeviceTrackComparator {
     public:
      bool Equals(const RefPtr<NonNativeInputTrack>& aTrack,
                  CubebUtils::AudioDeviceID aDeviceId) const {
        return aTrack->mDeviceId == aDeviceId;
      }
    };
    MOZ_ASSERT(!mNonNativeInputTracks.Contains(aTrack->mDeviceId,
                                               DeviceTrackComparator()));
    mNonNativeInputTracks.AppendElement(nonNative);
  }
}

void DeviceInputTrackManager::Remove(DeviceInputTrack* aTrack) {
  if (aTrack->AsNativeInputTrack()) {
    MOZ_ASSERT(mNativeInputTrack);
    MOZ_ASSERT(mNativeInputTrack.get() == aTrack->AsNativeInputTrack());
    mNativeInputTrack = nullptr;
  } else {
    NonNativeInputTrack* nonNative = aTrack->AsNonNativeInputTrack();
    MOZ_ASSERT(nonNative);
    DebugOnly<bool> removed = mNonNativeInputTracks.RemoveElement(nonNative);
    MOZ_ASSERT(removed);
  }
}

/**
 * A hash table containing the graph instances, one per Window ID,
 * sample rate, and device ID combination.
 */

struct MediaTrackGraphImpl::Lookup final {
  HashNumber Hash() const {
    return HashGeneric(mWindowID, mSampleRate, mOutputDeviceID);
  }
  const uint64_t mWindowID;
  const TrackRate mSampleRate;
  const CubebUtils::AudioDeviceID mOutputDeviceID;
};

// Implicit to support GraphHashSet.lookup(*graph).
MOZ_IMPLICIT MediaTrackGraphImpl::operator MediaTrackGraphImpl::Lookup() const {
  return {mWindowID, mSampleRate, PrimaryOutputDeviceID()};
}

namespace {
struct GraphHasher {  // for HashSet
  using Lookup = const MediaTrackGraphImpl::Lookup;

  static HashNumber hash(const Lookup& aLookup) { return aLookup.Hash(); }

  static bool match(const MediaTrackGraphImpl* aGraph, const Lookup& aLookup) {
    return aGraph->mWindowID == aLookup.mWindowID &&
           aGraph->GraphRate() == aLookup.mSampleRate &&
           aGraph->PrimaryOutputDeviceID() == aLookup.mOutputDeviceID;
  }
};

// The weak reference to the graph is removed when its last track is removed.
using GraphHashSet =
    HashSet<MediaTrackGraphImpl*, GraphHasher, InfallibleAllocPolicy>;
GraphHashSet* Graphs() {
  MOZ_ASSERT(NS_IsMainThread());
  static GraphHashSet sGraphs(4);  // 4 is minimum HashSet capacity
  return &sGraphs;
}
}  // anonymous namespace

static void ApplyTrackDisabling(DisabledTrackMode aDisabledMode,
                                MediaSegment* aSegment,
                                MediaSegment* aRawSegment) {
  if (aDisabledMode == DisabledTrackMode::ENABLED) {
    return;
  }
  if (aDisabledMode == DisabledTrackMode::SILENCE_BLACK) {
    aSegment->ReplaceWithDisabled();
    if (aRawSegment) {
      aRawSegment->ReplaceWithDisabled();
    }
  } else if (aDisabledMode == DisabledTrackMode::SILENCE_FREEZE) {
    aSegment->ReplaceWithNull();
    if (aRawSegment) {
      aRawSegment->ReplaceWithNull();
    }
  } else {
    MOZ_CRASH("Unsupported mode");
  }
}

MediaTrackGraphImpl::~MediaTrackGraphImpl() {
  MOZ_ASSERT(mTracks.IsEmpty() && mSuspendedTracks.IsEmpty(),
             "All tracks should have been destroyed by messages from the main "
             "thread");
  LOG(LogLevel::Debug, ("MediaTrackGraph {} destroyed", fmt::ptr(this)));
  LOG(LogLevel::Debug, ("MediaTrackGraphImpl::~MediaTrackGraphImpl"));
}

void MediaTrackGraphImpl::AddTrackGraphThread(MediaTrack* aTrack) {
  MOZ_ASSERT(OnGraphThreadOrNotRunning());
  aTrack->mStartTime = mProcessedTime;

  if (aTrack->IsSuspended()) {
    mSuspendedTracks.AppendElement(aTrack);
    LOG(LogLevel::Debug,
        ("{}: Adding media track {}, in the suspended track array",
         fmt::ptr(this), fmt::ptr(aTrack)));
  } else {
    mTracks.AppendElement(aTrack);
    LOG(LogLevel::Debug, ("{}:  Adding media track {}, count {}",
                          fmt::ptr(this), fmt::ptr(aTrack), mTracks.Length()));
  }

  SetTrackOrderDirty();
}

void MediaTrackGraphImpl::RemoveTrackGraphThread(MediaTrack* aTrack) {
  MOZ_ASSERT(OnGraphThreadOrNotRunning());
  // Remove references in mTrackUpdates before we allow aTrack to die.
  // Pending updates are not needed (since the main thread has already given
  // up the track) so we will just drop them.
  {
    MonitorAutoLock lock(mMonitor);
    for (uint32_t i = 0; i < mTrackUpdates.Length(); ++i) {
      if (mTrackUpdates[i].mTrack == aTrack) {
        mTrackUpdates[i].mTrack = nullptr;
      }
    }
  }

  // Ensure that mFirstCycleBreaker is updated when necessary.
  SetTrackOrderDirty();

  UnregisterAllAudioOutputs(aTrack);

  if (aTrack->IsSuspended()) {
    const bool removed = mSuspendedTracks.RemoveElement(aTrack);
    MOZ_DIAGNOSTIC_ASSERT(removed, "Suspended track not in mSuspendedTracks");
    if (!removed) {
      mTracks.RemoveElement(aTrack);
    }
  } else {
    const bool removed = mTracks.RemoveElement(aTrack);
    MOZ_DIAGNOSTIC_ASSERT(removed, "Non-suspended track not in mTracks");
    if (!removed) {
      mSuspendedTracks.RemoveElement(aTrack);
    }
  }

  LOG(LogLevel::Debug, ("{}: Removed media track {}, count {}", fmt::ptr(this),
                        fmt::ptr(aTrack), mTracks.Length()));

  NS_RELEASE(aTrack);  // probably destroying it
}

TrackTime MediaTrackGraphImpl::GraphTimeToTrackTimeWithBlocking(
    const MediaTrack* aTrack, GraphTime aTime) const {
  MOZ_ASSERT(
      aTime <= mStateComputedTime,
      "Don't ask about times where we haven't made blocking decisions yet");
  return std::max<TrackTime>(
      0, std::min(aTime, aTrack->mStartBlocking) - aTrack->mStartTime);
}

void MediaTrackGraphImpl::UpdateCurrentTimeForTracks(
    GraphTime aPrevCurrentTime) {
  MOZ_ASSERT(OnGraphThread());
  for (MediaTrack* track : AllTracks()) {
    // Shouldn't have already notified of ended *and* have output!
    MOZ_ASSERT_IF(track->mStartBlocking > aPrevCurrentTime,
                  !track->mNotifiedEnded);

    // Calculate blocked time and fire Blocked/Unblocked events
    GraphTime blockedTime = mStateComputedTime - track->mStartBlocking;
    NS_ASSERTION(blockedTime >= 0, "Error in blocking time");
    track->AdvanceTimeVaryingValuesToCurrentTime(mStateComputedTime,
                                                 blockedTime);
    LOG(LogLevel::Verbose,
        ("{}: MediaTrack {} bufferStartTime={} blockedTime={}", fmt::ptr(this),
         fmt::ptr(track), MediaTimeToSeconds(track->mStartTime),
         MediaTimeToSeconds(blockedTime)));
    track->mStartBlocking = mStateComputedTime;

    TrackTime trackCurrentTime =
        track->GraphTimeToTrackTime(mStateComputedTime);
    if (track->mEnded) {
      MOZ_ASSERT(track->GetEnd() <= trackCurrentTime);
      if (!track->mNotifiedEnded) {
        // Playout of this track ended and listeners have not been notified.
        track->mNotifiedEnded = true;
        SetTrackOrderDirty();
        for (const auto& listener : track->mTrackListeners) {
          listener->NotifyOutput(this, track->GetEnd());
          listener->NotifyEnded(this);
        }
      }
    } else {
      for (const auto& listener : track->mTrackListeners) {
        listener->NotifyOutput(this, trackCurrentTime);
      }
    }
  }
}

template <typename C, typename Chunk>
void MediaTrackGraphImpl::ProcessChunkMetadataForInterval(MediaTrack* aTrack,
                                                          C& aSegment,
                                                          TrackTime aStart,
                                                          TrackTime aEnd) {
  MOZ_ASSERT(OnGraphThreadOrNotRunning());
  MOZ_ASSERT(aTrack);

  TrackTime offset = 0;
  for (typename C::ConstChunkIterator chunk(aSegment); !chunk.IsEnded();
       chunk.Next()) {
    if (offset >= aEnd) {
      break;
    }
    offset += chunk->GetDuration();
    if (chunk->IsNull() || offset < aStart) {
      continue;
    }
    const PrincipalHandle& principalHandle = chunk->GetPrincipalHandle();
    if (principalHandle != aSegment.GetLastPrincipalHandle()) {
      aSegment.SetLastPrincipalHandle(principalHandle);
      LOG(LogLevel::Debug,
          ("{}: MediaTrack {}, principalHandle "
           "changed in {}Chunk with duration {}",
           fmt::ptr(this), fmt::ptr(aTrack),
           aSegment.GetType() == MediaSegment::AUDIO ? "Audio" : "Video",
           (long long)chunk->GetDuration()));
      for (const auto& listener : aTrack->mTrackListeners) {
        listener->NotifyPrincipalHandleChanged(this, principalHandle);
      }
    }
  }
}

void MediaTrackGraphImpl::ProcessChunkMetadata(GraphTime aPrevCurrentTime) {
  MOZ_ASSERT(OnGraphThreadOrNotRunning());
  for (MediaTrack* track : AllTracks()) {
    TrackTime iterationStart = track->GraphTimeToTrackTime(aPrevCurrentTime);
    TrackTime iterationEnd = track->GraphTimeToTrackTime(mProcessedTime);
    if (!track->mSegment) {
      continue;
    }
    if (track->mType == MediaSegment::AUDIO) {
      ProcessChunkMetadataForInterval<AudioSegment, AudioChunk>(
          track, *track->GetData<AudioSegment>(), iterationStart, iterationEnd);
    } else if (track->mType == MediaSegment::VIDEO) {
      ProcessChunkMetadataForInterval<VideoSegment, VideoChunk>(
          track, *track->GetData<VideoSegment>(), iterationStart, iterationEnd);
    } else {
      MOZ_CRASH("Unknown track type");
    }
  }
}

GraphTime MediaTrackGraphImpl::WillUnderrun(MediaTrack* aTrack,
                                            GraphTime aEndBlockingDecisions) {
  // Ended tracks can't underrun. ProcessedMediaTracks also can't cause
  // underrun currently, since we'll always be able to produce data for them
  // unless they block on some other track.
  if (aTrack->mEnded || aTrack->AsProcessedTrack()) {
    return aEndBlockingDecisions;
  }
  // This track isn't ended or suspended. We don't need to call
  // TrackTimeToGraphTime since an underrun is the only thing that can block
  // it.
  GraphTime bufferEnd = aTrack->GetEnd() + aTrack->mStartTime;
#ifdef DEBUG
  if (bufferEnd < mProcessedTime) {
    LOG(LogLevel::Error,
        ("{}: MediaTrack {} underrun, "
         "bufferEnd {} < mProcessedTime {} ({} < {}), TrackTime {}",
         fmt::ptr(this), fmt::ptr(aTrack), MediaTimeToSeconds(bufferEnd),
         MediaTimeToSeconds(mProcessedTime), bufferEnd, mProcessedTime,
         aTrack->GetEnd()));
    NS_ASSERTION(bufferEnd >= mProcessedTime, "Buffer underran");
  }
#endif
  return std::min(bufferEnd, aEndBlockingDecisions);
}

namespace {
// Value of mCycleMarker for unvisited tracks in cycle detection.
const uint32_t NOT_VISITED = UINT32_MAX;
// Value of mCycleMarker for ordered tracks in muted cycles.
const uint32_t IN_MUTED_CYCLE = 1;
}  // namespace

bool MediaTrackGraphImpl::AudioTrackPresent() {
  MOZ_ASSERT(OnGraphThreadOrNotRunning());

  bool audioTrackPresent = false;
  for (MediaTrack* track : mTracks) {
    if (track->AsAudioNodeTrack()) {
      audioTrackPresent = true;
      break;
    }

    if (track->mType == MediaSegment::AUDIO && !track->mNotifiedEnded) {
      audioTrackPresent = true;
      break;
    }
  }

  // We may not have audio input device when we only have AudioNodeTracks. But
  // if audioTrackPresent is false, we must have no input device.
  MOZ_DIAGNOSTIC_ASSERT_IF(
      !audioTrackPresent,
      !mDeviceInputTrackManagerGraphThread.GetNativeInputTrack());

  return audioTrackPresent;
}

void MediaTrackGraphImpl::CheckDriver() {
  MOZ_ASSERT(OnGraphThread());
  // An offline graph has only one driver.
  if (!mRealtime) {
    return;
  }

  AudioCallbackDriver* audioCallbackDriver =
      CurrentDriver()->AsAudioCallbackDriver();
  if (audioCallbackDriver && !audioCallbackDriver->OnFallback()) {
    for (PendingResumeOperation& op : mPendingResumeOperations) {
      op.Apply(this);
    }
    mPendingResumeOperations.Clear();
  }

  // Note that this looks for any audio tracks, input or output, and switches
  // to a SystemClockDriver if there are none active or no resume operations
  // to make any active.
  bool needAudioCallbackDriver =
      !mPendingResumeOperations.IsEmpty() || AudioTrackPresent();
  if (!needAudioCallbackDriver) {
    if (audioCallbackDriver && audioCallbackDriver->IsStarted()) {
      SwitchAtNextIteration(
          new SystemClockDriver(this, CurrentDriver(), mSampleRate));
    }
    return;
  }

  NativeInputTrack* native =
      mDeviceInputTrackManagerGraphThread.GetNativeInputTrack();
  CubebUtils::AudioDeviceID inputDevice = native ? native->mDeviceId : nullptr;
  uint32_t inputChannelCount = AudioInputChannelCount(inputDevice);
  AudioInputType inputPreference = AudioInputDevicePreference(inputDevice);
  Maybe<AudioInputProcessingParamsRequest> processingRequest =
      ToMaybeRef(native).map([](auto& native) {
        return native.UpdateRequestedProcessingParams();
      });

  uint32_t primaryOutputChannelCount = PrimaryOutputChannelCount();
  if (!audioCallbackDriver) {
    if (primaryOutputChannelCount > 0) {
      AudioCallbackDriver* driver = new AudioCallbackDriver(
          this, CurrentDriver(), mSampleRate, primaryOutputChannelCount,
          inputChannelCount, PrimaryOutputDeviceID(), inputDevice,
          inputPreference, processingRequest);
      SwitchAtNextIteration(driver);
    }
    return;
  }

  // Check if a new driver is required for a configuration change.  Generally,
  // a driver switch is explicitly triggered by an event (e.g., setting
  // the AudioDestinationNode channelCount), but if an HTMLMediaElement is
  // directly playing back via another HTMLMediaElement, the number of channels
  // of the media determines how many channels to output, and it can change
  // dynamically.
  if (primaryOutputChannelCount != audioCallbackDriver->OutputChannelCount() ||
      inputDevice != audioCallbackDriver->InputDeviceID() ||
      inputChannelCount != audioCallbackDriver->InputChannelCount() ||
      inputPreference != audioCallbackDriver->InputDevicePreference()) {
    AudioCallbackDriver* driver = new AudioCallbackDriver(
        this, CurrentDriver(), mSampleRate, primaryOutputChannelCount,
        inputChannelCount, PrimaryOutputDeviceID(), inputDevice,
        inputPreference, processingRequest);
    SwitchAtNextIteration(driver);
    return;
  }

  if (processingRequest &&
      processingRequest->mGeneration !=
          audioCallbackDriver->RequestedInputProcessingParams().mGeneration) {
    LOG(LogLevel::Debug,
        ("{}: Setting on the fly requested processing params {} (Gen {})",
         fmt::ptr(this),
         CubebUtils::ProcessingParamsToString(processingRequest->mParams).get(),
         processingRequest->mGeneration));
    audioCallbackDriver->RequestInputProcessingParams(*processingRequest);
  }
}

void MediaTrackGraphImpl::UpdateTrackOrder() {
  if (!mTrackOrderDirty) {
    return;
  }

  mTrackOrderDirty = false;

  // The algorithm for finding cycles is based on Tim Leslie's iterative
  // implementation [1][2] of Pearce's variant [3] of Tarjan's strongly
  // connected components (SCC) algorithm.  There are variations (a) to
  // distinguish whether tracks in SCCs of size 1 are in a cycle and (b) to
  // re-run the algorithm over SCCs with breaks at DelayNodes.
  //
  // [1] http://www.timl.id.au/?p=327
  // [2]
  // https://github.com/scipy/scipy/blob/e2c502fca/scipy/sparse/csgraph/_traversal.pyx#L582
  // [3] http://citeseerx.ist.psu.edu/viewdoc/summary?doi=10.1.1.102.1707
  //
  // There are two stacks.  One for the depth-first search (DFS),
  mozilla::LinkedList<MediaTrack> dfsStack;
  // and another for tracks popped from the DFS stack, but still being
  // considered as part of SCCs involving tracks on the stack.
  mozilla::LinkedList<MediaTrack> sccStack;

  // An index into mTracks for the next track found with no unsatisfied
  // upstream dependencies.
  uint32_t orderedTrackCount = 0;

  for (uint32_t i = 0; i < mTracks.Length(); ++i) {
    MediaTrack* t = mTracks[i];
    ProcessedMediaTrack* pt = t->AsProcessedTrack();
    if (pt) {
      // The dfsStack initially contains a list of all processed tracks in
      // unchanged order.
      dfsStack.insertBack(t);
      pt->mCycleMarker = NOT_VISITED;
    } else {
      // SourceMediaTracks have no inputs and so can be ordered now.
      mTracks[orderedTrackCount] = t;
      ++orderedTrackCount;
    }
  }

  // mNextStackMarker corresponds to "index" in Tarjan's algorithm.  It is a
  // counter to label mCycleMarker on the next visited track in the DFS
  // uniquely in the set of visited tracks that are still being considered.
  //
  // In this implementation, the counter descends so that the values are
  // strictly greater than the values that mCycleMarker takes when the track
  // has been ordered (0 or IN_MUTED_CYCLE).
  //
  // Each new track labelled, as the DFS searches upstream, receives a value
  // less than those used for all other tracks being considered.
  uint32_t nextStackMarker = NOT_VISITED - 1;
  // Reset list of DelayNodes in cycles stored at the tail of mTracks.
  mFirstCycleBreaker = mTracks.Length();

  // Rearrange dfsStack order as required to DFS upstream and pop tracks
  // in processing order to place in mTracks.
  while (auto pt = static_cast<ProcessedMediaTrack*>(dfsStack.getFirst())) {
    const auto& inputs = pt->mInputs;
    MOZ_ASSERT(pt->AsProcessedTrack());
    if (pt->mCycleMarker == NOT_VISITED) {
      // Record the position on the visited stack, so that any searches
      // finding this track again know how much of the stack is in the cycle.
      pt->mCycleMarker = nextStackMarker;
      --nextStackMarker;
      // Not-visited input tracks should be processed first.
      // SourceMediaTracks have already been ordered.
      for (uint32_t i = inputs.Length(); i--;) {
        if (inputs[i]->GetSource()->IsSuspended()) {
          continue;
        }
        auto input = inputs[i]->GetSource()->AsProcessedTrack();
        if (input && input->mCycleMarker == NOT_VISITED) {
          // It can be that this track has an input which is from a suspended
          // AudioContext.
          if (input->isInList()) {
            input->remove();
            dfsStack.insertFront(input);
          }
        }
      }
      continue;
    }

    // Returning from DFS.  Pop from dfsStack.
    pt->remove();

    // cycleStackMarker keeps track of the highest marker value on any
    // upstream track, if any, found receiving input, directly or indirectly,
    // from the visited stack (and so from |ps|, making a cycle).  In a
    // variation from Tarjan's SCC algorithm, this does not include |ps|
    // unless it is part of the cycle.
    uint32_t cycleStackMarker = 0;
    for (uint32_t i = inputs.Length(); i--;) {
      if (inputs[i]->GetSource()->IsSuspended()) {
        continue;
      }
      auto input = inputs[i]->GetSource()->AsProcessedTrack();
      if (input) {
        cycleStackMarker = std::max(cycleStackMarker, input->mCycleMarker);
      }
    }

    if (cycleStackMarker <= IN_MUTED_CYCLE) {
      // All inputs have been ordered and their stack markers have been removed.
      // This track is not part of a cycle.  It can be processed next.
      pt->mCycleMarker = 0;
      mTracks[orderedTrackCount] = pt;
      ++orderedTrackCount;
      continue;
    }

    // A cycle has been found.  Record this track for ordering when all
    // tracks in this SCC have been popped from the DFS stack.
    sccStack.insertFront(pt);

    if (cycleStackMarker > pt->mCycleMarker) {
      // Cycles have been found that involve tracks that remain on the stack.
      // Leave mCycleMarker indicating the most downstream (last) track on
      // the stack known to be part of this SCC.  In this way, any searches on
      // other paths that find |ps| will know (without having to traverse from
      // this track again) that they are part of this SCC (i.e. part of an
      // intersecting cycle).
      pt->mCycleMarker = cycleStackMarker;
      continue;
    }

    // |pit| is the root of an SCC involving no other tracks on dfsStack, the
    // complete SCC has been recorded, and tracks in this SCC are part of at
    // least one cycle.
    MOZ_ASSERT(cycleStackMarker == pt->mCycleMarker);
    // If there are DelayNodes in this SCC, then they may break the cycles.
    bool haveDelayNode = false;
    auto next = sccStack.getFirst();
    // Tracks in this SCC are identified by mCycleMarker <= cycleStackMarker.
    // (There may be other tracks later in sccStack from other incompletely
    // searched SCCs, involving tracks still on dfsStack.)
    //
    // DelayNodes in cycles must behave differently from those not in cycles,
    // so all DelayNodes in the SCC must be identified.
    while (next && static_cast<ProcessedMediaTrack*>(next)->mCycleMarker <=
                       cycleStackMarker) {
      auto nt = next->AsAudioNodeTrack();
      // Get next before perhaps removing from list below.
      next = next->getNext();
      if (nt && nt->Engine()->AsDelayNodeEngine()) {
        haveDelayNode = true;
        // DelayNodes break cycles by producing their output in a
        // preprocessing phase; they do not need to be ordered before their
        // consumers.  Order them at the tail of mTracks so that they can be
        // handled specially.  Do so now, so that DFS ignores them.
        nt->remove();
        nt->mCycleMarker = 0;
        --mFirstCycleBreaker;
        mTracks[mFirstCycleBreaker] = nt;
      }
    }
    auto after_scc = next;
    while ((next = sccStack.getFirst()) != after_scc) {
      next->remove();
      auto removed = static_cast<ProcessedMediaTrack*>(next);
      if (haveDelayNode) {
        // Return tracks to the DFS stack again (to order and detect cycles
        // without delayNodes).  Any of these tracks that are still inputs
        // for tracks on the visited stack must be returned to the front of
        // the stack to be ordered before their dependents.  We know that none
        // of these tracks need input from tracks on the visited stack, so
        // they can all be searched and ordered before the current stack head
        // is popped.
        removed->mCycleMarker = NOT_VISITED;
        dfsStack.insertFront(removed);
      } else {
        // Tracks in cycles without any DelayNodes must be muted, and so do
        // not need input and can be ordered now.  They must be ordered before
        // their consumers so that their muted output is available.
        removed->mCycleMarker = IN_MUTED_CYCLE;
        mTracks[orderedTrackCount] = removed;
        ++orderedTrackCount;
      }
    }
  }

  MOZ_ASSERT(orderedTrackCount == mFirstCycleBreaker);
}

TrackTime MediaTrackGraphImpl::PlayAudio(const TrackAndVolume& aOutput,
                                         GraphTime aPlayedTime,
                                         uint32_t aOutputChannelCount) {
  MOZ_ASSERT(OnGraphThread());
  MOZ_ASSERT(mRealtime, "Should only attempt to play audio in realtime mode");

  TrackTime ticksWritten = 0;

  ticksWritten = 0;
  MediaTrack* track = aOutput.mTrack;
  AudioSegment* audio = track->GetData<AudioSegment>();
  AudioSegment output;

  TrackTime offset = track->GraphTimeToTrackTime(aPlayedTime);

  // We don't update Track->mTracksStartTime here to account for time spent
  // blocked. Instead, we'll update it in UpdateCurrentTimeForTracks after
  // the blocked period has completed. But we do need to make sure we play
  // from the right offsets in the track buffer, even if we've already
  // written silence for some amount of blocked time after the current time.
  GraphTime t = aPlayedTime;
  while (t < mStateComputedTime) {
    bool blocked = t >= track->mStartBlocking;
    GraphTime end = blocked ? mStateComputedTime : track->mStartBlocking;
    NS_ASSERTION(end <= mStateComputedTime, "mStartBlocking is wrong!");

    // Check how many ticks of sound we can provide if we are blocked some
    // time in the middle of this cycle.
    TrackTime toWrite = end - t;

    if (blocked) {
      output.InsertNullDataAtStart(toWrite);
      ticksWritten += toWrite;
      LOG(LogLevel::Verbose,
          ("{}: MediaTrack {} writing {} blocking-silence samples for "
           "{} to {} ({} to {})",
           fmt::ptr(this), fmt::ptr(track), toWrite, MediaTimeToSeconds(t),
           MediaTimeToSeconds(end), offset, offset + toWrite));
    } else {
      TrackTime endTicksNeeded = offset + toWrite;
      TrackTime endTicksAvailable = audio->GetDuration();

      if (endTicksNeeded <= endTicksAvailable) {
        LOG(LogLevel::Verbose,
            ("{}: MediaTrack {} writing {} samples for {} to {} "
             "(samples {} to {})",
             fmt::ptr(this), fmt::ptr(track), toWrite, MediaTimeToSeconds(t),
             MediaTimeToSeconds(end), offset, endTicksNeeded));
        output.AppendSlice(*audio, offset, endTicksNeeded);
        ticksWritten += toWrite;
        offset = endTicksNeeded;
      } else {
        // MOZ_ASSERT(track->IsEnded(), "Not enough data, and track not
        // ended."); If we are at the end of the track, maybe write the
        // remaining samples, and pad with/output silence.
        if (endTicksNeeded > endTicksAvailable && offset < endTicksAvailable) {
          output.AppendSlice(*audio, offset, endTicksAvailable);

          LOG(LogLevel::Verbose,
              ("{}: MediaTrack {} writing {} samples for {} to {} "
               "(samples {} to {})",
               fmt::ptr(this), fmt::ptr(track), toWrite, MediaTimeToSeconds(t),
               MediaTimeToSeconds(end), offset, endTicksNeeded));
          uint32_t available = endTicksAvailable - offset;
          ticksWritten += available;
          toWrite -= available;
          offset = endTicksAvailable;
        }
        output.AppendNullData(toWrite);
        LOG(LogLevel::Verbose,
            ("{} MediaTrack {} writing {} padding slsamples for {} to "
             "{} (samples {} to {})",
             fmt::ptr(this), fmt::ptr(track), toWrite, MediaTimeToSeconds(t),
             MediaTimeToSeconds(end), offset, endTicksNeeded));
        ticksWritten += toWrite;
      }
      output.ApplyVolume(mGlobalVolume * aOutput.mVolume);
    }
    t = end;

    output.Mix(mMixer, aOutputChannelCount, mSampleRate);
  }
  return ticksWritten;
}

DeviceInputTrack* MediaTrackGraph::GetDeviceInputTrackMainThread(
    CubebUtils::AudioDeviceID aID) {
  MOZ_ASSERT(NS_IsMainThread());
  auto* impl = static_cast<MediaTrackGraphImpl*>(this);
  return impl->mDeviceInputTrackManagerMainThread.GetDeviceInputTrack(aID);
}

NativeInputTrack* MediaTrackGraph::GetNativeInputTrackMainThread() {
  MOZ_ASSERT(NS_IsMainThread());
  auto* impl = static_cast<MediaTrackGraphImpl*>(this);
  return impl->mDeviceInputTrackManagerMainThread.GetNativeInputTrack();
}

void MediaTrackGraphImpl::OpenAudioInputImpl(DeviceInputTrack* aTrack) {
  MOZ_ASSERT(OnGraphThread());
  LOG(LogLevel::Debug, ("{} OpenAudioInputImpl: device {}", fmt::ptr(this),
                        fmt::ptr(aTrack->mDeviceId)));

  mDeviceInputTrackManagerGraphThread.Add(aTrack);

  if (aTrack->AsNativeInputTrack()) {
    // CheckDriver() will check for changes, after all control messages are
    // processed.
    return;
  }
  NonNativeInputTrack* nonNative = aTrack->AsNonNativeInputTrack();
  MOZ_ASSERT(nonNative);
  // Start non-native input right away.
  nonNative->StartAudio(MakeRefPtr<AudioInputSource>(
      MakeRefPtr<AudioInputSourceListener>(nonNative),
      nonNative->GenerateSourceId(), nonNative->mDeviceId,
      AudioInputChannelCount(nonNative->mDeviceId),
      AudioInputDevicePreference(nonNative->mDeviceId) == AudioInputType::Voice,
      nonNative->mPrincipalHandle, nonNative->mSampleRate, GraphRate()));
}

void MediaTrackGraphImpl::OpenAudioInput(DeviceInputTrack* aTrack) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aTrack);

  LOG(LogLevel::Debug,
      ("{} OpenInput: DeviceInputTrack {} for device {}", fmt::ptr(this),
       fmt::ptr(aTrack), fmt::ptr(aTrack->mDeviceId)));

  class Message : public ControlMessage {
   public:
    Message(MediaTrackGraphImpl* aGraph, DeviceInputTrack* aInputTrack)
        : ControlMessage(nullptr), mGraph(aGraph), mInputTrack(aInputTrack) {}
    void Run() override {
      TRACE("MTG::OpenAudioInputImpl ControlMessage");
      mGraph->OpenAudioInputImpl(mInputTrack);
    }
    MediaTrackGraphImpl* mGraph;
    DeviceInputTrack* mInputTrack;
  };

  mDeviceInputTrackManagerMainThread.Add(aTrack);
  UpdateEnumeratorDefaultDeviceTracking();

  this->AppendMessage(MakeUnique<Message>(this, aTrack));
}

void MediaTrackGraphImpl::CloseAudioInputImpl(DeviceInputTrack* aTrack) {
  MOZ_ASSERT(OnGraphThread());

  LOG(LogLevel::Debug, ("{} CloseAudioInputImpl: device {}", fmt::ptr(this),
                        fmt::ptr(aTrack->mDeviceId)));

  if (NonNativeInputTrack* nonNative = aTrack->AsNonNativeInputTrack()) {
    nonNative->StopAudio();
    mDeviceInputTrackManagerGraphThread.Remove(aTrack);
    return;
  }

  MOZ_ASSERT(aTrack->AsNativeInputTrack());

  mDeviceInputTrackManagerGraphThread.Remove(aTrack);
  // CheckDriver() will check for changes, after all control messages are
  // processed.
}

void MediaTrackGraphImpl::UnregisterAllAudioOutputs(MediaTrack* aTrack) {
  MOZ_ASSERT(OnGraphThreadOrNotRunning());
  mOutputDevices.RemoveElementsBy([&](OutputDeviceEntry& aDeviceRef) {
    aDeviceRef.mTrackOutputs.RemoveElement(aTrack);
    // mReceiver is null for the primary output device, which is retained for
    // AudioCallbackDriver output even when no tracks have audio outputs.
    return aDeviceRef.mTrackOutputs.IsEmpty() && aDeviceRef.mReceiver;
  });
}

void MediaTrackGraphImpl::CloseAudioInput(DeviceInputTrack* aTrack) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aTrack);

  LOG(LogLevel::Debug,
      ("{} CloseInput: DeviceInputTrack {} for device {}", fmt::ptr(this),
       fmt::ptr(aTrack), fmt::ptr(aTrack->mDeviceId)));

  class Message : public ControlMessage {
   public:
    Message(MediaTrackGraphImpl* aGraph, DeviceInputTrack* aInputTrack)
        : ControlMessage(nullptr), mGraph(aGraph), mInputTrack(aInputTrack) {}
    void Run() override {
      TRACE("MTG::CloseAudioInputImpl ControlMessage");
      mGraph->CloseAudioInputImpl(mInputTrack);
    }
    MediaTrackGraphImpl* mGraph;
    DeviceInputTrack* mInputTrack;
  };

  // DeviceInputTrack is still alive (in mTracks) even we remove it here, since
  // aTrack->Destroy() is called after this. See DeviceInputTrack::CloseAudio
  // for more details.
  mDeviceInputTrackManagerMainThread.Remove(aTrack);
  UpdateEnumeratorDefaultDeviceTracking();

  this->AppendMessage(MakeUnique<Message>(this, aTrack));

  if (aTrack->AsNativeInputTrack()) {
    LOG(LogLevel::Debug, ("{} Native input device {} is closed!",
                          fmt::ptr(this), fmt::ptr(aTrack->mDeviceId)));
    SetNewNativeInput();
  }
}

// All AudioInput listeners get the same speaker data (at least for now).
void MediaTrackGraphImpl::NotifyOutputData(const AudioChunk& aChunk) {
  if (!mDeviceInputTrackManagerGraphThread.GetNativeInputTrack()) {
    return;
  }

#if defined(MOZ_WEBRTC)
  for (const auto& track : mTracks) {
    if (const auto& t = track->AsAudioProcessingTrack()) {
      t->NotifyOutputData(this, aChunk);
    }
  }
#endif
}

void MediaTrackGraphImpl::NotifyInputStopped() {
  NativeInputTrack* native =
      mDeviceInputTrackManagerGraphThread.GetNativeInputTrack();
  if (!native) {
    return;
  }
  native->NotifyInputStopped(this);
}

void MediaTrackGraphImpl::NotifyInputData(const AudioDataValue* aBuffer,
                                          size_t aFrames, TrackRate aRate,
                                          uint32_t aChannels,
                                          uint32_t aAlreadyBuffered) {
  // Either we have an audio input device, or we just removed the audio input
  // this iteration, and we're switching back to an output-only driver next
  // iteration.
  NativeInputTrack* native =
      mDeviceInputTrackManagerGraphThread.GetNativeInputTrack();
  MOZ_ASSERT(native || Switching());
  if (!native) {
    return;
  }
  native->NotifyInputData(this, aBuffer, aFrames, aRate, aChannels,
                          aAlreadyBuffered);
}

void MediaTrackGraphImpl::NotifySetRequestedInputProcessingParamsResult(
    AudioCallbackDriver* aDriver, int aGeneration,
    Result<cubeb_input_processing_params, int>&& aResult) {
  MOZ_ASSERT(NS_IsMainThread());
  NativeInputTrack* native =
      mDeviceInputTrackManagerMainThread.GetNativeInputTrack();
  if (!native) {
    return;
  }
  QueueControlMessageWithNoShutdown([this, self = RefPtr(this),
                                     driver = RefPtr(aDriver), aGeneration,
                                     result = std::move(aResult)]() mutable {
    NativeInputTrack* native =
        mDeviceInputTrackManagerGraphThread.GetNativeInputTrack();
    if (!native) {
      return;
    }
    if (driver != mDriver) {
      return;
    }
    native->NotifySetRequestedProcessingParamsResult(this, aGeneration, result);
  });
}

void MediaTrackGraphImpl::DeviceChangedImpl() {
  MOZ_ASSERT(OnGraphThread());
  NativeInputTrack* native =
      mDeviceInputTrackManagerGraphThread.GetNativeInputTrack();
  if (!native) {
    return;
  }
  native->DeviceChanged(this);
}

void MediaTrackGraphImpl::SetMaxOutputChannelCount(uint32_t aMaxChannelCount) {
  MOZ_ASSERT(OnGraphThread());
  mMaxOutputChannelCount = aMaxChannelCount;
}

void MediaTrackGraphImpl::DeviceChanged() {
  // This is safe to be called from any thread: this message comes from an
  // underlying platform API, and we don't have much guarantees. If it is not
  // called from the main thread (and it probably will rarely be), it will post
  // itself to the main thread, and the actual device change message will be ran
  // and acted upon on the graph thread.
  if (!NS_IsMainThread()) {
    RefPtr<nsIRunnable> runnable = WrapRunnable(
        RefPtr<MediaTrackGraphImpl>(this), &MediaTrackGraphImpl::DeviceChanged);
    mMainThread->Dispatch(runnable.forget());
    return;
  }

  class Message : public ControlMessage {
   public:
    explicit Message(MediaTrackGraph* aGraph)
        : ControlMessage(nullptr),
          mGraphImpl(static_cast<MediaTrackGraphImpl*>(aGraph)) {}
    void Run() override {
      TRACE("MTG::DeviceChangeImpl ControlMessage");
      mGraphImpl->DeviceChangedImpl();
    }
    // We know that this is valid, because the graph can't shutdown if it has
    // messages.
    MediaTrackGraphImpl* mGraphImpl;
  };

  if (mMainThreadTrackCount == 0 && mMainThreadPortCount == 0) {
    // This is a special case where the origin of this event cannot control the
    // lifetime of the graph, because the graph is controling the lifetime of
    // the AudioCallbackDriver where the event originated.
    // We know the graph is soon going away, so there's no need to notify about
    // this device change.
    return;
  }

  // Reset the latency, it will get fetched again next time it's queried.
  MOZ_ASSERT(NS_IsMainThread());
  mAudioOutputLatency = 0.0;

  // Dispatch to the bg thread to do the (potentially expensive) query of the
  // maximum channel count, and then dispatch back to the main thread, then to
  // the graph, with the new info. The "special case" above is to be handled
  // back on the main thread as well for the same reasons.
  RefPtr<MediaTrackGraphImpl> self = this;
  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "MaxChannelCountUpdateOnBgThread", [self{std::move(self)}]() {
        uint32_t maxChannelCount = CubebUtils::MaxNumberOfChannels();
        self->Dispatch(NS_NewRunnableFunction(
            "MaxChannelCountUpdateToMainThread",
            [self{self}, maxChannelCount]() {
              class MessageToGraph : public ControlMessage {
               public:
                explicit MessageToGraph(MediaTrackGraph* aGraph,
                                        uint32_t aMaxChannelCount)
                    : ControlMessage(nullptr),
                      mGraphImpl(static_cast<MediaTrackGraphImpl*>(aGraph)),
                      mMaxChannelCount(aMaxChannelCount) {}
                void Run() override {
                  TRACE("MTG::SetMaxOutputChannelCount ControlMessage")
                  mGraphImpl->SetMaxOutputChannelCount(mMaxChannelCount);
                }
                MediaTrackGraphImpl* mGraphImpl;
                uint32_t mMaxChannelCount;
              };

              if (self->mMainThreadTrackCount == 0 &&
                  self->mMainThreadPortCount == 0) {
                // See comments above.
                return;
              }

              self->AppendMessage(
                  MakeUnique<MessageToGraph>(self, maxChannelCount));
            }));
      }));

  AppendMessage(MakeUnique<Message>(this));
}

static const char* GetAudioInputTypeString(const AudioInputType& aType) {
  return aType == AudioInputType::Voice ? "Voice" : "Unknown";
}

void MediaTrackGraph::ReevaluateInputDevice(CubebUtils::AudioDeviceID aID) {
  MOZ_ASSERT(OnGraphThread());
  auto* impl = static_cast<MediaTrackGraphImpl*>(this);
  impl->ReevaluateInputDevice(aID);
}

void MediaTrackGraphImpl::ReevaluateInputDevice(CubebUtils::AudioDeviceID aID) {
  MOZ_ASSERT(OnGraphThread());
  LOG(LogLevel::Debug,
      ("{}: ReevaluateInputDevice: device {}", fmt::ptr(this), fmt::ptr(aID)));

  DeviceInputTrack* track =
      mDeviceInputTrackManagerGraphThread.GetDeviceInputTrack(aID);
  if (!track) {
    LOG(LogLevel::Debug,
        ("{}: No DeviceInputTrack for this device. Ignore", fmt::ptr(this)));
    return;
  }
  if (track->AsNativeInputTrack()) {
    // CheckDriver() will check for changes, after all control messages are
    // processed.
    return;
  }
  bool needToSwitch = false;

  NonNativeInputTrack* nonNative = track->AsNonNativeInputTrack();
  MOZ_ASSERT(nonNative);
  if (nonNative->NumberOfChannels() != AudioInputChannelCount(aID)) {
    LOG(LogLevel::Debug,
        ("{}: {}-channel non-native input device {} (track {}) is "
         "re-configured to {}-channel",
         fmt::ptr(this), nonNative->NumberOfChannels(), fmt::ptr(aID),
         fmt::ptr(track), AudioInputChannelCount(aID)));
    needToSwitch = true;
  }
  if (nonNative->DevicePreference() != AudioInputDevicePreference(aID)) {
    LOG(LogLevel::Debug,
        ("{}: {}-type non-native input device {} (track {}) is re-configured "
         "to {}-type",
         fmt::ptr(this), GetAudioInputTypeString(nonNative->DevicePreference()),
         fmt::ptr(aID), fmt::ptr(track),
         GetAudioInputTypeString(AudioInputDevicePreference(aID))));
    needToSwitch = true;
  }

  if (needToSwitch) {
    nonNative->StopAudio();
    nonNative->StartAudio(MakeRefPtr<AudioInputSource>(
        MakeRefPtr<AudioInputSourceListener>(nonNative),
        nonNative->GenerateSourceId(), aID, AudioInputChannelCount(aID),
        AudioInputDevicePreference(aID) == AudioInputType::Voice,
        nonNative->mPrincipalHandle, nonNative->mSampleRate, GraphRate()));
  }
}

bool MediaTrackGraphImpl::OnGraphThreadOrNotRunning() const {
  // either we're on the right thread (and calling CurrentDriver() is safe),
  // or we're going to fail the assert anyway, so don't cross-check
  // via CurrentDriver().
  return mGraphDriverRunning ? OnGraphThread() : NS_IsMainThread();
}

bool MediaTrackGraphImpl::OnGraphThread() const {
  // we're on the right thread (and calling mDriver is safe),
  MOZ_ASSERT(mDriver);
  if (mGraphRunner && mGraphRunner->OnThread()) {
    return true;
  }
  return mDriver->OnThread();
}

bool MediaTrackGraphImpl::Destroyed() const {
  MOZ_ASSERT(NS_IsMainThread());
  return !mSelfRef;
}

bool MediaTrackGraphImpl::ShouldUpdateMainThread() {
  MOZ_ASSERT(OnGraphThreadOrNotRunning());
  if (mRealtime) {
    return true;
  }

  TimeStamp now = TimeStamp::Now();
  // For offline graphs, update now if it has been long enough since the last
  // update, or if it has reached the end.
  if (now - mLastMainThreadUpdate > CurrentDriver()->IterationDuration() ||
      mStateComputedTime >= mEndTime) {
    mLastMainThreadUpdate = now;
    return true;
  }
  return false;
}

void MediaTrackGraphImpl::PrepareUpdatesToMainThreadState(bool aFinalUpdate) {
  MOZ_ASSERT(OnGraphThreadOrNotRunning());
  mMonitor.AssertCurrentThreadOwns();

  // We don't want to frequently update the main thread about timing update
  // when we are not running in realtime.
  if (aFinalUpdate || ShouldUpdateMainThread()) {
    // Strip updates that will be obsoleted below, so as to keep the length of
    // mTrackUpdates sane.
    size_t keptUpdateCount = 0;
    for (size_t i = 0; i < mTrackUpdates.Length(); ++i) {
      MediaTrack* track = mTrackUpdates[i].mTrack;
      // RemoveTrackGraphThread() clears mTrack in updates for
      // tracks that are removed from the graph.
      MOZ_ASSERT(!track || track->GraphImpl() == this);
      if (!track || track->MainThreadNeedsUpdates()) {
        // Discard this update as it has either been cleared when the track
        // was destroyed or there will be a newer update below.
        continue;
      }
      if (keptUpdateCount != i) {
        mTrackUpdates[keptUpdateCount] = std::move(mTrackUpdates[i]);
        MOZ_ASSERT(!mTrackUpdates[i].mTrack);
      }
      ++keptUpdateCount;
    }
    mTrackUpdates.TruncateLength(keptUpdateCount);

    mTrackUpdates.SetCapacity(mTrackUpdates.Length() + mTracks.Length() +
                              mSuspendedTracks.Length());
    for (MediaTrack* track : AllTracks()) {
      if (!track->MainThreadNeedsUpdates()) {
        continue;
      }
      TrackUpdate* update = mTrackUpdates.AppendElement();
      update->mTrack = track;
      // No blocking to worry about here, since we've passed
      // UpdateCurrentTimeForTracks.
      update->mNextMainThreadCurrentTime =
          track->GraphTimeToTrackTime(mProcessedTime);
      update->mNextMainThreadEnded = track->mNotifiedEnded;
    }
    mNextMainThreadGraphTime = mProcessedTime;
    if (!mPendingUpdateRunnables.IsEmpty()) {
      mUpdateRunnables.AppendElements(std::move(mPendingUpdateRunnables));
    }
  }

  // If this is the final update, then a stable state event will soon be
  // posted just before this thread finishes, and so there is no need to also
  // post here.
  if (!aFinalUpdate &&
      // Don't send the message to the main thread if it's not going to have
      // any work to do.
      !(mUpdateRunnables.IsEmpty() && mTrackUpdates.IsEmpty())) {
    EnsureStableStateEventPosted();
  }
}

GraphTime MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(GraphTime aTime) {
  if (aTime % WEBAUDIO_BLOCK_SIZE == 0) {
    return aTime;
  }
  return RoundUpToNextAudioBlock(aTime);
}

GraphTime MediaTrackGraphImpl::RoundUpToNextAudioBlock(GraphTime aTime) {
  uint64_t block = aTime >> WEBAUDIO_BLOCK_SIZE_BITS;
  uint64_t nextBlock = block + 1;
  GraphTime nextTime = nextBlock << WEBAUDIO_BLOCK_SIZE_BITS;
  return nextTime;
}

void MediaTrackGraphImpl::ProduceDataForTracksBlockByBlock(
    uint32_t aTrackIndex, TrackRate aSampleRate) {
  MOZ_ASSERT(OnGraphThread());
  MOZ_ASSERT(aTrackIndex <= mFirstCycleBreaker,
             "Cycle breaker is not AudioNodeTrack?");

  while (mProcessedTime < mStateComputedTime) {
    // Microtask checkpoints run script which must run with denormals enabled.
    // Re-enable denormals around the checkpoint and disable them again for
    // rendering.
    WebCore::DenormalEnabler enableForMicroTasks;
    nsAutoMicroTask mt;
    WebCore::DenormalDisabler disableForRendering;

    GraphTime next = RoundUpToNextAudioBlock(mProcessedTime);
    for (uint32_t i = mFirstCycleBreaker; i < mTracks.Length(); ++i) {
      auto nt = static_cast<AudioNodeTrack*>(mTracks[i]);
      MOZ_ASSERT(nt->AsAudioNodeTrack());
      nt->ProduceOutputBeforeInput(mProcessedTime);
    }
    for (uint32_t i = aTrackIndex; i < mTracks.Length(); ++i) {
      ProcessedMediaTrack* pt = mTracks[i]->AsProcessedTrack();
      if (pt) {
        pt->ProcessInput(
            mProcessedTime, next,
            (next == mStateComputedTime) ? ProcessedMediaTrack::ALLOW_END : 0);
      }
    }
    mProcessedTime = next;
  }
  MOZ_ASSERT(mProcessedTime == mStateComputedTime,
             "Something went wrong with rounding to block boundaries");
}

void MediaTrackGraphImpl::RunMessageAfterProcessing(
    UniquePtr<ControlMessageInterface> aMessage) {
  MOZ_ASSERT(OnGraphThread());

  if (mFrontMessageQueue.IsEmpty()) {
    mFrontMessageQueue.AppendElement();
  }

  // Only one block is used for messages from the graph thread.
  MOZ_ASSERT(mFrontMessageQueue.Length() == 1);
  mFrontMessageQueue[0].mMessages.AppendElement(std::move(aMessage));
}

void MediaTrackGraphImpl::RunMessagesInQueue() {
  TRACE("MTG::RunMessagesInQueue");
  MOZ_ASSERT(OnGraphThread());
  // Calculate independent action times for each batch of messages (each
  // batch corresponding to an event loop task). This isolates the performance
  // of different scripts to some extent.
  for (uint32_t i = 0; i < mFrontMessageQueue.Length(); ++i) {
    nsTArray<UniquePtr<ControlMessageInterface>>& messages =
        mFrontMessageQueue[i].mMessages;

    for (uint32_t j = 0; j < messages.Length(); ++j) {
      TRACE("ControlMessage::Run");
      messages[j]->Run();
    }
  }
  mFrontMessageQueue.Clear();
}

void MediaTrackGraphImpl::UpdateGraph(GraphTime aEndBlockingDecisions) {
  TRACE("MTG::UpdateGraph");
  MOZ_ASSERT(OnGraphThread());
  MOZ_ASSERT(aEndBlockingDecisions >= mProcessedTime);
  // The next state computed time can be the same as the previous: it
  // means the driver would have been blocking indefinitly, but the graph has
  // been woken up right after having been to sleep.
  MOZ_ASSERT(aEndBlockingDecisions >= mStateComputedTime);

  CheckDriver();
  UpdateTrackOrder();

  // Always do another iteration if there are tracks waiting to resume.
  bool ensureNextIteration = !mPendingResumeOperations.IsEmpty();

  for (MediaTrack* track : mTracks) {
    if (SourceMediaTrack* is = track->AsSourceTrack()) {
      ensureNextIteration |= is->PullNewData(aEndBlockingDecisions);
      is->ExtractPendingInput(mStateComputedTime, aEndBlockingDecisions);
    }
    if (track->mEnded) {
      // The track's not suspended, and since it's ended, underruns won't
      // stop it playing out. So there's no blocking other than what we impose
      // here.
      GraphTime endTime = track->GetEnd() + track->mStartTime;
      if (endTime <= mStateComputedTime) {
        LOG(LogLevel::Verbose,
            ("{}: MediaTrack {} is blocked due to being ended", fmt::ptr(this),
             fmt::ptr(track)));
        track->mStartBlocking = mStateComputedTime;
      } else {
        LOG(LogLevel::Verbose,
            ("{}: MediaTrack {} has ended, but is not blocked yet (current "
             "time {}, end at {})",
             fmt::ptr(this), fmt::ptr(track),
             MediaTimeToSeconds(mStateComputedTime),
             MediaTimeToSeconds(endTime)));
        // Data can't be added to a ended track, so underruns are irrelevant.
        MOZ_ASSERT(endTime <= aEndBlockingDecisions);
        track->mStartBlocking = endTime;
      }
    } else {
      track->mStartBlocking = WillUnderrun(track, aEndBlockingDecisions);

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
      if (SourceMediaTrack* s = track->AsSourceTrack()) {
        if (s->Ended()) {
          continue;
        }
        {
          MutexAutoLock lock(s->mMutex);
          if (!s->mUpdateTrack->mPullingEnabled) {
            // The invariant that data must be provided is only enforced when
            // pulling.
            continue;
          }
        }
        if (track->GetEnd() <
            track->GraphTimeToTrackTime(aEndBlockingDecisions)) {
          LOG(LogLevel::Error,
              ("{}: SourceMediaTrack {} ({}) is live and pulled, "
               "but wasn't fed "
               "enough data. TrackListeners={}. Track-end={}, "
               "Iteration-end={}",
               fmt::ptr(this), fmt::ptr(track),
               (track->mType == MediaSegment::AUDIO ? "audio" : "video"),
               track->mTrackListeners.Length(),
               MediaTimeToSeconds(track->GetEnd()),
               MediaTimeToSeconds(
                   track->GraphTimeToTrackTime(aEndBlockingDecisions))));
          MOZ_DIAGNOSTIC_CRASH(
              "A non-ended SourceMediaTrack wasn't fed "
              "enough data by NotifyPull");
        }
      }
#endif /* MOZ_DIAGNOSTIC_ASSERT_ENABLED */
    }
  }

  for (MediaTrack* track : mSuspendedTracks) {
    track->mStartBlocking = mStateComputedTime;
  }

  // If the loop is woken up so soon that mStateComputedTime does not advance
  // or if an offline graph is not currently rendering, we end up having
  // aEndBlockingDecisions == mStateComputedTime.
  // Since the process interval [mStateComputedTime, aEndBlockingDecision) is
  // empty, Process() will not find any unblocked track and so will not
  // ensure another iteration.  If the graph should be rendering, then ensure
  // another iteration to render.
  if (ensureNextIteration || (aEndBlockingDecisions == mStateComputedTime &&
                              mStateComputedTime < mEndTime)) {
    EnsureNextIteration();
  }
}

void MediaTrackGraphImpl::SelectOutputDeviceForAEC() {
  MOZ_ASSERT(OnGraphThread());
  size_t currentDeviceIndex = mOutputDevices.IndexOf(mOutputDeviceForAEC);
  if (currentDeviceIndex == mOutputDevices.NoIndex) {
    // Outputs for this device have been removed.
    // Fall back to the primary output device.
    LOG(LogLevel::Info, ("{}: No remaining outputs to device {}. "
                         "Switch to primary output device {} for AEC",
                         fmt::ptr(this), fmt::ptr(mOutputDeviceForAEC),
                         fmt::ptr(PrimaryOutputDeviceID())));
    mOutputDeviceForAEC = PrimaryOutputDeviceID();
    currentDeviceIndex = 0;
    MOZ_ASSERT(mOutputDevices[0].mDeviceID == mOutputDeviceForAEC);
  }
  if (mOutputDevices.Length() == 1) {
    // No other output devices so there is no choice.
    return;
  }

  // The output is considered silent intentionally only if the whole duration
  // (often more than just this processing interval) of audio data in the
  // MediaSegment is null so as to reduce switching between output devices
  // should there be short durations of silence.
  auto HasNonNullAudio = [](const TrackAndVolume& aTV) {
    return aTV.mVolume != 0 && !aTV.mTrack->IsSuspended() &&
           !aTV.mTrack->GetData()->IsNull();
  };
  // Keep using the same output device stream if it has non-null data,
  // so as to stay with a stream having ongoing audio.  If the output stream
  // is switched, the echo cancellation algorithm can take some time to adjust
  // to the change in delay, so there is less value in switching back and
  // forth between output devices for very short sounds.
  for (const auto& output : mOutputDevices[currentDeviceIndex].mTrackOutputs) {
    if (HasNonNullAudio(output)) {
      return;
    }
  }
  // The current output device is silent.  Use another if it has non-null data.
  for (const auto& outputDeviceEntry : mOutputDevices) {
    for (const auto& output : outputDeviceEntry.mTrackOutputs) {
      if (HasNonNullAudio(output)) {
        // Switch to this device.
        LOG(LogLevel::Info,
            ("{}: Switch output device for AEC from silent {} to non-null {}",
             fmt::ptr(this), fmt::ptr(mOutputDeviceForAEC),
             fmt::ptr(outputDeviceEntry.mDeviceID)));
        mOutputDeviceForAEC = outputDeviceEntry.mDeviceID;
        return;
      }
    }
  }
  // Null data for all outputs.  Keep using the same device.
}

void MediaTrackGraphImpl::Process(MixerCallbackReceiver* aMixerReceiver) {
  TRACE("MTG::Process");
  MOZ_ASSERT(OnGraphThread());
  if (mStateComputedTime == mProcessedTime) {  // No frames to render.
    return;
  }

  WebCore::DenormalDisabler disabler;

  // Play track contents.
  bool allBlockedForever = true;
  // True when we've done ProcessInput for all processed tracks.
  bool doneAllProducing = false;
  const GraphTime oldProcessedTime = mProcessedTime;

  // Figure out what each track wants to do
  for (uint32_t i = 0; i < mTracks.Length(); ++i) {
    MediaTrack* track = mTracks[i];
    if (!doneAllProducing) {
      ProcessedMediaTrack* pt = track->AsProcessedTrack();
      if (pt) {
        AudioNodeTrack* n = track->AsAudioNodeTrack();
        if (n) {
#ifdef DEBUG
          // Verify that the sampling rate for all of the following tracks is
          // the same
          for (uint32_t j = i + 1; j < mTracks.Length(); ++j) {
            AudioNodeTrack* nextTrack = mTracks[j]->AsAudioNodeTrack();
            if (nextTrack) {
              MOZ_ASSERT(n->mSampleRate == nextTrack->mSampleRate,
                         "All AudioNodeTracks in the graph must have the same "
                         "sampling rate");
            }
          }
#endif
          // Since an AudioNodeTrack is present, go ahead and
          // produce audio block by block for all the rest of the tracks.
          ProduceDataForTracksBlockByBlock(i, n->mSampleRate);
          doneAllProducing = true;
        } else {
          pt->ProcessInput(mProcessedTime, mStateComputedTime,
                           ProcessedMediaTrack::ALLOW_END);
          // Assert that a live track produced enough data
          MOZ_ASSERT_IF(!track->mEnded,
                        track->GetEnd() >= GraphTimeToTrackTimeWithBlocking(
                                               track, mStateComputedTime));
        }
      }
    }
    if (track->mStartBlocking > oldProcessedTime) {
      allBlockedForever = false;
    }
  }
  mProcessedTime = mStateComputedTime;

  SelectOutputDeviceForAEC();
  for (const auto& outputDeviceEntry : mOutputDevices) {
    uint32_t outputChannelCount;
    if (!outputDeviceEntry.mReceiver) {  // primary output
      if (!aMixerReceiver) {
        // Running off a system clock driver.  No need to mix output.
        continue;
      }
      MOZ_ASSERT(CurrentDriver()->AsAudioCallbackDriver(),
                 "Driver must be AudioCallbackDriver if aMixerReceiver");
      // Use the number of channel the driver expects: this is the number of
      // channel that can be output by the underlying system level audio stream.
      outputChannelCount =
          CurrentDriver()->AsAudioCallbackDriver()->OutputChannelCount();
    } else {
      outputChannelCount = AudioOutputChannelCount(outputDeviceEntry);
    }
    MOZ_ASSERT(mRealtime,
               "If there's an output device, this graph must be realtime");
    mMixer.StartMixing();
    // This is the number of frames that are written to the output buffer, for
    // this iteration.
    TrackTime ticksPlayed = 0;
    for (const auto& t : outputDeviceEntry.mTrackOutputs) {
      TrackTime ticksPlayedForThisTrack =
          PlayAudio(t, oldProcessedTime, outputChannelCount);
      if (ticksPlayed == 0) {
        ticksPlayed = ticksPlayedForThisTrack;
      } else {
        MOZ_ASSERT(
            !ticksPlayedForThisTrack || ticksPlayedForThisTrack == ticksPlayed,
            "Each track should have the same number of frames.");
      }
    }

    if (ticksPlayed == 0) {
      // Nothing was played, so the mixer doesn't know how many frames were
      // processed. We still tell it so AudioCallbackDriver knows how much has
      // been processed. (bug 1406027)
      mMixer.Mix(nullptr, outputChannelCount,
                 mStateComputedTime - oldProcessedTime, mSampleRate);
    }
    AudioChunk* outputChunk = mMixer.MixedChunk();
    if (outputDeviceEntry.mDeviceID == mOutputDeviceForAEC) {
      // Callback any observers for the AEC speaker data.  Note that one
      // (maybe) of these will be full-duplex, the others will get their input
      // data off separate cubeb callbacks.
      NotifyOutputData(*outputChunk);
    }
    if (!outputDeviceEntry.mReceiver) {  // primary output
      aMixerReceiver->MixerCallback(outputChunk, mSampleRate);
    } else {
      outputDeviceEntry.mReceiver->EnqueueAudio(*outputChunk);
    }
  }

  if (!allBlockedForever) {
    EnsureNextIteration();
  }
}

bool MediaTrackGraphImpl::UpdateMainThreadState() {
  MOZ_ASSERT(OnGraphThread());
  if (mForceShutDownReceived) {
    for (MediaTrack* track : AllTracks()) {
      track->OnGraphThreadDone();
    }
  }
  {
    MonitorAutoLock lock(mMonitor);
    bool finalUpdate =
        mForceShutDownReceived || (IsEmpty() && mBackMessageQueue.IsEmpty());
    PrepareUpdatesToMainThreadState(finalUpdate);
    if (!finalUpdate) {
      return true;
    }
    // The JSContext will not be used again.
    // Clear main thread access while under monitor.
    mJSContext = nullptr;
  }
  dom::WorkletThread::DeleteCycleCollectedJSContext();
  // Enter shutdown mode when this iteration is completed.
  // No need to Destroy tracks here. The main-thread owner of each
  // track is responsible for calling Destroy on them.
  return false;
}

auto MediaTrackGraphImpl::OneIteration(GraphTime aStateTime,
                                       MixerCallbackReceiver* aMixerReceiver)
    -> IterationResult {
  if (mGraphRunner) {
    return mGraphRunner->OneIteration(aStateTime, aMixerReceiver);
  }

  return OneIterationImpl(aStateTime, aMixerReceiver);
}

auto MediaTrackGraphImpl::OneIterationImpl(
    GraphTime aStateTime, MixerCallbackReceiver* aMixerReceiver)
    -> IterationResult {
  TRACE("MTG::OneIterationImpl");
  TRACE_AUDIO_CALLBACK_FRAME_COUNT(
      "MTG graph advance", aStateTime - mStateComputedTime, mSampleRate);

  if (SoftRealTimeLimitReached()) {
    TRACE("MTG::Demoting real-time thread!");
    DemoteThreadFromRealTime();
  }

  // Changes to LIFECYCLE_RUNNING occur before starting or reviving the graph
  // thread, and so the monitor need not be held to check mLifecycleState.
  // LIFECYCLE_THREAD_NOT_STARTED is possible when shutting down offline
  // graphs that have not started.

  // While changes occur on mainthread, this assert confirms that
  // this code shouldn't run if mainthread might be changing the state (to
  // > LIFECYCLE_RUNNING)

  // Ignore mutex warning: static during execution of the graph
  MOZ_PUSH_IGNORE_THREAD_SAFETY
  MOZ_DIAGNOSTIC_ASSERT(mLifecycleState <= LIFECYCLE_RUNNING);
  MOZ_POP_THREAD_SAFETY

  MOZ_ASSERT(OnGraphThread());

  // Process graph message from the main thread for this iteration.
  SwapMessageQueues();
  RunMessagesInQueue();

  // Process MessagePort events.
  // These require a single thread, which has an nsThread with an event queue.
  if (mGraphRunner || !mRealtime) {
    TRACE("MTG::MessagePort events");
    NS_ProcessPendingEvents(nullptr);
  }

  UpdateGraph(aStateTime);

  mStateComputedTime = aStateTime;

  GraphTime oldProcessedTime = mProcessedTime;
  Process(aMixerReceiver);
  MOZ_ASSERT(mProcessedTime == aStateTime);

  UpdateCurrentTimeForTracks(oldProcessedTime);

  ProcessChunkMetadata(oldProcessedTime);

  // Process graph messages queued from RunMessageAfterProcessing() on this
  // thread during the iteration.
  RunMessagesInQueue();

  if (!UpdateMainThreadState()) {
    if (Switching()) {
      // We'll never get to do this switch. Clear mNextDriver to break the
      // ref-cycle graph->nextDriver->currentDriver->graph.
      SwitchAtNextIteration(nullptr);
    }
    return IterationResult::CreateStop(
        NewRunnableMethod("MediaTrackGraphImpl::SignalMainThreadCleanup", this,
                          &MediaTrackGraphImpl::SignalMainThreadCleanup));
  }

  if (Switching()) {
    RefPtr<GraphDriver> nextDriver = std::move(mNextDriver);
    return IterationResult::CreateSwitchDriver(
        nextDriver, NewRunnableMethod<StoreRefPtrPassByPtr<GraphDriver>>(
                        "MediaTrackGraphImpl::SetCurrentDriver", this,
                        &MediaTrackGraphImpl::SetCurrentDriver, nextDriver));
  }

  return IterationResult::CreateStillProcessing();
}

void MediaTrackGraphImpl::ApplyTrackUpdate(TrackUpdate* aUpdate) {
  MOZ_ASSERT(NS_IsMainThread());
  mMonitor.AssertCurrentThreadOwns();

  MediaTrack* track = aUpdate->mTrack;
  if (!track) return;
  track->mMainThreadCurrentTime = aUpdate->mNextMainThreadCurrentTime;
  track->mMainThreadEnded = aUpdate->mNextMainThreadEnded;

  if (track->ShouldNotifyTrackEnded()) {
    track->NotifyMainThreadListeners();
  }
}

void MediaTrackGraphImpl::ForceShutDown() {
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on main thread");
  LOG(LogLevel::Debug, ("{}: MediaTrackGraph::ForceShutdown", fmt::ptr(this)));

  if (mShutdownBlocker) {
    // Avoid waiting forever for a graph to shut down
    // synchronously.  Reports are that some 3rd-party audio drivers
    // occasionally hang in shutdown (both for us and Chrome).
    NS_NewTimerWithCallback(
        getter_AddRefs(mShutdownTimer), this,
        MediaTrackGraph::AUDIO_CALLBACK_DRIVER_SHUTDOWN_TIMEOUT,
        nsITimer::TYPE_ONE_SHOT);
  }

  class Message final : public ControlMessage {
   public:
    explicit Message(MediaTrackGraphImpl* aGraph)
        : ControlMessage(nullptr), mGraph(aGraph) {}
    void Run() override {
      TRACE("MTG::ForceShutdown ControlMessage");
      mGraph->mForceShutDownReceived = true;
    }
    // The graph owns this message.
    MediaTrackGraphImpl* MOZ_NON_OWNING_REF mGraph;
  };

  if (mMainThreadTrackCount > 0 || mMainThreadPortCount > 0) {
    // If both the track and port counts are zero, the regular shutdown
    // sequence will progress shortly to shutdown threads and destroy the graph.
    AppendMessage(MakeUnique<Message>(this));
    InterruptJS();
  }
}

NS_IMETHODIMP
MediaTrackGraphImpl::Notify(nsITimer* aTimer) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mShutdownBlocker, "MediaTrackGraph took too long to shut down!");
  // Sigh, graph took too long to shut down.  Stop blocking system
  // shutdown and hope all is well.
  RemoveShutdownBlocker();
  mOutputDevicesChangedListener.DisconnectIfExists();
  return NS_OK;
}

static nsCString GetDocumentTitle(uint64_t aWindowID) {
  MOZ_ASSERT(NS_IsMainThread());
  nsCString title;
  auto* win = nsGlobalWindowInner::GetInnerWindowWithId(aWindowID);
  if (!win) {
    return title;
  }
  Document* doc = win->GetExtantDoc();
  if (!doc) {
    return title;
  }
  nsAutoString titleUTF16;
  doc->GetTitle(titleUTF16);
  CopyUTF16toUTF8(titleUTF16, title);
  return title;
}

NS_IMETHODIMP
MediaTrackGraphImpl::Observe(nsISupports* aSubject, const char* aTopic,
                             const char16_t* aData) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(strcmp(aTopic, "document-title-changed") == 0);
  nsCString streamName = GetDocumentTitle(mWindowID);
  LOG(LogLevel::Debug,
      ("{}: document title: {}", fmt::ptr(this), streamName.get()));
  if (streamName.IsEmpty()) {
    return NS_OK;
  }
  QueueControlMessageWithNoShutdown(
      [self = RefPtr{this}, this, streamName = std::move(streamName)] {
        CurrentDriver()->SetStreamName(streamName);
      });
  return NS_OK;
}

bool MediaTrackGraphImpl::AddShutdownBlocker() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mShutdownBlocker);

  class Blocker : public media::ShutdownBlocker {
    const RefPtr<MediaTrackGraphImpl> mGraph;

   public:
    Blocker(MediaTrackGraphImpl* aGraph, const nsString& aName)
        : media::ShutdownBlocker(aName), mGraph(aGraph) {}

    NS_IMETHOD
    BlockShutdown(nsIAsyncShutdownClient* aProfileBeforeChange) override {
      mGraph->ForceShutDown();
      return NS_OK;
    }
  };

  nsCOMPtr<nsIAsyncShutdownClient> barrier = media::GetShutdownBarrier();
  if (!barrier) {
    // We're already shutting down, we won't be able to add a blocker, bail.
    LOG(LogLevel::Error,
        ("{}: Couldn't get shutdown barrier, won't add shutdown blocker",
         fmt::ptr(this)));
    return false;
  }

  // Blocker names must be distinct.
  nsString blockerName;
  blockerName.AppendPrintf("MediaTrackGraph %p shutdown", this);
  mShutdownBlocker = MakeAndAddRef<Blocker>(this, blockerName);
  nsresult rv = barrier->AddBlocker(mShutdownBlocker,
                                    NS_LITERAL_STRING_FROM_CSTRING(__FILE__),
                                    __LINE__, u"MediaTrackGraph shutdown"_ns);
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  return true;
}

void MediaTrackGraphImpl::RemoveShutdownBlocker() {
  if (!mShutdownBlocker) {
    return;
  }
  media::MustGetShutdownBarrier()->RemoveBlocker(mShutdownBlocker);
  mShutdownBlocker = nullptr;
}

NS_IMETHODIMP
MediaTrackGraphImpl::GetName(nsACString& aName) {
  aName.AssignLiteral("MediaTrackGraphImpl");
  return NS_OK;
}

namespace {

class MediaTrackGraphShutDownRunnable : public Runnable {
 public:
  explicit MediaTrackGraphShutDownRunnable(MediaTrackGraphImpl* aGraph)
      : Runnable("MediaTrackGraphShutDownRunnable"), mGraph(aGraph) {}
  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is MOZ_CAN_RUN_SCRIPT.
  // See bug 1535398.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override {
    TRACE("MTG::MediaTrackGraphShutDownRunnable runnable");
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(!mGraph->mGraphDriverRunning && mGraph->mDriver,
               "We should know the graph thread control loop isn't running!");

    LOG(LogLevel::Debug, ("{}: Shutting down graph", fmt::ptr(mGraph.get())));

    // We've asserted the graph isn't running.  Use mDriver instead of
    // CurrentDriver to avoid thread-safety checks
#if 0  // AudioCallbackDrivers are released asynchronously anyways
    // XXX a better test would be have setting mGraphDriverRunning make sure
    // any current callback has finished and block future ones -- or just
    // handle it all in Shutdown()!
    if (mGraph->mDriver->AsAudioCallbackDriver()) {
      MOZ_ASSERT(!mGraph->mDriver->AsAudioCallbackDriver()->InCallback());
    }
#endif

    for (MediaTrackGraphImpl::PendingResumeOperation& op :
         mGraph->mPendingResumeOperations) {
      op.Abort();
    }

    if (mGraph->mGraphRunner) {
      RefPtr<GraphRunner>(mGraph->mGraphRunner)->Shutdown();
    }

    RefPtr<GraphDriver>(mGraph->mDriver)->Shutdown();

    // Release the driver now so that an AudioCallbackDriver will release its
    // SharedThreadPool reference.  Each SharedThreadPool reference must be
    // released before SharedThreadPool::SpinUntilEmpty() runs on
    // xpcom-shutdown-threads.  Don't wait for GC/CC to release references to
    // objects owning tracks, or for expiration of mGraph->mShutdownTimer,
    // which won't otherwise release its reference on the graph until
    // nsTimerImpl::Shutdown(), which runs after xpcom-shutdown-threads.
    mGraph->SetCurrentDriver(nullptr);

    // Safe to access these without the monitor since the graph isn't running.
    // We may be one of several graphs. Drop ticket to eventually unblock
    // shutdown.
    if (mGraph->mShutdownTimer && !mGraph->mShutdownBlocker) {
      MOZ_ASSERT(
          false,
          "AudioCallbackDriver took too long to shut down and we let shutdown"
          " continue - freezing and leaking");

      // The timer fired, so we may be deeper in shutdown now.  Block any
      // further teardown and just leak, for safety.
      return NS_OK;
    }

    // mGraph's thread is not running so it's OK to do whatever here
    for (MediaTrack* track : mGraph->AllTracks()) {
      // Clean up all MediaSegments since we cannot release Images too
      // late during shutdown. Also notify listeners that they were removed
      // so they can clean up any gfx resources.
      track->RemoveAllResourcesAndListenersImpl();
    }

#ifdef DEBUG
    {
      MonitorAutoLock lock(mGraph->mMonitor);
      MOZ_ASSERT(mGraph->mUpdateRunnables.IsEmpty());
    }
#endif
    mGraph->mPendingUpdateRunnables.Clear();

    mGraph->RemoveShutdownBlocker();

    // We can't block past the final LIFECYCLE_WAITING_FOR_TRACK_DESTRUCTION
    // stage, since completion of that stage requires all tracks to be freed,
    // which requires shutdown to proceed.

    if (mGraph->IsEmpty()) {
      // mGraph is no longer needed, so delete it.
      mGraph->Destroy();
    } else {
      // The graph is not empty.  We must be in a forced shutdown.
      // Some later AppendMessage will detect that the graph has
      // been emptied, and delete it.
      NS_ASSERTION(mGraph->mForceShutDownReceived, "Not in forced shutdown?");
      mGraph->LifecycleStateRef() =
          MediaTrackGraphImpl::LIFECYCLE_WAITING_FOR_TRACK_DESTRUCTION;
    }
    return NS_OK;
  }

 private:
  RefPtr<MediaTrackGraphImpl> mGraph;
};

class MediaTrackGraphStableStateRunnable : public Runnable {
 public:
  explicit MediaTrackGraphStableStateRunnable(MediaTrackGraphImpl* aGraph,
                                              bool aSourceIsMTG)
      : Runnable("MediaTrackGraphStableStateRunnable"),
        mGraph(aGraph),
        mSourceIsMTG(aSourceIsMTG) {}
  NS_IMETHOD Run() override {
    TRACE("MTG::MediaTrackGraphStableStateRunnable ControlMessage");
    if (mGraph) {
      mGraph->RunInStableState(mSourceIsMTG);
    }
    return NS_OK;
  }

 private:
  RefPtr<MediaTrackGraphImpl> mGraph;
  bool mSourceIsMTG;
};

/*
 * Control messages forwarded from main thread to graph manager thread
 */
class CreateMessage : public ControlMessage {
 public:
  explicit CreateMessage(MediaTrack* aTrack) : ControlMessage(aTrack) {}
  void Run() override {
    TRACE("MTG::AddTrackGraphThread ControlMessage");
    mTrack->GraphImpl()->AddTrackGraphThread(mTrack);
  }
  void RunDuringShutdown() override {
    // Make sure to run this message during shutdown too, to make sure
    // that we balance the number of tracks registered with the graph
    // as they're destroyed during shutdown.
    Run();
  }
};

}  // namespace

void MediaTrackGraphImpl::RunInStableState(bool aSourceIsMTG) {
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on main thread");

  nsTArray<nsCOMPtr<nsIRunnable>> runnables;
  // When we're doing a forced shutdown, pending control messages may be
  // run on the main thread via RunDuringShutdown. Those messages must
  // run without the graph monitor being held. So, we collect them here.
  nsTArray<UniquePtr<ControlMessageInterface>>
      controlMessagesToRunDuringShutdown;

  {
    MonitorAutoLock lock(mMonitor);
    if (aSourceIsMTG) {
      MOZ_ASSERT(mPostedRunInStableStateEvent);
      mPostedRunInStableStateEvent = false;
    }

    // This should be kept in sync with the LifecycleState enum in
    // MediaTrackGraphImpl.h
    const char* LifecycleState_str[] = {
        "LIFECYCLE_THREAD_NOT_STARTED", "LIFECYCLE_RUNNING",
        "LIFECYCLE_WAITING_FOR_MAIN_THREAD_CLEANUP",
        "LIFECYCLE_WAITING_FOR_THREAD_SHUTDOWN",
        "LIFECYCLE_WAITING_FOR_TRACK_DESTRUCTION"};

    if (LifecycleStateRef() != LIFECYCLE_RUNNING) {
      LOG(LogLevel::Debug,
          ("{}: Running stable state callback. Current state: {}",
           fmt::ptr(this), LifecycleState_str[LifecycleStateRef()]));
    }

    runnables = std::move(mUpdateRunnables);
    for (uint32_t i = 0; i < mTrackUpdates.Length(); ++i) {
      TrackUpdate* update = &mTrackUpdates[i];
      if (update->mTrack) {
        ApplyTrackUpdate(update);
      }
    }
    mTrackUpdates.Clear();

    mMainThreadGraphTime = mNextMainThreadGraphTime;

    if (mCurrentTaskMessageQueue.IsEmpty()) {
      if (LifecycleStateRef() == LIFECYCLE_WAITING_FOR_MAIN_THREAD_CLEANUP &&
          IsEmpty()) {
        // Complete shutdown. First, ensure that this graph is no longer used.
        // A new graph graph will be created if one is needed.
        // Asynchronously clean up old graph. We don't want to do this
        // synchronously because it spins the event loop waiting for threads
        // to shut down, and we don't want to do that in a stable state handler.
        LifecycleStateRef() = LIFECYCLE_WAITING_FOR_THREAD_SHUTDOWN;
        LOG(LogLevel::Debug,
            ("{}: Sending MediaTrackGraphShutDownRunnable", fmt::ptr(this)));
        nsCOMPtr<nsIRunnable> event = new MediaTrackGraphShutDownRunnable(this);
        mMainThread->Dispatch(event.forget());
      }
    } else {
      if (LifecycleStateRef() <= LIFECYCLE_WAITING_FOR_MAIN_THREAD_CLEANUP) {
        MessageBlock* block = mBackMessageQueue.AppendElement();
        block->mMessages = std::move(mCurrentTaskMessageQueue);
        EnsureNextIteration();
      }

      // If this MediaTrackGraph has entered regular (non-forced) shutdown it
      // is not able to process any more messages. Those messages being added to
      // the graph in the first place is an error.
      MOZ_DIAGNOSTIC_ASSERT(LifecycleStateRef() <
                                LIFECYCLE_WAITING_FOR_MAIN_THREAD_CLEANUP ||
                            mForceShutDownReceived);
    }

    if (LifecycleStateRef() == LIFECYCLE_THREAD_NOT_STARTED) {
      // Start the driver now. We couldn't start it earlier because the graph
      // might exit immediately on finding it has no tracks. The first message
      // for a new graph must create a track.
      MOZ_ASSERT(MessagesQueued());

      LOG(LogLevel::Debug,
          ("{}: Starting a graph with a {}", fmt::ptr(this),
           CurrentDriver()->AsAudioCallbackDriver() ? "AudioCallbackDriver"
                                                    : "SystemClockDriver"));
      LifecycleStateRef() = LIFECYCLE_RUNNING;
      mGraphDriverRunning = true;
      RefPtr<GraphDriver> driver = CurrentDriver();
      driver->Start();
      // It's not safe to Shutdown() a thread from StableState, and
      // releasing this may shutdown a SystemClockDriver thread.
      // Proxy the release to outside of StableState.
      NS_ReleaseOnMainThread("MediaTrackGraphImpl::CurrentDriver",
                             driver.forget(),
                             true);  // always proxy
    }

    if (LifecycleStateRef() == LIFECYCLE_WAITING_FOR_MAIN_THREAD_CLEANUP &&
        mForceShutDownReceived) {
      // Defer calls to RunDuringShutdown() to happen while mMonitor is not
      // held.
      for (uint32_t i = 0; i < mBackMessageQueue.Length(); ++i) {
        MessageBlock& mb = mBackMessageQueue[i];
        controlMessagesToRunDuringShutdown.AppendElements(
            std::move(mb.mMessages));
      }
      mBackMessageQueue.Clear();
      MOZ_ASSERT(mCurrentTaskMessageQueue.IsEmpty());
      // Stop MediaTrackGraph threads.
      LifecycleStateRef() = LIFECYCLE_WAITING_FOR_THREAD_SHUTDOWN;
      nsCOMPtr<nsIRunnable> event = new MediaTrackGraphShutDownRunnable(this);
      mMainThread->Dispatch(event.forget());
    }

    mGraphDriverRunning = LifecycleStateRef() == LIFECYCLE_RUNNING;
  }

  // Make sure we get a new current time in the next event loop task
  if (!aSourceIsMTG) {
    MOZ_ASSERT(mPostedRunInStableState);
    mPostedRunInStableState = false;
  }

  for (uint32_t i = 0; i < controlMessagesToRunDuringShutdown.Length(); ++i) {
    controlMessagesToRunDuringShutdown[i]->RunDuringShutdown();
  }

#ifdef DEBUG
  mCanRunMessagesSynchronously =
      !mGraphDriverRunning &&
      LifecycleStateRef() >= LIFECYCLE_WAITING_FOR_THREAD_SHUTDOWN;
#endif

  for (uint32_t i = 0; i < runnables.Length(); ++i) {
    runnables[i]->Run();
  }
}

void MediaTrackGraphImpl::EnsureRunInStableState() {
  MOZ_ASSERT(NS_IsMainThread(), "main thread only");

  if (mPostedRunInStableState) return;
  mPostedRunInStableState = true;
  nsCOMPtr<nsIRunnable> event =
      new MediaTrackGraphStableStateRunnable(this, false);
  nsContentUtils::RunInStableState(event.forget());
}

void MediaTrackGraphImpl::EnsureStableStateEventPosted() {
  MOZ_ASSERT(OnGraphThread());
  mMonitor.AssertCurrentThreadOwns();

  if (mPostedRunInStableStateEvent) return;
  mPostedRunInStableStateEvent = true;
  nsCOMPtr<nsIRunnable> event =
      new MediaTrackGraphStableStateRunnable(this, true);
  mMainThread->Dispatch(event.forget());
}

void MediaTrackGraphImpl::SignalMainThreadCleanup() {
  MOZ_ASSERT(mDriver->OnThread());

  MonitorAutoLock lock(mMonitor);
  // LIFECYCLE_THREAD_NOT_STARTED is possible when shutting down offline
  // graphs that have not started.
  MOZ_DIAGNOSTIC_ASSERT(mLifecycleState <= LIFECYCLE_RUNNING);
  LOG(LogLevel::Debug,
      ("{}: MediaTrackGraph waiting for main thread cleanup", fmt::ptr(this)));
  LifecycleStateRef() =
      MediaTrackGraphImpl::LIFECYCLE_WAITING_FOR_MAIN_THREAD_CLEANUP;
  EnsureStableStateEventPosted();
}

void MediaTrackGraphImpl::AppendMessage(
    UniquePtr<ControlMessageInterface> aMessage) {
  MOZ_ASSERT(NS_IsMainThread(), "main thread only");
  MOZ_DIAGNOSTIC_ASSERT(mMainThreadTrackCount > 0 || mMainThreadPortCount > 0);

  if (!mGraphDriverRunning &&
      LifecycleStateRef() > LIFECYCLE_WAITING_FOR_MAIN_THREAD_CLEANUP) {
    // The graph control loop is not running and main thread cleanup has
    // happened. From now on we can't append messages to
    // mCurrentTaskMessageQueue, because that will never be processed again, so
    // just RunDuringShutdown this message. This should only happen during
    // forced shutdown, or after a non-realtime graph has finished processing.
#ifdef DEBUG
    MOZ_ASSERT(mCanRunMessagesSynchronously);
    mCanRunMessagesSynchronously = false;
#endif
    aMessage->RunDuringShutdown();
#ifdef DEBUG
    mCanRunMessagesSynchronously = true;
#endif
    if (IsEmpty() &&
        LifecycleStateRef() >= LIFECYCLE_WAITING_FOR_TRACK_DESTRUCTION) {
      Destroy();
    }
    return;
  }

  mCurrentTaskMessageQueue.AppendElement(std::move(aMessage));
  EnsureRunInStableState();
}

void MediaTrackGraphImpl::Dispatch(already_AddRefed<nsIRunnable> aRunnable) {
  mMainThread->Dispatch(std::move(aRunnable));
}

MediaTrack::MediaTrack(TrackRate aSampleRate, MediaSegment::Type aType,
                       MediaSegment* aSegment)
    : mSampleRate(aSampleRate),
      mType(aType),
      mSegment(aSegment),
      mStartTime(0),
      mForgottenTime(0),
      mEnded(false),
      mNotifiedEnded(false),
      mDisabledMode(DisabledTrackMode::ENABLED),
      mStartBlocking(GRAPH_TIME_MAX),
      mSuspendedCount(0),
      mMainThreadCurrentTime(0),
      mMainThreadEnded(false),
      mEndedNotificationSent(false),
      mMainThreadDestroyed(false),
      mGraph(nullptr) {
  MOZ_COUNT_CTOR(MediaTrack);
  MOZ_ASSERT_IF(mSegment, mSegment->GetType() == aType);
}

MediaTrack::~MediaTrack() {
  MOZ_COUNT_DTOR(MediaTrack);
  NS_ASSERTION(mMainThreadDestroyed, "Should have been destroyed already");
  NS_ASSERTION(mMainThreadListeners.IsEmpty(),
               "All main thread listeners should have been removed");
}

size_t MediaTrack::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t amount = 0;

  // Not owned:
  // - mGraph - Not reported here
  // - mConsumers - elements
  // Future:
  // - mLastPlayedVideoFrame
  // - mTrackListeners - elements

  amount += mTrackListeners.ShallowSizeOfExcludingThis(aMallocSizeOf);
  amount += mMainThreadListeners.ShallowSizeOfExcludingThis(aMallocSizeOf);
  amount += mConsumers.ShallowSizeOfExcludingThis(aMallocSizeOf);

  return amount;
}

size_t MediaTrack::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

void MediaTrack::IncrementSuspendCount() {
  ++mSuspendedCount;
  if (mSuspendedCount != 1 || !mGraph) {
    MOZ_ASSERT(mGraph || mConsumers.IsEmpty());
    return;
  }
  AssertOnGraphThreadOrNotRunning();
  auto* graph = GraphImpl();
  for (uint32_t i = 0; i < mConsumers.Length(); ++i) {
    mConsumers[i]->Suspended();
  }
  const bool removed = graph->mTracks.RemoveElement(this);
  MOZ_DIAGNOSTIC_ASSERT(removed, "Track not in mTracks at suspend transition");
  if (removed) {
    graph->mSuspendedTracks.AppendElement(this);
    graph->SetTrackOrderDirty();
  }
}

void MediaTrack::DecrementSuspendCount() {
  MOZ_ASSERT(mSuspendedCount > 0, "Suspend count underrun");
  --mSuspendedCount;
  if (mSuspendedCount != 0 || !mGraph) {
    MOZ_ASSERT(mGraph || mConsumers.IsEmpty());
    return;
  }
  AssertOnGraphThreadOrNotRunning();
  auto* graph = GraphImpl();
  for (uint32_t i = 0; i < mConsumers.Length(); ++i) {
    mConsumers[i]->Resumed();
  }
  const bool removed = graph->mSuspendedTracks.RemoveElement(this);
  MOZ_DIAGNOSTIC_ASSERT(removed,
                        "Track not in mSuspendedTracks at resume transition");
  if (removed) {
    graph->mTracks.AppendElement(this);
    graph->SetTrackOrderDirty();
  }
}

void ProcessedMediaTrack::DecrementSuspendCount() {
  mCycleMarker = NOT_VISITED;
  MediaTrack::DecrementSuspendCount();
}

MediaTrackGraphImpl* MediaTrack::GraphImpl() {
  return static_cast<MediaTrackGraphImpl*>(mGraph);
}

const MediaTrackGraphImpl* MediaTrack::GraphImpl() const {
  return static_cast<MediaTrackGraphImpl*>(mGraph);
}

void MediaTrack::SetGraphImpl(MediaTrackGraphImpl* aGraph) {
  MOZ_ASSERT(!mGraph, "Should only be called once");
  MOZ_ASSERT(mSampleRate == aGraph->GraphRate());
  mGraph = aGraph;
}

void MediaTrack::SetGraphImpl(MediaTrackGraph* aGraph) {
  MediaTrackGraphImpl* graph = static_cast<MediaTrackGraphImpl*>(aGraph);
  SetGraphImpl(graph);
}

TrackTime MediaTrack::GraphTimeToTrackTime(GraphTime aTime) const {
  NS_ASSERTION(mStartBlocking == GraphImpl()->mStateComputedTime ||
                   aTime <= mStartBlocking,
               "Incorrectly ignoring blocking!");
  return aTime - mStartTime;
}

GraphTime MediaTrack::TrackTimeToGraphTime(TrackTime aTime) const {
  NS_ASSERTION(mStartBlocking == GraphImpl()->mStateComputedTime ||
                   aTime + mStartTime <= mStartBlocking,
               "Incorrectly ignoring blocking!");
  return aTime + mStartTime;
}

TrackTime MediaTrack::GraphTimeToTrackTimeWithBlocking(GraphTime aTime) const {
  return GraphImpl()->GraphTimeToTrackTimeWithBlocking(this, aTime);
}

void MediaTrack::RemoveAllResourcesAndListenersImpl() {
  GraphImpl()->AssertOnGraphThreadOrNotRunning();

  for (auto& l : mTrackListeners.Clone()) {
    l->NotifyRemoved(Graph());
  }
  mTrackListeners.Clear();

  RemoveAllDirectListenersImpl();

  if (mSegment) {
    mSegment->Clear();
  }
}

void MediaTrack::DestroyImpl() {
  for (int32_t i = mConsumers.Length() - 1; i >= 0; --i) {
    mConsumers[i]->Disconnect();
  }
  if (mSegment) {
    mSegment->Clear();
  }
  mGraph = nullptr;
}

void MediaTrack::Destroy() {
  // Keep this track alive until we leave this method
  RefPtr<MediaTrack> kungFuDeathGrip = this;
  // Keep a reference to the graph, since Message might RunDuringShutdown()
  // synchronously and make GraphImpl() invalid.
  RefPtr<MediaTrackGraphImpl> graph = GraphImpl();

  QueueControlOrShutdownMessage(
      [self = RefPtr{this}, this](IsInShutdown aInShutdown) {
        if (aInShutdown == IsInShutdown::No) {
          OnGraphThreadDone();
        }
        TRACE("MediaTrack::Destroy ControlMessage");
        RemoveAllResourcesAndListenersImpl();
        auto* graph = GraphImpl();
        DestroyImpl();
        graph->RemoveTrackGraphThread(this);
      });
  graph->RemoveTrack(this);
  // Message::RunDuringShutdown may have removed this track from the graph,
  // but our kungFuDeathGrip above will have kept this track alive if
  // necessary.
  mMainThreadDestroyed = true;
}

uint64_t MediaTrack::GetWindowId() const { return GraphImpl()->mWindowID; }

TrackTime MediaTrack::GetEnd() const {
  return mSegment ? mSegment->GetDuration() : 0;
}

void MediaTrack::AddAudioOutput(void* aKey, const AudioDeviceInfo* aSink) {
  MOZ_ASSERT(NS_IsMainThread());
  AudioDeviceID deviceID = nullptr;
  TrackRate preferredSampleRate = 0;
  if (aSink) {
    deviceID = aSink->DeviceID();
    preferredSampleRate = static_cast<TrackRate>(aSink->DefaultRate());
  }
  AddAudioOutput(aKey, deviceID, preferredSampleRate);
}

void MediaTrack::AddAudioOutput(void* aKey, CubebUtils::AudioDeviceID aDeviceID,
                                TrackRate aPreferredSampleRate) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mMainThreadDestroyed) {
    return;
  }
  LOG(LogLevel::Info, ("MediaTrack {} adding AudioOutput", fmt::ptr(this)));
  GraphImpl()->RegisterAudioOutput(this, aKey, aDeviceID, aPreferredSampleRate);
}

void MediaTrackGraphImpl::SetAudioOutputVolume(MediaTrack* aTrack, void* aKey,
                                               float aVolume) {
  MOZ_ASSERT(NS_IsMainThread());
  for (auto& params : mAudioOutputParams) {
    if (params.mKey == aKey && aTrack == params.mTrack) {
      params.mVolume = aVolume;
      UpdateAudioOutput(aTrack, params.mDeviceID);
      return;
    }
  }
  MOZ_CRASH("Audio output key not found when setting the volume.");
}

void MediaTrack::SetAudioOutputVolume(void* aKey, float aVolume) {
  if (mMainThreadDestroyed) {
    return;
  }
  GraphImpl()->SetAudioOutputVolume(this, aKey, aVolume);
}

void MediaTrack::RemoveAudioOutput(void* aKey) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mMainThreadDestroyed) {
    return;
  }
  LOG(LogLevel::Info, ("MediaTrack {} removing AudioOutput", fmt::ptr(this)));
  GraphImpl()->UnregisterAudioOutput(this, aKey);
}

void MediaTrackGraphImpl::RegisterAudioOutput(
    MediaTrack* aTrack, void* aKey, CubebUtils::AudioDeviceID aDeviceID,
    TrackRate aPreferredSampleRate) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mAudioOutputParams.Contains(TrackAndKey{aTrack, aKey}));

  IncrementOutputDeviceRefCnt(aDeviceID, aPreferredSampleRate);

  mAudioOutputParams.EmplaceBack(
      TrackKeyDeviceAndVolume{aTrack, aKey, aDeviceID, 1.f});

  UpdateAudioOutput(aTrack, aDeviceID);
}

void MediaTrackGraphImpl::UnregisterAudioOutput(MediaTrack* aTrack,
                                                void* aKey) {
  MOZ_ASSERT(NS_IsMainThread());

  size_t index = mAudioOutputParams.IndexOf(TrackAndKey{aTrack, aKey});
  MOZ_ASSERT(index != mAudioOutputParams.NoIndex);
  AudioDeviceID deviceID = mAudioOutputParams[index].mDeviceID;
  mAudioOutputParams.UnorderedRemoveElementAt(index);

  UpdateAudioOutput(aTrack, deviceID);

  DecrementOutputDeviceRefCnt(deviceID);
}

void MediaTrackGraphImpl::UpdateAudioOutput(MediaTrack* aTrack,
                                            AudioDeviceID aDeviceID) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!aTrack->IsDestroyed());

  float volume = 0.f;
  bool found = false;
  for (const auto& params : mAudioOutputParams) {
    if (params.mTrack == aTrack && params.mDeviceID == aDeviceID) {
      volume += params.mVolume;
      found = true;
    }
  }

  QueueControlMessageWithNoShutdown(
      // track has a strong reference to this.
      [track = RefPtr{aTrack}, aDeviceID, volume, found] {
        TRACE("MediaTrack::UpdateAudioOutput ControlMessage");
        MediaTrackGraphImpl* graph = track->GraphImpl();
        auto& outputDevicesRef = graph->mOutputDevices;
        size_t deviceIndex = outputDevicesRef.IndexOf(aDeviceID);
        MOZ_ASSERT(deviceIndex != outputDevicesRef.NoIndex);
        auto& deviceOutputsRef = outputDevicesRef[deviceIndex].mTrackOutputs;
        if (found) {
          for (auto& outputRef : deviceOutputsRef) {
            if (outputRef.mTrack == track) {
              outputRef.mVolume = volume;
              return;
            }
          }
          deviceOutputsRef.EmplaceBack(TrackAndVolume{track, volume});
        } else {
          DebugOnly<bool> removed = deviceOutputsRef.RemoveElement(track);
          MOZ_ASSERT(removed);
          // mOutputDevices[0] is retained for AudioCallbackDriver output even
          // when no tracks have audio outputs.
          if (deviceIndex != 0 && deviceOutputsRef.IsEmpty()) {
            // The device is no longer in use.
            outputDevicesRef.UnorderedRemoveElementAt(deviceIndex);
          }
        }
      });
}

void MediaTrackGraphImpl::IncrementOutputDeviceRefCnt(
    AudioDeviceID aDeviceID, TrackRate aPreferredSampleRate) {
  MOZ_ASSERT(NS_IsMainThread());

  for (auto& elementRef : mOutputDeviceRefCnts) {
    if (elementRef.mDeviceID == aDeviceID) {
      ++elementRef.mRefCnt;
      return;
    }
  }
  MOZ_ASSERT(aDeviceID != mPrimaryOutputDeviceID,
             "mOutputDeviceRefCnts should always have the primary device");
  // Need to add an output device.
  // Output via another graph for this device.
  // This sample rate is not exposed to content.
  TrackRate sampleRate =
      aPreferredSampleRate != 0
          ? aPreferredSampleRate
          : static_cast<TrackRate>(CubebUtils::PreferredSampleRate(
                /*aShouldResistFingerprinting*/ false));
  MediaTrackGraph* newGraph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::AUDIO_THREAD_DRIVER, mWindowID, sampleRate, aDeviceID,
      GetMainThreadSerialEventTarget());
  // CreateCrossGraphReceiver wants the sample rate of this graph.
  RefPtr receiver = newGraph->CreateCrossGraphReceiver(mSampleRate);
  receiver->AddAudioOutput(nullptr, aDeviceID, sampleRate);
  mOutputDeviceRefCnts.EmplaceBack(
      DeviceReceiverAndCount{aDeviceID, receiver, 1});

  QueueControlMessageWithNoShutdown([self = RefPtr{this}, this, aDeviceID,
                                     receiver = std::move(receiver)]() mutable {
    TRACE("MediaTrackGraph add output device ControlMessage");
    MOZ_ASSERT(!mOutputDevices.Contains(aDeviceID));
    mOutputDevices.EmplaceBack(
        OutputDeviceEntry{aDeviceID, std::move(receiver)});
  });
}

void MediaTrackGraphImpl::DecrementOutputDeviceRefCnt(AudioDeviceID aDeviceID) {
  MOZ_ASSERT(NS_IsMainThread());

  size_t index = mOutputDeviceRefCnts.IndexOf(aDeviceID);
  MOZ_ASSERT(index != mOutputDeviceRefCnts.NoIndex);
  // mOutputDeviceRefCnts[0] is retained for consistency with
  // mOutputDevices[0], which is retained for AudioCallbackDriver output even
  // when no tracks have audio outputs.
  if (--mOutputDeviceRefCnts[index].mRefCnt == 0 && index != 0) {
    mOutputDeviceRefCnts[index].mReceiver->Destroy();
    mOutputDeviceRefCnts.UnorderedRemoveElementAt(index);
  }
}

void MediaTrack::Suspend() {
  // This can happen if this method has been called asynchronously, and the
  // track has been destroyed since then.
  if (mMainThreadDestroyed) {
    return;
  }
  QueueControlMessageWithNoShutdown([self = RefPtr{this}, this] {
    TRACE("MediaTrack::IncrementSuspendCount ControlMessage");
    IncrementSuspendCount();
  });
}

void MediaTrack::Resume() {
  // This can happen if this method has been called asynchronously, and the
  // track has been destroyed since then.
  if (mMainThreadDestroyed) {
    return;
  }
  QueueControlMessageWithNoShutdown([self = RefPtr{this}, this] {
    TRACE("MediaTrack::DecrementSuspendCount ControlMessage");
    DecrementSuspendCount();
  });
}

void MediaTrack::AddListenerImpl(
    already_AddRefed<MediaTrackListener> aListener) {
  RefPtr<MediaTrackListener> l(aListener);
  mTrackListeners.AppendElement(std::move(l));

  PrincipalHandle lastPrincipalHandle = mSegment->GetLastPrincipalHandle();
  mTrackListeners.LastElement()->NotifyPrincipalHandleChanged(
      Graph(), lastPrincipalHandle);
  if (mNotifiedEnded) {
    mTrackListeners.LastElement()->NotifyEnded(Graph());
  }
  if (CombinedDisabledMode() == DisabledTrackMode::SILENCE_BLACK) {
    mTrackListeners.LastElement()->NotifyEnabledStateChanged(Graph(), false);
  }
}

void MediaTrack::AddListener(MediaTrackListener* aListener) {
  MOZ_ASSERT(mSegment, "Segment-less tracks do not support listeners");
  if (mMainThreadDestroyed) {
    return;
  }
  QueueControlMessageWithNoShutdown(
      [self = RefPtr{this}, this, listener = RefPtr{aListener}]() mutable {
        TRACE("MediaTrack::AddListenerImpl ControlMessage");
        AddListenerImpl(listener.forget());
      });
}

void MediaTrack::RemoveListenerImpl(MediaTrackListener* aListener) {
  for (size_t i = 0; i < mTrackListeners.Length(); ++i) {
    if (mTrackListeners[i] == aListener) {
      mTrackListeners[i]->NotifyRemoved(Graph());
      mTrackListeners.RemoveElementAt(i);
      return;
    }
  }
}

RefPtr<GenericPromise> MediaTrack::RemoveListener(
    MediaTrackListener* aListener) {
  MozPromiseHolder<GenericPromise> promiseHolder;
  RefPtr<GenericPromise> p = promiseHolder.Ensure(__func__);
  if (mMainThreadDestroyed) {
    promiseHolder.Reject(NS_ERROR_FAILURE, __func__);
    return p;
  }
  QueueControlOrShutdownMessage(
      [self = RefPtr{this}, this, listener = RefPtr{aListener},
       promiseHolder = std::move(promiseHolder)](IsInShutdown) mutable {
        TRACE("MediaTrack::RemoveListenerImpl ControlMessage");
        // During shutdown we still want the listener's NotifyRemoved to be
        // called, since not doing that might block shutdown of other modules.
        RemoveListenerImpl(listener);
        promiseHolder.Resolve(true, __func__);
      });
  return p;
}

void MediaTrack::AddDirectListenerImpl(
    already_AddRefed<DirectMediaTrackListener> aListener) {
  AssertOnGraphThread();
  // Base implementation, for tracks that don't support direct track listeners.
  RefPtr<DirectMediaTrackListener> listener = aListener;
  listener->NotifyDirectListenerInstalled(
      DirectMediaTrackListener::InstallationResult::TRACK_NOT_SUPPORTED);
}

void MediaTrack::AddDirectListener(DirectMediaTrackListener* aListener) {
  if (mMainThreadDestroyed) {
    return;
  }
  QueueControlMessageWithNoShutdown(
      [self = RefPtr{this}, this, listener = RefPtr{aListener}]() mutable {
        TRACE("MediaTrack::AddDirectListenerImpl ControlMessage");
        AddDirectListenerImpl(listener.forget());
      });
}

void MediaTrack::RemoveDirectListenerImpl(DirectMediaTrackListener* aListener) {
  // Base implementation, the listener was never added so nothing to do.
}

void MediaTrack::RemoveDirectListener(DirectMediaTrackListener* aListener) {
  if (mMainThreadDestroyed) {
    return;
  }
  QueueControlOrShutdownMessage(
      [self = RefPtr{this}, this, listener = RefPtr{aListener}](IsInShutdown) {
        TRACE("MediaTrack::RemoveDirectListenerImpl ControlMessage");
        // During shutdown we still want the listener's
        // NotifyDirectListenerUninstalled to be called, since not doing that
        // might block shutdown of other modules.
        RemoveDirectListenerImpl(listener);
      });
}

void MediaTrack::RunAfterPendingUpdates(
    already_AddRefed<nsIRunnable> aRunnable) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mMainThreadDestroyed) {
    return;
  }
  QueueControlOrShutdownMessage(
      [self = RefPtr{this}, this,
       runnable = nsCOMPtr{aRunnable}](IsInShutdown aInShutdown) mutable {
        TRACE("MediaTrack::DispatchToMainThreadStableState ControlMessage");
        if (aInShutdown == IsInShutdown::No) {
          Graph()->DispatchToMainThreadStableState(runnable.forget());
        } else {
          // Don't run mRunnable now as it may call AppendMessage() which would
          // assume that there are no remaining
          // controlMessagesToRunDuringShutdown.
          MOZ_ASSERT(NS_IsMainThread());
          GraphImpl()->Dispatch(runnable.forget());
        }
      });
}

void MediaTrack::SetDisabledTrackModeImpl(DisabledTrackMode aMode) {
  AssertOnGraphThread();
  MOZ_DIAGNOSTIC_ASSERT(
      aMode == DisabledTrackMode::ENABLED ||
          mDisabledMode == DisabledTrackMode::ENABLED,
      "Changing disabled track mode for a track is not allowed");
  DisabledTrackMode oldMode = CombinedDisabledMode();
  mDisabledMode = aMode;
  NotifyIfDisabledModeChangedFrom(oldMode);
}

void MediaTrack::SetDisabledTrackMode(DisabledTrackMode aMode) {
  if (mMainThreadDestroyed) {
    return;
  }
  QueueControlMessageWithNoShutdown([self = RefPtr{this}, this, aMode]() {
    TRACE("MediaTrack::SetDisabledTrackModeImpl ControlMessage");
    SetDisabledTrackModeImpl(aMode);
  });
}

void MediaTrack::ApplyTrackDisabling(MediaSegment* aSegment,
                                     MediaSegment* aRawSegment) {
  AssertOnGraphThread();
  mozilla::ApplyTrackDisabling(mDisabledMode, aSegment, aRawSegment);
}

void MediaTrack::AddMainThreadListener(
    MainThreadMediaTrackListener* aListener) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aListener);
  MOZ_ASSERT(!mMainThreadListeners.Contains(aListener));

  mMainThreadListeners.AppendElement(aListener);

  // If it is not yet time to send the notification, then exit here.
  if (!mEndedNotificationSent) {
    return;
  }

  class NotifyRunnable final : public Runnable {
   public:
    explicit NotifyRunnable(MediaTrack* aTrack)
        : Runnable("MediaTrack::NotifyRunnable"), mTrack(aTrack) {}

    NS_IMETHOD Run() override {
      TRACE("MediaTrack::NotifyMainThreadListeners Runnable");
      MOZ_ASSERT(NS_IsMainThread());
      mTrack->NotifyMainThreadListeners();
      return NS_OK;
    }

   private:
    ~NotifyRunnable() = default;

    RefPtr<MediaTrack> mTrack;
  };

  nsCOMPtr<nsIRunnable> runnable = new NotifyRunnable(this);
  GraphImpl()->Dispatch(runnable.forget());
}

void MediaTrack::AdvanceTimeVaryingValuesToCurrentTime(GraphTime aCurrentTime,
                                                       GraphTime aBlockedTime) {
  mStartTime += aBlockedTime;

  if (!mSegment) {
    // No data to be forgotten.
    return;
  }

  TrackTime time = aCurrentTime - mStartTime;
  // Only prune if there is a reasonable chunk (50ms) to forget, so we don't
  // spend too much time pruning segments.
  const TrackTime minChunkSize = mSampleRate * 50 / 1000;
  if (time < mForgottenTime + minChunkSize) {
    return;
  }

  mForgottenTime = std::min(GetEnd() - 1, time);
  mSegment->ForgetUpTo(mForgottenTime);
}

void MediaTrack::NotifyIfDisabledModeChangedFrom(DisabledTrackMode aOldMode) {
  DisabledTrackMode mode = CombinedDisabledMode();
  if (aOldMode == mode) {
    return;
  }

  for (const auto& listener : mTrackListeners) {
    listener->NotifyEnabledStateChanged(
        Graph(), mode != DisabledTrackMode::SILENCE_BLACK);
  }

  for (const auto& c : mConsumers) {
    if (c->GetDestination()) {
      c->GetDestination()->OnInputDisabledModeChanged(mode);
    }
  }
}

void MediaTrack::QueueMessage(UniquePtr<ControlMessageInterface> aMessage) {
  MOZ_ASSERT(NS_IsMainThread(), "Main thread only");
  MOZ_RELEASE_ASSERT(!IsDestroyed());
  GraphImpl()->AppendMessage(std::move(aMessage));
}

void MediaTrack::RunMessageAfterProcessing(
    UniquePtr<ControlMessageInterface> aMessage) {
  AssertOnGraphThread();
  GraphImpl()->RunMessageAfterProcessing(std::move(aMessage));
}

SourceMediaTrack::SourceMediaTrack(MediaSegment::Type aType,
                                   TrackRate aSampleRate)
    : MediaTrack(aSampleRate, aType,
                 aType == MediaSegment::AUDIO
                     ? static_cast<MediaSegment*>(new AudioSegment())
                     : static_cast<MediaSegment*>(new VideoSegment())),
      mMutex("mozilla::media::SourceMediaTrack") {
  mUpdateTrack = MakeUnique<TrackData>();
  mUpdateTrack->mInputRate = aSampleRate;
  mUpdateTrack->mResamplerChannelCount = 0;
  mUpdateTrack->mData = UniquePtr<MediaSegment>(mSegment->CreateEmptyClone());
  mUpdateTrack->mEnded = false;
  mUpdateTrack->mPullingEnabled = false;
  mUpdateTrack->mGraphThreadDone = false;
}

void SourceMediaTrack::DestroyImpl() {
  GraphImpl()->AssertOnGraphThreadOrNotRunning();
  for (int32_t i = mConsumers.Length() - 1; i >= 0; --i) {
    // Disconnect before we come under mMutex's lock since it can call back
    // through RemoveDirectListenerImpl() and deadlock.
    mConsumers[i]->Disconnect();
  }

  // Hold mMutex while mGraph is reset so that other threads holding mMutex
  // can null-check know that the graph will not destroyed.
  MutexAutoLock lock(mMutex);
  mUpdateTrack = nullptr;
  MediaTrack::DestroyImpl();
}

void SourceMediaTrack::SetPullingEnabled(bool aEnabled) {
  class Message : public ControlMessage {
   public:
    Message(SourceMediaTrack* aTrack, bool aEnabled)
        : ControlMessage(nullptr), mTrack(aTrack), mEnabled(aEnabled) {}
    void Run() override {
      TRACE("SourceMediaTrack::SetPullingEnabled ControlMessage");
      MutexAutoLock lock(mTrack->mMutex);
      if (!mTrack->mUpdateTrack) {
        // We can't enable pulling for a track that has ended. We ignore
        // this if we're disabling pulling, since shutdown sequences are
        // complex. If there's truly an issue we'll have issues enabling anyway.
        MOZ_ASSERT_IF(mEnabled, mTrack->mEnded);
        return;
      }
      MOZ_ASSERT(mTrack->mType == MediaSegment::AUDIO,
                 "Pulling is not allowed for video");
      mTrack->mUpdateTrack->mPullingEnabled = mEnabled;
    }
    SourceMediaTrack* mTrack;
    bool mEnabled;
  };
  GraphImpl()->AppendMessage(MakeUnique<Message>(this, aEnabled));
}

bool SourceMediaTrack::PullNewData(GraphTime aDesiredUpToTime) {
  TRACE_COMMENT("SourceMediaTrack::PullNewData", "%p", this);
  TrackTime t;
  TrackTime current;
  {
    if (mEnded) {
      return false;
    }
    MutexAutoLock lock(mMutex);
    if (mUpdateTrack->mEnded) {
      return false;
    }
    if (!mUpdateTrack->mPullingEnabled) {
      return false;
    }
    // Compute how much track time we'll need assuming we don't block
    // the track at all.
    t = GraphTimeToTrackTime(aDesiredUpToTime);
    current = GetEnd() + mUpdateTrack->mData->GetDuration();
  }
  if (t <= current) {
    return false;
  }
  LOG(LogLevel::Verbose, ("{}: Calling NotifyPull track={} t={} current end={}",
                          fmt::ptr(GraphImpl()), fmt::ptr(this),
                          GraphImpl()->MediaTimeToSeconds(t),
                          GraphImpl()->MediaTimeToSeconds(current)));
  for (auto& l : mTrackListeners) {
    l->NotifyPull(Graph(), current, t);
  }
  return true;
}

/**
 * This moves chunks from aIn to aOut. For audio this is simple. For video
 * we carry durations over if present, or extend up to aDesiredUpToTime if not.
 *
 * We also handle "resetters" from captured media elements. This type of source
 * pushes future frames into the track, and should it need to remove some, e.g.,
 * because of a seek or pause, it tells us by letting time go backwards. Without
 * this, tracks would be live for too long after a seek or pause.
 */
static void MoveToSegment(SourceMediaTrack* aTrack, MediaSegment* aIn,
                          MediaSegment* aOut, TrackTime aCurrentTime,
                          TrackTime aDesiredUpToTime)
    MOZ_REQUIRES(aTrack->GetMutex()) {
  MOZ_ASSERT(aIn->GetType() == aOut->GetType());
  MOZ_ASSERT(aOut->GetDuration() >= aCurrentTime);
  MOZ_ASSERT(aDesiredUpToTime >= aCurrentTime);
  if (aIn->GetType() == MediaSegment::AUDIO) {
    AudioSegment* in = static_cast<AudioSegment*>(aIn);
    AudioSegment* out = static_cast<AudioSegment*>(aOut);
    TrackTime desiredDurationToMove = aDesiredUpToTime - aCurrentTime;
    TrackTime end = std::min(in->GetDuration(), desiredDurationToMove);

    out->AppendSlice(*in, 0, end);
    in->RemoveLeading(end);

    aTrack->GetMutex().AssertCurrentThreadOwns();
    out->ApplyVolume(aTrack->GetVolumeLocked());
  } else {
    VideoSegment* in = static_cast<VideoSegment*>(aIn);
    VideoSegment* out = static_cast<VideoSegment*>(aOut);
    for (VideoSegment::ConstChunkIterator c(*in); !c.IsEnded(); c.Next()) {
      MOZ_ASSERT(!c->mTimeStamp.IsNull());
      VideoChunk* last = out->GetLastChunk();
      if (!last || last->mTimeStamp.IsNull()) {
        // This is the first frame, or the last frame pushed to `out` has been
        // all consumed. Just append and we deal with its duration later.
        out->AppendFrame(*c);
        if (c->GetDuration() > 0) {
          out->ExtendLastFrameBy(c->GetDuration());
        }
        continue;
      }

      // We now know when this frame starts, aka when the last frame ends.

      if (c->mTimeStamp < last->mTimeStamp) {
        // Time is going backwards. This is a resetting frame from
        // DecodedStream. Clear everything up to currentTime.
        out->Clear();
        out->AppendNullData(aCurrentTime);
      }

      // Append the current frame (will have duration 0).
      out->AppendFrame(*c);
      if (c->GetDuration() > 0) {
        out->ExtendLastFrameBy(c->GetDuration());
      }
    }
    if (out->GetDuration() < aDesiredUpToTime) {
      out->ExtendLastFrameBy(aDesiredUpToTime - out->GetDuration());
    }
    in->Clear();
    MOZ_ASSERT(aIn->GetDuration() == 0, "aIn must be consumed");
  }
}

void SourceMediaTrack::ExtractPendingInput(GraphTime aCurrentTime,
                                           GraphTime aDesiredUpToTime) {
  MutexAutoLock lock(mMutex);

  if (!mUpdateTrack) {
    MOZ_ASSERT(mEnded);
    return;
  }

  TrackTime trackCurrentTime = GraphTimeToTrackTime(aCurrentTime);

  ApplyTrackDisabling(mUpdateTrack->mData.get());

  if (!mUpdateTrack->mData->IsEmpty()) {
    for (const auto& l : mTrackListeners) {
      l->NotifyQueuedChanges(GraphImpl(), GetEnd(), *mUpdateTrack->mData);
    }
  }
  TrackTime trackDesiredUpToTime = GraphTimeToTrackTime(aDesiredUpToTime);
  TrackTime endTime = trackDesiredUpToTime;
  if (mUpdateTrack->mEnded) {
    endTime = std::min(trackDesiredUpToTime,
                       GetEnd() + mUpdateTrack->mData->GetDuration());
  }
  LOG(LogLevel::Verbose, ("{}: SourceMediaTrack {} advancing end from {} to {}",
                          fmt::ptr(GraphImpl()), fmt::ptr(this),
                          int64_t(trackCurrentTime), int64_t(endTime)));
  MoveToSegment(this, mUpdateTrack->mData.get(), mSegment.get(),
                trackCurrentTime, endTime);
  if (mUpdateTrack->mEnded && GetEnd() < trackDesiredUpToTime) {
    mEnded = true;
    mUpdateTrack = nullptr;
  }
}

void SourceMediaTrack::ResampleAudioToGraphSampleRate(MediaSegment* aSegment) {
  mMutex.AssertCurrentThreadOwns();
  if (aSegment->GetType() != MediaSegment::AUDIO ||
      mUpdateTrack->mInputRate == GraphImpl()->GraphRate()) {
    return;
  }
  AudioSegment* segment = static_cast<AudioSegment*>(aSegment);
  // 10 ms of 48 kHz audio always yields a representable size at GraphRate().
  DebugOnly<nsresult> rv = segment->ResampleChunks(
      mUpdateTrack->mResampler, &mUpdateTrack->mResamplerChannelCount,
      mUpdateTrack->mInputRate, GraphImpl()->GraphRate());
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

void SourceMediaTrack::AdvanceTimeVaryingValuesToCurrentTime(
    GraphTime aCurrentTime, GraphTime aBlockedTime) {
  MutexAutoLock lock(mMutex);
  MediaTrack::AdvanceTimeVaryingValuesToCurrentTime(aCurrentTime, aBlockedTime);
}

void SourceMediaTrack::SetAppendDataSourceRate(TrackRate aRate) {
  MutexAutoLock lock(mMutex);
  if (!mUpdateTrack) {
    return;
  }
  MOZ_DIAGNOSTIC_ASSERT(mSegment->GetType() == MediaSegment::AUDIO);
  // Set the new input rate and reset the resampler.
  mUpdateTrack->mInputRate = aRate;
  mUpdateTrack->mResampler.own(nullptr);
  mUpdateTrack->mResamplerChannelCount = 0;
}

TrackTime SourceMediaTrack::AppendData(MediaSegment* aSegment,
                                       MediaSegment* aRawSegment) {
  MutexAutoLock lock(mMutex);
  MOZ_DIAGNOSTIC_ASSERT(aSegment->GetType() == mType);
  TrackTime appended = 0;
  if (!mUpdateTrack || mUpdateTrack->mEnded || mUpdateTrack->mGraphThreadDone) {
    aSegment->Clear();
    return appended;
  }

  // Data goes into mData, and on the next iteration of the MTG moves
  // into the track's segment after NotifyQueuedTrackChanges().  This adds
  // 0-10ms of delay before data gets to direct listeners.
  // Indirect listeners (via subsequent TrackUnion nodes) are synced to
  // playout time, and so can be delayed by buffering.

  // Apply track disabling before notifying any consumers directly
  // or inserting into the graph
  mozilla::ApplyTrackDisabling(mDirectDisabledMode, aSegment, aRawSegment);

  ResampleAudioToGraphSampleRate(aSegment);

  // Must notify first, since AppendFrom() will empty out aSegment
  NotifyDirectConsumers(aRawSegment ? aRawSegment : aSegment);
  appended = aSegment->GetDuration();
  mUpdateTrack->mData->AppendFrom(aSegment);  // note: aSegment is now dead
  {
    auto graph = GraphImpl();
    MonitorAutoLock lock(graph->GetMonitor());
    if (graph->CurrentDriver()) {  // graph has not completed forced shutdown
      graph->EnsureNextIteration();
    }
  }

  return appended;
}

TrackTime SourceMediaTrack::ClearFutureData() {
  MutexAutoLock lock(mMutex);
  auto graph = GraphImpl();
  if (!mUpdateTrack || !graph) {
    return 0;
  }

  TrackTime duration = mUpdateTrack->mData->GetDuration();
  mUpdateTrack->mData->Clear();
  return duration;
}

void SourceMediaTrack::NotifyDirectConsumers(MediaSegment* aSegment) {
  mMutex.AssertCurrentThreadOwns();

  for (const auto& l : mDirectTrackListeners) {
    TrackTime offset = 0;  // FIX! need a separate TrackTime.... or the end of
                           // the internal buffer
    l->NotifyRealtimeTrackDataAndApplyTrackDisabling(Graph(), offset,
                                                     *aSegment);
  }
}

void SourceMediaTrack::AddDirectListenerImpl(
    already_AddRefed<DirectMediaTrackListener> aListener) {
  AssertOnGraphThread();
  MutexAutoLock lock(mMutex);

  RefPtr<DirectMediaTrackListener> listener = aListener;
  LOG(LogLevel::Debug,
      ("{}: Adding direct track listener {} to source track {}",
       fmt::ptr(GraphImpl()), fmt::ptr(listener.get()), fmt::ptr(this)));

  MOZ_ASSERT(mType == MediaSegment::VIDEO);
  for (const auto& l : mDirectTrackListeners) {
    if (l == listener) {
      listener->NotifyDirectListenerInstalled(
          DirectMediaTrackListener::InstallationResult::ALREADY_EXISTS);
      return;
    }
  }

  mDirectTrackListeners.AppendElement(listener);

  LOG(LogLevel::Debug, ("{}: Added direct track listener {}",
                        fmt::ptr(GraphImpl()), fmt::ptr(listener.get())));
  listener->NotifyDirectListenerInstalled(
      DirectMediaTrackListener::InstallationResult::SUCCESS);

  if (mDisabledMode != DisabledTrackMode::ENABLED) {
    listener->IncreaseDisabled(mDisabledMode);
  }

  if (mEnded) {
    return;
  }

  // Pass buffered data to the listener
  VideoSegment bufferedData;
  size_t videoFrames = 0;
  VideoSegment& segment = *GetData<VideoSegment>();
  for (VideoSegment::ConstChunkIterator iter(segment); !iter.IsEnded();
       iter.Next()) {
    if (iter->mTimeStamp.IsNull()) {
      // No timestamp means this is only for the graph's internal book-keeping,
      // denoting a late start of the track.
      continue;
    }
    ++videoFrames;
    bufferedData.AppendFrame(*iter);
  }

  VideoSegment& video = static_cast<VideoSegment&>(*mUpdateTrack->mData);
  for (VideoSegment::ConstChunkIterator iter(video); !iter.IsEnded();
       iter.Next()) {
    ++videoFrames;
    MOZ_ASSERT(!iter->mTimeStamp.IsNull());
    bufferedData.AppendFrame(*iter);
  }

  LOG(LogLevel::Info,
      ("{}: Notifying direct listener {} of {} video frames and duration "
       "{}",
       fmt::ptr(GraphImpl()), fmt::ptr(listener.get()), videoFrames,
       bufferedData.GetDuration()));
  listener->NotifyRealtimeTrackData(Graph(), 0, bufferedData);
}

void SourceMediaTrack::RemoveDirectListenerImpl(
    DirectMediaTrackListener* aListener) {
  mGraph->AssertOnGraphThreadOrNotRunning();
  MutexAutoLock lock(mMutex);
  for (int32_t i = mDirectTrackListeners.Length() - 1; i >= 0; --i) {
    const RefPtr<DirectMediaTrackListener>& l = mDirectTrackListeners[i];
    if (l == aListener) {
      if (mDisabledMode != DisabledTrackMode::ENABLED) {
        aListener->DecreaseDisabled(mDisabledMode);
      }
      aListener->NotifyDirectListenerUninstalled();
      mDirectTrackListeners.RemoveElementAt(i);
    }
  }
}

void SourceMediaTrack::End() {
  MutexAutoLock lock(mMutex);
  if (!mUpdateTrack) {
    // Already ended
    return;
  }
  mUpdateTrack->mEnded = true;
  if (auto graph = GraphImpl()) {
    MonitorAutoLock lock(graph->GetMonitor());
    if (graph->CurrentDriver()) {  // graph has not completed forced shutdown
      graph->EnsureNextIteration();
    }
  }
}

void SourceMediaTrack::SetDisabledTrackModeImpl(DisabledTrackMode aMode) {
  AssertOnGraphThread();
  {
    MutexAutoLock lock(mMutex);
    const DisabledTrackMode oldMode = mDirectDisabledMode;
    const bool oldEnabled = oldMode == DisabledTrackMode::ENABLED;
    const bool enabled = aMode == DisabledTrackMode::ENABLED;
    mDirectDisabledMode = aMode;
    for (const auto& l : mDirectTrackListeners) {
      if (!oldEnabled && enabled) {
        LOG(LogLevel::Debug, ("{}: SourceMediaTrack {} setting "
                              "direct listener enabled",
                              fmt::ptr(GraphImpl()), fmt::ptr(this)));
        l->DecreaseDisabled(oldMode);
      } else if (oldEnabled && !enabled) {
        LOG(LogLevel::Debug, ("{}: SourceMediaTrack {} setting "
                              "direct listener disabled",
                              fmt::ptr(GraphImpl()), fmt::ptr(this)));
        l->IncreaseDisabled(aMode);
      }
    }
  }
  MediaTrack::SetDisabledTrackModeImpl(aMode);
}

uint32_t SourceMediaTrack::NumberOfChannels() const {
  AudioSegment* audio = GetData<AudioSegment>();
  MOZ_DIAGNOSTIC_ASSERT(audio);
  if (!audio) {
    return 0;
  }
  return audio->MaxChannelCount();
}

void SourceMediaTrack::RemoveAllDirectListenersImpl() {
  GraphImpl()->AssertOnGraphThreadOrNotRunning();
  MutexAutoLock lock(mMutex);

  for (auto& l : mDirectTrackListeners.Clone()) {
    l->NotifyDirectListenerUninstalled();
  }
  mDirectTrackListeners.Clear();
}

void SourceMediaTrack::SetVolume(float aVolume) {
  MutexAutoLock lock(mMutex);
  mVolume = aVolume;
}

float SourceMediaTrack::GetVolumeLocked() {
  mMutex.AssertCurrentThreadOwns();
  return mVolume;
}

SourceMediaTrack::~SourceMediaTrack() = default;

void MediaInputPort::Init() {
  mGraph->AssertOnGraphThreadOrNotRunning();
  LOG(LogLevel::Debug,
      ("{}: Adding MediaInputPort {} (from {} to {})", fmt::ptr(mGraph),
       fmt::ptr(this), fmt::ptr(mSource), fmt::ptr(mDest)));
  // Only connect the port if it wasn't disconnected on allocation.
  if (mSource) {
    mSource->AddConsumer(this);
    mDest->AddInput(this);
  }
  // mPortCount decremented via MediaInputPort::Destroy's message
  ++mGraph->mPortCount;
}

void MediaInputPort::Disconnect() {
  mGraph->AssertOnGraphThreadOrNotRunning();
  NS_ASSERTION(!mSource == !mDest,
               "mSource and mDest must either both be null or both non-null");

  if (!mSource) {
    return;
  }

  mSource->RemoveConsumer(this);
  mDest->RemoveInput(this);
  mSource = nullptr;
  mDest = nullptr;

  mGraph->SetTrackOrderDirty();
}

MediaTrack* MediaInputPort::GetSource() const {
  mGraph->AssertOnGraphThreadOrNotRunning();
  return mSource;
}

ProcessedMediaTrack* MediaInputPort::GetDestination() const {
  mGraph->AssertOnGraphThreadOrNotRunning();
  return mDest;
}

MediaInputPort::InputInterval MediaInputPort::GetNextInputInterval(
    MediaInputPort const* aPort, GraphTime aTime) {
  InputInterval result = {GRAPH_TIME_MAX, GRAPH_TIME_MAX, false};
  if (!aPort) {
    result.mStart = aTime;
    result.mInputIsBlocked = true;
    return result;
  }
  aPort->mGraph->AssertOnGraphThreadOrNotRunning();
  if (aTime >= aPort->mDest->mStartBlocking) {
    return result;
  }
  result.mStart = aTime;
  result.mEnd = aPort->mDest->mStartBlocking;
  result.mInputIsBlocked = aTime >= aPort->mSource->mStartBlocking;
  if (!result.mInputIsBlocked) {
    result.mEnd = std::min(result.mEnd, aPort->mSource->mStartBlocking);
  }
  return result;
}

void MediaInputPort::Suspended() {
  mGraph->AssertOnGraphThreadOrNotRunning();
  mDest->InputSuspended(this);
}

void MediaInputPort::Resumed() {
  mGraph->AssertOnGraphThreadOrNotRunning();
  mDest->InputResumed(this);
}

void MediaInputPort::Destroy() {
  class Message : public ControlMessage {
   public:
    explicit Message(MediaInputPort* aPort)
        : ControlMessage(nullptr), mPort(aPort) {}
    void Run() override {
      TRACE("MediaInputPort::Destroy ControlMessage");
      mPort->Disconnect();
      --mPort->GraphImpl()->mPortCount;
      mPort->SetGraphImpl(nullptr);
      NS_RELEASE(mPort);
    }
    void RunDuringShutdown() override { Run(); }
    MediaInputPort* mPort;
  };
  // Keep a reference to the graph, since Message might RunDuringShutdown()
  // synchronously and make GraphImpl() invalid.
  RefPtr<MediaTrackGraphImpl> graph = mGraph;
  graph->AppendMessage(MakeUnique<Message>(this));
  --graph->mMainThreadPortCount;
}

MediaTrackGraphImpl* MediaInputPort::GraphImpl() const {
  mGraph->AssertOnGraphThreadOrNotRunning();
  return mGraph;
}

MediaTrackGraph* MediaInputPort::Graph() const { return mGraph; }

void MediaInputPort::SetGraphImpl(MediaTrackGraphImpl* aGraph) {
  MOZ_ASSERT(!mGraph || !aGraph, "Should only be set once");
  DebugOnly<MediaTrackGraphImpl*> graph = mGraph ? mGraph : aGraph;
  MOZ_ASSERT(graph->OnGraphThreadOrNotRunning());
  mGraph = aGraph;
}

already_AddRefed<MediaInputPort> ProcessedMediaTrack::AllocateInputPort(
    MediaTrack* aTrack, uint16_t aInputNumber, uint16_t aOutputNumber) {
  // This method creates two references to the MediaInputPort: one for
  // the main thread, and one for the MediaTrackGraph.
  class Message : public ControlMessage {
   public:
    explicit Message(MediaInputPort* aPort)
        : ControlMessage(aPort->mDest), mPort(aPort) {}
    void Run() override {
      TRACE("ProcessedMediaTrack::AllocateInputPort ControlMessage");
      mPort->Init();
      // The graph holds its reference implicitly
      mPort->GraphImpl()->SetTrackOrderDirty();
      NS_ADDREF(mPort.get());
    }
    void RunDuringShutdown() override { Run(); }
    RefPtr<MediaInputPort> mPort;
  };

  MOZ_DIAGNOSTIC_ASSERT(aTrack->mType == mType);
  RefPtr<MediaInputPort> port;
  if (aTrack->IsDestroyed()) {
    // Create a port that's disconnected, which is what it'd be after its source
    // track is Destroy()ed normally. Disconnect() is idempotent so destroying
    // this later is fine.
    port = new MediaInputPort(GraphImpl(), nullptr, nullptr, aInputNumber,
                              aOutputNumber);
  } else {
    MOZ_ASSERT(aTrack->GraphImpl() == GraphImpl());
    port = new MediaInputPort(GraphImpl(), aTrack, this, aInputNumber,
                              aOutputNumber);
  }
  ++GraphImpl()->mMainThreadPortCount;
  GraphImpl()->AppendMessage(MakeUnique<Message>(port));
  return port.forget();
}

void ProcessedMediaTrack::QueueSetAutoend(bool aAutoend) {
  class Message : public ControlMessage {
   public:
    Message(ProcessedMediaTrack* aTrack, bool aAutoend)
        : ControlMessage(aTrack), mAutoend(aAutoend) {}
    void Run() override {
      TRACE("ProcessedMediaTrack::SetAutoendImpl ControlMessage");
      static_cast<ProcessedMediaTrack*>(mTrack)->SetAutoendImpl(mAutoend);
    }
    bool mAutoend;
  };
  if (mMainThreadDestroyed) {
    return;
  }
  GraphImpl()->AppendMessage(MakeUnique<Message>(this, aAutoend));
}

void ProcessedMediaTrack::DestroyImpl() {
  for (int32_t i = mInputs.Length() - 1; i >= 0; --i) {
    mInputs[i]->Disconnect();
  }

  for (int32_t i = mSuspendedInputs.Length() - 1; i >= 0; --i) {
    mSuspendedInputs[i]->Disconnect();
  }

  MediaTrack::DestroyImpl();
  // The track order is only important if there are connections, in which
  // case MediaInputPort::Disconnect() called SetTrackOrderDirty().
  // MediaTrackGraphImpl::RemoveTrackGraphThread() will also call
  // SetTrackOrderDirty(), for other reasons.
}

MediaTrackGraphImpl::MediaTrackGraphImpl(uint64_t aWindowID,
                                         TrackRate aSampleRate,
                                         AudioDeviceID aPrimaryOutputDeviceID,
                                         nsISerialEventTarget* aMainThread)
    : MediaTrackGraph(aSampleRate, aPrimaryOutputDeviceID),
      mWindowID(aWindowID),
      mFirstCycleBreaker(0)
      // An offline graph is not initially processing.
      ,
      mPortCount(0),
      mMonitor("MediaTrackGraphImpl"),
      mLifecycleState(LIFECYCLE_THREAD_NOT_STARTED),
      mPostedRunInStableStateEvent(false),
      mGraphDriverRunning(false),
      mPostedRunInStableState(false),
      mTrackOrderDirty(false),
      mMainThread(aMainThread),
      mGlobalVolume(CubebUtils::GetVolumeScale())
#ifdef DEBUG
      ,
      mCanRunMessagesSynchronously(false)
#endif
      ,
      mMainThreadGraphTime(0, "MediaTrackGraphImpl::mMainThreadGraphTime"),
      mAudioOutputLatency(0.0),
      mMaxOutputChannelCount(CubebUtils::MaxNumberOfChannels()) {
}

void MediaTrackGraphImpl::Init(GraphDriverType aDriverRequested,
                               GraphRunType aRunTypeRequested,
                               uint32_t aChannelCount) {
  mSelfRef = this;
  mEndTime = aDriverRequested == OFFLINE_THREAD_DRIVER ? 0 : GRAPH_TIME_MAX;
  mRealtime = aDriverRequested != OFFLINE_THREAD_DRIVER;
  // The primary output device always exists because an AudioCallbackDriver
  // may exist, and want to be fed data, even when no tracks have audio
  // outputs.
  mOutputDeviceRefCnts.EmplaceBack(
      DeviceReceiverAndCount{mPrimaryOutputDeviceID, nullptr, 0});
  mOutputDevices.EmplaceBack(OutputDeviceEntry{mPrimaryOutputDeviceID});

  bool failedToGetShutdownBlocker = false;
  if (!IsNonRealtime()) {
    failedToGetShutdownBlocker = !AddShutdownBlocker();
  }

  mGraphRunner = aRunTypeRequested == SINGLE_THREAD
                     ? GraphRunner::Create(this)
                     : already_AddRefed<GraphRunner>(nullptr);

  if ((aRunTypeRequested == SINGLE_THREAD && !mGraphRunner) ||
      failedToGetShutdownBlocker) {
    MonitorAutoLock lock(mMonitor);
    // At least one of the following happened
    // - Failed to create thread.
    // - Failed to install a shutdown blocker when one is needed.
    // Because we have a fail state, jump to last phase of the lifecycle.
    mLifecycleState = LIFECYCLE_WAITING_FOR_TRACK_DESTRUCTION;
    RemoveShutdownBlocker();  // No-op if blocker wasn't added.
#ifdef DEBUG
    mCanRunMessagesSynchronously = true;
#endif
    return;
  }
  if (mRealtime) {
    if (aDriverRequested == AUDIO_THREAD_DRIVER) {
      // Always start with zero input channels, and no particular preferences
      // for the input channel.
      mDriver = new AudioCallbackDriver(
          this, nullptr, mSampleRate, aChannelCount, 0, PrimaryOutputDeviceID(),
          nullptr, AudioInputType::Unknown, Nothing());
    } else {
      mDriver = new SystemClockDriver(this, nullptr, mSampleRate);
    }
    nsCString streamName = GetDocumentTitle(mWindowID);
    LOG(LogLevel::Debug,
        ("{}: document title: {}", fmt::ptr(this), streamName.get()));
    mDriver->SetStreamName(streamName);
  } else {
    mDriver = new OfflineClockDriver(this, mSampleRate);
  }

  mLastMainThreadUpdate = TimeStamp::Now();

  RegisterWeakAsyncMemoryReporter(this);
}

#ifdef DEBUG
bool MediaTrackGraphImpl::InDriverIteration(const GraphDriver* aDriver) const {
  return aDriver->OnThread() ||
         (mGraphRunner && mGraphRunner->InDriverIteration(aDriver));
}
#endif

void MediaTrackGraphImpl::Destroy() {
  // First unregister from memory reporting.
  UnregisterWeakMemoryReporter(this);

  // Clear the self reference which will destroy this instance if all
  // associated GraphDrivers are destroyed.
  mSelfRef = nullptr;
}

// Internal method has a Window ID parameter so that TestAudioTrackGraph
// GTests can create a graph without a window.
/* static */
MediaTrackGraphImpl* MediaTrackGraphImpl::GetInstanceIfExists(
    uint64_t aWindowID, TrackRate aSampleRate,
    AudioDeviceID aPrimaryOutputDeviceID) {
  MOZ_ASSERT(NS_IsMainThread(), "Main thread only");
  MOZ_ASSERT(aSampleRate > 0);

  GraphHashSet::Ptr p =
      Graphs()->lookup({aWindowID, aSampleRate, aPrimaryOutputDeviceID});
  return p ? *p : nullptr;
}

// Public method has an nsPIDOMWindowInner* parameter to ensure that the
// window is a real inner Window, not a WindowProxy.
/* static */
MediaTrackGraph* MediaTrackGraph::GetInstanceIfExists(
    nsPIDOMWindowInner* aWindow, TrackRate aSampleRate,
    AudioDeviceID aPrimaryOutputDeviceID) {
  TrackRate sampleRate =
      aSampleRate ? aSampleRate
                  : CubebUtils::PreferredSampleRate(
                        aWindow->AsGlobal()->ShouldResistFingerprinting(
                            RFPTarget::AudioSampleRate));
  return MediaTrackGraphImpl::GetInstanceIfExists(
      aWindow->WindowID(), sampleRate, aPrimaryOutputDeviceID);
}

/* static */
MediaTrackGraphImpl* MediaTrackGraphImpl::GetInstance(
    GraphDriverType aGraphDriverRequested, uint64_t aWindowID,
    TrackRate aSampleRate, AudioDeviceID aPrimaryOutputDeviceID,
    nsISerialEventTarget* aMainThread) {
  MOZ_ASSERT(NS_IsMainThread(), "Main thread only");
  MOZ_ASSERT(aSampleRate > 0);
  MOZ_ASSERT(aGraphDriverRequested != OFFLINE_THREAD_DRIVER,
             "Use CreateNonRealtimeInstance() for offline graphs");

  MediaTrackGraphImpl* graph =
      GetInstanceIfExists(aWindowID, aSampleRate, aPrimaryOutputDeviceID);
  if (graph) {  // graph already exists
    return graph;
  }

  GraphRunType runType = DIRECT_DRIVER;
  if (Preferences::GetBool("media.audiograph.single_thread.enabled", true)) {
    runType = SINGLE_THREAD;
  }

  // In a real time graph, the number of output channels is determined by
  // the underlying number of channel of the default audio output device.
  uint32_t channelCount = CubebUtils::MaxNumberOfChannels();
  graph = new MediaTrackGraphImpl(aWindowID, aSampleRate,
                                  aPrimaryOutputDeviceID, aMainThread);
  graph->Init(aGraphDriverRequested, runType, channelCount);
  MOZ_ALWAYS_TRUE(Graphs()->putNew(
      {aWindowID, aSampleRate, aPrimaryOutputDeviceID}, graph));

  LOG(LogLevel::Debug, ("Starting up MediaTrackGraph {} for window 0x{:x}",
                        fmt::ptr(graph), aWindowID));

  return graph;
}

/* static */
MediaTrackGraph* MediaTrackGraph::GetInstance(
    GraphDriverType aGraphDriverRequested, nsPIDOMWindowInner* aWindow,
    TrackRate aSampleRate, AudioDeviceID aPrimaryOutputDeviceID) {
  TrackRate sampleRate =
      aSampleRate ? aSampleRate
                  : CubebUtils::PreferredSampleRate(
                        aWindow->AsGlobal()->ShouldResistFingerprinting(
                            RFPTarget::AudioSampleRate));
  return MediaTrackGraphImpl::GetInstance(
      aGraphDriverRequested, aWindow->WindowID(), sampleRate,
      aPrimaryOutputDeviceID, GetMainThreadSerialEventTarget());
}

MediaTrackGraph* MediaTrackGraphImpl::CreateNonRealtimeInstance(
    TrackRate aSampleRate) {
  MOZ_ASSERT(NS_IsMainThread(), "Main thread only");

  nsISerialEventTarget* mainThread = GetMainThreadSerialEventTarget();
  // Offline graphs have 0 output channel count: they write the output to a
  // buffer, not an audio output track.
  MediaTrackGraphImpl* graph = new MediaTrackGraphImpl(
      0, aSampleRate, DEFAULT_OUTPUT_DEVICE, mainThread);
  graph->Init(OFFLINE_THREAD_DRIVER, DIRECT_DRIVER, 0);

  LOG(LogLevel::Debug,
      ("Starting up Offline MediaTrackGraph {}", fmt::ptr(graph)));

  return graph;
}

MediaTrackGraph* MediaTrackGraph::CreateNonRealtimeInstance(
    TrackRate aSampleRate) {
  return MediaTrackGraphImpl::CreateNonRealtimeInstance(aSampleRate);
}

void MediaTrackGraph::ForceShutDown() {
  MOZ_ASSERT(NS_IsMainThread(), "Main thread only");

  MediaTrackGraphImpl* graph = static_cast<MediaTrackGraphImpl*>(this);

  graph->ForceShutDown();
}

NS_IMPL_ISUPPORTS(MediaTrackGraphImpl, nsIMemoryReporter, nsIObserver,
                  nsIThreadObserver, nsITimerCallback, nsINamed)

NS_IMETHODIMP
MediaTrackGraphImpl::CollectReports(nsIHandleReportCallback* aHandleReport,
                                    nsISupports* aData, bool aAnonymize) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mMainThreadTrackCount == 0) {
    // No tracks to report.
    FinishCollectReports(aHandleReport, aData, nsTArray<AudioNodeSizes>());
    return NS_OK;
  }

  class Message final : public ControlMessage {
   public:
    Message(MediaTrackGraphImpl* aGraph, nsIHandleReportCallback* aHandleReport,
            nsISupports* aHandlerData)
        : ControlMessage(nullptr),
          mGraph(aGraph),
          mHandleReport(aHandleReport),
          mHandlerData(aHandlerData) {}
    void Run() override {
      TRACE("MTG::CollectSizesForMemoryReport ControlMessage");
      mGraph->CollectSizesForMemoryReport(mHandleReport.forget(),
                                          mHandlerData.forget());
    }
    void RunDuringShutdown() override {
      // Run this message during shutdown too, so that endReports is called.
      Run();
    }
    MediaTrackGraphImpl* mGraph;
    // nsMemoryReporterManager keeps the callback and data alive only if it
    // does not time out.
    nsCOMPtr<nsIHandleReportCallback> mHandleReport;
    nsCOMPtr<nsISupports> mHandlerData;
  };

  AppendMessage(MakeUnique<Message>(this, aHandleReport, aData));

  return NS_OK;
}

void MediaTrackGraphImpl::CollectSizesForMemoryReport(
    already_AddRefed<nsIHandleReportCallback> aHandleReport,
    already_AddRefed<nsISupports> aHandlerData) {
  class FinishCollectRunnable final : public Runnable {
   public:
    explicit FinishCollectRunnable(
        already_AddRefed<nsIHandleReportCallback> aHandleReport,
        already_AddRefed<nsISupports> aHandlerData)
        : mozilla::Runnable("FinishCollectRunnable"),
          mHandleReport(aHandleReport),
          mHandlerData(aHandlerData) {}

    NS_IMETHOD Run() override {
      TRACE("MTG::FinishCollectReports ControlMessage");
      MediaTrackGraphImpl::FinishCollectReports(mHandleReport, mHandlerData,
                                                std::move(mAudioTrackSizes));
      return NS_OK;
    }

    nsTArray<AudioNodeSizes> mAudioTrackSizes;

   private:
    ~FinishCollectRunnable() = default;

    // Avoiding nsCOMPtr because NSCAP_ASSERT_NO_QUERY_NEEDED in its
    // constructor modifies the ref-count, which cannot be done off main
    // thread.
    RefPtr<nsIHandleReportCallback> mHandleReport;
    RefPtr<nsISupports> mHandlerData;
  };

  RefPtr<FinishCollectRunnable> runnable = new FinishCollectRunnable(
      std::move(aHandleReport), std::move(aHandlerData));

  auto audioTrackSizes = &runnable->mAudioTrackSizes;

  for (MediaTrack* t : AllTracks()) {
    AudioNodeTrack* track = t->AsAudioNodeTrack();
    if (track) {
      AudioNodeSizes* usage = audioTrackSizes->AppendElement();
      track->SizeOfAudioNodesIncludingThis(MallocSizeOf, *usage);
    }
  }

  mMainThread->Dispatch(runnable.forget());
}

void MediaTrackGraphImpl::FinishCollectReports(
    nsIHandleReportCallback* aHandleReport, nsISupports* aData,
    const nsTArray<AudioNodeSizes>& aAudioTrackSizes) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIMemoryReporterManager> manager =
      do_GetService("@mozilla.org/memory-reporter-manager;1");

  if (!manager) return;

#define REPORT(_path, _amount, _desc)                                    \
  aHandleReport->Callback(""_ns, _path, KIND_HEAP, UNITS_BYTES, _amount, \
                          nsLiteralCString(_desc), aData);

  for (size_t i = 0; i < aAudioTrackSizes.Length(); i++) {
    const AudioNodeSizes& usage = aAudioTrackSizes[i];
    const char* const nodeType =
        usage.mNodeType ? usage.mNodeType : "<unknown>";

    nsPrintfCString enginePath("explicit/webaudio/audio-node/%s/engine-objects",
                               nodeType);
    REPORT(enginePath, usage.mEngine,
           "Memory used by AudioNode engine objects (Web Audio).");

    nsPrintfCString trackPath("explicit/webaudio/audio-node/%s/track-objects",
                              nodeType);
    REPORT(trackPath, usage.mTrack,
           "Memory used by AudioNode track objects (Web Audio).");
  }

  size_t hrtfLoaders = WebCore::HRTFDatabaseLoader::sizeOfLoaders(MallocSizeOf);
  if (hrtfLoaders) {
    REPORT(nsLiteralCString(
               "explicit/webaudio/audio-node/PannerNode/hrtf-databases"),
           hrtfLoaders, "Memory used by PannerNode databases (Web Audio).");
  }

#undef REPORT

  manager->EndReport();
}

SourceMediaTrack* MediaTrackGraph::CreateSourceTrack(MediaSegment::Type aType) {
  SourceMediaTrack* track = new SourceMediaTrack(aType, GraphRate());
  AddTrack(track);
  return track;
}

ProcessedMediaTrack* MediaTrackGraph::CreateForwardedInputTrack(
    MediaSegment::Type aType) {
  ForwardedInputTrack* track = new ForwardedInputTrack(GraphRate(), aType);
  AddTrack(track);
  return track;
}

AudioCaptureTrack* MediaTrackGraph::CreateAudioCaptureTrack() {
  AudioCaptureTrack* track = new AudioCaptureTrack(GraphRate());
  AddTrack(track);
  return track;
}

CrossGraphTransmitter* MediaTrackGraph::CreateCrossGraphTransmitter(
    CrossGraphReceiver* aReceiver) {
  CrossGraphTransmitter* track =
      new CrossGraphTransmitter(GraphRate(), aReceiver);
  AddTrack(track);
  return track;
}

CrossGraphReceiver* MediaTrackGraph::CreateCrossGraphReceiver(
    TrackRate aTransmitterRate) {
  CrossGraphReceiver* track =
      new CrossGraphReceiver(GraphRate(), aTransmitterRate);
  AddTrack(track);
  return track;
}

void MediaTrackGraph::AddTrack(MediaTrack* aTrack) {
  MediaTrackGraphImpl* graph = static_cast<MediaTrackGraphImpl*>(this);
  MOZ_ASSERT(NS_IsMainThread());
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  if (graph->mRealtime) {
    GraphHashSet::Ptr p = Graphs()->lookup(*graph);
    MOZ_DIAGNOSTIC_ASSERT(p, "Graph must not be shutting down");
  }
#endif
  if (graph->mMainThreadTrackCount == 0 && graph->mRealtime) {
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService) {
      observerService->AddObserver(graph, "document-title-changed", false);
    }
  }

  NS_ADDREF(aTrack);
  aTrack->SetGraphImpl(graph);
  ++graph->mMainThreadTrackCount;
  graph->AppendMessage(MakeUnique<CreateMessage>(aTrack));
}

void MediaTrackGraphImpl::RemoveTrack(MediaTrack* aTrack) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(mMainThreadTrackCount > 0);

  mAudioOutputParams.RemoveElementsBy(
      [&](const TrackKeyDeviceAndVolume& aElement) {
        if (aElement.mTrack != aTrack) {
          return false;
        };
        DecrementOutputDeviceRefCnt(aElement.mDeviceID);
        return true;
      });

  if (--mMainThreadTrackCount == 0) {
    LOG(LogLevel::Info, ("MediaTrackGraph {}, last track {} removed from "
                         "main thread. Graph will shut down.",
                         fmt::ptr(this), fmt::ptr(aTrack)));
    if (mRealtime) {
      // Find the graph in the hash table and remove it.
      GraphHashSet* graphs = Graphs();
      GraphHashSet::Ptr p = graphs->lookup(*this);
      MOZ_ASSERT(*p == this);
      graphs->remove(p);

      nsCOMPtr<nsIObserverService> observerService =
          mozilla::services::GetObserverService();
      if (observerService) {
        observerService->RemoveObserver(this, "document-title-changed");
      }
    }
    // The graph thread will shut itself down soon, but won't be able to do
    // that if JS continues to run.
    InterruptJS();
  }
}

auto MediaTrackGraphImpl::NotifyWhenDeviceStarted(AudioDeviceID aDeviceID)
    -> RefPtr<GraphStartedPromise> {
  MOZ_ASSERT(NS_IsMainThread());

  size_t index = mOutputDeviceRefCnts.IndexOf(aDeviceID);
  if (index == decltype(mOutputDeviceRefCnts)::NoIndex) {
    return GraphStartedPromise::CreateAndReject(NS_ERROR_INVALID_ARG, __func__);
  }

  MozPromiseHolder<GraphStartedPromise> h;
  RefPtr<GraphStartedPromise> p = h.Ensure(__func__);

  if (CrossGraphReceiver* receiver = mOutputDeviceRefCnts[index].mReceiver) {
    receiver->GraphImpl()->NotifyWhenPrimaryDeviceStarted(std::move(h));
    return p;
  }

  // aSink corresponds to the primary audio output device of this graph.
  NotifyWhenPrimaryDeviceStarted(std::move(h));
  return p;
}

void MediaTrackGraphImpl::NotifyWhenPrimaryDeviceStarted(
    MozPromiseHolder<GraphStartedPromise>&& aHolder) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mOutputDeviceRefCnts[0].mRefCnt == 0) {
    // There are no track outputs that require the device, so the creator of
    // this promise no longer needs to know when the graph is running.  Don't
    // keep the graph alive with another message.
    aHolder.Reject(NS_ERROR_NOT_AVAILABLE, __func__);
    return;
  }

  QueueControlOrShutdownMessage(
      [self = RefPtr{this}, this,
       holder = std::move(aHolder)](IsInShutdown aInShutdown) mutable {
        if (aInShutdown == IsInShutdown::Yes) {
          holder.Reject(NS_ERROR_ILLEGAL_DURING_SHUTDOWN, __func__);
          return;
        }

        TRACE("MTG::NotifyWhenPrimaryDeviceStarted ControlMessage");
        // This runs on the graph thread, so when this runs, and the current
        // driver is an AudioCallbackDriver, we know the audio hardware is
        // started. If not, we are going to switch soon, keep reposting this
        // ControlMessage.
        if (CurrentDriver()->AsAudioCallbackDriver() &&
            CurrentDriver()->ThreadRunning() &&
            !CurrentDriver()->AsAudioCallbackDriver()->OnFallback()) {
          // Avoid Resolve's locking on the graph thread by doing it on main.
          Dispatch(NS_NewRunnableFunction(
              "MediaTrackGraphImpl::NotifyWhenPrimaryDeviceStarted::Resolver",
              [holder = std::move(holder)]() mutable {
                holder.Resolve(true, __func__);
              }));
        } else {
          DispatchToMainThreadStableState(
              NewRunnableMethod<
                  StoreCopyPassByRRef<MozPromiseHolder<GraphStartedPromise>>>(
                  "MediaTrackGraphImpl::NotifyWhenPrimaryDeviceStarted", this,
                  &MediaTrackGraphImpl::NotifyWhenPrimaryDeviceStarted,
                  std::move(holder)));
        }
      });
}

class AudioContextOperationControlMessage : public ControlMessage {
  using AudioContextOperationPromise =
      MediaTrackGraph::AudioContextOperationPromise;

 public:
  AudioContextOperationControlMessage(
      MediaTrack* aDestinationTrack, nsTArray<RefPtr<MediaTrack>> aTracks,
      AudioContextOperation aOperation,
      MozPromiseHolder<AudioContextOperationPromise>&& aHolder)
      : ControlMessage(aDestinationTrack),
        mTracks(std::move(aTracks)),
        mAudioContextOperation(aOperation),
        mHolder(std::move(aHolder)) {}
  void Run() override {
    TRACE_COMMENT("MTG::ApplyAudioContextOperationImpl ControlMessage",
                  kAudioContextOptionsStrings[static_cast<uint8_t>(
                      mAudioContextOperation)]);
    mTrack->GraphImpl()->ApplyAudioContextOperationImpl(this);
  }
  void RunDuringShutdown() override {
    MOZ_ASSERT(mAudioContextOperation == AudioContextOperation::Close,
               "We should be reviving the graph?");
    mHolder.Reject(false, __func__);
  }

  nsTArray<RefPtr<MediaTrack>> mTracks;
  AudioContextOperation mAudioContextOperation;
  MozPromiseHolder<AudioContextOperationPromise> mHolder;
};

void MediaTrackGraphImpl::ApplyAudioContextOperationImpl(
    AudioContextOperationControlMessage* aMessage) {
  MOZ_ASSERT(OnGraphThread());
  // Initialize state to zero. This silences a GCC warning about uninitialized
  // values, because although the switch below initializes state for all valid
  // enum values, the actual value could be any integer that fits in the enum.
  AudioContextState state{0};
  switch (aMessage->mAudioContextOperation) {
    // Suspend and Close operations may be performed immediately because no
    // specific kind of GraphDriver is required.  CheckDriver() will schedule
    // a change to a SystemCallbackDriver if all tracks are suspended.
    case AudioContextOperation::Suspend:
      state = AudioContextState::Suspended;
      break;
    case AudioContextOperation::Close:
      state = AudioContextState::Closed;
      break;
    case AudioContextOperation::Resume:
      // Resume operations require an AudioCallbackDriver.  CheckDriver() will
      // schedule an AudioCallbackDriver if necessary and process pending
      // operations if and when an AudioCallbackDriver is running.
      mPendingResumeOperations.EmplaceBack(aMessage);
      return;
  }
  // First resolve any pending Resume promises for the same AudioContext so as
  // to resolve its associated promises in the same order as they were
  // created.  These Resume operations are considered complete and immediately
  // canceled by the Suspend or Close.
  MediaTrack* destinationTrack = aMessage->GetTrack();
  bool shrinking = false;
  auto moveDest = mPendingResumeOperations.begin();
  for (PendingResumeOperation& op : mPendingResumeOperations) {
    if (op.DestinationTrack() == destinationTrack) {
      op.Apply(this);
      shrinking = true;
      continue;
    }
    if (shrinking) {  // Fill-in gaps in the array.
      *moveDest = std::move(op);
    }
    ++moveDest;
  }
  mPendingResumeOperations.TruncateLength(moveDest -
                                          mPendingResumeOperations.begin());

  for (MediaTrack* track : aMessage->mTracks) {
    track->IncrementSuspendCount();
  }
  // Resolve after main thread state is up to date with completed processing.
  DispatchToMainThreadStableState(NS_NewRunnableFunction(
      "MediaTrackGraphImpl::ApplyAudioContextOperationImpl",
      [holder = std::move(aMessage->mHolder), state]() mutable {
        holder.Resolve(state, __func__);
      }));
}

MediaTrackGraphImpl::PendingResumeOperation::PendingResumeOperation(
    AudioContextOperationControlMessage* aMessage)
    : mDestinationTrack(aMessage->GetTrack()),
      mTracks(std::move(aMessage->mTracks)),
      mHolder(std::move(aMessage->mHolder)) {
  MOZ_ASSERT(aMessage->mAudioContextOperation == AudioContextOperation::Resume);
}

void MediaTrackGraphImpl::PendingResumeOperation::Apply(
    MediaTrackGraphImpl* aGraph) {
  MOZ_ASSERT(aGraph->OnGraphThread());
  for (MediaTrack* track : mTracks) {
    track->DecrementSuspendCount();
  }
  // The graph is provided through the parameter so that it is available even
  // when the track is destroyed.
  aGraph->DispatchToMainThreadStableState(NS_NewRunnableFunction(
      "PendingResumeOperation::Apply", [holder = std::move(mHolder)]() mutable {
        holder.Resolve(AudioContextState::Running, __func__);
      }));
}

void MediaTrackGraphImpl::PendingResumeOperation::Abort() {
  // The graph is shutting down before the operation completed.
  MOZ_ASSERT(!mDestinationTrack->GraphImpl() ||
             mDestinationTrack->GraphImpl()->LifecycleStateRef() ==
                 MediaTrackGraphImpl::LIFECYCLE_WAITING_FOR_THREAD_SHUTDOWN);
  mHolder.Reject(false, __func__);
}

auto MediaTrackGraph::ApplyAudioContextOperation(
    MediaTrack* aDestinationTrack, nsTArray<RefPtr<MediaTrack>> aTracks,
    AudioContextOperation aOperation) -> RefPtr<AudioContextOperationPromise> {
  MozPromiseHolder<AudioContextOperationPromise> holder;
  RefPtr<AudioContextOperationPromise> p = holder.Ensure(__func__);
  MediaTrackGraphImpl* graphImpl = static_cast<MediaTrackGraphImpl*>(this);
  graphImpl->AppendMessage(MakeUnique<AudioContextOperationControlMessage>(
      aDestinationTrack, std::move(aTracks), aOperation, std::move(holder)));
  return p;
}

uint32_t MediaTrackGraphImpl::PrimaryOutputChannelCount() const {
  MOZ_ASSERT(!mOutputDevices[0].mReceiver);
  return AudioOutputChannelCount(mOutputDevices[0]);
}

uint32_t MediaTrackGraphImpl::AudioOutputChannelCount(
    const OutputDeviceEntry& aDevice) const {
  MOZ_ASSERT(OnGraphThread());
  // The audio output channel count for a graph is the maximum of the output
  // channel count of all the tracks with outputs to this device, or the max
  // audio output channel count the machine can do, whichever is smaller.
  uint32_t channelCount = 0;
  for (const auto& output : aDevice.mTrackOutputs) {
    channelCount = std::max(channelCount, output.mTrack->NumberOfChannels());
  }
  channelCount = std::min(channelCount, mMaxOutputChannelCount);
  if (channelCount) {
    return channelCount;
  } else {
    // null aDevice.mReceiver indicates the primary graph output device.
    if (!aDevice.mReceiver && CurrentDriver()->AsAudioCallbackDriver()) {
      return CurrentDriver()->AsAudioCallbackDriver()->OutputChannelCount();
    }
    return 2;
  }
}

double MediaTrackGraph::AudioOutputLatency() {
  return static_cast<MediaTrackGraphImpl*>(this)->AudioOutputLatency();
}

double MediaTrackGraphImpl::AudioOutputLatency() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mAudioOutputLatency != 0.0) {
    return mAudioOutputLatency;
  }
  MonitorAutoLock lock(mMonitor);
  if (CurrentDriver()->AsAudioCallbackDriver()) {
    mAudioOutputLatency = CurrentDriver()
                              ->AsAudioCallbackDriver()
                              ->AudioOutputLatency()
                              .ToSeconds();
  } else {
    // Failure mode: return 0.0 if running on a normal thread.
    mAudioOutputLatency = 0.0;
  }

  return mAudioOutputLatency;
}

bool MediaTrackGraph::OutputForAECMightDrift() {
  return static_cast<MediaTrackGraphImpl*>(this)->OutputForAECMightDrift();
}
bool MediaTrackGraph::OutputForAECIsPrimary() {
  return static_cast<MediaTrackGraphImpl*>(this)->OutputForAECIsPrimary();
}
bool MediaTrackGraph::IsNonRealtime() const {
  return !static_cast<const MediaTrackGraphImpl*>(this)->mRealtime;
}

void MediaTrackGraph::StartNonRealtimeProcessing(uint32_t aTicksToProcess) {
  MOZ_ASSERT(NS_IsMainThread(), "main thread only");

  MediaTrackGraphImpl* graph = static_cast<MediaTrackGraphImpl*>(this);
  NS_ASSERTION(!graph->mRealtime, "non-realtime only");

  graph->QueueControlMessageWithNoShutdown([graph = RefPtr{graph},
                                            aTicksToProcess]() {
    TRACE("MTG::StartNonRealtimeProcessing ControlMessage");
    MOZ_ASSERT(graph->mStateComputedTime == 0);
    MOZ_ASSERT(graph->mEndTime == 0,
               "StartNonRealtimeProcessing should be called only once");
    graph->mEndTime = aTicksToProcess;
    OfflineClockDriver* driver = graph->CurrentDriver()->AsOfflineClockDriver();
    MOZ_ASSERT(driver);
    driver->SetTickCountToRender(aTicksToProcess);
  });
}

void MediaTrackGraphImpl::InterruptJS() {
  MonitorAutoLock lock(mMonitor);
  mInterruptJSCalled = true;
  if (mJSContext) {
    JS_RequestInterruptCallback(mJSContext);
  }
}

static bool InterruptCallback(JSContext* aCx) {
  // Interrupt future calls also.
  JS_RequestInterruptCallback(aCx);
  // Stop execution.
  return false;
}

void MediaTrackGraph::NotifyJSContext(JSContext* aCx) {
  MOZ_ASSERT(OnGraphThread());
  MOZ_ASSERT(aCx);

  auto* impl = static_cast<MediaTrackGraphImpl*>(this);
  MonitorAutoLock lock(impl->mMonitor);
  if (impl->mJSContext) {
    MOZ_ASSERT(impl->mJSContext == aCx);
    return;
  }
  JS_AddInterruptCallback(aCx, InterruptCallback);
  impl->mJSContext = aCx;
  if (impl->mInterruptJSCalled) {
    JS_RequestInterruptCallback(aCx);
  }
}

void ProcessedMediaTrack::AddInput(MediaInputPort* aPort) {
  MediaTrack* t = aPort->GetSource();
  if (!t->IsSuspended()) {
    mInputs.AppendElement(aPort);
  } else {
    mSuspendedInputs.AppendElement(aPort);
  }
  GraphImpl()->SetTrackOrderDirty();
}

void ProcessedMediaTrack::InputSuspended(MediaInputPort* aPort) {
  GraphImpl()->AssertOnGraphThreadOrNotRunning();
  mInputs.RemoveElement(aPort);
  mSuspendedInputs.AppendElement(aPort);
  GraphImpl()->SetTrackOrderDirty();
}

void ProcessedMediaTrack::InputResumed(MediaInputPort* aPort) {
  GraphImpl()->AssertOnGraphThreadOrNotRunning();
  mSuspendedInputs.RemoveElement(aPort);
  mInputs.AppendElement(aPort);
  GraphImpl()->SetTrackOrderDirty();
}

void MediaTrackGraphImpl::SwitchAtNextIteration(GraphDriver* aNextDriver) {
  MOZ_ASSERT(OnGraphThread());
  LOG(LogLevel::Debug, ("{}: Switching to new driver: {}", fmt::ptr(this),
                        fmt::ptr(aNextDriver)));
  if (GraphDriver* nextDriver = NextDriver()) {
    if (nextDriver != CurrentDriver()) {
      LOG(LogLevel::Debug, ("{}: Discarding previous next driver: {}",
                            fmt::ptr(this), fmt::ptr(nextDriver)));
    }
  }
  mNextDriver = aNextDriver;
}

void MediaTrackGraph::RegisterCaptureTrackForWindow(
    uint64_t aWindowId, ProcessedMediaTrack* aCaptureTrack) {
  MOZ_ASSERT(NS_IsMainThread());
  MediaTrackGraphImpl* graphImpl = static_cast<MediaTrackGraphImpl*>(this);
  graphImpl->RegisterCaptureTrackForWindow(aWindowId, aCaptureTrack);
}

void MediaTrackGraphImpl::RegisterCaptureTrackForWindow(
    uint64_t aWindowId, ProcessedMediaTrack* aCaptureTrack) {
  MOZ_ASSERT(NS_IsMainThread());
  WindowAndTrack winAndTrack;
  winAndTrack.mWindowId = aWindowId;
  winAndTrack.mCaptureTrackSink = aCaptureTrack;
  mWindowCaptureTracks.AppendElement(winAndTrack);
}

void MediaTrackGraph::UnregisterCaptureTrackForWindow(uint64_t aWindowId) {
  MOZ_ASSERT(NS_IsMainThread());
  MediaTrackGraphImpl* graphImpl = static_cast<MediaTrackGraphImpl*>(this);
  graphImpl->UnregisterCaptureTrackForWindow(aWindowId);
}

void MediaTrackGraphImpl::UnregisterCaptureTrackForWindow(uint64_t aWindowId) {
  MOZ_ASSERT(NS_IsMainThread());
  mWindowCaptureTracks.RemoveElementsBy(
      [aWindowId](const auto& track) { return track.mWindowId == aWindowId; });
}

already_AddRefed<MediaInputPort> MediaTrackGraph::ConnectToCaptureTrack(
    uint64_t aWindowId, MediaTrack* aMediaTrack) {
  return aMediaTrack->GraphImpl()->ConnectToCaptureTrack(aWindowId,
                                                         aMediaTrack);
}

already_AddRefed<MediaInputPort> MediaTrackGraphImpl::ConnectToCaptureTrack(
    uint64_t aWindowId, MediaTrack* aMediaTrack) {
  MOZ_ASSERT(NS_IsMainThread());
  for (uint32_t i = 0; i < mWindowCaptureTracks.Length(); i++) {
    if (mWindowCaptureTracks[i].mWindowId == aWindowId) {
      ProcessedMediaTrack* sink = mWindowCaptureTracks[i].mCaptureTrackSink;
      return sink->AllocateInputPort(aMediaTrack);
    }
  }
  return nullptr;
}

void MediaTrackGraph::DispatchToMainThreadStableState(
    already_AddRefed<nsIRunnable> aRunnable) {
  AssertOnGraphThreadOrNotRunning();
  static_cast<MediaTrackGraphImpl*>(this)
      ->mPendingUpdateRunnables.AppendElement(std::move(aRunnable));
}

Watchable<mozilla::GraphTime>& MediaTrackGraphImpl::CurrentTime() {
  MOZ_ASSERT(NS_IsMainThread());
  return mMainThreadGraphTime;
}

GraphTime MediaTrackGraph::ProcessedTime() const {
  AssertOnGraphThreadOrNotRunning();
  return static_cast<const MediaTrackGraphImpl*>(this)->mProcessedTime;
}

void* MediaTrackGraph::CurrentDriver() const {
  AssertOnGraphThreadOrNotRunning();
  return static_cast<const MediaTrackGraphImpl*>(this)->mDriver;
}

uint32_t MediaTrackGraphImpl::AudioInputChannelCount(
    CubebUtils::AudioDeviceID aID) {
  MOZ_ASSERT(OnGraphThreadOrNotRunning());
  DeviceInputTrack* t =
      mDeviceInputTrackManagerGraphThread.GetDeviceInputTrack(aID);
  return t ? t->MaxRequestedInputChannels() : 0;
}

AudioInputType MediaTrackGraphImpl::AudioInputDevicePreference(
    CubebUtils::AudioDeviceID aID) {
  MOZ_ASSERT(OnGraphThreadOrNotRunning());
  DeviceInputTrack* t =
      mDeviceInputTrackManagerGraphThread.GetDeviceInputTrack(aID);
  return t && t->HasVoiceInput() ? AudioInputType::Voice
                                 : AudioInputType::Unknown;
}

void MediaTrackGraphImpl::SetNewNativeInput() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mDeviceInputTrackManagerMainThread.GetNativeInputTrack());

  LOG(LogLevel::Debug, ("{} SetNewNativeInput", fmt::ptr(this)));

  NonNativeInputTrack* track =
      mDeviceInputTrackManagerMainThread.GetFirstNonNativeInputTrack();
  if (!track) {
    LOG(LogLevel::Debug,
        ("{} No other devices opened. Do nothing", fmt::ptr(this)));
    return;
  }

  const CubebUtils::AudioDeviceID deviceId = track->mDeviceId;
  const PrincipalHandle principal = track->mPrincipalHandle;

  LOG(LogLevel::Debug, ("{} Select device {} as the new native input device",
                        fmt::ptr(this), fmt::ptr(deviceId)));

  struct TrackListener {
    DeviceInputConsumerTrack* track;
    // Keep its reference so it won't be dropped when after
    // DisconnectDeviceInput().
    RefPtr<AudioDataListener> listener;
  };
  nsTArray<TrackListener> pairs;

  for (const auto& t : track->GetConsumerTracks()) {
    pairs.AppendElement(
        TrackListener{t.get(), t->GetAudioDataListener().get()});
  }

  for (TrackListener& pair : pairs) {
    pair.track->DisconnectDeviceInput();
  }

  for (TrackListener& pair : pairs) {
    pair.track->ConnectDeviceInput(deviceId, pair.listener.get(), principal);
    LOG(LogLevel::Debug,
        ("{}: Reinitialize AudioProcessingTrack {} for device {}",
         fmt::ptr(this), fmt::ptr(pair.track), fmt::ptr(deviceId)));
  }

  LOG(LogLevel::Debug, ("{} Native input device is set to device {} now",
                        fmt::ptr(this), fmt::ptr(deviceId)));

  MOZ_ASSERT(mDeviceInputTrackManagerMainThread.GetNativeInputTrack());
}

void MediaTrackGraphImpl::UpdateEnumeratorDefaultDeviceTracking() {
  MOZ_ASSERT(NS_IsMainThread());
  auto onExit = MakeScopeExit([&] { UpdateDefaultDevice(); });

  if (!mDeviceInputTrackManagerMainThread.GetNativeInputTrack()) {
    mEnumeratorMainThread = nullptr;
    mOutputDevicesChangedListener.DisconnectIfExists();
    LOG(LogLevel::Debug,
        ("{} No longer tracking system default output device", fmt::ptr(this)));
    return;
  }

  if (mEnumeratorMainThread) {
    onExit.release();
    return;
  }

  mEnumeratorMainThread = CubebDeviceEnumerator::GetInstance();
  mOutputDevicesChangedListener =
      mEnumeratorMainThread->OnAudioOutputDeviceListChange().Connect(
          GetCurrentSerialEventTarget(), this,
          &MediaTrackGraphImpl::UpdateDefaultDevice);
  LOG(LogLevel::Debug,
      ("{} Now tracking system default output device", fmt::ptr(this)));
}

void MediaTrackGraphImpl::UpdateDefaultDevice() {
  MOZ_ASSERT(NS_IsMainThread());
  CubebUtils::AudioDeviceID id = nullptr;
  auto onExit = MakeScopeExit([&] {
    mDefaultOutputDeviceID.store(id, std::memory_order_relaxed);
    LOG(LogLevel::Debug,
        ("{} Tracked system default output device ID is now {}", fmt::ptr(this),
         fmt::ptr(id)));
  });

  if (!mEnumeratorMainThread) {
    return;
  }

  auto dev =
      mEnumeratorMainThread->DefaultDevice(CubebDeviceEnumerator::Side::OUTPUT);
  if (!dev) {
    return;
  }

  id = dev->DeviceID();
}

// nsIThreadObserver methods

NS_IMETHODIMP
MediaTrackGraphImpl::OnDispatchedEvent() {
  MonitorAutoLock lock(mMonitor);
  GraphDriver* driver = CurrentDriver();
  if (!driver) {
    // The ipc::BackgroundChild started by `UniqueMessagePortId()` destruction
    // can queue messages on the thread used for the graph after graph
    // shutdown.  See bug 1955768.
    //
    // Other threads may have already taken a reference to this graph as the
    // observer, so clearing the thread's observer (on the graph thread) would
    // not be effective to prevent this callback from being invoked.
    // https://searchfox.org/mozilla-central/rev/8b52ebe3cddf0e63bb42e8b51618bc3ab8dcb12d/xpcom/threads/ThreadEventQueue.cpp#113,135,139
    return NS_OK;
  }
  driver->EnsureNextIteration();
  return NS_OK;
}

NS_IMETHODIMP
MediaTrackGraphImpl::OnProcessNextEvent(nsIThreadInternal*, bool) {
  return NS_OK;
}

NS_IMETHODIMP
MediaTrackGraphImpl::AfterProcessNextEvent(nsIThreadInternal*, bool) {
  return NS_OK;
}
}  // namespace mozilla

#undef LOG
