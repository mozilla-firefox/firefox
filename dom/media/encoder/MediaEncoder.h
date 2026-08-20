/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MediaEncoder_h_
#define MediaEncoder_h_

#include "ContainerWriter.h"
#include "CubebUtils.h"
#include "MediaQueue.h"
#include "MediaTrackGraph.h"
#include "MediaTrackListener.h"
#include "TrackEncoder.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/MozPromise.h"
#include "mozilla/UniquePtr.h"
#include "nsIMemoryReporter.h"

namespace mozilla {

class DriftCompensator;
class Muxer;
class Runnable;
class TaskQueue;

namespace dom {
class AudioNode;
class AudioStreamTrack;
class BlobImpl;
class MediaStreamTrack;
class MutableBlobStorage;
class VideoStreamTrack;
}  // namespace dom

class DriftCompensator;

/**
 * MediaEncoder is the framework of encoding module, it controls and manages
 * procedures between Muxer, ContainerWriter and TrackEncoder. ContainerWriter
 * writes the encoded track data into a specific container (e.g. ogg, webm).
 * AudioTrackEncoder and VideoTrackEncoder are subclasses of TrackEncoder, and
 * are responsible for encoding raw data coming from MediaStreamTracks.
 *
 * MediaEncoder solves threading issues by doing message passing to a TaskQueue
 * (the "encoder thread") as passed in to the constructor. Each
 * MediaStreamTrack to be recorded is set up with a MediaTrackListener.
 * Typically there are a non-direct track listeners for audio, direct listeners
 * for video, and there is always a non-direct listener on each track for
 * time-keeping. The listeners forward data to their corresponding TrackEncoders
 * on the encoder thread.
 *
 * The MediaEncoder listens to events from all TrackEncoders, and in turn
 * signals events to interested parties. Typically a MediaRecorder::Session.
 * The MediaEncoder automatically encodes incoming data, muxes it, writes it
 * into a container and stores the container data into a MutableBlobStorage.
 * It is timeslice-aware so that it can notify listeners when it's time to
 * expose a blob due to filling the timeslice.
 *
 * MediaEncoder is designed to be a passive component, neither does it own or is
 * in charge of managing threads. Instead this is done by its owner.
 *
 * For example, usage from MediaRecorder of this component would be:
 * 1) Create an encoder with a valid MIME type. Note that there are more
 *    configuration options, see the docs on MediaEncoder::CreateEncoder.
 *    => encoder = MediaEncoder::CreateEncoder(aMIMEType);
 *    It then creates track encoders and the appropriate ContainerWriter
 *    according to the MIME type
 *
 * 2) Connect handlers through MediaEventListeners to the MediaEncoder's
 *    MediaEventSources, StartedEvent(), DataAvailableEvent(), ErrorEvent() and
 *    ShutdownEvent().
 *    => listener = encoder->DataAvailableEvent().Connect(mainThread, &OnBlob);
 *
 * 3) Connect the sources to be recorded. Either through:
 *    => encoder->ConnectAudioNode(node);
 *    or
 *    => encoder->ConnectMediaStreamTrack(track);
 *    These should not be mixed. When connecting MediaStreamTracks there is
 *    support for at most one of each kind.
 *
 * 4) MediaEncoder automatically encodes data from the connected tracks, muxes
 *    them and writes it all into a blob, including metadata. When the blob
 *    contains at least `timeslice` worth of data it notifies the
 *    DataAvailableEvent that was connected in step 2.
 *    => void OnBlob(RefPtr<BlobImpl> aBlob) {
 *    =>   DispatchBlobEvent(Blob::Create(GetRelevantGlobal(), aBlob));
 *    => };
 *
 * 5) To stop encoding, there are multiple options:
 *
 *    5.1) Stop() for a graceful stop.
 *         => encoder->Stop();
 *
 *    5.2) Cancel() for an immediate stop, if you don't need the data currently
 *         buffered.
 *         => encoder->Cancel();
 *
 *    5.3) When all input tracks end, the MediaEncoder will automatically stop
 *         and shut down.
 */
class MediaEncoder {
 private:
  class AudioTrackListener;
  class VideoTrackListener;
  class EncoderListener;

 public:
  using BlobPromise =
      MozPromise<RefPtr<dom::BlobImpl>, nsresult, false /* IsExclusive */>;
  using SizeOfPromise = MozPromise<size_t, size_t, true /* IsExclusive */>;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaEncoder)

 private:
  MediaEncoder(RefPtr<TaskQueue> aEncoderThread,
               RefPtr<DriftCompensator> aDriftCompensator,
               UniquePtr<ContainerWriter> aWriter,
               UniquePtr<AudioTrackEncoder> aAudioEncoder,
               UniquePtr<VideoTrackEncoder> aVideoEncoder,
               UniquePtr<MediaQueue<EncodedFrame>> aEncodedAudioQueue,
               UniquePtr<MediaQueue<EncodedFrame>> aEncodedVideoQueue,
               const nsAString& aMIMEType, uint64_t aMaxMemory,
               TimeDuration aTimeslice);

 public:
  /**
   * Called on main thread from MediaRecorder::Pause.
   */
  void Suspend();

  /**
   * Called on main thread from MediaRecorder::Resume.
   */
  void Resume();

  /**
   * Disconnects the input tracks, causing the encoding to stop.
   */
  void DisconnectTracks();

  /**
   * Connects an AudioNode with the appropriate encoder.
   */
  void ConnectAudioNode(dom::AudioNode* aNode, uint32_t aOutput);

  /**
   * Connects a MediaStreamTrack with the appropriate encoder.
   */
  void ConnectMediaStreamTrack(dom::MediaStreamTrack* aTrack);

  /**
   * Creates an encoder with the given MIME type. This must be a valid MIME type
   * or we will crash hard.
   * Bitrates are given either explicit, or with 0 for defaults.
   * aTrackRate is the rate in which data will be fed to the TrackEncoders.
   * aMaxMemory is the maximum number of bytes of muxed data allowed in memory.
   * Beyond that the blob is moved to a temporary file.
   * aTimeslice is the minimum duration of muxed data we gather before
   * automatically issuing a dataavailable event.
   */
  static already_AddRefed<MediaEncoder> CreateEncoder(
      RefPtr<TaskQueue> aEncoderThread, const nsAString& aMimeType,
      uint32_t aAudioBitrate, uint32_t aVideoBitrate, uint8_t aTrackTypes,
      TrackRate aTrackRate, uint64_t aMaxMemory, TimeDuration aTimeslice);

  /**
   * Encodes raw data for all tracks to aOutputBufs. The buffer of container
   * data is allocated in ContainerWriter::GetContainerData().
   *
   * On its first call, metadata is also encoded. TrackEncoders must have been
   * initialized before this is called.
   */
  nsresult GetEncodedData(nsTArray<nsTArray<uint8_t>>* aOutputBufs);

  /**
   * Asserts that Shutdown() has been called. Reasons are encoding
   * complete, encounter an error, or being canceled by its caller.
   */
  void AssertShutdownCalled() { MOZ_ASSERT(mShutdownPromise); }

  /**
   * Stops (encoding any data currently buffered) the encoding and shuts down
   * the encoder using Shutdown().
   */
  RefPtr<GenericNonExclusivePromise> Stop();

  /**
   * Cancels (discarding any data currently buffered) the encoding and shuts
   * down the encoder using Shutdown().
   */
  RefPtr<GenericNonExclusivePromise> Cancel();

  bool HasError();

  static bool IsWebMEncoderEnabled();

  /**
   * Updates internal state when track encoders are all initialized.
   */
  void UpdateInitialized();

  /**
   * Updates internal state when track encoders are all initialized, and
   * notifies listeners that this MediaEncoder has been started.
   */
  void UpdateStarted();

  MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOf)
  /*
   * Measure the size of the buffer, and heap memory in bytes occupied by
   * mAudioEncoder and mVideoEncoder.
   */
  RefPtr<SizeOfPromise> SizeOfExcludingThis(
      mozilla::MallocSizeOf aMallocSizeOf);

  /**
   * Encode, mux and store into blob storage what has been buffered until now,
   * then return the blob backed by that storage.
   */
  RefPtr<BlobPromise> RequestData();

  // Event that gets notified when all track encoders have received data.
  MediaEventSource<void>& StartedEvent() { return mStartedEvent; }
  // Event that gets notified when there was an error preventing continued
  // recording somewhere in the MediaEncoder stack.
  MediaEventSource<void>& ErrorEvent() { return mErrorEvent; }
  // Event that gets notified when the MediaEncoder stack has been shut down.
  MediaEventSource<void>& ShutdownEvent() { return mShutdownEvent; }
  // Event that gets notified after we have muxed at least mTimeslice worth of
  // data into the current blob storage.
  MediaEventSource<RefPtr<dom::BlobImpl>>& DataAvailableEvent() {
    return mDataAvailableEvent;
  }

 protected:
  ~MediaEncoder();

 private:
  /**
   * Registers listeners.
   */
  void RegisterListeners();

  /**
   * Sets mGraphTrack if not already set, using a new stream from aTrack's
   * graph.
   */
  void EnsureGraphTrackFrom(MediaTrack* aTrack);

  /**
   * Removes a connected MediaStreamTrack.
   */
  void RemoveMediaStreamTrack(dom::MediaStreamTrack* aTrack);

  /**
   * Shuts down gracefully if there is no remaining live track encoder.
   */
  void MaybeShutdown();

  /**
   * Waits for TrackEncoders to shut down, then shuts down the MediaEncoder and
   * cleans up track encoders.
   */
  RefPtr<GenericNonExclusivePromise> Shutdown();

  /**
   * Sets mError to true, notifies listeners of the error if mError changed,
   * and stops encoding.
   */
  void SetError();

  /**
   * Creates a new MutableBlobStorage if one doesn't exist.
   */
  void MaybeCreateMutableBlobStorage();

  /**
   * Called when an encoded audio frame has been pushed by the audio encoder.
   */
  void OnEncodedAudioPushed(const RefPtr<EncodedFrame>& aFrame);

  /**
   * Called when an encoded video frame has been pushed by the video encoder.
   */
  void OnEncodedVideoPushed(const RefPtr<EncodedFrame>& aFrame);

  /**
   * If enough data has been pushed to the muxer, extract it into the current
   * blob storage. If more than mTimeslice data has been pushed to the muxer
   * since the last DataAvailableEvent was notified, also gather the blob and
   * notify MediaRecorder.
   */
  void MaybeExtractOrGatherBlob();

  // Extracts encoded and muxed data into the current blob storage, creating one
  // if it doesn't exist. The returned promise resolves when data has been
  // stored into the blob.
  RefPtr<GenericPromise> Extract();

  // Stops gathering data into the current blob and resolves when the current
  // blob is available. Future data will be stored in a new blob.
  // Should a previous async GatherBlob() operation still be in progress, we'll
  // wait for it to finish before starting this one.
  RefPtr<BlobPromise> GatherBlob();

  RefPtr<BlobPromise> GatherBlobImpl();

  const RefPtr<nsISerialEventTarget> mMainThread;
  const RefPtr<TaskQueue> mEncoderThread;
  const RefPtr<DriftCompensator> mDriftCompensator;

  const UniquePtr<MediaQueue<EncodedFrame>> mEncodedAudioQueue;
  const UniquePtr<MediaQueue<EncodedFrame>> mEncodedVideoQueue;

  const UniquePtr<Muxer> mMuxer;
  const UniquePtr<AudioTrackEncoder> mAudioEncoder;
  RefPtr<AudioTrackListener> mAudioListener;
  const UniquePtr<VideoTrackEncoder> mVideoEncoder;
  RefPtr<VideoTrackListener> mVideoListener;
  const RefPtr<EncoderListener> mEncoderListener;

 public:
  const nsString mMimeType;

  // Max memory to use for the MutableBlobStorage.
  const uint64_t mMaxMemory;

  // The interval of passing encoded data from MutableBlobStorage to
  // onDataAvailable handler.
  const TimeDuration mTimeslice;

 private:
  MediaEventListener mAudioPushListener;
  MediaEventListener mAudioFinishListener;
  MediaEventListener mVideoPushListener;
  MediaEventListener mVideoFinishListener;

  MediaEventProducer<void> mStartedEvent;
  MediaEventProducer<void> mErrorEvent;
  MediaEventProducer<void> mShutdownEvent;
  MediaEventProducer<RefPtr<dom::BlobImpl>> mDataAvailableEvent;

  // The AudioNode we are encoding.
  // Will be null when input is media stream or destination node.
  RefPtr<dom::AudioNode> mAudioNode;
  // Pipe-track for allowing a track listener on a non-destination AudioNode.
  // Will be null when input is media stream or destination node.
  RefPtr<AudioNodeTrack> mPipeTrack;
  // Input port that connect mAudioNode to mPipeTrack.
  // Will be null when input is media stream or destination node.
  RefPtr<MediaInputPort> mInputPort;
  // An audio track that we are encoding. Will be null if the input stream
  // doesn't contain audio on start() or if the input is an AudioNode.
  RefPtr<dom::AudioStreamTrack> mAudioTrack;
  // A video track that we are encoding. Will be null if the input stream
  // doesn't contain video on start() or if the input is an AudioNode.
  RefPtr<dom::VideoStreamTrack> mVideoTrack;

  // A stream to keep the MediaTrackGraph alive while we're recording.
  RefPtr<SharedDummyTrack> mGraphTrack;

  // A buffer to cache muxed encoded data.
  RefPtr<dom::MutableBlobStorage> mMutableBlobStorage;
  // If set, is a promise for the latest GatherBlob() operation. Allows
  // GatherBlob() operations to be serialized in order to avoid races.
  RefPtr<BlobPromise> mBlobPromise;
  // The end time of the muxed data in the last gathered blob. If more than one
  // track is present, this is the end time of the track that ends the earliest
  // in the last blob. Encoder thread only.
  media::TimeUnit mLastBlobTime;
  // The end time of the muxed data in the current blob storage. If more than
  // one track is present, this is the end time of the track that ends the
  // earliest in the current blob storage. Encoder thread only.
  media::TimeUnit mLastExtractTime;
  // The end time of encoded audio data sent to the muxer. Positive infinity if
  // there is no audio encoder. Encoder thread only.
  media::TimeUnit mMuxedAudioEndTime;
  // The end time of encoded video data sent to the muxer. Positive infinity if
  // there is no video encoder. Encoder thread only.
  media::TimeUnit mMuxedVideoEndTime;

  TimeStamp mStartTime;
  bool mInitialized;
  bool mStarted;
  bool mCompleted;
  bool mError;
  // Set when shutdown starts.
  RefPtr<GenericNonExclusivePromise> mShutdownPromise;
  // Get duration from create encoder, for logging purpose
  double GetEncodeTimeStamp() {
    TimeDuration decodeTime;
    decodeTime = TimeStamp::Now() - mStartTime;
    return decodeTime.ToMilliseconds();
  }
};

}  // namespace mozilla

#endif
