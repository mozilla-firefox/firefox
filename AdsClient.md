# AdsClient in newtab

## 1. Add ads-client crate to dependencies

- See `docs/rust-components/developing-rust-components/uniffi.md`
- `./mach vendor run` to update the rust code
- `./mach uniffi generate` to generate the bindings
- Update `toolkit/components/uniffi-bindgen-gecko-js/config.toml` to add the new bindings wrappers:
```toml
[ads_client.async_wrappers]
MozAdsClient = "AsyncWrapped"
"MozAdsClient.new" = "Sync"
"MozAdsClient.clear_cache" = "Sync"
"MozAdsClient.cycle_context_id" = "Sync"
"MozAdsClient.record_click" = "AsyncWrapped"
"MozAdsClient.record_impression" = "AsyncWrapped"
"MozAdsClient.report_ad" = "AsyncWrapped"
"MozAdsClient.request_image_ads" = "AsyncWrapped"
"MozAdsClient.request_spoc_ads" = "AsyncWrapped"
"MozAdsClient.request_tile_ads" = "AsyncWrapped"
"MozAdsTelemetry.record_build_cache_error" = "Sync"
"MozAdsTelemetry.record_client_error" = "Sync"
"MozAdsTelemetry.record_client_operation_total" = "Sync"
"MozAdsTelemetry.record_deserialization_error" = "Sync"
"MozAdsTelemetry.record_http_cache_outcome" = "Sync"
```
- `./mach build` to build the rust code
- `./mach run` to run the newtab

## 2. Create the AdsClient singleton

Import the types from the generated bindings:
```js
import { MozAdsClient, MozAdsClientConfig, MozAdsCacheConfig, MozAdsEnvironment, MozAdsTelemetry } from "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs";
```

Create the AdsClient singleton:
```js
const AdsClient = MozAdsClient.init(new MozAdsClientConfig({
  cacheConfig: new MozAdsCacheConfig({
    dbPath: "ads_cache.db",
  }),
  environment: MozAdsEnvironment.PROD,
  telemetry: new LoggerTelemetry(),
}));
```

Example how to create a Telemetry implementation, this should use Glean in production.
> BUG: https://mozilla.slack.com/archives/C0559DDDPQF/p1767895618186219 
```js
class LoggerTelemetry extends MozAdsTelemetry {
  recordBuildCacheError(label, value) {
    console.error("AdsClient - recordBuildCacheError", label, value);
  }
  recordClientError(label, value) {
    console.error("AdsClient - recordClientError", label, value);
  }
  recordClientOperationTotal(label, value) {
    console.error("AdsClient - recordClientOperationTotal", label, value);
  }
  recordDeserializationError(label, value) {
    console.error("AdsClient - recordDeserializationError", label, value);
  }
  recordHttpCacheOutcome(label, value) {
    console.error("AdsClient - recordHttpCacheOutcome", label, value);
  }
}
```

## 3. Fetch ads using the AdsClient in the newtab

We can safely replace the existing `responseData`:
```js
if(USE_ADS_CLIENT) {
    responseData = await this.fetchWithAdsClient(placements);
}
```

With AdsClient, we need to differentiate separate calls between tiles, images and stories.

Here is an example of a `fetchWithAdsClient` implementation filtering on the placementId:
```js
async fetchWithAdsClient(placements) {
    console.error("AdsClient - fetchWithAdsClient calling with", placements);
    const formattedResponse = {};
    try {
        const tilesRequests = placements.filter(
            p => (p.placementId || p.placement)?.startsWith("newtab_tile_")
        ).map(p => new MozAdsPlacementRequest({
            placementId: p.placementId || p.placement,
            iabContent: p.iabContent ?? null,
        }));
        if (tilesRequests.length) {
            console.error("AdsClient - requestTileAds calling with", tilesRequests);
            const tiles = await AdsClient.requestTileAds(tilesRequests, null);
            console.error("AdsClient - requestTileAds SUCCESS - tiles:", tiles);

            for (const [placementId, tile] of tiles) {
                tile.name = `${VERSION}: ${tile.name}`;
                formattedResponse[placementId] = [tile];
            }
        }

        const storiesRequests = placements.filter(
            p => (p.placementId || p.placement)?.startsWith("newtab_stories_")
        ).map(p => new MozAdsPlacementRequestWithCount({
            placementId: p.placementId || p.placement,
            iabContent: p.iabContent ?? null,
            count: p.count,
        }));
        if (storiesRequests.length) {
            console.error("AdsClient - requestSpocAds calling with", storiesRequests);
            const spocs = await AdsClient.requestSpocAds(storiesRequests, null);
            console.error("AdsClient - requestSpocAds SUCCESS - spocs:", spocs);

            for (const [placementId, spoc] of spocs) {
                spoc[0].name = `${VERSION}: ${spoc[0].name}`;
                formattedResponse[placementId] = spoc;
            }
        }
    } catch (error) {
        console.error("AdsClient - requestTileAds ERROR:", error);
    }
    console.error("AdsClient - formattedResponse:", formattedResponse);
    return formattedResponse;
}
```

# 4. Cache the ads using the AdsClient

In AdsClient, we already have a persistent cache mechanism using a SQLite database.
This might conflict with the existing cache mechanism in the newtab `this.cache = this.PersistentCache(CACHE_KEY, true)`.
We could deactivate the existing cache by setting `this.cache = null` when using AdsClient or eventually remove it.

# 5. Recording clicks and impressions using the AdsClient

I did not go into the details of recording clicks and impressions using the AdsClient.
This should be straightforward to implement, just pass the click and impression URLs to the AdsClient.

```js
await AdsClient.recordClick(clickUrl);
await AdsClient.recordImpression(impressionUrl);
```

> WARNING: We manually add context to the URL using a query parameter `request_hash`. You NEED to pass an URL generated by the AdsClient itself to these methods.