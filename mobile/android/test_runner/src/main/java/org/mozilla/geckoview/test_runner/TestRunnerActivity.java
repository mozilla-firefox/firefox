/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test_runner;

import static org.mozilla.geckoview.ExperimentDelegate.ExperimentException.ERROR_EXPERIMENT_SLUG_NOT_FOUND;
import static org.mozilla.geckoview.ExperimentDelegate.ExperimentException.ERROR_FEATURE_NOT_FOUND;
import static org.mozilla.geckoview.ExperimentDelegate.ExperimentException.ERROR_UNKNOWN;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.graphics.SurfaceTexture;
import android.net.Uri;
import android.os.Bundle;
import android.view.Surface;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.json.JSONException;
import org.json.JSONObject;
import org.mozilla.geckoview.AllowOrDeny;
import org.mozilla.geckoview.ContentBlocking;
import org.mozilla.geckoview.ExperimentDelegate;
import org.mozilla.geckoview.GeckoDisplay;
import org.mozilla.geckoview.GeckoResult;
import org.mozilla.geckoview.GeckoRuntime;
import org.mozilla.geckoview.GeckoRuntimeSettings;
import org.mozilla.geckoview.GeckoSession;
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.ContentPermission;
import org.mozilla.geckoview.GeckoSessionSettings;
import org.mozilla.geckoview.GeckoView;
import org.mozilla.geckoview.OrientationController;
import org.mozilla.geckoview.WebExtension;
import org.mozilla.geckoview.WebExtensionController;
import org.mozilla.geckoview.WebRequestError;

public class TestRunnerActivity extends Activity {
  private static final String LOGTAG = "TestRunnerActivity";
  private static final String ERROR_PAGE =
      "<!DOCTYPE html><head><title>Error</title></head><body>Error!</body></html>";

  static GeckoRuntime sRuntime;

  private GeckoSession mPopupSession;
  private GeckoSession mSession;
  private GeckoView mView;
  private boolean mKillProcessOnDestroy;

  private HashMap<GeckoSession, Display> mDisplays = new HashMap<>();
  private HashMap<String, ExtensionWrapper> mExtensions = new HashMap<>();

  private static class ExtensionWrapper {
    public WebExtension extension;
    public HashMap<GeckoSession, WebExtension.Action> browserActions;
    public HashMap<GeckoSession, WebExtension.Action> pageActions;

    public ExtensionWrapper(final WebExtension extension) {
      this.extension = extension;
      browserActions = new HashMap<>();
      pageActions = new HashMap<>();
    }
  }

  private static class Display {
    public final SurfaceTexture texture;
    public final Surface surface;

    private final int width;
    private final int height;
    private GeckoDisplay sessionDisplay;

    public Display(final int width, final int height) {
      this.width = width;
      this.height = height;
      texture = new SurfaceTexture(0);
      texture.setDefaultBufferSize(width, height);
      surface = new Surface(texture);
    }

    public void attach(final GeckoSession session) {
      sessionDisplay = session.acquireDisplay();
      sessionDisplay.surfaceChanged(
          new GeckoDisplay.SurfaceInfo.Builder(surface).size(width, height).build());
    }

    public void release(final GeckoSession session) {
      sessionDisplay.surfaceDestroyed();
      session.releaseDisplay(sessionDisplay);
    }
  }

  private static WebExtensionController webExtensionController() {
    return sRuntime.getWebExtensionController();
  }

  private static OrientationController orientationController() {
    return sRuntime.getOrientationController();
  }

  // Keeps track of all sessions for this test runner. The top session in the deque is the
  // current active session for extension purposes.
  private ArrayDeque<GeckoSession> mOwnedSessions = new ArrayDeque<>();

  private GeckoSession.PermissionDelegate mPermissionDelegate =
      new GeckoSession.PermissionDelegate() {
        @Override
        public GeckoResult<Integer> onContentPermissionRequest(
            @NonNull final GeckoSession session, @NonNull ContentPermission perm) {
          return GeckoResult.fromValue(ContentPermission.VALUE_ALLOW);
        }

        @Override
        public void onAndroidPermissionsRequest(
            @NonNull final GeckoSession session,
            @Nullable final String[] permissions,
            @NonNull final Callback callback) {
          callback.grant();
        }
      };

  private GeckoSession.PromptDelegate mPromptDelegate =
      new GeckoSession.PromptDelegate() {
        Map<BasePrompt, GeckoResult<PromptResponse>> mPromptResults = new HashMap<>();
        public GeckoSession.PromptDelegate.PromptInstanceDelegate mPromptInstanceDelegate =
            new GeckoSession.PromptDelegate.PromptInstanceDelegate() {
              @Override
              public void onPromptDismiss(
                  final @NonNull GeckoSession.PromptDelegate.BasePrompt prompt) {
                mPromptResults.get(prompt).complete(prompt.dismiss());
              }
            };

        @Override
        public GeckoResult<PromptResponse> onAlertPrompt(
            @NonNull final GeckoSession session, @NonNull final AlertPrompt prompt) {
          mPromptResults.put(prompt, new GeckoResult<>());
          prompt.setDelegate(mPromptInstanceDelegate);
          return mPromptResults.get(prompt);
        }

        @Override
        public GeckoResult<PromptResponse> onButtonPrompt(
            @NonNull final GeckoSession session, @NonNull final ButtonPrompt prompt) {
          mPromptResults.put(prompt, new GeckoResult<>());
          prompt.setDelegate(mPromptInstanceDelegate);
          return mPromptResults.get(prompt);
        }

        @Override
        public GeckoResult<PromptResponse> onTextPrompt(
            @NonNull final GeckoSession session, @NonNull final TextPrompt prompt) {
          mPromptResults.put(prompt, new GeckoResult<>());
          prompt.setDelegate(mPromptInstanceDelegate);
          return mPromptResults.get(prompt);
        }
      };

  private GeckoSession.NavigationDelegate mNavigationDelegate =
      new GeckoSession.NavigationDelegate() {
        @Override
        public void onLocationChange(
            final GeckoSession session,
            final String url,
            final List<ContentPermission> perms,
            final Boolean hasUserGesture) {
          getActionBar().setSubtitle(url);
        }

        @Override
        public GeckoResult<AllowOrDeny> onLoadRequest(
            final GeckoSession session, final LoadRequest request) {
          // Allow Gecko to load all URIs
          return GeckoResult.allow();
        }

        @Override
        public GeckoResult<GeckoSession> onNewSession(
            final GeckoSession session, final String uri) {
          webExtensionController().setTabActive(mOwnedSessions.peek(), false);
          final GeckoSession newSession =
              createBackgroundSession(session.getSettings(), /* active */ true);
          webExtensionController().setTabActive(newSession, true);
          return GeckoResult.fromValue(newSession);
        }

        @Override
        public GeckoResult<String> onLoadError(
            final GeckoSession session, final String uri, final WebRequestError error) {

          return GeckoResult.fromValue("data:text/html," + ERROR_PAGE);
        }
      };

  private GeckoSession.ContentDelegate mContentDelegate =
      new GeckoSession.ContentDelegate() {
        private void onContentProcessGone() {
          if (System.getenv("MOZ_CRASHREPORTER_SHUTDOWN") != null) {
            sRuntime.shutdown();
          }
        }

        @Override
        public void onCloseRequest(final GeckoSession session) {
          closeSession(session);
        }

        @Override
        public void onCrash(final GeckoSession session) {
          onContentProcessGone();
        }

        @Override
        public void onKill(final GeckoSession session) {
          onContentProcessGone();
        }
      };

  private WebExtension.TabDelegate mTabDelegate =
      new WebExtension.TabDelegate() {
        @Override
        public GeckoResult<GeckoSession> onNewTab(
            final WebExtension source, final WebExtension.CreateTabDetails details) {
          GeckoSessionSettings settings = null;
          if (details.cookieStoreId != null) {
            settings = new GeckoSessionSettings.Builder().contextId(details.cookieStoreId).build();
          }

          if (details.active == Boolean.TRUE) {
            webExtensionController().setTabActive(mOwnedSessions.peek(), false);
          }
          final GeckoSession newSession = createSession(settings, details.active == Boolean.TRUE);
          return GeckoResult.fromValue(newSession);
        }
      };

  private WebExtension.ActionDelegate mActionDelegate =
      new WebExtension.ActionDelegate() {
        private GeckoResult<GeckoSession> togglePopup(
            final WebExtension extension, final boolean forceOpen) {
          if (mPopupSession != null) {
            mPopupSession.close();
            if (!forceOpen) {
              return null;
            }
          }

          mPopupSession = createBackgroundSession(null, /* active */ false);
          mPopupSession.open(sRuntime);

          // Set the progress delegate in case there is an observer to the popup being loaded.
          mTestApiImpl.setCurrentPopupExtension(extension);
          mPopupSession.setProgressDelegate(mTestApiImpl);

          return GeckoResult.fromValue(mPopupSession);
        }

        @Nullable
        @Override
        public GeckoResult<GeckoSession> onOpenPopup(
            final WebExtension extension, final WebExtension.Action action) {
          return togglePopup(extension, true);
        }

        @Nullable
        @Override
        public GeckoResult<GeckoSession> onTogglePopup(
            final WebExtension extension, final WebExtension.Action action) {
          return togglePopup(extension, false);
        }

        @Override
        public void onBrowserAction(
            final WebExtension extension,
            final GeckoSession session,
            final WebExtension.Action action) {
          mExtensions.get(extension.id).browserActions.put(session, action);
        }

        @Override
        public void onPageAction(
            final WebExtension extension,
            final GeckoSession session,
            final WebExtension.Action action) {
          mExtensions.get(extension.id).pageActions.put(session, action);
        }
      };

  private WebExtension.SessionTabDelegate mSessionTabDelegate =
      new WebExtension.SessionTabDelegate() {
        @NonNull
        @Override
        public GeckoResult<AllowOrDeny> onCloseTab(
            @Nullable final WebExtension source, @NonNull final GeckoSession session) {
          closeSession(session);
          return GeckoResult.allow();
        }

        @Override
        public GeckoResult<AllowOrDeny> onUpdateTab(
            @NonNull final WebExtension source,
            @NonNull final GeckoSession session,
            @NonNull final WebExtension.UpdateTabDetails updateDetails) {
          if (updateDetails.active == Boolean.TRUE) {
            // Move session to the top since it's now the active tab
            mOwnedSessions.remove(session);
            mOwnedSessions.addFirst(session);
          }

          return GeckoResult.allow();
        }
      };

  private class TestRunnerActivityDelegate implements GeckoView.ActivityContextDelegate {
    public Context getActivityContext() {
      return TestRunnerActivity.this;
    }
  }

  private TestRunnerActivityDelegate mActivityDelegate = new TestRunnerActivityDelegate();

  /**
   * Creates a session and adds it to the owned sessions deque.
   *
   * @param active Whether this session is the "active" session for extension purposes. The active
   *     session always sit at the top of the owned sessions deque.
   * @return the newly created session.
   */
  private GeckoSession createSession(final boolean active) {
    return createSession(null, active);
  }

  /**
   * Creates a session and adds it to the owned sessions deque.
   *
   * @param settings settings for the newly created {@link GeckoSession}, could be null if no extra
   *     settings need to be added.
   * @param active Whether this session is the "active" session for extension purposes. The active
   *     session always sit at the top of the owned sessions deque.
   * @return the newly created session.
   */
  private GeckoSession createSession(GeckoSessionSettings settings, final boolean active) {
    if (settings == null) {
      settings = new GeckoSessionSettings();
    }

    final GeckoSession session = new GeckoSession(settings);
    session.setNavigationDelegate(mNavigationDelegate);
    session.setContentDelegate(mContentDelegate);
    session.setPermissionDelegate(mPermissionDelegate);
    session.setPromptDelegate(mPromptDelegate);

    final WebExtension.SessionController sessionController = session.getWebExtensionController();
    for (final ExtensionWrapper wrapper : mExtensions.values()) {
      sessionController.setActionDelegate(wrapper.extension, mActionDelegate);
      sessionController.setTabDelegate(wrapper.extension, mSessionTabDelegate);
    }

    if (active) {
      mOwnedSessions.addFirst(session);
    } else {
      mOwnedSessions.addLast(session);
    }
    return session;
  }

  /**
   * Creates a session with a display attached.
   *
   * @param settings settings for the newly created {@link GeckoSession}, could be null if no extra
   *     settings need to be added.
   * @param active Whether this session is the "active" session for extension purposes. The active
   *     session always sit at the top of the owned sessions deque.
   * @return the newly created session.
   */
  private GeckoSession createBackgroundSession(
      final GeckoSessionSettings settings, final boolean active) {
    final GeckoSession session = createSession(settings, active);

    final Display display = new Display(mView.getWidth(), mView.getHeight());
    display.attach(session);

    mDisplays.put(session, display);

    return session;
  }

  private void closeSession(final GeckoSession session) {
    if (session == mOwnedSessions.peek()) {
      webExtensionController().setTabActive(session, false);
    }
    if (mDisplays.containsKey(session)) {
      final Display display = mDisplays.remove(session);
      display.release(session);
    }
    mOwnedSessions.remove(session);
    session.close();
    if (!mOwnedSessions.isEmpty()) {
      // Pick the top session as the current active
      webExtensionController().setTabActive(mOwnedSessions.peek(), true);
    }
  }

  @Override
  protected void onCreate(final Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);

    final Intent intent = getIntent();

    if ("org.mozilla.geckoview.test.XPCSHELL_TEST_MAIN".equals(intent.getAction())) {
      // This activity is just a stub to run xpcshell tests in a service
      return;
    }

    if (sRuntime == null) {
      final GeckoRuntimeSettings.Builder runtimeSettingsBuilder =
          new GeckoRuntimeSettings.Builder();

      // Mochitest and reftest encounter rounding errors if we have a
      // a window.devicePixelRation like 3.625, so simplify that here.
      runtimeSettingsBuilder
          .arguments(new String[] {"-purgecaches"})
          .displayDpiOverride(160)
          .displayDensityOverride(1.0f)
          .remoteDebuggingEnabled(true)
          .experimentDelegate(new TestRunnerExperimentDelegate());

      final Bundle extras = intent.getExtras();
      if (extras != null) {
        runtimeSettingsBuilder.extras(extras);
      }

      final ContentBlocking.SafeBrowsingProvider googleLegacy =
          ContentBlocking.SafeBrowsingProvider.from(
                  ContentBlocking.GOOGLE_LEGACY_SAFE_BROWSING_PROVIDER)
              .getHashUrl("http://mochi.test:8888/safebrowsing-dummy/gethash")
              .updateUrl("http://mochi.test:8888/safebrowsing-dummy/update")
              .build();

      final ContentBlocking.SafeBrowsingProvider google =
          ContentBlocking.SafeBrowsingProvider.from(ContentBlocking.GOOGLE_SAFE_BROWSING_PROVIDER)
              .getHashUrl("http://mochi.test:8888/safebrowsing4-dummy/gethash")
              .updateUrl("http://mochi.test:8888/safebrowsing4-dummy/update")
              .build();

      runtimeSettingsBuilder
          .consoleOutput(true)
          .contentBlocking(
              new ContentBlocking.Settings.Builder()
                  .safeBrowsingProviders(google, googleLegacy)
                  .build())
          .lowMemoryDetection(false); // Avoid unpredictability in tests

      sRuntime = GeckoRuntime.create(this, runtimeSettingsBuilder.build());

      webExtensionController()
          .setDebuggerDelegate(
              new WebExtensionController.DebuggerDelegate() {
                @Override
                public void onExtensionListUpdated() {
                  refreshExtensionList();
                }
              });

      webExtensionController()
          .installBuiltIn("resource://android/assets/test-runner-support/")
          .accept(
              extension -> {
                extension.setMessageDelegate(mApiEngine, "test-runner-support");
                extension.setTabDelegate(mTabDelegate);
              });

      webExtensionController()
          .setAddonManagerDelegate(new WebExtensionController.AddonManagerDelegate() {});

      sRuntime.setDelegate(
          () -> {
            mKillProcessOnDestroy = true;
            finish();
          });
    }

    orientationController()
        .setDelegate(
            new OrientationController.OrientationDelegate() {
              @Override
              public GeckoResult<AllowOrDeny> onOrientationLock(int aOrientation) {
                setRequestedOrientation(aOrientation);
                return GeckoResult.allow();
              }

              @Override
              public void onOrientationUnlock() {
                setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
              }
            });

    mSession = createSession(/* active */ true);
    webExtensionController().setTabActive(mOwnedSessions.peek(), true);
    mSession.open(sRuntime);

    // If we were passed a URI in the Intent, open it
    final Uri uri = intent.getData();
    if (uri != null) {
      mSession.loadUri(uri.toString());
    }

    mView = new GeckoView(this);
    mView.setSession(mSession);
    setContentView(mView);
    mView.setActivityContextDelegate(mActivityDelegate);

    sRuntime.setServiceWorkerDelegate(
        new GeckoRuntime.ServiceWorkerDelegate() {
          @NonNull
          @Override
          public GeckoResult<GeckoSession> onOpenWindow(@NonNull String url) {
            return mNavigationDelegate.onNewSession(mSession, url);
          }
        });
  }

  private final TestApiImpl mTestApiImpl = new TestApiImpl();
  private final TestRunnerApiEngine mApiEngine = new TestRunnerApiEngine(mTestApiImpl);

  private class TestApiImpl implements TestRunnerApiEngine.Api, GeckoSession.ProgressDelegate {
    private GeckoResult<WebExtension> mOnPopupLoaded;
    // Stores which extension opened the current popup
    private WebExtension mCurrentPopupExtension;

    @Override
    public void onPageStop(final GeckoSession session, final boolean success) {
      if (mOnPopupLoaded != null) {
        mOnPopupLoaded.complete(mCurrentPopupExtension);
        mOnPopupLoaded = null;
        mCurrentPopupExtension = null;
      }
      session.setProgressDelegate(null);
    }

    public void setCurrentPopupExtension(final WebExtension extension) {
      mCurrentPopupExtension = extension;
    }

    private GeckoResult<Void> clickAction(
        final String extensionId, final HashMap<GeckoSession, WebExtension.Action> actions) {
      final GeckoSession active = mOwnedSessions.peek();

      WebExtension.Action action = actions.get(active);
      if (action == null) {
        // Get default action if there's no specific one
        action = actions.get(null);
      }

      if (action == null) {
        return GeckoResult.fromException(
            new RuntimeException("No browser action for " + extensionId));
      }

      action.click();
      return GeckoResult.fromValue(null);
    }

    @Override
    public GeckoResult<Void> clickPageAction(final String extensionId) {
      if (!mExtensions.containsKey(extensionId)) {
        return GeckoResult.fromException(
            new RuntimeException("Extension not found: " + extensionId));
      }

      return clickAction(extensionId, mExtensions.get(extensionId).pageActions);
    }

    @Override
    public GeckoResult<Void> clickBrowserAction(final String extensionId) {
      if (!mExtensions.containsKey(extensionId)) {
        return GeckoResult.fromException(
            new RuntimeException("Extension not found: " + extensionId));
      }

      return clickAction(extensionId, mExtensions.get(extensionId).browserActions);
    }

    @Override
    public GeckoResult<Void> closePopup() {
      if (mPopupSession != null) {
        mPopupSession.close();
        mPopupSession = null;
      }
      return null;
    }

    @Override
    public GeckoResult<Void> awaitExtensionPopup(final String extensionId) {
      mOnPopupLoaded = new GeckoResult<>();
      return mOnPopupLoaded.accept(
          extension -> {
            if (!extension.id.equals(extensionId)) {
              throw new IllegalStateException(
                  "Expecting panel from extension: "
                      + extensionId
                      + " found "
                      + extension.id
                      + " instead.");
            }
          });
    }
  }

  // Random start timestamp for the BrowsingDataDelegate API.
  private static final int CLEAR_DATA_START_TIMESTAMP = 1234;

  private void refreshExtensionList() {
    webExtensionController()
        .list()
        .accept(
            extensions -> {
              mExtensions.clear();
              for (final WebExtension extension : extensions) {
                mExtensions.put(extension.id, new ExtensionWrapper(extension));
                extension.setActionDelegate(mActionDelegate);
                extension.setTabDelegate(mTabDelegate);

                extension.setBrowsingDataDelegate(
                    new WebExtension.BrowsingDataDelegate() {
                      @Nullable
                      @Override
                      public GeckoResult<Settings> onGetSettings() {
                        final long types =
                            Type.CACHE
                                | Type.COOKIES
                                | Type.HISTORY
                                | Type.FORM_DATA
                                | Type.DOWNLOADS;
                        return GeckoResult.fromValue(
                            new Settings(CLEAR_DATA_START_TIMESTAMP, types, types));
                      }
                    });

                for (final GeckoSession session : mOwnedSessions) {
                  final WebExtension.SessionController controller =
                      session.getWebExtensionController();
                  controller.setActionDelegate(extension, mActionDelegate);
                  controller.setTabDelegate(extension, mSessionTabDelegate);
                }
              }
            });
  }

  @Override
  protected void onDestroy() {
    mSession.close();
    super.onDestroy();

    if (mKillProcessOnDestroy) {
      android.os.Process.killProcess(android.os.Process.myPid());
    }
  }

  public GeckoView getGeckoView() {
    return mView;
  }

  public GeckoSession getGeckoSession() {
    return mSession;
  }

  class TestRunnerExperimentDelegate implements ExperimentDelegate {
    @Override
    public GeckoResult<JSONObject> onGetExperimentFeature(@NonNull String feature) {
      GeckoResult<JSONObject> result = new GeckoResult<>();
      if (feature.equals("test")) {
        try {
          result.complete(new JSONObject().put("item-one", true).put("item-two", 5));
        } catch (JSONException e) {
          result.completeExceptionally(new ExperimentException(ERROR_UNKNOWN));
        }
      } else {
        result.completeExceptionally(new ExperimentException(ERROR_FEATURE_NOT_FOUND));
      }
      return result;
    }

    @Override
    public GeckoResult<Void> onRecordExposureEvent(@NonNull String feature) {
      GeckoResult<Void> result = new GeckoResult<>();
      if (feature.equals("test")) {
        result.complete(null);
      } else {
        result.completeExceptionally(new ExperimentException(ERROR_FEATURE_NOT_FOUND));
      }
      return result;
    }

    @Override
    public GeckoResult<Void> onRecordExperimentExposureEvent(
        @NonNull String feature, @NonNull String slug) {
      GeckoResult<Void> result = new GeckoResult<>();
      if (feature.equals("test") && slug.equals("test")) {
        result.complete(null);
      } else if (!slug.equals("test") && feature.equals("test")) {
        result.completeExceptionally(new ExperimentException(ERROR_EXPERIMENT_SLUG_NOT_FOUND));
      } else {
        result.completeExceptionally(new ExperimentException(ERROR_FEATURE_NOT_FOUND));
      }
      return result;
    }

    @Override
    public GeckoResult<Void> onRecordMalformedConfigurationEvent(
        @NonNull String feature, @NonNull String part) {
      GeckoResult<Void> result = new GeckoResult<>();
      if (feature.equals("test")) {
        result.complete(null);
      } else {
        result.completeExceptionally(new ExperimentException(ERROR_FEATURE_NOT_FOUND));
      }
      return result;
    }
  }
}
