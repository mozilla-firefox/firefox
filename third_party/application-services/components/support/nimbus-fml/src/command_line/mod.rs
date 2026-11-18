// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

mod cli;
pub(crate) mod commands;
mod workflows;

use anyhow::{Result, anyhow};
use clap::Parser;

use crate::command_line::cli::{
    App, ChannelsArgs, GenerateArgs, GenerateExperimenterArgs, InfoArgs, LintArgs, SingleFileArgs,
    ValidateArgs,
};
pub use crate::command_line::cli::{Command, LoaderArgs};
pub use crate::command_line::commands::CliCmd;
use crate::command_line::commands::{
    GenerateExperimenterManifestCmd, GenerateSingleFileManifestCmd, GenerateStructCmd, LintCmd,
    PrintChannelsCmd, PrintInfoCmd, ValidateCmd,
};
use crate::intermediate_representation::TargetLanguage;
use crate::util::loaders::LoaderConfig;

const RELEASE_CHANNEL: &str = "release";

pub fn do_main() -> Result<()> {
    CliCmd::dispatch_from_cli()
}

impl CliCmd {
    pub(crate) fn dispatch_from_cli() -> Result<()> {
        let app = App::parse();
        Self::from_args(app.subcommand)?.dispatch()
    }

    pub fn from_args(subcommand: Command) -> Result<Self> {
        Ok(match subcommand {
            cli::Command::Generate(args) => {
                CliCmd::Generate(create_generate_command_from_cli(args)?)
            }
            cli::Command::GenerateExperimenter(args) => {
                CliCmd::GenerateExperimenter(create_generate_command_experimenter_from_cli(args)?)
            }
            cli::Command::Fetch(args) => CliCmd::FetchFile(
                LoaderConfig::from_args(args.loader_args, &args.manifest)?,
                args.manifest,
            ),
            cli::Command::SingleFile(args) => {
                CliCmd::GenerateSingleFileManifest(create_single_file_from_cli(args)?)
            }
            cli::Command::Validate(args) => {
                CliCmd::Validate(create_validate_command_from_cli(args)?)
            }
            cli::Command::Lint(args) => create_lint_command_from_cli(args)?,
            cli::Command::Channels(args) => {
                CliCmd::PrintChannels(create_print_channels_from_cli(args)?)
            }
            cli::Command::Info(args) => CliCmd::PrintInfo(create_print_info_from_cli(args)?),
        })
    }

    pub fn dispatch(&self) -> Result<()> {
        match self {
            Self::Generate(params) => workflows::generate_struct(params),
            Self::GenerateExperimenter(params) => workflows::generate_experimenter_manifest(params),
            Self::GenerateSingleFileManifest(params) => {
                workflows::generate_single_file_manifest(params)
            }
            Self::FetchFile(files, nm) => workflows::fetch_file(files, nm),
            Self::Validate(params) => workflows::validate(params),
            Self::Lint(params) => workflows::lint(params),
            Self::ListLints => workflows::list_lints(),
            Self::PrintChannels(params) => workflows::print_channels(params),
            Self::PrintInfo(params) => workflows::print_info(params),
        }
        .map_err(Into::into)
    }
}

fn create_single_file_from_cli(args: SingleFileArgs) -> Result<GenerateSingleFileManifestCmd> {
    let channel = args
        .channel
        .clone()
        .unwrap_or_else(|| RELEASE_CHANNEL.into());
    let loader = LoaderConfig::from_args(args.loader_args, &args.manifest)?;
    Ok(GenerateSingleFileManifestCmd {
        manifest: args.manifest,
        output: args.output,
        channel,
        loader,
    })
}

fn create_generate_command_experimenter_from_cli(
    args: GenerateExperimenterArgs,
) -> Result<GenerateExperimenterManifestCmd> {
    let loader = LoaderConfig::from_args(args.loader_args, &args.manifest)?;
    let cmd = GenerateExperimenterManifestCmd {
        manifest: args.manifest.clone(),
        output: args.output,
        loader,
    };
    Ok(cmd)
}

fn create_generate_command_from_cli(args: GenerateArgs) -> Result<GenerateStructCmd> {
    let language = match args.language {
        Some(s) => TargetLanguage::from(s),
        None => args.output.as_path().try_into().map_err(|_| {
            anyhow!("Can't infer a target language from the file or directory, so specify a --language flag explicitly")
        })?,
    };
    let channel = args.channel.clone();
    let loader = LoaderConfig::from_args(args.loader_args, &args.manifest)?;
    Ok(GenerateStructCmd {
        language,
        manifest: args.manifest,
        output: args.output,
        channel,
        loader,
    })
}

fn create_validate_command_from_cli(args: ValidateArgs) -> Result<ValidateCmd> {
    let loader = LoaderConfig::from_args(args.loader_args, &args.manifest)?;
    Ok(ValidateCmd {
        manifest: args.manifest,
        loader,
    })
}

fn create_lint_command_from_cli(args: LintArgs) -> Result<CliCmd> {
    if args.list {
        return Ok(CliCmd::ListLints);
    }

    // clap has already checked that there is an input file if we're not listing.
    let manifest = args.manifest.expect("required argument");
    let loader = LoaderConfig::from_args(args.loader_args, &manifest)?;

    Ok(CliCmd::Lint(LintCmd {
        manifest,
        loader,
        code_review_json: args.code_review_json.clone(),
    }))
}

fn create_print_channels_from_cli(args: ChannelsArgs) -> Result<PrintChannelsCmd> {
    let loader = LoaderConfig::from_args(args.loader_args, &args.manifest)?;
    Ok(PrintChannelsCmd {
        manifest: args.manifest,
        loader,
        as_json: args.json,
    })
}

fn create_print_info_from_cli(args: InfoArgs) -> Result<PrintInfoCmd> {
    let loader = LoaderConfig::from_args(args.loader_args, &args.manifest)?;

    Ok(PrintInfoCmd {
        manifest: args.manifest,
        channel: args.channel,
        loader,
        as_json: args.json,
        feature: args.feature,
    })
}

#[cfg(test)]
mod cli_tests {
    use std::env;
    use std::path::PathBuf;

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

    struct WithCwd {
        original_cwd: PathBuf,
    }

    impl Drop for WithCwd {
        fn drop(&mut self) {
            std::env::set_current_dir(&self.original_cwd).expect("Could not change directory");
        }
    }

    fn run_in_package_dir() -> Result<WithCwd> {
        let original_cwd = std::env::current_dir()?;
        std::env::set_current_dir(package_dir()?)?;

        Ok(WithCwd { original_cwd })
    }

    fn get_command_from_cli(argv: &[&'static str]) -> Result<CliCmd> {
        let app = App::parse_from(argv);
        CliCmd::from_args(app.subcommand)
    }

    // All these tests just exercise the command line parsing.
    // Each of the tests construct a command struct from the command line, and then
    // test the command struct against the expected values.

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_generate_android_features_language_implied() -> Result<()> {
        let _guard = run_in_package_dir()?;
        let cmd = get_command_from_cli(&[
            FML_BIN,
            "generate",
            "--channel",
            "channel-test",
            TEST_FILE,
            "./Implied.kt",
        ])?;

        assert!(matches!(cmd, CliCmd::Generate(_)));

        if let CliCmd::Generate(cmd) = cmd {
            assert_eq!(cmd.channel, "channel-test");
            assert_eq!(cmd.language, TargetLanguage::Kotlin);
            assert!(cmd.output.ends_with("Implied.kt"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    #[test]
    fn test_cli_generate_ios_features_language_implied() -> Result<()> {
        let _guard = run_in_package_dir()?;
        let cmd = get_command_from_cli(&[
            FML_BIN,
            "generate",
            "--channel",
            "channel-test",
            TEST_FILE,
            "./Implied.swift",
        ])?;

        assert!(matches!(cmd, CliCmd::Generate(_)));

        if let CliCmd::Generate(cmd) = cmd {
            assert_eq!(cmd.channel, "channel-test");
            assert_eq!(cmd.language, TargetLanguage::Swift);
            assert!(cmd.output.ends_with("Implied.swift"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////

    #[test]
    fn test_cli_generate_features_with_remote_flags() -> Result<()> {
        let _guard = run_in_package_dir()?;
        let cmd = get_command_from_cli(&[
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
        ])?;

        assert!(matches!(cmd, CliCmd::Generate(_)));

        if let CliCmd::Generate(cmd) = cmd {
            assert_eq!(cmd.channel, "channel-test");
            assert_eq!(cmd.language, TargetLanguage::Swift);
            assert!(cmd.output.ends_with("Implied.swift"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_generate_android_features_language_flag() -> Result<()> {
        let _guard = run_in_package_dir()?;
        let cmd = get_command_from_cli(&[
            FML_BIN,
            "generate",
            "--channel",
            "channel-test",
            "--language",
            "kotlin",
            TEST_FILE,
            "./build/generated",
        ])?;

        assert!(matches!(cmd, CliCmd::Generate(_)));

        if let CliCmd::Generate(cmd) = cmd {
            assert_eq!(cmd.channel, "channel-test");
            assert_eq!(cmd.language, TargetLanguage::Kotlin);
            assert!(cmd.output.ends_with("build/generated"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    #[test]
    fn test_cli_generate_ios_features_language_flag() -> Result<()> {
        let _guard = run_in_package_dir()?;
        let cmd = get_command_from_cli(&[
            FML_BIN,
            "generate",
            "--channel",
            "channel-test",
            "--language",
            "swift",
            TEST_FILE,
            "./build/generated",
        ])?;

        assert!(matches!(cmd, CliCmd::Generate(_)));

        if let CliCmd::Generate(cmd) = cmd {
            assert_eq!(cmd.channel, "channel-test");
            assert_eq!(cmd.language, TargetLanguage::Swift);
            assert!(cmd.output.ends_with("build/generated"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_generate_experimenter_android() -> Result<()> {
        let _guard = run_in_package_dir()?;
        let cmd = get_command_from_cli(&[
            FML_BIN,
            "generate-experimenter",
            TEST_FILE,
            ".experimenter.yaml",
        ])?;

        assert!(matches!(cmd, CliCmd::GenerateExperimenter(_)));

        if let CliCmd::GenerateExperimenter(cmd) = cmd {
            assert!(cmd.output.ends_with(".experimenter.yaml"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    #[test]
    fn test_cli_generate_experimenter_ios() -> Result<()> {
        let _guard = run_in_package_dir()?;
        let cmd = get_command_from_cli(&[
            FML_BIN,
            "generate-experimenter",
            "--channel",
            "test-channel",
            TEST_FILE,
            ".experimenter.yaml",
        ])?;

        assert!(matches!(cmd, CliCmd::GenerateExperimenter(_)));

        if let CliCmd::GenerateExperimenter(cmd) = cmd {
            assert!(cmd.output.ends_with(".experimenter.yaml"));
            assert!(cmd.manifest.ends_with(TEST_FILE));
        }
        Ok(())
    }

    #[test]
    fn test_cli_generate_features_for_directory_input() -> Result<()> {
        let _guard = run_in_package_dir()?;
        let cmd = get_command_from_cli(&[
            FML_BIN,
            "generate",
            "--language",
            "swift",
            "--channel",
            "release",
            TEST_DIR,
            GENERATED_DIR,
        ])?;

        assert!(matches!(cmd, CliCmd::Generate(_)));

        if let CliCmd::Generate(cmd) = cmd {
            assert_eq!(cmd.channel, "release");
            assert_eq!(cmd.language, TargetLanguage::Swift);
            assert!(cmd.output.ends_with("build/cli-test"));
            assert_eq!(&cmd.manifest, TEST_DIR);
        }
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_generate_validate() -> Result<()> {
        let _guard = run_in_package_dir()?;
        let cmd = get_command_from_cli(&[FML_BIN, "validate", TEST_FILE])?;

        assert!(matches!(cmd, CliCmd::Validate(_)));
        assert!(matches!(cmd, CliCmd::Validate(c) if c.manifest.ends_with(TEST_FILE)));
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_print_channels_command() -> Result<()> {
        let _guard = run_in_package_dir()?;
        let cmd = get_command_from_cli(&[FML_BIN, "channels", TEST_FILE])?;

        assert!(matches!(&cmd, CliCmd::PrintChannels(_)));
        assert!(matches!(&cmd, CliCmd::PrintChannels(c) if c.manifest.ends_with(TEST_FILE)));
        assert!(matches!(&cmd, CliCmd::PrintChannels(c) if !c.as_json));

        let cmd = get_command_from_cli(&[FML_BIN, "channels", TEST_FILE, "--json"])?;

        assert!(matches!(&cmd, CliCmd::PrintChannels(_)));
        assert!(matches!(&cmd, CliCmd::PrintChannels(c) if c.manifest.ends_with(TEST_FILE)));
        assert!(matches!(&cmd, CliCmd::PrintChannels(c) if c.as_json));
        Ok(())
    }

    ///////////////////////////////////////////////////////////////////////////
    #[test]
    fn test_cli_print_info_command() -> Result<()> {
        let _guard = run_in_package_dir()?;
        let cmd = get_command_from_cli(&[FML_BIN, "info", TEST_FILE, "--channel", "release"])?;

        assert!(matches!(&cmd, CliCmd::PrintInfo(_)));
        assert!(matches!(&cmd, CliCmd::PrintInfo(c) if c.manifest.ends_with(TEST_FILE)));
        assert!(
            matches!(&cmd, CliCmd::PrintInfo(PrintInfoCmd { channel: Some(channel), as_json, .. }) if channel.as_str() == "release" && !as_json )
        );

        let cmd =
            get_command_from_cli(&[FML_BIN, "info", TEST_FILE, "--channel", "beta", "--json"])?;

        assert!(matches!(&cmd, CliCmd::PrintInfo(_)));
        assert!(matches!(&cmd, CliCmd::PrintInfo(c) if c.manifest.ends_with(TEST_FILE)));
        assert!(
            matches!(&cmd, CliCmd::PrintInfo(PrintInfoCmd { channel: Some(channel), as_json, .. }) if channel.as_str() == "beta" && *as_json )
        );

        let cmd = get_command_from_cli(&[
            FML_BIN,
            "info",
            TEST_FILE,
            "--feature",
            "my-feature",
            "--json",
        ])?;

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
        let _guard = run_in_package_dir()?;
        let cmd = get_command_from_cli(&[
            FML_BIN,
            "generate-experimenter",
            "--ref",
            "my-tag",
            "@foo/bar/baz.fml.yaml",
            "./baz.yaml",
        ])?;

        assert!(matches!(cmd, CliCmd::GenerateExperimenter(_)));
        assert!(
            matches!(cmd, CliCmd::GenerateExperimenter(c) if c.loader.refs["@foo/bar"] == "my-tag")
        );
        Ok(())
    }
}
