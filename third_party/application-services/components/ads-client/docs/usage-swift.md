# Mozilla Ads Client (MAC) — Swift API Reference

## Overview

This document covers the full API reference for the `ads_client` component, with all examples and type definitions in Swift.
It includes every type and function exposed via UniFFI that is part of the public API surface.

---

## `MozAdsClient`

Top-level client object for requesting ads and recording lifecycle events.

```swift
class MozAdsClient {
    // No public properties — use MozAdsClientBuilder to create instances
}
```

#### Creating a Client

Use the `MozAdsClientBuilder` to configure and create the client. The builder provides a fluent API for setting configuration options.

```swift
let client = MozAdsClientBuilder()
    .environment(environment: .prod)
    .cacheConfig(cacheConfig: cache)
    .telemetry(telemetry: telemetry)
    .build()
```

#### Methods

| Method                                                                                                                       | Return Type                        | Description                                                                                                                                                                          |
| ---------------------------------------------------------------------------------------------------------------------------- | ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `clearCache()`                                                                                                               | `Void`                             | Clears the client's HTTP cache. Throws on failure.                                                                                                                                   |
| `recordClick(clickUrl: String, options: MozAdsCallbackOptions?)`                                                                                              | `Void`                             | Records a click using the provided callback URL (typically from `ad.callbacks.click`).                                                                                               |
| `recordImpression(impressionUrl: String, options: MozAdsCallbackOptions?)`                                                                                    | `Void`                             | Records an impression using the provided callback URL (typically from `ad.callbacks.impression`).                                                                                    |
| `reportAd(reportUrl: String, reason: MozAdsReportReason, options: MozAdsCallbackOptions?)`                                                                                                | `Void`                             | Reports an ad using the provided callback URL (typically from `ad.callbacks.report`).                                                                                                |
| `requestImageAds(mozAdRequests: [MozAdsPlacementRequest], options: MozAdsRequestOptions?)`                                   | `[String: MozAdsImage]`            | Requests one image ad per placement. Optional `MozAdsRequestOptions` can adjust caching behavior. Returns a dictionary keyed by `placementId`.                                       |
| `requestSpocAds(mozAdRequests: [MozAdsPlacementRequestWithCount], options: MozAdsRequestOptions?)`                           | `[String: [MozAdsSpoc]]`           | Requests spoc ads per placement. Each placement request specifies its own count. Optional `MozAdsRequestOptions` can adjust caching behavior. Returns a dictionary keyed by `placementId`. |
| `requestTileAds(mozAdRequests: [MozAdsPlacementRequest], options: MozAdsRequestOptions?)`                                    | `[String: MozAdsTile]`             | Requests one tile ad per placement. Optional `MozAdsRequestOptions` can adjust caching behavior. Returns a dictionary keyed by `placementId`.                                        |

> **Notes**
>
> - We recommend that this client be initialized as a singleton or something similar so that multiple instances of the client do not exist at once.
> - Responses omit placements with no fill. Empty placements do not appear in the returned dictionaries.
> - The HTTP cache is internally managed. Configuration can be set with `MozAdsClientBuilder`. Per-request cache settings can be set with `MozAdsRequestOptions`.
> - If `cacheConfig` is `nil`, caching is disabled entirely.

---

## `MozAdsClientBuilder`

Builder for configuring and creating the ads client. Use the fluent builder pattern to set configuration options.

```swift
class MozAdsClientBuilder {
    func environment(environment: MozAdsEnvironment) -> MozAdsClientBuilder
    func cacheConfig(cacheConfig: MozAdsCacheConfig) -> MozAdsClientBuilder
    func telemetry(telemetry: MozAdsTelemetry) -> MozAdsClientBuilder
    func userAgent(userAgent: String) -> MozAdsClientBuilder
    func build() -> MozAdsClient
}
```

#### Methods

- **`MozAdsClientBuilder()`** - Creates a new builder with default values
- **`environment(environment: MozAdsEnvironment)`** - Sets the MARS environment (Prod, Staging, or Test)
- **`cacheConfig(cacheConfig: MozAdsCacheConfig)`** - Sets the cache configuration
- **`telemetry(telemetry: MozAdsTelemetry)`** - Sets the telemetry implementation
- **`userAgent(userAgent: String)`** - Sets the browser's user agent
- **`build()`** - Builds and returns the configured client

| Configuration  | Type                  | Description                                                                                            |
| -------------- | --------------------- | ------------------------------------------------------------------------------------------------------ |
| `environment`  | `MozAdsEnvironment`   | Selects which MARS environment to connect to. Unless in a dev build, this value can only ever be Prod. Defaults to Prod. |
| `cacheConfig`  | `MozAdsCacheConfig?`  | Optional configuration for the internal cache.                                                         |
| `telemetry`    | `MozAdsTelemetry?`    | Optional telemetry instance for recording metrics. Telemetry is process-wide: the most recently built client's instance receives metrics from every client.    |
| `userAgent`    | `String?`             | The browser's user agent, for OHTTP requests (see the OHTTP note). |

---

## `MozAdsTelemetry`

Telemetry protocol for recording ads client metrics. You must provide an implementation of this protocol to the `MozAdsClientBuilder` to enable telemetry collection. Telemetry is process-wide and meant to be set up once: building a client with a telemetry instance makes it the destination for metrics from every client in the process, replacing any instance installed before it. It is dropped when any client shuts down. Until an instance is installed, no metrics are recorded.

```swift
protocol MozAdsTelemetry {
    func recordBuildCacheError(label: String, value: String)
    func recordClientError(label: String, value: String)
    func recordClientOperationTotal(label: String)
    func recordDeserializationError(label: String, value: String)
    func recordHttpCacheOutcome(label: String, value: String)
}
```

#### Implementation Example

```swift
import MozillaRustComponents
import Glean

public final class AdsClientTelemetry: MozAdsTelemetry {
    public func recordBuildCacheError(label: String, value: String) {
        AdsClientMetrics.buildCacheError[label].set(value)
    }

    public func recordClientError(label: String, value: String) {
        AdsClientMetrics.clientError[label].set(value)
    }

    public func recordClientOperationTotal(label: String) {
        AdsClientMetrics.clientOperationTotal[label].add()
    }

    public func recordDeserializationError(label: String, value: String) {
        AdsClientMetrics.deserializationError[label].set(value)
    }

    public func recordHttpCacheOutcome(label: String, value: String) {
        AdsClientMetrics.httpCacheOutcome[label].set(value)
    }
}
```

---

## `MozAdsCacheConfig`

Describes the behavior and location of the on-disk HTTP cache.

```swift
struct MozAdsCacheConfig {
    let dbPath: String
    let defaultCacheTtlSeconds: UInt64?
    let maxSizeMib: UInt64?
}
```

| Field                       | Type      | Description                                                                          |
| --------------------------- | --------- | ------------------------------------------------------------------------------------ |
| `dbPath`                    | `String`  | Path to the SQLite database file used for cache storage. Required to enable caching. |
| `defaultCacheTtlSeconds`    | `UInt64?` | Default TTL for cached entries. If omitted, defaults to 300 seconds (5 minutes).     |
| `maxSizeMib`                | `UInt64?` | Maximum cache size. If omitted, defaults to 10 MiB.                                  |

**Defaults**

- defaultCacheTtlSeconds: 300 seconds (5 min)
- maxSizeMib: 10 MiB

#### Configuration Example

```swift
let cache = MozAdsCacheConfig(
    dbPath: "/tmp/ads_cache.sqlite",
    defaultCacheTtlSeconds: 600,   // 10 min
    maxSizeMib: 20                 // 20 MiB
)

let telemetry = AdsClientTelemetry()

let client = MozAdsClientBuilder()
    .environment(environment: .prod)
    .cacheConfig(cacheConfig: cache)
    .telemetry(telemetry: telemetry)
    .build()
```

---

## `MozAdsPlacementRequest`

Describes a single ad placement to request from MARS. An array of these is required for the `requestImageAds` and `requestTileAds` methods on the client.

```swift
struct MozAdsPlacementRequest {
    let placementId: String
    let iabContent: MozAdsIABContent?
}
```

| Field          | Type                | Description                                                                     |
| -------------- | ------------------- | ------------------------------------------------------------------------------- |
| `placementId`  | `String`            | Unique identifier for the ad placement. Must be unique within one request call. |
| `iabContent`   | `MozAdsIABContent?` | Optional IAB content classification for targeting.                              |

**Validation Rules:**

- `placementId` values must be unique per request.

---

## `MozAdsPlacementRequestWithCount`

Describes a single ad placement to request from MARS with a count parameter. An array of these is required for the `requestSpocAds` method on the client.

```swift
struct MozAdsPlacementRequestWithCount {
    let count: UInt32
    let placementId: String
    let iabContent: MozAdsIABContent?
}
```

| Field          | Type                | Description                                                                     |
| -------------- | ------------------- | ------------------------------------------------------------------------------- |
| `count`        | `UInt32`            | Number of spoc ads to request for this placement.                               |
| `placementId`  | `String`            | Unique identifier for the ad placement. Must be unique within one request call. |
| `iabContent`   | `MozAdsIABContent?` | Optional IAB content classification for targeting.                              |

**Validation Rules:**

- `placementId` values must be unique per request.

---

## `MozAdsRequestOptions`

Options passed when making a single ad request.

```swift
struct MozAdsRequestOptions {
    let cachePolicy: MozAdsRequestCachePolicy?
    let flags: [String: Bool]  // default: [:]
    let ohttp: Bool  // default: false
    let relevance: [String: Double] // default: [:]
}
```

| Field         | Type                          | Description                                                                                                                                |
| ------------- | ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `cachePolicy` | `MozAdsRequestCachePolicy?`   | Per-request caching policy. If `nil`, uses the client's default TTL with a `cacheFirst` mode.                                              |
| `flags`       | `[String: Bool]`              | Request-level flags forwarded verbatim as the `flags` object on the wire. An empty dictionary omits it. e.g. `["contextual_placement": true]`. Defaults to `[:]`. |
| `ohttp`       | `Bool`                        | Whether to route this request through OHTTP. Defaults to `false`.                                                                          |
| `relevance`   | `[String: Double]`            | Per client calculated relevance scores. An empty object omits it.Defaults to `[:]`.                                                          |

---

## `MozAdsCallbackOptions`

Options passed when making callback requests (click, impression, report).

```swift
struct MozAdsCallbackOptions {
    let ohttp: Bool  // default: false
}
```

| Field   | Type   | Description                                                        |
| ------- | ------ | ------------------------------------------------------------------ |
| `ohttp` | `Bool` | Whether to route this callback through OHTTP. Defaults to `false`. |

#### OHTTP Usage Example

```swift
// Request ads over OHTTP
let ads = try client.requestTileAds(mozAdRequests: placements, options: MozAdsRequestOptions(ohttp: true))

// Record a click over OHTTP
try client.recordClick(clickUrl: ad.callbacks.click, options: MozAdsCallbackOptions(ohttp: true))

// Record an impression over OHTTP
try client.recordImpression(impressionUrl: ad.callbacks.impression, options: MozAdsCallbackOptions(ohttp: true))
```

> **Note:** OHTTP must be configured at the viaduct level before use. When `ohttp` is `true`, the client automatically performs a preflight request to obtain geo-location and user-agent headers, which are injected into the MARS request. If the preflight returns no user agent, the client sends `userAgent`, else viaduct's global default user agent, else the request fails.

---

## `MozAdsRequestCachePolicy`

Defines how each request interacts with the cache.

```swift
struct MozAdsRequestCachePolicy {
    let mode: MozAdsCacheMode
    let ttlSeconds: UInt64?
}
```

| Field         | Type              | Description                                                                                                            |
| ------------- | ----------------- | ---------------------------------------------------------------------------------------------------------------------- |
| `mode`        | `MozAdsCacheMode` | Strategy for combining cache and network. Can be `.cacheFirst` or `.networkFirst`.                                     |
| `ttlSeconds`  | `UInt64?`         | Optional per-request TTL override in seconds. `nil` uses the client default. `0` disables caching for this request.    |

#### Per-Request Cache Policy Override Example

```swift
// Always fetch from network but only cache for 60 seconds
let options = MozAdsRequestOptions(
    cachePolicy: MozAdsRequestCachePolicy(mode: .networkFirst, ttlSeconds: 60)
)

// Use it when requesting ads
let placements = client.requestImageAds(configs, options: options)
```

---

## `MozAdsCacheMode`

Determines how the cache is used during a request.

```swift
enum MozAdsCacheMode {
    case cacheFirst
    case networkFirst
}
```

| Variant        | Behavior                                                                                           |
| -------------- | -------------------------------------------------------------------------------------------------- |
| `.cacheFirst`  | Check cache first, return cached response if found, otherwise make a network request and store it. |
| `.networkFirst`| Always fetch from network, then cache the result.                                                  |

---

## `MozAdsImage`

The image ad creative, callbacks, and metadata provided for each image ad returned from MARS.

```swift
struct MozAdsImage {
    let altText: String?
    let blockKey: String
    let callbacks: MozAdsCallbacks
    let format: String
    let imageUrl: String
    let url: String
}
```

| Field       | Type              | Description                                 |
| ----------- | ----------------- | ------------------------------------------- |
| `url`       | `String`          | Destination URL.                            |
| `imageUrl`  | `String`          | Creative asset URL.                         |
| `format`    | `String`          | Ad format e.g., `"skyscraper"`.             |
| `blockKey`  | `String`          | The block key generated for the advertiser. |
| `altText`   | `String?`         | Alt text if available.                      |
| `callbacks` | `MozAdsCallbacks` | Lifecycle callback endpoints.               |

---

## `MozAdsSpoc`

The spoc ad creative, callbacks, and metadata provided for each spoc ad returned from MARS.

```swift
struct MozAdsSpoc {
    let blockKey: String
    let callbacks: MozAdsCallbacks
    let caps: MozAdsSpocFrequencyCaps
    let domain: String
    let excerpt: String
    let format: String
    let imageUrl: String
    let ranking: MozAdsSpocRanking
    let sponsor: String
    let sponsoredByOverride: String?
    let title: String
    let url: String
}
```

| Field                   | Type                      | Description                                 |
| ----------------------- | ------------------------- | ------------------------------------------- |
| `url`                   | `String`                  | Destination URL.                            |
| `imageUrl`              | `String`                  | Creative asset URL.                         |
| `format`                | `String`                  | Ad format e.g., `"spoc"`.                   |
| `blockKey`              | `String`                  | The block key generated for the advertiser. |
| `title`                 | `String`                  | Spoc ad title.                              |
| `excerpt`               | `String`                  | Spoc ad excerpt/description.                |
| `domain`                | `String`                  | Domain of the spoc ad.                      |
| `sponsor`               | `String`                  | Sponsor name.                               |
| `sponsoredByOverride`   | `String?`                 | Optional override for sponsor name.         |
| `caps`                  | `MozAdsSpocFrequencyCaps` | Frequency capping information.              |
| `ranking`               | `MozAdsSpocRanking`       | Ranking and personalization information.    |
| `callbacks`             | `MozAdsCallbacks`         | Lifecycle callback endpoints.               |

---

## `MozAdsTile`

The tile ad creative, callbacks, and metadata provided for each tile ad returned from MARS.

```swift
struct MozAdsTile {
    let blockKey: String
    let callbacks: MozAdsCallbacks
    let format: String
    let imageUrl: String
    let name: String
    let url: String
}
```

| Field       | Type              | Description                                 |
| ----------- | ----------------- | ------------------------------------------- |
| `url`       | `String`          | Destination URL.                            |
| `imageUrl`  | `String`          | Creative asset URL.                         |
| `format`    | `String`          | Ad format e.g., `"tile"`.                   |
| `blockKey`  | `String`          | The block key generated for the advertiser. |
| `name`      | `String`          | Tile ad name.                               |
| `callbacks` | `MozAdsCallbacks` | Lifecycle callback endpoints.               |

---

## `MozAdsSpocFrequencyCaps`

Frequency capping information for spoc ads.

```swift
struct MozAdsSpocFrequencyCaps {
    let capKey: String
    let day: UInt32
}
```

| Field    | Type     | Description                       |
| -------- | -------- | --------------------------------- |
| `capKey` | `String` | Frequency cap key identifier.     |
| `day`    | `UInt32` | Day number for the frequency cap. |

---

## `MozAdsSpocRanking`

Ranking and personalization information for spoc ads.

```swift
struct MozAdsSpocRanking {
    let priority: UInt32
    let personalizationModels: [String: UInt32]
    let itemScore: Double
}
```

| Field                    | Type               | Description                   |
| ------------------------ | ------------------ | ----------------------------- |
| `priority`               | `UInt32`           | Priority score for ranking.   |
| `personalizationModels`  | `[String: UInt32]` | Personalization model scores. |
| `itemScore`              | `Double`           | Overall item score.           |

---

## `MozAdsCallbacks`

```swift
struct MozAdsCallbacks {
    let click: String
    let impression: String
    let report: String?
}
```

| Field        | Type      | Description              |
| ------------ | --------- | ------------------------ |
| `click`      | `String`  | Click callback URL.      |
| `impression` | `String`  | Impression callback URL. |
| `report`     | `String?` | Report callback URL.     |

---

## `MozAdsIABContent`

Provides IAB content classification context for a placement.

```swift
struct MozAdsIABContent {
    let taxonomy: MozAdsIABContentTaxonomy
    let categoryIds: [String]
}
```

| Field          | Type                       | Description                           |
| -------------- | -------------------------- | ------------------------------------- |
| `taxonomy`     | `MozAdsIABContentTaxonomy` | IAB taxonomy version.                 |
| `categoryIds`  | `[String]`                 | One or more IAB category identifiers. |

---

## `MozAdsIABContentTaxonomy`

The [IAB Content Taxonomy](https://www.iab.com/guidelines/content-taxonomy/) version to be used in the request. e.g `IAB-1.0`

```swift
enum MozAdsIABContentTaxonomy {
    case iab1_0
    case iab2_0
    case iab2_1
    case iab2_2
    case iab3_0
}
```

> Note: The generated UniFFI bindings use lower camel-case for enum cases in Swift.

---

## Internal Cache Behavior

### Cache Overview

The internal HTTP cache is a SQLite-backed key-value store layered over the HTTP request layer.
It reduces redundant network traffic and improves latency for repeated or identical ad requests.

### Cache Lifecycle

Each network response can be stored in the cache with an associated effective TTL,
resolved by priority (highest to lowest):

1. `per_request_ttl` — caller-provided override on `MozAdsRequestCachePolicy`.
2. `server_max_age` — value of the HTTP `Cache-Control: max-age=N` header on the response.
3. `client_default_ttl` — configured on `MozAdsCacheConfig`.

If the effective TTL resolves to 0 seconds (e.g. `Cache-Control: max-age=0`),
the response is not cached. The resolved TTL is capped at 7 days regardless
of source.

### Configuring The Cache

```swift
let cache = MozAdsCacheConfig(
    dbPath: "/tmp/ads_cache.sqlite",
    defaultCacheTtlSeconds: 600,   // 10 min
    maxSizeMib: 20                 // 20 MiB
)

let telemetry = AdsClientTelemetry()

let client = MozAdsClientBuilder()
    .environment(environment: .prod)
    .cacheConfig(cacheConfig: cache)
    .telemetry(telemetry: telemetry)
    .build()
```

Where `dbPath` represents the location of the SQLite file. This must be a file that the client has permission to write to.

### Cache Invalidation

**TTL-based expiry (automatic):**

At the start of each send, the cache computes a cutoff from the current time minus the TTL and deletes rows older than that. This is a coarse, global freshness window that bounds how long entries can live.

**Size-based trimming (automatic):**
After storing a cacheable miss, the cache enforces `maxSizeMib` by deleting the oldest rows until the total stored size is at or below the maximum allowed size of the cache. Due to the small size of items in the cache and the relatively short TTL, this behavior should be rare.

**Manual clearing (explicit):**
The cache can be manually cleared by the client using the exposed `client.clearCache()` method. This clears _all_ objects in the cache.
