# State Machine

## Service state machine

The finite state machine is implemented in the `IPProtectionService` class
and the states are defined in the `IPProtectionStates` object.

### Service states

The service transitions across the following states:

- `UNINITIALIZED`: Service not initialized or feature disabled.
- `UNAVAILABLE`: User not eligible (Nimbus), signed out with no eligibility, or the feature is not sufficiently localized for a user who never interacted with it; UI hidden.
- `UNAUTHENTICATED`: User signed out but eligible; UI shows login.
- `READY`: Ready to activate the proxy.

### High‑level transitions

- Feature disabled → `UNINITIALIZED`.
- During startup, if initialization isn’t complete, use cached state from `IPPStartupCache`.
- Not eligible, or not localized enough for a user who never opened the panel → `UNAVAILABLE`.
- Not signed in → `UNAVAILABLE` if not eligible, otherwise `UNAUTHENTICATED`.
- If an entitlement is cached/valid → `READY`.
- Otherwise, check enrollment with Guardian (via `IPPFxaActivateAuthProvider.updateEntitlement()`):
  \- Not enrolled → `UNAVAILABLE` (not eligible).
  \- Enrolled → fetch entitlement; if successful → `READY`, else `UNAVAILABLE` when not eligible.

### Events and integration points

- `IPProtectionService:StateChanged` is dispatched on state changes with
  `detail.state` and `detail.prevState`.
- Helpers can call `IPProtectionService.updateState()` to recompute the state immediately; update any helper-owned data first because the call is synchronous.
- Public actions: `start(userAction)`, `stop(userAction)`.

## Proxy manager state machine

The `IPPProxyManager` layers a proxy‑specific finite state machine on top of
`IPProtectionService`. It mirrors eligibility changes from the service and
drives the lifecycle of the proxy connection.

### Proxy states

- `NOT_READY`: Service is not `READY` and UI should not offer activation.
- `READY`: Service is `READY` and the proxy can be activated.
- `ACTIVATING`: `start()` is working on an activation: waiting for the service while holding traffic behind a channel filter, then fetching a proxy pass and selecting an endpoint.
- `ACTIVE`: Proxy connected. Usage and network observers are reporting metrics.
- `ERROR`: An unrecoverable error occurred while the proxy is connected, such as failing when rotating credentials, or a system activation (`start(false)`) failed. The channel filter cancels proxied requests with a proxy error. Stop must be called to change states.
- `PAUSED`: Everything is working but the bandwidth limit has been reached so we can't connect to the VPN. The bandwidth will reset next month.

### Proxy transitions

- `IPProtectionService:StateChanged` → `IPPProxyManager.updateState()`:
  \- Service `READY` → proxy `READY` (resets connection/error history).
  \- Any other service state → proxy `NOT_READY` (stops active connections).
- `start(userAction)` registers the channel filter before it yields, so traffic is held for the whole activation rather than from after the server list and auth round-trips.
  \- From `READY` it moves to `ACTIVATING`.
  \- From `NOT_READY` it waits, as long as the service could still become ready: while the startup cache is open every non-`READY` state is a guess carried over from the previous session, and a service that is merely initializing is not an answer either. Once the real state is known, `UNAVAILABLE` and `UNAUTHENTICATED` need the user and `start()` reports `ERRORS.NOT_READY` instead of holding traffic. `browser.ipProtection.serviceWaitTimeout` is the backstop.
  \- Successful activation → `ACTIVE` and telemetry `ipprotection.started`.
  \- Failures during a user activation (missing entitlement, server list, proxy pass…) call `updateState()` to demote the proxy back to its previous state.
  \- Failures during a system activation move to `ERROR` instead, since its callers do not check the result and would otherwise start again from `READY`. `ERRORS.NOT_READY` and `ERRORS.CANCELED` are not failures and demote as above.
  \- Errors after the proxy is connected → `ERROR` via `#setErrorState`.
  \- From `ERROR`, a user activation (`start(true)`) is a retry: it tears the failed connection down and moves straight to `ACTIVATING`; a system activation is refused with the error. A retry that fails goes back to `ERROR` and keeps holding traffic, like a failed system activation.
- `stop(userAction)` from `ACTIVE` → `READY` after closing the channel filter and observers. It also cancels an activation that is in flight, including one still waiting for the service. `IPPAutoActivation` only starts it again on its own when a working connection (`ACTIVE` or `PAUSED`) goes back to `READY`, so a failed activation or a stop out of `ERROR` is not retried until a site rule changes or the service becomes ready anew.
- `reset()` or helper‑driven recomputations call `updateState()` which demotes the proxy back to `READY`/`NOT_READY` and clears the credential cache.
- Network errors (`proxy-http-error` with 401) trigger Proxy Pass rotation while staying `ACTIVE`; repeated failures bubble up through `#setErrorState`.

### Proxy events and hooks

- `IPPProxyManager:StateChanged` is dispatched with `detail.state` and `detail.prevState` whenever the proxy state machine moves.
- `IPPProxyManager` listens to `IPProtectionService:StateChanged` and to `proxy-http-error` from `IPPNetworkErrorObserver`.
- Consumers can observe `IPPProxyManager.state` (or listen for events) to synchronize UI/telemetry with the proxy lifecycle.
