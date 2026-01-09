# Mozilla Ads Client (MAC) — UniFFI API Reference

## Overview

This document lists the Rust types and functions exposed via UniFFI by the `ads_client` component.
It only includes items that are part of the UniFFI surface. This document is aimed at users of the ads-client who want to know what is available to them.

---

## `MozAdsClient`

Top-level client object for requesting ads and recording lifecycle events.

```rust
pub struct MozAdsClient {
  ... // No public fields
}
```

#### Constructors

```rust
impl MozAdsClient {
  pub fn new(client_config: Option<MozAdsClientConfig>) -> Self
}
```

Creates a new ads client with an optional configuration object.
If a cache configuration is provided, the client will initialize an on-disk HTTP cache at the given path.

#### Methods

| Method                                                                                                                  | Return Type                                            | Description                                                                                                                                                                          |
| ----------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `clear_cache(&self)`                                                                                                    | `AdsClientApiResult<()>`                               | Clears the client's HTTP cache. Returns an error if clearing fails.                                                                                                                  |
| `cycle_context_id(&self)`                                                                                               | `AdsClientApiResult<String>`                           | Rotates the client's context ID and returns the **previous** ID.                                                                                                                     |
| `record_click(&self, click_url: String)`                                                                                | `AdsClientApiResult<()>`                               | Records a click using the provided callback URL (typically from `ad.callbacks.click`).                                                                                               |
| `record_impression(&self, impression_url: String)`                                                                      | `AdsClientApiResult<()>`                               | Records an impression using the provided callback URL (typically from `ad.callbacks.impression`).                                                                                    |
| `report_ad(&self, report_url: String)`                                                                                  | `AdsClientApiResult<()>`                               | Reports an ad using the provided callback URL (typically from `ad.callbacks.report`).                                                                                                |
| `request_image_ads(&self, moz_ad_requests: Vec<MozAdsPlacementRequest>, options: Option<MozAdsRequestOptions>)`         | `AdsClientApiResult<HashMap<String, MozAdsImage>>`     | Requests one image ad per placement. Optional `MozAdsRequestOptions` can adjust caching behavior. Returns a map keyed by `placement_id`.                                             |
| `request_spoc_ads(&self, moz_ad_requests: Vec<MozAdsPlacementRequestWithCount>, options: Option<MozAdsRequestOptions>)` | `AdsClientApiResult<HashMap<String, Vec<MozAdsSpoc>>>` | Requests spoc ads per placement. Each placement request specifies its own count. Optional `MozAdsRequestOptions` can adjust caching behavior. Returns a map keyed by `placement_id`. |
| `request_tile_ads(&self, moz_ad_requests: Vec<MozAdsPlacementRequest>, options: Option<MozAdsRequestOptions>)`          | `AdsClientApiResult<HashMap<String, MozAdsTile>>`      | Requests one tile ad per placement. Optional `MozAdsRequestOptions` can adjust caching behavior. Returns a map keyed by `placement_id`.                                              |

> **Notes**
>
> - We recommend that this client be initialized as a singleton or something similar so that multiple instances of the client do not exist at once.
> - Responses omit placements with no fill. Empty placements do not appear in the returned maps.
> - The HTTP cache is internally managed. Configuration can be set with `MozAdsClientConfig`. Per-request cache settings can be set with `MozAdsRequestOptions`.
> - If `cache_config` is `None`, caching is disabled entirely.

---

## `MozAdsClientConfig`

Configuration for initializing the ads client.

```rust
pub struct MozAdsClientConfig {
  pub environment: Environment,
  pub cache_config: Option<MozAdsCacheConfig>,
  pub telemetry: Option<Arc<dyn MozAdsTelemetry>>,
}
```

| Field          | Type                               | Description                                                                                            |
| -------------- | ---------------------------------- | ------------------------------------------------------------------------------------------------------ |
| `environment`  | `Environment`                      | Selects which MARS environment to connect to. Unless in a dev build, this value can only ever be Prod. |
| `cache_config` | `Option<MozAdsCacheConfig>`        | Optional configuration for the internal cache.                                                         |
| `telemetry`    | `Option<Arc<dyn MozAdsTelemetry>>` | Optional telemetry instance for recording metrics. If not provided, a no-op implementation is used.    |

---

## `MozAdsTelemetry`

Telemetry interface for recording ads client metrics. You must provide an implementation of this interface to the `MozAdsClientConfig` constructor to enable telemetry collection. If no telemetry instance is provided, a no-op implementation is used and no metrics will be recorded.

```rust
pub trait MozAdsTelemetry: Send + Sync {
    fn record_build_cache_error(&self, label: String, value: String);
    fn record_client_error(&self, label: String, value: String);
    fn record_client_operation_total(&self, label: String);
    fn record_deserialization_error(&self, label: String, value: String);
    fn record_http_cache_outcome(&self, label: String, value: String);
}
```

### Implementing Telemetry

To enable telemetry collection, you need to implement the `MozAdsTelemetry` interface and provide an instance to the `MozAdsClientConfig` constructor. The following examples show how to bind Glean metrics to the telemetry interface.

#### Swift Example

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

#### Kotlin Example

```kotlin
import mozilla.appservices.adsclient.MozAdsTelemetry
import org.mozilla.appservices.ads_client.GleanMetrics.AdsClient

class AdsClientTelemetry : MozAdsTelemetry {
    override fun recordBuildCacheError(label: String, value: String) {
        AdsClient.buildCacheError[label].set(value)
    }

    override fun recordClientError(label: String, value: String) {
        AdsClient.clientError[label].set(value)
    }

    override fun recordClientOperationTotal(label: String) {
        AdsClient.clientOperationTotal[label].add()
    }

    override fun recordDeserializationError(label: String, value: String) {
        AdsClient.deserializationError[label].set(value)
    }

    override fun recordHttpCacheOutcome(label: String, value: String) {
        AdsClient.httpCacheOutcome[label].set(value)
    }
}
```

---

## `MozAdsCacheConfig`

Describes the behavior and location of the on-disk HTTP cache.

```rust
pub struct MozAdsCacheConfig {
  pub db_path: String,
  pub default_cache_ttl_seconds: Option<u64>,
  pub max_size_mib: Option<u64>,
}
```

| Field                       | Type          | Description                                                                          |
| --------------------------- | ------------- | ------------------------------------------------------------------------------------ |
| `db_path`                   | `String`      | Path to the SQLite database file used for cache storage. Required to enable caching. |
| `default_cache_ttl_seconds` | `Option<u64>` | Default TTL for cached entries. If omitted, defaults to 300 seconds (5 minutes).     |
| `max_size_mib`              | `Option<u64>` | Maximum cache size. If omitted, defaults to 10 MiB.                                  |

**Defaults**

- default_cache_ttl_seconds: 300 seconds (5 min)
- max_size_mib: 10 MiB

---

## `MozAdsPlacementRequest`

Describes a single ad placement to request from MARS. A vector of these are required for the `request_image_ads` and `request_tile_ads` methods on the client.

```rust
pub struct MozAdsPlacementRequest {
  pub placement_id: String,
  pub iab_content: Option<MozAdsIABContent>,
}
```

| Field          | Type                       | Description                                                                     |
| -------------- | -------------------------- | ------------------------------------------------------------------------------- |
| `placement_id` | `String`                   | Unique identifier for the ad placement. Must be unique within one request call. |
| `iab_content`  | `Option<MozAdsIABContent>` | Optional IAB content classification for targeting.                              |

**Validation Rules:**

- `placement_id` values must be unique per request.

---

## `MozAdsPlacementRequestWithCount`

Describes a single ad placement to request from MARS with a count parameter. A vector of these are required for the `request_spoc_ads` method on the client.

```rust
pub struct MozAdsPlacementRequestWithCount {
  pub count: u32,
  pub placement_id: String,
  pub iab_content: Option<MozAdsIABContent>,
}
```

| Field          | Type                       | Description                                                                     |
| -------------- | -------------------------- | ------------------------------------------------------------------------------- |
| `count`        | `u32`                      | Number of spoc ads to request for this placement.                               |
| `placement_id` | `String`                   | Unique identifier for the ad placement. Must be unique within one request call. |
| `iab_content`  | `Option<MozAdsIABContent>` | Optional IAB content classification for targeting.                              |

**Validation Rules:**

- `placement_id` values must be unique per request.

---

## `MozAdsRequestOptions`

Options passed when making a single ad request.

```rust
pub struct MozAdsRequestOptions {
  pub cache_policy: Option<MozAdsRequestCachePolicy>,
}
```

| Field          | Type                               | Description                                                                                    |
| -------------- | ---------------------------------- | ---------------------------------------------------------------------------------------------- |
| `cache_policy` | `Option<MozAdsRequestCachePolicy>` | Per-request caching policy. If `None`, uses the client's default TTL with a `CacheFirst` mode. |

---

## `MozAdsRequestCachePolicy`

Defines how each request interacts with the cache.

```rust
pub struct MozAdsRequestCachePolicy {
  pub mode: MozAdsCacheMode,
  pub ttl_seconds: Option<u64>,
}
```

| Field         | Type              | Description                                                                                                                |
| ------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------- |
| `mode`        | `MozAdsCacheMode` | Strategy for combining cache and network. Can be `CacheFirst` or `NetworkFirst`.                                           |
| `ttl_seconds` | `Option<u64>`     | Optional per-request TTL override in seconds. `None` uses the client default. `Some(0)` disables caching for this request. |

---

## `MozAdsCacheMode`

Determines how the cache is used during a request.

```rust
pub enum MozAdsCacheMode {
  CacheFirst,
  NetworkFirst,
}
```

| Variant        | Behavior                                                                                           |
| -------------- | -------------------------------------------------------------------------------------------------- |
| `CacheFirst`   | Check cache first, return cached response if found, otherwise make a network request and store it. |
| `NetworkFirst` | Always fetch from network, then cache the result.                                                  |

---

## `MozAdsImage`

The image ad creative, callbacks, and metadata provided for each image ad returned from MARS.

```rust
pub struct MozAdsImage {
  pub alt_text: Option<String>,
  pub block_key: String,
  pub callbacks: MozAdsCallbacks,
  pub format: String,
  pub image_url: String,
  pub url: String,
}
```

| Field       | Type              | Description                                 |
| ----------- | ----------------- | ------------------------------------------- |
| `url`       | `String`          | Destination URL.                            |
| `image_url` | `String`          | Creative asset URL.                         |
| `format`    | `String`          | Ad format e.g., `"skyscraper"`.             |
| `block_key` | `String`          | The block key generated for the advertiser. |
| `alt_text`  | `Option<String>`  | Alt text if available.                      |
| `callbacks` | `MozAdsCallbacks` | Lifecycle callback endpoints.               |

---

## `MozAdsSpoc`

The spoc ad creative, callbacks, and metadata provided for each spoc ad returned from MARS.

```rust
pub struct MozAdsSpoc {
  pub block_key: String,
  pub callbacks: MozAdsCallbacks,
  pub caps: MozAdsSpocFrequencyCaps,
  pub domain: String,
  pub excerpt: String,
  pub format: String,
  pub image_url: String,
  pub ranking: MozAdsSpocRanking,
  pub sponsor: String,
  pub sponsored_by_override: Option<String>,
  pub title: String,
  pub url: String,
}
```

| Field                   | Type                      | Description                                 |
| ----------------------- | ------------------------- | ------------------------------------------- |
| `url`                   | `String`                  | Destination URL.                            |
| `image_url`             | `String`                  | Creative asset URL.                         |
| `format`                | `String`                  | Ad format e.g., `"spoc"`.                   |
| `block_key`             | `String`                  | The block key generated for the advertiser. |
| `title`                 | `String`                  | Spoc ad title.                              |
| `excerpt`               | `String`                  | Spoc ad excerpt/description.                |
| `domain`                | `String`                  | Domain of the spoc ad.                      |
| `sponsor`               | `String`                  | Sponsor name.                               |
| `sponsored_by_override` | `Option<String>`          | Optional override for sponsor name.         |
| `caps`                  | `MozAdsSpocFrequencyCaps` | Frequency capping information.              |
| `ranking`               | `MozAdsSpocRanking`       | Ranking and personalization information.    |
| `callbacks`             | `MozAdsCallbacks`         | Lifecycle callback endpoints.               |

---

## `MozAdsTile`

The tile ad creative, callbacks, and metadata provided for each tile ad returned from MARS.

```rust
pub struct MozAdsTile {
  pub block_key: String,
  pub callbacks: MozAdsCallbacks,
  pub format: String,
  pub image_url: String,
  pub name: String,
  pub url: String,
}
```

| Field       | Type              | Description                                 |
| ----------- | ----------------- | ------------------------------------------- |
| `url`       | `String`          | Destination URL.                            |
| `image_url` | `String`          | Creative asset URL.                         |
| `format`    | `String`          | Ad format e.g., `"tile"`.                   |
| `block_key` | `String`          | The block key generated for the advertiser. |
| `name`      | `String`          | Tile ad name.                               |
| `callbacks` | `MozAdsCallbacks` | Lifecycle callback endpoints.               |

---

## `MozAdsSpocFrequencyCaps`

Frequency capping information for spoc ads.

```rust
pub struct MozAdsSpocFrequencyCaps {
  pub cap_key: String,
  pub day: u32,
}
```

| Field     | Type     | Description                       |
| --------- | -------- | --------------------------------- |
| `cap_key` | `String` | Frequency cap key identifier.     |
| `day`     | `u32`    | Day number for the frequency cap. |

---

## `MozAdsSpocRanking`

Ranking and personalization information for spoc ads.

```rust
pub struct MozAdsSpocRanking {
  pub priority: u32,
  pub personalization_models: HashMap<String, u32>,
  pub item_score: f64,
}
```

| Field                    | Type                   | Description                   |
| ------------------------ | ---------------------- | ----------------------------- |
| `priority`               | `u32`                  | Priority score for ranking.   |
| `personalization_models` | `HashMap<String, u32>` | Personalization model scores. |
| `item_score`             | `f64`                  | Overall item score.           |

---

## `MozAdsCallbacks`

```rust
pub struct MozAdsCallbacks {
  pub click: Url,
  pub impression: Url,
  pub report: Option<Url>,
}
```

| Field        | Type          | Description              |
| ------------ | ------------- | ------------------------ |
| `click`      | `Url`         | Click callback URL.      |
| `impression` | `Url`         | Impression callback URL. |
| `report`     | `Option<Url>` | Report callback URL.     |

---

## `MozAdsIABContent`

Provides IAB content classification context for a placement.

```rust
pub struct MozAdsIABContent {
  pub taxonomy: MozAdsIABContentTaxonomy,
  pub category_ids: Vec<String>,
}
```

| Field          | Type                       | Description                           |
| -------------- | -------------------------- | ------------------------------------- |
| `taxonomy`     | `MozAdsIABContentTaxonomy` | IAB taxonomy version.                 |
| `category_ids` | `Vec<String>`              | One or more IAB category identifiers. |

---

## `MozAdsIABContentTaxonomy`

The [IAB Content Taxonomy](https://www.iab.com/guidelines/content-taxonomy/) version to be used in the request. e.g `IAB-1.0`

```rust
pub enum MozAdsIABContentTaxonomy {
  IAB1_0,
  IAB2_0,
  IAB2_1,
  IAB2_2,
  IAB3_0,
}
```

> Note: The generated native bindings for the values may look different depending on the language (snake-case, camel case, etc.) as a result of UniFFI's formatting.

---

## Internal Cache Behavior

### Cache Overview

The internal HTTP cache is a SQLite-backed key-value store layered over viaduct::Request::send().
It reduces redundant network traffic and improves latency for repeated or identical ad requests.

### Cache Lifecycle

Each network response can be stored in the cache with an associated effective TTL, computed as:

```rust
effective_ttl = min(server_max_age, client_default_ttl, per_request_ttl)
```

where:

- `server_max_age` comes from the HTTP Cache-Control: max-age=N header (if present),
- `client_default_ttl` is set in `MozAdsCacheConfig`,
- `per_request_ttl` is an optional override set in `MozAdsRequestCachePolicy`.

If the effective TTL resolves to 0 seconds, the response is not cached.

### Configuring The Cache

#### Example Client Configuration

```swift
// Swift example
let cache = MozAdsCacheConfig(
    dbPath: "/tmp/ads_cache.sqlite",
    defaultCacheTtlSeconds: 600,   // 10 min
    maxSizeMib: 20                 // 20 MiB
)

let telemetry = AdsClientTelemetry()
let clientCfg = MozAdsClientConfig(
    environment: .prod,
    cacheConfig: cache,
    telemetry: telemetry
)

let client = MozAdsClient(clientConfig: clientCfg)
```

```kotlin
// Kotlin example
val cache = MozAdsCacheConfig(
    dbPath = "/tmp/ads_cache.sqlite",
    defaultCacheTtlSeconds = 600L,   // 10 min
    maxSizeMib = 20L                 // 20 MiB
)

val telemetry = AdsClientTelemetry()
val clientCfg = MozAdsClientConfig(
    environment = MozAdsEnvironment.PROD,
    cacheConfig = cache,
    telemetry = telemetry
)

val client = MozAdsClient(clientCfg)
```

Where `db_path` represents the location of the SQLite file. This must be a file that the client has permission to write to.

#### Example Request Policy Override

Assuming you have at least initialized the client with a `db_path`, individual requests can override caching behavior. However, recall the minimum TTL is always respected. So this override will only provide a new ttl floor.

```rust
// Always fetch from network but only cache for 60 seconds
let options = MozAdsRequestOptions(
    cachePolicy: MozAdsRequestCachePolicy(mode: .networkFirst, ttlSeconds: 60)
)

// Use it when requesting ads
let placements = client.requestImageAds(configs, options: options)
```

### Cache Invalidation

**TTL-based expiry (automatic):**

At the start of each send, the cache computes a cutoff from chrono::Utc::now() - ttl and deletes rows older than that. This is a coarse, global freshness window that bounds how long entries can live.

**Size-based trimming (automatic):**
After storing a cacheable miss, the cache enforces max_size by deleting the oldest rows until the total stored size is ≤ the maximum allowed size of the cache. Due to the small size of items in the cache and the relatively short TTL, this behavior should be rare.

**Manual clearing (explicit):**
The cache can be manually cleared by the client using the exposed `client.clear_cache()` method. This clears _all_ objects in the cache.

---

### Example Usage

Under construction
