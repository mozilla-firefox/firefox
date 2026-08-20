/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

extern crate libsqlite3_sys;

use anyhow::Result;
use clap::{Parser, Subcommand};
use merino::curated_recommendations::models::request::{
    CuratedRecommendationsConfig, CuratedRecommendationsRequest,
};
use merino::curated_recommendations::CuratedRecommendationsClient;
use merino::suggest::{SuggestClient, SuggestConfig, SuggestOptions};
use viaduct::{configure_ohttp_channel, OhttpConfig};

#[derive(Debug, Parser)]
struct Cli {
    /// Optional base host (defaults to prod)
    #[arg(long)]
    base_host: Option<String>,

    #[command(subcommand)]
    command: Commands,
}

#[derive(Debug, Subcommand)]
enum Commands {
    /// Fetch curated recommendations
    Recommendations {
        /// Required user agent header
        #[arg(long)]
        user_agent: String,

        /// JSON string of type CuratedRecommendationsRequest
        #[clap(long)]
        json: Option<String>,

        /// Path to a JSON file containing the request
        #[clap(long, value_name = "FILE")]
        json_file: Option<std::path::PathBuf>,
    },
    /// Fetch suggestions
    Suggest {
        /// OHTTP relay URL
        #[arg(long, default_value = "https://ohttp-merino.mozilla.fastly-edge.com")]
        relay_url: String,

        /// OHTTP gateway host
        #[arg(long, default_value = "ohttp-gateway-merino.services.mozilla.com")]
        gateway_host: String,

        /// Search query
        #[arg(long)]
        query: String,

        /// List of providers (e.g. --providers wikipedia --providers adm)
        #[arg(long)]
        providers: Option<Vec<String>>,

        /// Source identifier sent to Merino (e.g urlbar, new tab. defaults to unknown)
        #[arg(long)]
        source: Option<String>,

        /// Country code (e.g. "US")
        #[arg(long)]
        country: Option<String>,

        /// Region code (e.g. "CA")
        #[arg(long)]
        region: Option<String>,

        /// City name
        #[arg(long)]
        city: Option<String>,

        /// Client variants (e.g. --client-variants control --client-variants treatment)
        #[arg(long)]
        client_variants: Option<Vec<String>>,

        /// Request type (e.g location | weather)
        #[arg(long)]
        request_type: Option<String>,

        /// Accept-Language header value (e.g. "en-US")
        #[arg(long)]
        accept_language: Option<String>,
    },
}

fn main() -> Result<()> {
    viaduct_hyper::viaduct_init_backend_hyper();

    let cli = Cli::parse();

    match cli.command {
        Commands::Recommendations {
            user_agent,
            json,
            json_file,
        } => {
            let config = CuratedRecommendationsConfig {
                base_host: cli.base_host,
                user_agent_header: user_agent,
            };
            let client = CuratedRecommendationsClient::new(config)?;
            let json_data = match (json_file, json) {
                (Some(path), _) => std::fs::read_to_string(path)?,
                (None, Some(raw)) => raw,
                (None, None) => anyhow::bail!("You must provide either --json or --json-file"),
            };
            let request: CuratedRecommendationsRequest = serde_json::from_str(&json_data)?;
            let response = client.get_curated_recommendations(&request)?;
            println!("{}", serde_json::to_string_pretty(&response)?);
        }
        Commands::Suggest {
            relay_url,
            gateway_host,
            query,
            providers,
            source,
            country,
            region,
            city,
            client_variants,
            request_type,
            accept_language,
        } => {
            configure_ohttp_channel(
                "merino".to_string(),
                OhttpConfig {
                    relay_url,
                    gateway_host,
                },
            )?;
            let config = SuggestConfig {
                base_host: cli.base_host,
            };
            let client = SuggestClient::new(config)?;
            let options = SuggestOptions {
                providers,
                source,
                country,
                region,
                city,
                client_variants,
                request_type,
                accept_language,
            };
            match client.get_suggestions(query, options)? {
                Some(response) => {
                    let json: serde_json::Value = serde_json::from_str(&response)?;
                    println!("{}", serde_json::to_string_pretty(&json)?);
                }
                None => println!("No suggestions available (204 No Content)"),
            }
        }
    }

    Ok(())
}
