/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::time::{Duration, SystemTime};

use error_support::breadcrumb;

use crate::internal::FirefoxAccount;
use crate::{Error, Result};

impl FirefoxAccount {
    /// Perform an operation, retrying in the face of authorization errors
    ///
    /// When we see an auth error, we clear the cached access token and try again.
    /// Only use this for operations where a stale access token can cause an error
    pub fn with_auth_retry<T>(
        &mut self,
        access_token_scope: &str,
        method_name: &str,
        mut op: impl FnMut(&mut Self) -> Result<T>,
    ) -> Result<T> {
        let had_cached_access_token = self.state.has_cached_access_token(access_token_scope);
        match op(self) {
            Ok(v) => Ok(v),
            Err(e) if matches!(e, Error::RemoteError { code: 401, .. }) => {
                if had_cached_access_token && self.auth_retry_bucket.try_use_token() {
                    breadcrumb!("fxa-client: retrying {method_name} after auth error");
                    self.state.remove_cached_access_token(access_token_scope);
                    op(self)
                } else {
                    Err(e)
                }
            }
            Err(e) => Err(e),
        }
    }

    /// Perform an operation, retrying in the face of network errors
    pub fn with_network_retry<T>(
        &mut self,
        method_name: &str,
        mut op: impl FnMut(&mut Self) -> Result<T>,
    ) -> Result<T> {
        let mut retry_count = 0;
        loop {
            match op(self) {
                Ok(v) => return Ok(v),
                Err(e) if matches!(e, Error::RequestError(_)) => {
                    if retry_count >= self.network_retry_bucket.retries_per_min {
                        // We've retried `retries_per_min` times. Maybe the operation was so slow
                        // that the tokens filled up faster than they left the bucket?  Don't
                        // continue retrying.
                        breadcrumb!("fxa-client: {method_name} failed {retry_count} times.  Not retrying anymore ({e})");
                        return Err(e);
                    }
                    if !self.network_retry_bucket.try_use_token() {
                        breadcrumb!("fxa-client: {method_name} failed and the token bucket is empty.  Not retrying anymore ({e})");
                        return Err(e);
                    }
                    retry_count += 1;
                    breadcrumb!("fxa-client: retrying {method_name} after network error ({e})");
                    continue;
                }
                Err(e) => return Err(e),
            }
        }
    }

    /// Perform an operation, retrying in the face of auth and network errors
    pub fn with_auth_and_network_retry<T>(
        &mut self,
        access_token_scope: &str,
        method_name: &str,
        mut op: impl FnMut(&mut Self) -> Result<T>,
    ) -> Result<T> {
        self.with_auth_retry(access_token_scope, method_name, |s| {
            s.with_network_retry(method_name, |s| op(s))
        })
    }
}

/// Token bucket implementation
///
/// This is used to rate-limit retries.
pub struct TokenBucket {
    retries_per_min: u32,
    /// Maximum token count
    capacity: u32,
    /// Current token count
    current: u32,
    /// How often to add a token to the bucket
    fill_interval: Duration,
    /// The last time we added a token to the bucket
    ///
    /// Stored as `SystemTime` instead of `Instant` in case the user sets the clock backwards. It's
    /// better to have one weird update at that moment, than to have a non-steady clock that could
    /// run slow from that moment forward.
    last_increment: SystemTime,
}

impl TokenBucket {
    /// Create a new TokenBucket
    pub fn new(retries_per_min: u32) -> Self {
        let fill_interval = Duration::from_secs(60) / retries_per_min;

        Self {
            retries_per_min,
            capacity: retries_per_min,
            current: retries_per_min,
            fill_interval,
            last_increment: SystemTime::now(),
        }
    }

    /// Token bucket for network retries
    ///
    /// This is used for network errors and allows 3 retries per minute.
    pub fn network() -> Self {
        Self::new(3)
    }

    /// Token bucket for auth retries
    ///
    /// This handles 401 errors by clearing the cached access token used for the operation and
    /// trying again. It allows 1 retry per minute.
    pub fn auth() -> Self {
        Self::new(1)
    }

    /// Check if we have a token available
    ///
    /// If so, the token count will be decremented and `true` will be returned.
    /// If not, `false` will be returned without decrementing anything.
    pub fn try_use_token(&mut self) -> bool {
        self.refill_bucket();
        if self.current > 0 {
            self.current -= 1;
            true
        } else {
            false
        }
    }

    /// Refill the bucket based on the time since `last_increment`
    fn refill_bucket(&mut self) {
        let now = SystemTime::now();
        let since = match now.duration_since(self.last_increment) {
            Ok(duration) => duration,
            Err(_) => {
                // Failure to calculate the elapsed duration, maybe the system clock went backwards?
                // Reset the clock which should fix things.
                self.last_increment = now;
                return;
            }
        };
        // In extreme cases the following calculation could overflow. Clamp it to `u32::MAX` in
        // those cases, which will always fill the bucket to capacity.
        let increment =
            u32::try_from(since.as_nanos() / self.fill_interval.as_nanos()).unwrap_or(u32::MAX);
        if self.current.saturating_add(increment) >= self.capacity {
            self.current = self.capacity;
            self.last_increment = now;
        } else {
            self.current += increment;
            self.last_increment += self.fill_interval * increment;
        }
    }
}
