/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.os.Handler;
import android.util.Log;
import androidx.annotation.AnyThread;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.Deque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import org.mozilla.gecko.annotation.ReflectionTarget;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.gecko.util.BundleEventListener;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ThreadUtils;
import org.mozilla.geckoview.BuildConfig;
import org.mozilla.geckoview.GeckoResult;

public final class EventDispatcher extends JNIObject {
  private static final String LOGTAG = "GeckoEventDispatcher";

  private static final EventDispatcher INSTANCE = new EventDispatcher();

  /**
   * The capacity of a HashMap is rounded up to the next power-of-2. Every time the size of the map
   * goes beyond 75% of the capacity, the map is rehashed. Therefore, to empirically determine the
   * initial capacity that avoids rehashing, we need to determine the initial size, divide it by
   * 75%, and round up to the next power-of-2.
   */
  private static final int DEFAULT_UI_EVENTS_COUNT = 128; // Empirically measured

  private static class Message {
    final String type;
    final GeckoBundle bundle;
    final EventCallback callback;

    Message(final String type, final GeckoBundle bundle, final EventCallback callback) {
      this.type = type;
      this.bundle = bundle;
      this.callback = callback;
    }
  }

  // GeckoBundle-based events.
  private final MultiMap<String, BundleEventListener> mListeners =
      new MultiMap<>(DEFAULT_UI_EVENTS_COUNT);
  private Deque<Message> mPendingMessages = new ArrayDeque<>();

  private boolean mAttachedToGecko;
  private final NativeQueue mNativeQueue;
  private final String mName;

  private static Map<String, EventDispatcher> sDispatchers = new HashMap<>();

  @ReflectionTarget
  @WrapForJNI(calledFrom = "gecko")
  public static EventDispatcher getInstance() {
    return INSTANCE;
  }

  /**
   * Gets a named EventDispatcher.
   *
   * <p>Named EventDispatchers can be used to communicate to Gecko's corresponding named
   * EventDispatcher.
   *
   * <p>Messages for named EventDispatcher are queued by default when no listener is present. Queued
   * messages will be released automatically when a listener is attached.
   *
   * <p>A named EventDispatcher needs to be disposed manually by calling {@link #shutdown} when it
   * is not needed anymore.
   *
   * @param name Name for this EventDispatcher.
   * @return the existing named EventDispatcher for a given name or a newly created one if it
   *     doesn't exist.
   */
  @ReflectionTarget
  @WrapForJNI(calledFrom = "gecko")
  public static EventDispatcher byName(final String name) {
    synchronized (sDispatchers) {
      EventDispatcher dispatcher = sDispatchers.get(name);

      if (dispatcher == null) {
        dispatcher = new EventDispatcher(name);
        sDispatchers.put(name, dispatcher);
      }

      return dispatcher;
    }
  }

  /* package */ EventDispatcher() {
    mNativeQueue = GeckoThread.getNativeQueue();
    mName = null;
  }

  /* package */ EventDispatcher(final String name) {
    mNativeQueue = GeckoThread.getNativeQueue();
    mName = name;
  }

  public EventDispatcher(final NativeQueue queue) {
    mNativeQueue = queue;
    mName = null;
  }

  private boolean isReadyForDispatchingToGecko() {
    return mNativeQueue.isReady();
  }

  @WrapForJNI
  @Override // JNIObject
  protected native void disposeNative();

  @WrapForJNI(stubName = "Shutdown")
  protected native void shutdownNative();

  @WrapForJNI private static final int DETACHED = 0;
  @WrapForJNI private static final int ATTACHED = 1;
  @WrapForJNI private static final int REATTACHING = 2;

  @WrapForJNI(calledFrom = "gecko")
  private synchronized void setAttachedToGecko(final int state) {
    if (mAttachedToGecko && state == DETACHED) {
      dispose(false);
    }
    mAttachedToGecko = (state == ATTACHED);
  }

  /**
   * Shuts down this EventDispatcher and release resources.
   *
   * <p>Only named EventDispatcher can be shut down manually. A shut down EventDispatcher will not
   * receive any further messages.
   */
  public void shutdown() {
    if (mName == null) {
      throw new RuntimeException("Only named EventDispatcher's can be shut down.");
    }

    mAttachedToGecko = false;
    shutdownNative();
    dispose(false);

    synchronized (sDispatchers) {
      sDispatchers.put(mName, null);
    }
  }

  private void dispose(final boolean force) {
    final Handler geckoHandler = ThreadUtils.sGeckoHandler;
    if (geckoHandler == null) {
      return;
    }

    geckoHandler.post(
        new Runnable() {
          @Override
          public void run() {
            if (force || !mAttachedToGecko) {
              disposeNative();
            }
          }
        });
  }

  public void registerUiThreadListener(final BundleEventListener listener, final String... events) {
    try {
      synchronized (mListeners) {
        for (final String event : events) {
          if (!BuildConfig.RELEASE_OR_BETA && mListeners.containsEntry(event, listener)) {
            throw new IllegalStateException("Already registered " + event);
          }
          mListeners.add(event, listener);
        }
        flush(events);
      }
    } catch (final Exception e) {
      throw new IllegalArgumentException("Invalid new list type", e);
    }
  }

  public void unregisterUiThreadListener(
      final BundleEventListener listener, final String... events) {
    synchronized (mListeners) {
      for (final String event : events) {
        if (!mListeners.remove(event, listener) && !BuildConfig.RELEASE_OR_BETA) {
          throw new IllegalArgumentException(event + " was not registered");
        }
      }
    }
  }

  @WrapForJNI
  private native boolean hasGeckoListener(final String event);

  @WrapForJNI(dispatchTo = "gecko")
  private native void dispatchToGecko(
      final String event, final GeckoBundle data, final EventCallback callback);

  /**
   * Dispatch event to any registered Bundle listeners (non-Gecko thread listeners).
   *
   * @param type Event type
   * @param message Bundle message
   */
  public void dispatch(final String type, final GeckoBundle message) {
    dispatch(type, message, /* callback */ null);
  }

  private static class CallbackResult<T> extends GeckoResult<T> implements EventCallback {
    private final String mType;

    CallbackResult(final String type) {
      mType = type;
    }

    @Override
    @SuppressWarnings("unchecked")
    public void sendSuccess(final Object response) {
      complete((T) response);
    }

    @Override
    public void sendError(final Object response) {
      completeExceptionally(new QueryException(mType, response));
    }
  }

  public static class QueryException extends Exception {
    private static final long serialVersionUID = 1L;

    public final transient Object data;

    public QueryException(final String type, final Object data) {
      super("Failed on message type: " + type);
      this.data = data;
    }
  }

  /**
   * Query event to any registered Bundle listeners (non-Gecko thread listeners).
   *
   * <p>The returned GeckoResult completes when the event handler returns.
   *
   * @param type Event type
   */
  public GeckoResult<Void> queryVoid(final String type) {
    return queryVoid(type, null);
  }

  /**
   * Query event to any registered Bundle listeners (non-Gecko thread listeners).
   *
   * <p>The returned GeckoResult completes when the event handler returns.
   *
   * @param type Event type
   * @param message GeckoBundle message
   */
  public GeckoResult<Void> queryVoid(final String type, final GeckoBundle message) {
    return query(type, message);
  }

  /**
   * Query event to any registered Bundle listeners (non-Gecko thread listeners).
   *
   * <p>The returned GeckoResult completes with the given boolean value returned by the handler.
   *
   * @param type Event type
   */
  public GeckoResult<Boolean> queryBoolean(final String type) {
    return queryBoolean(type, null);
  }

  /**
   * Query event to any registered Bundle listeners (non-Gecko thread listeners).
   *
   * <p>The returned GeckoResult completes with the given boolean value returned by the handler.
   *
   * @param type Event type
   * @param message GeckoBundle message
   */
  public GeckoResult<Boolean> queryBoolean(final String type, final GeckoBundle message) {
    return query(type, message);
  }

  /**
   * Query event to any registered Bundle listeners (non-Gecko thread listeners).
   *
   * <p>The returned GeckoResult completes with the given String value returned by the handler.
   *
   * @param type Event type
   */
  public GeckoResult<String> queryString(final String type) {
    return queryString(type, null);
  }

  /**
   * Query event to any registered Bundle listeners (non-Gecko thread listeners).
   *
   * <p>The returned GeckoResult completes with the given String value returned by the handler.
   *
   * @param type Event type
   * @param message GeckoBundle message
   */
  public GeckoResult<String> queryString(final String type, final GeckoBundle message) {
    return query(type, message);
  }

  /**
   * Query event to any registered Bundle listeners (non-Gecko thread listeners).
   *
   * <p>The returned GeckoResult completes with the given {@link GeckoBundle} value returned by the
   * handler.
   *
   * @param type Event type
   */
  public GeckoResult<GeckoBundle> queryBundle(final String type) {
    return queryBundle(type, null);
  }

  /**
   * Query event to any registered Bundle listeners (non-Gecko thread listeners).
   *
   * <p>The returned GeckoResult completes with the given {@link GeckoBundle} value returned by the
   * handler.
   *
   * @param type Event type
   * @param message GeckoBundle message
   */
  public GeckoResult<GeckoBundle> queryBundle(final String type, final GeckoBundle message) {
    return query(type, message);
  }

  private <T> GeckoResult<T> query(final String type, final GeckoBundle message) {
    final CallbackResult<T> result = new CallbackResult<>(type);
    dispatch(type, message, result);
    return result;
  }

  /**
   * Flushes pending messages of given types.
   *
   * <p>All unhandled messages are put into a pending state by default for named EventDispatcher
   * obtained from {@link #byName}.
   *
   * @param types Types of message to flush.
   */
  private void flush(final String[] types) {
    final Set<String> typeSet = new HashSet<>(Arrays.asList(types));

    final Deque<Message> pendingMessages;
    synchronized (mPendingMessages) {
      pendingMessages = mPendingMessages;
      mPendingMessages = new ArrayDeque<>(pendingMessages.size());
    }

    Message message;
    while (!pendingMessages.isEmpty()) {
      message = pendingMessages.removeFirst();
      if (typeSet.contains(message.type)) {
        dispatchToThreads(message.type, message.bundle, message.callback);
      } else {
        synchronized (mPendingMessages) {
          mPendingMessages.addLast(message);
        }
      }
    }
  }

  /**
   * Dispatch event to any registered Bundle listeners (non-Gecko thread listeners).
   *
   * @param type Event type
   * @param message Bundle message
   * @param callback Optional object for callbacks from events.
   */
  @AnyThread
  private void dispatch(
      final String type, final GeckoBundle message, final EventCallback callback) {
    final boolean isGeckoReady;
    synchronized (this) {
      isGeckoReady = isReadyForDispatchingToGecko();
      if (isGeckoReady && mAttachedToGecko && hasGeckoListener(type)) {
        dispatchToGecko(type, message, JavaCallbackDelegate.wrap(callback));
        return;
      }
    }

    dispatchToThreads(type, message, callback, isGeckoReady);
  }

  @WrapForJNI(calledFrom = "gecko")
  private boolean dispatchToThreads(
      final String type, final GeckoBundle message, final EventCallback callback) {
    return dispatchToThreads(type, message, callback, /* isGeckoReady */ true);
  }

  private boolean dispatchToThreads(
      final String type,
      final GeckoBundle message,
      final EventCallback callback,
      final boolean isGeckoReady) {
    // We need to hold the lock throughout dispatching, to ensure the listeners list
    // is consistent, while we iterate over it. We don't have to worry about listeners
    // running for a long time while we have the lock, because the listeners will run
    // on a separate thread.
    synchronized (mListeners) {
      if (mListeners.containsKey(type)) {
        // Use a delegate to make sure callbacks happen on a specific thread.
        final EventCallback wrappedCallback = JavaCallbackDelegate.wrap(callback);

        // Event listeners will call | callback.sendError | if applicable.
        for (final BundleEventListener listener : mListeners.get(type)) {
          ThreadUtils.getUiHandler()
              .post(
                  new Runnable() {
                    @Override
                    public void run() {
                      final Double startTime = GeckoJavaSampler.tryToGetProfilerTime();
                      listener.handleMessage(type, message, wrappedCallback);
                      GeckoJavaSampler.addMarker(
                          "EventDispatcher handleMessage", startTime, null, type);
                    }
                  });
        }
        return true;
      }
    }

    if (!isGeckoReady) {
      // Usually, we discard an event if there is no listeners for it by
      // the time of the dispatch. However, if Gecko(View) is not ready and
      // there is no listener for this event that's possibly headed to
      // Gecko, we make a special exception to queue this event until
      // Gecko(View) is ready. This way, Gecko can first register its
      // listeners, and accept the event when it is ready.
      mNativeQueue.queueUntilReady(
          this,
          "dispatchToGecko",
          String.class,
          type,
          GeckoBundle.class,
          message,
          EventCallback.class,
          JavaCallbackDelegate.wrap(callback));
      return true;
    }

    // Named EventDispatchers use pending messages
    if (mName != null) {
      synchronized (mPendingMessages) {
        mPendingMessages.addLast(new Message(type, message, callback));
      }
      return true;
    }

    final String error = "No listener for " + type;
    if (callback != null) {
      callback.sendError(error);
    }

    Log.w(LOGTAG, error);
    return false;
  }

  @WrapForJNI
  public boolean hasListener(final String event) {
    synchronized (mListeners) {
      return mListeners.containsKey(event);
    }
  }

  @Override
  protected void finalize() throws Throwable {
    dispose(true);
  }

  private static class NativeCallbackDelegate extends JNIObject implements EventCallback {
    @WrapForJNI(calledFrom = "gecko")
    private NativeCallbackDelegate() {}

    @Override // JNIObject
    protected void disposeNative() {
      // We dispose in finalize().
      throw new UnsupportedOperationException();
    }

    @WrapForJNI(dispatchTo = "proxy")
    @Override // EventCallback
    public native void sendSuccess(Object response);

    @WrapForJNI(dispatchTo = "proxy")
    @Override // EventCallback
    public native void sendError(Object response);

    @WrapForJNI(dispatchTo = "gecko")
    @Override // Object
    protected native void finalize();
  }

  private static class JavaCallbackDelegate implements EventCallback {
    private final Thread mOriginalThread = Thread.currentThread();
    private final EventCallback mCallback;

    public static EventCallback wrap(final EventCallback callback) {
      if (callback == null) {
        return null;
      }
      if (callback instanceof NativeCallbackDelegate) {
        // NativeCallbackDelegate always posts to Gecko thread if needed.
        return callback;
      }
      return new JavaCallbackDelegate(callback);
    }

    JavaCallbackDelegate(final EventCallback callback) {
      mCallback = callback;
    }

    private void makeCallback(final boolean callSuccess, final Object rawResponse) {
      final Object response;
      if (rawResponse instanceof Number) {
        // There is ambiguity because a number can be converted to either int or
        // double, so e.g. the user can be expecting a double when we give it an
        // int. To avoid these pitfalls, we disallow all numbers. The workaround
        // is to wrap the number in a JS object / GeckoBundle, which supports
        // type coersion for numbers.
        throw new UnsupportedOperationException("Cannot use number as Java callback result");
      } else if (rawResponse != null && rawResponse.getClass().isArray()) {
        // Same with arrays.
        throw new UnsupportedOperationException("Cannot use arrays as Java callback result");
      } else if (rawResponse instanceof Character) {
        response = rawResponse.toString();
      } else {
        response = rawResponse;
      }

      // Call back synchronously if we happen to be on the same thread as the thread
      // making the original request.
      if (ThreadUtils.isOnThread(mOriginalThread)) {
        if (callSuccess) {
          mCallback.sendSuccess(response);
        } else {
          mCallback.sendError(response);
        }
        return;
      }

      // Make callback on the thread of the original request, if the original thread
      // is the UI or Gecko thread. Otherwise default to the background thread.
      final Handler handler =
          mOriginalThread == ThreadUtils.getUiThread()
              ? ThreadUtils.getUiHandler()
              : mOriginalThread == ThreadUtils.sGeckoThread
                  ? ThreadUtils.sGeckoHandler
                  : ThreadUtils.getBackgroundHandler();
      final EventCallback callback = mCallback;

      handler.post(
          new Runnable() {
            @Override
            public void run() {
              if (callSuccess) {
                callback.sendSuccess(response);
              } else {
                callback.sendError(response);
              }
            }
          });
    }

    @Override // EventCallback
    public void sendSuccess(final Object response) {
      makeCallback(/* success */ true, response);
    }

    @Override // EventCallback
    public void sendError(final Object response) {
      makeCallback(/* success */ false, response);
    }
  }
}
