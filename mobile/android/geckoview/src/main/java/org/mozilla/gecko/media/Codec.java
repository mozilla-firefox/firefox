/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecInfo.VideoCapabilities;
import android.media.MediaCodecList;
import android.media.MediaCrypto;
import android.media.MediaFormat;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;
import android.view.Surface;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.LinkedList;
import java.util.List;
import java.util.Queue;
import org.mozilla.gecko.gfx.GeckoSurface;

/* package */ final class Codec extends ICodec.Stub implements IBinder.DeathRecipient {
  private static final String LOGTAG = "GeckoRemoteCodec";
  private static final boolean DEBUG = false;
  public static final String SW_CODEC_PREFIX = "OMX.google.";

  public enum Error {
    DECODE,
    FATAL
  }

  private final class Callbacks implements AsyncCodec.Callbacks {
    @Override
    public void onInputBufferAvailable(final AsyncCodec codec, final int index) {
      mInputProcessor.onBuffer(index);
    }

    @Override
    public void onOutputBufferAvailable(
        final AsyncCodec codec, final int index, final MediaCodec.BufferInfo info) {
      mOutputProcessor.onBuffer(index, info);
    }

    @Override
    public void onError(final AsyncCodec codec, final int error) {
      reportError(Error.FATAL, new Exception("codec error:" + error));
    }

    @Override
    public void onOutputFormatChanged(final AsyncCodec codec, final MediaFormat format) {
      mOutputProcessor.onFormatChanged(format);
    }
  }

  private static final class Input {
    public final Sample sample;
    public boolean reported;

    public Input(final Sample sample) {
      this.sample = sample;
    }
  }

  private final class InputProcessor {
    private boolean mHasInputCapacitySet;
    private Queue<Integer> mAvailableInputBuffers = new LinkedList<>();
    private Queue<Sample> mDequeuedSamples = new LinkedList<>();
    private Queue<Input> mInputSamples = new LinkedList<>();
    private boolean mStopped;

    private synchronized Sample onAllocate(final int size) {
      final Sample sample = mSamplePool.obtainInput(size);
      sample.session = mSession;
      mDequeuedSamples.add(sample);
      return sample;
    }

    private synchronized void onSample(final Sample sample) {
      if (sample == null) {
        // Ignore empty input.
        mSamplePool.recycleInput(mDequeuedSamples.remove());
        Log.w(LOGTAG, "WARN: empty input sample");
        return;
      }

      if (sample.isEOS()) {
        // queueInput() is oneway, so an EOS sent before a flush() can arrive
        // after it. Queuing it would leave the codec ignoring all new input.
        if (sample.session >= mSession) {
          queueSample(sample);
        }
        return;
      }

      if (sample.session >= mSession) {
        final Sample dequeued = mDequeuedSamples.remove();
        dequeued.setBufferInfo(sample.info);
        dequeued.setCryptoInfo(sample.cryptoInfo);
        queueSample(dequeued);
      }

      sample.dispose();
    }

    private void queueSample(final Sample sample) {
      if (!mInputSamples.offer(new Input(sample))) {
        reportError(Error.FATAL, new Exception("FAIL: input sample queue is full"));
        return;
      }

      try {
        feedSampleToBuffer();
      } catch (final Exception e) {
        reportError(Error.FATAL, e);
      }
    }

    private synchronized void onBuffer(final int index) {
      if (mStopped || !isValidBuffer(index)) {
        return;
      }

      if (!mHasInputCapacitySet) {
        final int capacity = mCodec.getInputBuffer(index).capacity();
        if (capacity > 0) {
          mSamplePool.setInputBufferSize(capacity);
          mHasInputCapacitySet = true;
        }
      }

      if (mAvailableInputBuffers.offer(index)) {
        feedSampleToBuffer();
      } else {
        reportError(Error.FATAL, new Exception("FAIL: input buffer queue is full"));
      }
    }

    private boolean isValidBuffer(final int index) {
      try {
        return mCodec.getInputBuffer(index) != null;
      } catch (final IllegalStateException e) {
        if (DEBUG) {
          Log.d(LOGTAG, "invalid input buffer#" + index, e);
        }
        return false;
      }
    }

    private void feedSampleToBuffer() {
      while (!mAvailableInputBuffers.isEmpty() && !mInputSamples.isEmpty()) {
        final int index = mAvailableInputBuffers.poll();
        if (!isValidBuffer(index)) {
          continue;
        }
        int len = 0;
        final Sample sample = mInputSamples.poll().sample;
        final long pts = sample.info.presentationTimeUs;
        final int flags = sample.info.flags;
        final MediaCodec.CryptoInfo cryptoInfo = sample.cryptoInfo;
        if (!sample.isEOS() && sample.bufferId != Sample.NO_BUFFER) {
          len = sample.info.size;
          final ByteBuffer buf = mCodec.getInputBuffer(index);
          try {
            mSamplePool
                .getInputBuffer(sample.bufferId)
                .writeToByteBuffer(buf, sample.info.offset, len);
          } catch (final IOException e) {
            e.printStackTrace();
            len = 0;
          }
          mSamplePool.recycleInput(sample);
        }

        try {
          if (cryptoInfo != null && len > 0) {
            mCodec.queueSecureInputBuffer(index, 0, cryptoInfo, pts, flags);
          } else {
            mCodec.queueInputBuffer(index, 0, len, pts, flags);
          }
          mCallbacks.onInputQueued(pts);
        } catch (final RemoteException e) {
          e.printStackTrace();
        } catch (final Exception e) {
          reportError(Error.FATAL, e);
          return;
        }
      }
      reportPendingInputs();
    }

    private void reportPendingInputs() {
      try {
        for (final Input i : mInputSamples) {
          if (!i.reported) {
            i.reported = true;
            mCallbacks.onInputPending(i.sample.info.presentationTimeUs);
          }
        }
      } catch (final RemoteException e) {
        e.printStackTrace();
      }
    }

    private synchronized void reset() {
      for (final Input i : mInputSamples) {
        if (!i.sample.isEOS()) {
          mSamplePool.recycleInput(i.sample);
        }
      }
      mInputSamples.clear();

      for (final Sample s : mDequeuedSamples) {
        mSamplePool.recycleInput(s);
      }
      mDequeuedSamples.clear();

      mAvailableInputBuffers.clear();
    }

    private synchronized void start() {
      if (!mStopped) {
        return;
      }
      mStopped = false;
    }

    private synchronized void stop() {
      if (mStopped) {
        return;
      }
      mStopped = true;
      reset();
    }
  }

  private static final class Output {
    public final Sample sample;
    public final int index;

    public Output(final Sample sample, final int index) {
      this.sample = sample;
      this.index = index;
    }
  }

  private class OutputProcessor {
    private final boolean mRenderToSurface;
    // Tracks the largest output frame size observed so far. MediaCodec's
    // getOutputBuffer(idx).capacity() is not reliable as a max-buffer hint
    // on modern Android (it reports the valid data range of the current
    // frame, e.g. ~28 bytes for the first SPS/PPS), so we grow the
    // SamplePool's default monotonically from observed info.size instead.
    private int mMaxObservedOutputSize;
    private Queue<Output> mSentOutputs = new LinkedList<>();
    private boolean mStopped;

    private OutputProcessor(final boolean renderToSurface) {
      mRenderToSurface = renderToSurface;
    }

    private synchronized void onBuffer(final int index, final MediaCodec.BufferInfo info) {
      if (mStopped || !isValidBuffer(index)) {
        return;
      }

      try {
        final Sample output = obtainOutputSample(index, info);
        mSentOutputs.add(new Output(output, index));
        output.session = mSession;
        mCallbacks.onOutput(output);
      } catch (final Exception e) {
        e.printStackTrace();
        mCodec.releaseOutputBuffer(index, false);
      }

      final boolean eos = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
      if (DEBUG && eos) {
        Log.d(LOGTAG, "output EOS");
      }
    }

    private boolean isValidBuffer(final int index) {
      try {
        return (mCodec.getOutputBuffer(index) != null) || mRenderToSurface;
      } catch (final IllegalStateException e) {
        if (DEBUG) {
          Log.e(LOGTAG, "invalid buffer#" + index, e);
        }
        return false;
      }
    }

    private Sample obtainOutputSample(final int index, final MediaCodec.BufferInfo info) {
      // Grow the pool's minimum-useful capacity ahead of allocation so
      // setDefaultBufferSize sweeps any now-stranded recycled samples
      // before obtainOutput could hand one back.
      if (!mRenderToSurface && info.size > mMaxObservedOutputSize) {
        mMaxObservedOutputSize = info.size;
        mSamplePool.setOutputBufferSize(mMaxObservedOutputSize);
      }

      final Sample sample = mSamplePool.obtainOutput(info);

      if (mRenderToSurface) {
        return sample;
      }

      final ByteBuffer output = mCodec.getOutputBuffer(index);
      if (info.size > 0) {
        try {
          mSamplePool
              .getOutputBuffer(sample.bufferId)
              .readFromByteBuffer(output, info.offset, info.size);
        } catch (final IOException e) {
          Log.e(LOGTAG, "Fail to read output buffer:" + e.getMessage());
        }
      }

      return sample;
    }

    private synchronized void onRelease(final Sample sample, final boolean render) {
      final Output output = mSentOutputs.poll();
      if (output != null) {
        mCodec.releaseOutputBuffer(output.index, render);
        mSamplePool.recycleOutput(output.sample);
      } else if (DEBUG) {
        Log.d(LOGTAG, sample + " already released");
      }

      sample.dispose();
    }

    private synchronized void onFormatChanged(final MediaFormat format) {
      if (mStopped) {
        return;
      }
      try {
        mCallbacks.onOutputFormatChanged(new FormatParam(format));
      } catch (final RemoteException re) {
        // Dead recipient.
        re.printStackTrace();
      }
    }

    private synchronized void reset() {
      for (final Output o : mSentOutputs) {
        mCodec.releaseOutputBuffer(o.index, false);
        mSamplePool.recycleOutput(o.sample);
      }
      mSentOutputs.clear();
    }

    private synchronized void start() {
      if (!mStopped) {
        return;
      }
      mStopped = false;
    }

    private synchronized void stop() {
      if (mStopped) {
        return;
      }
      mStopped = true;
      reset();
    }
  }

  private volatile ICodecCallbacks mCallbacks;
  private GeckoSurface mSurface;
  private AsyncCodec mCodec;
  private InputProcessor mInputProcessor;
  private OutputProcessor mOutputProcessor;
  private long mSession;
  private SamplePool mSamplePool;
  // Values will be updated after configure called.
  private volatile boolean mIsAdaptivePlaybackSupported = false;
  private volatile boolean mIsHardwareAccelerated = false;
  private boolean mIsTunneledPlaybackSupported = false;

  public synchronized void setCallbacks(final ICodecCallbacks callbacks) throws RemoteException {
    mCallbacks = callbacks;
    callbacks.asBinder().linkToDeath(this, 0);
  }

  // IBinder.DeathRecipient
  @Override
  public synchronized void binderDied() {
    Log.e(LOGTAG, "Callbacks is dead");
    try {
      release();
    } catch (final RemoteException e) {
      // Nowhere to report the error.
    }
  }

  @Override
  public synchronized boolean configure(
      final FormatParam format, final GeckoSurface surface, final int flags, final String drmStubId)
      throws RemoteException {
    if (mCallbacks == null) {
      Log.e(LOGTAG, "FAIL: callbacks must be set before calling configure()");
      return false;
    }

    if (mCodec != null) {
      if (DEBUG) {
        Log.d(LOGTAG, "release existing codec: " + mCodec);
      }
      mCodec.release();
    }

    if (DEBUG) {
      Log.d(LOGTAG, "configure " + this);
    }

    final MediaFormat fmt = format.asFormat();
    final String mime = fmt.getString(MediaFormat.KEY_MIME);
    if (mime == null || mime.isEmpty()) {
      Log.e(LOGTAG, "invalid MIME type: " + mime);
      return false;
    }

    final List<String> found =
        findMatchingCodecNames(fmt, flags == MediaCodec.CONFIGURE_FLAG_ENCODE);
    for (final String name : found) {
      final AsyncCodec codec =
          configureCodec(
              name, fmt, surface != null ? surface.getSurface() : null, flags, drmStubId);
      if (codec == null) {
        Log.w(LOGTAG, "unable to configure " + name + ". Try next.");
        continue;
      }
      mIsHardwareAccelerated = !name.startsWith(SW_CODEC_PREFIX);
      mCodec = codec;
      // Bug 1789846: Check if the Codec provides stride or height values to use.
      if (flags == MediaCodec.CONFIGURE_FLAG_ENCODE && fmt.containsKey(MediaFormat.KEY_WIDTH)) {
        final MediaFormat inputFormat = mCodec.getInputFormat();
        if (inputFormat != null) {
          if (inputFormat.containsKey(MediaFormat.KEY_STRIDE)) {
            fmt.setInteger(MediaFormat.KEY_STRIDE, inputFormat.getInteger(MediaFormat.KEY_STRIDE));
          }
          if (inputFormat.containsKey(MediaFormat.KEY_SLICE_HEIGHT)) {
            fmt.setInteger(
                MediaFormat.KEY_SLICE_HEIGHT, inputFormat.getInteger(MediaFormat.KEY_SLICE_HEIGHT));
          }
        }
      }
      mInputProcessor = new InputProcessor();
      final boolean renderToSurface = surface != null;
      mOutputProcessor = new OutputProcessor(renderToSurface);
      mSamplePool = new SamplePool(name, renderToSurface);
      if (renderToSurface) {
        mIsTunneledPlaybackSupported = mCodec.isTunneledPlaybackSupported(mime);
        mSurface = surface; // Take ownership of surface.
      }
      if (DEBUG) {
        Log.d(LOGTAG, codec.toString() + " created. Render to surface?" + renderToSurface);
      }
      return true;
    }

    return false;
  }

  private List<String> findMatchingCodecNames(final MediaFormat format, final boolean isEncoder) {
    final String mimeType = format.getString(MediaFormat.KEY_MIME);
    // Missing width and height value in format means audio;
    // Video format should never has 0 width or height.
    final int width =
        format.containsKey(MediaFormat.KEY_WIDTH) ? format.getInteger(MediaFormat.KEY_WIDTH) : 0;
    final int height =
        format.containsKey(MediaFormat.KEY_HEIGHT) ? format.getInteger(MediaFormat.KEY_HEIGHT) : 0;

    int numCodecs = 0;
    final List<String> found = new ArrayList<>();
    try {
      numCodecs = MediaCodecList.getCodecCount();
    } catch (final RuntimeException e) {
      Log.e(LOGTAG, "Failed retrieving codec count finding matching codec names", e);
      return found;
    }

    for (int i = 0; i < numCodecs; i++) {
      final MediaCodecInfo info = MediaCodecList.getCodecInfoAt(i);
      if (info.isEncoder() == !isEncoder) {
        continue;
      }

      final String[] types = info.getSupportedTypes();
      for (final String t : types) {
        if (!t.equalsIgnoreCase(mimeType)) {
          continue;
        }
        final String name = info.getName();
        if (isEncoder && width > 0 && height > 0) {
          final VideoCapabilities c = info.getCapabilitiesForType(mimeType).getVideoCapabilities();
          if (c != null && !c.isSizeSupported(width, height)) {
            if (DEBUG) {
              Log.d(LOGTAG, name + ": " + width + "x" + height + " not supported");
            }
            continue;
          }
        }

        found.add(name);
        if (DEBUG) {
          Log.d(
              LOGTAG,
              "found " + (isEncoder ? "encoder:" : "decoder:") + name + " for mime:" + mimeType);
        }
      }
    }
    return found;
  }

  private AsyncCodec configureCodec(
      final String name,
      final MediaFormat format,
      final Surface surface,
      final int flags,
      final String drmStubId) {
    try {
      final AsyncCodec codec = AsyncCodec.create(name);
      codec.setCallbacks(new Callbacks(), null);

      final MediaCrypto crypto = RemoteMediaDrmBridgeStub.getMediaCrypto(drmStubId);
      if (DEBUG) {
        Log.d(
            LOGTAG,
            "configure mediacodec with crypto(" + (crypto != null) + ") / Id :" + drmStubId);
      }

      if (surface != null) {
        setupAdaptivePlayback(codec, format);
      }

      codec.configure(format, surface, crypto, flags);
      return codec;
    } catch (final Exception e) {
      Log.e(LOGTAG, "codec creation error", e);
      return null;
    }
  }

  private void setupAdaptivePlayback(final AsyncCodec codec, final MediaFormat format) {
    // Video decoder should config with adaptive playback capability.
    mIsAdaptivePlaybackSupported =
        codec.isAdaptivePlaybackSupported(format.getString(MediaFormat.KEY_MIME));
    if (mIsAdaptivePlaybackSupported) {
      if (DEBUG) {
        Log.d(LOGTAG, "codec supports adaptive playback  = " + mIsAdaptivePlaybackSupported);
      }
      // TODO: may need to find a way to not use hard code to decide the max w/h.
      format.setInteger(MediaFormat.KEY_MAX_WIDTH, 1920);
      format.setInteger(MediaFormat.KEY_MAX_HEIGHT, 1080);
    }
  }

  @Override
  public synchronized boolean isAdaptivePlaybackSupported() {
    return mIsAdaptivePlaybackSupported;
  }

  @Override
  public synchronized boolean isHardwareAccelerated() {
    return mIsHardwareAccelerated;
  }

  @Override
  public synchronized boolean isTunneledPlaybackSupported() {
    return mIsTunneledPlaybackSupported;
  }

  @Override
  public synchronized void start() throws RemoteException {
    if (DEBUG) {
      Log.d(LOGTAG, "start " + this);
    }
    mInputProcessor.start();
    mOutputProcessor.start();
    try {
      mCodec.start();
    } catch (final Exception e) {
      reportError(Error.FATAL, e);
    }
  }

  private void reportError(final Error error, final Exception e) {
    if (e != null) {
      e.printStackTrace();
    }
    try {
      mCallbacks.onError(error == Error.FATAL);
    } catch (final NullPointerException ne) {
      // mCallbacks has been disposed by release().
    } catch (final RemoteException re) {
      re.printStackTrace();
    }
  }

  @Override
  public synchronized void stop() throws RemoteException {
    if (DEBUG) {
      Log.d(LOGTAG, "stop " + this);
    }
    try {
      mInputProcessor.stop();
      mOutputProcessor.stop();

      mCodec.stop();
    } catch (final Exception e) {
      reportError(Error.FATAL, e);
    }
  }

  @Override
  public synchronized void flush() throws RemoteException {
    if (DEBUG) {
      Log.d(LOGTAG, "flush " + this);
    }
    try {
      mInputProcessor.stop();
      mOutputProcessor.stop();

      mCodec.flush();
      if (DEBUG) {
        Log.d(LOGTAG, "flushed " + this);
      }
      mInputProcessor.start();
      mOutputProcessor.start();
      mCodec.resumeReceivingInputs();
      mSession++;
    } catch (final Exception e) {
      reportError(Error.FATAL, e);
    }
  }

  @Override
  public synchronized Sample dequeueInput(final int size) throws RemoteException {
    try {
      return mInputProcessor.onAllocate(size);
    } catch (final Exception e) {
      // Translate allocation error to remote exception.
      throw new RemoteException(e.getMessage());
    }
  }

  @Override
  public synchronized SampleBuffer getInputBuffer(final int id) {
    if (mSamplePool == null) {
      return null;
    }
    return mSamplePool.getInputBuffer(id);
  }

  @Override
  public synchronized SampleBuffer getOutputBuffer(final int id) {
    if (mSamplePool == null) {
      return null;
    }
    return mSamplePool.getOutputBuffer(id);
  }

  @Override
  public synchronized void queueInput(final Sample sample) throws RemoteException {
    try {
      mInputProcessor.onSample(sample);
    } catch (final Exception e) {
      throw new RemoteException(e.getMessage());
    }
  }

  @Override
  public synchronized void setBitrate(final int bps) {
    try {
      mCodec.setBitrate(bps);
    } catch (final Exception e) {
      reportError(Error.FATAL, e);
    }
  }

  @Override
  public synchronized void releaseOutput(final Sample sample, final boolean render) {
    try {
      mOutputProcessor.onRelease(sample, render);
    } catch (final Exception e) {
      reportError(Error.FATAL, e);
    }
  }

  @Override
  public synchronized void release() throws RemoteException {
    if (DEBUG) {
      Log.d(LOGTAG, "release " + this);
    }
    try {
      // In case Codec.stop() is not called yet.
      mInputProcessor.stop();
      mOutputProcessor.stop();

      mCodec.release();
    } catch (final Exception e) {
      reportError(Error.FATAL, e);
    }
    mCodec = null;
    mSamplePool.reset();
    mSamplePool = null;
    mCallbacks.asBinder().unlinkToDeath(this, 0);
    mCallbacks = null;
    if (mSurface != null) {
      mSurface.release();
      mSurface = null;
    }
  }
}
