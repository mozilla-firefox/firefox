# Mozilla Ads Client (MAC) — JavaScript API Reference

## Overview

This document covers the full API reference for the `ads_client` component, with all examples and type definitions in JavaScript.
It includes every type and function exposed via UniFFI that is part of the public API surface.

---

## `MozAdsClient`

Top-level client object for requesting ads and recording lifecycle events.

```javascript
// No public fields — use MozAdsClientBuilder to create instances
const client = MozAdsClientBuilder()
    .environment(MozAdsEnvironment.Prod)
    .cacheConfig(cache)
    .telemetry(telemetry)
    .build();
```

#### Creating a Client

Use the `MozAdsClientBuilder` to configure and create the client. The builder provides a fluent API for setting configuration options.

```javascript
const client = MozAdsClientBuilder()
    .environment(MozAdsEnvironment.Prod)
    .cacheConfig(cache)
    .telemetry(telemetry)
    .build();
```

#### Methods

| Method                                                                              | Return Type                               | Description                                                                                                                                                                          |
| ----------------------------------------------------------------------------------- | ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `clearCache()`                                                                      | `void`                                    | Clears the client's HTTP cache. Throws on failure.                                                                                                                                   |
| `recordClick(clickUrl, options?)`                                                   | `void`                                    | Records a click using the provided callback URL (typically from `ad.callbacks.click`). Optional `MozAdsCallbackOptions` can enable OHTTP.                                            |
| `recordImpression(impressionUrl, options?)`                                         | `void`                                    | Records an impression using the provided callback URL (typically from `ad.callbacks.impression`). Optional `MozAdsCallbackOptions` can enable OHTTP.                                 |
| `reportAd(reportUrl, reason, options?)`                                             | `void`                                    | Reports an ad using the provided callback URL (typically from `ad.callbacks.report`). Optional `MozAdsCallbackOptions` can enable OHTTP.                                             |
| `requestImageAds(mozAdRequests, options?)`                                          | `Object.<string, MozAdsImage>`            | Requests one image ad per placement. Optional `MozAdsRequestOptions` can adjust caching behavior. Returns an object keyed by `placementId`.                                          |
| `requestSpocAds(mozAdRequests, options?)`                                           | `Object.<string, Array.<MozAdsSpoc>>`     | Requests spoc ads per placement. Each placement request specifies its own count. Optional `MozAdsRequestOptions` can adjust caching behavior. Returns an object keyed by `placementId`. |
| `requestTileAds(mozAdRequests, options?)`                                           | `Object.<string, MozAdsTile>`             | Requests one tile ad per placement. Optional `MozAdsRequestOptions` can adjust caching behavior. Returns an object keyed by `placementId`.                                           |

> **Notes**
>
> - We recommend that this client be initialized as a singleton or something similar so that multiple instances of the client do not exist at once.
> - Responses omit placements with no fill. Empty placements do not appear in the returned objects.
> - The HTTP cache is internally managed. Configuration can be set with `MozAdsClientBuilder`. Per-request cache settings can be set with `MozAdsRequestOptions`.
> - If `cacheConfig` is `null`, caching is disabled entirely.

---

## `MozAdsClientBuilder`

Builder for configuring and creating the ads client. Use the fluent builder pattern to set configuration options.

```javascript
/**
 * @returns {MozAdsClientBuilder}
 */
MozAdsClientBuilder()

/**
 * @param {MozAdsEnvironment} environment
 * @returns {MozAdsClientBuilder}
 */
builder.environment(environment)

/**
 * @param {MozAdsCacheConfig} cacheConfig
 * @returns {MozAdsClientBuilder}
 */
builder.cacheConfig(cacheConfig)

/**
 * @param {MozAdsTelemetry} telemetry
 * @returns {MozAdsClientBuilder}
 */
builder.telemetry(telemetry)

/**
 * @param {string} userAgent
 * @returns {MozAdsClientBuilder}
 */
builder.userAgent(userAgent)

/**
 * @returns {MozAdsClient}
 */
builder.build()
```

#### Methods

- **`MozAdsClientBuilder()`** - Creates a new builder with default values
- **`environment(environment)`** - Sets the MARS environment (Prod, Staging, or Test)
- **`cacheConfig(cacheConfig)`** - Sets the cache configuration
- **`telemetry(telemetry)`** - Sets the telemetry implementation
- **`userAgent(userAgent)`** - Sets the browser's user agent
- **`build()`** - Builds and returns the configured client

| Configuration  | Type                       | Description                                                                                            |
| -------------- | -------------------------- | ------------------------------------------------------------------------------------------------------ |
| `environment`  | `MozAdsEnvironment`        | Selects which MARS environment to connect to. Unless in a dev build, this value can only ever be Prod. Defaults to Prod. |
| `cacheConfig`  | `MozAdsCacheConfig \| null`| Optional configuration for the internal cache.                                                         |
| `telemetry`    | `MozAdsTelemetry \| null`  | Optional telemetry instance for recording metrics. Telemetry is process-wide: the most recently built client's instance receives metrics from every client.    |
| `userAgent`    | `string \| null`           | The browser's user agent, for OHTTP requests (see the OHTTP note). |

---

## `MozAdsTelemetry`

Telemetry interface for recording ads client metrics. You must provide an implementation of this interface to the `MozAdsClientBuilder` to enable telemetry collection. Telemetry is process-wide and meant to be set up once: building a client with a telemetry instance makes it the destination for metrics from every client in the process, replacing any instance installed before it. It is dropped when any client shuts down. Until an instance is installed, no metrics are recorded.

```javascript
/**
 * @typedef {Object} MozAdsTelemetry
 * @property {function(string, string): void} recordBuildCacheError
 * @property {function(string, string): void} recordClientError
 * @property {function(string): void} recordClientOperationTotal
 * @property {function(string, string): void} recordDeserializationError
 * @property {function(string, string): void} recordHttpCacheOutcome
 */
```

#### Implementation Example

```javascript
class AdsClientTelemetry {
    recordBuildCacheError(label, value) {
        // Bind to your telemetry system
    }

    recordClientError(label, value) {
        // Bind to your telemetry system
    }

    recordClientOperationTotal(label) {
        // Bind to your telemetry system
    }

    recordDeserializationError(label, value) {
        // Bind to your telemetry system
    }

    recordHttpCacheOutcome(label, value) {
        // Bind to your telemetry system
    }
}
```

---

## `MozAdsCacheConfig`

Describes the behavior and location of the on-disk HTTP cache.

```javascript
/**
 * @typedef {Object} MozAdsCacheConfig
 * @property {string} dbPath - Path to the SQLite database file.
 * @property {number|null} defaultCacheTtlSeconds - Default TTL in seconds (default: 300).
 * @property {number|null} maxSizeMib - Maximum cache size in MiB (default: 10).
 */
```

| Field                       | Type             | Description                                                                          |
| --------------------------- | ---------------- | ------------------------------------------------------------------------------------ |
| `dbPath`                    | `string`         | Path to the SQLite database file used for cache storage. Required to enable caching. |
| `defaultCacheTtlSeconds`    | `number \| null` | Default TTL for cached entries. If omitted, defaults to 300 seconds (5 minutes).     |
| `maxSizeMib`                | `number \| null` | Maximum cache size. If omitted, defaults to 10 MiB.                                  |

**Defaults**

- defaultCacheTtlSeconds: 300 seconds (5 min)
- maxSizeMib: 10 MiB

#### Configuration Example

```javascript
const cache = MozAdsCacheConfig({
    dbPath: "/tmp/ads_cache.sqlite",
    defaultCacheTtlSeconds: 600,   // 10 min
    maxSizeMib: 20                 // 20 MiB
});

const telemetry = new AdsClientTelemetry();

const client = MozAdsClientBuilder()
    .environment(MozAdsEnvironment.Prod)
    .cacheConfig(cache)
    .telemetry(telemetry)
    .build();
```

---

## `MozAdsPlacementRequest`

Describes a single ad placement to request from MARS. An array of these is required for the `requestImageAds` and `requestTileAds` methods on the client.

```javascript
/**
 * @typedef {Object} MozAdsPlacementRequest
 * @property {string} placementId - Unique identifier for the ad placement.
 * @property {MozAdsIABContent|null} iabContent - Optional IAB content classification.
 */
```

| Field          | Type                       | Description                                                                     |
| -------------- | -------------------------- | ------------------------------------------------------------------------------- |
| `placementId`  | `string`                   | Unique identifier for the ad placement. Must be unique within one request call. |
| `iabContent`   | `MozAdsIABContent \| null` | Optional IAB content classification for targeting.                              |

**Validation Rules:**

- `placementId` values must be unique per request.

---

## `MozAdsPlacementRequestWithCount`

Describes a single ad placement to request from MARS with a count parameter. An array of these is required for the `requestSpocAds` method on the client.

```javascript
/**
 * @typedef {Object} MozAdsPlacementRequestWithCount
 * @property {number} count - Number of spoc ads to request.
 * @property {string} placementId - Unique identifier for the ad placement.
 * @property {MozAdsIABContent|null} iabContent - Optional IAB content classification.
 */
```

| Field          | Type                       | Description                                                                     |
| -------------- | -------------------------- | ------------------------------------------------------------------------------- |
| `count`        | `number`                   | Number of spoc ads to request for this placement.                               |
| `placementId`  | `string`                   | Unique identifier for the ad placement. Must be unique within one request call. |
| `iabContent`   | `MozAdsIABContent \| null` | Optional IAB content classification for targeting.                              |

**Validation Rules:**

- `placementId` values must be unique per request.

---

## `MozAdsRequestOptions`

Options passed when making a single ad request.

```javascript
/**
 * @typedef {Object} MozAdsRequestOptions
 * @property {MozAdsRequestCachePolicy|null} cachePolicy - Per-request caching policy.
 * @property {Object.<string, boolean>} flags - Request-level flags forwarded as the `flags` object on the wire. An empty object omits it.
 * @property {boolean} ohttp - Whether to route this request through OHTTP (default: false).
 * @property {Object.<string, boolean>} relevance - Per client calculated relevance scores.  An empty object omits it.  Defaults to `{}`.
 */
```

| Field         | Type                                  | Description                                                                                                                                |
| ------------- | ------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `cachePolicy` | `MozAdsRequestCachePolicy \| null`    | Per-request caching policy. If `null`, uses the client's default TTL with a `CacheFirst` mode.                                             |
| `flags`       | `Object.<string, boolean>`            | Request-level flags forwarded verbatim as the `flags` object on the wire. An empty object omits it. e.g. `{ contextual_placement: true }`. Defaults to `{}`. |
| `ohttp`       | `boolean`                             | Whether to route this request through OHTTP. Defaults to `false`.                                                                          |
| `relevance`   | `Object.<string, number>`             | Per client calculated relevance scores.  An empty object omits it.  Defaults to `{}`.

---

## `MozAdsCallbackOptions`

Options passed when making callback requests (click, impression, report).

```javascript
/**
 * @typedef {Object} MozAdsCallbackOptions
 * @property {boolean} ohttp - Whether to route this callback through OHTTP (default: false).
 */
```

| Field   | Type      | Description                                                        |
| ------- | --------- | ------------------------------------------------------------------ |
| `ohttp` | `boolean` | Whether to route this callback through OHTTP. Defaults to `false`. |

#### OHTTP Usage Example

```javascript
// Request ads over OHTTP
const ads = client.requestTileAds(placements, {
    ohttp: true
});

// Record a click over OHTTP
client.recordClick(ad.callbacks.click, { ohttp: true });

// Record an impression over OHTTP
client.recordImpression(ad.callbacks.impression, { ohttp: true });
```

> **Note:** OHTTP must be configured at the viaduct level before use. When `ohttp` is `true`, the client automatically performs a preflight request to obtain geo-location and user-agent headers, which are injected into the MARS request. If the preflight returns no user agent, the client sends `userAgent`, else viaduct's global default user agent, else the request fails.

---

## `MozAdsRequestCachePolicy`

Defines how each request interacts with the cache.

```javascript
/**
 * @typedef {Object} MozAdsRequestCachePolicy
 * @property {MozAdsCacheMode} mode - Strategy for combining cache and network.
 * @property {number|null} ttlSeconds - Optional per-request TTL override in seconds.
 */
```

| Field         | Type              | Description                                                                                                              |
| ------------- | ----------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `mode`        | `MozAdsCacheMode` | Strategy for combining cache and network. Can be `CacheFirst` or `NetworkFirst`.                                         |
| `ttlSeconds`  | `number \| null`  | Optional per-request TTL override in seconds. `null` uses the client default. `0` disables caching for this request.     |

#### Per-Request Cache Policy Override Example

```javascript
// Always fetch from network but only cache for 60 seconds
const options = MozAdsRequestOptions({
    cachePolicy: MozAdsRequestCachePolicy({ mode: MozAdsCacheMode.NetworkFirst, ttlSeconds: 60 })
});

// Use it when requesting ads
const placements = client.requestImageAds(configs, options);
```

---

## `MozAdsCacheMode`

Determines how the cache is used during a request.

```javascript
/**
 * @enum {string}
 */
const MozAdsCacheMode = {
    CacheFirst: "CacheFirst",
    NetworkFirst: "NetworkFirst"
};
```

| Variant        | Behavior                                                                                           |
| -------------- | -------------------------------------------------------------------------------------------------- |
| `CacheFirst`   | Check cache first, return cached response if found, otherwise make a network request and store it. |
| `NetworkFirst` | Always fetch from network, then cache the result.                                                  |

---

## `MozAdsImage`

The image ad creative, callbacks, and metadata provided for each image ad returned from MARS.

```javascript
/**
 * @typedef {Object} MozAdsImage
 * @property {string|null} altText - Alt text if available.
 * @property {string} blockKey - The block key generated for the advertiser.
 * @property {MozAdsCallbacks} callbacks - Lifecycle callback endpoints.
 * @property {string} format - Ad format e.g., "skyscraper".
 * @property {string} imageUrl - Creative asset URL.
 * @property {string} url - Destination URL.
 */
```

| Field       | Type              | Description                                 |
| ----------- | ----------------- | ------------------------------------------- |
| `url`       | `string`          | Destination URL.                            |
| `imageUrl`  | `string`          | Creative asset URL.                         |
| `format`    | `string`          | Ad format e.g., `"skyscraper"`.             |
| `blockKey`  | `string`          | The block key generated for the advertiser. |
| `altText`   | `string \| null`  | Alt text if available.                      |
| `callbacks` | `MozAdsCallbacks` | Lifecycle callback endpoints.               |

---

## `MozAdsSpoc`

The spoc ad creative, callbacks, and metadata provided for each spoc ad returned from MARS.

```javascript
/**
 * @typedef {Object} MozAdsSpoc
 * @property {string} blockKey - The block key generated for the advertiser.
 * @property {MozAdsCallbacks} callbacks - Lifecycle callback endpoints.
 * @property {MozAdsSpocFrequencyCaps} caps - Frequency capping information.
 * @property {string} domain - Domain of the spoc ad.
 * @property {string} excerpt - Spoc ad excerpt/description.
 * @property {string} format - Ad format e.g., "spoc".
 * @property {string} imageUrl - Creative asset URL.
 * @property {MozAdsSpocRanking} ranking - Ranking and personalization information.
 * @property {string} sponsor - Sponsor name.
 * @property {string|null} sponsoredByOverride - Optional override for sponsor name.
 * @property {string} title - Spoc ad title.
 * @property {string} url - Destination URL.
 */
```

| Field                   | Type                      | Description                                 |
| ----------------------- | ------------------------- | ------------------------------------------- |
| `url`                   | `string`                  | Destination URL.                            |
| `imageUrl`              | `string`                  | Creative asset URL.                         |
| `format`                | `string`                  | Ad format e.g., `"spoc"`.                   |
| `blockKey`              | `string`                  | The block key generated for the advertiser. |
| `title`                 | `string`                  | Spoc ad title.                              |
| `excerpt`               | `string`                  | Spoc ad excerpt/description.                |
| `domain`                | `string`                  | Domain of the spoc ad.                      |
| `sponsor`               | `string`                  | Sponsor name.                               |
| `sponsoredByOverride`   | `string \| null`          | Optional override for sponsor name.         |
| `caps`                  | `MozAdsSpocFrequencyCaps` | Frequency capping information.              |
| `ranking`               | `MozAdsSpocRanking`       | Ranking and personalization information.    |
| `callbacks`             | `MozAdsCallbacks`         | Lifecycle callback endpoints.               |

---

## `MozAdsTile`

The tile ad creative, callbacks, and metadata provided for each tile ad returned from MARS.

```javascript
/**
 * @typedef {Object} MozAdsTile
 * @property {string} blockKey - The block key generated for the advertiser.
 * @property {MozAdsCallbacks} callbacks - Lifecycle callback endpoints.
 * @property {string} format - Ad format e.g., "tile".
 * @property {string} imageUrl - Creative asset URL.
 * @property {string} name - Tile ad name.
 * @property {string} url - Destination URL.
 */
```

| Field       | Type              | Description                                 |
| ----------- | ----------------- | ------------------------------------------- |
| `url`       | `string`          | Destination URL.                            |
| `imageUrl`  | `string`          | Creative asset URL.                         |
| `format`    | `string`          | Ad format e.g., `"tile"`.                   |
| `blockKey`  | `string`          | The block key generated for the advertiser. |
| `name`      | `string`          | Tile ad name.                               |
| `callbacks` | `MozAdsCallbacks` | Lifecycle callback endpoints.               |

---

## `MozAdsSpocFrequencyCaps`

Frequency capping information for spoc ads.

```javascript
/**
 * @typedef {Object} MozAdsSpocFrequencyCaps
 * @property {string} capKey - Frequency cap key identifier.
 * @property {number} day - Day number for the frequency cap.
 */
```

| Field    | Type     | Description                       |
| -------- | -------- | --------------------------------- |
| `capKey` | `string` | Frequency cap key identifier.     |
| `day`    | `number` | Day number for the frequency cap. |

---

## `MozAdsSpocRanking`

Ranking and personalization information for spoc ads.

```javascript
/**
 * @typedef {Object} MozAdsSpocRanking
 * @property {number} priority - Priority score for ranking.
 * @property {Object.<string, number>} personalizationModels - Personalization model scores.
 * @property {number} itemScore - Overall item score.
 */
```

| Field                    | Type                      | Description                   |
| ------------------------ | ------------------------- | ----------------------------- |
| `priority`               | `number`                  | Priority score for ranking.   |
| `personalizationModels`  | `Object.<string, number>` | Personalization model scores. |
| `itemScore`              | `number`                  | Overall item score.           |

---

## `MozAdsCallbacks`

```javascript
/**
 * @typedef {Object} MozAdsCallbacks
 * @property {string} click - Click callback URL.
 * @property {string} impression - Impression callback URL.
 * @property {string|null} report - Report callback URL.
 */
```

| Field        | Type             | Description              |
| ------------ | ---------------- | ------------------------ |
| `click`      | `string`         | Click callback URL.      |
| `impression` | `string`         | Impression callback URL. |
| `report`     | `string \| null` | Report callback URL.     |

---

## `MozAdsIABContent`

Provides IAB content classification context for a placement.

```javascript
/**
 * @typedef {Object} MozAdsIABContent
 * @property {MozAdsIABContentTaxonomy} taxonomy - IAB taxonomy version.
 * @property {string[]} categoryIds - One or more IAB category identifiers.
 */
```

| Field          | Type                       | Description                           |
| -------------- | -------------------------- | ------------------------------------- |
| `taxonomy`     | `MozAdsIABContentTaxonomy` | IAB taxonomy version.                 |
| `categoryIds`  | `string[]`                 | One or more IAB category identifiers. |

---

## `MozAdsIABContentTaxonomy`

The [IAB Content Taxonomy](https://www.iab.com/guidelines/content-taxonomy/) version to be used in the request. e.g `IAB-1.0`

```javascript
/**
 * @enum {string}
 */
const MozAdsIABContentTaxonomy = {
    Iab1_0: "Iab1_0",
    Iab2_0: "Iab2_0",
    Iab2_1: "Iab2_1",
    Iab2_2: "Iab2_2",
    Iab3_0: "Iab3_0"
};
```

> Note: The generated UniFFI bindings may use different casing for enum values depending on the JavaScript environment.

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

```javascript
const cache = MozAdsCacheConfig({
    dbPath: "/tmp/ads_cache.sqlite",
    defaultCacheTtlSeconds: 600,   // 10 min
    maxSizeMib: 20                 // 20 MiB
});

const telemetry = new AdsClientTelemetry();

const client = MozAdsClientBuilder()
    .environment(MozAdsEnvironment.Prod)
    .cacheConfig(cache)
    .telemetry(telemetry)
    .build();
```

Where `dbPath` represents the location of the SQLite file. This must be a file that the client has permission to write to.

### Cache Invalidation

**TTL-based expiry (automatic):**

At the start of each send, the cache computes a cutoff from the current time minus the TTL and deletes rows older than that. This is a coarse, global freshness window that bounds how long entries can live.

**Size-based trimming (automatic):**
After storing a cacheable miss, the cache enforces `maxSizeMib` by deleting the oldest rows until the total stored size is at or below the maximum allowed size of the cache. Due to the small size of items in the cache and the relatively short TTL, this behavior should be rare.

**Manual clearing (explicit):**
The cache can be manually cleared by the client using the exposed `client.clearCache()` method. This clears _all_ objects in the cache.
