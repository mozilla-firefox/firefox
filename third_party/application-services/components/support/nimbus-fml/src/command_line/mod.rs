/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

mod cli;
pub(crate) mod commands;
mod workflows;

use crate::intermediate_representation::TargetLanguage;
use crate::util::loaders::LoaderConfig;
use anyhow::Result;
use clap::Parser;
use commands::{
    CliCmd, GenerateExperimenterManifestCmd, GenerateSingleFileManifestCmd, GenerateStructCmd,
    LintCmd, PrintChannelsCmd, ValidateCmd,
};

use std::{collections::BTreeMap, ffi::OsString, path::Path};

use self::commands::PrintInfoCmd;

const RELEASE_CHANNEL: &str = "release";

pub fn do_main<I, T>(args: I, cwd: &Path) -> Result<()>
where
    I: IntoIterator<Item = T>,
    T: Into<OsString> + Clone,
{
    let cmd = get_command_from_cli(args, cwd)?;
    process_command(&cmd)
}

fn process_command(cmd: &CliCmd) -> Result<()> {
    match cmd {
        CliCmd::Generate(params) => workflows::generate_struct(params)?,
        CliCmd::GenerateExperimenter(params) => workflows::generate_experimenter_manifest(params)?,
        CliCmd::GenerateSingleFileManifest(params) => {
            workflows::generate_single_file_manifest(params)?
        }
        CliCmd::FetchFile(files, nm) => workflows::fetch_file(files, nm)?,
        CliCmd::Validate(params) => workflows::validate(params)?,
        CliCmd::Lint(params) => workflows::lint(params)?,
        CliCmd::ListLints => workflows::list_lints()?,
        CliCmd::PrintChannels(params) => workflows::print_channels(params)?,
        CliCmd::PrintInfo(params) => workflows::print_info(params)?,
    };
    Ok(())
}

fn get_command_from_cli<I, T>(args: I, cwd: &Path) -> Result<CliCmd>
where
    I: IntoIterator<Item = T>,
    T: Into<OsString> + Clone,
{
    let app = cli::App::parse_from(args);

    Ok(match app.subcommand {
        cli::Command::Generate(cmd) => {
            CliCmd::Generate(create_generate_command_from_cli(&cmd, cwd)?)
        }
        cli::Command::GenerateExperimenter(cmd) => {
            CliCmd::GenerateExperimenter(create_generate_command_experimenter_from_cli(&cmd, cwd)?)
        }
        cli::Command::Fetch(cmd) => {
            CliCmd::FetchFile(create_loader(&cmd.input, &cmd.loader_info, cwd)?, cmd.input)
        }
        cli::Command::SingleFile(cmd) => {
            CliCmd::GenerateSingleFileManifest(create_single_file_from_cli(&cmd, cwd)?)
        }
        cli::Command::Validate(cmd) => {
            CliCmd::Validate(create_validate_command_from_cli(&cmd, cwd)?)
        }
        cli::Command::Lint(cmd) => create_lint_command_from_cli(&cmd, cwd)?,
        cli::Command::Channels(cmd) => {
            CliCmd::PrintChannels(create_print_channels_from_cli(&cmd, cwd)?)
        }
        cli::Command::Info(cmd) => CliCmd::PrintInfo(create_print_info_from_cli(&cmd, cwd)?),
    })
}

fn create_single_file_from_cli(
    cmd: &cli::SingleFile,
    cwd: &Path,
) -> Result<GenerateSingleFileManifestCmd> {
    let manifest = cmd.input.clone();
    let output = cwd.join(&cmd.output);
    let channel = cmd
        .channel
        .clone()
        .unwrap_or_else(|| RELEASE_CHANNEL.into());
    let loader = create_loader(&cmd.input, &cmd.loader_info, cwd)?;
    Ok(GenerateSingleFileManifestCmd {
        manifest,
        output,
        channel,
        loader,
    })
}

fn create_generate_command_experimenter_from_cli(
    cmd: &cli::GenerateExperimenter,
    cwd: &Path,
) -> Result<GenerateExperimenterManifestCmd> {
    let manifest = cmd.input.clone();
    let load_from_ir =
        TargetLanguage::ExperimenterJSON == TargetLanguage::from_extension(&manifest)?;
    let output = cwd.join(&cmd.output);
    let language = output.as_path().try_into()?;
    let loader = create_loader(&cmd.input, &cmd.loader_info, cwd)?;
    let cmd = GenerateExperimenterManifestCmd {
        manifest: cmd.input.clone(),
        output,
        language,
        load_from_ir,
        loader,
    };
    Ok(cmd)
}

fn create_generate_command_from_cli(cmd: &cli::Generate, cwd: &Path) -> Result<GenerateStructCmd> {
    let manifest = cmd.input.clone();
    let load_from_ir = matches!(
        TargetLanguage::from_extension(&manifest),
        Ok(TargetLanguage::ExperimenterJSON)
    );
    let output = cwd.join(&cmd.output);
    let language = match cmd.language {
        Some(s) => TargetLanguage::from(s),
        None => output.as_path().try_into().map_err(|_| anyhow::anyhow!("Can't infer a target language from the file or directory, so specify a --language flag explicitly"))?,
    };
    let channel = cmd.channel.clone();
    let loader = create_loader(&cmd.input, &cmd.loader_info, cwd)?;
    Ok(GenerateStructCmd {
        language,
        manifest,
        output,
        load_from_ir,
        channel,
        loader,
    })
}

fn create_loader(
    input_file: &str,
    loader_info: &cli::LoaderInfo,
    cwd: &Path,
) -> Result<LoaderConfig> {
    let cwd = cwd.to_path_buf();
    let cache_dir = loader_info
        .cache_dir
        .as_ref()
        .map(|f| Some(cwd.join(f)))
        .unwrap_or_default();

    let repo_files = loader_info
        .repo_file
        .iter()
        .map(|s| s.to_string())
        .collect();

    let mut refs: BTreeMap<_, _> = Default::default();
    match (LoaderConfig::repo_and_path(input_file), &loader_info.ref_) {
        (Some((repo, _)), Some(ref_)) => refs.insert(repo, ref_.clone()),
        _ => None,
    };

    Ok(LoaderConfig {
        cache_dir,
        repo_files,
        cwd,
        refs,
        lax_gecko_pref_validation: loader_info.lax_gecko_pref_validation,
    })
}

fn create_validate_command_from_cli(cmd: &cli::Validate, cwd: &Path) -> Result<ValidateCmd> {
    let manifest = cmd.input.clone();
    let loader = create_loader(&cmd.input, &cmd.loader_info, cwd)?;
    Ok(ValidateCmd { manifest, loader })
}

fn create_lint_command_from_cli(cmd: &cli::Lint, cwd: &Path) -> Result<CliCmd> {
    if cmd.list {
        return Ok(CliCmd::ListLints);
    }

    // clap has already checked that there is an input file if we're not listing.
    let manifest = cmd.input.clone().unwrap_or_default();
    let loader = create_loader(&manifest, &cmd.loader_info, cwd)?;

    Ok(CliCmd::Lint(LintCmd {
        manifest,
        loader,
        allow: cmd.allow.clone(),
        deny: cmd.deny.clone(),
        error_on_warning: cmd.error_on_warning,
        include_imports: cmd.include_imports,
        as_json: cmd.json,
    }))
}

fn create_print_channels_from_cli(cmd: &cli::Channels, cwd: &Path) -> Result<PrintChannelsCmd> {
    let manifest = cmd.input.clone();
    let loader = create_loader(&cmd.input, &cmd.loader_info, cwd)?;
    let as_json = cmd.json;
    Ok(PrintChannelsCmd {
        manifest,
        loader,
        as_json,
    })
}

fn create_print_info_from_cli(cmd: &cli::Info, cwd: &Path) -> Result<PrintInfoCmd> {
    let manifest = cmd.input.clone();
    let loader = create_loader(&cmd.input, &cmd.loader_info, cwd)?;
    let as_json = cmd.json;

    let channel = cmd.channel.clone();
    let feature = cmd.feature.clone();

    Ok(PrintInfoCmd {
        manifest,
        channel,
        loader,
        as_json,
        feature,
    })
}

#[cfg(test)]
mod cli_tests {
    use std::{env, path::PathBuf};

    use super::*;

    const FML_BIN: &str = "nimbus-fml";
    const TEST_FILE: &str = "fixtures/fe/importing/simple/app.yaml";
    const TEST_DIR: &str = "fixtures/fe/importing/including-imports";
    const GENERATED_DIR: &str = "build/cli-test";
    const CACHE_DIR: &str = "./build/cache";
    const REPO_FILE_1: &str = "./repos.versions.json";
    const REPO_FILE_2: &str = "./repos.local.json";

    fn package_dir() -> Result<PathBuf> {
        let string = env::var("CARGO_MANIFEST_DIR")?;
        Ok(PathBuf::from(string))
    }

    // All these tests just exercise the command line parsing.
    // Each of the tests construct a command struct from the command line, and then
    // test the command struct against the expected values.

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_generate_android_features_language_implied() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli(
            [
                FML_BIN,
                "generate",
                "--channel",
                "channel-test",
                TEST_FILE,
                "./Implied.kt",
            ],
            &cwd,
        )?;

        assert!(matches!(cmd, CliCmd::Generate(_)));

        if let CliCmd::Generate(cmd) = cmd {
            assert_eq!(cmd.channel, "channel-test");
            assert_eq!(cmd.language, TargetLanguage::Kotlin);
            assert!(!cmd.load_from_ir);
            assert!(cmd.output.ends_with("Implied.kt"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    #[test]
    fn test_cli_generate_ios_features_language_implied() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli(
            [
                FML_BIN,
                "generate",
                "--channel",
                "channel-test",
                TEST_FILE,
                "./Implied.swift",
            ],
            &cwd,
        )?;

        assert!(matches!(cmd, CliCmd::Generate(_)));

        if let CliCmd::Generate(cmd) = cmd {
            assert_eq!(cmd.channel, "channel-test");
            assert_eq!(cmd.language, TargetLanguage::Swift);
            assert!(!cmd.load_from_ir);
            assert!(cmd.output.ends_with("Implied.swift"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////

    #[test]
    fn test_cli_generate_features_with_remote_flags() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli(
            [
                FML_BIN,
                "generate",
                "--repo-file",
                REPO_FILE_1,
                "--repo-file",
                REPO_FILE_2,
                "--cache-dir",
                CACHE_DIR,
                TEST_FILE,
                "./Implied.swift",
                "--channel",
                "channel-test",
            ],
            &cwd,
        )?;

        assert!(matches!(cmd, CliCmd::Generate(_)));

        if let CliCmd::Generate(cmd) = cmd {
            assert_eq!(cmd.channel, "channel-test");
            assert_eq!(cmd.language, TargetLanguage::Swift);
            assert!(!cmd.load_from_ir);
            assert!(cmd.output.ends_with("Implied.swift"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_generate_android_features_language_flag() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli(
            [
                FML_BIN,
                "generate",
                "--channel",
                "channel-test",
                "--language",
                "kotlin",
                TEST_FILE,
                "./build/generated",
            ],
            &cwd,
        )?;

        assert!(matches!(cmd, CliCmd::Generate(_)));

        if let CliCmd::Generate(cmd) = cmd {
            assert_eq!(cmd.channel, "channel-test");
            assert_eq!(cmd.language, TargetLanguage::Kotlin);
            assert!(!cmd.load_from_ir);
            assert!(cmd.output.ends_with("build/generated"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    #[test]
    fn test_cli_generate_ios_features_language_flag() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli(
            [
                FML_BIN,
                "generate",
                "--channel",
                "channel-test",
                "--language",
                "swift",
                TEST_FILE,
                "./build/generated",
            ],
            &cwd,
        )?;

        assert!(matches!(cmd, CliCmd::Generate(_)));

        if let CliCmd::Generate(cmd) = cmd {
            assert_eq!(cmd.channel, "channel-test");
            assert_eq!(cmd.language, TargetLanguage::Swift);
            assert!(!cmd.load_from_ir);
            assert!(cmd.output.ends_with("build/generated"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_generate_experimenter_android() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli(
            [
                FML_BIN,
                "generate-experimenter",
                TEST_FILE,
                ".experimenter.yaml",
            ],
            &cwd,
        )?;

        assert!(matches!(cmd, CliCmd::GenerateExperimenter(_)));

        if let CliCmd::GenerateExperimenter(cmd) = cmd {
            assert_eq!(cmd.language, TargetLanguage::ExperimenterYAML);
            assert!(!cmd.load_from_ir);
            assert!(cmd.output.ends_with(".experimenter.yaml"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    #[test]
    fn test_cli_generate_experimenter_ios() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli(
            [
                FML_BIN,
                "generate-experimenter",
                "--channel",
                "test-channel",
                TEST_FILE,
                ".experimenter.yaml",
            ],
            &cwd,
        )?;

        assert!(matches!(cmd, CliCmd::GenerateExperimenter(_)));

        if let CliCmd::GenerateExperimenter(cmd) = cmd {
            assert_eq!(cmd.language, TargetLanguage::ExperimenterYAML);
            assert!(!cmd.load_from_ir);
            assert!(cmd.output.ends_with(".experimenter.yaml"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    #[test]
    fn test_cli_generate_experimenter_with_json() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli(
            [
                FML_BIN,
                "generate-experimenter",
                TEST_FILE,
                ".experimenter.json",
            ],
            &cwd,
        )?;

        assert!(matches!(cmd, CliCmd::GenerateExperimenter(_)));

        if let CliCmd::GenerateExperimenter(cmd) = cmd {
            assert_eq!(cmd.language, TargetLanguage::ExperimenterJSON);
            assert!(!cmd.load_from_ir);
            assert!(cmd.output.ends_with(".experimenter.json"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    #[test]
    fn test_cli_generate_experimenter_with_remote_flags() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli(
            [
                FML_BIN,
                "generate-experimenter",
                "--repo-file",
                REPO_FILE_1,
                "--repo-file",
                REPO_FILE_2,
                "--cache-dir",
                CACHE_DIR,
                TEST_FILE,
                ".experimenter.json",
            ],
            &cwd,
        )?;

        assert!(matches!(cmd, CliCmd::GenerateExperimenter(_)));

        if let CliCmd::GenerateExperimenter(cmd) = cmd {
            assert_eq!(cmd.language, TargetLanguage::ExperimenterJSON);
            assert!(!cmd.load_from_ir);
            assert!(cmd.output.ends_with(".experimenter.json"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    #[test]
    fn test_cli_generate_features_for_directory_input() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli(
            [
                FML_BIN,
                "generate",
                "--language",
                "swift",
                "--channel",
                "release",
                TEST_DIR,
                GENERATED_DIR,
            ],
            &cwd,
        )?;

        assert!(matches!(cmd, CliCmd::Generate(_)));

        if let CliCmd::Generate(cmd) = cmd {
            assert_eq!(cmd.channel, "release");
            assert_eq!(cmd.language, TargetLanguage::Swift);
            assert!(!cmd.load_from_ir);
            assert!(cmd.output.ends_with("build/cli-test"));
            assert_eq!(&cmd.manifest, TEST_DIR);
        }
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_generate_validate() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli([FML_BIN, "validate", TEST_FILE], &cwd)?;

        assert!(matches!(cmd, CliCmd::Validate(_)));
        assert!(matches!(cmd, CliCmd::Validate(c) if c.manifest.ends_with(TEST_FILE)));
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_print_channels_command() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli([FML_BIN, "channels", TEST_FILE], &cwd)?;

        assert!(matches!(&cmd, CliCmd::PrintChannels(_)));
        assert!(matches!(&cmd, CliCmd::PrintChannels(c) if c.manifest.ends_with(TEST_FILE)));
        assert!(matches!(&cmd, CliCmd::PrintChannels(c) if !c.as_json));

        let cmd = get_command_from_cli([FML_BIN, "channels", TEST_FILE, "--json"], &cwd)?;

        assert!(matches!(&cmd, CliCmd::PrintChannels(_)));
        assert!(matches!(&cmd, CliCmd::PrintChannels(c) if c.manifest.ends_with(TEST_FILE)));
        assert!(matches!(&cmd, CliCmd::PrintChannels(c) if c.as_json));
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_print_info_command() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli([FML_BIN, "info", TEST_FILE, "--channel", "release"], &cwd)?;

        assert!(matches!(&cmd, CliCmd::PrintInfo(_)));
        assert!(matches!(&cmd, CliCmd::PrintInfo(c) if c.manifest.ends_with(TEST_FILE)));
        assert!(
            matches!(&cmd, CliCmd::PrintInfo(PrintInfoCmd { channel: Some(channel), as_json, .. }) if channel.as_str() == "release" && !as_json )
        );

        let cmd = get_command_from_cli(
            [FML_BIN, "info", TEST_FILE, "--channel", "beta", "--json"],
            &cwd,
        )?;

        assert!(matches!(&cmd, CliCmd::PrintInfo(_)));
        assert!(matches!(&cmd, CliCmd::PrintInfo(c) if c.manifest.ends_with(TEST_FILE)));
        assert!(
            matches!(&cmd, CliCmd::PrintInfo(PrintInfoCmd { channel: Some(channel), as_json, .. }) if channel.as_str() == "beta" && *as_json )
        );

        let cmd = get_command_from_cli(
            [
                FML_BIN,
                "info",
                TEST_FILE,
                "--feature",
                "my-feature",
                "--json",
            ],
            &cwd,
        )?;

        assert!(matches!(&cmd, CliCmd::PrintInfo(_)));
        assert!(matches!(&cmd, CliCmd::PrintInfo(c) if c.manifest.ends_with(TEST_FILE)));
        assert!(
            matches!(&cmd, CliCmd::PrintInfo(PrintInfoCmd { feature: Some(feature), as_json, .. }) if feature.as_str() == "my-feature" && *as_json )
        );
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_add_ref_arg() -> Result<()> {
        let cwd = package_dir()?;
        let cmd = get_command_from_cli(
            [
                FML_BIN,
                "generate-experimenter",
                "--ref",
                "my-tag",
                "@foo/bar/baz.fml.yaml",
                "./baz.yaml",
            ],
            &cwd,
        )?;

        assert!(matches!(cmd, CliCmd::GenerateExperimenter(_)));
        assert!(
            matches!(cmd, CliCmd::GenerateExperimenter(c) if c.loader.refs["@foo/bar"] == "my-tag")
        );
        Ok(())
    }
}
