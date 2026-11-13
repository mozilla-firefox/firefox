/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use super::cache_control::CacheControl;
use super::request_hash::RequestHash;
use super::response_age::ResponseAge;
use super::store::HttpCacheStore;
use super::ttl::EffectiveTtl;
use super::{CacheOutcome, HttpCacheSendResult};
use crate::clock::Clock;
use std::time::Duration;
use viaduct::{Client, Request};

pub(super) struct CacheFirst {
    pub default_ttl: Duration,
    pub explicit_ttl: Option<Duration>,
    pub hash: RequestHash,
    pub request: Request,
}

impl CacheFirst {
    pub(super) fn apply(
        self,
        client: &Client,
        store: &HttpCacheStore,
        clock: &Clock,
    ) -> HttpCacheSendResult {
        let mut outcomes = vec![];
        match store.lookup(&self.hash) {
            Ok(Some(response)) => return Ok((response, vec![CacheOutcome::Hit])),
            Err(e) => outcomes.push(CacheOutcome::LookupFailed(e)),
            Ok(None) => {}
        }

        let network = NetworkFirst {
            default_ttl: self.default_ttl,
            explicit_ttl: self.explicit_ttl,
            hash: self.hash,
            request: self.request,
        };
        let (response, mut network_outcomes) = network.apply(client, store, clock)?;
        outcomes.append(&mut network_outcomes);
        Ok((response, outcomes))
    }
}

pub(super) struct NetworkFirst {
    pub default_ttl: Duration,
    pub explicit_ttl: Option<Duration>,
    pub hash: RequestHash,
    pub request: Request,
}

impl NetworkFirst {
    pub(super) fn apply(
        self,
        client: &Client,
        store: &HttpCacheStore,
        clock: &Clock,
    ) -> HttpCacheSendResult {
        let response = client.send_sync(self.request)?;
        let now = clock.now();
        let cache_control = CacheControl::from(&response);
        // An error response is not an answer to cache: the next send retries.
        let outcome = if response.is_success() && cache_control.should_cache() {
            let ttl = EffectiveTtl {
                default: self.default_ttl,
                explicit: self.explicit_ttl,
                server: cache_control.time_to_live(ResponseAge::from(&response).at(now)),
            }
            .resolve();
            if ttl.is_zero() {
                return Ok((response, vec![CacheOutcome::NoCache]));
            }
            match store.store_with_ttl(&self.hash, &response, ttl, now) {
                Ok(()) => CacheOutcome::MissStored,
                Err(e) => CacheOutcome::StoreFailed(e),
            }
        } else {
            CacheOutcome::MissNotCacheable
        };
        Ok((response, vec![outcome]))
    }
}
