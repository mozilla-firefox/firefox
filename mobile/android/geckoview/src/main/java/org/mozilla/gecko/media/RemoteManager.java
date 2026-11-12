/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.media.MediaFormat;
import android.os.DeadObjectException;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;
import java.util.LinkedList;
import java.util.List;
import java.util.NoSuchElementException;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.gfx.GeckoSurface;

public final class RemoteManager implements IBinder.DeathRecipient {
  private static final String LOGTAG = "GeckoRemoteManager";
  private static final boolean DEBUG = false;
  private static RemoteManager sRemoteManager = null;

  public static synchronized RemoteManager getInstance() {
    if (sRemoteManager == null) {
      sRemoteManager = new RemoteManager();
    }

    sRemoteManager.init();
    return sRemoteManager;
  }

  private List<CodecProxy> mCodecs = new LinkedList<CodecProxy>();
  private List<IMediaDrmBridge> mDrmBridges = new LinkedList<IMediaDrmBridge>();

  private volatile IMediaManager mRemote;

  private final class RemoteConnection implements ServiceConnection {
    @Override
    public void onServiceConnected(final ComponentName name, final IBinder service) {
      if (DEBUG) Log.d(LOGTAG, "service connected");
      try {
        service.linkToDeath(RemoteManager.this, 0);
      } catch (final RemoteException e) {
        e.printStackTrace();
      }
      synchronized (this) {
        mRemote = IMediaManager.Stub.asInterface(service);
        notify();
      }
    }

    @Override
    public void onServiceDisconnected(final ComponentName name) {
      if (DEBUG) Log.d(LOGTAG, "service disconnected");
      unlink();
    }

    private boolean connect() {
      final Context appCtxt = GeckoAppShell.getApplicationContext();
      appCtxt.bindService(
          new Intent(appCtxt, MediaManager.class),
          mConnection,
          Context.BIND_AUTO_CREATE | Context.BIND_IMPORTANT);
      waitConnect();
      return mRemote != null;
    }

    // Wait up to 5s.
    private synchronized void waitConnect() {
      int waitCount = 0;
      while (mRemote == null && waitCount < 5) {
        try {
          wait(1000);
          waitCount++;
        } catch (final InterruptedException e) {
          if (DEBUG) {
            e.printStackTrace();
          }
        }
      }
      if (DEBUG) {
        Log.d(
            LOGTAG,
            "wait ~" + waitCount + "s for connection: " + (mRemote == null ? "fail" : "ok"));
      }
    }

    private synchronized void waitDisconnect() {
      while (mRemote != null) {
        try {
          wait(1000);
        } catch (final InterruptedException e) {
          if (DEBUG) {
            e.printStackTrace();
          }
        }
      }
    }

    private synchronized void unlink() {
      if (mRemote == null) {
        return;
      }
      try {
        mRemote.asBinder().unlinkToDeath(RemoteManager.this, 0);
      } catch (final NoSuchElementException e) {
        Log.w(LOGTAG, "death recipient already released");
      }
      mRemote = null;
      notify();
    }
  }

  RemoteConnection mConnection = new RemoteConnection();

  private synchronized boolean init() {
    if (mRemote != null) {
      return true;
    }

    if (DEBUG) Log.d(LOGTAG, "init remote manager " + this);
    return mConnection.connect();
  }

  public synchronized CodecProxy createCodec(
      final boolean isEncoder,
      final MediaFormat format,
      final GeckoSurface surface,
      final CodecProxy.Callbacks callbacks,
      final String drmStubId) {
    if (mRemote == null) {
      if (DEBUG) Log.d(LOGTAG, "createCodec failed due to not initialize");
      return null;
    }
    try {
      final ICodec remote = mRemote.createCodec();
      final CodecProxy proxy =
          CodecProxy.createCodecProxy(isEncoder, format, surface, callbacks, drmStubId);
      if (proxy.init(remote)) {
        mCodecs.add(proxy);
        return proxy;
      } else {
        // proxy.init() failed (e.g. configure() found no matching codec), so
        // this request will never reach releaseCodec()'s endRequest() call.
        // Balance the createCodec() call's implicit request count here, or
        // MediaManager.onUnbind() will see a stale active-request count on a
        // later, unrelated unbind and kill its own process.
        try {
          mRemote.endRequest();
        } catch (final RemoteException | NullPointerException e) {
          Log.e(LOGTAG, "fail to report remote codec creation failure", e);
        }
        return null;
      }
    } catch (final RemoteException e) {
      e.printStackTrace();
      return null;
    }
  }

  public synchronized IMediaDrmBridge createRemoteMediaDrmBridge(
      final String keySystem, final String stubId) {
    if (mRemote == null) {
      if (DEBUG) Log.d(LOGTAG, "createRemoteMediaDrmBridge failed due to not initialize");
      return null;
    }
    try {
      final IMediaDrmBridge remoteBridge = mRemote.createRemoteMediaDrmBridge(keySystem, stubId);
      if (remoteBridge != null) {
        mDrmBridges.add(remoteBridge);
      }
      return remoteBridge;
    } catch (final RemoteException e) {
      Log.e(LOGTAG, "Got exception during createRemoteMediaDrmBridge().", e);
      return null;
    }
  }

  @Override
  public void binderDied() {
    Log.e(LOGTAG, "remote codec is dead");
    handleRemoteDeath();
  }

  private synchronized void handleRemoteDeath() {
    mConnection.waitDisconnect();

    notifyError(!(init() && recoverRemoteCodec()));
  }

  private synchronized void notifyError(final boolean fatal) {
    for (final CodecProxy proxy : mCodecs) {
      proxy.reportError(fatal);
    }
  }

  private synchronized boolean recoverRemoteCodec() {
    if (DEBUG) Log.d(LOGTAG, "recover codec");
    boolean ok = true;
    try {
      for (final CodecProxy proxy : mCodecs) {
        ok &= proxy.init(mRemote.createCodec());
      }
      return ok;
    } catch (final RemoteException e) {
      return false;
    }
  }

  public void releaseCodec(final CodecProxy proxy) throws DeadObjectException, RemoteException {
    if (mRemote == null) {
      if (DEBUG) Log.d(LOGTAG, "releaseCodec called but not initialized yet");
      return;
    }
    proxy.deinit();
    synchronized (this) {
      if (mCodecs.remove(proxy)) {
        try {
          mRemote.endRequest();
          releaseIfNeeded();
        } catch (final RemoteException | NullPointerException e) {
          Log.e(LOGTAG, "fail to report remote codec disconnection");
        }
      }
    }
  }

  private void releaseIfNeeded() {
    if (!mCodecs.isEmpty() || !mDrmBridges.isEmpty()) {
      return;
    }

    if (DEBUG) Log.d(LOGTAG, "release remote manager " + this);
    mConnection.unlink();
    final Context appCtxt = GeckoAppShell.getApplicationContext();
    appCtxt.unbindService(mConnection);
  }

  public void onRemoteMediaDrmBridgeReleased(final IMediaDrmBridge remote) {
    if (!mDrmBridges.contains(remote)) {
      Log.e(LOGTAG, "Try to release unknown remote MediaDrm bridge: " + remote);
      return;
    }

    synchronized (this) {
      if (mDrmBridges.remove(remote)) {
        try {
          mRemote.endRequest();
          releaseIfNeeded();
        } catch (final RemoteException | NullPointerException e) {
          Log.e(LOGTAG, "Fail to report remote DRM bridge disconnection");
        }
      }
    }
  }
} // RemoteManager
