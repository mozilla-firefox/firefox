/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.annotation.SuppressLint;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Build;
import android.os.Looper;
import android.os.Process;
import android.os.SystemClock;
import android.util.Log;
import androidx.annotation.GuardedBy;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Objects;
import java.util.Queue;
import java.util.Set;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.geckoview.GeckoResult;

/**
 * Takes samples and adds markers for Java threads for the Gecko profiler.
 *
 * <p>This class is thread safe because it uses synchronized on accesses to its mutable state. One
 * exception is {@link #isProfilerActive()}: see the javadoc for details.
 */
public class GeckoJavaSampler {

  private static final String LOGTAG = "GeckoJavaSampler";

  private static final String PROFILER_SERVICE_CLASS_NAME =
      "org.mozilla.fenix.perf.ProfilerService";
  private static final String PROFILER_SERVICE_ACTION = "mozilla.perf.action.START_PROFILING";
  public static final String INTENT_PROFILER_STATE_CHANGED =
      "org.mozilla.fenix.PROFILER_STATE_CHANGED";

  /**
   * The thread ID to use for the main thread instead of its true thread ID.
   *
   * <p>The main thread is sampled twice: once for native code and once on the JVM. The native
   * version uses the thread's id so we replace it to avoid a collision. We use this thread ID
   * because it's unlikely any other thread currently has it. We can't use 0 because 0 is considered
   * "unspecified" in native code:
   * https://searchfox.org/mozilla-central/rev/d4ebb53e719b913afdbcf7c00e162f0e96574701/mozglue/baseprofiler/public/BaseProfilerUtils.h#194
   */
  private static final long REPLACEMENT_MAIN_THREAD_ID = 1;

  /**
   * The thread name to use for the main thread instead of its true thread name. The name is "main",
   * which is ambiguous with the JS main thread, so we rename it to match the C++ replacement. We
   * expect our code to later add a suffix to avoid a collision with the C++ thread name. See {@link
   * #REPLACEMENT_MAIN_THREAD_ID} for related details.
   */
  private static final String REPLACEMENT_MAIN_THREAD_NAME = "AndroidUI";

  @GuardedBy("GeckoJavaSampler.class")
  private static SamplingRunnable sSamplingRunnable;

  @GuardedBy("GeckoJavaSampler.class")
  private static ScheduledExecutorService sSamplingScheduler;

  // See isProfilerActive for details on the AtomicReference.
  @GuardedBy("GeckoJavaSampler.class")
  private static final AtomicReference<ScheduledFuture<?>> sSamplingFuture =
      new AtomicReference<>();

  private static final MarkerStorage sMarkerStorage = new MarkerStorage();

  /**
   * Returns true if profiler is running and unpaused at the moment which means it's allowed to add
   * a marker.
   *
   * <p>Thread policy: we want this method to be inexpensive (i.e. non-blocking) because we want to
   * be able to use it in performance-sensitive code. That's why we rely on an AtomicReference. If
   * this requirement didn't exist, the AtomicReference could be removed because the class thread
   * policy is to call synchronized on mutable state access.
   */
  public static boolean isProfilerActive() {
    // This value will only be present if the profiler is started and not paused.
    return sSamplingFuture.get() != null;
  }

  // Use the same timer primitive as the profiler
  // to get a perfect sample syncing.
  @WrapForJNI
  private static native double getProfilerTime();

  /** Try to get the profiler time. Returns null if profiler is not running. */
  public static @Nullable Double tryToGetProfilerTime() {
    if (!isProfilerActive()) {
      // Android profiler hasn't started yet.
      return null;
    }
    if (!GeckoThread.isStateAtLeast(GeckoThread.State.JNI_READY)) {
      // getProfilerTime is not available yet; either libs are not loaded,
      // or profiling hasn't started on the Gecko side yet
      return null;
    }

    return getProfilerTime();
  }

  /**
   * A data container for a profiler sample. This class is effectively immutable (i.e. technically
   * mutable but never mutated after construction) so is thread safe *if it is safely published*
   * (see Java Concurrency in Practice, 2nd Ed., Section 3.5.3 for safe publication idioms).
   */
  private static class Sample {
    public final long mThreadId;
    public final Frame[] mFrames;
    public final double mTime;
    public final long mJavaTime; // non-zero if Android system time is used

    public Sample(final long aThreadId, final StackTraceElement[] aStack) {
      mThreadId = aThreadId;
      mFrames = new Frame[aStack.length];
      mTime = GeckoThread.isStateAtLeast(GeckoThread.State.JNI_READY) ? getProfilerTime() : 0;

      // if mTime == 0, getProfilerTime is not available yet; either libs are not loaded,
      // or profiling hasn't started on the Gecko side yet
      mJavaTime = mTime == 0.0d ? SystemClock.elapsedRealtime() : 0;

      for (int i = 0; i < aStack.length; i++) {
        mFrames[aStack.length - 1 - i] =
            new Frame(
                aStack[i].getMethodName(), aStack[i].getClassName(), aStack[i].getLineNumber());
      }
    }
  }

  /**
   * A container for the metadata around a call in a stack. This class is thread safe by being
   * immutable.
   */
  private static class Frame {
    public final String methodName;
    public final String className;
    public final int lineNumber;

    private Frame(final String methodName, final String className, final int lineNumber) {
      this.methodName = methodName;
      this.className = className;
      this.lineNumber = lineNumber;
    }
  }

  /** A data container for thread metadata. */
  private static class ThreadInfo {
    private final long mId;
    private final String mName;

    public ThreadInfo(final long mId, final String mName) {
      this.mId = mId;
      this.mName = mName;
    }

    @WrapForJNI
    public long getId() {
      return mId;
    }

    @WrapForJNI
    public String getName() {
      return mName;
    }
  }

  /**
   * A data container for metadata around a marker. This class is thread safe by being immutable.
   */
  private static class Marker extends JNIObject {
    /** The id of the thread this marker was captured on. */
    private final long mThreadId;

    /** Name of the marker */
    private final String mMarkerName;

    /** Either start time for the duration markers or time for a point-in-time markers. */
    private final double mTime;

    /**
     * A fallback field of {@link #mTime} but it only exists when {@link #getProfilerTime()} is
     * failed. It is non-zero if Android time is used.
     */
    private final long mJavaTime;

    /** End time for the duration markers. It's zero for point-in-time markers. */
    private final double mEndTime;

    /**
     * A fallback field of {@link #mEndTime} but it only exists when {@link #getProfilerTime()} is
     * failed. It is non-zero if Android time is used.
     */
    private final long mEndJavaTime;

    /** A nullable additional information field for the marker. */
    private @Nullable final String mText;

    /**
     * Constructor for the Marker class. It initializes different kinds of markers depending on the
     * parameters. Here are some combinations to create different kinds of markers:
     *
     * <p>If you want to create a marker that points a single point in time: <code>
     * new Marker("name", null, null, null)</code> to implicitly get the time when this marker is
     * added, or <code>new Marker("name", null, endTime, null)</code> to use an explicit time as an
     * end time retrieved from {@link #tryToGetProfilerTime()}.
     *
     * <p>If you want to create a marker that has a start and end time: <code>
     * new Marker("name", startTime, null, null)</code> to implicitly get the end time when this
     * marker is added, or <code>new Marker("name", startTime, endTime, null)</code> to explicitly
     * give the marker start and end time retrieved from {@link #tryToGetProfilerTime()}.
     *
     * <p>Last parameter is optional and can be given with any combination. This gives users the
     * ability to add more context into a marker.
     *
     * @param aThreadId The id of the thread this marker was captured on.
     * @param aMarkerName Identifier of the marker as a string.
     * @param aStartTime Start time as Double. It can be null if you want to mark a point of time.
     * @param aEndTime End time as Double. If it's null, this function implicitly gets the end time.
     * @param aText An optional string field for more information about the marker.
     */
    public Marker(
        final long aThreadId,
        @NonNull final String aMarkerName,
        @Nullable final Double aStartTime,
        @Nullable final Double aEndTime,
        @Nullable final String aText) {
      mThreadId = getAdjustedThreadId(aThreadId);
      mMarkerName = aMarkerName;
      mText = aText;

      if (aStartTime != null) {
        // Start time is provided. This is an interval marker.
        mTime = aStartTime;
        mJavaTime = 0;
        if (aEndTime != null) {
          // End time is also provided.
          mEndTime = aEndTime;
          mEndJavaTime = 0;
        } else {
          // End time is not provided. Get the profiler time now and use it.
          mEndTime =
              GeckoThread.isStateAtLeast(GeckoThread.State.JNI_READY) ? getProfilerTime() : 0;

          // if mEndTime == 0, getProfilerTime is not available yet; either libs are not loaded,
          // or profiling hasn't started on the Gecko side yet
          mEndJavaTime = mEndTime == 0.0d ? SystemClock.elapsedRealtime() : 0;
        }

      } else {
        // Start time is not provided. This is point-in-time marker.
        mEndTime = 0;
        mEndJavaTime = 0;

        if (aEndTime != null) {
          // End time is also provided. Use that to point the time.
          mTime = aEndTime;
          mJavaTime = 0;
        } else {
          mTime = GeckoThread.isStateAtLeast(GeckoThread.State.JNI_READY) ? getProfilerTime() : 0;

          // if mTime == 0, getProfilerTime is not available yet; either libs are not loaded,
          // or profiling hasn't started on the Gecko side yet
          mJavaTime = mTime == 0.0d ? SystemClock.elapsedRealtime() : 0;
        }
      }
    }

    @WrapForJNI
    @Override // JNIObject
    protected native void disposeNative();

    @WrapForJNI
    public double getStartTime() {
      if (mJavaTime != 0) {
        return (mJavaTime - SystemClock.elapsedRealtime()) + getProfilerTime();
      }
      return mTime;
    }

    @WrapForJNI
    public double getEndTime() {
      if (mEndJavaTime != 0) {
        return (mEndJavaTime - SystemClock.elapsedRealtime()) + getProfilerTime();
      }
      return mEndTime;
    }

    @WrapForJNI
    public long getThreadId() {
      return mThreadId;
    }

    @WrapForJNI
    public @NonNull String getMarkerName() {
      return mMarkerName;
    }

    @WrapForJNI
    public @Nullable String getMarkerText() {
      return mText;
    }
  }

  /**
   * Public method to add a new marker to Gecko profiler. This can be used to add a marker *inside*
   * the geckoview code, but ideally ProfilerController methods should be used instead.
   *
   * @param aMarkerName Identifier of the marker as a string.
   * @param aStartTime Start time as Double. It can be null if you want to mark a point of time.
   * @param aEndTime End time as Double. If it's null, this function implicitly gets the end time.
   * @param aText An optional string field for more information about the marker.
   * @see Marker#Marker(long, String, Double, Double, String)
   */
  public static void addMarker(
      @NonNull final String aMarkerName,
      @Nullable final Double aStartTime,
      @Nullable final Double aEndTime,
      @Nullable final String aText) {
    sMarkerStorage.addMarker(aMarkerName, aStartTime, aEndTime, aText);
  }

  /**
   * A routine to store profiler samples. This class is thread safe because it synchronizes access
   * to its mutable state.
   */
  private static class SamplingRunnable implements Runnable {
    private final long mMainThreadId = Looper.getMainLooper().getThread().getId();

    // Sampling interval that is used by start and unpause
    public final int mInterval;
    private final int mSampleCount;

    @GuardedBy("GeckoJavaSampler.class")
    private boolean mBufferOverflowed = false;

    @GuardedBy("GeckoJavaSampler.class")
    private @NonNull final List<Thread> mThreadsToProfile;

    @GuardedBy("GeckoJavaSampler.class")
    private final Sample[] mSamples;

    @GuardedBy("GeckoJavaSampler.class")
    private int mSamplePos;

    public SamplingRunnable(
        @NonNull final List<Thread> aThreadsToProfile,
        final int aInterval,
        final int aSampleCount) {
      mThreadsToProfile = aThreadsToProfile;
      // Sanity check of sampling interval.
      mInterval = Math.max(1, aInterval);
      mSampleCount = aSampleCount;
      mSamples = new Sample[mSampleCount];
      mSamplePos = 0;
    }

    @Override
    public void run() {
      synchronized (GeckoJavaSampler.class) {
        // To minimize allocation in the critical section, we use a traditional for loop instead of
        // a for each (i.e. `elem : coll`) loop because that allocates an iterator.
        //
        // We won't capture threads that are started during profiling because we iterate through an
        // unchanging list of threads (bug 1759550).
        for (int i = 0; i < mThreadsToProfile.size(); i++) {
          final Thread thread = mThreadsToProfile.get(i);

          // getStackTrace will return an empty trace if the thread is not alive: we call continue
          // to avoid wasting space in the buffer for an empty sample.
          final StackTraceElement[] stackTrace = thread.getStackTrace();
          if (stackTrace.length == 0) {
            continue;
          }

          mSamples[mSamplePos] = new Sample(thread.getId(), stackTrace);
          mSamplePos += 1;
          if (mSamplePos == mSampleCount) {
            // Sample array is full now, go back to start of
            // the array and override old samples
            mSamplePos = 0;
            mBufferOverflowed = true;
          }
        }
      }
    }

    private Sample getSample(final int aSampleId) {
      synchronized (GeckoJavaSampler.class) {
        if (aSampleId >= mSampleCount) {
          // Return early because there is no more sample left.
          return null;
        }

        int samplePos = aSampleId;
        if (mBufferOverflowed) {
          // This is a circular buffer and the buffer is overflowed. Start
          // of the buffer is mSamplePos now. Calculate the real index.
          samplePos = (samplePos + mSamplePos) % mSampleCount;
        }

        // Since the array elements are initialized to null, it will return
        // null whenever we access to an element that's not been written yet.
        // We want it to return null in that case, so it's okay.
        return mSamples[samplePos];
      }
    }
  }

  /**
   * Returns the sample with the given sample ID.
   *
   * <p>Thread safety code smell: this method call is synchronized but this class returns a
   * reference to an effectively immutable object so that the reference is accessible after
   * synchronization ends. It's unclear if this is thread safe. However, this is safe with the
   * current callers (because they are all synchronized and don't leak the Sample) so we don't
   * investigate it further.
   */
  private static synchronized Sample getSample(final int aSampleId) {
    return sSamplingRunnable.getSample(aSampleId);
  }

  @WrapForJNI
  public static Marker pollNextMarker() {
    return sMarkerStorage.pollNextMarker();
  }

  @WrapForJNI
  public static synchronized int getRegisteredThreadCount() {
    return sSamplingRunnable.mThreadsToProfile.size();
  }

  @WrapForJNI
  public static synchronized ThreadInfo getRegisteredThreadInfo(final int aIndex) {
    final Thread thread = sSamplingRunnable.mThreadsToProfile.get(aIndex);

    // See REPLACEMENT_MAIN_THREAD_NAME for why we do this.
    String adjustedThreadName =
        thread.getId() == sSamplingRunnable.mMainThreadId
            ? REPLACEMENT_MAIN_THREAD_NAME
            : thread.getName();

    // To distinguish JVM threads from native threads, we append a JVM-specific suffix.
    adjustedThreadName += " (JVM)";
    return new ThreadInfo(getAdjustedThreadId(thread.getId()), adjustedThreadName);
  }

  @WrapForJNI
  public static synchronized long getThreadId(final int aSampleId) {
    final Sample sample = getSample(aSampleId);
    return getAdjustedThreadId(sample != null ? sample.mThreadId : 0);
  }

  private static synchronized long getAdjustedThreadId(final long threadId) {
    // See REPLACEMENT_MAIN_THREAD_ID for why we do this.
    return threadId == sSamplingRunnable.mMainThreadId ? REPLACEMENT_MAIN_THREAD_ID : threadId;
  }

  @WrapForJNI
  public static synchronized double getSampleTime(final int aSampleId) {
    final Sample sample = getSample(aSampleId);
    if (sample != null) {
      if (sample.mJavaTime != 0) {
        return (sample.mJavaTime - SystemClock.elapsedRealtime()) + getProfilerTime();
      }
      return sample.mTime;
    }
    return 0;
  }

  @WrapForJNI
  public static synchronized String getFrameName(final int aSampleId, final int aFrameId) {
    final Sample sample = getSample(aSampleId);
    if (sample != null && aFrameId < sample.mFrames.length) {
      final Frame frame = sample.mFrames[aFrameId];
      if (frame == null) {
        return null;
      }
      return frame.className + "." + frame.methodName + "()";
    }
    return null;
  }

  /**
   * Returns the source line number of a frame, or a negative value if unknown.
   *
   * @param aSampleId the index of the sample
   * @param aFrameId the index of the frame within the sample
   * @return the line number
   */
  @WrapForJNI
  public static synchronized int getFrameLineNumber(final int aSampleId, final int aFrameId) {
    final Sample sample = getSample(aSampleId);
    if (sample != null && aFrameId < sample.mFrames.length) {
      final Frame frame = sample.mFrames[aFrameId];
      if (frame != null) {
        return frame.lineNumber;
      }
    }
    return -1;
  }

  /**
   * A start/stop-aware container for storing profiler markers.
   *
   * <p>This class is thread safe: see {@link #mMarkers} and other member variables for the
   * threading policy. Start/stop are guaranteed to execute in the order they are called but other
   * methods do not have such ordering guarantees.
   */
  private static class MarkerStorage {
    /**
     * The underlying storage for the markers. This field maintains thread safety without using
     * synchronized everywhere by:
     * <li>- using volatile to allow non-blocking reads
     * <li>- leveraging a thread safe collection when accessing the underlying data
     * <li>- looping until success for compound read-write operations
     */
    private volatile Queue<Marker> mMarkers;

    /**
     * The thread ids of the threads we're profiling. This field maintains thread safety by writing
     * a read-only value to this volatile field before concurrency begins and only reading it during
     * concurrent sections.
     */
    private volatile Set<Long> mProfiledThreadIds = Collections.emptySet();

    MarkerStorage() {}

    public synchronized void start(final int aMarkerCount, final List<Thread> aProfiledThreads) {
      if (this.mMarkers != null) {
        return;
      }
      this.mMarkers = new LinkedBlockingQueue<>(aMarkerCount);

      final Set<Long> profiledThreadIds = new HashSet<>(aProfiledThreads.size());
      for (final Thread thread : aProfiledThreads) {
        profiledThreadIds.add(thread.getId());
      }

      // We use a temporary collection, rather than mutating the collection within the member
      // variable, to ensure the collection is fully written before the state is made available to
      // all threads via the volatile write into the member variable. This collection must be
      // read-only for it to remain thread safe.
      mProfiledThreadIds = Collections.unmodifiableSet(profiledThreadIds);
    }

    public synchronized void stop() {
      if (this.mMarkers == null) {
        return;
      }
      this.mMarkers = null;
      mProfiledThreadIds = Collections.emptySet();
    }

    private void addMarker(
        @NonNull final String aMarkerName,
        @Nullable final Double aStartTime,
        @Nullable final Double aEndTime,
        @Nullable final String aText) {
      final Queue<Marker> markersQueue = this.mMarkers;
      if (markersQueue == null) {
        // Profiler is not active.
        return;
      }

      final long threadId = Thread.currentThread().getId();
      if (!mProfiledThreadIds.contains(threadId)) {
        return;
      }

      final Marker newMarker = new Marker(threadId, aMarkerName, aStartTime, aEndTime, aText);
      boolean successful = markersQueue.offer(newMarker);
      while (!successful) {
        // Marker storage is full, remove the head and add again.
        markersQueue.poll();
        successful = markersQueue.offer(newMarker);
      }
    }

    private Marker pollNextMarker() {
      final Queue<Marker> markersQueue = this.mMarkers;
      if (markersQueue == null) {
        // Profiler is not active.
        return null;
      }
      // Retrieve and return the head of this queue.
      // Returns null if the queue is empty.
      return markersQueue.poll();
    }
  }

  @SuppressLint("DiscouragedApi")
  @WrapForJNI
  public static void start(
      @NonNull final Object[] aFilters, final int aInterval, final int aEntryCount) {
    synchronized (GeckoJavaSampler.class) {
      if (sSamplingRunnable != null) {
        return;
      }

      final ScheduledFuture<?> future = sSamplingFuture.get();
      if (future != null && !future.isDone()) {
        return;
      }

      // Setting a limit of 120000 (2 mins with 1ms interval) for samples and markers for now
      // to make sure we are not allocating too much.
      final int limitedEntryCount = Math.min(aEntryCount, 120000);

      final List<Thread> threadsToProfile = getThreadsToProfile(aFilters);
      if (threadsToProfile.size() < 1) {
        throw new IllegalStateException("Expected >= 1 thread to profile (main thread).");
      }
      Log.i(LOGTAG, "Number of threads to profile: " + threadsToProfile.size());

      sSamplingRunnable = new SamplingRunnable(threadsToProfile, aInterval, limitedEntryCount);
      sMarkerStorage.start(limitedEntryCount, threadsToProfile);
      sSamplingScheduler = Executors.newSingleThreadScheduledExecutor();
      sSamplingFuture.set(
          sSamplingScheduler.scheduleAtFixedRate(
              sSamplingRunnable, 0, sSamplingRunnable.mInterval, TimeUnit.MILLISECONDS));
    }
  }

  private static @NonNull List<Thread> getThreadsToProfile(final Object[] aFilters) {
    // Clean up filters.
    final List<String> cleanedFilters = new ArrayList<>();
    for (final Object rawFilter : aFilters) {
      // aFilters is a String[] but jni can only accept Object[] so we're forced to cast.
      //
      // We could pass the lowercased filters from native code but it may not handle lowercasing the
      // same way Java does so we lower case here so it's consistent later when we lower case the
      // thread name and compare against it.
      final String filter = ((String) rawFilter).trim().toLowerCase(Locale.US);

      // If the filter is empty, it's not meaningful: skip.
      if (filter.isEmpty()) {
        continue;
      }

      cleanedFilters.add(filter);
    }

    final ThreadGroup rootThreadGroup = getRootThreadGroup();
    final Thread[] activeThreads = getActiveThreads(rootThreadGroup);
    final Thread mainThread = Looper.getMainLooper().getThread();

    // We model these catch-all filters after the C++ code (which we should eventually deduplicate):
    // https://searchfox.org/mozilla-central/rev/b0779bcc485dc1c04334dfb9ea024cbfff7b961a/tools/profiler/core/platform.cpp#778-801
    if (cleanedFilters.contains("*") || doAnyFiltersMatchPid(cleanedFilters, Process.myPid())) {
      final List<Thread> activeThreadList = new ArrayList<>();
      Collections.addAll(activeThreadList, activeThreads);
      if (!activeThreadList.contains(mainThread)) {
        activeThreadList.add(mainThread); // see below for why this is necessary.
      }
      return activeThreadList;
    }

    // We always want to profile the main thread. We're not certain getActiveThreads returns
    // all active threads since we've observed that getActiveThreads doesn't include the main thread
    // during xpcshell tests even though it's alive (bug 1760716). We intentionally don't rely on
    // that method to add the main thread here.
    final List<Thread> threadsToProfile = new ArrayList<>();
    threadsToProfile.add(mainThread);

    for (final Thread thread : activeThreads) {
      if (shouldProfileThread(thread, cleanedFilters, mainThread)) {
        threadsToProfile.add(thread);
      }
    }
    return threadsToProfile;
  }

  private static boolean shouldProfileThread(
      final Thread aThread, final List<String> aFilters, final Thread aMainThread) {
    final String threadName = aThread.getName().trim().toLowerCase(Locale.US);
    if (threadName.isEmpty()) {
      return false; // We can't match against a thread with no name: skip.
    }

    if (aThread.equals(aMainThread)) {
      return false; // We've already added the main thread outside of this method.
    }

    for (final String filter : aFilters) {
      // In order to generically support thread pools with thread names like "arch_disk_io_0" (the
      // kotlin IO dispatcher), we check if the filter is inside the thread name (e.g. a filter of
      // "io" will match all of the threads in that pool) rather than an equality check.
      if (threadName.contains(filter)) {
        return true;
      }
    }

    return false;
  }

  private static boolean doAnyFiltersMatchPid(
      @NonNull final List<String> aFilters, final long aPid) {
    final String prefix = "pid:";
    for (final String filter : aFilters) {
      if (!filter.startsWith(prefix)) {
        continue;
      }

      try {
        final long filterPid = Long.parseLong(filter.substring(prefix.length()));
        if (filterPid == aPid) {
          return true;
        }
      } catch (final NumberFormatException e) {
        /* do nothing. */
      }
    }

    return false;
  }

  private static @NonNull Thread[] getActiveThreads(final @NonNull ThreadGroup rootThreadGroup) {
    // We need the root thread group to get all of the active threads because of how
    // ThreadGroup.enumerate works.
    //
    // ThreadGroup.enumerate is inherently racey so we loop until we capture all of the active
    // threads. We can only detect if we didn't capture all of the threads if the number of threads
    // found (the value returned by enumerate) is smaller than the array we're capturing them in.
    // Therefore, we make the array slightly larger than the known number of threads.
    Thread[] allThreads;
    int threadsFound;
    do {
      allThreads = new Thread[rootThreadGroup.activeCount() + 15];
      threadsFound = rootThreadGroup.enumerate(allThreads, /* recurse */ true);
    } while (threadsFound >= allThreads.length);

    // There will be more indices in the array than threads and these will be set to null. We remove
    // the null values to minimize bugs.
    return Arrays.copyOfRange(allThreads, 0, threadsFound);
  }

  private static @NonNull ThreadGroup getRootThreadGroup() {
    // Assert non-null: getThreadGroup only returns null for dead threads but the current thread
    // can't be dead.
    ThreadGroup parentGroup = Objects.requireNonNull(Thread.currentThread().getThreadGroup());

    ThreadGroup group = null;
    while (parentGroup != null) {
      group = parentGroup;
      parentGroup = group.getParent();
    }
    return group;
  }

  @WrapForJNI
  public static void pauseSampling() {
    synchronized (GeckoJavaSampler.class) {
      final ScheduledFuture<?> future = sSamplingFuture.getAndSet(null);
      future.cancel(false /* mayInterruptIfRunning */);
    }
  }

  @SuppressLint("DiscouragedApi")
  @WrapForJNI
  public static void unpauseSampling() {
    synchronized (GeckoJavaSampler.class) {
      if (sSamplingFuture.get() != null) {
        return;
      }
      sSamplingFuture.set(
          sSamplingScheduler.scheduleAtFixedRate(
              sSamplingRunnable, 0, sSamplingRunnable.mInterval, TimeUnit.MILLISECONDS));
    }
  }

  @WrapForJNI
  public static void stop() {
    synchronized (GeckoJavaSampler.class) {
      if (sSamplingRunnable == null) {
        return;
      }

      Log.i(
          LOGTAG,
          "Profiler stopping. Sample array position: "
              + sSamplingRunnable.mSamplePos
              + ". Overflowed? "
              + sSamplingRunnable.mBufferOverflowed);

      try {
        sSamplingScheduler.shutdown();
        // 1s is enough to wait shutdown.
        sSamplingScheduler.awaitTermination(1000, TimeUnit.MILLISECONDS);
      } catch (final InterruptedException e) {
        Log.e(LOGTAG, "Sampling scheduler isn't terminated. Last sampling data might be broken.");
        sSamplingScheduler.shutdownNow();
      }
      sSamplingScheduler = null;
      sSamplingRunnable = null;
      sSamplingFuture.set(null);
      sMarkerStorage.stop();
    }
  }

  /**
   * Notifies Fenix layer about profiler state changes by broadcasting the new state. This is called
   * from native code whenever the profiler starts or stops, ensuring that the Fenix repository is
   * always synchronized with the actual native profiler state.
   *
   * @param isActive true if the profiler is now active, false if it stopped
   */
  @WrapForJNI
  public static void notifyProfilerStateChanged(final boolean isActive) {
    if (isActive) {
      final ComponentName componentName =
          new ComponentName(GeckoAppShell.getApplicationContext(), PROFILER_SERVICE_CLASS_NAME);
      final Intent serviceIntent = new Intent();
      serviceIntent.setComponent(componentName);
      serviceIntent.setAction(PROFILER_SERVICE_ACTION);
      ContextCompat.startForegroundService(GeckoAppShell.getApplicationContext(), serviceIntent);
    }

    final Intent intent = new Intent(INTENT_PROFILER_STATE_CHANGED);
    intent.putExtra("isActive", isActive);
    intent.setPackage(GeckoAppShell.getApplicationContext().getPackageName());
    final String permission =
        GeckoAppShell.getApplicationContext().getPackageName() + ".permission.PROFILER_INTERNAL";
    GeckoAppShell.getApplicationContext().sendBroadcast(intent, permission);
  }

  @WrapForJNI(dispatchTo = "gecko", stubName = "StartProfiler")
  private static native void startProfilerNative(String[] aFilters, String[] aFeaturesArr);

  @WrapForJNI(dispatchTo = "gecko", stubName = "StopProfiler")
  private static native void stopProfilerNative(GeckoResult<byte[]> aResult);

  public static void startProfiler(final String[] aFilters, final String[] aFeaturesArr) {
    startProfilerNative(aFilters, aFeaturesArr);
  }

  public static GeckoResult<byte[]> stopProfiler() {
    final GeckoResult<byte[]> result = new GeckoResult<byte[]>();
    stopProfilerNative(result);
    return result;
  }

  /** Returns the device brand and model as a string. */
  @WrapForJNI
  public static String getDeviceInformation() {
    final StringBuilder sb = new StringBuilder(Build.BRAND);
    sb.append(" ");
    sb.append(Build.MODEL);
    return sb.toString();
  }

  private static volatile String sProguardUuid;

  /**
   * Sets the UUID of the embedding app's R8/ProGuard mapping file, recorded in profiles.
   *
   * @param proguardUuid the mapping file UUID
   */
  public static void setProguardUuid(final String proguardUuid) {
    sProguardUuid = proguardUuid;
  }

  /** Returns the UUID of the embedding app's R8/ProGuard mapping file, or null if unknown. */
  @WrapForJNI
  public static String getProguardUuid() {
    return sProguardUuid;
  }
}
