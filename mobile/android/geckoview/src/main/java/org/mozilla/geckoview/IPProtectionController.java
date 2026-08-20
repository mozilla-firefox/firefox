/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.util.Log;
import androidx.annotation.AnyThread;
import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.UiThread;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.util.BundleEventListener;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ThreadUtils;

/** Controller for managing IP protection state. */
@ExperimentalGeckoViewApi
public class IPProtectionController {
  private static final String LOGTAG = "IPProtectionController";
  private Delegate mDelegate;
  private AuthProvider mAuthProvider;
  private GpiProvider mGpiProvider;
  private final BundleEventListener mEventListener;

  /** The possible states of the IP protection service. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({
    SERVICE_STATE_UNINITIALIZED,
    SERVICE_STATE_UNAVAILABLE,
    SERVICE_STATE_UNAUTHENTICATED,
    SERVICE_STATE_OPTED_OUT,
    SERVICE_STATE_READY
  })
  public @interface ServiceState {}

  /** The service has not been initialized yet. */
  public static final int SERVICE_STATE_UNINITIALIZED = 0;

  /** The user is not eligible or still not signed in. */
  public static final int SERVICE_STATE_UNAVAILABLE = 1;

  /** The user is signed out but eligible. */
  public static final int SERVICE_STATE_UNAUTHENTICATED = 2;

  /** The user has opted out from using VPN. */
  public static final int SERVICE_STATE_OPTED_OUT = 3;

  /** The service is ready to be activated. */
  public static final int SERVICE_STATE_READY = 4;

  private static @ServiceState int parseServiceState(final @Nullable String state) {
    switch (state) {
      case "uninitialized":
        return SERVICE_STATE_UNINITIALIZED;
      case "unavailable":
        return SERVICE_STATE_UNAVAILABLE;
      case "unauthenticated":
        return SERVICE_STATE_UNAUTHENTICATED;
      case "optedout":
        return SERVICE_STATE_OPTED_OUT;
      case "ready":
        return SERVICE_STATE_READY;
      default:
        throw new IPProxyException(IPProxyException.ERROR_UNKNOWN);
    }
  }

  /** Holds the current IP proxy state and any associated error. */
  public static class ProxyState {

    /** The possible states of the IP proxy. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef({NOT_READY, READY, ACTIVATING, ACTIVE, ERROR, PAUSED})
    public @interface Code {}

    /** The proxy is not ready. */
    public static final int NOT_READY = 0;

    /** The proxy is ready to be activated. */
    public static final int READY = 1;

    /** The proxy is in the process of activating. */
    public static final int ACTIVATING = 2;

    /** The proxy is active. */
    public static final int ACTIVE = 3;

    /** The proxy encountered an error. */
    public static final int ERROR = 4;

    /** The proxy is paused (e.g. bandwidth limit reached). */
    public static final int PAUSED = 5;

    /** The current proxy state. One of the {@link Code} constants. */
    public final @Code int state;

    /** The error type if {@link #state} is {@link #ERROR}, null otherwise. */
    public final @Nullable String errorType;

    /** Default constructor. */
    protected ProxyState() {
      state = NOT_READY;
      errorType = null;
    }

    /* package */ ProxyState(final @NonNull GeckoBundle bundle) {
      state = parseCode(bundle.getString("state"));
      errorType = bundle.getString("errorType");
    }

    private static @Code int parseCode(final @Nullable String s) {
      switch (s) {
        case "not-ready":
          return NOT_READY;
        case "ready":
          return READY;
        case "activating":
          return ACTIVATING;
        case "active":
          return ACTIVE;
        case "error":
          return ERROR;
        case "paused":
          return PAUSED;
        default:
          throw new IllegalStateException("Unknown proxy state: " + s);
      }
    }
  }

  /** Holds the result of an enrollment attempt. */
  public static class EnrollResult {
    /** Whether the user is now enrolled and entitled to use the proxy. */
    public final boolean isEnrolledAndEntitled;

    /** Error string describing why enrollment failed, or {@code null} on success. */
    public final @Nullable String error;

    /** Default constructor. */
    protected EnrollResult() {
      isEnrolledAndEntitled = false;
      error = null;
    }

    /* package */ EnrollResult(final @NonNull GeckoBundle bundle) {
      isEnrolledAndEntitled = bundle.getBoolean("isEnrolledAndEntitled", false);
      error = bundle.getString("error");
    }
  }

  /**
   * Holds information about the current IP proxy usage. Whenever remaining,max and resetTime are
   * equal to 0, this means there are no usage restrictions for the current User at the time.
   */
  public static class UsageInfo {
    /** Remaining usage allowance in bytes. */
    public final long remaining;

    /** Maximum usage allowance in bytes. */
    public final long max;

    /** The time when usage resets, as an ISO 8601 string, or null if unavailable. */
    public final @Nullable String resetTime;

    /** Default constructor. */
    protected UsageInfo() {
      remaining = 0L;
      max = 0L;
      resetTime = null;
    }

    /* package */ UsageInfo(final @NonNull GeckoBundle bundle) {
      remaining = bundle.getLong("remaining", 0L);
      max = bundle.getLong("max", 0L);
      resetTime = bundle.getString("resetTime");
    }
  }

  /** A country available in the proxy serverlist. */
  public static class Country {
    /** ISO 3166-1 alpha-2 country code. */
    public final @NonNull String code;

    /** Whether the country has at least one available (non-quarantined) server. */
    public final boolean available;

    /**
     * Creates a country.
     *
     * @param code ISO 3166-1 alpha-2 country code.
     * @param available Whether the country has at least one available server.
     */
    public Country(final @NonNull String code, final boolean available) {
      this.code = code;
      this.available = available;
    }

    /* package */ Country(final @NonNull GeckoBundle bundle) {
      this(bundle.getString("code", ""), bundle.getBoolean("available", true));
    }
  }

  /** Embedder-provided hooks for Google Play Integrity authentication. */
  public interface GpiProvider {
    /**
     * Called when Gecko requests GPI warm-up. The returned {@link GeckoResult} should resolve once
     * warm-up is complete. On success the controller dispatches {@code
     * GeckoView:IPProtection:GPI:WarmUpCompleted} to Gecko; on failure {@code
     * GeckoView:IPProtection:GPI:WarmUpFailed} is dispatched so the auth provider can fall back to
     * FxA.
     *
     * @return A {@link GeckoResult} that resolves when warm-up finishes.
     */
    @UiThread
    default @NonNull GeckoResult<Void> warmUp() {
      return GeckoResult.fromValue(null);
    }

    /**
     * Invoked by Gecko to obtain a fresh GPI integrity token from the embedder. This is triggered
     * during enrollment with the Guardian backend when the user turns on IP Protection, or when the
     * cached auth token has expired and must be renewed.
     *
     * <p>To signal that no token is available, return a {@link GeckoResult} that rejects or
     * resolves to {@code null} or an empty string.
     *
     * @return A {@link GeckoResult} that resolves to a non-empty token string.
     */
    @UiThread
    default @NonNull GeckoResult<String> onTokenRequest() {
      return GeckoResult.fromException(new RuntimeException(ERROR_LOGIN_NEEDED));
    }
  }

  /** Embedder-provided hooks for authentication. */
  public interface AuthProvider {
    /**
     * Invoked by Gecko to obtain a fresh authentication token from the embedder. Called for every
     * Guardian API request; the implementer is responsible for caching and refreshing.
     *
     * <p>The token must have the "https://identity.mozilla.com/apps/vpn" scope.
     *
     * <p>To signal that no token is available, return a {@link GeckoResult} that rejects (e.g.
     * {@link GeckoResult#fromException(Throwable)}) or resolves to {@code null} or an empty string.
     * In all of those cases the in-flight Guardian request will fail with {@code "no-token"}.
     *
     * @return A {@link GeckoResult} that resolves to a non-empty token string.
     */
    @UiThread
    default @NonNull GeckoResult<String> onTokenRequest() {
      return GeckoResult.fromException(new RuntimeException(ERROR_LOGIN_NEEDED));
    }
  }

  /** Delegate for receiving IP protection state notifications. */
  public interface Delegate {
    /**
     * Called when the IP protection service state changes.
     *
     * @param state The current service state. One of the {@link ServiceState} constants.
     */
    @UiThread
    default void onServiceStateChanged(final @ServiceState int state) {}

    /**
     * Called when the IP proxy state changes.
     *
     * @param state The current proxy state.
     */
    @UiThread
    default void onProxyStateChanged(final @NonNull ProxyState state) {}

    /**
     * Called when the IP proxy usage changes.
     *
     * @param info The current usage information.
     */
    @UiThread
    default void onUsageChanged(final @NonNull UsageInfo info) {}

    /**
     * Called when the proxy serverlist changes, either in response to {@link
     * IPProtectionController#getCountryList()} or whenever the underlying list is replaced.
     *
     * @param countries The current list of available {@link Country countries}.
     */
    @UiThread
    default void onCountryListChanged(final @NonNull List<Country> countries) {}
  }

  /* package */ IPProtectionController() {
    mEventListener = new EventListener();
    EventDispatcher.getInstance()
        .registerUiThreadListener(
            mEventListener,
            "GeckoView:IPProtection:IPProtectionService:StateChanged",
            "GeckoView:IPProtection:IPPProxyManager:StateChanged",
            "GeckoView:IPProtection:IPPProxyManager:UsageChanged",
            "GeckoView:IPProtection:ServerList:ListChanged",
            "GeckoView:IPProtection:GetToken",
            "GeckoView:IPProtection:GPI:WarmUp",
            "GeckoView:IPProtection:GPI:RequestToken");
  }

  /**
   * Initializes the IP protection service.
   *
   * @return A {@link GeckoResult} that resolves when initialization completes.
   */
  @HandlerThread
  public @NonNull GeckoResult<Void> init() {
    ThreadUtils.assertOnHandlerThread();
    return EventDispatcher.getInstance().queryVoid("GeckoView:IPProtection:Init");
  }

  /**
   * Sets the {@link Delegate} for this instance.
   *
   * @param delegate The {@link Delegate} instance.
   */
  @UiThread
  public void setDelegate(final @Nullable Delegate delegate) {
    ThreadUtils.assertOnUiThread();
    mDelegate = delegate;
  }

  /**
   * Gets the {@link Delegate} for this instance.
   *
   * @return The {@link Delegate} instance.
   */
  @UiThread
  @Nullable
  public Delegate getDelegate() {
    ThreadUtils.assertOnUiThread();
    return mDelegate;
  }

  /**
   * Sets the {@link AuthProvider} used to supply authentication tokens. Pass {@code null} to clear
   * the provider.
   *
   * <p>The provider governs token retrieval only. Sign-in state must be signalled separately via
   * {@link #notifySignInStateChanged(boolean)}.
   *
   * <p>Requires {@link #init()} to have been called; otherwise the JS-side listeners that consume
   * the provider have not yet been registered.
   *
   * @param provider The {@link AuthProvider}, or {@code null} to clear.
   */
  @UiThread
  public void setAuthProvider(final @Nullable AuthProvider provider) {
    ThreadUtils.assertOnUiThread();
    mAuthProvider = provider;
  }

  /**
   * Gets the {@link AuthProvider} for this instance.
   *
   * @return The {@link AuthProvider} instance, or {@code null} if none is set.
   */
  @UiThread
  @Nullable
  public AuthProvider getAuthProvider() {
    ThreadUtils.assertOnUiThread();
    return mAuthProvider;
  }

  /**
   * Sets the {@link GpiProvider} used to handle Google Play Integrity warm-up and token requests.
   * Pass {@code null} to clear the provider.
   *
   * @param provider The {@link GpiProvider}, or {@code null} to clear.
   */
  @UiThread
  public void setGpiProvider(final @Nullable GpiProvider provider) {
    ThreadUtils.assertOnUiThread();
    mGpiProvider = provider;
  }

  /**
   * Gets the {@link GpiProvider} for this instance.
   *
   * @return The {@link GpiProvider} instance, or {@code null} if none is set.
   */
  @UiThread
  @Nullable
  public GpiProvider getGpiProvider() {
    ThreadUtils.assertOnUiThread();
    return mGpiProvider;
  }

  /**
   * Notifies the IP protection service of a sign-in state change. The service recomputes its state
   * and may transition in or out of {@link #SERVICE_STATE_UNAUTHENTICATED}.
   *
   * <p>Announcing a signed-in user requires an {@link AuthProvider} to be set via {@link
   * #setAuthProvider(AuthProvider)} beforehand, since the service immediately queries tokens
   * through the provider. Announcing a signed-out user does not require a provider. If the
   * precondition is violated, the returned {@link GeckoResult} is rejected with {@link
   * IllegalStateException}.
   *
   * <p>Requires {@link #init()} to have been called; otherwise the JS-side listeners that consume
   * the event have not yet been registered.
   *
   * @param signedIn Whether the embedding app currently has a signed-in user.
   * @return A {@link GeckoResult} that resolves once the JS side has acknowledged the event, or
   *     rejects with {@link IllegalStateException} if {@code signedIn} is true and no {@link
   *     AuthProvider} has been set.
   */
  @UiThread
  public @NonNull GeckoResult<Void> notifySignInStateChanged(final boolean signedIn) {
    ThreadUtils.assertOnUiThread();
    if (signedIn && mAuthProvider == null) {
      return GeckoResult.fromException(
          new IllegalStateException(
              "notifySignInStateChanged(true) requires an AuthProvider; call setAuthProvider first"));
    }
    final GeckoBundle bundle = new GeckoBundle(1);
    bundle.putBoolean("isSignedIn", signedIn);
    return EventDispatcher.getInstance()
        .queryVoid("GeckoView:IPProtection:AuthStateChanged", bundle);
  }

  /**
   * Uninitializes the IP protection service, resetting the controller to its initial state.
   *
   * @return A {@link GeckoResult} that resolves when uninitialization completes.
   */
  @HandlerThread
  public @NonNull GeckoResult<Void> uninit() {
    ThreadUtils.assertOnHandlerThread();
    return EventDispatcher.getInstance().queryVoid("GeckoView:IPProtection:Uninit");
  }

  /**
   * Gets the current IP protection service state.
   *
   * @return A {@link GeckoResult} that resolves to one of the {@link ServiceState} constants.
   */
  @HandlerThread
  @ServiceState
  public @NonNull GeckoResult<Integer> getServiceState() {
    ThreadUtils.assertOnHandlerThread();
    return EventDispatcher.getInstance()
        .queryBundle("GeckoView:IPProtection:IPProtectionService:GetState")
        .map(b -> parseServiceState(b.getString("state")));
  }

  /**
   * Requests the list of countries available in the proxy serverlist. The list is delivered
   * asynchronously via {@link Delegate#onCountryListChanged(List)}, which is also invoked whenever
   * the underlying serverlist changes.
   *
   * @return A {@link GeckoResult} that resolves once the request has been processed.
   */
  @HandlerThread
  public @NonNull GeckoResult<Void> getCountryList() {
    ThreadUtils.assertOnHandlerThread();
    return EventDispatcher.getInstance()
        .queryVoid("GeckoView:IPProtection:ServerList:GetCountryList");
  }

  private static @NonNull List<Country> countriesFromBundle(final @NonNull GeckoBundle bundle) {
    final List<Country> result = new ArrayList<>();
    final GeckoBundle[] countries = bundle.getBundleArray("countries");
    if (countries != null) {
      for (final GeckoBundle country : countries) {
        result.add(new Country(country));
      }
    }
    return result;
  }

  /**
   * Gets the current IP proxy state.
   *
   * @return A {@link GeckoResult} that resolves to a {@link ProxyState}.
   */
  @HandlerThread
  public @NonNull GeckoResult<ProxyState> getProxyState() {
    ThreadUtils.assertOnHandlerThread();
    return EventDispatcher.getInstance()
        .queryBundle("GeckoView:IPProtection:IPPProxyManager:GetState")
        .map(bundle -> bundle != null ? new ProxyState(bundle) : null);
  }

  /**
   * Activates the IP proxy by user action, in a non-private context, using the recommended
   * location.
   *
   * @return A {@link GeckoResult} that resolves when activated, or rejects with an {@link
   *     IPProxyException} describing the failure.
   */
  @HandlerThread
  public @NonNull GeckoResult<Void> activate() {
    return activate(true, false, null);
  }

  /**
   * Activates the IP proxy.
   *
   * <p>If the proxy is already active, calling this again with a different {@code country} switches
   * the active connection to a server in that country without tearing down the proxy.
   *
   * @param userAction Whether activation was triggered by an explicit user action, as opposed to a
   *     system action.
   * @param inPrivateBrowsing Whether activation was triggered from a private browsing context.
   * @param country The ISO 3166-1 alpha-2 code of the country to route through, or {@code null} to
   *     use the recommended location.
   * @return A {@link GeckoResult} that resolves when activated, or rejects with an {@link
   *     IPProxyException} describing the failure.
   */
  @HandlerThread
  public @NonNull GeckoResult<Void> activate(
      final boolean userAction, final boolean inPrivateBrowsing, final @Nullable String country) {
    ThreadUtils.assertOnHandlerThread();
    final GeckoBundle bundle = new GeckoBundle(3);
    bundle.putBoolean("userAction", userAction);
    bundle.putBoolean("inPrivateBrowsing", inPrivateBrowsing);
    if (country != null) {
      bundle.putString("country", country);
    }
    return EventDispatcher.getInstance()
        .queryVoid("GeckoView:IPProtection:Activate", bundle)
        .map(null, IPProtectionController::toIPProxyException);
  }

  private static @NonNull IPProxyException toIPProxyException(final @NonNull Throwable e) {
    return IPProxyException.fromErrorString(
        e instanceof EventDispatcher.QueryException
            ? ((EventDispatcher.QueryException) e).data.toString()
            : null);
  }

  /**
   * Triggers enrollment via the active auth provider.
   *
   * @return A {@link GeckoResult} that resolves to an {@link EnrollResult} describing whether the
   *     user is now enrolled and entitled, and the error string if not.
   */
  @HandlerThread
  public @NonNull GeckoResult<EnrollResult> enroll() {
    ThreadUtils.assertOnHandlerThread();
    return EventDispatcher.getInstance()
        .queryBundle("GeckoView:IPProtection:Enroll")
        .map(bundle -> bundle != null ? new EnrollResult(bundle) : new EnrollResult());
  }

  /**
   * Deactivates the IP proxy.
   *
   * @return A {@link GeckoResult} that resolves when deactivated, or rejects with an {@link
   *     IPProxyException} if an unexpected error occurs.
   */
  @HandlerThread
  public @NonNull GeckoResult<Void> deactivate() {
    ThreadUtils.assertOnHandlerThread();
    return EventDispatcher.getInstance()
        .queryVoid("GeckoView:IPProtection:Deactivate")
        .map(null, IPProtectionController::toIPProxyException);
  }

  /**
   * Requests a refresh of the proxy usage information. The updated usage will be delivered
   * asynchronously via {@link Delegate#onUsageChanged(UsageInfo)}.
   *
   * @return A {@link GeckoResult} that resolves when the refresh request has been processed, or
   *     rejects with an {@link IPProxyException} if an unexpected error occurs.
   */
  @HandlerThread
  public @NonNull GeckoResult<Void> refreshUsage() {
    ThreadUtils.assertOnHandlerThread();
    return EventDispatcher.getInstance()
        .queryVoid("GeckoView:IPProtection:RefreshUsage")
        .map(null, IPProtectionController::toIPProxyException);
  }

  /** Exception type for IP proxy errors. */
  public static class IPProxyException extends RuntimeException {

    /** An unexpected error occurred. */
    public static final int ERROR_UNKNOWN = -1;

    /** The network is unavailable. */
    public static final int ERROR_NETWORK = -2;

    /** Activation timed out. */
    public static final int ERROR_TIMEOUT = -3;

    /** No proxy pass was returned from the server. */
    public static final int ERROR_PASS_UNAVAILABLE = -4;

    /** No server was found for this location. */
    public static final int ERROR_SERVER_NOT_FOUND = -5;

    /** Activation was canceled (e.g. deactivate was called mid-activation). */
    public static final int ERROR_ACTIVATION_CANCELED = -6;

    /** The proxy service failed in a way it cannot recover from. */
    public static final int ERROR_CATASTROPHIC = -7;

    /** The proxy is not available in the region of the user. */
    public static final int ERROR_VPN_UNAVAILABLE = -8;

    /** The proxy failed for a reason it did not report. */
    public static final int ERROR_GENERIC = -9;

    /** Activation was requested while the proxy was not ready to be activated. */
    public static final int ERROR_NOT_READY = -10;

    /** The bandwidth limit has been reached. */
    public static final int ERROR_QUOTA_EXHAUSTED = -11;

    /** The user needs to authenticate again before the proxy can be used. */
    public static final int ERROR_AUTH_REQUIRED = -12;

    /** Enrolling the user with the proxy service failed. */
    public static final int ERROR_ENROLLMENT_FAILED = -13;

    /** No authentication provider is installed. */
    public static final int ERROR_NO_AUTH_PROVIDER = -14;

    /** The proxy service returned a response that could not be understood. */
    public static final int ERROR_INVALID_RESPONSE = -15;

    /** The proxy connection never came up. */
    public static final int ERROR_CONNECTION_FAILED = -16;

    /** The list of proxy servers could not be fetched. */
    public static final int ERROR_SERVERLIST_UNAVAILABLE = -17;

    /** The proxy violated one of its own invariants. */
    public static final int ERROR_INTERNAL = -18;

    /** The credentials were valid but the user has no proxy subscription. */
    public static final int ERROR_NOT_ENTITLED = -19;

    /** No Play Integrity provider is installed. */
    public static final int ERROR_NO_GPI_PROVIDER = -20;

    /** Play Integrity did not return a token. */
    public static final int ERROR_NO_GPI_TOKEN = -21;

    /** Error codes for {@link IPProxyException}. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef(
        value = {
          ERROR_UNKNOWN,
          ERROR_NETWORK,
          ERROR_TIMEOUT,
          ERROR_PASS_UNAVAILABLE,
          ERROR_SERVER_NOT_FOUND,
          ERROR_ACTIVATION_CANCELED,
          ERROR_CATASTROPHIC,
          ERROR_VPN_UNAVAILABLE,
          ERROR_GENERIC,
          ERROR_NOT_READY,
          ERROR_QUOTA_EXHAUSTED,
          ERROR_AUTH_REQUIRED,
          ERROR_ENROLLMENT_FAILED,
          ERROR_NO_AUTH_PROVIDER,
          ERROR_INVALID_RESPONSE,
          ERROR_CONNECTION_FAILED,
          ERROR_SERVERLIST_UNAVAILABLE,
          ERROR_INTERNAL,
          ERROR_NOT_ENTITLED,
          ERROR_NO_GPI_PROVIDER,
          ERROR_NO_GPI_TOKEN,
        })
    public @interface Code {}

    /** The error code for this exception. One of the {@link Code} constants. */
    public final @Code int code;

    /**
     * @param code One of the {@link Code} constants.
     */
    /* package */ IPProxyException(final @Code int code) {
      this.code = code;
    }

    /**
     * Converts a raw error string from the proxy into a typed {@link IPProxyException}.
     *
     * @param error The error string returned by the proxy, or null.
     * @return An {@link IPProxyException} with the corresponding {@link Code}.
     */
    @AnyThread
    public static @NonNull IPProxyException fromErrorString(final @Nullable String error) {
      switch (error != null ? error : "") {
        case "network-error":
          return new IPProxyException(ERROR_NETWORK);
        case "timeout-error":
          return new IPProxyException(ERROR_TIMEOUT);
        case "pass-unavailable":
          return new IPProxyException(ERROR_PASS_UNAVAILABLE);
        case "server-not-found":
          return new IPProxyException(ERROR_SERVER_NOT_FOUND);
        case "activation-canceled":
          return new IPProxyException(ERROR_ACTIVATION_CANCELED);
        case "catastrophic-error":
          return new IPProxyException(ERROR_CATASTROPHIC);
        case "vpn-unavailable":
          return new IPProxyException(ERROR_VPN_UNAVAILABLE);
        case "generic-error":
          return new IPProxyException(ERROR_GENERIC);
        case "not-ready-error":
          return new IPProxyException(ERROR_NOT_READY);
        case "quota-exhausted":
          return new IPProxyException(ERROR_QUOTA_EXHAUSTED);
        case "login_needed":
        case "unauthorized":
          return new IPProxyException(ERROR_AUTH_REQUIRED);
        case "enrollment_failed":
          return new IPProxyException(ERROR_ENROLLMENT_FAILED);
        case "no_auth_provider":
          return new IPProxyException(ERROR_NO_AUTH_PROVIDER);
        case "no_gpi_provider":
          return new IPProxyException(ERROR_NO_GPI_PROVIDER);
        case "no_gpi_token":
          return new IPProxyException(ERROR_NO_GPI_TOKEN);
        case "not_entitled":
          return new IPProxyException(ERROR_NOT_ENTITLED);
        case "invalid_response":
        case "parse_error":
        case "unexpected_status":
          return new IPProxyException(ERROR_INVALID_RESPONSE);
        case "connection-failed":
          return new IPProxyException(ERROR_CONNECTION_FAILED);
        case "serverlist-unavailable":
          return new IPProxyException(ERROR_SERVERLIST_UNAVAILABLE);
        case "missing-activation-promise":
        case "missing-abort-controller":
        case "missing-pass":
          return new IPProxyException(ERROR_INTERNAL);
        default:
          return new IPProxyException(ERROR_UNKNOWN);
      }
    }
  }

  // Reported to Gecko, so these must be spelled as the auth provider errors in
  // toolkit/components/ipprotection/IPPAuthProvider.sys.mjs.
  private static final String ERROR_NO_AUTH_PROVIDER = "no_auth_provider";
  private static final String ERROR_NO_GPI_PROVIDER = "no_gpi_provider";
  private static final String ERROR_NO_GPI_TOKEN = "no_gpi_token";
  private static final String ERROR_LOGIN_NEEDED = "login_needed";

  private class EventListener implements BundleEventListener {
    @Override
    public void handleMessage(
        final String event, final GeckoBundle message, final EventCallback callback) {
      switch (event) {
        case "GeckoView:IPProtection:IPProtectionService:StateChanged":
          withDelegate(
              event, d -> d.onServiceStateChanged(parseServiceState(message.getString("state"))));
          break;
        case "GeckoView:IPProtection:IPPProxyManager:StateChanged":
          withDelegate(event, d -> d.onProxyStateChanged(new ProxyState(message)));
          break;
        case "GeckoView:IPProtection:IPPProxyManager:UsageChanged":
          withDelegate(event, d -> d.onUsageChanged(new UsageInfo(message)));
          break;
        case "GeckoView:IPProtection:ServerList:ListChanged":
          withDelegate(event, d -> d.onCountryListChanged(countriesFromBundle(message)));
          break;
        case "GeckoView:IPProtection:GetToken":
          {
            final AuthProvider provider = tryAuthProvider(event, callback);
            if (provider == null) return;
            callback.resolveTo(
                provider
                    .onTokenRequest()
                    .map(
                        token -> {
                          if (token == null || token.isEmpty()) {
                            throw new RuntimeException(ERROR_LOGIN_NEEDED);
                          }
                          final GeckoBundle result = new GeckoBundle(1);
                          result.putString("token", token);
                          return result;
                        }));
            break;
          }
        case "GeckoView:IPProtection:GPI:WarmUp":
          {
            final GpiProvider gpiProvider = mGpiProvider;
            if (gpiProvider == null) {
              Log.w(LOGTAG, "Received " + event + " but no GPI provider is set");
              break;
            }
            gpiProvider
                .warmUp()
                .then(
                    v -> {
                      EventDispatcher.getInstance()
                          .dispatch("GeckoView:IPProtection:GPI:WarmUpCompleted", null);
                      return null;
                    },
                    e -> {
                      Log.w(LOGTAG, "GPI warm-up failed", e);
                      EventDispatcher.getInstance()
                          .dispatch("GeckoView:IPProtection:GPI:WarmUpFailed", null);
                      return null;
                    });
            break;
          }
        case "GeckoView:IPProtection:GPI:RequestToken":
          {
            final GpiProvider gpiProvider = tryGpiProvider(event, callback);
            if (gpiProvider == null) return;
            callback.resolveTo(
                gpiProvider
                    .onTokenRequest()
                    .map(
                        token -> {
                          if (token == null || token.isEmpty()) {
                            throw new RuntimeException(ERROR_NO_GPI_TOKEN);
                          }
                          final GeckoBundle result = new GeckoBundle(1);
                          result.putString("token", token);
                          return result;
                        }));
            break;
          }
      }
    }

    private void withDelegate(final String event, final Consumer<Delegate> action) {
      if (mDelegate == null) {
        Log.w(LOGTAG, "Received event " + event + " but no delegate is set");
        return;
      }
      action.accept(mDelegate);
    }

    private @Nullable AuthProvider tryAuthProvider(
        final String event, final EventCallback callback) {
      if (mAuthProvider == null) {
        Log.w(LOGTAG, "Received event " + event + " but no auth provider is set");
        callback.sendError(ERROR_NO_AUTH_PROVIDER);
        return null;
      }
      return mAuthProvider;
    }

    private @Nullable GpiProvider tryGpiProvider(final String event, final EventCallback callback) {
      if (mGpiProvider == null) {
        Log.w(LOGTAG, "Received event " + event + " but no GPI provider is set");
        callback.sendError(ERROR_NO_GPI_PROVIDER);
        return null;
      }
      return mGpiProvider;
    }
  }
}
