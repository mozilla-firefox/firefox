/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc.videoengine;

import java.io.IOException;
import java.util.List;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.util.Log;
import android.view.Display;
import android.view.Surface;
import android.view.WindowManager;
import androidx.annotation.NonNull;

import java.util.concurrent.CountDownLatch;

import org.mozilla.gecko.annotation.WebRTCJNITarget;
import org.mozilla.gecko.GeckoAppShell;

import org.webrtc.CameraEnumerator;
import org.webrtc.Camera1Enumerator;
import org.webrtc.Camera2Enumerator;
import org.webrtc.CameraVideoCapturer;
import org.webrtc.CapturerObserver;
import org.webrtc.EglBase;
import org.webrtc.SurfaceTextureHelper;
import org.webrtc.VideoFrame;
import org.webrtc.VideoFrame.I420Buffer;

public class VideoCaptureAndroid implements CameraVideoCapturer.CameraEventsHandler, CapturerObserver {
  private final static String TAG = "WEBRTC-JC";

  private final String deviceName;
  private volatile long native_capturer;  // |VideoCaptureAndroid*| in C++.
  private final Context context;
  private CameraVideoCapturer cameraVideoCapturer;

  // This class is recreated everytime we start/stop capture, so we
  // can safely create the CountDownLatches here.
  private final CountDownLatch capturerStarted = new CountDownLatch(1);
  private boolean capturerStartedSucceeded = false;
  private final CountDownLatch capturerStopped = new CountDownLatch(1);

  @WebRTCJNITarget
  public static VideoCaptureAndroid create(@NonNull final String deviceName) {
    final Context context = GetRotationAwareContext();
    return new VideoCaptureAndroid(context, deviceName,
                                   Camera2Enumerator.isSupported(context)
                                       ? new Camera2Enumerator(context)
                                       : new Camera1Enumerator());
  }

  // libwebrtc reads the device rotation per-frame via
  // applicationContext.getSystemService(WINDOW_SERVICE).getDefaultDisplay().getRotation()
  // (see Camera{1,2}Session.getFrameOrientation). When the host app's Application
  // context has had its base wrapped via Context.createConfigurationContext — as
  // Fenix does for locale handling in FenixApplication.attachBaseContext — that
  // WindowManager returns a Display whose rotation is latched to the value at
  // process start, so the rotation stamped on every captured frame never updates as
  // the device rotates. Build a context bound to the default display so the
  // WindowManager libwebrtc sees returns a live Display.
  //
  // The Display-taking createWindowContext overload is API 31 (S), not 30 (R), so
  // we gate it on S to mirror GeckoAppShell.AndroidSScreenCompat, which makes the
  // same call for the screen-orientation path. On older versions we fall back to
  // createDisplayContext, which likewise binds the context to a live Display.
  private static Context GetRotationAwareContext() {
    final Context appContext = GeckoAppShell.getApplicationContext();
    try {
      final DisplayManager dm =
          (DisplayManager) appContext.getSystemService(Context.DISPLAY_SERVICE);
      if (dm == null) {
        return appContext;
      }
      final Display display = dm.getDisplay(Display.DEFAULT_DISPLAY);
      if (display == null) {
        return appContext;
      }
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        return appContext.createWindowContext(
            display, WindowManager.LayoutParams.TYPE_APPLICATION, null);
      }
      return appContext.createDisplayContext(display);
    } catch (Throwable t) {
      Log.w(TAG, "GetRotationAwareContext: falling back to application context", t);
      return appContext;
    }
  }

  private VideoCaptureAndroid(@NonNull final Context context, @NonNull final String deviceName, @NonNull final CameraEnumerator enumerator) {
    // Remove the camera facing information from the name.
    String[] parts = deviceName.split("Facing (front|back):");
    if (parts.length == 2) {
      this.deviceName = parts[1].replace(" (infrared)", "");
    } else {
      Log.e(TAG, "VideoCaptureAndroid: Expected facing mode as part of name: " + deviceName);
      this.deviceName = deviceName;
    }
    this.context = context;

    try {
      cameraVideoCapturer = enumerator.createCapturer(this.deviceName, this);
      final EglBase eglBase = EglBase.create();
      final SurfaceTextureHelper surfaceTextureHelper = SurfaceTextureHelper.create("VideoCaptureAndroidSurfaceTextureHelper", eglBase.getEglBaseContext());
      cameraVideoCapturer.initialize(surfaceTextureHelper, context, this);
    } catch (java.lang.RuntimeException e) {
      Log.e(TAG, "VideoCaptureAndroid: Exception while creating capturer: " + e);
      cameraVideoCapturer = null;
    }
  }

  public boolean canCapture() {
    return cameraVideoCapturer != null;
  }

  // Called by native code.  Returns true if capturer is started.
  //
  // Note that this actually opens the camera, and Camera callbacks run on the
  // thread that calls open(), so this is done on the CameraThread.  Since ViE
  // API needs a synchronous success return value we wait for the result.
  @WebRTCJNITarget
  private synchronized boolean startCapture(
      final int width, final int height,
      final int min_mfps, final int max_mfps,
      long native_capturer) {
    Log.d(TAG, "startCapture: " + width + "x" + height + "@" +
        min_mfps + ":" + max_mfps);

    if (cameraVideoCapturer == null) {
      return false;
    }

    if (native_capturer == 0) {
      Log.d(TAG, "startCapture: invalid native capturer pointer");
      return false;
    }

    if (this.native_capturer != 0) {
      Log.d(TAG, "startCapture: already started");
      return true;
    }

    cameraVideoCapturer.startCapture(width, height, max_mfps);
    try {
      capturerStarted.await();
    } catch (InterruptedException e) {
      return false;
    }
    if (capturerStartedSucceeded) {
      this.native_capturer = native_capturer;
    }
    return capturerStartedSucceeded;
  }

  // Called by native code.  Returns true when camera is known to be stopped.
  @WebRTCJNITarget
  private synchronized boolean stopCapture() {
    Log.d(TAG, "stopCapture");
    if (cameraVideoCapturer == null) {
      return false;
    }

    if (native_capturer == 0) {
      Log.d(TAG, "stopCapture: wasn't started");
      return true;
    }

    native_capturer = 0;
    try {
      cameraVideoCapturer.stopCapture();
      capturerStopped.await();
    } catch (InterruptedException e) {
      return false;
    }
    Log.d(TAG, "stopCapture done");
    return true;
  }

  @WebRTCJNITarget
  @SuppressWarnings("deprecation")
  private int getDeviceOrientation() {
    int orientation = 0;
    if (context != null) {
      WindowManager wm = (WindowManager) context.getSystemService(
          Context.WINDOW_SERVICE);
      switch(wm.getDefaultDisplay().getRotation()) {
        case Surface.ROTATION_90:
          orientation = 90;
          break;
        case Surface.ROTATION_180:
          orientation = 180;
          break;
        case Surface.ROTATION_270:
          orientation = 270;
          break;
        case Surface.ROTATION_0:
        default:
          orientation = 0;
          break;
      }
    }
    return orientation;
  }

  @WebRTCJNITarget
  private native void ProvideCameraFrame(
      int width, int height,
      java.nio.ByteBuffer dataY, int strideY,
      java.nio.ByteBuffer dataU, int strideU,
      java.nio.ByteBuffer dataV, int strideV,
      int rotation, long timeStamp, long captureObject);

  //
  // CameraVideoCapturer.CameraEventsHandler interface
  //

  // Camera error handler - invoked when camera can not be opened
  // or any camera exception happens on camera thread.
  public void onCameraError(String errorDescription) {}

  // Called when camera is disconnected.
  public void onCameraDisconnected() {}

  // Invoked when camera stops receiving frames.
  public void onCameraFreezed(String errorDescription) {}

  // Callback invoked when camera is opening.
  public void onCameraOpening(String cameraName) {}

  // Callback invoked when first camera frame is available after camera is started.
  public void onFirstFrameAvailable() {}

  // Callback invoked when camera is closed.
  public void onCameraClosed() {}

  //
  // CapturerObserver interface
  //

  // Notify if the capturer have been started successfully or not.
  public void onCapturerStarted(boolean success) {
    capturerStartedSucceeded = success;
    capturerStarted.countDown();
  }

  // Notify that the capturer has been stopped.
  public void onCapturerStopped() {
    capturerStopped.countDown();
  }

  // Delivers a captured frame.
  public void onFrameCaptured(VideoFrame frame) {
    if (native_capturer == 0) {
      return;
    }

    I420Buffer i420Buffer = frame.getBuffer().toI420();
    if (i420Buffer == null) {
      return;
    }

    ProvideCameraFrame(i420Buffer.getWidth(), i420Buffer.getHeight(),
        i420Buffer.getDataY(), i420Buffer.getStrideY(),
        i420Buffer.getDataU(), i420Buffer.getStrideU(),
        i420Buffer.getDataV(), i420Buffer.getStrideV(),
        frame.getRotation(),
        frame.getTimestampNs() / 1000000, native_capturer);

    i420Buffer.release();
  }
}
