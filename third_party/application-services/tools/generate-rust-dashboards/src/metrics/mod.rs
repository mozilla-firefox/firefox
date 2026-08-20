/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

pub mod counter;
pub mod distribution;
pub mod event;
pub mod labeled_counter;
pub mod labeled_distribution;
pub mod rust_component_errors;
pub mod sync;

use crate::{
    config::{Metric, TeamConfig},
    schema::DashboardBuilder,
    Result,
};

impl Metric {
    pub fn add_to_dashboard(
        &self,
        builder: &mut DashboardBuilder,
        config: &TeamConfig,
    ) -> Result<()> {
        match self {
            Self::Counter(metric) => counter::add_to_dashboard(builder, config, metric),
            Self::LabeledCounter(metric) => {
                labeled_counter::add_to_dashboard(builder, config, metric)
            }
            Self::Distribution(metric) => distribution::add_to_dashboard(builder, config, metric),
            Self::LabeledDistribution(metric) => {
                labeled_distribution::add_to_dashboard(builder, config, metric)
            }
            Self::Events(metric) => event::add_to_dashboard(builder, config, metric),
        }
    }
}
