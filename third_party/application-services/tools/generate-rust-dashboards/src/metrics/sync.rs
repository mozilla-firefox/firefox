/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::{
    config::{Application, ReleaseChannel, TeamConfig, Unit},
    schema::{
        CustomVariable, Dashboard, DashboardBuilder, DataLink, Datasource, FieldConfig,
        FieldConfigCustom, FieldConfigDefaults, GridPos, LogOptions, LogPanel, Panel,
        ScaleDistribution, Target, TextPanel, TimeSeriesPanel, Transformation,
    },
    sql::Query,
    util::{Join, UrlBuilder},
    Result,
};

pub fn add_to_main_dashboard(builder: &mut DashboardBuilder, config: &TeamConfig) -> Result<()> {
    add_overview_panels(
        builder,
        "Sync success rate",
        config,
        SyncMetric::SuccessRate,
    );
    add_overview_panels(
        builder,
        "Sync: total counts",
        config,
        SyncMetric::TotalCounts,
    );
    add_overview_panels(
        builder,
        "Sync: average time",
        config,
        SyncMetric::AverageTime,
    );

    if config.team_name == "SYNC" {
        builder.add_panel_title("Legacy dashboards");
        builder.add_panel_full(sync_legacy_dashboard_panel());
    }

    Ok(())
}

pub fn extra_dashboard(config: &TeamConfig) -> Result<Dashboard> {
    let mut builder = DashboardBuilder::new(
        format!("{} - Sync Details", config.team_name),
        format!("{}-sync-extra", config.team_slug()),
    );
    builder.add_application_variable(config)?;
    builder.add_channel_variable();
    builder.add_variable(CustomVariable {
        label: "Sync Engine".into(),
        name: "engine".into(),
        query: config
            .components
            .iter()
            .flat_map(|a| a.sync_engines())
            .map(|s| s.to_string())
            .join(","),
        ..CustomVariable::default()
    });

    builder.add_panel_title("Metrics");
    builder.add_panel_full(details_dash_count_panel(
        "Success Rate",
        SyncMetric::SuccessRate,
    ));
    builder.add_panel_full(details_dash_count_panel(
        "Total counts (7 day moving average)",
        SyncMetric::TotalCounts,
    ));
    builder.add_panel_full(details_dash_count_panel(
        "Average sync time (7 day moving average)",
        SyncMetric::AverageTime,
    ));
    builder.add_panel_title("Errors");
    builder.add_panel_full(details_dash_error_count_panel(config));
    builder.add_panel_full(details_dash_error_log_panel(config));

    Ok(builder.dashboard)
}

#[derive(Clone, Copy)]
enum SyncMetric {
    SuccessRate,
    TotalCounts,
    AverageTime,
}

impl SyncMetric {
    fn column_name(&self) -> &'static str {
        match &self {
            SyncMetric::SuccessRate => "success_rate",
            SyncMetric::TotalCounts => "count_total",
            SyncMetric::AverageTime => "avg_sync_time",
        }
    }

    fn moving_average(&self) -> bool {
        matches!(self, SyncMetric::TotalCounts | SyncMetric::AverageTime)
    }

    fn field_config_custom(&self) -> FieldConfigCustom {
        match self {
            SyncMetric::SuccessRate => FieldConfigCustom {
                axis_label: "success rate".into(),
                axis_soft_min: 99,
                axis_soft_max: 100,
                ..FieldConfigCustom::default()
            },
            SyncMetric::TotalCounts => FieldConfigCustom {
                scale_distribution: ScaleDistribution {
                    type_: "log".into(),
                    log: Some(10),
                },
                ..FieldConfigCustom::default()
            },
            SyncMetric::AverageTime => FieldConfigCustom::default(),
        }
    }

    fn unit(&self) -> Option<Unit> {
        match self {
            SyncMetric::SuccessRate => None,
            SyncMetric::TotalCounts => Some(Unit::SiShort),
            SyncMetric::AverageTime => Some(Unit::Seconds),
        }
    }

    fn column_expr(&self) -> String {
        let column_name = self.column_name();
        if !self.moving_average() {
            column_name.into()
        } else {
            format!(
                "AVG({column_name}) OVER (
                PARTITION BY engine_name
                ORDER BY submission_date
                ROWS BETWEEN 6 PRECEDING AND CURRENT ROW
              ) AS {column_name}"
            )
        }
    }
}

fn add_overview_panels(
    builder: &mut DashboardBuilder,
    title: &str,
    config: &TeamConfig,
    metric: SyncMetric,
) {
    builder.add_panel_title(title);

    for app in config.applications().iter() {
        builder.add_panel_third(overview_panel(
            config,
            *app,
            ReleaseChannel::Nightly,
            metric,
        ));
        builder.add_panel_third(overview_panel(config, *app, ReleaseChannel::Beta, metric));
        builder.add_panel_third(overview_panel(
            config,
            *app,
            ReleaseChannel::Release,
            metric,
        ));
    }
}

fn overview_panel(
    config: &TeamConfig,
    application: Application,
    channel: ReleaseChannel,
    metric: SyncMetric,
) -> Panel {
    if application == Application::Ios && channel == ReleaseChannel::Nightly {
        return TextPanel {
            content: "## N/A".into(),
            mode: "markdown".into(),
            grid_pos: GridPos::height(8),
        }
        .into();
    }

    let column_name = metric.column_name();
    let query = Query {
        select: vec![
            "TIMESTAMP(submission_date) as time".into(),
            "engine_name".into(),
            metric.column_expr(),
        ],
        from: format!("({COMBINED_SUBQUERY})"),
        where_: vec![
            "$__timeFilter(TIMESTAMP(submission_date))".into(),
            format!("channel = '{channel}'"),
            match application {
                Application::Desktop => "application = 'desktop'",
                Application::Ios => "application = 'firefox-ios'",
                Application::Android => "application = 'firefox-android'",
            }
            .into(),
            engine_where_clause(config),
        ],
        order_by: Some("time".into()),
        ..Query::default()
    };

    Panel::from(TimeSeriesPanel {
        title: application.display_name(channel),
        grid_pos: GridPos::height(8),
        datasource: Datasource::bigquery(),
        // needs to be fairly large since the total sync count can be low on mobile/nightly
        interval: "1d".into(),
        targets: vec![Target::table(query.sql())],
        field_config: FieldConfig {
            defaults: FieldConfigDefaults {
                links: vec![DataLink {
                    url: UrlBuilder::new_dashboard(format!("{}-sync-extra", config.team_slug()))
                        .with_time_range_param()
                        .with_param("var-application", application.slug())
                        .with_param("var-channel", channel.to_string())
                        .with_param("var-engine", "${__data.fields[\"engine_name\"]}")
                        .build(),
                    target_blank: true,
                    one_click: true,
                    title: "Errors".into(),
                }],
                custom: metric.field_config_custom(),
                unit: metric.unit(),
            },
        },
        transformations: vec![
            Transformation::PartitionByValues {
                fields: vec!["engine_name".into()],
                keep_fields: true,
            },
            // Fixup the field names for better legend labels
            Transformation::RenameByRegex {
                regex: format!("{column_name} (.*)"),
                rename_pattern: "$1".into(),
            },
        ],
        ..TimeSeriesPanel::default()
    })
}

fn details_dash_count_panel(title: &str, metric: SyncMetric) -> Panel {
    let query = Query {
        select: vec![
            "TIMESTAMP(submission_date) as time".into(),
            metric.column_expr(),
        ],
        from: format!("(\n{COMBINED_SUBQUERY}\n)"),
        where_: vec![
            "$__timeFilter(TIMESTAMP(submission_date))".into(),
            "channel = '${channel}'".into(),
            "application=CASE '${application}'
                WHEN 'firefox_desktop' THEN 'desktop'
                WHEN 'firefox_android' THEN 'firefox-android'
                WHEN 'firefox_ios' THEN 'firefox-ios'
                ELSE '${application}'
            END"
            .into(),
            "engine_name = '${engine}'".into(),
        ],
        ..Query::default()
    };

    TimeSeriesPanel {
        title: title.into(),
        grid_pos: GridPos::height(10),
        datasource: Datasource::bigquery(),
        // needs to be fairly large since the total sync count can be low on mobile/nightly
        interval: "1d".into(),
        targets: vec![Target::table(query.sql())],
        transformations: vec![],
        field_config: FieldConfig {
            defaults: FieldConfigDefaults {
                links: vec![],
                custom: metric.field_config_custom(),
                unit: metric.unit(),
            },
        },
        ..TimeSeriesPanel::default()
    }
    .into()
}

fn details_dash_error_count_panel(config: &TeamConfig) -> Panel {
    let query = Query {
        select: vec![
            "error".into(),
            "$__timeGroup(submission_timestamp, $__interval) as time".into(),
            "COUNT(*) as count".into(),
        ],
        where_: vec![
            "application='${application}'".into(),
            "engine_name = '${engine}'".into(),
            "normalized_channel = '${channel}'".into(),
            "$__timeFilter(submission_timestamp)".into(),
        ],
        from: format!("(\n{}\n)", error_subquery(config)),
        group_by: Some("1, 2".into()),
        order_by: Some("count DESC".into()),
        ..Query::default()
    };

    TimeSeriesPanel {
        title: "Error counts by type".into(),
        grid_pos: GridPos::height(10),
        datasource: Datasource::bigquery(),
        // needs to be fairly large since the total sync count can be low on mobile/nightly
        interval: "1d".into(),
        targets: vec![Target::table(query.sql())],
        transformations: vec![
            Transformation::PartitionByValues {
                fields: vec!["error".into()],
                keep_fields: true,
            },
            Transformation::RenameByRegex {
                regex: "count (.*)".into(),
                rename_pattern: "$1".into(),
            },
        ],
        ..TimeSeriesPanel::default()
    }
    .into()
}

fn details_dash_error_log_panel(config: &TeamConfig) -> Panel {
    let query = Query {
        select: vec![
            "CONCAT(IFNULL(error, 'unknown'), ': ', IFNULL(details, 'unknown')) as message".into(),
            "submission_timestamp".into(),
        ],
        from: format!("(\n{}\n)", error_subquery(config)),
        where_: vec![
            "engine_name = '${engine}'".into(),
            "normalized_channel = '${channel}'".into(),
            "application='${application}'".into(),
            "$__timeFilter(submission_timestamp)".into(),
        ],
        order_by: Some("submission_timestamp DESC".into()),
        limit: Some(1000),
        ..Query::default()
    };

    LogPanel {
        title: "Error list".into(),
        grid_pos: GridPos::height(20),
        datasource: Datasource::bigquery(),
        targets: vec![Target::table(query.sql())],
        options: LogOptions {
            enable_log_details: false,
            ..LogOptions::default()
        },
        ..LogPanel::default()
    }
    .into()
}

// Subquery that combines errors from both the legacy and glean sync tables
fn error_subquery(config: &TeamConfig) -> String {
    let mut queries = vec![];

    // Desktop
    queries.push(
        "\
SELECT
  'firefox_desktop' as application,
  STRING(engine.name) AS engine_name,
  normalized_channel,
  JSON_VALUE(engine.failureReason, '$.name') AS error,
  JSON_VALUE(engine.failureReason, '$.error') AS details,
  submission_timestamp
FROM
  firefox_desktop.sync
CROSS JOIN
  UNNEST(JSON_QUERY_ARRAY(metrics.object.syncs_syncs)) AS syncs
CROSS JOIN
  UNNEST(JSON_QUERY_ARRAY(syncs,'$.engines')) AS engine
WHERE
  metrics IS NOT NULL
  AND engine.failureReason IS NOT NULL
  AND client_info.os NOT IN ('iOS', 'Android')"
            .to_string(),
    );

    queries.extend(
        config
            .components
            .iter()
            .flat_map(|c| c.sync_engines())
            // filter out desktop-only engines
            .filter(|c| **c != "rust-logins")
            .flat_map(|engine_name| {
                [
                    format!(
                        "\
    SELECT
        'firefox_android' as application,
        '{engine_name}' as engine_name,
        normalized_channel,
        failure_reason.key as error,
        failure_reason.value as details,
        submission_timestamp
    FROM mozdata.fenix.{engine_name}_sync
    CROSS JOIN UNNEST(metrics.labeled_string.{engine_name}_sync_v2_failure_reason) as failure_reason"
                    ),
                    format!(
                        "\
    SELECT
        'firefox_ios' as application,
        '{engine_name}' as engine_name,
        normalized_channel,
        failure_reason.key as error,
        failure_reason.value as details,
        submission_timestamp
    FROM mozdata.firefox_ios.{engine_name}_sync
    CROSS JOIN UNNEST(metrics.labeled_string.{engine_name}_sync_v2_failure_reason) as failure_reason"
                    ),
                ]
            }),
    );

    queries.join("\nUNION ALL\n")
}

fn sync_legacy_dashboard_panel() -> Panel {
    let content = "\
# Legacy Sync dashboards
* [iOS Credit Cards Verification Usage](https://sql.telemetry.mozilla.org/dashboard/ios-credit-cards-verification-usage)
* [Mobile Logins Verification Usage](https://sql.telemetry.mozilla.org/dashboard/mobile-logins-verification-usage?p_channel=org_mozilla_ios_firefox)
* [iOS FxA Keychain Rollout Enrollment](https://sql.telemetry.mozilla.org/dashboard/ios-credit-cards-key-regeneration-metrics)
";
    TextPanel {
        content: content.to_string(),
        mode: "markdown".into(),
        grid_pos: GridPos::height(8),
    }
    .into()
}

fn engine_where_clause(config: &TeamConfig) -> String {
    let mut engines: Vec<_> = config
        .components
        .iter()
        .flat_map(|c| c.sync_engines())
        .map(|e| format!("'{e}'"))
        .collect();
    engines.sort_unstable();
    engines.dedup();
    format!("engine_name IN ({})", engines.join(", "))
}

/// Subquery that combines the desktop and mobile ETL tables
const COMBINED_SUBQUERY: &str = "\
SELECT 
    submission_date,
    channel,
    application,
    engine_name,
    success_rate,
    avg_sync_time,
    count_total
FROM
    moz-fx-data-shared-prod.sync_derived.desktop_v1

UNION ALL

SELECT 
    submission_date,
    channel,
    application,
    engine_name,
    success_rate,
    0 as avg_sync_time, -- TODO: make this work on Mobile
    count_total
FROM
    moz-fx-data-shared-prod.sync_derived.mobile_v1
";
