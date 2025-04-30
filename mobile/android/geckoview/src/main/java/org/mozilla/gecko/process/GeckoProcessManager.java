/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.process;

import android.os.DeadObjectException;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;
import androidx.annotation.NonNull;
import androidx.collection.ArrayMap;
import androidx.collection.ArraySet;
import androidx.collection.SimpleArrayMap;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.GeckoNetworkManager;
import org.mozilla.gecko.GeckoThread;
import org.mozilla.gecko.IGeckoEditableChild;
import org.mozilla.gecko.IGeckoEditableParent;
import org.mozilla.gecko.TelemetryUtils;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.gfx.CompositorSurfaceManager;
import org.mozilla.gecko.gfx.ISurfaceAllocator;
import org.mozilla.gecko.gfx.RemoteSurfaceAllocator;
import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.gecko.process.ServiceAllocator.PriorityLevel;
import org.mozilla.gecko.util.ThreadUtils;
import org.mozilla.gecko.util.XPCOMEventTarget;
import org.mozilla.geckoview.GeckoResult;

public final class GeckoProcessManager extends IProcessManager.Stub {
  private static final String LOGTAG = "GeckoProcessManager";
  private static final GeckoProcessManager INSTANCE = new GeckoProcessManager();
  private static final int INVALID_PID = 0;

  // This id univocally identifies the current process manager instance
  private final String mInstanceId;

  public static GeckoProcessManager getInstance() {
    return INSTANCE;
  }

  @WrapForJNI(calledFrom = "gecko")
  private static void setEditableChildParent(
      final IGeckoEditableChild child, final IGeckoEditableParent parent) {
    try {
      child.transferParent(parent);
    } catch (final RemoteException e) {
      Log.e(LOGTAG, "Cannot set parent", e);
    }
  }

  @WrapForJNI(stubName = "GetEditableParent", dispatchTo = "gecko")
  private static native void nativeGetEditableParent(
      IGeckoEditableChild child, long contentId, long tabId);

  @Override // IProcessManager
  public void getEditableParent(
      final IGeckoEditableChild child, final long contentId, final long tabId) {
    nativeGetEditableParent(child, contentId, tabId);
  }

  /**
   * Returns the surface allocator interface to be used by child processes to allocate Surfaces. The
   * service bound to the returned interface may live in either the GPU process or parent process.
   */
  @Override // IProcessManager
  public ISurfaceAllocator getSurfaceAllocator() {
    final boolean gpuEnabled = GeckoAppShell.isGpuProcessEnabled();

    try {
      final GeckoResult<ISurfaceAllocator> allocator = new GeckoResult<>();
      if (gpuEnabled) {
        // The GPU process is enabled, so look it up and ask it for its surface allocator.
        XPCOMEventTarget.runOnLauncherThread(
            () -> {
              final Selector selector = new Selector(GeckoProcessType.GPU);
              final GpuProcessConnection conn =
                  (GpuProcessConnection) INSTANCE.mConnections.getExistingConnection(selector);
              if (conn != null) {
                allocator.complete(conn.getSurfaceAllocator());
              } else {
                // If we cannot find a GPU process, it has probably been killed and not yet
                // restarted. Return null here, and allow the caller to try again later.
                // We definitely do *not* want to return the parent process allocator instead, as
                // that will result in surfaces being allocated in the parent process, which
                // therefore won't be usable when the GPU process is eventually launched.
                allocator.complete(null);
              }
            });
      } else {
        // The GPU process is disabled, so return the parent process allocator instance.
        allocator.complete(RemoteSurfaceAllocator.getInstance(0));
      }
      return allocator.poll(100);
    } catch (final Throwable e) {
      Log.e(LOGTAG, "Error in getSurfaceAllocator", e);
      return null;
    }
  }

  @WrapForJNI
  public static CompositorSurfaceManager getCompositorSurfaceManager() {
    final Selector selector = new Selector(GeckoProcessType.GPU);
    final GpuProcessConnection conn =
        (GpuProcessConnection) INSTANCE.mConnections.getExistingConnection(selector);
    if (conn == null) {
      return null;
    }
    return conn.getCompositorSurfaceManager();
  }

  /** Gecko uses this class to uniquely identify a process managed by GeckoProcessManager. */
  public static final class Selector {
    private final GeckoProcessType mType;
    private final int mPid;

    @WrapForJNI
    private Selector(@NonNull final GeckoProcessType type, final int pid) {
      if (pid == INVALID_PID) {
        throw new RuntimeException("Invalid PID");
      }

      mType = type;
      mPid = pid;
    }

    @WrapForJNI
    private Selector(@NonNull final GeckoProcessType type) {
      mType = type;
      mPid = INVALID_PID;
    }

    public GeckoProcessType getType() {
      return mType;
    }

    public int getPid() {
      return mPid;
    }

    @Override
    public boolean equals(final Object obj) {
      if (obj == null) {
        return false;
      }

      if (obj == ((Object) this)) {
        return true;
      }

      final Selector other = (Selector) obj;
      return mType == other.mType && mPid == other.mPid;
    }

    @Override
    public int hashCode() {
      return Arrays.hashCode(new Object[] {mType, mPid});
    }
  }

  private static final class IncompleteChildConnectionException extends RuntimeException {
    public IncompleteChildConnectionException(@NonNull final String msg) {
      super(msg);
    }
  }

  /**
   * Maintains state pertaining to an individual child process. Inheriting from
   * ServiceAllocator.InstanceInfo enables this class to work with ServiceAllocator.
   */
  private static class ChildConnection extends ServiceAllocator.InstanceInfo {
    private IChildProcess mChild;
    private GeckoResult<IChildProcess> mPendingBind;
    private int mPid;

    protected ChildConnection(
        @NonNull final ServiceAllocator allocator,
        @NonNull final GeckoProcessType type,
        @NonNull final PriorityLevel initialPriority) {
      super(allocator, type, initialPriority);
      mPid = INVALID_PID;
    }

    public int getPid() {
      XPCOMEventTarget.assertOnLauncherThread();
      if (mChild == null) {
        throw new IncompleteChildConnectionException(
            "Calling ChildConnection.getPid() on an incomplete connection");
      }

      return mPid;
    }

    protected IChildProcess getChild() {
      return mChild;
    }

    private GeckoResult<IChildProcess> completeFailedBind(
        @NonNull final ServiceAllocator.BindException e) {
      XPCOMEventTarget.assertOnLauncherThread();
      Log.e(LOGTAG, "Failed bind", e);

      if (mPendingBind == null) {
        throw new IllegalStateException("Bind failed with null mPendingBind");
      }

      final GeckoResult<IChildProcess> bindResult = mPendingBind;
      mPendingBind = null;
      unbind().accept(v -> bindResult.completeExceptionally(e));
      return bindResult;
    }

    public GeckoResult<IChildProcess> bind() {
      XPCOMEventTarget.assertOnLauncherThread();

      if (mChild != null) {
        // Already bound
        return GeckoResult.fromValue(mChild);
      }

      if (mPendingBind != null) {
        // Bind in progress
        return mPendingBind;
      }

      mPendingBind = new GeckoResult<>();
      try {
        if (!bindService()) {
          throw new ServiceAllocator.BindException("Cannot connect to process");
        }
      } catch (final ServiceAllocator.BindException e) {
        return completeFailedBind(e);
      }

      return mPendingBind;
    }

    public GeckoResult<Void> unbind() {
      XPCOMEventTarget.assertOnLauncherThread();

      if (mPendingBind != null) {
        // We called unbind() while bind() was still pending completion
        return mPendingBind.then(child -> unbind());
      }

      if (mChild == null) {
        // Not bound in the first place
        return GeckoResult.fromValue(null);
      }

      unbindService();

      return GeckoResult.fromValue(null);
    }

    @Override
    protected void onBinderConnected(final IBinder service) {
      XPCOMEventTarget.assertOnLauncherThread();

      final IChildProcess child = IChildProcess.Stub.asInterface(service);
      try {
        mPid = child.getPid();
      } catch (final DeadObjectException e) {
        unbindService();

        // mPendingBind might be null if a bind was initiated by the system (eg Service Restart)
        if (mPendingBind != null) {
          mPendingBind.completeExceptionally(e);
          mPendingBind = null;
        }

        return;
      } catch (final RemoteException e) {
        throw new RuntimeException(e);
      }

      mChild = child;
      GeckoProcessManager.INSTANCE.mConnections.onBindComplete(this);

      // mPendingBind might be null if a bind was initiated by the system (eg Service Restart)
      if (mPendingBind != null) {
        mPendingBind.complete(mChild);
        mPendingBind = null;
      }
    }

    @Override
    protected void onReleaseResources() {
      XPCOMEventTarget.assertOnLauncherThread();

      // NB: This must happen *before* resetting mPid!
      GeckoProcessManager.INSTANCE.mConnections.removeConnection(this);

      mChild = null;
      mPid = INVALID_PID;
    }
  }

  private static class NonContentConnection extends ChildConnection {
    public NonContentConnection(
        @NonNull final ServiceAllocator allocator, @NonNull final GeckoProcessType type) {
      super(allocator, type, PriorityLevel.FOREGROUND);
      if (type == GeckoProcessType.CONTENT) {
        throw new AssertionError("Attempt to create a NonContentConnection as CONTENT");
      }
    }

    protected void onAppForeground() {
      setPriorityLevel(PriorityLevel.FOREGROUND);
    }

    protected void onAppBackground() {
      setPriorityLevel(PriorityLevel.IDLE);
    }
  }

  private static final class GpuProcessConnection extends NonContentConnection {
    private CompositorSurfaceManager mCompositorSurfaceManager;
    private ISurfaceAllocator mSurfaceAllocator;

    // Unique ID used to identify each GPU process instance. Will always be non-zero,
    // and unlike the process' pid cannot be the same value for successive instances.
    private final int mUniqueGpuProcessId;
    // Static counter used to initialize each instance's mUniqueGpuProcessId
    private static int sUniqueGpuProcessIdCounter = 0;

    public GpuProcessConnection(@NonNull final ServiceAllocator allocator) {
      super(allocator, GeckoProcessType.GPU);

      // Initialize the unique ID ensuring we skip 0 (as that is reserved for parent process
      // allocators).
      if (sUniqueGpuProcessIdCounter == 0) {
        sUniqueGpuProcessIdCounter++;
      }
      mUniqueGpuProcessId = sUniqueGpuProcessIdCounter++;
    }

    public CompositorSurfaceManager getCompositorSurfaceManager() {
      if (mCompositorSurfaceManager == null && getChild() != null) {
        try {
          mCompositorSurfaceManager =
              new CompositorSurfaceManager(getChild().getCompositorSurfaceManager());
        } catch (final RemoteException ignored) {
        }
      }
      return mCompositorSurfaceManager;
    }

    public ISurfaceAllocator getSurfaceAllocator() {
      if (mSurfaceAllocator == null && getChild() != null) {
        try {
          mSurfaceAllocator = getChild().getSurfaceAllocator(mUniqueGpuProcessId);
        } catch (final RemoteException ignored) {
        }
      }
      return mSurfaceAllocator;
    }
  }

  private static final class SocketProcessConnection extends NonContentConnection {
    private boolean mIsForeground = true;
    private boolean mIsNetworkUp = true;

    public SocketProcessConnection(@NonNull final ServiceAllocator allocator) {
      super(allocator, GeckoProcessType.SOCKET);
      GeckoProcessManager.INSTANCE.mConnections.enableNetworkNotifications();
    }

    public void onNetworkStateChange(final boolean isNetworkUp) {
      mIsNetworkUp = isNetworkUp;
      prioritize();
    }

    @Override
    protected void onAppForeground() {
      mIsForeground = true;
      prioritize();
    }

    @Override
    protected void onAppBackground() {
      mIsForeground = false;
      prioritize();
    }

    private static final PriorityLevel[][] sPriorityStates = initPriorityStates();

    private static PriorityLevel[][] initPriorityStates() {
      final PriorityLevel[][] states = new PriorityLevel[2][2];
      // Background, no network
      states[0][0] = PriorityLevel.IDLE;
      // Background, network
      states[0][1] = PriorityLevel.BACKGROUND;
      // Foreground, no network
      states[1][0] = PriorityLevel.IDLE;
      // Foreground, network
      states[1][1] = PriorityLevel.FOREGROUND;
      return states;
    }

    private void prioritize() {
      final PriorityLevel nextPriority =
          sPriorityStates[mIsForeground ? 1 : 0][mIsNetworkUp ? 1 : 0];
      setPriorityLevel(nextPriority);
    }
  }

  private static final class ContentConnection extends ChildConnection {
    private static final String TELEMETRY_PROCESS_LIFETIME_HISTOGRAM_NAME =
        "GV_CONTENT_PROCESS_LIFETIME_MS";

    private TelemetryUtils.UptimeTimer mLifetimeTimer = null;

    public ContentConnection(
        @NonNull final ServiceAllocator allocator, @NonNull final PriorityLevel initialPriority) {
      super(allocator, GeckoProcessType.CONTENT, initialPriority);
    }

    @Override
    protected void onBinderConnected(final IBinder service) {
      mLifetimeTimer = new TelemetryUtils.UptimeTimer(TELEMETRY_PROCESS_LIFETIME_HISTOGRAM_NAME);
      super.onBinderConnected(service);
    }

    @Override
    protected void onReleaseResources() {
      if (mLifetimeTimer != null) {
        mLifetimeTimer.stop();
        mLifetimeTimer = null;
      }

      super.onReleaseResources();
    }
  }

  /** This class manages the state surrounding existing connections and their priorities. */
  private static final class ConnectionManager extends JNIObject {
    // Connections to non-content processes
    private final ArrayMap<GeckoProcessType, NonContentConnection> mNonContentConnections;
    // Mapping of pid to content process
    private final SimpleArrayMap<Integer, ContentConnection> mContentPids;
    // Set of initialized content process connections
    private final ArraySet<ContentConnection> mContentConnections;
    // Set of bound but uninitialized content connections
    private final ArraySet<ContentConnection> mNonStartedContentConnections;
    // Allocator for service IDs
    private final ServiceAllocator mServiceAllocator;
    private boolean mIsObservingNetwork = false;

    public ConnectionManager() {
      mNonContentConnections = new ArrayMap<GeckoProcessType, NonContentConnection>();
      mContentPids = new SimpleArrayMap<Integer, ContentConnection>();
      mContentConnections = new ArraySet<ContentConnection>();
      mNonStartedContentConnections = new ArraySet<ContentConnection>();
      mServiceAllocator = new ServiceAllocator();

      // Attach to native once JNI is ready.
      if (GeckoThread.isStateAtLeast(GeckoThread.State.JNI_READY)) {
        attachTo(this);
      } else {
        GeckoThread.queueNativeCallUntil(
            GeckoThread.State.JNI_READY, ConnectionManager.class, "attachTo", this);
      }
    }

    private void enableNetworkNotifications() {
      if (mIsObservingNetwork) {
        return;
      }

      mIsObservingNetwork = true;

      // Ensure that GeckoNetworkManager is monitoring network events so that we can
      // prioritize the socket process.
      ThreadUtils.runOnUiThread(
          () -> {
            GeckoNetworkManager.getInstance().enableNotifications();
          });

      observeNetworkNotifications();
    }

    @WrapForJNI(dispatchTo = "gecko")
    private static native void attachTo(ConnectionManager instance);

    @WrapForJNI(dispatchTo = "gecko")
    private native void observeNetworkNotifications();

    @WrapForJNI(calledFrom = "gecko")
    private void onBackground() {
      XPCOMEventTarget.runOnLauncherThread(() -> onAppBackgroundInternal());
    }

    @WrapForJNI(calledFrom = "gecko")
    private void onForeground() {
      XPCOMEventTarget.runOnLauncherThread(() -> onAppForegroundInternal());
    }

    @WrapForJNI(calledFrom = "gecko")
    private void onNetworkStateChange(final boolean isUp) {
      XPCOMEventTarget.runOnLauncherThread(() -> onNetworkStateChangeInternal(isUp));
    }

    @Override
    protected native void disposeNative();

    private void onAppBackgroundInternal() {
      XPCOMEventTarget.assertOnLauncherThread();

      for (final NonContentConnection conn : mNonContentConnections.values()) {
        conn.onAppBackground();
      }
    }

    private void onAppForegroundInternal() {
      XPCOMEventTarget.assertOnLauncherThread();

      for (final NonContentConnection conn : mNonContentConnections.values()) {
        conn.onAppForeground();
      }
    }

    private void onNetworkStateChangeInternal(final boolean isUp) {
      XPCOMEventTarget.assertOnLauncherThread();

      final SocketProcessConnection conn =
          (SocketProcessConnection) mNonContentConnections.get(GeckoProcessType.SOCKET);
      if (conn == null) {
        return;
      }

      conn.onNetworkStateChange(isUp);
    }

    private void removeContentConnection(@NonNull final ChildConnection conn) {
      if (!mContentConnections.remove(conn)) {
        throw new RuntimeException("Attempt to remove non-registered connection");
      }
      mNonStartedContentConnections.remove(conn);

      final int pid;

      try {
        pid = conn.getPid();
      } catch (final IncompleteChildConnectionException e) {
        // conn lost its binding before it was able to retrieve its pid. It follows that
        // mContentPids does not have an entry for this connection, so we can just return.
        return;
      }

      if (pid == INVALID_PID) {
        return;
      }

      final ChildConnection removed = mContentPids.remove(Integer.valueOf(pid));
      if (removed != null && removed != conn) {
        throw new RuntimeException(
            "Integrity error - connection mismatch for pid " + Integer.toString(pid));
      }
    }

    public void removeConnection(@NonNull final ChildConnection conn) {
      XPCOMEventTarget.assertOnLauncherThread();

      if (conn.getType() == GeckoProcessType.CONTENT) {
        removeContentConnection(conn);
        return;
      }

      final ChildConnection removed = mNonContentConnections.remove(conn.getType());
      if (removed != conn) {
        throw new RuntimeException(
            "Integrity error - connection mismatch for process type " + conn.getType().toString());
      }
    }

    /** Saves any state information that was acquired upon start completion. */
    public void onBindComplete(@NonNull final ChildConnection conn) {
      if (conn.getType() == GeckoProcessType.CONTENT) {
        final int pid = conn.getPid();
        if (pid == INVALID_PID) {
          throw new AssertionError(
              "PID is invalid even though our caller just successfully retrieved it after binding");
        }

        mContentPids.put(pid, (ContentConnection) conn);
      }
    }

    /** Retrieve the ChildConnection for an already running content process. */
    private ContentConnection getExistingContentConnection(@NonNull final Selector selector) {
      XPCOMEventTarget.assertOnLauncherThread();
      if (selector.getType() != GeckoProcessType.CONTENT) {
        throw new IllegalArgumentException("Selector is not for content!");
      }

      return mContentPids.get(selector.getPid());
    }

    /** Unconditionally create a new content connection for the specified priority. */
    private ContentConnection getNewContentConnection(@NonNull final PriorityLevel newPriority) {
      final ContentConnection result = new ContentConnection(mServiceAllocator, newPriority);
      mContentConnections.add(result);

      return result;
    }

    /** Retrieve the ChildConnection for an already running child process of any type. */
    public ChildConnection getExistingConnection(@NonNull final Selector selector) {
      XPCOMEventTarget.assertOnLauncherThread();

      final GeckoProcessType type = selector.getType();

      if (type == GeckoProcessType.CONTENT) {
        return getExistingContentConnection(selector);
      }

      return mNonContentConnections.get(type);
    }

    /**
     * Retrieve a ChildConnection for a content process for the purposes of starting. If there are
     * any preloaded content processes already running, we will use one of those. Otherwise we will
     * allocate a new ChildConnection.
     */
    private ChildConnection getContentConnectionForStart() {
      XPCOMEventTarget.assertOnLauncherThread();

      if (mNonStartedContentConnections.isEmpty()) {
        return getNewContentConnection(PriorityLevel.FOREGROUND);
      }

      final ChildConnection conn =
          mNonStartedContentConnections.removeAt(mNonStartedContentConnections.size() - 1);
      conn.setPriorityLevel(PriorityLevel.FOREGROUND);
      return conn;
    }

    /** Retrieve or create a new child process for the specified non-content process. */
    private ChildConnection getNonContentConnection(@NonNull final GeckoProcessType type) {
      XPCOMEventTarget.assertOnLauncherThread();
      if (type == GeckoProcessType.CONTENT) {
        throw new IllegalArgumentException("Content processes not supported by this method");
      }

      NonContentConnection connection = mNonContentConnections.get(type);
      if (connection == null) {
        if (type == GeckoProcessType.SOCKET) {
          connection = new SocketProcessConnection(mServiceAllocator);
        } else if (type == GeckoProcessType.GPU) {
          connection = new GpuProcessConnection(mServiceAllocator);
        } else {
          connection = new NonContentConnection(mServiceAllocator, type);
        }

        mNonContentConnections.put(type, connection);
      }

      return connection;
    }

    /** Retrieve a ChildConnection for the purposes of starting a new child process. */
    public ChildConnection getConnectionForStart(@NonNull final GeckoProcessType type) {
      if (type == GeckoProcessType.CONTENT) {
        return getContentConnectionForStart();
      }

      return getNonContentConnection(type);
    }

    /** Retrieve a ChildConnection for the purposes of preloading a new child process. */
    public ChildConnection getConnectionForPreload(@NonNull final GeckoProcessType type) {
      if (type == GeckoProcessType.CONTENT) {
        final ContentConnection conn = getNewContentConnection(PriorityLevel.BACKGROUND);
        mNonStartedContentConnections.add(conn);
        return conn;
      }

      return getNonContentConnection(type);
    }
  }

  private final ConnectionManager mConnections;

  private GeckoProcessManager() {
    mConnections = new ConnectionManager();
    mInstanceId = UUID.randomUUID().toString();
  }

  public void preload(final GeckoProcessType... types) {
    XPCOMEventTarget.launcherThread()
        .execute(
            () -> {
              for (final GeckoProcessType type : types) {
                final ChildConnection connection = mConnections.getConnectionForPreload(type);
                connection.bind();
              }
            });
  }

  public void crashChild(@NonNull final Selector selector) {
    XPCOMEventTarget.launcherThread()
        .execute(
            () -> {
              final ChildConnection conn = mConnections.getExistingConnection(selector);
              if (conn == null) {
                return;
              }

              conn.bind()
                  .accept(
                      proc -> {
                        try {
                          proc.crash();
                        } catch (final RemoteException e) {
                        }
                      });
            });
  }

  @WrapForJNI
  private static void shutdownProcess(final Selector selector) {
    XPCOMEventTarget.assertOnLauncherThread();
    final ChildConnection conn = INSTANCE.mConnections.getExistingConnection(selector);
    if (conn == null) {
      return;
    }

    conn.unbind();
  }

  @WrapForJNI
  private static void setProcessPriority(
      @NonNull final Selector selector,
      @NonNull final PriorityLevel priorityLevel,
      final int relativeImportance) {
    XPCOMEventTarget.runOnLauncherThread(
        () -> {
          final ChildConnection conn = INSTANCE.mConnections.getExistingConnection(selector);
          if (conn == null) {
            return;
          }

          conn.setPriorityLevel(priorityLevel, relativeImportance);
        });
  }

  @WrapForJNI
  private static GeckoResult<Integer> start(
      final GeckoProcessType type, final String[] args, final int[] fds) {
    final GeckoResult<Integer> result = new GeckoResult<>();
    final StartInfo info =
        new StartInfo(
            type,
            GeckoThread.InitInfo.builder()
                .args(args)
                .userSerialNumber(System.getenv("MOZ_ANDROID_USER_SERIAL_NUMBER"))
                .extras(GeckoThread.getActiveExtras())
                .flags(
                    filterFlagsForChild(
                        GeckoThread.getActiveFlags(), type != GeckoProcessType.PARENT))
                .fds(fds)
                .build());

    XPCOMEventTarget.runOnLauncherThread(
        () -> {
          INSTANCE
              .start(info)
              .accept(result::complete, result::completeExceptionally)
              .finally_(info::cleanup);
        });

    return result;
  }

  private static int filterFlagsForChild(final int flags, final boolean child) {
    return (flags & GeckoThread.FLAG_ENABLE_NATIVE_CRASHREPORTER)
        | (child ? GeckoThread.FLAG_CHILD : 0);
  }

  private static class StartInfo {
    final GeckoProcessType type;
    final String crashHandler;
    final GeckoThread.InitInfo init;

    final ParcelFileDescriptor[] pfds;

    private StartInfo(final GeckoProcessType type, final GeckoThread.InitInfo initInfo) {
      this.type = type;
      this.init = initInfo;
      crashHandler =
          GeckoAppShell.getCrashHandlerService() != null
              ? GeckoAppShell.getCrashHandlerService().getName()
              : null;

      // The native side owns the File Descriptors so we cannot call adopt here.
      pfds = new ParcelFileDescriptor[initInfo.fds.length];
      for (int i = 0; i < initInfo.fds.length; ++i) {
        try {
          pfds[i] = ParcelFileDescriptor.fromFd(initInfo.fds[i]);
        } catch (final IOException ex) {
          throw new RuntimeException(ex);
        }
      }
    }

    private void cleanup() {
      for (final ParcelFileDescriptor pfd : pfds) {
        try {
          pfd.close();
        } catch (final IOException ex) {
          Log.d(LOGTAG, "Failed to close fd", ex);
        }
      }
    }
  }

  private static final int MAX_RETRIES = 3;

  private GeckoResult<Integer> start(final StartInfo info) {
    return start(info, new ArrayList<>());
  }

  private GeckoResult<Integer> retry(
      final StartInfo info, final List<Throwable> retryLog, final Throwable error) {
    retryLog.add(error);

    if (error instanceof StartException) {
      final StartException startError = (StartException) error;
      if (startError.errorCode == IChildProcess.STARTED_BUSY) {
        // This process is owned by a different runtime, so we can't use
        // it. We will keep retrying indefinitely until we find a non-busy process.
        // Note: this strategy is pretty bad, we go through each process in
        // sequence until one works, the multiple runtime case is test-only
        // for now, so that's ok. We can improve on this if we eventually
        // end up needing something fancier.
        return start(info, retryLog);
      }
    }

    // If we couldn't unbind there's something very wrong going on and we bail
    // immediately.
    if (retryLog.size() >= MAX_RETRIES || error instanceof UnbindException) {
      return GeckoResult.fromException(fromRetryLog(retryLog));
    }

    return start(info, retryLog);
  }

  private String serializeLog(final List<Throwable> retryLog) {
    if (retryLog == null || retryLog.size() == 0) {
      return "Empty log.";
    }

    final StringBuilder message = new StringBuilder();

    for (final Throwable error : retryLog) {
      if (error instanceof UnbindException) {
        message.append("Could not unbind: ");
      } else if (error instanceof StartException) {
        message.append("Cannot restart child: ");
      } else {
        message.append("Error while binding: ");
      }
      message.append(error);
      message.append(";");
    }

    return message.toString();
  }

  private RuntimeException fromRetryLog(final List<Throwable> retryLog) {
    return new RuntimeException(serializeLog(retryLog), retryLog.get(retryLog.size() - 1));
  }

  private GeckoResult<Integer> start(final StartInfo info, final List<Throwable> retryLog) {
    return startInternal(info).then(GeckoResult::fromValue, error -> retry(info, retryLog, error));
  }

  private static class StartException extends RuntimeException {
    public final int errorCode;

    public StartException(final int errorCode, final int pid) {
      super("Could not start process, errorCode: " + errorCode + " PID: " + pid);
      this.errorCode = errorCode;
    }
  }

  private GeckoResult<Integer> startInternal(final StartInfo info) {
    XPCOMEventTarget.assertOnLauncherThread();

    final ChildConnection connection = mConnections.getConnectionForStart(info.type);
    return connection
        .bind()
        .map(
            child -> {
              final int result =
                  child.start(
                      this,
                      mInstanceId,
                      info.init.args,
                      info.init.extras,
                      info.init.flags,
                      info.init.userSerialNumber,
                      info.crashHandler,
                      info.pfds);
              if (result == IChildProcess.STARTED_OK) {
                return connection.getPid();
              } else {
                throw new StartException(result, connection.getPid());
              }
            })
        .then(GeckoResult::fromValue, error -> handleBindError(connection, error));
  }

  private GeckoResult<Integer> handleBindError(
      final ChildConnection connection, final Throwable error) {
    return connection
        .unbind()
        .then(
            unused -> GeckoResult.fromException(error),
            unbindError -> GeckoResult.fromException(new UnbindException(unbindError)));
  }

  private static class UnbindException extends RuntimeException {
    public UnbindException(final Throwable cause) {
      super(cause);
    }
  }
} // GeckoProcessManager
