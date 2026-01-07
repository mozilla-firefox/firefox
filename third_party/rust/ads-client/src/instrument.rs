/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use std::sync::LazyLock;

use crate::error::{ComponentError, EmitTelemetryError};
use parking_lot::RwLock;
use serde::{Deserialize, Serialize};
use url::Url;
use viaduct::Request;

static DEFAULT_TELEMETRY_ENDPOINT: &str = "https://ads.mozilla.org/v1/log";
static TELEMETRY_ENDPONT: LazyLock<RwLock<String>> =
    LazyLock::new(|| RwLock::new(DEFAULT_TELEMETRY_ENDPOINT.to_string()));

fn get_telemetry_endpoint() -> String {
    TELEMETRY_ENDPONT.read().clone()
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum TelemetryEvent {
    Init,
    RenderError,
    AdLoadError,
    FetchError,
    InvalidUrlError,
}

pub trait TrackError<T, ComponentError> {
    fn emit_telemetry_if_error(self) -> Self;
}

impl<T> TrackError<T, ComponentError> for Result<T, ComponentError> {
    /// Attempts to emit a telemetry event if the Error type can map to an event type.
    fn emit_telemetry_if_error(self) -> Self {
        if let Err(ref err) = self {
            let error_type = map_error_to_event_type(err);
            let _ = emit_telemetry_event(error_type);
        }
        self
    }
}

fn map_error_to_event_type(err: &ComponentError) -> Option<TelemetryEvent> {
    match err {
        ComponentError::RequestAds(_) => Some(TelemetryEvent::FetchError),
        ComponentError::RecordImpression(_) => Some(TelemetryEvent::InvalidUrlError),
        ComponentError::RecordClick(_) => Some(TelemetryEvent::InvalidUrlError),
        ComponentError::ReportAd(_) => Some(TelemetryEvent::InvalidUrlError),
    }
}

pub fn emit_telemetry_event(event_type: Option<TelemetryEvent>) -> Result<(), EmitTelemetryError> {
    let endpoint = get_telemetry_endpoint();
    let mut url = Url::parse(&endpoint)?;
    if let Some(event) = event_type {
        let event_string = serde_json::to_string(&event)?;
        url.set_query(Some(&format!("event={}", event_string)));
        Request::get(url).send()?;
    }
    Ok(())
}
