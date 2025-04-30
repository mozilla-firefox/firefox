/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test_runner;

import android.app.Service;
import android.content.Intent;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;
import androidx.annotation.Nullable;
import java.util.HashMap;
import org.mozilla.geckoview.ContentBlocking;
import org.mozilla.geckoview.GeckoResult;
import org.mozilla.geckoview.GeckoRuntime;
import org.mozilla.geckoview.GeckoRuntimeSettings;
import org.mozilla.geckoview.WebExtension;
import org.mozilla.geckoview.WebExtensionController;

public class XpcshellTestRunnerService extends Service {
  public static final class i0 extends XpcshellTestRunnerService {}

  public static final class i1 extends XpcshellTestRunnerService {}

  public static final class i2 extends XpcshellTestRunnerService {}

  public static final class i3 extends XpcshellTestRunnerService {}

  public static final class i4 extends XpcshellTestRunnerService {}

  public static final class i5 extends XpcshellTestRunnerService {}

  public static final class i6 extends XpcshellTestRunnerService {}

  public static final class i7 extends XpcshellTestRunnerService {}

  public static final class i8 extends XpcshellTestRunnerService {}

  public static final class i9 extends XpcshellTestRunnerService {}

  private static final String LOGTAG = "XpcshellTestRunner";
  static GeckoRuntime sRuntime;

  private HashMap<String, WebExtension> mExtensions = new HashMap<>();

  private static WebExtensionController webExtensionController() {
    return sRuntime.getWebExtensionController();
  }

  @Override
  public synchronized int onStartCommand(Intent intent, int flags, int startId) {
    if (sRuntime != null) {
      // We don't support restarting GeckoRuntime
      throw new RuntimeException("Cannot start more than once");
    }

    final Bundle extras = intent.getExtras();
    for (final String key : extras.keySet()) {
      Log.i(LOGTAG, "Got extras " + key + "=" + extras.get(key));
    }

    final ContentBlocking.SafeBrowsingProvider googleLegacy =
        ContentBlocking.SafeBrowsingProvider.from(
                ContentBlocking.GOOGLE_LEGACY_SAFE_BROWSING_PROVIDER)
            .getHashUrl("http://mochi.test:8888/safebrowsing-dummy/gethash")
            .updateUrl("http://mochi.test:8888/safebrowsing-dummy/update")
            .build();

    final ContentBlocking.SafeBrowsingProvider google =
        ContentBlocking.SafeBrowsingProvider.from(ContentBlocking.GOOGLE_SAFE_BROWSING_PROVIDER)
            .getHashUrl("http://mochi.test:8888/safebrowsing4-dummy/gethash")
            .updateUrl("http://mochi.test:8888/safebrowsing4-dummy/update")
            .build();

    final GeckoRuntimeSettings runtimeSettings =
        new GeckoRuntimeSettings.Builder()
            .arguments(new String[] {"-xpcshell"})
            .extras(extras)
            .consoleOutput(true)
            .contentBlocking(
                new ContentBlocking.Settings.Builder()
                    .safeBrowsingProviders(google, googleLegacy)
                    .build())
            .lowMemoryDetection(false) // Avoid unpredictability in tests
            .build();

    sRuntime = GeckoRuntime.create(this, runtimeSettings);

    webExtensionController()
        .setDebuggerDelegate(
            new WebExtensionController.DebuggerDelegate() {
              @Override
              public void onExtensionListUpdated() {
                refreshExtensionList();
              }
            });

    webExtensionController()
        .setAddonManagerDelegate(new WebExtensionController.AddonManagerDelegate() {});

    sRuntime.setDelegate(
        () -> {
          stopSelf();
          System.exit(0);
        });

    return Service.START_NOT_STICKY;
  }

  // Random start timestamp for the BrowsingDataDelegate API.
  private static final int CLEAR_DATA_START_TIMESTAMP = 1234;

  private void refreshExtensionList() {
    webExtensionController()
        .list()
        .accept(
            extensions -> {
              mExtensions.clear();
              for (final WebExtension extension : extensions) {
                mExtensions.put(extension.id, extension);

                extension.setBrowsingDataDelegate(
                    new WebExtension.BrowsingDataDelegate() {
                      @Nullable
                      @Override
                      public GeckoResult<Settings> onGetSettings() {
                        final long types =
                            Type.CACHE
                                | Type.COOKIES
                                | Type.HISTORY
                                | Type.FORM_DATA
                                | Type.DOWNLOADS;
                        return GeckoResult.fromValue(
                            new Settings(CLEAR_DATA_START_TIMESTAMP, types, types));
                      }
                    });
              }
            });
  }

  @Override
  public synchronized IBinder onBind(Intent intent) {
    return null;
  }
}
