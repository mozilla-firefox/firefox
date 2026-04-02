# [Android Components](../../../README.md) > Lib > INTEGRITY-GOOGLEPLAY

> [!NOTE]
> Part of [Mozilla Android Components](https://mozac.org/). This component is only available on devices that have Google Play Services installed.

An [Android Components](https://github.com/mozilla-firefox/firefox/tree/main/mobile/android/android-components) library that wraps the [Google Play Integrity Standard API](https://developer.android.com/google/play/integrity/overview), exposing it through the `concept-integrity` interface so calling code stays fully decoupled from Google Play.

Use it to request a signed attestation token that your backend server can verify to confirm:

- The app binary is genuine and unmodified
- The app was installed from Google Play
- The device passes Android's integrity checks

---

## Contents

- [Background: Why the indirection?](#background-why-the-indirection)
- [Architecture](#architecture)
- [Usage](#usage)
- [Error handling](#error-handling)
- [Testing](#testing)
- [Dependencies](#dependencies)
- [License](#license)

---

## Background: Why the indirection?

The Play Integrity Standard API is **two-phased**, and this shapes the entire module structure.

### Phase 1 — Prepare

You call `StandardIntegrityManager.prepareIntegrityToken()` once, passing your Google Cloud project number. Google pre-warms a `StandardIntegrityTokenProvider` on-device. This involves a network round-trip which

### Phase 2 — Request

Once the provider is ready, you call `provider.request()` as many times as needed. This is cheap because the heavy lifting already happened in Phase 1.

### The complication: provider expiry

The pre-warmed provider **can expire** mid-session. When it does, the API returns error code `INTEGRITY_TOKEN_PROVIDER_INVALID`. At that point Phase 1 must be re-run before Phase 2 can succeed again.

This two-phase lifecycle, combined with testability requirements and the need to model invalid configuration explicitly, drives the layered interface design rather than a single monolithic class.

```
IntegrityManagerProvider
    └─▶ TokenProviderFactory        (runs Phase 1: prepare)
            └─▶ TokenProvider       (runs Phase 2: request)
                    └─▶ IntegrityToken
```

`GooglePlayIntegrityClient` sits above this chain. It caches the `TokenProvider`, re-runs Phase 1 transparently if the provider expires, and retries the failed request once before propagating the error.

### Flow diagram

```
                        Caller
                          │
                    request() / warmUp()
                          │
               ┌──────────▼──────────┐
               │ GooglePlayIntegrity  │
               │       Client        │  caches TokenProvider
               └──────────┬──────────┘  retries on expiry
                          │
           ┌──────────────┴──────────────┐
           │                             │
     warmUp() / Phase 1             request() / Phase 2
           │                             │
  ┌────────▼────────┐          ┌─────────▼────────┐
  │ TokenProvider   │          │  TokenProvider   │
  │    Factory      │          │                  │
  └────────┬────────┘          └─────────┬────────┘
           │                             │
           └────────────┬────────────────┘
                        │
          ┌─────────────▼──────────────┐
          │  Google Play Integrity API  │
          │  (StandardIntegrityManager) │
          │                             │
          │  prepareIntegrityToken()    │
          │  requestToken()             │
          └─────────────┬───────────────┘
                        │
               ┌────────▼────────┐
               │  IntegrityToken │  signed JWT
               │                 │  validate server-side
               └─────────────────┘
 
  On INTEGRITY_TOKEN_PROVIDER_INVALID:
  ┌──────────────────────────────────────────┐
  │  Refresh: re-run Phase 1, retry Phase 2  │──▶ TokenProviderFactory
  └──────────────────────────────────────────┘
```
 
---

## Architecture
### Key types

| Type                        | Role                                                                                                                                                       |
|-----------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `GooglePlayIntegrityClient` | Top-level `IntegrityClient` implementation. Orchestrates warm-up, caching, and transparent refresh on provider expiry.                                     |
| `GoogleProjectNumber`       | Sealed class wrapping parse-time validation of the Google Cloud project number from `BuildConfig`. Models invalid input as `Invalid` rather than throwing. |
| `IntegrityManagerProvider`  | Interface that creates a `StandardIntegrityManager`. The default implementation delegates to `IntegrityManagerFactory.createStandard(context)`.            |
| `TokenProviderFactory`      | Interface that runs Phase 1 (`prepare()`). Returns a `Result<TokenProvider>`. An `Invalid` project number short-circuits this with `InvalidProjectNumber`. |
| `TokenProvider`             | Interface that runs Phase 2 (`request()`). Takes a `RequestHashProvider` for replay-attack protection.                                                     |
| `RequestHashProvider`       | Interface that generates a per-request hash (default: random UUID).                                                                                        |

### Provider expiry handling

```kotlin
// Inside GooglePlayIntegrityClient.request()
warmUp()

val provider = checkNotNull(tokenProvider) {
    "GooglePlayIntegrityClient is missing a token provider"
}.getOrThrow()

return provider.request(requestHashProvider).onFailure {
    if (it.tokenHasExpired) {
        refreshTokenProvider()
        return request()
    }
}
```

The retry is bounded, it recurses exactly once because a freshly prepared provider will not immediately expire.
 
---

## Usage

### 1. Provide your Google Cloud project number

The project number must be configured in your `BuildConfig` (typically via `local.properties` or CI secrets. **Do not commit it**):

```groovy
// build.gradle
android {
    defaultConfig {
        buildConfigField("String", "GOOGLE_CLOUD_PROJECT_NUMBER", "\"${project.findProperty('googleCloudProjectNumber') ?: ''}\"")
    }
}
```

> [!IMPORTANT]
> You must link a Google Cloud project in the [Play Console](https://play.google.com/console) under **Test and release → App integrity → Play Integrity API** before tokens will be issued.

### 2. Construct the client

```kotlin
val integrityClient = GooglePlayIntegrityClient(
    tokenProviderFactory = TokenProviderFactory.create(
        integrityManagerProvider = IntegrityManagerProvider.create(context),
        projectNumber = GoogleProjectNumber.create(BuildConfig.GOOGLE_CLOUD_PROJECT_NUMBER),
    ),
    requestHashProvider = RequestHashProvider.randomHashProvider(),
)
```

### 3. Warm up early (recommended)

Google recommends that we `warmUp()` as early as possible (e.g. on app start or before a user action that will need attestation). This runs Phase 1 in the background so the first `request()` call is fast.

```kotlin
// In your ViewModel or Application class
lifecycleScope.launch {
    integrityClient.warmUp()
}
```

`warmUp()` is idempotent — safe to call multiple times.

### 4. Request a token

```kotlin
lifecycleScope.launch {
    val result = runCatching {
        val token = integrityClient.request().getOrThrow()
        // Send token.value for server-side verification.
        // Never inspect or trust the token on the client.
        myBackend.verify(token)
    }
}
```
 
---

## Error handling

`request()` returns `Result<IntegrityToken>` and never throws. All failure cases are surfaced as typed exceptions in the `Result.failure`:

| Exception                    | When it occurs                                                                                                                                                                                                                |
|------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `InvalidProjectNumber`       | `BuildConfig.GOOGLE_CLOUD_PROJECT_NUMBER` was empty or not parseable as a `Long`. Fails at factory creation, before any network call.                                                                                         |
| `IllegalStateException`      | Defensive case, the internal provider was null when `request()` was called. Should not occur in normal usage.                                                                                                                 |
| `StandardIntegrityException` | Propagated from the Play Integrity SDK after a failed Phase 1 or Phase 2 call. The client automatically handles `INTEGRITY_TOKEN_PROVIDER_INVALID` by refreshing and retrying once. All other error codes are passed through. |

See [Play Integrity error codes](https://developer.android.com/google/play/integrity/reference/com/google/android/play/core/integrity/model/IntegrityErrorCode) for the full list of `StandardIntegrityException` causes.
 
---

## Testing

The `concept-integrity` module provides the `IntegrityClient` interface, allowing you to swap the real implementation for a test double at any layer without touching production code.
 
---

## Dependencies

| Dependency                          | Purpose                                                      |
|-------------------------------------|--------------------------------------------------------------|
| `concept-integrity`                 | `IntegrityClient` and `IntegrityToken` interface definitions |
| `com.google.android.play:integrity` | Google Play Integrity SDK                                    |
 
---

## License

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.