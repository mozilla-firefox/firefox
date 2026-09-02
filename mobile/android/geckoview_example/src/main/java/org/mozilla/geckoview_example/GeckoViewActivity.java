/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview_example;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.PictureInPictureParams;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.IntentSender;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.SystemClock;
import android.provider.MediaStore;
import android.text.InputType;
import android.util.Log;
import android.util.LruCache;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.RelativeLayout;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.OptIn;
import androidx.annotation.UiThread;
import androidx.appcompat.app.ActionBar;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;
import androidx.core.content.ContextCompat;
import androidx.preference.PreferenceManager;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.json.JSONObject;
import org.mozilla.geckoview.AIFeaturesController;
import org.mozilla.geckoview.AIFeaturesController.RuntimeAIFeatures;
import org.mozilla.geckoview.AllowOrDeny;
import org.mozilla.geckoview.Autocomplete;
import org.mozilla.geckoview.BasicSelectionActionDelegate;
import org.mozilla.geckoview.ContentBlocking;
import org.mozilla.geckoview.ExperimentalGeckoViewApi;
import org.mozilla.geckoview.GeckoResult;
import org.mozilla.geckoview.GeckoRuntime;
import org.mozilla.geckoview.GeckoRuntimeSettings;
import org.mozilla.geckoview.GeckoSession;
import org.mozilla.geckoview.GeckoSession.PermissionDelegate;
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.ContentPermission;
import org.mozilla.geckoview.GeckoSessionSettings;
import org.mozilla.geckoview.GeckoView;
import org.mozilla.geckoview.GeckoWebExecutor;
import org.mozilla.geckoview.Image;
import org.mozilla.geckoview.MediaSession;
import org.mozilla.geckoview.OrientationController;
import org.mozilla.geckoview.ProfilerController;
import org.mozilla.geckoview.SlowScriptResponse;
import org.mozilla.geckoview.TranslationsController;
import org.mozilla.geckoview.WebExtension;
import org.mozilla.geckoview.WebExtensionController;
import org.mozilla.geckoview.WebNotification;
import org.mozilla.geckoview.WebNotificationDelegate;
import org.mozilla.geckoview.WebRequest;
import org.mozilla.geckoview.WebRequestError;
import org.mozilla.geckoview.WebResponse;
import org.mozilla.geckoview_example.utils.EdgeToEdgeUtils;
import org.mozilla.geckoview_example.utils.WindowUtils;

interface WebExtensionDelegate {
  default GeckoSession toggleBrowserActionPopup(boolean force) {
    return null;
  }

  default void onActionButton(ActionButton button) {}

  default TabSession getSession(GeckoSession session) {
    return null;
  }

  default TabSession getCurrentSession() {
    return null;
  }

  default void closeTab(TabSession session) {}

  default void updateTab(TabSession session, WebExtension.UpdateTabDetails details) {}

  default TabSession openNewTab(WebExtension.CreateTabDetails details) {
    return null;
  }
}

class WebExtensionManager
    implements WebExtension.ActionDelegate,
        WebExtension.SessionTabDelegate,
        WebExtension.TabDelegate,
        WebExtensionController.PromptDelegate,
        WebExtensionController.DebuggerDelegate,
        TabSessionManager.TabObserver {
  public WebExtension extension;

  private LruCache<Image, Bitmap> mBitmapCache = new LruCache<>(5);
  private GeckoRuntime mRuntime;
  private WebExtension.Action mDefaultAction;
  private TabSessionManager mTabManager;

  private WeakReference<WebExtensionDelegate> mExtensionDelegate;

  @Nullable
  @Override
  public GeckoResult<WebExtension.PermissionPromptResponse> onInstallPromptRequest(
      @NonNull WebExtension extension,
      @NonNull String[] permissions,
      @NonNull String[] origins,
      @NonNull final String[] dataCollectionPermissions) {
    return GeckoResult.fromValue(
        new org.mozilla.geckoview.WebExtension.PermissionPromptResponse(
            true, // isPermissionsGranted
            true, // isPrivateModeGranted
            false // isTechnicalAndInteractionDataGranted
            ));
  }

  @Nullable
  @Override
  public GeckoResult<AllowOrDeny> onUpdatePrompt(
      @NonNull WebExtension updatedExtension,
      @NonNull String[] newPermissions,
      @NonNull String[] newOrigins,
      @NonNull String[] newDataCollectionPermissions) {
    return GeckoResult.allow();
  }

  @Nullable
  @Override
  public GeckoResult<AllowOrDeny> onOptionalPrompt(
      final @NonNull WebExtension extension,
      final String[] permissions,
      final String[] origins,
      final String[] dataCollectionPermissions) {
    return GeckoResult.allow();
  }

  @Override
  public void onExtensionListUpdated() {
    refreshExtensionList();
  }

  // We only support either one browserAction or one pageAction
  private void onAction(
      final WebExtension extension, final GeckoSession session, final WebExtension.Action action) {
    WebExtensionDelegate delegate = mExtensionDelegate.get();
    if (delegate == null) {
      return;
    }

    WebExtension.Action resolved;

    if (session == null) {
      // This is the default action
      mDefaultAction = action;
      resolved = actionFor(delegate.getCurrentSession());
    } else {
      if (delegate.getSession(session) == null) {
        return;
      }
      delegate.getSession(session).action = action;
      if (delegate.getCurrentSession() != session) {
        // This update is not for the session that we are currently displaying,
        // no need to update the UI
        return;
      }
      resolved = action.withDefault(mDefaultAction);
    }

    updateAction(resolved);
  }

  @Override
  public GeckoResult<GeckoSession> onNewTab(
      WebExtension source, WebExtension.CreateTabDetails details) {
    WebExtensionDelegate delegate = mExtensionDelegate.get();
    if (delegate == null) {
      return GeckoResult.fromValue(null);
    }
    return GeckoResult.fromValue(delegate.openNewTab(details));
  }

  @Override
  public GeckoResult<AllowOrDeny> onCloseTab(WebExtension extension, GeckoSession session) {
    final WebExtensionDelegate delegate = mExtensionDelegate.get();
    if (delegate == null) {
      return GeckoResult.deny();
    }

    final TabSession tabSession = mTabManager.getSession(session);
    if (tabSession != null) {
      delegate.closeTab(tabSession);
    }

    return GeckoResult.allow();
  }

  @Override
  public GeckoResult<AllowOrDeny> onUpdateTab(
      WebExtension extension, GeckoSession session, WebExtension.UpdateTabDetails updateDetails) {
    final WebExtensionDelegate delegate = mExtensionDelegate.get();
    if (delegate == null) {
      return GeckoResult.deny();
    }

    final TabSession tabSession = mTabManager.getSession(session);
    if (tabSession != null) {
      delegate.updateTab(tabSession, updateDetails);
    }

    return GeckoResult.allow();
  }

  @Override
  public void onPageAction(
      final WebExtension extension, final GeckoSession session, final WebExtension.Action action) {
    onAction(extension, session, action);
  }

  @Override
  public void onBrowserAction(
      final WebExtension extension, final GeckoSession session, final WebExtension.Action action) {
    onAction(extension, session, action);
  }

  private GeckoResult<GeckoSession> togglePopup(boolean force) {
    WebExtensionDelegate extensionDelegate = mExtensionDelegate.get();
    if (extensionDelegate == null) {
      return null;
    }

    GeckoSession session = extensionDelegate.toggleBrowserActionPopup(false);
    if (session == null) {
      return null;
    }

    return GeckoResult.fromValue(session);
  }

  @Override
  public GeckoResult<GeckoSession> onTogglePopup(
      final @NonNull WebExtension extension, final @NonNull WebExtension.Action action) {
    return togglePopup(false);
  }

  @Override
  public GeckoResult<GeckoSession> onOpenPopup(
      final @NonNull WebExtension extension, final @NonNull WebExtension.Action action) {
    return togglePopup(true);
  }

  private WebExtension.Action actionFor(TabSession session) {
    if (session.action == null) {
      return mDefaultAction;
    } else {
      return session.action.withDefault(mDefaultAction);
    }
  }

  private void updateAction(WebExtension.Action resolved) {
    WebExtensionDelegate extensionDelegate = mExtensionDelegate.get();
    if (extensionDelegate == null) {
      return;
    }

    if (resolved == null || resolved.enabled == null || !resolved.enabled) {
      extensionDelegate.onActionButton(null);
      return;
    }

    if (resolved.icon != null) {
      if (mBitmapCache.get(resolved.icon) != null) {
        extensionDelegate.onActionButton(
            new ActionButton(
                mBitmapCache.get(resolved.icon),
                resolved.badgeText,
                resolved.badgeTextColor,
                resolved.badgeBackgroundColor));
      } else {
        resolved
            .icon
            .getBitmap(100)
            .accept(
                bitmap -> {
                  mBitmapCache.put(resolved.icon, bitmap);
                  extensionDelegate.onActionButton(
                      new ActionButton(
                          bitmap,
                          resolved.badgeText,
                          resolved.badgeTextColor,
                          resolved.badgeBackgroundColor));
                })
            .exceptionally(
                throwable -> {
                  Bitmap errorIcon = Bitmap.createBitmap(64, 64, Bitmap.Config.ARGB_8888);
                  Canvas canvas = new Canvas(errorIcon);
                  Paint paint = new Paint();
                  paint.setColor(Color.RED);
                  paint.setStyle(Paint.Style.FILL);
                  canvas.drawRect(0f, 0f, 64f, 64f, paint);

                  extensionDelegate.onActionButton(
                      new ActionButton(
                          errorIcon,
                          resolved.badgeText,
                          resolved.badgeTextColor,
                          resolved.badgeBackgroundColor));

                  return null;
                });
      }
    } else {
      extensionDelegate.onActionButton(null);
    }
  }

  public void onClicked(TabSession session) {
    WebExtension.Action action = actionFor(session);
    if (action != null) {
      action.click();
    }
  }

  public void setExtensionDelegate(WebExtensionDelegate delegate) {
    mExtensionDelegate = new WeakReference<>(delegate);
  }

  @Override
  public void onCurrentSession(TabSession session) {
    if (mDefaultAction == null) {
      // No action was ever defined, so nothing to do
      return;
    }

    if (session.action != null) {
      updateAction(session.action.withDefault(mDefaultAction));
    } else {
      updateAction(mDefaultAction);
    }
  }

  public GeckoResult<Void> unregisterExtension() {
    if (extension == null) {
      return GeckoResult.fromValue(null);
    }

    mTabManager.unregisterWebExtension();

    return mRuntime
        .getWebExtensionController()
        .uninstall(extension)
        .accept(
            (unused) -> {
              extension = null;
              mDefaultAction = null;
              updateAction(null);
            });
  }

  public GeckoResult<WebExtension> updateExtension() {
    if (extension == null) {
      return GeckoResult.fromValue(null);
    }

    return mRuntime
        .getWebExtensionController()
        .update(extension)
        .map(
            newExtension -> {
              registerExtension(newExtension);
              return newExtension;
            });
  }

  public void registerExtension(WebExtension extension) {
    extension.setActionDelegate(this);
    extension.setTabDelegate(this);
    mTabManager.setWebExtensionDelegates(extension, this, this);
    this.extension = extension;
  }

  private void refreshExtensionList() {
    mRuntime
        .getWebExtensionController()
        .list()
        .accept(
            extensions -> {
              for (final WebExtension extension : extensions) {
                registerExtension(extension);
              }
            });
  }

  public WebExtensionManager(GeckoRuntime runtime, TabSessionManager tabManager) {
    mTabManager = tabManager;
    mRuntime = runtime;
    refreshExtensionList();
  }
}

public class GeckoViewActivity extends AppCompatActivity
    implements ToolbarLayout.TabListener,
        WebExtensionDelegate,
        SharedPreferences.OnSharedPreferenceChangeListener {
  private static final String LOGTAG = "GeckoViewActivity";
  private static final String FULL_ACCESSIBILITY_TREE_EXTRA = "full_accessibility_tree";
  private static final String SEARCH_URI_BASE = "https://www.google.com/search?q=";
  private static final String ACTION_SHUTDOWN = "org.mozilla.geckoview_example.SHUTDOWN";
  private static final String CHANNEL_ID = "GeckoViewExample";
  private static final int REQUEST_FILE_PICKER = 1;
  private static final int REQUEST_PERMISSIONS = 2;
  private static final int REQUEST_WRITE_EXTERNAL_STORAGE = 3;

  private static GeckoRuntime sGeckoRuntime;

  private static WebExtensionManager sExtensionManager;

  private TabSessionManager mTabSessionManager;
  private GeckoView mGeckoView;
  private boolean mFullAccessibilityTree;
  private boolean mUsePrivateBrowsing;
  private boolean mCollapsed;
  private boolean mKillProcessOnDestroy;
  private boolean mDesktopMode;

  private TabSession mPopupSession;
  private View mPopupView;
  private ContentPermission mTrackingProtectionPermission;

  private boolean mShowNotificationsRejected;
  private ArrayList<String> mAcceptedPersistentStorage = new ArrayList<String>();

  private ToolbarLayout mToolbarView;
  private String mCurrentUri;
  private boolean mCanGoBack;
  private boolean mCanGoForward;
  private boolean mFullScreen;
  private boolean mExpectedTranslate = false;
  private boolean mTranslateRestore = false;
  private boolean mTranslationsEnabled = true;
  private boolean mPipFullscreenMedia = false;
  private boolean mPipIsPlaying = false;

  private String mDetectedLanguage = null;

  private HashMap<String, Integer> mNotificationIDMap = new HashMap<>();
  private int mLastID = 100;

  private ProgressBar mProgressView;

  private LinkedList<WebResponse> mPendingDownloads = new LinkedList<>();

  private int mNextActivityResultCode = 10;
  private HashMap<Integer, GeckoResult<Intent>> mPendingActivityResult = new HashMap<>();

  private boolean supportsPip() {
    return getPackageManager().hasSystemFeature(PackageManager.FEATURE_PICTURE_IN_PICTURE);
  }

  private boolean maybeEnterPip() {
    if (!supportsPip()) return false;
    if (!mPipFullscreenMedia || !mPipIsPlaying) return false;
    try {
      enterPictureInPictureMode(new PictureInPictureParams.Builder().build());
      return true;
    } catch (IllegalStateException e) {
      return false;
    }
  }

  @Override
  public void onUserLeaveHint() {
    if (!maybeEnterPip()) {
      super.onUserLeaveHint();
    }
  }

  @Override
  public void onPictureInPictureModeChanged(boolean isInPictureInPictureMode) {
    super.onPictureInPictureModeChanged(isInPictureInPictureMode);
    GeckoSession s = (mGeckoView != null) ? mGeckoView.getSession() : null;
    if (s != null) {
      s.getCompositorController().onPipModeChanged(isInPictureInPictureMode);
    }
  }

  private LocationView.CommitListener mCommitListener =
      new LocationView.CommitListener() {
        @Override
        public void onCommit(String text) {
          if (text.startsWith("data:")
              || ((text.contains(".") || text.contains(":")) && !text.contains(" "))) {
            mTabSessionManager.getCurrentSession().loadUri(text);
          } else {
            mTabSessionManager.getCurrentSession().loadUri(SEARCH_URI_BASE + text);
          }
          mGeckoView.requestFocus();
        }
      };

  @Override
  public TabSession openNewTab(WebExtension.CreateTabDetails details) {
    final TabSession newSession = createSession(details.cookieStoreId);
    mToolbarView.updateTabCount();
    if (details.active == Boolean.TRUE) {
      setGeckoViewSession(newSession, false);
    }
    return newSession;
  }

  private final List<Setting<?>> SETTINGS = new ArrayList<>();

  private abstract class Setting<T> {
    private int mKey;
    private int mDefaultKey;
    private final boolean mReloadCurrentSession;
    private T mValue;

    public Setting(final int key, final int defaultValueKey, final boolean reloadCurrentSession) {
      mKey = key;
      mDefaultKey = defaultValueKey;
      mReloadCurrentSession = reloadCurrentSession;

      SETTINGS.add(this);
    }

    public void onPrefChange(SharedPreferences pref) {
      final T defaultValue = getDefaultValue(mDefaultKey, getResources());
      final String key = getResources().getString(this.mKey);
      final T value = getValue(key, defaultValue, pref);
      if (!value().equals(value)) {
        setValue(value);
      }
    }

    private void setValue(final T newValue) {
      mValue = newValue;
      for (final TabSession session : mTabSessionManager.getSessions()) {
        setValue(session.getSettings(), value());
      }
      if (sGeckoRuntime != null) {
        setValue(sGeckoRuntime.getSettings(), value());
        if (sExtensionManager != null) {
          setValue(sGeckoRuntime.getWebExtensionController(), value());
        }
      }

      final GeckoSession current = mTabSessionManager.getCurrentSession();
      if (mReloadCurrentSession && current != null) {
        current.reload();
      }
    }

    public T value() {
      return mValue == null ? getDefaultValue(mDefaultKey, getResources()) : mValue;
    }

    protected abstract T getDefaultValue(final int key, final Resources res);

    protected abstract T getValue(
        final String key, final T defaultValue, final SharedPreferences preferences);

    /** Override one of these to define the behavior when this setting changes. */
    protected void setValue(final GeckoSessionSettings settings, final T value) {}

    protected void setValue(final GeckoRuntimeSettings settings, final T value) {}

    protected void setValue(final WebExtensionController controller, final T value) {}
  }

  private class StringSetting extends Setting<String> {
    public StringSetting(final int key, final int defaultValueKey) {
      this(key, defaultValueKey, false);
    }

    public StringSetting(
        final int key, final int defaultValueKey, final boolean reloadCurrentSession) {
      super(key, defaultValueKey, reloadCurrentSession);
    }

    @Override
    protected String getDefaultValue(int key, final Resources res) {
      return res.getString(key);
    }

    @Override
    public String getValue(
        final String key, final String defaultValue, final SharedPreferences preferences) {
      return preferences.getString(key, defaultValue);
    }
  }

  private class BooleanSetting extends Setting<Boolean> {
    public BooleanSetting(final int key, final int defaultValueKey) {
      this(key, defaultValueKey, false);
    }

    public BooleanSetting(
        final int key, final int defaultValueKey, final boolean reloadCurrentSession) {
      super(key, defaultValueKey, reloadCurrentSession);
    }

    @Override
    protected Boolean getDefaultValue(int key, Resources res) {
      return res.getBoolean(key);
    }

    @Override
    public Boolean getValue(
        final String key, final Boolean defaultValue, final SharedPreferences preferences) {
      return preferences.getBoolean(key, defaultValue);
    }
  }

  private class IntSetting extends Setting<Integer> {
    public IntSetting(final int key, final int defaultValueKey) {
      this(key, defaultValueKey, false);
    }

    public IntSetting(
        final int key, final int defaultValueKey, final boolean reloadCurrentSession) {
      super(key, defaultValueKey, reloadCurrentSession);
    }

    @Override
    protected Integer getDefaultValue(int key, Resources res) {
      return res.getInteger(key);
    }

    @Override
    public Integer getValue(
        final String key, final Integer defaultValue, final SharedPreferences preferences) {
      return Integer.parseInt(preferences.getString(key, Integer.toString(defaultValue)));
    }
  }

  private final IntSetting mDisplayMode =
      new IntSetting(R.string.key_display_mode, R.integer.display_mode_default) {
        @Override
        public void setValue(final GeckoSessionSettings settings, final Integer value) {
          settings.setDisplayMode(value);
        }
      };

  private final IntSetting mPreferredColorScheme =
      new IntSetting(
          R.string.key_preferred_color_scheme,
          R.integer.preferred_color_scheme_default,
          /* reloadCurrentSession */ true) {
        @Override
        public void setValue(final GeckoRuntimeSettings settings, final Integer value) {
          settings.setPreferredColorScheme(value);
        }
      };

  private final StringSetting mUserAgent =
      new StringSetting(
          R.string.key_user_agent_override,
          R.string.user_agent_override_default,
          /* reloadCurrentSession */ true) {
        @Override
        public void setValue(final GeckoSessionSettings settings, final String value) {
          settings.setUserAgentOverride(value.isEmpty() ? null : value);
        }
      };

  private final BooleanSetting mRemoteDebugging =
      new BooleanSetting(R.string.key_remote_debugging, R.bool.remote_debugging_default) {
        @Override
        public void setValue(final GeckoRuntimeSettings settings, final Boolean value) {
          settings.setRemoteDebuggingEnabled(value);
        }
      };

  private final BooleanSetting mJavascriptEnabled =
      new BooleanSetting(
          R.string.key_javascript_enabled,
          R.bool.javascript_enabled_default,
          /* reloadCurrentSession */ true) {
        @Override
        public void setValue(final GeckoRuntimeSettings settings, final Boolean value) {
          settings.setJavaScriptEnabled(value);
        }
      };

  private final BooleanSetting mGlobalPrivacyControlEnabled =
      new BooleanSetting(
          R.string.key_global_privacy_control_enabled,
          R.bool.global_privacy_control_enabled_default,
          /* reloadCurrentSession */ true) {
        @Override
        public void setValue(final GeckoRuntimeSettings settings, final Boolean value) {
          settings.setGlobalPrivacyControl(value);
        }
      };

  private final BooleanSetting mEtbPrivateModeEnabled =
      new BooleanSetting(
          R.string.key_etb_private_mode_enabled,
          R.bool.etb_private_mode_enabled_default,
          /* reloadCurrentSession */ true) {
        @Override
        public void setValue(final GeckoRuntimeSettings settings, final Boolean value) {
          settings.getContentBlocking().setEmailTrackerBlockingPrivateBrowsing(value);
        }
      };

  private final BooleanSetting mExtensionsProcessEnabled =
      new BooleanSetting(
          R.string.key_extensions_process_enabled,
          R.bool.extensions_process_enabled_default,
          /* reloadCurrentSession */ true) {
        @Override
        public void setValue(final GeckoRuntimeSettings settings, final Boolean value) {
          settings.setExtensionsProcessEnabled(value);
        }
      };

  private final BooleanSetting mTrackingProtection =
      new BooleanSetting(R.string.key_tracking_protection, R.bool.tracking_protection_default) {
        @Override
        public void setValue(final GeckoRuntimeSettings settings, final Boolean value) {
          mTabSessionManager.setUseTrackingProtection(value);
          settings.getContentBlocking().setStrictSocialTrackingProtection(value);
        }
      };

  private final BooleanSetting mEdgeToEdgeEnabled =
      new BooleanSetting(R.string.key_edge_to_edge_enabled, R.bool.edge_to_edge_enabled_default) {
        @Override
        public void setValue(final GeckoRuntimeSettings settings, final Boolean value) {
          EdgeToEdgeUtils.setEdgeToEdgeEnabled(value);
        }
      };

  private final StringSetting mEnhancedTrackingProtection =
      new StringSetting(
          R.string.key_enhanced_tracking_protection,
          R.string.enhanced_tracking_protection_default) {
        @Override
        public void setValue(final GeckoRuntimeSettings settings, final String value) {
          int etpLevel;
          switch (value) {
            case "disabled":
              etpLevel = ContentBlocking.EtpLevel.NONE;
              break;
            case "standard":
              etpLevel = ContentBlocking.EtpLevel.DEFAULT;
              break;
            case "strict":
              etpLevel = ContentBlocking.EtpLevel.STRICT;
              break;
            default:
              throw new RuntimeException("Invalid ETP level: " + value);
          }

          settings.getContentBlocking().setEnhancedTrackingProtectionLevel(etpLevel);
        }
      };

  private final BooleanSetting mDynamicFirstPartyIsolation =
      new BooleanSetting(R.string.key_dfpi, R.bool.dfpi_default) {
        @Override
        public void setValue(final GeckoRuntimeSettings settings, final Boolean value) {
          // If dFPI is enabled set appropriate cookieBehavior, else do not overwrite.
          if (value) {
            settings
                .getContentBlocking()
                .setCookieBehavior(
                    ContentBlocking.CookieBehavior.ACCEPT_FIRST_PARTY_AND_ISOLATE_OTHERS);
          }
        }
      };

  private final BooleanSetting mAllowAutoplay =
      new BooleanSetting(
          R.string.key_autoplay, R.bool.autoplay_default, /* reloadCurrentSession */ true);

  private final BooleanSetting mAllowExtensionsInPrivateBrowsing =
      new BooleanSetting(
          R.string.key_allow_extensions_in_private_browsing,
          R.bool.allow_extensions_in_private_browsing_default) {
        @Override
        public void setValue(final WebExtensionController controller, final Boolean value) {
          controller.setAllowedInPrivateBrowsing(sExtensionManager.extension, value);
        }
      };

  private void onPreferencesChange(SharedPreferences preferences) {
    for (Setting<?> setting : SETTINGS) {
      setting.onPrefChange(preferences);
    }
  }

  @OptIn(markerClass = ExperimentalGeckoViewApi.class)
  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);

    // We might have been started because the user clicked on a notification
    WebNotification notification = getIntent().getParcelableExtra("onClick");
    if (notification != null) {
      getIntent().removeExtra("onClick");
      notification.click();
    }

    Log.i(LOGTAG, "zerdatime " + SystemClock.elapsedRealtime() + " - application start");
    createNotificationChannel();
    setContentView(R.layout.geckoview_activity);
    mGeckoView = findViewById(R.id.gecko_view);
    mGeckoView.setActivityContextDelegate(new ExampleActivityDelegate());
    mTabSessionManager = new TabSessionManager();

    WindowUtils.setupPersistentInsets(getWindow(), true);
    WindowUtils.setupImeBehavior(getWindow());
    setSupportActionBar(findViewById(R.id.toolbar));

    SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(this);
    preferences.registerOnSharedPreferenceChangeListener(this);
    // Read initial preference state
    onPreferencesChange(preferences);

    // This references a preference value, so this is after reading initial preference state
    EdgeToEdgeUtils.init(getWindow(), mEdgeToEdgeEnabled.value());

    mToolbarView = new ToolbarLayout(this, mTabSessionManager);
    mToolbarView.setId(R.id.toolbar_layout);
    mToolbarView.setTabListener(this);

    getSupportActionBar()
        .setCustomView(
            mToolbarView,
            new ActionBar.LayoutParams(
                ActionBar.LayoutParams.MATCH_PARENT, ActionBar.LayoutParams.WRAP_CONTENT));
    getSupportActionBar().setDisplayOptions(ActionBar.DISPLAY_SHOW_CUSTOM);

    mFullAccessibilityTree = getIntent().getBooleanExtra(FULL_ACCESSIBILITY_TREE_EXTRA, false);
    mProgressView = findViewById(R.id.page_progress);

    if (sGeckoRuntime == null) {
      final GeckoRuntimeSettings.Builder runtimeSettingsBuilder =
    new GeckoRuntimeSettings.Builder();

      if (BuildConfig.DEBUG) {
        // In debug builds, we want to load JavaScript resources fresh with
        // each build.
        runtimeSettingsBuilder.arguments(new String[] {"-purgecaches"});
      }

      final Bundle extras = getIntent().getExtras();
      if (extras != null) {
        runtimeSettingsBuilder.extras(extras);
      }
      runtimeSettingsBuilder
          .remoteDebuggingEnabled(mRemoteDebugging.value())
          .consoleOutput(true)
          .contentBlocking(
              new ContentBlocking.Settings.Builder()
                  .antiTracking(
                      ContentBlocking.AntiTracking.DEFAULT | ContentBlocking.AntiTracking.STP)
                  .safeBrowsing(ContentBlocking.SafeBrowsing.DEFAULT)
                  .cookieBehavior(ContentBlocking.CookieBehavior.ACCEPT_NON_TRACKERS)
                  .cookieBehaviorPrivateMode(ContentBlocking.CookieBehavior.ACCEPT_NON_TRACKERS)
                  .enhancedTrackingProtectionLevel(ContentBlocking.EtpLevel.DEFAULT)
                  .emailTrackerBlockingPrivateMode(mEtbPrivateModeEnabled.value())
                  .build())
          .crashHandler(ExampleCrashHandler.class)
          .preferredColorScheme(mPreferredColorScheme.value())
          .javaScriptEnabled(mJavascriptEnabled.value())
          .extensionsProcessEnabled(mExtensionsProcessEnabled.value())
          .globalPrivacyControlEnabled(mGlobalPrivacyControlEnabled.value())
          .aboutConfigEnabled(true);

      


      sGeckoRuntime = GeckoRuntime.create(
          this, runtimeSettingsBuilder.build().setCertificateTransparencyMode(1));

      sExtensionManager = new WebExtensionManager(sGeckoRuntime, mTabSessionManager);
      mTabSessionManager.setTabObserver(sExtensionManager);

      sGeckoRuntime.getWebExtensionController().setDebuggerDelegate(sExtensionManager);
      sGeckoRuntime.setAutocompleteStorageDelegate(new ExampleAutocompleteStorageDelegate());
      sGeckoRuntime.getOrientationController().setDelegate(new ExampleOrientationDelegate());
      sGeckoRuntime.setServiceWorkerDelegate(
          new GeckoRuntime.ServiceWorkerDelegate() {
            @NonNull
            @Override
            public GeckoResult<GeckoSession> onOpenWindow(@NonNull String url) {
              return mNavigationDelegate.onNewSession(null, url);
            }
          });

      sGeckoRuntime.setWebNotificationDelegate(
          new WebNotificationDelegate() {
            NotificationManager notificationManager = getSystemService(NotificationManager.class);

            @Override
            public void onShowNotification(@NonNull WebNotification notification) {
              Intent clickIntent = new Intent(GeckoViewActivity.this, GeckoViewActivity.class);
              clickIntent.putExtra("onClick", notification);
              PendingIntent dismissIntent =
                  PendingIntent.getActivity(
                      GeckoViewActivity.this, mLastID, clickIntent, PendingIntent.FLAG_IMMUTABLE);

              NotificationCompat.Builder builder =
                  new NotificationCompat.Builder(GeckoViewActivity.this, CHANNEL_ID)
                      .setContentTitle(notification.title)
                      .setContentText(notification.text)
                      .setSmallIcon(R.drawable.ic_status_logo)
                      .setContentIntent(dismissIntent)
                      .setAutoCancel(true);

              mNotificationIDMap.put(notification.tag, mLastID);

              if (notification.imageUrl != null && notification.imageUrl.length() > 0) {
                final GeckoWebExecutor executor = new GeckoWebExecutor(sGeckoRuntime);

                GeckoResult<WebResponse> response =
                    executor.fetch(
                        new WebRequest.Builder(notification.imageUrl)
                            .addHeader("Accept", "image")
                            .build());
                response.accept(
                    value -> {
                      Bitmap bitmap = BitmapFactory.decodeStream(value.body);
                      builder.setLargeIcon(bitmap);
                      notificationManager.notify(mLastID++, builder.build());
                    });
              } else {
                notificationManager.notify(mLastID++, builder.build());
              }
              notification.show();
            }

            @Override
            public void onCloseNotification(@NonNull WebNotification notification) {
              if (mNotificationIDMap.containsKey(notification.tag)) {
                int id = mNotificationIDMap.get(notification.tag);
                notificationManager.cancel(id);
                mNotificationIDMap.remove(notification.tag);
              }
            }
          });

      sGeckoRuntime.setDelegate(
          () -> {
            mKillProcessOnDestroy = true;
            finish();
          });

      sGeckoRuntime.setActivityDelegate(
          pendingIntent -> {
            final GeckoResult<Intent> result = new GeckoResult<>();
            try {
              final int code = mNextActivityResultCode++;
              mPendingActivityResult.put(code, result);
              GeckoViewActivity.this.startIntentSenderForResult(
                  pendingIntent.getIntentSender(), code, null, 0, 0, 0);
            } catch (IntentSender.SendIntentException e) {
              result.completeExceptionally(e);
            }
            return result;
          });
    }

    sExtensionManager.setExtensionDelegate(this);

    if (savedInstanceState == null) {
      TabSession session = getIntent().getParcelableExtra("session");
      if (session != null) {
        connectSession(session);

        if (!session.isOpen()) {
          session.open(sGeckoRuntime);
        }

        mFullAccessibilityTree = session.getSettings().getFullAccessibilityTree();

        mTabSessionManager.addSession(session);
        session.open(sGeckoRuntime);
        setGeckoViewSession(session);
      } else {
        session = createSession();
        session.open(sGeckoRuntime);
        mTabSessionManager.setCurrentSession(session);
        mGeckoView.setSession(session);
        sGeckoRuntime.getWebExtensionController().setTabActive(session, true);
      }
      loadFromIntent(getIntent());
    }

    mGeckoView.setDynamicToolbarMaxHeight(findViewById(R.id.toolbar).getLayoutParams().height);

    mToolbarView.getLocationView().setCommitListener(mCommitListener);
    mToolbarView.updateTabCount();

    RuntimeAIFeatures.listFeatures()
        .then(
            features -> {
              final AIFeaturesController.AIFeature translations = features.get("translations");
              if (translations != null) {
                mTranslationsEnabled = translations.isEnabled;
                invalidateOptionsMenu();
              }
              return null;
            });
  }

  private void openSettingsActivity() {
    Intent intent = new Intent(this, SettingsActivity.class);
    startActivity(intent);
  }

  @Override
  public TabSession getSession(GeckoSession session) {
    return mTabSessionManager.getSession(session);
  }

  @Override
  public TabSession getCurrentSession() {
    return mTabSessionManager.getCurrentSession();
  }

  @Override
  public void onActionButton(ActionButton button) {
    mToolbarView.setBrowserActionButton(button);
  }

  @Override
  public GeckoSession toggleBrowserActionPopup(boolean force) {
    if (mPopupSession == null) {
      openPopupSession();
    }

    ViewGroup.LayoutParams params = mPopupView.getLayoutParams();
    boolean shouldShow = force || params.width == 0;
    setViewVisibility(mPopupView, shouldShow);

    return shouldShow ? mPopupSession : null;
  }

  private static void setViewVisibility(final View view, final boolean visible) {
    if (view == null) {
      return;
    }

    ViewGroup.LayoutParams params = view.getLayoutParams();

    if (visible) {
      params.height = ViewGroup.LayoutParams.MATCH_PARENT;
      params.width = ViewGroup.LayoutParams.MATCH_PARENT;
    } else {
      params.height = 0;
      params.width = 0;
    }

    view.setLayoutParams(params);
  }

  @Override
  public void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String s) {
    onPreferencesChange(sharedPreferences);
  }

  private class PopupSessionContentDelegate implements GeckoSession.ContentDelegate {
    @Override
    public void onCloseRequest(final GeckoSession session) {
      setViewVisibility(mPopupView, false);
      if (mPopupSession != null) {
        mPopupSession.close();
      }
      mPopupSession = null;
      mPopupView = null;
    }
  }

  private void openPopupSession() {
    LayoutInflater inflater = (LayoutInflater) getSystemService(LAYOUT_INFLATER_SERVICE);
    mPopupView = inflater.inflate(R.layout.browser_action_popup, null);
    GeckoView geckoView = mPopupView.findViewById(R.id.gecko_view_popup);
    geckoView.setViewBackend(GeckoView.BACKEND_TEXTURE_VIEW);
    mPopupSession = new TabSession();
    mPopupSession.setContentDelegate(new PopupSessionContentDelegate());
    mPopupSession.open(sGeckoRuntime);
    geckoView.setSession(mPopupSession);

    mPopupView.setOnFocusChangeListener(this::hideBrowserAction);
    RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams(0, 0);
    params.addRule(RelativeLayout.ABOVE, R.id.toolbar);
    mPopupView.setLayoutParams(params);
    mPopupView.setFocusable(true);
    ((ViewGroup) findViewById(R.id.main)).addView(mPopupView);
  }

  private void hideBrowserAction(View view, boolean hasFocus) {
    if (!hasFocus) {
      ViewGroup.LayoutParams params = mPopupView.getLayoutParams();
      params.height = 0;
      params.width = 0;
      mPopupView.setLayoutParams(params);
    }
  }

  private void createNotificationChannel() {
    CharSequence name = getString(R.string.app_name);
    String description = getString(R.string.activity_label);
    int importance = NotificationManager.IMPORTANCE_DEFAULT;
    NotificationChannel channel = new NotificationChannel(CHANNEL_ID, name, importance);
    channel.setDescription(description);
    // Register the channel with the system; you can't change the importance
    // or other notification behaviors after this
    NotificationManager notificationManager = getSystemService(NotificationManager.class);
    notificationManager.createNotificationChannel(channel);
  }

  private TabSession createSession(final @Nullable String cookieStoreId) {
    GeckoSessionSettings.Builder settingsBuilder = new GeckoSessionSettings.Builder();
    settingsBuilder
        .usePrivateMode(mUsePrivateBrowsing)
        .fullAccessibilityTree(mFullAccessibilityTree)
        .userAgentOverride(mUserAgent.value())
        .viewportMode(
            mDesktopMode
                ? GeckoSessionSettings.VIEWPORT_MODE_DESKTOP
                : GeckoSessionSettings.VIEWPORT_MODE_MOBILE)
        .userAgentMode(
            mDesktopMode
                ? GeckoSessionSettings.USER_AGENT_MODE_DESKTOP
                : GeckoSessionSettings.USER_AGENT_MODE_MOBILE)
        .useTrackingProtection(mTrackingProtection.value())
        .displayMode(mDisplayMode.value());

    if (cookieStoreId != null) {
      settingsBuilder.contextId(cookieStoreId);
    }

    TabSession session = mTabSessionManager.newSession(settingsBuilder.build());
    connectSession(session);

    return session;
  }

  private TabSession createSession() {
    return createSession(null);
  }

  private final GeckoSession.NavigationDelegate mNavigationDelegate =
      new ExampleNavigationDelegate();

  private void connectSession(GeckoSession session) {
    session.setContentDelegate(new ExampleContentDelegate());
    session.setHistoryDelegate(new ExampleHistoryDelegate());
    final ExampleContentBlockingDelegate cb = new ExampleContentBlockingDelegate();
    session.setContentBlockingDelegate(cb);
    session.setProgressDelegate(new ExampleProgressDelegate(cb));
    session.setNavigationDelegate(mNavigationDelegate);

    final BasicGeckoViewPrompt prompt = new BasicGeckoViewPrompt(this);
    prompt.filePickerRequestCode = REQUEST_FILE_PICKER;
    session.setPromptDelegate(prompt);

    final ExamplePermissionDelegate permission = new ExamplePermissionDelegate();
    permission.androidPermissionRequestCode = REQUEST_PERMISSIONS;
    session.setPermissionDelegate(permission);

    session.setMediaDelegate(new ExampleMediaDelegate(this));

    session.setMediaSessionDelegate(new ExampleMediaSessionDelegate(this));

    session.setTranslationsSessionDelegate(new ExampleTranslationsSessionDelegate());

    session.setSelectionActionDelegate(new BasicSelectionActionDelegate(this));
    if (sExtensionManager.extension != null) {
      final WebExtension.SessionController sessionController = session.getWebExtensionController();
      sessionController.setActionDelegate(sExtensionManager.extension, sExtensionManager);
      sessionController.setTabDelegate(sExtensionManager.extension, sExtensionManager);
    }

    updateDesktopMode(session);
  }

  private void recreateSession() {
    recreateSession(mTabSessionManager.getCurrentSession());
  }

  private void recreateSession(TabSession session) {
    if (session != null) {
      mTabSessionManager.closeSession(session);
    }

    session = createSession();
    session.open(sGeckoRuntime);
    mTabSessionManager.setCurrentSession(session);
    mGeckoView.setSession(session);
    sGeckoRuntime.getWebExtensionController().setTabActive(session, true);
    if (mCurrentUri != null) {
      session.loadUri(mCurrentUri);
    }
  }

  @Override
  public void onRestoreInstanceState(Bundle savedInstanceState) {
    super.onRestoreInstanceState(savedInstanceState);
    if (savedInstanceState != null && mGeckoView.getSession() != null) {
      mTabSessionManager.setCurrentSession((TabSession) mGeckoView.getSession());
      sGeckoRuntime.getWebExtensionController().setTabActive(mGeckoView.getSession(), true);
    } else {
      recreateSession();
    }
  }

  private void updateDesktopMode(GeckoSession session) {
    session
        .getSettings()
        .setViewportMode(
            mDesktopMode
                ? GeckoSessionSettings.VIEWPORT_MODE_DESKTOP
                : GeckoSessionSettings.VIEWPORT_MODE_MOBILE);
    session
        .getSettings()
        .setUserAgentMode(
            mDesktopMode
                ? GeckoSessionSettings.USER_AGENT_MODE_DESKTOP
                : GeckoSessionSettings.USER_AGENT_MODE_MOBILE);
  }

  @Override
  public void onBackPressed() {
    GeckoSession session = mTabSessionManager.getCurrentSession();
    if (mFullScreen && session != null) {
      session.exitFullScreen();
      return;
    }

    session
        .processBackPressed()
        .accept(
            handled -> {
              if (handled) {
                return;
              }

              if (mCanGoBack && session != null) {
                session.goBack();
                return;
              }

              super.onBackPressed();
            });
  }

  @Override
  public boolean onCreateOptionsMenu(Menu menu) {
    MenuInflater inflater = getMenuInflater();
    inflater.inflate(R.menu.actions, menu);
    return true;
  }

  @Override
  public boolean onPrepareOptionsMenu(Menu menu) {
    menu.findItem(R.id.action_pb).setChecked(mUsePrivateBrowsing);
    menu.findItem(R.id.collapse).setChecked(mCollapsed);
    menu.findItem(R.id.desktop_mode).setChecked(mDesktopMode);
    menu.findItem(R.id.action_tpe)
        .setChecked(
            mTrackingProtectionPermission != null
                && mTrackingProtectionPermission.value == ContentPermission.VALUE_ALLOW);
    menu.findItem(R.id.action_forward).setEnabled(mCanGoForward);

    final boolean hasSession = mTabSessionManager.getCurrentSession() != null;
    menu.findItem(R.id.action_reload).setEnabled(hasSession);
    menu.findItem(R.id.action_forward).setEnabled(hasSession);
    menu.findItem(R.id.action_close_tab).setEnabled(hasSession);
    menu.findItem(R.id.action_tpe).setEnabled(hasSession && mTrackingProtectionPermission != null);
    menu.findItem(R.id.action_pb).setEnabled(hasSession);
    menu.findItem(R.id.desktop_mode).setEnabled(hasSession);
    menu.findItem(R.id.translate).setVisible(mExpectedTranslate);
    menu.findItem(R.id.translate_restore).setVisible(mTranslateRestore);
    menu.findItem(R.id.translate_disable).setVisible(mTranslationsEnabled);
    menu.findItem(R.id.translate_enable).setVisible(!mTranslationsEnabled);
    return true;
  }

  @Override
  public boolean onOptionsItemSelected(MenuItem item) {
    GeckoSession session = mTabSessionManager.getCurrentSession();
    int id = item.getItemId();

    if (id == R.id.action_reload) {
      session.reload();
    } else if (id == R.id.action_forward) {
      session.goForward();
    } else if (id == R.id.action_tpe) {
      sGeckoRuntime
          .getStorageController()
          .setPermission(
              mTrackingProtectionPermission,
              mTrackingProtectionPermission.value == ContentPermission.VALUE_ALLOW
                  ? ContentPermission.VALUE_DENY
                  : ContentPermission.VALUE_ALLOW);
      session.reload();
    } else if (id == R.id.desktop_mode) {
      mDesktopMode = !mDesktopMode;
      updateDesktopMode(session);
      session.reload();
    } else if (id == R.id.action_pb) {
      mUsePrivateBrowsing = !mUsePrivateBrowsing;
      recreateSession();
    } else if (id == R.id.collapse) {
      mCollapsed = !mCollapsed;
      setViewVisibility(mGeckoView, !mCollapsed);
    } else if (id == R.id.install_addon) {
      installAddon();
    } else if (id == R.id.update_addon) {
      updateAddon();
    } else if (id == R.id.settings) {
      openSettingsActivity();
    } else if (id == R.id.action_new_tab) {
      createNewTab();
    } else if (id == R.id.action_close_tab) {
      closeTab((TabSession) session);
    } else if (id == R.id.save_pdf) {
      savePdf(session);
    } else if (id == R.id.print_page) {
      printPage(session);
    } else if (id == R.id.translate) {
      translate(session);
    } else if (id == R.id.translate_restore) {
      translateRestore(session);
    } else if (id == R.id.translate_manage) {
      translateManage();
    } else if (id == R.id.translate_disable) {
      translateSetEnabled(false);
    } else if (id == R.id.translate_enable) {
      translateSetEnabled(true);
    } else if (id == R.id.webcompat_info) {
      webCompatInfo(session);
    } else if (id == R.id.take_screenshot) {
      takeScreenshot();
    } else {
      return super.onOptionsItemSelected(item);
    }
    return true;
  }

  private void installAddon() {
    final AlertDialog.Builder builder = new AlertDialog.Builder(this);
    builder.setTitle(R.string.install_addon);

    final EditText input = new EditText(this);
    input.setInputType(InputType.TYPE_CLASS_TEXT);
    input.setHint(R.string.install_addon_hint);
    builder.setView(input);

    builder.setPositiveButton(
        R.string.install,
        (dialog, which) -> {
          final String uri = input.getText().toString();

          // We only suopport one extension at a time, so remove the currently installed
          // extension if there is one
          setViewVisibility(mPopupView, false);
          mPopupView = null;
          mPopupSession = null;
          sExtensionManager
              .unregisterExtension()
              .then(
                  unused -> {
                    final WebExtensionController controller =
                        sGeckoRuntime.getWebExtensionController();
                    controller.setPromptDelegate(sExtensionManager);
                    return controller.install(uri, null);
                  })
              .then(
                  extension ->
                      sGeckoRuntime
                          .getWebExtensionController()
                          .setAllowedInPrivateBrowsing(
                              extension, mAllowExtensionsInPrivateBrowsing.value()))
              .accept(extension -> sExtensionManager.registerExtension(extension));
        });
    builder.setNegativeButton(
        R.string.cancel,
        (dialog, which) -> {
          // Nothing to do
        });

    builder.show();
  }

  private void updateAddon() {
    final AlertDialog.Builder builder = new AlertDialog.Builder(this);
    builder.setTitle(R.string.update_addon);

    sExtensionManager
        .updateExtension()
        .accept(
            extension -> {
              if (extension != null) {
                builder.setMessage("Success");
              } else {
                builder.setMessage("No addon to update");
              }
              builder.show();
            },
            exception -> {
              builder.setMessage("Failed: " + exception);
              builder.show();
            });
  }

  private void createNewTab() {
    Double startTime = ProfilerController.getProfilerTime();
    TabSession newSession = createSession();
    newSession.open(sGeckoRuntime);
    setGeckoViewSession(newSession);
    mToolbarView.updateTabCount();
    ProfilerController.addMarker("Create new tab", startTime);
  }

  private void takeScreenshot() {
    mGeckoView
        .capturePixels()
        .map(
            bitmap -> {
              ContentResolver resolver = getContentResolver();

              ContentValues contentValues = new ContentValues();
              contentValues.put(MediaStore.MediaColumns.DISPLAY_NAME, "screenshot.jpg");
              contentValues.put(MediaStore.MediaColumns.MIME_TYPE, "image/png");
              contentValues.put(
                  MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_PICTURES);
              contentValues.put(MediaStore.MediaColumns.IS_PENDING, 1);

              Uri screenshotUri =
                  resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, contentValues);

              if (screenshotUri != null) {
                try (OutputStream out = resolver.openOutputStream(screenshotUri)) {
                  bitmap.compress(Bitmap.CompressFormat.PNG, 100, out);
                } catch (Throwable e) {
                  Log.e(LOGTAG, "Error saving screenshot: " + e.getMessage());
                  if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    resolver.delete(screenshotUri, null);
                  }
                }
              } else {
                Log.e(LOGTAG, "Error saving screenshot: the screenshotUri is null");
              }
              contentValues.clear();
              contentValues.put(MediaStore.MediaColumns.IS_PENDING, 0);
              if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                resolver.update(screenshotUri, contentValues, null);
              }
              return null;
            });
  }

  @SuppressLint("WrongThread")
  @UiThread
  private void savePdf(GeckoSession session) {
    session
        .saveAsPdf()
        .accept(
            pdfStream -> {
              try {
                WebResponse response =
                    new WebResponse.Builder(null)
                        .body(pdfStream)
                        .addHeader("Content-Type", "application/pdf")
                        .addHeader("Content-Disposition", "attachment; filename=PDFDownload.pdf")
                        .build();
                session.getContentDelegate().onExternalResponse(session, response);

              } catch (Exception e) {
                Log.d(LOGTAG, e.getMessage());
              }
            });
  }

  private void printPage(GeckoSession session) {
    session.didPrintPageContent();
  }

  private void translate(GeckoSession session) {
    final AlertDialog.Builder builder = new AlertDialog.Builder(this);
    builder.setTitle(R.string.translate);
    Spinner fromSelect = new Spinner(this);
    Spinner toSelect = new Spinner(this);

    // Set spinners with data
    TranslationsController.RuntimeTranslation.listSupportedLanguages()
        .then(
            supportedLanguages -> {
              // Just a check if sorting is working on the Language object by reversing, Languages
              // should generally come from the API in the display order.
              Collections.reverse(supportedLanguages.fromLanguages);
              ArrayAdapter<TranslationsController.Language> fromData =
                  new ArrayAdapter<TranslationsController.Language>(
                      this.getBaseContext(),
                      android.R.layout.simple_spinner_item,
                      supportedLanguages.fromLanguages);
              fromSelect.setAdapter(fromData);
              // Set detected language
              final int index =
                  fromData.getPosition(
                      new TranslationsController.Language(mDetectedLanguage, null));
              fromSelect.setSelection(index);

              ArrayAdapter<TranslationsController.Language> toData =
                  new ArrayAdapter<TranslationsController.Language>(
                      this.getBaseContext(),
                      android.R.layout.simple_spinner_item,
                      supportedLanguages.toLanguages);
              toSelect.setAdapter(toData);
              // Set preferred language
              TranslationsController.RuntimeTranslation.preferredLanguages()
                  .then(
                      preferredList -> {
                        Log.d(LOGTAG, "Preferred Translation Languages: " + preferredList);
                        // Reorder dropdown listing based on preferences
                        for (int i = preferredList.size() - 1; i >= 0; i--) {
                          final int langIndex =
                              toData.getPosition(
                                  new TranslationsController.Language(preferredList.get(i), null));
                          if (langIndex < 0) {
                            continue;
                          }
                          TranslationsController.Language displayLanguage =
                              toData.getItem(langIndex);
                          toData.remove(displayLanguage);
                          toData.insert(displayLanguage, 0);
                          if (i == 0) {
                            toSelect.setSelection(0);
                          }
                        }
                        return null;
                      });
              return null;
            });
    builder.setView(
        translateLayout(
            fromSelect,
            R.string.translate_language_from_hint,
            toSelect,
            R.string.translate_language_to_hint,
            -1));
    builder.setPositiveButton(
        R.string.translate_action,
        (dialog, which) -> {
          final TranslationsController.Language fromLang =
              (TranslationsController.Language) fromSelect.getSelectedItem();
          final TranslationsController.Language toLang =
              (TranslationsController.Language) toSelect.getSelectedItem();
          session.getSessionTranslation().translate(fromLang.code, toLang.code, null);
          mTranslateRestore = true;
        });
    builder.setNegativeButton(
        R.string.cancel,
        (dialog, which) -> {
          // Nothing to do
        });

    builder.show();
  }

  private void translateRestore(GeckoSession session) {
    session
        .getSessionTranslation()
        .restoreOriginalPage()
        .then(
            new GeckoResult.OnValueListener<Void, Object>() {
              @Nullable
              @Override
              public GeckoResult<Object> onValue(@Nullable Void value) throws Throwable {
                mTranslateRestore = false;
                return null;
              }
            });
  }

  @OptIn(markerClass = ExperimentalGeckoViewApi.class)
  private void translateSetEnabled(final boolean enabled) {
    RuntimeAIFeatures.setFeatureEnablement("translations", enabled)
        .then(
            value -> {
              mTranslationsEnabled = enabled;
              invalidateOptionsMenu();
              return null;
            },
            exception -> {
              Log.e(LOGTAG, "Could not set translations enabled state: " + exception);
              return null;
            });
  }

  private void translateManage() {
    Spinner languageSelect = new Spinner(this);
    Spinner operationSelect = new Spinner(this);
    // Should match ModelOperation choices
    List<String> operationChoices =
        new ArrayList<>(
            Arrays.asList(
                new String[] {
                  TranslationsController.RuntimeTranslation.DELETE.toString(),
                  TranslationsController.RuntimeTranslation.DOWNLOAD.toString()
                }));
    ArrayAdapter<String> operationData =
        new ArrayAdapter<String>(
            this.getBaseContext(), android.R.layout.simple_spinner_item, operationChoices);
    operationSelect.setAdapter(operationData);

    // Get current model states
    GeckoResult<List<TranslationsController.RuntimeTranslation.LanguageModel>> currentStates =
        TranslationsController.RuntimeTranslation.listModelDownloadStates();
    currentStates.then(
        models -> {
          List<TranslationsController.Language> languages =
              new ArrayList<TranslationsController.Language>();
          // Pseudo container of "all" just to simplify spinner for GVE
          languages.add(new TranslationsController.Language("all", "All Models"));
          for (var model : models) {
            Log.i(LOGTAG, "Translate Model State: " + model);
            languages.add(model.language);
          }
          ArrayAdapter<TranslationsController.Language> languageData =
              new ArrayAdapter<TranslationsController.Language>(
                  this.getBaseContext(), android.R.layout.simple_spinner_item, languages);
          languageSelect.setAdapter(languageData);
          return null;
        });

    final AlertDialog.Builder builder = new AlertDialog.Builder(this);
    builder.setTitle(R.string.translate_manage);
    builder.setView(
        translateLayout(
            languageSelect,
            R.string.translate_manage_languages,
            operationSelect,
            R.string.translate_manage_operations,
            R.string.translate_display_hint));
    builder.setPositiveButton(
        R.string.translate_manage_action,
        (dialog, which) -> {
          final TranslationsController.Language selectedLanguage =
              (TranslationsController.Language) languageSelect.getSelectedItem();

          final String operation = (String) operationSelect.getSelectedItem();

          String operationLevel = TranslationsController.RuntimeTranslation.LANGUAGE;
          // Pseudo option for ease of GVE
          if (selectedLanguage.code.equals("all")) {
            operationLevel = TranslationsController.RuntimeTranslation.ALL;
          }
          TranslationsController.RuntimeTranslation.ModelManagementOptions options =
              new TranslationsController.RuntimeTranslation.ModelManagementOptions.Builder()
                  .languageToManage(selectedLanguage.code)
                  .operation(operation)
                  .operationLevel(operationLevel)
                  .build();

          // Complete Operation
          GeckoResult<Void> requestOperation =
              TranslationsController.RuntimeTranslation.manageLanguageModel(options);
          requestOperation.then(
              opt -> {
                // Log Changes
                GeckoResult<List<TranslationsController.RuntimeTranslation.LanguageModel>>
                    reportChanges =
                        TranslationsController.RuntimeTranslation.listModelDownloadStates();
                reportChanges.then(
                    models -> {
                      for (var model : models) {
                        Log.i(LOGTAG, "Translate Model State: " + model);
                      }
                      return null;
                    });
                return null;
              });
        });
    builder.setNegativeButton(
        R.string.cancel,
        (dialog, which) -> {
          // Nothing to do
        });

    builder.show();
  }

  private RelativeLayout translateLayout(
      Spinner spinnerA, int labelA, Spinner spinnerB, int labelB, int labelInfo) {
    // From fields
    TextView fromLangLabel = new TextView(this);
    fromLangLabel.setText(labelA);
    LinearLayout from = new LinearLayout(this);
    from.setId(View.generateViewId());
    from.addView(fromLangLabel);
    from.addView(spinnerA);
    RelativeLayout.LayoutParams fromParams =
        new RelativeLayout.LayoutParams(
            RelativeLayout.LayoutParams.WRAP_CONTENT, RelativeLayout.LayoutParams.WRAP_CONTENT);
    fromParams.setMarginStart(30);

    // To fields
    TextView toLangLabel = new TextView(this);
    toLangLabel.setText(labelB);
    LinearLayout to = new LinearLayout(this);
    to.setId(View.generateViewId());
    to.addView(toLangLabel);
    to.addView(spinnerB);
    RelativeLayout.LayoutParams toParams =
        new RelativeLayout.LayoutParams(
            RelativeLayout.LayoutParams.WRAP_CONTENT, RelativeLayout.LayoutParams.WRAP_CONTENT);
    toParams.setMarginStart(30);
    toParams.addRule(RelativeLayout.BELOW, from.getId());

    // Layout
    RelativeLayout layout = new RelativeLayout(this);
    layout.addView(from, fromParams);
    layout.addView(to, toParams);

    // Hint
    TextView info = new TextView(this);
    if (labelInfo != -1) {
      RelativeLayout.LayoutParams infoParams =
          new RelativeLayout.LayoutParams(
              RelativeLayout.LayoutParams.WRAP_CONTENT, RelativeLayout.LayoutParams.WRAP_CONTENT);
      infoParams.setMarginStart(30);
      infoParams.addRule(RelativeLayout.BELOW, to.getId());
      info.setText(labelInfo);
      layout.addView(info, infoParams);
    }

    return layout;
  }

  @Override
  public void closeTab(TabSession session) {
    mTabSessionManager.closeSession(session);
    TabSession tabSession = mTabSessionManager.getCurrentSession();
    setGeckoViewSession(tabSession);
    if (tabSession != null) {
      tabSession.reload();
    }
    mToolbarView.updateTabCount();
  }

  @Override
  public void updateTab(TabSession session, WebExtension.UpdateTabDetails details) {
    if (details.active == Boolean.TRUE) {
      switchToSession(session, false);
    }
  }

  public void onBrowserActionClick() {
    sExtensionManager.onClicked(mTabSessionManager.getCurrentSession());
  }

  public void switchToSession(TabSession session, boolean activateTab) {
    TabSession currentSession = mTabSessionManager.getCurrentSession();
    if (session != currentSession) {
      setGeckoViewSession(session, activateTab);
      mCurrentUri = session.getUri();
      if (!session.isOpen()) {
        // Session's process was previously killed; reopen
        session.open(sGeckoRuntime);
        session.loadUri(mCurrentUri);
      }
      mToolbarView.getLocationView().setText(mCurrentUri);
    }
  }

  public void switchToTab(int index) {
    TabSession nextSession = mTabSessionManager.getSession(index);
    switchToSession(nextSession, true);
  }

  private void setGeckoViewSession(TabSession session) {
    setGeckoViewSession(session, true);
  }

  private void setGeckoViewSession(TabSession session, boolean activateTab) {
    final WebExtensionController controller = sGeckoRuntime.getWebExtensionController();
    final GeckoSession previousSession = mGeckoView.getSession();
    if (previousSession != null) {
      controller.setTabActive(previousSession, false);
    }

    final boolean hasSession = session != null;
    final LocationView view = mToolbarView.getLocationView();
    // No point having the URL bar enabled if there's no session to navigate to
    view.setEnabled(hasSession);

    if (hasSession) {
      mGeckoView.setSession(session);
      if (activateTab) {
        controller.setTabActive(session, true);
      }
      mTabSessionManager.setCurrentSession(session);
    } else {
      mGeckoView.coverUntilFirstPaint(Color.WHITE);
      view.setText("");
    }
  }

  @Override
  public void onDestroy() {
    if (mKillProcessOnDestroy) {
      android.os.Process.killProcess(android.os.Process.myPid());
    }

    super.onDestroy();
  }

  @Override
  protected void onNewIntent(final Intent intent) {
    super.onNewIntent(intent);

    if (ACTION_SHUTDOWN.equals(intent.getAction())) {
      mKillProcessOnDestroy = true;
      if (sGeckoRuntime != null) {
        sGeckoRuntime.shutdown();
      }
      finish();
      return;
    }

    if (intent.hasExtra("onClick")) {
      WebNotification notification = intent.getExtras().getParcelable("onClick");
      if (notification != null) {
        intent.removeExtra("onClick");
        notification.click();
      }
    }

    setIntent(intent);

    if (intent.getData() != null) {
      loadFromIntent(intent);
    }
  }

  private void loadFromIntent(final Intent intent) {
    final Uri uri = intent.getData();
    if (uri != null) {
      mTabSessionManager
          .getCurrentSession()
          .load(
              new GeckoSession.Loader()
                  .uri(uri.toString())
                  .flags(GeckoSession.LOAD_FLAGS_EXTERNAL));
    }
  }

  @Override
  protected void onActivityResult(final int requestCode, final int resultCode, final Intent data) {
    if (requestCode == REQUEST_FILE_PICKER) {
      final BasicGeckoViewPrompt prompt =
          (BasicGeckoViewPrompt) mTabSessionManager.getCurrentSession().getPromptDelegate();
      prompt.onFileCallbackResult(resultCode, data);
    } else if (mPendingActivityResult.containsKey(requestCode)) {
      final GeckoResult<Intent> result = mPendingActivityResult.remove(requestCode);

      if (resultCode == Activity.RESULT_OK) {
        result.complete(data);
      } else {
        result.completeExceptionally(new RuntimeException("Unknown error"));
      }
    } else {
      super.onActivityResult(requestCode, resultCode, data);
    }
  }

  @Override
  public void onRequestPermissionsResult(
      final int requestCode, final String[] permissions, final int[] grantResults) {
    if (requestCode == REQUEST_PERMISSIONS) {
      final ExamplePermissionDelegate permission =
          (ExamplePermissionDelegate)
              mTabSessionManager.getCurrentSession().getPermissionDelegate();
      permission.onRequestPermissionsResult(permissions, grantResults);
    } else if (requestCode == REQUEST_WRITE_EXTERNAL_STORAGE
        && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
      continueDownloads();
    } else {
      super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }
  }

  private void continueDownloads() {
    final LinkedList<WebResponse> downloads = mPendingDownloads;
    mPendingDownloads = new LinkedList<>();

    for (final WebResponse response : downloads) {
      downloadFile(response);
    }
  }

  private String sanitizeMimeType(String mimeType) {
    if (mimeType != null) {
      if (mimeType.contains(";")) {
        return mimeType.split(";")[0].trim();
      } else {
        return mimeType.trim();
      }
    } else {
      return null;
    }
  }

  private void downloadFile(final WebResponse response) {
    if (response.body == null) return;

    final String filename = getFileName(response);
    Log.i(LOGTAG, "FileName:" + filename);

    if (Build.VERSION.SDK_INT > Build.VERSION_CODES.Q) { // latest
      ContentResolver contentResolver = getContentResolver();
      String mime = sanitizeMimeType(response.headers.get("Content-Type"));
      if (mime == null || mime.isEmpty()) mime = "*/*";
      ContentValues contentValues = new ContentValues();
      contentValues.put(MediaStore.MediaColumns.DISPLAY_NAME, filename);
      contentValues.put(MediaStore.MediaColumns.MIME_TYPE, mime);
      Log.i(LOGTAG, "MimeType:" + mime);
      contentValues.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
      contentValues.put(MediaStore.MediaColumns.IS_PENDING, 1);

      Uri collection = MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);
      Uri fileUri = contentResolver.insert(collection, contentValues);
      if (fileUri == null) {
        Toast.makeText(this, "Unable to access downloads directory", Toast.LENGTH_LONG).show();
        return;
      }
      // write to file
      Toast.makeText(this, "Downloading " + filename, Toast.LENGTH_LONG).show();
      int bufferSize = 1024; // to read in 1Mb increments
      byte[] buffer = new byte[bufferSize];
      try (OutputStream out = contentResolver.openOutputStream(fileUri)) {
        int len;
        while ((len = response.body.read(buffer)) != -1) {
          out.write(buffer, 0, len);
        }
      } catch (Throwable e) {
        Log.i(LOGTAG, String.valueOf(e.getStackTrace()));
      }
      // Release Pending
      contentValues.clear();
      contentValues.put(MediaStore.MediaColumns.IS_PENDING, 0);
      contentResolver.update(fileUri, contentValues, null, null);
    } else { // legacy support
      if (ContextCompat.checkSelfPermission(
              GeckoViewActivity.this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
          != PackageManager.PERMISSION_GRANTED) {
        mPendingDownloads.add(response);
        ActivityCompat.requestPermissions(
            GeckoViewActivity.this,
            new String[] {Manifest.permission.WRITE_EXTERNAL_STORAGE},
            REQUEST_WRITE_EXTERNAL_STORAGE);
      }
    }
  }

  private String getFileName(final WebResponse response) {
    String filename;
    String contentDispositionHeader;
    if (response.headers.containsKey("content-disposition")) {
      contentDispositionHeader = response.headers.get("content-disposition");
    } else {
      contentDispositionHeader =
          response.headers.getOrDefault("Content-Disposition", "default filename=GVDownload");
    }
    Pattern pattern = Pattern.compile("(filename=\"?)(.+)(\"?)");
    Matcher matcher = pattern.matcher(contentDispositionHeader);
    if (matcher.find()) {
      filename = matcher.group(2).replaceAll("\\s", "%20").replaceAll("\"", "");
    } else {
      filename = "GVEdownload";
    }

    return filename;
  }

  private static boolean isForeground() {
    final ActivityManager.RunningAppProcessInfo appProcessInfo =
        new ActivityManager.RunningAppProcessInfo();
    ActivityManager.getMyMemoryState(appProcessInfo);
    return appProcessInfo.importance == ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND
        || appProcessInfo.importance == ActivityManager.RunningAppProcessInfo.IMPORTANCE_VISIBLE;
  }

  private String mErrorTemplate;

  private String createErrorPage(final String error) {
    if (mErrorTemplate == null) {
      InputStream stream = null;
      BufferedReader reader = null;
      StringBuilder builder = new StringBuilder();
      try {
        stream = getResources().getAssets().open("error.html");
        reader = new BufferedReader(new InputStreamReader(stream));

        String line;
        while ((line = reader.readLine()) != null) {
          builder.append(line);
          builder.append("\n");
        }

        mErrorTemplate = builder.toString();
      } catch (IOException e) {
        Log.d(LOGTAG, "Failed to open error page template", e);
        return null;
      } finally {
        if (stream != null) {
          try {
            stream.close();
          } catch (IOException e) {
            Log.e(LOGTAG, "Failed to close error page template stream", e);
          }
        }

        if (reader != null) {
          try {
            reader.close();
          } catch (IOException e) {
            Log.e(LOGTAG, "Failed to close error page template reader", e);
          }
        }
      }
    }

    return mErrorTemplate.replace("$ERROR", error);
  }

  private class ExampleHistoryDelegate implements GeckoSession.HistoryDelegate {
    private final HashSet<String> mVisitedURLs;

    private ExampleHistoryDelegate() {
      mVisitedURLs = new HashSet<String>();
    }

    @Override
    public GeckoResult<Boolean> onVisited(
        GeckoSession session, String url, String lastVisitedURL, int flags) {
      Log.i(LOGTAG, "Visited URL: " + url);

      mVisitedURLs.add(url);
      return GeckoResult.fromValue(true);
    }

    @Override
    public GeckoResult<boolean[]> getVisited(GeckoSession session, String[] urls) {
      boolean[] visited = new boolean[urls.length];
      for (int i = 0; i < urls.length; i++) {
        visited[i] = mVisitedURLs.contains(urls[i]);
      }
      return GeckoResult.fromValue(visited);
    }

    @Override
    public void onHistoryStateChange(
        final GeckoSession session, final GeckoSession.HistoryDelegate.HistoryList state) {
      Log.i(LOGTAG, "History state updated");
    }
  }

  private class ExampleAutocompleteStorageDelegate implements Autocomplete.StorageDelegate {
    private Map<String, Autocomplete.LoginEntry> mStorage = new HashMap<>();

    @Nullable
    @Override
    public GeckoResult<Autocomplete.LoginEntry[]> onLoginFetch() {
      return GeckoResult.fromValue(mStorage.values().toArray(new Autocomplete.LoginEntry[0]));
    }

    @Override
    public void onLoginSave(@NonNull Autocomplete.LoginEntry login) {
      mStorage.put(login.guid, login);
    }
  }

  private class ExampleOrientationDelegate implements OrientationController.OrientationDelegate {
    @Override
    public GeckoResult<AllowOrDeny> onOrientationLock(@NonNull int aOrientation) {
      setRequestedOrientation(aOrientation);
      return GeckoResult.allow();
    }

    @Override
    public void onOrientationUnlock() {
      setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
    }
  }

  private class ExampleContentDelegate implements GeckoSession.ContentDelegate {
    @Override
    public void onTitleChange(GeckoSession session, String title) {
      Log.i(LOGTAG, "Content title changed to " + title);
      TabSession tabSession = mTabSessionManager.getSession(session);
      if (tabSession != null) {
        tabSession.setTitle(title);
      }
    }

    @Override
    public void onFullScreen(final GeckoSession session, final boolean fullScreen) {
      mFullScreen = fullScreen;
      if (fullScreen) {
        getSupportActionBar().hide();
        mGeckoView.setDynamicToolbarMaxHeight(0);
        WindowUtils.enterImmersiveMode(getWindow());
      } else {
        getSupportActionBar().show();
        mGeckoView.setDynamicToolbarMaxHeight(getSupportActionBar().getHeight());
        WindowUtils.exitImmersiveMode(getWindow());
      }
    }

    @Override
    public void onFocusRequest(final GeckoSession session) {
      Log.i(LOGTAG, "Content requesting focus");
    }

    @Override
    public void onCloseRequest(final GeckoSession session) {
      final TabSession currentSession = mTabSessionManager.getCurrentSession();
      if (session == currentSession) {
        closeTab(currentSession);
      }
    }

    @Override
    public void onContextMenu(
        final GeckoSession session, int screenX, int screenY, final ContextElement element) {
      Log.d(
          LOGTAG,
          "onContextMenu screenX="
              + screenX
              + " screenY="
              + screenY
              + " type="
              + element.type
              + " linkUri="
              + element.linkUri
              + " title="
              + element.title
              + " alt="
              + element.altText
              + " srcUri="
              + element.srcUri);
    }

    @Override
    public void onExternalResponse(@NonNull GeckoSession session, @NonNull WebResponse response) {
      downloadFile(response);
    }

    @Override
    public void onCrash(GeckoSession session) {
      Log.e(LOGTAG, "Crashed, reopening session");
      session.open(sGeckoRuntime);
    }

    @Override
    public void onKill(GeckoSession session) {
      TabSession tabSession = mTabSessionManager.getSession(session);
      if (tabSession == null) {
        return;
      }

      if (tabSession != mTabSessionManager.getCurrentSession()) {
        Log.e(LOGTAG, "Background session killed");
        return;
      }

      if (isForeground()) {
        throw new IllegalStateException("Foreground content process unexpectedly killed by OS!");
      }

      Log.e(LOGTAG, "Current session killed, reopening");

      tabSession.open(sGeckoRuntime);
      tabSession.loadUri(tabSession.getUri());
    }

    @Override
    public void onFirstComposite(final GeckoSession session) {
      Log.d(LOGTAG, "onFirstComposite");
    }

    @Override
    public void onWebAppManifest(final GeckoSession session, JSONObject manifest) {
      Log.d(LOGTAG, "onWebAppManifest: " + manifest);
    }

    private boolean activeAlert = false;

    @Override
    public GeckoResult<SlowScriptResponse> onSlowScript(
        final GeckoSession geckoSession, final String scriptFileName) {
      BasicGeckoViewPrompt prompt =
          (BasicGeckoViewPrompt) mTabSessionManager.getCurrentSession().getPromptDelegate();
      if (prompt != null) {
        GeckoResult<SlowScriptResponse> result = new GeckoResult<SlowScriptResponse>();
        if (!activeAlert) {
          activeAlert = true;
          prompt.onSlowScriptPrompt(geckoSession, getString(R.string.slow_script), result);
        }
        return result.then(
            value -> {
              activeAlert = false;
              return GeckoResult.fromValue(value);
            });
      }
      return null;
    }

    @Override
    public void onMetaViewportFitChange(final GeckoSession session, final String viewportFit) {
      if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P) {
        return;
      }
      WindowManager.LayoutParams layoutParams = getWindow().getAttributes();
      if (viewportFit.equals("cover")) {
        layoutParams.layoutInDisplayCutoutMode =
            WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
      } else if (viewportFit.equals("contain")) {
        layoutParams.layoutInDisplayCutoutMode =
            WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER;
      } else {
        layoutParams.layoutInDisplayCutoutMode =
            WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT;
      }
      getWindow().setAttributes(layoutParams);
    }

    @Override
    public void onShowDynamicToolbar(final GeckoSession session) {
      final View toolbar = findViewById(R.id.toolbar);
      if (toolbar != null) {
        toolbar.setTranslationY(0f);
        mGeckoView.setVerticalClipping(0);
      }
    }

    @Override
    public void onHideDynamicToolbar(final GeckoSession session) {
      final View toolbar = findViewById(R.id.toolbar);
      if (toolbar != null) {
        toolbar.setTranslationY(toolbar.getHeight());
      }
    }
  }

  private class ExampleProgressDelegate implements GeckoSession.ProgressDelegate {
    private ExampleContentBlockingDelegate mCb;

    private ExampleProgressDelegate(final ExampleContentBlockingDelegate cb) {
      mCb = cb;
    }

    @Override
    public void onPageStart(GeckoSession session, String url) {
      Log.i(LOGTAG, "Starting to load page at " + url);
      Log.i(LOGTAG, "zerdatime " + SystemClock.elapsedRealtime() + " - page load start");
      mCb.clearCounters();
      mExpectedTranslate = false;
      mTranslateRestore = false;
    }

    @Override
    public void onPageStop(GeckoSession session, boolean success) {
      Log.i(LOGTAG, "Stopping page load " + (success ? "successfully" : "unsuccessfully"));
      Log.i(LOGTAG, "zerdatime " + SystemClock.elapsedRealtime() + " - page load stop");
      mCb.logCounters();
    }

    @Override
    public void onProgressChange(GeckoSession session, int progress) {
      Log.i(LOGTAG, "onProgressChange " + progress);

      mProgressView.setProgress(progress);

      if (progress > 0 && progress < 100) {
        mProgressView.setVisibility(View.VISIBLE);
      } else {
        mProgressView.setVisibility(View.GONE);
      }
    }

    @Override
    public void onSecurityChange(GeckoSession session, SecurityInformation securityInfo) {
      Log.i(LOGTAG, "Security status changed to " + securityInfo.securityMode);
    }

    @Override
    public void onSessionStateChange(GeckoSession session, GeckoSession.SessionState state) {
      Log.i(LOGTAG, "New Session state: " + state.toString());
    }
  }

  private class ExamplePermissionDelegate implements PermissionDelegate {

    public int androidPermissionRequestCode = 1;
    private Callback mCallback;

    class ExampleNotificationCallback implements PermissionDelegate.Callback {
      private final PermissionDelegate.Callback mCallback;

      ExampleNotificationCallback(final PermissionDelegate.Callback callback) {
        mCallback = callback;
      }

      @Override
      public void reject() {
        mShowNotificationsRejected = true;
        mCallback.reject();
      }

      @Override
      public void grant() {
        mShowNotificationsRejected = false;
        mCallback.grant();
      }
    }

    class ExamplePersistentStorageCallback implements PermissionDelegate.Callback {
      private final PermissionDelegate.Callback mCallback;
      private final String mUri;

      ExamplePersistentStorageCallback(final PermissionDelegate.Callback callback, String uri) {
        mCallback = callback;
        mUri = uri;
      }

      @Override
      public void reject() {
        mCallback.reject();
      }

      @Override
      public void grant() {
        mAcceptedPersistentStorage.add(mUri);
        mCallback.grant();
      }
    }

    public void onRequestPermissionsResult(final String[] permissions, final int[] grantResults) {
      if (mCallback == null) {
        return;
      }

      final Callback cb = mCallback;
      mCallback = null;
      for (final int result : grantResults) {
        if (result != PackageManager.PERMISSION_GRANTED) {
          // At least one permission was not granted.
          cb.reject();
          return;
        }
      }
      cb.grant();
    }

    @Override
    public void onAndroidPermissionsRequest(
        final GeckoSession session, final String[] permissions, final Callback callback) {
      mCallback = callback;
      requestPermissions(permissions, androidPermissionRequestCode);
    }

    @Override
    public GeckoResult<Integer> onContentPermissionRequest(
        final GeckoSession session, final ContentPermission perm) {
      final int resId;
      switch (perm.permission) {
        case PERMISSION_GEOLOCATION:
          resId = R.string.request_geolocation;
          break;
        case PERMISSION_DESKTOP_NOTIFICATION:
          resId = R.string.request_notification;
          break;
        case PERMISSION_PERSISTENT_STORAGE:
          resId = R.string.request_storage;
          break;
        case PERMISSION_XR:
          resId = R.string.request_xr;
          break;
        case PERMISSION_AUTOPLAY_AUDIBLE:
        case PERMISSION_AUTOPLAY_INAUDIBLE:
          if (!mAllowAutoplay.value()) {
            return GeckoResult.fromValue(ContentPermission.VALUE_DENY);
          } else {
            return GeckoResult.fromValue(ContentPermission.VALUE_ALLOW);
          }
        case PERMISSION_MEDIA_KEY_SYSTEM_ACCESS:
          resId = R.string.request_media_key_system_access;
          break;
        case PERMISSION_STORAGE_ACCESS:
          resId = R.string.request_storage_access;
          break;
        default:
          return GeckoResult.fromValue(ContentPermission.VALUE_DENY);
      }

      final String title = getString(resId, Uri.parse(perm.uri).getAuthority());
      final BasicGeckoViewPrompt prompt =
          (BasicGeckoViewPrompt) mTabSessionManager.getCurrentSession().getPromptDelegate();
      return prompt.onPermissionPrompt(session, title, perm);
    }

    private String[] normalizeMediaName(final MediaSource[] sources) {
      if (sources == null) {
        return null;
      }

      String[] res = new String[sources.length];
      for (int i = 0; i < sources.length; i++) {
        final int mediaSource = sources[i].source;
        final String name = sources[i].name;
        if (MediaSource.SOURCE_CAMERA == mediaSource) {
          if (name.toLowerCase(Locale.ROOT).contains("front")) {
            res[i] = getString(R.string.media_front_camera);
          } else {
            res[i] = getString(R.string.media_back_camera);
          }
        } else if (!name.isEmpty()) {
          res[i] = name;
        } else if (MediaSource.SOURCE_MICROPHONE == mediaSource) {
          res[i] = getString(R.string.media_microphone);
        } else {
          res[i] = getString(R.string.media_other);
        }
      }

      return res;
    }

    @Override
    public void onMediaPermissionRequest(
        final GeckoSession session,
        final String uri,
        final MediaSource[] video,
        final MediaSource[] audio,
        final MediaCallback callback) {
      // If we don't have device permissions at this point, just automatically reject the request
      // as we will have already have requested device permissions before getting to this point
      // and if we've reached here and we don't have permissions then that means that the user
      // denied them.
      if ((audio != null
              && ContextCompat.checkSelfPermission(
                      GeckoViewActivity.this, Manifest.permission.RECORD_AUDIO)
                  != PackageManager.PERMISSION_GRANTED)
          || (video != null
              && ContextCompat.checkSelfPermission(
                      GeckoViewActivity.this, Manifest.permission.CAMERA)
                  != PackageManager.PERMISSION_GRANTED)) {
        callback.reject();
        return;
      }

      final String host = Uri.parse(uri).getAuthority();
      final String title;
      if (audio == null) {
        title = getString(R.string.request_video, host);
      } else if (video == null) {
        title = getString(R.string.request_audio, host);
      } else {
        title = getString(R.string.request_media, host);
      }

      String[] videoNames = normalizeMediaName(video);
      String[] audioNames = normalizeMediaName(audio);

      final BasicGeckoViewPrompt prompt =
          (BasicGeckoViewPrompt) mTabSessionManager.getCurrentSession().getPromptDelegate();
      prompt.onMediaPrompt(session, title, video, audio, videoNames, audioNames, callback);
    }
  }

  private ContentPermission getTrackingProtectionPermission(final List<ContentPermission> perms) {
    for (ContentPermission perm : perms) {
      if (perm.permission == PermissionDelegate.PERMISSION_TRACKING) {
        return perm;
      }
    }
    return null;
  }

  public void webCompatInfo(@NonNull final GeckoSession session) {
    GeckoResult<JSONObject> result = session.getWebCompatInfo();
    result.map(
        getWebCompatInfo -> {
          Log.d(LOGTAG, "Received web compat info.");
          if (getWebCompatInfo != null) {
            JSONObject info = new JSONObject();
            info.put("reason", "Reason");
            info.put("description", "Description");
            info.put("endpointUrl", "https://webcompat.com/issues/new");
            info.put("reportUrl", "https://www.mozilla.org/en-US/firefox/");

            JSONObject reporterConfig = new JSONObject();
            reporterConfig.put("src", "android-components-reporter");
            reporterConfig.put("utm_campaign", "report-site-issue-button");
            reporterConfig.put("utm_source", "android-components-reporter");

            info.put("reporterConfig", reporterConfig);
            info.put("webcompatInfo", getWebCompatInfo);

            session.sendMoreWebCompatInfo(info);
          }
          return getWebCompatInfo;
        });
  }

  private class ExampleNavigationDelegate implements GeckoSession.NavigationDelegate {
    @Override
    public void onLocationChange(
        GeckoSession session,
        final String url,
        final List<ContentPermission> perms,
        Boolean hasUserGesture) {
      mToolbarView.getLocationView().setText(url);
      TabSession tabSession = mTabSessionManager.getSession(session);
      if (tabSession != null) {
        tabSession.onLocationChange(url);
      }
      mTrackingProtectionPermission = getTrackingProtectionPermission(perms);
      mCurrentUri = url;
    }

    @Override
    public void onCanGoBack(GeckoSession session, boolean canGoBack) {
      mCanGoBack = canGoBack;
    }

    @Override
    public void onCanGoForward(GeckoSession session, boolean canGoForward) {
      mCanGoForward = canGoForward;
    }

    @Override
    public GeckoResult<AllowOrDeny> onLoadRequest(
        final GeckoSession session, final LoadRequest request) {
      Log.d(
          LOGTAG,
          "onLoadRequest="
              + request.uri
              + " triggerUri="
              + request.triggerUri
              + " where="
              + request.target
              + " isRedirect="
              + request.isRedirect
              + " isDirectNavigation="
              + request.isDirectNavigation);

      return GeckoResult.allow();
    }

    @Override
    public GeckoResult<AllowOrDeny> onSubframeLoadRequest(
        final GeckoSession session, final LoadRequest request) {
      Log.d(
          LOGTAG,
          "onSubframeLoadRequest="
              + request.uri
              + " triggerUri="
              + request.triggerUri
              + " isRedirect="
              + request.isRedirect
              + "isDirectNavigation="
              + request.isDirectNavigation);

      return GeckoResult.allow();
    }

    @Override
    public GeckoResult<GeckoSession> onNewSession(final GeckoSession session, final String uri) {
      final TabSession newSession = createSession();
      mToolbarView.updateTabCount();
      setGeckoViewSession(newSession);
      // A reference to newSession is stored by mTabSessionManager,
      // which prevents the session from being garbage-collected.
      return GeckoResult.fromValue(newSession);
    }

    private String categoryToString(final int category) {
      switch (category) {
        case WebRequestError.ERROR_CATEGORY_UNKNOWN:
          return "ERROR_CATEGORY_UNKNOWN";
        case WebRequestError.ERROR_CATEGORY_SECURITY:
          return "ERROR_CATEGORY_SECURITY";
        case WebRequestError.ERROR_CATEGORY_NETWORK:
          return "ERROR_CATEGORY_NETWORK";
        case WebRequestError.ERROR_CATEGORY_CONTENT:
          return "ERROR_CATEGORY_CONTENT";
        case WebRequestError.ERROR_CATEGORY_URI:
          return "ERROR_CATEGORY_URI";
        case WebRequestError.ERROR_CATEGORY_PROXY:
          return "ERROR_CATEGORY_PROXY";
        case WebRequestError.ERROR_CATEGORY_SAFEBROWSING:
          return "ERROR_CATEGORY_SAFEBROWSING";
        default:
          return "UNKNOWN";
      }
    }

    private String errorToString(final int error) {
      switch (error) {
        case WebRequestError.ERROR_UNKNOWN:
          return "ERROR_UNKNOWN";
        case WebRequestError.ERROR_SECURITY_SSL:
          return "ERROR_SECURITY_SSL";
        case WebRequestError.ERROR_SECURITY_BAD_CERT:
          return "ERROR_SECURITY_BAD_CERT";
        case WebRequestError.ERROR_NET_RESET:
          return "ERROR_NET_RESET";
        case WebRequestError.ERROR_NET_INTERRUPT:
          return "ERROR_NET_INTERRUPT";
        case WebRequestError.ERROR_NET_TIMEOUT:
          return "ERROR_NET_TIMEOUT";
        case WebRequestError.ERROR_CONNECTION_REFUSED:
          return "ERROR_CONNECTION_REFUSED";
        case WebRequestError.ERROR_LOCAL_NETWORK_ACCESS_DENIED:
          return "ERROR_LOCAL_NETWORK_ACCESS_DENIED";
        case WebRequestError.ERROR_UNKNOWN_PROTOCOL:
          return "ERROR_UNKNOWN_PROTOCOL";
        case WebRequestError.ERROR_UNKNOWN_HOST:
          return "ERROR_UNKNOWN_HOST";
        case WebRequestError.ERROR_UNKNOWN_SOCKET_TYPE:
          return "ERROR_UNKNOWN_SOCKET_TYPE";
        case WebRequestError.ERROR_UNKNOWN_PROXY_HOST:
          return "ERROR_UNKNOWN_PROXY_HOST";
        case WebRequestError.ERROR_MALFORMED_URI:
          return "ERROR_MALFORMED_URI";
        case WebRequestError.ERROR_REDIRECT_LOOP:
          return "ERROR_REDIRECT_LOOP";
        case WebRequestError.ERROR_SAFEBROWSING_PHISHING_URI:
          return "ERROR_SAFEBROWSING_PHISHING_URI";
        case WebRequestError.ERROR_SAFEBROWSING_MALWARE_URI:
          return "ERROR_SAFEBROWSING_MALWARE_URI";
        case WebRequestError.ERROR_SAFEBROWSING_UNWANTED_URI:
          return "ERROR_SAFEBROWSING_UNWANTED_URI";
        case WebRequestError.ERROR_SAFEBROWSING_HARMFUL_URI:
          return "ERROR_SAFEBROWSING_HARMFUL_URI";
        case WebRequestError.ERROR_HARMFULADDON_URI:
          return "ERROR_HARMFULADDON_URI";
        case WebRequestError.ERROR_CONTENT_CRASHED:
          return "ERROR_CONTENT_CRASHED";
        case WebRequestError.ERROR_OFFLINE:
          return "ERROR_OFFLINE";
        case WebRequestError.ERROR_PORT_BLOCKED:
          return "ERROR_PORT_BLOCKED";
        case WebRequestError.ERROR_PROXY_CONNECTION_REFUSED:
          return "ERROR_PROXY_CONNECTION_REFUSED";
        case WebRequestError.ERROR_FILE_NOT_FOUND:
          return "ERROR_FILE_NOT_FOUND";
        case WebRequestError.ERROR_FILE_ACCESS_DENIED:
          return "ERROR_FILE_ACCESS_DENIED";
        case WebRequestError.ERROR_INVALID_CONTENT_ENCODING:
          return "ERROR_INVALID_CONTENT_ENCODING";
        case WebRequestError.ERROR_UNSAFE_CONTENT_TYPE:
          return "ERROR_UNSAFE_CONTENT_TYPE";
        case WebRequestError.ERROR_CORRUPTED_CONTENT:
          return "ERROR_CORRUPTED_CONTENT";
        case WebRequestError.ERROR_HTTPS_ONLY:
          return "ERROR_HTTPS_ONLY";
        case WebRequestError.ERROR_BAD_HSTS_CERT:
          return "ERROR_BAD_HSTS_CERT";
        default:
          return "UNKNOWN";
      }
    }

    private String createErrorPage(final int category, final int error) {
      if (mErrorTemplate == null) {
        InputStream stream = null;
        BufferedReader reader = null;
        StringBuilder builder = new StringBuilder();
        try {
          stream = getResources().getAssets().open("error.html");
          reader = new BufferedReader(new InputStreamReader(stream));

          String line;
          while ((line = reader.readLine()) != null) {
            builder.append(line);
            builder.append("\n");
          }

          mErrorTemplate = builder.toString();
        } catch (IOException e) {
          Log.d(LOGTAG, "Failed to open error page template", e);
          return null;
        } finally {
          if (stream != null) {
            try {
              stream.close();
            } catch (IOException e) {
              Log.e(LOGTAG, "Failed to close error page template stream", e);
            }
          }

          if (reader != null) {
            try {
              reader.close();
            } catch (IOException e) {
              Log.e(LOGTAG, "Failed to close error page template reader", e);
            }
          }
        }
      }

      return GeckoViewActivity.this.createErrorPage(
          categoryToString(category) + " : " + errorToString(error));
    }

    @Override
    public GeckoResult<String> onLoadError(
        final GeckoSession session, final String uri, final WebRequestError error) {
      Log.d(
          LOGTAG,
          "onLoadError=" + uri + " error category=" + error.category + " error=" + error.code);

      return GeckoResult.fromValue("data:text/html," + createErrorPage(error.category, error.code));
    }
  }

  private class ExampleContentBlockingDelegate implements ContentBlocking.Delegate {
    private int mBlockedAds = 0;
    private int mBlockedAnalytics = 0;
    private int mBlockedSocial = 0;
    private int mBlockedContent = 0;
    private int mBlockedTest = 0;
    private int mBlockedStp = 0;

    private void clearCounters() {
      mBlockedAds = 0;
      mBlockedAnalytics = 0;
      mBlockedSocial = 0;
      mBlockedContent = 0;
      mBlockedTest = 0;
      mBlockedStp = 0;
    }

    private void logCounters() {
      Log.d(
          LOGTAG,
          "Trackers blocked: "
              + mBlockedAds
              + " ads, "
              + mBlockedAnalytics
              + " analytics, "
              + mBlockedSocial
              + " social, "
              + mBlockedContent
              + " content, "
              + mBlockedTest
              + " test, "
              + mBlockedStp
              + "stp");
    }

    @Override
    public void onContentBlocked(
        final GeckoSession session, final ContentBlocking.BlockEvent event) {
      Log.d(
          LOGTAG,
          "onContentBlocked"
              + " AT: "
              + event.getAntiTrackingCategory()
              + " SB: "
              + event.getSafeBrowsingCategory()
              + " CB: "
              + event.getCookieBehaviorCategory()
              + " URI: "
              + event.uri);
      if ((event.getAntiTrackingCategory() & ContentBlocking.AntiTracking.TEST) != 0) {
        mBlockedTest++;
      }
      if ((event.getAntiTrackingCategory() & ContentBlocking.AntiTracking.AD) != 0) {
        mBlockedAds++;
      }
      if ((event.getAntiTrackingCategory() & ContentBlocking.AntiTracking.ANALYTIC) != 0) {
        mBlockedAnalytics++;
      }
      if ((event.getAntiTrackingCategory() & ContentBlocking.AntiTracking.SOCIAL) != 0) {
        mBlockedSocial++;
      }
      if ((event.getAntiTrackingCategory() & ContentBlocking.AntiTracking.CONTENT) != 0) {
        mBlockedContent++;
      }
      if ((event.getAntiTrackingCategory() & ContentBlocking.AntiTracking.STP) != 0) {
        mBlockedStp++;
      }
    }

    @Override
    public void onContentLoaded(
        final GeckoSession session, final ContentBlocking.BlockEvent event) {
      Log.d(
          LOGTAG,
          "onContentLoaded"
              + " AT: "
              + event.getAntiTrackingCategory()
              + " SB: "
              + event.getSafeBrowsingCategory()
              + " CB: "
              + event.getCookieBehaviorCategory()
              + " URI: "
              + event.uri);
    }
  }

  private class ExampleMediaDelegate implements GeckoSession.MediaDelegate {
    private Integer mLastNotificationId = 100;
    private Integer mNotificationId;
    private final Activity mActivity;

    public ExampleMediaDelegate(Activity activity) {
      mActivity = activity;
    }

    @Override
    public void onRecordingStatusChanged(@NonNull GeckoSession session, RecordingDevice[] devices) {
      String message;
      int icon;
      NotificationManagerCompat notificationManager = NotificationManagerCompat.from(mActivity);
      RecordingDevice camera = null;
      RecordingDevice microphone = null;

      for (RecordingDevice device : devices) {
        if (device.type == RecordingDevice.Type.CAMERA) {
          camera = device;
        } else if (device.type == RecordingDevice.Type.MICROPHONE) {
          microphone = device;
        }
      }
      if (camera != null && microphone != null) {
        Log.d(LOGTAG, "ExampleDeviceDelegate:onRecordingDeviceEvent display alert_mic_camera");
        message = getResources().getString(R.string.device_sharing_camera_and_mic);
        icon = R.drawable.alert_mic_camera;
      } else if (camera != null) {
        Log.d(LOGTAG, "ExampleDeviceDelegate:onRecordingDeviceEvent display alert_camera");
        message = getResources().getString(R.string.device_sharing_camera);
        icon = R.drawable.alert_camera;
      } else if (microphone != null) {
        Log.d(LOGTAG, "ExampleDeviceDelegate:onRecordingDeviceEvent display alert_mic");
        message = getResources().getString(R.string.device_sharing_microphone);
        icon = R.drawable.alert_mic;
      } else {
        Log.d(LOGTAG, "ExampleDeviceDelegate:onRecordingDeviceEvent dismiss any notifications");
        if (mNotificationId != null) {
          notificationManager.cancel(mNotificationId);
          mNotificationId = null;
        }
        return;
      }
      if (mNotificationId == null) {
        mNotificationId = ++mLastNotificationId;
      }

      Intent intent = new Intent(mActivity, GeckoViewActivity.class);
      intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
      PendingIntent pendingIntent =
          PendingIntent.getActivity(
              mActivity.getApplicationContext(), 0, intent, PendingIntent.FLAG_IMMUTABLE);

      NotificationCompat.Builder builder =
          new NotificationCompat.Builder(mActivity.getApplicationContext(), CHANNEL_ID)
              .setSmallIcon(icon)
              .setContentTitle(getResources().getString(R.string.app_name))
              .setContentText(message)
              .setPriority(NotificationCompat.PRIORITY_DEFAULT)
              .setContentIntent(pendingIntent)
              .setCategory(NotificationCompat.CATEGORY_SERVICE);

      notificationManager.notify(mNotificationId, builder.build());
    }
  }

  private class ExampleTranslationsSessionDelegate
      implements TranslationsController.SessionTranslation.Delegate {
    @Override
    public void onOfferTranslate(@NonNull GeckoSession session) {
      Log.i(LOGTAG, "onOfferTranslate");
    }

    @Override
    public void onExpectedTranslate(@NonNull GeckoSession session) {
      Log.i(LOGTAG, "onExpectedTranslate");
      mExpectedTranslate = true;
    }

    @Override
    public void onTranslationStateChange(
        @NonNull GeckoSession session,
        @Nullable TranslationsController.SessionTranslation.TranslationState translationState) {
      Log.i(LOGTAG, "onTranslationStateChange");
      if (translationState.detectedLanguages != null) {
        mDetectedLanguage = translationState.detectedLanguages.docLangTag;
      }
    }
  }

  private class ExampleMediaSessionDelegate implements MediaSession.Delegate {
    private final Activity mActivity;

    public ExampleMediaSessionDelegate(Activity activity) {
      mActivity = activity;
    }

    @Override
    public void onPlay(@NonNull GeckoSession session, @NonNull MediaSession mediaSession) {
      mPipIsPlaying = true;
    }

    @Override
    public void onPause(@NonNull GeckoSession session, @NonNull MediaSession mediaSession) {
      mPipIsPlaying = false;
    }

    @Override
    public void onStop(@NonNull GeckoSession session, @NonNull MediaSession mediaSession) {
      mPipIsPlaying = false;
    }

    @Override
    public void onFullscreen(
        @NonNull final GeckoSession session,
        @NonNull final MediaSession mediaSession,
        final boolean enabled,
        @Nullable final MediaSession.ElementMetadata meta) {
      mPipFullscreenMedia = enabled;
      Log.d(LOGTAG, "onFullscreen: Metadata=" + (meta != null ? meta.toString() : "null"));

      if (!enabled) {
        mActivity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_USER);
        return;
      }

      if (meta == null) {
        return;
      }

      if (meta.width > meta.height) {
        mActivity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
      } else {
        mActivity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT);
      }
    }
  }

  private class ExampleActivityDelegate implements GeckoView.ActivityContextDelegate {
    public Context getActivityContext() {
      return GeckoViewActivity.this;
    }
  }
}
