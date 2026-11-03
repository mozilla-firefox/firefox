/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#if !defined(AudioStream_h_)
#  define AudioStream_h_

#  include "AudioSampleFormat.h"
#  include "CubebUtils.h"
#  include "MediaInfo.h"
#  include "MediaSink.h"
#  include "WavDumper.h"
#  include "mozilla/Atomics.h"
#  include "mozilla/Monitor.h"
#  include "mozilla/MozPromise.h"
#  include "mozilla/ProfilerUtils.h"
#  include "mozilla/RefPtr.h"
#  include "mozilla/SPSCQueue.h"
#  include "mozilla/TimeStamp.h"
#  include "mozilla/UniquePtr.h"
#  include "nsCOMPtr.h"
#  include "nsThreadUtils.h"

namespace mozilla {

struct CubebDestroyPolicy {
  void operator()(cubeb_stream* aStream) const {
    cubeb_stream_destroy(aStream);
  }
};

class AudioStream;
class FrameHistory;
class AudioConfig;
class RLBoxSoundTouch;

// A struct that contains the number of frames serviced or underrun by a
// callback, alongside the sample-rate for this callback (in case of playback
// rate change, it can be variable).
struct CallbackInfo {
  CallbackInfo() = default;
  CallbackInfo(uint32_t aServiced, uint32_t aUnderrun, uint32_t aOutputRate)
      : mServiced(aServiced), mUnderrun(aUnderrun), mOutputRate(aOutputRate) {}

  // Every frame this callback handed to the backend, serviced and underrun.
  uint32_t TotalFrames() const { return mServiced + mUnderrun; }

  uint32_t mServiced = 0;
  uint32_t mUnderrun = 0;
  uint32_t mOutputRate = 0;
};

class AudioClock {
 public:
  // Whether a rebase should carry the audio the backend has been handed but has
  // not played yet. It must not when that audio will never be played.
  enum class CarryUnplayed { No, Yes };

  explicit AudioClock(uint32_t aInRate);
  // Out-of-line so FrameHistory only needs to be a complete type in
  // AudioStream.cpp; this lets other translation units construct and destroy an
  // AudioClock without FrameHistory's definition, e.g. gtest.
  ~AudioClock();

  // Update the number of samples that has been written in the audio backend.
  // Called on the audio thread only.
  void UpdateFrameHistory(uint32_t aServiced, uint32_t aUnderrun,
                          bool aAudioThreadChanged);

  // Rebase the clock for a stream reused across a seek so the reported position
  // resumes from zero. |aBaseOffset| is the current raw engine frame count.
  // With |aCarry| set, audio handed over but not yet played is carried across
  // as worth no media time; pass No when that audio will never be played.
  //
  // Safe to call with the audio callback still running. On macOS it must run on
  // the same thread as GetPosition; elsewhere mMutex makes the thread
  // immaterial. Must run before any post-seek audio reaches the data source, or
  // that audio loses its media time permanently.
  void Rebase(int64_t aBaseOffset, CarryUnplayed aCarry);

  /**
   * @param aFrames The playback position in frames of the audio engine.
   * @return The playback position in frames of the stream,
   *         adjusted by playback rate changes and underrun frames.
   */
  int64_t GetPositionInFrames(int64_t aFrames);

  /**
   * @param frames The playback position in frames of the audio engine.
   * @return The playback position in microseconds of the stream,
   *         adjusted by playback rate changes and underrun frames.
   */
  int64_t GetPosition(int64_t frames);

  // Set the playback rate.
  // Called on the audio thread only.
  void SetPlaybackRate(double aPlaybackRate);
  // Get the current playback rate.
  // Called on the audio thread only.
  double GetPlaybackRate() const;
  // Set if we are preserving the pitch.
  // Called on the audio thread only.
  void SetPreservesPitch(bool aPreservesPitch);
  // Get the current pitch preservation state.
  // Called on the audio thread only.
  bool GetPreservesPitch() const;

  // Called on either thread.
  uint32_t GetInputRate() const { return mInRate; }
  uint32_t GetOutputRate() const { return mOutRate; }

 private:
#  ifdef XP_MACOSX
  // Apply the callback info the audio thread has queued to the frame history.
  // Claims the single-consumer role on the queue, so owner thread only, which
  // Dequeue asserts. A callback that found the queue full keeps its frames
  // until a later call collects them, so the history can trail the write
  // cursor by an unbounded amount while nothing drains the queue, and an item
  // can arrive after a rebase has already accounted for its frames.
  void ApplyQueuedCallbackInfo();
#  endif

  // Three positions on one axis, the count of frames handed to the backend.
  // Comparing them is how a callback item that arrives after a rebase is
  // recognised as describing frames the anchor has already moved past.
  struct FrameHandoff {
    // The write cursor: everything handed over, serviced and underrun alike.
    // Counted here rather than summed from the callback items because a
    // callback that finds the queue full keeps its item on the audio thread,
    // so mSeen can trail this by an unbounded amount. Bumped on the audio
    // thread, read on the owner thread.
    Atomic<uint64_t> mTotal{0};
#  ifdef XP_MACOSX
    // How far the reader has got, counting every item it takes off the queue,
    // including the ones it drops: the count is only comparable to mTotal if
    // nothing is missed. Owner thread only.
    uint64_t mSeen = 0;

    // Where mTotal stood at the last rebase. An item landing at or below it
    // was handed over earlier, so appending it would put frames the anchor has
    // passed in front of it. Owner thread only.
    uint64_t mRebasedThrough = 0;
#  endif
  };
  FrameHandoff mHandoff;

  // Output rate in Hz (characteristic of the playback rate). Written on the
  // audio thread, read on either thread.
  Atomic<uint32_t> mOutRate;
  // Input rate in Hz (characteristic of the media being played).
  const uint32_t mInRate;
  // True if the we are timestretching, false if we are resampling. Accessed on
  // the audio thread only.
  bool mPreservesPitch;
  // The history of frames sent to the audio engine in each DataCallback.
  // Only accessed from non-audio threads on macOS, accessed on both threads and
  // protected by the AudioStream monitor on other platforms.
  const UniquePtr<FrameHistory> mFrameHistory
#  ifndef XP_MACOSX
      MOZ_GUARDED_BY(mMutex)
#  endif
          ;
#  ifdef XP_MACOSX
  // Enqueued on the audio thread, dequeued from the other thread. The maximum
  // size of this queue has been chosen empirically.
  SPSCQueue<CallbackInfo> mCallbackInfoQueue{100};
  // If it isn't possible to send the callback info to the non-audio thread,
  // store them here until it's possible to send them. This is an unlikely
  // fallback path. The size of this array has been chosen empirically. Only
  // ever accessed on the audio thread.
  AutoTArray<CallbackInfo, 5> mAudioThreadCallbackInfo;
#  else
  Mutex mMutex{"AudioClock"};
#  endif
};

/*
 * A bookkeeping class to track the read/write position of an audio buffer.
 */
class AudioBufferCursor {
 public:
  AudioBufferCursor(Span<AudioDataValue> aSpan, uint32_t aChannels,
                    uint32_t aFrames)
      : mChannels(aChannels), mSpan(aSpan), mFrames(aFrames) {}

  // Advance the cursor to account for frames that are consumed.
  uint32_t Advance(uint32_t aFrames) {
    MOZ_DIAGNOSTIC_ASSERT(Contains(aFrames));
    MOZ_ASSERT(mFrames >= aFrames);
    mFrames -= aFrames;
    mOffset += mChannels * aFrames;
    return aFrames;
  }

  // The number of frames available for read/write in this buffer.
  uint32_t Available() const { return mFrames; }

  // Return a pointer where read/write should begin.
  AudioDataValue* Ptr() const {
    MOZ_DIAGNOSTIC_ASSERT(mOffset <= mSpan.Length());
    return mSpan.Elements() + mOffset;
  }

 protected:
  bool Contains(uint32_t aFrames) const {
    return mSpan.Length() >= mOffset + mChannels * aFrames;
  }
  const uint32_t mChannels;

 private:
  const Span<AudioDataValue> mSpan;
  size_t mOffset = 0;
  uint32_t mFrames;
};

/*
 * A helper class to encapsulate pointer arithmetic and provide means to modify
 * the underlying audio buffer.
 */
class AudioBufferWriter : public AudioBufferCursor {
 public:
  AudioBufferWriter(Span<AudioDataValue> aSpan, uint32_t aChannels,
                    uint32_t aFrames)
      : AudioBufferCursor(aSpan, aChannels, aFrames) {}

  uint32_t WriteZeros(uint32_t aFrames) {
    MOZ_DIAGNOSTIC_ASSERT(Contains(aFrames));
    memset(Ptr(), 0, sizeof(AudioDataValue) * mChannels * aFrames);
    return Advance(aFrames);
  }

  uint32_t Write(const AudioDataValue* aPtr, uint32_t aFrames) {
    MOZ_DIAGNOSTIC_ASSERT(Contains(aFrames));
    memcpy(Ptr(), aPtr, sizeof(AudioDataValue) * mChannels * aFrames);
    return Advance(aFrames);
  }

  // Provide a write fuction to update the audio buffer with the following
  // signature: uint32_t(const AudioDataValue* aPtr, uint32_t aFrames)
  // aPtr: Pointer to the audio buffer.
  // aFrames: The number of frames available in the buffer.
  // return: The number of frames actually written by the function.
  template <typename Function>
  uint32_t Write(const Function& aFunction, uint32_t aFrames) {
    MOZ_DIAGNOSTIC_ASSERT(Contains(aFrames));
    return Advance(aFunction(Ptr(), aFrames));
  }

  using AudioBufferCursor::Available;
};

// AudioStream owns a single cubeb output stream and runs a small state machine
// over the cubeb lifetime (see the StreamState enum):
//
//   INITIALIZED --Start()--> STARTED <==Resume()/Pause()==> STOPPED
//        |                       |                             |
//        | Start() fails         | cubeb stop/start fails      |
//        |                       | or StateCallback(ERROR)     |
//        v                       v                             v
//        +---------------------> ERRORED <-------------------- +
//
//   STARTED/STOPPED --StateCallback(end-of-stream)--> DRAINED
//   any state ---------------ShutDown()-------------> SHUTDOWN  (terminal)
//
// DRAINED and ERRORED are terminal for playback; only ShutDown() leaves them.
// SHUTDOWN is final.
//
// AudioStream runs in one of two modes, the standard mode and the keep-running
// mode, both exercised in normal playback.
//
// In the standard mode mState mirrors the physical cubeb stream: Pause() calls
// cubeb_stream_stop, Resume() calls cubeb_stream_start, and re-pausing an
// already stopped stream (or re-resuming an already started one) is disallowed.
//
// The keep-running mode, toggled by SetKeepRunningMode(), reuses the stream
// across a seek instead of destroying it. When it is on and cubeb is physically
// running, Pause()/Resume() update just the STARTED/STOPPED value and do NOT
// stop or start cubeb, so mState becomes a logical view that can differ from
// the still-running physical stream, and re-entering the same state is a
// harmless no-op. If cubeb is not running, pause/resume fall back to the
// standard stop/start path. Skipping the backend calls is safe because playback
// is driven by the ring buffer and the frame-counter clock, never by mState.
//
// Access to a single instance of this class must be synchronized by
// callers, or made from a single thread.  SetVolume, Get{Rate,Channels} and
// SetMicrophoneActive are thread-safe without external synchronization.
// GetPosition, GetPositionInFrames and RebaseLive are not: on macOS they claim
// the single-consumer role on the clock's callback-info queue and drive
// owner-thread-only state, so all three must run on one thread. Elsewhere a
// mutex guards that state on both sides.
class AudioStream final {
  virtual ~AudioStream();

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AudioStream)

  class Chunk {
   public:
    // Return a pointer to the audio data.
    virtual const AudioDataValue* Data() const = 0;
    // Return the number of frames in this chunk.
    virtual uint32_t Frames() const = 0;
    // Return the number of audio channels.
    virtual uint32_t Channels() const = 0;
    // Return the sample rate of this chunk.
    virtual uint32_t Rate() const = 0;
    // Return a writable pointer for downmixing.
    virtual AudioDataValue* GetWritable() const = 0;
    virtual ~Chunk() = default;
  };

  class DataSource {
   public:
    // Attempt to acquire aFrames frames of audio, and returns the number of
    // frames successfuly acquired.
    virtual uint32_t PopFrames(AudioDataValue* aAudio, uint32_t aFrames,
                               bool aAudioThreadChanged) = 0;
    // Return true if no more data will be added to the source.
    virtual bool Ended() const = 0;
    // Return true when a shortfall from PopFrames is deliberate rather than a
    // glitch. Such frames are still accounted as underrun, which is what stops
    // them advancing the reported position; this only suppresses the reporting.
    virtual bool IsIntentionallySilent() const { return false; }

   protected:
    virtual ~DataSource() = default;
  };

  // aOutputChannels is the number of audio channels (1 for mono, 2 for stereo,
  // etc), aChannelMap is the indicator for channel layout(mono, stereo, 5.1 or
  // 7.1 ). Initialize the audio stream.and aRate is the sample rate
  // (22050Hz, 44100Hz, etc).
  AudioStream(DataSource& aSource, uint32_t aInRate, uint32_t aOutputChannels,
              AudioConfig::ChannelLayout::ChannelMap aChannelMap);

  nsresult Init(AudioDeviceInfo* aSinkInfo);

  // Closes the stream. All future use of the stream is an error.
  void ShutDown();

  // Turn the keep-running mode on or off. When on, a pause across a seek keeps
  // the audio stream alive and reusable instead of tearing it down.
  void SetKeepRunningMode(bool aKeepRunning);

  bool IsCubebStarted() const { return mCubebStarted; }

  // Rebase the clock of the reused stream so the reported time resumes from the
  // seek target. Does not touch the ended promise.
  void RebaseLive();

  // Re-arm the ended promise for a reused stream: resolve any outstanding one
  // and return a fresh promise, or a rejected one if the stream errored while
  // it stayed running. Kept separate from RebaseLive() because re-arming the
  // end signal and rebasing the clock are unrelated concerns.
  RefPtr<MediaSink::EndedPromise> ReinitEndedPromise();

  // Set the current volume of the audio playback. This is a value from
  // 0 (meaning muted) to 1 (meaning full volume).  Thread-safe.
  void SetVolume(double aVolume);

  void SetStreamName(const nsAString& aStreamName);

  // Start the stream.
  RefPtr<MediaSink::EndedPromise> Start();

  // Pause audio playback.
  void Pause();

  // Resume audio playback.
  void Resume();

  // Return the position in microseconds of the audio frame being played by
  // the audio hardware, compensated for playback rate change. Thread-safe.
  int64_t GetPosition();

  // Return the position, measured in audio frames played since the stream
  // was opened, of the audio hardware.  Thread-safe.
  int64_t GetPositionInFrames();

  uint32_t GetOutChannels() const { return mOutChannels; }

  // Set playback rate as a multiple of the intrinsic playback rate. This is
  // to be called only with aPlaybackRate > 0.0.
  nsresult SetPlaybackRate(double aPlaybackRate);
  // Switch between resampling (if false) and time stretching (if true,
  // default).
  nsresult SetPreservesPitch(bool aPreservesPitch);

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const;

  bool IsPlaybackCompleted() const;

  // Returns true if at least one DataCallback has been called.
  bool CallbackStarted() const { return mCallbacksStarted; }

 protected:
  friend class AudioClock;

  // Return the position, measured in audio frames played since the stream was
  // opened, of the audio hardware, not adjusted for the changes of playback
  // rate or underrun frames.
  // Caller must own the monitor.
  int64_t GetPositionInFramesUnlocked();

 private:
  nsresult OpenCubeb(cubeb* aContext, cubeb_stream_params& aParams,
                     TimeStamp aStartTime, bool aIsFirst);

  static long DataCallback_S(cubeb_stream*, void* aThis,
                             const void* /* aInputBuffer */,
                             void* aOutputBuffer, long aFrames) {
    return static_cast<AudioStream*>(aThis)->DataCallback(aOutputBuffer,
                                                          aFrames);
  }

  static void StateCallback_S(cubeb_stream*, void* aThis, cubeb_state aState) {
    static_cast<AudioStream*>(aThis)->StateCallback(aState);
  }

  long DataCallback(void* aBuffer, long aFrames);
  void StateCallback(cubeb_state aState);

  // Audio thread only
  nsresult EnsureTimeStretcherInitialized();
  void GetUnprocessed(AudioBufferWriter& aWriter);
  void GetTimeStretched(AudioBufferWriter& aWriter);
  void UpdatePlaybackRateIfNeeded();

  // Return true if audio frames are valid (correct sampling rate and valid
  // channel count) otherwise false.
  bool IsValidAudioFormat(Chunk* aChunk) MOZ_REQUIRES(mMonitor);

  template <typename Function, typename... Args>
  int InvokeCubeb(Function aFunction, Args&&... aArgs) MOZ_REQUIRES(mMonitor);
  bool CheckThreadIdChanged();
  void AssertIsOnAudioThread() const;

  RLBoxSoundTouch* mTimeStretcher;
  AudioClock mAudioClock;

  WavDumper mDumpFile;

  const AudioConfig::ChannelLayout::ChannelMap mChannelMap;

  // The monitor is held to protect all access to member variables below.
  Monitor mMonitor;

  const uint32_t mOutChannels;

  // mCubebStream holds a bare pointer to cubeb, so we hold a ref on its behalf
  RefPtr<CubebUtils::CubebHandle> mCubeb;
  // Owning reference to a cubeb_stream.  Set in Init(), cleared in ShutDown, so
  // no lock is needed to access.
  UniquePtr<cubeb_stream, CubebDestroyPolicy> mCubebStream;

  enum StreamState {
    INITIALIZED,  // Initialized, playback has not begun.
    STARTED,      // cubeb started.
    STOPPED,      // Stopped by a call to Pause().
    DRAINED,      // StateCallback has indicated that the drain is complete.
    ERRORED,      // Stream disabled due to an internal error.
    SHUTDOWN      // ShutDown has been called
  };

  std::atomic<StreamState> mState;

  // DataSource::PopFrames can never be called concurrently.
  // DataSource::IsEnded uses only atomics.
  DataSource& mDataSource;

  // The device info of the current sink. If null
  // the default device is used. It is set
  // during the Init() in decoder thread.
  RefPtr<AudioDeviceInfo> mSinkInfo;
  // Contains the id of the audio thread, from profiler_get_thread_id.
  std::atomic<ProfilerThreadId> mAudioThreadId;
  const bool mSandboxed = false;

  MozPromiseHolder<MediaSink::EndedPromise> mEndedPromise
      MOZ_GUARDED_BY(mMonitor);
  std::atomic<bool> mPlaybackComplete;
  // Both written on the MDSM thread, read on the audio thread.
  std::atomic<float> mPlaybackRate;
  std::atomic<bool> mPreservesPitch;
  // Audio thread only
  bool mAudioThreadChanged = false;
  Atomic<bool> mCallbacksStarted;

  // The keep-running mode. When on, a pause across a seek leaves the cubeb
  // stream running and pause/resume only track the logical playing state, so
  // the stream can be reused instead of being torn down and recreated.
  Atomic<bool> mKeepRunning{false};

  // Whether the cubeb stream is currently physically running, i.e. started and
  // not yet stopped. Only read and written on the owner thread, so it needs no
  // synchronization.
  bool mCubebStarted = false;

  // Whether the last Resume() found the cubeb stream still running, so the
  // frames handed over before the seek are still queued in the device. A
  // restart discards them. Owner thread only.
  bool mResumeKeptCubebRunning = false;
};

}  // namespace mozilla

#endif
