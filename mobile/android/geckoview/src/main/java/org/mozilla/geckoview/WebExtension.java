/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.annotation.SuppressLint;
import android.graphics.Color;
import android.util.Log;
import androidx.annotation.AnyThread;
import androidx.annotation.IntDef;
import androidx.annotation.LongDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.UiThread;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import org.json.JSONException;
import org.json.JSONObject;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.util.BundleEventListener;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoBundle;

/** Represents a WebExtension that may be used by GeckoView. */
public class WebExtension {
  /**
   * <code>file:</code> or <code>resource:</code> URI that points to the install location of this
   * WebExtension. When the WebExtension is included with the APK the file can be specified using
   * the <code>resource://android</code> alias. E.g.
   *
   * <pre><code>
   *      resource://android/assets/web_extensions/my_webextension/
   * </code></pre>
   *
   * Will point to folder <code>/assets/web_extensions/my_webextension/</code> in the APK.
   */
  public final @NonNull String location;

  /** Unique identifier for this WebExtension */
  public final @NonNull String id;

  /** {@link Flags} for this WebExtension. */
  public final @WebExtensionFlags long flags;

  /** Provides information about this {@link WebExtension}. */
  public final @NonNull MetaData metaData;

  /**
   * Whether this extension is built-in. Built-in extension can be installed using {@link
   * WebExtensionController#installBuiltIn}.
   */
  public final boolean isBuiltIn;

  /**
   * Called whenever a delegate is set or unset on this {@link WebExtension} instance. /* package
   */
  interface DelegateController {
    void onMessageDelegate(final String nativeApp, final MessageDelegate delegate);

    void onActionDelegate(final ActionDelegate delegate);

    void onBrowsingDataDelegate(final BrowsingDataDelegate delegate);

    void onTabDelegate(final TabDelegate delegate);

    void onDownloadDelegate(final DownloadDelegate delegate);

    ActionDelegate getActionDelegate();

    BrowsingDataDelegate getBrowsingDataDelegate();

    TabDelegate getTabDelegate();

    DownloadDelegate getDownloadDelegate();
  }

  /* package */ interface DelegateControllerProvider {
    @NonNull
    DelegateController controllerFor(final WebExtension extension);
  }

  private final DelegateController mDelegateController;

  @Override
  public String toString() {
    return "WebExtension {"
        + "location="
        + location
        + ", "
        + "id="
        + id
        + ", "
        + "flags="
        + flags
        + "}";
  }

  private static final String LOGTAG = "WebExtension";

  /**
   * The list of data collection permission names.
   *
   * <p>This list should be kept in sync with the WebExtensions JSON schema defined at the gecko
   * toolkit level. It is made public so that upper Android layers can have access to a list of data
   * collection permissions that is guaranteed to match the gecko one, which can be useful in tests
   * for instance.
   */
  public static final List<String> DATA_COLLECTION_PERMISSIONS =
      List.of(
          "authenticationInfo",
          "bookmarksInfo",
          "browsingActivity",
          "financialAndPaymentInfo",
          "healthInfo",
          "locationInfo",
          "none",
          "personalCommunications",
          "personallyIdentifyingInfo",
          "searchTerms",
          "technicalAndInteraction",
          "websiteActivity",
          "websiteContent");

  // Keep in sync with GeckoViewWebExtension.sys.mjs
  /** Flags that can be used to configure WebExtension behavior. */
  public static class Flags {
    /** Default flags for this WebExtension. */
    public static final long NONE = 0;

    /**
     * Set this flag if you want to enable content scripts messaging. To listen to such messages you
     * can use {@link SessionController#setMessageDelegate}. This flag is set if the extension is
     * built-in or a privileged extension, with its manifest.json containing the
     * "nativeMessagingFromContent" permission.
     */
    public static final long ALLOW_CONTENT_MESSAGING = 1 << 0;

    /** Do not instantiate this class. */
    protected Flags() {}
  }

  /** Defines the valid flags that can be applied to a WebExtension. */
  @Retention(RetentionPolicy.SOURCE)
  @LongDef(
      flag = true,
      value = {Flags.NONE, Flags.ALLOW_CONTENT_MESSAGING})
  public @interface WebExtensionFlags {}

  /* package */ WebExtension(final DelegateControllerProvider provider, final GeckoBundle bundle) {
    location = bundle.getString("locationURI");
    id = bundle.getString("webExtensionId");
    flags = bundle.getInt("webExtensionFlags", 0);
    isBuiltIn = bundle.getBoolean("isBuiltIn", false);
    if (bundle.containsKey("metaData")) {
      try {
        metaData = new MetaData(bundle.getBundle("metaData"));
      } catch (final Exception e) {
        throw new InvalidMetaDataException(e, id);
      }
    } else {
      metaData = null;
    }
    mDelegateController = provider.controllerFor(this);
  }

  /**
   * Defines the message delegate for a Native App.
   *
   * <p>This message delegate will receive messages from the background script for the native app
   * specified in <code>nativeApp</code>.
   *
   * <p>For messages from content scripts, set a session-specific message delegate using {@link
   * SessionController#setMessageDelegate}.
   *
   * <p>This functionality currently requires the "geckoViewAddons" permission, only available to
   * built-in and privileged extensions.
   *
   * <p>For the extension API side, see <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Native_messaging">
   * WebExtensions/Native_messaging</a>.
   *
   * <p>For the GeckoView API side, see <a
   * href="https://firefox-source-docs.mozilla.org/mobile/android/geckoview/consumer/web-extensions.html">
   * Using GeckoView: Interacting with Web content and WebExtensions</a>.
   *
   * @param messageDelegate handles messaging between the WebExtension and the app. To send a
   *     message from the WebExtension use the <code>runtime.sendNativeMessage</code> WebExtension
   *     API: E.g.
   *     <pre><code>
   *        browser.runtime.sendNativeMessage(nativeApp,
   *              {message: "Hello from WebExtension!"});
   *      </code></pre>
   *     For bidirectional communication, use <code>runtime.connectNative</code>. E.g. in a content
   *     script:
   *     <pre><code>
   *          let port = browser.runtime.connectNative(nativeApp);
   *          port.onMessage.addListener(message =&gt; {
   *              console.log("Message received from app");
   *          });
   *          port.postMessage("Ping from WebExtension");
   *      </code></pre>
   *     The code above will trigger a {@link MessageDelegate#onConnect} call that will contain the
   *     corresponding {@link Port} object that can be used to send messages to the WebExtension.
   *     Note: the <code>nativeApp</code> specified in the WebExtension needs to match the <code>
   *     nativeApp</code> parameter of this method.
   *     <p>You can unset the message delegate by setting a <code>null</code> messageDelegate.
   * @param nativeApp which native app id this message delegate will handle messaging for. Needs to
   *     match the <code>application</code> parameter of <code>runtime.sendNativeMessage</code> and
   *     <code>runtime.connectNative</code>.
   * @see SessionController#setMessageDelegate
   */
  @UiThread
  public void setMessageDelegate(
      final @Nullable MessageDelegate messageDelegate, final @NonNull String nativeApp) {
    mDelegateController.onMessageDelegate(nativeApp, messageDelegate);
  }

  /** Browsing data type definitions for extensions. */
  @Retention(RetentionPolicy.SOURCE)
  @LongDef(
      value = {
        BrowsingDataDelegate.Type.CACHE,
        BrowsingDataDelegate.Type.COOKIES,
        BrowsingDataDelegate.Type.DOWNLOADS,
        BrowsingDataDelegate.Type.FORM_DATA,
        BrowsingDataDelegate.Type.HISTORY,
        BrowsingDataDelegate.Type.LOCAL_STORAGE,
        BrowsingDataDelegate.Type.PASSWORDS
      },
      flag = true)
  public @interface BrowsingDataTypes {}

  /**
   * This delegate is used to handle calls from the |browsingData| WebExtension API.
   *
   * <p>See also: <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browsingData">
   * WebExtensions/API/browsingData </a>
   */
  @UiThread
  public interface BrowsingDataDelegate {
    /**
     * This class represents the current default settings for the "Clear Data" functionality in the
     * browser.
     *
     * <p>See also: <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browsingData/settings">
     * WebExtensions/API/browsingData/settings </a>
     */
    @UiThread
    class Settings {
      /**
       * Currently selected setting in the browser's "Clear Data" UI for how far back in time to
       * remove data given in milliseconds since the UNIX epoch.
       */
      public final int sinceUnixTimestamp;

      /**
       * Data types that can be toggled in the browser's "Clear Data" UI. One or more flags from
       * {@link Type}.
       */
      public final @BrowsingDataTypes long toggleableTypes;

      /**
       * Data types currently selected in the browser's "Clear Data" UI. One or more flags from
       * {@link Type}.
       */
      public final @BrowsingDataTypes long selectedTypes;

      /**
       * Creates an instance of Settings.
       *
       * <p>This class represents the current default settings for the "Clear Data" functionality in
       * the browser.
       *
       * @param since Currently selected setting in the browser's "Clear Data" UI for how far back
       *     in time to remove data given in milliseconds since the UNIX epoch.
       * @param toggleableTypes Data types that can be toggled in the browser's "Clear Data" UI. One
       *     or more flags from {@link Type}.
       * @param selectedTypes Data types currently selected in the browser's "Clear Data" UI. One or
       *     more flags from {@link Type}.
       */
      @UiThread
      public Settings(
          final int since,
          final @BrowsingDataTypes long toggleableTypes,
          final @BrowsingDataTypes long selectedTypes) {
        this.toggleableTypes = toggleableTypes;
        this.selectedTypes = selectedTypes;
        this.sinceUnixTimestamp = since;
      }

      private GeckoBundle fromBrowsingDataType(final @BrowsingDataTypes long types) {
        final GeckoBundle result = new GeckoBundle(7);
        result.putBoolean("cache", (types & Type.CACHE) != 0);
        result.putBoolean("cookies", (types & Type.COOKIES) != 0);
        result.putBoolean("downloads", (types & Type.DOWNLOADS) != 0);
        result.putBoolean("formData", (types & Type.FORM_DATA) != 0);
        result.putBoolean("history", (types & Type.HISTORY) != 0);
        result.putBoolean("localStorage", (types & Type.LOCAL_STORAGE) != 0);
        result.putBoolean("passwords", (types & Type.PASSWORDS) != 0);
        return result;
      }

      /* package */ GeckoBundle toGeckoBundle() {
        final GeckoBundle options = new GeckoBundle(1);
        options.putLong("since", sinceUnixTimestamp);

        final GeckoBundle result = new GeckoBundle(3);
        result.putBundle("options", options);
        result.putBundle("dataToRemove", fromBrowsingDataType(selectedTypes));
        result.putBundle("dataRemovalPermitted", fromBrowsingDataType(toggleableTypes));
        return result;
      }
    }

    /** Types of data that a browser "Clear Data" UI might have access to. */
    class Type {
      /** Utility class; do not instantiate. */
      protected Type() {}

      /** Cache entries. */
      public static final long CACHE = 1 << 0;

      /** Cookie data. */
      public static final long COOKIES = 1 << 1;

      /** Downloads data. */
      public static final long DOWNLOADS = 1 << 2;

      /** Form data. */
      public static final long FORM_DATA = 1 << 3;

      /** Browsing history. */
      public static final long HISTORY = 1 << 4;

      /** Local storage. */
      public static final long LOCAL_STORAGE = 1 << 5;

      /** Saved passwords. */
      public static final long PASSWORDS = 1 << 6;
    }

    /**
     * Gets current settings for the browser's "Clear Data" UI.
     *
     * @return a {@link GeckoResult} that resolves to an instance of {@link Settings} that
     *     represents the current state for the browser's "Clear Data" UI.
     * @see Settings
     */
    @Nullable
    default GeckoResult<Settings> onGetSettings() {
      return null;
    }

    /**
     * Clear form data created after the given timestamp.
     *
     * @param sinceUnixTimestamp timestamp in seconds since the UNIX Epoch.
     * @return a {@link GeckoResult} that resolves when data has been cleared.
     */
    @Nullable
    default GeckoResult<Void> onClearFormData(final long sinceUnixTimestamp) {
      return null;
    }

    /**
     * Clear passwords saved after the given timestamp.
     *
     * @param sinceUnixTimestamp timestamp in seconds since the UNIX Epoch.
     * @return a {@link GeckoResult} that resolves when data has been cleared.
     */
    @Nullable
    default GeckoResult<Void> onClearPasswords(final long sinceUnixTimestamp) {
      return null;
    }

    /**
     * Clear history saved after the given timestamp.
     *
     * @param sinceUnixTimestamp timestamp in seconds since the UNIX Epoch.
     * @return a {@link GeckoResult} that resolves when data has been cleared.
     */
    @Nullable
    default GeckoResult<Void> onClearHistory(final long sinceUnixTimestamp) {
      return null;
    }

    /**
     * Clear downloads created after the given timestamp.
     *
     * @param sinceUnixTimestamp timestamp in seconds since the UNIX Epoch.
     * @return a {@link GeckoResult} that resolves when data has been cleared.
     */
    @Nullable
    default GeckoResult<Void> onClearDownloads(final long sinceUnixTimestamp) {
      return null;
    }
  }

  /** Delegates that responds to messages sent from a WebExtension. */
  @UiThread
  public interface MessageDelegate {
    /**
     * Called whenever the WebExtension sends a message to an app using <code>
     * runtime.sendNativeMessage</code>.
     *
     * @param nativeApp The application identifier of the MessageDelegate that sent this message.
     * @param message The message that was sent, either a primitive type or a {@link
     *     org.json.JSONObject}.
     * @param sender The {@link MessageSender} corresponding to the frame that originated the
     *     message.
     *     <p>Note: all messages are to be considered untrusted and should be checked carefully for
     *     validity.
     * @return A {@link GeckoResult} that resolves with a response to the message.
     */
    @Nullable
    default GeckoResult<Object> onMessage(
        final @NonNull String nativeApp,
        final @NonNull Object message,
        final @NonNull MessageSender sender) {
      return null;
    }

    /**
     * Called whenever the WebExtension connects to an app using <code>runtime.connectNative</code>.
     *
     * @param port {@link Port} instance that can be used to send and receive messages from the
     *     WebExtension. Use {@link Port#sender} to verify the origin of this connection request.
     */
    @Nullable
    default void onConnect(final @NonNull Port port) {}
  }

  /**
   * Delegate that handles communication from a WebExtension on a specific {@link Port} instance.
   */
  @UiThread
  public interface PortDelegate {
    /**
     * Called whenever a message is sent through the corresponding {@link Port} instance.
     *
     * @param message The message that was sent, either a primitive type or a {@link
     *     org.json.JSONObject}.
     * @param port The {@link Port} instance that received this message.
     */
    default void onPortMessage(final @NonNull Object message, final @NonNull Port port) {}

    /**
     * Called whenever the corresponding {@link Port} instance is disconnected or the corresponding
     * {@link GeckoSession} is destroyed. Any message sent from the port after this call will be
     * ignored.
     *
     * @param port The {@link Port} instance that was disconnected.
     */
    @NonNull
    default void onDisconnect(final @NonNull Port port) {}
  }

  /**
   * Port object that can be used for bidirectional communication with a WebExtension.
   *
   * <p>See also: <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/runtime/Port">
   * WebExtensions/API/runtime/Port </a>.
   *
   * @see MessageDelegate#onConnect
   */
  @UiThread
  public static class Port {
    /* package */ final long id;
    /* package */ PortDelegate delegate;
    /* package */ boolean disconnected = false;
    /* package */ final EventDispatcher mEventDispatcher;
    /* package */ boolean mListenersRegistered = false;

    /** {@link MessageSender} corresponding to this port. */
    public @NonNull final MessageSender sender;

    /** The application identifier of the MessageDelegate that opened this port. */
    public @NonNull final String name;

    /** Override for tests. */
    protected Port() {
      this.id = -1;
      this.delegate = null;
      this.sender = null;
      this.name = null;
      mEventDispatcher = null;
    }

    /* package */ Port(final String name, final long id, final MessageSender sender) {
      this.id = id;
      this.delegate = null;
      this.sender = sender;
      this.name = name;
      mEventDispatcher = EventDispatcher.byName("port:" + id);
    }

    private BundleEventListener mEventListener =
        new BundleEventListener() {
          @Override
          public void handleMessage(
              final String event, final GeckoBundle message, final EventCallback callback) {
            if ("GeckoView:WebExtension:Disconnect".equals(event)) {
              disconnectFromExtension(callback);
            } else if ("GeckoView:WebExtension:PortMessage".equals(event)) {
              portMessage(message, callback);
            }
          }
        };

    private void disconnectFromExtension(final EventCallback callback) {
      delegate.onDisconnect(this);
      disconnected();
    }

    private void portMessage(final GeckoBundle bundle, final EventCallback callback) {
      final Object content;
      try {
        content = bundle.toJSONObject().get("data");
      } catch (final JSONException ex) {
        callback.sendError(ex);
        return;
      }

      delegate.onPortMessage(content, this);
    }

    /**
     * Post a message to the WebExtension connected to this {@link Port} instance.
     *
     * @param message {@link JSONObject} that will be sent to the WebExtension.
     */
    public void postMessage(final @NonNull JSONObject message) {
      final GeckoBundle args = new GeckoBundle(1);
      try {
        args.putBundle("message", GeckoBundle.fromJSONObject(message));
      } catch (final JSONException ex) {
        throw new RuntimeException(ex);
      }

      mEventDispatcher.dispatch("GeckoView:WebExtension:PortMessageFromApp", args);
    }

    /** Disconnects this port and notifies the other end. */
    public void disconnect() {
      if (this.disconnected) {
        return;
      }

      final GeckoBundle args = new GeckoBundle(1);
      args.putLong("portId", id);

      mEventDispatcher.dispatch("GeckoView:WebExtension:PortDisconnect", args);
      disconnected();
    }

    private void disconnected() {
      unregisterListeners();
      mEventDispatcher.shutdown();
      this.disconnected = true;
    }

    /**
     * Set a delegate for incoming messages through this {@link Port}.
     *
     * @param delegate Delegate that will receive messages sent through this {@link Port}.
     */
    public void setDelegate(final @Nullable PortDelegate delegate) {
      this.delegate = delegate;

      if (delegate != null) {
        registerListeners();
      } else {
        unregisterListeners();
      }
    }

    private void unregisterListeners() {
      if (!mListenersRegistered) {
        return;
      }

      mEventDispatcher.unregisterUiThreadListener(
          mEventListener,
          "GeckoView:WebExtension:Disconnect",
          "GeckoView:WebExtension:PortMessage");
      mListenersRegistered = false;
    }

    private void registerListeners() {
      if (mListenersRegistered) {
        return;
      }

      mEventDispatcher.registerUiThreadListener(
          mEventListener,
          "GeckoView:WebExtension:Disconnect",
          "GeckoView:WebExtension:PortMessage");
      mListenersRegistered = true;
    }
  }

  /**
   * This delegate is invoked whenever an extension uses the `tabs` WebExtension API to modify the
   * state of a tab. See also <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/tabs">WebExtensions/API/tabs</a>.
   */
  public interface SessionTabDelegate {
    /**
     * Called when tabs.remove is invoked, this method decides if WebExtension can close the tab. In
     * case WebExtension can close the tab, it should close passed GeckoSession and return
     * GeckoResult.ALLOW or GeckoResult.DENY in case tab cannot be closed.
     *
     * <p>See also: <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/tabs/remove">
     * WebExtensions/API/tabs/remove</a>
     *
     * @param source An instance of {@link WebExtension}
     * @param session An instance of {@link GeckoSession} to be closed.
     * @return GeckoResult.ALLOW if the tab will be closed, GeckoResult.DENY otherwise
     */
    @UiThread
    @NonNull
    default GeckoResult<AllowOrDeny> onCloseTab(
        @Nullable final WebExtension source, @NonNull final GeckoSession session) {
      return GeckoResult.deny();
    }

    /**
     * Called when tabs.update is invoked. The uri is provided for informational purposes, there's
     * no need to call <code>loadURI</code> on it. The page will be loaded if this method returns
     * GeckoResult.ALLOW.
     *
     * <p>See also: <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/tabs/update">
     * WebExtensions/API/tabs/update</a>
     *
     * @param extension The extension that requested to update the tab.
     * @param session The {@link GeckoSession} instance that needs to be updated.
     * @param details {@link UpdateTabDetails} instance that describes what needs to be updated for
     *     this tab.
     * @return <code>GeckoResult.ALLOW</code> if the tab will be updated, <code>GeckoResult.DENY
     *     </code> otherwise.
     */
    @UiThread
    @NonNull
    default GeckoResult<AllowOrDeny> onUpdateTab(
        final @NonNull WebExtension extension,
        final @NonNull GeckoSession session,
        final @NonNull UpdateTabDetails details) {
      return GeckoResult.deny();
    }
  }

  /**
   * Provides details about upating a tab with <code>tabs.update</code>.
   *
   * <p>Whenever a field is not passed in by the extension that value will be <code>null</code>.
   *
   * <p>See also: <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/tabs/update">
   * WebExtensions/API/tabs/update </a>.
   */
  public static class UpdateTabDetails {
    /**
     * Whether the tab should become active. If <code>true</code>, non-active highlighted tabs
     * should stop being highlighted. If <code>false</code>, does nothing.
     */
    @Nullable public final Boolean active;

    /** Whether the tab should be discarded automatically by the app when resources are low. */
    @Nullable public final Boolean autoDiscardable;

    /** If <code>true</code> and the tab is not highlighted, it should become active by default. */
    @Nullable public final Boolean highlighted;

    /** Whether the tab should be muted. */
    @Nullable public final Boolean muted;

    /** Whether the tab should be pinned. */
    @Nullable public final Boolean pinned;

    /**
     * The url that the tab will be navigated to. This url is provided just for informational
     * purposes, there is no need to load the URL manually. The corresponding {@link GeckoSession}
     * will be navigated to the right URL after returning <code>GeckoResult.ALLOW</code> from {@link
     * SessionTabDelegate#onUpdateTab}
     */
    @Nullable public final String url;

    /** For testing. */
    protected UpdateTabDetails() {
      active = null;
      autoDiscardable = null;
      highlighted = null;
      muted = null;
      pinned = null;
      url = null;
    }

    /* package */ UpdateTabDetails(final GeckoBundle bundle) {
      active = bundle.getBooleanObject("active");
      autoDiscardable = bundle.getBooleanObject("autoDiscardable");
      highlighted = bundle.getBooleanObject("highlighted");
      muted = bundle.getBooleanObject("muted");
      pinned = bundle.getBooleanObject("pinned");
      url = bundle.getString("url");
    }
  }

  /**
   * Provides details about creating a tab with <code>tabs.create</code>. See also: <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/tabs/create">
   * WebExtensions/API/tabs/create </a>.
   *
   * <p>Whenever a field is not passed in by the extension that value will be <code>null</code>.
   */
  public static class CreateTabDetails {
    /**
     * Whether the tab should become active. If <code>true</code>, non-active highlighted tabs
     * should stop being highlighted. If <code>false</code>, does nothing.
     */
    @Nullable public final Boolean active;

    /**
     * The CookieStoreId used for the tab. This option is only available if the extension has the
     * "cookies" permission.
     */
    @Nullable public final String cookieStoreId;

    /**
     * Whether the tab is created and made visible in the tab bar without any content loaded into
     * memory, a state known as discarded. The tab’s content should be loaded when the tab is
     * activated.
     */
    @Nullable public final Boolean discarded;

    /** The position the tab should take in the window. */
    @Nullable public final Integer index;

    /** If true, open this tab in Reader Mode. */
    @Nullable public final Boolean openInReaderMode;

    /** Whether the tab should be pinned. */
    @Nullable public final Boolean pinned;

    /**
     * The url that the tab will be navigated to. This url is provided just for informational
     * purposes, there is no need to load the URL manually. The corresponding {@link GeckoSession}
     * will be navigated to the right URL after returning <code>GeckoResult.ALLOW</code> from {@link
     * TabDelegate#onNewTab}
     */
    @Nullable public final String url;

    /** For testing. */
    protected CreateTabDetails() {
      active = null;
      cookieStoreId = null;
      discarded = null;
      index = null;
      openInReaderMode = null;
      pinned = null;
      url = null;
    }

    /* package */ CreateTabDetails(final GeckoBundle bundle) {
      active = bundle.getBooleanObject("active");
      cookieStoreId = bundle.getString("cookieStoreId");
      discarded = bundle.getBooleanObject("discarded");
      index = bundle.getInteger("index");
      openInReaderMode = bundle.getBooleanObject("openInReaderMode");
      pinned = bundle.getBooleanObject("pinned");
      url = bundle.getString("url");
    }
  }

  /**
   * This delegate is invoked whenever an extension uses the `tabs` WebExtension API and the request
   * is not specific to an existing tab, e.g. when creating a new tab. See also <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/tabs">WebExtensions/API/tabs</a>.
   */
  public interface TabDelegate {
    /**
     * Called when tabs.create is invoked, this method returns a *newly-created* session that
     * GeckoView will use to load the requested page on. If the returned value is null the page will
     * not be opened.
     *
     * @param source An instance of {@link WebExtension}
     * @param createDetails Information about this tab.
     * @return A {@link GeckoResult} which holds the returned GeckoSession. May be null, in which
     *     case the request for a new tab by the extension will fail. The implementation of onNewTab
     *     is responsible for maintaining a reference to the returned object, to prevent it from
     *     being garbage collected.
     */
    @UiThread
    @Nullable
    default GeckoResult<GeckoSession> onNewTab(
        @NonNull final WebExtension source, @NonNull final CreateTabDetails createDetails) {
      return null;
    }

    /**
     * Called when runtime.openOptionsPage is invoked with options_ui.open_in_tab = false. In this
     * case, GeckoView delegates options page handling to the app. With options_ui.open_in_tab =
     * true, {@link #onNewTab} is called instead.
     *
     * @param source An instance of {@link WebExtension}.
     */
    @UiThread
    default void onOpenOptionsPage(@NonNull final WebExtension source) {}
  }

  /**
   * Get the tab delegate for this extension.
   *
   * <p>See also <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/tabs">WebExtensions/API/tabs</a>.
   *
   * @return The {@link TabDelegate} instance for this extension.
   */
  @UiThread
  @Nullable
  public WebExtension.TabDelegate getTabDelegate() {
    return mDelegateController.getTabDelegate();
  }

  /**
   * Set the tab delegate for this extension. This delegate will be invoked whenever this extension
   * tries to modify the tabs state using the `tabs` WebExtension API.
   *
   * <p>See also <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/tabs">WebExtensions/API/tabs</a>.
   *
   * @param delegate the {@link TabDelegate} instance for this extension.
   */
  @UiThread
  public void setTabDelegate(final @Nullable TabDelegate delegate) {
    mDelegateController.onTabDelegate(delegate);
  }

  /**
   * Returns the delegate handling browsing-data operations for this extension.
   *
   * @return the current BrowsingDataDelegate, or null if none set
   */
  @UiThread
  @Nullable
  public BrowsingDataDelegate getBrowsingDataDelegate() {
    return mDelegateController.getBrowsingDataDelegate();
  }

  /**
   * Sets the delegate to handle browsing-data operations (clear, remove, get settings).
   *
   * @param delegate the BrowsingDataDelegate to receive browsing-data calls; may be null
   */
  @UiThread
  public void setBrowsingDataDelegate(final @Nullable BrowsingDataDelegate delegate) {
    mDelegateController.onBrowsingDataDelegate(delegate);
  }

  private static class Sender {
    public String webExtensionId;
    public String nativeApp;

    public Sender(final String webExtensionId, final String nativeApp) {
      this.webExtensionId = webExtensionId;
      this.nativeApp = nativeApp;
    }

    @Override
    public boolean equals(final Object other) {
      if (!(other instanceof Sender)) {
        return false;
      }

      final Sender o = (Sender) other;
      return webExtensionId.equals(o.webExtensionId) && nativeApp.equals(o.nativeApp);
    }

    @Override
    public int hashCode() {
      return Arrays.hashCode(new Object[] {webExtensionId, nativeApp});
    }
  }

  /** Controller for WebExtension session-related APIs (tabs, browsingData, downloads, etc.). */
  public static class SessionController {
    private final Listener<SessionTabDelegate> mListener;

    /* package */ void setRuntime(final GeckoRuntime runtime) {
      mListener.runtime = runtime;
    }

    /* package */ SessionController(final GeckoSession session) {
      mListener = new Listener<>(session);
    }

    /**
     * Defines a message delegate for a Native App.
     *
     * <p>If a delegate is already present, this delegate will replace the existing one.
     *
     * <p>This message delegate will be responsible for handling messaging between a WebExtension
     * content script or extension page running on the {@link GeckoSession}.
     *
     * <p>Note: To receive messages from content scripts, the WebExtension needs to explicitly allow
     * it in {@link WebExtension#WebExtension} by setting {@link Flags#ALLOW_CONTENT_MESSAGING}.
     *
     * <p>For more information, see <a
     * href="https://firefox-source-docs.mozilla.org/mobile/android/geckoview/consumer/web-extensions.html">
     * Using GeckoView: Interacting with Web content and WebExtensions</a>.
     *
     * @param webExtension {@link WebExtension} that this delegate receives messages from.
     * @param delegate {@link MessageDelegate} that will receive messages from this session.
     * @param nativeApp which native app id this message delegate will handle messaging for.
     * @see WebExtension#setMessageDelegate
     */
    // ThreadConstraint false positive: delegate registration is thread-safe; delivery is dispatched
    // to the UI thread.
    @SuppressLint("ThreadConstraint")
    @AnyThread
    public void setMessageDelegate(
        final @NonNull WebExtension webExtension,
        final @Nullable WebExtension.MessageDelegate delegate,
        final @NonNull String nativeApp) {
      mListener.setMessageDelegate(webExtension, delegate, nativeApp);
    }

    /**
     * Get the message delegate for <code>nativeApp</code>.
     *
     * @param extension {@link WebExtension} that this delegate receives messages from.
     * @param nativeApp identifier for the native app
     * @return The {@link MessageDelegate} attached to the <code>nativeApp</code>. <code>null</code>
     *     if no delegate is present.
     */
    @AnyThread
    public @Nullable WebExtension.MessageDelegate getMessageDelegate(
        final @NonNull WebExtension extension, final @NonNull String nativeApp) {
      return mListener.getMessageDelegate(extension, nativeApp);
    }

    /**
     * Set the Action delegate for this session.
     *
     * <p>This delegate will receive page and browser action overrides specific to this session. The
     * default Action will be received by the delegate set by {@link
     * WebExtension#setActionDelegate}.
     *
     * @param extension the {@link WebExtension} object this delegate will receive updates for
     * @param delegate the {@link ActionDelegate} that will receive updates.
     * @see WebExtension.Action
     */
    @AnyThread
    public void setActionDelegate(
        final @NonNull WebExtension extension, final @Nullable ActionDelegate delegate) {
      mListener.setActionDelegate(extension, delegate);
    }

    /**
     * Get the Action delegate for this session.
     *
     * @param extension {@link WebExtension} that this delegates receive updates for.
     * @return {@link ActionDelegate} for this session
     */
    @AnyThread
    @Nullable
    public ActionDelegate getActionDelegate(final @NonNull WebExtension extension) {
      return mListener.getActionDelegate(extension);
    }

    /**
     * Set the TabDelegate for this session.
     *
     * <p>This delegate will receive messages specific for this session coming from the WebExtension
     * <code>tabs</code> API.
     *
     * @param extension the {@link WebExtension} this delegate will receive updates for
     * @param delegate the {@link TabDelegate} that will receive updates.
     * @see WebExtension#setTabDelegate
     */
    @AnyThread
    public void setTabDelegate(
        final @NonNull WebExtension extension, final @Nullable SessionTabDelegate delegate) {
      mListener.setTabDelegate(extension, delegate);
    }

    /**
     * Get the TabDelegate for the given extension.
     *
     * @param extension the {@link WebExtension} this delegate refers to.
     * @return the current {@link SessionTabDelegate} instance
     */
    @AnyThread
    @Nullable
    public SessionTabDelegate getTabDelegate(final @NonNull WebExtension extension) {
      return mListener.getTabDelegate(extension);
    }
  }

  /* package */ static final class Listener<TabDelegate> implements BundleEventListener {
    private final HashMap<Sender, MessageDelegate> mMessageDelegates;
    private final HashMap<String, ActionDelegate> mActionDelegates;
    private final HashMap<String, BrowsingDataDelegate> mBrowsingDataDelegates;
    private final HashMap<String, TabDelegate> mTabDelegates;
    private final HashMap<String, DownloadDelegate> mDownloadDelegates;

    private final GeckoSession mSession;
    private final EventDispatcher mEventDispatcher;

    private boolean mActionDelegateRegistered = false;
    private boolean mBrowsingDataDelegateRegistered = false;
    private boolean mTabDelegateRegistered = false;

    public GeckoRuntime runtime;

    public Listener(final GeckoRuntime runtime) {
      this(null, runtime);
    }

    public Listener(final GeckoSession session) {
      this(session, null);

      // Close tab event is forwarded to the main listener so we need to listen
      // to it here.
      mEventDispatcher.registerUiThreadListener(
          this,
          "GeckoView:WebExtension:NewTab",
          "GeckoView:WebExtension:UpdateTab",
          "GeckoView:WebExtension:CloseTab",
          "GeckoView:WebExtension:OpenOptionsPage");
      mTabDelegateRegistered = true;
    }

    private Listener(final GeckoSession session, final GeckoRuntime runtime) {
      mMessageDelegates = new HashMap<>();
      mActionDelegates = new HashMap<>();
      mBrowsingDataDelegates = new HashMap<>();
      mTabDelegates = new HashMap<>();
      mDownloadDelegates = new HashMap<>();
      mEventDispatcher =
          session != null ? session.getEventDispatcher() : EventDispatcher.getInstance();
      mSession = session;
      this.runtime = runtime;

      // We queue these messages if the delegate has not been attached yet,
      // so we need to start listening immediately.
      mEventDispatcher.registerUiThreadListener(
          this,
          "GeckoView:WebExtension:Message",
          "GeckoView:WebExtension:PortMessage",
          "GeckoView:WebExtension:Connect",
          "GeckoView:WebExtension:Disconnect",
          "GeckoView:BrowsingData:GetSettings",
          "GeckoView:BrowsingData:Clear",
          "GeckoView:WebExtension:Download");
    }

    public void unregisterWebExtension(final WebExtension extension) {
      mMessageDelegates.remove(extension.id);
      mActionDelegates.remove(extension.id);
      mBrowsingDataDelegates.remove(extension.id);
      mTabDelegates.remove(extension.id);
      mDownloadDelegates.remove(extension.id);
    }

    public void setTabDelegate(final WebExtension webExtension, final TabDelegate delegate) {
      if (!mTabDelegateRegistered && delegate != null) {
        mEventDispatcher.registerUiThreadListener(
            this,
            "GeckoView:WebExtension:NewTab",
            "GeckoView:WebExtension:UpdateTab",
            "GeckoView:WebExtension:CloseTab",
            "GeckoView:WebExtension:OpenOptionsPage");
        mTabDelegateRegistered = true;
      }

      mTabDelegates.put(webExtension.id, delegate);
    }

    public TabDelegate getTabDelegate(final WebExtension webExtension) {
      return mTabDelegates.get(webExtension.id);
    }

    public void setBrowsingDataDelegate(
        final WebExtension webExtension, final BrowsingDataDelegate delegate) {
      mBrowsingDataDelegates.put(webExtension.id, delegate);
    }

    public BrowsingDataDelegate getBrowsingDataDelegate(final WebExtension webExtension) {
      return mBrowsingDataDelegates.get(webExtension.id);
    }

    public void setActionDelegate(
        final WebExtension webExtension, final WebExtension.ActionDelegate delegate) {
      if (!mActionDelegateRegistered && delegate != null) {
        mEventDispatcher.registerUiThreadListener(
            this,
            "GeckoView:BrowserAction:Update",
            "GeckoView:BrowserAction:OpenPopup",
            "GeckoView:PageAction:Update",
            "GeckoView:PageAction:OpenPopup");
        mActionDelegateRegistered = true;
      }

      mActionDelegates.put(webExtension.id, delegate);
    }

    public WebExtension.ActionDelegate getActionDelegate(final WebExtension webExtension) {
      return mActionDelegates.get(webExtension.id);
    }

    public void setMessageDelegate(
        final WebExtension webExtension,
        final WebExtension.MessageDelegate delegate,
        final String nativeApp) {
      mMessageDelegates.put(new Sender(webExtension.id, nativeApp), delegate);

      if (runtime != null && delegate != null) {
        runtime
            .getWebExtensionController()
            .releasePendingMessages(webExtension, nativeApp, mSession);
      }
    }

    public WebExtension.MessageDelegate getMessageDelegate(
        final WebExtension webExtension, final String nativeApp) {
      return mMessageDelegates.get(new Sender(webExtension.id, nativeApp));
    }

    @Override
    public void handleMessage(
        final String event, final GeckoBundle message, final EventCallback callback) {
      if (runtime == null) {
        return;
      }

      runtime.getWebExtensionController().handleMessage(event, message, callback, mSession);
    }

    public void setDownloadDelegate(
        final @NonNull WebExtension extension, final @Nullable DownloadDelegate delegate) {
      mDownloadDelegates.put(extension.id, delegate);
    }

    public WebExtension.DownloadDelegate getDownloadDelegate(final WebExtension extension) {
      return mDownloadDelegates.get(extension.id);
    }
  }

  /**
   * Describes the sender of a message from a WebExtension.
   *
   * <p>See also: <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/runtime/MessageSender">
   * WebExtensions/API/runtime/MessageSender</a>
   */
  @UiThread
  public static class MessageSender {
    /** {@link WebExtension} that sent this message. */
    public final @NonNull WebExtension webExtension;

    /**
     * {@link GeckoSession} that sent this message. <code>null</code> if coming from a background
     * script.
     */
    public final @Nullable GeckoSession session;

    /** Environment type definitions for WebExtension messaging. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef({ENV_TYPE_UNKNOWN, ENV_TYPE_EXTENSION, ENV_TYPE_CONTENT_SCRIPT})
    public @interface EnvType {}

    /* package */ static final int ENV_TYPE_UNKNOWN = 0;

    /** This sender originated inside a privileged extension context like a background script. */
    public static final int ENV_TYPE_EXTENSION = 1;

    /** This sender originated inside a content script. */
    public static final int ENV_TYPE_CONTENT_SCRIPT = 2;

    /**
     * Type of environment that sent this message, either
     *
     * <ul>
     *   <li>{@link MessageSender#ENV_TYPE_EXTENSION} if the message was sent from a background page
     *   <li>{@link MessageSender#ENV_TYPE_CONTENT_SCRIPT} if the message was sent from a content
     *       script
     * </ul>
     */
    // TODO: Bug 1534640 do we need ENV_TYPE_EXTENSION_PAGE ?
    public final @EnvType int environmentType;

    /**
     * URL of the frame that sent this message.
     *
     * <p>Use this value together with {@link MessageSender#isTopLevel} to verify that the message
     * is coming from the expected page. Only top level frames can be trusted.
     */
    public final @NonNull String url;

    /* package */ final boolean isTopLevel;

    /* package */ MessageSender(
        final @NonNull WebExtension webExtension,
        final @Nullable GeckoSession session,
        final @Nullable String url,
        final @EnvType int environmentType,
        final boolean isTopLevel) {
      this.webExtension = webExtension;
      this.session = session;
      this.isTopLevel = isTopLevel;
      this.url = url;
      this.environmentType = environmentType;
    }

    /** Override for testing. */
    protected MessageSender() {
      this.webExtension = null;
      this.session = null;
      this.isTopLevel = false;
      this.url = null;
      this.environmentType = ENV_TYPE_UNKNOWN;
    }

    /**
     * Whether this MessageSender belongs to a top level frame.
     *
     * @return true if the MessageSender was sent from the top level frame, false otherwise.
     */
    public boolean isTopLevel() {
      return this.isTopLevel;
    }
  }

  /* package */ static WebExtension fromBundle(
      final DelegateControllerProvider provider, final GeckoBundle bundle) {
    if (bundle == null) {
      return null;
    }
    return new WebExtension(provider, bundle.getBundle("extension"));
  }

  /**
   * Represents either a Browser Action or a Page Action from the WebExtension API.
   *
   * <p>Instances of this class may represent the default <code>Action</code> which applies to all
   * WebExtension tabs or a tab-specific override. To reconstruct the full <code>Action</code>
   * object, you can use {@link Action#withDefault}.
   *
   * <p>Tab specific overrides can be obtained by registering a delegate using {@link
   * SessionController#setActionDelegate}, while default values can be obtained by registering a
   * delegate using {@link #setActionDelegate}. <br>
   * See also
   *
   * <ul>
   *   <li><a target=_blank
   *       href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browserAction">
   *       WebExtensions/API/browserAction </a>
   *   <li><a target=_blank
   *       href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/pageAction">
   *       WebExtensions/API/pageAction </a>
   * </ul>
   */
  @AnyThread
  public static class Action {
    /**
     * Title of this Action.
     *
     * <p>See also: <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/pageAction/getTitle">
     * pageAction/getTitle</a>, <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browserAction/getTitle">
     * browserAction/getTitle</a>
     */
    public final @Nullable String title;

    /**
     * Icon for this Action.
     *
     * <p>See also: <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/pageAction/setIcon">
     * pageAction/setIcon</a>, <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browserAction/setIcon">
     * browserAction/setIcon</a>
     */
    public final @Nullable Image icon;

    /**
     * Whether this action is enabled and should be visible.
     *
     * <p>Note: for page action, this is <code>true</code> when the extension calls <code>
     * pageAction.show</code> and <code>false</code> when the extension calls <code>pageAction.hide
     * </code>.
     *
     * <p>See also: <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/pageAction/show">
     * pageAction/show</a>, <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browserAction/enabled">
     * browserAction/enabled</a>
     */
    public final @Nullable Boolean enabled;

    /**
     * Badge text for this action.
     *
     * <p>See also: <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browserAction/getBadgeText">
     * browserAction/getBadgeText</a>
     */
    public final @Nullable String badgeText;

    /**
     * Background color for the badge for this Action.
     *
     * <p>This method will return an Android color int that can be used in {@link
     * android.widget.TextView#setBackgroundColor(int)} and similar methods.
     *
     * <p>See also: <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browserAction/getBadgeBackgroundColor">
     * browserAction/getBadgeBackgroundColor</a>
     */
    public final @Nullable Integer badgeBackgroundColor;

    /**
     * Text color for the badge for this Action.
     *
     * <p>This method will return an Android color int that can be used in {@link
     * android.widget.TextView#setTextColor(int)} and similar methods.
     *
     * <p>See also: <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browserAction/getBadgeTextColor">
     * browserAction/getBadgeTextColor</a>
     */
    public final @Nullable Integer badgeTextColor;

    private final WebExtension mExtension;

    /* package */ static final int TYPE_BROWSER_ACTION = 1;
    /* package */ static final int TYPE_PAGE_ACTION = 2;

    /** Action type definitions for WebExtension actions. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef({TYPE_BROWSER_ACTION, TYPE_PAGE_ACTION})
    public @interface ActionType {}

    /* package */ final @ActionType int type;

    /* package */ Action(
        final @ActionType int type, final GeckoBundle bundle, final WebExtension extension) {
      mExtension = extension;

      this.type = type;

      title = bundle.getString("title");
      badgeText = bundle.getString("badgeText");
      badgeBackgroundColor = colorFromRgbaArray(bundle.getDoubleArray("badgeBackgroundColor"));
      badgeTextColor = colorFromRgbaArray(bundle.getDoubleArray("badgeTextColor"));

      if (bundle.containsKey("icon")) {
        icon = Image.fromSizeSrcBundle(bundle.getBundle("icon"));
      } else {
        icon = null;
      }

      if (bundle.getBoolean("patternMatching", false)) {
        // This action was enabled by pattern matching
        enabled = true;
      } else if (bundle.containsKey("enabled")) {
        enabled = bundle.getBoolean("enabled");
      } else {
        enabled = null;
      }
    }

    private Integer colorFromRgbaArray(final double[] c) {
      if (c == null) {
        return null;
      }

      return Color.argb((int) c[3], (int) c[0], (int) c[1], (int) c[2]);
    }

    @Override
    public String toString() {
      return "Action {\n"
          + "\ttitle: "
          + this.title
          + ",\n"
          + "\ticon: "
          + this.icon
          + ",\n"
          + "\tenabled: "
          + this.enabled
          + ",\n"
          + "\tbadgeText: "
          + this.badgeText
          + ",\n"
          + "\tbadgeTextColor: "
          + this.badgeTextColor
          + ",\n"
          + "\tbadgeBackgroundColor: "
          + this.badgeBackgroundColor
          + ",\n"
          + "}";
    }

    /** Create a default Action for testing purposes. */
    protected Action() {
      type = TYPE_BROWSER_ACTION;
      mExtension = null;
      title = null;
      icon = null;
      enabled = null;
      badgeText = null;
      badgeTextColor = null;
      badgeBackgroundColor = null;
    }

    /**
     * Merges values from this Action with the default Action.
     *
     * @param defaultValue the default Action as received from {@link
     *     ActionDelegate#onBrowserAction} or {@link ActionDelegate#onPageAction}.
     * @return an {@link Action} where all <code>null</code> values from this instance are replaced
     *     with values from <code>defaultValue</code>.
     * @throws IllegalArgumentException if defaultValue is not of the same type, e.g. if this Action
     *     is a Page Action and default value is a Browser Action.
     */
    @NonNull
    public Action withDefault(final @NonNull Action defaultValue) {
      return new Action(this, defaultValue);
    }

    /**
     * @see Action#withDefault
     */
    private Action(final Action source, final Action defaultValue) {
      if (source.type != defaultValue.type) {
        throw new IllegalArgumentException("defaultValue must be of the same type.");
      }

      type = source.type;
      mExtension = source.mExtension;

      title = source.title != null ? source.title : defaultValue.title;
      icon = source.icon != null ? source.icon : defaultValue.icon;
      enabled = source.enabled != null ? source.enabled : defaultValue.enabled;
      badgeText = source.badgeText != null ? source.badgeText : defaultValue.badgeText;
      badgeTextColor =
          source.badgeTextColor != null ? source.badgeTextColor : defaultValue.badgeTextColor;
      badgeBackgroundColor =
          source.badgeBackgroundColor != null
              ? source.badgeBackgroundColor
              : defaultValue.badgeBackgroundColor;
    }

    /** Notifies the extension that the user has clicked on this Action. */
    @UiThread
    public void click() {
      final GeckoBundle bundle = new GeckoBundle(1);
      bundle.putString("extensionId", mExtension.id);

      // The click event will return the popup uri if we should open a popup in
      // response to clicking on the action button.
      final GeckoResult<String> popupUri;
      if (type == TYPE_BROWSER_ACTION) {
        popupUri =
            EventDispatcher.getInstance().queryString("GeckoView:BrowserAction:Click", bundle);
      } else if (type == TYPE_PAGE_ACTION) {
        popupUri = EventDispatcher.getInstance().queryString("GeckoView:PageAction:Click", bundle);
      } else {
        throw new IllegalStateException("Unknown Action type");
      }

      popupUri.accept(
          uri -> {
            if (uri == null || uri.isEmpty()) {
              return;
            }

            final ActionDelegate delegate = mExtension.mDelegateController.getActionDelegate();
            if (delegate == null) {
              return;
            }

            // The .accept method will be called from the UIThread in this case because
            // the GeckoResult instance was created on the UIThread
            @SuppressLint("WrongThread")
            final GeckoResult<GeckoSession> popup = delegate.onTogglePopup(mExtension, this);
            openPopup(popup, uri);
          });
    }

    /* package */ void openPopup(final GeckoResult<GeckoSession> popup, final String popupUri) {
      if (popup == null) {
        return;
      }

      popup.accept(
          session -> {
            if (session == null) {
              return;
            }

            session.getSettings().setIsExtensionPopup(true);
            session.loadUri(popupUri);
          });
    }
  }

  /**
   * Receives updates whenever a Browser action or a Page action has been defined by an extension.
   *
   * <p>This delegate will receive the default action when registered with {@link
   * WebExtension#setActionDelegate}. To receive {@link GeckoSession}-specific overrides you can use
   * {@link SessionController#setActionDelegate}.
   */
  public interface ActionDelegate {
    /**
     * Called whenever a browser action is defined or updated.
     *
     * <p>This method will be called whenever an extension that defines a browser action is
     * registered or the properties of the Action are updated.
     *
     * <p>See also <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browserAction">
     * WebExtensions/API/browserAction </a>, <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/browser_action">
     * WebExtensions/manifest.json/browser_action </a>.
     *
     * @param extension The extension that defined this browser action.
     * @param session Either the {@link GeckoSession} corresponding to the tab to which this Action
     *     override applies. <code>null</code> if <code>action</code> is the new default value.
     * @param action {@link Action} containing the override values for this {@link GeckoSession} or
     *     the default value if <code>session</code> is <code>null</code>.
     */
    @UiThread
    default void onBrowserAction(
        final @NonNull WebExtension extension,
        final @Nullable GeckoSession session,
        final @NonNull Action action) {}

    /**
     * Called whenever a page action is defined or updated.
     *
     * <p>This method will be called whenever an extension that defines a page action is registered
     * or the properties of the Action are updated.
     *
     * <p>See also <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/pageAction">
     * WebExtensions/API/pageAction </a>, <a target=_blank
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/page_action">
     * WebExtensions/manifest.json/page_action </a>.
     *
     * @param extension The extension that defined this page action.
     * @param session Either the {@link GeckoSession} corresponding to the tab to which this Action
     *     override applies. <code>null</code> if <code>action</code> is the new default value.
     * @param action {@link Action} containing the override values for this {@link GeckoSession} or
     *     the default value if <code>session</code> is <code>null</code>.
     */
    @UiThread
    default void onPageAction(
        final @NonNull WebExtension extension,
        final @Nullable GeckoSession session,
        final @NonNull Action action) {}

    /**
     * Called whenever the action wants to toggle a popup view.
     *
     * @param extension The extension that wants to display a popup
     * @param action The action where the popup is defined
     * @return A GeckoSession that will be used to display the pop-up, null if no popup will be
     *     displayed.
     */
    @UiThread
    @Nullable
    default GeckoResult<GeckoSession> onTogglePopup(
        final @NonNull WebExtension extension, final @NonNull Action action) {
      return null;
    }

    /**
     * Called whenever the action wants to open a popup view.
     *
     * @param extension The extension that wants to display a popup
     * @param action The action where the popup is defined
     * @return A GeckoSession that will be used to display the pop-up, null if no popup will be
     *     displayed.
     */
    @UiThread
    @Nullable
    default GeckoResult<GeckoSession> onOpenPopup(
        final @NonNull WebExtension extension, final @NonNull Action action) {
      return null;
    }
  }

  /**
   * Exception thrown when GeckoView receives unexpected invalid WebExtension metadata from Gecko
   * (e.g. when metadata properties in the bundle are not matching the type expected by GeckoView).
   */
  public static class InvalidMetaDataException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    /** For testing */
    protected InvalidMetaDataException() {
      super("InvalidMetaDataException");
    }

    /* package */ InvalidMetaDataException(
        final @Nullable Throwable cause, final @Nullable String extensionId) {
      super(
          "InvalidMetaDataException" + (extensionId != null ? " for add-on id " + extensionId : ""),
          cause);
    }
  }

  /** Extension thrown when an error occurs during extension installation. */
  public static class InstallException extends Exception {
    private static final long serialVersionUID = 1L;

    /** Error code definitions for extension installation failures. */
    public static class ErrorCodes {
      /** The download failed due to network problems. */
      public static final int ERROR_NETWORK_FAILURE = -1;

      /** The downloaded file did not match the provided hash. */
      public static final int ERROR_INCORRECT_HASH = -2;

      /** The downloaded file seems to be corrupted in some way. */
      public static final int ERROR_CORRUPT_FILE = -3;

      /** An error occurred trying to write to the filesystem. */
      public static final int ERROR_FILE_ACCESS = -4;

      /** The extension must be signed and isn't. */
      public static final int ERROR_SIGNEDSTATE_REQUIRED = -5;

      /** The downloaded extension had a different type than expected. */
      public static final int ERROR_UNEXPECTED_ADDON_TYPE = -6;

      /** The downloaded extension had a different version than expected. */
      public static final int ERROR_UNEXPECTED_ADDON_VERSION = -9;

      /** The extension did not have the expected ID. */
      public static final int ERROR_INCORRECT_ID = -7;

      /** The extension did not have the expected ID. */
      public static final int ERROR_INVALID_DOMAIN = -8;

      /** The extension is (hard) blocked. */
      public static final int ERROR_BLOCKLISTED = -10;

      /** The extension is incompatible. */
      public static final int ERROR_INCOMPATIBLE = -11;

      /** The extension type is not supported by the platform. */
      public static final int ERROR_UNSUPPORTED_ADDON_TYPE = -12;

      /** The extension can only be installed via Enterprise Policies. */
      public static final int ERROR_ADMIN_INSTALL_ONLY = -13;

      /** The extension is soft-blocked. */
      public static final int ERROR_SOFT_BLOCKED = -14;

      /** The extension install was canceled. */
      public static final int ERROR_USER_CANCELED = -100;

      /** The extension install was postponed until restart. */
      public static final int ERROR_POSTPONED = -101;

      /** For testing. */
      protected ErrorCodes() {}
    }

    /** These states should match gecko's AddonManager.STATE_* constants. */
    private static class StateCodes {
      public static final int STATE_POSTPONED = 7;
      public static final int STATE_CANCELED = 12;
    }

    /* package */ static Throwable fromQueryException(final Throwable exception) {
      final EventDispatcher.QueryException queryException =
          (EventDispatcher.QueryException) exception;
      final Object response = queryException.data;
      if (response instanceof GeckoBundle && ((GeckoBundle) response).containsKey("installError")) {
        final GeckoBundle bundle = (GeckoBundle) response;
        int errorCode = bundle.getInt("installError");
        final int installState = bundle.getInt("state");
        if (errorCode == 0
            && installState == StateCodes.STATE_CANCELED
            && bundle.getBoolean("cancelledByUser")) {
          errorCode = ErrorCodes.ERROR_USER_CANCELED;
        } else if (errorCode == 0 && installState == StateCodes.STATE_POSTPONED) {
          errorCode = ErrorCodes.ERROR_POSTPONED;
        }
        return new WebExtension.InstallException(errorCode);
      } else {
        return new Exception(response.toString());
      }
    }

    /** Error code type definitions for WebExtension installation exceptions. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef(
        value = {
          ErrorCodes.ERROR_NETWORK_FAILURE,
          ErrorCodes.ERROR_INCORRECT_HASH,
          ErrorCodes.ERROR_CORRUPT_FILE,
          ErrorCodes.ERROR_FILE_ACCESS,
          ErrorCodes.ERROR_SIGNEDSTATE_REQUIRED,
          ErrorCodes.ERROR_UNEXPECTED_ADDON_TYPE,
          ErrorCodes.ERROR_UNEXPECTED_ADDON_VERSION,
          ErrorCodes.ERROR_INCORRECT_ID,
          ErrorCodes.ERROR_INVALID_DOMAIN,
          ErrorCodes.ERROR_BLOCKLISTED,
          ErrorCodes.ERROR_INCOMPATIBLE,
          ErrorCodes.ERROR_USER_CANCELED,
          ErrorCodes.ERROR_POSTPONED,
          ErrorCodes.ERROR_UNSUPPORTED_ADDON_TYPE,
          ErrorCodes.ERROR_ADMIN_INSTALL_ONLY,
          ErrorCodes.ERROR_SOFT_BLOCKED,
        })
    public @interface Codes {}

    /** One of {@link ErrorCodes} that provides more information about this exception. */
    public final @Codes int code;

    /** An optional ID of the extension that caused the exception. */
    public final @Nullable String extensionId;

    /** An optional name of the extension that caused the exception. */
    public final @Nullable String extensionName;

    /** An optional version of the extension that caused the exception. */
    public final @Nullable String extensionVersion;

    /** For testing */
    protected InstallException() {
      this.code = ErrorCodes.ERROR_NETWORK_FAILURE;
      this.extensionId = null;
      this.extensionName = null;
      this.extensionVersion = null;
    }

    @Override
    public String toString() {
      return "InstallException: " + code;
    }

    /* package */ InstallException(
        final @Codes int code,
        final @Nullable String extensionId,
        final @Nullable String extensionName,
        final @Nullable String extensionVersion) {
      this.code = code;
      this.extensionId = extensionId;
      this.extensionName = extensionName;
      this.extensionVersion = extensionVersion;
    }

    /* package */ InstallException(
        final @Codes int code,
        final @Nullable String extensionId,
        final @Nullable String extensionName) {
      this.code = code;
      this.extensionId = extensionId;
      this.extensionName = extensionName;
      this.extensionVersion = null;
    }

    /* package */ InstallException(final @Codes int code, final @Nullable String extensionName) {
      this.code = code;
      this.extensionId = null;
      this.extensionName = extensionName;
      this.extensionVersion = null;
    }

    /* package */ InstallException(final @Codes int code) {
      this.code = code;
      this.extensionId = null;
      this.extensionName = null;
      this.extensionVersion = null;
    }
  }

  /**
   * Set the Action delegate for this WebExtension.
   *
   * <p>This delegate will receive updates every time the default Action value changes.
   *
   * <p>To listen for {@link GeckoSession}-specific updates, use {@link
   * SessionController#setActionDelegate}
   *
   * @param delegate {@link ActionDelegate} that will receive updates.
   */
  @AnyThread
  public void setActionDelegate(final @Nullable ActionDelegate delegate) {
    mDelegateController.onActionDelegate(delegate);

    final GeckoBundle bundle = new GeckoBundle(1);
    bundle.putString("extensionId", id);

    if (delegate != null) {
      EventDispatcher.getInstance().dispatch("GeckoView:ActionDelegate:Attached", bundle);
    }
  }

  /**
   * Describes the signed status for a {@link WebExtension}.
   *
   * <p>See <a href="https://support.mozilla.org/en-US/kb/add-on-signing-in-firefox">Add-on signing
   * in Firefox. </a>
   */
  public static class SignedStateFlags {
    /** Public constructor for SignedStateFlags. */
    public SignedStateFlags() {}

    // Keep in sync with AddonManager.sys.mjs
    /**
     * This extension may be signed but by a certificate that doesn't chain to our our trusted
     * certificate.
     */
    public static final int UNKNOWN = -1;

    /** This extension is unsigned. */
    public static final int MISSING = 0;

    /** This extension has been preliminarily reviewed. */
    public static final int PRELIMINARY = 1;

    /** This extension has been fully reviewed. */
    public static final int SIGNED = 2;

    /** This extension is a system add-on. */
    public static final int SYSTEM = 3;

    /** This extension is signed with a "Mozilla Extensions" certificate. */
    public static final int PRIVILEGED = 4;

    /* package */ static final int LAST = PRIVILEGED;
  }

  /** Signed state type definitions for WebExtension verification status. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({
    SignedStateFlags.UNKNOWN,
    SignedStateFlags.MISSING,
    SignedStateFlags.PRELIMINARY,
    SignedStateFlags.SIGNED,
    SignedStateFlags.SYSTEM,
    SignedStateFlags.PRIVILEGED
  })
  public @interface SignedState {}

  /**
   * Describes the blocklist state for a {@link WebExtension}. See <a
   * href="https://support.mozilla.org/en-US/kb/add-ons-cause-issues-are-on-blocklist">Add-ons that
   * cause stability or security issues are put on a blocklist </a>.
   */
  public static class BlocklistStateFlags {
    /** Public constructor for BlocklistStateFlags. */
    public BlocklistStateFlags() {}

    // Keep in sync with nsIBlocklistService.idl
    /** This extension does not appear in the blocklist. */
    public static final int NOT_BLOCKED = 0;

    /**
     * This extension is in the blocklist but the problem is not severe enough to warant forcibly
     * blocking.
     */
    public static final int SOFTBLOCKED = 1;

    /** This extension should be blocked and never used. */
    public static final int BLOCKED = 2;

    /** This extension is considered outdated, and there is a known update available. */
    public static final int OUTDATED = 3;

    /** This extension is vulnerable and there is an update. */
    public static final int VULNERABLE_UPDATE_AVAILABLE = 4;

    /** This extension is vulnerable and there is no update. */
    public static final int VULNERABLE_NO_UPDATE = 5;
  }

  /** Blocklist state definitions for extensions. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({
    BlocklistStateFlags.NOT_BLOCKED,
    BlocklistStateFlags.SOFTBLOCKED,
    BlocklistStateFlags.BLOCKED,
    BlocklistStateFlags.OUTDATED,
    BlocklistStateFlags.VULNERABLE_UPDATE_AVAILABLE,
    BlocklistStateFlags.VULNERABLE_NO_UPDATE
  })
  public @interface BlocklistState {}

  /** Flags indicating reasons why an extension is disabled. */
  public static class DisabledFlags {
    /** Public constructor for DisabledFlags. */
    public DisabledFlags() {}

    /** The extension has been disabled by the user */
    public static final int USER = 1 << 1;

    /**
     * The extension has been disabled by the blocklist. The details of why this extension was
     * blocked can be found in {@link MetaData#blocklistState}.
     */
    public static final int BLOCKLIST = 1 << 2;

    /**
     * The extension has been disabled by the application. To enable the extension you can use
     * {@link WebExtensionController#enable} passing in {@link
     * WebExtensionController.EnableSource#APP} as <code>source</code>.
     */
    public static final int APP = 1 << 3;

    /** The extension has been disabled because it is not correctly signed. */
    public static final int SIGNATURE = 1 << 4;

    /**
     * The extension has been disabled because it is not compatible with the application version.
     */
    public static final int APP_VERSION = 1 << 5;

    /**
     * The extension has been disabled by the soft-blocklist. The details of why this extension was
     * blocked can be found in {@link MetaData#blocklistState}.
     */
    public static final int SOFT_BLOCKLIST = 1 << 6;
  }

  /** Enabled/disabled flag definitions for WebExtensions. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef(
      flag = true,
      value = {
        DisabledFlags.USER,
        DisabledFlags.BLOCKLIST,
        DisabledFlags.APP,
        DisabledFlags.SIGNATURE,
        DisabledFlags.APP_VERSION,
        DisabledFlags.SOFT_BLOCKLIST,
      })
  public @interface EnabledFlags {}

  /** Provides information about a {@link WebExtension}. */
  public class MetaData {
    /**
     * Main {@link Image} branding for this {@link WebExtension}. Can be used when displaying
     * prompts.
     */
    public final @NonNull Image icon;

    /**
     * Required permissions for this extension.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/permissions#API_permissions">
     * API permissions </a>.
     */
    public final @NonNull String[] requiredPermissions;

    /**
     * Required origin permissions for this extension.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/permissions#Host_permissions">
     * Host permissions </a>.
     */
    public final @NonNull String[] requiredOrigins;

    /** Required data collection permissions for this extension. */
    public final @NonNull String[] requiredDataCollectionPermissions;

    /**
     * Optional permissions for this extension.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/permissions#API_permissions">
     * API permissions </a>.
     */
    public final @NonNull String[] optionalPermissions;

    /**
     * Granted optional permissions for this extension.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/permissions#API_permissions">
     * API permissions </a>.
     */
    public final @NonNull String[] grantedOptionalPermissions;

    /**
     * Optional origin permissions for this extension.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/permissions#Host_permissions">
     * Host permissions </a>.
     */
    public final @NonNull String[] optionalOrigins;

    /**
     * Granted optional origin permissions for this extension.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/permissions#Host_permissions">
     * Host permissions </a>.
     */
    public final @NonNull String[] grantedOptionalOrigins;

    /** Optional data collection permissions for this extension. */
    public final @NonNull String[] optionalDataCollectionPermissions;

    /** Granted optional data collection permissions for this extension. */
    public final @NonNull String[] grantedOptionalDataCollectionPermissions;

    /**
     * Branding name for this extension.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/name">
     * manifest.json/name </a>
     */
    public final @Nullable String name;

    /**
     * Branding description for this extension. This string will be localized using the current
     * GeckoView language setting.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/description">
     * manifest.json/description </a>
     */
    public final @Nullable String description;

    /** The full description of this extension. See: `AddonWrapper.fullDescription`. */
    public final @Nullable String fullDescription;

    /** The average rating of this extension. See: `AddonWrapper.averageRating`. */
    public final double averageRating;

    /** The review count for this extension. See: `AddonWrapper.reviewCount`. */
    public final int reviewCount;

    /** The link to the review page for this extension. See `AddonWrapper.reviewURL`. */
    public final @Nullable String reviewUrl;

    /**
     * The string representation of the date that this extension was most recently updated
     * (simplified ISO 8601 format). See `AddonWrapper.updateDate`.
     */
    public final @Nullable String updateDate;

    /** The URL used to install this extension. See: `AddonInternal.sourceURI`. */
    public final @Nullable String downloadUrl;

    /**
     * Version string for this extension.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/version">
     * manifest.json/version </a>
     */
    public final @NonNull String version;

    /**
     * Creator name as provided in the manifest.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/developer">
     * manifest.json/developer </a>
     */
    public final @Nullable String creatorName;

    /**
     * Creator url as provided in the manifest.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/developer">
     * manifest.json/developer </a>
     */
    public final @Nullable String creatorUrl;

    /**
     * Homepage url as provided in the manifest.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/homepage_url">
     * manifest.json/homepage_url </a>
     */
    public final @Nullable String homepageUrl;

    /**
     * Options page as provided in the manifest.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/options_ui">
     * manifest.json/options_ui </a>
     */
    public final @Nullable String optionsPageUrl;

    /**
     * Whether the options page should be open in a Tab or not.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/options_ui#Syntax">
     * manifest.json/options_ui#Syntax </a>
     */
    public final boolean openOptionsPageInTab;

    /**
     * Whether or not this is a recommended extension.
     *
     * <p>See <a href="https://blog.mozilla.org/firefox/firefox-recommended-extensions/">Recommended
     * Extensions program </a>
     */
    public final boolean isRecommended;

    /**
     * Blocklist status for this extension.
     *
     * <p>See <a href="https://support.mozilla.org/en-US/kb/add-ons-cause-issues-are-on-blocklist">
     * Add-ons that cause stability or security issues are put on a blocklist </a>.
     */
    public final @BlocklistState int blocklistState;

    /**
     * Signed status for this extension.
     *
     * <p>See <a href="https://support.mozilla.org/en-US/kb/add-on-signing-in-firefox">Add-on
     * signing in Firefox. </a>.
     */
    public final @SignedState int signedState;

    /**
     * Disabled binary flags for this extension.
     *
     * <p>This will be either equal to <code>0</code> if the extension is enabled or will contain
     * one or more flags from {@link DisabledFlags}.
     *
     * <p>e.g. if the extension has been disabled by the user, the value in {@link
     * DisabledFlags#USER} will be equal to <code>1</code>:
     *
     * <pre><code>
     *     boolean isUserDisabled = metaData.disabledFlags
     *          &amp; DisabledFlags.USER &gt; 0;
     * </code></pre>
     */
    public final @EnabledFlags int disabledFlags;

    /**
     * Root URL for this extension's pages. Can be used to determine if a given URL belongs to this
     * extension.
     */
    public final @NonNull String baseUrl;

    /**
     * Whether this extension is allowed to run in private browsing or not. To modify this value use
     * {@link WebExtensionController#setAllowedInPrivateBrowsing}.
     */
    public final boolean allowedInPrivateBrowsing;

    /** Whether this extension is enabled or not. */
    public final boolean enabled;

    /**
     * Whether this extension is temporary or not. Temporary extensions are not retained and will be
     * uninstalled when the browser exits.
     */
    public final boolean temporary;

    /** The link to the AMO detail page for this extension. See `AddonWrapper.amoListingURL`. */
    public final @Nullable String amoListingUrl;

    /**
     * Indicates how the extension works with private browsing windows.
     *
     * <p>See <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/incognito">
     * manifest.json/incognito </a>
     */
    public final @Nullable String incognito;

    /** Override for testing. */
    protected MetaData() {
      icon = null;
      requiredPermissions = null;
      requiredOrigins = null;
      requiredDataCollectionPermissions = null;
      optionalPermissions = null;
      optionalOrigins = null;
      optionalDataCollectionPermissions = null;
      grantedOptionalPermissions = null;
      grantedOptionalOrigins = null;
      grantedOptionalDataCollectionPermissions = null;
      name = null;
      description = null;
      version = null;
      creatorName = null;
      creatorUrl = null;
      homepageUrl = null;
      optionsPageUrl = null;
      openOptionsPageInTab = false;
      isRecommended = false;
      blocklistState = BlocklistStateFlags.NOT_BLOCKED;
      signedState = SignedStateFlags.UNKNOWN;
      disabledFlags = 0;
      enabled = true;
      temporary = false;
      baseUrl = null;
      allowedInPrivateBrowsing = false;
      fullDescription = null;
      averageRating = 0;
      reviewCount = 0;
      reviewUrl = null;
      updateDate = null;
      downloadUrl = null;
      amoListingUrl = null;
      incognito = null;
    }

    /* package */ MetaData(final GeckoBundle bundle) {
      requiredPermissions = bundle.getStringArray("requiredPermissions");
      requiredOrigins = bundle.getStringArray("requiredOrigins");
      requiredDataCollectionPermissions =
          bundle.getStringArray("requiredDataCollectionPermissions");
      optionalPermissions = bundle.getStringArray("optionalPermissions");
      optionalOrigins = bundle.getStringArray("optionalOrigins");
      optionalDataCollectionPermissions =
          bundle.getStringArray("optionalDataCollectionPermissions");
      grantedOptionalPermissions = bundle.getStringArray("grantedOptionalPermissions");
      grantedOptionalOrigins = bundle.getStringArray("grantedOptionalOrigins");
      grantedOptionalDataCollectionPermissions =
          bundle.getStringArray("grantedOptionalDataCollectionPermissions");
      description = bundle.getString("description");
      version = bundle.getString("version");
      creatorName = bundle.getString("creatorName");
      creatorUrl = bundle.getString("creatorURL");
      homepageUrl = bundle.getString("homepageURL");
      name = bundle.getString("name");
      optionsPageUrl = bundle.getString("optionsPageURL");
      openOptionsPageInTab = bundle.getBoolean("openOptionsPageInTab");
      isRecommended = bundle.getBoolean("isRecommended");
      blocklistState = bundle.getInt("blocklistState", BlocklistStateFlags.NOT_BLOCKED);
      enabled = bundle.getBoolean("enabled", false);
      temporary = bundle.getBoolean("temporary", false);
      baseUrl = bundle.getString("baseURL");
      allowedInPrivateBrowsing = bundle.getBoolean("privateBrowsingAllowed", false);
      fullDescription = bundle.getString("fullDescription");
      averageRating = bundle.getDouble("averageRating");
      reviewCount = bundle.getInt("reviewCount");
      reviewUrl = bundle.getString("reviewURL");
      updateDate = bundle.getString("updateDate");
      downloadUrl = bundle.getString("downloadUrl");
      amoListingUrl = bundle.getString("amoListingURL");
      incognito = bundle.getString("incognito");

      final int signedState = bundle.getInt("signedState", SignedStateFlags.UNKNOWN);
      if (signedState <= SignedStateFlags.LAST) {
        this.signedState = signedState;
      } else {
        Log.e(LOGTAG, "Unrecognized signed state: " + signedState);
        this.signedState = SignedStateFlags.UNKNOWN;
      }

      int disabledFlags = 0;
      final String[] disabledFlagsString = bundle.getStringArray("disabledFlags");

      for (final String flag : disabledFlagsString) {
        if (flag.equals("userDisabled")) {
          disabledFlags |= DisabledFlags.USER;
        } else if (flag.equals("blocklistDisabled")) {
          disabledFlags |= DisabledFlags.BLOCKLIST;
        } else if (flag.equals("softBlocklistDisabled")) {
          disabledFlags |= DisabledFlags.SOFT_BLOCKLIST;
        } else if (flag.equals("appDisabled")) {
          disabledFlags |= DisabledFlags.APP;
        } else if (flag.equals("signatureDisabled")) {
          disabledFlags |= DisabledFlags.SIGNATURE;
        } else if (flag.equals("appVersionDisabled")) {
          disabledFlags |= DisabledFlags.APP_VERSION;
        } else {
          Log.e(LOGTAG, "Unrecognized disabledFlag state: " + flag);
        }
      }
      this.disabledFlags = disabledFlags;

      if (bundle.containsKey("icons")) {
        icon = Image.fromSizeSrcBundle(bundle.getBundle("icons"));
      } else {
        icon = null;
      }
    }
  }

  // TODO: make public bug 1595822

  /** Context flag definitions for menu items. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef(
      flag = true,
      value = {
        Context.NONE,
        Context.BOOKMARK,
        Context.BROWSER_ACTION,
        Context.PAGE_ACTION,
        Context.TAB,
        Context.TOOLS_MENU
      })
  public @interface ContextFlags {}

  /**
   * Flags to determine which contexts a menu item should be shown in. See <a
   * href=https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/menus/ContextType>
   * menus.ContextType</a>.
   */
  static class Context {
    /** Shows the menu item in no contexts. */
    static final int NONE = 0;

    /**
     * Shows the menu item when the user context-clicks an item on the bookmarks toolbar, bookmarks
     * menu, bookmarks sidebar, or Library window.
     */
    static final int BOOKMARK = 1 << 1;

    /** Shows the menu item when the user context-clicks the extension's browser action. */
    static final int BROWSER_ACTION = 1 << 2;

    /** Shows the menu item when the user context-clicks on the extension's page action. */
    static final int PAGE_ACTION = 1 << 3;

    /** Shows when the user context-clicks on a tab (such as the element on the tab bar.) */
    static final int TAB = 1 << 4;

    /** Adds the item to the browser's tools menu. */
    static final int TOOLS_MENU = 1 << 5;
  }

  // TODO: make public bug 1595822

  /**
   * Represents an addition to the context menu by an extension.
   *
   * <p>In the <a
   * href=https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/menus>menus</a>
   * API, all elements added by one extension should be collapsed under one header. This class
   * represents all of one extension's menu items, as well as the icon that should be used with that
   * header.
   */
  static class Menu {
    /** List of menu items that belong to this extension. */
    final @NonNull List<MenuItem> items;

    /** Icon for this extension. */
    final @Nullable Image icon;

    /** Title for the menu header. */
    final @Nullable String title;

    /** The extension adding this Menu to the context menu. */
    final @NonNull WebExtension extension;

    /* package */ Menu(final @NonNull WebExtension extension, final GeckoBundle bundle) {
      this.extension = extension;
      title = bundle.getString("title", "");
      final GeckoBundle[] items = bundle.getBundleArray("items");
      this.items = new ArrayList<>();
      if (items != null) {
        for (final GeckoBundle item : items) {
          this.items.add(new MenuItem(this.extension, item));
        }
      }

      if (bundle.containsKey("icon")) {
        icon = Image.fromSizeSrcBundle(bundle.getBundle("icon"));
      } else {
        icon = null;
      }
    }

    /** Notifies the extension that a user has opened the context menu. */
    void show() {
      final GeckoBundle bundle = new GeckoBundle(1);
      bundle.putString("extensionId", extension.id);

      EventDispatcher.getInstance().dispatch("GeckoView:WebExtension:MenuShow", bundle);
    }

    /** Notifies the extension that a user has hidden the context menu. */
    void hide() {
      final GeckoBundle bundle = new GeckoBundle(1);
      bundle.putString("extensionId", extension.id);

      EventDispatcher.getInstance().dispatch("GeckoView:WebExtension:MenuHide", bundle);
    }
  }

  // TODO: make public bug 1595822
  /**
   * Represents an item in the menu.
   *
   * <p>If there is only one menu item in the list, the embedder should display that item as itself,
   * not under a header.
   */
  static class MenuItem {

    /** Menu item type definitions. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef(
        flag = false,
        value = {MenuType.NORMAL, MenuType.CHECKBOX, MenuType.RADIO, MenuType.SEPARATOR})
    public @interface Type {}

    /** A set of constants that represents the display type of this menu item. */
    static class MenuType {
      /**
       * This represents a menu item that just displays a label.
       *
       * <p>See <a
       * href=https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/menus/ItemType>
       * menus.ItemType.normal</a>
       */
      static final int NORMAL = 0;

      /**
       * This represents a menu item that can be selected and deselected.
       *
       * <p>See <a
       * href=https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/menus/ItemType>
       * menus.ItemType.checkbox</a>
       */
      static final int CHECKBOX = 1;

      /**
       * This represents a menu item that is one of a group of choices. All menu items for an
       * extension that are of type radio are part of one radio group.
       *
       * <p>See <a
       * href=https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/menus/ItemType>
       * menus.ItemType.radio</a>
       */
      static final int RADIO = 2;

      /**
       * This represents a line separating elements.
       *
       * <p>See <a
       * href=https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/menus/ItemType>
       * menus.ItemType.separator</a>
       */
      static final int SEPARATOR = 3;
    }

    /**
     * Direct children for this menu item. These should be displayed as a sub-menu.
     *
     * <p>See <a
     * href=https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/menus/create>
     * createProperties.parentId</a>
     */
    final @Nullable List<MenuItem> children;

    /** One of the {@link Type} constants. Determines the type of the action. */
    final @Type int type;

    /**
     * The id of this menu item. See <a
     * href=https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/menus/create>
     * createProperties.id</a>
     */
    final @Nullable String id;

    /** Determines if the menu item should be currently displayed. */
    final boolean visible;

    /** The title to be displayed for this menu item. */
    final @Nullable String title;

    /** Whether or not the menu item is initially checked. Defaults to false. */
    final boolean checked;

    /** Contexts that this menu item should be shown in. */
    final @ContextFlags int contexts;

    /** Icon for this menu item. */
    final @Nullable Image icon;

    final WebExtension mExtension;

    /**
     * Creates a new menu item using a bundle and a reference to the extension that this item
     * belongs to.
     *
     * @param extension WebExtension object.
     * @param bundle GeckoBundle containing the item information.
     */
    /* package */ MenuItem(final WebExtension extension, final GeckoBundle bundle) {
      title = bundle.getString("title");
      mExtension = extension;
      checked = bundle.getBoolean("checked", false);
      visible = bundle.getBoolean("visible", true);
      id = bundle.getString("id");
      contexts = bundle.getInt("contexts");
      type = bundle.getInt("type");
      children = new ArrayList<>();

      if (bundle.containsKey("icon")) {
        icon = Image.fromSizeSrcBundle(bundle.getBundle("icon"));
      } else {
        icon = null;
      }
    }

    /** Notifies the extension that the user has clicked on this menu item. */
    void click() {
      final GeckoBundle bundle = new GeckoBundle(2);
      bundle.putString("menuId", this.id);
      bundle.putString("extensionId", mExtension.id);

      EventDispatcher.getInstance().dispatch("GeckoView:WebExtension:MenuClick", bundle);
    }
  }

  /** Handles download requests from WebExtensions. */
  public interface DownloadDelegate {
    /**
     * Method that is called when Web Extension requests a download (when downloads.download() is
     * called in Web Extension)
     *
     * @param source - Web Extension that requested the download
     * @param request - contains the {@link WebRequest} and additional parameters for the request
     * @return {@link DownloadInitData} instance
     */
    @AnyThread
    @Nullable
    default GeckoResult<WebExtension.DownloadInitData> onDownload(
        @NonNull final WebExtension source, @NonNull final DownloadRequest request) {
      return null;
    }
  }

  /**
   * Set the download delegate for this extension. This delegate will be invoked whenever this
   * extension tries to use the `downloads` WebExtension API.
   *
   * <p>See also <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/downloads">WebExtensions/API/downloads</a>.
   *
   * @param delegate the {@link DownloadDelegate} instance for this extension.
   */
  @UiThread
  public void setDownloadDelegate(final @Nullable DownloadDelegate delegate) {
    mDelegateController.onDownloadDelegate(delegate);
  }

  /**
   * Get the download delegate for this extension.
   *
   * <p>See also <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/downloads">WebExtensions
   * downloads API</a>.
   *
   * @return The {@link DownloadDelegate} instance for this extension.
   */
  @UiThread
  @Nullable
  public DownloadDelegate getDownloadDelegate() {
    return mDelegateController.getDownloadDelegate();
  }

  /**
   * Represents a download for <a
   * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/downloads">downloads
   * API</a> Instantiate using {@link WebExtensionController#createDownload}
   */
  public static class Download {
    /**
     * Represents a unique identifier for the downloaded item that is persistent across browser
     * sessions
     */
    public final int id;

    /**
     * For testing.
     *
     * @param id - integer id for the download item
     */
    protected Download(final int id) {
      this.id = id;
    }

    /* package */ void setDelegate(final Delegate delegate) {}

    /**
     * Updates the download state. This will trigger a call to <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/downloads/onChanged">downloads.onChanged</a>
     * event to the corresponding `DownloadItem` on the extension side.
     *
     * @param data - current metadata associated with the download. {@link Download.Info}
     *     implementation instance
     * @return GeckoResult with nothing or error inside
     */
    @Nullable
    @UiThread
    public GeckoResult<Void> update(final @NonNull Download.Info data) {
      final GeckoBundle bundle = new GeckoBundle(12);

      bundle.putInt("downloadItemId", this.id);

      bundle.putString("filename", data.filename());
      bundle.putString("mime", data.mime());
      bundle.putString("startTime", String.valueOf(data.startTime()));
      bundle.putString("endTime", data.endTime() == null ? null : String.valueOf(data.endTime()));
      bundle.putInt("state", data.state());
      bundle.putBoolean("canResume", data.canResume());
      bundle.putBoolean("paused", data.paused());
      final Integer error = data.error();
      if (error != null) {
        bundle.putInt("error", error);
      }
      bundle.putLong("totalBytes", data.totalBytes());
      bundle.putLong("fileSize", data.fileSize());
      bundle.putBoolean("exists", data.fileExists());

      return EventDispatcher.getInstance()
          .queryVoid("GeckoView:WebExtension:DownloadChanged", bundle)
          .map(
              null,
              e -> {
                if (e instanceof EventDispatcher.QueryException) {
                  final EventDispatcher.QueryException queryException =
                      (EventDispatcher.QueryException) e;
                  if (queryException.data instanceof String) {
                    return new IllegalArgumentException((String) queryException.data);
                  }
                }
                return e;
              });
    }

    /* package */ interface Delegate {

      default GeckoResult<Void> onPause(
          final WebExtension source, final WebExtension.Download download) {
        return null;
      }

      default GeckoResult<Void> onResume(
          final WebExtension source, final WebExtension.Download download) {
        return null;
      }

      default GeckoResult<Void> onCancel(
          final WebExtension source, final WebExtension.Download download) {
        return null;
      }

      default GeckoResult<Void> onErase(
          final WebExtension source, final WebExtension.Download download) {
        return null;
      }

      default GeckoResult<Void> onOpen(
          final WebExtension source, final WebExtension.Download download) {
        return null;
      }

      default GeckoResult<Void> onRemoveFile(
          final WebExtension source, final WebExtension.Download download) {
        return null;
      }
    }

    /**
     * Represents a download in progress where the app is currently receiving data from the server.
     * See also {@link Info#state()}.
     */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef({STATE_IN_PROGRESS, STATE_INTERRUPTED, STATE_COMPLETE})
    public @interface DownloadState {}

    /** Download is in progress. Default state */
    public static final int STATE_IN_PROGRESS = 0;

    /** An error broke the connection with the server. */
    public static final int STATE_INTERRUPTED = 1;

    /** The download completed successfully. */
    public static final int STATE_COMPLETE = 2;

    /**
     * Represents a possible reason why a download was interrupted. See also {@link Info#error()}.
     */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef({
      INTERRUPT_REASON_NO_INTERRUPT,
      INTERRUPT_REASON_FILE_FAILED,
      INTERRUPT_REASON_FILE_ACCESS_DENIED,
      INTERRUPT_REASON_FILE_NO_SPACE,
      INTERRUPT_REASON_FILE_NAME_TOO_LONG,
      INTERRUPT_REASON_FILE_TOO_LARGE,
      INTERRUPT_REASON_FILE_VIRUS_INFECTED,
      INTERRUPT_REASON_FILE_TRANSIENT_ERROR,
      INTERRUPT_REASON_FILE_BLOCKED,
      INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED,
      INTERRUPT_REASON_FILE_TOO_SHORT,
      INTERRUPT_REASON_NETWORK_FAILED,
      INTERRUPT_REASON_NETWORK_TIMEOUT,
      INTERRUPT_REASON_NETWORK_DISCONNECTED,
      INTERRUPT_REASON_NETWORK_SERVER_DOWN,
      INTERRUPT_REASON_NETWORK_INVALID_REQUEST,
      INTERRUPT_REASON_SERVER_FAILED,
      INTERRUPT_REASON_SERVER_NO_RANGE,
      INTERRUPT_REASON_SERVER_BAD_CONTENT,
      INTERRUPT_REASON_SERVER_UNAUTHORIZED,
      INTERRUPT_REASON_SERVER_CERT_PROBLEM,
      INTERRUPT_REASON_SERVER_FORBIDDEN,
      INTERRUPT_REASON_USER_CANCELED,
      INTERRUPT_REASON_USER_SHUTDOWN,
      INTERRUPT_REASON_CRASH
    })
    public @interface DownloadInterruptReason {}

    // File-related errors
    /** No interruption. */
    public static final int INTERRUPT_REASON_NO_INTERRUPT = 0;

    /** Generic file error. */
    public static final int INTERRUPT_REASON_FILE_FAILED = 1;

    /** File access denied. */
    public static final int INTERRUPT_REASON_FILE_ACCESS_DENIED = 2;

    /** Insufficient disk space. */
    public static final int INTERRUPT_REASON_FILE_NO_SPACE = 3;

    /** File name too long. */
    public static final int INTERRUPT_REASON_FILE_NAME_TOO_LONG = 4;

    /** File too large. */
    public static final int INTERRUPT_REASON_FILE_TOO_LARGE = 5;

    /** File infected by virus. */
    public static final int INTERRUPT_REASON_FILE_VIRUS_INFECTED = 6;

    /** Transient file error. */
    public static final int INTERRUPT_REASON_FILE_TRANSIENT_ERROR = 7;

    /** File blocked. */
    public static final int INTERRUPT_REASON_FILE_BLOCKED = 8;

    /** File security check failed. */
    public static final int INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED = 9;

    /** File download too short. */
    public static final int INTERRUPT_REASON_FILE_TOO_SHORT = 10;

    // Network-related errors
    /** Network request failed. */
    public static final int INTERRUPT_REASON_NETWORK_FAILED = 11;

    /** Network timed out. */
    public static final int INTERRUPT_REASON_NETWORK_TIMEOUT = 12;

    /** Network disconnected. */
    public static final int INTERRUPT_REASON_NETWORK_DISCONNECTED = 13;

    /** Server is down. */
    public static final int INTERRUPT_REASON_NETWORK_SERVER_DOWN = 14;

    /** Invalid network request. */
    public static final int INTERRUPT_REASON_NETWORK_INVALID_REQUEST = 15;

    // Server-related errors
    /** Server returned a generic error. */
    public static final int INTERRUPT_REASON_SERVER_FAILED = 16;

    /** Server does not support range requests. */
    public static final int INTERRUPT_REASON_SERVER_NO_RANGE = 17;

    /** Server returned bad content. */
    public static final int INTERRUPT_REASON_SERVER_BAD_CONTENT = 18;

    /** Server unauthorized request. */
    public static final int INTERRUPT_REASON_SERVER_UNAUTHORIZED = 19;

    /** Server certificate problem. */
    public static final int INTERRUPT_REASON_SERVER_CERT_PROBLEM = 20;

    /** Server forbidden request. */
    public static final int INTERRUPT_REASON_SERVER_FORBIDDEN = 21;

    // User-related errors
    /** Download canceled by user. */
    public static final int INTERRUPT_REASON_USER_CANCELED = 22;

    /** Download terminated due to user shutdown. */
    public static final int INTERRUPT_REASON_USER_SHUTDOWN = 23;

    // Miscellaneous
    /** Download interrupted by crash. */
    public static final int INTERRUPT_REASON_CRASH = 24;

    /**
     * Interface for communicating the state of downloads to Web Extensions. See also <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/downloads/DownloadItem">WebExtensions/API/downloads/DownloadItem</a>
     */
    public interface Info {

      /**
       * {@return a number representing the number of bytes received so far from the host during the
       * download} This does not take file compression into consideration.
       */
      @UiThread
      default long bytesReceived() {
        return 0;
      }

      /**
       * {@return a boolean indicating whether a currently-interrupted (e.g. paused) download can be
       * resumed from the point where it was interrupted}
       */
      @UiThread
      default boolean canResume() {
        return false;
      }

      /**
       * {@return a number representing the time when this download ended} This is null if the
       * download has not yet finished.
       */
      @Nullable
      @UiThread
      default Long endTime() {
        return null;
      }

      /**
       * {@return one of <a
       * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/downloads/InterruptReason">Interrupt
       * Reason</a> constants denoting the error reason}
       */
      @Nullable
      @UiThread
      default @DownloadInterruptReason Integer error() {
        return null;
      }

      /**
       * {@return the estimated number of milliseconds between the UNIX epoch and when this download
       * is estimated to be completed} This is null if it is not known.
       */
      @Nullable
      @UiThread
      default Long estimatedEndTime() {
        return null;
      }

      /** {@return a boolean indicating whether a downloaded file still exists} */
      @UiThread
      default boolean fileExists() {
        return false;
      }

      /** {@return the filename} */
      @NonNull
      @UiThread
      default String filename() {
        return "";
      }

      /**
       * {@return the total number of bytes in the whole file, after decompression} A value of -1
       * means that the total file size is unknown.
       */
      @UiThread
      default long fileSize() {
        return -1;
      }

      /** {@return the downloaded file's MIME type} */
      @NonNull
      @UiThread
      default String mime() {
        return "";
      }

      /**
       * {@return a boolean indicating whether the download is paused i.e. if the download has
       * stopped reading data from the host but has kept the connection open}
       */
      @UiThread
      default boolean paused() {
        return false;
      }

      /** {@return a string representing the downloaded file's referrer} */
      @NonNull
      @UiThread
      default String referrer() {
        return "";
      }

      /**
       * {@return the number of milliseconds between the UNIX epoch and when this download began}
       */
      @UiThread
      default long startTime() {
        return -1;
      }

      /**
       * {@return a new state; one of the state constants to indicate whether the download is in
       * progress, interrupted or complete}
       */
      @UiThread
      default @DownloadState int state() {
        return STATE_IN_PROGRESS;
      }

      /**
       * {@return the total number of bytes in the file being downloaded} This does not take file
       * compression into consideration. A value of -1 here means that the total number of bytes is
       * unknown.
       */
      @UiThread
      default long totalBytes() {
        return -1;
      }
    }

    @NonNull
    /* package */ static GeckoBundle downloadInfoToBundle(final @NonNull Info data) {
      final GeckoBundle dataBundle = new GeckoBundle();

      dataBundle.putLong("bytesReceived", data.bytesReceived());
      dataBundle.putBoolean("canResume", data.canResume());
      dataBundle.putBoolean("exists", data.fileExists());
      dataBundle.putString("filename", data.filename());
      dataBundle.putLong("fileSize", data.fileSize());
      dataBundle.putString("mime", data.mime());
      dataBundle.putBoolean("paused", data.paused());
      dataBundle.putString("referrer", data.referrer());
      dataBundle.putString("startTime", String.valueOf(data.startTime()));
      dataBundle.putInt("state", data.state());
      dataBundle.putLong("totalBytes", data.totalBytes());

      final Long endTime = data.endTime();
      if (endTime != null) {
        dataBundle.putString("endTime", endTime.toString());
      }
      final Integer error = data.error();
      if (error != null) {
        dataBundle.putInt("error", error);
      }
      final Long estimatedEndTime = data.estimatedEndTime();
      if (estimatedEndTime != null) {
        dataBundle.putString("estimatedEndTime", estimatedEndTime.toString());
      }

      return dataBundle;
    }
  }

  /** Represents Web Extension API specific download request */
  public static class DownloadRequest {
    /** Regular GeckoView {@link WebRequest} object */
    public final @NonNull WebRequest request;

    /** Optional fetch flags for {@link GeckoWebExecutor} */
    public final @GeckoWebExecutor.FetchFlags int downloadFlags;

    /** A file path relative to the default downloads directory */
    public final @Nullable String filename;

    /**
     * The action you want taken if there is a filename conflict, as defined <a
     * href="https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/downloads/FilenameConflictAction">here</a>
     */
    public final @ConflictActionFlags int conflictActionFlag;

    /**
     * Specifies whether to provide a file chooser dialog to allow the user to select a filename
     * (true), or not (false)
     */
    public final boolean saveAs;

    /**
     * Flag that enables downloads to continue even if they encounter HTTP errors. When false, the
     * download is canceled when it encounters an HTTP error. When true, the download continues when
     * an HTTP error is encountered and the HTTP server error is not reported. However, if the
     * download fails due to file-related, network-related, user-related, or other error, that error
     * is reported.
     */
    public final boolean allowHttpErrors;

    /** Conflict action flag definitions for downloads. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef(
        flag = true,
        value = {CONFLICT_ACTION_UNIQUIFY, CONFLICT_ACTION_OVERWRITE, CONFLICT_ACTION_PROMPT})
    public @interface ConflictActionFlags {}

    /** The app should modify the filename to make it unique */
    public static final int CONFLICT_ACTION_UNIQUIFY = 0;

    /** The app should overwrite the old file with the newly-downloaded file */
    public static final int CONFLICT_ACTION_OVERWRITE = 1;

    /** The app should prompt the user, asking them to choose whether to uniquify or overwrite */
    public static final int CONFLICT_ACTION_PROMPT = 1 << 1;

    /**
     * Create a DownloadRequest from the given builder.
     *
     * @param builder The builder containing the request configuration.
     */
    protected DownloadRequest(final DownloadRequest.Builder builder) {
      this.request = builder.mRequest;
      this.downloadFlags = builder.mDownloadFlags;
      this.filename = builder.mFilename;
      this.conflictActionFlag = builder.mConflictActionFlag;
      this.saveAs = builder.mSaveAs;
      this.allowHttpErrors = builder.mAllowHttpErrors;
    }

    /**
     * Convenience method to convert a GeckoBundle to a DownloadRequest.
     *
     * @param optionsBundle - in the shape of the options object browser.downloads.download()
     *     accepts
     * @return request - a DownloadRequest instance
     */
    /* package */ static DownloadRequest fromBundle(final GeckoBundle optionsBundle) {
      final String uri = optionsBundle.getString("url");

      final WebRequest.Builder mainRequestBuilder = new WebRequest.Builder(uri);

      final String method = optionsBundle.getString("method");
      if (method != null) {
        mainRequestBuilder.method(method);

        if (method.equals("POST")) {
          final String body = optionsBundle.getString("body");
          mainRequestBuilder.body(body);
        }
      }

      final GeckoBundle[] headers = optionsBundle.getBundleArray("headers");
      if (headers != null) {
        for (final GeckoBundle header : headers) {
          String value = header.getString("value");
          if (value == null) {
            value = header.getString("binaryValue");
          }
          mainRequestBuilder.addHeader(header.getString("name"), value);
        }
      }

      final WebRequest mainRequest = mainRequestBuilder.build();

      int downloadFlags = GeckoWebExecutor.FETCH_FLAGS_NONE;
      final boolean incognito = optionsBundle.getBoolean("incognito");
      if (incognito) {
        downloadFlags |= GeckoWebExecutor.FETCH_FLAGS_PRIVATE;
      }

      final boolean allowHttpErrors = optionsBundle.getBoolean("allowHttpErrors");

      int conflictActionFlags = CONFLICT_ACTION_UNIQUIFY;
      final String conflictActionString = optionsBundle.getString("conflictAction");
      if (conflictActionString != null) {
        switch (conflictActionString.toLowerCase(Locale.ROOT)) {
          case "overwrite":
            conflictActionFlags |= WebExtension.DownloadRequest.CONFLICT_ACTION_OVERWRITE;
            break;
          case "prompt":
            conflictActionFlags |= WebExtension.DownloadRequest.CONFLICT_ACTION_PROMPT;
            break;
        }
      }

      final boolean saveAs = optionsBundle.getBoolean("saveAs");

      return new Builder(mainRequest)
          .filename(optionsBundle.getString("filename"))
          .downloadFlags(downloadFlags)
          .conflictAction(conflictActionFlags)
          .saveAs(saveAs)
          .allowHttpErrors(allowHttpErrors)
          .build();
    }

    /* package */ static class Builder {
      private final WebRequest mRequest;
      private @GeckoWebExecutor.FetchFlags int mDownloadFlags = 0;
      private String mFilename = null;
      private @ConflictActionFlags int mConflictActionFlag = CONFLICT_ACTION_UNIQUIFY;
      private boolean mSaveAs = false;
      private boolean mAllowHttpErrors = false;

      /* package */ Builder(final WebRequest request) {
        this.mRequest = request;
      }

      /* package */ Builder downloadFlags(final @GeckoWebExecutor.FetchFlags int flags) {
        this.mDownloadFlags = flags;
        return this;
      }

      /* package */ Builder filename(final String filename) {
        this.mFilename = filename;
        return this;
      }

      /* package */ Builder conflictAction(final @ConflictActionFlags int conflictActionFlag) {
        this.mConflictActionFlag = conflictActionFlag;
        return this;
      }

      /* package */ Builder saveAs(final boolean saveAs) {
        this.mSaveAs = saveAs;
        return this;
      }

      /* package */ Builder allowHttpErrors(final boolean allowHttpErrors) {
        this.mAllowHttpErrors = allowHttpErrors;
        return this;
      }

      /* package */ DownloadRequest build() {
        return new DownloadRequest(this);
      }
    }
  }

  /** Represents initial information on a download provided to Web Extension */
  public static class DownloadInitData {
    /** The download object associated with this data. */
    @NonNull public final WebExtension.Download download;

    /** The initial download information. */
    @NonNull public final Download.Info initData;

    /**
     * Create a DownloadInitData with the given download and info.
     *
     * @param download The download object.
     * @param initData The initial download information.
     */
    public DownloadInitData(final Download download, final Download.Info initData) {
      this.download = download;
      this.initData = initData;
    }
  }

  /**
   * Holds all the information which the user has submited as part of a confirmation of a
   * permissions prompt request.
   */
  public static class PermissionPromptResponse {
    /** Whether the user granted permissions or not. */
    @Nullable public final Boolean isPermissionsGranted;

    /** Whether the user granted access in private mode or not. */
    @Nullable public final Boolean isPrivateModeGranted;

    /** Whether the user granted access to technical and interaction data collection. */
    @Nullable public final Boolean isTechnicalAndInteractionDataGranted;

    /**
     * Creates a new PermissionPromptResponse with the given fields.
     *
     * @param isPermissionsGranted Whether the user granted permissions or not.
     * @param isPrivateModeGranted Whether the user granted access in private mode or not.
     * @param isTechnicalAndInteractionDataGranted Whether the user granted access to technical and
     *     interaction data collection.
     */
    public PermissionPromptResponse(
        final Boolean isPermissionsGranted,
        final Boolean isPrivateModeGranted,
        final Boolean isTechnicalAndInteractionDataGranted) {
      this.isPermissionsGranted = isPermissionsGranted;
      this.isPrivateModeGranted = isPrivateModeGranted;
      this.isTechnicalAndInteractionDataGranted = isTechnicalAndInteractionDataGranted;
    }
  }
}
