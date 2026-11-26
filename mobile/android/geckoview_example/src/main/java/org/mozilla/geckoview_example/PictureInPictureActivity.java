/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview_example;

import android.app.PictureInPictureParams;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.Build;
import android.os.Bundle;
import android.util.Rational;
import android.view.ViewGroup;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.OptIn;
import androidx.appcompat.app.AppCompatActivity;
import androidx.lifecycle.Lifecycle;
import java.lang.ref.WeakReference;
import org.mozilla.geckoview.ExperimentalGeckoViewApi;
import org.mozilla.geckoview.GeckoSession;
import org.mozilla.geckoview.GeckoView;

/**
 * Displays the session created for a picture-in-picture request in a native picture-in-picture
 * window, and on expanding returns to the tab the video came from.
 */
public class PictureInPictureActivity extends AppCompatActivity {
  private static final String LOGTAG = "PictureInPicture";
  private static final String ABOUT_BLANK = "about:blank";
  private static final int DEFAULT_ASPECT_WIDTH = 16;
  private static final int DEFAULT_ASPECT_HEIGHT = 9;

  private GeckoView mGeckoView;
  private @Nullable GeckoSession mGeckoSession;

  /** The tab whose video {@link #mGeckoSession} is displaying. */
  private @Nullable GeckoSession mSourceSession;

  private boolean mHasEnteredPip;
  private boolean mWasDismissed;
  private boolean mReturnOnResume;

  @Override
  protected void onCreate(final @Nullable Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);

    mGeckoView = new GeckoView(this);
    mGeckoView.setLayoutParams(
        new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    setContentView(mGeckoView);

    PipSessionHolder.sActiveActivity = new WeakReference<>(this);

    final GeckoSession session = PipSessionHolder.take();
    if (session == null) {
      DebugLog.d(LOGTAG, "No session to display, finishing");
      finish();
      return;
    }
    attachSession(session);
  }

  /**
   * The window is declared {@code singleTask}, so a request that starts it while it is already up
   * is delivered here instead of creating a second window.
   */
  @Override
  protected void onNewIntent(final Intent intent) {
    super.onNewIntent(intent);
    final GeckoSession session = PipSessionHolder.take();
    if (session != null) {
      adoptSession(session);
    }
  }

  /** Whether {@code session} is the tab whose video this window is currently displaying. */
  public boolean isShowingSource(final @NonNull GeckoSession session) {
    return mSourceSession == session;
  }

  /** Closes the window because Gecko asked to, so Gecko is not told about it again. */
  public void closeForGecko() {
    mSourceSession = null;
    finish();
  }

  /**
   * Displays {@code session} in a window that is already up, which is the case for a request made
   * while another video is already in picture-in-picture. The video being displayed so far, if any,
   * is dropped in favour of the new one, and its tab is told it left the window unless it is also
   * the tab the new video comes from.
   */
  public void adoptSession(final @NonNull GeckoSession session) {
    if (isFinishing()) {
      session.close();
      return;
    }
    if (mSourceSession == PipSessionHolder.sSourceSession) {
      mSourceSession = null;
    }
    releaseSession();
    attachSession(session);
  }

  @OptIn(markerClass = ExperimentalGeckoViewApi.class)
  private void attachSession(final @NonNull GeckoSession session) {
    mGeckoSession = session;
    mSourceSession = PipSessionHolder.sSourceSession;
    if (mSourceSession != null) {
      mSourceSession.getSessionPictureInPicture().setKeepActive(true);
    }
    mGeckoView.setSession(session);
    session.loadUri(ABOUT_BLANK);
  }

  /** Tells the source tab its video left the window, and tears down the window's copy of it. */
  @OptIn(markerClass = ExperimentalGeckoViewApi.class)
  private void releaseSession() {
    if (mSourceSession != null && mSourceSession.isOpen()) {
      mSourceSession.getSessionPictureInPicture().exit();
      PipSessionHolder.releaseSource(mSourceSession);
    }
    mSourceSession = null;
    if (mGeckoSession != null) {
      mGeckoView.releaseSession();
      mGeckoSession.close();
      mGeckoSession = null;
    }
  }

  @Override
  protected void onResume() {
    super.onResume();
    if (mReturnOnResume) {
      mReturnOnResume = false;
      returnToSource();
      finish();
      return;
    }
    if (!mHasEnteredPip) {
      mHasEnteredPip = true;
      if (!enterPictureInPictureMode(buildPipParams())) {
        DebugLog.d(LOGTAG, "Refused picture-in-picture mode, finishing");
        finish();
      }
    }
  }

  @Override
  public void onUserLeaveHint() {
    super.onUserLeaveHint();
    enterPictureInPictureMode(buildPipParams());
  }

  /**
   * Leaving picture-in-picture mode covers both the user dismissing the window and expanding it
   * back to full screen, and the two are only told apart by the lifecycle state: a dismissal stops
   * the activity, so it has already been taken down to {@link Lifecycle.State#CREATED} by the time
   * this runs, while an expansion keeps it started.
   */
  @Override
  public void onPictureInPictureModeChanged(
      final boolean isInPictureInPictureMode, final Configuration newConfig) {
    super.onPictureInPictureModeChanged(isInPictureInPictureMode, newConfig);
    if (isInPictureInPictureMode) {
      return;
    }
    mWasDismissed = getLifecycle().getCurrentState() == Lifecycle.State.CREATED;
    if (mWasDismissed) {
      finish();
      return;
    }
    mReturnOnResume = true;
  }

  @Override
  protected void onDestroy() {
    super.onDestroy();
    releaseSession();
    PipSessionHolder.sSession = null;
    PipSessionHolder.sSourceSession = null;
    PipSessionHolder.sActiveActivity = null;
  }

  /** Brings the browser back to the front, on the tab the video was playing in. */
  private void returnToSource() {
    PipSessionHolder.sReturnToSession = mSourceSession;
    startActivity(
        new Intent(this, GeckoViewActivity.class)
            .setAction(GeckoViewActivity.ACTION_RETURN_TO_PIP_SOURCE)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP));
  }

  private PictureInPictureParams buildPipParams() {
    final PictureInPictureParams.Builder builder =
        new PictureInPictureParams.Builder()
            .setAspectRatio(new Rational(DEFAULT_ASPECT_WIDTH, DEFAULT_ASPECT_HEIGHT));
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
      builder.setAutoEnterEnabled(true);
    }
    return builder.build();
  }
}
