/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.Process;
import android.os.RemoteException;
import android.util.Log;
import org.mozilla.gecko.mozglue.GeckoLoader;
import org.mozilla.geckoview.BuildConfig;

public final class MediaManager extends Service {
  private static final String LOGTAG = "GeckoMediaManager";
  private static final boolean DEBUG = !BuildConfig.MOZILLA_OFFICIAL;
  private static boolean sNativeLibLoaded;
  private int mNumActiveRequests = 0;

  private Binder mBinder =
      new IMediaManager.Stub() {
        @Override
        public ICodec createCodec() throws RemoteException {
          if (DEBUG) Log.d(LOGTAG, "request codec. Current active requests:" + mNumActiveRequests);
          mNumActiveRequests++;
          return new Codec();
        }

        @Override
        public IMediaDrmBridge createRemoteMediaDrmBridge(
            final String keySystem, final String stubId) throws RemoteException {
          if (DEBUG)
            Log.d(LOGTAG, "request DRM bridge. Current active requests:" + mNumActiveRequests);
          final RemoteMediaDrmBridgeStub stub;
          try {
            stub = new RemoteMediaDrmBridgeStub(keySystem, stubId);
          } catch (final RemoteException e) {
            // A RemoteException cannot be sent to the caller, and the caller
            // will not end a request for a bridge it never received.
            Log.e(LOGTAG, "Cannot create DRM bridge", e);
            return null;
          }
          mNumActiveRequests++;
          return stub;
        }

        @Override
        public void endRequest() {
          if (DEBUG) Log.d(LOGTAG, "end request. Current active requests:" + mNumActiveRequests);
          if (mNumActiveRequests > 0) {
            mNumActiveRequests--;
          } else {
            final RuntimeException e =
                new RuntimeException("unmatched codec/DRM bridge creation and ending calls!");
            Log.e(LOGTAG, "Error:", e);
          }
        }
      };

  @Override
  public synchronized void onCreate() {
    if (!sNativeLibLoaded) {
      GeckoLoader.doLoadLibrary(this, "mozglue");
      GeckoLoader.suppressCrashDialog();
      sNativeLibLoaded = true;
    }
  }

  @Override
  public IBinder onBind(final Intent intent) {
    return mBinder;
  }

  @Override
  public boolean onUnbind(final Intent intent) {
    Log.i(LOGTAG, "Media service has been unbound. Stopping.");
    stopSelf();
    if (mNumActiveRequests != 0) {
      // Not unbound by RemoteManager -- caller process is dead.
      Log.w(LOGTAG, "unbound while client still active.");
      Process.killProcess(Process.myPid());
    }
    return false;
  }
}
