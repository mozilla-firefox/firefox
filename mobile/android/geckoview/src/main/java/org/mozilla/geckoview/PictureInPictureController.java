/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import androidx.annotation.NonNull;
import androidx.annotation.UiThread;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ThreadUtils;

/**
 * Picture-in-picture clones a playing video into a window the application owns and hosts, so Gecko
 * can only ask for one and be told when it is gone.
 */
@ExperimentalGeckoViewApi
public class PictureInPictureController {
  /** Public constructor for PictureInPictureController. */
  public PictureInPictureController() {}

  /**
   * Session picture-in-picture drives the picture-in-picture window for the video a single session
   * is playing.
   */
  public static class SessionPictureInPicture {
    private static final String REQUEST_EVENT = "GeckoView:PictureInPicture:Request";
    private static final String EXIT_EVENT = "GeckoView:PictureInPicture:Exit";
    private static final String LAUNCH_EVENT = "GeckoView:PictureInPicture:Launch";
    private static final String CLOSE_EVENT = "GeckoView:PictureInPicture:Close";

    private final GeckoSession mSession;
    private final Handler mHandler;

    private enum KeepActiveState {
      NOT_KEEPING_ACTIVE,
      KEEPING_ACTIVE,
      KEEPING_ACTIVE_WITH_DEACTIVATION_DEFERRED,
    }

    /** UI thread only. */
    private KeepActiveState mKeepActiveState = KeepActiveState.NOT_KEEPING_ACTIVE;

    /* package */ SessionPictureInPicture(final @NonNull GeckoSession session) {
      mSession = session;
      mHandler = new Handler(session);
    }

    /* package */ @NonNull
    Handler getHandler() {
      return mHandler;
    }

    /**
     * Requests a picture-in-picture window for the video this session last started playing.
     *
     * @return A GeckoResult that completes once the picture-in-picture window is showing the video,
     *     or completes exceptionally if it could not be set up. It completes with no window having
     *     been opened if the session is not playing a video.
     */
    @HandlerThread
    public @NonNull GeckoResult<Void> request() {
      ThreadUtils.assertOnHandlerThread();
      return mSession.getEventDispatcher().queryVoid(REQUEST_EVENT);
    }

    /**
     * Notifies Gecko that the picture-in-picture window showing a video from this session has been
     * closed by the application. Must be called when the application tears the window down itself,
     * so that state held for the video can be released.
     *
     * @return A GeckoResult that completes once that state has been released.
     */
    @UiThread
    public @NonNull GeckoResult<Void> exit() {
      ThreadUtils.assertOnUiThread();
      return mSession.getEventDispatcher().queryVoid(EXIT_EVENT);
    }

    /**
     * Sets whether the {@link GeckoView} showing this session leaves it active when it stops
     * showing it. An inactive page is hidden, and pages commonly pause their video when hidden, so
     * set this while a picture-in-picture window is showing one of this session's videos.
     *
     * <p>Once the window is gone, clear it. If this returns {@code true}, the view skipped
     * deactivating the session and will not do it later, so call {@link GeckoSession#setActive}
     * with {@code false}.
     *
     * @param keepActive Whether to keep this session active.
     * @return When clearing, whether a {@link GeckoView} stopped showing this session while it was
     *     kept active and none has shown it since. Only meaningful when clearing.
     */
    @UiThread
    public boolean setKeepActive(final boolean keepActive) {
      ThreadUtils.assertOnUiThread();
      final boolean wasDeactivationDeferred =
          mKeepActiveState == KeepActiveState.KEEPING_ACTIVE_WITH_DEACTIVATION_DEFERRED;
      if (!keepActive) {
        mKeepActiveState = KeepActiveState.NOT_KEEPING_ACTIVE;
      } else if (mKeepActiveState == KeepActiveState.NOT_KEEPING_ACTIVE) {
        mKeepActiveState = KeepActiveState.KEEPING_ACTIVE;
      }
      return wasDeactivationDeferred;
    }

    /* package */ boolean keepsActive() {
      return mKeepActiveState != KeepActiveState.NOT_KEEPING_ACTIVE;
    }

    /* package */ void setDeactivationDeferred(final boolean deferred) {
      if (mKeepActiveState != KeepActiveState.NOT_KEEPING_ACTIVE) {
        mKeepActiveState =
            deferred
                ? KeepActiveState.KEEPING_ACTIVE_WITH_DEACTIVATION_DEFERRED
                : KeepActiveState.KEEPING_ACTIVE;
      }
    }

    /** Delegate for opening and closing picture-in-picture windows on Gecko's behalf. */
    public interface Delegate {
      /**
       * Called when the page loaded in {@code session} requests picture-in-picture.
       *
       * <p>If the window is already showing a video from another session, call {@link #exit} on
       * that session before replacing it.
       *
       * @param session The session the video belongs to.
       * @param placementToken Token that opens the picture-in-picture session where it can clone
       *     the video, passed to {@link GeckoSessionSettings.Builder#pictureInPictureToken}. It
       *     opens a single session and is only good until this request is answered or superseded.
       *     Might be empty if Gecko failed to generate one, in which case no window should be
       *     opened.
       */
      @UiThread
      default void onEnterPictureInPicture(
          final @NonNull GeckoSession session, final @NonNull String placementToken) {}

      /**
       * Called when the picture-in-picture window created for {@code session} should be closed.
       *
       * @param session The session the picture-in-picture window was created for.
       */
      @UiThread
      default void onExitPictureInPicture(final @NonNull GeckoSession session) {}
    }

    /* package */ static class Handler extends GeckoSessionHandler<Delegate> {

      private final GeckoSession mSession;

      private Handler(final GeckoSession session) {
        super("GeckoViewPictureInPicture", session, new String[] {LAUNCH_EVENT, CLOSE_EVENT});
        mSession = session;
      }

      @Override
      public void handleMessage(
          final Delegate delegate,
          final String event,
          final GeckoBundle message,
          final EventCallback callback) {
        if (LAUNCH_EVENT.equals(event)) {
          delegate.onEnterPictureInPicture(mSession, message.getString("placementToken", ""));
        } else if (CLOSE_EVENT.equals(event)) {
          delegate.onExitPictureInPicture(mSession);
        }
      }
    }
  }
}
