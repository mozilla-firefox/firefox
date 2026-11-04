/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use std::hash::Hash;

use sql_support::open_database;
use viaduct::{Client, ClientSettings, Request, Response};

use super::error::{HTTPError, TransportError};
use crate::{
    http_cache::{HttpCache, RequestHash},
    telemetry, CachePolicy,
};

const OHTTP_CHANNEL_ID: &str = "ads-client";

pub(super) struct MARSTransport {
    http_cache: Option<HttpCache>,
}

impl MARSTransport {
    pub(super) fn new(http_cache: Option<HttpCache>) -> Self {
        Self { http_cache }
    }

    pub(super) fn clear_cache(&self) -> Result<(), open_database::Error> {
        if let Some(cache) = self.http_cache.as_ref() {
            cache.clear()?;
        }
        Ok(())
    }

    pub(super) fn fire(&self, request: Request, ohttp: bool) -> Result<(), TransportError> {
        let client = Self::client_for(ohttp)?;
        let response = client.send_sync(request)?;
        HTTPError::check(&response)?;
        Ok(())
    }

    pub(super) fn invalidate_cache_by_hash(
        &self,
        request_hash: &RequestHash,
    ) -> Result<(), open_database::Error> {
        if let Some(cache) = self.http_cache.as_ref() {
            cache.invalidate_by_hash(request_hash)?;
        }
        Ok(())
    }

    pub(super) fn send<R: Hash + Into<Request>>(
        &self,
        request: R,
        policy: &CachePolicy,
        ohttp: bool,
    ) -> Result<Response, TransportError> {
        let client = Self::client_for(ohttp)?;
        if let Some(cache) = self.http_cache.as_ref() {
            let (response, outcomes) = cache.send_with_policy(&client, request, policy)?;
            for outcome in &outcomes {
                telemetry::record_http_cache_outcome(outcome);
            }
            HTTPError::check(&response)?;
            Ok(response)
        } else {
            let response = client.send_sync(request.into())?;
            HTTPError::check(&response)?;
            Ok(response)
        }
    }

    pub(super) fn shutdown_db(&self) {
        if let Some(cache) = self.http_cache.as_ref() {
            cache.shutdown_db();
        }
    }

    fn client_for(ohttp: bool) -> Result<Client, viaduct::ViaductError> {
        if ohttp {
            Client::with_ohttp_channel(OHTTP_CHANNEL_ID, ClientSettings::default())
        } else {
            Ok(Client::new(ClientSettings::default()))
        }
    }
}
