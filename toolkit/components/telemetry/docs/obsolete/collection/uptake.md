(telemetry-collection-uptake)=

# Uptake Telemetry (deprecated)

:::{important}
Legacy Telemetry collection APIs are now deprecated.
Please use Glean instead.
:::

Firefox continuously pulls data from different remote sources (eg. settings, system add-ons, …). In order to have consistent insights about the *uptake rate* of these *update sources*, our clients can use a unified Telemetry helper to report their *update status*.

The helper — described below — reports predefined update status, which eventually gives a unified way to obtain:

- the proportion of success among clients;
- its evolution over time;
- the distribution of error causes.

:::{note}
Examples of update sources: *remote settings, add-ons update, add-ons, gfx, and plugins blocklists, certificate revocation, certificate pinning, system add-ons delivery…*

Examples of update status: *up-to-date, success, network error, server error, signature error, server backoff, unknown error…*
:::

Every call to the UptakeTelemetry helper may send a {ref}`Telemetry Event <eventtelemetry>`. Because events are expensive, we take some measures to avoid overwhelming Mozilla systems with the flood of data that this produces. We always send events when not on release channel. On release channel, we only send events from 1% of clients.

## Usage

```js
const { UptakeTelemetry } = ChromeUtils.importESModule("resource://services-common/uptake-telemetry.sys.mjs", {});

UptakeTelemetry.report(component, status, { source });
```

- `component`, a `string` that identifies the calling component (eg. `"remotesettings"`, `"normandy"`). Arbitrary components have to be previously declared in the {ref}`Telemetry Events definition file <eventdefinition>`.

- `source`, a `string` to distinguish what is being pulled or updated in the component (eg. `"blocklists/addons"`, `"recipes/33"`)

- `status`, one of the following status constants:

  - `UptakeTelemetry.STATUS.UP_TO_DATE`: Local content was already up-to-date with remote content.
  - `UptakeTelemetry.STATUS.SUCCESS`: Local content was updated successfully.
  - `UptakeTelemetry.STATUS.BACKOFF`: Remote server asked clients to backoff.
  - `UptakeTelemetry.STATUS.PREF_DISABLED`: Update is disabled in user preferences.
  - `UptakeTelemetry.STATUS.PARSE_ERROR`: Parsing server response has failed.
  - `UptakeTelemetry.STATUS.CONTENT_ERROR`: Server response has unexpected content.
  - `UptakeTelemetry.STATUS.CORRUPTION_ERROR`: Error related to corrupted local data.
  - `UptakeTelemetry.STATUS.SIGNATURE_ERROR`: Signature verification after diff-based sync has failed.
  - `UptakeTelemetry.STATUS.SIGNATURE_RETRY_ERROR`: Signature verification after full fetch has failed.
  - `UptakeTelemetry.STATUS.CONFLICT_ERROR`: Some remote changes are in conflict with local changes.
  - `UptakeTelemetry.STATUS.SYNC_ERROR`: Synchronization of remote changes has failed.
  - `UptakeTelemetry.STATUS.APPLY_ERROR`: Application of changes locally has failed.
  - `UptakeTelemetry.STATUS.SERVER_ERROR`: Server failed to respond.
  - `UptakeTelemetry.STATUS.CERTIFICATE_ERROR`: Server certificate verification has failed.
  - `UptakeTelemetry.STATUS.DOWNLOAD_ERROR`: Data could not be fully retrieved.
  - `UptakeTelemetry.STATUS.TIMEOUT_ERROR`: Server response has timed out.
  - `UptakeTelemetry.STATUS.NETWORK_ERROR`: Communication with server has failed.
  - `UptakeTelemetry.STATUS.NETWORK_OFFLINE_ERROR`: Network not available.
  - `UptakeTelemetry.STATUS.CLEANUP_ERROR`: Clean-up of temporary files has failed.
  - `UptakeTelemetry.STATUS.SHUTDOWN_ERROR`: Error occurring during shutdown.
  - `UptakeTelemetry.STATUS.UNKNOWN_ERROR`: Uncategorized error.
  - `UptakeTelemetry.STATUS.CUSTOM_1_ERROR`: Error #1 specific to this update source.
  - `UptakeTelemetry.STATUS.CUSTOM_2_ERROR`: Error #2 specific to this update source.
  - `UptakeTelemetry.STATUS.CUSTOM_3_ERROR`: Error #3 specific to this update source.
  - `UptakeTelemetry.STATUS.CUSTOM_4_ERROR`: Error #4 specific to this update source.
  - `UptakeTelemetry.STATUS.CUSTOM_5_ERROR`: Error #5 specific to this update source.

Example:

```js
const COMPONENT = "normandy";
const UPDATE_SOURCE = "update-monitoring";

let status;
try {
  const data = await fetch(uri);
  status = UptakeTelemetry.STATUS.SUCCESS;
} catch (e) {
  status = /NetworkError/.test(e) ?
              UptakeTelemetry.STATUS.NETWORK_ERROR :
              UptakeTelemetry.STATUS.SERVER_ERROR ;
}
UptakeTelemetry.report(COMPONENT, status, { source: UPDATE_SOURCE });
```

### Additional Event Info

Events sent using the telemetry events API can contain additional information. Uptake Telemetry allows you to add the following extra fields to events by adding them to the `options` argument:

- `trigger`: A label to distinguish what triggered the polling/fetching of remote content (eg. `"broadcast"`, `"timer"`, `"forced"`, `"manual"`)
- `age`: The age of pulled data in seconds (ie. difference between publication time and fetch time).
- `duration`: The duration of the synchronization process in milliseconds.

```js
UptakeTelemetry.report(component, status, { source, trigger: "timer", age: 138 });
```

Remember that events are sampled on release channel. Those calls to uptake telemetry that do not produce events will ignore these extra fields.

## Use-cases

The following remote data sources are already using this unified histogram.

- remote settings changes monitoring
- add-ons blocklist
- gfx blocklist
- plugins blocklist
- certificate revocation
- certificate pinning
- Normandy Recipe client (legacy)

Obviously, the goal is to eventually converge and avoid ad-hoc Telemetry probes for measuring uptake of remote content. Some notable potential use-cases are:

- nsUpdateService
- mozapps extensions update
