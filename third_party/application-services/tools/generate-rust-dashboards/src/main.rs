/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::fs;

use anyhow::{anyhow, Result};
use camino::Utf8PathBuf;
use clap::Parser;

pub mod component_config;
pub mod config;
mod main_dashboard;
pub mod metrics;
pub mod schema;
mod sql;
mod team_config;
pub mod util;

#[derive(Parser, Debug)]
#[clap(name = "generate-rust-dashboard")]
struct Cli {
    /// Your team name
    team_name: String,

    /// Directory to write JSON files to
    output_dir: Utf8PathBuf,
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    let config = team_config::all_dashboards()
        .into_iter()
        .find(|d| {
            d.team_name
                .to_ascii_lowercase()
                .contains(&cli.team_name.to_ascii_lowercase())
        })
        .ok_or_else(|| anyhow!("Dashboard not found: {}", cli.team_name))?;

    let mut main_dashboard_builder = main_dashboard::start_dashboard(&config);
    let mut extra_dashboards: Vec<schema::Dashboard> = vec![];

    if config.sync_metrics {
        metrics::sync::add_to_main_dashboard(&mut main_dashboard_builder, &config)?;
        extra_dashboards.push(metrics::sync::extra_dashboard(&config)?);
    }

    if config.component_errors {
        metrics::rust_component_errors::add_to_dashboard(&mut main_dashboard_builder, &config)?;
        extra_dashboards.push(metrics::rust_component_errors::extra_dashboard(&config)?);
    }

    for metric in config.main_dashboard_metrics.iter() {
        metric.add_to_dashboard(&mut main_dashboard_builder, &config)?;
    }

    for extra_dash_config in config.extra_dashboards.iter() {
        let mut builder = schema::DashboardBuilder::new(
            extra_dash_config.name,
            util::slug(extra_dash_config.name),
        );
        for metric in extra_dash_config.metrics.iter() {
            metric.add_to_dashboard(&mut builder, &config)?;
        }
        extra_dashboards.push(builder.dashboard);
    }

    if !cli.output_dir.exists() {
        fs::create_dir_all(&cli.output_dir)?;
    }

    println!();
    println!("Generating Dashboards:");
    let dashboards = std::iter::once(main_dashboard_builder.dashboard).chain(extra_dashboards);
    for dashboard in dashboards {
        let mut value = serde_json::to_value(&dashboard)?;
        value.sort_all_objects();
        let content = serde_json::to_string_pretty(&value)?;
        let path = cli.output_dir.join(format!("{}.json", dashboard.uid));
        fs::write(&path, content)?;
        println!("{path}");
    }

    println!();
    println!("* Go to https://yardstick.mozilla.org/dashboards");
    println!("* Create/navigate to a folder for your team");
    println!("* Click New -> Import and import each generated JSON file");

    Ok(())
}
