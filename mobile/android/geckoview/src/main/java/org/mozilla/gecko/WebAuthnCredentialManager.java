/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.pm.PackageManager;
import android.credentials.CreateCredentialException;
import android.credentials.CreateCredentialRequest;
import android.credentials.CreateCredentialResponse;
import android.credentials.CredentialManager;
import android.credentials.CredentialOption;
import android.credentials.GetCredentialException;
import android.credentials.GetCredentialRequest;
import android.credentials.GetCredentialResponse;
import android.credentials.PrepareGetCredentialResponse;
import android.os.Build;
import android.os.Bundle;
import android.os.OutcomeReceiver;
import android.util.Log;
import androidx.annotation.UiThread;
import java.util.concurrent.atomic.AtomicBoolean;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.mozilla.gecko.util.ThreadUtils;
import org.mozilla.gecko.util.WebAuthnUtils;
import org.mozilla.geckoview.GeckoResult;

// Credential Manager implementation that is introduced from Android 14.
public class WebAuthnCredentialManager {
  private static final String LOGTAG = "WebAuthnCredMan";
  private static final boolean DEBUG = false;

  // This defines are from androidx.credentials.
  // https://cs.android.com/androidx/platform/frameworks/support/+/androidx-main:credentials/credentials/src/main/java/androidx/credentials/
  private static final String CM_PREFIX = "androidx.credentials.";
  private static final String TYPE_PUBLIC_KEY_CREDENTIAL = CM_PREFIX + "TYPE_PUBLIC_KEY_CREDENTIAL";
  private static final String BUNDLE_KEY_CLIENT_DATA_HASH =
      CM_PREFIX + "BUNDLE_KEY_CLIENT_DATA_HASH";
  private static final String BUNDLE_KEY_REGISTRATION_RESPONSE_JSON =
      CM_PREFIX + "BUNDLE_KEY_REGISTRATION_RESPONSE_JSON";
  private static final String BUNDLE_KEY_AUTHENTICATION_RESPONSE_JSON =
      CM_PREFIX + "BUNDLE_KEY_AUTHENTICATION_RESPONSE_JSON";
  private static final String BUNDLE_KEY_REQUEST_JSON = CM_PREFIX + "BUNDLE_KEY_REQUEST_JSON";
  private static final String BUNDLE_KEY_REQUEST_DISPLAY_INFO =
      CM_PREFIX + "BUNDLE_KEY_REQUEST_DISPLAY_INFO";
  private static final String BUNDLE_KEY_SUBTYPE = CM_PREFIX + "BUNDLE_KEY_SUBTYPE";
  private static final String BUNDLE_KEY_USER_DISPLAY_NAME =
      CM_PREFIX + "BUNDLE_KEY_USER_DISPLAY_NAME";
  private static final String BUNDLE_KEY_USER_ID = CM_PREFIX + "BUNDLE_KEY_USER_ID";
  private static final String BUNDLE_VALUE_SUBTYPE_CREATE_PUBLIC_KEY_CREDENTIAL_REQUEST =
      CM_PREFIX + "BUNDLE_VALUE_SUBTYPE_CREATE_PUBLIC_KEY_CREDENTIAL_REQUEST";
  private static final String BUNDLE_VALUE_SUBTYPE_GET_PUBLIC_KEY_CREDENTIAL_OPTION =
      CM_PREFIX + "BUNDLE_VALUE_SUBTYPE_GET_PUBLIC_KEY_CREDENTIAL_OPTION";
  private static final String BUNDLE_KEY_IS_AUTO_SELECT_ALLOWED =
      CM_PREFIX + "BUNDLE_KEY_IS_AUTO_SELECT_ALLOWED";
  private static final String BUNDLE_KEY_PREFER_IMMEDIATELY_AVAILABLE_CREDENTIALS =
      CM_PREFIX + "BUNDLE_KEY_PREFER_IMMEDIATELY_AVAILABLE_CREDENTIALS";

  private static Bundle getRequestBundle(final String requestJSON, final byte[] clientDataHash) {
    final Bundle requestBundle = new Bundle();
    requestBundle.putString(BUNDLE_KEY_REQUEST_JSON, requestJSON);
    requestBundle.putByteArray(BUNDLE_KEY_CLIENT_DATA_HASH, clientDataHash);

    return requestBundle;
  }

  private static Bundle getRequestBundleForMakeCredential(
      final byte[] clientDataHash, final String requestJSON) {
    final Bundle bundle = getRequestBundle(requestJSON, clientDataHash);
    if (bundle == null) {
      return null;
    }

    final JSONObject json;
    try {
      json = new JSONObject(requestJSON);
    } catch (final JSONException e) {
      return null;
    }

    final JSONObject userField = json.optJSONObject("user");
    if (userField == null) {
      return null;
    }

    final Bundle displayInfoBundle = new Bundle();
    displayInfoBundle.putCharSequence(BUNDLE_KEY_USER_ID, userField.optString("id"));
    displayInfoBundle.putString(BUNDLE_KEY_USER_DISPLAY_NAME, userField.optString("displayName"));
    bundle.putBundle(BUNDLE_KEY_REQUEST_DISPLAY_INFO, displayInfoBundle);

    bundle.putString(BUNDLE_KEY_SUBTYPE, BUNDLE_VALUE_SUBTYPE_CREATE_PUBLIC_KEY_CREDENTIAL_REQUEST);
    return bundle;
  }

  private static Bundle getRequestBundleForGetAssertion(
      final byte[] clientDataHash, final String requestJSON) {
    final Bundle bundle = getRequestBundle(requestJSON, clientDataHash);
    if (bundle == null) {
      return null;
    }

    final JSONObject json;
    try {
      json = new JSONObject(requestJSON);
    } catch (final JSONException e) {
      return null;
    }
    final JSONArray allowList = json.optJSONArray("allowCredentials");
    bundle.putBoolean(
        BUNDLE_KEY_IS_AUTO_SELECT_ALLOWED, allowList != null && allowList.length() > 0);

    bundle.putString(BUNDLE_KEY_SUBTYPE, BUNDLE_VALUE_SUBTYPE_GET_PUBLIC_KEY_CREDENTIAL_OPTION);
    return bundle;
  }

  @UiThread
  @SuppressLint("MissingPermission")
  public static GeckoResult<WebAuthnUtils.MakeCredentialResponse> makeCredential(
      final String origin, final byte[] clientDataHash, final String requestJSON) {
    ThreadUtils.assertOnUiThread();

    // We use Credential Manager first. If it doesn't work, we use GMS FIDO2.
    // Credential manager may support non-discoverable keys,
    // Else, following the specifications, `residentKey=discouraged` allows discoverable keys too
    // but prefer non-discoverable keys
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
      return GeckoResult.fromException(new WebAuthnUtils.Exception("NOT_SUPPORTED_ERR"));
    }
    final Context context = GeckoAppShell.getApplicationContext();
    // Some vendors disabled Credential Manager on Android 14+ devices.
    // https://issuetracker.google.com/issues/349310440
    if (!context.getPackageManager().hasSystemFeature(PackageManager.FEATURE_CREDENTIALS)) {
      Log.w(LOGTAG, "Credential Manager is disabled on this device");
      return GeckoResult.fromException(new WebAuthnUtils.Exception("NOT_SUPPORTED_ERR"));
    }

    final Bundle requestBundle = getRequestBundleForMakeCredential(clientDataHash, requestJSON);
    if (requestBundle == null) {
      return GeckoResult.fromException(new WebAuthnUtils.Exception("UNKNOWN_ERR"));
    }

    requestBundle.putBoolean(BUNDLE_KEY_PREFER_IMMEDIATELY_AVAILABLE_CREDENTIALS, false);
    requestBundle.putBoolean(BUNDLE_KEY_IS_AUTO_SELECT_ALLOWED, false);

    final CreateCredentialRequest request =
        new CreateCredentialRequest.Builder(
                TYPE_PUBLIC_KEY_CREDENTIAL, requestBundle, requestBundle)
            .setAlwaysSendAppInfoToProvider(true)
            .setOrigin(origin)
            .build();

    final CredentialManager manager =
        (CredentialManager) context.getSystemService(Context.CREDENTIAL_SERVICE);
    if (manager == null) {
      return GeckoResult.fromException(new WebAuthnUtils.Exception("UNKNOWN_ERR"));
    }
    final GeckoResult<WebAuthnUtils.MakeCredentialResponse> result = new GeckoResult<>();
    final AtomicBoolean completed = new AtomicBoolean();
    try {
      manager.createCredential(
          context,
          request,
          null,
          context.getMainExecutor(),
          new OutcomeReceiver<CreateCredentialResponse, CreateCredentialException>() {
            @Override
            public void onResult(final CreateCredentialResponse createCredentialResponse) {
              if (!completed.compareAndSet(false, true)) {
                return;
              }
              final Bundle data = createCredentialResponse.getData();
              final String responseJson = data.getString(BUNDLE_KEY_REGISTRATION_RESPONSE_JSON);
              if (responseJson == null) {
                result.completeExceptionally(new WebAuthnUtils.Exception("DATA_ERR"));
                return;
              }
              if (DEBUG) {
                Log.d(LOGTAG, "Response JSON: " + responseJson);
              }
              try {
                result.complete(WebAuthnUtils.getMakeCredentialResponse(responseJson));
              } catch (final IllegalArgumentException e) {
                Log.e(LOGTAG, "Invalid response", e);
                result.completeExceptionally(new WebAuthnUtils.Exception("DATA_ERR"));
              } catch (final JSONException e) {
                Log.e(LOGTAG, "Couldn't parse response JSON", e);
                result.completeExceptionally(new WebAuthnUtils.Exception("DATA_ERR"));
              }
            }

            @Override
            public void onError(final CreateCredentialException exception) {
              if (!completed.compareAndSet(false, true)) {
                return;
              }
              final String errorType = exception.getType();
              if (DEBUG) {
                Log.d(LOGTAG, "Couldn't create credential. errorType=" + errorType);
              }
              if (errorType.equals(CreateCredentialException.TYPE_USER_CANCELED)) {
                result.completeExceptionally(new WebAuthnUtils.Exception("ABORT_ERR"));
                return;
              }
              if (errorType.equals(CreateCredentialException.TYPE_NO_CREATE_OPTIONS)) {
                result.completeExceptionally(new WebAuthnUtils.Exception("NOT_SUPPORTED_ERR"));
                return;
              }
              result.completeExceptionally(new WebAuthnUtils.Exception("UNKNOWN_ERR"));
            }
          });
    } catch (final SecurityException e) {
      // We might be no permission for Credential Manager
      return GeckoResult.fromException(new WebAuthnUtils.Exception("NOT_SUPPORTED_ERR"));
    } catch (final java.lang.Exception e) {
      Log.w(LOGTAG, "Couldn't make credential", e);
      return GeckoResult.fromException(new WebAuthnUtils.Exception("UNKNOWN_ERR"));
    }
    return result;
  }

  @UiThread
  @SuppressLint("MissingPermission")
  public static GeckoResult<PrepareGetCredentialResponse.PendingGetCredentialHandle>
      prepareGetAssertion(
          final String origin, final byte[] clientDataHash, final String requestJSON) {
    ThreadUtils.assertOnUiThread();

    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
      // No credential manager. Relay to GMS FIDO2
      return GeckoResult.fromValue(null);
    }
    final Bundle requestBundle = getRequestBundleForGetAssertion(clientDataHash, requestJSON);
    if (requestBundle == null) {
      return GeckoResult.fromValue(null);
    }

    final CredentialOption credentialOption =
        new CredentialOption.Builder(TYPE_PUBLIC_KEY_CREDENTIAL, requestBundle, requestBundle)
            .build();
    final GetCredentialRequest request =
        new GetCredentialRequest.Builder(requestBundle)
            .addCredentialOption(credentialOption)
            .setAlwaysSendAppInfoToProvider(true)
            .setOrigin(origin)
            .build();

    final Context context = GeckoAppShell.getApplicationContext();
    final CredentialManager manager =
        (CredentialManager) context.getSystemService(Context.CREDENTIAL_SERVICE);
    if (manager == null) {
      // No credential manager. Relay to GMS FIDO2
      return GeckoResult.fromValue(null);
    }

    final GeckoResult<PrepareGetCredentialResponse.PendingGetCredentialHandle> result =
        new GeckoResult<>();
    final AtomicBoolean completed = new AtomicBoolean();
    try {
      manager.prepareGetCredential(
          request,
          null,
          context.getMainExecutor(),
          new OutcomeReceiver<PrepareGetCredentialResponse, GetCredentialException>() {
            @Override
            public void onResult(final PrepareGetCredentialResponse prepareGetCredentialResponse) {
              if (!completed.compareAndSet(false, true)) {
                return;
              }
              final boolean hasPublicKeyCredentials =
                  prepareGetCredentialResponse.hasCredentialResults(TYPE_PUBLIC_KEY_CREDENTIAL);
              final boolean hasAuthenticationResults =
                  prepareGetCredentialResponse.hasAuthenticationResults();

              if (DEBUG) {
                Log.d(
                    LOGTAG,
                    "prepareGetCredential: hasPublicKeyCredentials="
                        + hasPublicKeyCredentials
                        + ", hasAuthenticationResults="
                        + hasAuthenticationResults);
              }
              if (!hasPublicKeyCredentials && !hasAuthenticationResults) {
                // No passkey and result. Relay to GMS FIDO2.
                result.complete(null);
                return;
              }

              result.complete(prepareGetCredentialResponse.getPendingGetCredentialHandle());
            }

            @Override
            public void onError(final GetCredentialException exception) {
              if (!completed.compareAndSet(false, true)) {
                return;
              }
              final String errorType = exception.getType();
              if (DEBUG) {
                Log.d(LOGTAG, "Couldn't get credential. errorType=" + errorType);
              }
              if (errorType.equals(GetCredentialException.TYPE_NO_CREDENTIAL)) {
                result.complete(null);
                return;
              }
              result.completeExceptionally(new WebAuthnUtils.Exception("UNKNOWN_ERR"));
            }
          });
    } catch (final SecurityException e) {
      // We might be no permission for Credential Manager. Use FIDO2 API
      return GeckoResult.fromValue(null);
    } catch (final java.lang.Exception e) {
      Log.e(LOGTAG, "prepareGetCredential throws an error", e);
      return GeckoResult.fromValue(null);
    }
    return result;
  }

  @UiThread
  public static GeckoResult<WebAuthnUtils.GetAssertionResponse> getAssertion(
      final PrepareGetCredentialResponse.PendingGetCredentialHandle pendingHandle) {
    ThreadUtils.assertOnUiThread();

    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
      return GeckoResult.fromException(new WebAuthnUtils.Exception("NOT_SUPPORTED_ERR"));
    }
    final GeckoResult<WebAuthnUtils.GetAssertionResponse> result = new GeckoResult<>();
    final AtomicBoolean completed = new AtomicBoolean();
    final Context context = GeckoAppShell.getApplicationContext();
    final CredentialManager manager =
        (CredentialManager) context.getSystemService(Context.CREDENTIAL_SERVICE);
    if (manager == null) {
      return GeckoResult.fromException(new WebAuthnUtils.Exception("UNKNOWN_ERR"));
    }
    try {
      manager.getCredential(
          context,
          pendingHandle,
          null,
          context.getMainExecutor(),
          new OutcomeReceiver<GetCredentialResponse, GetCredentialException>() {
            @Override
            public void onResult(final GetCredentialResponse getCredentialResponse) {
              if (!completed.compareAndSet(false, true)) {
                return;
              }
              final Bundle data = getCredentialResponse.getCredential().getData();
              final String responseJson = data.getString(BUNDLE_KEY_AUTHENTICATION_RESPONSE_JSON);
              if (responseJson == null) {
                result.completeExceptionally(new WebAuthnUtils.Exception("DATA_ERR"));
                return;
              }
              if (DEBUG) {
                Log.d(LOGTAG, "Response JSON: " + responseJson);
              }
              try {
                result.complete(WebAuthnUtils.getGetAssertionResponse(responseJson));
              } catch (final IllegalArgumentException e) {
                Log.e(LOGTAG, "Invalid response", e);
                result.completeExceptionally(new WebAuthnUtils.Exception("DATA_ERR"));
              } catch (final JSONException e) {
                Log.e(LOGTAG, "Couldn't parse response JSON", e);
                result.completeExceptionally(new WebAuthnUtils.Exception("DATA_ERR"));
              }
            }

            @Override
            public void onError(final GetCredentialException exception) {
              if (!completed.compareAndSet(false, true)) {
                return;
              }
              final String errorType = exception.getType();
              if (DEBUG) {
                Log.d(LOGTAG, "Couldn't get credential. errorType=" + errorType);
              }
              if (errorType.equals(GetCredentialException.TYPE_USER_CANCELED)) {
                result.completeExceptionally(new WebAuthnUtils.Exception("ABORT_ERR"));
                return;
              }
              result.completeExceptionally(new WebAuthnUtils.Exception("UNKNOWN_ERR"));
            }
          });
    } catch (final SecurityException e) {
      // We might be no permission for Credential Manager.
      return GeckoResult.fromException(new WebAuthnUtils.Exception("NOT_SUPPORTED_ERR"));
    } catch (final java.lang.Exception e) {
      Log.w(LOGTAG, "Couldn't get assertion", e);
      return GeckoResult.fromException(new WebAuthnUtils.Exception("UNKNOWN_ERR"));
    }
    return result;
  }
}
