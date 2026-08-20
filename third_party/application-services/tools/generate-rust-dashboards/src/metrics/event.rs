/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::{
    config::{Application, EventsMetric, ReleaseChannel, TeamConfig},
    schema::{
        DashboardBuilder, Datasource, FieldConfig, FieldConfigCustom, FieldConfigDefaults, GridPos,
        Panel, Target, TimeSeriesPanel, Transformation,
    },
    sql::{Query, Union},
    Result,
};

pub fn add_to_dashboard(
    builder: &mut DashboardBuilder,
    _config: &TeamConfig,
    metric: &EventsMetric,
) -> Result<()> {
    builder.add_panel_title(metric.display_name);
    for app in metric.applications.iter().cloned() {
        builder.add_panel_third(count_panel(app, ReleaseChannel::Nightly, metric));
        builder.add_panel_third(count_panel(app, ReleaseChannel::Beta, metric));
        builder.add_panel_third(count_panel(app, ReleaseChannel::Release, metric));
    }
    Ok(())
}

fn count_panel(application: Application, channel: ReleaseChannel, metric: &EventsMetric) -> Panel {
    let EventsMetric {
        ping,
        category,
        metrics,
        ..
    } = metric;

    let mut query = Union::default();
    for metric in metrics {
        query.queries.push(Query {
            select: vec![
                "TIMESTAMP(submission_date) as time".into(),
                format!("'{metric}' as label"),
                "SUM(count) as count".into(),
            ],
            from: format!("`mozdata.rust_components.{ping}_{category}_{metric}`"),
            where_: vec![
                "$__timeFilter(TIMESTAMP(submission_date))".into(),
                format!("application = '{}'", application.slug()),
                format!("channel = '{channel}'"),
            ],
            group_by: Some("1, 2".into()),
            ..Query::default()
        });
    }
    query.order_by = Some("submission_date asc".into());

    TimeSeriesPanel {
        title: application.display_name(channel),
        grid_pos: GridPos::height(8),
        datasource: Datasource::bigquery(),
        interval: "1d".into(),
        targets: vec![Target::table(query.sql())],
        field_config: FieldConfig {
            defaults: FieldConfigDefaults {
                links: vec![],
                custom: FieldConfigCustom {
                    axis_label: "count / day".into(),
                    ..FieldConfigCustom::default()
                },
                unit: None,
            },
        },
        transformations: vec![
            Transformation::PartitionByValues {
                fields: vec!["label".into()],
                keep_fields: true,
            },
            // Fixup the field names for better legend labels
            Transformation::RenameByRegex {
                regex: "count (.*)".into(),
                rename_pattern: "$1".into(),
            },
        ],
        ..TimeSeriesPanel::default()
    }
    .into()
}
