/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test;

import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.io.File;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.geckoview.GeckoResult;
import org.mozilla.geckoview.GeckoRuntime;
import org.mozilla.geckoview.GeckoRuntimeSettings;
import org.mozilla.geckoview.GeckoSession;
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule;

public class TestRuntimeService extends Service
    implements GeckoSession.ProgressDelegate, GeckoRuntime.Delegate {
  // Used by the client to register themselves
  public static final int MESSAGE_REGISTER = 1;
  // Sent when the first page load completes
  public static final int MESSAGE_INIT_COMPLETE = 2;
  // Sent when GeckoRuntime exits
  public static final int MESSAGE_QUIT = 3;
  // Reload current session
  public static final int MESSAGE_RELOAD = 4;
  // Load URI in current session
  public static final int MESSAGE_LOAD_URI = 5;
  // Receive a reply for a message
  public static final int MESSAGE_REPLY = 6;
  // Execute action on the remote service
  public static final int MESSAGE_PAGE_STOP = 7;

  // Used by clients to know the first safe ID that can be used
  // for additional message types
  public static final int FIRST_SAFE_MESSAGE = MESSAGE_PAGE_STOP + 1;

  // Generic service instances
  public static final class instance0 extends TestRuntimeService {}

  public static final class instance1 extends TestRuntimeService {}

  protected GeckoRuntime mRuntime;
  protected GeckoSession mSession;
  protected GeckoBundle mTestData;

  private Messenger mClient;

  private class TestHandler extends Handler {
    @Override
    public void handleMessage(@NonNull final Message msg) {
      final Bundle msgData = msg.getData();
      final GeckoBundle data =
          msgData != null ? GeckoBundle.fromBundle(msgData.getBundle("data")) : null;
      final String id = msgData != null ? msgData.getString("id") : null;

      switch (msg.what) {
        case MESSAGE_REGISTER:
          mClient = msg.replyTo;
          return;
        case MESSAGE_QUIT:
          // Unceremoniously exit
          System.exit(0);
          return;
        case MESSAGE_RELOAD:
          mSession.reload();
          break;
        case MESSAGE_LOAD_URI:
          mSession.loadUri(data.getString("uri"));
          break;
        default:
          {
            final GeckoResult<GeckoBundle> result =
                TestRuntimeService.this.handleMessage(msg.what, data);
            if (result != null) {
              result.accept(
                  bundle -> {
                    final GeckoBundle reply = new GeckoBundle();
                    reply.putString("id", id);
                    reply.putBundle("data", bundle);
                    TestRuntimeService.this.sendMessage(MESSAGE_REPLY, reply);
                  });
            }
            return;
          }
      }
    }
  }

  final Messenger mMessenger = new Messenger(new TestHandler());

  @Override
  public void onShutdown() {
    sendMessage(MESSAGE_QUIT);
  }

  protected void sendMessage(final int message) {
    sendMessage(message, null);
  }

  protected void sendMessage(final int message, final GeckoBundle bundle) {
    if (mClient == null) {
      throw new IllegalStateException("Service is not connected yet!");
    }

    Message msg = Message.obtain(null, message);
    msg.replyTo = mMessenger;
    if (bundle != null) {
      msg.setData(bundle.toBundle());
    }

    try {
      mClient.send(msg);
    } catch (RemoteException ex) {
      throw new RuntimeException(ex);
    }
  }

  private boolean mFirstPageStop = true;

  @Override
  public void onPageStop(@NonNull final GeckoSession session, final boolean success) {
    // Notify the subclass that the session is ready to use
    if (success && mFirstPageStop) {
      onSessionReady(session);
      mFirstPageStop = false;
      sendMessage(MESSAGE_INIT_COMPLETE);
    } else {
      sendMessage(MESSAGE_PAGE_STOP);
    }
  }

  protected void onSessionReady(final GeckoSession session) {}

  @Override
  public void onDestroy() {
    // Sometimes the service doesn't die on it's own so we need to kill it here.
    System.exit(0);
  }

  @Nullable
  @Override
  public IBinder onBind(final Intent intent) {
    // Request to be killed as soon as the client unbinds.
    stopSelf();

    if (mRuntime != null) {
      // We only expect one client
      throw new RuntimeException("Multiple clients !?");
    }

    mRuntime = createRuntime(getApplicationContext(), intent);
    mRuntime.setDelegate(this);

    if (intent.hasExtra("test-data")) {
      mTestData = GeckoBundle.fromBundle(intent.getBundleExtra("test-data"));
    }

    mSession = createSession(intent);
    mSession.setProgressDelegate(this);
    mSession.open(mRuntime);

    return mMessenger.getBinder();
  }

  /** Override this to handle custom messages. */
  protected GeckoResult<GeckoBundle> handleMessage(final int messageId, final GeckoBundle data) {
    return null;
  }

  /** Override this to change the default runtime */
  protected GeckoRuntime createRuntime(
      final @NonNull Context context, final @NonNull Intent intent) {
    return GeckoRuntime.create(
        context,
        new GeckoRuntimeSettings.Builder()
            .extras(intent.getExtras())
            .lowMemoryDetection(false) // Avoid unpredictability in tests
            .build());
  }

  /** Override this to change the default session */
  protected GeckoSession createSession(final Intent intent) {
    return new GeckoSession();
  }

  /**
   * Starts GeckoRuntime in the process given in input, and waits for the MESSAGE_INIT_COMPLETE
   * event that's fired when the first GeckoSession receives the onPageStop event.
   *
   * <p>We wait for a page load to make sure that everything started up correctly (as opposed to
   * quitting during the startup procedure).
   */
  public static class RuntimeInstance<T> {
    public boolean isConnected = false;
    public GeckoResult<Void> disconnected = new GeckoResult<>();
    public GeckoResult<Void> started = new GeckoResult<>();
    public GeckoResult<Void> quitted = new GeckoResult<>();
    public final Context context;
    public final Class<T> service;

    private final File mProfileFolder;
    private final GeckoBundle mTestData;
    private final ClientHandler mClientHandler = new ClientHandler();
    private Messenger mMessenger;
    private Messenger mServiceMessenger;
    private GeckoResult<Void> mPageStop = null;

    private Map<String, GeckoResult<GeckoBundle>> mPendingMessages = new HashMap<>();

    protected RuntimeInstance(
        final Context context, final Class<T> service, final File profileFolder) {
      this(context, service, profileFolder, null);
    }

    protected RuntimeInstance(
        final Context context,
        final Class<T> service,
        final File profileFolder,
        final GeckoBundle testData) {
      this.context = context;
      this.service = service;
      mProfileFolder = profileFolder;
      mTestData = testData;
    }

    public static <T> RuntimeInstance<T> start(
        final Context context, final Class<T> service, final File profileFolder) {
      RuntimeInstance<T> instance = new RuntimeInstance<>(context, service, profileFolder);
      instance.sendIntent();
      return instance;
    }

    class ClientHandler extends Handler implements ServiceConnection {
      @Override
      public void handleMessage(@NonNull Message msg) {
        switch (msg.what) {
          case MESSAGE_INIT_COMPLETE:
            started.complete(null);
            break;
          case MESSAGE_QUIT:
            quitted.complete(null);
            // No reason to keep the service around anymore
            context.unbindService(mClientHandler);
            break;
          case MESSAGE_REPLY:
            final String messageId = msg.getData().getString("id");
            final Bundle data = msg.getData().getBundle("data");
            mPendingMessages.remove(messageId).complete(GeckoBundle.fromBundle(data));
            break;
          case MESSAGE_PAGE_STOP:
            if (mPageStop != null) {
              mPageStop.complete(null);
              mPageStop = null;
            }
            break;
          default:
            RuntimeInstance.this.handleMessage(msg);
            break;
        }
      }

      @Override
      public void onServiceConnected(ComponentName name, IBinder binder) {
        mMessenger = new Messenger(mClientHandler);
        mServiceMessenger = new Messenger(binder);
        isConnected = true;

        RuntimeInstance.this.sendMessage(MESSAGE_REGISTER);
      }

      @Override
      public void onServiceDisconnected(ComponentName name) {
        isConnected = false;
        context.unbindService(this);
        disconnected.complete(null);
      }
    }

    /** Override this to handle additional messages. */
    protected void handleMessage(Message msg) {}

    /** Override to modify the intent sent to the service */
    protected Intent createIntent(final Context context) {
      return new Intent(context, service);
    }

    private GeckoResult<GeckoBundle> sendMessageInternal(
        final int message, final GeckoBundle bundle, final GeckoResult<GeckoBundle> result) {
      if (!isConnected) {
        throw new IllegalStateException("Service is not connected yet!");
      }

      final String messageId = UUID.randomUUID().toString();
      GeckoBundle data = new GeckoBundle();
      data.putString("id", messageId);
      if (bundle != null) {
        data.putBundle("data", bundle);
      }

      Message msg = Message.obtain(null, message);
      msg.replyTo = mMessenger;
      msg.setData(data.toBundle());

      if (result != null) {
        mPendingMessages.put(messageId, result);
      }

      try {
        mServiceMessenger.send(msg);
      } catch (RemoteException ex) {
        throw new RuntimeException(ex);
      }

      return result;
    }

    private GeckoResult<Void> waitForPageStop() {
      if (mPageStop == null) {
        mPageStop = new GeckoResult<>();
      }
      return mPageStop;
    }

    protected GeckoResult<GeckoBundle> query(final int message) {
      return query(message, null);
    }

    protected GeckoResult<GeckoBundle> query(final int message, final GeckoBundle bundle) {
      final GeckoResult<GeckoBundle> result = new GeckoResult<>();
      return sendMessageInternal(message, bundle, result);
    }

    protected void sendMessage(final int message) {
      sendMessage(message, null);
    }

    protected void sendMessage(final int message, final GeckoBundle bundle) {
      sendMessageInternal(message, bundle, null);
    }

    protected void sendIntent() {
      final Intent intent = createIntent(context);
      intent.putExtra("args", "-profile " + mProfileFolder.getAbsolutePath());
      if (mTestData != null) {
        intent.putExtra("test-data", mTestData.toBundle());
      }
      context.bindService(intent, mClientHandler, Context.BIND_AUTO_CREATE);
    }

    /**
     * Quits the current runtime.
     *
     * @return a {@link GeckoResult} that is resolved when the service fully disconnects.
     */
    public GeckoResult<Void> quit() {
      sendMessage(MESSAGE_QUIT);
      return disconnected;
    }

    /**
     * Reloads the current session.
     *
     * @return A {@link GeckoResult} that is resolved when the page is fully reloaded.
     */
    public GeckoResult<Void> reload() {
      sendMessage(MESSAGE_RELOAD);
      return waitForPageStop();
    }

    /**
     * Load a test path in the current session.
     *
     * @return A {@link GeckoResult} that is resolved when the page is fully loaded.
     */
    public GeckoResult<Void> loadTestPath(final String path) {
      return loadUri(GeckoSessionTestRule.TEST_ENDPOINT + path);
    }

    /**
     * Load an arbitrary URI in the current session.
     *
     * @return A {@link GeckoResult} that is resolved when the page is fully loaded.
     */
    public GeckoResult<Void> loadUri(final String uri) {
      return started.then(
          unused -> {
            final GeckoBundle data = new GeckoBundle(1);
            data.putString("uri", uri);
            sendMessage(MESSAGE_LOAD_URI, data);
            return waitForPageStop();
          });
    }
  }
}
