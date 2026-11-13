/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use clap::{Args, Parser, Subcommand, ValueEnum};

use crate::intermediate_representation::TargetLanguage;

#[derive(Parser)]
#[command(name = "nimbus-fml")]
#[command(author = "nimbus-dev@mozilla.com")]
/// Tool for working with Nimbus Feature Manifests
pub struct App {
    #[clap(subcommand)]
    pub subcommand: Command,
}

#[derive(Subcommand)]
pub enum Command {
    /// Generate feature structs against the Feature Variables API.
    Generate(Generate),

    /// Generate a version of this manifest compatible with Experimenter's format.
    GenerateExperimenter(GenerateExperimenter),

    /// Get the input file, with the same rules that govern how FilePaths work.
    Fetch(Fetch),

    /// Create a single file out of the given manifest, suited for production environments where
    /// only one file is allowed, and only one channel is needed.
    SingleFile(SingleFile),

    /// Validate an FML configuration and all of its channels.
    Validate(Validate),

    /// Check an FML configuration against the Nimbus feature design lints.
    Lint(Lint),

    /// Print out all the channels to stdout, as JSON or one-per-line
    Channels(Channels),

    /// Prints out information about the manifest
    Info(Info),
}

#[derive(Args)]
pub struct Generate {
    /// Sets the input file to use
    #[arg(value_name = "INPUT")]
    pub input: String,

    /// The file or directory where generated code is created
    #[arg(value_name = "OUTPUT")]
    pub output: String,

    /// The language of the output file
    #[arg(long)]
    pub language: Option<Language>,

    /// The channel to generate the defaults for
    #[arg(long)]
    pub channel: String,

    #[command(flatten)]
    pub loader_info: LoaderInfo,
}

#[derive(Args)]
pub struct GenerateExperimenter {
    /// Sets the input file to use
    #[arg(value_name = "INPUT")]
    pub input: String,

    /// The file or directory where generated code is created
    #[arg(value_name = "OUTPUT")]
    pub output: String,

    /// Deprecated: The channel to generate the defaults for. This can be omitted.
    #[arg(long)]
    // This is no longer needed, but we keep it for backward compatibility.
    pub channel: Option<String>,

    #[command(flatten)]
    pub loader_info: LoaderInfo,
}

#[derive(Args)]
pub struct Fetch {
    /// Sets the input file to use
    #[arg(value_name = "INPUT")]
    pub input: String,

    #[command(flatten)]
    pub loader_info: LoaderInfo,
}

#[derive(Args)]
pub struct SingleFile {
    /// Sets the input file to use
    #[arg(value_name = "INPUT")]
    pub input: String,

    /// The file or directory where generated code is created
    #[arg(value_name = "OUTPUT")]
    pub output: String,

    /// The channel to generate the defaults for
    #[arg(long)]
    pub channel: Option<String>,

    #[command(flatten)]
    pub loader_info: LoaderInfo,
}

#[derive(Args)]
pub struct Validate {
    /// Sets the input file to use
    #[arg(value_name = "INPUT")]
    pub input: String,

    #[command(flatten)]
    pub loader_info: LoaderInfo,
}

#[derive(Args)]
pub struct Lint {
    /// Sets the input file to use
    #[arg(value_name = "INPUT", required_unless_present = "list")]
    pub input: Option<String>,

    #[command(flatten)]
    pub loader_info: LoaderInfo,

    /// Switch a lint off for this run. May be repeated.
    #[arg(long, value_name = "LINT")]
    pub allow: Vec<String>,

    /// Turn a lint into an error for this run. May be repeated.
    #[arg(long, value_name = "LINT")]
    pub deny: Vec<String>,

    /// Exit with an error if there are any warnings.
    #[arg(long)]
    pub error_on_warning: bool,

    /// Also lint the features of imported manifests.
    #[arg(long)]
    pub include_imports: bool,

    /// If present, then print the findings as JSON.
    #[arg(long)]
    pub json: bool,

    /// Print the available lints and exit.
    #[arg(long)]
    pub list: bool,
}

#[derive(Args)]
pub struct Channels {
    /// Sets the input file to use
    #[arg(value_name = "INPUT")]
    pub input: String,

    #[command(flatten)]
    pub loader_info: LoaderInfo,

    /// If present, then print the channels as JSON. If not, then print one per line.
    #[arg(long)]
    pub json: bool,
}

#[derive(Args)]
pub struct Info {
    /// Sets the input file to use
    #[arg(value_name = "INPUT")]
    pub input: String,

    /// The channel to generate the defaults for
    #[arg(long)]
    pub channel: Option<String>,

    /// Print the info of one feature only, if present
    #[arg(long)]
    pub feature: Option<String>,

    #[command(flatten)]
    pub loader_info: LoaderInfo,

    /// If present, then print the channels as JSON. If not, then print one per line.
    #[arg(long)]
    pub json: bool,
}

#[derive(Clone, Copy, ValueEnum)]
pub enum Language {
    Swift,
    Kotlin,
}

#[derive(Args)]
pub struct LoaderInfo {
    /// The directory where downloaded files are cached
    #[arg(long)]
    pub cache_dir: Option<String>,

    /// The file containing the version/refs/locations for other repos
    #[arg(long)]
    pub repo_file: Vec<String>,

    /// If INPUT is a remote file, then use this as the tag or branch name.
    #[arg(long = "ref")]
    pub ref_: Option<String>,

    #[arg(long)]
    /// Enable lax gecko-pref validation.
    pub lax_gecko_pref_validation: bool,
}

impl From<Language> for TargetLanguage {
    fn from(lang: Language) -> Self {
        match lang {
            Language::Swift => Self::Swift,
            Language::Kotlin => Self::Kotlin,
        }
    }
}
