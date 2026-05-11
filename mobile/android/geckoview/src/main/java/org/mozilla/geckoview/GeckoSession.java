/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import static org.mozilla.geckoview.GeckoSession.GeckoPrintException.ERROR_NO_PRINT_DELEGATE;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Matrix;
import android.graphics.Point;
import android.graphics.PointF;
import android.graphics.Rect;
import android.graphics.RectF;
import android.net.Uri;
import android.os.Binder;
import android.os.Build;
import android.os.IInterface;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import android.text.TextUtils;
import android.util.Base64;
import android.util.Log;
import android.view.PointerIcon;
import android.view.Surface;
import android.view.View;
import android.view.ViewStructure;
import android.view.WindowManager;
import android.view.inputmethod.CursorAnchorInfo;
import android.view.inputmethod.ExtractedText;
import android.view.inputmethod.ExtractedTextRequest;
import android.widget.Magnifier;
import androidx.annotation.AnyThread;
import androidx.annotation.IntDef;
import androidx.annotation.LongDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.OptIn;
import androidx.annotation.RequiresApi;
import androidx.annotation.StringDef;
import androidx.annotation.UiThread;
import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.ref.WeakReference;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.security.Principal;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.AbstractSequentialList;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.ListIterator;
import java.util.Locale;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import org.json.JSONException;
import org.json.JSONObject;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.GeckoDragAndDrop;
import org.mozilla.gecko.GeckoThread;
import org.mozilla.gecko.HapticFeedbackController;
import org.mozilla.gecko.IGeckoEditableParent;
import org.mozilla.gecko.MagnifiableSurfaceView;
import org.mozilla.gecko.NativeQueue;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.gecko.util.BundleEventListener;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.IntentUtils;
import org.mozilla.gecko.util.ThreadUtils;
import org.mozilla.geckoview.GeckoDisplay.SurfaceInfo;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.IdentityCredential.AccountSelectorPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.IdentityCredential.PrivacyPolicyPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.IdentityCredential.ProviderSelectorPrompt;

/**
 * A session for interacting with web content in Gecko.
 *
 * <p>GeckoSession represents a single tab or browser window and provides APIs for loading content,
 * handling navigation, managing permissions, and interacting with web content through various
 * delegate interfaces.
 */
public class GeckoSession {
  private static final String LOGTAG = "GeckoSession";
  private static final boolean DEBUG = false;

  // Type of changes given to onWindowChanged.
  // Window has been cleared due to the session being closed.
  private static final int WINDOW_CLOSE = 0;
  // Window has been set due to the session being opened.
  private static final int WINDOW_OPEN = 1; // Window has been opened.
  // Window has been cleared due to the session being transferred to another session.
  private static final int WINDOW_TRANSFER_OUT = 2; // Window has been transfer.
  // Window has been set due to another session being transferred to this one.
  private static final int WINDOW_TRANSFER_IN = 3;

  private static final int DATA_URI_MAX_LENGTH = 2 * 1024 * 1024;

  // Delay running compositor memory pressure by 10s to avoid interfering with tab switching.
  private static final int NOTIFY_MEMORY_PRESSURE_DELAY_MS = 10 * 1000;

  private final Runnable mNotifyMemoryPressure =
      new Runnable() {
        @Override
        public void run() {
          if (mCompositorReady) {
            mCompositor.notifyMemoryPressure();
          }
        }
      };

  private enum State implements NativeQueue.State {
    INITIAL(0),
    READY(1);

    private final int mRank;

    State(final int rank) {
      mRank = rank;
    }

    @Override
    public boolean is(final NativeQueue.State other) {
      return this == other;
    }

    @Override
    public boolean isAtLeast(final NativeQueue.State other) {
      return (other instanceof State) && mRank >= ((State) other).mRank;
    }
  }

  private final NativeQueue mNativeQueue = new NativeQueue(State.INITIAL, State.READY);

  private final EventDispatcher mEventDispatcher = new EventDispatcher(mNativeQueue);

  private final SessionTextInput mTextInput = new SessionTextInput(this, mNativeQueue);

  private final HapticFeedbackController mHapticFeedbackController = new HapticFeedbackController();
  private SessionAccessibility mAccessibility;
  private SessionFinder mFinder;
  private SessionPdfFileSaver mPdfFileSaver;
  private TranslationsController.SessionTranslation mTranslations =
      new TranslationsController.SessionTranslation(this);

  /** Session page extractor. Initialized once, in {@link #getSessionPageExtractor()} */
  @OptIn(markerClass = ExperimentalGeckoViewApi.class)
  private PageExtractionController.SessionPageExtractor mPageExtractor;

  /** {@code SessionMagnifier} handles magnifying glass. */
  /* package */ interface SessionMagnifier {
    /**
     * Get the current {@link android.view.View} for magnifying glass.
     *
     * @return Current View for magnifying glass or null if not set.
     */
    @UiThread
    default @Nullable View getView() {
      return null;
    }

    /**
     * Set the current {@link android.view.View} for magnifying glass.
     *
     * @param view View for magnifying glass or null to clear current View.
     */
    @UiThread
    default void setView(final @NonNull View view) {}

    /**
     * Show magnifying glass.
     *
     * @param sourceCenter The source center of view that magnifying glass is attached
     */
    @UiThread
    default void show(final @NonNull PointF sourceCenter) {}

    /** Dismiss magnifying glass. */
    @UiThread
    default void dismiss() {}
  }

  @RequiresApi(Build.VERSION_CODES.P)
  private class SessionMagnifierP implements GeckoSession.SessionMagnifier {
    private @Nullable View mView;
    private @Nullable Magnifier mMagnifier;
    private final @NonNull Compositor mCompositor;

    private SessionMagnifierP(final Compositor compositor) {
      mCompositor = compositor;
    }

    @Override
    @UiThread
    public @Nullable View getView() {
      ThreadUtils.assertOnUiThread();

      return mView;
    }

    @Override
    @UiThread
    public void setView(final @NonNull View view) {
      ThreadUtils.assertOnUiThread();

      if (mMagnifier != null) {
        mMagnifier.dismiss();
        mMagnifier = null;
      }
      mView = view;
    }

    @Override
    @UiThread
    public void show(final @NonNull PointF sourceCenter) {
      ThreadUtils.assertOnUiThread();

      if (mView == null) {
        return;
      }
      if (mMagnifier == null) {
        mMagnifier = new Magnifier(mView);
      }

      if (mView instanceof MagnifiableSurfaceView) {
        final MagnifiableSurfaceView view = (MagnifiableSurfaceView) mView;
        view.setMagnifierSurface(mCompositor.getMagnifiableSurface());
      }
      mMagnifier.show(sourceCenter.x, sourceCenter.y);
      if (mView instanceof MagnifiableSurfaceView) {
        final MagnifiableSurfaceView view = (MagnifiableSurfaceView) mView;
        view.setMagnifierSurface(null);
      }
    }

    @Override
    @UiThread
    public void dismiss() {
      ThreadUtils.assertOnUiThread();

      if (mMagnifier == null) {
        return;
      }

      mMagnifier.dismiss();
      mMagnifier = null;
    }
  }

  private SessionMagnifier mMagnifier;

  private String mId;

  /* package */ String getId() {
    return mId;
  }

  private boolean mShouldPinOnScreen;

  // All fields are accessed on UI thread only.
  private PanZoomController mPanZoomController = new PanZoomController(this);
  private OverscrollEdgeEffect mOverscroll;
  private CompositorController mController;
  private Autofill.Support mAutofillSupport;

  private boolean mAttachedCompositor;
  private boolean mCompositorReady;
  private SurfaceInfo mSurfaceInfo;
  private GeckoDisplay.NewSurfaceProvider mNewSurfaceProvider;

  // All fields of coordinates are in screen units.
  private int mLeft;
  private int mTop; // Top of the surface (including toolbar);
  private int mClientTop; // Top of the client area (i.e. excluding toolbar);
  private int mWidth;
  private int mHeight; // Height of the surface (including toolbar);
  private int mClientHeight; // Height of the client area (i.e. excluding toolbar);
  private int mFixedBottomOffset =
      0; // The margin for fixed elements attached to the bottom of the viewport.
  private int mDynamicToolbarMaxHeight = 0; // The maximum height of the dynamic toolbar
  private float mViewportLeft;
  private float mViewportTop;
  private float mViewportZoom = 1.0f;
  private ScrollPositionUpdate mLastScrollPositionUpdate = null;
  private int mKeyboardHeight = 0; // The software keyboard height, 0 if it's hidden.

  //
  // NOTE: These values are also defined in
  // gfx/layers/ipc/UiCompositorControllerMessageTypes.h and must be kept in sync. Any
  // new AnimatorMessageType added here must also be added there.
  //
  // Sent from compositor after first paint
  /* package */ static final int FIRST_PAINT = 0;
  // Sent from compositor when a layer has been updated
  /* package */ static final int LAYERS_UPDATED = 1;
  // Special message sent from UiCompositorControllerChild once it is open
  /* package */ static final int COMPOSITOR_CONTROLLER_OPEN = 2;
  // Special message sent from controller to query if the compositor controller is open.
  /* package */ static final int IS_COMPOSITOR_CONTROLLER_OPEN = 3;

  /* protected */ class Compositor extends JNIObject {
    public boolean isReady() {
      return GeckoSession.this.isCompositorReady();
    }

    @WrapForJNI(calledFrom = "ui")
    private void onCompositorAttached() {
      GeckoSession.this.onCompositorAttached();
    }

    @WrapForJNI(calledFrom = "ui")
    private void onCompositorDetached() {
      // Clear out any pending calls on the UI thread.
      GeckoSession.this.onCompositorDetached();
    }

    @WrapForJNI(dispatchTo = "gecko")
    @Override
    protected native void disposeNative();

    @WrapForJNI(calledFrom = "ui", dispatchTo = "gecko")
    public native void attachNPZC(PanZoomController.NativeProvider npzc);

    @WrapForJNI(calledFrom = "ui", dispatchTo = "proxy")
    public native void onBoundsChanged(int left, int top, int width, int height);

    @WrapForJNI(calledFrom = "ui", dispatchTo = "gecko")
    public native void setDynamicToolbarMaxHeight(int height);

    @WrapForJNI(calledFrom = "ui", dispatchTo = "gecko")
    public native void notifyMemoryPressure();

    // Gecko thread pauses compositor; blocks UI thread.
    @WrapForJNI(calledFrom = "ui", dispatchTo = "current")
    public native void syncPauseCompositor();

    // UI thread resumes compositor and notifies Gecko thread; does not block UI thread.
    @WrapForJNI(calledFrom = "ui", dispatchTo = "current")
    public native void syncResumeResizeCompositor(
        int x, int y, int width, int height, Object surface, Object surfaceControl);

    // Returns a Surface that content has been rendered in to, which should be used when the
    // magnifier is shown. This may differ from the Surface we have passed to
    // syncResumeResizeCompositor().
    @WrapForJNI(calledFrom = "ui", dispatchTo = "current")
    public native Surface getMagnifiableSurface();

    @WrapForJNI(calledFrom = "ui", dispatchTo = "current")
    public native void setMaxToolbarHeight(int height);

    @WrapForJNI(calledFrom = "ui", dispatchTo = "gecko")
    public native void setFixedBottomOffset(int offset);

    @WrapForJNI(calledFrom = "ui", dispatchTo = "current")
    public native void sendToolbarAnimatorMessage(int message);

    @WrapForJNI(calledFrom = "ui")
    private void recvToolbarAnimatorMessage(final int message) {
      GeckoSession.this.handleCompositorMessage(message);
    }

    @WrapForJNI(calledFrom = "ui")
    private void requestNewSurface() {
      final GeckoDisplay.NewSurfaceProvider provider = GeckoSession.this.mNewSurfaceProvider;
      if (provider != null) {
        provider.requestNewSurface();
      } else {
        Log.w(LOGTAG, "Cannot request new Surface: No NewSurfaceProvider set.");
      }
    }

    @WrapForJNI(calledFrom = "ui", dispatchTo = "current")
    public native void setDefaultClearColor(int color);

    @WrapForJNI(calledFrom = "ui", dispatchTo = "current")
    /* package */ native void requestScreenPixels(
        final GeckoResult<Bitmap> result,
        final Bitmap target,
        final int x,
        final int y,
        final int srcWidth,
        final int srcHeight,
        final int outWidth,
        final int outHeight);

    @WrapForJNI(calledFrom = "ui", dispatchTo = "current")
    public native void enableLayerUpdateNotifications(boolean enable);

    // The compositor invokes this function just before compositing a frame where the
    // document is different from the document composited on the last frame. In these
    // cases, the viewport information we have in Java is no longer valid and needs to
    // be replaced with the new viewport information provided.
    @WrapForJNI(calledFrom = "ui")
    private void notifyCompositorScrollUpdate(
        final float scrollX, final float scrollY, final float zoom, final int source) {
      GeckoSession.this.onCompositorScrollUpdate(scrollX, scrollY, zoom, source);
    }

    @WrapForJNI(calledFrom = "ui")
    private void updateOverscrollVelocity(final float x, final float y) {
      GeckoSession.this.updateOverscrollVelocity(x, y);
    }

    @WrapForJNI(calledFrom = "ui")
    private void updateOverscrollOffset(final float x, final float y) {
      GeckoSession.this.updateOverscrollOffset(x, y);
    }

    @WrapForJNI(calledFrom = "ui", dispatchTo = "proxy")
    public native void onSafeAreaInsetsChanged(int top, int right, int bottom, int left);

    @WrapForJNI(calledFrom = "ui", dispatchTo = "gecko")
    public native void onPipModeChanged(boolean enabled);

    @WrapForJNI(calledFrom = "ui", dispatchTo = "proxy")
    public native void onKeyboardHeightChanged(int height);

    @WrapForJNI(calledFrom = "ui")
    public void setPointerIcon(
        final int defaultCursor, final Bitmap customCursor, final float x, final float y) {
      GeckoSession.this.setPointerIcon(defaultCursor, customCursor, x, y);
    }

    @WrapForJNI(calledFrom = "ui")
    private void startDragAndDrop(final Bitmap bitmap) {
      GeckoSession.this.startDragAndDrop(bitmap);
    }

    @WrapForJNI(calledFrom = "ui")
    private void updateDragImage(final Bitmap bitmap) {
      GeckoSession.this.updateDragImage(bitmap);
    }

    @Override
    protected void finalize() throws Throwable {
      disposeNative();
    }
  }

  /* package */ final Compositor mCompositor = new Compositor();

  @WrapForJNI(stubName = "GetCompositor", calledFrom = "ui")
  private Object getCompositorFromNative() {
    // Only used by native code.
    return mCompositorReady ? mCompositor : null;
  }

  private final GeckoSessionHandler<HistoryDelegate> mHistoryHandler =
      new GeckoSessionHandler<HistoryDelegate>(
          "GeckoViewHistory",
          this,
          new String[] {
            "GeckoView:OnVisited", "GeckoView:GetVisited", "GeckoView:StateUpdated",
          }) {
        @Override
        public void handleMessage(
            final HistoryDelegate delegate,
            final String event,
            final GeckoBundle message,
            final EventCallback callback) {
          if ("GeckoView:OnVisited".equals(event)) {
            final GeckoResult<Boolean> result =
                delegate.onVisited(
                    GeckoSession.this,
                    message.getString("url"),
                    message.getString("lastVisitedURL"),
                    message.getInt("flags"));

            if (result == null) {
              callback.sendSuccess(false);
              return;
            }

            result.accept(
                visited -> callback.sendSuccess(visited.booleanValue()),
                exception -> callback.sendSuccess(false));
          } else if ("GeckoView:GetVisited".equals(event)) {
            final String[] urls = message.getStringArray("urls");

            final GeckoResult<boolean[]> result = delegate.getVisited(GeckoSession.this, urls);

            if (result == null) {
              callback.sendSuccess(null);
              return;
            }

            result.accept(
                visited -> callback.sendSuccess(visited),
                exception -> callback.sendError("Failed to fetch visited statuses for URIs"));
          } else if ("GeckoView:StateUpdated".equals(event)) {

            final GeckoBundle update = message.getBundle("data");

            if (update == null) {
              return;
            }
            final int previousHistorySize = mStateCache.size();
            mStateCache.updateSessionState(update);

            final ProgressDelegate progressDelegate = getProgressDelegate();
            if (progressDelegate != null) {
              final SessionState state = new SessionState(mStateCache);
              if (!state.isEmpty()) {
                progressDelegate.onSessionStateChange(GeckoSession.this, state);
              }
            }

            if (update.getBundle("historychange") != null) {
              final SessionState state = new SessionState(mStateCache);

              delegate.onHistoryStateChange(GeckoSession.this, state);

              // If the previous history was larger than one entry and the new size is one, it means
              // the
              // History has been purged and the navigation delegate needs to be update.
              if ((previousHistorySize > 1)
                  && (state.size() == 1)
                  && mNavigationHandler.getDelegate() != null) {
                mNavigationHandler.getDelegate().onCanGoForward(GeckoSession.this, false);
                mNavigationHandler.getDelegate().onCanGoBack(GeckoSession.this, false);
              }
            }
          }
        }
      };

  private final WebExtension.SessionController mWebExtensionController;

  private final GeckoSessionHandler<ContentDelegate> mContentHandler =
      new GeckoSessionHandler<ContentDelegate>(
          "GeckoViewContent",
          this,
          new String[] {
            "GeckoView:ContentCrash",
            "GeckoView:ContentKill",
            "GeckoView:ContextMenu",
            "GeckoView:DOMMetaViewportFit",
            "GeckoView:PageTitleChanged",
            "GeckoView:DOMWindowClose",
            "GeckoView:ExternalResponse",
            "GeckoView:FocusRequest",
            "GeckoView:FullScreenEnter",
            "GeckoView:FullScreenExit",
            "GeckoView:WebAppManifest",
            "GeckoView:FirstContentfulPaint",
            "GeckoView:PaintStatusReset",
            "GeckoView:PreviewImage",
            "GeckoView:CookieBannerEvent:Detected",
            "GeckoView:CookieBannerEvent:Handled",
            "GeckoView:SavePdf",
            "GeckoView:GetNimbusFeature",
          }) {
        @Override
        public void handleMessage(
            final ContentDelegate delegate,
            final String event,
            final GeckoBundle message,
            final EventCallback callback) {
          if ("GeckoView:ContentCrash".equals(event)) {
            close();
            delegate.onCrash(GeckoSession.this);
          } else if ("GeckoView:ContentKill".equals(event)) {
            close();
            delegate.onKill(GeckoSession.this);
          } else if ("GeckoView:ContextMenu".equals(event)) {
            final ContentDelegate.ContextElement elem =
                new ContentDelegate.ContextElement(
                    message.getString("baseUri"),
                    message.getString("uri"),
                    message.getString("title"),
                    message.getString("alt"),
                    message.getString("elementType"),
                    message.getString("elementSrc"),
                    message.getString("linkText"));

            delegate.onContextMenu(
                GeckoSession.this, message.getInt("screenX"), message.getInt("screenY"), elem);

          } else if ("GeckoView:DOMMetaViewportFit".equals(event)) {
            delegate.onMetaViewportFitChange(GeckoSession.this, message.getString("viewportfit"));
          } else if ("GeckoView:PageTitleChanged".equals(event)) {
            delegate.onTitleChange(GeckoSession.this, message.getString("title"));
          } else if ("GeckoView:FocusRequest".equals(event)) {
            delegate.onFocusRequest(GeckoSession.this);
          } else if ("GeckoView:DOMWindowClose".equals(event)) {
            if (getSelectionActionDelegate() != null) {
              getSelectionActionDelegate().onDismissClipboardPermissionRequest(GeckoSession.this);
            }
            delegate.onCloseRequest(GeckoSession.this);
          } else if ("GeckoView:FullScreenEnter".equals(event)) {
            delegate.onFullScreen(GeckoSession.this, true);
          } else if ("GeckoView:FullScreenExit".equals(event)) {
            delegate.onFullScreen(GeckoSession.this, false);
          } else if ("GeckoView:WebAppManifest".equals(event)) {
            final GeckoBundle manifest = message.getBundle("manifest");
            if (manifest == null) {
              return;
            }

            try {
              delegate.onWebAppManifest(
                  GeckoSession.this, fixupWebAppManifest(manifest.toJSONObject()));
            } catch (final JSONException e) {
              Log.e(LOGTAG, "Failed to convert web app manifest to JSON", e);
            }
          } else if ("GeckoView:FirstContentfulPaint".equals(event)) {
            delegate.onFirstContentfulPaint(GeckoSession.this);
          } else if ("GeckoView:PaintStatusReset".equals(event)) {
            delegate.onPaintStatusReset(GeckoSession.this);
          } else if ("GeckoView:PreviewImage".equals(event)) {
            delegate.onPreviewImage(GeckoSession.this, message.getString("previewImageUrl"));
          } else if ("GeckoView:CookieBannerEvent:Detected".equals(event)) {
            delegate.onCookieBannerDetected(GeckoSession.this);
          } else if ("GeckoView:CookieBannerEvent:Handled".equals(event)) {
            delegate.onCookieBannerHandled(GeckoSession.this);
          } else if ("GeckoView:SavePdf".equals(event)) {
            final GeckoResult<WebResponse> result =
                SessionPdfFileSaver.createResponse(
                    GeckoSession.this,
                    message.getString("url"),
                    message.getString("filename"),
                    message.getString("originalUrl"),
                    message.getBoolean("skipConfirmation"),
                    message.getBoolean("requestExternalApp"));
            if (result == null) {
              if (callback != null) {
                callback.sendError("Failed to create response");
              }
              return;
            }
            result.accept(
                response ->
                    ThreadUtils.runOnUiThread(
                        () -> delegate.onExternalResponse(GeckoSession.this, response)),
                exception -> {
                  if (callback != null) {
                    callback.sendError("Failed to create response");
                  }
                });
          }
        }
      };

  private final GeckoSessionHandler<NavigationDelegate> mNavigationHandler =
      new GeckoSessionHandler<NavigationDelegate>(
          "GeckoViewNavigation",
          this,
          new String[] {"GeckoView:LocationChange", "GeckoView:OnNewSession"},
          new String[] {
            "GeckoView:OnLoadError", "GeckoView:OnLoadRequest",
          }) {
        // This needs to match nsIBrowserDOMWindow.idl
        private int convertGeckoTarget(final int geckoTarget) {
          switch (geckoTarget) {
            case 0: // OPEN_DEFAULTWINDOW
            case 1: // OPEN_CURRENTWINDOW
              return NavigationDelegate.TARGET_WINDOW_CURRENT;
            default: // OPEN_NEWWINDOW, OPEN_NEWTAB, OPEN_NEWTAB_BACKGROUND, OPEN_NEWTAB_FOREGROUND
              return NavigationDelegate.TARGET_WINDOW_NEW;
          }
        }

        @Override
        public void handleDefaultMessage(
            final String event, final GeckoBundle message, final EventCallback callback) {

          if ("GeckoView:OnLoadRequest".equals(event)) {
            callback.sendSuccess(false);
          } else if ("GeckoView:OnLoadError".equals(event)) {
            callback.sendSuccess(null);
          } else {
            super.handleDefaultMessage(event, message, callback);
          }
        }

        // For .isOpen(), the linter is not smart enough to figure out we're asserting that we're on
        // the UI thread.
        @SuppressLint("WrongThread")
        @Override
        public void handleMessage(
            final NavigationDelegate delegate,
            final String event,
            final GeckoBundle message,
            final EventCallback callback) {
          Log.d(LOGTAG, "handleMessage " + event + " uri=" + message.getString("uri"));
          if ("GeckoView:LocationChange".equals(event)) {
            if (message.getBoolean("isTopLevel")) {
              final GeckoBundle[] perms = message.getBundleArray("permissions");
              final List<PermissionDelegate.ContentPermission> permList =
                  PermissionDelegate.ContentPermission.fromBundleArray(perms);
              delegate.onLocationChange(
                  GeckoSession.this,
                  message.getString("uri"),
                  permList,
                  message.getBoolean("hasUserGesture"));
            }
            delegate.onCanGoBack(GeckoSession.this, message.getBoolean("canGoBack"));
            delegate.onCanGoForward(GeckoSession.this, message.getBoolean("canGoForward"));
          } else if ("GeckoView:OnLoadRequest".equals(event)) {
            final NavigationDelegate.LoadRequest request =
                new NavigationDelegate.LoadRequest(
                    message.getString("uri"),
                    message.getString("triggerUri"),
                    message.getInt("where"),
                    message.getInt("flags"),
                    message.getBoolean("hasUserGesture"),
                    /* isDirectNavigation */ false);

            if (!IntentUtils.isUriSafeForScheme(request.uri)) {
              callback.sendError("Blocked unsafe intent URI");

              delegate.onLoadError(
                  GeckoSession.this,
                  request.uri,
                  new WebRequestError(
                      WebRequestError.ERROR_MALFORMED_URI,
                      WebRequestError.ERROR_CATEGORY_URI,
                      null));

              return;
            }

            final GeckoResult<AllowOrDeny> result =
                delegate.onLoadRequest(GeckoSession.this, request);

            if (result == null) {
              callback.sendSuccess(false);
              return;
            }

            callback.resolveTo(
                result.map(
                    value -> {
                      ThreadUtils.assertOnUiThread();
                      if (value == AllowOrDeny.ALLOW) {
                        return false;
                      }
                      if (value == AllowOrDeny.DENY) {
                        return true;
                      }
                      throw new IllegalArgumentException("Invalid response");
                    }));
          } else if ("GeckoView:OnLoadError".equals(event)) {
            final String uri = message.getString("uri");
            final long errorCode = message.getLong("error");
            final int errorModule = message.getInt("errorModule");
            final int errorClass = message.getInt("errorClass");

            final WebRequestError err =
                WebRequestError.fromGeckoError(errorCode, errorModule, errorClass, null);

            final GeckoResult<String> result = delegate.onLoadError(GeckoSession.this, uri, err);
            if (result == null) {
              callback.sendError("abort");
              return;
            }

            callback.resolveTo(
                result.map(
                    url -> {
                      if (url == null) {
                        throw new IllegalArgumentException("abort");
                      }
                      final String lowerCasedUri = url.toLowerCase(Locale.ROOT);
                      if (lowerCasedUri.startsWith("http") || lowerCasedUri.startsWith("https")) {
                        throw new IllegalArgumentException(
                            "Unsupported URI scheme for an error page");
                      }
                      return url;
                    }));
          } else if ("GeckoView:OnNewSession".equals(event)) {
            final String uri = message.getString("uri");

            // Early check whether parent is opened. If not opened, we cannot get parent's runtime.
            if (!GeckoSession.this.isOpen()) {
              callback.sendError("Parent session is closed or isn't opened yet");
              return;
            }

            final GeckoResult<GeckoSession> result = delegate.onNewSession(GeckoSession.this, uri);
            if (result == null) {
              callback.sendSuccess(false);
              return;
            }

            final String newSessionId = message.getString("newSessionId");
            callback.resolveTo(
                result.map(
                    session -> {
                      ThreadUtils.assertOnUiThread();
                      if (session == null) {
                        return false;
                      }

                      if (session.isOpen()) {
                        throw new AssertionError("Must use an unopened GeckoSession instance");
                      }

                      if (GeckoSession.this.mWindow == null) {
                        throw new IllegalArgumentException("Session is not attached to a window");
                      }

                      session.open(GeckoSession.this.mWindow.runtime, newSessionId);
                      return true;
                    }));
          }
        }
      };

  private final GeckoSessionHandler<PrintDelegate> mPrintHandler =
      new GeckoSessionHandler<PrintDelegate>(
          "GeckoViewPrint", this, new String[] {"GeckoView:DotPrintRequest"}) {
        @Override
        public void handleMessage(
            final PrintDelegate delegate,
            final String event,
            final GeckoBundle message,
            final EventCallback callback) {

          if ("GeckoView:DotPrintRequest".equals(event)) {
            final Long cbcId = message.getLong("canonicalBrowsingContextId");
            final GeckoResult<InputStream> pdfResult = saveAsPdfByBrowsingContext(cbcId);
            final GeckoBundle bundle = new GeckoBundle();
            pdfResult
                .accept(
                    pdfStream -> {
                      final GeckoResult<Boolean> dialogFinished =
                          delegate.onPrintWithStatus(pdfStream);
                      try {
                        dialogFinished
                            .accept(
                                isDialogFinished -> {
                                  bundle.putBoolean("isPdfSuccessful", true);
                                  mEventDispatcher.dispatch("GeckoView:DotPrintFinish", bundle);
                                })
                            .exceptionally(
                                e -> {
                                  bundle.putBoolean("isPdfSuccessful", false);
                                  if (e instanceof GeckoPrintException) {
                                    bundle.putInt("errorReason", ((GeckoPrintException) e).code);
                                  }
                                  mEventDispatcher.dispatch("GeckoView:DotPrintFinish", bundle);
                                  return null;
                                });
                      } catch (final Exception e) {
                        bundle.putBoolean("isPdfSuccessful", false);
                        mEventDispatcher.dispatch("GeckoView:DotPrintFinish", bundle);
                        Log.e(LOGTAG, "Print delegate needs to be fully implemented to print.", e);
                      }
                    })
                .exceptionally(
                    e -> {
                      bundle.putBoolean("isPdfSuccessful", false);
                      if (e instanceof GeckoPrintException) {
                        bundle.putInt("errorReason", ((GeckoPrintException) e).code);
                      }
                      mEventDispatcher.dispatch("GeckoView:DotPrintFinish", bundle);
                      Log.e(LOGTAG, "Could not complete DotPrintRequest.", e);
                      return null;
                    });
          }
        }
      };

  private final GeckoSessionHandler<ExperimentDelegate> mExperimentHandler =
      new GeckoSessionHandler<ExperimentDelegate>(
          "GeckoViewExperiment",
          this,
          new String[] {
            "GeckoView:GetExperimentFeature",
            "GeckoView:RecordExposure",
            "GeckoView:RecordExperimentExposure",
            "GeckoView:RecordMalformedConfig"
          }) {
        @Override
        public void handleMessage(
            final ExperimentDelegate delegate,
            final String event,
            final GeckoBundle message,
            final EventCallback callback) {

          if (delegate == null) {
            if (callback != null) {
              callback.sendError("No experiment delegate registered.");
            }
            Log.w(LOGTAG, "No experiment delegate registered.");
            return;
          }
          final String feature = message.getString("feature", "");
          if ("GeckoView:GetExperimentFeature".equals(event) && callback != null) {
            final GeckoResult<JSONObject> result = delegate.onGetExperimentFeature(feature);
            result
                .accept(
                    json -> {
                      try {
                        callback.sendSuccess(GeckoBundle.fromJSONObject(json));
                      } catch (final JSONException e) {
                        callback.sendError("An error occured when serializing the feature data.");
                      }
                    })
                .exceptionally(
                    e -> {
                      callback.sendError("An error occurred while retrieving feature data.");
                      return null;
                    });

          } else if ("GeckoView:RecordExposure".equals(event) && callback != null) {
            final GeckoResult<Void> result = delegate.onRecordExposureEvent(feature);
            result
                .accept(
                    a -> {
                      callback.sendSuccess(true);
                    })
                .exceptionally(
                    e -> {
                      callback.sendError("An error occurred while recording feature.");
                      return null;
                    });

          } else if ("GeckoView:RecordExperimentExposure".equals(event) && callback != null) {
            final String slug = message.getString("slug", "");
            final GeckoResult<Void> result =
                delegate.onRecordExperimentExposureEvent(feature, slug);
            result
                .accept(
                    a -> {
                      callback.sendSuccess(true);
                    })
                .exceptionally(
                    e -> {
                      callback.sendError("An error occurred while recording experiment feature.");
                      return null;
                    });

          } else if ("GeckoView:RecordMalformedConfig".equals(event) && callback != null) {
            final String part = message.getString("part", "");
            final GeckoResult<Void> result =
                delegate.onRecordMalformedConfigurationEvent(feature, part);
            result
                .accept(
                    a -> {
                      callback.sendSuccess(true);
                    })
                .exceptionally(
                    e -> {
                      callback.sendError(
                          "An error occurred while recording malformed feature config.");
                      return null;
                    });
          }
        }
      };

  private final GeckoSessionHandler<ContentDelegate> mProcessHangHandler =
      new GeckoSessionHandler<ContentDelegate>(
          "GeckoViewProcessHangMonitor", this, new String[] {"GeckoView:HangReport"}) {

        @Override
        protected void handleMessage(
            final ContentDelegate delegate,
            final String event,
            final GeckoBundle message,
            final EventCallback eventCallback) {
          Log.d(LOGTAG, "handleMessage " + event + " uri=" + message.getString("uri"));

          final GeckoResult<SlowScriptResponse> result =
              delegate.onSlowScript(GeckoSession.this, message.getString("scriptFileName"));
          if (result != null) {
            final int mReportId = message.getInt("hangId");
            result.accept(
                stopOrContinue -> {
                  if (stopOrContinue != null) {
                    final GeckoBundle bundle = new GeckoBundle();
                    bundle.putInt("hangId", mReportId);
                    switch (stopOrContinue) {
                      case STOP:
                        mEventDispatcher.dispatch("GeckoView:HangReportStop", bundle);
                        break;
                      case CONTINUE:
                        mEventDispatcher.dispatch("GeckoView:HangReportWait", bundle);
                        break;
                    }
                  }
                });
          } else {
            // default to stopping the script
            final GeckoBundle bundle = new GeckoBundle();
            bundle.putInt("hangId", message.getInt("hangId"));
            mEventDispatcher.dispatch("GeckoView:HangReportStop", bundle);
          }
        }
      };

  private final GeckoSessionHandler<ProgressDelegate> mProgressHandler =
      new GeckoSessionHandler<ProgressDelegate>(
          "GeckoViewProgress",
          this,
          new String[] {
            "GeckoView:PageStart",
            "GeckoView:PageStop",
            "GeckoView:ProgressChanged",
            "GeckoView:SecurityChanged",
            "GeckoView:StateUpdated",
          }) {
        @Override
        public void handleMessage(
            final ProgressDelegate delegate,
            final String event,
            final GeckoBundle message,
            final EventCallback callback) {
          Log.d(LOGTAG, "handleMessage " + event + " uri=" + message.getString("uri"));
          if ("GeckoView:PageStart".equals(event)) {
            if (getSelectionActionDelegate() != null) {
              getSelectionActionDelegate().onDismissClipboardPermissionRequest(GeckoSession.this);
            }
            delegate.onPageStart(GeckoSession.this, message.getString("uri"));
          } else if ("GeckoView:PageStop".equals(event)) {
            delegate.onPageStop(GeckoSession.this, message.getBoolean("success"));
          } else if ("GeckoView:ProgressChanged".equals(event)) {
            delegate.onProgressChange(GeckoSession.this, message.getInt("progress"));
          } else if ("GeckoView:SecurityChanged".equals(event)) {
            final GeckoBundle identity = message.getBundle("identity");
            delegate.onSecurityChange(
                GeckoSession.this, new ProgressDelegate.SecurityInformation(identity));
          } else if ("GeckoView:StateUpdated".equals(event)) {
            final GeckoBundle update = message.getBundle("data");
            if (update != null) {
              if (getHistoryDelegate() == null) {
                mStateCache.updateSessionState(update);
                final SessionState state = new SessionState(mStateCache);
                if (!state.isEmpty()) {
                  delegate.onSessionStateChange(GeckoSession.this, state);
                }
              }
            }
          }
        }
      };

  private final GeckoSessionHandler<ScrollDelegate> mScrollHandler =
      new GeckoSessionHandler<ScrollDelegate>(
          "GeckoViewScroll", this, new String[] {"GeckoView:ScrollChanged"}) {
        @Override
        public void handleMessage(
            final ScrollDelegate delegate,
            final String event,
            final GeckoBundle message,
            final EventCallback callback) {

          if ("GeckoView:ScrollChanged".equals(event)) {
            delegate.onScrollChanged(
                GeckoSession.this, message.getInt("scrollX"), message.getInt("scrollY"));
          }
        }
      };

  private CompositorScrollDelegate mCompositorScrollDelegate = null;

  private final GeckoSessionHandler<ContentBlocking.Delegate> mContentBlockingHandler =
      new GeckoSessionHandler<ContentBlocking.Delegate>(
          "GeckoViewContentBlocking", this, new String[] {"GeckoView:ContentBlockingEvent"}) {
        @Override
        public void handleMessage(
            final ContentBlocking.Delegate delegate,
            final String event,
            final GeckoBundle message,
            final EventCallback callback) {

          if ("GeckoView:ContentBlockingEvent".equals(event)) {
            final ContentBlocking.BlockEvent be = ContentBlocking.BlockEvent.fromBundle(message);
            if (be.isBlocking()) {
              delegate.onContentBlocked(GeckoSession.this, be);
            } else {
              delegate.onContentLoaded(GeckoSession.this, be);
            }
          }
        }
      };

  private final GeckoSessionHandler<PermissionDelegate> mPermissionHandler =
      new GeckoSessionHandler<PermissionDelegate>(
          "GeckoViewPermission",
          this,
          new String[] {
            "GeckoView:AndroidPermission",
            "GeckoView:ContentPermission",
            "GeckoView:MediaPermission"
          }) {
        @Override
        public void handleMessage(
            final PermissionDelegate delegate,
            final String event,
            final GeckoBundle message,
            final EventCallback callback) {
          Log.d(LOGTAG, "handleMessage: " + event);
          if (delegate == null) {
            callback.sendSuccess(/* granted */ false);
            return;
          }
          if ("GeckoView:AndroidPermission".equals(event)) {
            List<String> permsList = Arrays.asList(message.getStringArray("perms"));
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
              if (permsList.contains(Manifest.permission.ACCESS_FINE_LOCATION)
                  && !permsList.contains(Manifest.permission.ACCESS_COARSE_LOCATION)) {
                // If we are requesting ACCESS_FINE_LOCATION we should also be
                // requesting ACCESS_COARSE_LOCATION. See bug 1790467
                permsList = new ArrayList<String>(permsList);
                permsList.add(Manifest.permission.ACCESS_COARSE_LOCATION);
              }
            }
            delegate.onAndroidPermissionsRequest(
                GeckoSession.this,
                permsList.toArray(new String[0]),
                new PermissionCallback("android", callback));
          } else if ("GeckoView:ContentPermission".equals(event)) {
            final GeckoResult<Integer> res =
                delegate.onContentPermissionRequest(
                    GeckoSession.this, new PermissionDelegate.ContentPermission(message));
            if (res == null) {
              callback.sendSuccess(PermissionDelegate.ContentPermission.VALUE_PROMPT);
              return;
            }

            callback.resolveTo(res);
          } else if ("GeckoView:MediaPermission".equals(event)) {
            final GeckoBundle[] videoBundles = message.getBundleArray("video");
            final GeckoBundle[] audioBundles = message.getBundleArray("audio");
            PermissionDelegate.MediaSource[] videos = null;
            PermissionDelegate.MediaSource[] audios = null;

            if (videoBundles != null) {
              videos = new PermissionDelegate.MediaSource[videoBundles.length];
              for (int i = 0; i < videoBundles.length; i++) {
                videos[i] = new PermissionDelegate.MediaSource(videoBundles[i]);
              }
            }

            if (audioBundles != null) {
              audios = new PermissionDelegate.MediaSource[audioBundles.length];
              for (int i = 0; i < audioBundles.length; i++) {
                audios[i] = new PermissionDelegate.MediaSource(audioBundles[i]);
              }
            }

            delegate.onMediaPermissionRequest(
                GeckoSession.this,
                message.getString("uri"),
                videos,
                audios,
                new PermissionCallback("media", callback));
          }
        }
      };

  private final GeckoSessionHandler<SelectionActionDelegate> mSelectionActionDelegate =
      new GeckoSessionHandler<SelectionActionDelegate>(
          "GeckoViewSelectionAction",
          this,
          new String[] {
            "GeckoView:HideSelectionAction",
            "GeckoView:ShowSelectionAction",
            "GeckoView:HideMagnifier",
            "GeckoView:ShowMagnifier",
            "GeckoView:ClipboardPermissionRequest",
            "GeckoView:DismissClipboardPermissionRequest",
          }) {
        @Override
        public void handleMessage(
            final SelectionActionDelegate delegate,
            final String event,
            final GeckoBundle message,
            final EventCallback callback) {
          Log.d(LOGTAG, "handleMessage: " + event);
          if ("GeckoView:ShowSelectionAction".equals(event)) {
            final @SelectionActionDelegateAction HashSet<String> actionsSet =
                new HashSet<>(Arrays.asList(message.getStringArray("actions")));
            final SelectionActionDelegate.Selection selection =
                new SelectionActionDelegate.Selection(message, actionsSet, mEventDispatcher);

            delegate.onShowActionRequest(GeckoSession.this, selection);

          } else if ("GeckoView:HideSelectionAction".equals(event)) {
            final String reasonString = message.getString("reason");
            final int reason;
            if ("invisibleselection".equals(reasonString)) {
              reason = SelectionActionDelegate.HIDE_REASON_INVISIBLE_SELECTION;
            } else if ("presscaret".equals(reasonString)) {
              reason = SelectionActionDelegate.HIDE_REASON_ACTIVE_SELECTION;
            } else if ("scroll".equals(reasonString)) {
              reason = SelectionActionDelegate.HIDE_REASON_ACTIVE_SCROLL;
            } else if ("visibilitychange".equals(reasonString)) {
              reason = SelectionActionDelegate.HIDE_REASON_NO_SELECTION;
            } else {
              throw new IllegalArgumentException();
            }

            delegate.onHideAction(GeckoSession.this, reason);
          } else if ("GeckoView:ShowMagnifier".equals(event)) {
            final PointF point = message.getPointF("screenPoint");
            if (point == null) {
              throw new IllegalArgumentException("Invalid argument");
            }

            // Magnifier is surface coordinate.
            point.x -= GeckoSession.this.mLeft;
            point.y -= GeckoSession.this.mClientTop;
            GeckoSession.this.getMagnifier().show(point);
          } else if ("GeckoView:HideMagnifier".equals(event)) {
            GeckoSession.this.getMagnifier().dismiss();
          } else if ("GeckoView:ClipboardPermissionRequest".equals(event)) {
            final SelectionActionDelegate.ClipboardPermission permission =
                new SelectionActionDelegate.ClipboardPermission(message);

            final GeckoResult<AllowOrDeny> result =
                delegate.onShowClipboardPermissionRequest(GeckoSession.this, permission);
            callback.resolveTo(
                result.map(
                    value -> {
                      if (value == AllowOrDeny.ALLOW) {
                        return true;
                      }
                      if (value == AllowOrDeny.DENY) {
                        return false;
                      }
                      throw new IllegalArgumentException("Invalid response");
                    }));
          } else if ("GeckoView:DismissClipboardPermissionRequest".equals(event)) {
            delegate.onDismissClipboardPermissionRequest(GeckoSession.this);
          }
        }
      };

  private final GeckoSessionHandler<MediaDelegate> mMediaHandler =
      new GeckoSessionHandler<MediaDelegate>(
          "GeckoViewMedia",
          this,
          new String[] {
            "GeckoView:MediaRecordingStatusChanged",
          }) {
        @Override
        public void handleMessage(
            final MediaDelegate delegate,
            final String event,
            final GeckoBundle message,
            final EventCallback callback) {
          if ("GeckoView:MediaRecordingStatusChanged".equals(event)) {
            final GeckoBundle[] deviceBundles = message.getBundleArray("devices");
            final MediaDelegate.RecordingDevice[] devices =
                new MediaDelegate.RecordingDevice[deviceBundles.length];
            for (int i = 0; i < deviceBundles.length; i++) {
              devices[i] = new MediaDelegate.RecordingDevice(deviceBundles[i]);
            }
            delegate.onRecordingStatusChanged(GeckoSession.this, devices);
            return;
          }
        }
      };

  private final MediaSession.Handler mMediaSessionHandler = new MediaSession.Handler(this);
  private final TranslationsController.SessionTranslation.Handler mTranslationsHandler =
      mTranslations.getHandler();

  /* package */ int handlersCount;

  private final GeckoSessionHandler<?>[] mSessionHandlers =
      new GeckoSessionHandler<?>[] {
        mContentHandler,
        mHistoryHandler,
        mMediaHandler,
        mNavigationHandler,
        mPermissionHandler,
        mPrintHandler,
        mProcessHangHandler,
        mProgressHandler,
        mScrollHandler,
        mSelectionActionDelegate,
        mTranslationsHandler,
        mContentBlockingHandler,
        mMediaSessionHandler,
        mExperimentHandler
      };

  private static class PermissionCallback
      implements PermissionDelegate.Callback, PermissionDelegate.MediaCallback {

    private final String mType;
    private EventCallback mCallback;

    public PermissionCallback(final String type, final EventCallback callback) {
      mType = type;
      mCallback = callback;
    }

    private void submit(final Object response) {
      if (mCallback != null) {
        mCallback.sendSuccess(response);
        mCallback = null;
      }
    }

    @Override // PermissionDelegate.Callback
    public void grant() {
      if ("media".equals(mType)) {
        throw new UnsupportedOperationException();
      }
      submit(/* response */ true);
    }

    @Override // PermissionDelegate.Callback, PermissionDelegate.MediaCallback
    public void reject() {
      submit(/* response */ false);
    }

    @Override // PermissionDelegate.MediaCallback
    public void grant(final String video, final String audio) {
      if (!"media".equals(mType)) {
        throw new UnsupportedOperationException();
      }
      final GeckoBundle response = new GeckoBundle(2);
      response.putString("video", video);
      response.putString("audio", audio);
      submit(response);
    }

    @Override // PermissionDelegate.MediaCallback
    public void grant(
        final PermissionDelegate.MediaSource video, final PermissionDelegate.MediaSource audio) {
      grant(video != null ? video.id : null, audio != null ? audio.id : null);
    }
  }

  /**
   * Get the current user agent string for this GeckoSession.
   *
   * @return a {@link GeckoResult} containing the UserAgent string
   */
  @HandlerThread
  public @NonNull GeckoResult<String> getUserAgent() {
    ThreadUtils.assertOnHandlerThread();
    return mEventDispatcher.queryString("GeckoView:GetUserAgent");
  }

  /**
   * Get the default user agent for this GeckoView build.
   *
   * <p>This method does not account for any override that might have been applied to the user agent
   * string.
   *
   * @return the default user agent string
   */
  @AnyThread
  public static @NonNull String getDefaultUserAgent() {
    // Spoof version "Android 10" for Android OS versions < 10 (Q) to reduce
    // their fingerprintable user information. For Android OS versions >= 10,
    // report the real OS version because some enterprise websites only want to
    // permit clients with recent OS version (like bug 1876742).
    return Build.VERSION.SDK_INT < Build.VERSION_CODES.Q
        ? BuildConfig.USER_AGENT_GECKOVIEW_MOBILE_ANDROID_10
        : BuildConfig.USER_AGENT_GECKOVIEW_MOBILE;
  }

  /**
   * Get the current permission delegate for this GeckoSession.
   *
   * @return PermissionDelegate instance or null if using default delegate.
   */
  @UiThread
  public @Nullable PermissionDelegate getPermissionDelegate() {
    ThreadUtils.assertOnUiThread();
    return mPermissionHandler.getDelegate();
  }

  /**
   * Set the current permission delegate for this GeckoSession.
   *
   * @param delegate PermissionDelegate instance or null to use the default delegate.
   */
  @UiThread
  public void setPermissionDelegate(final @Nullable PermissionDelegate delegate) {
    ThreadUtils.assertOnUiThread();
    mPermissionHandler.setDelegate(delegate, this);
  }

  private PromptDelegate mPromptDelegate;

  private final Listener mListener = new Listener();

  /* package */ static final class Window extends JNIObject implements IInterface {
    public final GeckoRuntime runtime;
    private WeakReference<GeckoSession> mOwner;
    private NativeQueue mNativeQueue;
    private Binder mBinder;

    public Window(
        final @NonNull GeckoRuntime runtime,
        final @NonNull GeckoSession owner,
        final @NonNull NativeQueue nativeQueue) {
      this.runtime = runtime;
      mOwner = new WeakReference<>(owner);
      mNativeQueue = nativeQueue;
    }

    @Override // IInterface
    public Binder asBinder() {
      if (mBinder == null) {
        mBinder = new Binder();
        mBinder.attachInterface(this, Window.class.getName());
      }
      return mBinder;
    }

    // Create a new Gecko window and assign an initial set of Java session objects to it.
    @WrapForJNI(dispatchTo = "proxy")
    public static native void open(
        Window instance,
        NativeQueue queue,
        Compositor compositor,
        EventDispatcher dispatcher,
        SessionAccessibility.NativeProvider sessionAccessibility,
        GeckoBundle initData,
        String id,
        String chromeUri,
        boolean privateMode);

    @Override // JNIObject
    public void disposeNative() {
      if (GeckoThread.isStateAtLeast(GeckoThread.State.PROFILE_READY)) {
        nativeDisposeNative();
      } else {
        GeckoThread.queueNativeCallUntil(
            GeckoThread.State.PROFILE_READY, this, "nativeDisposeNative");
      }
    }

    @WrapForJNI(dispatchTo = "proxy", stubName = "DisposeNative")
    private native void nativeDisposeNative();

    // Force the underlying Gecko window to close and release assigned Java objects.
    public void close() {
      // Reset our queue, so we don't end up with queued calls on a disposed object.
      synchronized (this) {
        if (mNativeQueue == null) {
          // Already closed elsewhere.
          return;
        }
        mNativeQueue.reset(State.INITIAL);
        mNativeQueue = null;
        mOwner = new WeakReference<>(null);
      }

      // Detach ourselves from the binder as well, to prevent this window from being
      // read from any parcels.
      asBinder().attachInterface(null, Window.class.getName());

      if (GeckoThread.isStateAtLeast(GeckoThread.State.PROFILE_READY)) {
        nativeClose();
      } else {
        GeckoThread.queueNativeCallUntil(GeckoThread.State.PROFILE_READY, this, "nativeClose");
      }
    }

    @WrapForJNI(dispatchTo = "proxy", stubName = "Close")
    private native void nativeClose();

    @WrapForJNI(dispatchTo = "proxy", stubName = "Transfer")
    private native void nativeTransfer(
        NativeQueue queue,
        Compositor compositor,
        EventDispatcher dispatcher,
        SessionAccessibility.NativeProvider sessionAccessibility,
        GeckoBundle initData);

    @WrapForJNI(dispatchTo = "proxy")
    public native void attachEditable(IGeckoEditableParent parent);

    @WrapForJNI(dispatchTo = "proxy")
    public native void attachAccessibility(
        SessionAccessibility.NativeProvider sessionAccessibility);

    @WrapForJNI(dispatchTo = "proxy")
    public native void printToPdf(GeckoResult<InputStream> geckoResult);

    @WrapForJNI(dispatchTo = "proxy")
    private native void printToPdf(GeckoResult<InputStream> geckoResult, long browserContextId);

    @WrapForJNI(calledFrom = "gecko")
    private synchronized void onReady(final @Nullable NativeQueue queue) {
      // onReady is called the first time the Gecko window is ready, with a null queue
      // argument. In this case, we simply set the current queue to ready state.
      //
      // After the initial call, onReady is called again every time Window.transfer()
      // is called, with a non-null queue argument. In this case, we only set the
      // current queue to ready state _if_ the current queue matches the given queue,
      // because if the queues don't match, we know there is another onReady call coming.

      if ((queue == null && mNativeQueue == null) || (queue != null && mNativeQueue != queue)) {
        return;
      }

      if (mNativeQueue.checkAndSetState(State.INITIAL, State.READY) && queue == null) {
        Log.i(LOGTAG, "zerdatime " + SystemClock.elapsedRealtime() + " - chrome startup finished");
      }
    }

    @Override
    protected void finalize() throws Throwable {
      close();
      disposeNative();
    }

    @WrapForJNI(calledFrom = "gecko")
    private GeckoResult<Boolean> onLoadRequest(
        final @NonNull String uri,
        final int windowType,
        final int flags,
        final @Nullable String triggeringUri,
        final boolean hasUserGesture,
        final boolean isTopLevel) {
      final Double onLoadRequestProfilerStartTime = ProfilerController.getProfilerTime();
      final Runnable addMarker =
          () ->
              ProfilerController.addMarker(
                  "GeckoSession.onLoadRequest", onLoadRequestProfilerStartTime);

      final GeckoSession session = mOwner.get();
      if (session == null) {
        // Don't handle any load request if we can't get the session for some reason.
        return GeckoResult.fromValue(false);
      }
      final GeckoResult<Boolean> res = new GeckoResult<>();

      ThreadUtils.postToUiThread(
          new Runnable() {
            @Override
            public void run() {
              final NavigationDelegate delegate = session.getNavigationDelegate();

              if (delegate == null) {
                res.complete(false);
                addMarker.run();
                return;
              }

              if (!IntentUtils.isUriSafeForScheme(uri)) {
                delegate.onLoadError(
                    session,
                    uri,
                    new WebRequestError(
                        WebRequestError.ERROR_MALFORMED_URI,
                        WebRequestError.ERROR_CATEGORY_URI,
                        null));
                res.complete(true);
                addMarker.run();
                return;
              }

              final String trigger = TextUtils.isEmpty(triggeringUri) ? null : triggeringUri;
              final NavigationDelegate.LoadRequest req =
                  new NavigationDelegate.LoadRequest(
                      uri,
                      trigger,
                      windowType,
                      flags,
                      hasUserGesture,
                      false /* isDirectNavigation */);
              final GeckoResult<AllowOrDeny> reqResponse =
                  isTopLevel
                      ? delegate.onLoadRequest(session, req)
                      : delegate.onSubframeLoadRequest(session, req);

              if (reqResponse == null) {
                res.complete(false);
                addMarker.run();
                return;
              }

              reqResponse.accept(
                  value -> {
                    if (value == AllowOrDeny.DENY) {
                      res.complete(true);
                    } else {
                      res.complete(false);
                    }
                    addMarker.run();
                  },
                  ex -> {
                    res.complete(false);
                    addMarker.run();
                  });
            }
          });

      return res;
    }

    @WrapForJNI(calledFrom = "ui")
    private void passExternalWebResponse(final WebResponse response) {
      final GeckoSession session = mOwner.get();
      if (session == null) {
        return;
      }
      final ContentDelegate delegate = session.getContentDelegate();
      if (delegate != null) {
        delegate.onExternalResponse(session, response);
      }
    }

    @WrapForJNI(calledFrom = "gecko")
    private void onShowDynamicToolbar() {
      final Window self = this;
      ThreadUtils.runOnUiThread(
          () -> {
            final GeckoSession session = self.mOwner.get();
            if (session == null) {
              return;
            }
            final ContentDelegate delegate = session.getContentDelegate();
            if (delegate != null) {
              delegate.onShowDynamicToolbar(session);
            }
          });
    }

    @WrapForJNI(calledFrom = "gecko")
    private void onHideDynamicToolbar() {
      final Window self = this;
      ThreadUtils.runOnUiThread(
          () -> {
            final GeckoSession session = self.mOwner.get();
            if (session == null) {
              return;
            }
            final ContentDelegate delegate = session.getContentDelegate();
            if (delegate != null) {
              delegate.onHideDynamicToolbar(session);
            }
          });
    }

    @WrapForJNI(calledFrom = "gecko")
    private void performHapticFeedback(final int effect) {
      final Window self = this;
      ThreadUtils.runOnUiThread(
          () -> {
            final GeckoSession session = self.mOwner.get();
            if (session == null) {
              return;
            }
            session.getHapticFeedbackController().performHapticFeedback(effect);
          });
    }
  }

  private class Listener implements BundleEventListener {
    /* package */ void registerListeners() {
      getEventDispatcher()
          .registerUiThreadListener(
              this,
              "GeckoView:PinOnScreen",
              "GeckoView:Prompt",
              "GeckoView:Prompt:Dismiss",
              "GeckoView:Prompt:Update",
              null);
    }

    @Override
    public void handleMessage(
        final String event, final GeckoBundle message, final EventCallback callback) {
      Log.d(LOGTAG, "handleMessage " + event);

      if ("GeckoView:PinOnScreen".equals(event)) {
        GeckoSession.this.setShouldPinOnScreen(message.getBoolean("pinned"));
      } else if ("GeckoView:Prompt".equals(event)) {
        mPromptController.handleEvent(GeckoSession.this, message.getBundle("prompt"), callback);
      } else if ("GeckoView:Prompt:Dismiss".equals(event)) {
        mPromptController.dismissPrompt(message.getString("id"));
      } else if ("GeckoView:Prompt:Update".equals(event)) {
        mPromptController.updatePrompt(message.getBundle("prompt"));
      }
    }
  }

  private final PromptController mPromptController;

  /** The nsWindow connected to this GeckoSession; null if not attached. */
  protected @Nullable Window mWindow;

  private GeckoSessionSettings mSettings;

  /** Creates a new GeckoSession with default settings. */
  public GeckoSession() {
    this(null);
  }

  /**
   * Creates a new GeckoSession with the specified settings.
   *
   * @param settings The settings to configure this GeckoSession, or null to use defaults.
   */
  public GeckoSession(final @Nullable GeckoSessionSettings settings) {
    mSettings = new GeckoSessionSettings(settings, this);
    mListener.registerListeners();

    mWebExtensionController = new WebExtension.SessionController(this);
    mPromptController = new PromptController();

    mAutofillSupport = new Autofill.Support(this);
    mAutofillSupport.registerListeners();

    if (BuildConfig.DEBUG_BUILD && handlersCount != mSessionHandlers.length) {
      throw new AssertionError("Add new handler to handlers list");
    }
  }

  /* package */ @Nullable
  GeckoRuntime getRuntime() {
    if (mWindow == null) {
      return null;
    }
    return mWindow.runtime;
  }

  /* package */ synchronized void abandonWindow() {
    if (mWindow == null) {
      return;
    }

    onWindowChanged(WINDOW_TRANSFER_OUT, /* inProgress */ true);
    mWindow = null;
    onWindowChanged(WINDOW_TRANSFER_OUT, /* inProgress */ false);
  }

  /**
   * Return whether this session is open.
   *
   * @return True if session is open.
   * @see #open
   * @see #close
   */
  @UiThread
  public boolean isOpen() {
    ThreadUtils.assertOnUiThread();
    return mWindow != null;
  }

  /* package */ boolean isReady() {
    return mNativeQueue.isReady();
  }

  private GeckoBundle createInitData() {
    final GeckoBundle initData = new GeckoBundle(2);
    initData.putBundle("settings", mSettings.toBundle());

    final GeckoBundle modules = new GeckoBundle(mSessionHandlers.length);
    for (final GeckoSessionHandler<?> handler : mSessionHandlers) {
      modules.putBoolean(handler.getName(), handler.isEnabled());
    }
    initData.putBundle("modules", modules);
    return initData;
  }

  /**
   * Opens the session.
   *
   * <p>Call this when you are ready to use a GeckoSession instance.
   *
   * <p>The session is in a 'closed' state when first created. Opening it creates the underlying
   * Gecko objects necessary to load a page, etc. Most GeckoSession methods only take affect on an
   * open session, and are queued until the session is opened here. Opening a session is an
   * asynchronous operation.
   *
   * @param runtime The Gecko runtime to attach this session to.
   * @see #close
   * @see #isOpen
   */
  @UiThread
  public void open(final @NonNull GeckoRuntime runtime) {
    open(runtime, UUID.randomUUID().toString().replace("-", ""));
  }

  /* package */ void open(final @NonNull GeckoRuntime runtime, final String id) {
    ThreadUtils.assertOnUiThread();

    if (isOpen()) {
      // We will leak the existing Window if we open another one.
      throw new IllegalStateException("Session is open");
    }

    final String chromeUri = mSettings.getChromeUri();
    final boolean isPrivate = mSettings.getUsePrivateMode();

    mId = id;
    mWindow = new Window(runtime, this, mNativeQueue);
    mWebExtensionController.setRuntime(runtime);
    mExperimentHandler.setDelegate(getRuntimeExperimentDelegate(), this);

    onWindowChanged(WINDOW_OPEN, /* inProgress */ true);

    if (GeckoThread.isStateAtLeast(GeckoThread.State.PROFILE_READY)) {
      Window.open(
          mWindow,
          mNativeQueue,
          mCompositor,
          mEventDispatcher,
          mAccessibility != null ? mAccessibility.nativeProvider : null,
          createInitData(),
          mId,
          chromeUri,
          isPrivate);
    } else {
      GeckoThread.queueNativeCallUntil(
          GeckoThread.State.PROFILE_READY,
          Window.class,
          "open",
          Window.class,
          mWindow,
          NativeQueue.class,
          mNativeQueue,
          Compositor.class,
          mCompositor,
          EventDispatcher.class,
          mEventDispatcher,
          SessionAccessibility.NativeProvider.class,
          mAccessibility != null ? mAccessibility.nativeProvider : null,
          GeckoBundle.class,
          createInitData(),
          String.class,
          mId,
          String.class,
          chromeUri,
          isPrivate);
    }

    onWindowChanged(WINDOW_OPEN, /* inProgress */ false);
  }

  /**
   * Closes the session.
   *
   * <p>This frees the underlying Gecko objects and unloads the current page. The session may be
   * reopened later, but page state is not restored. Call this when you are finished using a
   * GeckoSession instance.
   *
   * @see #open
   * @see #isOpen
   */
  @UiThread
  public void close() {
    ThreadUtils.assertOnUiThread();

    if (!isOpen()) {
      Log.w(LOGTAG, "Attempted to close a GeckoSession that was already closed.");
      return;
    }

    onWindowChanged(WINDOW_CLOSE, /* inProgress */ true);

    // We need to ensure the compositor releases any Surface it currently holds.
    onSurfaceDestroyed();

    mWindow.close();
    mWindow.disposeNative();
    // Can't access the compositor after we dispose of the window
    mCompositorReady = false;
    mWindow = null;

    onWindowChanged(WINDOW_CLOSE, /* inProgress */ false);
  }

  private void onWindowChanged(final int change, final boolean inProgress) {
    if ((change == WINDOW_OPEN || change == WINDOW_TRANSFER_IN) && !inProgress) {
      mTextInput.onWindowChanged(mWindow);
    }
    if ((change == WINDOW_CLOSE || change == WINDOW_TRANSFER_OUT) && !inProgress) {
      getAutofillSupport().clear();
    }
  }

  /**
   * Get the SessionTextInput instance for this session. May be called on any thread.
   *
   * @return SessionTextInput instance.
   */
  @AnyThread
  public @NonNull SessionTextInput getTextInput() {
    // May be called on any thread.
    return mTextInput;
  }

  /**
   * Get the HapticFeedbackController instance for this session.
   *
   * @return HapticFeedbackController instance.
   */
  @UiThread
  /* package */ @NonNull
  HapticFeedbackController getHapticFeedbackController() {
    return mHapticFeedbackController;
  }

  /**
   * Get the SessionAccessibility instance for this session.
   *
   * @return SessionAccessibility instance.
   */
  @UiThread
  public @NonNull SessionAccessibility getAccessibility() {
    ThreadUtils.assertOnUiThread();
    if (mAccessibility != null) {
      return mAccessibility;
    }

    mAccessibility = new SessionAccessibility(this);
    if (mWindow != null) {
      if (GeckoThread.isStateAtLeast(GeckoThread.State.PROFILE_READY)) {
        mWindow.attachAccessibility(mAccessibility.nativeProvider);
      } else {
        GeckoThread.queueNativeCallUntil(
            GeckoThread.State.PROFILE_READY,
            mWindow,
            "attachAccessibility",
            SessionAccessibility.NativeProvider.class,
            mAccessibility.nativeProvider);
      }
    }
    return mAccessibility;
  }

  /**
   * Get the SessionMagnifier instance for this session.
   *
   * @return SessionMagnifier instance.
   */
  @UiThread
  /* package */ @NonNull
  SessionMagnifier getMagnifier() {
    ThreadUtils.assertOnUiThread();
    if (mMagnifier == null) {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
        mMagnifier = new SessionMagnifierP(mCompositor);
      } else {
        mMagnifier = new SessionMagnifier() {};
      }
    }

    return mMagnifier;
  }

  /** The priority of the GeckoSession, either default or high. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({PRIORITY_DEFAULT, PRIORITY_HIGH})
  public @interface Priority {}

  /** Value for Priority when it is default. */
  public static final int PRIORITY_DEFAULT = 0;

  /** Value for Priority when it is high. */
  public static final int PRIORITY_HIGH = 1;

  /** Load flags type definitions for nsIWebNavigation. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef(
      flag = true,
      value = {
        LOAD_FLAGS_NONE,
        LOAD_FLAGS_BYPASS_CACHE,
        LOAD_FLAGS_BYPASS_PROXY,
        LOAD_FLAGS_EXTERNAL,
        LOAD_FLAGS_ALLOW_POPUPS,
        LOAD_FLAGS_FORCE_ALLOW_DATA_URI,
        LOAD_FLAGS_REPLACE_HISTORY,
        LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE,
      })
  public @interface LoadFlags {}

  // These flags follow similarly named ones in Gecko's nsIWebNavigation.idl
  // https://searchfox.org/firefox-main/source/docshell/base/nsIWebNavigation.idl
  //
  // We do not use the same values directly in order to insulate ourselves from
  // changes in Gecko. Instead, the flags are converted in GeckoViewNavigation.sys.mjs.

  /** Default load flag, no special considerations. */
  public static final int LOAD_FLAGS_NONE = 0;

  /** Bypass the cache. */
  public static final int LOAD_FLAGS_BYPASS_CACHE = 1 << 0;

  /** Bypass the proxy, if one has been configured. */
  public static final int LOAD_FLAGS_BYPASS_PROXY = 1 << 1;

  /** The load is coming from an external app. Perform additional checks. */
  public static final int LOAD_FLAGS_EXTERNAL = 1 << 2;

  /** Popup blocking will be disabled for this load */
  public static final int LOAD_FLAGS_ALLOW_POPUPS = 1 << 3;

  /** Bypass the URI classifier (content blocking and Safe Browsing). */
  public static final int LOAD_FLAGS_BYPASS_CLASSIFIER = 1 << 4;

  /**
   * Allows a top-level data: navigation to occur. E.g. view-image is an explicit user action which
   * should be allowed.
   */
  public static final int LOAD_FLAGS_FORCE_ALLOW_DATA_URI = 1 << 5;

  /** This flag specifies that any existing history entry should be replaced. */
  public static final int LOAD_FLAGS_REPLACE_HISTORY = 1 << 6;

  /** This load should bypass the NavigationDelegate.onLoadRequest. */
  public static final int LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE = 1 << 7;

  /**
   * Filter headers according to the CORS safelisted rules.
   *
   * <p>See <a
   * href="https://developer.mozilla.org/en-US/docs/Glossary/CORS-safelisted_request_header">
   * CORS-safelisted request header </a>.
   */
  public static final int HEADER_FILTER_CORS_SAFELISTED = 1;

  /**
   * Allows most headers.
   *
   * <p>Note: the <code>Host</code> and <code>Connection</code> headers are still ignored.
   *
   * <p>This should only be used when input is hard-coded from the app or when properly sanitized,
   * as some headers could cause unexpected consequences and security issues.
   *
   * <p>Only use this if you know what you're doing.
   */
  public static final int HEADER_FILTER_UNRESTRICTED_UNSAFE = 2;

  /** Keep in sync with GeckoViewNavigation.java */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef(value = {HEADER_FILTER_CORS_SAFELISTED, HEADER_FILTER_UNRESTRICTED_UNSAFE})
  public @interface HeaderFilter {}

  /**
   * App link intent's launch type for cold start, i.e. the app process was not running when the
   * intent arrived.
   */
  public static final int APP_LINK_LAUNCH_TYPE_COLD = 1;

  /**
   * App link intent's launch type for warm start, i.e. the app process was alive, but the system
   * created a new Activity instance to handle the intent.
   */
  public static final int APP_LINK_LAUNCH_TYPE_WARM = 2;

  /**
   * App link intent's launch type for hot start, i.e. the intent was delivered to an existing
   * activity instance; no new activity was created.
   */
  public static final int APP_LINK_LAUNCH_TYPE_HOT = 3;

  /**
   * App link intent's launch type for unknown state, i.e. the launch type is not applicable or not
   * yet determined.
   */
  public static final int APP_LINK_LAUNCH_TYPE_UNKNOWN = 0;

  /** App link launch type constants indicating how the app was started by the external intent. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef(
      value = {
        APP_LINK_LAUNCH_TYPE_COLD,
        APP_LINK_LAUNCH_TYPE_WARM,
        APP_LINK_LAUNCH_TYPE_HOT,
        APP_LINK_LAUNCH_TYPE_UNKNOWN,
      })
  public @interface AppLinkLaunchType {}

  /**
   * Main entry point for loading URIs into a {@link GeckoSession}.
   *
   * <p>The simplest use case is loading a URIs with no extra options, this can be accomplished by
   * specifying the URI in {@link #uri} and then calling {@link #load}, e.g.
   *
   * <pre><code>
   *     session.load(new Loader().uri("http://mozilla.org"));
   * </code></pre>
   *
   * This class can also be used to load <code>data:</code> URIs, either from a <code>byte[]</code>
   * array or a <code>String</code> using {@link #data}, e.g.
   *
   * <pre><code>
   *     session.load(new Loader().data("the data:1234,5678", "text/plain"));
   * </code></pre>
   *
   * This class also allows you to specify some extra data, e.g. you can set a referrer using {@link
   * #referrer} which can either be a {@link GeckoSession} or a plain URL string. You can also
   * specify some Load Flags using {@link #flags}.
   *
   * <p>The class is structured as a Builder, so method calls can be easily chained, e.g.
   *
   * <pre><code>
   *     session.load(new Loader()
   *          .url("http://mozilla.org")
   *          .referrer("http://my-referrer.com")
   *          .flags(...));
   * </code></pre>
   */
  @AnyThread
  public static class Loader {
    private String mUri;
    private GeckoSession mReferrerSession;
    private String mReferrerUri;
    private GeckoBundle mHeaders;
    private @LoadFlags int mLoadFlags = LOAD_FLAGS_NONE;
    private boolean mIsDataUri;
    private @HeaderFilter int mHeaderFilter = HEADER_FILTER_CORS_SAFELISTED;
    private @Nullable String mOriginalInput;
    private boolean mTextDirectiveUserActivation;
    private @AppLinkLaunchType int mAppLinkLaunchType = APP_LINK_LAUNCH_TYPE_UNKNOWN;

    private static @NonNull String createDataUri(
        @NonNull final byte[] bytes, @Nullable final String mimeType) {
      return String.format(
          "data:%s;base64,%s",
          mimeType != null ? mimeType : "", Base64.encodeToString(bytes, Base64.NO_WRAP));
    }

    private static @NonNull String createDataUri(
        @NonNull final String data, @Nullable final String mimeType) {
      return String.format("data:%s,%s", mimeType != null ? mimeType : "", data);
    }

    @Override
    public int hashCode() {
      // Move to Objects.hashCode once our MIN_SDK >= 19
      return Arrays.hashCode(
          new Object[] {
            mUri, mReferrerSession, mReferrerUri, mHeaders, mLoadFlags, mIsDataUri, mHeaderFilter
          });
    }

    private static boolean equals(final Object a, final Object b) {
      return Objects.equals(a, b);
    }

    @Override
    public boolean equals(final @Nullable Object obj) {
      if (!(obj instanceof Loader)) {
        return false;
      }

      final Loader other = (Loader) obj;
      return equals(mUri, other.mUri)
          && equals(mReferrerSession, other.mReferrerSession)
          && equals(mReferrerUri, other.mReferrerUri)
          && equals(mHeaders, other.mHeaders)
          && equals(mLoadFlags, other.mLoadFlags)
          && equals(mIsDataUri, other.mIsDataUri)
          && equals(mHeaderFilter, other.mHeaderFilter)
          && equals(mTextDirectiveUserActivation, other.mTextDirectiveUserActivation);
    }

    /**
     * Set the URI of the resource to load.
     *
     * @param uri a String containg the URI
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader uri(final @NonNull String uri) {
      mUri = uri;
      mIsDataUri = false;
      return this;
    }

    /**
     * Set the URI of the resource to load.
     *
     * @param uri a {@link Uri} instance
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader uri(final @NonNull Uri uri) {
      mUri = uri.toString();
      mIsDataUri = false;
      return this;
    }

    /**
     * Set the data URI of the resource to load.
     *
     * @param bytes a <code>byte</code> array containing the data to load.
     * @param mimeType a <code>String</code> containing the mime type for this data URI, e.g.
     *     "text/plain"
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader data(final @NonNull byte[] bytes, final @Nullable String mimeType) {
      mUri = createDataUri(bytes, mimeType);
      mIsDataUri = true;
      return this;
    }

    /**
     * Set the data URI of the resource to load.
     *
     * @param data a <code>String</code> array containing the data to load.
     * @param mimeType a <code>String</code> containing the mime type for this data URI, e.g.
     *     "text/plain"
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader data(final @NonNull String data, final @Nullable String mimeType) {
      mUri = createDataUri(data, mimeType);
      mIsDataUri = true;
      return this;
    }

    /**
     * Set the referrer for this load.
     *
     * @param referrer a <code>GeckoSession</code> that will be used as the referrer
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader referrer(final @NonNull GeckoSession referrer) {
      mReferrerSession = referrer;
      return this;
    }

    /**
     * Set the referrer for this load.
     *
     * @param referrerUri a {@link Uri} that will be used as the referrer
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader referrer(final @NonNull Uri referrerUri) {
      mReferrerUri = referrerUri != null ? referrerUri.toString() : null;
      return this;
    }

    /**
     * Set the referrer for this load.
     *
     * @param referrerUri a <code>String</code> containing the URI that will be used as the referrer
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader referrer(final @NonNull String referrerUri) {
      mReferrerUri = referrerUri;
      return this;
    }

    /**
     * Add headers for this load.
     *
     * <p>Note: only CORS safelisted headers are allowed by default. To modify this behavior use
     * {@link #headerFilter}.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Glossary/CORS-safelisted_request_header">
     * CORS-safelisted request header </a>.
     *
     * @param headers a <code>Map</code> containing headers that will be added to this load.
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader additionalHeaders(final @NonNull Map<String, String> headers) {
      final GeckoBundle bundle = new GeckoBundle(headers.size());
      for (final Map.Entry<String, String> entry : headers.entrySet()) {
        if (entry.getKey() == null) {
          // Ignore null keys
          continue;
        }
        bundle.putString(entry.getKey(), entry.getValue());
      }
      mHeaders = bundle;
      return this;
    }

    /**
     * Modify the header filter behavior. By default only CORS safelisted headers are allowed.
     *
     * @param filter one of the {@link GeckoSession#HEADER_FILTER_CORS_SAFELISTED HEADER_FILTER_*}
     *     constants.
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader headerFilter(final @HeaderFilter int filter) {
      mHeaderFilter = filter;
      return this;
    }

    /**
     * Set the load flags for this load.
     *
     * @param flags the load flags to use, an OR-ed value of {@link #LOAD_FLAGS_NONE LOAD_FLAGS_*}
     *     that will be used as the referrer
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader flags(final @LoadFlags int flags) {
      mLoadFlags = flags;
      return this;
    }

    /**
     * If this load originates from the address bar, sets the original user input before it got
     * fixed up to a URI.
     *
     * @param originalInput original user address bar input.
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader originalInput(final @Nullable String originalInput) {
      mOriginalInput = originalInput;
      return this;
    }

    /**
     * Set the text directive user activation for the document opened in the window.
     *
     * @param textDirectiveUserActivation true if the first text directive wants to be scrolled into
     *     view.
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader textDirectiveUserActivation(final boolean textDirectiveUserActivation) {
      mTextDirectiveUserActivation = textDirectiveUserActivation;
      return this;
    }

    /**
     * Set the app link intent launch type for this load.
     *
     * @param appLinkLaunchType the app link intent launch type to use, one of the {@link
     *     #APP_LINK_LAUNCH_TYPE_UNKNOWN APP_LINK_LAUNCH_TYPE_*}. For the loads other than app link
     *     intents, the default {@code APP_LINK_LAUNCH_TYPE_UNKNOWN} is used.
     * @return this {@link Loader} instance.
     */
    @NonNull
    public Loader appLinkLaunchType(final @AppLinkLaunchType int appLinkLaunchType) {
      mAppLinkLaunchType = appLinkLaunchType;
      return this;
    }
  }

  /**
   * Load page using the {@link Loader} specified.
   *
   * @param request Loader for this request.
   * @see Loader
   */
  @AnyThread
  public void load(final @NonNull Loader request) {
    if (request.mUri == null) {
      throw new IllegalArgumentException(
          "You need to specify at least one between `uri` and `data`.");
    }

    if (request.mReferrerUri != null && request.mReferrerSession != null) {
      throw new IllegalArgumentException(
          "Cannot specify both a referrer session and a referrer URI.");
    }

    final NavigationDelegate navDelegate = mNavigationHandler.getDelegate();
    final boolean isDataUriTooLong = !maybeCheckDataUriLength(request);
    if (navDelegate == null && isDataUriTooLong) {
      throw new IllegalArgumentException("data URI is too long");
    }

    final int loadFlags =
        request.mIsDataUri
            // If this is a data: load then we need to force allow it.
            ? request.mLoadFlags | LOAD_FLAGS_FORCE_ALLOW_DATA_URI
            : request.mLoadFlags;

    // For performance reasons we short-circuit the delegate here
    // instead of making Gecko call it for direct load calls.
    final NavigationDelegate.LoadRequest loadRequest =
        new NavigationDelegate.LoadRequest(
            request.mUri,
            null, /* triggerUri */
            1, /* geckoTarget: OPEN_CURRENTWINDOW */
            0, /* flags */
            false, /* hasUserGesture */
            true /* isDirectNavigation */);

    shouldLoadUri(loadRequest, loadFlags)
        .getOrAccept(
            allowOrDeny -> {
              if (allowOrDeny == AllowOrDeny.DENY) {
                return;
              }

              if (isDataUriTooLong) {
                ThreadUtils.runOnUiThread(
                    () -> {
                      navDelegate.onLoadError(
                          this,
                          request.mUri,
                          new WebRequestError(
                              WebRequestError.ERROR_DATA_URI_TOO_LONG,
                              WebRequestError.ERROR_CATEGORY_URI,
                              null));
                    });
                return;
              }

              final GeckoBundle msg = new GeckoBundle();
              msg.putString("uri", request.mUri);
              msg.putInt("flags", loadFlags);
              msg.putInt("headerFilter", request.mHeaderFilter);
              msg.putBoolean("textDirectiveUserActivation", request.mTextDirectiveUserActivation);

              if (request.mReferrerUri != null) {
                msg.putString("referrerUri", request.mReferrerUri);
              }

              if (request.mReferrerSession != null) {
                msg.putString("referrerSessionId", request.mReferrerSession.mId);
              }

              if (request.mHeaders != null) {
                msg.putBundle("headers", request.mHeaders);
              }

              if (request.mOriginalInput != null) {
                msg.putString("originalInput", request.mOriginalInput);
              }

              msg.putInt("appLinkLaunchType", request.mAppLinkLaunchType);

              mEventDispatcher.dispatch("GeckoView:LoadUri", msg);
            });
  }

  /**
   * Load the given URI.
   *
   * <p>Convenience method for
   *
   * <pre><code>
   *     session.load(new Loader().uri(uri));
   * </code></pre>
   *
   * @param uri The URI of the resource to load.
   */
  @AnyThread
  public void loadUri(final @NonNull String uri) {
    load(new Loader().uri(uri));
  }

  private GeckoResult<AllowOrDeny> shouldLoadUri(
      final NavigationDelegate.LoadRequest request, final int loadFlags) {
    final NavigationDelegate delegate = mNavigationHandler.getDelegate();
    if (delegate == null || (loadFlags & LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE) != 0) {
      return GeckoResult.allow();
    }

    // Always run the callback on the UI thread regardless of what thread we were called in.
    final GeckoResult<AllowOrDeny> result = new GeckoResult<>(ThreadUtils.getUiHandler());

    ThreadUtils.runOnUiThread(
        () -> {
          final GeckoResult<AllowOrDeny> delegateResult = delegate.onLoadRequest(this, request);

          if (delegateResult == null) {
            result.complete(AllowOrDeny.ALLOW);
          } else {
            delegateResult.getOrAccept(
                allowOrDeny -> result.complete(allowOrDeny),
                error -> result.completeExceptionally(error));
          }
        });

    return result;
  }

  /** Reload the current URI. */
  @AnyThread
  public void reload() {
    reload(LOAD_FLAGS_NONE);
  }

  /**
   * Reload the current URI.
   *
   * @param flags the load flags to use, an OR-ed value of {@link #LOAD_FLAGS_NONE LOAD_FLAGS_*}
   */
  @AnyThread
  public void reload(final @LoadFlags int flags) {
    final GeckoBundle msg = new GeckoBundle();
    msg.putInt("flags", flags);
    mEventDispatcher.dispatch("GeckoView:Reload", msg);
  }

  /** Stop loading. */
  @AnyThread
  public void stop() {
    mEventDispatcher.dispatch("GeckoView:Stop", null);
  }

  /**
   * Go back in history and assumes the call was based on a user interaction.
   *
   * @see #goBack(boolean)
   */
  @AnyThread
  public void goBack() {
    goBack(true);
  }

  /**
   * Go back in history.
   *
   * @param userInteraction Whether the action was invoked by a user interaction.
   */
  @AnyThread
  public void goBack(final boolean userInteraction) {
    final GeckoBundle msg = new GeckoBundle(1);
    msg.putBoolean("userInteraction", userInteraction);
    mEventDispatcher.dispatch("GeckoView:GoBack", msg);
  }

  /**
   * Go forward in history and assumes the call was based on a user interaction.
   *
   * @see #goForward(boolean)
   */
  @AnyThread
  public void goForward() {
    goForward(true);
  }

  /**
   * Go forward in history.
   *
   * @param userInteraction Whether the action was invoked by a user interaction.
   */
  @AnyThread
  public void goForward(final boolean userInteraction) {
    final GeckoBundle msg = new GeckoBundle(1);
    msg.putBoolean("userInteraction", userInteraction);
    mEventDispatcher.dispatch("GeckoView:GoForward", msg);
  }

  /**
   * Navigate to an index in browser history; the index of the currently viewed page can be
   * retrieved from an up-to-date HistoryList by calling {@link
   * HistoryDelegate.HistoryList#getCurrentIndex()}.
   *
   * @param index The index of the location in browser history you want to navigate to.
   */
  @AnyThread
  public void gotoHistoryIndex(final int index) {
    final GeckoBundle msg = new GeckoBundle(1);
    msg.putInt("index", index);
    mEventDispatcher.dispatch("GeckoView:GotoHistoryIndex", msg);
  }

  /**
   * Determine if the current page uses a qualified website authentication certificate (QWAC).
   *
   * @return A {@link GeckoResult} which holds the QWAC as an {@link X509Certificate}, or null if
   *     the site does not use one.
   */
  @UiThread
  public @NonNull GeckoResult<X509Certificate> qwacStatus() {
    return mEventDispatcher
        .queryString("GeckoView:GetQWACStatus")
        .map(
            qwacPem -> {
              X509Certificate qwac = null;
              if (qwacPem != null) {
                try {
                  final CertificateFactory factory = CertificateFactory.getInstance("X.509");
                  final byte[] qwacBytes = Base64.decode(qwacPem, Base64.NO_WRAP);
                  qwac =
                      (X509Certificate)
                          factory.generateCertificate(new ByteArrayInputStream(qwacBytes));
                } catch (final CertificateException e) {
                  Log.e(LOGTAG, "Failed to decode certificate", e);
                }
              }
              return qwac;
            });
  }

  /**
   * Returns a WebExtensionController for this GeckoSession. Delegates attached to this controller
   * will receive events specific to this session.
   *
   * @return an instance of {@link WebExtension.SessionController}.
   */
  @UiThread
  public @NonNull WebExtension.SessionController getWebExtensionController() {
    return mWebExtensionController;
  }

  /**
   * Purge history for the session. The session history is used for back and forward history.
   * Purging the session history means {@link NavigationDelegate#onCanGoBack(GeckoSession, boolean)}
   * and {@link NavigationDelegate#onCanGoForward(GeckoSession, boolean)} will be false.
   */
  @AnyThread
  public void purgeHistory() {
    mEventDispatcher.dispatch("GeckoView:PurgeHistory", null);
  }

  /** Finder search flag definitions for find-in-page operations. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef(
      flag = true,
      value = {
        FINDER_FIND_FORWARD,
        FINDER_FIND_BACKWARDS,
        FINDER_FIND_LINKS_ONLY,
        FINDER_FIND_MATCH_CASE,
        FINDER_FIND_WHOLE_WORD
      })
  public @interface FinderFindFlags {}

  /** Go forward when finding the next match. */
  public static final int FINDER_FIND_FORWARD = 0;

  /** Go backwards when finding the next match. */
  public static final int FINDER_FIND_BACKWARDS = 1;

  /** Perform case-sensitive match; default is to perform a case-insensitive match. */
  public static final int FINDER_FIND_MATCH_CASE = 1 << 1;

  /** Must match entire words; default is to allow matching partial words. */
  public static final int FINDER_FIND_WHOLE_WORD = 1 << 2;

  /** Limit matches to links on the page. */
  public static final int FINDER_FIND_LINKS_ONLY = 1 << 3;

  /** Finder display flag definitions for find-in-page display options. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef(
      flag = true,
      value = {
        FINDER_DISPLAY_HIGHLIGHT_ALL,
        FINDER_DISPLAY_DIM_PAGE,
        FINDER_DISPLAY_DRAW_LINK_OUTLINE
      })
  public @interface FinderDisplayFlags {}

  /** Highlight all find-in-page matches. */
  public static final int FINDER_DISPLAY_HIGHLIGHT_ALL = 1;

  /** Dim the rest of the page when showing a find-in-page match. */
  public static final int FINDER_DISPLAY_DIM_PAGE = 1 << 1;

  /** Draw outlines around matching links. */
  public static final int FINDER_DISPLAY_DRAW_LINK_OUTLINE = 1 << 2;

  /** Represent the result of a find-in-page operation. */
  @AnyThread
  public static class FinderResult {
    /** Whether a match was found. */
    public final boolean found;

    /** Whether the search wrapped around the top or bottom of the page. */
    public final boolean wrapped;

    /** Ordinal number of the current match starting from 1, or 0 if no match. */
    public final int current;

    /** Total number of matches found so far, or -1 if unknown. */
    public final int total;

    /** Search string. */
    @NonNull public final String searchString;

    /**
     * Flags used for the search; either 0 or a combination of {@link #FINDER_FIND_BACKWARDS
     * FINDER_FIND_*} flags.
     */
    @FinderFindFlags public final int flags;

    /** URI of the link, if the current match is a link, or null otherwise. */
    @Nullable public final String linkUri;

    /** Bounds of the current match in client coordinates, or null if unknown. */
    @Nullable public final RectF clientRect;

    /* package */ FinderResult(@NonNull final GeckoBundle bundle) {
      found = bundle.getBoolean("found");
      wrapped = bundle.getBoolean("wrapped");
      current = bundle.getInt("current", 0);
      total = bundle.getInt("total", -1);
      searchString = bundle.getString("searchString");
      flags = SessionFinder.getFlagsFromBundle(bundle.getBundle("flags"));
      linkUri = bundle.getString("linkURL");
      clientRect = bundle.getRectF("clientRect");
    }

    /** Empty constructor for tests */
    protected FinderResult() {
      found = false;
      wrapped = false;
      current = 0;
      total = 0;
      flags = 0;
      searchString = "";
      linkUri = "";
      clientRect = null;
    }
  }

  /**
   * Get the SessionFinder instance for this session, to perform find-in-page operations.
   *
   * @return SessionFinder instance.
   */
  @AnyThread
  public @NonNull SessionFinder getFinder() {
    if (mFinder == null) {
      mFinder = new SessionFinder(getEventDispatcher());
    }
    return mFinder;
  }

  /**
   * Checks whether we have a rule for this session. Uses the browsing context or any of its
   * children, calls nsICookieBannerService.hasRuleForBrowsingContextTree
   *
   * @return {@link GeckoResult} with boolean
   */
  @HandlerThread
  public @NonNull GeckoResult<Boolean> hasCookieBannerRuleForBrowsingContextTree() {
    ThreadUtils.assertOnHandlerThread();
    return mEventDispatcher.queryBoolean("GeckoView:HasCookieBannerRuleForBrowsingContextTree");
  }

  /**
   * Get the SessionPdfFileSaver instance for this session, to save a pdf document.
   *
   * @return SessionPdfFileSaver instance.
   */
  @AnyThread
  public @NonNull SessionPdfFileSaver getPdfFileSaver() {
    if (mPdfFileSaver == null) {
      mPdfFileSaver = new SessionPdfFileSaver(this);
    }
    return mPdfFileSaver;
  }

  /** Represent the result of a save-pdf operation. */
  @AnyThread
  public static class PdfSaveResult {
    /** Binary data representing a PDF. */
    @NonNull public final byte[] bytes;

    /** PDF file name. */
    @NonNull public final String filename;

    /** True if this PDF was generated in a private‐browsing session. */
    public final boolean isPrivate;

    /* package */ PdfSaveResult(@NonNull final GeckoBundle bundle) {
      filename = bundle.getString("filename");
      isPrivate = bundle.getBoolean("isPrivate");
      bytes = bundle.getByteArray("bytes");
    }

    /** Empty constructor for tests */
    protected PdfSaveResult() {
      filename = "";
      isPrivate = false;
      bytes = new byte[0];
    }
  }

  /**
   * Check if the document being viewed is a pdf.
   *
   * @return Result of the check operation as a {@link GeckoResult} object.
   */
  @HandlerThread
  public @NonNull GeckoResult<Boolean> isPdfJs() {
    ThreadUtils.assertOnHandlerThread();
    return mEventDispatcher.queryBoolean("GeckoView:IsPdfJs");
  }

  /**
   * Flushes the current {@link GeckoSession} state. This method triggers an asynchronous operation
   * to flush the session state maintained by Gecko. The flush ensures that the most recent session
   * data (such as navigation history and other stateful information) is captured and made available
   * for persistence or inspection. Note: Since the operation is asynchronous, the flush may not
   * complete immediately after this method returns.
   */
  @AnyThread
  public void flushSessionState() {
    mEventDispatcher.dispatch("GeckoView:FlushSessionState", null);
  }

  /**
   * Set this GeckoSession as active or inactive, which represents if the session is currently
   * visible or not. Setting a GeckoSession to inactive will significantly reduce its memory
   * footprint, but should only be done if the GeckoSession is not currently visible. Note that a
   * session can be active (i.e. visible) but not focused. When a session is set inactive, it will
   * flush the session state and trigger a `ProgressDelegate.onSessionStateChange` callback.
   *
   * @param active A boolean determining whether the GeckoSession is active.
   * @see #setFocused
   */
  @AnyThread
  public void setActive(final boolean active) {
    final GeckoBundle msg = new GeckoBundle(1);
    msg.putBoolean("active", active);
    mEventDispatcher.dispatch("GeckoView:SetActive", msg);

    if (!active) {
      flushSessionState();
      ThreadUtils.postToUiThreadDelayed(mNotifyMemoryPressure, NOTIFY_MEMORY_PRESSURE_DELAY_MS);
    } else {
      // Delete any pending memory pressure events since we're active again.
      ThreadUtils.removeUiThreadCallbacks(mNotifyMemoryPressure);

      if (mAttachedCompositor) {
        mCompositor.onKeyboardHeightChanged(mKeyboardHeight);
      }
    }

    ThreadUtils.runOnUiThread(() -> getAutofillSupport().onActiveChanged(active));
  }

  /**
   * Move focus to this session or away from this session. Only one session has focus at a given
   * time. Note that a session can be unfocused but still active (i.e. visible).
   *
   * @param focused True if the session should gain focus or false if the session should lose focus.
   * @see #setActive
   */
  @AnyThread
  public void setFocused(final boolean focused) {
    mEventDispatcher.dispatch("GeckoView:DismissClipboardPermissionRequest", null);

    final GeckoBundle msg = new GeckoBundle(1);
    msg.putBoolean("focused", focused);
    mEventDispatcher.dispatch("GeckoView:SetFocused", msg);
  }

  /**
   * Notify GeckoView of the priority for this GeckoSession.
   *
   * <p>Set this GeckoSession to high priority (PRIORITY_HIGH) whenever the app wants to signal to
   * GeckoView that this GeckoSession is important to the app. GeckoView will keep the session state
   * as long as possible. Set this to default priority (PRIORITY_DEFAULT) in any other case.
   *
   * @param priorityHint Priority of the geckosession, either high priority or default.
   */
  @AnyThread
  public void setPriorityHint(final @Priority int priorityHint) {
    final GeckoBundle msg = new GeckoBundle(1);
    msg.putInt("priorityHint", priorityHint);
    mEventDispatcher.dispatch("GeckoView:SetPriorityHint", msg);
  }

  /** Class representing a saved session state. */
  @AnyThread
  public static class SessionState extends AbstractSequentialList<HistoryDelegate.HistoryItem>
      implements HistoryDelegate.HistoryList, Parcelable {
    private GeckoBundle mState;

    private class SessionStateItem implements HistoryDelegate.HistoryItem {
      private final GeckoBundle mItem;

      private SessionStateItem(final @NonNull GeckoBundle item) {
        mItem = item;
      }

      @Override /* HistoryItem */
      public String getUri() {
        return mItem.getString("url");
      }

      @Override /* HistoryItem */
      public String getTitle() {
        return mItem.getString("title");
      }
    }

    private class SessionStateIterator implements ListIterator<HistoryDelegate.HistoryItem> {
      private final SessionState mState;
      private int mIndex;

      private SessionStateIterator(final @NonNull SessionState state) {
        this(state, 0);
      }

      private SessionStateIterator(final @NonNull SessionState state, final int index) {
        mIndex = index;
        mState = state;
      }

      @Override /* ListIterator */
      public void add(final HistoryDelegate.HistoryItem item) {
        throw new UnsupportedOperationException();
      }

      @Override /* ListIterator */
      public boolean hasNext() {
        final GeckoBundle[] entries = mState.getHistoryEntries();

        if (entries == null) {
          Log.w(LOGTAG, "No history entries found.");
          return false;
        }

        return mIndex < mState.getHistoryEntries().length;
      }

      @Override /* ListIterator */
      public boolean hasPrevious() {
        return mIndex > 0;
      }

      @Override /* ListIterator */
      public HistoryDelegate.HistoryItem next() {
        if (hasNext()) {
          mIndex++;
          return new SessionStateItem(mState.getHistoryEntries()[mIndex - 1]);
        } else {
          throw new NoSuchElementException();
        }
      }

      @Override /* ListIterator */
      public int nextIndex() {
        return mIndex;
      }

      @Override /* ListIterator */
      public HistoryDelegate.HistoryItem previous() {
        if (hasPrevious()) {
          mIndex--;
          return new SessionStateItem(mState.getHistoryEntries()[mIndex]);
        } else {
          throw new NoSuchElementException();
        }
      }

      @Override /* ListIterator */
      public int previousIndex() {
        return mIndex - 1;
      }

      @Override /* ListIterator */
      public void remove() {
        throw new UnsupportedOperationException();
      }

      @Override /* ListIterator */
      public void set(final @NonNull HistoryDelegate.HistoryItem item) {
        throw new UnsupportedOperationException();
      }
    }

    private SessionState() {
      mState = new GeckoBundle(3);
    }

    private SessionState(final @NonNull GeckoBundle state) {
      mState = new GeckoBundle(state);
    }

    /**
     * Copy constructor for SessionState.
     *
     * @param state the SessionState instance to duplicate.
     */
    public SessionState(final @NonNull SessionState state) {
      mState = new GeckoBundle(state.mState);
    }

    /* package */ void updateSessionState(final @NonNull GeckoBundle updateData) {
      if (updateData == null) {
        Log.w(LOGTAG, "Session state update has no data field.");
        return;
      }

      final GeckoBundle history = updateData.getBundle("historychange");
      final GeckoBundle scroll = updateData.getBundle("scroll");
      final GeckoBundle formdata = updateData.getBundle("formdata");

      if (history != null) {
        // when full session history update received, don't bother with partial state update ops.
        // This is due to the suboptimal array ops in the partial update logic which regresses
        // thread cpuTime while the legacy bundle operation performs better.
        if (history.getInt("fromIdx") == -1) {
          mState.putBundle("history", history);
        } else {
          mState.putBundle(
              "history", getPartiallyUpdatedHistoryChange(history).getBundle("history"));
        }
      }

      if (scroll != null) {
        mState.putBundle("scrolldata", scroll);
      }

      if (formdata != null) {
        mState.putBundle("formdata", formdata);
      }

      return;
    }

    private @NonNull GeckoBundle getPartiallyUpdatedHistoryChange(
        final @NonNull GeckoBundle update) {
      final int kLastIndex = Integer.MAX_VALUE - 1;
      final GeckoBundle historyBundle = new GeckoBundle();
      final GeckoBundle[] updateHistoryEntries = update.getBundleArray("entries");
      final int updateFromIdx = update.getInt("fromIdx");

      // start off with an empty session entries array and
      // then populate it with the updated session entries
      update.putBundleArray("entries", new GeckoBundle[] {});

      // no need to store fromIdx in the history state bundle as it has nothing to do with it
      update.remove("fromIdx");

      historyBundle.putBundle("history", update);

      if (updateFromIdx != kLastIndex) {
        final int start = updateFromIdx + 1;
        historyBundle
            .getBundle("history")
            .putBundleArray("entries", spliceSessionHistory(start, updateHistoryEntries));
      }
      return historyBundle;
    }

    private GeckoBundle[] spliceSessionHistory(final int startIndex, final GeckoBundle[] entries) {
      final GeckoBundle[] historyEntries = getHistoryEntries();
      if (historyEntries != null) {
        // when a partial history update received, delete the session entries starting from
        // startIndex then append the new session entries to the end.
        final GeckoBundle[] newHistoryEntries = new GeckoBundle[startIndex + entries.length];
        System.arraycopy(historyEntries, 0, newHistoryEntries, 0, startIndex);
        System.arraycopy(entries, 0, newHistoryEntries, startIndex, entries.length);
        return newHistoryEntries;
      }
      return new GeckoBundle[] {};
    }

    @Override
    public int hashCode() {
      return mState.hashCode();
    }

    @Override
    public boolean equals(final Object other) {
      if (!(other instanceof SessionState)) {
        return false;
      }

      final SessionState otherState = (SessionState) other;

      return this.mState.equals(otherState.mState);
    }

    /**
     * Creates a new SessionState instance from a value previously returned by {@link #toString()}.
     *
     * @param value The serialized SessionState in String form.
     * @return A new SessionState instance if input is valid; otherwise null.
     */
    public static @Nullable SessionState fromString(final @Nullable String value) {
      final GeckoBundle bundleState;
      try {
        bundleState = GeckoBundle.fromJSONObject(new JSONObject(value));
      } catch (final Exception e) {
        Log.e(LOGTAG, "String does not represent valid session state.");
        return null;
      }

      if (bundleState == null) {
        return null;
      }

      return new SessionState(bundleState);
    }

    @Override
    public @Nullable String toString() {
      if (mState == null) {
        Log.w(LOGTAG, "Can't convert SessionState with null state to string");
        return null;
      }

      String res;
      try {
        res = mState.toJSONObject().toString();
      } catch (final JSONException e) {
        Log.e(LOGTAG, "Could not convert session state to string.");
        res = null;
      }

      return res;
    }

    @Override // Parcelable
    public int describeContents() {
      return 0;
    }

    @Override // Parcelable
    public void writeToParcel(final Parcel dest, final int flags) {
      dest.writeString(toString());
    }

    /**
     * Reads session state from a Parcel. This may be called by AIDL code.
     *
     * @param source The Parcel containing the serialized session state.
     */
    public void readFromParcel(final @NonNull Parcel source) {
      if (source.readString() == null) {
        Log.w(LOGTAG, "Can't reproduce session state from Parcel");
      }

      try {
        mState = GeckoBundle.fromJSONObject(new JSONObject(source.readString()));
      } catch (final JSONException e) {
        Log.e(LOGTAG, "Could not convert string to session state.");
        mState = null;
      }
    }

    /** Creator used by the Android framework to deserialize SessionState from a Parcel. */
    public static final Parcelable.Creator<SessionState> CREATOR =
        new Parcelable.Creator<SessionState>() {
          @Override
          public SessionState createFromParcel(final Parcel source) {
            if (source.readString() == null) {
              Log.w(LOGTAG, "Can't create session state from Parcel");
            }

            GeckoBundle res;
            try {
              res = GeckoBundle.fromJSONObject(new JSONObject(source.readString()));
            } catch (final JSONException e) {
              Log.e(LOGTAG, "Could not convert parcel to session state.");
              res = null;
            }

            return new SessionState(res);
          }

          @Override
          public SessionState[] newArray(final int size) {
            return new SessionState[size];
          }
        };

    @Override /* AbstractSequentialList */
    public @NonNull HistoryDelegate.HistoryItem get(final int index) {
      final GeckoBundle[] entries = getHistoryEntries();

      if (entries == null || index < 0 || index >= entries.length) {
        throw new NoSuchElementException();
      }

      return new SessionStateItem(entries[index]);
    }

    @Override /* AbstractSequentialList */
    public @NonNull Iterator<HistoryDelegate.HistoryItem> iterator() {
      return listIterator(0);
    }

    @Override /* AbstractSequentialList */
    public @NonNull ListIterator<HistoryDelegate.HistoryItem> listIterator(final int index) {
      return new SessionStateIterator(this, index);
    }

    @Override /* AbstractSequentialList */
    public int size() {
      final GeckoBundle[] entries = getHistoryEntries();

      if (entries == null) {
        Log.w(LOGTAG, "No history entries found.");
        return 0;
      }

      return entries.length;
    }

    @Override /* HistoryList */
    public int getCurrentIndex() {
      final GeckoBundle history = getHistory();

      if (history == null) {
        throw new IllegalStateException("No history state exists.");
      }

      // The index for the array of session entries is 1-based,
      // so we subtract 1 to get the current index
      return history.getInt("index") - 1;
    }

    // Some helpers for common code.
    private GeckoBundle getHistory() {
      if (mState == null) {
        return null;
      }

      return mState.getBundle("history");
    }

    private GeckoBundle[] getHistoryEntries() {
      final GeckoBundle history = getHistory();

      if (history == null) {
        return null;
      }

      return history.getBundleArray("entries");
    }
  }

  private SessionState mStateCache = new SessionState();

  /**
   * Restore a saved state to this GeckoSession; only data that is saved (history, scroll position,
   * zoom, and form data) will be restored. These will overwrite the corresponding state of this
   * GeckoSession.
   *
   * @param state A saved session state; this should originate from onSessionStateChange().
   */
  @AnyThread
  public void restoreState(final @NonNull SessionState state) {
    mEventDispatcher.dispatch("GeckoView:RestoreState", state.mState);
  }

  /**
   * Get whether this GeckoSession has form data.
   *
   * @return a {@link GeckoResult} result of if there is existing form data.
   */
  @HandlerThread
  public @NonNull GeckoResult<Boolean> containsFormData() {
    ThreadUtils.assertOnHandlerThread();
    return mEventDispatcher.queryBoolean("GeckoView:ContainsFormData");
  }

  /**
   * Get the web compatibility info when a site is reported as broken.
   *
   * @return a {@link GeckoResult} containing the WebCompatInfo as a JSONObject.
   */
  @HandlerThread
  public @NonNull GeckoResult<JSONObject> getWebCompatInfo() {
    ThreadUtils.assertOnHandlerThread();
    return mEventDispatcher
        .queryString("GeckoView:GetWebCompatInfo")
        .map(
            value -> {
              if (value == null) {
                throw new IllegalStateException("Unable to get web compat info");
              }
              return new JSONObject(value);
            });
  }

  /**
   * Send more web compatibility info when a site is reported as broken.
   *
   * @param info A {@link JSONObject} containing the web compatibility report details. The expected
   *     format of the JSON object is:
   *     <pre><code>
   *             {
   *               "reason": "User-selected reason for reporting",
   *               "description": "User-provided description of the issue",
   *               "endpointUrl": "https://webcompat.com/issues/new",
   *               "reportUrl": "URL of the reported site",
   *               "reporterConfig": {
   *                 "src": "android-components-reporter",
   *                 "utm_campaign": "report-site-issue-button",
   *                 "utm_source": "android-components-reporter"
   *               },
   *               "webcompatInfo": { //JSONObject from GeckoSession.getWebCompatInfo() }
   *             }
   *             </code></pre>
   *
   * @return a {@link GeckoResult} wil complete if sending more web compatibility info was
   *     successful. Will complete exceptionally if the web compat info was not sent.
   */
  @HandlerThread
  public @NonNull GeckoResult<Void> sendMoreWebCompatInfo(@NonNull final JSONObject info) {
    ThreadUtils.warnOnHandlerThread();
    final GeckoBundle bundle = new GeckoBundle();
    bundle.putString("info", info.toString());
    return mEventDispatcher.queryVoid("GeckoView:SendMoreWebCompatInfo", bundle);
  }

  // This is the GeckoDisplay acquired via acquireDisplay(), if any.
  private GeckoDisplay mDisplay;

  /* package */ interface Owner {
    void onRelease();
  }

  private static final WeakReference<Owner> NO_OWNER = new WeakReference<>(null);
  private WeakReference<Owner> mOwner = NO_OWNER;

  @UiThread
  /* package */ void releaseOwner() {
    ThreadUtils.assertOnUiThread();
    mOwner = NO_OWNER;
  }

  @UiThread
  /* package */ void setOwner(final Owner owner) {
    ThreadUtils.assertOnUiThread();
    final Owner oldOwner = mOwner.get();
    if (oldOwner != null && owner != oldOwner) {
      oldOwner.onRelease();
    }
    mOwner = new WeakReference<>(owner);
  }

  /* package */ GeckoDisplay getDisplay() {
    return mDisplay;
  }

  /**
   * Acquire the GeckoDisplay instance for providing the session with a drawing Surface. Be sure to
   * call {@link GeckoDisplay#surfaceChanged(SurfaceInfo)} on the acquired display if there is
   * already a valid Surface.
   *
   * @return GeckoDisplay instance.
   * @see #releaseDisplay(GeckoDisplay)
   */
  @UiThread
  public @NonNull GeckoDisplay acquireDisplay() {
    ThreadUtils.assertOnUiThread();

    if (mDisplay != null) {
      throw new IllegalStateException("Display already acquired");
    }

    mDisplay = new GeckoDisplay(this);
    return mDisplay;
  }

  /**
   * Release an acquired GeckoDisplay instance. Be sure to call {@link
   * GeckoDisplay#surfaceDestroyed()} before releasing the display if it still has a valid Surface.
   *
   * @param display Acquired GeckoDisplay instance.
   * @see #acquireDisplay()
   */
  @UiThread
  public void releaseDisplay(final @NonNull GeckoDisplay display) {
    ThreadUtils.assertOnUiThread();

    if (display != mDisplay) {
      throw new IllegalArgumentException("Display not attached");
    }

    mDisplay = null;
  }

  /**
   * Returns the settings used by this GeckoSession.
   *
   * @return The non-null GeckoSessionSettings for this session.
   */
  @AnyThread
  public @NonNull GeckoSessionSettings getSettings() {
    return mSettings;
  }

  /** Exits fullscreen mode */
  @AnyThread
  public void exitFullScreen() {
    mEventDispatcher.dispatch("GeckoViewContent:ExitFullScreen", null);
  }

  /**
   * Set the content callback handler. This will replace the current handler.
   *
   * @param delegate An implementation of ContentDelegate.
   */
  @UiThread
  public void setContentDelegate(final @Nullable ContentDelegate delegate) {
    ThreadUtils.assertOnUiThread();
    mContentHandler.setDelegate(delegate, this);
    mProcessHangHandler.setDelegate(delegate, this);
  }

  /**
   * Get the content callback handler.
   *
   * @return The current content callback handler.
   */
  @UiThread
  public @Nullable ContentDelegate getContentDelegate() {
    ThreadUtils.assertOnUiThread();
    return mContentHandler.getDelegate();
  }

  /**
   * Set the progress callback handler. This will replace the current handler.
   *
   * @param delegate An implementation of ProgressDelegate.
   */
  @UiThread
  public void setProgressDelegate(final @Nullable ProgressDelegate delegate) {
    ThreadUtils.assertOnUiThread();
    mProgressHandler.setDelegate(delegate, this);
  }

  /**
   * Get the progress callback handler.
   *
   * @return The current progress callback handler.
   */
  @UiThread
  public @Nullable ProgressDelegate getProgressDelegate() {
    ThreadUtils.assertOnUiThread();
    return mProgressHandler.getDelegate();
  }

  /**
   * Set the navigation callback handler. This will replace the current handler.
   *
   * @param delegate An implementation of NavigationDelegate.
   */
  @UiThread
  public void setNavigationDelegate(final @Nullable NavigationDelegate delegate) {
    ThreadUtils.assertOnUiThread();
    mNavigationHandler.setDelegate(delegate, this);
  }

  /**
   * Get the navigation callback handler.
   *
   * @return The current navigation callback handler.
   */
  @UiThread
  public @Nullable NavigationDelegate getNavigationDelegate() {
    ThreadUtils.assertOnUiThread();
    return mNavigationHandler.getDelegate();
  }

  /**
   * Set the content scroll callback handler. This will replace the current handler.
   *
   * @param delegate An implementation of ScrollDelegate.
   */
  @UiThread
  public void setScrollDelegate(final @Nullable ScrollDelegate delegate) {
    ThreadUtils.assertOnUiThread();
    mScrollHandler.setDelegate(delegate, this);
  }

  /**
   * Get the current scroll callback handler.
   *
   * @return An implementation of ScrollDelegate.
   */
  @UiThread
  public @Nullable ScrollDelegate getScrollDelegate() {
    ThreadUtils.assertOnUiThread();
    return mScrollHandler.getDelegate();
  }

  /**
   * Set the compositor scroll callback handler. This will replace the current handler.
   *
   * @param delegate An implementation of CompositorScrollDelegate.
   */
  @UiThread
  public void setCompositorScrollDelegate(final @Nullable CompositorScrollDelegate delegate) {
    ThreadUtils.assertOnUiThread();
    if (mCompositorScrollDelegate == delegate) {
      return;
    }

    mCompositorScrollDelegate = delegate;

    // Notify the newly registered delegate immediately about the
    // most recent update, if there is one.
    if (mCompositorScrollDelegate != null && mLastScrollPositionUpdate != null) {
      mCompositorScrollDelegate.onScrollChanged(this, mLastScrollPositionUpdate);
    }
  }

  /**
   * Get the current compositor scroll callback handler.
   *
   * @return An implementation of CompositorScrollDelegate.
   */
  @UiThread
  public @Nullable CompositorScrollDelegate getCompositorScrollDelegate() {
    ThreadUtils.assertOnUiThread();
    return mCompositorScrollDelegate;
  }

  /**
   * Set the history tracking delegate for this session, replacing the current delegate if one is
   * set.
   *
   * @param delegate The history tracking delegate, or {@code null} to unset.
   */
  @AnyThread
  public void setHistoryDelegate(final @Nullable HistoryDelegate delegate) {
    mHistoryHandler.setDelegate(delegate, this);
  }

  /**
   * @return The history tracking delegate for this session.
   */
  @AnyThread
  public @Nullable HistoryDelegate getHistoryDelegate() {
    return mHistoryHandler.getDelegate();
  }

  /**
   * Set the content blocking callback handler. This will replace the current handler.
   *
   * @param delegate An implementation of {@link ContentBlocking.Delegate}.
   */
  @AnyThread
  public void setContentBlockingDelegate(final @Nullable ContentBlocking.Delegate delegate) {
    mContentBlockingHandler.setDelegate(delegate, this);
  }

  /**
   * Get the content blocking callback handler.
   *
   * @return The current content blocking callback handler.
   */
  @AnyThread
  public @Nullable ContentBlocking.Delegate getContentBlockingDelegate() {
    return mContentBlockingHandler.getDelegate();
  }

  /**
   * Set the current prompt delegate for this GeckoSession.
   *
   * @param delegate PromptDelegate instance or null to use the built-in delegate.
   */
  @AnyThread
  public void setPromptDelegate(final @Nullable PromptDelegate delegate) {
    mPromptDelegate = delegate;
  }

  /**
   * Get the current prompt delegate for this GeckoSession.
   *
   * @return PromptDelegate instance or null if using built-in delegate.
   */
  @AnyThread
  public @Nullable PromptDelegate getPromptDelegate() {
    return mPromptDelegate;
  }

  /**
   * Set the current selection action delegate for this GeckoSession.
   *
   * @param delegate SelectionActionDelegate instance or null to unset.
   */
  @UiThread
  public void setSelectionActionDelegate(final @Nullable SelectionActionDelegate delegate) {
    ThreadUtils.assertOnUiThread();

    if (getSelectionActionDelegate() != null) {
      // When the delegate is changed or cleared, make sure onHideAction is called
      // one last time to hide any existing selection action UI. Gecko doesn't keep
      // track of the old delegate, so we can't rely on Gecko to do that for us.
      getSelectionActionDelegate()
          .onHideAction(this, GeckoSession.SelectionActionDelegate.HIDE_REASON_NO_SELECTION);
    }
    mSelectionActionDelegate.setDelegate(delegate, this);
  }

  /**
   * Set the media callback handler. This will replace the current handler.
   *
   * @param delegate An implementation of MediaDelegate.
   */
  @AnyThread
  public void setMediaDelegate(final @Nullable MediaDelegate delegate) {
    mMediaHandler.setDelegate(delegate, this);
  }

  /**
   * Get the Media callback handler.
   *
   * @return The current Media callback handler.
   */
  @AnyThread
  public @Nullable MediaDelegate getMediaDelegate() {
    return mMediaHandler.getDelegate();
  }

  /**
   * Set the media session delegate. This will replace the current handler.
   *
   * @param delegate An implementation of {@link MediaSession.Delegate}.
   */
  @AnyThread
  public void setMediaSessionDelegate(final @Nullable MediaSession.Delegate delegate) {
    mMediaSessionHandler.setDelegate(delegate, this);
  }

  /**
   * Get the media session delegate.
   *
   * @return The current media session delegate.
   */
  @AnyThread
  public @Nullable MediaSession.Delegate getMediaSessionDelegate() {
    return mMediaSessionHandler.getDelegate();
  }

  /**
   * The session translation object coordinates receiving and sending session messages with the
   * translations toolkit. Notably, it can be used to request translations.
   *
   * @return The current translation session coordinator.
   */
  @AnyThread
  public @Nullable TranslationsController.SessionTranslation getSessionTranslation() {
    return mTranslations;
  }

  /**
   * Set the translation delegate, which receives translations events.
   *
   * @param delegate An implementation of @link{TranslationsController.SessionTranslation.Delegate}.
   */
  @AnyThread
  public void setTranslationsSessionDelegate(
      final @Nullable TranslationsController.SessionTranslation.Delegate delegate) {
    mTranslationsHandler.setDelegate(delegate, this);
  }

  /**
   * Get the translations delegate. The application embedder must initially set the translations
   * delegate for use.
   *
   * @return The current translations delegate.
   */
  @AnyThread
  public @Nullable TranslationsController.SessionTranslation.Delegate
      getTranslationsSessionDelegate() {
    return mTranslationsHandler.getDelegate();
  }

  /**
   * Get the page extractor for this GeckoSession.
   *
   * @return The current page extractor session coordinator.
   */
  @AnyThread
  @ExperimentalGeckoViewApi
  public @NonNull PageExtractionController.SessionPageExtractor getSessionPageExtractor() {
    if (mPageExtractor == null) {
      mPageExtractor = new PageExtractionController.SessionPageExtractor(this);
    }
    return mPageExtractor;
  }

  /**
   * Get the current selection action delegate for this GeckoSession.
   *
   * @return SelectionActionDelegate instance or null if not set.
   */
  @AnyThread
  public @Nullable SelectionActionDelegate getSelectionActionDelegate() {
    return mSelectionActionDelegate.getDelegate();
  }

  /**
   * Sets whether the content should be pinned on screen.
   *
   * @param pinned true to pin the content on screen, false to unpin.
   */
  @UiThread
  protected void setShouldPinOnScreen(final boolean pinned) {
    if (DEBUG) {
      ThreadUtils.assertOnUiThread();
    }

    mShouldPinOnScreen = pinned;
  }

  /* package */ boolean shouldPinOnScreen() {
    ThreadUtils.assertOnUiThread();
    return mShouldPinOnScreen;
  }

  @AnyThread
  /* package */ @NonNull
  EventDispatcher getEventDispatcher() {
    return mEventDispatcher;
  }

  /** Interface for handling progress updates and security information during content loading. */
  public interface ProgressDelegate {
    /** Class representing security information for a site. */
    class SecurityInformation {
      /** Security mode type definitions for site security status. */
      @Retention(RetentionPolicy.SOURCE)
      @IntDef({SECURITY_MODE_UNKNOWN, SECURITY_MODE_IDENTIFIED, SECURITY_MODE_VERIFIED})
      public @interface SecurityMode {}

      /** Unknown security mode. */
      public static final int SECURITY_MODE_UNKNOWN = 0;

      /** Identified security mode */
      public static final int SECURITY_MODE_IDENTIFIED = 1;

      /** Verified security mode */
      public static final int SECURITY_MODE_VERIFIED = 2;

      /** Content blocking type definitions for blocked/loaded content status. */
      @Retention(RetentionPolicy.SOURCE)
      @IntDef({CONTENT_UNKNOWN, CONTENT_BLOCKED, CONTENT_LOADED})
      public @interface ContentType {}

      /** Unknown content status. */
      public static final int CONTENT_UNKNOWN = 0;

      /** Content was blocked. */
      public static final int CONTENT_BLOCKED = 1;

      /** Content was loaded. */
      public static final int CONTENT_LOADED = 2;

      /** Indicates whether or not the site is secure. */
      public final boolean isSecure;

      /** Indicates whether or not the site is a security exception. */
      public final boolean isException;

      /** Contains the origin of the certificate. */
      public final @Nullable String origin;

      /** Contains the host associated with the certificate. */
      public final @NonNull String host;

      /** The server certificate in use, if any. */
      public final @Nullable X509Certificate certificate;

      /**
       * Indicates the security level of the site; possible values are SECURITY_MODE_UNKNOWN,
       * SECURITY_MODE_IDENTIFIED, and SECURITY_MODE_VERIFIED. SECURITY_MODE_IDENTIFIED indicates
       * domain validation only, while SECURITY_MODE_VERIFIED indicates extended validation.
       */
      public final @SecurityMode int securityMode;

      /**
       * Indicates the presence of passive mixed content; possible values are CONTENT_UNKNOWN,
       * CONTENT_BLOCKED, and CONTENT_LOADED.
       */
      public final @ContentType int mixedModePassive;

      /**
       * Indicates the presence of active mixed content; possible values are CONTENT_UNKNOWN,
       * CONTENT_BLOCKED, and CONTENT_LOADED.
       */
      public final @ContentType int mixedModeActive;

      /* package */ SecurityInformation(final GeckoBundle identityData) {
        final GeckoBundle mode = identityData.getBundle("mode");

        mixedModePassive = mode.getInt("mixed_display");
        mixedModeActive = mode.getInt("mixed_active");

        securityMode = mode.getInt("identity");

        isSecure = identityData.getBoolean("secure");
        isException = identityData.getBoolean("securityException");
        origin = identityData.getString("origin");
        host = identityData.getString("host");

        X509Certificate decodedCert = null;
        try {
          final CertificateFactory factory = CertificateFactory.getInstance("X.509");
          final String certString = identityData.getString("certificate");
          if (certString != null) {
            final byte[] certBytes = Base64.decode(certString, Base64.NO_WRAP);
            decodedCert =
                (X509Certificate) factory.generateCertificate(new ByteArrayInputStream(certBytes));
          }
        } catch (final CertificateException e) {
          Log.e(LOGTAG, "Failed to decode certificate", e);
        }

        certificate = decodedCert;
      }

      /** Empty constructor for tests */
      protected SecurityInformation() {
        mixedModePassive = CONTENT_UNKNOWN;
        mixedModeActive = CONTENT_UNKNOWN;
        securityMode = SECURITY_MODE_UNKNOWN;
        isSecure = false;
        isException = false;
        origin = "";
        host = "";
        certificate = null;
      }
    }

    /**
     * A View has started loading content from the network.
     *
     * @param session GeckoSession that initiated the callback.
     * @param url The resource being loaded.
     */
    @UiThread
    default void onPageStart(@NonNull final GeckoSession session, @NonNull final String url) {}

    /**
     * A View has finished loading content from the network.
     *
     * @param session GeckoSession that initiated the callback.
     * @param success Whether the page loaded successfully or an error occurred.
     */
    @UiThread
    default void onPageStop(@NonNull final GeckoSession session, final boolean success) {}

    /**
     * Page loading has progressed.
     *
     * @param session GeckoSession that initiated the callback.
     * @param progress Current page load progress value [0, 100].
     */
    @UiThread
    default void onProgressChange(@NonNull final GeckoSession session, final int progress) {}

    /**
     * The security status has been updated.
     *
     * @param session GeckoSession that initiated the callback.
     * @param securityInfo The new security information.
     */
    @UiThread
    default void onSecurityChange(
        @NonNull final GeckoSession session, @NonNull final SecurityInformation securityInfo) {}

    /**
     * The browser session state has changed. This can happen in response to navigation, scrolling,
     * or form data changes; the session state passed includes the most up to date information on
     * all of these.
     *
     * @param session GeckoSession that initiated the callback.
     * @param sessionState SessionState representing the latest browser state.
     */
    @UiThread
    default void onSessionStateChange(
        @NonNull final GeckoSession session, @NonNull final SessionState sessionState) {}
  }

  /** WebResponseInfo contains information about a single web response. */
  @AnyThread
  public static class WebResponseInfo {
    /** The URI of the response. Cannot be null. */
    @NonNull public final String uri;

    /** The content type (mime type) of the response. May be null. */
    @Nullable public final String contentType;

    /** The content length of the response. May be 0 if unknokwn. */
    @Nullable public final long contentLength;

    /** The filename obtained from the content disposition, if any. May be null. */
    @Nullable public final String filename;

    /* package */ WebResponseInfo(final GeckoBundle message) {
      uri = message.getString("uri");
      if (uri == null) {
        throw new IllegalArgumentException("URI cannot be null");
      }

      contentType = message.getString("contentType");
      contentLength = message.getLong("contentLength");
      filename = message.getString("filename");
    }

    /** Empty constructor for tests. */
    protected WebResponseInfo() {
      uri = "";
      contentType = "";
      contentLength = 0;
      filename = "";
    }
  }

  /**
   * Interface for handling content-related events such as title changes, focus events, and context
   * menus.
   */
  public interface ContentDelegate {
    /**
     * A page title was discovered in the content or updated after the content loaded.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param title The title sent from the content.
     */
    @UiThread
    default void onTitleChange(@NonNull final GeckoSession session, @Nullable final String title) {}

    /**
     * A preview image was discovered in the content after the content loaded.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param previewImageUrl The preview image URL sent from the content.
     */
    @UiThread
    default void onPreviewImage(
        @NonNull final GeckoSession session, @NonNull final String previewImageUrl) {}

    /**
     * A page has requested focus. Note that window.focus() in content will not result in this being
     * called.
     *
     * @param session The GeckoSession that initiated the callback.
     */
    @UiThread
    default void onFocusRequest(@NonNull final GeckoSession session) {}

    /**
     * A page has requested to close
     *
     * @param session The GeckoSession that initiated the callback.
     */
    @UiThread
    default void onCloseRequest(@NonNull final GeckoSession session) {}

    /**
     * A page has entered or exited full screen mode. Typically, the implementation would set the
     * Activity containing the GeckoSession to full screen when the page is in full screen mode.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param fullScreen True if the page is in full screen mode.
     */
    @UiThread
    default void onFullScreen(@NonNull final GeckoSession session, final boolean fullScreen) {}

    /**
     * A viewport-fit was discovered in the content or updated after the content.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param viewportFit The value of viewport-fit of meta element in content.
     * @see <a href="https://drafts.csswg.org/css-round-display/#viewport-fit-descriptor">4.1. The
     *     viewport-fit descriptor</a>
     */
    @UiThread
    default void onMetaViewportFitChange(
        @NonNull final GeckoSession session, @NonNull final String viewportFit) {}

    /** Element details for onContextMenu callbacks. */
    class ContextElement {
      /** Media type definitions for fullscreen content. */
      @Retention(RetentionPolicy.SOURCE)
      @IntDef({TYPE_NONE, TYPE_IMAGE, TYPE_VIDEO, TYPE_AUDIO})
      public @interface Type {}

      /** Represents no content type. */
      public static final int TYPE_NONE = 0;

      /** Represents an image content type. */
      public static final int TYPE_IMAGE = 1;

      /** Represents a video content type. */
      public static final int TYPE_VIDEO = 2;

      /** Represents an audio content type. */
      public static final int TYPE_AUDIO = 3;

      /** The base URI of the element's document. */
      public final @Nullable String baseUri;

      /** The absolute link URI (href) of the element. */
      public final @Nullable String linkUri;

      /** The title text of the element. */
      public final @Nullable String title;

      /** The alternative text (alt) for the element. */
      public final @Nullable String altText;

      /** The type of the element. One of the {@link ContextElement#TYPE_NONE} flags. */
      public final @Type int type;

      /** The source URI (src) of the element. Set for (nested) media elements. */
      public final @Nullable String srcUri;

      /** The link text of the element */
      public final @Nullable String linkText;

      // TODO: Bug 1595822 make public
      final List<WebExtension.Menu> extensionMenus;

      /**
       * ContextElement constructor.
       *
       * @param baseUri The base URI.
       * @param linkUri The absolute link URI (href).
       * @param title The title text.
       * @param altText The alternative text (alt).
       * @param typeStr The type of the element.
       * @param srcUri The source URI (src).
       * @param linkText The link text content.
       */
      protected ContextElement(
          final @Nullable String baseUri,
          final @Nullable String linkUri,
          final @Nullable String title,
          final @Nullable String altText,
          final @NonNull String typeStr,
          final @Nullable String srcUri,
          final @Nullable String linkText) {
        this.baseUri = baseUri;
        this.linkUri = linkUri;
        this.title = title;
        this.altText = altText;
        this.type = getType(typeStr);
        this.srcUri = srcUri;
        this.extensionMenus = null;
        this.linkText = linkText;
      }

      private static int getType(final String name) {
        if ("HTMLImageElement".equals(name)) {
          return TYPE_IMAGE;
        } else if ("HTMLVideoElement".equals(name)) {
          return TYPE_VIDEO;
        } else if ("HTMLAudioElement".equals(name)) {
          return TYPE_AUDIO;
        }
        return TYPE_NONE;
      }
    }

    /**
     * A user has initiated the context menu via long-press. This event is fired on links, (nested)
     * images and (nested) media elements.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param screenX The screen coordinates of the press.
     * @param screenY The screen coordinates of the press.
     * @param element The details for the pressed element.
     */
    @UiThread
    default void onContextMenu(
        @NonNull final GeckoSession session,
        final int screenX,
        final int screenY,
        @NonNull final ContextElement element) {}

    /**
     * This is fired when there is a response that cannot be handled by Gecko (e.g., a download).
     *
     * @param session the GeckoSession that received the external response.
     * @param response the external WebResponse.
     */
    @UiThread
    default void onExternalResponse(
        @NonNull final GeckoSession session, @NonNull final WebResponse response) {}

    /**
     * The content process hosting this GeckoSession has crashed. The GeckoSession is now closed and
     * unusable. You may call {@link #open(GeckoRuntime)} to recover the session, but no state is
     * preserved. Most applications will want to call {@link #load} or {@link
     * #restoreState(SessionState)} at this point.
     *
     * @param session The GeckoSession for which the content process has crashed.
     */
    @UiThread
    default void onCrash(@NonNull final GeckoSession session) {}

    /**
     * The content process hosting this GeckoSession has been killed. The GeckoSession is now closed
     * and unusable. You may call {@link #open(GeckoRuntime)} to recover the session, but no state
     * is preserved. Most applications will want to call {@link #load} or {@link
     * #restoreState(SessionState)} at this point.
     *
     * @param session The GeckoSession for which the content process has been killed.
     */
    @UiThread
    default void onKill(@NonNull final GeckoSession session) {}

    /**
     * Notification that the first content composition has occurred. This callback is invoked for
     * the first content composite after either a start or a restart of the compositor.
     *
     * @param session The GeckoSession that had a first paint event.
     */
    @UiThread
    default void onFirstComposite(@NonNull final GeckoSession session) {}

    /**
     * Notification that the first content paint has occurred. This callback is invoked for the
     * first content paint after a page has been loaded, or after a {@link
     * #onPaintStatusReset(GeckoSession)} event. The function {@link
     * #onFirstComposite(GeckoSession)} will be called once the compositor has started rendering.
     * However, it is possible for the compositor to start rendering before there is any content to
     * render. onFirstContentfulPaint() is called once some content has been rendered. It may be
     * nothing more than the page background color. It is not an indication that the whole page has
     * been rendered.
     *
     * @param session The GeckoSession that had a first paint event.
     */
    @UiThread
    default void onFirstContentfulPaint(@NonNull final GeckoSession session) {}

    /**
     * Notification that the paint status has been reset.
     *
     * <p>This callback is invoked whenever the painted content is no longer being displayed. This
     * can occur in response to the session being paused. After this has fired the compositor may
     * continue rendering, but may not render the page content. This callback can therefore be used
     * in conjunction with {@link #onFirstContentfulPaint(GeckoSession)} to determine when there is
     * valid content being rendered.
     *
     * @param session The GeckoSession that had the paint status reset event.
     */
    @UiThread
    default void onPaintStatusReset(@NonNull final GeckoSession session) {}

    /**
     * A page has requested to change pointer icon.
     *
     * <p>If the application wants to control pointer icon, it should override this, then handle it.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param icon The pointer icon sent from the content.
     */
    @UiThread
    default void onPointerIconChange(
        @NonNull final GeckoSession session, @NonNull final PointerIcon icon) {
      final View view = session.getTextInput().getView();
      if (view != null) {
        view.setPointerIcon(icon);
      }
    }

    /**
     * This is fired when the loaded document has a valid Web App Manifest present.
     *
     * <p>The various colors (theme_color, background_color, etc.) present in the manifest have been
     * transformed into #AARRGGBB format.
     *
     * @param session The GeckoSession that contains the Web App Manifest
     * @param manifest A parsed and validated {@link JSONObject} containing the manifest contents.
     * @see <a href="https://www.w3.org/TR/appmanifest/">Web App Manifest specification</a>
     */
    @UiThread
    default void onWebAppManifest(
        @NonNull final GeckoSession session, @NonNull final JSONObject manifest) {}

    /**
     * A script has exceeded its execution timeout value
     *
     * @param geckoSession GeckoSession that initiated the callback.
     * @param scriptFileName Filename of the slow script
     * @return A {@link GeckoResult} with a SlowScriptResponse value which indicates whether to
     *     allow the Slow Script to continue processing. Stop will halt the slow script. Continue
     *     will pause notifications for a period of time before resuming.
     */
    @UiThread
    default @Nullable GeckoResult<SlowScriptResponse> onSlowScript(
        @NonNull final GeckoSession geckoSession, @NonNull final String scriptFileName) {
      return null;
    }

    /**
     * The app should display its dynamic toolbar, fully expanded to the height that was previously
     * specified via {@link GeckoView#setDynamicToolbarMaxHeight}.
     *
     * @param geckoSession GeckoSession that initiated the callback.
     */
    @UiThread
    default void onShowDynamicToolbar(@NonNull final GeckoSession geckoSession) {}

    /**
     * The app should hide its dynamic toolbar.
     *
     * @param geckoSession GeckoSession that initiated the callback.
     */
    @UiThread
    default void onHideDynamicToolbar(@NonNull final GeckoSession geckoSession) {}

    /**
     * This method is called when a cookie banner was detected.
     *
     * <p>Note: this method is called only if the cookie banner setting is such that allows to
     * handle the banner. For example, if cookiebanners.service.mode=1 (Reject only) but a cookie
     * banner can only be accepted on the website - the detection in that case won't be reported.
     * The exception is MODE_DETECT_ONLY mode, when only the detection event is emitted.
     *
     * @param session GeckoSession that initiated the callback.
     */
    @AnyThread
    default void onCookieBannerDetected(@NonNull final GeckoSession session) {}

    /**
     * This method is called when a cookie banner was handled.
     *
     * @param session GeckoSession that initiated the callback.
     */
    @AnyThread
    default void onCookieBannerHandled(@NonNull final GeckoSession session) {}
  }

  /** Interface for handling text selection actions and providing custom selection action items. */
  public interface SelectionActionDelegate {
    /** The selection is collapsed at a single position. */
    int FLAG_IS_COLLAPSED = 1 << 0;

    /**
     * The selection is inside editable content such as an input element or contentEditable node.
     */
    int FLAG_IS_EDITABLE = 1 << 1;

    /** The selection is inside a password field. */
    int FLAG_IS_PASSWORD = 1 << 2;

    /** Hide selection actions and cause {@link #onHideAction} to be called. */
    String ACTION_HIDE = "org.mozilla.geckoview.HIDE";

    /** Copy onto the clipboard then delete the selected content. Selection must be editable. */
    String ACTION_CUT = "org.mozilla.geckoview.CUT";

    /** Copy the selected content onto the clipboard. */
    String ACTION_COPY = "org.mozilla.geckoview.COPY";

    /** Delete the selected content. Selection must be editable. */
    String ACTION_DELETE = "org.mozilla.geckoview.DELETE";

    /** Replace the selected content with the clipboard content. Selection must be editable. */
    String ACTION_PASTE = "org.mozilla.geckoview.PASTE";

    /**
     * Replace the selected content with the clipboard content as plain text. Selection must be
     * editable.
     */
    String ACTION_PASTE_AS_PLAIN_TEXT = "org.mozilla.geckoview.PASTE_AS_PLAIN_TEXT";

    /** Select the entire content of the document or editor. */
    String ACTION_SELECT_ALL = "org.mozilla.geckoview.SELECT_ALL";

    /** Clear the current selection. Selection must not be editable. */
    String ACTION_UNSELECT = "org.mozilla.geckoview.UNSELECT";

    /** Collapse the current selection to its start position. Selection must be editable. */
    String ACTION_COLLAPSE_TO_START = "org.mozilla.geckoview.COLLAPSE_TO_START";

    /** Collapse the current selection to its end position. Selection must be editable. */
    String ACTION_COLLAPSE_TO_END = "org.mozilla.geckoview.COLLAPSE_TO_END";

    /** Represents attributes of a selection. */
    class Selection {
      /**
       * Flags describing the current selection, as a bitwise combination of the {@link
       * #FLAG_IS_COLLAPSED FLAG_*} constants.
       */
      public final @SelectionActionDelegateFlag int flags;

      /**
       * Text content of the current selection. An empty string indicates the selection is collapsed
       * or the selection cannot be represented as plain text.
       */
      public final @NonNull String text;

      /** The bounds of the current selection in screen coordinates. */
      public final @Nullable RectF screenRect;

      /** Set of valid actions available through {@link Selection#execute(String)} */
      public final @NonNull @SelectionActionDelegateAction Collection<String> availableActions;

      private final String mActionId;

      private final WeakReference<EventDispatcher> mEventDispatcher;

      /* package */ Selection(
          final GeckoBundle bundle,
          final @NonNull @SelectionActionDelegateAction Set<String> actions,
          final EventDispatcher eventDispatcher) {
        flags =
            (bundle.getBoolean("collapsed") ? SelectionActionDelegate.FLAG_IS_COLLAPSED : 0)
                | (bundle.getBoolean("editable") ? SelectionActionDelegate.FLAG_IS_EDITABLE : 0)
                | (bundle.getBoolean("password") ? SelectionActionDelegate.FLAG_IS_PASSWORD : 0);
        text = bundle.getString("selection");
        screenRect = bundle.getRectF("screenRect");
        availableActions = actions;
        mActionId = bundle.getString("actionId");
        mEventDispatcher = new WeakReference<>(eventDispatcher);
      }

      /** Empty constructor for tests. */
      protected Selection() {
        flags = 0;
        text = "";
        screenRect = null;
        availableActions = new HashSet<>();
        mActionId = null;
        mEventDispatcher = null;
      }

      /**
       * Checks if the passed action is available
       *
       * @param action An {@link SelectionActionDelegate} to perform
       * @return True if the action is available.
       */
      @AnyThread
      public boolean isActionAvailable(
          @NonNull @SelectionActionDelegateAction final String action) {
        return availableActions.contains(action);
      }

      /**
       * Execute an {@link SelectionActionDelegate} action.
       *
       * @throws IllegalStateException If the action was not available.
       * @param action A {@link SelectionActionDelegate} action.
       */
      @AnyThread
      public void execute(@NonNull @SelectionActionDelegateAction final String action) {
        if (!isActionAvailable(action)) {
          throw new IllegalStateException("Action not available");
        }
        final EventDispatcher eventDispatcher = mEventDispatcher.get();
        if (eventDispatcher == null) {
          // The session is not available anymore, nothing really to do
          Log.w(LOGTAG, "Calling execute on a stale Selection.");
          return;
        }
        final GeckoBundle response = new GeckoBundle(2);
        response.putString("id", action);
        response.putString("actionId", mActionId);
        eventDispatcher.dispatch("GeckoView:ExecuteSelectionAction", response);
      }

      /**
       * Hide selection actions and cause {@link #onHideAction} to be called.
       *
       * @throws IllegalStateException If the action was not available.
       */
      @AnyThread
      public void hide() {
        execute(ACTION_HIDE);
      }

      /**
       * Copy onto the clipboard then delete the selected content.
       *
       * @throws IllegalStateException If the action was not available.
       */
      @AnyThread
      public void cut() {
        execute(ACTION_CUT);
      }

      /**
       * Copy the selected content onto the clipboard.
       *
       * @throws IllegalStateException If the action was not available.
       */
      @AnyThread
      public void copy() {
        execute(ACTION_COPY);
      }

      /**
       * Delete the selected content.
       *
       * @throws IllegalStateException If the action was not available.
       */
      @AnyThread
      public void delete() {
        execute(ACTION_DELETE);
      }

      /**
       * Replace the selected content with the clipboard content.
       *
       * @throws IllegalStateException If the action was not available.
       */
      @AnyThread
      public void paste() {
        execute(ACTION_PASTE);
      }

      /**
       * Replace the selected content with the clipboard content as plain text.
       *
       * @throws IllegalStateException If the action was not available.
       */
      @AnyThread
      public void pasteAsPlainText() {
        execute(ACTION_PASTE_AS_PLAIN_TEXT);
      }

      /**
       * Select the entire content of the document or editor.
       *
       * @throws IllegalStateException If the action was not available.
       */
      @AnyThread
      public void selectAll() {
        execute(ACTION_SELECT_ALL);
      }

      /**
       * Clear the current selection.
       *
       * @throws IllegalStateException If the action was not available.
       */
      @AnyThread
      public void unselect() {
        execute(ACTION_UNSELECT);
      }

      /**
       * Collapse the current selection to its start position.
       *
       * @throws IllegalStateException If the action was not available.
       */
      @AnyThread
      public void collapseToStart() {
        execute(ACTION_COLLAPSE_TO_START);
      }

      /**
       * Collapse the current selection to its end position.
       *
       * @throws IllegalStateException If the action was not available.
       */
      @AnyThread
      public void collapseToEnd() {
        execute(ACTION_COLLAPSE_TO_END);
      }
    }

    /**
     * Selection actions are available. Selection actions become available when the user selects
     * some content in the document or editor. Inside an editor, selection actions can also become
     * available when the user explicitly requests editor action UI, for example by tapping on the
     * caret handle.
     *
     * <p>In response to this callback, applications typically display a toolbar containing the
     * selection actions. To perform a certain action, check if the action is available with {@link
     * Selection#isActionAvailable} then either use the relevant helper method or {@link
     * Selection#execute}
     *
     * <p>Once an {@link #onHideAction} call (with particular reasons) or another {@link
     * #onShowActionRequest} call is received, the previous Selection object is no longer usable.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param selection Current selection attributes and Callback object for performing built-in
     *     actions. May be used multiple times to perform multiple actions at once.
     */
    @UiThread
    default void onShowActionRequest(
        @NonNull final GeckoSession session, @NonNull final Selection selection) {}

    /** Actions are no longer available due to the user clearing the selection. */
    int HIDE_REASON_NO_SELECTION = 0;

    /**
     * Actions are no longer available due to the user moving the selection out of view. Previous
     * actions are still available after a callback with this reason.
     */
    int HIDE_REASON_INVISIBLE_SELECTION = 1;

    /**
     * Actions are no longer available due to the user actively changing the selection. {@link
     * #onShowActionRequest} may be called again once the user has set a selection, if the new
     * selection has available actions.
     */
    int HIDE_REASON_ACTIVE_SELECTION = 2;

    /**
     * Actions are no longer available due to the user actively scrolling the page. {@link
     * #onShowActionRequest} may be called again once the user has stopped scrolling the page, if
     * the selection is still visible. Until then, previous actions are still available after a
     * callback with this reason.
     */
    int HIDE_REASON_ACTIVE_SCROLL = 3;

    /**
     * Previous actions are no longer available due to the user interacting with the page.
     * Applications typically hide the action toolbar in response.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param reason The reason that actions are no longer available, as one of the {@link
     *     #HIDE_REASON_NO_SELECTION HIDE_REASON_*} constants.
     */
    @UiThread
    default void onHideAction(
        @NonNull final GeckoSession session, @SelectionActionDelegateHideReason final int reason) {}

    /**
     * Permission for reading clipboard data. See: <a
     * href="https://developer.mozilla.org/en-US/docs/Web/API/Clipboard/readText">Clipboard.readText()</a>
     */
    int PERMISSION_CLIPBOARD_READ = 1;

    /** Represents attributes of a clipboard permission. */
    class ClipboardPermission {
      /** The URI associated with this content permission. */
      public final @NonNull String uri;

      /**
       * The type of this permission; one of {@link #PERMISSION_CLIPBOARD_READ
       * PERMISSION_CLIPBOARD_*}.
       */
      public final @ClipboardPermissionType int type;

      /**
       * The last mouse or touch location in screen coordinates when the permission is requested.
       */
      public final @Nullable Point screenPoint;

      /** Empty constructor for tests */
      protected ClipboardPermission() {
        this.uri = "";
        this.type = PERMISSION_CLIPBOARD_READ;
        this.screenPoint = null;
      }

      private ClipboardPermission(final @NonNull GeckoBundle bundle) {
        this.uri = bundle.getString("uri");
        this.type = PERMISSION_CLIPBOARD_READ;
        this.screenPoint = bundle.getPoint("screenPoint");
      }
    }

    /**
     * Request clipboard permission.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param permission An {@link ClipboardPermission} describing the permission being requested.
     * @return A {@link GeckoResult} with {@link AllowOrDeny}, determining the response to the
     *     permission request for this site.
     */
    @UiThread
    default @Nullable GeckoResult<AllowOrDeny> onShowClipboardPermissionRequest(
        @NonNull final GeckoSession session, @NonNull ClipboardPermission permission) {
      return GeckoResult.deny();
    }

    /**
     * Dismiss requesting clipboard permission popup or model.
     *
     * @param session The GeckoSession that initiated the callback.
     */
    @UiThread
    default void onDismissClipboardPermissionRequest(@NonNull final GeckoSession session) {}
  }

  /** Selection action type definitions for text selection operations. */
  @Retention(RetentionPolicy.SOURCE)
  @StringDef({
    SelectionActionDelegate.ACTION_HIDE,
    SelectionActionDelegate.ACTION_CUT,
    SelectionActionDelegate.ACTION_COPY,
    SelectionActionDelegate.ACTION_DELETE,
    SelectionActionDelegate.ACTION_PASTE,
    SelectionActionDelegate.ACTION_PASTE_AS_PLAIN_TEXT,
    SelectionActionDelegate.ACTION_SELECT_ALL,
    SelectionActionDelegate.ACTION_UNSELECT,
    SelectionActionDelegate.ACTION_COLLAPSE_TO_START,
    SelectionActionDelegate.ACTION_COLLAPSE_TO_END
  })
  public @interface SelectionActionDelegateAction {}

  /** Selection flag type definitions for text selection state. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef(
      flag = true,
      value = {
        SelectionActionDelegate.FLAG_IS_COLLAPSED,
        SelectionActionDelegate.FLAG_IS_EDITABLE,
        SelectionActionDelegate.FLAG_IS_PASSWORD
      })
  public @interface SelectionActionDelegateFlag {}

  /** Selection hide reason type definitions for text selection dismissal. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({
    SelectionActionDelegate.HIDE_REASON_NO_SELECTION,
    SelectionActionDelegate.HIDE_REASON_INVISIBLE_SELECTION,
    SelectionActionDelegate.HIDE_REASON_ACTIVE_SELECTION,
    SelectionActionDelegate.HIDE_REASON_ACTIVE_SCROLL
  })
  public @interface SelectionActionDelegateHideReason {}

  /** Clipboard permission type definitions for clipboard access. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({
    SelectionActionDelegate.PERMISSION_CLIPBOARD_READ,
  })
  public @interface ClipboardPermissionType {}

  /** Interface for handling navigation events such as loading, redirects, and location changes. */
  public interface NavigationDelegate {
    /**
     * A view has started loading content from the network.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param url The resource being loaded.
     * @param perms The permissions currently associated with this url.
     * @param hasUserGesture Whether or not there was an active user gesture when the location
     *     change was requested.
     */
    @UiThread
    default void onLocationChange(
        @NonNull GeckoSession session,
        @Nullable String url,
        final @NonNull List<PermissionDelegate.ContentPermission> perms,
        @NonNull Boolean hasUserGesture) {}

    /**
     * The view's ability to go back has changed.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param canGoBack The new value for the ability.
     */
    @UiThread
    default void onCanGoBack(@NonNull final GeckoSession session, final boolean canGoBack) {}

    /**
     * The view's ability to go forward has changed.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param canGoForward The new value for the ability.
     */
    @UiThread
    default void onCanGoForward(@NonNull final GeckoSession session, final boolean canGoForward) {}

    /** No target window specified. */
    int TARGET_WINDOW_NONE = 0;

    /** Open in the current window. */
    int TARGET_WINDOW_CURRENT = 1;

    /** Open in a new window. */
    int TARGET_WINDOW_NEW = 2;

    // Match with nsIWebNavigation.idl.
    /** The load request was triggered by an HTTP redirect. */
    int LOAD_REQUEST_IS_REDIRECT = 0x800000;

    /** Load request details. */
    class LoadRequest {
      /* package */ LoadRequest(
          @NonNull final String uri,
          @Nullable final String triggerUri,
          final int geckoTarget,
          final int flags,
          final boolean hasUserGesture,
          final boolean isDirectNavigation) {
        this.uri = uri;
        this.triggerUri = triggerUri;
        this.target = convertGeckoTarget(geckoTarget);
        this.isRedirect = (flags & LOAD_REQUEST_IS_REDIRECT) != 0;
        this.hasUserGesture = hasUserGesture;
        this.isDirectNavigation = isDirectNavigation;
      }

      /** Empty constructor for tests. */
      protected LoadRequest() {
        uri = "";
        triggerUri = null;
        target = TARGET_WINDOW_NONE;
        isRedirect = false;
        hasUserGesture = false;
        isDirectNavigation = false;
      }

      // This needs to match nsIBrowserDOMWindow.idl
      private @TargetWindow int convertGeckoTarget(final int geckoTarget) {
        switch (geckoTarget) {
          case 0: // OPEN_DEFAULTWINDOW
          case 1: // OPEN_CURRENTWINDOW
            return TARGET_WINDOW_CURRENT;
          default: // OPEN_NEWWINDOW, OPEN_NEWTAB, OPEN_NEWTAB_BACKGROUND, OPEN_NEWTAB_FOREGROUND
            return TARGET_WINDOW_NEW;
        }
      }

      /** The URI to be loaded. */
      public final @NonNull String uri;

      /**
       * The URI of the origin page that triggered the load request. null for initial loads and
       * loads originating from data: URIs.
       */
      public final @Nullable String triggerUri;

      /**
       * The target where the window has requested to open. One of {@link #TARGET_WINDOW_NONE
       * TARGET_WINDOW_*}.
       */
      public final @TargetWindow int target;

      /**
       * True if and only if the request was triggered by an HTTP redirect.
       *
       * <p>If the user loads URI "a", which redirects to URI "b", then <code>onLoadRequest</code>
       * will be called twice, first with uri "a" and <code>isRedirect = false</code>, then with uri
       * "b" and <code>isRedirect = true</code>.
       */
      public final boolean isRedirect;

      /** True if there was an active user gesture when the load was requested. */
      public final boolean hasUserGesture;

      /**
       * This load request was initiated by a direct navigation from the application. E.g. when
       * calling {@link GeckoSession#load}.
       */
      public final boolean isDirectNavigation;

      @Override
      public String toString() {
        final StringBuilder out = new StringBuilder("LoadRequest { ");
        out.append("uri: " + uri)
            .append(", triggerUri: " + triggerUri)
            .append(", target: " + target)
            .append(", isRedirect: " + isRedirect)
            .append(", hasUserGesture: " + hasUserGesture)
            .append(", fromLoadUri: " + hasUserGesture)
            .append(" }");
        return out.toString();
      }
    }

    /**
     * A request to open an URI. This is called before each top-level page load to allow custom
     * behavior. For example, this can be used to override the behavior of TAGET_WINDOW_NEW
     * requests, which defaults to requesting a new GeckoSession via onNewSession.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param request The {@link LoadRequest} containing the request details.
     * @return A {@link GeckoResult} with a {@link AllowOrDeny} value which indicates whether or not
     *     the load was handled. If unhandled, Gecko will continue the load as normal. If handled (a
     *     {@link AllowOrDeny#DENY DENY} value), Gecko will abandon the load. A null return value is
     *     interpreted as {@link AllowOrDeny#ALLOW ALLOW} (unhandled).
     */
    @UiThread
    default @Nullable GeckoResult<AllowOrDeny> onLoadRequest(
        @NonNull final GeckoSession session, @NonNull final LoadRequest request) {
      return null;
    }

    /**
     * A request to load a URI in a non-top-level context.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param request The {@link LoadRequest} containing the request details.
     * @return A {@link GeckoResult} with a {@link AllowOrDeny} value which indicates whether or not
     *     the load was handled. If unhandled, Gecko will continue the load as normal. If handled (a
     *     {@link AllowOrDeny#DENY DENY} value), Gecko will abandon the load. A null return value is
     *     interpreted as {@link AllowOrDeny#ALLOW ALLOW} (unhandled).
     */
    @UiThread
    default @Nullable GeckoResult<AllowOrDeny> onSubframeLoadRequest(
        @NonNull final GeckoSession session, @NonNull final LoadRequest request) {
      return null;
    }

    /**
     * A request has been made to open a new session. The URI is provided only for informational
     * purposes. Do not call GeckoSession.load here. Additionally, the returned GeckoSession must be
     * a newly-created one.
     *
     * @param session The GeckoSession that initiated the callback.
     * @param uri The URI to be loaded.
     * @return A {@link GeckoResult} which holds the returned GeckoSession. May be null, in which
     *     case the request for a new window by web content will fail. e.g., <code>window.open()
     *     </code> will return null. The implementation of onNewSession is responsible for
     *     maintaining a reference to the returned object, to prevent it from being garbage
     *     collected.
     */
    @UiThread
    default @Nullable GeckoResult<GeckoSession> onNewSession(
        @NonNull final GeckoSession session, @NonNull final String uri) {
      return null;
    }

    /**
     * @param session The GeckoSession that initiated the callback.
     * @param uri The URI that failed to load.
     * @param error A WebRequestError containing details about the error
     * @return A URI to display as an error (cannot be http/https). Returning null or http/https URL
     *     will halt the load entirely. The following special methods are made available to the URI:
     *     - document.addCertException(isTemporary), returns Promise -
     *     document.getFailedCertSecurityInfo(), returns FailedCertSecurityInfo -
     *     document.getNetErrorInfo(), returns NetErrorInfo document.reloadWithHttpsOnlyException()
     * @see <a
     *     href="https://searchfox.org/firefox-main/source/dom/webidl/FailedCertSecurityInfo.webidl">FailedCertSecurityInfo
     *     IDL</a>
     * @see <a
     *     href="https://searchfox.org/firefox-main/source/dom/webidl/NetErrorInfo.webidl">NetErrorInfo
     *     IDL</a>
     */
    @UiThread
    default @Nullable GeckoResult<String> onLoadError(
        @NonNull final GeckoSession session,
        @Nullable final String uri,
        @NonNull final WebRequestError error) {
      return null;
    }
  }

  /** Target window type definitions for navigation target. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({
    NavigationDelegate.TARGET_WINDOW_NONE,
    NavigationDelegate.TARGET_WINDOW_CURRENT,
    NavigationDelegate.TARGET_WINDOW_NEW
  })
  public @interface TargetWindow {}

  /**
   * GeckoSession applications implement this interface to handle prompts triggered by content in
   * the GeckoSession, such as alerts, authentication dialogs, and select list pickers.
   */
  public interface PromptDelegate {
    /** PromptResponse is an opaque class created upon confirming or dismissing a prompt. */
    class PromptResponse {
      private final BasePrompt mPrompt;

      /* package */ PromptResponse(@NonNull final BasePrompt prompt) {
        mPrompt = prompt;
      }

      /* package */ void dispatch(@NonNull final EventCallback callback) {
        if (mPrompt == null) {
          throw new RuntimeException("Trying to confirm/dismiss a null prompt.");
        }
        mPrompt.dispatch(callback);
      }
    }

    /** Interface for receiving lifecycle events on a specific prompt instance. */
    interface PromptInstanceDelegate {
      /**
       * Called when this prompt has been dismissed by the system.
       *
       * <p>This can happen e.g. when the page navigates away and the content of the prompt is not
       * relevant anymore.
       *
       * <p>When this method is called, you should hide the prompt UI elements.
       *
       * @param prompt the prompt that should be dismissed.
       */
      @UiThread
      default void onPromptDismiss(final @NonNull BasePrompt prompt) {}

      /**
       * Called when this prompt has been updated.
       *
       * <p>This is called if inner &lt;option&gt; elements are updated when using &lt;select&gt;
       * element.
       *
       * <p>When this method is called, you should update the prompt UI elements.
       *
       * @param prompt the new prompt that should be updated.
       */
      @UiThread
      default void onPromptUpdate(final @NonNull BasePrompt prompt) {}
    }

    /** Core prompt container that manages confirmation, dismissal, and observer notification. */
    class BasePrompt {
      private boolean mIsCompleted;
      private boolean mIsConfirmed;
      private GeckoBundle mResult;
      private final WeakReference<Observer> mObserver;
      private PromptInstanceDelegate mDelegate;

      /** Observer interface for prompt completion events. */
      protected interface Observer {
        /**
         * Called when the prompt has been completed.
         *
         * @param prompt the prompt that was completed.
         */
        @AnyThread
        default void onPromptCompleted(@NonNull BasePrompt prompt) {}
      }

      private void complete() {
        mIsCompleted = true;
        final Observer observer = mObserver.get();
        if (observer != null) {
          observer.onPromptCompleted(this);
        }
      }

      /** The title of this prompt; may be null. */
      public final @Nullable String title;

      /* package */ String id;

      private BasePrompt(
          @NonNull final String id, @Nullable final String title, final Observer observer) {
        this.title = title;
        this.id = id;
        mIsConfirmed = false;
        mIsCompleted = false;
        mObserver = new WeakReference<>(observer);
      }

      /**
       * Confirms the prompt, marking it completed and notifying its observer.
       *
       * @return a PromptResponse which can be dispatched to signal confirmation.
       * @throws RuntimeException if this prompt has already been completed.
       */
      @UiThread
      protected @NonNull PromptResponse confirm() {
        if (mIsCompleted) {
          throw new RuntimeException("Cannot confirm/dismiss a Prompt twice.");
        }

        mIsConfirmed = true;
        complete();
        return new PromptResponse(this);
      }

      /**
       * This dismisses the prompt without sending any meaningful information back to content.
       *
       * @return A {@link PromptResponse} with which you can complete the {@link GeckoResult} that
       *     corresponds to this prompt.
       */
      @UiThread
      public @NonNull PromptResponse dismiss() {
        if (mIsCompleted) {
          throw new RuntimeException("Cannot confirm/dismiss a Prompt twice.");
        }

        complete();
        return new PromptResponse(this);
      }

      /**
       * Set the delegate for this prompt.
       *
       * @param delegate the {@link PromptInstanceDelegate} instance.
       */
      @UiThread
      public void setDelegate(final @Nullable PromptInstanceDelegate delegate) {
        mDelegate = delegate;
      }

      /**
       * Get the delegate for this prompt.
       *
       * @return the {@link PromptInstanceDelegate} instance.
       */
      @UiThread
      @Nullable
      public PromptInstanceDelegate getDelegate() {
        return mDelegate;
      }

      /* package */ GeckoBundle ensureResult() {
        if (mResult == null) {
          // Usually result object contains two items.
          mResult = new GeckoBundle(2);
        }
        return mResult;
      }

      /**
       * This returns true if the prompt has already been confirmed or dismissed.
       *
       * @return A boolean which is true if the prompt has been confirmed or dismissed, and false
       *     otherwise.
       */
      @UiThread
      public boolean isComplete() {
        return mIsCompleted;
      }

      /* package */ void dispatch(@NonNull final EventCallback callback) {
        if (!mIsCompleted) {
          throw new RuntimeException("Trying to dispatch an incomplete prompt.");
        }

        if (!mIsConfirmed) {
          callback.sendSuccess(null);
        } else {
          callback.sendSuccess(mResult);
        }
      }
    }

    /**
     * BeforeUnloadPrompt represents the onbeforeunload prompt. See
     * https://developer.mozilla.org/en-US/docs/Web/API/WindowEventHandlers/onbeforeunload
     */
    class BeforeUnloadPrompt extends BasePrompt {
      /**
       * Constructs a BeforeUnloadPrompt for the onbeforeunload event.
       *
       * @param id the unique identifier for this prompt
       * @param observer the observer to notify when the prompt is completed
       */
      protected BeforeUnloadPrompt(@NonNull final String id, @NonNull final Observer observer) {
        super(id, null, observer);
      }

      /**
       * Confirms the prompt.
       *
       * @param allowOrDeny whether the navigation should be allowed to continue or not.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(final @Nullable AllowOrDeny allowOrDeny) {
        ensureResult().putBoolean("allow", allowOrDeny != AllowOrDeny.DENY);
        return super.confirm();
      }
    }

    /**
     * FolderUploadPrompt represents a prompt shown whenever the browser needs to upload folder data
     */
    class FolderUploadPrompt extends BasePrompt {
      /** The directory name to confirm folder tries to uploading. */
      public final @Nullable String directoryName;

      /**
       * A constructor for FolderUploadPrompt
       *
       * @param id The identification for this prompt.
       * @param directoryName The directory that is confirmed.
       * @param observer A callback to notify when the prompt has been completed.
       */
      protected FolderUploadPrompt(
          @NonNull final String id,
          @Nullable final String directoryName,
          @NonNull final Observer observer) {
        super(id, null, observer);
        this.directoryName = directoryName;
      }

      /**
       * Confirms the prompt.
       *
       * @param allowOrDeny whether the browser should allow resubmitting data.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(final @Nullable AllowOrDeny allowOrDeny) {
        ensureResult().putBoolean("allow", allowOrDeny != AllowOrDeny.DENY);
        return super.confirm();
      }
    }

    /** WebAuthnRelatedOriginPrompt is shown to confirm a WebAuthn related origin request. */
    class WebAuthnRelatedOriginPrompt extends BasePrompt {
      /** The origin of the site making the request. */
      public final @Nullable String origin;

      /** The relying party ID for the passkey. */
      public final @Nullable String rpId;

      /** Whether this is a create (true) or use (false) request. */
      public final boolean isCreate;

      /**
       * A constructor for WebAuthnRelatedOriginPrompt.
       *
       * @param id The identification for this prompt.
       * @param origin The origin of the site making the request.
       * @param rpId The relying party ID for the passkey.
       * @param isCreate Whether this is a create (true) or use (false) request.
       * @param observer A callback to notify when the prompt has been completed.
       */
      protected WebAuthnRelatedOriginPrompt(
          @NonNull final String id,
          @Nullable final String origin,
          @Nullable final String rpId,
          final boolean isCreate,
          @NonNull final Observer observer) {
        super(id, null, observer);
        this.origin = origin;
        this.rpId = rpId;
        this.isCreate = isCreate;
      }

      /**
       * Confirms the prompt.
       *
       * @param allowOrDeny whether the user confirmed or denied the request.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(final @Nullable AllowOrDeny allowOrDeny) {
        ensureResult().putBoolean("allow", allowOrDeny != AllowOrDeny.DENY);
        return super.confirm();
      }
    }

    /**
     * RepostConfirmPrompt represents a prompt shown whenever the browser needs to resubmit POST
     * data (e.g. due to page refresh).
     */
    class RepostConfirmPrompt extends BasePrompt {
      /**
       * Constructs a RepostConfirmPrompt for POST data resubmission prompts.
       *
       * @param id the unique identifier for this prompt
       * @param observer the observer to notify when the prompt is completed
       */
      protected RepostConfirmPrompt(@NonNull final String id, @NonNull final Observer observer) {
        super(id, null, observer);
      }

      /**
       * Confirms the prompt.
       *
       * @param allowOrDeny whether the browser should allow resubmitting data.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(final @Nullable AllowOrDeny allowOrDeny) {
        ensureResult().putBoolean("allow", allowOrDeny != AllowOrDeny.DENY);
        return super.confirm();
      }
    }

    /**
     * AlertPrompt contains the information necessary to represent a JavaScript alert() call from
     * content; it can only be dismissed, not confirmed.
     */
    class AlertPrompt extends BasePrompt {
      /** The message to be displayed with this alert; may be null. */
      public final @Nullable String message;

      /**
       * Constructs an AlertPrompt for JavaScript alert dialogs.
       *
       * @param id the unique identifier for this prompt
       * @param title optional title text of the alert; may be null
       * @param message the message to be displayed with this alert; may be null
       * @param observer observer to notify when the prompt is dismissed
       */
      protected AlertPrompt(
          @NonNull final String id,
          @Nullable final String title,
          @Nullable final String message,
          @NonNull final Observer observer) {
        super(id, title, observer);
        this.message = message;
      }
    }

    /** Contains all the Identity credential prompts (FedCM) */
    final class IdentityCredential {
      /**
       * ProviderSelectorPrompt contains the information necessary to represent a prompt that allows
       * the user to select the identity credential provider they would like to use.
       */
      public static class ProviderSelectorPrompt extends BasePrompt {
        /** The providers from which the user could select. */
        public final @NonNull Provider[] providers;

        /**
         * Creates a new {@link ProviderSelectorPrompt} with the given parameters.
         *
         * @param id The identification for this prompt.
         * @param providers The providers from which the user could select.
         * @param observer A callback to notify when the prompt has been completed.
         */
        protected ProviderSelectorPrompt(
            @NonNull final String id,
            @NonNull final Provider[] providers,
            @NonNull final Observer observer) {
          super(id, null, observer);
          this.providers = providers;
        }

        /**
         * Confirms the prompt and passes the provider index back to content.
         *
         * @param providerIndex providerIndex An integer representing the index of the provider
         *     chosen by the user to be returned to content.
         * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
         *     associated with this prompt.
         */
        @UiThread
        public @NonNull PromptResponse confirm(final int providerIndex) {
          ensureResult().putInt("providerIndex", providerIndex);
          return super.confirm();
        }

        /** A representation of an Identity Credential Provider. */
        public static class Provider {
          /** A base64 string for given icon for the provider; may be null. */
          public final @Nullable String icon;

          /** The name of the provider. */
          public final @NonNull String name;

          /** The id of the provider. */
          public final int id;

          /** The domain of the provider */
          public final @NonNull String domain;

          /**
           * Creates a new {@link Provider} with the given parameters.
           *
           * @param id The identification for this prompt.
           * @param icon A string base64 icon.
           * @param name The name of the {@link Provider}.
           * @param domain The domain of the {@link Provider}.
           */
          public Provider(
              final int id,
              final @NonNull String name,
              final @Nullable String icon,
              final @NonNull String domain) {
            this.id = id;
            this.icon = icon;
            this.name = name;
            this.domain = domain;
          }

          /* package */
          static @NonNull Provider fromBundle(final @NonNull GeckoBundle bundle) {
            final int id = bundle.getInt("providerIndex");
            final String icon = bundle.getString("icon");
            final String name = bundle.getString("name");
            final String domain = bundle.getString("domain");
            return new Provider(id, name, icon, domain);
          }
        }
      }

      /**
       * AccountSelectorPrompt contains the information necessary to represent a prompt that allows
       * the user to select the account they would like to use.
       */
      public static class AccountSelectorPrompt extends BasePrompt {
        /** The accounts from which the user could select. */
        public final @NonNull Account[] accounts;

        /** The name of the provider the user is trying to login with */
        public final @NonNull Provider provider;

        /**
         * Creates a new {@link AccountSelectorPrompt} with the given parameters.
         *
         * @param id The identification for this prompt.
         * @param accounts The accounts from which the user could select.
         * @param provider The provider on which the user is trying to log in.
         * @param observer A callback to notify when the prompt has been completed.
         */
        public AccountSelectorPrompt(
            @NonNull final String id,
            @NonNull final Account[] accounts,
            @NonNull final Provider provider,
            final Observer observer) {
          super(id, null, observer);
          this.accounts = accounts;
          this.provider = provider;
        }

        /**
         * Confirms the prompt and passes the account index back to content.
         *
         * @param accountIndex An integer representing the index of the account chosen by the user
         *     to be returned to content.
         * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
         *     associated with this prompt.
         */
        @UiThread
        public @NonNull PromptResponse confirm(@NonNull final int accountIndex) {
          ensureResult().putInt("accountIndex", accountIndex);
          return super.confirm();
        }

        /** A representation of an Identity Credential Provider Accounts. */
        public static class ProviderAccounts {
          /** The name of the provider. */
          public final @Nullable Provider provider;

          /** The accounts available for this provider. */
          public final @NonNull Account[] accounts;

          /** The id of this prompt. */
          public final int id;

          /**
           * Creates a new {@link ProviderAccounts} with the given parameters
           *
           * @param id The identification for this prompt.
           * @param provider The name of the provider.
           * @param accounts The list of {@link Account}s available for this provider.
           */
          public ProviderAccounts(
              final int id, @Nullable final Provider provider, @NonNull final Account[] accounts) {
            this.id = id;
            this.provider = provider;
            this.accounts = accounts;
          }

          /* package */
          static @NonNull ProviderAccounts fromBundle(final @NonNull GeckoBundle bundle) {
            final int id = bundle.getInt("accountIndex");
            final Provider provider = Provider.fromBundle(bundle.getBundle("provider"));

            final GeckoBundle[] accountsBundle = bundle.getBundleArray("accounts");
            if (accountsBundle == null) {
              return new ProviderAccounts(id, provider, new Account[0]);
            }

            final Account[] accounts = new Account[accountsBundle.length];
            for (int i = 0; i < accountsBundle.length; i++) {
              accounts[i] = Account.fromBundle(accountsBundle[i]);
            }
            return new ProviderAccounts(id, provider, accounts);
          }
        }

        /** A representation of an Identity Credential Account. */
        public static class Account {
          /** The id of the account. */
          public final int id;

          /** The email associated to this account. */
          public final @NonNull String email;

          /** The name of this account. */
          public final @NonNull String name;

          /** A base64 string for given icon for the account; may be null. */
          public final @Nullable String icon;

          /**
           * Creates a new {@link Account} with the given parameters.
           *
           * @param id The identification for this account.
           * @param email The email of this account.
           * @param name The name of this account.
           * @param icon A string base64 icon.
           */
          public Account(
              final int id,
              @NonNull final String email,
              @NonNull final String name,
              @Nullable final String icon) {
            this.email = email;
            this.name = name;
            this.icon = icon;
            this.id = id;
          }

          /* package */
          static @NonNull Account fromBundle(final @NonNull GeckoBundle bundle) {
            final int id = bundle.getInt("id");
            final String icon = bundle.getString("icon");
            final String name = bundle.getString("name");
            final String email = bundle.getString("email");
            return new Account(id, email, name, icon);
          }
        }

        /** A representation of an Identity Credential Provider for an Account Selector Prompt */
        public static class Provider {
          /** The name of the provider */
          public final @NonNull String name;

          /** The domain of the provider */
          public final @NonNull String domain;

          /** A base64 string for given icon for the provider; may be null. */
          public final @Nullable String icon;

          /**
           * Creates a new {@link Provider} with the given parameters
           *
           * @param name the name of the Provider
           * @param favicon A string base64 icon for the provider
           * @param domain A string base64 icon for the provider
           */
          public Provider(
              @NonNull final String name,
              @NonNull final String domain,
              @Nullable final String favicon) {
            this.name = name;
            this.domain = domain;
            this.icon = favicon;
          }

          /* package */
          static @NonNull Provider fromBundle(final @NonNull GeckoBundle bundle) {
            final String name = bundle.getString("name");
            final String domain = bundle.getString("domain");
            final String icon = bundle.getString("icon");
            return new Provider(name, domain, icon);
          }
        }
      }

      /**
       * PrivacyPolicyPrompt contains the information necessary to represent a prompt that allows
       * the user to indicate if agrees or not with the privacy policy of the identity credential
       * provider.
       */
      public static class PrivacyPolicyPrompt extends BasePrompt {
        /** The URL where the policy for using this provider is hosted. */
        public final @NonNull String privacyPolicyUrl;

        /** The URL where the terms of service for using this provider are hosted. */
        public final @NonNull String termsOfServiceUrl;

        /** The domain of the provider. */
        public final @NonNull String providerDomain;

        /** The host of the provider. */
        public final @NonNull String host;

        /** A base64 string for given icon for the provider; may be null. */
        public final @Nullable String icon;

        /**
         * Creates a new {@link IdentityCredential.ProviderSelectorPrompt} with the given
         * parameters.
         *
         * @param id The identification for this prompt.
         * @param privacyPolicyUrl The URL where the policy for using this provider is hosted.
         * @param termsOfServiceUrl The URL where the terms of service for using this provider are
         *     hosted.
         * @param providerDomain The domain of the provider.
         * @param host The host of the provider.
         * @param icon A base64 string for given icon for the provider; may be null.
         * @param observer A callback to notify when the prompt has been completed.
         */
        protected PrivacyPolicyPrompt(
            @NonNull final String id,
            @NonNull final String privacyPolicyUrl,
            @NonNull final String termsOfServiceUrl,
            @NonNull final String providerDomain,
            @NonNull final String host,
            @Nullable final String icon,
            @NonNull final Observer observer) {
          super(id, null, observer);
          this.privacyPolicyUrl = privacyPolicyUrl;
          this.termsOfServiceUrl = termsOfServiceUrl;
          this.providerDomain = providerDomain;
          this.host = host;
          this.icon = icon;
        }

        /**
         * Confirms the prompt and passes the provider accept value back to content.
         *
         * @param accept A boolean indicating if the user accepts or not the Privacy Policy of the
         *     provider.
         * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
         *     associated with this prompt.
         */
        @UiThread
        public @NonNull PromptResponse confirm(final boolean accept) {
          ensureResult().putBoolean("accept", accept);
          return super.confirm();
        }
      }
    }

    /**
     * ButtonPrompt contains the information necessary to represent a JavaScript confirm() call from
     * content.
     */
    class ButtonPrompt extends BasePrompt {
      /** Button type definitions for prompt responses. */
      @Retention(RetentionPolicy.SOURCE)
      @IntDef({Type.POSITIVE, Type.NEGATIVE})
      public @interface ButtonType {}

      /** Button prompt types: positive (e.g. "OK") or negative (e.g. "Cancel"”"). */
      public static class Type {
        /** Index of positive response button (eg, "Yes", "OK") */
        public static final int POSITIVE = 0;

        /** Index of negative response button (eg, "No", "Cancel") */
        public static final int NEGATIVE = 2;

        /** Utility class; do not instantiate. */
        protected Type() {}
      }

      /** The message to be displayed with this prompt; may be null. */
      public final @Nullable String message;

      /**
       * Constructs a ButtonPrompt for JavaScript confirm() dialogs.
       *
       * @param id the unique identifier for this prompt
       * @param title optional title text; may be null
       * @param message the message to display; may be null
       * @param observer observer to notify when the prompt is completed
       */
      protected ButtonPrompt(
          @NonNull final String id,
          @Nullable final String title,
          @Nullable final String message,
          @NonNull final Observer observer) {
        super(id, title, observer);
        this.message = message;
      }

      /**
       * Confirms this prompt, returning the selected button to content.
       *
       * @param selection An int representing the selected button, must be one of {@link Type}.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(@ButtonType final int selection) {
        ensureResult().putInt("button", selection);
        return super.confirm();
      }
    }

    /**
     * TextPrompt contains the information necessary to represent a Javascript prompt() call from
     * content.
     */
    class TextPrompt extends BasePrompt {
      /** The message to be displayed with this prompt; may be null. */
      public final @Nullable String message;

      /** The default value for the text field; may be null. */
      public final @Nullable String defaultValue;

      /**
       * Constructs a TextPrompt for JavaScript prompt() dialogs.
       *
       * @param id the unique identifier for this prompt
       * @param title optional title text; may be null
       * @param message the message to display; may be null
       * @param defaultValue the default input value; may be null
       * @param observer observer to notify when the prompt is completed
       */
      protected TextPrompt(
          @NonNull final String id,
          @Nullable final String title,
          @Nullable final String message,
          @Nullable final String defaultValue,
          @NonNull final Observer observer) {
        super(id, title, observer);
        this.message = message;
        this.defaultValue = defaultValue;
      }

      /**
       * Confirms this prompt, returning the input text to content.
       *
       * @param text A String containing the text input given by the user.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(@NonNull final String text) {
        ensureResult().putString("text", text);
        return super.confirm();
      }
    }

    /**
     * AuthPrompt contains the information necessary to represent an HTML authorization prompt
     * generated by content.
     */
    class AuthPrompt extends BasePrompt {
      /** Defines available authentication options and their flags for authorization prompts. */
      public static class AuthOptions {
        /** Authentication flag type definitions for auth prompts. */
        @Retention(RetentionPolicy.SOURCE)
        @IntDef(
            flag = true,
            value = {
              Flags.HOST,
              Flags.PROXY,
              Flags.ONLY_PASSWORD,
              Flags.PREVIOUS_FAILED,
              Flags.CROSS_ORIGIN_SUB_RESOURCE
            })
        public @interface AuthFlag {}

        /** Auth prompt flags. */
        public static class Flags {
          /** The auth prompt is for a network host. */
          public static final int HOST = 1 << 0;

          /** The auth prompt is for a proxy. */
          public static final int PROXY = 1 << 1;

          /** The auth prompt should only request a password. */
          public static final int ONLY_PASSWORD = 1 << 3;

          /** The auth prompt is the result of a previous failed login. */
          public static final int PREVIOUS_FAILED = 1 << 4;

          /** The auth prompt is for a cross-origin sub-resource. */
          public static final int CROSS_ORIGIN_SUB_RESOURCE = 1 << 5;

          /** Utility class; do not instantiate. */
          protected Flags() {}
        }

        /** Authentication level type definitions for auth security. */
        @Retention(RetentionPolicy.SOURCE)
        @IntDef({Level.NONE, Level.PW_ENCRYPTED, Level.SECURE})
        public @interface AuthLevel {}

        /** Auth prompt levels. */
        public static class Level {
          /** The auth request is unencrypted or the encryption status is unknown. */
          public static final int NONE = 0;

          /** The auth request only encrypts password but not data. */
          public static final int PW_ENCRYPTED = 1;

          /** The auth request encrypts both password and data. */
          public static final int SECURE = 2;

          /** Utility class; do not instantiate. */
          protected Level() {}
        }

        /** An int bit-field of {@link Flags}. */
        public @AuthFlag final int flags;

        /** A string containing the URI for the auth request or null if unknown. */
        public @Nullable final String uri;

        /** An int, one of {@link Level}, indicating level of encryption. */
        public @AuthLevel final int level;

        /** A string containing the initial username or null if password-only. */
        public @Nullable final String username;

        /** A string containing the initial password. */
        public @Nullable final String password;

        /* package */ AuthOptions(final GeckoBundle options) {
          flags = options.getInt("flags");
          uri = options.getString("uri");
          level = options.getInt("level");
          username = options.getString("username");
          password = options.getString("password");
        }

        /** Empty constructor for tests */
        protected AuthOptions() {
          flags = 0;
          uri = "";
          level = Level.NONE;
          username = "";
          password = "";
        }
      }

      /** The message to be displayed with this prompt; may be null. */
      public final @Nullable String message;

      /** The {@link AuthOptions} that describe the type of authorization prompt. */
      public final @NonNull AuthOptions authOptions;

      /**
       * Constructs an AuthPrompt for HTTP authentication requests.
       *
       * @param id the unique identifier for this prompt
       * @param title optional title text for the prompt; may be null
       * @param message the message to display; may be null
       * @param authOptions the AuthOptions describing flags, URI, and encryption level
       * @param observer the observer to notify when the prompt is completed
       */
      protected AuthPrompt(
          @NonNull final String id,
          @Nullable final String title,
          @Nullable final String message,
          @NonNull final AuthOptions authOptions,
          @NonNull final Observer observer) {
        super(id, title, observer);
        this.message = message;
        this.authOptions = authOptions;
      }

      /**
       * Confirms this prompt with just a password, returning the password to content.
       *
       * @param password A String containing the password input by the user.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(@NonNull final String password) {
        ensureResult().putString("password", password);
        return super.confirm();
      }

      /**
       * Confirms this prompt with a username and password, returning both to content.
       *
       * @param username A String containing the username input by the user.
       * @param password A String containing the password input by the user.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(
          @NonNull final String username, @NonNull final String password) {
        ensureResult().putString("username", username);
        ensureResult().putString("password", password);
        return super.confirm();
      }
    }

    /**
     * ChoicePrompt contains the information necessary to display a menu or list prompt generated by
     * content.
     */
    class ChoicePrompt extends BasePrompt {
      /** Defines one selectable element in a ChoicePrompt. */
      public static class Choice {
        /**
         * A boolean indicating if the item is disabled. Item should not be selectable if this is
         * true.
         */
        public final boolean disabled;

        /**
         * A String giving the URI of the item icon, or null if none exists (only valid for menus)
         */
        public final @Nullable String icon;

        /** A String giving the ID of the item or group */
        public final @NonNull String id;

        /** A Choice array of sub-items in a group, or null if not a group */
        public final @Nullable Choice[] items;

        /** A string giving the label for displaying the item or group */
        public final @NonNull String label;

        /** A boolean indicating if the item should be pre-selected (pre-checked for menu items) */
        public final boolean selected;

        /** A boolean indicating if the item should be a menu separator (only valid for menus) */
        public final boolean separator;

        /* package */ Choice(final GeckoBundle choice) {
          disabled = choice.getBoolean("disabled");
          icon = choice.getString("icon");
          id = choice.getString("id");
          label = choice.getString("label", "");
          selected = choice.getBoolean("selected");
          separator = choice.getBoolean("separator");

          final GeckoBundle[] choices = choice.getBundleArray("items");
          if (choices == null) {
            items = null;
          } else {
            items = new Choice[choices.length];
            for (int i = 0; i < choices.length; i++) {
              items[i] = new Choice(choices[i]);
            }
          }
        }

        /** Empty constructor for tests. */
        protected Choice() {
          disabled = false;
          icon = "";
          id = "";
          label = "";
          selected = false;
          separator = false;
          items = null;
        }
      }

      /** Choice type definitions for selection prompts. */
      @Retention(RetentionPolicy.SOURCE)
      @IntDef({Type.MENU, Type.SINGLE, Type.MULTIPLE})
      public @interface ChoiceType {}

      /** Choice prompt types definitions. */
      public static class Type {
        /** Display choices in a menu that dismisses as soon as an item is chosen. */
        public static final int MENU = 1;

        /** Display choices in a list that allows a single selection. */
        public static final int SINGLE = 2;

        /** Display choices in a list that allows multiple selections. */
        public static final int MULTIPLE = 3;

        /** Utility class; do not instantiate. */
        protected Type() {}
      }

      /** The message to be displayed with this prompt; may be null. */
      public final @Nullable String message;

      /** One of {@link Type}. */
      public final @ChoiceType int type;

      /** An array of {@link Choice} representing possible choices. */
      public final @NonNull Choice[] choices;

      /**
       * Constructs a ChoicePrompt with the specified message, type, and choices.
       *
       * @param id the unique identifier for this prompt
       * @param title optional title text for the prompt; may be null
       * @param message the message to be displayed with this prompt; may be null
       * @param type the choice prompt type (menu, single, or multiple)
       * @param choices the array of Choice objects representing possible selections
       * @param observer the observer to notify when the prompt is completed
       */
      protected ChoicePrompt(
          @NonNull final String id,
          @Nullable final String title,
          @Nullable final String message,
          @ChoiceType final int type,
          @NonNull final Choice[] choices,
          @NonNull final Observer observer) {
        super(id, title, observer);
        this.message = message;
        this.type = type;
        this.choices = choices;
      }

      /**
       * Confirms this prompt with the string id of a single choice.
       *
       * @param selectedId The string ID of the selected choice.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(@NonNull final String selectedId) {
        return confirm(new String[] {selectedId});
      }

      /**
       * Confirms this prompt with the string ids of multiple choices
       *
       * @param selectedIds The string IDs of the selected choices.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(@NonNull final String[] selectedIds) {
        if ((Type.MENU == type || Type.SINGLE == type)
            && (selectedIds == null || selectedIds.length != 1)) {
          throw new IllegalArgumentException();
        }
        ensureResult().putStringArray("choices", selectedIds);
        return super.confirm();
      }

      /**
       * Confirms this prompt with a single choice.
       *
       * @param selectedChoice The selected choice.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(@NonNull final Choice selectedChoice) {
        return confirm(selectedChoice == null ? null : selectedChoice.id);
      }

      /**
       * Confirms this prompt with multiple choices.
       *
       * @param selectedChoices The selected choices.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(@NonNull final Choice[] selectedChoices) {
        if ((Type.MENU == type || Type.SINGLE == type)
            && (selectedChoices == null || selectedChoices.length != 1)) {
          throw new IllegalArgumentException();
        }

        if (selectedChoices == null) {
          return confirm((String[]) null);
        }

        final String[] ids = new String[selectedChoices.length];
        for (int i = 0; i < ids.length; i++) {
          ids[i] = (selectedChoices[i] == null) ? null : selectedChoices[i].id;
        }

        return confirm(ids);
      }
    }

    /**
     * ColorPrompt contains the information necessary to represent a prompt for color input
     * generated by content.
     */
    class ColorPrompt extends BasePrompt {
      /** The default value supplied by content. */
      public final @Nullable String defaultValue;

      /** The predefined values by &lt;datalist&gt; element */
      public final @Nullable String[] predefinedValues;

      /**
       * Constructs a ColorPrompt for color input generated by content.
       *
       * @param id the unique identifier for this prompt
       * @param title the title text for the prompt; may be null
       * @param defaultValue the default color value supplied by content; may be null
       * @param predefinedValues the array of predefined colors from &lt;datalist&gt; may be null
       * @param observer the observer to notify when the prompt is completed
       */
      protected ColorPrompt(
          @NonNull final String id,
          @Nullable final String title,
          @Nullable final String defaultValue,
          @Nullable final String[] predefinedValues,
          @NonNull final Observer observer) {
        super(id, title, observer);
        this.defaultValue = defaultValue;
        this.predefinedValues = predefinedValues;
      }

      /**
       * Confirms the prompt and passes the color value back to content.
       *
       * @param color A String representing the color to be returned to content.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(@NonNull final String color) {
        ensureResult().putString("color", color);
        return super.confirm();
      }
    }

    /**
     * DateTimePrompt contains the information necessary to represent a prompt for date and/or time
     * input generated by content.
     */
    class DateTimePrompt extends BasePrompt {
      /** Datetime type definitions for date/time prompts. */
      @Retention(RetentionPolicy.SOURCE)
      @IntDef({Type.DATE, Type.MONTH, Type.WEEK, Type.TIME, Type.DATETIME_LOCAL})
      public @interface DatetimeType {}

      /** Prompt for date/time input types. */
      public static class Type {
        /** Prompt for year, month, and day. */
        public static final int DATE = 1;

        /** Prompt for year and month. */
        public static final int MONTH = 2;

        /** Prompt for year and week. */
        public static final int WEEK = 3;

        /** Prompt for hour and minute. */
        public static final int TIME = 4;

        /** Prompt for year, month, day, hour, and minute, without timezone. */
        public static final int DATETIME_LOCAL = 5;

        /** Utility class; do not instantiate. */
        protected Type() {}
      }

      /** One of {@link Type} indicating the type of prompt. */
      public final @DatetimeType int type;

      /** A String representing the default value supplied by content. */
      public final @Nullable String defaultValue;

      /** A String representing the minimum value allowed by content. */
      public final @Nullable String minValue;

      /** A String representing the maximum value allowed by content. */
      public final @Nullable String maxValue;

      /** A String representing the step value allowed by content. */
      public final @Nullable String stepValue;

      /** For testing. */
      private DateTimePrompt() {
        // Initialize final members
        super("", null, null);
        this.type = Type.DATE;
        this.defaultValue = null;
        this.minValue = null;
        this.maxValue = null;
        this.stepValue = null;
      }

      /* package */ DateTimePrompt(
          @NonNull final String id,
          @Nullable final String title,
          @DatetimeType final int type,
          @Nullable final String defaultValue,
          @Nullable final String minValue,
          @Nullable final String maxValue,
          @Nullable final String stepValue,
          @NonNull final Observer observer) {
        super(id, title, observer);
        this.type = type;
        this.defaultValue = defaultValue;
        this.minValue = minValue;
        this.maxValue = maxValue;
        this.stepValue = stepValue;
      }

      /**
       * Confirms the prompt and passes the date and/or time value back to content.
       *
       * @param datetime A String representing the date and time to be returned to content.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(@NonNull final String datetime) {
        ensureResult().putString("datetime", datetime);
        return super.confirm();
      }
    }

    /**
     * FilePrompt contains the information necessary to represent a prompt for a file or files
     * generated by content.
     */
    class FilePrompt extends BasePrompt {
      /** File type definitions for file selection prompts. */
      @Retention(RetentionPolicy.SOURCE)
      @IntDef({Type.SINGLE, Type.MULTIPLE, Type.FOLDER})
      public @interface FileType {}

      /** Types of file prompts. */
      public static class Type {
        /** Prompt for a single file. */
        public static final int SINGLE = 1;

        /** Prompt for multiple files. */
        public static final int MULTIPLE = 2;

        /** Prompt for directory. */
        public static final int FOLDER = 3;

        /** Utility class; do not instantiate. */
        protected Type() {}
      }

      /** Capture type definitions for file input capture options. */
      @Retention(RetentionPolicy.SOURCE)
      @IntDef({Capture.NONE, Capture.ANY, Capture.USER, Capture.ENVIRONMENT})
      public @interface CaptureType {}

      /** Possible capture attribute values. */
      public static class Capture {
        // These values should match the corresponding values in nsIFilePicker.idl
        /** No capture attribute has been supplied by content. */
        public static final int NONE = 0;

        /** The capture attribute was supplied with a missing or invalid value. */
        public static final int ANY = 1;

        /** The "user" capture attribute has been supplied by content. */
        public static final int USER = 2;

        /** The "environment" capture attribute has been supplied by content. */
        public static final int ENVIRONMENT = 3;

        /** Utility class; do not instantiate. */
        protected Capture() {}
      }

      /** One of {@link Type} indicating the prompt type. */
      public final @FileType int type;

      /**
       * An array of Strings giving the MIME types specified by the "accept" attribute, if any are
       * specified.
       */
      public final @Nullable String[] mimeTypes;

      /** One of {@link Capture} indicating the capture attribute supplied by content. */
      public final @CaptureType int capture;

      /**
       * Constructs a FilePrompt for file selection inputs.
       *
       * @param id the unique identifier for this prompt
       * @param title the title text for the prompt; may be null
       * @param type the file prompt type (single, multiple, or folder)
       * @param capture the capture attribute specified by content
       * @param mimeTypes the array of MIME types from the "accept" attribute; may be null
       * @param observer the observer to notify when the prompt is completed
       */
      protected FilePrompt(
          @NonNull final String id,
          @Nullable final String title,
          @FileType final int type,
          @CaptureType final int capture,
          @Nullable final String[] mimeTypes,
          @NonNull final Observer observer) {
        super(id, title, observer);
        this.type = type;
        this.capture = capture;
        this.mimeTypes = mimeTypes;
      }

      /**
       * Confirms the prompt and passes the file URI back to content.
       *
       * @param context An Application context for parsing URIs.
       * @param uri The URI of the file chosen by the user.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(
          @NonNull final Context context, @NonNull final Uri uri) {
        return confirm(context, new Uri[] {uri});
      }

      /**
       * Confirms the prompt and passes the file URIs back to content.
       *
       * @param context An Application context for parsing URIs.
       * @param uris The URIs of the files chosen by the user.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(
          @NonNull final Context context, @NonNull final Uri[] uris) {
        if ((Type.SINGLE == type || Type.FOLDER == type) && (uris == null || uris.length != 1)) {
          throw new IllegalArgumentException();
        }

        final String[] paths = new String[uris != null ? uris.length : 0];
        for (int i = 0; i < paths.length; i++) {
          paths[i] = getFile(context, uris[i]);
          if (paths[i] == null) {
            if (DEBUG) {
              Log.e(LOGTAG, "Only file URIs are supported: " + uris[i]);
            }
          }
        }
        ensureResult().putStringArray("files", paths);

        if (Type.FOLDER == type && uris[0] != null) {
          GeckoBundle[] filesInWebKitDirectory = filesInWebKitDirectory = new GeckoBundle[0];
          try {
            if (DocumentsContract.isTreeUri(uris[0])) {
              filesInWebKitDirectory =
                  IntentUtils.traverseTreeUri(context, uris[0]).stream()
                      .map(f -> f.toGeckoBundle())
                      .toArray(GeckoBundle[]::new);
            }
          } catch (final OutOfMemoryError e) {
            Log.e(LOGTAG, "Cannot traverse child directories", e);
          }
          ensureResult().putBundleArray("filesInWebKitDirectory", filesInWebKitDirectory);
        }

        return super.confirm();
      }

      private static String getFile(final @NonNull Context context, final @NonNull Uri uri) {
        if (uri == null) {
          return null;
        }
        if ("file".equals(uri.getScheme())) {
          return uri.getPath();
        }
        if ("content".equals(uri.getScheme())) {
          if (DocumentsContract.isTreeUri(uri)) {
            return IntentUtils.resolveTreeUri(context, uri);
          }
          if (DocumentsContract.isDocumentUri(context, uri)) {
            return IntentUtils.resolveDocumentUri(context, uri);
          }
          return IntentUtils.resolveContentUri(context, uri);
        }
        return null;
      }
    }

    /** PopupPrompt contains the information necessary to represent a popup blocking request. */
    class PopupPrompt extends BasePrompt {
      /** The target URI for the popup; may be null. */
      public final @Nullable String targetUri;

      /**
       * Constructor for PopupPrompt with the given target URI.
       *
       * @param id Unique identifier for this prompt.
       * @param targetUri The target URI for the popup; may be null.
       * @param observer Observer to notify when the prompt is completed.
       */
      protected PopupPrompt(
          @NonNull final String id,
          @Nullable final String targetUri,
          @NonNull final Observer observer) {
        super(id, null, observer);
        this.targetUri = targetUri;
      }

      /**
       * Confirms the prompt and either allows or blocks the popup.
       *
       * @param response An {@link AllowOrDeny} specifying whether to allow or deny the popup.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(@NonNull final AllowOrDeny response) {
        final boolean res = AllowOrDeny.ALLOW == response;
        ensureResult().putBoolean("response", res);
        return super.confirm();
      }
    }

    /**
     * RedirectPrompt contains the information necessary to represent a redirect blocking request.
     */
    class RedirectPrompt extends BasePrompt {
      /** The target URI for the redirect; may be null. */
      public final @Nullable String targetUri;

      /**
       * Constructor for RedirectPrompt with the given target URI.
       *
       * @param id Unique identifier for this prompt.
       * @param targetUri The target URI for the redirect; may be null.
       * @param observer Observer to notify when the prompt is completed.
       */
      protected RedirectPrompt(
          @NonNull final String id,
          @Nullable final String targetUri,
          @NonNull final Observer observer) {
        super(id, null, observer);
        this.targetUri = targetUri;
      }

      /**
       * Confirms the prompt and either allows or blocks the redirect.
       *
       * @param response An {@link AllowOrDeny} specifying whether to allow or deny the redirect.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(@NonNull final AllowOrDeny response) {
        final boolean res = AllowOrDeny.ALLOW == response;
        ensureResult().putBoolean("response", res);
        return super.confirm();
      }
    }

    /** SharePrompt contains the information necessary to represent a (v1) WebShare request. */
    class SharePrompt extends BasePrompt {
      /** Share result type definitions for share operation outcomes. */
      @Retention(RetentionPolicy.SOURCE)
      @IntDef({Result.SUCCESS, Result.FAILURE, Result.ABORT})
      public @interface ShareResult {}

      /** Possible results to a {@link SharePrompt}. */
      public static class Result {
        /** The user shared with another app successfully. */
        public static final int SUCCESS = 0;

        /** The user attempted to share with another app, but it failed. */
        public static final int FAILURE = 1;

        /** The user aborted the share. */
        public static final int ABORT = 2;

        /** Utility class; do not instantiate. */
        protected Result() {}
      }

      /** The text for the share request. */
      public final @Nullable String text;

      /** The uri for the share request. */
      public final @Nullable String uri;

      /**
       * Constructor for SharePrompt carrying share text and URI.
       *
       * @param id Unique identifier for this prompt.
       * @param title Optional title for the share prompt; may be null.
       * @param text The text to share; may be null.
       * @param uri The URI to share; may be null.
       * @param observer Observer to notify when the prompt is completed.
       */
      protected SharePrompt(
          @NonNull final String id,
          @Nullable final String title,
          @Nullable final String text,
          @Nullable final String uri,
          @NonNull final Observer observer) {
        super(id, title, observer);
        this.text = text;
        this.uri = uri;
      }

      /**
       * Confirms the prompt and either blocks or allows the share request.
       *
       * @param response One of {@link Result} specifying the outcome of the share attempt.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(@ShareResult final int response) {
        ensureResult().putInt("response", response);
        return super.confirm();
      }

      /**
       * Dismisses the prompt and returns {@link Result#ABORT} to web content.
       *
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse dismiss() {
        ensureResult().putInt("response", Result.ABORT);
        return super.dismiss();
      }
    }

    /** CertificateRequest represents a request for a client authentication certificate. */
    class CertificateRequest extends BasePrompt {
      /** The host requesting the certificate. */
      public final @NonNull String host;

      /** The X.500 Distinguished Names the server specified as acceptable issuers. */
      public final @Nullable Principal[] issuers;

      /**
       * Constructor for CertificateRequest containing host and issuer details.
       *
       * @param id the unique identifier for this prompt
       * @param observer the observer to notify when the prompt is completed
       * @param host the host requesting the client certificate
       * @param issuers the acceptable certificate issuers as distinguished names (may be null)
       */
      protected CertificateRequest(
          final @NonNull String id,
          final Observer observer,
          final String host,
          final Principal[] issuers) {
        super(id, null, observer);
        this.host = host;
        this.issuers = issuers;
      }

      /**
       * Complete the request by responding with the alias of the selected certificate (or null if
       * none was selected).
       *
       * @param alias The alias of the certificate selected (may be null).
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(final @Nullable String alias) {
        ensureResult().putString("alias", alias);
        return super.confirm();
      }
    }

    /** Request containing information required to resolve Autocomplete prompt requests. */
    class AutocompleteRequest<T extends Autocomplete.Option<?>> extends BasePrompt {
      /**
       * The Autocomplete options for this request. This can contain a single or multiple entries.
       */
      public final @NonNull T[] options;

      /**
       * Constructor for AutocompleteRequest with available options.
       *
       * @param id the unique identifier for this prompt
       * @param options the array of autocomplete options
       * @param observer the observer to notify when the prompt is completed
       */
      protected AutocompleteRequest(
          final @NonNull String id, final @NonNull T[] options, final Observer observer) {
        super(id, null, observer);
        this.options = options;
      }

      /**
       * Confirm the request by responding with a selection. See the PromptDelegate callbacks for
       * specifics.
       *
       * @param selection The {@link Autocomplete.Option} used to confirm the request.
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse confirm(final @NonNull Autocomplete.Option<?> selection) {
        ensureResult().putBundle("selection", selection.toBundle());
        return super.confirm();
      }

      /**
       * Dismiss the request. See the PromptDelegate callbacks for specifics.
       *
       * @return A {@link PromptResponse} which can be used to complete the {@link GeckoResult}
       *     associated with this prompt.
       */
      @UiThread
      public @NonNull PromptResponse dismiss() {
        return super.dismiss();
      }
    }

    // Delegate functions.
    /**
     * Display an alert prompt.
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link AlertPrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onAlertPrompt(
        @NonNull final GeckoSession session, @NonNull final AlertPrompt prompt) {
      return null;
    }

    /**
     * Display a onbeforeunload prompt.
     *
     * <p>See https://developer.mozilla.org/en-US/docs/Web/API/WindowEventHandlers/onbeforeunload
     * See {@link BeforeUnloadPrompt}
     *
     * @param session GeckoSession that triggered the prompt
     * @param prompt the {@link BeforeUnloadPrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to {@link AllowOrDeny#ALLOW} if the page is allowed
     *     to continue with the navigation or {@link AllowOrDeny#DENY} otherwise.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onBeforeUnloadPrompt(
        @NonNull final GeckoSession session, @NonNull final BeforeUnloadPrompt prompt) {
      return null;
    }

    /**
     * Display a POST resubmission confirmation prompt.
     *
     * <p>This prompt will trigger whenever refreshing or navigating to a page needs resubmitting
     * POST data that has been submitted already.
     *
     * @param session GeckoSession that triggered the prompt
     * @param prompt the {@link RepostConfirmPrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to {@link AllowOrDeny#ALLOW} if the page is allowed
     *     to continue with the navigation and resubmit the POST data or {@link AllowOrDeny#DENY}
     *     otherwise.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onRepostConfirmPrompt(
        @NonNull final GeckoSession session, @NonNull final RepostConfirmPrompt prompt) {
      return null;
    }

    /**
     * Display a button prompt.
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link ButtonPrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onButtonPrompt(
        @NonNull final GeckoSession session, @NonNull final ButtonPrompt prompt) {
      return null;
    }

    /**
     * Display a folder upload prompt.
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link FolderUploadPrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onFolderUploadPrompt(
        @NonNull final GeckoSession session, @NonNull final FolderUploadPrompt prompt) {
      return null;
    }

    /**
     * Display a WebAuthn related origin confirmation prompt.
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link WebAuthnRelatedOriginPrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} with allow=true if the
     *     user confirmed, or allow=false if the user denied.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onWebAuthnRelatedOriginPrompt(
        @NonNull final GeckoSession session, @NonNull final WebAuthnRelatedOriginPrompt prompt) {
      return GeckoResult.fromValue(prompt.confirm(AllowOrDeny.DENY));
    }

    /**
     * Display a text prompt.
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link TextPrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onTextPrompt(
        @NonNull final GeckoSession session, @NonNull final TextPrompt prompt) {
      return null;
    }

    /**
     * Display an authorization prompt.
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link AuthPrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onAuthPrompt(
        @NonNull final GeckoSession session, @NonNull final AuthPrompt prompt) {
      return null;
    }

    /**
     * Display a list/menu prompt.
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link ChoicePrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onChoicePrompt(
        @NonNull final GeckoSession session, @NonNull final ChoicePrompt prompt) {
      return null;
    }

    /**
     * Display a color prompt.
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link ColorPrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onColorPrompt(
        @NonNull final GeckoSession session, @NonNull final ColorPrompt prompt) {
      return null;
    }

    /**
     * Display a date/time prompt.
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link DateTimePrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onDateTimePrompt(
        @NonNull final GeckoSession session, @NonNull final DateTimePrompt prompt) {
      return null;
    }

    /**
     * Display a file prompt.
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link FilePrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onFilePrompt(
        @NonNull final GeckoSession session, @NonNull final FilePrompt prompt) {
      return null;
    }

    /**
     * Display a popup request prompt; this occurs when content attempts to open a new window in a
     * way that doesn't appear to be the result of user input.
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link PopupPrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onPopupPrompt(
        @NonNull final GeckoSession session, @NonNull final PopupPrompt prompt) {
      return null;
    }

    /**
     * Display a redirect request prompt; this occurs when a third-party frame attempts to redirect
     * the top-level window in a way that doesn't appear to be the result of user input.
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link RedirectPrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onRedirectPrompt(
        @NonNull final GeckoSession session, @NonNull final RedirectPrompt prompt) {
      return null;
    }

    /**
     * Display a share request prompt; this occurs when content attempts to use the WebShare API.
     * See: https://developer.mozilla.org/en-US/docs/Web/API/Navigator/share
     *
     * @param session GeckoSession that triggered the prompt.
     * @param prompt The {@link SharePrompt} that describes the prompt.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onSharePrompt(
        @NonNull final GeckoSession session, @NonNull final SharePrompt prompt) {
      return null;
    }

    /**
     * Handle a login save prompt request. This is triggered by the user entering new or modified
     * login credentials into a login form.
     *
     * @param session The {@link GeckoSession} that triggered the request.
     * @param request The {@link AutocompleteRequest} containing the request details.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse}.
     *     <p>Confirm the request with an {@link Autocomplete.Option} to trigger a {@link
     *     Autocomplete.StorageDelegate#onLoginSave} request to save the given selection. The
     *     confirmed selection may be an entry out of the request's options, a modified option, or a
     *     freshly created login entry.
     *     <p>Dismiss the request to deny the saving request.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onLoginSave(
        @NonNull final GeckoSession session,
        @NonNull final AutocompleteRequest<Autocomplete.LoginSaveOption> request) {
      return null;
    }

    /**
     * Handle a address save prompt request. This is triggered by the user entering new or modified
     * address credentials into a address form.
     *
     * @param session The {@link GeckoSession} that triggered the request.
     * @param request The {@link AutocompleteRequest} containing the request details.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse}.
     *     <p>Confirm the request with an {@link Autocomplete.Option} to trigger a {@link
     *     Autocomplete.StorageDelegate#onAddressSave} request to save the given selection. The
     *     confirmed selection may be an entry out of the request's options, a modified option, or a
     *     freshly created address entry.
     *     <p>Dismiss the request to deny the saving request.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onAddressSave(
        @NonNull final GeckoSession session,
        @NonNull final AutocompleteRequest<Autocomplete.AddressSaveOption> request) {
      return null;
    }

    /**
     * Handle a request for a client authentication certificate. This will occur when a host
     * requests one during the TLS handshake.
     *
     * @param session The {@link GeckoSession} that triggered the request.
     * @param request The {@link CertificateRequest} containing the request details.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse}.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onRequestCertificate(
        @NonNull final GeckoSession session, @NonNull final CertificateRequest request) {
      return null;
    }

    /**
     * Handle a credit card save prompt request. This is triggered by the user entering new or
     * modified credit card credentials into a form.
     *
     * @param session The {@link GeckoSession} that triggered the request.
     * @param request The {@link AutocompleteRequest} containing the request details.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse}.
     *     <p>Confirm the request with an {@link Autocomplete.Option} to trigger a {@link
     *     Autocomplete.StorageDelegate#onCreditCardSave} request to save the given selection. The
     *     confirmed selection may be an entry out of the request's options, a modified option, or a
     *     freshly created credit card entry.
     *     <p>Dismiss the request to deny the saving request.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onCreditCardSave(
        @NonNull final GeckoSession session,
        @NonNull final AutocompleteRequest<Autocomplete.CreditCardSaveOption> request) {
      return null;
    }

    /**
     * Handle a login selection prompt request. This is triggered by the user focusing on a login
     * username field.
     *
     * @param session The {@link GeckoSession} that triggered the request.
     * @param request The {@link AutocompleteRequest} containing the request details.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse}
     *     <p>Confirm the request with an {@link Autocomplete.Option} to let GeckoView fill out the
     *     login forms with the given selection details. The confirmed selection may be an entry out
     *     of the request's options, a modified option, or a freshly created login entry.
     *     <p>Dismiss the request to deny autocompletion for the detected form.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onLoginSelect(
        @NonNull final GeckoSession session,
        @NonNull final AutocompleteRequest<Autocomplete.LoginSelectOption> request) {
      return null;
    }

    /**
     * Handle an Identity Credential Provider selection prompt request. This is triggered by the
     * user focusing on selecting a provider for authenticating.
     *
     * @param session The {@link GeckoSession} that triggered the request.
     * @param prompt The {@link ProviderSelectorPrompt} containing the request details.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onSelectIdentityCredentialProvider(
        @NonNull final GeckoSession session, @NonNull final ProviderSelectorPrompt prompt) {
      return null;
    }

    /**
     * Handle an Identity Credential Account selection prompt request. This is triggered by the user
     * focusing on selecting a provider for authenticating.
     *
     * @param session The {@link GeckoSession} that triggered the request.
     * @param prompt The {@link ProviderSelectorPrompt} containing the request details.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onSelectIdentityCredentialAccount(
        @NonNull final GeckoSession session, @NonNull final AccountSelectorPrompt prompt) {
      return null;
    }

    /**
     * Handle an Identity Credential privacy policy prompt request.
     *
     * @param session The {@link GeckoSession} that triggered the request.
     * @param prompt The {@link PrivacyPolicyPrompt} containing the request details.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse} which includes all
     *     necessary information to resolve the prompt.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onShowPrivacyPolicyIdentityCredential(
        @NonNull final GeckoSession session, @NonNull final PrivacyPolicyPrompt prompt) {
      return null;
    }

    /**
     * Handle a credit card selection prompt request. This is triggered by the user focusing on a
     * credit card input field.
     *
     * @param session The {@link GeckoSession} that triggered the request.
     * @param request The {@link AutocompleteRequest} containing the request details.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse}
     *     <p>Confirm the request with an {@link Autocomplete.Option} to let GeckoView fill out the
     *     credit card forms with the given selection details. The confirmed selection may be an
     *     entry out of the request's options, a modified option, or a freshly created credit card
     *     entry.
     *     <p>Dismiss the request to deny autocompletion for the detected form.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onCreditCardSelect(
        @NonNull final GeckoSession session,
        @NonNull final AutocompleteRequest<Autocomplete.CreditCardSelectOption> request) {
      return null;
    }

    /**
     * Handle a address selection prompt request. This is triggered by the user focusing on a
     * address field.
     *
     * @param session The {@link GeckoSession} that triggered the request.
     * @param request The {@link AutocompleteRequest} containing the request details.
     * @return A {@link GeckoResult} resolving to a {@link PromptResponse}
     *     <p>Confirm the request with an {@link Autocomplete.Option} to let GeckoView fill out the
     *     address forms with the given selection details. The confirmed selection may be an entry
     *     out of the request's options, a modified option, or a freshly created address entry.
     *     <p>Dismiss the request to deny autocompletion for the detected form.
     */
    @UiThread
    default @Nullable GeckoResult<PromptResponse> onAddressSelect(
        @NonNull final GeckoSession session,
        @NonNull final AutocompleteRequest<Autocomplete.AddressSelectOption> request) {
      return null;
    }
  }

  /** GeckoSession applications implement this interface to handle content scroll events. */
  public interface ScrollDelegate {
    /**
     * The scroll position of the content has changed.
     *
     * @param session GeckoSession that initiated the callback.
     * @param scrollX The new horizontal scroll position in pixels.
     * @param scrollY The new vertical scroll position in pixels.
     */
    @UiThread
    default void onScrollChanged(
        @NonNull final GeckoSession session, final int scrollX, final int scrollY) {}
  }

  /** Information about an update to the content's scroll position. */
  public class ScrollPositionUpdate {
    /** The scroll position changed as a direct result of user interaction. */
    @WrapForJNI public static final int SOURCE_USER_INTERACTION = 0;

    /**
     * The scroll position changed progammatically. This can include changes caused by script on the
     * page, and changes caused by the browser engine such as scrolling an element into view.
     */
    @WrapForJNI public static final int SOURCE_OTHER = 1;

    /** The new horizontal scroll position in CSS pixels. */
    public float scrollX;

    /** The new vertical scroll position in CSS pixels. */
    public float scrollY;

    /**
     * The new zoom level. This is used to relate scrollX and scrollY, which are in CSS pixels, to
     * quantities in screen pixels. Multiply scrollX/scrollY by zoom to get screen pixels.
     */
    public float zoom;

    /** The source of the scroll position change. One of SOURCE_USER_INTERACTION or SOURCE_OTHER. */
    public int source;
  }

  /**
   * GeckoSession applications implement this interface to handle scroll events.
   *
   * <p>Differences from ScrollDelegate:
   *
   * <ul>
   *   <li>onScrollChanged() is called as soon as the scroll change is composited visually. For
   *       scrolling triggered by user interaction, this notification can have a lower latency than
   *       ScrollDelegate.onScrollChanged().
   *   <li>In addition to the scroll position in pixels, the notification contains auxiliary
   *       information such as whether the scroll change was a result of user interaction. This can
   *       be extended over time as needed.
   * </ul>
   */
  public interface CompositorScrollDelegate {
    /**
     * The scroll position of the content has changed.
     *
     * @param session GeckoSession that initiated the callback.
     * @param update Information about the scroll position change.
     */
    @UiThread
    default void onScrollChanged(
        @NonNull final GeckoSession session, @NonNull final ScrollPositionUpdate update) {}
  }

  /**
   * Get the PanZoomController instance for this session.
   *
   * @return PanZoomController instance.
   */
  @UiThread
  public @NonNull PanZoomController getPanZoomController() {
    ThreadUtils.assertOnUiThread();

    return mPanZoomController;
  }

  /**
   * Get the OverscrollEdgeEffect instance for this session.
   *
   * @return OverscrollEdgeEffect instance.
   */
  @UiThread
  public @NonNull OverscrollEdgeEffect getOverscrollEdgeEffect() {
    ThreadUtils.assertOnUiThread();

    if (mOverscroll == null) {
      mOverscroll = new OverscrollEdgeEffect();
    }
    return mOverscroll;
  }

  /**
   * Get the CompositorController instance for this session.
   *
   * @return CompositorController instance.
   */
  @UiThread
  public @NonNull CompositorController getCompositorController() {
    ThreadUtils.assertOnUiThread();

    if (mController == null) {
      mController = new CompositorController(this);
      if (mCompositorReady) {
        mController.onCompositorReady();
      }
    }
    return mController;
  }

  /**
   * Get a matrix for transforming from client coordinates to surface coordinates.
   *
   * @param matrix Matrix to be replaced by the transformation matrix.
   * @see #getClientToScreenMatrix(Matrix)
   * @see #getPageToSurfaceMatrix(Matrix)
   */
  @UiThread
  public void getClientToSurfaceMatrix(@NonNull final Matrix matrix) {
    ThreadUtils.assertOnUiThread();

    matrix.setScale(mViewportZoom, mViewportZoom);
    if (mClientTop != mTop) {
      matrix.postTranslate(0, mClientTop - mTop);
    }
  }

  /**
   * Get a matrix for transforming from client coordinates to screen coordinates. The client
   * coordinates are in CSS pixels and are relative to the viewport origin; their relation to screen
   * coordinates does not depend on the current scroll position.
   *
   * @param matrix Matrix to be replaced by the transformation matrix.
   * @see #getClientToSurfaceMatrix(Matrix)
   * @see #getPageToScreenMatrix(Matrix)
   */
  @UiThread
  public void getClientToScreenMatrix(@NonNull final Matrix matrix) {
    ThreadUtils.assertOnUiThread();

    getClientToSurfaceMatrix(matrix);
    matrix.postTranslate(mLeft, mTop);
  }

  /**
   * Get a matrix for transforming from page coordinates to screen coordinates. The page coordinates
   * are in CSS pixels and are relative to the page origin; their relation to screen coordinates
   * depends on the current scroll position of the outermost frame.
   *
   * @param matrix Matrix to be replaced by the transformation matrix.
   * @see #getPageToSurfaceMatrix(Matrix)
   * @see #getClientToScreenMatrix(Matrix)
   */
  @UiThread
  public void getPageToScreenMatrix(@NonNull final Matrix matrix) {
    ThreadUtils.assertOnUiThread();

    getPageToSurfaceMatrix(matrix);
    matrix.postTranslate(mLeft, mTop);
  }

  /**
   * Get a matrix for transforming from page coordinates to surface coordinates.
   *
   * @param matrix Matrix to be replaced by the transformation matrix.
   * @see #getPageToScreenMatrix(Matrix)
   * @see #getClientToSurfaceMatrix(Matrix)
   */
  @UiThread
  public void getPageToSurfaceMatrix(@NonNull final Matrix matrix) {
    ThreadUtils.assertOnUiThread();

    getClientToSurfaceMatrix(matrix);
    matrix.postTranslate(-mViewportLeft, -mViewportTop);
  }

  /**
   * Get a matrix for transforming from layout device client coordinates to screen coordinates.
   *
   * @param matrix Matrix to be replaced by the transformation matrix.
   * @see #getClientToScreenMatrix(Matrix)
   * @see #getPageToSurfaceMatrix(Matrix)
   */
  @UiThread
  /* package */ void getClientToScreenOffsetMatrix(@NonNull final Matrix matrix) {
    ThreadUtils.assertOnUiThread();

    matrix.postTranslate(mLeft, mTop);
  }

  /**
   * Get a matrix for transforming from screen coordinates to Android's current window coordinates.
   *
   * @param activity an Activity of this window.
   * @param matrix Matrix to be replaced by the transformation matrix.
   * @see <a
   *     href="https://developer.android.com/guide/topics/large-screens/multi-window-support#window_metrics">...</a>
   */
  @SuppressLint("BlockedPrivateApi")
  @UiThread
  /* package */ void getScreenToWindowManagerOffsetMatrix(
      @NonNull final Activity activity, @NonNull final Matrix matrix) {
    ThreadUtils.assertOnUiThread();

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
      final WindowManager wm =
          (WindowManager)
              GeckoAppShell.getApplicationContext().getSystemService(Context.WINDOW_SERVICE);
      final Rect currentWindowRect = wm.getCurrentWindowMetrics().getBounds();
      matrix.postTranslate(-currentWindowRect.left, -currentWindowRect.top);
      return;
    }

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
      try {
        // Android 9/10 hides WindowConfiguration object.
        final Configuration config = activity.getResources().getConfiguration();
        final Field windowConfigurationField =
            Configuration.class.getDeclaredField("windowConfiguration");
        windowConfigurationField.setAccessible(true);
        final Object windowConfig = windowConfigurationField.get(config);
        final Method getBoundsMethod;
        if (activity.isInMultiWindowMode()) {
          getBoundsMethod = windowConfig.getClass().getDeclaredMethod("getBounds");
        } else {
          getBoundsMethod = windowConfig.getClass().getDeclaredMethod("getAppBounds");
        }
        final Rect currentWindowRect = (Rect) getBoundsMethod.invoke(windowConfig);
        matrix.postTranslate(-currentWindowRect.left, -currentWindowRect.top);
      } catch (final NoSuchMethodException
          | NoSuchFieldException
          | IllegalAccessException
          | InvocationTargetException e) {
        Log.e(LOGTAG, "Could not convert from screen coordinate to window manager coordinate", e);
      }
      return;
    }

    // TODO(m_kato): Bug 1678531
    // How to get window coordinate on Android 8 that supports split window?
  }

  /**
   * Get the bounds of the client area in client coordinates. The returned top-left coordinates are
   * always (0, 0). Use the matrix from {@link #getClientToSurfaceMatrix(Matrix)} or {@link
   * #getClientToScreenMatrix(Matrix)} to map these bounds to surface or screen coordinates,
   * respectively.
   *
   * @param rect RectF to be replaced by the client bounds in client coordinates.
   * @see #getSurfaceBounds(Rect)
   */
  @UiThread
  public void getClientBounds(@NonNull final RectF rect) {
    ThreadUtils.assertOnUiThread();

    rect.set(0.0f, 0.0f, (float) mWidth / mViewportZoom, (float) mClientHeight / mViewportZoom);
  }

  /**
   * Get the bounds of the client area in surface coordinates. This is equivalent to mapping the
   * bounds returned by #getClientBounds(RectF) with the matrix returned by
   * #getClientToSurfaceMatrix(Matrix).
   *
   * @param rect Rect to be replaced by the client bounds in surface coordinates.
   */
  @UiThread
  public void getSurfaceBounds(@NonNull final Rect rect) {
    ThreadUtils.assertOnUiThread();

    rect.set(0, mClientTop - mTop, mWidth, mHeight);
  }

  /**
   * GeckoSession applications implement this interface to handle requests for permissions from
   * content, such as geolocation and notifications. For each permission, usually two requests are
   * generated: one request for the Android app permission through requestAppPermissions, which is
   * typically handled by a system permission dialog; and another request for the content permission
   * (e.g. through requestContentPermission), which is typically handled by an app-specific
   * permission dialog.
   *
   * <p>When denying an Android app permission, the response is not stored by GeckoView. It is the
   * responsibility of the consumer to store the response state and therefore prevent further
   * requests from being presented to the user.
   */
  public interface PermissionDelegate {
    /**
     * Permission for using the geolocation API. See:
     * https://developer.mozilla.org/en-US/docs/Web/API/Geolocation
     */
    int PERMISSION_GEOLOCATION = 0;

    /**
     * Permission for using the notifications API. See:
     * https://developer.mozilla.org/en-US/docs/Web/API/notification
     */
    int PERMISSION_DESKTOP_NOTIFICATION = 1;

    /**
     * Permission for using the storage API. See:
     * https://developer.mozilla.org/en-US/docs/Web/API/Storage_API
     */
    int PERMISSION_PERSISTENT_STORAGE = 2;

    /** Permission for using the WebXR API. See: https://www.w3.org/TR/webxr */
    int PERMISSION_XR = 3;

    /** Permission for allowing autoplay of inaudible (silent) video. */
    int PERMISSION_AUTOPLAY_INAUDIBLE = 4;

    /** Permission for allowing autoplay of audible video. */
    int PERMISSION_AUTOPLAY_AUDIBLE = 5;

    /** Permission for accessing system media keys used to decode DRM media. */
    int PERMISSION_MEDIA_KEY_SYSTEM_ACCESS = 6;

    /**
     * Permission for trackers to operate on the page -- disables all tracking protection features
     * for a given site.
     */
    int PERMISSION_TRACKING = 7;

    /**
     * Permission for third party frames to access first party cookies. May be granted heuristically
     * in some cases.
     */
    int PERMISSION_STORAGE_ACCESS = 8;

    /** Permission for local device (loopback-network) access */
    int PERMISSION_LOCAL_DEVICE_ACCESS = 9;

    /** Permission for local network access */
    int PERMISSION_LOCAL_NETWORK_ACCESS = 10;

    /**
     * Represents a content permission -- including the type of permission, the present value of the
     * permission, the URL the permission pertains to, and other information.
     */
    class ContentPermission {
      /** Permission value type definitions for permission states. */
      @Retention(RetentionPolicy.SOURCE)
      @IntDef({VALUE_PROMPT, VALUE_DENY, VALUE_ALLOW})
      public @interface Value {}

      /** The corresponding permission is currently set to default/prompt behavior. */
      public static final int VALUE_PROMPT = 3;

      /** The corresponding permission is currently set to deny. */
      public static final int VALUE_DENY = 2;

      /** The corresponding permission is currently set to allow. */
      public static final int VALUE_ALLOW = 1;

      /** The URI associated with this content permission. */
      public final @NonNull String uri;

      /**
       * The third party origin associated with the request; currently only used for storage access
       * permission.
       */
      public final @Nullable String thirdPartyOrigin;

      /**
       * A boolean indicating whether this content permission is associated with private browsing.
       */
      public final boolean privateMode;

      /** The type of this permission; one of {@link #PERMISSION_GEOLOCATION PERMISSION_*}. */
      public final int permission;

      /** The value of the permission; one of {@link #VALUE_PROMPT VALUE_}. */
      public final @Value int value;

      /**
       * The context ID associated with the permission if any.
       *
       * @see GeckoSessionSettings.Builder#contextId
       */
      public final @Nullable String contextId;

      private final String mPrincipal;

      /**
       * Default constructor for ContentPermission. Initializes all fields to their default values.
       */
      protected ContentPermission() {
        this.uri = "";
        this.thirdPartyOrigin = null;
        this.privateMode = false;
        this.permission = PERMISSION_GEOLOCATION;
        this.value = VALUE_ALLOW;
        this.mPrincipal = "";
        this.contextId = null;
      }

      private ContentPermission(final @NonNull GeckoBundle bundle) {
        this.uri = bundle.getString("uri");
        this.mPrincipal = bundle.getString("principal");
        this.privateMode = bundle.getBoolean("privateMode");

        final String permission = bundle.getString("perm");
        this.permission = convertType(permission);
        if (permission.startsWith("3rdPartyStorage^")) {
          // Storage access permissions are stored with the key "3rdPartyStorage^https://foo.com"
          // where the third party origin is "https://foo.com".
          this.thirdPartyOrigin = permission.substring(16);
        } else if (permission.startsWith("3rdPartyFrameStorage^")) {
          // Storage access permissions may also be stored with the key
          // "3rdPartyFrameStorage^https://foo.com" where the third party
          // origin is "https://foo.com".
          this.thirdPartyOrigin = permission.substring(21);
        } else {
          this.thirdPartyOrigin = bundle.getString("thirdPartyOrigin");
        }

        this.value = bundle.getInt("value");
        this.contextId =
            StorageController.retrieveUnsafeSessionContextId(bundle.getString("contextId"));
      }

      /**
       * Converts a JSONObject to a ContentPermission -- should only be used on the output of {@link
       * #toJson()}.
       *
       * @param perm A JSONObject representing a ContentPermission, output by {@link #toJson()}.
       * @return The corresponding ContentPermission.
       */
      @AnyThread
      public static @Nullable ContentPermission fromJson(final @NonNull JSONObject perm) {
        ContentPermission res = null;
        try {
          res = new ContentPermission(GeckoBundle.fromJSONObject(perm));
        } catch (final JSONException e) {
          Log.w(LOGTAG, "Failed to create ContentPermission; invalid JSONObject.", e);
        }
        return res;
      }

      /**
       * Converts a ContentPermission to a JSONObject that can be converted back to a
       * ContentPermission by {@link #fromJson(JSONObject)}.
       *
       * @return A JSONObject representing this ContentPermission. Modifying any of the fields may
       *     result in undefined behavior when converted back to a ContentPermission and used.
       * @throws JSONException if the conversion fails for any reason.
       */
      @AnyThread
      public @NonNull JSONObject toJson() throws JSONException {
        return toGeckoBundle().toJSONObject();
      }

      private static int convertType(final @NonNull String type) {
        if ("geolocation".equals(type)) {
          return PERMISSION_GEOLOCATION;
        } else if ("desktop-notification".equals(type)) {
          return PERMISSION_DESKTOP_NOTIFICATION;
        } else if ("persistent-storage".equals(type)) {
          return PERMISSION_PERSISTENT_STORAGE;
        } else if ("xr".equals(type)) {
          return PERMISSION_XR;
        } else if ("autoplay-media-inaudible".equals(type)) {
          return PERMISSION_AUTOPLAY_INAUDIBLE;
        } else if ("autoplay-media-audible".equals(type)) {
          return PERMISSION_AUTOPLAY_AUDIBLE;
        } else if ("media-key-system-access".equals(type)) {
          return PERMISSION_MEDIA_KEY_SYSTEM_ACCESS;
        } else if ("trackingprotection".equals(type) || "trackingprotection-pb".equals(type)) {
          return PERMISSION_TRACKING;
        } else if ("storage-access".equals(type)
            || type.startsWith("3rdPartyStorage^")
            || type.startsWith("3rdPartyFrameStorage^")) {
          return PERMISSION_STORAGE_ACCESS;
        } else if ("loopback-network".equals(type)) {
          return PERMISSION_LOCAL_DEVICE_ACCESS;
        } else if ("local-network".equals(type)) {
          return PERMISSION_LOCAL_NETWORK_ACCESS;
        } else {
          return -1;
        }
      }

      // This also gets used in StorageController, so it's package rather than private.
      /* package */ static String convertType(final int type, final boolean privateMode) {
        switch (type) {
          case PERMISSION_GEOLOCATION:
            return "geolocation";
          case PERMISSION_DESKTOP_NOTIFICATION:
            return "desktop-notification";
          case PERMISSION_PERSISTENT_STORAGE:
            return "persistent-storage";
          case PERMISSION_XR:
            return "xr";
          case PERMISSION_AUTOPLAY_INAUDIBLE:
            return "autoplay-media-inaudible";
          case PERMISSION_AUTOPLAY_AUDIBLE:
            return "autoplay-media-audible";
          case PERMISSION_MEDIA_KEY_SYSTEM_ACCESS:
            return "media-key-system-access";
          case PERMISSION_TRACKING:
            return privateMode ? "trackingprotection-pb" : "trackingprotection";
          case PERMISSION_STORAGE_ACCESS:
            return "storage-access";
          case PERMISSION_LOCAL_DEVICE_ACCESS:
            return "loopback-network";
          case PERMISSION_LOCAL_NETWORK_ACCESS:
            return "local-network";
          default:
            return "";
        }
      }

      /* package */ static @NonNull ArrayList<ContentPermission> fromBundleArray(
          final @NonNull GeckoBundle[] bundleArray) {
        final ArrayList<ContentPermission> res = new ArrayList<ContentPermission>();
        if (bundleArray == null) {
          return res;
        }

        for (final GeckoBundle bundle : bundleArray) {
          final ContentPermission temp = new ContentPermission(bundle);
          if (temp.permission == -1 || temp.value < 1 || temp.value > 3) {
            continue;
          }
          res.add(temp);
        }
        return res;
      }

      /* package */ @NonNull
      GeckoBundle toGeckoBundle() {
        final GeckoBundle res = new GeckoBundle(7);
        res.putString("uri", uri);
        res.putString("thirdPartyOrigin", thirdPartyOrigin);
        res.putString("principal", mPrincipal);
        res.putBoolean("privateMode", privateMode);
        res.putString("perm", convertType(permission, privateMode));
        res.putInt("value", value);
        res.putString("contextId", contextId);
        return res;
      }
    }

    /** Callback interface for notifying the result of a permission request. */
    interface Callback {
      /**
       * Called by the implementation after permissions are granted; the implementation must call
       * either grant() or reject() for every request.
       */
      @UiThread
      default void grant() {}

      /**
       * Called by the implementation when permissions are not granted; the implementation must call
       * either grant() or reject() for every request.
       */
      @UiThread
      default void reject() {}
    }

    /**
     * Request Android app permissions.
     *
     * @param session GeckoSession instance requesting the permissions.
     * @param permissions List of permissions to request; possible values are,
     *     android.Manifest.permission.ACCESS_COARSE_LOCATION
     *     android.Manifest.permission.ACCESS_FINE_LOCATION android.Manifest.permission.CAMERA
     *     android.Manifest.permission.RECORD_AUDIO
     * @param callback Callback interface.
     */
    @UiThread
    default void onAndroidPermissionsRequest(
        @NonNull final GeckoSession session,
        @Nullable final String[] permissions,
        @NonNull final Callback callback) {
      callback.reject();
    }

    /**
     * Request content permission.
     *
     * <p>Note, that in the case of PERMISSION_PERSISTENT_STORAGE, once permission has been granted
     * for a site, it cannot be revoked. If the permission has previously been granted, it is the
     * responsibility of the consuming app to remember the permission and prevent the prompt from
     * being redisplayed to the user.
     *
     * @param session GeckoSession instance requesting the permission.
     * @param perm An {@link ContentPermission} describing the permission being requested and its
     *     current status.
     * @return A {@link GeckoResult} resolving to one of {@link ContentPermission#VALUE_PROMPT
     *     VALUE_*}, determining the response to the permission request and updating the permissions
     *     for this site.
     */
    @UiThread
    default @Nullable GeckoResult<Integer> onContentPermissionRequest(
        @NonNull final GeckoSession session, @NonNull ContentPermission perm) {
      return GeckoResult.fromValue(ContentPermission.VALUE_PROMPT);
    }

    /** Defines the set of media source categories and types used for media capture. */
    class MediaSource {
      /** Media source type definitions for media device sources. */
      @Retention(RetentionPolicy.SOURCE)
      @IntDef({
        SOURCE_CAMERA, SOURCE_SCREEN,
        SOURCE_MICROPHONE, SOURCE_AUDIOCAPTURE,
        SOURCE_OTHER
      })
      public @interface Source {}

      /** Constant to indicate that camera will be recorded. */
      public static final int SOURCE_CAMERA = 0;

      /** Constant to indicate that screen will be recorded. */
      public static final int SOURCE_SCREEN = 1;

      /** Constant to indicate that microphone will be recorded. */
      public static final int SOURCE_MICROPHONE = 2;

      /** Constant to indicate that device audio playback will be recorded. */
      public static final int SOURCE_AUDIOCAPTURE = 3;

      /** Constant to indicate a media source that does not fall under the other categories. */
      public static final int SOURCE_OTHER = 4;

      /** Media device type definitions for media capture. */
      @Retention(RetentionPolicy.SOURCE)
      @IntDef({TYPE_VIDEO, TYPE_AUDIO})
      public @interface Type {}

      /** The media type is video. */
      public static final int TYPE_VIDEO = 0;

      /** The media type is audio. */
      public static final int TYPE_AUDIO = 1;

      /** A string giving a unique source identifier. */
      public final @NonNull String id;

      /**
       * A string giving the name of the video source from the system (for example, "Camera 0,
       * Facing back, Orientation 90"). May be empty.
       */
      public final @Nullable String name;

      /**
       * An int indicating the media source type. Possible values for a video source are:
       * SOURCE_CAMERA, SOURCE_SCREEN, and SOURCE_OTHER. Possible values for an audio source are:
       * SOURCE_MICROPHONE, SOURCE_AUDIOCAPTURE, and SOURCE_OTHER.
       */
      public final @Source int source;

      /** An int giving the type of media, must be either TYPE_VIDEO or TYPE_AUDIO. */
      public final @Type int type;

      private static @Source int getSourceFromString(final String src) {
        // The strings here should match those in MediaSourceEnum in MediaStreamTrack.webidl
        if ("camera".equals(src)) {
          return SOURCE_CAMERA;
        } else if ("screen".equals(src) || "window".equals(src) || "browser".equals(src)) {
          return SOURCE_SCREEN;
        } else if ("microphone".equals(src)) {
          return SOURCE_MICROPHONE;
        } else if ("audioCapture".equals(src)) {
          return SOURCE_AUDIOCAPTURE;
        } else if ("other".equals(src) || "application".equals(src)) {
          return SOURCE_OTHER;
        } else {
          throw new IllegalArgumentException(
              "String: " + src + " is not a valid media source string");
        }
      }

      private static @Type int getTypeFromString(final String type) {
        // The strings here should match the possible types in MediaDevice::MediaDevice in
        // MediaManager.cpp
        if ("videoinput".equals(type)) {
          return TYPE_VIDEO;
        } else if ("audioinput".equals(type)) {
          return TYPE_AUDIO;
        } else {
          throw new IllegalArgumentException(
              "String: " + type + " is not a valid media type string");
        }
      }

      /* package */ MediaSource(final GeckoBundle media) {
        id = media.getString("id");
        name = media.getString("name");
        source = getSourceFromString(media.getString("mediaSource"));
        type = getTypeFromString(media.getString("type"));
      }

      /** Empty constructor for tests. */
      protected MediaSource() {
        id = null;
        name = null;
        source = SOURCE_CAMERA;
        type = TYPE_VIDEO;
      }
    }

    /**
     * Callback interface for notifying the result of a media permission request, including which
     * media source(s) to use.
     */
    interface MediaCallback {
      /**
       * Called by the implementation after permissions are granted; the implementation must call
       * one of grant() or reject() for every request.
       *
       * @param video "id" value from the bundle for the video source to use, or null when video is
       *     not requested.
       * @param audio "id" value from the bundle for the audio source to use, or null when audio is
       *     not requested.
       */
      @UiThread
      default void grant(final @Nullable String video, final @Nullable String audio) {}

      /**
       * Called by the implementation after permissions are granted; the implementation must call
       * one of grant() or reject() for every request.
       *
       * @param video MediaSource for the video source to use (must be an original MediaSource
       *     object that was passed to the implementation); or null when video is not requested.
       * @param audio MediaSource for the audio source to use (must be an original MediaSource
       *     object that was passed to the implementation); or null when audio is not requested.
       */
      @UiThread
      default void grant(final @Nullable MediaSource video, final @Nullable MediaSource audio) {}

      /**
       * Called by the implementation when permissions are not granted; the implementation must call
       * one of grant() or reject() for every request.
       */
      @UiThread
      default void reject() {}
    }

    /**
     * Request content media permissions, including request for which video and/or audio source to
     * use.
     *
     * <p>Media permissions will still be requested if the associated device permissions have been
     * denied if there are video or audio sources in that category that can still be accessed. It is
     * the responsibility of consumers to ensure that media permission requests are not displayed in
     * this case.
     *
     * @param session GeckoSession instance requesting the permission.
     * @param uri The URI of the content requesting the permission.
     * @param video List of video sources, or null if not requesting video.
     * @param audio List of audio sources, or null if not requesting audio.
     * @param callback Callback interface.
     */
    @UiThread
    default void onMediaPermissionRequest(
        @NonNull final GeckoSession session,
        @NonNull final String uri,
        @Nullable final MediaSource[] video,
        @Nullable final MediaSource[] audio,
        @NonNull final MediaCallback callback) {
      callback.reject();
    }
  }

  /** Permission type definitions for web permissions. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({
    PermissionDelegate.PERMISSION_GEOLOCATION,
    PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION,
    PermissionDelegate.PERMISSION_PERSISTENT_STORAGE,
    PermissionDelegate.PERMISSION_XR,
    PermissionDelegate.PERMISSION_AUTOPLAY_INAUDIBLE,
    PermissionDelegate.PERMISSION_AUTOPLAY_AUDIBLE,
    PermissionDelegate.PERMISSION_MEDIA_KEY_SYSTEM_ACCESS,
    PermissionDelegate.PERMISSION_TRACKING,
    PermissionDelegate.PERMISSION_STORAGE_ACCESS
  })
  public @interface Permission {}

  /**
   * Interface that SessionTextInput uses for performing operations such as opening and closing the
   * software keyboard. If the delegate is not set, these operations are forwarded to the system
   * {@link android.view.inputmethod.InputMethodManager} automatically.
   */
  public interface TextInputDelegate {
    /** Restarting input due to an input field gaining focus. */
    int RESTART_REASON_FOCUS = 0;

    /** Restarting input due to an input field losing focus. */
    int RESTART_REASON_BLUR = 1;

    /**
     * Restarting input due to the content of the input field changing. For example, the input field
     * type may have changed, or the current composition may have been committed outside of the
     * input method.
     */
    int RESTART_REASON_CONTENT_CHANGE = 2;

    /**
     * Reset the input method, and discard any existing states such as the current composition or
     * current autocompletion. Because the current focused editor may have changed, as part of the
     * reset, a custom input method would normally call {@link
     * SessionTextInput#onCreateInputConnection} to update its knowledge of the focused editor. Note
     * that {@code restartInput} should be used to detect changes in focus, rather than {@link
     * #showSoftInput} or {@link #hideSoftInput}, because focus changes are not always accompanied
     * by requests to show or hide the soft input. This method is always called, even in viewless
     * mode.
     *
     * @param session Session instance.
     * @param reason Reason for the reset.
     */
    @UiThread
    default void restartInput(
        @NonNull final GeckoSession session, @RestartReason final int reason) {}

    /**
     * Display the soft input. May be called consecutively, even if the soft input is already shown.
     * This method is always called, even in viewless mode.
     *
     * @param session Session instance.
     * @see #hideSoftInput
     */
    @UiThread
    default void showSoftInput(@NonNull final GeckoSession session) {}

    /**
     * Hide the soft input. May be called consecutively, even if the soft input is already hidden.
     * This method is always called, even in viewless mode.
     *
     * @param session Session instance.
     * @see #showSoftInput
     */
    @UiThread
    default void hideSoftInput(@NonNull final GeckoSession session) {}

    /**
     * Update the soft input on the current selection. This method is <i>not</i> called in viewless
     * mode.
     *
     * @param session Session instance.
     * @param selStart Start offset of the selection.
     * @param selEnd End offset of the selection.
     * @param compositionStart Composition start offset, or -1 if there is no composition.
     * @param compositionEnd Composition end offset, or -1 if there is no composition.
     */
    @UiThread
    default void updateSelection(
        @NonNull final GeckoSession session,
        final int selStart,
        final int selEnd,
        final int compositionStart,
        final int compositionEnd) {}

    /**
     * Update the soft input on the current extracted text, as requested through {@link
     * android.view.inputmethod.InputConnection#getExtractedText}. Consequently, this method is
     * <i>not</i> called in viewless mode.
     *
     * @param session Session instance.
     * @param request The extract text request.
     * @param text The extracted text.
     */
    @UiThread
    default void updateExtractedText(
        @NonNull final GeckoSession session,
        @NonNull final ExtractedTextRequest request,
        @NonNull final ExtractedText text) {}

    /**
     * Update the cursor-anchor information as requested through {@link
     * android.view.inputmethod.InputConnection#requestCursorUpdates}. Consequently, this method is
     * <i>not</i> called in viewless mode.
     *
     * @param session Session instance.
     * @param info Cursor-anchor information.
     */
    @UiThread
    default void updateCursorAnchorInfo(
        @NonNull final GeckoSession session, @NonNull final CursorAnchorInfo info) {}
  }

  /** Restart reason type definitions for text input restart. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({
    TextInputDelegate.RESTART_REASON_FOCUS,
    TextInputDelegate.RESTART_REASON_BLUR,
    TextInputDelegate.RESTART_REASON_CONTENT_CHANGE
  })
  public @interface RestartReason {}

  /* package */ void onSurfaceChanged(final @NonNull SurfaceInfo surfaceInfo) {
    ThreadUtils.assertOnUiThread();

    mWidth = surfaceInfo.mWidth;
    mHeight = surfaceInfo.mHeight;
    mNewSurfaceProvider = surfaceInfo.mNewSurfaceProvider;

    if (mCompositorReady) {
      mCompositor.syncResumeResizeCompositor(
          surfaceInfo.mLeft,
          surfaceInfo.mTop,
          surfaceInfo.mWidth,
          surfaceInfo.mHeight,
          surfaceInfo.mSurface,
          surfaceInfo.mSurfaceControl);
      onWindowBoundsChanged();
      return;
    }

    // We have a valid surface but we're not attached or the compositor
    // is not ready; save the surface for later when we're ready.
    mSurfaceInfo = surfaceInfo;

    // Adjust bounds as the last step.
    onWindowBoundsChanged();
  }

  /* package */ void onSurfaceDestroyed() {
    ThreadUtils.assertOnUiThread();

    mNewSurfaceProvider = null;

    if (mCompositorReady) {
      mCompositor.syncPauseCompositor();
      return;
    }

    // While the surface was valid, we never became attached or the
    // compositor never became ready; clear the saved surface.
    mSurfaceInfo = null;
  }

  /* package */ void onScreenOriginChanged(final int left, final int top) {
    ThreadUtils.assertOnUiThread();

    if (mLeft == left && mTop == top) {
      return;
    }

    mLeft = left;
    mTop = top;
    onWindowBoundsChanged();
  }

  /* package */ void setDynamicToolbarMaxHeight(final int height) {
    if (mDynamicToolbarMaxHeight == height) {
      return;
    }

    if (mHeight != 0 && height != 0 && mHeight < height) {
      Log.w(
          LOGTAG,
          new AssertionError(
              "The maximum height of the dynamic toolbar ("
                  + height
                  + ") should be smaller than GeckoView height ("
                  + mHeight
                  + ")"));
    }

    mDynamicToolbarMaxHeight = height;

    if (mAttachedCompositor) {
      mCompositor.setDynamicToolbarMaxHeight(mDynamicToolbarMaxHeight);
    }
  }

  /* package */ void setFixedBottomOffset(final int offset) {
    if (mFixedBottomOffset == offset) {
      return;
    }

    mFixedBottomOffset = offset;

    if (mCompositorReady) {
      mCompositor.setFixedBottomOffset(mFixedBottomOffset);
    }
  }

  /* package */ void onCompositorAttached() {
    if (DEBUG) {
      ThreadUtils.assertOnUiThread();
    }

    mAttachedCompositor = true;
    mCompositor.attachNPZC(mPanZoomController.mNative);

    if (mSurfaceInfo != null) {
      // If we have a valid surface, create the compositor now that we're attached.
      // Leave mSurface alone because we'll need it later for onCompositorReady.
      onSurfaceChanged(mSurfaceInfo);
    }

    mCompositor.sendToolbarAnimatorMessage(IS_COMPOSITOR_CONTROLLER_OPEN);
    mCompositor.setDynamicToolbarMaxHeight(mDynamicToolbarMaxHeight);
  }

  /* package */ void onCompositorDetached() {
    if (DEBUG) {
      ThreadUtils.assertOnUiThread();
    }

    if (mController != null) {
      mController.onCompositorDetached();
    }

    mAttachedCompositor = false;
    mCompositorReady = false;
  }

  /* package */ void handleCompositorMessage(final int message) {
    if (DEBUG) {
      ThreadUtils.assertOnUiThread();
    }

    switch (message) {
      case COMPOSITOR_CONTROLLER_OPEN:
        {
          if (isCompositorReady()) {
            return;
          }

          // Delay calling onCompositorReady to avoid deadlock due
          // to synchronous call to the compositor.
          ThreadUtils.postToUiThread(this::onCompositorReady);
          break;
        }

      case FIRST_PAINT:
        {
          if (mController != null) {
            mController.onFirstPaint();
          }
          final ContentDelegate delegate = mContentHandler.getDelegate();
          if (delegate != null) {
            delegate.onFirstComposite(this);
          }
          break;
        }

      case LAYERS_UPDATED:
        {
          if (mController != null) {
            mController.notifyDrawCallbacks();
          }
          break;
        }

      default:
        {
          Log.w(LOGTAG, "Unexpected message: " + message);
          break;
        }
    }
  }

  /* package */ boolean isCompositorReady() {
    return mCompositorReady;
  }

  /* package */ void onCompositorReady() {
    if (DEBUG) {
      ThreadUtils.assertOnUiThread();
    }

    if (!mAttachedCompositor) {
      return;
    }

    mCompositorReady = true;

    if (mController != null) {
      mController.onCompositorReady();
    }

    if (mSurfaceInfo != null) {
      // If we have a valid surface, resume the
      // compositor now that the compositor is ready.
      onSurfaceChanged(mSurfaceInfo);
      mSurfaceInfo = null;
    }

    if (mFixedBottomOffset != 0) {
      mCompositor.setFixedBottomOffset(mFixedBottomOffset);
    }
  }

  /* package */ void updateOverscrollVelocity(final float x, final float y) {
    if (DEBUG) {
      ThreadUtils.assertOnUiThread();
    }

    if (mOverscroll == null) {
      return;
    }

    // Multiply the velocity by 1000 to match what was done in JPZ.
    mOverscroll.setVelocity(x * 1000.0f, OverscrollEdgeEffect.AXIS_X);
    mOverscroll.setVelocity(y * 1000.0f, OverscrollEdgeEffect.AXIS_Y);
  }

  /* package */ void updateOverscrollOffset(final float x, final float y) {
    if (DEBUG) {
      ThreadUtils.assertOnUiThread();
    }

    if (mOverscroll == null) {
      return;
    }

    mOverscroll.setDistance(x, OverscrollEdgeEffect.AXIS_X);
    mOverscroll.setDistance(y, OverscrollEdgeEffect.AXIS_Y);
  }

  /* package */ void onCompositorScrollUpdate(
      final float scrollX, final float scrollY, final float zoom, final int source) {
    if (DEBUG) {
      ThreadUtils.assertOnUiThread();
    }

    // Tbe incoming scrollX and scrollY are in CSS pixels.
    // For mViewportLeft/Top, convert them to Screen pixels.
    mViewportLeft = scrollX * zoom;
    mViewportTop = scrollY * zoom;
    mViewportZoom = zoom;

    final ScrollPositionUpdate update = new ScrollPositionUpdate();
    update.scrollX = scrollX;
    update.scrollY = scrollY;
    update.zoom = zoom;
    update.source = source;
    mLastScrollPositionUpdate = update;
    if (mCompositorScrollDelegate != null) {
      mCompositorScrollDelegate.onScrollChanged(this, update);
    }
  }

  /* protected */ void onWindowBoundsChanged() {
    if (DEBUG) {
      ThreadUtils.assertOnUiThread();
    }

    if (mHeight != 0 && mDynamicToolbarMaxHeight != 0 && mHeight < mDynamicToolbarMaxHeight) {
      Log.w(
          LOGTAG,
          new AssertionError(
              "The maximum height of the dynamic toolbar ("
                  + mDynamicToolbarMaxHeight
                  + ") should be smaller than GeckoView height ("
                  + mHeight
                  + ")"));
    }

    final int toolbarHeight = 0;

    mClientTop = mTop + toolbarHeight;
    // If the view is not tall enough to even fix the toolbar we just
    // default the client height to 0
    mClientHeight = Math.max(mHeight - toolbarHeight, 0);

    if (mAttachedCompositor) {
      mCompositor.onBoundsChanged(mLeft, mClientTop, mWidth, mClientHeight);
    }

    if (mOverscroll != null) {
      mOverscroll.setSize(mWidth, mClientHeight);
    }
  }

  /* pacakge */ void onSafeAreaInsetsChanged(
      final int top, final int right, final int bottom, final int left) {
    ThreadUtils.assertOnUiThread();

    if (mAttachedCompositor) {
      mCompositor.onSafeAreaInsetsChanged(top, right, bottom, left);
    }
  }

  /* package */ void onKeyboardHeight(final int height) {
    ThreadUtils.assertOnUiThread();

    if (mKeyboardHeight == height) {
      return;
    }

    mKeyboardHeight = height;

    if (mAttachedCompositor) {
      mCompositor.onKeyboardHeightChanged(mKeyboardHeight);
    }
  }

  /* package */ void setPointerIcon(
      final int defaultCursor, final @Nullable Bitmap customCursor, final float x, final float y) {
    ThreadUtils.assertOnUiThread();

    final PointerIcon icon;
    if (customCursor != null) {
      try {
        icon = PointerIcon.create(customCursor, x, y);
      } catch (final IllegalArgumentException e) {
        // x/y hotspot might be invalid
        return;
      }
    } else {
      final Context context = GeckoAppShell.getApplicationContext();
      icon = PointerIcon.getSystemIcon(context, defaultCursor);
    }

    final ContentDelegate delegate = getContentDelegate();
    if (delegate != null) {
      delegate.onPointerIconChange(this, icon);
    }
  }

  /* package */ void startDragAndDrop(final Bitmap bitmap) {
    ThreadUtils.assertOnUiThread();

    final View view = getTextInput().getView();
    if (view == null) {
      return;
    }

    GeckoDragAndDrop.startDragAndDrop(view, bitmap);
  }

  /* package */ void updateDragImage(final Bitmap bitmap) {
    ThreadUtils.assertOnUiThread();

    final View view = getTextInput().getView();
    if (view == null) {
      return;
    }

    GeckoDragAndDrop.updateDragImage(view, bitmap);
  }

  /** GeckoSession applications implement this interface to handle media events. */
  public interface MediaDelegate {
    /** Utility class for recording device flags and types. */
    class RecordingDevice {
      /** Default status flags for this RecordingDevice. */
      public static class Status {
        /** The device is currently recording. */
        public static final long RECORDING = 0;

        /** The device is inactive. */
        public static final long INACTIVE = 1 << 0;

        /** Do not instantiate this class. */
        protected Status() {}
      }

      /** Default device types for this RecordingDevice. */
      public static class Type {
        /** Camera device type. */
        public static final long CAMERA = 0;

        /** Microphone device type. */
        public static final long MICROPHONE = 1 << 0;

        /** Do not instantiate this class. */
        protected Type() {}
      }

      /** Recording status type definitions for media recording state. */
      @Retention(RetentionPolicy.SOURCE)
      @LongDef(
          flag = true,
          value = {Status.RECORDING, Status.INACTIVE})
      public @interface RecordingStatus {}

      /** Device type definitions for media recording devices. */
      @Retention(RetentionPolicy.SOURCE)
      @LongDef(
          flag = true,
          value = {Type.CAMERA, Type.MICROPHONE})
      public @interface DeviceType {}

      /**
       * A long giving the current recording status, must be either Status.RECORDING, Status.PAUSED
       * or Status.INACTIVE.
       */
      public final @RecordingStatus long status;

      /**
       * A long giving the type of the recording device, must be either Type.CAMERA or
       * Type.MICROPHONE.
       */
      public final @DeviceType long type;

      private static @DeviceType long getTypeFromString(final String type) {
        if ("microphone".equals(type)) {
          return Type.MICROPHONE;
        } else if ("camera".equals(type)) {
          return Type.CAMERA;
        } else {
          throw new IllegalArgumentException(
              "String: " + type + " is not a valid recording device string");
        }
      }

      private static @RecordingStatus long getStatusFromString(final String type) {
        if ("recording".equals(type)) {
          return Status.RECORDING;
        } else {
          return Status.INACTIVE;
        }
      }

      /* package */ RecordingDevice(final GeckoBundle media) {
        status = getStatusFromString(media.getString("status"));
        type = getTypeFromString(media.getString("type"));
      }

      /** Empty constructor for tests. */
      protected RecordingDevice() {
        status = Status.INACTIVE;
        type = Type.CAMERA;
      }
    }

    /**
     * A recording device has changed state. Any change to the recording state of the devices
     * microphone or camera will call this delegate method. The argument provides details of the
     * active recording devices.
     *
     * @param session The session that the event has originated from.
     * @param devices The list of active devices and their recording state.
     */
    @UiThread
    default void onRecordingStatusChanged(
        @NonNull final GeckoSession session, @NonNull final RecordingDevice[] devices) {}
  }

  /** An interface for recording new history visits and fetching the visited status for links. */
  public interface HistoryDelegate {
    /** A representation of an entry in browser history. */
    interface HistoryItem {
      /**
       * Get the URI of this history element.
       *
       * @return A String representing the URI of this history element.
       */
      @AnyThread
      default @NonNull String getUri() {
        throw new UnsupportedOperationException("HistoryItem.getUri() called on invalid object.");
      }

      /**
       * Get the title of this history element.
       *
       * @return A String representing the title of this history element.
       */
      @AnyThread
      default @NonNull String getTitle() {
        throw new UnsupportedOperationException(
            "HistoryItem.getString() called on invalid object.");
      }
    }

    /**
     * A representation of browser history, accessible as a `List`. The list itself and its entries
     * are immutable; any attempt to mutate will result in an `UnsupportedOperationException`.
     */
    interface HistoryList extends List<HistoryItem> {
      /**
       * Get the current index in browser history.
       *
       * @return An int representing the current index in browser history.
       */
      @AnyThread
      default int getCurrentIndex() {
        throw new UnsupportedOperationException(
            "HistoryList.getCurrentIndex() called on invalid object.");
      }
    }

    // These flags are similar to those in `IHistory::LoadFlags`, but we use
    // different values to decouple GeckoView from Gecko changes. These
    // should be kept in sync with `GeckoViewHistory::GeckoViewVisitFlags`.

    /** The URL was visited a top-level window. */
    int VISIT_TOP_LEVEL = 1 << 0;

    /** The URL is the target of a temporary redirect. */
    int VISIT_REDIRECT_TEMPORARY = 1 << 1;

    /** The URL is the target of a permanent redirect. */
    int VISIT_REDIRECT_PERMANENT = 1 << 2;

    /** The URL is temporarily redirected to another URL. */
    int VISIT_REDIRECT_SOURCE = 1 << 3;

    /** The URL is permanently redirected to another URL. */
    int VISIT_REDIRECT_SOURCE_PERMANENT = 1 << 4;

    /** The URL failed to load due to a client or server error. */
    int VISIT_UNRECOVERABLE_ERROR = 1 << 5;

    /**
     * Records a visit to a page.
     *
     * @param session The session where the URL was visited.
     * @param url The visited URL.
     * @param lastVisitedURL The last visited URL in this session, to detect redirects and reloads.
     * @param flags Additional flags for this visit, including redirect and error statuses. This is
     *     a bitmask of one or more {@link #VISIT_TOP_LEVEL VISIT_*} flags, OR-ed together.
     * @return A {@link GeckoResult} completed with a boolean indicating whether to highlight links
     *     for the new URL as visited ({@code true}) or unvisited ({@code false}).
     */
    @UiThread
    default @Nullable GeckoResult<Boolean> onVisited(
        @NonNull final GeckoSession session,
        @NonNull final String url,
        @Nullable final String lastVisitedURL,
        @VisitFlags final int flags) {
      return null;
    }

    /**
     * Returns the visited statuses for links on a page. This is used to highlight links as visited
     * or unvisited, for example.
     *
     * @param session The session requesting the visited statuses.
     * @param urls A list of URLs to check.
     * @return A {@link GeckoResult} completed with a list of booleans corresponding to the URLs in
     *     {@code urls}, and indicating whether to highlight links for each URL as visited ({@code
     *     true}) or unvisited ({@code false}).
     */
    @UiThread
    default @Nullable GeckoResult<boolean[]> getVisited(
        @NonNull final GeckoSession session, @NonNull final String[] urls) {
      return null;
    }

    /**
     * Notifies the delegate that the navigation history state has changed.
     *
     * @param session The session where the history state changed.
     * @param historyList The updated history list for this session.
     */
    @UiThread
    default void onHistoryStateChange(
        @NonNull final GeckoSession session, @NonNull final HistoryList historyList) {}
  }

  /** Visit flag type definitions for history visits. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef(
      flag = true,
      value = {
        HistoryDelegate.VISIT_TOP_LEVEL,
        HistoryDelegate.VISIT_REDIRECT_TEMPORARY,
        HistoryDelegate.VISIT_REDIRECT_PERMANENT,
        HistoryDelegate.VISIT_REDIRECT_SOURCE,
        HistoryDelegate.VISIT_REDIRECT_SOURCE_PERMANENT,
        HistoryDelegate.VISIT_UNRECOVERABLE_ERROR
      })
  public @interface VisitFlags {}

  private Autofill.Support getAutofillSupport() {
    return mAutofillSupport;
  }

  /**
   * Sets the autofill delegate for this session.
   *
   * @param delegate An instance of {@link Autofill.Delegate}.
   */
  @UiThread
  public void setAutofillDelegate(final @Nullable Autofill.Delegate delegate) {
    getAutofillSupport().setDelegate(delegate);
  }

  /**
   * @return The current {@link Autofill.Delegate} for this session, if any.
   */
  @UiThread
  public @Nullable Autofill.Delegate getAutofillDelegate() {
    return getAutofillSupport().getDelegate();
  }

  /**
   * Provides an autofill structure similar to {@link
   * View#onProvideAutofillVirtualStructure(ViewStructure, int)} , but does not rely on {@link
   * ViewStructure} to build the tree. This is useful for apps that want to provide autofill
   * functionality without using the Android autofill system or requiring API 26.
   *
   * @return The elements available for autofill.
   */
  @UiThread
  public @NonNull Autofill.Session getAutofillSession() {
    return getAutofillSupport().getAutofillSession();
  }

  /**
   * Saves a PDF of the currently displayed page.
   *
   * @return A GeckoResult with an InputStream containing the PDF. The result could
   *     CompleteExceptionally with a {@link GeckoPrintException}s, if there are any issues while
   *     generating the PDF.
   */
  @HandlerThread
  public @NonNull GeckoResult<InputStream> saveAsPdf() {
    return saveAsPdfByBrowsingContext(null);
  }

  /**
   * Saves a PDF of the specified browsing context. Use null if the browsing context is unknown or
   * to print the main page.
   *
   * @param browsingContextId the browsing context id of the item to print
   * @return A GeckoResult with an InputStream containing the PDF.
   */
  @AnyThread
  private @NonNull GeckoResult<InputStream> saveAsPdfByBrowsingContext(
      final @Nullable Long browsingContextId) {
    ThreadUtils.assertOnHandlerThread();
    final GeckoResult<InputStream> geckoResult = new GeckoResult<>();
    if (browsingContextId == null) {
      // Ensures the canonical browsing context is available
      setFocused(true);
      this.mWindow.printToPdf(geckoResult);
    } else {
      this.mWindow.printToPdf(geckoResult, browsingContextId);
    }
    return geckoResult;
  }

  /** Prints the currently displayed page. */
  @AnyThread
  public void printPageContent() {
    final PrintDelegate delegate = getPrintDelegate();
    if (delegate != null) {
      delegate.onPrint(this);
    } else {
      Log.w(LOGTAG, "Print delegate required for printing.");
    }
  }

  /**
   * Prints the currently displayed page and provides dialog finished status or if an exception
   * occurred.
   *
   * @return if the printing dialog finished or an exception.
   */
  @HandlerThread
  public @NonNull GeckoResult<Boolean> didPrintPageContent() {
    ThreadUtils.assertOnHandlerThread();
    final PrintDelegate delegate = getPrintDelegate();
    final GeckoResult<Boolean> result = new GeckoResult<>();
    if (delegate == null) {
      result.completeExceptionally(new GeckoPrintException(ERROR_NO_PRINT_DELEGATE));
      return result;
    }
    return saveAsPdfByBrowsingContext(null)
        .then(pdfStream -> delegate.onPrintWithStatus(pdfStream));
  }

  private static String rgbaToArgb(final String color) {
    // We expect #rrggbbaa
    if (color.length() != 9 || !color.startsWith("#")) {
      throw new IllegalArgumentException("Invalid color format");
    }

    return "#" + color.substring(7) + color.substring(1, 7);
  }

  private static void fixupManifestColor(final JSONObject manifest, final String name)
      throws JSONException {
    if (manifest.isNull(name)) {
      return;
    }

    manifest.put(name, rgbaToArgb(manifest.getString(name)));
  }

  private static JSONObject fixupWebAppManifest(final JSONObject manifest) {
    // Colors are #rrggbbaa, but we want them to be #aarrggbb, since that's what
    // android.graphics.Color expects.
    try {
      fixupManifestColor(manifest, "theme_color");
      fixupManifestColor(manifest, "background_color");
    } catch (final JSONException e) {
      Log.w(LOGTAG, "Failed to fixup web app manifest", e);
    }

    return manifest;
  }

  private static boolean maybeCheckDataUriLength(final @NonNull Loader request) {
    if (!request.mIsDataUri) {
      return true;
    }

    return request.mUri.length() <= DATA_URI_MAX_LENGTH;
  }

  /**
   * Used for printing page content.
   *
   * <p>The provided implementation is in {@link GeckoView}. It uses a PDF of the content and the
   * Android print API to print the page.
   */
  @AnyThread
  public interface PrintDelegate {
    /**
     * Print the current page content.
     *
     * @param session to print
     */
    default void onPrint(@NonNull final GeckoSession session) {}

    /**
     * Print any provided PDF InputStream.
     *
     * @param pdfInputStream an InputStream containing a PDF
     */
    default void onPrint(@NonNull final InputStream pdfInputStream) {}

    /**
     * Print any provided PDF InputStream.
     *
     * @param pdfInputStream an InputStream containing a PDF
     * @return A GeckoResult if the print dialog has closed
     */
    default @Nullable GeckoResult<Boolean> onPrintWithStatus(
        @NonNull final InputStream pdfInputStream) {
      return null;
    }
  }

  /**
   * Gets the print delegate for this session.
   *
   * @return The current {@link PrintDelegate} for this session, if any.
   */
  @AnyThread
  public @Nullable PrintDelegate getPrintDelegate() {
    return mPrintHandler.getDelegate();
  }

  /**
   * Sets the print delegate for this session.
   *
   * @param delegate An instance of {@link PrintDelegate}.
   */
  @AnyThread
  public void setPrintDelegate(final @Nullable PrintDelegate delegate) {
    mPrintHandler.setDelegate(delegate, this);
  }

  /**
   * Gets the experiment delegate for this session.
   *
   * @return The current {@link ExperimentDelegate} for this session, if any.
   */
  @AnyThread
  public @Nullable ExperimentDelegate getExperimentDelegate() {
    return mExperimentHandler.getDelegate();
  }

  /**
   * Gets the experiment delegate from the runtime.
   *
   * @return The current {@link ExperimentDelegate} for the runtime or null.
   */
  @AnyThread
  private @Nullable ExperimentDelegate getRuntimeExperimentDelegate() {
    final GeckoRuntime runtime = this.getRuntime();
    if (runtime != null) {
      final GeckoRuntimeSettings runtimeSettings = runtime.getSettings();
      if (runtimeSettings != null) {
        return runtimeSettings.getExperimentDelegate();
      }
    }
    Log.w(LOGTAG, "Could not retrieve experiment delegate from runtime.");
    return null;
  }

  /**
   * Sets the experiment delegate for this session. Default is set to the runtime experiment
   * delegate.
   *
   * @param delegate An instance of {@link ExperimentDelegate}.
   */
  @AnyThread
  public void setExperimentDelegate(final @Nullable ExperimentDelegate delegate) {
    mExperimentHandler.setDelegate(delegate, this);
  }

  /**
   * Handle back key pressed on Web content to dismiss some HTML elements such as &lt;dialog&gt;.
   *
   * <p>See <a
   * href="https://developer.mozilla.org/en-US/docs/Web/API/CloseWatcher">CloseWatcher</a>.
   *
   * @return true if the back key is processed.
   */
  @UiThread
  public @NonNull GeckoResult<Boolean> processBackPressed() {
    return mEventDispatcher.queryBoolean("GeckoView:ProcessBackPressed");
  }

  /** Thrown when failure occurs when printing from a website. */
  @WrapForJNI
  public static class GeckoPrintException extends Exception {
    /** The print service was not available. */
    public static final int ERROR_PRINT_SETTINGS_SERVICE_NOT_AVAILABLE = -1;

    /** The print service was not created due to an initialization error. */
    public static final int ERROR_UNABLE_TO_CREATE_PRINT_SETTINGS = -2;

    /** An error happened while trying to find the canonical browing context */
    public static final int ERROR_UNABLE_TO_RETRIEVE_CANONICAL_BROWSING_CONTEXT = -3;

    /** An error happened while trying to find the activity context delegate */
    public static final int ERROR_NO_ACTIVITY_CONTEXT_DELEGATE = -4;

    /** An error happened while trying to find the activity context */
    public static final int ERROR_NO_ACTIVITY_CONTEXT = -5;

    /** An error happened while trying to find the print delegate */
    public static final int ERROR_NO_PRINT_DELEGATE = -6;

    /** Error code type definitions for print exceptions. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef(
        value = {
          ERROR_PRINT_SETTINGS_SERVICE_NOT_AVAILABLE,
          ERROR_UNABLE_TO_CREATE_PRINT_SETTINGS,
          ERROR_UNABLE_TO_RETRIEVE_CANONICAL_BROWSING_CONTEXT,
          ERROR_NO_ACTIVITY_CONTEXT_DELEGATE,
          ERROR_NO_ACTIVITY_CONTEXT,
          ERROR_NO_PRINT_DELEGATE
        })
    public @interface Codes {}

    /** One of {@link Codes} that provides more information about this exception. */
    public final @Codes int code;

    @Override
    public String toString() {
      return "GeckoPrintException: " + code;
    }

    /* package */ GeckoPrintException(final @Codes int code) {
      this.code = code;
    }

    /** For testing. */
    protected GeckoPrintException() {
      code = ERROR_PRINT_SETTINGS_SERVICE_NOT_AVAILABLE;
    }
  }
}
