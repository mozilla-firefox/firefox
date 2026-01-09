/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::ViaductError;
use parking_lot::Mutex;
use std::collections::HashMap;

#[derive(Debug, thiserror::Error)]
pub enum OhttpError {
    #[error("OHTTP key config is malformed")]
    MalformedKeyConfig,
    #[error("Unsupported OHTTP encryption algorithm")]
    UnsupportedKeyConfig,
    #[error("OhttpSession is in invalid state")]
    InvalidSession,
    #[error("Cannot encode message as BHTTP/OHTTP")]
    CannotEncodeMessage,
    #[error("Cannot decode OHTTP/BHTTP message")]
    MalformedMessage,
    #[error("Duplicate HTTP response headers")]
    DuplicateHeaders,
}

impl From<OhttpError> for ViaductError {
    fn from(e: OhttpError) -> Self {
        ViaductError::OhttpRequestError(e.to_string())
    }
}

#[derive(Default)]
enum ExchangeState {
    #[default]
    Invalid,
    Request(ohttp::ClientRequest),
    Response(ohttp::ClientResponse),
}

pub struct OhttpSession {
    state: Mutex<ExchangeState>,
}

pub struct OhttpResponse {
    status_code: u16,
    headers: HashMap<String, String>,
    payload: Vec<u8>,
}

impl OhttpResponse {
    pub fn into_parts(self) -> (u16, HashMap<String, String>, Vec<u8>) {
        (self.status_code, self.headers, self.payload)
    }
}

fn headers_to_map(message: &bhttp::Message) -> Result<HashMap<String, String>, OhttpError> {
    let mut headers = HashMap::new();

    for field in message.header().iter() {
        if headers
            .insert(
                std::str::from_utf8(field.name())
                    .map_err(|_| OhttpError::MalformedMessage)?
                    .into(),
                std::str::from_utf8(field.value())
                    .map_err(|_| OhttpError::MalformedMessage)?
                    .into(),
            )
            .is_some()
        {
            return Err(OhttpError::DuplicateHeaders);
        }
    }

    Ok(headers)
}

impl OhttpSession {
    pub fn new(config: &[u8]) -> Result<Self, OhttpError> {
        ohttp::init();

        let request = ohttp::ClientRequest::from_encoded_config(config).map_err(|e| match e {
            ohttp::Error::Unsupported => OhttpError::UnsupportedKeyConfig,
            _ => OhttpError::MalformedKeyConfig,
        })?;

        let state = Mutex::new(ExchangeState::Request(request));
        Ok(OhttpSession { state })
    }

    pub fn encapsulate(
        &self,
        method: &str,
        scheme: &str,
        server: &str,
        endpoint: &str,
        mut headers: HashMap<String, String>,
        payload: &[u8],
    ) -> Result<Vec<u8>, OhttpError> {
        let mut message =
            bhttp::Message::request(method.into(), scheme.into(), server.into(), endpoint.into());

        for (k, v) in headers.drain() {
            message.put_header(k, v);
        }

        message.write_content(payload);

        let mut encoded = vec![];
        message
            .write_bhttp(bhttp::Mode::KnownLength, &mut encoded)
            .map_err(|_| OhttpError::CannotEncodeMessage)?;

        let mut state = self.state.lock();
        let request = match std::mem::take(&mut *state) {
            ExchangeState::Request(request) => request,
            _ => return Err(OhttpError::InvalidSession),
        };
        let (capsule, response) = request
            .encapsulate(&encoded)
            .map_err(|_| OhttpError::CannotEncodeMessage)?;
        *state = ExchangeState::Response(response);

        Ok(capsule)
    }

    pub fn decapsulate(&self, encoded: &[u8]) -> Result<OhttpResponse, OhttpError> {
        let mut state = self.state.lock();
        let decoder = match std::mem::take(&mut *state) {
            ExchangeState::Response(response) => response,
            _ => return Err(OhttpError::InvalidSession),
        };
        let binary = decoder
            .decapsulate(encoded)
            .map_err(|_| OhttpError::MalformedMessage)?;

        let mut cursor = std::io::Cursor::new(binary);
        let message =
            bhttp::Message::read_bhttp(&mut cursor).map_err(|_| OhttpError::MalformedMessage)?;

        let headers = headers_to_map(&message)?;

        Ok(OhttpResponse {
            status_code: match message.control() {
                bhttp::ControlData::Response(sc) => (*sc).into(),
                _ => return Err(OhttpError::InvalidSession),
            },
            headers,
            payload: message.content().into(),
        })
    }
}
