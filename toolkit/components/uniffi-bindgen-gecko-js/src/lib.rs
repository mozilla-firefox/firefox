/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::fs;
use std::io::Write;

use anyhow::{Context, Result};
use askama::Template;
use camino::{Utf8Path, Utf8PathBuf};
use clap::Parser;
use indexmap::IndexMap;
use serde::{Deserialize, Serialize};
use uniffi_bindgen::{pipeline::initial, GlobalConfig};
use uniffi_pipeline::PrintOptions;

mod bindgen_paths;
pub mod filters;
pub mod pipeline;

use bindgen_paths::gecko_js_bindgen_paths;
use pipeline::{gecko_js_pipeline, nodes::*, GeckoPipeline};
use uniffi_pipeline::Node;

#[derive(Debug, Parser)]
#[clap(name = "uniffi-bindgen-gecko-js")]
#[clap(version = clap::crate_version!())]
#[clap(about = "JS bindings generator for Rust")]
#[clap(propagate_version = true)]
struct CliArgs {
    // This is a really convoluted set of arguments, but we're only expecting to be called by
    // `mach_commands.py`
    #[clap(long, value_name = "FILE")]
    library_path: Utf8PathBuf,

    #[clap(long, value_name = "FILE")]
    fixtures_library_path: Utf8PathBuf,

    #[clap(subcommand)]
    command: Command,
}

#[derive(clap::Subcommand, Debug, Clone)]
enum Command {
    /// Generate new C++ and JS bindings
    Generate(GenerateArgs),
    /// Execute the UniFFI pipeline for debugging purposes
    Pipeline(PipelineArgs),
}

#[derive(clap::Args, Debug, Clone)]
struct GenerateArgs {
    #[clap(long, value_name = "FILE")]
    js_dir: Utf8PathBuf,
    #[clap(long, value_name = "FILE")]
    fixture_js_dir: Utf8PathBuf,
    #[clap(long, value_name = "FILE")]
    cpp_path: Utf8PathBuf,
    #[clap(long, value_name = "FILE")]
    docs_path: Utf8PathBuf,
}

#[derive(clap::Args, Debug, Clone)]
struct PipelineArgs {
    /// Only show passes that match <PASS>
    ///
    /// Use `last` to only show the last pass, this can be useful when you're writing new pipelines
    #[clap(short, long)]
    pass: Option<String>,

    /// Only show data for types with name <FILTER_TYPE>
    #[clap(short = 't', long = "type")]
    filter_type: Option<String>,

    /// Only show data for items with fields that match <FILTER>
    #[clap(short = 'n', long = "name")]
    filter_name: Option<String>,

    /// Only show data for items with fields that match <FILTER>
    #[clap(long)]
    no_diff: bool,
}

/// Configuration for a single Component
#[derive(Clone, Default, Debug, Deserialize, Serialize, Node)]
pub struct Config {
    #[serde(default)]
    pub async_wrappers: IndexMap<String, ConcurrencyMode>,
    #[serde(default)]
    custom_types: IndexMap<String, CustomTypeConfig>,
}

/// Callable sync/async configuration, from `config.toml.`
#[derive(Copy, Clone, Debug, Deserialize, Serialize, Node, PartialEq, Eq)]
#[serde(rename_all = "PascalCase")]
pub enum ConcurrencyMode {
    /// Sync function that will remain synchronous, running on the main thread.
    Sync,
    /// Async function will remain asynchronous.
    Async,
    /// Rust sync call that's wrapped to be async.
    /// The C++ code schedules the call in a worker thread and returns the result to JS via a
    /// promise.
    ///
    /// Used to adapt blocking Rust sync functions to be async.
    /// This way they don't block the JS main thread.
    ///
    /// Only valid for Rust calls, not callback interfaces or trait interfaces with foreign support.
    AsyncWrapped,
    /// JS sync callback method that's adapted to be "fire-and-forget".
    /// The C++ code schedules the call, then immediately returns void.
    ///
    /// Used to adapt JS sync methods that we don't want to wait for (e.g. logging calls).
    ///
    /// Only valid for callback interface methods.
    FireAndForget,
}

#[derive(Clone, Debug, Deserialize, Serialize, Node)]
struct CustomTypeConfig {
    /// The name of the type in JavaScript
    #[serde(default)]
    type_name: Option<String>,
    /// Modules to import (e.g., for Node.js require statements)
    #[serde(default)]
    imports: Vec<String>,
    /// Expression to convert from FFI type to JS type
    /// {} will be replaced with the value
    lift: String,
    /// Expression to convert from JS type to FFI type
    lower: String,
}

fn render(out_path: &Utf8Path, template: impl Template) -> Result<()> {
    let contents = template.render()?;
    let mut f = fs::File::create(out_path)
        .context(format!("Failed to create file {:?}", out_path.file_name()))?;
    writeln!(f, "{}", contents).context(format!("Failed to write file {}", out_path))?;
    println!("Generated: {out_path}");
    Ok(())
}

pub fn run_main() -> Result<()> {
    let args = CliArgs::parse();
    let (root, pipeline) = root_and_pipeline(args.library_path, args.fixtures_library_path)?;

    match args.command {
        Command::Generate(generate_args) => run_generate(root, pipeline, generate_args),
        Command::Pipeline(pipeline_args) => run_pipeline(root, pipeline, pipeline_args),
    }
}

fn run_generate(
    root: initial::Root,
    mut pipeline: GeckoPipeline,
    args: GenerateArgs,
) -> Result<()> {
    let root = pipeline.execute(root)?;
    render(&args.cpp_path, CppScaffolding { root: &root })?;
    for namespace in root.namespaces() {
        let dir = if namespace.is_fixture() {
            &args.fixture_js_dir
        } else {
            &args.js_dir
        };
        render(&dir.join(&namespace.js_filename()), namespace)?;
    }
    for entry in fs::read_dir(&args.docs_path)? {
        let path = entry?.path();
        if path.file_name().unwrap() != "index.md" {
            fs::remove_file(path)?;
        }
    }
    for module_docs in root.module_docs.iter() {
        render(&args.docs_path.join(&module_docs.filename), module_docs)?;
    }
    Ok(())
}

fn run_pipeline(
    root: initial::Root,
    mut pipeline: GeckoPipeline,
    args: PipelineArgs,
) -> Result<()> {
    let opts = PrintOptions {
        pass: args.pass,
        no_diff: args.no_diff,
        filter_type: args.filter_type,
        filter_name: args.filter_name,
    };
    pipeline.print_passes(root, opts)
}

fn root_and_pipeline(
    library_path: Utf8PathBuf,
    fixtures_library_path: Utf8PathBuf,
) -> Result<(initial::Root, GeckoPipeline)> {
    let bindgen_paths = gecko_js_bindgen_paths()?;
    let config = GlobalConfig::default();
    let root = initial::Root::from_library(&bindgen_paths, &config, &library_path, None)?;
    let fixtures_root =
        initial::Root::from_library(&bindgen_paths, &config, &fixtures_library_path, None)?;
    let root = initial::Root {
        namespaces: root
            .namespaces
            .into_iter()
            .chain(fixtures_root.namespaces)
            .filter(|(name, _)| name != "glean")
            .collect(),
        cdylib: None,
    };
    let pipeline = gecko_js_pipeline(toml::from_str(include_str!("../config.toml"))?);
    Ok((root, pipeline))
}

#[derive(Debug, Clone, Template)]
#[template(path = "cpp/UniFFIScaffolding.cpp", escape = "none")]
struct CppScaffolding<'a> {
    root: &'a pipeline::Root,
}
