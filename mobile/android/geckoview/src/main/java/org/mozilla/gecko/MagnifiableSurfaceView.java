/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/**
 * A {@link android.view.SurfaceView} which allows a {@link android.widget.Magnifier} widget to
 * magnify a custom {@link android.view.Surface} rather than the SurfaceView's default Surface.
 */
public class MagnifiableSurfaceView extends SurfaceView {
  private static final String LOGTAG = "MagnifiableSurfaceView";

  private SurfaceHolderWrapper mHolder;

  public MagnifiableSurfaceView(final Context context) {
    super(context);
  }

  @Override
  public SurfaceHolder getHolder() {
    if (mHolder != null) {
      // Only return our custom holder if we are being called from the Magnifier class.
      // Throwable.getStackTrace() is faster than Thread.getStackTrace(), but still has a cost,
      // hence why we only check the caller if we have set an override Surface.
      final StackTraceElement[] stackTrace = new Throwable().getStackTrace();
      if (stackTrace.length >= 2
          && stackTrace[1].getClassName().equals("android.widget.Magnifier")) {
        return mHolder;
      }
    }
    return super.getHolder();
  }

  /**
   * Sets the Surface that should be magnified by a Magnifier widget.
   *
   * <p>This should be set immediately before calling {@link android.widget.Magnifier#show()} or
   * {@link android.widget.Magnifier#update()}, and unset immediately afterwards.
   *
   * @param surface The Surface to be magnified. If null, the SurfaceView's default Surface will be
   *     used.
   */
  public void setMagnifierSurface(final Surface surface) {
    if (surface != null) {
      mHolder = new SurfaceHolderWrapper(getHolder(), surface);
    } else {
      mHolder = null;
    }
  }

  /**
   * A {@link android.view.SurfaceHolder} implementation that simply forwards all methods to a
   * provided SurfaceHolder instance, except for getSurface() which returns a custom Surface.
   */
  private class SurfaceHolderWrapper implements SurfaceHolder {
    private final SurfaceHolder mHolder;
    private final Surface mSurface;

    public SurfaceHolderWrapper(final SurfaceHolder holder, final Surface surface) {
      mHolder = holder;
      mSurface = surface;
    }

    @Override
    public void addCallback(final Callback callback) {
      mHolder.addCallback(callback);
    }

    @Override
    public void removeCallback(final Callback callback) {
      mHolder.removeCallback(callback);
    }

    @Override
    public boolean isCreating() {
      return mHolder.isCreating();
    }

    @Override
    @SuppressWarnings("deprecation")
    public void setType(final int type) {
      mHolder.setType(type);
    }

    @Override
    public void setFixedSize(final int width, final int height) {
      mHolder.setFixedSize(width, height);
    }

    @Override
    public void setSizeFromLayout() {
      mHolder.setSizeFromLayout();
    }

    @Override
    public void setFormat(final int format) {
      mHolder.setFormat(format);
    }

    @Override
    public void setKeepScreenOn(final boolean screenOn) {
      mHolder.setKeepScreenOn(screenOn);
    }

    @Override
    public Canvas lockCanvas() {
      return mHolder.lockCanvas();
    }

    @Override
    public Canvas lockCanvas(final Rect dirty) {
      return mHolder.lockCanvas(dirty);
    }

    @Override
    public void unlockCanvasAndPost(final Canvas canvas) {
      mHolder.unlockCanvasAndPost(canvas);
    }

    @Override
    public Rect getSurfaceFrame() {
      return mHolder.getSurfaceFrame();
    }

    @Override
    public Surface getSurface() {
      return mSurface;
    }
  }
}
