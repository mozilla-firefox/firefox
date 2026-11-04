# Threat model

## What this project does and where untrusted input enters

`happy-eyeballs` implements Happy Eyeballs v3 (draft-ietf-happy-happyeyeballs-v3) as a deterministic, sans-I/O state
machine. Firefox uses it to decide which DNS queries to send and which endpoints (address, port, HTTP version, ECH
config) to attempt a connection to. The caller performs all I/O and passes in the current time.

Untrusted input enters through:

- `HappyEyeballs::new`/`new_with_network_config`: the origin host comes from a URL, which web content controls.
- `Input::DnsResult`: A, AAAA and HTTPS records come from the network via the resolver. HTTPS records carry
  peer-controlled priorities, target names, ALPN, ports, IP hints and ECH configs, in any number and combination.
- `Input::ConnectionResult`: `EchRetry` carries ECH `retry_configs` sent by the server.
- `NetworkConfig` alt-svc entries, which come from HTTP `Alt-Svc` headers sent by servers.

The application calling the API (Firefox) is trusted; the data above is not, except for the rest of `NetworkConfig`
and the `Instant`s passed in.

## Components that matter most / least

- Most important: `src/lib.rs` and `src/id.rs`, i.e. all of the state machine.
- Out of scope: `tests/`, and the FFI glue in Firefox (`netwerk/`), which is not part of this repository.
- Third-party crates (`url`, `thiserror`, `log`): in scope only where this crate misuses them, or where a bug in them is
  reachable with the untrusted input above.

## How to exercise it

- `cargo test --locked` runs the test suite; it is prebuilt in this image.
- `tests/common/` has helpers to drive the state machine through DNS results and connection results with explicit
  time, which are the easiest way to write a reproducer as a Rust test.

## How you rate severity

### Critical or high

- Memory safety issues reachable with the untrusted input above.
- ECH downgrade: emitting a connection attempt without ECH, or falling back to the origin, when ECH is enabled and the
  HTTPS records carry an ECH config for that service.
- Attaching an ECH config, or the origin's connect name, to an endpoint it does not belong to.
- Attempting a connection that `NetworkConfig` forbids, e.g. an address family excluded by `IpPreference` or an HTTP
  version that is disabled.
- A reachable `debug_assert!` that guards one of the properties above. Tests enable debug assertions; Firefox release
  builds do not.

### Low

These should still be reported:

- Denial of service: panics, infinite loops, and unbounded memory or CPU use driven by DNS results, ECH retries or
  alt-svc entries. A panic without memory corruption is a denial of service, not a memory-safety issue (Mozilla rates
  these sec-low, see https://wiki.mozilla.org/Security_Severity_Ratings/Client).
- Reachable `debug_assert!` failures without the consequences above.
- Issues that require the application to misuse the API.
- Deviations from the specification without a security impact.

## Reports

- Include a reproducer, preferably a Rust test using the helpers in `tests/common/`, and the exact commit.
- Proposed patches should be minimal, follow the existing code style, be formatted with `cargo fmt` and pass
  `cargo clippy --locked --all-targets -- -D warnings`.
