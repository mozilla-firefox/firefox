/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::config::*;

use Application::*;

pub fn all_dashboards() -> Vec<TeamConfig> {
    vec![
        TeamConfig {
            team_name: "SYNC",
            components: vec![
                Component::Autofill,
                Component::Fxa,
                Component::Logins,
                Component::Places,
                Component::RemoteSettings,
                Component::Tabs,
            ],
            component_errors: true,
            sync_metrics: true,
            main_dashboard_metrics: vec![
                DistributionMetric {
                    kind: DistributionMetricKind::Timing,
                    display_name: "Places run_maintenance() time",
                    ping: "metrics",
                    category: "places_manager",
                    metric: "run_maintenance_time",
                    axis_label: "time",
                    unit: Some(Unit::Milliseconds),
                    value_divisor: Some(1_000_000),
                    applications: vec![Android],
                    link_to: Some("Sync Maintenance Times"),
                }
                .into(),
                EventsMetric {
                    display_name: "Logins key regeneration",
                    ping: "metrics",
                    category: "logins_store",
                    metrics: vec![
                        "key_regenerated_lost",
                        "key_regenerated_corrupt",
                        "key_regenerated_other",
                    ],
                    applications: vec![Android, Ios],
                }
                .into(),
                EventsMetric {
                    display_name: "Credit Card key regeneration",
                    ping: "metrics",
                    category: "credit_card_key_regeneration",
                    metrics: vec!["keychain_data_lost", "lost", "corrupt", "other"],
                    applications: vec![Ios],
                }
                .into(),
            ],
            extra_dashboards: vec![ExtraDashboard {
                name: "Sync Maintenance Times",
                metrics: vec![
                    DistributionMetric {
                        kind: DistributionMetricKind::Timing,
                        display_name: "Places run_maintenance_chk_pnt_time() time",
                        ping: "metrics",
                        category: "places_manager",
                        metric: "run_maintenance_chk_pnt_time",
                        axis_label: "time",
                        unit: Some(Unit::Milliseconds),
                        value_divisor: Some(1_000_000),
                        applications: vec![Android],
                        link_to: None,
                    }
                    .into(),
                    DistributionMetric {
                        kind: DistributionMetricKind::Timing,
                        display_name: "Places run_maintenance_optimize_time() time",
                        ping: "metrics",
                        category: "places_manager",
                        metric: "run_maintenance_optimize_time",
                        axis_label: "time",
                        unit: Some(Unit::Milliseconds),
                        value_divisor: Some(1_000_000),
                        applications: vec![Android],
                        link_to: None,
                    }
                    .into(),
                    DistributionMetric {
                        kind: DistributionMetricKind::Timing,
                        display_name: "Places run_maintenance_prune_time() time",
                        ping: "metrics",
                        category: "places_manager",
                        metric: "run_maintenance_prune_time",
                        axis_label: "time",
                        unit: Some(Unit::Milliseconds),
                        value_divisor: Some(1_000_000),
                        applications: vec![Android],
                        link_to: None,
                    }
                    .into(),
                    DistributionMetric {
                        kind: DistributionMetricKind::Timing,
                        display_name: "Places run_maintenance_vacuum_time() time",
                        ping: "metrics",
                        category: "places_manager",
                        metric: "run_maintenance_vacuum_time",
                        axis_label: "time",
                        unit: Some(Unit::Milliseconds),
                        value_divisor: Some(1_000_000),
                        applications: vec![Android],
                        link_to: None,
                    }
                    .into(),
                ],
            }],
        },
        TeamConfig {
            team_name: "DISCO",
            components: vec![Component::Suggest],
            component_errors: true,
            sync_metrics: false,
            main_dashboard_metrics: vec![
                LabeledDistributionMetric {
                    kind: DistributionMetricKind::Timing,
                    display_name: "Suggest ingest time",
                    ping: "metrics",
                    category: "suggest",
                    metric: "ingest_time",
                    axis_label: "time",
                    unit: Some(Unit::Milliseconds),
                    value_divisor: Some(1_000_000),
                    applications: vec![Desktop],
                }
                .into(),
                LabeledDistributionMetric {
                    kind: DistributionMetricKind::Timing,
                    display_name: "Suggest ingest download time",
                    ping: "metrics",
                    category: "suggest",
                    metric: "ingest_download_time",
                    axis_label: "time",
                    unit: Some(Unit::Milliseconds),
                    value_divisor: Some(1_000_000),
                    applications: vec![Desktop],
                }
                .into(),
                LabeledDistributionMetric {
                    kind: DistributionMetricKind::Timing,
                    display_name: "Suggest query time",
                    ping: "metrics",
                    category: "suggest",
                    metric: "query_time",
                    axis_label: "time",
                    unit: Some(Unit::Microseconds),
                    value_divisor: Some(1_000),
                    applications: vec![Desktop],
                }
                .into(),
            ],
            extra_dashboards: vec![],
        },
        TeamConfig {
            team_name: "Credential Management",
            components: vec![Component::Logins],
            component_errors: true,
            sync_metrics: true,
            main_dashboard_metrics: vec![],
            extra_dashboards: vec![],
        },
    ]
}
