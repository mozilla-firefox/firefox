// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

mod cli;
mod cmd;
mod config;
mod feature_utils;
mod output;
mod protocol;
mod sources;
mod updater;
mod value_utils;
mod version_utils;

use anyhow::{bail, Result};
use clap::Parser;
use cli::{Cli, CliCommand, ExperimentArgs, OpenArgs};
use sources::{ExperimentListSource, ExperimentSource, ManifestSource};
use std::{ffi::OsString, path::PathBuf};

pub(crate) static USER_AGENT: &str =
    concat!(env!("CARGO_PKG_NAME"), "/", env!("CARGO_PKG_VERSION"),);

fn main() -> Result<()> {
    viaduct_backend_rust::viaduct_init_backend_rust();
    let cmds = get_commands_from_cli(std::env::args_os())?;
    for c in cmds {
        let success = cmd::process_cmd(&c)?;
        if !success {
            bail!("Failed");
        }
    }
    updater::check_for_update();
    Ok(())
}

fn get_commands_from_cli<I, T>(args: I) -> Result<Vec<AppCommand>>
where
    I: IntoIterator<Item = T>,
    T: Into<OsString> + Clone,
{
    let cli = Cli::try_parse_from(args)?;

    let mut commands: Vec<AppCommand> = Default::default();

    // We do this here to ensure that all the command line is valid
    // with respect to the main command. We do this here because
    // as the cli has expanded, we've changed when we need `--app`
    // and `--channel`. We catch those types of errors early by doing this
    // here.
    let main_command = AppCommand::try_from(&cli)?;

    // Validating the command line args. Most of this should be done with clap,
    // but for everything else there's:
    cli.command.check_valid(cli.app.as_deref())?;

    // Validating experiments against manifests
    commands.push(AppCommand::try_validate(&cli)?);

    if cli.command.should_kill(cli.app.as_deref()) {
        let app = LaunchableApp::try_from(&cli)?;
        commands.push(AppCommand::Kill { app });
    }
    if cli.command.should_reset() {
        let app = LaunchableApp::try_from(&cli)?;
        commands.push(AppCommand::Reset { app });
    }
    commands.push(main_command);

    Ok(commands)
}

#[derive(Clone, Debug, PartialEq)]
enum LaunchableApp {
    Android {
        package_name: String,
        activity_name: String,
        device_id: Option<String>,
        scheme: Option<String>,
        open_deeplink: Option<String>,
    },
    Ios {
        device_id: String,
        app_id: String,
        scheme: Option<String>,
    },
}

#[derive(Clone, Debug, PartialEq)]
pub(crate) struct NimbusApp {
    app_name: Option<String>,
    channel: Option<String>,
}

impl NimbusApp {
    #[cfg(test)]
    fn new(app: &str, channel: &str) -> Self {
        Self {
            app_name: Some(app.to_string()),
            channel: Some(channel.to_string()),
        }
    }

    fn channel(&self) -> Option<String> {
        self.channel.clone()
    }
    fn app_name(&self) -> Option<String> {
        self.app_name.clone()
    }
}

impl From<&Cli> for NimbusApp {
    fn from(value: &Cli) -> Self {
        Self {
            channel: value.channel.clone(),
            app_name: value.app.clone(),
        }
    }
}

#[derive(Debug, PartialEq)]
enum AppCommand {
    ApplyFile {
        app: LaunchableApp,
        open: AppOpenArgs,
        list: ExperimentListSource,
        preserve_nimbus_db: bool,
    },

    CaptureLogs {
        app: LaunchableApp,
        file: PathBuf,
    },

    Defaults {
        manifest: ManifestSource,
        feature_id: Option<String>,
        output: Option<PathBuf>,
    },

    Enroll {
        app: LaunchableApp,
        params: NimbusApp,
        experiment: ExperimentSource,
        rollouts: Vec<ExperimentSource>,
        branch: String,
        preserve_targeting: bool,
        preserve_bucketing: bool,
        preserve_nimbus_db: bool,
        open: AppOpenArgs,
    },

    ExtractFeatures {
        experiment: ExperimentSource,
        branch: String,
        manifest: ManifestSource,

        feature_id: Option<String>,
        validate: bool,
        multi: bool,

        output: Option<PathBuf>,
    },

    FetchList {
        list: ExperimentListSource,
        file: Option<PathBuf>,
    },

    FmlPassthrough {
        args: Vec<OsString>,
        cwd: PathBuf,
    },

    Info {
        experiment: ExperimentSource,
        output: Option<PathBuf>,
    },

    Kill {
        app: LaunchableApp,
    },

    List {
        list: ExperimentListSource,
    },

    LogState {
        app: LaunchableApp,
        open: AppOpenArgs,
    },

    // No Op, does nothing.
    NoOp,

    Open {
        app: LaunchableApp,
        open: AppOpenArgs,
    },

    Reset {
        app: LaunchableApp,
    },

    #[cfg(feature = "server")]
    StartServer,

    TailLogs {
        app: LaunchableApp,
    },

    Unenroll {
        app: LaunchableApp,
        open: AppOpenArgs,
    },

    ValidateExperiment {
        params: NimbusApp,
        manifest: ManifestSource,
        experiment: ExperimentSource,
    },

    EvalJexl {
        app: LaunchableApp,
        expression: String,
        open: AppOpenArgs,
    },
}

impl AppCommand {
    fn try_validate(cli: &Cli) -> Result<Self> {
        let params = cli.into();
        Ok(match &cli.command {
            CliCommand::Enroll {
                no_validate,
                manifest,
                ..
            }
            | CliCommand::TestFeature {
                no_validate,
                manifest,
                ..
            } if !no_validate => {
                let experiment = ExperimentSource::try_from(cli)?;
                let manifest = ManifestSource::try_from(&params, manifest)?;
                AppCommand::ValidateExperiment {
                    params,
                    experiment,
                    manifest,
                }
            }
            CliCommand::Validate { manifest, .. } => {
                let experiment = ExperimentSource::try_from(cli)?;
                let manifest = ManifestSource::try_from(&params, manifest)?;
                AppCommand::ValidateExperiment {
                    params,
                    experiment,
                    manifest,
                }
            }
            _ => Self::NoOp,
        })
    }
}

impl TryFrom<&Cli> for AppCommand {
    type Error = anyhow::Error;

    fn try_from(cli: &Cli) -> Result<Self> {
        let params = NimbusApp::from(cli);
        Ok(match cli.command.clone() {
            CliCommand::ApplyFile {
                file,
                preserve_nimbus_db,
                open,
            } => {
                let app = LaunchableApp::try_from(cli)?;
                let list = ExperimentListSource::try_from(file.as_path())?;
                AppCommand::ApplyFile {
                    app,
                    open: open.into(),
                    list,
                    preserve_nimbus_db,
                }
            }
            CliCommand::CaptureLogs { file } => {
                let app = LaunchableApp::try_from(cli)?;
                AppCommand::CaptureLogs { app, file }
            }
            CliCommand::Defaults {
                feature_id,
                output,
                manifest,
            } => {
                let manifest = ManifestSource::try_from(&params, &manifest)?;
                AppCommand::Defaults {
                    manifest,
                    feature_id,
                    output,
                }
            }
            CliCommand::Enroll {
                branch,
                rollouts,
                preserve_targeting,
                preserve_bucketing,
                preserve_nimbus_db,
                experiment,
                open,
                ..
            } => {
                let app = LaunchableApp::try_from(cli)?;
                // Ensure we get the rollouts from the same place we get the experiment from.
                let mut recipes: Vec<ExperimentSource> = Vec::new();
                for r in rollouts {
                    let rollout = ExperimentArgs {
                        experiment: r,
                        ..experiment.clone()
                    };
                    recipes.push(ExperimentSource::try_from(&rollout)?);
                }

                let experiment = ExperimentSource::try_from(cli)?;

                Self::Enroll {
                    app,
                    params,
                    experiment,
                    branch,
                    rollouts: recipes,
                    preserve_targeting,
                    preserve_bucketing,
                    preserve_nimbus_db,
                    open: open.into(),
                }
            }
            CliCommand::Features {
                manifest,
                branch,
                feature_id,
                output,
                validate,
                multi,
                ..
            } => {
                let manifest = ManifestSource::try_from(&params, &manifest)?;
                let experiment = ExperimentSource::try_from(cli)?;
                AppCommand::ExtractFeatures {
                    experiment,
                    branch,
                    manifest,
                    feature_id,
                    validate,
                    multi,
                    output,
                }
            }
            CliCommand::Fetch { output, .. } | CliCommand::FetchList { output, .. } => {
                let list = ExperimentListSource::try_from(cli)?;

                AppCommand::FetchList { list, file: output }
            }
            CliCommand::Fml { args } => {
                let cwd = std::env::current_dir().expect("Current Working Directory is not set");
                AppCommand::FmlPassthrough { args, cwd }
            }
            CliCommand::Info { experiment, output } => AppCommand::Info {
                experiment: ExperimentSource::try_from(&experiment)?,
                output,
            },
            CliCommand::List { .. } => {
                let list = ExperimentListSource::try_from(cli)?;
                AppCommand::List { list }
            }
            CliCommand::LogState { open } => {
                let app = LaunchableApp::try_from(cli)?;
                AppCommand::LogState {
                    app,
                    open: open.into(),
                }
            }
            CliCommand::Open { open, .. } => {
                let app = LaunchableApp::try_from(cli)?;
                AppCommand::Open {
                    app,
                    open: open.into(),
                }
            }
            #[cfg(feature = "server")]
            CliCommand::StartServer => AppCommand::StartServer,
            CliCommand::TailLogs => {
                let app = LaunchableApp::try_from(cli)?;
                AppCommand::TailLogs { app }
            }
            CliCommand::TestFeature { files, open, .. } => {
                let app = LaunchableApp::try_from(cli)?;
                let experiment = ExperimentSource::try_from(cli)?;
                let first = files
                    .first()
                    .ok_or_else(|| anyhow::Error::msg("Need at least one file to make a branch"))?;
                let branch = feature_utils::slug(first)?;

                Self::Enroll {
                    app,
                    params,
                    experiment,
                    branch,
                    rollouts: Default::default(),
                    open: open.into(),
                    preserve_targeting: false,
                    preserve_bucketing: false,
                    preserve_nimbus_db: false,
                }
            }
            CliCommand::Unenroll { open } => {
                let app = LaunchableApp::try_from(cli)?;
                AppCommand::Unenroll {
                    app,
                    open: open.into(),
                }
            }
            CliCommand::EvalJexl { expression, open } => {
                let app = LaunchableApp::try_from(cli)?;
                AppCommand::EvalJexl {
                    app,
                    expression: expression.clone(),
                    open: open.into(),
                }
            }
            _ => Self::NoOp,
        })
    }
}

impl CliCommand {
    fn check_valid(&self, maybe_app: Option<&str>) -> Result<()> {
        // Check validity of the OpenArgs.
        if let Some(open) = self.open_args() {
            if open.reset_app || !open.passthrough.is_empty() {
                const ERR: &str = "does not work with --reset-app or passthrough args";
                if open.pbcopy {
                    bail!(format!("{} {}", "--pbcopy", ERR));
                }
                if open.pbpaste {
                    bail!(format!("{} {}", "--pbpaste", ERR));
                }
                if open.output.is_some() {
                    bail!(format!("{} {}", "--output", ERR));
                }
            }
            if open.deeplink.is_some() {
                const ERR: &str = "does not work with --deeplink";
                if open.output.is_some() {
                    bail!("--output {ERR}");
                }
            }

            if let Some(app) = maybe_app {
                match app {
                    "fenix" => {
                        if !open.legacy_open_mode
                            && open.deeplink.is_some()
                            && !matches!(self, Self::Open { .. })
                        {
                            bail!("--deeplink requires --legacy-open for fenix and is not compatible with newer releases");
                        }
                    }

                    _ if open.legacy_open_mode => {
                        bail!("--legacy-open-mode is not supported for {app}");
                    }
                    _ => {}
                }
            }
        }
        Ok(())
    }

    fn open_args(&self) -> Option<&OpenArgs> {
        if let Self::ApplyFile { open, .. }
        | Self::Open { open, .. }
        | Self::Enroll { open, .. }
        | Self::LogState { open, .. }
        | Self::TestFeature { open, .. }
        | Self::Unenroll { open, .. }
        | Self::EvalJexl { open, .. } = self
        {
            Some(open)
        } else {
            None
        }
    }

    fn should_kill(&self, maybe_app: Option<&str>) -> bool {
        if let Some(open) = self.open_args() {
            let using_links = open.pbcopy || open.pbpaste;
            let output_to_file = open.output.is_some();
            let no_clobber = if let Self::Open { no_clobber, .. } = self {
                *no_clobber
            } else {
                false
            };

            if !open.legacy_open_mode {
                match (maybe_app, &self) {
                    (Some("fenix"), Self::Open { no_clobber, .. }) => {
                        return !using_links && !no_clobber && !output_to_file;
                    }
                    (Some("fenix"), _) => {
                        return false;
                    }
                    _ => {}
                }
            }

            !using_links && !no_clobber && !output_to_file
        } else {
            matches!(self, Self::ResetApp)
        }
    }

    fn should_reset(&self) -> bool {
        if let Some(open) = self.open_args() {
            open.reset_app
        } else {
            matches!(self, Self::ResetApp)
        }
    }
}

#[derive(Debug, Default, PartialEq)]
pub(crate) struct AppOpenArgs {
    deeplink: Option<String>,
    passthrough: Vec<String>,
    pbcopy: bool,
    pbpaste: bool,

    output: Option<PathBuf>,
    legacy_open_mode: bool,
}

impl From<OpenArgs> for AppOpenArgs {
    fn from(value: OpenArgs) -> Self {
        Self {
            deeplink: value.deeplink,
            passthrough: value.passthrough,
            pbcopy: value.pbcopy,
            pbpaste: value.pbpaste,
            output: value.output,
            legacy_open_mode: value.legacy_open_mode,
        }
    }
}

impl AppOpenArgs {
    fn args(&self) -> (&[String], &[String]) {
        let splits = &mut self.passthrough.splitn(2, |item| item == "{}");
        match (splits.next(), splits.next()) {
            (Some(first), Some(last)) => (first, last),
            (None, Some(last)) | (Some(last), None) => (&[], last),
            _ => (&[], &[]),
        }
    }
}

#[cfg(test)]
mod unit_tests {
    use crate::sources::ExperimentListFilter;

    use super::*;

    #[test]
    fn test_launchable_app() -> Result<()> {
        fn cli(app: &str, channel: &str) -> Cli {
            Cli {
                app: Some(app.to_string()),
                channel: Some(channel.to_string()),
                device_id: None,
                command: CliCommand::ResetApp,
            }
        }
        fn android(
            package: &str,
            activity: &str,
            scheme: Option<&str>,
            open_deeplink: Option<&str>,
        ) -> LaunchableApp {
            LaunchableApp::Android {
                package_name: package.to_string(),
                activity_name: activity.to_string(),
                device_id: None,
                scheme: scheme.map(str::to_string),
                open_deeplink: open_deeplink.map(str::to_string),
            }
        }
        fn ios(id: &str, scheme: Option<&str>) -> LaunchableApp {
            LaunchableApp::Ios {
                app_id: id.to_string(),
                device_id: "booted".to_string(),
                scheme: scheme.map(str::to_string),
            }
        }

        // Firefox for Android, a.k.a. fenix
        assert_eq!(
            LaunchableApp::try_from(&cli("fenix", "developer"))?,
            android(
                "org.mozilla.fenix.debug",
                ".App",
                Some("fenix-dev"),
                Some("open")
            )
        );
        assert_eq!(
            LaunchableApp::try_from(&cli("fenix", "nightly"))?,
            android(
                "org.mozilla.fenix",
                ".App",
                Some("fenix-nightly"),
                Some("open")
            )
        );
        assert_eq!(
            LaunchableApp::try_from(&cli("fenix", "beta"))?,
            android(
                "org.mozilla.firefox_beta",
                ".App",
                Some("fenix-beta"),
                Some("open")
            )
        );
        assert_eq!(
            LaunchableApp::try_from(&cli("fenix", "release"))?,
            android("org.mozilla.firefox", ".App", Some("fenix"), Some("open"))
        );

        // Firefox for iOS
        assert_eq!(
            LaunchableApp::try_from(&cli("firefox_ios", "developer"))?,
            ios("org.mozilla.ios.Fennec", Some("fennec"))
        );
        assert_eq!(
            LaunchableApp::try_from(&cli("firefox_ios", "beta"))?,
            ios("org.mozilla.ios.FirefoxBeta", Some("firefox-beta"))
        );
        assert_eq!(
            LaunchableApp::try_from(&cli("firefox_ios", "release"))?,
            ios("org.mozilla.ios.Firefox", Some("firefox-internal"))
        );

        // Focus for Android
        assert_eq!(
            LaunchableApp::try_from(&cli("focus_android", "developer"))?,
            android(
                "org.mozilla.focus.debug",
                "org.mozilla.focus.activity.MainActivity",
                None,
                None,
            )
        );
        assert_eq!(
            LaunchableApp::try_from(&cli("focus_android", "nightly"))?,
            android(
                "org.mozilla.focus.nightly",
                "org.mozilla.focus.activity.MainActivity",
                None,
                None,
            )
        );
        assert_eq!(
            LaunchableApp::try_from(&cli("focus_android", "beta"))?,
            android(
                "org.mozilla.focus.beta",
                "org.mozilla.focus.activity.MainActivity",
                None,
                None,
            )
        );
        assert_eq!(
            LaunchableApp::try_from(&cli("focus_android", "release"))?,
            android(
                "org.mozilla.focus",
                "org.mozilla.focus.activity.MainActivity",
                None,
                None,
            )
        );

        Ok(())
    }

    #[test]
    fn test_split_args() -> Result<()> {
        let mut open = AppOpenArgs {
            passthrough: vec![],
            ..Default::default()
        };
        let empty: &[String] = &[];
        let expected = (empty, empty);
        let observed = open.args();
        assert_eq!(observed.0, expected.0);
        assert_eq!(observed.1, expected.1);

        open.passthrough = vec!["{}".to_string()];
        let expected = (empty, empty);
        let observed = open.args();
        assert_eq!(observed.0, expected.0);
        assert_eq!(observed.1, expected.1);

        open.passthrough = vec!["foo".to_string(), "bar".to_string()];
        let expected: (&[String], &[String]) = (empty, &["foo".to_string(), "bar".to_string()]);
        let observed = open.args();
        assert_eq!(observed.0, expected.0);
        assert_eq!(observed.1, expected.1);

        open.passthrough = vec!["foo".to_string(), "bar".to_string(), "{}".to_string()];
        let expected: (&[String], &[String]) = (&["foo".to_string(), "bar".to_string()], empty);
        let observed = open.args();
        assert_eq!(observed.0, expected.0);
        assert_eq!(observed.1, expected.1);

        open.passthrough = vec!["foo".to_string(), "{}".to_string(), "bar".to_string()];
        let expected: (&[String], &[String]) = (&["foo".to_string()], &["bar".to_string()]);
        let observed = open.args();
        assert_eq!(observed.0, expected.0);
        assert_eq!(observed.1, expected.1);

        open.passthrough = vec!["{}".to_string(), "foo".to_string(), "bar".to_string()];
        let expected: (&[String], &[String]) = (empty, &["foo".to_string(), "bar".to_string()]);
        let observed = open.args();
        assert_eq!(observed.0, expected.0);
        assert_eq!(observed.1, expected.1);

        Ok(())
    }

    fn fenix() -> LaunchableApp {
        LaunchableApp::Android {
            package_name: "org.mozilla.fenix.debug".to_string(),
            activity_name: ".App".to_string(),
            device_id: None,
            scheme: Some("fenix-dev".to_string()),
            open_deeplink: Some("open".to_string()),
        }
    }

    fn firefox_ios() -> LaunchableApp {
        LaunchableApp::Ios {
            app_id: "org.mozilla.ios.Fennec".to_string(),
            device_id: "foobar".to_string(),
            scheme: Some("fennec".to_string()),
        }
    }

    fn fenix_params() -> NimbusApp {
        NimbusApp::new("fenix", "developer")
    }

    fn firefox_ios_params() -> NimbusApp {
        NimbusApp::new("firefox_ios", "developer")
    }

    fn fenix_old_manifest_with_ref(ref_: &str) -> ManifestSource {
        ManifestSource::FromGithub {
            channel: "developer".into(),
            github_repo: "mozilla-mobile/firefox-android".into(),
            ref_: ref_.into(),
            manifest_file: "@mozilla-mobile/firefox-android/fenix/app/nimbus.fml.yaml".into(),
        }
    }

    fn fenix_manifest() -> ManifestSource {
        fenix_manifest_with_ref("main")
    }

    fn fenix_manifest_with_ref(ref_: &str) -> ManifestSource {
        ManifestSource::FromGithub {
            github_repo: "mozilla-firefox/firefox".to_string(),
            ref_: ref_.to_string(),
            manifest_file: "@mozilla-firefox/firefox/mobile/android/fenix/app/nimbus.fml.yaml"
                .into(),
            channel: "developer".to_string(),
        }
    }

    fn manifest_from_file(file: &str) -> ManifestSource {
        ManifestSource::FromFile {
            channel: "developer".to_string(),
            manifest_file: file.to_string(),
        }
    }

    fn experiment(slug: &str) -> ExperimentSource {
        let endpoint = config::api_v6_production_server();
        ExperimentSource::FromApiV6 {
            slug: slug.to_string(),
            endpoint,
        }
    }

    fn feature_experiment(feature_id: &str, files: &[&str]) -> ExperimentSource {
        ExperimentSource::FromFeatureFiles {
            app: fenix_params(),
            feature_id: feature_id.to_string(),
            files: files.iter().map(|f| f.into()).collect(),
        }
    }

    fn with_deeplink(link: &str) -> AppOpenArgs {
        AppOpenArgs {
            deeplink: Some(link.to_string()),
            ..Default::default()
        }
    }

    fn with_pbcopy() -> AppOpenArgs {
        AppOpenArgs {
            pbcopy: true,
            ..Default::default()
        }
    }

    fn with_passthrough(params: &[&str]) -> AppOpenArgs {
        AppOpenArgs {
            passthrough: params.iter().map(|s| s.to_string()).collect(),
            ..Default::default()
        }
    }

    fn with_output(filename: &str) -> AppOpenArgs {
        AppOpenArgs {
            output: Some(PathBuf::from(filename)),
            ..Default::default()
        }
    }

    fn for_app(app: &str, list: ExperimentListSource) -> ExperimentListSource {
        ExperimentListSource::Filtered {
            filter: ExperimentListFilter::for_app(app),
            inner: Box::new(list),
        }
    }

    fn for_feature(feature: &str, list: ExperimentListSource) -> ExperimentListSource {
        ExperimentListSource::Filtered {
            filter: ExperimentListFilter::for_feature(feature),
            inner: Box::new(list),
        }
    }

    fn for_active_on_date(date: &str, list: ExperimentListSource) -> ExperimentListSource {
        ExperimentListSource::Filtered {
            filter: ExperimentListFilter::for_active_on(date),
            inner: Box::new(list),
        }
    }

    fn for_enrolling_on_date(date: &str, list: ExperimentListSource) -> ExperimentListSource {
        ExperimentListSource::Filtered {
            filter: ExperimentListFilter::for_enrolling_on(date),
            inner: Box::new(list),
        }
    }

    #[test]
    fn test_enroll_fenix_legacy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--no-validate",
            "--legacy-open",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: experiment("my-experiment"),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..Default::default()
                },
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_enroll_fenix() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--no-validate",
        ])?;
        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: experiment("my-experiment"),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: Default::default(),
            },
        ];

        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_enroll_ios() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "firefox_ios",
            "--channel",
            "developer",
            "--device-id",
            "foobar",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--no-validate",
        ])?;

        assert_eq!(
            &observed,
            &[
                AppCommand::NoOp,
                AppCommand::Kill { app: firefox_ios() },
                AppCommand::Enroll {
                    app: firefox_ios(),
                    params: firefox_ios_params(),
                    experiment: ExperimentSource::FromApiV6 {
                        slug: "my-experiment".to_string(),
                        endpoint: config::api_v6_production_server()
                    },
                    rollouts: Default::default(),
                    branch: "my-branch".to_string(),
                    preserve_targeting: false,
                    preserve_bucketing: false,
                    preserve_nimbus_db: false,
                    open: Default::default(),
                }
            ]
        );

        Ok(())
    }

    #[test]
    fn test_enroll_ios_legacy() {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "firefox_ios",
            "--channel",
            "release",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--no-validate",
            "--legacy-open",
        ]);

        assert_eq!(
            observed.unwrap_err().to_string(),
            "--legacy-open-mode is not supported for firefox_ios"
        );
    }

    #[test]
    fn test_enroll_with_reset_app_fenix_legacy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--reset-app",
            "--no-validate",
            "--legacy-open",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::Reset { app: fenix() },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: experiment("my-experiment"),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..Default::default()
                },
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_enroll_with_reset_app_fenix() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--reset-app",
            "--no-validate",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Reset { app: fenix() },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: experiment("my-experiment"),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: Default::default(),
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_enroll_with_validate_fenix_legacy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--reset-app",
            "--legacy-open",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: fenix_manifest(),
                experiment: experiment("my-experiment"),
            },
            AppCommand::Kill { app: fenix() },
            AppCommand::Reset { app: fenix() },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: experiment("my-experiment"),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..Default::default()
                },
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_enroll_with_validate_fenix() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--reset-app",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: fenix_manifest(),
                experiment: experiment("my-experiment"),
            },
            AppCommand::Reset { app: fenix() },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: experiment("my-experiment"),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: Default::default(),
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_enroll_with_deeplink_fenix_legacy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--no-validate",
            "--deeplink",
            "host/path?key=value",
            "--legacy-open",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: experiment("my-experiment"),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..with_deeplink("host/path?key=value")
                },
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_enroll_with_deeplink_fenix() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--no-validate",
            "--deeplink",
            "host/path?key=value",
        ]);

        assert_eq!(
            observed.unwrap_err().to_string(),
            "--deeplink requires --legacy-open for fenix and is not compatible with newer releases"
        );
        Ok(())
    }

    #[test]
    fn test_enroll_with_passthrough_fenix_legacy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--no-validate",
            "--legacy-open",
            "--",
            "--start-profiler",
            "./profile.file",
            "{}",
            "--esn",
            "TEST_FLAG",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: experiment("my-experiment"),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..with_passthrough(&[
                        "--start-profiler",
                        "./profile.file",
                        "{}",
                        "--esn",
                        "TEST_FLAG",
                    ])
                },
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_enroll_with_passthrough_fenix() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--no-validate",
            "--",
            "--start-profiler",
            "./profile.file",
            "{}",
            "--esn",
            "TEST_FLAG",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: experiment("my-experiment"),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: with_passthrough(&[
                    "--start-profiler",
                    "./profile.file",
                    "{}",
                    "--esn",
                    "TEST_FLAG",
                ]),
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_enroll_with_pbcopy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--no-validate",
            "--pbcopy",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: experiment("my-experiment"),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: with_pbcopy(),
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_enroll_with_output() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "enroll",
            "my-experiment",
            "--branch",
            "my-branch",
            "--no-validate",
            "--output",
            "./file.json",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: experiment("my-experiment"),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: with_output("./file.json"),
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_validate() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "validate",
            "my-experiment",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: fenix_manifest(),
                experiment: experiment("my-experiment"),
            },
            AppCommand::NoOp,
        ];
        assert_eq!(expected, observed);

        // With a specific version of the manifest.
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "validate",
            "my-experiment",
            "--version",
            "114",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: fenix_old_manifest_with_ref("releases_v114"),
                experiment: experiment("my-experiment"),
            },
            AppCommand::NoOp,
        ];
        assert_eq!(expected, observed);

        // With a specific version of the manifest, via a ref.
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "validate",
            "my-experiment",
            "--ref",
            "my-tag",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: fenix_manifest_with_ref("my-tag"),
                experiment: experiment("my-experiment"),
            },
            AppCommand::NoOp,
        ];
        assert_eq!(expected, observed);

        // With a file on disk
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--channel",
            "developer",
            "validate",
            "my-experiment",
            "--manifest",
            "./manifest.fml.yaml",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: NimbusApp {
                    channel: Some("developer".to_string()),
                    app_name: None,
                },
                manifest: manifest_from_file("./manifest.fml.yaml"),
                experiment: experiment("my-experiment"),
            },
            AppCommand::NoOp,
        ];
        assert_eq!(expected, observed);

        Ok(())
    }

    #[test]
    fn test_test_feature_fenix_legacy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "test-feature",
            "my-feature",
            "./my-branch.json",
            "./my-treatment.json",
            "--legacy-open",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: fenix_manifest(),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
            },
            AppCommand::Kill { app: fenix() },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..Default::default()
                },
            },
        ];
        assert_eq!(expected, observed);

        // With a specific version of the manifest.
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "test-feature",
            "my-feature",
            "./my-branch.json",
            "./my-treatment.json",
            "--version",
            "114",
            "--legacy-open",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: fenix_old_manifest_with_ref("releases_v114"),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
            },
            AppCommand::Kill { app: fenix() },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..Default::default()
                },
            },
        ];
        assert_eq!(expected, observed);

        // With a specific version of the manifest, via a ref.
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "test-feature",
            "my-feature",
            "./my-branch.json",
            "./my-treatment.json",
            "--ref",
            "my-tag",
            "--legacy-open",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: fenix_manifest_with_ref("my-tag"),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
            },
            AppCommand::Kill { app: fenix() },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..Default::default()
                },
            },
        ];
        assert_eq!(expected, observed);

        // With a file on disk
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "test-feature",
            "my-feature",
            "./my-branch.json",
            "./my-treatment.json",
            "--manifest",
            "./manifest.fml.yaml",
            "--legacy-open",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: manifest_from_file("./manifest.fml.yaml"),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
            },
            AppCommand::Kill { app: fenix() },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..Default::default()
                },
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "test-feature",
            "my-feature",
            "./my-branch.json",
            "./my-treatment.json",
            "--no-validate",
            "--deeplink",
            "host/path?key=value",
            "--legacy-open",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..with_deeplink("host/path?key=value")
                },
            },
        ];
        assert_eq!(expected, observed);

        Ok(())
    }

    #[test]
    fn test_test_feature_fenix() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "test-feature",
            "my-feature",
            "./my-branch.json",
            "./my-treatment.json",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: fenix_manifest(),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
            },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: Default::default(),
            },
        ];
        assert_eq!(expected, observed);

        // With a specific version of the manifest.
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "test-feature",
            "my-feature",
            "./my-branch.json",
            "./my-treatment.json",
            "--version",
            "114",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: fenix_old_manifest_with_ref("releases_v114"),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
            },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: Default::default(),
            },
        ];
        assert_eq!(expected, observed);

        // With a specific version of the manifest, via a ref.
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "test-feature",
            "my-feature",
            "./my-branch.json",
            "./my-treatment.json",
            "--ref",
            "my-tag",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: fenix_manifest_with_ref("my-tag"),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
            },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: Default::default(),
            },
        ];
        assert_eq!(expected, observed);

        // With a file on disk
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "test-feature",
            "my-feature",
            "./my-branch.json",
            "./my-treatment.json",
            "--manifest",
            "./manifest.fml.yaml",
        ])?;

        let expected = vec![
            AppCommand::ValidateExperiment {
                params: fenix_params(),
                manifest: manifest_from_file("./manifest.fml.yaml"),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
            },
            AppCommand::Enroll {
                app: fenix(),
                params: fenix_params(),
                experiment: feature_experiment(
                    "my-feature",
                    &["./my-branch.json", "./my-treatment.json"],
                ),
                rollouts: Default::default(),
                branch: "my-branch".to_string(),
                preserve_targeting: false,
                preserve_bucketing: false,
                preserve_nimbus_db: false,
                open: Default::default(),
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "test-feature",
            "my-feature",
            "./my-branch.json",
            "./my-treatment.json",
            "--no-validate",
            "--deeplink",
            "host/path?key=value",
        ]);

        assert_eq!(
            observed.unwrap_err().to_string(),
            "--deeplink requires --legacy-open for fenix and is not compatible with newer releases"
        );

        Ok(())
    }

    #[test]
    fn test_open() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "open",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::Open {
                app: fenix(),
                open: Default::default(),
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_open_with_reset_fenix_legacy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "open",
            "--reset-app",
            "--legacy-open",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::Reset { app: fenix() },
            AppCommand::Open {
                app: fenix(),
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..Default::default()
                },
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_open_with_reset_fenix() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "open",
            "--reset-app",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::Reset { app: fenix() },
            AppCommand::Open {
                app: fenix(),
                open: Default::default(),
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_open_with_deeplink_fenix_legacy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "open",
            "--deeplink",
            "host/path",
            "--legacy-open",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::Open {
                app: fenix(),
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..with_deeplink("host/path")
                },
            },
        ];
        assert_eq!(expected, observed);

        Ok(())
    }

    #[test]
    fn test_open_with_deeplink_fenix() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "open",
            "--deeplink",
            "host/path",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::Open {
                app: fenix(),
                open: with_deeplink("host/path"),
            },
        ];
        assert_eq!(expected, observed);

        Ok(())
    }

    #[test]
    fn test_open_with_passthrough_params_fenix_legacy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "open",
            "--legacy-open",
            "--",
            "--start-profiler",
            "./profile.file",
            "{}",
            "--esn",
            "TEST_FLAG",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::Open {
                app: fenix(),
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..with_passthrough(&[
                        "--start-profiler",
                        "./profile.file",
                        "{}",
                        "--esn",
                        "TEST_FLAG",
                    ])
                },
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_open_with_passthrough_params_fenix() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "open",
            "--",
            "--start-profiler",
            "./profile.file",
            "{}",
            "--esn",
            "TEST_FLAG",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::Open {
                app: fenix(),
                open: with_passthrough(&[
                    "--start-profiler",
                    "./profile.file",
                    "{}",
                    "--esn",
                    "TEST_FLAG",
                ]),
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_open_with_noclobber() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "open",
            "--no-clobber",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Open {
                app: fenix(),
                open: Default::default(),
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_open_with_pbcopy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "open",
            "--pbcopy",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Open {
                app: fenix(),
                open: with_pbcopy(),
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_fetch() -> Result<()> {
        let file = Some(PathBuf::from("./archived.json"));
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "fetch",
            "--output",
            "./archived.json",
            "my-experiment",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::FetchList {
                list: for_app(
                    "fenix",
                    ExperimentListSource::FromRecipes {
                        recipes: vec![experiment("my-experiment")],
                    },
                ),
                file: file.clone(),
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "fetch",
            "--output",
            "./archived.json",
            "my-experiment-1",
            "my-experiment-2",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::FetchList {
                list: for_app(
                    "fenix",
                    ExperimentListSource::FromRecipes {
                        recipes: vec![experiment("my-experiment-1"), experiment("my-experiment-2")],
                    },
                ),
                file,
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_fetch_list() -> Result<()> {
        let file = Some(PathBuf::from("./archived.json"));
        let observed =
            get_commands_from_cli(["nimbus-cli", "fetch-list", "--output", "./archived.json"])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::FetchList {
                list: ExperimentListSource::FromRemoteSettings {
                    endpoint: config::rs_production_server(),
                    is_preview: false,
                },
                file: file.clone(),
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "fetch-list",
            "--output",
            "./archived.json",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::FetchList {
                list: for_app(
                    "fenix",
                    ExperimentListSource::FromRemoteSettings {
                        endpoint: config::rs_production_server(),
                        is_preview: false,
                    },
                ),
                file: file.clone(),
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli([
            "nimbus-cli",
            "fetch-list",
            "--output",
            "./archived.json",
            "stage",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::FetchList {
                list: ExperimentListSource::FromRemoteSettings {
                    endpoint: config::rs_stage_server(),
                    is_preview: false,
                },
                file: file.clone(),
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli([
            "nimbus-cli",
            "fetch-list",
            "--output",
            "./archived.json",
            "preview",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::FetchList {
                list: ExperimentListSource::FromRemoteSettings {
                    endpoint: config::rs_production_server(),
                    is_preview: true,
                },
                file: file.clone(),
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli([
            "nimbus-cli",
            "fetch-list",
            "--output",
            "./archived.json",
            "--use-api",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::FetchList {
                list: ExperimentListSource::FromApiV6 {
                    endpoint: config::api_v6_production_server(),
                },
                file: file.clone(),
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli([
            "nimbus-cli",
            "fetch-list",
            "--use-api",
            "--output",
            "./archived.json",
            "stage",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::FetchList {
                list: ExperimentListSource::FromApiV6 {
                    endpoint: config::api_v6_stage_server(),
                },
                file,
            },
        ];
        assert_eq!(expected, observed);

        Ok(())
    }

    #[test]
    fn test_list() -> Result<()> {
        let observed = get_commands_from_cli(["nimbus-cli", "list"])?;
        let expected = vec![
            AppCommand::NoOp,
            AppCommand::List {
                list: ExperimentListSource::FromRemoteSettings {
                    endpoint: config::rs_production_server(),
                    is_preview: false,
                },
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli(["nimbus-cli", "list", "preview"])?;
        let expected = vec![
            AppCommand::NoOp,
            AppCommand::List {
                list: ExperimentListSource::FromRemoteSettings {
                    endpoint: config::rs_production_server(),
                    is_preview: true,
                },
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli(["nimbus-cli", "list", "stage"])?;
        let expected = vec![
            AppCommand::NoOp,
            AppCommand::List {
                list: ExperimentListSource::FromRemoteSettings {
                    endpoint: config::rs_stage_server(),
                    is_preview: false,
                },
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli(["nimbus-cli", "list", "--use-api", "stage"])?;
        let expected = vec![
            AppCommand::NoOp,
            AppCommand::List {
                list: ExperimentListSource::FromApiV6 {
                    endpoint: config::api_v6_stage_server(),
                },
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli(["nimbus-cli", "list", "--use-api"])?;
        let expected = vec![
            AppCommand::NoOp,
            AppCommand::List {
                list: ExperimentListSource::FromApiV6 {
                    endpoint: config::api_v6_production_server(),
                },
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_list_filter() -> Result<()> {
        let observed = get_commands_from_cli(["nimbus-cli", "--app", "my-app", "list"])?;
        let expected = vec![
            AppCommand::NoOp,
            AppCommand::List {
                list: for_app(
                    "my-app",
                    ExperimentListSource::FromRemoteSettings {
                        endpoint: config::rs_production_server(),
                        is_preview: false,
                    },
                ),
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli(["nimbus-cli", "list", "--feature", "messaging"])?;
        let expected = vec![
            AppCommand::NoOp,
            AppCommand::List {
                list: for_feature(
                    "messaging",
                    ExperimentListSource::FromRemoteSettings {
                        endpoint: config::rs_production_server(),
                        is_preview: false,
                    },
                ),
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "my-app",
            "list",
            "--feature",
            "messaging",
        ])?;
        let expected = vec![
            AppCommand::NoOp,
            AppCommand::List {
                list: for_app(
                    "my-app",
                    for_feature(
                        "messaging",
                        ExperimentListSource::FromRemoteSettings {
                            endpoint: config::rs_production_server(),
                            is_preview: false,
                        },
                    ),
                ),
            },
        ];
        assert_eq!(expected, observed);

        Ok(())
    }

    #[test]
    fn test_list_filter_by_date_with_error() -> Result<()> {
        let observed = get_commands_from_cli(["nimbus-cli", "list", "--active-on", "FOO"]);
        assert!(observed.is_err());
        let err = observed.unwrap_err();
        assert!(err.to_string().contains("Date string must be yyyy-mm-dd"));

        let observed = get_commands_from_cli(["nimbus-cli", "list", "--enrolling-on", "FOO"]);
        assert!(observed.is_err());
        let err = observed.unwrap_err();
        assert!(err.to_string().contains("Date string must be yyyy-mm-dd"));
        Ok(())
    }

    #[test]
    fn test_list_filter_by_dates() -> Result<()> {
        let today = "1970-01-01";

        let observed = get_commands_from_cli(["nimbus-cli", "list", "--active-on", today])?;
        let expected = vec![
            AppCommand::NoOp,
            AppCommand::List {
                list: for_active_on_date(
                    today,
                    ExperimentListSource::FromRemoteSettings {
                        endpoint: config::rs_production_server(),
                        is_preview: false,
                    },
                ),
            },
        ];
        assert_eq!(expected, observed);

        let observed = get_commands_from_cli(["nimbus-cli", "list", "--enrolling-on", today])?;
        let expected = vec![
            AppCommand::NoOp,
            AppCommand::List {
                list: for_enrolling_on_date(
                    today,
                    ExperimentListSource::FromRemoteSettings {
                        endpoint: config::rs_production_server(),
                        is_preview: false,
                    },
                ),
            },
        ];
        assert_eq!(expected, observed);

        Ok(())
    }

    #[test]
    fn test_jexl_fenix_legacy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "eval-jexl",
            "locale == 'en-US'",
            "--legacy-open",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::Kill { app: fenix() },
            AppCommand::EvalJexl {
                app: fenix(),
                expression: "locale == 'en-US'".to_string(),
                open: AppOpenArgs {
                    legacy_open_mode: true,
                    ..Default::default()
                },
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_jexl_fenix() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "eval-jexl",
            "locale == 'en-US'",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::EvalJexl {
                app: fenix(),
                expression: "locale == 'en-US'".to_string(),
                open: Default::default(),
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }

    #[test]
    fn test_jexl_with_pbcopy() -> Result<()> {
        let observed = get_commands_from_cli([
            "nimbus-cli",
            "--app",
            "fenix",
            "--channel",
            "developer",
            "eval-jexl",
            "locale == 'en-US'",
            "--pbcopy",
        ])?;

        let expected = vec![
            AppCommand::NoOp,
            AppCommand::EvalJexl {
                app: fenix(),
                expression: "locale == 'en-US'".to_string(),
                open: with_pbcopy(),
            },
        ];
        assert_eq!(expected, observed);
        Ok(())
    }
}
