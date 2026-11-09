/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import org.json.JSONException;
import org.json.JSONObject;
import org.mozilla.geckoview.GeckoRuntime;
import org.mozilla.geckoview.test.util.UiThreadUtils;

public class TestCrashHandler extends Service {
  private static final int MSG_EVAL_NEXT_CRASH_DUMP = 1;
  private static final int MSG_CRASH_DUMP_EVAL_RESULT = 2;
  private static final String LOGTAG = "TestCrashHandler";

  public static final class EvalResult {
    private static final String BUNDLE_KEY_RESULT = "TestCrashHandler.EvalResult.mResult";
    private static final String BUNDLE_KEY_MSG = "TestCrashHandler.EvalResult.mMsg";

    public EvalResult(final boolean result, final String msg) {
      mResult = result;
      mMsg = msg;
    }

    public EvalResult(final Bundle bundle) {
      mResult = bundle.getBoolean(BUNDLE_KEY_RESULT, false);
      mMsg = bundle.getString(BUNDLE_KEY_MSG);
    }

    public Bundle asBundle() {
      final Bundle bundle = new Bundle();
      bundle.putBoolean(BUNDLE_KEY_RESULT, mResult);
      bundle.putString(BUNDLE_KEY_MSG, mMsg);
      return bundle;
    }

    public boolean mResult;
    public String mMsg;
  }

  public static final class Client {
    private static final String LOGTAG = "TestCrashHandler.Client";

    private class Receiver extends Handler {
      public Receiver(final Looper looper) {
        super(looper);
      }

      @Override
      public void handleMessage(final Message msg) {
        if (msg.what == MSG_CRASH_DUMP_EVAL_RESULT) {
          setEvalResult(new EvalResult(msg.getData()));
          return;
        }

        super.handleMessage(msg);
      }
    }

    private Receiver mReceiver;
    private boolean mDoUnbind = false;
    private Messenger mService = null;
    private Messenger mMessenger;
    private Context mContext;
    private HandlerThread mThread;
    private EvalResult mResult = null;

    private ServiceConnection mConnection =
        new ServiceConnection() {
          @Override
          public void onServiceConnected(final ComponentName className, final IBinder service) {
            mService = new Messenger(service);
          }

          @Override
          public void onServiceDisconnected(final ComponentName className) {
            disconnect();
          }
        };

    public Client(final Context context) {
      mContext = context;
      mThread = new HandlerThread("TestCrashHandler.Client");
      mThread.start();
      mReceiver = new Receiver(mThread.getLooper());
      mMessenger = new Messenger(mReceiver);
    }

    /**
     * Tests should call this to notify the crash handler that the next crash it sees is intentional
     * and that its intent should be checked for correctness.
     *
     * @param expectedProcessVisibility The visibility of the process the incoming crash is expected
     *     to be for.
     * @param expectedProcessType The type of process the incoming crash is expected to be for.
     * @param expectedRemoteType The type of content process the incoming crash is expected to be
     *     for.
     */
    public void setEvalNextCrashDump(
        final String expectedProcessVisibility,
        final String expectedProcessType,
        final String expectedRemoteType) {
      setEvalResult(null);
      final Bundle bundle = new Bundle();
      bundle.putString(GeckoRuntime.EXTRA_CRASH_PROCESS_VISIBILITY, expectedProcessVisibility);
      bundle.putString(GeckoRuntime.EXTRA_CRASH_PROCESS_TYPE, expectedProcessType);
      bundle.putString(GeckoRuntime.EXTRA_CRASH_REMOTE_TYPE, expectedRemoteType);
      final Message msg = Message.obtain(null, MSG_EVAL_NEXT_CRASH_DUMP, bundle);
      msg.replyTo = mMessenger;

      try {
        mService.send(msg);
      } catch (final RemoteException e) {
        throw new RuntimeException(e.getMessage());
      }
    }

    public boolean connect(final long timeoutMillis) {
      final Intent intent = new Intent(mContext, TestCrashHandler.class);
      mDoUnbind =
          mContext.bindService(
              intent, mConnection, Context.BIND_AUTO_CREATE | Context.BIND_IMPORTANT);
      if (!mDoUnbind) {
        return false;
      }

      UiThreadUtils.waitForCondition(() -> mService != null, timeoutMillis);

      return mService != null;
    }

    public void disconnect() {
      if (mDoUnbind) {
        mContext.unbindService(mConnection);
        mService = null;
        mDoUnbind = false;
      }
      mThread.quitSafely();
    }

    private synchronized void setEvalResult(final EvalResult result) {
      mResult = result;
    }

    private synchronized EvalResult getEvalResult() {
      return mResult;
    }

    /**
     * Tests should call this method after initiating the intentional crash to wait for the result
     * from the crash handler.
     *
     * @param timeoutMillis timeout in milliseconds
     * @return EvalResult containing the boolean result of the test and an error message.
     */
    public EvalResult getEvalResult(final long timeoutMillis) {
      UiThreadUtils.waitForCondition(() -> getEvalResult() != null, timeoutMillis);
      return getEvalResult();
    }
  }

  private static final class MessageHandler extends Handler {
    private Messenger mReplyToMessenger;
    private String mExpectedProcessVisibility;
    private String mExpectedProcessType;
    private String mExpectedRemoteType;

    MessageHandler() {
      super(Looper.myLooper());
    }

    @Override
    public void handleMessage(final Message msg) {
      if (msg.what == MSG_EVAL_NEXT_CRASH_DUMP) {
        mReplyToMessenger = msg.replyTo;
        Bundle bundle = (Bundle) msg.obj;
        mExpectedProcessVisibility = bundle.getString(GeckoRuntime.EXTRA_CRASH_PROCESS_VISIBILITY);
        mExpectedProcessType = bundle.getString(GeckoRuntime.EXTRA_CRASH_PROCESS_TYPE);
        mExpectedRemoteType = bundle.getString(GeckoRuntime.EXTRA_CRASH_REMOTE_TYPE);
        return;
      }

      super.handleMessage(msg);
    }

    public void reportResult(final EvalResult result) {
      if (mReplyToMessenger == null) {
        return;
      }

      final Message msg = Message.obtain(null, MSG_CRASH_DUMP_EVAL_RESULT);
      msg.setData(result.asBundle());

      try {
        mReplyToMessenger.send(msg);
      } catch (final RemoteException e) {
        throw new RuntimeException(e.getMessage());
      }

      mReplyToMessenger = null;
    }

    public String getExpectedProcessVisibility() {
      return mExpectedProcessVisibility;
    }

    public String getExpectedProcessType() {
      return mExpectedProcessType;
    }

    public String getExpectedRemoteType() {
      return mExpectedRemoteType;
    }
  }

  private Messenger mMessenger;
  private MessageHandler mMsgHandler;

  public TestCrashHandler() {}

  private static JSONObject readExtraFile(final String filePath) throws IOException, JSONException {
    final byte[] buffer = new byte[4096];
    final FileInputStream inputStream = new FileInputStream(filePath);
    final ByteArrayOutputStream outputStream = new ByteArrayOutputStream();
    int bytesRead = 0;

    while ((bytesRead = inputStream.read(buffer)) != -1) {
      outputStream.write(buffer, 0, bytesRead);
    }

    final String contents = new String(outputStream.toByteArray(), "UTF-8");
    return new JSONObject(contents);
  }

  private static boolean hasMinidumpHeader(final File file) {
    final byte[] signature = new byte[4];
    try (FileInputStream inputStream = new FileInputStream(file)) {
      return inputStream.read(signature) == signature.length
          && new String(signature, "US-ASCII").equals("MDMP");
    } catch (final IOException e) {
      return false;
    }
  }

  private EvalResult evalCrashInfo(final Intent intent) {
    if (!intent.getAction().equals(GeckoRuntime.ACTION_CRASHED)) {
      return new EvalResult(false, "Action should match");
    }

    final File dumpFile = new File(intent.getStringExtra(GeckoRuntime.EXTRA_MINIDUMP_PATH));
    final boolean dumpFileExists = dumpFile.exists();
    final boolean dumpFileValid = hasMinidumpHeader(dumpFile);
    dumpFile.delete();

    final File extrasFile = new File(intent.getStringExtra(GeckoRuntime.EXTRA_EXTRAS_PATH));
    final boolean extrasFileExists = extrasFile.exists();
    final boolean expectAnnotations;
    try {
      final JSONObject annotations = readExtraFile(extrasFile.getPath());

      // TODO: remove this when bug 2082922 is fixed
      expectAnnotations = !annotations.optString("MinidumpRemoteExecutorForTesting").equals("1");

      if (expectAnnotations && !annotations.getString("MozCrashReason").startsWith("MOZ_CRASH(")) {
        return new EvalResult(false, "Missing or invalid child crash annotations");
      }

      final String dumperError = annotations.optString("DumperError");
      if (!dumperError.isEmpty()
          && (expectAnnotations || !dumperError.equals("MissingChildProcessAnnotations"))) {
        return new EvalResult(false, "Minidump writer error: " + dumperError);
      }

      extrasFile.delete();
    } catch (final Exception e) {
      return new EvalResult(false, e.toString());
    }

    if (!dumpFileExists) {
      return new EvalResult(false, "Dump file should exist");
    }

    if (!dumpFileValid) {
      return new EvalResult(false, "Dump file should be a minidump");
    }

    if (!extrasFileExists) {
      return new EvalResult(false, "Extras file should exist");
    }

    final String expectedProcessVisibility = mMsgHandler.getExpectedProcessVisibility();
    final String processVisibility =
        intent.getStringExtra(GeckoRuntime.EXTRA_CRASH_PROCESS_VISIBILITY);
    if (processVisibility == null) {
      return new EvalResult(false, "Intent missing process visibility");
    }
    if (!processVisibility.equals(expectedProcessVisibility)) {
      return new EvalResult(
          false,
          "Expected process visibility "
              + expectedProcessVisibility
              + ", found "
              + processVisibility);
    }

    final String expectedProcessType = mMsgHandler.getExpectedProcessType();
    final String processType = intent.getStringExtra(GeckoRuntime.EXTRA_CRASH_PROCESS_TYPE);
    if (processType == null) {
      return new EvalResult(false, "Intent missing process type");
    }
    if (!processType.equals(expectedProcessType)) {
      return new EvalResult(
          false, "Expected process type " + expectedProcessType + ", found " + processType);
    }

    final String expectedRemoteType = mMsgHandler.getExpectedRemoteType();
    final String remoteType = intent.getStringExtra(GeckoRuntime.EXTRA_CRASH_REMOTE_TYPE);
    if ((remoteType == null && expectedRemoteType != null && expectAnnotations)
        || (remoteType != null && !remoteType.equals(expectedRemoteType))) {
      return new EvalResult(
          false, "Expected remote type " + expectedRemoteType + ", found " + remoteType);
    }

    return new EvalResult(true, "Crash Dump OK");
  }

  @Override
  public synchronized int onStartCommand(final Intent intent, final int flags, final int startId) {
    final NotificationManager manager = getSystemService(NotificationManager.class);
    manager.createNotificationChannel(
        new NotificationChannel(LOGTAG, LOGTAG, NotificationManager.IMPORTANCE_LOW));
    startForeground(
        1,
        new Notification.Builder(this, LOGTAG)
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentTitle(LOGTAG)
            .build());

    if (mMsgHandler != null) {
      mMsgHandler.reportResult(evalCrashInfo(intent));
      // We must manually call stopSelf() here to ensure the Service gets killed once the client
      // unbinds. If we don't, then when the next client attempts to bind for a different test,
      // onBind() will not be called, and mMsgHandler will not get set.
      stopSelf();
      return Service.START_NOT_STICKY;
    }

    // We don't want to do anything, this handler only exists
    // so we produce a crash dump which is picked up by the
    // test harness.
    System.exit(0);
    return Service.START_NOT_STICKY;
  }

  @Override
  public synchronized IBinder onBind(final Intent intent) {
    mMsgHandler = new MessageHandler();
    mMessenger = new Messenger(mMsgHandler);
    return mMessenger.getBinder();
  }

  @Override
  public synchronized boolean onUnbind(final Intent intent) {
    mMsgHandler = null;
    mMessenger = null;
    return false;
  }
}
