/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use crate::mars::error::{FetchAdsError, RecordClickError, RecordImpressionError, ReportAdError};

#[derive(Debug, thiserror::Error)]
pub enum ComponentError {
    #[cfg(feature = "stateful")]
    #[error("Error requesting ads from worker: {0}")]
    BackgroundWorker(#[from] BackgroundWorkerError),

    #[error("Error clearing the cache: {0}")]
    ClearCache(#[from] sql_support::open_database::Error),

    #[error("Error recording a click for a placement: {0}")]
    RecordClick(#[from] RecordClickError),

    #[error("Error recording an impressions for a placement: {0}")]
    RecordImpression(#[from] RecordImpressionError),

    #[error("Error reporting an ad: {0}")]
    ReportAd(#[from] ReportAdError),

    #[error("Error requesting ads: {0}")]
    RequestAds(#[from] RequestAdsError),
}

#[derive(Debug, thiserror::Error)]
pub enum RequestAdsError {
    #[error(transparent)]
    ContextId(#[from] context_id::ApiError),

    #[error("Error requesting ads from MARS: {0}")]
    FetchAds(#[from] FetchAdsError),
}

#[cfg(feature = "stateful")]
#[derive(Debug, thiserror::Error)]
pub enum BackgroundWorkerError {
    #[error("Background worker is closed")]
    Closed,
}
