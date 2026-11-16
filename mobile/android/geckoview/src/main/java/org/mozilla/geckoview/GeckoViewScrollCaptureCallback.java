/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.graphics.Rect;
import android.os.Build;
import android.os.CancellationSignal;
import android.util.Log;
import android.view.ScrollCaptureCallback;
import android.view.ScrollCaptureSession;
import android.view.Surface;
import androidx.annotation.NonNull;
import androidx.annotation.RequiresApi;
import androidx.annotation.VisibleForTesting;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;
import java.util.function.Supplier;
import org.mozilla.gecko.annotation.WrapForJNI;

@RequiresApi(Build.VERSION_CODES.S)
/*package*/ class GeckoViewScrollCaptureCallback implements ScrollCaptureCallback {
  private static final String LOGTAG = "GeckoViewScrollCapture";

  // Outcome values for the geckoview.scroll_capture_tile_result labeled counter.
  // Exposed to C++ via @WrapForJNI: GeckoViewSupport::RecordScrollCaptureTileResult
  // switches on these constants, so the two sides cannot drift apart.
  @WrapForJNI static final int TILE_RESULT_SUCCESS = 0;
  @WrapForJNI static final int TILE_RESULT_CANCELED_BEFORE = 1;
  @WrapForJNI static final int TILE_RESULT_CANCELED_INFLIGHT = 2;
  @WrapForJNI static final int TILE_RESULT_NO_METRICS = 3;
  @WrapForJNI static final int TILE_RESULT_INVALID_SURFACE = 4;
  @WrapForJNI static final int TILE_RESULT_OUTSIDE_CONTENT = 5;
  @WrapForJNI static final int TILE_RESULT_EMPTY_RECT = 6;
  @WrapForJNI static final int TILE_RESULT_PAINT_ERROR = 7;

  // Per-capture verdicts for the geckoview.scroll_capture_outcome labeled
  // counter, recorded once per capture in onScrollCaptureEnd. Exposed to C++ via
  // @WrapForJNI: GeckoViewSupport::RecordScrollCaptureOutcome switches on these.
  @WrapForJNI static final int OUTCOME_COMPLETE = 0;
  @WrapForJNI static final int OUTCOME_PARTIAL = 1;
  @WrapForJNI static final int OUTCOME_FAILED = 2;
  @WrapForJNI static final int OUTCOME_CANCELED = 3;

  private final Supplier<GeckoSession> mSessionSupplier;
  private GeckoSession.Window.ContentMetrics mContentMetrics;

  // Tile outcomes tallied across the current capture (onScrollCaptureStart to
  // onScrollCaptureEnd), used to derive the per-capture verdict.
  private int mCaptureTiles;
  private int mCaptureSuccesses;
  private int mCaptureFailures;
  private int mCaptureCancellations;
  private int mCapturePending;

  // Bumped each capture so a native paint result that lands after its capture
  // ended (or after a new one started) does not mutate the live tally.
  private int mCaptureGeneration;

  @VisibleForTesting /* package */ int mLastCaptureOutcome = -1;

  /* package */ GeckoViewScrollCaptureCallback(
      @NonNull final Supplier<GeckoSession> sessionSupplier) {
    mSessionSupplier = sessionSupplier;
  }

  private void recordTileResult(
      final GeckoSession session, final int generation, final int outcome) {
    // The per-tile counter is cumulative, so record it regardless of generation.
    if (session != null && session.mWindow != null) {
      session.mWindow.recordScrollCaptureTileResult(outcome);
    }
    // A result from a capture that has since ended (or been superseded) must not
    // mutate the current capture's tally.
    if (generation != mCaptureGeneration) {
      return;
    }
    mCaptureTiles++;
    switch (outcome) {
      case TILE_RESULT_SUCCESS:
        mCaptureSuccesses++;
        break;
      case TILE_RESULT_CANCELED_BEFORE:
      case TILE_RESULT_CANCELED_INFLIGHT:
        mCaptureCancellations++;
        break;
      case TILE_RESULT_NO_METRICS:
      case TILE_RESULT_INVALID_SURFACE:
      case TILE_RESULT_PAINT_ERROR:
        mCaptureFailures++;
        break;
      default:
        // TILE_RESULT_OUTSIDE_CONTENT and TILE_RESULT_EMPTY_RECT are benign boundary
        // declines; they count toward the tile total but not the verdict.
        break;
    }
  }

  private void resetCaptureTally() {
    mCaptureTiles = 0;
    mCaptureSuccesses = 0;
    mCaptureFailures = 0;
    mCaptureCancellations = 0;
    mCapturePending = 0;
    mCaptureGeneration++;
    mLastCaptureOutcome = -1;
  }

  private void recordCaptureOutcome() {
    if (mCaptureTiles == 0 && mCapturePending == 0) {
      return;
    }
    final int outcome;
    if (mCapturePending > 0 || mCaptureCancellations > 0) {
      // Ending with a paint still in flight means the capture was abandoned
      // before it could finish (the in-flight paint cannot be aborted).
      outcome = OUTCOME_CANCELED;
    } else if (mCaptureSuccesses > 0) {
      outcome = (mCaptureFailures > 0) ? OUTCOME_PARTIAL : OUTCOME_COMPLETE;
    } else {
      outcome = OUTCOME_FAILED;
    }
    mLastCaptureOutcome = outcome;
    final GeckoSession session = mSessionSupplier.get();
    if (session != null && session.mWindow != null) {
      session.mWindow.recordScrollCaptureOutcome(outcome);
    }
  }

  @Override
  public void onScrollCaptureEnd(@NonNull final Runnable runnable) {
    recordCaptureOutcome();
    mContentMetrics = null;
    runnable.run();
  }

  @Override
  public void onScrollCaptureImageRequest(
      @NonNull final ScrollCaptureSession scrollCaptureSession,
      @NonNull final CancellationSignal cancellationSignal,
      @NonNull final Rect captureArea,
      @NonNull final Consumer<Rect> onComplete) {
    final GeckoSession session = mSessionSupplier.get();
    final int generation = mCaptureGeneration;
    final Surface surface = scrollCaptureSession.getSurface();
    final float dpr = (mContentMetrics == null) ? 0 : mContentMetrics.devicePixelRatio;
    final boolean canceled = cancellationSignal.isCanceled();
    if (canceled
        || mContentMetrics == null
        || session == null
        || session.mWindow == null
        || surface == null
        || !surface.isValid()
        || dpr <= 0) {
      Log.w(
          LOGTAG,
          "onScrollCaptureImageRequest declined: canceled="
              + canceled
              + " metrics="
              + (mContentMetrics != null)
              + " session="
              + (session != null)
              + " window="
              + (session != null && session.mWindow != null)
              + " surfaceValid="
              + (surface != null && surface.isValid())
              + " dpr="
              + dpr);
      final int outcome =
          canceled
              ? TILE_RESULT_CANCELED_BEFORE
              : (dpr <= 0) ? TILE_RESULT_NO_METRICS : TILE_RESULT_INVALID_SURFACE;
      recordTileResult(session, generation, outcome);
      onComplete.accept(new Rect());
      return;
    }

    final int contentWidth = Math.round(mContentMetrics.width * dpr);
    final int contentHeight = Math.round(mContentMetrics.height * dpr);
    final int scrollX = Math.round(mContentMetrics.scrollX * dpr);
    final int scrollY = Math.round(mContentMetrics.scrollY * dpr);

    final Rect content = new Rect(captureArea);
    content.offset(scrollX, scrollY);
    if (!content.intersect(0, 0, contentWidth, contentHeight)) {
      Log.w(
          LOGTAG,
          "onScrollCaptureImageRequest declined: captureArea "
              + captureArea
              + " maps outside content "
              + contentWidth
              + "x"
              + contentHeight
              + " at scroll ("
              + scrollX
              + ","
              + scrollY
              + ")");
      recordTileResult(session, generation, TILE_RESULT_OUTSIDE_CONTENT);
      onComplete.accept(new Rect());
      return;
    }

    // Snap the content-absolute rect out to the CSS pixel grid so the painted region has integer
    // CSS bounds. The compositor scales CSS->device by truncation (surfaceSize = cssSize * scale as
    // an int), so derive the captured rect the same way to match the painted pixels exactly.
    final int cssLeft = Math.max(0, (int) Math.floor(content.left / dpr));
    final int cssTop = Math.max(0, (int) Math.floor(content.top / dpr));
    final int cssRight = Math.min(mContentMetrics.width, (int) Math.ceil(content.right / dpr));
    final int cssBottom = Math.min(mContentMetrics.height, (int) Math.ceil(content.bottom / dpr));
    final int cssWidth = cssRight - cssLeft;
    final int cssHeight = cssBottom - cssTop;
    if (cssWidth <= 0 || cssHeight <= 0) {
      Log.w(
          LOGTAG,
          "onScrollCaptureImageRequest declined: empty CSS rect " + cssWidth + "x" + cssHeight);
      recordTileResult(session, generation, TILE_RESULT_EMPTY_RECT);
      onComplete.accept(new Rect());
      return;
    }

    // The native blit clamps the painted snapshot to the Surface buffer, which is sized to the
    // scroll bounds. Clamp captured to the same bounds so it never reports more than the buffer
    // holds.
    final Rect scrollBounds = scrollCaptureSession.getScrollBounds();
    final int capturedLeft = (int) (cssLeft * dpr) - scrollX;
    final int capturedTop = (int) (cssTop * dpr) - scrollY;
    final int capturedWidth = Math.min((int) (cssWidth * dpr), scrollBounds.width());
    final int capturedHeight = Math.min((int) (cssHeight * dpr), scrollBounds.height());
    final Rect captured =
        new Rect(
            capturedLeft, capturedTop, capturedLeft + capturedWidth, capturedTop + capturedHeight);

    // The in-flight native paint cannot be aborted,
    // but if the request is canceled we discard its result.
    final AtomicBoolean cancelled = new AtomicBoolean(false);
    cancellationSignal.setOnCancelListener(() -> cancelled.set(true));

    mCapturePending++;
    final GeckoResult<Void> result = new GeckoResult<>();
    session.mWindow.requestRegionToSurface(
        result, surface, cssLeft, cssTop, cssWidth, cssHeight, dpr);
    result.accept(
        v -> {
          if (generation == mCaptureGeneration) {
            mCapturePending--;
          }
          final boolean wasCancelled = cancelled.get();
          recordTileResult(
              session,
              generation,
              wasCancelled ? TILE_RESULT_CANCELED_INFLIGHT : TILE_RESULT_SUCCESS);
          onComplete.accept(wasCancelled ? new Rect() : captured);
        },
        error -> {
          if (generation == mCaptureGeneration) {
            mCapturePending--;
          }
          recordTileResult(session, generation, TILE_RESULT_PAINT_ERROR);
          onComplete.accept(new Rect());
        });
  }

  @Override
  public void onScrollCaptureSearch(
      @NonNull final CancellationSignal cancellationSignal,
      @NonNull final Consumer<Rect> consumer) {
    final GeckoSession session = mSessionSupplier.get();

    final Rect bounds = new Rect();
    if (session != null) {
      session.getSurfaceBounds(bounds);
    }
    consumer.accept(bounds);
  }

  @Override
  public void onScrollCaptureStart(
      @NonNull final ScrollCaptureSession scrollCaptureSession,
      @NonNull final CancellationSignal cancellationSignal,
      @NonNull final Runnable onReady) {
    resetCaptureTally();
    final GeckoSession session = mSessionSupplier.get();
    if (cancellationSignal.isCanceled() || session == null || session.mWindow == null) {
      onReady.run();
      return;
    }

    final GeckoResult<GeckoSession.Window.ContentMetrics> result = new GeckoResult<>();
    session.mWindow.requestContentMetrics(result, new GeckoSession.Window.ContentMetrics());
    result.accept(
        metrics -> {
          mContentMetrics = metrics;
          onReady.run();
        },
        error -> {
          mContentMetrics = null;
          onReady.run();
        });
  }
}
