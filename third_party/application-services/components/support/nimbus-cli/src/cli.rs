// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::{ffi::OsString, path::PathBuf};

use chrono::Utc;
use clap::{Args, Parser, Subcommand};

#[derive(Parser)]
#[command(
    author,
    long_about = r#"Mozilla Nimbus' command line tool for mobile apps"#
)]
pub(crate) struct Cli {
    /// The app name according to Nimbus.
    #[arg(short, long, value_name = "APP")]
    pub(crate) app: Option<String>,

    /// The channel according to Nimbus. This determines which app to talk to.
    #[arg(short, long, value_name = "CHANNEL")]
    pub(crate) channel: Option<String>,

    /// The device id of the simulator, emulator or device.
    #[arg(short, long, value_name = "DEVICE_ID")]
    pub(crate) device_id: Option<String>,

    #[command(subcommand)]
    pub(crate) command: CliCommand,
}

#[derive(Subcommand, Clone)]
pub(crate) enum CliCommand {
    /// Send a complete JSON file to the Nimbus SDK and apply it immediately.
    ApplyFile {
        /// The filename to be loaded into the SDK.
        file: PathBuf,

        /// Keeps existing enrollments and experiments before enrolling.
        ///
        /// This is unlikely what you want to do.
        #[arg(long, default_value = "false")]
        preserve_nimbus_db: bool,

        #[command(flatten)]
        open: OpenArgs,
    },

    /// Capture the logs into a file.
    CaptureLogs {
        /// The file to put the logs.
        file: PathBuf,
    },

    /// Print the defaults for the manifest.
    Defaults {
        /// An optional feature-id
        #[arg(short, long = "feature")]
        feature_id: Option<String>,

        /// An optional file to print the manifest defaults.
        #[arg(short, long, value_name = "OUTPUT_FILE")]
        output: Option<PathBuf>,

        #[command(flatten)]
        manifest: ManifestArgs,
    },

    /// Enroll into an experiment or a rollout.
    ///
    /// The experiment slug is a combination of the actual slug, and the server it came from.
    ///
    /// * `release`/`stage` determines the server.
    ///
    /// * `preview` selects the preview collection.
    ///
    /// These can be further combined: e.g. $slug, preview/$slug, stage/$slug, stage/preview/$slug
    Enroll {
        #[command(flatten)]
        experiment: ExperimentArgs,

        /// The branch slug.
        #[arg(short, long, value_name = "BRANCH")]
        branch: String,

        /// Optional rollout slugs, including the server and collection.
        #[arg(value_name = "ROLLOUTS")]
        rollouts: Vec<String>,

        /// Preserves the original experiment targeting
        #[arg(long, default_value = "false")]
        preserve_targeting: bool,

        /// Preserves the original experiment bucketing
        #[arg(long, default_value = "false")]
        preserve_bucketing: bool,

        #[command(flatten)]
        open: OpenArgs,

        /// Keeps existing enrollments and experiments before enrolling.
        ///
        /// This is unlikely what you want to do.
        #[arg(long, default_value = "false")]
        preserve_nimbus_db: bool,

        /// Don't validate the feature config files before enrolling
        #[arg(long, default_value = "false")]
        no_validate: bool,

        #[command(flatten)]
        manifest: ManifestArgs,
    },

    /// Print the feature configuration involved in the branch of an experiment.
    ///
    /// This can be optionally merged with the defaults from the feature manifest.
    Features {
        #[command(flatten)]
        manifest: ManifestArgs,

        #[command(flatten)]
        experiment: ExperimentArgs,

        /// The branch of the experiment
        #[arg(short, long)]
        branch: String,

        /// If set, then merge the experimental configuration with the defaults from the manifest
        #[arg(short, long, default_value = "false")]
        validate: bool,

        /// An optional feature-id: if it exists in this branch, print this feature
        /// on its own.
        #[arg(short, long = "feature")]
        feature_id: Option<String>,

        /// Print out the features involved in this branch as in a format:
        /// `{ $feature_id: $value }`.
        ///
        /// Automated tools should use this, since the output is predictable.
        #[arg(short, long = "multi", default_value = "false")]
        multi: bool,

        /// An optional file to print the output.
        #[arg(short, long, value_name = "OUTPUT_FILE")]
        output: Option<PathBuf>,
    },

    /// Fetch one or more named experiments and rollouts and put them in a file.
    Fetch {
        /// The file to download the recipes to.
        #[arg(short, long, value_name = "OUTPUT_FILE")]
        output: Option<PathBuf>,

        #[command(flatten)]
        experiment: ExperimentArgs,

        /// The recipe slugs, including server.
        ///
        /// Use once per recipe to download. e.g.
        /// fetch --output file.json preview/my-experiment my-rollout
        ///
        /// Cannot be used with the server option: use `fetch-list` instead.
        #[arg(value_name = "RECIPE")]
        recipes: Vec<String>,
    },

    /// Fetch a list of experiments and put it in a file.
    FetchList {
        /// The file to download the recipes to.
        #[arg(short, long, value_name = "OUTPUT_FILE")]
        output: Option<PathBuf>,

        #[command(flatten)]
        list: ExperimentListArgs,
    },

    /// Execute a nimbus-fml command. See
    ///
    /// nimbus-cli fml -- --help
    ///
    /// for more.
    Fml { args: Vec<OsString> },

    /// Displays information about an experiment
    Info {
        #[command(flatten)]
        experiment: ExperimentArgs,

        /// An optional file to print the output.
        #[arg(short, long, value_name = "OUTPUT_FILE")]
        output: Option<PathBuf>,
    },

    /// List the experiments from a server
    List {
        #[command(flatten)]
        list: ExperimentListArgs,
    },

    /// Print the state of the Nimbus database to logs.
    ///
    /// This causes a restart of the app.
    LogState {
        #[command(flatten)]
        open: OpenArgs,
    },

    /// Open the app without changing the state of experiment enrollments.
    Open {
        #[command(flatten)]
        open: OpenArgs,

        /// By default, the app is terminated before sending the a deeplink.
        ///
        /// If this flag is set, then do not terminate the app if it is already running.
        #[arg(long, default_value = "false")]
        no_clobber: bool,
    },

    /// Start a server
    #[cfg(feature = "server")]
    StartServer,

    /// Reset the app back to its just installed state
    ResetApp,

    /// Follow the logs for the given app.
    TailLogs,

    /// Configure an application feature with one or more feature config files.
    ///
    /// One file per branch. The branch slugs will correspond to the file names.
    ///
    /// By default, the files are validated against the manifest; this can be
    /// overridden with `--no-validate`.
    TestFeature {
        /// The identifier of the feature to configure
        feature_id: String,

        /// One or more files containing a feature config for the feature.
        files: Vec<PathBuf>,

        /// An optional patch file, used to patch feature configurations
        ///
        /// This is of the format that comes from the
        /// `features --multi` or `defaults` commands.
        #[arg(long, value_name = "PATCH_FILE")]
        patch: Option<PathBuf>,

        #[command(flatten)]
        open: OpenArgs,

        /// Don't validate the feature config files before enrolling
        #[arg(long, default_value = "false")]
        no_validate: bool,

        #[command(flatten)]
        manifest: ManifestArgs,
    },

    /// Unenroll from all experiments and rollouts
    Unenroll {
        #[command(flatten)]
        open: OpenArgs,
    },

    /// Validate an experiment against a feature manifest
    Validate {
        #[command(flatten)]
        experiment: ExperimentArgs,

        #[command(flatten)]
        manifest: ManifestArgs,
    },

    /// Evaluate a JEXL expression against the app context
    EvalJexl {
        /// The JEXL expression to evaluate
        expression: String,

        #[command(flatten)]
        open: OpenArgs,
    },
}

#[derive(Args, Clone, Debug, Default)]
pub(crate) struct ManifestArgs {
    /// An optional manifest file
    #[arg(long, value_name = "MANIFEST_FILE")]
    pub(crate) manifest: Option<String>,

    /// An optional version of the app.
    /// If present, constructs the `ref` from an app specific template.
    /// Due to inconsistencies in branching names, this isn't always
    /// reliable.
    #[arg(long, value_name = "APP_VERSION")]
    pub(crate) version: Option<String>,

    /// The branch/tag/commit for the version of the manifest
    /// to get from Github.
    #[arg(long, value_name = "APP_VERSION", default_value = "main")]
    pub(crate) ref_: String,
}

#[derive(Args, Clone, Debug, Default)]
pub(crate) struct OpenArgs {
    /// Optional deeplink. If present, launch with this link.
    #[arg(long, value_name = "DEEPLINK")]
    pub(crate) deeplink: Option<String>,

    /// Resets the app back to its initial state before launching
    #[arg(long, default_value = "false")]
    pub(crate) reset_app: bool,

    /// Instead of opening via adb or xcrun simctl, construct a deeplink
    /// and put it into the pastebuffer.
    ///
    /// If present, then the app is not launched, so this option does not work with
    /// `--reset-app` or passthrough arguments.
    #[arg(long, default_value = "false")]
    pub(crate) pbcopy: bool,

    /// Instead of opening via adb or xcrun simctl, construct a deeplink
    /// and put it into the pastebuffer.
    ///
    /// If present, then the app is not launched, so this option does not work with
    /// `--reset-app` or passthrough arguments.
    #[arg(long, default_value = "false")]
    pub(crate) pbpaste: bool,

    /// Optionally, add platform specific arguments to the adb or xcrun command.
    ///
    /// By default, arguments are added to the end of the command, likely to be passed
    /// directly to the app.
    ///
    /// Arguments before a special placeholder `{}` are passed to
    /// `adb am start` or `xcrun simctl launch` commands directly.
    #[arg(last = true, value_name = "PASSTHROUGH_ARGS")]
    pub(crate) passthrough: Vec<String>,

    /// Attempt to trigger Firefox via the legacy codepath.
    ///
    /// This is required for some older Firefox versions.
    #[arg(long = "legacy-open")]
    pub(crate) legacy_open_mode: bool,

    /// An optional file to dump experiments into.
    ///
    /// If present, then the app is not launched, so this option does not work with
    /// `--reset-app` or passthrough arguments.
    #[arg(long, value_name = "OUTPUT_FILE")]
    pub(crate) output: Option<PathBuf>,
}

#[derive(Args, Clone, Debug, Default)]
pub(crate) struct ExperimentArgs {
    /// The experiment slug, including the server and collection.
    #[arg(value_name = "EXPERIMENT_SLUG")]
    pub(crate) experiment: String,

    /// An optional file from which to get the experiment.
    ///
    /// By default, the file is fetched from the server.
    #[arg(long, value_name = "EXPERIMENTS_FILE")]
    pub(crate) file: Option<PathBuf>,

    /// Use remote settings to fetch the experiment recipe.
    ///
    /// By default, the file is fetched from the v6 api of experimenter.
    #[arg(long, default_value = "false")]
    pub(crate) use_rs: bool,

    /// An optional patch file, used to patch feature configurations
    ///
    /// This is of the format that comes from the
    /// `features --multi` or `defaults` commands.
    #[arg(long, value_name = "PATCH_FILE")]
    pub(crate) patch: Option<PathBuf>,
}

#[derive(Args, Clone, Debug, Default)]
pub(crate) struct ExperimentListArgs {
    #[command(flatten)]
    pub(crate) source: ExperimentListSourceArgs,

    #[command(flatten)]
    pub(crate) filter: ExperimentListFilterArgs,
}

#[derive(Args, Clone, Debug, Default)]
pub(crate) struct ExperimentListSourceArgs {
    /// A server slug e.g. preview, release, stage, stage/preview
    #[arg(default_value = "")]
    pub(crate) server: String,

    /// An optional file
    #[arg(short, long, value_name = "FILE")]
    pub(crate) file: Option<PathBuf>,

    /// Use the v6 API to fetch the experiment recipes.
    ///
    /// By default, the file is fetched from the Remote Settings.
    ///
    /// The API contains *all* launched experiments, past and present,
    /// so this is considerably slower and longer than Remote Settings.
    #[arg(long, default_value = "false")]
    pub(crate) use_api: bool,
}

#[derive(Args, Clone, Debug, Default)]
pub(crate) struct ExperimentListFilterArgs {
    #[arg(short = 'S', long, value_name = "SLUG_PATTERN")]
    pub(crate) slug: Option<String>,

    #[arg(short = 'F', long, value_name = "FEATURE_PATTERN")]
    pub(crate) feature: Option<String>,

    #[arg(short = 'A', long, value_name = "DATE", value_parser=validate_date)]
    pub(crate) active_on: Option<String>,

    #[arg(short = 'E', long, value_name = "DATE", value_parser=validate_date)]
    pub(crate) enrolling_on: Option<String>,

    #[arg(short = 'C', long, value_name = "CHANNEL")]
    pub(crate) channel: Option<String>,

    #[arg(short = 'R', long, value_name = "FLAG")]
    pub(crate) is_rollout: Option<bool>,
}

fn validate_num(s: &str, l: usize) -> Result<(), &'static str> {
    if !s.chars().all(char::is_numeric) {
        Err("String contains non-numeric characters")
    } else if s.len() != l {
        Err("String is the wrong length")
    } else {
        Ok(())
    }
}

fn validate_date_parts(yyyy: &str, mm: &str, dd: &str) -> Result<(), &'static str> {
    validate_num(yyyy, 4)?;
    validate_num(mm, 2)?;
    validate_num(dd, 2)?;
    Ok(())
}

fn validate_date(s: &str) -> Result<String, String> {
    if s == "today" {
        let now = Utc::now();
        return Ok(format!("{}", now.format("%Y-%m-%d")));
    }
    match s.splitn(3, '-').collect::<Vec<_>>().as_slice() {
        [yyyy, mm, dd] if validate_date_parts(yyyy, mm, dd).is_ok() => Ok(s.to_string()),
        _ => Err("Date string must be yyyy-mm-dd".to_string()),
    }
}
