/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.process;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;
import androidx.annotation.ChecksSdkIntAtLeast;
import androidx.annotation.NonNull;
import androidx.annotation.RequiresApi;
import java.security.SecureRandom;
import java.util.BitSet;
import java.util.EnumMap;
import java.util.HashSet;
import java.util.Map.Entry;
import java.util.Set;
import java.util.UUID;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.util.XPCOMEventTarget;

/* package */ final class ServiceAllocator {
  private static final String LOGTAG = "ServiceAllocator";
  private static final int MAX_NUM_ISOLATED_CONTENT_SERVICES =
      GeckoChildProcessServices.MAX_NUM_ISOLATED_CONTENT_SERVICES;

  @ChecksSdkIntAtLeast(api = Build.VERSION_CODES.Q)
  private static boolean hasQApis() {
    return Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q;
  }

  /**
   * Possible priority levels that are available to child services. Each one maps to a flag that is
   * passed into Context.bindService().
   */
  @WrapForJNI
  public enum PriorityLevel {
    FOREGROUND(Context.BIND_IMPORTANT),
    BACKGROUND(0),
    IDLE(Context.BIND_WAIVE_PRIORITY);

    private final int mAndroidFlag;

    PriorityLevel(final int androidFlag) {
      mAndroidFlag = androidFlag;
    }

    public int getAndroidFlag() {
      return mAndroidFlag;
    }
  }

  public static final class BindException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    public BindException(@NonNull final String msg) {
      super(msg);
    }
  }

  private interface BindServiceDelegate {
    boolean bindService(ServiceConnection binding, PriorityLevel priority);

    String getServiceName();
  }

  /**
   * Abstract class that holds the essential per-service data that is required to work with
   * ServiceAllocator. ServiceAllocator clients should extend this class when implementing their
   * per-service connection objects.
   */
  public abstract static class InstanceInfo {
    abstract int getPid() throws Exception;

    private class Binding implements ServiceConnection {
      /**
       * This implementation of ServiceConnection.onServiceConnected simply bounces the connection
       * notification over to the launcher thread (if it is not already on it).
       */
      @Override
      public final void onServiceConnected(final ComponentName name, final IBinder service) {
        XPCOMEventTarget.runOnLauncherThread(
            () -> {
              onBinderConnectedInternal(service);
            });
      }

      /**
       * This implementation of ServiceConnection.onServiceDisconnected simply bounces the
       * disconnection notification over to the launcher thread (if it is not already on it).
       */
      @Override
      public final void onServiceDisconnected(final ComponentName name) {
        XPCOMEventTarget.runOnLauncherThread(
            () -> {
              onBinderConnectionLostInternal();
            });
      }
    }

    private class DefaultBindDelegate implements BindServiceDelegate {
      @Override
      public boolean bindService(
          @NonNull final ServiceConnection binding, @NonNull final PriorityLevel priority) {
        final Context context = GeckoAppShell.getApplicationContext();
        final Intent intent = new Intent();
        intent.setClassName(context, getServiceName());
        return bindServiceDefault(context, intent, binding, getAndroidFlags(priority));
      }

      @Override
      public String getServiceName() {
        return getSvcClassNameDefault(InstanceInfo.this);
      }
    }

    private class IsolatedGmpBindDelegate extends DefaultBindDelegate {
      @Override
      public String getServiceName() {
        return ServiceUtils.buildIsolatedGmpSvcName();
      }
    }

    @RequiresApi(Build.VERSION_CODES.Q)
    private class IsolatedBindDelegate implements BindServiceDelegate {
      @Override
      public boolean bindService(
          @NonNull final ServiceConnection binding, @NonNull final PriorityLevel priority) {
        final Context context = GeckoAppShell.getApplicationContext();
        final Intent intent = new Intent();
        intent.setClassName(context, getServiceName());
        return bindServiceIsolated(
            context, intent, getAndroidFlags(priority), getIdInternal(), binding);
      }

      @Override
      public String getServiceName() {
        return ServiceUtils.buildIsolatedSvcName(getType());
      }
    }

    private final ServiceAllocator mAllocator;
    private final GeckoProcessType mType;
    private final String mId;
    private final EnumMap<PriorityLevel, Binding> mBindings;
    private final BindServiceDelegate mBindDelegate;

    private boolean mCalledConnected = false;
    private boolean mCalledConnectionLost = false;
    private boolean mIsDefunct = false;

    private PriorityLevel mCurrentPriority;
    private int mRelativeImportance = 0;

    protected InstanceInfo(
        @NonNull final ServiceAllocator allocator,
        @NonNull final GeckoProcessType type,
        @NonNull final PriorityLevel initialPriority) {
      mAllocator = allocator;
      mType = type;
      mId = mAllocator.allocate(type);
      mBindings = new EnumMap<PriorityLevel, Binding>(PriorityLevel.class);
      mBindDelegate = getBindServiceDelegate();

      mCurrentPriority = initialPriority;
    }

    private BindServiceDelegate getBindServiceDelegate() {
      if (mType == GeckoProcessType.GMPLUGIN
          && GeckoProcessManager.getInstance().isIsolatedGmpEnabled()) {
        return this.new IsolatedGmpBindDelegate();
      }

      if (!isContent()) {
        // Non-content services just use default binding
        return this.new DefaultBindDelegate();
      }

      // Content services defer to the alloc policy
      return mAllocator.mContentAllocPolicy.getBindServiceDelegate(this);
    }

    public PriorityLevel getPriorityLevel() {
      XPCOMEventTarget.assertOnLauncherThread();
      return mCurrentPriority;
    }

    public boolean setPriorityLevel(@NonNull final PriorityLevel newPriority) {
      return setPriorityLevel(newPriority, 0);
    }

    public boolean setPriorityLevel(
        @NonNull final PriorityLevel newPriority, final int relativeImportance) {
      XPCOMEventTarget.assertOnLauncherThread();
      mCurrentPriority = newPriority;
      mRelativeImportance = relativeImportance;

      // If we haven't bound yet then we can just return
      if (mBindings.size() == 0) {
        return true;
      }

      // Otherwise we need to update our bindings
      return updateBindings();
    }

    /**
     * Only content services have unique IDs. This method throws if called for a non-content service
     * type.
     */
    public String getId() {
      if (mId == null) {
        throw new RuntimeException("This service does not have a unique id");
      }

      return mId;
    }

    /** This method is infallible and returns an empty string for non-content services. */
    private String getIdInternal() {
      return mId == null ? "" : mId;
    }

    public boolean isContent() {
      return mType == GeckoProcessType.CONTENT
          || mType == GeckoProcessType.CONTENT_ISOLATED
          || mType == GeckoProcessType.CONTENT_ISOLATED_WITH_ZYGOTE;
    }

    public GeckoProcessType getType() {
      return mType;
    }

    protected boolean bindService() {
      if (mIsDefunct) {
        final String errorMsg =
            "Attempt to bind a defunct InstanceInfo for " + mType + " child process";
        throw new BindException(errorMsg);
      }

      return updateBindings();
    }

    /**
     * Unbinds the service described by |this| and releases our unique ID. This method may safely be
     * called multiple times even if we are already defunct.
     */
    protected void unbindService() {
      XPCOMEventTarget.assertOnLauncherThread();

      // This could happen if a service death races with our attempt to shut it down.
      if (mIsDefunct) {
        return;
      }

      final Context context = GeckoAppShell.getApplicationContext();

      // Make a clone of mBindings to iterate over since we're going to mutate the original
      final EnumMap<PriorityLevel, Binding> cloned = mBindings.clone();
      for (final Entry<PriorityLevel, Binding> entry : cloned.entrySet()) {
        try {
          context.unbindService(entry.getValue());
        } catch (final IllegalArgumentException e) {
          // The binding was already dead. That's okay.
        }

        mBindings.remove(entry.getKey());
      }

      if (mBindings.size() != 0) {
        throw new IllegalStateException("Unable to release all bindings");
      }

      mIsDefunct = true;
      mAllocator.release(this);
      onReleaseResources();
    }

    private void onBinderConnectedInternal(@NonNull final IBinder service) {
      XPCOMEventTarget.assertOnLauncherThread();
      // We only care about the first time this is called; subsequent bindings can be ignored.
      if (mCalledConnected) {
        return;
      }

      mCalledConnected = true;

      onBinderConnected(service);
    }

    private void onBinderConnectionLostInternal() {
      XPCOMEventTarget.assertOnLauncherThread();
      // We only care about the first time this is called; subsequent connection errors can be
      // ignored.
      if (mCalledConnectionLost) {
        return;
      }

      mCalledConnectionLost = true;

      onBinderConnectionLost();
    }

    protected abstract void onBinderConnected(@NonNull final IBinder service);

    protected abstract void onReleaseResources();

    // Optionally overridable by subclasses, but this is a sane default
    protected void onBinderConnectionLost() {
      // The binding has lost its connection, but the binding itself might still be active.
      // Gecko itself will request a process restart, so here we attempt to unbind so that
      // Android does not try to automatically restart and reconnect the service.
      unbindService();
    }

    /**
     * This function relies on the fact that the PriorityLevel enum is ordered from highest priority
     * to lowest priority. We examine the ordinal of the current priority setting, and then iterate
     * across all possible priority levels, adjusting as necessary. Any priority levels whose
     * ordinals are less than then current priority level ordinal must be unbound, while all
     * priority levels whose ordinals are greater than or equal to the current priority level
     * ordinal must be bound.
     */
    private boolean updateBindings() {
      XPCOMEventTarget.assertOnLauncherThread();
      int numBindSuccesses = 0;
      int numBindFailures = 0;
      int numUnbindSuccesses = 0;

      final Context context = GeckoAppShell.getApplicationContext();

      // This code assumes that the order of the PriorityLevel enum is highest to lowest
      final int curPriorityOrdinal = mCurrentPriority.ordinal();
      final PriorityLevel[] levels = PriorityLevel.values();

      for (int curLevelIdx = 0; curLevelIdx < levels.length; ++curLevelIdx) {
        final PriorityLevel curLevel = levels[curLevelIdx];
        final Binding existingBinding = mBindings.get(curLevel);
        final boolean hasExistingBinding = existingBinding != null;

        if (curLevelIdx < curPriorityOrdinal) {
          // Remove if present
          if (hasExistingBinding) {
            try {
              context.unbindService(existingBinding);
              ++numUnbindSuccesses;
              mBindings.remove(curLevel);
            } catch (final IllegalArgumentException e) {
              // The binding was already dead. That's okay.
              ++numUnbindSuccesses;
              mBindings.remove(curLevel);
            }
          }
        } else {
          // Normally we only need to do a bind if we do not yet have an existing binding
          // for this priority level.
          boolean bindNeeded = !hasExistingBinding;

          // We only update the service group when the binding for this level already
          // exists and no binds have occurred yet during the current updateBindings call.
          if (hasExistingBinding && hasQApis() && (numBindSuccesses + numBindFailures) == 0) {
            // NB: Right now we're passing 0 as the |group| argument, indicating that
            // the process is not grouped with any other processes. Once we support
            // Fission we should re-evaluate this.
            context.updateServiceGroup(existingBinding, 0, mRelativeImportance);
            // Now we need to call bindService with the existing binding to make this
            // change take effect.
            bindNeeded = true;
          }

          if (bindNeeded) {
            final Binding useBinding = hasExistingBinding ? existingBinding : this.new Binding();
            if (mBindDelegate.bindService(useBinding, curLevel)) {
              ++numBindSuccesses;
              if (!hasExistingBinding) {
                mBindings.put(curLevel, useBinding);
              }
            } else {
              ++numBindFailures;
            }
          }
        }
      }

      final String svcName = mBindDelegate.getServiceName();
      String pid;
      try {
        pid = String.valueOf(getPid());
      } catch (final Exception e) {
        pid = "not connected";
      }

      final StringBuilder builder = new StringBuilder(svcName);
      builder
          .append(" updateBindings: ")
          .append(mCurrentPriority)
          .append(" priority, ")
          .append(mRelativeImportance)
          .append(" importance, ")
          .append(pid)
          .append(" pid, ")
          .append(numBindSuccesses)
          .append(" successful binds, ")
          .append(numBindFailures)
          .append(" failed binds, ")
          .append(numUnbindSuccesses)
          .append(" successful unbinds");
      Log.d(LOGTAG, builder.toString());

      return numBindFailures == 0;
    }
  }

  private interface ContentAllocationPolicy {
    /**
     * @return BindServiceDelegate that will be used for binding a new content service.
     */
    BindServiceDelegate getBindServiceDelegate(InstanceInfo info);

    /**
     * Allocate an unused service ID for use by the caller.
     *
     * @return The new service id.
     */
    String allocate();

    /**
     * Release a previously used service ID.
     *
     * @param id The service id being released.
     */
    void release(final String id);
  }

  /**
   * This policy is intended for Android versions &lt; 10, as well as for content process services
   * that are not defined as isolated processes. In this case, the number of possible content
   * service IDs has a fixed upper bound, so we use a BitSet to manage their allocation.
   */
  private static final class DefaultContentPolicy implements ContentAllocationPolicy {
    private final int mMaxNumSvcs;
    private final BitSet mAllocator;
    private final SecureRandom mRandom;

    public DefaultContentPolicy() {
      mMaxNumSvcs = getContentServiceCount();
      mAllocator = new BitSet(mMaxNumSvcs);
      mRandom = new SecureRandom();
    }

    @Override
    public BindServiceDelegate getBindServiceDelegate(@NonNull final InstanceInfo info) {
      return info.new DefaultBindDelegate();
    }

    @Override
    public String allocate() {
      final int[] available = new int[mMaxNumSvcs];
      int size = 0;
      for (int i = 0; i < mMaxNumSvcs; i++) {
        if (!mAllocator.get(i)) {
          available[size] = i;
          size++;
        }
      }

      if (size == 0) {
        throw new RuntimeException("No more content services available");
      }

      final int next = available[mRandom.nextInt(size)];
      mAllocator.set(next);
      return Integer.toString(next);
    }

    @Override
    public void release(final String stringId) {
      final int id = Integer.valueOf(stringId);
      if (!mAllocator.get(id)) {
        throw new IllegalStateException("Releasing an unallocated id=" + id);
      }

      mAllocator.clear(id);
    }

    /**
     * Get the number of content (isolated content) services defined in our manifest. This isn't sum
     * of content + isolated content.
     *
     * @return The number of content services defined in our manifest.
     */
    private static int getContentServiceCount() {
      return ServiceUtils.getServiceCount(
          GeckoAppShell.getApplicationContext(), GeckoProcessType.CONTENT);
    }
  }

  /**
   * This policy is intended for Android versions &gt;= 10 when our content process services are
   * defined in our manifest as having isolated processes. Since isolated services share a single
   * service definition, there is no longer an Android-induced hard limit on the number of content
   * processes that may be started. We simply use a monotonically-increasing counter to generate
   * unique instance IDs in this case.
   */
  @RequiresApi(Build.VERSION_CODES.Q)
  private static final class IsolatedContentPolicy implements ContentAllocationPolicy {
    private final Set<String> mRunningServiceIds = new HashSet<>();

    @Override
    public BindServiceDelegate getBindServiceDelegate(@NonNull final InstanceInfo info) {
      return info.new IsolatedBindDelegate();
    }

    /**
     * We generate a new instance ID simply by incrementing a counter. We do track how many content
     * services are currently active for the purposes of maintaining the configured limit on number
     * of simultaneous content processes.
     */
    @Override
    public String allocate() {
      if (mRunningServiceIds.size() >= MAX_NUM_ISOLATED_CONTENT_SERVICES) {
        throw new RuntimeException("No more content services available");
      }

      final String newId = UUID.randomUUID().toString();
      final String dashlessId = newId.replaceAll("\\-", "");
      mRunningServiceIds.add(dashlessId);
      return dashlessId;
    }

    /** Just drop the count of active services. */
    @Override
    public void release(final String id) {
      if (!mRunningServiceIds.remove(id)) {
        throw new IllegalStateException("Releasing an unallocated id");
      }
    }
  }

  /** The policy used for allocating content processes. */
  private ContentAllocationPolicy mContentAllocPolicy = null;

  /**
   * Allocate a service ID.
   *
   * @param type The type of service.
   * @return Integer encapsulating the service ID, or null if no ID is necessary.
   */
  private String allocate(@NonNull final GeckoProcessType type) {
    XPCOMEventTarget.assertOnLauncherThread();
    if (!GeckoProcessManager.isContent(type)) {
      // No unique id necessary
      return null;
    }

    // Lazy initialization of mContentAllocPolicy to ensure that it is constructed on the
    // launcher thread.
    if (mContentAllocPolicy == null) {
      if (hasQApis()
          && (type == GeckoProcessType.CONTENT_ISOLATED
              || type == GeckoProcessType.CONTENT_ISOLATED_WITH_ZYGOTE)) {
        mContentAllocPolicy = new IsolatedContentPolicy();
      } else {
        mContentAllocPolicy = new DefaultContentPolicy();
      }
    }

    return mContentAllocPolicy.allocate();
  }

  /**
   * Free a defunct service's ID if necessary.
   *
   * @param info The InstanceInfo-derived object that contains essential information for tearing
   *     down the child service.
   */
  private void release(@NonNull final InstanceInfo info) {
    XPCOMEventTarget.assertOnLauncherThread();
    if (!info.isContent()) {
      return;
    }

    mContentAllocPolicy.release(info.getId());
  }

  /**
   * Find out whether the desired service type is defined in our manifest as having an isolated
   * process.
   *
   * @param type Service type to query
   * @return true if this service type may use isolated binding, otherwise false.
   */
  private static boolean canBindIsolated(@NonNull final GeckoProcessType type) {
    if (!hasQApis()) {
      return false;
    }

    final Context context = GeckoAppShell.getApplicationContext();
    final int svcFlags = ServiceUtils.getServiceFlags(context, type);
    return (svcFlags & ServiceInfo.FLAG_ISOLATED_PROCESS) != 0;
  }

  /** Convert PriorityLevel into the flags argument to Context.bindService() et al */
  private static int getAndroidFlags(@NonNull final PriorityLevel priority) {
    return Context.BIND_AUTO_CREATE | priority.getAndroidFlag();
  }

  /** Obtain the class name to use for service binding in the default (ie, non-isolated) case. */
  private static String getSvcClassNameDefault(@NonNull final InstanceInfo info) {
    return ServiceUtils.buildSvcName(info.getType(), info.getIdInternal());
  }

  /**
   * Wrapper for bindService() that utilizes the Context.bindService() overload that accepts an
   * Executor argument, when available. Otherwise it falls back to the legacy overload.
   */
  private static boolean bindServiceDefault(
      @NonNull final Context context,
      @NonNull final Intent intent,
      @NonNull final ServiceConnection conn,
      final int flags) {
    if (hasQApis()) {
      // We always specify the launcher thread as our Executor.
      return context.bindService(intent, flags, XPCOMEventTarget.launcherThread(), conn);
    }

    return context.bindService(intent, conn, flags);
  }

  @RequiresApi(Build.VERSION_CODES.Q)
  private static boolean bindServiceIsolated(
      @NonNull final Context context,
      @NonNull final Intent intent,
      final int flags,
      @NonNull final String instanceId,
      @NonNull final ServiceConnection conn) {
    // We always specify the launcher thread as our Executor.
    return context.bindIsolatedService(
        intent, flags, instanceId, XPCOMEventTarget.launcherThread(), conn);
  }
}
