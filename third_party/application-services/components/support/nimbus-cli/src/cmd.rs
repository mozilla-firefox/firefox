// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#[cfg(feature = "server")]
use crate::output::server;
use crate::{
    output::{deeplink, fml_cli},
    protocol::StartAppProtocol,
    sources::ManifestSource,
    value_utils::{
        self, prepare_experiment, prepare_rollout, try_find_branches_from_experiment,
        try_find_features_from_branch, CliUtils,
    },
    AppCommand, AppOpenArgs, ExperimentListSource, ExperimentSource, LaunchableApp, NimbusApp,
};
use anyhow::{bail, Result};
use nimbus_fml::intermediate_representation::FeatureManifest;
use serde_json::{json, Value};
use std::io::Write;
use std::path::PathBuf;
use std::process::Command;
use termcolor::{Color, ColorChoice, ColorSpec, StandardStream, WriteColor};

pub(crate) fn process_cmd(cmd: &AppCommand) -> Result<bool> {
    let status = match cmd {
        AppCommand::ApplyFile {
            app,
            open,
            list,
            preserve_nimbus_db,
        } => app.apply_list(open, list, preserve_nimbus_db)?,
        AppCommand::CaptureLogs { app, file } => app.capture_logs(file)?,
        AppCommand::Defaults {
            manifest,
            feature_id,
            output,
        } => manifest.print_defaults(feature_id.as_ref(), output.as_ref())?,
        AppCommand::Enroll {
            app,
            params,
            experiment,
            rollouts,
            branch,
            preserve_targeting,
            preserve_bucketing,
            preserve_nimbus_db,
            open,
            ..
        } => app.enroll(
            params,
            experiment,
            rollouts,
            branch,
            preserve_targeting,
            preserve_bucketing,
            preserve_nimbus_db,
            open,
        )?,
        AppCommand::ExtractFeatures {
            experiment,
            branch,
            manifest,
            feature_id,
            validate,
            multi,
            output,
        } => experiment.print_features(
            branch,
            manifest,
            feature_id.as_ref(),
            *validate,
            *multi,
            output.as_ref(),
        )?,

        AppCommand::FetchList { list, file } => list.fetch_list(file.as_ref())?,
        AppCommand::FmlPassthrough { args, cwd } => fml_cli(args, cwd)?,
        AppCommand::Info { experiment, output } => experiment.print_info(output.as_ref())?,
        AppCommand::Kill { app } => app.kill_app()?,
        AppCommand::List { list, .. } => list.print_list()?,
        AppCommand::LogState { app, open } => app.log_state(open)?,
        AppCommand::NoOp => true,
        AppCommand::Open {
            app, open: args, ..
        } => app.open(args)?,
        AppCommand::Reset { app } => app.reset_app()?,
        #[cfg(feature = "server")]
        AppCommand::StartServer => server::start_server()?,
        AppCommand::TailLogs { app } => app.tail_logs()?,
        AppCommand::Unenroll { app, open } => app.unenroll_all(open)?,
        AppCommand::ValidateExperiment {
            params,
            manifest,
            experiment,
        } => params.validate_experiment(manifest, experiment)?,
        AppCommand::EvalJexl {
            app,
            expression,
            open,
        } => app.eval_jexl(expression, open)?,
    };

    Ok(status)
}

fn prompt(stream: &mut StandardStream, command: &str) -> Result<()> {
    stream.set_color(ColorSpec::new().set_fg(Some(Color::Cyan)))?;
    write!(stream, "$ ")?;
    stream.set_color(ColorSpec::new().set_fg(Some(Color::Yellow)))?;
    writeln!(stream, "{}", command)?;
    stream.reset()?;

    Ok(())
}

fn output_ok(stream: &mut StandardStream, title: &str) -> Result<()> {
    write!(stream, "✅ ")?;
    stream.set_color(ColorSpec::new().set_fg(Some(Color::Green)))?;
    writeln!(stream, "{title}")?;
    stream.reset()?;

    Ok(())
}

fn output_err(stream: &mut StandardStream, title: &str, detail: &str) -> Result<()> {
    write!(stream, "❎ ")?;
    stream.set_color(ColorSpec::new().set_fg(Some(Color::Red)))?;
    write!(stream, "{title}")?;
    stream.reset()?;
    writeln!(stream, ": {detail}")?;

    Ok(())
}

impl LaunchableApp {
    #[cfg(feature = "server")]
    fn platform(&self) -> &str {
        match self {
            Self::Android { .. } => "android",
            Self::Ios { .. } => "ios",
        }
    }

    fn exe(&self) -> Result<Command> {
        Ok(match self {
            Self::Android { device_id, .. } => {
                let adb_name = if std::env::consts::OS != "windows" {
                    "adb"
                } else {
                    "adb.exe"
                };
                let adb = std::env::var("ADB_PATH").unwrap_or_else(|_| adb_name.to_string());
                let mut cmd = Command::new(adb);
                if let Some(id) = device_id {
                    cmd.args(["-s", id]);
                }
                cmd
            }
            Self::Ios { .. } => {
                if std::env::consts::OS != "macos" {
                    panic!("Cannot run commands for iOS on anything except macOS");
                }
                let xcrun = std::env::var("XCRUN_PATH").unwrap_or_else(|_| "xcrun".to_string());
                let mut cmd = Command::new(xcrun);
                cmd.arg("simctl");
                cmd
            }
        })
    }

    fn kill_app(&self) -> Result<bool> {
        Ok(match self {
            Self::Android { package_name, .. } => self
                .exe()?
                .arg("shell")
                .arg(format!("am force-stop {}", package_name))
                .spawn()?
                .wait()?
                .success(),
            Self::Ios {
                app_id, device_id, ..
            } => {
                let _ = self
                    .exe()?
                    .args(["terminate", device_id, app_id])
                    .output()?;
                true
            }
        })
    }

    fn unenroll_all(&self, open: &AppOpenArgs) -> Result<bool> {
        let payload = TryFrom::try_from(&ExperimentListSource::Empty)?;
        let protocol = StartAppProtocol {
            log_state: true,
            experiments: Some(&payload),
            ..Default::default()
        };
        self.start_app(protocol, open)
    }

    fn reset_app(&self) -> Result<bool> {
        Ok(match self {
            Self::Android { package_name, .. } => self
                .exe()?
                .arg("shell")
                .arg(format!("pm clear {}", package_name))
                .spawn()?
                .wait()?
                .success(),
            Self::Ios {
                app_id, device_id, ..
            } => {
                self.exe()?
                    .args(["privacy", device_id, "reset", "all", app_id])
                    .status()?;
                let data = self.ios_app_container("data")?;
                let groups = self.ios_app_container("groups")?;
                self.ios_reset(data, groups)?;
                true
            }
        })
    }

    fn tail_logs(&self) -> Result<bool> {
        let mut stdout = StandardStream::stdout(ColorChoice::Auto);

        Ok(match self {
            Self::Android { .. } => {
                let mut args = logcat_args();
                args.append(&mut vec!["-v", "color"]);
                prompt(&mut stdout, &format!("adb {}", args.join(" ")))?;
                self.exe()?.args(args).spawn()?.wait()?.success()
            }
            Self::Ios { .. } => {
                prompt(
                    &mut stdout,
                    &format!("{} | xargs tail -f", self.ios_log_file_command()),
                )?;
                let log = self.ios_log_file()?;

                Command::new("tail")
                    .arg("-f")
                    .arg(log.as_path().to_str().unwrap())
                    .spawn()?
                    .wait()?
                    .success()
            }
        })
    }

    fn capture_logs(&self, file: &PathBuf) -> Result<bool> {
        let mut stdout = StandardStream::stdout(ColorChoice::Auto);

        Ok(match self {
            Self::Android { .. } => {
                let mut args = logcat_args();
                args.append(&mut vec!["-d"]);
                prompt(
                    &mut stdout,
                    &format!(
                        "adb {} > {}",
                        args.join(" "),
                        file.as_path().to_str().unwrap()
                    ),
                )?;
                let output = self.exe()?.args(args).output()?;
                std::fs::write(file, String::from_utf8_lossy(&output.stdout).to_string())?;
                true
            }

            Self::Ios { .. } => {
                let log = self.ios_log_file()?;
                prompt(
                    &mut stdout,
                    &format!(
                        "{} | xargs -J %log_file% cp %log_file% {}",
                        self.ios_log_file_command(),
                        file.as_path().to_str().unwrap()
                    ),
                )?;
                std::fs::copy(log, file)?;
                true
            }
        })
    }

    fn ios_log_file(&self) -> Result<PathBuf> {
        let data = self.ios_app_container("data")?;
        let mut files = glob::glob(&format!("{}/**/*.log", data))?;
        let log = files.next();
        Ok(log.ok_or_else(|| {
            anyhow::Error::msg(
                "Logs are not available before the app is started for the first time",
            )
        })??)
    }

    fn ios_log_file_command(&self) -> String {
        if let Self::Ios {
            device_id, app_id, ..
        } = self
        {
            format!(
                "find $(xcrun simctl get_app_container {0} {1} data) -name \\*.log",
                device_id, app_id
            )
        } else {
            unreachable!()
        }
    }

    fn log_state(&self, open: &AppOpenArgs) -> Result<bool> {
        let protocol = StartAppProtocol {
            log_state: true,
            ..Default::default()
        };
        self.start_app(protocol, open)
    }

    #[allow(clippy::too_many_arguments)]
    fn enroll(
        &self,
        params: &NimbusApp,
        experiment: &ExperimentSource,
        rollouts: &Vec<ExperimentSource>,
        branch: &str,
        preserve_targeting: &bool,
        preserve_bucketing: &bool,
        preserve_nimbus_db: &bool,
        open: &AppOpenArgs,
    ) -> Result<bool> {
        let mut stdout = StandardStream::stdout(ColorChoice::Auto);

        let experiment = Value::try_from(experiment)?;
        let slug = experiment.get_str("slug")?.to_string();

        let mut recipes = vec![prepare_experiment(
            &experiment,
            params,
            branch,
            *preserve_targeting,
            *preserve_bucketing,
        )?];
        prompt(
            &mut stdout,
            &format!("# Enrolling in the '{0}' branch of '{1}'", branch, slug),
        )?;

        for r in rollouts {
            let rollout = Value::try_from(r)?;
            let slug = rollout.get_str("slug")?.to_string();
            recipes.push(prepare_rollout(
                &rollout,
                params,
                *preserve_targeting,
                *preserve_bucketing,
            )?);
            prompt(
                &mut stdout,
                &format!("# Enrolling into the '{0}' rollout", slug),
            )?;
        }

        let payload = json! {{ "data": recipes }};
        let protocol = StartAppProtocol {
            reset_db: !preserve_nimbus_db,
            experiments: Some(&payload),
            log_state: true,
            jexl_expression: None,
        };
        self.start_app(protocol, open)
    }

    fn apply_list(
        &self,
        open: &AppOpenArgs,
        list: &ExperimentListSource,
        preserve_nimbus_db: &bool,
    ) -> Result<bool> {
        let value: Value = list.try_into()?;

        let protocol = StartAppProtocol {
            reset_db: !preserve_nimbus_db,
            experiments: Some(&value),
            log_state: true,
            jexl_expression: None,
        };
        self.start_app(protocol, open)
    }

    fn ios_app_container(&self, container: &str) -> Result<String> {
        if let Self::Ios {
            app_id, device_id, ..
        } = self
        {
            // We need to get the app container directories, and delete them.
            let output = self
                .exe()?
                .args(["get_app_container", device_id, app_id, container])
                .output()
                .expect("Expected an app-container from the simulator");
            let string = String::from_utf8_lossy(&output.stdout).to_string();
            Ok(string.trim().to_string())
        } else {
            unreachable!()
        }
    }

    fn ios_reset(&self, data_dir: String, groups_string: String) -> Result<bool> {
        let mut stdout = StandardStream::stdout(ColorChoice::Auto);
        prompt(&mut stdout, "# Resetting the app")?;
        if !data_dir.is_empty() {
            prompt(&mut stdout, &format!("rm -Rf {}/* 2>/dev/null", data_dir))?;
            let _ = std::fs::remove_dir_all(&data_dir);
            let _ = std::fs::create_dir_all(&data_dir);
        }
        let lines = groups_string.split('\n');

        for line in lines {
            let words = line.splitn(2, '\t').collect::<Vec<_>>();
            if let [_, dir] = words.as_slice() {
                if !dir.is_empty() {
                    prompt(&mut stdout, &format!("rm -Rf {}/* 2>/dev/null", dir))?;
                    let _ = std::fs::remove_dir_all(dir);
                    let _ = std::fs::create_dir_all(dir);
                }
            }
        }
        Ok(true)
    }

    fn open(&self, open: &AppOpenArgs) -> Result<bool> {
        self.start_app(Default::default(), open)
    }

    fn eval_jexl(&self, expression: &str, open: &AppOpenArgs) -> Result<bool> {
        let mut stdout = StandardStream::stdout(ColorChoice::Auto);
        prompt(&mut stdout, &format!("# Evaluating JEXL: {}", expression))?;

        // Clear logcat before starting to ensure we only read fresh logs
        if matches!(self, Self::Android { .. }) {
            self.exe()?.args(["logcat", "-c"]).output()?;
        }

        let protocol = StartAppProtocol {
            jexl_expression: Some(expression),
            ..Default::default()
        };

        self.start_app(protocol, open)?;

        prompt(&mut stdout, "# Waiting for result...")?;

        const MAX_RETRIES: u32 = 3;
        let mut last_error = None;

        for attempt in 1..=MAX_RETRIES {
            std::thread::sleep(std::time::Duration::from_secs(3));

            match self.capture_jexl_result() {
                Ok(result) => {
                    println!("\n{}", result);
                    return Ok(true);
                }
                Err(e) => {
                    last_error = Some(e);
                    if attempt < MAX_RETRIES {
                        prompt(
                            &mut stdout,
                            &format!("# Retry {}/{}...", attempt, MAX_RETRIES - 1),
                        )?;
                    }
                }
            }
        }

        Err(last_error.unwrap())
    }

    fn capture_jexl_result(&self) -> Result<String> {
        match self {
            Self::Android { .. } => {
                let output = self.exe()?.args(["logcat", "-d"]).output()?;

                let logs = String::from_utf8_lossy(&output.stdout);

                for line in logs.lines().rev() {
                    if let Some(json) = line.split("JEXL_RESULT: ").nth(1) {
                        return Ok(json.to_string());
                    }
                }

                bail!("Failed to find JEXL result in logs")
            }

            Self::Ios { device_id, .. } => {
                let output = self
                    .exe()?
                    .args([
                        "spawn",
                        device_id,
                        "log",
                        "show",
                        "--predicate",
                        "process == 'Client'",
                        "--last",
                        "10s",
                        "--style",
                        "compact",
                    ])
                    .output()?;

                let logs = String::from_utf8_lossy(&output.stdout);

                for line in logs.lines().rev() {
                    if let Some(json) = line.split("JEXL_RESULT: ").nth(1) {
                        return Ok(json.to_string());
                    }
                }

                bail!("Failed to find JEXL result in logs")
            }
        }
    }

    fn start_app(&self, app_protocol: StartAppProtocol, open: &AppOpenArgs) -> Result<bool> {
        let mut stdout = StandardStream::stdout(ColorChoice::Auto);
        if open.pbcopy {
            let len = self.copy_to_clipboard(&app_protocol, open)?;
            prompt(
                &mut stdout,
                &format!("# Copied a deeplink URL ({len} characters) in to the clipboard"),
            )?;
        }
        #[cfg(feature = "server")]
        if open.pbpaste {
            let url = self.longform_url(&app_protocol, open)?;
            let addr = server::get_address()?;
            match server::post_deeplink(self.platform(), &url, app_protocol.experiments) {
                Err(_) => output_err(
                    &mut stdout,
                    "Cannot post to the server",
                    "Start the server with `nimbus-cli start-server`",
                )?,
                _ => output_ok(&mut stdout, &format!("Posted to server at http://{addr}"))?,
            };
        }
        if let Some(file) = &open.output {
            let ex = app_protocol.experiments;
            if let Some(contents) = ex {
                value_utils::write_to_file_or_print(Some(file), contents)?;
                output_ok(
                    &mut stdout,
                    &format!(
                        "Written to JSON to file {}",
                        file.to_str().unwrap_or_default()
                    ),
                )?;
            } else {
                output_err(
                    &mut stdout,
                    "No content",
                    &format!("File {} not written", file.to_str().unwrap_or_default()),
                )?;
            }
        }
        if open.pbcopy || open.pbpaste || open.output.is_some() {
            return Ok(true);
        }

        Ok(match self {
            Self::Android { .. } => self
                .android_start(app_protocol, open)?
                .spawn()?
                .wait()?
                .success(),
            Self::Ios { .. } => self
                .ios_start(app_protocol, open)?
                .spawn()?
                .wait()?
                .success(),
        })
    }

    fn android_start(&self, app_protocol: StartAppProtocol, open: &AppOpenArgs) -> Result<Command> {
        if let Self::Android {
            package_name,
            activity_name,
            ..
        } = self
        {
            let mut args: Vec<String> = Vec::new();

            let (start_args, ending_args) = open.args();
            args.extend_from_slice(start_args);

            // If app_protocol is Default::default(), we are doing an open
            // command and need to launch the browser with am start.
            let legacy_open_mode = open.legacy_open_mode
                || (!app_protocol.reset_db
                    && app_protocol.experiments.is_none()
                    && !app_protocol.log_state
                    && app_protocol.jexl_expression.is_none());

            let am_cmd = if legacy_open_mode {
                "start"
            } else {
                "broadcast"
            };

            match (open.legacy_open_mode, self.deeplink(open)?) {
                (true, Some(deeplink)) => {
                    args.extend([
                        "-a android.intent.action.VIEW".to_string(),
                        "-c android.intent.category.DEFAULT".to_string(),
                        "-c android.intent.category.BROWSABLE".to_string(),
                        format!("-d {}", deeplink),
                    ]);
                }
                (true, None) => {
                    args.extend([
                        format!("-n {}/{}", package_name, activity_name),
                        "-a android.intent.action.MAIN".to_string(),
                        "-c android.intent.category.LAUNCHER".to_string(),
                    ]);
                }
                (false, Some(_)) => {
                    bail!("--deeplink is unsupported without --legacy-open");
                }
                (false, None) => {
                    args.extend([
                        format!("-p {}", package_name),
                        "-a org.mozilla.fenix.NIMBUS_TOOLING".to_string(),
                        "-f 0x20".to_string(), // android.content.Intent.FLAG_INCLUDE_STOPPED_PACKAGES
                    ]);
                }
            }

            let StartAppProtocol {
                reset_db,
                experiments,
                log_state,
                jexl_expression,
            } = app_protocol;

            if log_state || experiments.is_some() || reset_db || jexl_expression.is_some() {
                args.extend(["--esn nimbus-cli".to_string(), "--ei version 1".to_string()]);
            }

            if reset_db {
                args.push("--ez reset-db true".to_string());
            }
            if let Some(s) = experiments {
                let json = s.to_string().replace('\'', "&apos;");
                args.push(format!("--es experiments '{}'", json))
            }
            if log_state {
                args.push("--ez log-state true".to_string());
            }
            if let Some(expr) = jexl_expression {
                let escaped = expr.replace('\'', "&apos;");
                args.push(format!("--es eval-jexl '{}'", escaped));
            };
            args.extend_from_slice(ending_args);

            let sh = format!(r#"am {am_cmd} {}"#, args.join(" \\\n        "),);
            let mut stdout = StandardStream::stdout(ColorChoice::Auto);
            prompt(&mut stdout, &format!("adb shell \"{}\"", sh))?;
            let mut cmd = self.exe()?;
            cmd.arg("shell").arg(&sh);
            Ok(cmd)
        } else {
            unreachable!();
        }
    }

    fn ios_start(&self, app_protocol: StartAppProtocol, open: &AppOpenArgs) -> Result<Command> {
        if let Self::Ios {
            app_id, device_id, ..
        } = self
        {
            if open.legacy_open_mode {
                bail!("--legacy-open unsupported for iOS");
            }

            let mut args: Vec<String> = Vec::new();

            let (starting_args, ending_args) = open.args();

            if let Some(deeplink) = self.deeplink(open)? {
                let deeplink = deeplink::longform_deeplink_url(&deeplink, &app_protocol)?;
                if deeplink.len() >= 2047 {
                    anyhow::bail!("Deeplink is too long for xcrun simctl openurl. Use --pbcopy to copy the URL to the clipboard")
                }
                args.push("openurl".to_string());
                args.extend_from_slice(starting_args);
                args.extend([device_id.to_string(), deeplink]);
            } else {
                args.push("launch".to_string());
                args.extend_from_slice(starting_args);
                args.extend([device_id.to_string(), app_id.to_string()]);

                let StartAppProtocol {
                    log_state,
                    experiments,
                    reset_db,
                    jexl_expression,
                } = app_protocol;

                if log_state || experiments.is_some() || reset_db || jexl_expression.is_some() {
                    args.extend([
                        "--nimbus-cli".to_string(),
                        "--version".to_string(),
                        "1".to_string(),
                    ]);
                }

                if reset_db {
                    // We don't check launch here, because reset-db is never used
                    // without enroll.
                    args.push("--reset-db".to_string());
                }
                if let Some(s) = experiments {
                    args.extend([
                        "--experiments".to_string(),
                        s.to_string().replace('\'', "&apos;"),
                    ]);
                }
                if log_state {
                    args.push("--log-state".to_string());
                }
                if let Some(expr) = jexl_expression {
                    args.extend(["--eval-jexl".to_string(), expr.replace('\'', "&apos;")]);
                }
            }
            args.extend_from_slice(ending_args);

            let mut cmd = self.exe()?;
            cmd.args(args.clone());

            let sh = format!(r#"xcrun simctl {}"#, args.join(" \\\n        "),);
            let mut stdout = StandardStream::stdout(ColorChoice::Auto);
            prompt(&mut stdout, &sh)?;
            Ok(cmd)
        } else {
            unreachable!()
        }
    }
}

fn logcat_args<'a>() -> Vec<&'a str> {
    vec!["logcat", "-b", "main"]
}

impl NimbusApp {
    fn validate_experiment(
        &self,
        manifest_source: &ManifestSource,
        experiment: &ExperimentSource,
    ) -> Result<bool> {
        let mut stdout = StandardStream::stdout(ColorChoice::Auto);
        let value: Value = experiment.try_into()?;

        let manifest = match TryInto::<FeatureManifest>::try_into(manifest_source) {
            Ok(manifest) => {
                output_ok(
                    &mut stdout,
                    &format!("Loaded manifest from {manifest_source}"),
                )?;
                manifest
            }
            Err(err) => {
                output_err(
                    &mut stdout,
                    &format!("Problem with manifest from {manifest_source}"),
                    &err.to_string(),
                )?;
                bail!("Error when loading and validating the manifest");
            }
        };

        let mut is_valid = true;
        for b in try_find_branches_from_experiment(&value)? {
            let branch = b.get_str("slug")?;
            for f in try_find_features_from_branch(&b)? {
                let id = f.get_str("featureId")?;
                let value = f
                    .get("value")
                    .unwrap_or_else(|| panic!("Branch {branch} feature {id} has no value"));
                let res = manifest.validate_feature_config(id, value.clone());
                match res {
                    Ok(_) => output_ok(&mut stdout, &format!("{branch: <15} {id}"))?,
                    Err(err) => {
                        is_valid = false;
                        output_err(
                            &mut stdout,
                            &format!("{branch: <15} {id}"),
                            &err.to_string(),
                        )?
                    }
                }
            }
        }
        if !is_valid {
            bail!("At least one error detected");
        }
        Ok(true)
    }
}
