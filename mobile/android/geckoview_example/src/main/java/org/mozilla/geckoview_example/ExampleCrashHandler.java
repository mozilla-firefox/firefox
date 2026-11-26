/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview_example;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.StrictMode;
import android.util.Log;
import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;
import org.mozilla.geckoview.BuildConfig;
import org.mozilla.geckoview.CrashReporter;
import org.mozilla.geckoview.GeckoRuntime;

public class ExampleCrashHandler extends Service {
  private static final String LOGTAG = "ExampleCrashHandler";

  private static final String CHANNEL_ID = "geckoview_example_crashes";
  private static final int NOTIFY_ID = 42;

  private static final String ACTION_REPORT_CRASH =
      "org.mozilla.geckoview_example.ACTION_REPORT_CRASH";
  private static final String ACTION_DISMISS = "org.mozilla.geckoview_example.ACTION_DISMISS";

  private Intent mCrashIntent;

  public ExampleCrashHandler() {}

  @Override
  public int onStartCommand(Intent intent, int flags, int startId) {
    if (intent == null) {
      stopSelf();
      return Service.START_NOT_STICKY;
    }

    if (GeckoRuntime.ACTION_CRASHED.equals(intent.getAction())) {
      mCrashIntent = intent;

      DebugLog.d(
          LOGTAG, "Dump File: " + mCrashIntent.getStringExtra(GeckoRuntime.EXTRA_MINIDUMP_PATH));
      DebugLog.d(
          LOGTAG, "Extras File: " + mCrashIntent.getStringExtra(GeckoRuntime.EXTRA_EXTRAS_PATH));
      DebugLog.d(
          LOGTAG,
          "Process Visibility: "
              + mCrashIntent.getStringExtra(GeckoRuntime.EXTRA_CRASH_PROCESS_VISIBILITY));
      DebugLog.d(
          LOGTAG,
          "Process Type: " + mCrashIntent.getStringExtra(GeckoRuntime.EXTRA_CRASH_PROCESS_TYPE));
      final String remoteType = mCrashIntent.getStringExtra(GeckoRuntime.EXTRA_CRASH_REMOTE_TYPE);
      if (remoteType != null) {
        DebugLog.d(LOGTAG, "Remote Type: " + remoteType);
      }

      String id = createNotificationChannel();

      int intentFlag = 0;
      intentFlag = PendingIntent.FLAG_IMMUTABLE;

      PendingIntent reportIntent =
          PendingIntent.getService(
              this,
              0,
              new Intent(ACTION_REPORT_CRASH, null, this, ExampleCrashHandler.class),
              intentFlag);

      PendingIntent dismissIntent =
          PendingIntent.getService(
              this,
              0,
              new Intent(ACTION_DISMISS, null, this, ExampleCrashHandler.class),
              intentFlag);

      Notification notification =
          new NotificationCompat.Builder(this, id)
              .setSmallIcon(R.drawable.ic_crash)
              .setContentTitle(getResources().getString(R.string.crashed_title))
              .setContentText(getResources().getString(R.string.crashed_text))
              .setDefaults(Notification.DEFAULT_ALL)
              .addAction(0, getResources().getString(R.string.crashed_ignore), dismissIntent)
              .addAction(0, getResources().getString(R.string.crashed_report), reportIntent)
              .setAutoCancel(true)
              .setOngoing(false)
              .build();

      startForeground(NOTIFY_ID, notification);
    } else if (ACTION_REPORT_CRASH.equals(intent.getAction())) {
      StrictMode.ThreadPolicy oldPolicy = null;
      if (BuildConfig.DEBUG_BUILD) {
        oldPolicy = StrictMode.getThreadPolicy();

        // We do some disk I/O and network I/O on the main thread, but it's fine.
        StrictMode.setThreadPolicy(
            new StrictMode.ThreadPolicy.Builder(oldPolicy)
                .permitDiskReads()
                .permitDiskWrites()
                .permitNetwork()
                .build());
      }

      try {
        CrashReporter.sendCrashReport(this, mCrashIntent, "GeckoViewExample");
      } catch (Exception e) {
        Log.e(LOGTAG, "Failed to send crash report", e);
      }

      if (oldPolicy != null) {
        StrictMode.setThreadPolicy(oldPolicy);
      }

      stopSelf();
    } else if (ACTION_DISMISS.equals(intent.getAction())) {
      stopSelf();
    }

    return Service.START_NOT_STICKY;
  }

  @Nullable
  @Override
  public IBinder onBind(Intent intent) {
    return null;
  }

  private String createNotificationChannel() {
    NotificationChannel channel =
        new NotificationChannel(
            CHANNEL_ID, "GeckoView Example Crashes", NotificationManager.IMPORTANCE_LOW);
    NotificationManager notificationManager = getSystemService(NotificationManager.class);
    notificationManager.createNotificationChannel(channel);
    return CHANNEL_ID;
  }
}
