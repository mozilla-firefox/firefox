/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Rect;
import android.os.Build;
import android.view.Surface;
import android.view.SurfaceControl;
import android.view.WindowInsets;
import androidx.annotation.AnyThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.UiThread;
import androidx.core.view.WindowInsetsCompat;
import org.mozilla.gecko.util.ThreadUtils;

/**
 * Applications use a GeckoDisplay instance to provide {@link GeckoSession} with a {@link Surface}
 * for displaying content. To ensure drawing only happens on a valid {@link Surface}, {@link
 * GeckoSession} will only use the provided {@link Surface} after {@link
 * #surfaceChanged(SurfaceInfo)} is called and before {@link #surfaceDestroyed()} returns.
 */
public class GeckoDisplay {
  private final GeckoSession mSession;

  /**
   * Protected constructor for GeckoDisplay.
   *
   * @param session The GeckoSession this display is associated with
   */
  protected GeckoDisplay(final GeckoSession session) {
    mSession = session;
  }

  /**
   * Interface that allows Gecko the request a new Surface from the application. An implementation
   * of this should be set on the {@link GeckoDisplay.SurfaceInfo} object passed to {@link
   * GeckoDisplay#surfaceChanged(SurfaceInfo)}, by using {@link
   * GeckoDisplay.SurfaceInfo.Builder#newSurfaceProvider(NewSurfaceProvider)}.
   */
  public interface NewSurfaceProvider {
    /**
     * Called by Gecko to request a new Surface from the application.
     *
     * <p>Occasionally the Surface provided to Gecko via {@link #surfaceChanged(SurfaceInfo)} is
     * invalid and Gecko is unable to render in to it. This function will be called in such
     * circumstances. It is the implementation's responsibility to ensure that {@link
     * #surfaceChanged(SurfaceInfo)} gets called soon afterwards with a new Surface, allowing Gecko
     * to resume rendering.
     *
     * <p>Failure to implement this function may result in Gecko either crashing or not rendering
     * correctly should it encounter an invalid Surface.
     */
    @UiThread
    void requestNewSurface();
  }

  /**
   * Wrapper class containing a Surface and associated information that the compositor should render
   * in to. Should be constructed using {@link SurfaceInfo.Builder}.
   */
  public static class SurfaceInfo {
    /* package */ final @NonNull Surface mSurface;
    /* package */ final @Nullable SurfaceControl mSurfaceControl;
    /* package */ final @Nullable NewSurfaceProvider mNewSurfaceProvider;
    /* package */ final int mLeft;
    /* package */ final int mTop;
    /* package */ final int mWidth;
    /* package */ final int mHeight;

    private SurfaceInfo(final @NonNull Builder builder) {
      mSurface = builder.mSurface;
      mSurfaceControl = builder.mSurfaceControl;
      mNewSurfaceProvider = builder.mNewSurfaceProvider;
      mLeft = builder.mLeft;
      mTop = builder.mTop;
      mWidth = builder.mWidth;
      mHeight = builder.mHeight;
    }

    /** Helper class for constructing a {@link SurfaceInfo} object. */
    public static class Builder {
      private Surface mSurface;
      private SurfaceControl mSurfaceControl;
      private NewSurfaceProvider mNewSurfaceProvider;
      private int mLeft;
      private int mTop;
      private int mWidth;
      private int mHeight;

      /**
       * Creates a new Builder and sets the new Surface.
       *
       * @param surface The new Surface.
       */
      public Builder(final @NonNull Surface surface) {
        mSurface = surface;
      }

      /**
       * Sets the SurfaceControl associated with the new Surface's SurfaceView.
       *
       * <p>This must be called when rendering in to a {@link android.view.SurfaceView} on SDK level
       * 29 or above. On earlier SDK levels, or when rendering in to something other than a
       * SurfaceView, this call can be omitted or the value can be null.
       *
       * @param surfaceControl The SurfaceControl associated with the new Surface's SurfaceView, or
       *     null.
       * @return The builder object
       */
      @UiThread
      public @NonNull Builder surfaceControl(final @Nullable SurfaceControl surfaceControl) {
        mSurfaceControl = surfaceControl;
        return this;
      }

      /**
       * Sets a NewSurfaceProvider from which Gecko can request a new Surface.
       *
       * <p>This allows Gecko to recover from situations where the current Surface is for whatever
       * reason invalid and Gecko is unable to render in to it. Failure to set this field correctly
       * may result in Gecko either crashing or not rendering correctly should it encounter an
       * invalid Surface.
       *
       * @param newSurfaceProvider A NewSurfaceProvider from which Gecko can request a new Surface.
       * @return The builder object
       */
      @UiThread
      public @NonNull Builder newSurfaceProvider(
          final @Nullable NewSurfaceProvider newSurfaceProvider) {
        mNewSurfaceProvider = newSurfaceProvider;
        return this;
      }

      /**
       * Sets the new compositor origin offset.
       *
       * @param left The compositor origin offset in the X axis. Can not be negative.
       * @param top The compositor origin offset in the Y axis. Can not be negative.
       * @return The builder object
       */
      @UiThread
      public @NonNull Builder offset(final int left, final int top) {
        mLeft = left;
        mTop = top;
        return this;
      }

      /**
       * Sets the new surface size.
       *
       * @param width New width of the Surface. Can not be negative.
       * @param height New height of the Surface. Can not be negative.
       * @return The builder object
       */
      @UiThread
      public @NonNull Builder size(final int width, final int height) {
        mWidth = width;
        mHeight = height;
        return this;
      }

      /**
       * Builds the {@link SurfaceInfo} object with the specified properties.
       *
       * @return The SurfaceInfo object
       */
      @UiThread
      public @NonNull SurfaceInfo build() {
        if ((mLeft < 0) || (mTop < 0)) {
          throw new IllegalArgumentException("Left and Top offsets can not be negative.");
        }

        return new SurfaceInfo(this);
      }
    }
  }

  /**
   * Sets a surface for the compositor render a surface.
   *
   * <p>Required call. The display's Surface has been created or changed. Must be called on the
   * application main thread. GeckoSession may block this call to ensure the Surface is valid while
   * resuming drawing.
   *
   * <p>If rendering in to a {@link android.view.SurfaceView} on SDK level 29 or above, please
   * ensure that the SurfaceControl field of the {@link SurfaceInfo} object is set.
   *
   * @param surfaceInfo Information about the new Surface.
   */
  @UiThread
  public void surfaceChanged(@NonNull final SurfaceInfo surfaceInfo) {
    ThreadUtils.assertOnUiThread();

    if (mSession.getDisplay() == this) {
      mSession.onSurfaceChanged(surfaceInfo);
    }
  }

  /**
   * Removes the current surface registered with the compositor.
   *
   * <p>Required call. The display's Surface has been destroyed. Must be called on the application
   * main thread. GeckoSession may block this call to ensure the Surface is valid while pausing
   * drawing.
   */
  @UiThread
  public void surfaceDestroyed() {
    ThreadUtils.assertOnUiThread();

    if (mSession.getDisplay() == this) {
      mSession.onSurfaceDestroyed();
    }
  }

  /**
   * Update the position of the surface on the screen.
   *
   * <p>Optional call. The display's coordinates on the screen has changed. Must be called on the
   * application main thread.
   *
   * @param left The X coordinate of the display on the screen, in screen pixels.
   * @param top The Y coordinate of the display on the screen, in screen pixels.
   */
  @UiThread
  public void screenOriginChanged(final int left, final int top) {
    ThreadUtils.assertOnUiThread();

    if (mSession.getDisplay() == this) {
      mSession.onScreenOriginChanged(left, top);
    }
  }

  /**
   * Update the safe area insets of the surface on the screen.
   *
   * @param left left margin of safe area
   * @param top top margin of safe area
   * @param right right margin of safe area
   * @param bottom bottom margin of safe area
   */
  @UiThread
  public void safeAreaInsetsChanged(
      final int top, final int right, final int bottom, final int left) {
    ThreadUtils.assertOnUiThread();

    if (mSession.getDisplay() == this) {
      mSession.onSafeAreaInsetsChanged(top, right, bottom, left);
    }
  }

  /**
   * Set the maximum height of the dynamic toolbar(s).
   *
   * <p>If the toolbar is dynamic, this function needs to be called with the maximum possible
   * toolbar height so that Gecko can make the ICB static even during the dynamic toolbar height is
   * being changed.
   *
   * @param height The maximum height of the dynamic toolbar(s).
   */
  @UiThread
  public void setDynamicToolbarMaxHeight(final int height) {
    ThreadUtils.assertOnUiThread();

    if (mSession != null) {
      mSession.setDynamicToolbarMaxHeight(height);
    }
  }

  /**
   * Update the amount of vertical space that is clipped or visibly obscured in the bottom portion
   * of the display. Tells gecko where to put bottom fixed elements so they are fully visible.
   *
   * <p>Optional call. The display's visible vertical space has changed. Must be called on the
   * application main thread.
   *
   * @param clippingHeight The height of the bottom clipped space in screen pixels.
   */
  @UiThread
  public void setVerticalClipping(final int clippingHeight) {
    ThreadUtils.assertOnUiThread();

    if (mSession != null) {
      mSession.setFixedBottomOffset(clippingHeight);
    }
  }

  /**
   * Return whether the display should be pinned on the screen.
   *
   * <p>When pinned, the display should not be moved on the screen due to animation, scrolling, etc.
   * A common reason for the display being pinned is when the user is dragging a selection caret
   * inside the display; normal user interaction would be disrupted in that case if the display was
   * moved on screen.
   *
   * @return True if display should be pinned on the screen.
   */
  @UiThread
  public boolean shouldPinOnScreen() {
    ThreadUtils.assertOnUiThread();
    return mSession.getDisplay() == this && mSession.shouldPinOnScreen();
  }

  /**
   * Request a {@link Bitmap} of the visible portion of the web page currently being rendered.
   *
   * <p>Returned {@link Bitmap} will have the same dimensions as the {@link Surface} the {@link
   * GeckoDisplay} is currently using.
   *
   * <p>If the {@link GeckoSession#isCompositorReady} is false the {@link GeckoResult} will complete
   * with an {@link IllegalStateException}.
   *
   * <p>This function must be called on the UI thread.
   *
   * @return A {@link GeckoResult} that completes with a {@link Bitmap} containing the pixels and
   *     size information of the currently visible rendered web page.
   */
  @UiThread
  public @NonNull GeckoResult<Bitmap> capturePixels() {
    return screenshot().capture();
  }

  /**
   * Request a {@link Bitmap} of the web page currently being rendered, including areas outside the
   * current view.
   *
   * <p>This function must be called on the UI thread.
   *
   * @return A {@link GeckoResult} that completes with a {@link Bitmap} containing the pixels and
   *     size information of the currently rendered web page.
   */
  @UiThread
  public @NonNull GeckoResult<Bitmap> captureFullPage() {
    return screenshot().captureFullPage();
  }

  /** Builder to construct screenshot requests. */
  public static final class ScreenshotBuilder {
    private static final int NONE = 0;
    private static final int SCALE = 1;
    private static final int ASPECT = 2;
    private static final int FULL = 3;
    private static final int RECYCLE = 4;

    private final GeckoSession mSession;
    private int mOffsetX;
    private int mOffsetY;
    private int mSrcWidth;
    private int mSrcHeight;
    private int mOutWidth;
    private int mOutHeight;
    private int mAspectPreservingWidth;
    private float mScale;
    private Bitmap mRecycle;
    private int mSizeType;

    /* package */ ScreenshotBuilder(final GeckoSession session) {
      this.mSizeType = NONE;
      this.mSession = session;
    }

    /**
     * The screenshot will be of a region instead of the entire screen / web page
     *
     * @param x Left most pixel of the source region (in screen pixels).
     * @param y Top most pixel of the source region (in screen pixels).
     * @param width Width of the source region in screen pixels
     * @param height Height of the source region in screen pixels
     * @return The builder
     */
    @AnyThread
    public @NonNull ScreenshotBuilder source(
        final int x, final int y, final int width, final int height) {
      mOffsetX = x;
      mOffsetY = y;
      mSrcWidth = width;
      mSrcHeight = height;
      return this;
    }

    /**
     * The screenshot will be of a region instead of the entire screen / web page
     *
     * @param source Region of the screen to capture in screen pixels
     * @return The builder
     */
    @AnyThread
    public @NonNull ScreenshotBuilder source(final @NonNull Rect source) {
      mOffsetX = source.left;
      mOffsetY = source.top;
      mSrcWidth = source.width();
      mSrcHeight = source.height();
      return this;
    }

    private void checkAndSetSizeType(final int sizeType) {
      if (mSizeType != NONE) {
        throw new IllegalStateException("Size has already been set.");
      }
      mSizeType = sizeType;
    }

    /**
     * The width of the bitmap to create when taking the screenshot. The height will be calculated
     * to match the aspect ratio of the source as closely as possible. The source screenshot will be
     * scaled into the resulting Bitmap.
     *
     * @param width of the result Bitmap in screen pixels.
     * @return The builder
     * @throws IllegalStateException if the size has already been set in some other way.
     */
    @AnyThread
    public @NonNull ScreenshotBuilder aspectPreservingSize(final int width) {
      checkAndSetSizeType(ASPECT);
      mAspectPreservingWidth = width;
      return this;
    }

    /**
     * The scale of the bitmap relative to the source. The height and width of the output bitmap
     * will be within one pixel of this multiple of the source dimensions. The source screenshot
     * will be scaled into the resulting Bitmap.
     *
     * @param scale of the result Bitmap relative to the source.
     * @return The builder
     * @throws IllegalStateException if the size has already been set in some other way.
     */
    @AnyThread
    public @NonNull ScreenshotBuilder scale(final float scale) {
      checkAndSetSizeType(SCALE);
      mScale = scale;
      return this;
    }

    /**
     * Size of the bitmap to create when taking the screenshot. The source screenshot will be scaled
     * into the resulting Bitmap
     *
     * @param width of the result Bitmap in screen pixels.
     * @param height of the result Bitmap in screen pixels.
     * @return The builder
     * @throws IllegalStateException if the size has already been set in some other way.
     */
    @AnyThread
    public @NonNull ScreenshotBuilder size(final int width, final int height) {
      checkAndSetSizeType(FULL);
      mOutWidth = width;
      mOutHeight = height;
      return this;
    }

    /**
     * Instead of creating a new Bitmap for the result, the builder will use the passed Bitmap.
     *
     * @param bitmap The Bitmap to use in the result.
     * @return The builder.
     * @throws IllegalStateException if the size has already been set in some other way.
     */
    @AnyThread
    public @NonNull ScreenshotBuilder bitmap(final @Nullable Bitmap bitmap) {
      checkAndSetSizeType(RECYCLE);
      mRecycle = bitmap;
      return this;
    }

    /**
     * Request a {@link Bitmap} of the requested portion of the web page currently being rendered
     * using any parameters specified with the builder.
     *
     * <p>This function must be called on the UI thread.
     *
     * @return A {@link GeckoResult} that completes with a {@link Bitmap} containing the pixels and
     *     size information of the requested portion of the visible web page, or returns a failure
     *     {@link GeckoResult} including the reason why in an {@link Exception}
     */
    @UiThread
    public @NonNull GeckoResult<Bitmap> capture() {
      ThreadUtils.assertOnUiThread();
      if (!mSession.isCompositorReady()) {
        return GeckoResult.fromException(
            new IllegalStateException("Compositor must be ready before pixels can be captured"));
      }

      final GeckoResult<Bitmap> result = new GeckoResult<>();
      final Bitmap target;
      final Rect rect = new Rect();

      if (mSrcWidth == 0 || mSrcHeight == 0) {
        // Source is unset or invalid, use defaults.
        mSession.getSurfaceBounds(rect);
        mSrcWidth = rect.width();
        mSrcHeight = rect.height();
      }

      switch (mSizeType) {
        case NONE:
          mOutWidth = mSrcWidth;
          mOutHeight = mSrcHeight;
          break;
        case SCALE:
          mSession.getSurfaceBounds(rect);
          mOutWidth = (int) (rect.width() * mScale);
          mOutHeight = (int) (rect.height() * mScale);
          break;
        case ASPECT:
          mSession.getSurfaceBounds(rect);
          mOutWidth = mAspectPreservingWidth;
          mOutHeight = (int) (rect.height() * (mAspectPreservingWidth / (double) rect.width()));
          break;
        case RECYCLE:
          mOutWidth = mRecycle.getWidth();
          mOutHeight = mRecycle.getHeight();
          break;
          // case FULL does not need to be handled, as width and height are already set.
      }

      if (mRecycle == null) {
        try {
          target = Bitmap.createBitmap(mOutWidth, mOutHeight, Bitmap.Config.ARGB_8888);
        } catch (final Throwable e) {
          if (e instanceof NullPointerException || e instanceof OutOfMemoryError) {
            return GeckoResult.fromException(
                new OutOfMemoryError("Not enough memory to allocate for bitmap"));
          }
          return GeckoResult.fromException(new Throwable("Failed to create bitmap", e));
        }
      } else {
        target = mRecycle;
      }

      mSession.mCompositor.requestScreenPixels(
          result, target, mOffsetX, mOffsetY, mSrcWidth, mSrcHeight, mOutWidth, mOutHeight);

      return result;
    }

    /**
     * Request a {@link Bitmap} of the full web page currently being rendered using any parameters
     * specified with the builder.
     *
     * <p>This function must be called on the UI thread.
     *
     * @return A {@link GeckoResult} that completes with a {@link Bitmap} containing the pixels and
     *     size information of the full web page, or returns a failure {@link GeckoResult} including
     *     the reason why in an {@link Exception}
     */
    @UiThread
    public @NonNull GeckoResult<Bitmap> captureFullPage() {
      ThreadUtils.assertOnUiThread();
      if (!mSession.isOpen()) {
        return GeckoResult.fromException(
            new IllegalStateException("Session must be open before a screenshot can be captured"));
      }

      final GeckoResult<GeckoSession.Window.ContentMetrics> metricsResult = new GeckoResult<>();
      final GeckoSession.Window.ContentMetrics metrics = new GeckoSession.Window.ContentMetrics();
      mSession.mWindow.requestContentMetrics(metricsResult, metrics);

      return metricsResult.then(
          contentMetrics -> {
            final float dpr = contentMetrics.devicePixelRatio;

            final int cssOffsetX;
            final int cssOffsetY;
            final int cssWidth;
            final int cssHeight;
            final int srcWidth;
            final int srcHeight;

            if (mSrcWidth == 0 || mSrcHeight == 0) {
              cssOffsetX = 0;
              cssOffsetY = 0;
              cssWidth = contentMetrics.width;
              cssHeight = contentMetrics.height;
              srcWidth = (int) (contentMetrics.width * dpr);
              srcHeight = (int) (contentMetrics.height * dpr);
            } else {
              cssOffsetX = (int) (mOffsetX / dpr);
              cssOffsetY = (int) (mOffsetY / dpr);
              cssWidth = (int) (mSrcWidth / dpr);
              cssHeight = (int) (mSrcHeight / dpr);
              srcWidth = mSrcWidth;
              srcHeight = mSrcHeight;
            }

            int outWidth = mOutWidth;
            int outHeight = mOutHeight;
            float effectiveRenderingScale = Float.NaN;

            switch (mSizeType) {
              case NONE:
                outWidth = srcWidth;
                outHeight = srcHeight;
                effectiveRenderingScale = dpr;
                break;
              case SCALE:
                outWidth = (int) (srcWidth * mScale);
                outHeight = (int) (srcHeight * mScale);
                break;
              case ASPECT:
                outWidth = mAspectPreservingWidth;
                outHeight = (int) (srcHeight * (mAspectPreservingWidth / (double) srcWidth));
                break;
              case RECYCLE:
                if (mRecycle == null) {
                  return GeckoResult.fromException(
                      new IllegalStateException("Bitmap to recycle is null"));
                }
                outWidth = mRecycle.getWidth();
                outHeight = mRecycle.getHeight();
                break;
                // case FULL does not need to be handled, as width and height are already set.
            }

            if (outWidth <= 0 || outHeight <= 0 || cssWidth <= 0 || cssHeight <= 0) {
              return GeckoResult.fromException(new IllegalStateException("Invalid dimensions"));
            }

            if (Float.isNaN(effectiveRenderingScale)) {
              // Use the smaller axis ratio so a RECYCLE/FULL target with a
              // different aspect ratio than the content is scaled to fit
              // rather than clipped.
              effectiveRenderingScale =
                  Math.min((float) outWidth / cssWidth, (float) outHeight / cssHeight);
            }

            final Bitmap target;
            if (mRecycle == null) {
              try {
                target = Bitmap.createBitmap(outWidth, outHeight, Bitmap.Config.ARGB_8888);
              } catch (final Throwable e) {
                if (e instanceof NullPointerException || e instanceof OutOfMemoryError) {
                  return GeckoResult.fromException(
                      new OutOfMemoryError("Not enough memory to allocate for bitmap"));
                }
                return GeckoResult.fromException(new Throwable("Failed to create bitmap", e));
              }
            } else {
              target = mRecycle;
            }

            // Pre-fill with the opaque white background so device pixels not
            // covered by a tile (edge rounding, or a skipped non-critical tile)
            // show background instead of transparent/stale pixels
            target.eraseColor(Color.WHITE);

            if (mSession.mWindow == null) {
              return GeckoResult.fromException(
                  new IllegalStateException("Session closed before screenshot could be taken"));
            }

            final GeckoResult<Bitmap> result = new GeckoResult<>();
            mSession.mWindow.requestFullScreenshot(
                result,
                target,
                cssOffsetX,
                cssOffsetY,
                cssWidth,
                cssHeight,
                effectiveRenderingScale);
            return result;
          });
    }
  }

  /**
   * Creates a new screenshot builder.
   *
   * @return The new {@link ScreenshotBuilder}
   */
  @UiThread
  public @NonNull ScreenshotBuilder screenshot() {
    return new ScreenshotBuilder(mSession);
  }

  private static int getKeyboardHeight(@NonNull final WindowInsetsCompat insetsCompat) {
    if (Build.VERSION.SDK_INT >= 30) {
      final WindowInsets insets = insetsCompat.toWindowInsets();
      if (insets == null) {
        return 0;
      }

      // https://developer.android.com/reference/android/view/WindowInsets.Type#ime()
      final int imeHeight = insets.getInsets(WindowInsets.Type.ime()).bottom;
      if (imeHeight == 0) {
        return 0;
      }
      return imeHeight - insets.getInsets(WindowInsets.Type.navigationBars()).bottom;
    }

    if (Build.VERSION.SDK_INT >= 29) {
      return insetsCompat.getInsets(WindowInsetsCompat.Type.ime()).bottom;
    }

    return getSystemWindowInsetsBottom(insetsCompat);
  }

  @SuppressWarnings("deprecation")
  private static int getSystemWindowInsetsBottom(@NonNull final WindowInsetsCompat insetsCompat) {
    return insetsCompat.getSystemWindowInsets().bottom;
  }

  /**
   * Called when the root window insets changed.
   *
   * @param insets the WindowInsetsCompat
   */
  @UiThread
  public void windowInsetsChanged(@NonNull final WindowInsetsCompat insets) {
    ThreadUtils.assertOnUiThread();

    final int keyboardHeight = getKeyboardHeight(insets);

    if (mSession != null) {
      mSession.onKeyboardHeight(keyboardHeight);
    }
  }
}
