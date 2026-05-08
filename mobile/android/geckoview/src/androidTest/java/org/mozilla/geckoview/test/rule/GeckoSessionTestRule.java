/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test.rule;

import static org.hamcrest.Matchers.*;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.fail;

import android.app.Instrumentation;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.graphics.Point;
import android.graphics.SurfaceTexture;
import android.location.Criteria;
import android.location.Location;
import android.location.LocationManager;
import android.os.SystemClock;
import android.util.Log;
import android.util.Pair;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.Surface;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.File;
import java.lang.annotation.Annotation;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import kotlin.jvm.JvmClassMappingKt;
import kotlin.reflect.KClass;
import org.hamcrest.Matcher;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.json.JSONTokener;
import org.junit.rules.ErrorCollector;
import org.junit.rules.TestRule;
import org.junit.runner.Description;
import org.junit.runners.model.Statement;
import org.mozilla.gecko.MultiMap;
import org.mozilla.gecko.util.ThreadUtils;
import org.mozilla.geckoview.Autocomplete;
import org.mozilla.geckoview.Autofill;
import org.mozilla.geckoview.ContentBlocking;
import org.mozilla.geckoview.ExperimentDelegate;
import org.mozilla.geckoview.GeckoDisplay;
import org.mozilla.geckoview.GeckoPreferenceController;
import org.mozilla.geckoview.GeckoResult;
import org.mozilla.geckoview.GeckoRuntime;
import org.mozilla.geckoview.GeckoRuntime.ActivityDelegate;
import org.mozilla.geckoview.GeckoRuntime.ServiceWorkerDelegate;
import org.mozilla.geckoview.GeckoSession;
import org.mozilla.geckoview.GeckoSession.ContentDelegate;
import org.mozilla.geckoview.GeckoSession.HistoryDelegate;
import org.mozilla.geckoview.GeckoSession.MediaDelegate;
import org.mozilla.geckoview.GeckoSession.NavigationDelegate;
import org.mozilla.geckoview.GeckoSession.PermissionDelegate;
import org.mozilla.geckoview.GeckoSession.PrintDelegate;
import org.mozilla.geckoview.GeckoSession.ProgressDelegate;
import org.mozilla.geckoview.GeckoSession.PromptDelegate;
import org.mozilla.geckoview.GeckoSession.ScrollDelegate;
import org.mozilla.geckoview.GeckoSession.SelectionActionDelegate;
import org.mozilla.geckoview.GeckoSession.TextInputDelegate;
import org.mozilla.geckoview.GeckoSessionSettings;
import org.mozilla.geckoview.MediaSession;
import org.mozilla.geckoview.OrientationController;
import org.mozilla.geckoview.SessionTextInput;
import org.mozilla.geckoview.TranslationsController;
import org.mozilla.geckoview.WebExtension;
import org.mozilla.geckoview.WebExtensionController;
import org.mozilla.geckoview.WebNotificationDelegate;
import org.mozilla.geckoview.WebPushDelegate;
import org.mozilla.geckoview.test.GeckoViewTestActivity;
import org.mozilla.geckoview.test.util.Environment;
import org.mozilla.geckoview.test.util.RuntimeCreator;
import org.mozilla.geckoview.test.util.TestServer;
import org.mozilla.geckoview.test.util.UiThreadUtils;

/**
 * TestRule that, for each test, sets up a GeckoSession, runs the test on the UI thread, and tears
 * down the GeckoSession at the end of the test. The rule also provides methods for waiting on
 * particular callbacks to be called, and methods for asserting that callbacks are called in the
 * proper order.
 */
public class GeckoSessionTestRule implements TestRule {
  private static final String LOGTAG = "GeckoSessionTestRule";

  public static final int TEST_PORT = 4245;
  public static final String TEST_HOST = "localhost";
  public static final String TEST_ENDPOINT = "http://" + TEST_HOST + ":" + TEST_PORT;

  private static final Method sOnPageStart;
  private static final Method sOnPageStop;
  private static final Method sOnNewSession;
  private static final Method sOnCrash;
  private static final Method sOnKill;

  static {
    try {
      sOnPageStart =
          GeckoSession.ProgressDelegate.class.getMethod(
              "onPageStart", GeckoSession.class, String.class);
      sOnPageStop =
          GeckoSession.ProgressDelegate.class.getMethod(
              "onPageStop", GeckoSession.class, boolean.class);
      sOnNewSession =
          GeckoSession.NavigationDelegate.class.getMethod(
              "onNewSession", GeckoSession.class, String.class);
      sOnCrash = GeckoSession.ContentDelegate.class.getMethod("onCrash", GeckoSession.class);
      sOnKill = GeckoSession.ContentDelegate.class.getMethod("onKill", GeckoSession.class);
    } catch (final NoSuchMethodException e) {
      throw new RuntimeException(e);
    }
  }

  public void addDisplay(final GeckoSession session, final int x, final int y) {
    final GeckoDisplay display = session.acquireDisplay();

    final SurfaceTexture displayTexture = new SurfaceTexture(0);
    displayTexture.setDefaultBufferSize(x, y);

    final Surface displaySurface = new Surface(displayTexture);
    display.surfaceChanged(new GeckoDisplay.SurfaceInfo.Builder(displaySurface).size(x, y).build());

    mDisplays.put(session, display);
    mDisplayTextures.put(session, displayTexture);
    mDisplaySurfaces.put(session, displaySurface);
  }

  public void releaseDisplay(final GeckoSession session) {
    if (!mDisplays.containsKey(session)) {
      // No display to release
      return;
    }
    final GeckoDisplay display = mDisplays.remove(session);
    display.surfaceDestroyed();
    session.releaseDisplay(display);
    final Surface displaySurface = mDisplaySurfaces.remove(session);
    displaySurface.release();
    final SurfaceTexture displayTexture = mDisplayTextures.remove(session);
    displayTexture.release();
  }

  /**
   * Specify the timeout for any of the wait methods, in milliseconds, relative to {@link
   * Environment#DEFAULT_TIMEOUT_MILLIS}. When the default timeout scales to account for differences
   * in the device under test, the timeout value here will be scaled as well. Can be used on classes
   * or methods.
   */
  @Target({ElementType.METHOD, ElementType.TYPE})
  @Retention(RetentionPolicy.RUNTIME)
  public @interface TimeoutMillis {
    long value();
  }

  /** Specify the display size for the GeckoSession in device pixels */
  @Target({ElementType.METHOD, ElementType.TYPE})
  @Retention(RetentionPolicy.RUNTIME)
  public @interface WithDisplay {
    int width();

    int height();
  }

  /** Specify that the main session should not be opened at the start of the test. */
  @Target({ElementType.METHOD, ElementType.TYPE})
  @Retention(RetentionPolicy.RUNTIME)
  public @interface ClosedSessionAtStart {
    boolean value() default true;
  }

  /**
   * Specify that the test will set a delegate to null when creating a session, rather than setting
   * the delegate to a proxy. The test cannot wait on any delegates that are set to null.
   */
  @Target({ElementType.METHOD, ElementType.TYPE})
  @Retention(RetentionPolicy.RUNTIME)
  public @interface NullDelegate {
    Class<?> value();

    @Target({ElementType.METHOD, ElementType.TYPE})
    @Retention(RetentionPolicy.RUNTIME)
    @interface List {
      NullDelegate[] value();
    }
  }

  /**
   * Specify a list of GeckoSession settings to be applied to the GeckoSession object under test.
   * Can be used on classes or methods. Note that the settings values must be string literals
   * regardless of the type of the settings.
   *
   * <p>Enable tracking protection for a particular test:
   *
   * <pre>
   * &#64;Setting.List(&#64;Setting(key = Setting.Key.USE_TRACKING_PROTECTION,
   *                        value = "false"))
   * &#64;Test public void test() { ... }
   * </pre>
   *
   * <p>Use multiple settings:
   *
   * <pre>
   * &#64;Setting.List({&#64;Setting(key = Setting.Key.USE_PRIVATE_MODE,
   *                         value = "true"),
   *                &#64;Setting(key = Setting.Key.USE_TRACKING_PROTECTION,
   *                         value = "false")})
   * </pre>
   */
  @Target({ElementType.METHOD, ElementType.TYPE})
  @Retention(RetentionPolicy.RUNTIME)
  public @interface Setting {
    enum Key {
      CHROME_URI,
      DISPLAY_MODE,
      ALLOW_JAVASCRIPT,
      SCREEN_ID,
      USE_PRIVATE_MODE,
      USE_TRACKING_PROTECTION,
      FULL_ACCESSIBILITY_TREE;

      private final GeckoSessionSettings.Key<?> mKey;
      private final Class<?> mType;

      Key() {
        final Field field;
        try {
          field = GeckoSessionSettings.class.getDeclaredField(name());
          field.setAccessible(true);
          mKey = (GeckoSessionSettings.Key<?>) field.get(null);
        } catch (final NoSuchFieldException | IllegalAccessException e) {
          throw new RuntimeException(e);
        }

        final ParameterizedType genericType = (ParameterizedType) field.getGenericType();
        mType = (Class<?>) genericType.getActualTypeArguments()[0];
      }

      @SuppressWarnings("unchecked")
      public void set(final GeckoSessionSettings settings, final String value) {
        try {
          if (boolean.class.equals(mType) || Boolean.class.equals(mType)) {
            final Method method =
                GeckoSessionSettings.class.getDeclaredMethod(
                    "setBoolean", GeckoSessionSettings.Key.class, boolean.class);
            method.setAccessible(true);
            method.invoke(settings, mKey, Boolean.valueOf(value));
          } else if (int.class.equals(mType) || Integer.class.equals(mType)) {
            final Method method =
                GeckoSessionSettings.class.getDeclaredMethod(
                    "setInt", GeckoSessionSettings.Key.class, int.class);
            method.setAccessible(true);
            try {
              method.invoke(
                  settings, mKey, (Integer) GeckoSessionSettings.class.getField(value).get(null));
            } catch (final NoSuchFieldException | IllegalAccessException | ClassCastException e) {
              method.invoke(settings, mKey, Integer.valueOf(value));
            }
          } else if (String.class.equals(mType)) {
            final Method method =
                GeckoSessionSettings.class.getDeclaredMethod(
                    "setString", GeckoSessionSettings.Key.class, String.class);
            method.setAccessible(true);
            method.invoke(settings, mKey, value);
          } else {
            throw new IllegalArgumentException("Unsupported type: " + mType.getSimpleName());
          }
        } catch (final NoSuchMethodException
            | IllegalAccessException
            | InvocationTargetException e) {
          throw new RuntimeException(e);
        }
      }
    }

    @Target({ElementType.METHOD, ElementType.TYPE})
    @Retention(RetentionPolicy.RUNTIME)
    @interface List {
      Setting[] value();
    }

    Key key();

    String value();
  }

  /**
   * Assert that a method is called or not called, and if called, the order and number of times it
   * is called. The order number is a monotonically increasing integer; if an called method's order
   * number is less than the current order number, an exception is raised for out-of-order call.
   *
   * <p>{@code @AssertCalled} asserts the method must be called at least once.
   *
   * <p>{@code @AssertCalled(false)} asserts the method must not be called.
   *
   * <p>{@code @AssertCalled(order = 2)} asserts the method must be called once and after any other
   * method with order number less than 2.
   *
   * <p>{@code @AssertCalled(order = {2, 4})} asserts order number 2 for first call and order number
   * 4 for any subsequent calls.
   *
   * <p>{@code @AssertCalled(count = 2)} asserts two calls total in any order with respect to other
   * calls.
   *
   * <p>{@code @AssertCalled(count = 2, order = 2)} asserts two calls, both with order number 2.
   *
   * <p>{@code @AssertCalled(count = 2, order = {2, 4, 6})} asserts two calls total: the first with
   * order number 2 and the second with order number 4.
   */
  @Target(ElementType.METHOD)
  @Retention(RetentionPolicy.RUNTIME)
  public @interface AssertCalled {
    /**
     * @return True if the method must be called if count != 0, or false if the method must not be
     *     called.
     */
    boolean value() default true;

    /**
     * @return The number of calls allowed. Specify -1 to allow any number > 0. Specify 0 to assert
     *     the method is not called, even if value() is true.
     */
    int count() default -1;

    /**
     * @return If called, the order number for each call, or 0 to allow arbitrary order. If order's
     *     length is more than count, extra elements are not used; if order's length is less than
     *     count, the last element is repeated.
     */
    int[] order() default 0;
  }

  /** Interface that represents a function that registers or unregisters a delegate. */
  public interface DelegateRegistrar<T> {
    void invoke(T delegate) throws Throwable;
  }

  /*
   * If the value here is true, content crashes will be ignored. If false, the test will
   * be failed immediately if a content crash occurs. This is also the case when
   * {@link IgnoreCrash} is not present.
   */
  @Target(ElementType.METHOD)
  @Retention(RetentionPolicy.RUNTIME)
  public @interface IgnoreCrash {
    /**
     * @return True if content crashes should be ignored, false otherwise. Default is true.
     */
    boolean value() default true;
  }

  public static class ChildCrashedException extends RuntimeException {
    public ChildCrashedException(final String detailMessage) {
      super(detailMessage);
    }
  }

  public static class RejectedPromiseException extends RuntimeException {
    private final Object mReason;

    /* package */ RejectedPromiseException(final Object reason) {
      super(String.valueOf(reason));
      mReason = reason;
    }

    public Object getReason() {
      return mReason;
    }
  }

  public static class CallRequirement {
    public final boolean allowed;
    public final int count;
    public final int[] order;

    public CallRequirement(final boolean allowed, final int count, final int[] order) {
      this.allowed = allowed;
      this.count = count;
      this.order = order;
    }
  }

  public static class CallInfo {
    public final int counter;
    public final int order;

    /* package */ CallInfo(final int counter, final int order) {
      this.counter = counter;
      this.order = order;
    }
  }

  public static class MethodCall {
    public final GeckoSession session;
    public final Method method;
    public final CallRequirement requirement;
    public final Object target;
    private int currentCount;

    public MethodCall(
        final GeckoSession session, final Method method, final CallRequirement requirement) {
      this(session, method, requirement, /* target */ null);
    }

    /* package */ MethodCall(
        final GeckoSession session,
        final Method method,
        final AssertCalled annotation,
        final Object target) {
      this(
          session,
          method,
          (annotation != null)
              ? new CallRequirement(annotation.value(), annotation.count(), annotation.order())
              : null,
          /* target */ target);
    }

    /* package */ MethodCall(
        final GeckoSession session,
        final Method method,
        final CallRequirement requirement,
        final Object target) {
      this.session = session;
      this.method = method;
      this.requirement = requirement;
      this.target = target;
      currentCount = 0;
    }

    @Override
    public boolean equals(final Object other) {
      if (this == other) {
        return true;
      } else if (other instanceof MethodCall) {
        final MethodCall otherCall = (MethodCall) other;
        return (session == null || otherCall.session == null || session.equals(otherCall.session))
            && methodsEqual(method, ((MethodCall) other).method);
      } else if (other instanceof Method) {
        return methodsEqual(method, (Method) other);
      }
      return false;
    }

    @Override
    public int hashCode() {
      return method.hashCode();
    }

    /* package */ int getOrder() {
      if (requirement == null || currentCount == 0) {
        return 0;
      }

      final int[] order = requirement.order;
      if (order == null || order.length == 0) {
        return 0;
      }
      return order[Math.min(currentCount - 1, order.length - 1)];
    }

    /* package */ int getCount() {
      return (requirement == null) ? -1 : requirement.allowed ? requirement.count : 0;
    }

    /* package */ void incrementCounter() {
      currentCount++;
    }

    /* package */ int getCurrentCount() {
      return currentCount;
    }

    /* package */ boolean allowUnlimitedCalls() {
      return getCount() == -1;
    }

    /* package */ boolean allowMoreCalls() {
      final int count = getCount();
      return count == -1 || count > currentCount;
    }

    /* package */ CallInfo getInfo() {
      return new CallInfo(currentCount, getOrder());
    }

    // Similar to Method.equals, but treat the same method from an interface and an
    // overriding class as the same (e.g. CharSequence.length == String.length).
    private static boolean methodsEqual(final @NonNull Method m1, final @NonNull Method m2) {
      return (m1.getDeclaringClass().isAssignableFrom(m2.getDeclaringClass())
              || m2.getDeclaringClass().isAssignableFrom(m1.getDeclaringClass()))
          && m1.getName().equals(m2.getName())
          && m1.getReturnType().equals(m2.getReturnType())
          && Arrays.equals(m1.getParameterTypes(), m2.getParameterTypes());
    }
  }

  protected static class CallRecord {
    public final Method method;
    public final MethodCall methodCall;
    public final Object[] args;

    public CallRecord(final GeckoSession session, final Method method, final Object[] args) {
      this.method = method;
      this.methodCall = new MethodCall(session, method, /* requirement */ null);
      this.args = args;
    }
  }

  protected interface CallRecordHandler {
    boolean handleCall(Method method, Object[] args);
  }

  protected final class ExternalDelegate<T> {
    public final Class<T> delegate;
    private final DelegateRegistrar<T> mRegister;
    private final DelegateRegistrar<T> mUnregister;
    private final T mProxy;
    private boolean mRegistered;

    public ExternalDelegate(
        final Class<T> delegate,
        final T impl,
        final DelegateRegistrar<T> register,
        final DelegateRegistrar<T> unregister) {
      this.delegate = delegate;
      mRegister = register;
      mUnregister = unregister;

      @SuppressWarnings("unchecked")
      final T delegateProxy =
          (T)
              Proxy.newProxyInstance(
                  getClass().getClassLoader(),
                  impl.getClass().getInterfaces(),
                  Proxy.getInvocationHandler(mCallbackProxy));
      mProxy = delegateProxy;
    }

    @Override
    public int hashCode() {
      return delegate.hashCode();
    }

    @Override
    public boolean equals(final Object obj) {
      return obj instanceof ExternalDelegate<?>
          && delegate.equals(((ExternalDelegate<?>) obj).delegate);
    }

    public void register() {
      try {
        if (!mRegistered) {
          mRegister.invoke(mProxy);
          mRegistered = true;
        }
      } catch (final Throwable e) {
        throw unwrapRuntimeException(e);
      }
    }

    public void unregister() {
      try {
        if (mRegistered) {
          mUnregister.invoke(mProxy);
          mRegistered = false;
        }
      } catch (final Throwable e) {
        throw unwrapRuntimeException(e);
      }
    }
  }

  protected class CallbackDelegates {
    private final Map<Pair<GeckoSession, Method>, MethodCall> mDelegates = new HashMap<>();
    private final List<ExternalDelegate<?>> mExternalDelegates = new ArrayList<>();
    private int mOrder;
    private JSONObject mOldPrefs;

    public void delegate(final @Nullable GeckoSession session, final @NonNull Object callback) {
      for (final Class<?> ifce : mAllDelegates) {
        if (!ifce.isInstance(callback)) {
          continue;
        }
        assertThat("Cannot delegate null-delegate callbacks", ifce, not(isIn(mNullDelegates)));
        addDelegatesForInterface(session, callback, ifce);
      }
    }

    private void addDelegatesForInterface(
        @Nullable final GeckoSession session,
        @NonNull final Object callback,
        @NonNull final Class<?> ifce) {
      for (final Method method : ifce.getMethods()) {
        final Method callbackMethod;
        try {
          callbackMethod =
              callback.getClass().getMethod(method.getName(), method.getParameterTypes());
        } catch (final NoSuchMethodException e) {
          throw new RuntimeException(e);
        }
        final Pair<GeckoSession, Method> pair = new Pair<>(session, method);
        final MethodCall call =
            new MethodCall(
                session, callbackMethod, getAssertCalled(callbackMethod, callback), callback);
        // It's unclear if we should assert the call count if we replace an existing
        // delegate half way through. Until that is resolved, forbid replacing an
        // existing delegate during a test. If you are thinking about changing this
        // behavior, first see if #delegateDuringNextWait fits your needs.
        assertThat("Cannot replace an existing delegate", mDelegates, not(hasKey(pair)));
        mDelegates.put(pair, call);
      }
    }

    public <T> ExternalDelegate<T> addExternalDelegate(
        @NonNull final Class<T> delegate,
        @NonNull final DelegateRegistrar<T> register,
        @NonNull final DelegateRegistrar<T> unregister,
        @NonNull final T impl) {
      assertThat("Delegate must be an interface", delegate.isInterface(), equalTo(true));

      // Delegate each interface to the real thing, then register the delegate using our
      // proxy. That way all calls to the delegate are recorded just like our internal
      // delegates.
      addDelegatesForInterface(/* session */ null, impl, delegate);

      final ExternalDelegate<T> externalDelegate =
          new ExternalDelegate<>(delegate, impl, register, unregister);
      mExternalDelegates.add(externalDelegate);
      mAllDelegates.add(delegate);
      return externalDelegate;
    }

    @NonNull
    public List<ExternalDelegate<?>> getExternalDelegates() {
      return mExternalDelegates;
    }

    /** Generate a JS function to set new prefs and return a set of saved prefs. */
    public void setPrefs(final @NonNull Map<String, ?> prefs) {
      mOldPrefs =
          (JSONObject)
              webExtensionApiCall(
                  "SetPrefs",
                  args -> {
                    final JSONObject existingPrefs =
                        mOldPrefs != null ? mOldPrefs : new JSONObject();

                    final JSONObject newPrefs = new JSONObject();
                    for (final Map.Entry<String, ?> pref : prefs.entrySet()) {
                      final Object value = pref.getValue();
                      if (value instanceof Boolean
                          || value instanceof Number
                          || value instanceof CharSequence) {
                        newPrefs.put(pref.getKey(), value);
                      } else {
                        throw new IllegalArgumentException("Unsupported pref value: " + value);
                      }
                    }

                    args.put("oldPrefs", existingPrefs);
                    args.put("newPrefs", newPrefs);
                  });
    }

    /** Generate a JS function to set new prefs and reset a set of saved prefs. */
    private void restorePrefs() {
      if (mOldPrefs == null) {
        return;
      }

      webExtensionApiCall(
          "RestorePrefs",
          args -> {
            args.put("oldPrefs", mOldPrefs);
            mOldPrefs = null;
          });
    }

    public void clear() {
      for (int i = mExternalDelegates.size() - 1; i >= 0; i--) {
        mExternalDelegates.get(i).unregister();
      }
      mExternalDelegates.clear();
      mDelegates.clear();
      mOrder = 0;

      restorePrefs();
    }

    public void clearAndAssert() {
      final Collection<MethodCall> values = mDelegates.values();
      final MethodCall[] valuesArray = values.toArray(new MethodCall[values.size()]);

      clear();

      for (final MethodCall call : valuesArray) {
        assertMatchesCount(call);
      }
    }

    public MethodCall prepareMethodCall(final GeckoSession session, final Method method) {
      MethodCall call = mDelegates.get(new Pair<>(session, method));
      if (call == null && session != null) {
        call = mDelegates.get(new Pair<>((GeckoSession) null, method));
      }
      if (call == null) {
        return null;
      }

      assertAllowMoreCalls(call);
      call.incrementCounter();
      assertOrder(call, mOrder);
      mOrder = Math.max(call.getOrder(), mOrder);
      return call;
    }
  }

  /* package */ static AssertCalled getAssertCalled(final Method method, final Object callback) {
    final AssertCalled annotation = method.getAnnotation(AssertCalled.class);
    if (annotation != null) {
      return annotation;
    }

    // Some Kotlin lambdas have an invoke method that carries the annotation,
    // instead of the interface method carrying the annotation.
    try {
      return callback
          .getClass()
          .getDeclaredMethod("invoke", method.getParameterTypes())
          .getAnnotation(AssertCalled.class);
    } catch (final NoSuchMethodException e) {
      return null;
    }
  }

  private static final Set<Class<?>> DEFAULT_DELEGATES = new HashSet<>();

  static {
    DEFAULT_DELEGATES.add(Autofill.Delegate.class);
    DEFAULT_DELEGATES.add(ContentBlocking.Delegate.class);
    DEFAULT_DELEGATES.add(ContentDelegate.class);
    DEFAULT_DELEGATES.add(HistoryDelegate.class);
    DEFAULT_DELEGATES.add(MediaDelegate.class);
    DEFAULT_DELEGATES.add(MediaSession.Delegate.class);
    DEFAULT_DELEGATES.add(NavigationDelegate.class);
    DEFAULT_DELEGATES.add(PermissionDelegate.class);
    DEFAULT_DELEGATES.add(PrintDelegate.class);
    DEFAULT_DELEGATES.add(ProgressDelegate.class);
    DEFAULT_DELEGATES.add(PromptDelegate.class);
    DEFAULT_DELEGATES.add(ScrollDelegate.class);
    DEFAULT_DELEGATES.add(SelectionActionDelegate.class);
    DEFAULT_DELEGATES.add(TextInputDelegate.class);
    DEFAULT_DELEGATES.add(TranslationsController.SessionTranslation.Delegate.class);
  }

  private static final Set<Class<?>> DEFAULT_RUNTIME_DELEGATES = new HashSet<>();

  static {
    DEFAULT_RUNTIME_DELEGATES.add(Autocomplete.StorageDelegate.class);
    DEFAULT_RUNTIME_DELEGATES.add(ActivityDelegate.class);
    DEFAULT_RUNTIME_DELEGATES.add(GeckoRuntime.Delegate.class);
    DEFAULT_RUNTIME_DELEGATES.add(OrientationController.OrientationDelegate.class);
    DEFAULT_RUNTIME_DELEGATES.add(ServiceWorkerDelegate.class);
    DEFAULT_RUNTIME_DELEGATES.add(WebNotificationDelegate.class);
    DEFAULT_RUNTIME_DELEGATES.add(WebExtensionController.PromptDelegate.class);
    DEFAULT_RUNTIME_DELEGATES.add(WebPushDelegate.class);
  }

  private static class DefaultImpl
      implements
          // Session delegates
          Autofill.Delegate,
          ContentBlocking.Delegate,
          ContentDelegate,
          HistoryDelegate,
          MediaDelegate,
          MediaSession.Delegate,
          NavigationDelegate,
          PermissionDelegate,
          PrintDelegate,
          ProgressDelegate,
          PromptDelegate,
          ScrollDelegate,
          SelectionActionDelegate,
          TextInputDelegate,
          TranslationsController.SessionTranslation.Delegate,
          // Runtime delegates
          ActivityDelegate,
          Autocomplete.StorageDelegate,
          GeckoRuntime.Delegate,
          OrientationController.OrientationDelegate,
          ServiceWorkerDelegate,
          WebExtensionController.PromptDelegate,
          WebNotificationDelegate,
          WebPushDelegate {
    @Override
    public GeckoResult<Intent> onStartActivityForResult(@NonNull PendingIntent intent) {
      return null;
    }

    @Override
    public void onLocationChange(
        @NonNull GeckoSession session,
        @Nullable String url,
        @NonNull final List<ContentPermission> perms,
        @NonNull Boolean hasUserGesture) {}

    @Override
    public void onShutdown() {}

    @Override
    public GeckoResult<GeckoSession> onOpenWindow(@NonNull String url) {
      return GeckoResult.fromValue(null);
    }
  }

  private static final DefaultImpl DEFAULT_IMPL = new DefaultImpl();

  public final Environment env = new Environment();

  protected final Instrumentation mInstrumentation = InstrumentationRegistry.getInstrumentation();
  protected final GeckoSessionSettings mDefaultSettings;
  protected final Set<GeckoSession> mSubSessions = new HashSet<>();

  protected ErrorCollector mErrorCollector;
  protected GeckoSession mMainSession;
  protected Object mCallbackProxy;
  protected Set<Class<?>> mNullDelegates;
  protected Set<Class<?>> mAllDelegates;
  protected List<CallRecord> mCallRecords;
  protected CallRecordHandler mCallRecordHandler;
  protected CallbackDelegates mWaitScopeDelegates;
  protected CallbackDelegates mTestScopeDelegates;
  protected int mLastWaitStart;
  protected int mLastWaitEnd;
  protected MethodCall mCurrentMethodCall;
  protected long mTimeoutMillis;
  protected Point mDisplaySize;
  protected Map<GeckoSession, SurfaceTexture> mDisplayTextures = new HashMap<>();
  protected Map<GeckoSession, Surface> mDisplaySurfaces = new HashMap<>();
  protected Map<GeckoSession, GeckoDisplay> mDisplays = new HashMap<>();
  protected boolean mClosedSession;
  protected boolean mIgnoreCrash;

  @Nullable private Map<String, String> mServerCustomHeaders = null;
  @Nullable private Map<String, TestServer.ResponseModifier> mResponseModifiers = null;

  public GeckoSessionTestRule() {
    mDefaultSettings = new GeckoSessionSettings.Builder().build();
  }

  public GeckoSessionTestRule(@Nullable Map<String, String> mServerCustomHeaders) {
    this();
    this.mServerCustomHeaders = mServerCustomHeaders;
  }

  public GeckoSessionTestRule(
      @Nullable Map<String, String> serverCustomHeaders,
      @Nullable Map<String, TestServer.ResponseModifier> responseModifiers) {
    this();
    this.mServerCustomHeaders = serverCustomHeaders;
    this.mResponseModifiers = responseModifiers;
  }

  /**
   * Set an ErrorCollector for assertion errors, or null to not use one.
   *
   * @param ec ErrorCollector or null.
   */
  public void setErrorCollector(final @Nullable ErrorCollector ec) {
    mErrorCollector = ec;
  }

  /**
   * Get the current ErrorCollector, or null if not using one.
   *
   * @return ErrorCollector or null.
   */
  public @Nullable ErrorCollector getErrorCollector() {
    return mErrorCollector;
  }

  /**
   * Get the current timeout value in milliseconds.
   *
   * @return The current timeout value in milliseconds.
   */
  public long getTimeoutMillis() {
    return mTimeoutMillis;
  }

  /**
   * Assert a condition with junit.Assert or an error collector.
   *
   * @param reason Reason string
   * @param value Value to check
   * @param matcher Matcher for checking the value
   */
  public <T> void checkThat(final String reason, final T value, final Matcher<? super T> matcher) {
    if (mErrorCollector != null) {
      mErrorCollector.checkThat(reason, value, matcher);
    } else {
      assertThat(reason, value, matcher);
    }
  }

  private void assertAllowMoreCalls(final MethodCall call) {
    final int count = call.getCount();
    if (count != -1) {
      checkThat(
          call.method.getName() + " call count should be within limit",
          call.getCurrentCount() + 1,
          lessThanOrEqualTo(count));
    }
  }

  private void assertOrder(final MethodCall call, final int order) {
    final int newOrder = call.getOrder();
    if (newOrder != 0) {
      checkThat(
          call.method.getName() + " should be in order", newOrder, greaterThanOrEqualTo(order));
    }
  }

  private void assertMatchesCount(final MethodCall call) {
    if (call.requirement == null) {
      return;
    }
    final int count = call.getCount();
    if (count == 0) {
      checkThat(
          call.method.getName() + " should not be called", call.getCurrentCount(), equalTo(0));
    } else if (count == -1) {
      checkThat(
          call.method.getName() + " should be called", call.getCurrentCount(), greaterThan(0));
    } else {
      checkThat(
          call.method.getName() + " should be called specified number of times",
          call.getCurrentCount(),
          equalTo(count));
    }
  }

  /**
   * Get the session set up for the current test.
   *
   * @return GeckoSession object.
   */
  public @NonNull GeckoSession getSession() {
    return mMainSession;
  }

  /**
   * Get the runtime set up for the current test.
   *
   * @return GeckoRuntime object.
   */
  public @NonNull GeckoRuntime getRuntime() {
    return RuntimeCreator.getRuntime();
  }

  /** Sets an experiment delegate on the runtime creator. */
  public void setExperimentDelegate(final ExperimentDelegate delegate) {
    RuntimeCreator.setExperimentDelegate(delegate);
  }

  /**
   * Convenience method to facilitate setting a preference delegate in order to use the test harness
   * delegate callback framework.
   */
  public void setPreferenceDelegate(final GeckoPreferenceController.Observer.Delegate delegate) {
    RuntimeCreator.setGeckoPreferenceDelegate(delegate);
  }

  public @Nullable GeckoDisplay getDisplay() {
    return mDisplays.get(mMainSession);
  }

  protected static void setDelegate(
      final @NonNull Class<?> cls,
      final @NonNull GeckoSession session,
      final @Nullable Object delegate)
      throws NoSuchMethodException, IllegalAccessException, InvocationTargetException {
    if (cls == GeckoSession.TextInputDelegate.class) {
      session.getTextInput().setDelegate((TextInputDelegate) delegate);
    } else if (cls == ContentBlocking.Delegate.class) {
      session.setContentBlockingDelegate((ContentBlocking.Delegate) delegate);
    } else if (cls == Autofill.Delegate.class) {
      session.setAutofillDelegate((Autofill.Delegate) delegate);
    } else if (cls == MediaSession.Delegate.class) {
      session.setMediaSessionDelegate((MediaSession.Delegate) delegate);
    } else if (cls == TranslationsController.SessionTranslation.Delegate.class) {
      session.setTranslationsSessionDelegate(
          (TranslationsController.SessionTranslation.Delegate) delegate);
    } else {
      GeckoSession.class.getMethod("set" + cls.getSimpleName(), cls).invoke(session, delegate);
    }
  }

  protected static void setRuntimeDelegate(
      final @NonNull Class<?> cls,
      final @NonNull GeckoRuntime runtime,
      final @Nullable Object delegate) {
    if (cls == Autocomplete.StorageDelegate.class) {
      runtime.setAutocompleteStorageDelegate((Autocomplete.StorageDelegate) delegate);
    } else if (cls == ActivityDelegate.class) {
      runtime.setActivityDelegate((ActivityDelegate) delegate);
    } else if (cls == GeckoRuntime.Delegate.class) {
      runtime.setDelegate((GeckoRuntime.Delegate) delegate);
    } else if (cls == OrientationController.OrientationDelegate.class) {
      runtime
          .getOrientationController()
          .setDelegate((OrientationController.OrientationDelegate) delegate);
    } else if (cls == ServiceWorkerDelegate.class) {
      runtime.setServiceWorkerDelegate((ServiceWorkerDelegate) delegate);
    } else if (cls == WebNotificationDelegate.class) {
      runtime.setWebNotificationDelegate((WebNotificationDelegate) delegate);
    } else if (cls == WebExtensionController.PromptDelegate.class) {
      runtime
          .getWebExtensionController()
          .setPromptDelegate((WebExtensionController.PromptDelegate) delegate);
    } else if (cls == WebPushDelegate.class) {
      runtime.getWebPushController().setDelegate((WebPushDelegate) delegate);
    } else {
      throw new IllegalStateException("Unknown runtime delegate " + cls.getName());
    }
  }

  protected static Object getRuntimeDelegate(
      final @NonNull Class<?> cls, final @NonNull GeckoRuntime runtime) {
    if (cls == Autocomplete.StorageDelegate.class) {
      return runtime.getAutocompleteStorageDelegate();
    } else if (cls == ActivityDelegate.class) {
      return runtime.getActivityDelegate();
    } else if (cls == GeckoRuntime.Delegate.class) {
      return runtime.getDelegate();
    } else if (cls == OrientationController.OrientationDelegate.class) {
      return runtime.getOrientationController().getDelegate();
    } else if (cls == ServiceWorkerDelegate.class) {
      return runtime.getServiceWorkerDelegate();
    } else if (cls == WebNotificationDelegate.class) {
      return runtime.getWebNotificationDelegate();
    } else if (cls == WebExtensionController.PromptDelegate.class) {
      return runtime.getWebExtensionController().getPromptDelegate();
    } else if (cls == WebPushDelegate.class) {
      return runtime.getWebPushController().getDelegate();
    } else {
      throw new IllegalStateException("Unknown runtime delegate " + cls.getName());
    }
  }

  protected static Object getDelegate(
      final @NonNull Class<?> cls, final @NonNull GeckoSession session)
      throws NoSuchMethodException, IllegalAccessException, InvocationTargetException {
    if (cls == GeckoSession.TextInputDelegate.class) {
      return SessionTextInput.class.getMethod("getDelegate").invoke(session.getTextInput());
    }
    if (cls == ContentBlocking.Delegate.class) {
      return GeckoSession.class.getMethod("getContentBlockingDelegate").invoke(session);
    }
    if (cls == Autofill.Delegate.class) {
      return GeckoSession.class.getMethod("getAutofillDelegate").invoke(session);
    }
    if (cls == MediaSession.Delegate.class) {
      return GeckoSession.class.getMethod("getMediaSessionDelegate").invoke(session);
    }
    if (cls == TranslationsController.SessionTranslation.Delegate.class) {
      return GeckoSession.class.getMethod("getTranslationsSessionDelegate").invoke(session);
    }
    return GeckoSession.class.getMethod("get" + cls.getSimpleName()).invoke(session);
  }

  @NonNull
  private Set<Class<?>> getCurrentDelegates() {
    final List<ExternalDelegate<?>> waitDelegates = mWaitScopeDelegates.getExternalDelegates();
    final List<ExternalDelegate<?>> testDelegates = mTestScopeDelegates.getExternalDelegates();

    final Set<Class<?>> set = new HashSet<>(DEFAULT_DELEGATES);
    set.addAll(DEFAULT_RUNTIME_DELEGATES);

    for (final ExternalDelegate<?> delegate : waitDelegates) {
      set.add(delegate.delegate);
    }
    for (final ExternalDelegate<?> delegate : testDelegates) {
      set.add(delegate.delegate);
    }
    return set;
  }

  private void addNullDelegate(final Class<?> delegate) {
    assertThat(
        "Null-delegate must be valid interface class",
        delegate,
        either(isIn(DEFAULT_DELEGATES)).or(isIn(DEFAULT_RUNTIME_DELEGATES)));
    mNullDelegates.add(delegate);
  }

  protected void applyAnnotations(
      final Collection<Annotation> annotations, final GeckoSessionSettings settings) {
    for (final Annotation annotation : annotations) {
      if (TimeoutMillis.class.equals(annotation.annotationType())) {
        // Scale timeout based on the default timeout to account for the device under test.
        final long value = ((TimeoutMillis) annotation).value();
        final long timeout =
            value * env.getScaledTimeoutMillis() / Environment.DEFAULT_TIMEOUT_MILLIS;
        mTimeoutMillis = Math.max(timeout, 1000);
      } else if (Setting.class.equals(annotation.annotationType())) {
        ((Setting) annotation).key().set(settings, ((Setting) annotation).value());
      } else if (Setting.List.class.equals(annotation.annotationType())) {
        for (final Setting setting : ((Setting.List) annotation).value()) {
          setting.key().set(settings, setting.value());
        }
      } else if (NullDelegate.class.equals(annotation.annotationType())) {
        addNullDelegate(((NullDelegate) annotation).value());
      } else if (NullDelegate.List.class.equals(annotation.annotationType())) {
        for (final NullDelegate nullDelegate : ((NullDelegate.List) annotation).value()) {
          addNullDelegate(nullDelegate.value());
        }
      } else if (WithDisplay.class.equals(annotation.annotationType())) {
        final WithDisplay displaySize = (WithDisplay) annotation;
        mDisplaySize = new Point(displaySize.width(), displaySize.height());
      } else if (ClosedSessionAtStart.class.equals(annotation.annotationType())) {
        mClosedSession = ((ClosedSessionAtStart) annotation).value();
      } else if (IgnoreCrash.class.equals(annotation.annotationType())) {
        mIgnoreCrash = ((IgnoreCrash) annotation).value();
      }
    }
  }

  private static RuntimeException unwrapRuntimeException(final Throwable e) {
    final Throwable cause = e.getCause();
    if (cause instanceof RuntimeException) {
      return (RuntimeException) cause;
    } else if (e instanceof RuntimeException) {
      return (RuntimeException) e;
    }

    return new RuntimeException(cause != null ? cause : e);
  }

  protected void prepareStatement(final Description description) {
    final GeckoSessionSettings settings = new GeckoSessionSettings(mDefaultSettings);
    mTimeoutMillis = env.getDefaultTimeoutMillis();
    mNullDelegates = new HashSet<>();
    mClosedSession = false;
    mIgnoreCrash = false;

    applyAnnotations(Arrays.asList(description.getTestClass().getAnnotations()), settings);
    applyAnnotations(description.getAnnotations(), settings);

    final List<CallRecord> records = new ArrayList<>();
    final CallbackDelegates waitDelegates = new CallbackDelegates();
    final CallbackDelegates testDelegates = new CallbackDelegates();
    mCallRecords = records;
    mWaitScopeDelegates = waitDelegates;
    mTestScopeDelegates = testDelegates;
    mLastWaitStart = 0;
    mLastWaitEnd = 0;

    final InvocationHandler recorder =
        new InvocationHandler() {
          @Override
          public Object invoke(final Object proxy, final Method method, final Object[] args) {
            boolean ignore = false;
            MethodCall call = null;

            if (Object.class.equals(method.getDeclaringClass())) {
              switch (method.getName()) {
                case "equals":
                  return proxy == args[0];
                case "toString":
                  return "Call Recorder";
              }
              ignore = true;
            } else if (mCallRecordHandler != null) {
              ignore = mCallRecordHandler.handleCall(method, args);
            }

            final boolean isDefaultDelegate =
                DEFAULT_DELEGATES.contains(method.getDeclaringClass());
            final boolean isDefaultRuntimeDelegate =
                DEFAULT_RUNTIME_DELEGATES.contains(method.getDeclaringClass());

            if (!ignore) {
              if (isDefaultDelegate) {
                ThreadUtils.assertOnUiThread();
              }

              final GeckoSession session;
              if (!isDefaultDelegate) {
                session = null;
              } else {
                assertThat(
                    "Callback first argument must be session object",
                    args,
                    arrayWithSize(greaterThan(0)));
                assertThat(
                    "Callback first argument must be session object",
                    args[0],
                    instanceOf(GeckoSession.class));
                session = (GeckoSession) args[0];
              }

              if ((sOnCrash.equals(method) || sOnKill.equals(method))
                  && !mIgnoreCrash
                  && isUsingSession(session)) {
                if (env.shouldShutdownOnCrash()) {
                  getRuntime().shutdown();
                }

                throw new ChildCrashedException("Child process crashed");
              }

              records.add(new CallRecord(session, method, args));

              call = waitDelegates.prepareMethodCall(session, method);
              if (call == null) {
                call = testDelegates.prepareMethodCall(session, method);
              }

              if (!isDefaultDelegate && !isDefaultRuntimeDelegate) {
                assertThat("External delegate should be registered", call, notNullValue());
              }
            }

            Object returnValue = null;
            try {
              mCurrentMethodCall = call;
              if (call != null && call.target != null) {
                returnValue = method.invoke(call.target, args);
              } else {
                returnValue = method.invoke(DEFAULT_IMPL, args);
              }
            } catch (final IllegalAccessException | InvocationTargetException e) {
              throw unwrapRuntimeException(e);
            } finally {
              mCurrentMethodCall = null;
            }

            return returnValue;
          }
        };

    final Set<Class<?>> delegates = new HashSet<>();
    delegates.addAll(DEFAULT_DELEGATES);
    delegates.addAll(DEFAULT_RUNTIME_DELEGATES);
    final Class<?>[] classes = delegates.toArray(new Class<?>[delegates.size()]);
    mCallbackProxy = Proxy.newProxyInstance(GeckoSession.class.getClassLoader(), classes, recorder);
    mAllDelegates = new HashSet<>(delegates);

    mMainSession = new GeckoSession(settings);
    prepareSession(mMainSession);
    prepareRuntime(getRuntime());

    if (mDisplaySize != null) {
      addDisplay(mMainSession, mDisplaySize.x, mDisplaySize.y);
    }

    if (!mClosedSession) {
      openSession(mMainSession);
      UiThreadUtils.waitForCondition(
          () -> RuntimeCreator.sTestSupport.get() != RuntimeCreator.TEST_SUPPORT_INITIAL,
          env.getDefaultTimeoutMillis());
      if (RuntimeCreator.sTestSupport.get() != RuntimeCreator.TEST_SUPPORT_OK) {
        throw new RuntimeException("Could not register TestSupport, see logs for error.");
      }
    }
  }

  protected void prepareRuntime(final GeckoRuntime runtime) {
    UiThreadUtils.waitForCondition(
        () -> RuntimeCreator.sTestSupport.get() != RuntimeCreator.TEST_SUPPORT_INITIAL,
        env.getDefaultTimeoutMillis());
    for (final Class<?> cls : DEFAULT_RUNTIME_DELEGATES) {
      setRuntimeDelegate(cls, runtime, mNullDelegates.contains(cls) ? null : mCallbackProxy);
    }
  }

  protected void prepareSession(final GeckoSession session) {
    UiThreadUtils.waitForCondition(
        () -> RuntimeCreator.sTestSupport.get() != RuntimeCreator.TEST_SUPPORT_INITIAL,
        env.getDefaultTimeoutMillis());
    session
        .getWebExtensionController()
        .setMessageDelegate(RuntimeCreator.sTestSupportExtension, mMessageDelegate, "browser");
    for (final Class<?> cls : DEFAULT_DELEGATES) {
      try {
        setDelegate(cls, session, mNullDelegates.contains(cls) ? null : mCallbackProxy);
      } catch (final NoSuchMethodException | IllegalAccessException | InvocationTargetException e) {
        throw new RuntimeException(e);
      }
    }
  }

  /**
   * Call open() on a session, and ensure it's ready for use by the test. In particular, remove any
   * extra calls recorded as part of opening the session.
   *
   * @param session Session to open.
   */
  public void openSession(final GeckoSession session) {
    ThreadUtils.assertOnUiThread();
    // We receive an initial about:blank load; don't expose that to the test. The initial
    // load ends with the first onPageStop call, so ignore everything from the session
    // until the first onPageStop call.

    try {
      // We cannot detect initial page load without progress delegate.
      assertThat(
          "ProgressDelegate cannot be null-delegate when opening session",
          GeckoSession.ProgressDelegate.class,
          not(isIn(mNullDelegates)));
      mCallRecordHandler =
          (method, args) -> {
            Log.e(LOGTAG, "method: " + method);
            final boolean matching =
                DEFAULT_DELEGATES.contains(method.getDeclaringClass()) && session.equals(args[0]);
            if (matching && sOnPageStop.equals(method)) {
              mCallRecordHandler = null;
            }
            return matching;
          };

      session.open(getRuntime());

      UiThreadUtils.waitForCondition(
          () -> mCallRecordHandler == null, env.getDefaultTimeoutMillis());
    } finally {
      mCallRecordHandler = null;
    }
  }

  private void waitForOpenSession(final GeckoSession session) {
    ThreadUtils.assertOnUiThread();
    // We receive an initial about:blank load; don't expose that to the test. The initial
    // load ends with the first onPageStop call, so ignore everything from the session
    // until the first onPageStop call.

    try {
      // We cannot detect initial page load without progress delegate.
      assertThat(
          "ProgressDelegate cannot be null-delegate when opening session",
          GeckoSession.ProgressDelegate.class,
          not(isIn(mNullDelegates)));
      mCallRecordHandler =
          (method, args) -> {
            Log.e(LOGTAG, "method: " + method);
            final boolean matching =
                DEFAULT_DELEGATES.contains(method.getDeclaringClass()) && session.equals(args[0]);
            if (matching && sOnPageStop.equals(method)) {
              mCallRecordHandler = null;
            }
            return matching;
          };

      UiThreadUtils.waitForCondition(
          () -> mCallRecordHandler == null, env.getDefaultTimeoutMillis());
    } finally {
      mCallRecordHandler = null;
    }
  }

  /** Internal method to perform callback checks at the end of a test. */
  public void performTestEndCheck() {
    mWaitScopeDelegates.clearAndAssert();
    mTestScopeDelegates.clearAndAssert();
  }

  protected void cleanupRuntime(final GeckoRuntime runtime) {
    for (final Class<?> cls : DEFAULT_RUNTIME_DELEGATES) {
      setRuntimeDelegate(cls, runtime, null);
    }
  }

  protected void cleanupSession(final GeckoSession session) {
    if (session.isOpen()) {
      session.close();
    }
    releaseDisplay(session);
  }

  protected boolean isUsingSession(final GeckoSession session) {
    return session.equals(mMainSession) || mSubSessions.contains(session);
  }

  protected void deleteCrashDumps() {
    final File dumpDir = new File(getProfilePath(), "minidumps");
    for (final File dump : dumpDir.listFiles()) {
      dump.delete();
    }
  }

  protected void cleanupExtensions() throws Throwable {
    final WebExtensionController controller = getRuntime().getWebExtensionController();
    final List<WebExtension> list = waitForResult(controller.list(), env.getDefaultTimeoutMillis());

    boolean hasTestSupport = false;
    // Uninstall any left-over extensions
    for (final WebExtension extension : list) {
      if (!extension.id.equals(RuntimeCreator.TEST_SUPPORT_EXTENSION_ID)) {
        waitForResult(controller.uninstall(extension), env.getDefaultTimeoutMillis());
      } else {
        hasTestSupport = true;
      }
    }

    // If an extension was still installed, this test should fail.
    // Note the test support extension is always kept for speed.
    assertThat(
        "A WebExtension was left installed during this test.",
        list.size(),
        equalTo(hasTestSupport ? 1 : 0));
  }

  protected void cleanupStatement() throws Throwable {
    mWaitScopeDelegates.clear();
    mTestScopeDelegates.clear();

    for (final GeckoSession session : mSubSessions) {
      cleanupSession(session);
    }

    cleanupRuntime(getRuntime());
    cleanupSession(mMainSession);
    cleanupExtensions();

    if (mIgnoreCrash) {
      deleteCrashDumps();
    }

    mMainSession = null;
    mCallbackProxy = null;
    mAllDelegates = null;
    mNullDelegates = null;
    mCallRecords = null;
    mWaitScopeDelegates = null;
    mTestScopeDelegates = null;
    mLastWaitStart = 0;
    mLastWaitEnd = 0;
    mTimeoutMillis = 0;
    RuntimeCreator.setExperimentDelegate(null);
  }

  // These markers are used by runjunit.py to capture the logcat of a test
  private static final String TEST_START_MARKER = "test_start 1f0befec-3ff2-40ff-89cf-b127eb38b1ec";
  private static final String TEST_END_MARKER = "test_end c5ee677f-bc83-49bd-9e28-2d35f3d0f059";

  @Override
  public Statement apply(final Statement base, final Description description) {
    return new Statement() {
      private TestServer mServer;

      private void initTest() {
        try {
          mServer.start(TEST_PORT);

          RuntimeCreator.setPortDelegate(mMessageDelegate);
          getRuntime();

          Log.e(LOGTAG, TEST_START_MARKER + " " + description);
          Log.e(LOGTAG, "before prepareStatement " + description);
          prepareStatement(description);
          Log.e(LOGTAG, "after prepareStatement");
        } catch (final Throwable t) {
          // Any error here is not related to a specific test
          throw new TestHarnessException(t);
        }
      }

      @Override
      public void evaluate() throws Throwable {
        final AtomicReference<Throwable> exceptionRef = new AtomicReference<>();

        mServer =
            new TestServer(
                InstrumentationRegistry.getInstrumentation().getTargetContext(),
                mServerCustomHeaders,
                mResponseModifiers);

        mInstrumentation.runOnMainSync(
            () -> {
              try {
                initTest();
                base.evaluate();
                Log.e(LOGTAG, "after evaluate");
                performTestEndCheck();
                Log.e(LOGTAG, "after performTestEndCheck");
              } catch (final Throwable t) {
                Log.e(LOGTAG, "Error", t);
                exceptionRef.set(t);
              } finally {
                try {
                  mServer.stop();
                  cleanupStatement();
                } catch (final Throwable t) {
                  exceptionRef.compareAndSet(null, t);
                }
                Log.e(LOGTAG, TEST_END_MARKER + " " + description);
              }
            });

        final Throwable throwable = exceptionRef.get();
        if (throwable != null) {
          throw throwable;
        }
      }
    };
  }

  /** This simply sends an empty message to the web content and waits for a reply. */
  public void waitForRoundTrip(final GeckoSession session) {
    waitForJS(session, "true");
  }

  /**
   * Wait until a page load has finished on any session. A session must have started a page load
   * since the last wait, or this method will wait indefinitely.
   */
  public void waitForPageStop() {
    waitForPageStop(/* session */ null);
  }

  /**
   * Wait until a page load has finished. The session must have started a page load since the last
   * wait, or this method will wait indefinitely.
   *
   * @param session Session to wait on, or null to wait on any session.
   */
  public void waitForPageStop(final GeckoSession session) {
    waitForPageStops(session, /* count */ 1);
  }

  /**
   * Wait until a page load has finished on any session. A session must have started a page load
   * since the last wait, or this method will wait indefinitely.
   *
   * @param count Number of page loads to wait for.
   */
  public void waitForPageStops(final int count) {
    waitForPageStops(/* session */ null, count);
  }

  /**
   * Wait until a page load has finished. The session must have started a page load since the last
   * wait, or this method will wait indefinitely.
   *
   * @param session Session to wait on, or null to wait on any session.
   * @param count Number of page loads to wait for.
   */
  public void waitForPageStops(final GeckoSession session, final int count) {
    final List<MethodCall> methodCalls = new ArrayList<>(1);
    methodCalls.add(
        new MethodCall(session, sOnPageStop, new CallRequirement(/* allowed */ true, count, null)));

    waitUntilCalled(session, GeckoSession.ProgressDelegate.class, methodCalls, null);
  }

  /**
   * Wait until the specified methods have been called on the specified callback interface for any
   * session. If no methods are specified, wait until any method has been called.
   *
   * @param callback Target callback interface; must be an interface under GeckoSession.
   * @param methods List of methods to wait on; use empty or null or wait on any method.
   */
  public void waitUntilCalled(
      final @NonNull KClass<?> callback, final @Nullable String... methods) {
    waitUntilCalled(/* session */ null, callback, methods);
  }

  /**
   * Wait until the specified methods have been called on the specified callback interface. If no
   * methods are specified, wait until any method has been called.
   *
   * @param session Session to wait on, or null to wait on any session.
   * @param callback Target callback interface; must be an interface under GeckoSession.
   * @param methods List of methods to wait on; use empty or null or wait on any method.
   */
  public void waitUntilCalled(
      final @Nullable GeckoSession session,
      final @NonNull KClass<?> callback,
      final @Nullable String... methods) {
    waitUntilCalled(session, JvmClassMappingKt.getJavaClass(callback), methods);
  }

  /**
   * Wait until the specified methods have been called on the specified callback interface for any
   * session. If no methods are specified, wait until any method has been called.
   *
   * @param callback Target callback interface; must be an interface under GeckoSession.
   * @param methods List of methods to wait on; use empty or null or wait on any method.
   */
  public void waitUntilCalled(final @NonNull Class<?> callback, final @Nullable String... methods) {
    waitUntilCalled(/* session */ null, callback, methods);
  }

  /**
   * Wait until the specified methods have been called on the specified callback interface. If no
   * methods are specified, wait until any method has been called.
   *
   * @param session Session to wait on, or null to wait on any session.
   * @param callback Target callback interface; must be an interface under GeckoSession.
   * @param methods List of methods to wait on; use empty or null or wait on any method.
   */
  public void waitUntilCalled(
      final @Nullable GeckoSession session,
      final @NonNull Class<?> callback,
      final @Nullable String... methods) {
    final int length = (methods != null) ? methods.length : 0;
    final Pattern[] patterns = new Pattern[length];
    for (int i = 0; i < length; i++) {
      patterns[i] = Pattern.compile(methods[i]);
    }

    final List<MethodCall> waitMethods = new ArrayList<>();
    boolean isSessionCallback = false;

    for (final Class<?> ifce : getCurrentDelegates()) {
      if (!ifce.isAssignableFrom(callback)) {
        continue;
      }
      for (final Method method : ifce.getMethods()) {
        for (final Pattern pattern : patterns) {
          if (!pattern.matcher(method.getName()).matches()) {
            continue;
          }
          waitMethods.add(new MethodCall(session, method, new CallRequirement(true, -1, null)));
          break;
        }
      }
      isSessionCallback = true;
    }

    assertThat(
        "Delegate should be a GeckoSession delegate " + "or registered external delegate",
        isSessionCallback,
        equalTo(true));

    waitUntilCalled(session, callback, waitMethods, null);
  }

  /**
   * Wait until the specified methods have been called on the specified object for any session, as
   * specified by any {@link AssertCalled @AssertCalled} annotations. If no {@link
   * AssertCalled @AssertCalled} annotations are found, wait until any method has been called. Only
   * methods belonging to a GeckoSession callback are supported.
   *
   * @param callback Target callback object; must implement an interface under GeckoSession.
   */
  public void waitUntilCalled(final @NonNull Object callback) {
    waitUntilCalled(/* session */ null, callback);
  }

  /**
   * Wait until the specified methods have been called on the specified object, as specified by any
   * {@link AssertCalled @AssertCalled} annotations. If no {@link AssertCalled @AssertCalled}
   * annotations are found, wait until any method has been called. Only methods belonging to a
   * GeckoSession callback are supported.
   *
   * @param session Session to wait on, or null to wait on any session.
   * @param callback Target callback object; must implement an interface under GeckoSession.
   */
  public void waitUntilCalled(
      final @Nullable GeckoSession session, final @NonNull Object callback) {
    if (callback instanceof Class<?>) {
      waitUntilCalled(session, (Class<?>) callback, (String[]) null);
      return;
    }

    final List<MethodCall> methodCalls = new ArrayList<>();
    boolean isSessionCallback = false;

    for (final Class<?> ifce : getCurrentDelegates()) {
      if (!ifce.isInstance(callback)) {
        continue;
      }
      for (final Method method : ifce.getMethods()) {
        final Method callbackMethod;
        try {
          callbackMethod =
              callback.getClass().getMethod(method.getName(), method.getParameterTypes());
        } catch (final NoSuchMethodException e) {
          throw new RuntimeException(e);
        }
        final AssertCalled ac = getAssertCalled(callbackMethod, callback);
        methodCalls.add(new MethodCall(session, method, ac, /* target */ null));
      }
      isSessionCallback = true;
    }

    assertThat(
        "Delegate should implement a GeckoSession, GeckoRuntime delegate "
            + "or registered external delegate",
        isSessionCallback,
        equalTo(true));

    waitUntilCalled(session, callback.getClass(), methodCalls, callback);
  }

  /**
   * * Implement this interface in {@link #waitUntilCalled} to allow waiting until this method
   * returns true. E.g. for when the test needs to wait for a specific value on a delegate call.
   */
  public interface ShouldContinue {
    /**
     * Whether the test should keep waiting or not.
     *
     * @return true if the test should keep waiting.
     */
    default boolean shouldContinue() {
      return false;
    }
  }

  private void waitUntilCalled(
      final @Nullable GeckoSession session,
      final @NonNull Class<?> delegate,
      final @NonNull List<MethodCall> methodCalls,
      final @Nullable Object callback) {
    ThreadUtils.assertOnUiThread();

    if (session != null && !session.equals(mMainSession)) {
      assertThat("Session should be wrapped through wrapSession", session, isIn(mSubSessions));
    }

    // Make sure all handlers are set though #delegateUntilTestEnd or #delegateDuringNextWait,
    // instead of through GeckoSession directly, so that we can still record calls even with
    // custom handlers set.
    for (final Class<?> ifce : DEFAULT_DELEGATES) {
      final Object sessionDelegate;
      try {
        sessionDelegate = getDelegate(ifce, session == null ? mMainSession : session);
      } catch (final NoSuchMethodException | IllegalAccessException | InvocationTargetException e) {
        throw unwrapRuntimeException(e);
      }
      if (mNullDelegates.contains(ifce)) {
        // Null-delegates are initially null but are allowed to be any value.
        continue;
      }
      assertThat(
          ifce.getSimpleName()
              + " callbacks should be "
              + "accessed through GeckoSessionTestRule delegate methods",
          sessionDelegate,
          sameInstance(mCallbackProxy));
    }

    for (final Class<?> ifce : DEFAULT_RUNTIME_DELEGATES) {
      final Object runtimeDelegate = getRuntimeDelegate(ifce, getRuntime());
      if (mNullDelegates.contains(ifce)) {
        // Null-delegates are initially null but are allowed to be any value.
        continue;
      }
      assertThat(
          ifce.getSimpleName()
              + " callbacks should be "
              + "accessed through GeckoSessionTestRule delegate methods",
          runtimeDelegate,
          sameInstance(mCallbackProxy));
    }

    if (methodCalls.isEmpty()) {
      // Waiting for any call on `delegate`; make sure it doesn't contain any null-delegates.
      for (final Class<?> ifce : mNullDelegates) {
        assertThat(
            "Cannot wait on null-delegate callbacks", delegate, not(typeCompatibleWith(ifce)));
      }
    } else {
      // Waiting for particular calls; make sure those calls aren't from a null-delegate.
      for (final MethodCall call : methodCalls) {
        assertThat(
            "Cannot wait on null-delegate callbacks",
            call.method.getDeclaringClass(),
            not(isIn(mNullDelegates)));
      }
    }

    boolean calledAny = false;
    int index = mLastWaitEnd;
    final long startTime = SystemClock.uptimeMillis();

    beforeWait();

    ShouldContinue cont = new ShouldContinue() {};
    if (callback instanceof ShouldContinue) {
      cont = (ShouldContinue) callback;
    }

    List<MethodCall> pendingMethodCalls =
        methodCalls.stream()
            .filter(
                mc -> mc.requirement != null && mc.requirement.count != 0 && mc.requirement.allowed)
            .collect(Collectors.toList());

    int order = 0;
    while (!calledAny || !pendingMethodCalls.isEmpty() || cont.shouldContinue()) {
      final int currentIndex = index;

      // Let's wait for more messages if we reached the end
      UiThreadUtils.waitForCondition(() -> (currentIndex < mCallRecords.size()), mTimeoutMillis);

      if (SystemClock.uptimeMillis() - startTime > mTimeoutMillis) {
        throw new UiThreadUtils.TimeoutException("Timed out after " + mTimeoutMillis + "ms");
      }

      final CallRecord record = mCallRecords.get(index);
      final MethodCall recorded = record.methodCall;

      final boolean isDelegate = recorded.method.getDeclaringClass().isAssignableFrom(delegate);

      calledAny |= isDelegate;
      index++;

      final int i = methodCalls.indexOf(recorded);
      if (i < 0) {
        continue;
      }

      final MethodCall methodCall = methodCalls.get(i);
      assertAllowMoreCalls(methodCall);

      methodCall.incrementCounter();
      assertOrder(methodCall, order);
      order = Math.max(methodCall.getOrder(), order);

      if (methodCall.allowUnlimitedCalls() || !methodCall.allowMoreCalls()) {
        pendingMethodCalls.remove(methodCall);
      }

      if (isDelegate && callback != null) {
        try {
          mCurrentMethodCall = methodCall;
          record.method.invoke(callback, record.args);
        } catch (IllegalAccessException | InvocationTargetException e) {
          throw unwrapRuntimeException(e);
        } finally {
          mCurrentMethodCall = null;
        }
      }
    }

    afterWait(index);
  }

  protected void beforeWait() {
    mLastWaitStart = mLastWaitEnd;
  }

  protected void afterWait(final int endCallIndex) {
    mLastWaitEnd = endCallIndex;
    mWaitScopeDelegates.clearAndAssert();

    // Register any test-delegates that were not registered due to wait-delegates
    // having precedence.
    for (final ExternalDelegate<?> delegate : mTestScopeDelegates.getExternalDelegates()) {
      delegate.register();
    }
  }

  /**
   * Playback callbacks that were made on all sessions during the previous wait. For any methods
   * annotated with {@link AssertCalled @AssertCalled}, assert that the callbacks satisfy the
   * specified requirements. If no {@link AssertCalled @AssertCalled} annotations are found, assert
   * any method has been called. Only methods belonging to a GeckoSession callback are supported.
   *
   * @param callback Target callback object; must implement one or more interfaces under
   *     GeckoSession.
   */
  public void forCallbacksDuringWait(final @NonNull Object callback) {
    forCallbacksDuringWait(/* session */ null, callback);
  }

  /**
   * Playback callbacks that were made during the previous wait. For any methods annotated with
   * {@link AssertCalled @AssertCalled}, assert that the callbacks satisfy the specified
   * requirements. If no {@link AssertCalled @AssertCalled} annotations are found, assert any method
   * has been called. Only methods belonging to a GeckoSession callback are supported.
   *
   * @param session Target session object, or null to playback all sessions.
   * @param callback Target callback object; must implement one or more interfaces under
   *     GeckoSession.
   */
  public void forCallbacksDuringWait(
      final @Nullable GeckoSession session, final @NonNull Object callback) {
    final Method[] declaredMethods = callback.getClass().getDeclaredMethods();
    final List<MethodCall> methodCalls = new ArrayList<>(declaredMethods.length);
    boolean assertingAnyCall = true;
    Class<?> foundNullDelegate = null;

    for (final Class<?> ifce : mAllDelegates) {
      if (!ifce.isInstance(callback)) {
        continue;
      }
      if (mNullDelegates.contains(ifce)) {
        foundNullDelegate = ifce;
      }
      for (final Method method : ifce.getMethods()) {
        final Method callbackMethod;
        try {
          callbackMethod =
              callback.getClass().getMethod(method.getName(), method.getParameterTypes());
        } catch (final NoSuchMethodException e) {
          throw new RuntimeException(e);
        }
        final MethodCall call =
            new MethodCall(
                session,
                callbackMethod,
                getAssertCalled(callbackMethod, callback),
                /* target */ null);
        methodCalls.add(call);

        if (call.requirement != null) {
          if (foundNullDelegate == ifce) {
            fail("Cannot assert on null-delegate " + ifce.getSimpleName());
          }
          assertingAnyCall = false;
        }
      }
    }

    if (assertingAnyCall && foundNullDelegate != null) {
      fail("Cannot assert on null-delegate " + foundNullDelegate.getSimpleName());
    }

    int order = 0;
    boolean calledAny = false;

    for (int index = mLastWaitStart; index < mLastWaitEnd; index++) {
      final CallRecord record = mCallRecords.get(index);

      if (!record.method.getDeclaringClass().isInstance(callback)
          || (session != null
              && DEFAULT_DELEGATES.contains(record.method.getDeclaringClass())
              && !session.equals(record.args[0]))) {
        continue;
      }

      final int i = methodCalls.indexOf(record.methodCall);
      checkThat(record.method.getName() + " should be found", i, greaterThanOrEqualTo(0));

      final MethodCall methodCall = methodCalls.get(i);
      assertAllowMoreCalls(methodCall);
      methodCall.incrementCounter();
      assertOrder(methodCall, order);
      order = Math.max(methodCall.getOrder(), order);

      try {
        mCurrentMethodCall = methodCall;
        record.method.invoke(callback, record.args);
      } catch (final IllegalAccessException | InvocationTargetException e) {
        throw unwrapRuntimeException(e);
      } finally {
        mCurrentMethodCall = null;
      }
      calledAny = true;
    }

    for (final MethodCall methodCall : methodCalls) {
      assertMatchesCount(methodCall);
      if (methodCall.requirement != null) {
        calledAny = true;
      }
    }

    checkThat(
        "Should have called one of " + Arrays.toString(callback.getClass().getInterfaces()),
        calledAny,
        equalTo(true));
  }

  /**
   * Get information about the current call. Only valid during a {@link #forCallbacksDuringWait},
   * {@link #delegateDuringNextWait}, or {@link #delegateUntilTestEnd} callback.
   *
   * @return Call information
   */
  public @NonNull CallInfo getCurrentCall() {
    assertThat("Should be in a method call", mCurrentMethodCall, notNullValue());
    return mCurrentMethodCall.getInfo();
  }

  /**
   * Delegate implemented interfaces to the specified callback object for all sessions, for the rest
   * of the test. Only GeckoSession callback interfaces are supported. Delegates for {@code
   * delegateUntilTestEnd} can be temporarily overridden by delegates for {@link
   * #delegateDuringNextWait}.
   *
   * @param callback Callback object, or null to clear all previously-set delegates.
   */
  public void delegateUntilTestEnd(final @NonNull Object callback) {
    delegateUntilTestEnd(/* session */ null, callback);
  }

  /**
   * Delegate implemented interfaces to the specified callback object, for the rest of the test.
   * Only GeckoSession callback interfaces are supported. Delegates for {@link
   * #delegateUntilTestEnd} can be temporarily overridden by delegates for {@link
   * #delegateDuringNextWait}.
   *
   * @param session Session to target, or null to target all sessions.
   * @param callback Callback object, or null to clear all previously-set delegates.
   */
  public void delegateUntilTestEnd(
      final @Nullable GeckoSession session, final @NonNull Object callback) {
    mTestScopeDelegates.delegate(session, callback);
  }

  /**
   * Delegate implemented interfaces to the specified callback object for all sessions, during the
   * next wait. Only GeckoSession callback interfaces are supported. Delegates for {@code
   * delegateDuringNextWait} can temporarily take precedence over delegates for {@link
   * #delegateUntilTestEnd}.
   *
   * @param callback Callback object, or null to clear all previously-set delegates.
   */
  public void delegateDuringNextWait(final @NonNull Object callback) {
    delegateDuringNextWait(/* session */ null, callback);
  }

  /**
   * Delegate implemented interfaces to the specified callback object, during the next wait. Only
   * GeckoSession callback interfaces are supported. Delegates for {@link #delegateDuringNextWait}
   * can temporarily take precedence over delegates for {@link #delegateUntilTestEnd}.
   *
   * @param session Session to target, or null to target all sessions.
   * @param callback Callback object, or null to clear all previously-set delegates.
   */
  public void delegateDuringNextWait(
      final @Nullable GeckoSession session, final @NonNull Object callback) {
    mWaitScopeDelegates.delegate(session, callback);
  }

  /**
   * Synthesize a tap event at the specified location using the main session. The session must have
   * been created with a display.
   *
   * @param session Target session
   * @param x X coordinate
   * @param y Y coordinate
   */
  public void synthesizeTap(final @NonNull GeckoSession session, final int x, final int y) {
    final long downTime = SystemClock.uptimeMillis();
    final MotionEvent down =
        MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), MotionEvent.ACTION_DOWN, x, y, 0);
    session.getPanZoomController().onTouchEvent(down);

    final MotionEvent up =
        MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), MotionEvent.ACTION_UP, x, y, 0);
    session.getPanZoomController().onTouchEvent(up);
  }

  /**
   * Synthesize a mouse event at the specified location using the main session. The session must
   * have been created with a display.
   *
   * @param session Target session
   * @param downTime A time when any buttons are down
   * @param action An action such as MotionEvent.ACTION_DOWN
   * @param x X coordinate
   * @param y Y coordinate
   * @param buttonState A button stats such as MotionEvent.BUTTON_PRIMARY
   */
  public void synthesizeMouse(
      final @NonNull GeckoSession session,
      final long downTime,
      final int action,
      final int x,
      final int y,
      final int buttonState) {
    final MotionEvent.PointerProperties pointerProperty = new MotionEvent.PointerProperties();
    pointerProperty.id = 0;
    pointerProperty.toolType = MotionEvent.TOOL_TYPE_MOUSE;

    final MotionEvent.PointerCoords pointerCoord = new MotionEvent.PointerCoords();
    pointerCoord.x = x;
    pointerCoord.y = y;

    final MotionEvent.PointerProperties[] pointerProperties =
        new MotionEvent.PointerProperties[] {pointerProperty};
    final MotionEvent.PointerCoords[] pointerCoords =
        new MotionEvent.PointerCoords[] {pointerCoord};

    final MotionEvent moveEvent =
        MotionEvent.obtain(
            downTime,
            SystemClock.uptimeMillis(),
            action,
            1,
            pointerProperties,
            pointerCoords,
            0,
            buttonState,
            1.0f,
            1.0f,
            0,
            0,
            InputDevice.SOURCE_MOUSE,
            0);
    session.getPanZoomController().onTouchEvent(moveEvent);
  }

  /**
   * Synthesize a mouse move event at the specified location using the main session. The session
   * must have been created with a display.
   *
   * @param session Target session
   * @param x X coordinate
   * @param y Y coordinate
   */
  public void synthesizeMouseMove(final @NonNull GeckoSession session, final int x, final int y) {
    final long moveTime = SystemClock.uptimeMillis();
    synthesizeMouse(session, moveTime, MotionEvent.ACTION_HOVER_MOVE, x, y, 0);
  }

  /**
   * Simulates a press to the Home button, causing the application to go to onPause. NB: Some time
   * must elapse for the event to fully occur.
   *
   * @param context starting the Home intent
   */
  public void simulatePressHome(Context context) {
    Intent intent = new Intent();
    intent.setAction(Intent.ACTION_MAIN);
    intent.addCategory(Intent.CATEGORY_HOME);
    intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    context.startActivity(intent);
  }

  /**
   * Simulates returningGeckoViewTestActivity to the foreground. Activity must already be in use.
   * NB: Some time must elapse for the event to fully occur.
   *
   * @param context starting the intent
   */
  public void requestActivityToForeground(Context context) {
    Intent notificationIntent = new Intent(context, GeckoViewTestActivity.class);
    notificationIntent.setAction(Intent.ACTION_MAIN);
    notificationIntent.addCategory(Intent.CATEGORY_LAUNCHER);
    notificationIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    context.startActivity(notificationIntent);
  }

  /**
   * Mock Location Provider can be used in testing for creating mock locations. NB: Likely also need
   * to set test setting geo.provider.testing to false to prevent network geolocation from
   * interfering when using.
   */
  public class MockLocationProvider {

    private final LocationManager locationManager;
    private final String mockProviderName;
    private boolean isActiveTestProvider = false;
    private double mockLatitude;
    private double mockLongitude;
    private float mockAccuracy = .000001f;
    private boolean doContinuallyPost;

    @Nullable private ScheduledExecutorService executor;

    /**
     * Mock Location Provider adds a test provider to the location manager and controls sending mock
     * locations. Use @{@link #postLocation()} to post the location to the location manager.
     * Use @{@link #removeMockLocationProvider()} to remove the location provider to clean-up the
     * test harness. Default accuracy is .000001f.
     *
     * @param locationManager location manager to accept the locations
     * @param mockProviderName location provider that will use this location
     * @param mockLatitude initial latitude in degrees that @{@link #postLocation()} will use
     * @param mockLongitude initial longitude in degrees that @{@link #postLocation()} will use
     * @param doContinuallyPost when posting a location, continue to post every 3s to keep location
     *     current
     */
    public MockLocationProvider(
        LocationManager locationManager,
        String mockProviderName,
        double mockLatitude,
        double mockLongitude,
        boolean doContinuallyPost) {
      this.locationManager = locationManager;
      this.mockProviderName = mockProviderName;
      this.mockLatitude = mockLatitude;
      this.mockLongitude = mockLongitude;
      this.doContinuallyPost = doContinuallyPost;
      addMockLocationProvider();
    }

    /** Adds a mock location provider that can have locations manually set. */
    private void addMockLocationProvider() {
      // Ensures that only one location provider with this name exists
      removeMockLocationProvider();
      locationManager.addTestProvider(
          mockProviderName,
          false,
          false,
          false,
          false,
          false,
          false,
          false,
          Criteria.POWER_LOW,
          Criteria.ACCURACY_FINE);
      locationManager.setTestProviderEnabled(mockProviderName, true);
      isActiveTestProvider = true;
    }

    /**
     * Removes the location provider. Recommend calling when ending test to prevent the mock
     * provider remaining as a test provider.
     */
    public void removeMockLocationProvider() {
      stopPostingLocation();
      try {
        locationManager.removeTestProvider(mockProviderName);
      } catch (Exception e) {
        // Throws an exception if there is no provider with that name
      }
      isActiveTestProvider = false;
    }

    /**
     * Sets the mock location on MockLocationProvider, that will be used by @{@link #postLocation()}
     *
     * @param latitude latitude in degrees to mock
     * @param longitude longitude in degrees to mock
     */
    public void setMockLocation(double latitude, double longitude) {
      mockLatitude = latitude;
      mockLongitude = longitude;
    }

    /**
     * Sets the mock location on a MockLocationProvider, that will be used by @{@link
     * #postLocation()} . Note, changing the accuracy can affect the importance of the mock provider
     * compared to other location providers.
     *
     * @param latitude latitude in degrees to mock
     * @param longitude longitude in degrees to mock
     * @param accuracy horizontal accuracy in meters to mock
     */
    public void setMockLocation(double latitude, double longitude, float accuracy) {
      mockLatitude = latitude;
      mockLongitude = longitude;
      mockAccuracy = accuracy;
    }

    /**
     * When doContinuallyPost is set to true, @{@link #postLocation()} will post the location to the
     * location manager every 2s. When set to false, @{@link #postLocation()} will only post the
     * location once. Purpose is to prevent the location from becoming stale.
     *
     * @param doContinuallyPost setting for continually posting the location after calling @{@link
     *     #postLocation()}
     */
    public void setDoContinuallyPost(boolean doContinuallyPost) {
      this.doContinuallyPost = doContinuallyPost;
    }

    /**
     * Shutsdown and removes the executor created by @{@link #postLocation()} when @{@link
     * #doContinuallyPost is true} to stop posting the location.
     */
    public void stopPostingLocation() {
      if (executor != null) {
        executor.shutdownNow();
        executor = null;
      }
    }

    /**
     * Posts the set location to the system location manager. If @{@link #doContinuallyPost} is
     * true, the location will be posted every 2s by an executor, otherwise will post once.
     */
    public void postLocation() {
      if (!isActiveTestProvider) {
        throw new IllegalStateException("The mock test provider is not active.");
      }

      // Ensure the thread that was posting a location (if applicable) is stopped.
      stopPostingLocation();

      // Set Location
      Location location = new Location(mockProviderName);
      location.setAccuracy(mockAccuracy);
      location.setLatitude(mockLatitude);
      location.setLongitude(mockLongitude);
      location.setElapsedRealtimeNanos(SystemClock.elapsedRealtimeNanos());
      location.setTime(System.currentTimeMillis());
      locationManager.setTestProviderLocation(mockProviderName, location);
      Log.i(
          LOGTAG,
          mockProviderName
              + " is posting location, lat: "
              + mockLatitude
              + " lon: "
              + mockLongitude
              + " acc: "
              + mockAccuracy);
      // Continually post location
      if (doContinuallyPost) {
        executor = Executors.newScheduledThreadPool(1);
        executor.scheduleAtFixedRate(
            new Runnable() {
              @Override
              public void run() {
                location.setElapsedRealtimeNanos(SystemClock.elapsedRealtimeNanos());
                location.setTime(System.currentTimeMillis());
                locationManager.setTestProviderLocation(mockProviderName, location);
                Log.i(
                    LOGTAG,
                    mockProviderName
                        + " is posting location, lat: "
                        + mockLatitude
                        + " lon: "
                        + mockLongitude
                        + " acc: "
                        + mockAccuracy);
              }
            },
            0,
            2,
            TimeUnit.SECONDS);
      }
    }
  }

  Map<GeckoSession, WebExtension.Port> mPorts = new HashMap<>();

  private class MessageDelegate implements WebExtension.MessageDelegate, WebExtension.PortDelegate {
    @Override
    public void onConnect(final @NonNull WebExtension.Port port) {
      // Sometimes we get a new onConnect call _before_ onDisconnect, so we might
      // have to detach the port here before we attach to a new one
      detach(mPorts.remove(port.sender.session));
      attach(port);
    }

    private void attach(WebExtension.Port port) {
      mPorts.put(port.sender.session, port);
      port.setDelegate(mMessageDelegate);
    }

    private void detach(WebExtension.Port port) {
      // If there are pending messages for this port we need to resolve them with an exception
      // otherwise the test will wait for them indefinitely.
      for (final String id : mPendingResponses.get(port)) {
        final EvalJSResult result = new EvalJSResult();
        result.exception = new PortDisconnectException();
        mPendingMessages.put(id, result);
      }
      mPendingResponses.remove(port);
    }

    @Override
    public void onPortMessage(
        @NonNull final Object message, @NonNull final WebExtension.Port port) {
      final JSONObject response = (JSONObject) message;

      final String id;
      try {
        id = response.getString("id");
        final EvalJSResult result = new EvalJSResult();

        final Object exception = response.get("exception");
        if (exception != JSONObject.NULL) {
          result.exception = exception;
        }

        final Object value = response.get("response");
        if (value != JSONObject.NULL) {
          result.value = value;
        }

        mPendingMessages.put(id, result);
      } catch (final JSONException ex) {
        throw new RuntimeException(ex);
      }
    }

    @Override
    public void onDisconnect(final @NonNull WebExtension.Port port) {
      detach(port);
      // Sometimes the onDisconnect call comes _after_ the new onConnect so we need to check
      // here whether this port is still in use.
      if (mPorts.get(port.sender.session) == port) {
        mPorts.remove(port.sender.session);
      }
    }

    public class PortDisconnectException extends RuntimeException {
      public PortDisconnectException() {
        super(
            "The port disconnected before a message could be received."
                + "Usually this happens when the page navigates away while "
                + "waiting for a message.");
      }
    }
  }

  private MessageDelegate mMessageDelegate = new MessageDelegate();

  private static class EvalJSResult {
    Object value;
    Object exception;
  }

  Map<String, EvalJSResult> mPendingMessages = new HashMap<>();
  MultiMap<WebExtension.Port, String> mPendingResponses = new MultiMap<>();

  public class ExtensionPromise {
    private UUID mUuid;
    private GeckoSession mSession;

    protected ExtensionPromise(final UUID uuid, final GeckoSession session, final String js) {
      mUuid = uuid;
      mSession = session;
      evaluateJS(session, "this['" + uuid + "'] = " + js + "; true");
    }

    public Object getValue() {
      return evaluateJS(mSession, "this['" + mUuid + "']");
    }
  }

  public ExtensionPromise evaluatePromiseJS(
      final @NonNull GeckoSession session, final @NonNull String js) {
    return new ExtensionPromise(UUID.randomUUID(), session, js);
  }

  public Object evaluateExtensionJS(final @NonNull String js) {
    return webExtensionApiCall(
        "Eval",
        args -> {
          args.put("code", js);
        });
  }

  public Object evaluateJS(final @NonNull GeckoSession session, final @NonNull String js) {
    // Let's make sure we have the port already
    UiThreadUtils.waitForCondition(() -> mPorts.containsKey(session), mTimeoutMillis);

    final JSONObject message = new JSONObject();
    final String id = UUID.randomUUID().toString();
    try {
      message.put("id", id);
      message.put("eval", js);
    } catch (final JSONException ex) {
      throw new RuntimeException(ex);
    }

    final WebExtension.Port port = mPorts.get(session);
    port.postMessage(message);

    return waitForMessage(port, id);
  }

  public int getSessionPid(final @NonNull GeckoSession session) {
    final Double dblPid = (Double) webExtensionApiCall(session, "GetPidForTab", null);
    return dblPid.intValue();
  }

  public void waitForContentTransformsReceived(final @NonNull GeckoSession session) {
    webExtensionApiCall(session, "WaitForContentTransformsReceived", null);
  }

  public String getProfilePath() {
    return (String) webExtensionApiCall("GetProfilePath", null);
  }

  public int[] getAllSessionPids() {
    final JSONArray jsonPids = (JSONArray) webExtensionApiCall("GetAllBrowserPids", null);
    final int[] pids = new int[jsonPids.length()];
    for (int i = 0; i < jsonPids.length(); i++) {
      try {
        pids[i] = jsonPids.getInt(i);
      } catch (final JSONException e) {
        throw new RuntimeException(e);
      }
    }
    return pids;
  }

  public void killContentProcess(final int pid) {
    webExtensionApiCall(
        "KillContentProcess",
        args -> {
          args.put("pid", pid);
        });
  }

  public boolean getActive(final @NonNull GeckoSession session) {
    return (Boolean) webExtensionApiCall(session, "GetActive", null);
  }

  public void triggerCookieBannerDetected(final @NonNull GeckoSession session) {
    webExtensionApiCall(session, "TriggerCookieBannerDetected", null);
  }

  public void triggerCookieBannerHandled(final @NonNull GeckoSession session) {
    webExtensionApiCall(session, "TriggerCookieBannerHandled", null);
  }

  public void triggerTranslationsOffer(final @NonNull GeckoSession session) {
    webExtensionApiCall(session, "TriggerTranslationsOffer", null);
  }

  public void triggerLanguageStateChange(
      final @NonNull GeckoSession session, final @NonNull JSONObject languageState) {
    webExtensionApiCall(
        session,
        "TriggerLanguageStateChange",
        args -> {
          args.put("languageState", languageState);
        });
  }

  public void teardownAlertsService(final @NonNull GeckoSession session) {
    webExtensionApiCall(session, "TeardownAlertsService", null);
  }

  private Object waitForMessage(final WebExtension.Port port, final String id) {
    mPendingResponses.add(port, id);
    UiThreadUtils.waitForCondition(() -> mPendingMessages.containsKey(id), mTimeoutMillis);
    mPendingResponses.remove(port);

    final EvalJSResult result = mPendingMessages.get(id);
    mPendingMessages.remove(id);

    if (result.exception != null) {
      throw new RejectedPromiseException(result.exception);
    }

    if (result.value == null) {
      return null;
    }

    Object value;
    try {
      value = new JSONTokener((String) result.value).nextValue();
    } catch (final JSONException ex) {
      value = result.value;
    }

    if (value instanceof Integer) {
      return ((Integer) value).doubleValue();
    }
    return value;
  }

  /**
   * Initialize and keep track of the specified session within the test rule. The session is
   * automatically cleaned up at the end of the test.
   *
   * @param session Session to keep track of.
   * @return Same session
   */
  public GeckoSession wrapSession(final GeckoSession session) {
    try {
      mSubSessions.add(session);
      prepareSession(session);
    } catch (final Throwable e) {
      throw unwrapRuntimeException(e);
    }
    return session;
  }

  private GeckoSession createSession(final GeckoSessionSettings settings, final boolean open) {
    final GeckoSession session = wrapSession(new GeckoSession(settings));
    if (open) {
      openSession(session);
    }
    return session;
  }

  /**
   * Create a new, opened session using the main session settings.
   *
   * @return New session.
   */
  public GeckoSession createOpenSession() {
    return createSession(mMainSession.getSettings(), /* open */ true);
  }

  /**
   * Create a new, opened session using the specified settings.
   *
   * @param settings Settings for the new session.
   * @return New session.
   */
  public GeckoSession createOpenSession(final GeckoSessionSettings settings) {
    return createSession(settings, /* open */ true);
  }

  /**
   * Create a new, closed session using the specified settings.
   *
   * @return New session.
   */
  public GeckoSession createClosedSession() {
    return createSession(mMainSession.getSettings(), /* open */ false);
  }

  /**
   * Create a new, closed session using the specified settings.
   *
   * @param settings Settings for the new session.
   * @return New session.
   */
  public GeckoSession createClosedSession(final GeckoSessionSettings settings) {
    return createSession(settings, /* open */ false);
  }

  /**
   * Return a value from the given array indexed by the current call counter. Only valid during a
   * {@link #forCallbacksDuringWait}, {@link #delegateDuringNextWait}, or {@link
   * #delegateUntilTestEnd} callback.
   *
   * <p>
   *
   * <p>Asserts that {@code foo} is equal to {@code "bar"} during the first call and {@code "baz"}
   * during the second call:
   *
   * <pre>{@code assertThat("Foo should match", foo, equalTo(forEachCall("bar",
   * "baz")));}</pre>
   *
   * @param values Input array
   * @return Value from input array indexed by the current call counter.
   */
  @SafeVarargs
  public final <T> T forEachCall(final T... values) {
    assertThat("Should be in a method call", mCurrentMethodCall, notNullValue());
    return values[Math.min(mCurrentMethodCall.getCurrentCount(), values.length) - 1];
  }

  /**
   * Evaluate a JavaScript expression and return the result, similar to {@link #evaluateJS}. In
   * addition, treat the evaluation as a wait event, which will affect other calls such as {@link
   * #forCallbacksDuringWait}. If the result is a Promise, wait on the Promise to settle and return
   * or throw based on the outcome.
   *
   * @param session Session containing the target page.
   * @param js JavaScript expression.
   * @return Result of the expression or value of the resolved Promise.
   * @see #evaluateJS
   */
  public @Nullable Object waitForJS(final @NonNull GeckoSession session, final @NonNull String js) {
    try {
      beforeWait();
      return evaluateJS(session, js);
    } finally {
      afterWait(mCallRecords.size());
    }
  }

  /**
   * Get a list of Gecko prefs. Undefined prefs will return as null.
   *
   * @param prefs List of pref names.
   * @return Pref values as a list of values.
   */
  public JSONArray getPrefs(final @NonNull String... prefs) {
    return (JSONArray)
        webExtensionApiCall(
            "GetPrefs",
            args -> {
              args.put("prefs", new JSONArray(Arrays.asList(prefs)));
            });
  }

  /**
   * Called to clear a user set value from a specific preference. This will, in effect, reset the
   * value to the default value. If no default value exists the preference will cease to exist.
   *
   * <p>See nsIPrefBranch.idl for more details.
   *
   * @param pref Pref name.
   */
  public void clearUserPref(final @NonNull String pref) {
    webExtensionApiCall(
        "ClearUserPref",
        args -> {
          args.put("pref", pref);
        });
  }

  /**
   * Gets the color of a link for a given selector.
   *
   * @param selector Selector that matches the link
   * @return String representing the color, e.g. rgb(0, 0, 255)
   */
  public String getLinkColor(final GeckoSession session, final String selector) {
    return (String)
        webExtensionApiCall(
            session,
            "GetLinkColor",
            args -> {
              args.put("selector", selector);
            });
  }

  public List<String> getRequestedLocales() {
    try {
      final JSONArray locales = (JSONArray) webExtensionApiCall("GetRequestedLocales", null);
      final List<String> result = new ArrayList<>();

      for (int i = 0; i < locales.length(); i++) {
        result.add(locales.getString(i));
      }

      return result;
    } catch (final JSONException ex) {
      throw new RuntimeException(ex);
    }
  }

  /**
   * Adds value to the given histogram.
   *
   * @param id the histogram id to increment.
   * @param value to add to the histogram.
   */
  public void addHistogram(final String id, final long value) {
    webExtensionApiCall(
        "AddHistogram",
        args -> {
          args.put("id", id);
          args.put("value", value);
        });
  }

  /** Revokes all SSL overrides */
  public void removeAllCertOverrides() {
    webExtensionApiCall("RemoveAllCertOverrides", null);
  }

  /**
   * Seeds the tracking protection database with the given content blocking log.
   *
   * @param logJson JSON-serialized ContentBlockingLog (origin keys mapped to arrays of [state,
   *     blocked, count] tuples).
   */
  public void saveTrackingDBEvents(final @NonNull String logJson) {
    webExtensionApiCall(
        "SaveTrackingDBEvents",
        args -> {
          args.put("log", logJson);
        });
  }

  /** Removes all entries from the tracking protection database. */
  public void clearTrackingDB() {
    webExtensionApiCall("ClearTrackingDB", null);
  }

  private interface SetArgs {
    void setArgs(JSONObject object) throws JSONException;
  }

  /** Invokes nsIDOMWindowUtils.setResolutionAndScaleTo. */
  public void setResolutionAndScaleTo(final GeckoSession session, final float resolution) {
    webExtensionApiCall(
        session,
        "SetResolutionAndScaleTo",
        args -> {
          args.put("resolution", resolution);
        });
  }

  /** Invokes nsIDOMWindowUtils.flushApzRepaints. */
  public void flushApzRepaints(final GeckoSession session) {
    webExtensionApiCall(session, "FlushApzRepaints", null);
  }

  /** Invokes nsIDOMWindowUtils.zoomToFocusedInput. */
  public void zoomToFocusedInput(final GeckoSession session) {
    webExtensionApiCall(session, "ZoomToFocusedInput", null);
  }

  /** Invokes a simplified version of promiseAllPaintsDone in paint_listener.js. */
  public void promiseAllPaintsDone(final GeckoSession session) {
    webExtensionApiCall(session, "PromiseAllPaintsDone", null);
  }

  /** Invokes nsIDOMWindowUtils.setHandlingUserInput. */
  public void setHandlingUserInput(final GeckoSession session, final boolean handlingUserInput) {
    webExtensionApiCall(
        session,
        "SetHandlingUserInput",
        args -> {
          args.put("handlingUserInput", handlingUserInput);
        });
  }

  /** Returns true if Gecko is using a GPU process. */
  public boolean usingGpuProcess() {
    return (Boolean) webExtensionApiCall("UsingGpuProcess", null);
  }

  /** Kills the GPU process cleanly with generating a crash report. */
  public void killGpuProcess() {
    webExtensionApiCall("KillGpuProcess", null);
  }

  /** Causes the GPU process to crash. */
  public void crashGpuProcess() {
    webExtensionApiCall("CrashGpuProcess", null);
  }

  /** Clears sites from the HSTS list. */
  public void clearHSTSState() {
    webExtensionApiCall("ClearHSTSState", null);
  }

  /** Checks if fission is running. */
  public boolean isFissionRunning() {
    return (Boolean) webExtensionApiCall("IsFissionRunning", null);
  }

  /** Simulate user gesture activation */
  public void notifyUserGestureActivation(final GeckoSession session) {
    webExtensionApiCall(session, "NotifyUserGestureActivation", null);
  }

  /** Adds a virtual WebAuthn authenticator. Returns the authenticator ID. */
  public String addVirtualAuthenticator() {
    return (String) webExtensionApiCall("AddVirtualAuthenticator", null);
  }

  /** Removes a virtual WebAuthn authenticator. */
  public void removeVirtualAuthenticator(final String authenticatorId) {
    webExtensionApiCall(
        "RemoveVirtualAuthenticator",
        args -> {
          args.put("authenticatorId", authenticatorId);
        });
  }

  /**
   * Gets all the permission names defined in the WebExtensions API JSONSchema given an array of
   * type names.
   *
   * @param typeNames JSONSchema type names to retrieve permission names for.
   * @return JSONArray array of the permission names found.
   */
  public JSONArray getWebExtensionsSchemaPermissionNames(
      final GeckoSession session, final String[] typeNames) {
    return (JSONArray)
        webExtensionApiCall(
            session,
            "GetWebExtensionSchemaPermissionNames",
            args -> {
              JSONArray jsonTypeNames = new JSONArray();
              for (String str : typeNames) {
                jsonTypeNames.put(str);
              }
              args.put("typeNames", jsonTypeNames);
            });
  }

  private Object webExtensionApiCall(
      final @NonNull String apiName, final @NonNull SetArgs argsSetter) {
    return webExtensionApiCall(null, apiName, argsSetter);
  }

  private Object webExtensionApiCall(
      final GeckoSession session,
      final @NonNull String apiName,
      final @NonNull SetArgs argsSetter) {
    // Ensure background script is connected
    UiThreadUtils.waitForCondition(() -> RuntimeCreator.backgroundPort() != null, mTimeoutMillis);

    if (session != null) {
      // Ensure content script is connected
      UiThreadUtils.waitForCondition(() -> mPorts.get(session) != null, mTimeoutMillis);
    }

    final String id = UUID.randomUUID().toString();

    final JSONObject message = new JSONObject();

    try {
      final JSONObject args = new JSONObject();
      if (argsSetter != null) {
        argsSetter.setArgs(args);
      }

      message.put("id", id);
      message.put("type", apiName);
      message.put("args", args);
    } catch (final JSONException ex) {
      throw new RuntimeException(ex);
    }

    final WebExtension.Port port;
    if (session == null) {
      port = RuntimeCreator.backgroundPort();
    } else {
      // We post the message using session's port instead of the background port. By routing
      // the message through the extension's content script, we are able to obtain and attach
      // the session's WebExtension tab as a `tab` argument to the API.
      port = mPorts.get(session);
    }

    port.postMessage(message);
    return waitForMessage(port, id);
  }

  /**
   * Set a list of Gecko prefs for the rest of the test. Prefs set in {@link
   * #setPrefsDuringNextWait} can temporarily take precedence over prefs set in {@code
   * setPrefsUntilTestEnd}.
   *
   * @param prefs Map of pref names to values.
   * @see #setPrefsDuringNextWait
   */
  public void setPrefsUntilTestEnd(final @NonNull Map<String, ?> prefs) {
    mTestScopeDelegates.setPrefs(prefs);
  }

  /**
   * Set a list of Gecko prefs during the next wait. Prefs set in {@code setPrefsDuringNextWait} can
   * temporarily take precedence over prefs set in {@link #setPrefsUntilTestEnd}.
   *
   * @param prefs Map of pref names to values.
   * @see #setPrefsUntilTestEnd
   */
  public void setPrefsDuringNextWait(final @NonNull Map<String, ?> prefs) {
    mWaitScopeDelegates.setPrefs(prefs);
  }

  /**
   * Register an external, non-GeckoSession delegate, and start recording the delegate calls until
   * the end of the test. The delegate can then be used with methods such as {@link
   * #waitUntilCalled(Class, String...)} and {@link #forCallbacksDuringWait(Object)}. At the end of
   * the test, the delegate is automatically unregistered. Delegates added by {@link
   * #addExternalDelegateDuringNextWait} can temporarily take precedence over delegates added by
   * {@code delegateUntilTestEnd}.
   *
   * @param delegate Delegate instance to register.
   * @param register DelegateRegistrar instance that represents a function to register the delegate.
   * @param unregister DelegateRegistrar instance that represents a function to unregister the
   *     delegate.
   * @param impl Default delegate implementation. Its methods may be annotated with {@link
   *     AssertCalled} annotations to assert expected behavior.
   * @see #addExternalDelegateDuringNextWait
   */
  public <T> void addExternalDelegateUntilTestEnd(
      @NonNull final Class<T> delegate,
      @NonNull final DelegateRegistrar<T> register,
      @NonNull final DelegateRegistrar<T> unregister,
      @NonNull final T impl) {
    final ExternalDelegate<T> externalDelegate =
        mTestScopeDelegates.addExternalDelegate(delegate, register, unregister, impl);

    // Register if there is not a wait delegate to take precedence over this call.
    if (!mWaitScopeDelegates.getExternalDelegates().contains(externalDelegate)) {
      externalDelegate.register();
    }
  }

  /**
   * @see #addExternalDelegateUntilTestEnd(Class, DelegateRegistrar, DelegateRegistrar, Object)
   */
  public <T> void addExternalDelegateUntilTestEnd(
      @NonNull final KClass<T> delegate,
      @NonNull final DelegateRegistrar<T> register,
      @NonNull final DelegateRegistrar<T> unregister,
      @NonNull final T impl) {
    addExternalDelegateUntilTestEnd(
        JvmClassMappingKt.getJavaClass(delegate), register, unregister, impl);
  }

  /**
   * Register an external, non-GeckoSession delegate, and start recording the delegate calls during
   * the next wait. The delegate can then be used with methods such as {@link
   * #waitUntilCalled(Class, String...)} and {@link #forCallbacksDuringWait(Object)}. After the next
   * wait, the delegate is automatically unregistered. Delegates added by {@code
   * addExternalDelegateDuringNextWait} can temporarily take precedence over delegates added by
   * {@link #delegateUntilTestEnd}.
   *
   * @param delegate Delegate instance to register.
   * @param register DelegateRegistrar instance that represents a function to register the delegate.
   * @param unregister DelegateRegistrar instance that represents a function to unregister the
   *     delegate.
   * @param impl Default delegate implementation. Its methods may be annotated with {@link
   *     AssertCalled} annotations to assert expected behavior.
   * @see #addExternalDelegateDuringNextWait
   */
  public <T> void addExternalDelegateDuringNextWait(
      @NonNull final Class<T> delegate,
      @NonNull final DelegateRegistrar<T> register,
      @NonNull final DelegateRegistrar<T> unregister,
      @NonNull final T impl) {
    final ExternalDelegate<T> externalDelegate =
        mWaitScopeDelegates.addExternalDelegate(delegate, register, unregister, impl);

    // Always register because this call always takes precedence, but make sure to unregister
    // any test-delegates first.
    final int index = mTestScopeDelegates.getExternalDelegates().indexOf(externalDelegate);
    if (index >= 0) {
      mTestScopeDelegates.getExternalDelegates().get(index).unregister();
    }
    externalDelegate.register();
  }

  /**
   * @see #addExternalDelegateDuringNextWait(Class, DelegateRegistrar, DelegateRegistrar, Object)
   */
  public <T> void addExternalDelegateDuringNextWait(
      @NonNull final KClass<T> delegate,
      @NonNull final DelegateRegistrar<T> register,
      @NonNull final DelegateRegistrar<T> unregister,
      @NonNull final T impl) {
    addExternalDelegateDuringNextWait(
        JvmClassMappingKt.getJavaClass(delegate), register, unregister, impl);
  }

  /**
   * This waits for the given result and returns it's value. If the result failed with an exception,
   * it is rethrown.
   *
   * @param result A {@link GeckoResult} instance.
   * @param <T> The type of the value held by the {@link GeckoResult}
   * @return The value of the completed {@link GeckoResult}.
   */
  public <T> T waitForResult(@NonNull final GeckoResult<T> result) throws Throwable {
    return waitForResult(result, mTimeoutMillis);
  }

  /**
   * This is similar to waitForResult with specific timeout.
   *
   * @param result A {@link GeckoResult} instance.
   * @param timeout timeout in milliseconds
   * @param <T> The type of the value held by the {@link GeckoResult}
   * @return The value of the completed {@link GeckoResult}.
   */
  private <T> T waitForResult(@NonNull final GeckoResult<T> result, final long timeout)
      throws Throwable {
    beforeWait();
    try {
      return UiThreadUtils.waitForResult(result, timeout);
    } catch (final Throwable e) {
      throw unwrapRuntimeException(e);
    } finally {
      afterWait(mCallRecords.size());
    }
  }
}
