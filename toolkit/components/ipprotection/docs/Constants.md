# Constants

The `ERRORS` constants for the IP Protection feature are defined in
`toolkit/components/ipprotection/IPPProxyManager.sys.mjs`.

## Error Codes

The `ERRORS` object collects all error string constants used across the
feature. Each value is a stable identifier that travels as a rejection
reason or a state payload through the proxy activation pipeline and into
the UI layer.

`ERRORS.GENERIC`

: Catch-all fallback used when no more-specific code is available.
  The UI surfaces a generic error message.

`ERRORS.NETWORK`

: The device is offline at activation time. The UI renders a dedicated
  network error message distinct from the generic one.

`ERRORS.TIMEOUT`

: Activation exceeded the 30-second deadline. The activation is aborted
  and the proxy moves to the `ERROR` state.

`ERRORS.PASS_UNAVAILABLE`

: The server did not return a valid proxy pass during activation.

`ERRORS.SERVER_NOT_FOUND`

: No proxy server is available for the default location.

`ERRORS.MISSING_PROMISE`

: Internal consistency guard: the activation promise was unexpectedly
  absent while the proxy was in the `ACTIVATING` state. Should never
  occur in normal operation.

`ERRORS.MISSING_ABORT`

: Internal consistency guard: the abort controller was unexpectedly
  absent while stopping an in-progress activation. Should never occur
  in normal operation.

`ERRORS.CANCELED`

: The activation was canceled by calling `stop()` while the proxy was
  still in the `ACTIVATING` state. The UI suppresses the error message
  in this case.

`ERRORS.VPN_UNAVAILABLE`

: VPN service is unavailable in the given region.

`ERRORS.CATASTROPHIC`

: The service failed in a way it cannot recover from.

`ERRORS.NOT_READY`

: Activation was requested while the proxy was not in the `READY` state.

`ERRORS.QUOTA_EXHAUSTED`

: The bandwidth limit has been reached, so the proxy is `PAUSED` rather
  than activating.

`ERRORS.CONNECTION_FAILED`

: The connection never became active, even though a pass and a server
  were available.

`ERRORS.SERVERLIST_UNAVAILABLE`

: The list of proxy servers could not be fetched.

`ERRORS.MISSING_PASS`

: Internal consistency guard: a pass rotation completed without returning
  either a pass or an error. Should never occur in normal operation.

### Error propagation

Errors thrown inside `IPPProxyManager.start()` are caught by the activation
promise. The proxy reverts to its previous state (typically `READY`) and the
promise resolves with `{ started: false, error }`. The panel reads the error
code from this result to determine which message to show.

Errors that occur while the proxy is already `ACTIVE` (such as a pass
rotation failure) move the state machine to `IPPProxyStates.ERROR` and are
recorded as its `errorType`.

`ERRORS.MISSING_PROMISE` and `ERRORS.MISSING_ABORT` are thrown directly
from `start()` or `stop()` as `Error` objects and bypass the activation
promise; they represent internal consistency violations and do not affect proxy
state.
