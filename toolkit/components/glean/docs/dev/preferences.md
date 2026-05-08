# Preferences and Defines

## User Preferences

`datareporting.healthreport.uploadEnabled`

This determines whether the Glean SDK has `collection_enabled` set.
It can be controlled by users via `about:preferences#privacy`.
If this is set to false from true, we send a
["deletion-request" ping](https://mozilla.github.io/glean/book/user/pings/deletion_request.html),
and the only data collection that can happen are for
[pings that set `follows_collection_enabled: false`](https://searchfox.org/firefox-main/search?q=follows_collection_enabled%3A%20false&path=).
More information can be found in
[this support article](https://support.mozilla.org/kb/technical-and-interaction-data).

## Test-only Preferences

`telemetry.fog.test.localhost_port`

If set to a value `port` which is greater than 0, pings will be sent to
`http://localhost:port` instead of `https://incoming.telemetry.mozilla.org`.
If set to a value `port` which is less than 0, FOG will:
1) Tell Glean that upload is enabled, even if it isn't.
2) Take all pings scheduled for upload and drop them on the floor,
   telling the Glean SDK that it was sent successfully.

This is how you emulate "recording enabled but upload disabled"
like developer builds have in Firefox Telemetry.
When switching from `port < 0` to `port >= 0`,
Glean will be told (if just temporarily) that upload is disabled.
This clears the stores of recorded-but-not-reported data.
Defaults to 0.

`telemetry.fog.test.activity_limit`
`telemetry.fog.test.inactivity_limit`

This pair of prefs control the length of time of activity before inactivity
(or vice versa)
needed before FOG informs the SDK's Client Activity API that the client was (in)active.
Present to allow testing without figuring out how to mock Rust's clock.
Their values are integer seconds.
The value `-1` is treated as no limit (never trigger (in)activity).
Defaults to 120 (activity), 1200 (inactivity).

## Internal Preferences

`telemetry.fog.artifact_build`

Read-only. This pref is `true` only if `MOZ_ARTIFACT_BUILDS` was set during configure.
If true, [JOG](./jog) is enabled so that artifact builds will exhibit changes to their Glean metrics.

`telemetry.fog.init_on_shutdown`

Defaults to `true`.
Controls whether Glean initializes on shutdown if it hasn't been initialized, in order to capture data from very short sessions.
In case a policy modal needs to be shown to the user, which will delay Glean initialization,
it's set to `false` until the user has dealt with the modal. It's turned back to `true` afterwards.
This ensures we don't capture data until the user had the chance to make an explicit choice.

## Defines

`MOZ_AUTOMATION`

If set, and `GLEAN_SOURCE_TAGS` isn't set, FOG will set a
[Glean source tag](https://mozilla.github.io/glean/book/reference/debug/sourceTags.html)
of `automation` and FOG will disable the submission of "metrics" pings.

If `GLEAN_SOURCE_TAGS` is set, the `automation` source tag will not be added automatically.

If not set, any tags set by `GLEAN_SOURCE_TAGS` will be present.

`MOZ_GLEAN_ANDROID`

If set, the Glean SDK is assumed to be managed by something other than FOG, meaning:
* [GIFFT][gifft] is disabled.
* FOG doesn't initialize Glean
* FOG doesn't relay (in)activity or experiment annotations to Glean

`MOZILLA_OFFICIAL`

If unset, we set a `glean_disable_upload` Rust feature in
`gkrust` and `gkrust-shared` which is forwarded to `fog_control` as `disable_upload`.
This feature defaults FOG to an "upload disabled"
mode where collection on the client proceeds as normal but no ping is sent.
This mode can be overridden at runtime in two ways:
* If the ping has a
  [Debug Tag](https://mozilla.github.io/glean/book/user/debugging/index.html)
  then it is sent so that it may be inspected in the
  [Glean Debug Ping Viewer](https://debug-ping-preview.firebaseapp.com/).
* If the preference `telemetry.fog.test.localhost_port` is set to a value greater than 0,
  then pings are sent to a server operating locally at that port
  (even if the ping has a Debug Tag), to enable testing.

`MOZILLA_OFFICIAL` tends to be set on most builds released to users,
including builds distributed by Linux distributions.
It tends to not be set on local developer builds.
See [bug 1680025](https://bugzilla.mozilla.org/show_bug.cgi?id=1680025) for details.

`MOZ_ARTIFACT_BUILDS`

If `MOZ_ARTIFACT_BUILDS` is set in the build config,
[JOG](./jog) will generate a file for the runtime-registration of metrics and pings.
This is to support [Artifact Builds](/contributing/build/artifact_builds).

See also `telemetry.fog.artifact_build`.

[gifft]: ../user/gifft
