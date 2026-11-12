/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.util.Log;
import java.util.UUID;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.mozglue.JNIObject;

public final class MediaDrmProxy {
  private static final String LOGTAG = "GeckoMediaDrmProxy";
  private static final boolean DEBUG = false;

  // A flag to avoid using the native object that has been destroyed.
  private boolean mDestroyed;
  private GeckoMediaDrm mImpl;
  private String mDrmStubId;

  // Interface for callback to native.
  public interface Callbacks {
    void onSessionCreated(int createSessionToken, int promiseId, byte[] sessionId, byte[] request);

    void onSessionUpdated(int promiseId, byte[] sessionId);

    void onSessionClosed(int promiseId, byte[] sessionId);

    void onSessionMessage(byte[] sessionId, int sessionMessageType, byte[] request);

    void onSessionError(byte[] sessionId, String message);

    // MediaDrm.KeyStatus is available in API level 23(M)
    // https://developer.android.com/reference/android/media/MediaDrm.KeyStatus.html
    // For compatibility between L and M above, we'll unwrap the KeyStatus structure
    // and store the keyid and status into SessionKeyInfo and pass to native(MediaDrmCDMProxy).
    void onSessionBatchedKeyChanged(byte[] sessionId, SessionKeyInfo[] keyInfos);

    void onRejectPromise(int promiseId, String message);
  } // Callbacks

  public static class NativeMediaDrmProxyCallbacks extends JNIObject implements Callbacks {
    @WrapForJNI(calledFrom = "gecko")
    NativeMediaDrmProxyCallbacks() {}

    @Override
    @WrapForJNI(dispatchTo = "gecko")
    public native void onSessionCreated(
        int createSessionToken, int promiseId, byte[] sessionId, byte[] request);

    @Override
    @WrapForJNI(dispatchTo = "gecko")
    public native void onSessionUpdated(int promiseId, byte[] sessionId);

    @Override
    @WrapForJNI(dispatchTo = "gecko")
    public native void onSessionClosed(int promiseId, byte[] sessionId);

    @Override
    @WrapForJNI(dispatchTo = "gecko")
    public native void onSessionMessage(byte[] sessionId, int sessionMessageType, byte[] request);

    @Override
    @WrapForJNI(dispatchTo = "gecko")
    public native void onSessionError(byte[] sessionId, String message);

    @Override
    @WrapForJNI(dispatchTo = "gecko")
    public native void onSessionBatchedKeyChanged(byte[] sessionId, SessionKeyInfo[] keyInfos);

    @Override
    @WrapForJNI(dispatchTo = "gecko")
    public native void onRejectPromise(int promiseId, String message);

    @Override // JNIObject
    protected void disposeNative() {
      throw new UnsupportedOperationException();
    }
  } // NativeMediaDrmProxyCallbacks

  // A proxy to callback from LocalMediaDrmBridge to native instance.
  public static class MediaDrmProxyCallbacks implements GeckoMediaDrm.Callbacks {
    private final Callbacks mNativeCallbacks;
    private final MediaDrmProxy mProxy;

    public MediaDrmProxyCallbacks(final MediaDrmProxy proxy, final Callbacks callbacks) {
      mNativeCallbacks = callbacks;
      mProxy = proxy;
    }

    @Override
    public void onSessionCreated(
        final int createSessionToken,
        final int promiseId,
        final byte[] sessionId,
        final byte[] request) {
      if (!mProxy.isDestroyed()) {
        mNativeCallbacks.onSessionCreated(createSessionToken, promiseId, sessionId, request);
      }
    }

    @Override
    public void onSessionUpdated(final int promiseId, final byte[] sessionId) {
      if (!mProxy.isDestroyed()) {
        mNativeCallbacks.onSessionUpdated(promiseId, sessionId);
      }
    }

    @Override
    public void onSessionClosed(final int promiseId, final byte[] sessionId) {
      if (!mProxy.isDestroyed()) {
        mNativeCallbacks.onSessionClosed(promiseId, sessionId);
      }
    }

    @Override
    public void onSessionMessage(
        final byte[] sessionId, final int sessionMessageType, final byte[] request) {
      if (!mProxy.isDestroyed()) {
        mNativeCallbacks.onSessionMessage(sessionId, sessionMessageType, request);
      }
    }

    @Override
    public void onSessionError(final byte[] sessionId, final String message) {
      if (!mProxy.isDestroyed()) {
        mNativeCallbacks.onSessionError(sessionId, message);
      }
    }

    @Override
    public void onSessionBatchedKeyChanged(
        final byte[] sessionId, final SessionKeyInfo[] keyInfos) {
      if (!mProxy.isDestroyed()) {
        mNativeCallbacks.onSessionBatchedKeyChanged(sessionId, keyInfos);
      }
    }

    @Override
    public void onRejectPromise(final int promiseId, final String message) {
      if (!mProxy.isDestroyed()) {
        mNativeCallbacks.onRejectPromise(promiseId, message);
      }
    }
  } // MediaDrmProxyCallbacks

  public boolean isDestroyed() {
    return mDestroyed;
  }

  @WrapForJNI(calledFrom = "gecko")
  public static MediaDrmProxy create(final String keySystem, final Callbacks nativeCallbacks) {
    final MediaDrmProxy proxy = new MediaDrmProxy(keySystem, nativeCallbacks);
    return proxy.mImpl != null ? proxy : null;
  }

  MediaDrmProxy(final String keySystem, final Callbacks nativeCallbacks) {
    if (DEBUG) Log.d(LOGTAG, "Constructing MediaDrmProxy");
    try {
      mDrmStubId = UUID.randomUUID().toString();
      final IMediaDrmBridge remoteBridge =
          RemoteManager.getInstance().createRemoteMediaDrmBridge(keySystem, mDrmStubId);
      if (remoteBridge == null) {
        throw new IllegalStateException("No remote MediaDrm bridge");
      }
      final RemoteMediaDrmBridge impl = new RemoteMediaDrmBridge(remoteBridge);
      impl.setCallbacks(new MediaDrmProxyCallbacks(this, nativeCallbacks));
      mImpl = impl;
    } catch (final Exception e) {
      Log.e(LOGTAG, "Constructing MediaDrmProxy ... error", e);
    }
  }

  @WrapForJNI
  private void createSession(
      final int createSessionToken,
      final int promiseId,
      final String initDataType,
      final byte[] initData) {
    if (DEBUG) Log.d(LOGTAG, "createSession, promiseId = " + promiseId);
    mImpl.createSession(createSessionToken, promiseId, initDataType, initData);
  }

  @WrapForJNI
  private void updateSession(final int promiseId, final String sessionId, final byte[] response) {
    if (DEBUG)
      Log.d(LOGTAG, "updateSession, primiseId(" + promiseId + "sessionId(" + sessionId + ")");
    mImpl.updateSession(promiseId, sessionId, response);
  }

  @WrapForJNI
  private void closeSession(final int promiseId, final String sessionId) {
    if (DEBUG)
      Log.d(LOGTAG, "closeSession, primiseId(" + promiseId + "sessionId(" + sessionId + ")");
    mImpl.closeSession(promiseId, sessionId);
  }

  @WrapForJNI(calledFrom = "gecko")
  private String getStubId() {
    return mDrmStubId;
  }

  @WrapForJNI
  public boolean setServerCertificate(final byte[] cert) {
    try {
      mImpl.setServerCertificate(cert);
      return true;
    } catch (final RuntimeException e) {
      return false;
    }
  }

  @WrapForJNI
  public void setOriginID(final String originID) {
    if (DEBUG) Log.d(LOGTAG, "setOriginID");
    try {
      mImpl.setOriginID(originID);
    } catch (final RuntimeException e) {
      Log.w(LOGTAG, "setOriginID failed", e);
    }
  }

  @WrapForJNI // Called when natvie object is destroyed.
  private void destroy() {
    if (DEBUG) Log.d(LOGTAG, "destroy!! Native object is destroyed.");
    if (mDestroyed) {
      return;
    }
    mDestroyed = true;
    release();
  }

  private void release() {
    if (DEBUG) Log.d(LOGTAG, "release");
    mImpl.release();
  }
}
