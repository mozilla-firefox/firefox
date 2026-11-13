/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use std::time::Duration;

/// Hard ceiling on any resolved TTL, regardless of source. Guards against a
/// misconfigured server (e.g. `Cache-Control: max-age=315360000`) pinning
/// responses in the cache for far longer than is reasonable.
pub(super) const MAX_TTL: Duration = Duration::from_secs(7 * 24 * 60 * 60);

// Deliberately unsorted: fields are in resolution priority order, matching
// `resolve()` below.
/// The TTL to use when storing a response in the cache, computed from
/// three possible sources.
///
/// `explicit` comes from the caller, `server` from the response's
/// `Cache-Control` header, and `default` from the cache's configuration.
pub(super) struct EffectiveTtl {
    /// Per-request override provided by the caller, if any.
    pub explicit: Option<Duration>,
    /// What the response's `Cache-Control: max-age` leaves after its age,
    /// if present.
    pub server: Option<Duration>,
    /// The cache's configured default TTL.
    pub default: Duration,
}

impl EffectiveTtl {
    /// Resolve the TTL by priority (highest to lowest):
    /// 1. `explicit` — caller-provided per-request override.
    /// 2. `server` — what the response's `Cache-Control: max-age` leaves.
    /// 3. `default` — the cache's configured default.
    ///
    /// The result is capped at [`MAX_TTL`].
    pub(super) fn resolve(&self) -> Duration {
        let chosen = self.explicit.or(self.server).unwrap_or(self.default);
        chosen.min(MAX_TTL)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn explicit_overrides_server_and_default() {
        let ttl = EffectiveTtl {
            default: Duration::from_secs(300),
            explicit: Some(Duration::from_secs(60)),
            server: Some(Duration::from_secs(3600)),
        }
        .resolve();
        assert_eq!(ttl, Duration::from_secs(60));
    }

    #[test]
    fn falls_back_to_server_when_no_explicit() {
        let ttl = EffectiveTtl {
            default: Duration::from_secs(300),
            explicit: None,
            server: Some(Duration::from_secs(3600)),
        }
        .resolve();
        assert_eq!(ttl, Duration::from_secs(3600));
    }

    #[test]
    fn falls_back_to_default_when_no_explicit_and_no_server() {
        let ttl = EffectiveTtl {
            default: Duration::from_secs(300),
            explicit: None,
            server: None,
        }
        .resolve();
        assert_eq!(ttl, Duration::from_secs(300));
    }

    #[test]
    fn zero_server_ttl_yields_zero() {
        // Lets the strategy emit NoCache without a network round-trip.
        let ttl = EffectiveTtl {
            default: Duration::from_secs(300),
            explicit: None,
            server: Some(Duration::ZERO),
        }
        .resolve();
        assert_eq!(ttl, Duration::ZERO);
    }

    #[test]
    fn server_ttl_is_capped_at_max_ttl() {
        let ttl = EffectiveTtl {
            default: Duration::from_secs(300),
            explicit: None,
            server: Some(Duration::from_secs(365 * 24 * 60 * 60)),
        }
        .resolve();
        assert_eq!(ttl, MAX_TTL);
    }

    #[test]
    fn explicit_ttl_is_capped_at_max_ttl() {
        let ttl = EffectiveTtl {
            default: Duration::from_secs(300),
            explicit: Some(Duration::from_secs(30 * 24 * 60 * 60)),
            server: None,
        }
        .resolve();
        assert_eq!(ttl, MAX_TTL);
    }

    #[test]
    fn default_ttl_is_capped_at_max_ttl() {
        let ttl = EffectiveTtl {
            default: Duration::from_secs(30 * 24 * 60 * 60),
            explicit: None,
            server: None,
        }
        .resolve();
        assert_eq!(ttl, MAX_TTL);
    }
}
